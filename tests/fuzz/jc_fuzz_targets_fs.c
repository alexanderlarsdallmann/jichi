/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fuzz_targets_fs.c - the path-fence property target (Tier 2).
 *
 * Split out of jc_fuzz_targets.c because this target needs a real filesystem
 * (mkdtemp/symlink) and therefore the same _XOPEN_SOURCE request src/util/
 * jc_path.c makes -- the build-wide -D_POSIX_C_SOURCE=200112L does not declare
 * realpath/mkdtemp/symlink.
 *
 * What is fuzzed, and why this layer: docs/proposals/2026-07-fuzzing-suite.md
 * names `jc_app_path_under_root`, which does not exist. The real write-side
 * chokepoint is `jc_app_path_denied_ex` (src/chat/jc_app.c), and it is exactly
 * the composition `jc_path_resolve` -> `jc_path_under_root`, fail-closed when
 * resolve errors -- i.e. `jc_path_in_root` (src/util/jc_path.c). So this target
 * fuzzes that composition against a real temp root, no jc_app needed (mode
 * gating and reference roots are separately covered by tests/test_app.c).
 *
 * The proposal calls this a security property: for any generated path, an
 * approval must never denote a file outside the root.
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "jc_fuzz.h"

#include "jc_path.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* jc_snprintf returns C99 snprintf semantics (the length that WOULD be
 * written), so a fit check -- not a jc_status compare -- is the correct test. */
static int fits(int n, size_t cap)
{
    return n >= 0 && (size_t)n < cap;
}

/* base/root  <- the fence root
 * base/outside/secret  <- must never be reachable through an approval
 * base/root/sub/f      <- an ordinary contained file
 * base/root/esc        -> ../outside   (the interesting symlink)
 * base/root/self       -> .
 */
static char g_root[JC_PATH_MAX];
static char g_outside[JC_PATH_MAX];
static int  g_ready;       /* 1 = usable, -1 = setup failed (skip quietly) */

static void fence_setup(void)
{
    char base[JC_PATH_MAX];
    char buf[JC_PATH_MAX];
    FILE *f;

    g_ready = -1;
    /* A pid-derived directory rather than mkdtemp(): mkdtemp is POSIX-2008 and
     * the build-wide -D_POSIX_C_SOURCE=200112L does not declare it. The name
     * only has to be unique per process, not unguessable. */
    if (!fits(jc_snprintf(base, sizeof(base), "/tmp/jc_fuzz_fence.%lu",
                          (unsigned long)getpid()), sizeof(base))) return;
    if (jc_mkdir_p(base) != JC_OK) return;

    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root/sub", base), sizeof(buf)))
        return;
    if (jc_mkdir_p(buf) != JC_OK) return;
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/outside", base), sizeof(buf)))
        return;
    if (jc_mkdir_p(buf) != JC_OK) return;

    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root/sub/f", base), sizeof(buf)))
        return;
    f = fopen(buf, "w");
    if (f == NULL) return;
    fputs("contained\n", f);
    fclose(f);

    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/outside/secret", base),
              sizeof(buf))) return;
    f = fopen(buf, "w");
    if (f == NULL) return;
    fputs("escaped\n", f);
    fclose(f);

    /* unlink first so a re-run in the same pid-named dir is idempotent. */
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root/esc", base), sizeof(buf)))
        return;
    (void)unlink(buf);
    if (symlink("../outside", buf) != 0) return;
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root/self", base), sizeof(buf)))
        return;
    (void)unlink(buf);
    if (symlink(".", buf) != 0) return;
    /* M607: a DANGLING trap -- a link whose target does not exist. realpath()
     * fails on it, so the resolver's parent branch used to approve the leaf
     * verbatim; the corpus planted only existing targets and could not see it. */
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root/dangle", base), sizeof(buf)))
        return;
    (void)unlink(buf);
    if (symlink("../outside/not_yet", buf) != 0) return;

    /* Canonicalize both, exactly as jc_app does with app->root at startup. */
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/root", base), sizeof(buf)))
        return;
    if (jc_path_resolve(buf, g_root, sizeof(g_root)) != JC_OK) return;
    if (!fits(jc_snprintf(buf, sizeof(buf), "%s/outside", base), sizeof(buf)))
        return;
    if (jc_path_resolve(buf, g_outside, sizeof(g_outside)) != JC_OK) return;

    g_ready = 1;
    /* The temp tree is left for the OS to reap: this is a test binary that a
     * sanitizer may abort at any point, so an atexit unlink would be unreliable
     * and could mask the very crash we are hunting. */
}

