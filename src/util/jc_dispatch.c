/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_dispatch.c - the dirty-tree dispatch probe (M1006; M1006-a refinements).
 *
 * ONE function, because the two check sites need identical behaviour: a copy
 * in each tool file is the drift the M852 "say it once" principle exists to
 * prevent.
 *
 * M1006-a: the probe runs git with an ARGV array, not a shell string. The old
 * `git -C '<root>' status --porcelain` was jc_snprintf'd into a shell command
 * and popped via jc_proc_popen; a workspace path containing a single quote
 * broke the quoting (sh: unterminated quoted string, exit 2) and the probe
 * read the failure as not-dirty -- so the guard silently did not fire on such
 * a tree. The argv form passes the path to git as a single argument, intact.
 *
 * The probe now answers THREE states (see jc_dispatch.h): CLEAN (git ran,
 * exit 0, no porcelain), DIRTY (git ran, exit 0, porcelain reported paths),
 * and UNKNOWN (git did not run successfully in a workspace that IS a git
 * repository). The exit-status distinction is the whole point: a failed
 * `git status` must read unknown, never clean (M519/M628 -- the classifier's
 * else-branch says unknown). The one carve-out: a workspace that is NOT a git
 * repository reads not-dirty (the relay_git_facts "a missing repo omits the
 * line rather than asserting a falsehood" rule), and that distinction is made
 * with `git rev-parse --is-inside-work-tree` (exit 0 in a git work tree, 128
 * outside one -- proven by hand against a corrupt .git/index, which fails
 * status but not rev-parse). */
#include "jc_dispatch.h"

#include "jc_proc.h"      /* jc_proc_capture: argv spawn + bounded capture    */
#include "jc_str.h"       /* struct jc_sb, jc_sb_init/free                    */
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>

/* Bounded per-path copy buffer for the named-paths display (a workspace
 * path is rarely this long; truncation is fine -- the count is still exact). */
#define JD_PATH_BUF 512
/* Bound on captured git output. A dirty workspace's porcelain is at most a
 * handful of lines per file; 64 KiB covers any realistic dispatch base. */
#define JD_OUT_CAP 65536

/* Parse the porcelain text: a line "XY <path>" (two status codes + a space)
 * names one dirty path; a rename "XY <old> -> <new>" keeps the rest of the
 * line after the codes. Only called when git exited 0, so a failed git's
 * stderr (captured on the same pipe) is never misread as a path. Sets *npaths
 * to the total count and fills `paths` with the first max_named, comma-joined.
 * Pure: writes only into the caller's buffers. */
static void parse_porcelain(const char *text, int *npaths,
                            char *paths, size_t cap, int max_named)
{
    const char *line = text;
    size_t have = 0;
    char pbuf[JD_PATH_BUF];

    *npaths = 0;
    if (paths != NULL && cap > 0) {
        paths[0] = '\0';
    }
    for (;;) {
        const char *nl = strchr(line, '\n');
        size_t llen;
        if (nl != NULL) {
            llen = (size_t)(nl - line);
        } else {
            llen = strlen(line);
        }
        if (llen >= 4) {
            /* "XY <p>": need two status codes + a space (length >= 4). */
            size_t plen = llen - 3;
            if (plen >= sizeof(pbuf)) {
                plen = sizeof(pbuf) - 1;
            }
            memcpy(pbuf, line + 3, plen);
            pbuf[plen] = '\0';
            if (pbuf[0] != '\0') {
                *npaths += 1;
                if (paths != NULL && cap > 0 && *npaths <= max_named) {
                    size_t room = plen + (have > 0 ? 2u : 0u);
                    if (have + room < cap) {
                        if (have > 0) {
                            jc_snprintf(paths + have, cap - have, ", ");
                            have += 2;
                        }
                        jc_snprintf(paths + have, cap - have, "%s", pbuf);
                        have += plen;
                    }
                }
            }
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
}

int jc_dispatch_dirty_probe(const char *root, int *state, int *npaths,
                            char *paths, size_t cap, int max_named)
{
    char *argv[8];
    struct jc_sb out;
    int exit_code;

    *state = JC_DISPATCH_CLEAN;
    *npaths = 0;
    if (paths != NULL && cap > 0) {
        paths[0] = '\0';
    }
    if (root == NULL || root[0] == '\0') {
        return 0;
    }

    /* No shell: argv form. `root` is one argument, so a single quote (or a
     * space, or any metacharacter) reaches git intact. */
    argv[0] = (char *)"git";
    argv[1] = (char *)"-C";
    argv[2] = (char *)root;
    argv[3] = (char *)"status";
    argv[4] = (char *)"--porcelain";
    argv[5] = NULL;

    jc_sb_init(&out);
    /* 30 s caps a stuck git (the driver's own deadline is looser); NULL
     * abort flag -- the probe has no cancellation seam of its own. */
    exit_code = jc_proc_capture(argv, NULL, NULL, &out, JD_OUT_CAP, 30, NULL);

    if (exit_code == 0) {
        /* git ran and finished: any valid porcelain line means dirty. */
        int n = 0;
        parse_porcelain(out.data != NULL ? out.data : "", &n,
                        paths, cap, max_named);
        *npaths = n;
        *state = (n > 0) ? JC_DISPATCH_DIRTY : JC_DISPATCH_CLEAN;
    } else {
        /* git did not run successfully (nonzero exit, or spawn -1 / timeout
         * -2). The else-branch says unknown, not clean (M519/M628) -- UNLESS
         * the workspace is not a git repository, which reads not-dirty. The
         * discriminator: rev-parse --is-inside-work-tree exits 0 in a git
         * work tree and 128 outside one. A corrupt .git/index fails status
         * (rc 128) but NOT rev-parse (rc 0), so it reads UNKNOWN (a git
         * workspace git cannot read) -- the non-git case and the broken-git
         * case are told apart by rev-parse, not by the status exit code
         * (which is 128 for both). */
        char *argv2[8];
        struct jc_sb rp;
        int rc;
        argv2[0] = (char *)"git";
        argv2[1] = (char *)"-C";
        argv2[2] = (char *)root;
        argv2[3] = (char *)"rev-parse";
        argv2[4] = (char *)"--is-inside-work-tree";
        argv2[5] = NULL;
        jc_sb_init(&rp);
        rc = jc_proc_capture(argv2, NULL, NULL, &rp, 64, 30, NULL);
        jc_sb_free(&rp);
        *state = (rc == 0) ? JC_DISPATCH_UNKNOWN : JC_DISPATCH_CLEAN;
    }
    jc_sb_free(&out);
    return 0;
}
