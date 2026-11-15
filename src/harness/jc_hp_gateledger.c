/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateledger.c - the `gate run --stage ledger` body (M1154, batch
 * H9b, wave 1, task 2): the C89 port of the 2026-10-27 hardening-host
 * drive's ledger.sh (read line by line: :11 rev-list, :13-14 the address
 * checks, :16-18 the Co-Authored-By check, :19-22 the two trailers,
 * :23 the priced-model case, :24-26 the bad line + the count line,
 * :27 the exit). CHANGED (M1154 note behaviour 2 DD11): the reference
 * hard-codes ADDR -- the port takes --ledger-address (REQUIRED; the
 * config's host.ledgerAddress is the absent-flag default, the M1153
 * idiom) -- and carries the host's accept list (--ledger-accept, one
 * FULL 40-hex id per line; a listed commit is ACCEPTED iff every one of
 * its reasons is a missing-trailer reason, prints `<sha8> accepted:
 * <reasons> (<subject cut 70>)`, and does not count toward bad).
 * CHANGED (M1154 T4, SV:0137 -- the host's own ledger.sh beside the
 * reference): ONE more accepted shape, DD6 the supervisor's
 * docs/internal-only commit (the operator as author AND committer, the
 * single Co-Authored-By exactly `Claude Opus 5.5 <noreply@anthropic.com>`,
 * every changed path under docs/internal/), printed as `<sha8> accepted:
 * the supervisor's commit (Claude's trailer, docs/internal/ only; DD6)`,
 * never counted toward bad; the same trailer with anything else changed,
 * beside another Co-Authored-By, or under another author is still bad.
 *
 * The git children run through jc_hp_git_run (the M1056 git-child idiom,
 * the M2-neutral -c flags + the GIT_CONFIG_=/dev/null slots): `git -C
 * <REPO>` with the reference's own argv (rev-list / log -1
 * --format=...). Everything else (the reason order, the accept decision,
 * the byte shapes) is the PURE core exported through
 * jc_hp_gateledger.h and unit-tested by tests/test_gateledger.c. rc:
 * 0 iff N > 0 and B == 0, 1 otherwise (the reference's :27); 2 for
 * usage (no config / a bad config, the missing --ledger-address). */

#include "jc_hp_gateledger.h"

#include <stdio.h>
#include <string.h>

#include "jc_harness_config.h"  /* the config load (the host block's fields) */
#include "jc_harness_push.h"    /* jc_hp_git_run (the shared git child) */
#include "jc_mem.h"             /* jc_arena_strdup / _alloc */
#include "jc_platform.h"        /* jc_status, jc_size */
#include "jc_snprintf.h"        /* jc_snprintf, never sprintf */
#include "jc_str.h"             /* struct jc_sb (the rev-list capture) */
#include <stdarg.h>             /* va_list (gl_append) */

#define GL_CAP_LINE   (8 * 1024)   /* one trailer / log-format capture */
#define GL_CAP_SHA    96           /* a full 40-hex id + slack */

/* One git child in REPO: jc_hp_git_run with no credential wiring (a read
 * child, the records' git_read shape); returns its code and leaves the
 * combined output in out (NUL-terminated, truncated). `args` is the
 * git SUBCOMMAND's argv (args[0] = "rev-list" / "log" / ...). */
static int
gl_git(const char *repo, const char *args[], int n_args, char *out,
       int cap)
{
    return jc_hp_git_run(NULL, repo, NULL, 0, NULL, 0, args, n_args, cap,
                         out, NULL);
}

/* jc_hp_gateledger_is_priced (exported, pure): ledger.sh:23's case over
 * the six provider id patterns (*anthropic/, *openai/, *vertex_ai/,
 * *gemini/, *bedrock/, *azure/). A NULL / empty id is not priced (an
 * absent Jichi-Model is the no-Jichi-Model reason, not a priced one). */
int
jc_hp_gateledger_is_priced(const char *id)
{
    static const char *const providers[] =
        { "anthropic", "openai", "vertex_ai", "gemini", "bedrock", "azure" };
    int i;

    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    for (i = 0; i < (int)(sizeof providers / sizeof providers[0]); i++) {
        if (strstr(id, providers[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* Append one reason to `why` (a leading space per reason, the reference's
 * why="$why <reason>" shape), bounded; a format that cannot fit truncates
 * the whole line (a cut reason is a different ledger line -- never
 * silently). */
static void
gl_append(char *why, size_t cap, const char *fmt, ...)
{
    char part[GL_CAP_LINE];
    char next[GL_CAP_LINE * 2];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(part, sizeof part, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    /* Build the concatenation in a scratch buffer, then copy back: writing
     * into `why` while reading it as a format argument (the naive
     * jc_snprintf(why, cap, "%s %s", why, part)) is undefined behaviour and
     * dropped all but the last reason on this platform. */
    if ((size_t)jc_snprintf(next, sizeof next, "%s %s", why, part)
        >= sizeof next) {
        why[0] = '\0';   /* a reason that cannot fit: the whole line stops */
        return;
    }
    if (strlen(next) >= cap) {
        why[0] = '\0';
        return;
    }
    memcpy(why, next, strlen(next) + 1);
}

/* jc_hp_gateledger_reasons (exported, pure): the reference's six checks in
 * its order (ledger.sh:14-23), joined with a leading space per reason so
 * `printf("%.8s%s\n", sha, why)` is byte-identical to ledger.sh:24. */
void
jc_hp_gateledger_reasons(struct jc_hp_gateledger_commit *c,
                         const char *addr, char *why, size_t cap)
{
    why[0] = '\0';
    if (c == NULL || addr == NULL) {
        return;
    }
    /* :14-15 the author / committer address. */
    if (c->ae == NULL || strcmp(c->ae, addr) != 0) {
        gl_append(why, cap, "author=%s", c->ae != NULL ? c->ae : "");
    }
    if (c->ce == NULL || strcmp(c->ce, addr) != 0) {
        gl_append(why, cap, "committer=%s", c->ce != NULL ? c->ce : "");
    }
    /* :16-18 the Co-Authored-By: absent -> no-Co-Authored-By; present but
     * ANY value without <addr> -> the bracket join (ledger.sh:18's
     * tr '\n' '|' over the valueonly,unfold values). */
    if (c->co_list == NULL || c->co_list->len == 0) {
        gl_append(why, cap, "no-Co-Authored-By");
    } else {
        int any_off = 0;
        size_t i;
        char bracket[512];

        /* ledger.sh:18: ANY value line without `<ADDR>` is the reason.
         * The return is deliberately unused (bracket is a 512-byte sink;
         * jc_snprintf always NUL-terminates): the (void) keeps clang's
         * -Wunused-value quiet on the expression (M1160-c, SV:1309:
         * make ci's clang stage, from 1501836d). */
        (void)jc_snprintf(bracket, sizeof bracket, "<%s>", addr);
        for (i = 0; i < c->co_list->len; i++) {
            const char *v = *(const char **)jc_vec_at(c->co_list, i);
            if (v == NULL || strstr(v, bracket) == NULL) {
                any_off = 1;
                break;
            }
        }
        if (any_off) {
            char join[GL_CAP_LINE * 4];
            size_t jlen = 0;
            int first = 1;
            join[0] = '\0';
            for (i = 0; i < c->co_list->len && jlen < sizeof join - 2; i++) {
                const char *v = *(const char **)jc_vec_at(c->co_list, i);
                if (!first) {
                    join[jlen++] = '|';
                }
                first = 0;
                if (v != NULL) {
                    size_t vl = strlen(v);
                    if (vl > sizeof join - jlen - 1) {
                        vl = sizeof join - jlen - 1;
                    }
                    memcpy(join + jlen, v, vl);
                    jlen += vl;
                }
                join[jlen] = '\0';
            }
            gl_append(why, cap, "co-authored-by=[%s]", join);
        }
    }
    /* :19-22 the two trailers (valueonly, "" when absent). */
    if (c->jb == NULL || c->jb[0] == '\0') {
        gl_append(why, cap, "no-Jichi-Build");
    }
    if (c->jm == NULL || c->jm[0] == '\0') {
        gl_append(why, cap, "no-Jichi-Model");
    }
    /* :23 the priced provider's id (a Jichi-Model that IS present). */
    if (c->jm != NULL && c->jm[0] != '\0' &&
        jc_hp_gateledger_is_priced(c->jm)) {
        gl_append(why, cap, "priced-model=%s", c->jm);
    }
}

/* jc_hp_gateledger_all_missing_trailers (exported, pure): the DD11 accept
 * arm (M1154 note behaviour 2 CHANGED): every reason is one of the three
 * missing-trailer reasons. A CLEAN commit (no reasons at all) qualifies
 * (there is nothing to exempt) -- the accept list's own commits are the
 * missing-trailer ones, and a clean listed commit would not need the list.
 * The reasons are space-separated `key=value` words, so scan the words: a
 * word that is not one of the three (it has no key=, or a wrong key)
 * disqualifies. */
int
jc_hp_gateledger_all_missing_trailers(struct jc_hp_gateledger_commit *c,
                                      const char *addr)
{
    char why[GL_CAP_LINE];
    const char *p;

    if (c == NULL) {
        return 0;
    }
    jc_hp_gateledger_reasons(c, addr, why, sizeof why);
    for (p = why; *p != '\0';) {
        const char *start;
        const char *end;
        char word[GL_CAP_LINE];

        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        end = start;
        while (*end != '\0' && *end != ' ') {
            end++;
        }
        if ((size_t)(end - start) >= sizeof word) {
            return 0;   /* an unreasonably long word is not one of the three */
        }
        memcpy(word, start, (size_t)(end - start));
        word[end - start] = '\0';
        if (strcmp(word, "no-Co-Authored-By") != 0 &&
            strcmp(word, "no-Jichi-Build") != 0 &&
            strcmp(word, "no-Jichi-Model") != 0) {
            return 0;
        }
        p = end;
    }
    return 1;
}

/* jc_hp_gateledger_accept_listed (exported, pure): the FULL 40-hex id is a
 * whole line of the accept list (one id per line, the reference's
 * ledger-accept shape; the reader trims the surrounding whitespace). */
int
jc_hp_gateledger_accept_listed(const char *id, const struct jc_vec *accept)
{
    size_t i;
    const char **elems;

    if (id == NULL || accept == NULL || strlen(id) != 40) {
        return 0;
    }
    elems = (const char **)accept->data;
    for (i = 0; i < accept->len; i++) {
        const char *line = elems[i];
        if (line != NULL && strcmp(line, id) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Trim trailing newlines / carriage returns (the child's output carries the
 * format's final newline; command substitution in the reference strips it). */
static void
gl_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/* `git -C repo log -1 --format=%(trailers:key=K,valueonly,unfold) c`:
 * the trailer value into out (the reference's sed '/^$/d' drops the
 * empty tail, which the caller trims; an absent trailer is ""). Returns
 * the child's code. */
static int
gl_trailer(const char *repo, const char *commit, const char *key, char *out,
           int cap)
{
    char fmt[160];
    const char *args[5];
    int n;

    out[0] = '\0';
    n = jc_snprintf(fmt, sizeof fmt,
                    "--format=%%(trailers:key=%s,valueonly,unfold)", key);
    if (n < 0 || (size_t)n >= sizeof fmt) {
        return -1;
    }
    args[0] = "log";
    args[1] = "-1";
    args[2] = (char *)fmt;   /* jc_hp_git_run reads the n_args words; the
                                --format=... word is one argv entry. */
    args[3] = commit;
    args[4] = NULL;
    n = gl_git(repo, args, 4, out, cap);
    gl_trim(out);
    return n;
}

/* `git -C repo log -1 --format=<atom> c` (the reference's %ae / %ce / %s
 * reads); one such child into out. */
static int
gl_atom(const char *repo, const char *commit, const char *atom, char *out,
        int cap)
{
    char fmt[64];
    const char *args[5];
    int n;

    out[0] = '\0';
    n = jc_snprintf(fmt, sizeof fmt, "--format=%s", atom);
    if (n < 0 || (size_t)n >= sizeof fmt) {
        return -1;
    }
    args[0] = "log";
    args[1] = "-1";
    args[2] = fmt;
    args[3] = commit;
    args[4] = NULL;
    n = gl_git(repo, args, 4, out, cap);
    gl_trim(out);
    return n;
}

/* The accept file's lines into `accept` (a vector of `const char *`,
 * arena-owned; an unreadable / missing file is an EMPTY list, not an
 * error -- a gate with no accept list is the reference's shape; -1 is
 * OOM only). */
static int
gl_read_accept(const char *path, struct jc_arena *a, struct jc_vec *accept)
{
    FILE *f;
    char line[512];

    jc_vec_init(accept, sizeof (const char *));
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        size_t len = strlen(line);
        const char *s;
        const char *v;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                           line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }
        s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            continue;
        }
        v = jc_arena_strdup(a, s);
        if (v == NULL || jc_vec_push(accept, &v) != JC_OK) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

/* jc_hp_gateledger_load_accept (exported): see the header (M1178). */
int
jc_hp_gateledger_load_accept(const char *path, struct jc_arena *a,
                             struct jc_vec *accept)
{
    jc_vec_init(accept, sizeof (const char *));
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    return gl_read_accept(path, a, accept);
}

/* Split the trailers capture (newlines) into the co_list (arena-owned
 * values; the empty tail the reference's sed '/^$/d' drops). -1 is OOM.
 * `scan` is the position walked through the string; each pass advances it,
 * so the loop terminates. */
static int
gl_split_lines(const char *s, struct jc_arena *a, struct jc_vec *list)
{
    size_t scan = 0;
    size_t slen = strlen(s);

    jc_vec_clear(list);
    for (;;) {
        size_t start, end;

        if (scan >= slen) {
            break;
        }
        start = scan;
        end = start;
        while (end < slen && s[end] != '\n') {
            end++;
        }
        while (start < end && (s[start] == ' ' || s[start] == '\t')) {
            start++;
        }
        while (end > start &&
               (s[end - 1] == '\n' || s[end - 1] == '\r' || s[end - 1] == ' ' ||
                s[end - 1] == '\t')) {
            end--;
        }
        if (start < end) {
            const char *v = jc_arena_strndup(a, s + start, (jc_size)(end - start));
            if (v == NULL || jc_vec_push(list, &v) != JC_OK) {
                return -1;
            }
        }
        scan = end;
        if (scan < slen) {
            scan++;   /* skip the newline */
        } else {
            break;
        }
    }
    return 0;
}

/* The DD6 supervisor's Co-Authored-By value (SV:0137's quote of the host
 * ledger.sh: SUP='Claude Opus 5.5 <noreply@anthropic.com>' -- the trailer
 * docs/analysis/COMMIT-ATTRIBUTION.md gives an agent other than jichi). */
static const char *const GL_DD6_SUP = "Claude Opus 5.5 <noreply@anthropic.com>";

/* jc_hp_gateledger_dd6_guard (exported, pure): the reference's
 * `[ -z "$why" ] && [ "$co" = "$SUP" ]` -- author AND committer are the
 * operator, and the split Co-Authored-By list is EXACTLY one value,
 * byte-equal to SUP (a second Co-Authored-By line makes the reference's
 * $co multi-line, not equal -> the normal co-authored-by reason). `c`
 * needs ae / ce / co_list only (jb / jm / dd6_paths are the
 * classifier's, read after the guard passes). */
int
jc_hp_gateledger_dd6_guard(struct jc_hp_gateledger_commit *c, const char *addr)
{
    const char *v;

    if (c == NULL || addr == NULL) {
        return 0;
    }
    if (c->ae == NULL || strcmp(c->ae, addr) != 0) {
        return 0;   /* the reference's why is non-empty: the shape never fires */
    }
    if (c->ce == NULL || strcmp(c->ce, addr) != 0) {
        return 0;
    }
    if (c->co_list == NULL || c->co_list->len != 1) {
        return 0;   /* a second Co-Authored-By line: $co is multi-line */
    }
    v = *(const char **)jc_vec_at(c->co_list, 0);
    if (v == NULL || strcmp(v, GL_DD6_SUP) != 0) {
        return 0;
    }
    return 1;
}

/* The reference's paths read: `git -C repo diff-tree -r --name-only
 * --no-commit-id <c^> <c>` (the --no-commit-id keeps the capture the
 * name-only lines alone; git itself parses the `^` suffix on the parent
 * revision, exactly as the reference passes "$c^") split into `paths`
 * (arena-owned, the gl_split_lines idiom). A commit with NO parent (a
 * range's first) cannot be a DD6 commit -- the parent revision fails,
 * which IS the reference's `||` arm (the "could not be read" reason; a
 * range's first commit with the SUP trailer would hit it). 0 = read, -1 =
 * OOM, the git code otherwise (the reference's failure). */
static int
gl_dd6_paths(const char *repo, const char *sha, struct jc_arena *a,
             struct jc_vec *paths)
{
    char pparent[42];
    char cap[GL_CAP_LINE * 8];
    const char *args[6];
    int n;
    int rc;

    if (sha == NULL || strlen(sha) != 40) {
        return 1;   /* not a full id: the revision cannot resolve */
    }
    n = jc_snprintf(pparent, sizeof pparent, "%s^", sha);
    if (n < 0 || (size_t)n >= sizeof pparent) {
        return 1;
    }
    args[0] = "diff-tree";
    args[1] = "-r";
    args[2] = "--name-only";
    args[3] = "--no-commit-id";
    args[4] = pparent;
    args[5] = sha;
    rc = gl_git(repo, args, 6, cap, (int)sizeof cap);
    if (rc != 0) {
        return rc;   /* the reference's `|| { bad=...; continue; }` arm */
    }
    return gl_split_lines(cap, a, paths);
}

/* jc_hp_gateledger_dd6 (exported, pure): the host's ledger.sh DD6 shape
 * (SV:0137's measured cells) after the guard has passed. The changed paths
 * readable (`dd6_paths` non-NULL) AND every one of them starting
 * `docs/internal/` -> 1 (the accepted arm); paths unreadable -> it fills
 * `out` (cap) with the reference's could-not-read reason and returns 0; a
 * path outside docs/internal/ -> 0 (the fall-through: the normal reasons,
 * the commit stays bad). `out` is the ledger line's reason slot; "" when
 * it returns 1 or the guard had already disqualified it. */
int
jc_hp_gateledger_dd6(struct jc_hp_gateledger_commit *c, const char *addr,
                     char *out, size_t cap)
{
    const char *p;
    size_t i;

    out[0] = '\0';
    if (c == NULL || addr == NULL) {
        return 0;
    }
    if (!jc_hp_gateledger_dd6_guard(c, addr)) {
        return 0;
    }
    if (c->dd6_paths == NULL) {
        /* The reference's `||` arm (its bad line for an unreadable paths
         * read): one reason, the commit counted bad, never accepted. */
        gl_append(out, cap,
                  "the supervisor's trailer, but its changed paths could "
                  "not be read");
        return 0;
    }
    for (i = 0; i < c->dd6_paths->len; i++) {
        p = *(const char **)jc_vec_at(c->dd6_paths, i);
        /* The reference's grep -v -q '^docs/internal/': a line that does
         * not match is the fall-through (never accepted). */
        if (p == NULL || strncmp(p, "docs/internal/", 14) != 0) {
            return 0;
        }
    }
    return 1;
}

/* jc_hp_gateledger_run (the dispatch entry): the config's host block is
 * the absent-flag default (the M1153 idiom, the flag wins); the ledger's
 * lines go to stdout (the orchestrator, wave 4, captures them into
 * <gate-home>/ledger.txt and says the last line). */
int
jc_hp_gateledger_run(const char *config_path, const char *repo,
                     const char *base, const char *head, const char *addr,
                     const char *accept_file, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    char buf[GL_CAP_LINE];
    char ae[GL_CAP_SHA], ce[GL_CAP_SHA], subj[GL_CAP_SHA];
    char jb[GL_CAP_LINE], jm[GL_CAP_LINE];
    char co[GL_CAP_LINE * 4];
    char range[GL_CAP_LINE * 2];
    struct jc_sb list;
    struct jc_vec co_list;
    struct jc_vec accept;
    const char *c_addr, *c_accept;
    int n_commits = 0, bad = 0;
    size_t off;

    if (config_path == NULL) {
        fprintf(stderr, "jichi-harness: gate run --stage ledger: --config "
                        "<path> is required\n");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        fprintf(stderr, "jichi-harness: gate run --stage ledger: %s\n",
                msg != NULL ? msg : "could not read the harness config");
        return 2;
    }
    /* --ledger-address is REQUIRED (M1154 note CHANGED: the reference
     * hard-codes ADDR): the flag wins, the config's host.ledgerAddress is
     * the absent-flag default, both absent -> exit 2 naming the flag. */
    c_addr = (addr != NULL && addr[0] != '\0') ? addr
            : (cfg.host_ledger_address != NULL &&
               cfg.host_ledger_address[0] != '\0')
                  ? cfg.host_ledger_address : NULL;
    if (c_addr == NULL) {
        fprintf(stderr, "jichi-harness: gate run --stage ledger: "
                        "--ledger-address <addr> is required (the flag or "
                        "the config's host.ledgerAddress)\n");
        return 2;
    }
    /* --ledger-accept (DD11): the flag wins, the config's host.ledgerAccept
     * is the absent-flag default; absent = no accept list. */
    c_accept = (accept_file != NULL && accept_file[0] != '\0')
                   ? accept_file
                   : (cfg.host_ledger_accept != NULL &&
                      cfg.host_ledger_accept[0] != '\0')
                         ? cfg.host_ledger_accept : NULL;
    if (jc_hp_gateledger_load_accept(c_accept, a, &accept) != 0) {
        fprintf(stderr, "jichi-harness: gate run --stage ledger: out of "
                        "memory reading the accept list\n");
        jc_vec_free(&accept);
        return 1;
    }
    jc_sb_init(&list);
    jc_vec_init(&co_list, sizeof (const char *));
    /* :11 the range, rev-list order (newest first, the reference's). */
    if (jc_snprintf(range, sizeof range, "%s..%s", base, head)
        >= (int)sizeof range) {
        fprintf(stderr, "jichi-harness: gate run --stage ledger: the range "
                        "is too long\n");
        goto fail_1;
    }
    {
        const char *a_rev[3];
        char revcap[GL_CAP_LINE * 8];
        int rc;

        a_rev[0] = "rev-list";
        a_rev[1] = range;
        a_rev[2] = NULL;
        rc = gl_git(repo, a_rev, 2, revcap, (int)sizeof revcap);
        if (rc != 0) {
            fprintf(stderr, "jichi-harness: gate run --stage ledger: git "
                            "rev-list failed (rc=%d): %s\n", rc, revcap);
            goto fail_1;
        }
        jc_sb_append(&list, revcap);
    }
    /* Walk the rev-list output: one full sha per line, newest first. */
    off = 0;
    while (off < list.len) {
        size_t len = 0;
        char *line = (char *)list.data + off;

        while (len < list.len - off && line[len] != '\n') {
            len++;
        }
        off += len;
        if (len == 0) {   /* a blank line (the capture's tail) */
            if (off < list.len) {
                off++;
            }
            continue;
        }
        if (off < list.len) {
            off++;   /* the newline */
        }
        if (len != 40) {
            continue;   /* not a full sha line: skip (rev-list's are) */
        }
        {
            char sha[41];
            struct jc_hp_gateledger_commit cc;

            memcpy(sha, line, 40);
            sha[40] = '\0';
            n_commits++;
            jc_vec_clear(&co_list);
            /* :13-14 the two addresses. */
            if (gl_atom(repo, sha, "%ae", ae, (int)sizeof ae) != 0 ||
                gl_atom(repo, sha, "%ce", ce, (int)sizeof ce) != 0) {
                fprintf(stderr,
                        "jichi-harness: gate run --stage ledger: git log "
                        "failed for %.8s\n", sha);
                goto fail_1;
            }
            /* :16 the Co-Authored-By values (valueonly,unfold; the split
             * drops the empty lines the reference's sed '/^$/d' drops). */
            if (gl_trailer(repo, sha, "Co-Authored-By", co,
                           (int)sizeof co) != 0 ||
                gl_split_lines(co, a, &co_list) != 0) {
                fprintf(stderr,
                        "jichi-harness: gate run --stage ledger: git log "
                        "failed for %.8s (trailers)\n", sha);
                goto fail_1;
            }
            /* :19-20 the two trailers. */
            gl_trailer(repo, sha, "Jichi-Build", jb, (int)sizeof jb);
            gl_trailer(repo, sha, "Jichi-Model", jm, (int)sizeof jm);
            cc.ae = ae;
            cc.ce = ce;
            cc.co_list = &co_list;
            cc.jb = jb;
            cc.jm = jm;
            cc.dd6_paths = NULL;
            jc_hp_gateledger_reasons(&cc, c_addr, buf, sizeof buf);
            if (buf[0] != '\0' &&
                jc_hp_gateledger_dd6_guard(&cc, c_addr)) {
                /* The DD6 arm (the host's ledger.sh beside the reference,
                 * M1154 T4): the author/committer reasons are EMPTY (the
                 * guard's `[ -z "$why" ]`) and the single Co-Authored-By is
                 * exactly SUP. Save the normal reasons (the walk's buf, the
                 * reference's why here) first: on the fall-through (a path
                 * outside docs/internal/) the classifier leaves out "" and
                 * the normal reasons apply. */
                char why_saved[GL_CAP_LINE];
                struct jc_vec paths;
                int prc;

                memcpy(why_saved, buf, sizeof why_saved);
                jc_vec_init(&paths, sizeof (const char *));
                prc = gl_dd6_paths(repo, sha, a, &paths);
                if (prc < 0) {
                    jc_vec_free(&paths);
                    fprintf(stderr,
                            "jichi-harness: gate run --stage ledger: out of "
                            "memory for %.8s (DD6 paths)\n", sha);
                    goto fail_1;
                }
                if (prc != 0) {
                    /* The reference's `||` arm: the changed paths could not
                     * be read -- a HARD continue (bad, printed, never
                     * accepted; the accept list does not apply). */
                    char dd6why[GL_CAP_LINE];
                    jc_hp_gateledger_dd6(&cc, c_addr, dd6why, sizeof dd6why);
                    bad++;
                    printf("%.8s%s\n", sha, dd6why);
                    jc_vec_free(&paths);
                    continue;
                }
                cc.dd6_paths = &paths;
                if (jc_hp_gateledger_dd6(&cc, c_addr, buf, sizeof buf)) {
                    /* The accept: printed, never counted (the reference's
                     * `printf ...; continue` -- no bad++). */
                    printf("%.8s accepted: the supervisor's commit (Claude's "
                           "trailer, docs/internal/ only; DD6)\n", sha);
                    jc_vec_free(&paths);
                    continue;
                }
                jc_vec_free(&paths);
                /* The fall-through (a path outside docs/internal/): restore
                 * the normal reasons so the commit is counted bad with them,
                 * exactly as before DD6. */
                memcpy(buf, why_saved, sizeof buf);
            }
            if (buf[0] != '\0') {
                /* The DD11 accept arm: the FULL id listed AND every reason
                 * a missing-trailer one -> accepted (never counted). */
                if (jc_hp_gateledger_accept_listed(sha, &accept) &&
                    jc_hp_gateledger_all_missing_trailers(&cc, c_addr)) {
                    gl_atom(repo, sha, "%s", subj, (int)sizeof subj);
                    if (strlen(subj) > 70) {
                        subj[70] = '\0';
                    }
                    printf("%.8s accepted:%s (%s)\n", sha, buf, subj);
                } else {
                    bad++;
                    printf("%.8s%s\n", sha, buf);
                }
            }
        }
    }
    /* :26-27 the count line + the exit (0 iff N > 0 and B == 0). */
    printf("ledger: %d commit(s), %d bad\n", n_commits, bad);
    jc_sb_free(&list);
    jc_vec_free(&co_list);
    jc_vec_free(&accept);
    return (n_commits > 0 && bad == 0) ? 0 : 1;

fail_1:
    jc_sb_free(&list);
    jc_vec_free(&co_list);
    jc_vec_free(&accept);
    return 1;
}