static int has_dotdot_segment(const char *s)
{
    const char *p;
    for (p = s; *p != '\0'; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p == s || p[-1] == '/') &&
            (p[2] == '\0' || p[2] == '/')) {
            return 1;
        }
    }
    return 0;
}

void jc_fuzz_pathfence(const unsigned char *data, size_t len)
{
    char *s;
    char candidate[JC_PATH_MAX];
    char resolved[JC_PATH_MAX];
    char normalized[JC_PATH_MAX];

    if (g_ready == 0) fence_setup();
    if (g_ready != 1) return;

    s = jc_fuzz_dup0(data, len);
    if (s == NULL) return;

    /* P1 -- the pure boundary predicate, probed where it can actually fail:
     * the fuzz bytes are glued DIRECTLY onto the root with no separator, which
     * is the "/work" vs "/workother" regression class. An approval then implies
     * the suffix is empty or itself starts with '/'. (Feeding the raw fuzz
     * string here instead would never reach the interesting branch -- mutating
     * a relative seed does not synthesize the absolute root prefix, which is
     * how a deliberately broken boundary check first survived this target.) */
    if (fits(jc_snprintf(candidate, sizeof(candidate), "%s%s", g_root, s),
             sizeof(candidate)) &&
        jc_path_under_root(g_root, candidate)) {
        if (s[0] != '\0' && s[0] != '/') {
            fprintf(stderr, "FUZZ: under_root approved a sibling path: "
                            "root=%s path=%s\n", g_root, candidate);
            abort();
        }
    }

    /* P1b -- an arbitrary approved string is the root or root + '/' + suffix. */
    if (jc_path_under_root(g_root, s)) {
        size_t rlen = strlen(g_root);
        if (strncmp(s, g_root, rlen) != 0 ||
            (s[rlen] != '\0' && s[rlen] != '/')) {
            fprintf(stderr, "FUZZ: under_root approved a non-contained path: "
                            "root=%s path=%s\n", g_root, s);
            abort();
        }
    }

    /* P2 -- normalize refuses ".." outright, and a RELATIVE input it accepts
     * stays under the cwd it was resolved against. An absolute input is not a
     * containment violation: normalize's contract is lexical normalization, and
     * containment is the fence's separate job (P3). Asserting otherwise fails
     * on the input "/" -- which is how this target's own first red run went. */
    if (jc_path_normalize(g_root, s, normalized, sizeof(normalized)) == JC_OK) {
        if (has_dotdot_segment(s)) {
            fprintf(stderr, "FUZZ: normalize accepted a '..' path: %s\n", s);
            abort();
        }
        if (s[0] != '/' && !jc_path_under_root(g_root, normalized)) {
            fprintf(stderr, "FUZZ: normalize left the root: %s -> %s\n",
                    s, normalized);
            abort();
        }
    }

    /* P3 -- the fence property, through the filesystem and its symlinks:
     * treat the input as a workspace-relative write target, resolve it the way
     * jc_app_path_denied_ex does, and assert an approval never denotes a file
     * outside the root. `esc` (-> ../outside) and `dangle` (-> ../outside/not_yet,
     * a target that does not exist; M607) are the traps. */
    if (s[0] != '\0' &&
        fits(jc_snprintf(candidate, sizeof(candidate), "%s/%s", g_root, s),
             sizeof(candidate))) {
        if (jc_path_resolve(candidate, resolved, sizeof(resolved)) == JC_OK &&
            jc_path_under_root(g_root, resolved)) {
            if (jc_path_under_root(g_outside, resolved)) {
                fprintf(stderr, "FUZZ: fence escape -- %s resolved to %s "
                                "(outside=%s)\n", candidate, resolved,
                        g_outside);
                abort();
            }
            if (has_dotdot_segment(resolved)) {
                fprintf(stderr, "FUZZ: approved an unresolved '..' path: %s\n",
                        resolved);
                abort();
            }
        }
    }

    free(s);
}
