/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateverdict.c - the `gate run --stage verdict` body (M1154, batch
 * H9b, wave 1, task 3): the C89 port of the 2026-10-27 hardening-host
 * drive's sweep-verdict.sh, read line by line: :15-16 the argv + the repo
 * default, :18 the g() helper, :19-20 the build + sweep patterns, :21 the
 * mutant-OK pattern, :22-23 the no-changed-drivers pattern + the MUTANT_
 * BASE extraction, :24 the two counting git children, :30-37 the second
 * count's loop (nall = the diff ALONE; the union's smoke scripts; the
 * 14:47 uses_bin selector), :38-47 the four count outcomes, :51-54 the
 * unit + container patterns, :55-56 the flagged pipe (head -3, cut
 * -c1-120, newline->|), :57-58 the green<note?> / red: output.
 *
 * The per-stage patterns + the note assembly are the PURE core exported
 * through jc_hp_gateverdict.h and unit-tested by tests/test_gateverdict.c;
 * the second count's git children run through jc_hp_git_run (the M1056
 * idiom, T2's gl_git shape: a read child, no credential wiring). rc: 0
 * green, 1 red, 2 usage (no config / a bad config), 1 on OOM. */

#include "jc_hp_gateverdict.h"

#include <stdio.h>
#include <string.h>

#include "jc_harness_config.h"  /* the config load (the host block's fields) */
#include "jc_harness_push.h"    /* jc_hp_git_run (the shared git child) */
#include "jc_mem.h"             /* jc_arena_strdup / _strndup */
#include "jc_platform.h"        /* jc_size, JC_OK */
#include "jc_snprintf.h"        /* jc_snprintf, never sprintf */
#include "jc_str.h"             /* struct jc_sb (the sweep file capture) */

#define GV_CAP_COUNT (256 * 1024)   /* a name-only capture (the child truncates) */
#define GV_CAP_BODY  (64 * 1024)    /* one driver's body */
#define GV_FLAG_MAX  120            /* cut -c1-120 (the reference's :55) */
#define GV_LINE_MAX  8192           /* one sweep.txt line (the cuts are 120) */
#define GV_CAP_PATH  1024           /* <s>:<driver> for the cat-file / show */
#define GV_CAP_GERR  1024           /* the "could not read" driver list */
#define GV_CAP_WHY   2048           /* the red: reason line */

/* One git child in REPO: jc_hp_git_run with no credential wiring (a read
 * child, the gl_git shape of T2's ledger body); returns its code and
 * leaves the combined output in out (NUL-terminated, truncated). `args`
 * is the git SUBCOMMAND's argv (args[0] = "diff" / "diff-tree" / ...). */
static int
gv_git(const char *repo, const char *args[], int n_args, char *out,
       int cap)
{
    return jc_hp_git_run(NULL, repo, NULL, 0, NULL, 0, args, n_args, cap,
                         out, NULL);
}

/* The subset of POSIX ERE the reference's six g() patterns use
 * (sweep-verdict.sh:19-20, :21, :51-54): literals, the [0-9] / [1-9]
 * digit classes, the [0-9]* / [0-9]+ / .* quantifiers, and the ^ / $
 * anchors. The matcher is a small recursive-descent over that grammar --
 * byte-identical to grep -q -E for these patterns, and it keeps this TU
 * free of a regex engine (the build's -D_POSIX_C_SOURCE=200112L hides
 * regcomp on the target libcs the Makefile's probes name). A pattern
 * WITHOUT a trailing $ matches a PREFIX of the line (grep's unanchored
 * end: `.*mutant: OK` may end mid-line); one WITH a ^ cannot start
 * anywhere but the line's first byte. */
#define GV_CLS_DIG 1   /* the [0-9] class */
#define GV_CLS_NIN 2   /* the [1-9] class */

static int
gv_cls(int cls, char c)
{
    if (c == '\0') {
        return 0;
    }
    if (cls == GV_CLS_DIG) {
        return c >= '0' && c <= '9';
    }
    if (cls == GV_CLS_NIN) {
        return c >= '1' && c <= '9';
    }
    return c == (char)cls;
}

/* The pattern's CORE (the ^ stripped, a trailing $ honored only as the
 * final character) matched against t starting NOW: 1 iff the whole core
 * consumes t to the point where -- when the core ends in $ -- t is done.
 * exact_end is the core's trailing-$ half (a pattern's $ only ever sits
 * last, so the core-with-$ is fed in whole). */
static int
gv_match_core(const char *p, const char *t, int exact_end)
{
    for (;;) {
        if (*p == '\0') {
            return exact_end ? (*t == '\0') : 1;
        }
        if (*p == '$' && p[1] == '\0') {
            return (*t == '\0');
        }
        if (*p == '^') {
            p++;
            continue;
        }
        if (*p == '.' && p[1] == '*') {
            const char *q = t;
            do {
                if (gv_match_core(p + 2, q, exact_end)) {
                    return 1;
                }
            } while (*q++ != '\0');
            return 0;
        }
        {
            int cls;
            const char *np;

            if (*p == '[') {
                if (strncmp(p, "[0-9]", 5) == 0) {
                    cls = GV_CLS_DIG;
                    np = p + 5;
                } else if (strncmp(p, "[1-9]", 5) == 0) {
                    cls = GV_CLS_NIN;
                    np = p + 5;
                } else {
                    return 0;   /* not in the grammar this matcher speaks */
                }
            } else {
                cls = (unsigned char)*p;
                np = p + 1;
            }
            if (np[0] == '*' || np[0] == '+') {
                /* zero-or-more / one-or-more of the class: grep -E tries
                 * every run length; this loop does the same. */
                int i = 0;
                int min = (np[0] == '+') ? 1 : 0;

                for (;;) {
                    if (i >= min && gv_match_core(np + 1, t + i, exact_end)) {
                        return 1;
                    }
                    if (!gv_cls(cls, t[i])) {
                        break;
                    }
                    i++;
                }
                return 0;
            }
            if (!gv_cls(cls, *t)) {
                return 0;
            }
            p = np;
            t++;
        }
    }
}

/* One line of the file against one g() pattern (grep -q -E semantics
 * over the line). */
static int
gv_line_match(const char *pat, const char *line)
{
    int anchored = (pat[0] == '^');
    const char *core = anchored ? pat + 1 : pat;
    size_t cn;
    int exact;
    size_t ln;
    size_t i;

    cn = strlen(core);
    exact = (cn >= 1 && core[cn - 1] == '$');
    ln = strlen(line);
    for (i = 0; ; i++) {
        if (gv_match_core(core, line + i, exact)) {
            return 1;
        }
        if (i >= ln) {
            break;
        }
        if (anchored) {
            break;
        }
    }
    return 0;
}

/* The flagged grep set `INVALID|FAILED|SKIPPED|^  RED ` (the reference's
 * :55): any of the four sub-patterns matches anywhere in the line. */
static int
gv_flagged(const char *line)
{
    return (strstr(line, "INVALID") != NULL ||
            strstr(line, "FAILED") != NULL ||
            strstr(line, "SKIPPED") != NULL ||
            strncmp(line, "  RED ", 6) == 0);
}

/* The no-changed-drivers line's fixed halves (the prepared reference's
 * :29 regex `^  smoke-mutant rc=0 \(MUTANT_BASE=[0-9a-f]{7,40}\):
 * mutant: no changed drivers to sweep \([0-9]+ excluded\)$`, checked
 * by hand below -- the {7,40} repeat and the digit run are the grammar
 * this matcher does not speak). M1187-b: the tail accepts ANY digit
 * run (the (0 excluded) literal was the REL3 false-RED bug: the count
 * is mutant-sweep.sh's own nskip, not a number this script asserts). */
static const char GV_MUT_PRE[] = "  smoke-mutant rc=0 (MUTANT_BASE=";
static const char GV_MUT_MID[] = "): mutant: no changed drivers to sweep (";
static const char GV_MUT_END[] = " excluded)";

static int
gv_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* jc_hp_gv_classify (exported, pure): one pass over the sweep file's
 * lines, the reference's g() calls (:18-20, :21-22, :51-54, :55). */
void
jc_hp_gv_classify(const char *text, const char *s,
                  struct jc_hp_gv_class *c)
{
    char pat_build[160];
    char pat_sweep[192];
    char pat_ok[96];
    char pat_san[96];
    char pat_o2[96];
    char pat_cf[128];
    char pat_cr[128];
    size_t off;
    size_t len;

    memset(c, 0, sizeof *c);
    if (text == NULL || s == NULL) {
        return;
    }
    (void)jc_snprintf(pat_build, sizeof pat_build, "^  build: %s$", s);
    (void)jc_snprintf(pat_sweep, sizeof pat_sweep,
                      "^sweep: ran [1-9][0-9]* drivers on %s; red: none$",
                      s);
    (void)jc_snprintf(pat_ok, sizeof pat_ok,
                      "^  smoke-mutant rc=0 .*mutant: OK");
    (void)jc_snprintf(pat_san, sizeof pat_san,
                      "^  unit san rc=0 [0-9]+ checks, 0 failures$");
    (void)jc_snprintf(pat_o2, sizeof pat_o2,
                      "^  unit o2 rc=0 [0-9]+ checks, 0 failures$");
    (void)jc_snprintf(pat_cf, sizeof pat_cf,
                      "^  container fetch_ssrf_reuse rc=0 [1-9][0-9]* ok,"
                      " 0 not ok$");
    (void)jc_snprintf(pat_cr, sizeof pat_cr,
                      "^  container resolve_timeout_connect rc=0"
                      " [1-9][0-9]* ok, 0 not ok$");
    off = 0;
    len = strlen(text);
    while (off < len) {
        size_t start, end;
        char line[GV_LINE_MAX];
        size_t ll;

        start = off;
        end = start;
        while (end < len && text[end] != '\n') {
            end++;
        }
        ll = end - start;
        if (ll >= sizeof line) {
            ll = sizeof line - 1;   /* no pattern can need more */
        }
        memcpy(line, text + start, ll);
        line[ll] = '\0';
        off = end;
        if (end < len) {
            off++;
        }
        if (gv_line_match(pat_build, line)) {
            c->build = 1;
        }
        if (gv_line_match(pat_sweep, line)) {
            c->sweep = 1;
        }
        if (gv_line_match(pat_ok, line)) {
            c->mut_ok = 1;
        }
        if (strncmp(line, GV_MUT_PRE, sizeof GV_MUT_PRE - 1) == 0) {
            const char *h = line + (sizeof GV_MUT_PRE - 1);
            const char *t;
            const char *d;
            const char *after;
            size_t hn = 0;
            size_t tn;
            size_t i2;
            int elen = (int)(sizeof GV_MUT_END - 1);
            int nex = 0;

            while (h[hn] != '\0' && gv_hex(h[hn]) && hn < 40) {
                hn++;
            }
            if (hn >= 7 && !c->mut_none) {
                t = h + hn;   /* MID then the digit run then END, at the end */
                if (strncmp(t, GV_MUT_MID, sizeof GV_MUT_MID - 1) == 0) {
                    d = t + (sizeof GV_MUT_MID - 1);
                    tn = 0;
                    while (d[tn] >= '0' && d[tn] <= '9' && tn < 9) {
                        tn++;
                    }
                    if (tn >= 1) {
                        after = d + tn;
                        if (strncmp(after, GV_MUT_END, elen) == 0 &&
                            after[elen] == '\0') {
                            for (i2 = 0; i2 < tn; i2++) {
                                nex = nex * 10 + (d[i2] - '0');
                            }
                            c->mut_none = 1;   /* the FIRST such line */
                            memcpy(c->mut_mb, h, hn);
                            c->mut_mb[hn] = '\0';
                            c->mut_nex = nex;   /* the reference's nxm */
                        }
                    }
                }
            }
        }
        if (gv_line_match(pat_san, line)) {
            c->san = 1;
        }
        if (gv_line_match(pat_o2, line)) {
            c->o2 = 1;
        }
        if (gv_line_match(pat_cf, line)) {
            c->cf = 1;
        }
        if (gv_line_match(pat_cr, line)) {
            c->cr = 1;
        }
        if (c->n_flagged < 3 && gv_flagged(line)) {
            size_t n = strlen(line);   /* head -3, cut -c1-120 */
            if (n > GV_FLAG_MAX) {
                n = GV_FLAG_MAX;
            }
            memcpy(c->flagged[c->n_flagged], line, n);
            c->flagged[c->n_flagged][n] = '\0';
            c->n_flagged++;
        }
    }
}

/* jc_hp_gv_uses_bin (exported, pure): the 14:47 selector (the reference's
 * :36 over each driver body: `sed -e 's|#.*$||' | grep -q '"$BIN"'` --
 * strip every line from its first # to end-of-line, then require the
 * literal `"$BIN"` in what remains). A `"$BIN"` in a comment does not
 * count; one in a live line does. */
int
jc_hp_gv_uses_bin(const char *body)
{
    size_t off;
    size_t len;

    if (body == NULL) {
        return 0;
    }
    off = 0;
    len = strlen(body);
    while (off < len) {
        size_t start = off;
        size_t hash = len;   /* the first # on the line (sed's cut point) */
        size_t end = start;
        size_t i;

        while (end < len && body[end] != '\n') {
            if (body[end] == '#' && hash == len) {
                hash = end;   /* s|#.*$|| cuts at the FIRST # */
            }
            end++;
        }
        for (i = start; i + 6 <= hash; i++) {
            if (memcmp(body + i, "\"$BIN\"", 6) == 0) {
                return 1;
            }
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return 0;
}

/* One name-only capture (newline-separated paths) -> the non-empty line
 * count (the reference's :30 `printf | grep -c .`). Pure. */
static int
gv_count_names(const char *cap)
{
    int n = 0;
    size_t off;
    size_t len;

    if (cap == NULL) {
        return 0;
    }
    off = 0;
    len = strlen(cap);
    while (off < len) {
        size_t start = off;
        size_t end = start;

        while (end < len && cap[end] != '\n') {
            end++;
        }
        if (end > start) {
            n++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return n;
}

/* jc_hp_gv_smoke_union (exported, pure apart from the arena copies):
 * the reference's :31 selection -- `printf '%s\n%s\n' "$all" "$own" |
 * grep '^tests/smoke/.*\.sh$' | sort -u`. */
int
jc_hp_gv_smoke_union(const char *all, const char *own, struct jc_arena *a,
                     struct jc_vec *out)
{
    const char *segs[2];
    int i;

    jc_vec_clear(out);
    segs[0] = all;
    segs[1] = own;
    for (i = 0; i < 2; i++) {
        const char *p = segs[i];

        if (p == NULL) {
            continue;
        }
        while (*p != '\0') {
            const char *line = p;
            size_t ln = 0;
            int dup = 0;

            while (p[ln] != '\0' && p[ln] != '\n') {
                ln++;
            }
            p += ln;
            if (*p == '\n') {
                p++;
            }
            /* ^tests/smoke/.*\.sh$ : the prefix, a body (.* may be
             * empty), the .sh suffix, whole line. */
            if (ln >= 15 && strncmp(line, "tests/smoke/", 12) == 0 &&
                strncmp(line + ln - 3, ".sh", 3) == 0) {
                const char *copy;
                const char *key;
                size_t j;
                int k;

                copy = jc_arena_strndup(a, line, ln);
                if (copy == NULL) {
                    return -1;
                }
                for (j = 0; j < out->len; j++) {
                    const char *o = *(const char **)jc_vec_at(out, j);
                    if (strcmp(o, copy) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (dup) {
                    continue;
                }
                /* Insertion-sorted (sort -u): push, then walk back. */
                key = copy;
                if (jc_vec_push(out, &key) != JC_OK) {
                    return -1;
                }
                for (k = (int)out->len - 1; k > 0; k--) {
                    const char *prev = *(const char **)jc_vec_at(out, k - 1);
                    if (strcmp(prev, key) <= 0) {
                        break;
                    }
                    *(const char **)jc_vec_at(out, k) = prev;
                }
                *(const char **)jc_vec_at(out, k) = key;
            }
        }
    }
    return 0;
}

/* jc_hp_gv_excl_parse (exported, pure apart from the arena copy): the
 * reference's :35-36 awk, byte-for-byte. Line by line: at the first line
 * beginning `EXCLUDE='` (awk's `/^EXCLUDE='/` is an anchored match, so
 * the prefix must sit at the line's start) drop that prefix and emit
 * the rest (awk's `sub(/^EXCLUDE='/, "")`); then keep whole following
 * lines until a line containing a `'` -- that line contributes only its
 * prefix before the first `'` (awk's quote-strip); stop. Emitted
 * pieces are joined with single spaces (the `tr '\n' ' '`). A text with
 * no `EXCLUDE='` line emits nothing (the caller's table_empty arm). A
 * second line may carry the closing quote. */
int
jc_hp_gv_excl_parse(const char *text, struct jc_arena *a, char **out)
{
    const char *p;
    char *acc;
    size_t alen;
    size_t cap;
    int started;

    *out = NULL;
    if (text == NULL || a == NULL || out == NULL) {
        return -1;
    }
    acc = (char *)jc_arena_alloc(a, 128);
    if (acc == NULL) {
        return -1;
    }
    cap = 128;
    alen = 0;
    acc[0] = '\0';
    started = 0;
    p = text;
    while (*p != '\0') {
        const char *pstart;
        size_t plen;
        size_t ln;
        const char *lq;

        ln = 0;
        while (p[ln] != '\0' && p[ln] != '\n') {
            ln++;
        }
        pstart = p;
        plen = ln;
        if (!started) {
            if (ln >= 9 && strncmp(p, "EXCLUDE='", 9) == 0) {
                /* awk's rule 1: f = 1; sub(/^EXCLUDE='/, ""). */
                started = 1;
                pstart = p + 9;
                plen = ln - 9;
            }
        }
        if (started) {
            /* awk's rule 2: a line with a ' keeps its prefix before the
             * first ' and exits; otherwise the whole line prints. Each
             * print ends in a newline; the `tr '\n' ' '` turns every one
             * into a space, so append the piece then one space. */
            lq = (const char *)memchr(pstart, '\'', plen);
            if (lq != NULL) {
                plen = (size_t)(lq - pstart);
            }
            if (alen + plen + 2 > cap) {
                char *na;
                size_t nc = cap;

                while (nc < alen + plen + 2) {
                    nc *= 2;
                }
                na = (char *)jc_arena_alloc(a, nc);
                if (na == NULL) {
                    return -1;
                }
                memcpy(na, acc, alen + 1);
                acc = na;
                cap = nc;
            }
            memcpy(acc + alen, pstart, plen);
            alen += plen;
            acc[alen++] = ' ';
            acc[alen] = '\0';
            if (lq != NULL) {
                break;   /* awk's exit */
            }
        }
        p += ln;
        if (*p == '\n') {
            p++;
        }
    }
    *out = acc;
    return 0;
}

/* jc_hp_gv_excl_contains (exported, pure): the reference's :49 word
 * match against the joined names (see the .h). The reference matches
 * against " $excl " (a space ADDED at both ends); the joined names
 * carry only the trailing space, so the word boundaries here are
 * start-of-string / a space, then the name, then a space / end-of-
 * string -- a name that is a PREFIX of another table entry (smoke_lint
 * vs smoke_lint_extra) must not match. */
int
jc_hp_gv_excl_contains(const char *table, const char *name)
{
    const char *t;
    size_t nl;

    if (table == NULL || name == NULL) {
        return 0;
    }
    nl = strlen(name);
    if (nl == 0) {
        return 0;
    }
    t = table;
    while (*t != '\0') {
        if (strncmp(t, name, nl) == 0 &&
            (t == table || t[-1] == ' ') &&
            (t[nl] == ' ' || t[nl] == '\0')) {
            return 1;
        }
        t++;
    }
    return 0;
}

/* Append one reason to `why` (the reference's leading-";" form: each
 * reason carries a "; " that the :58 `${why#;}` strips at the front). A
 * reason that cannot fit still counts red (the line just stops). */
static void
gv_reason(char *why, size_t cap, int *red, const char *part)
{
    size_t wl = strlen(why);
    size_t pl = strlen(part);

    *red = 1;
    if (wl + pl + 1 > cap) {
        return;
    }
    memcpy(why + wl, part, pl + 1);
}

/* jc_hp_gv_verdict (exported, pure): the reference's why/note assembly
 * in ITS order (:19-20, :21-67, :71-74, :75-76, M1187-b); returns 1 iff
 * green (no reason at all). The GV_MUT_ARM half is the prepared if-chain
 * IN ITS ORDER (the order is the behaviour): cnt_fail (the diff /
 * diff-tree / show git-failure arm), gerr, table_empty (a table that
 * reads empty says red, never a silent green), the green guard
 * (nall > 0 && nd == 0 && nx == nxm), nd > 0 (the disagreement),
 * nx != nxm (the cross-check), else (nall == 0: "no changed file"). */
int
jc_hp_gv_verdict(const struct jc_hp_gv_class *c, const char *s,
                 int arm, int cnt_fail, const char *gerr,
                 int table_empty,
                 int nall, int nsm, int nd, int nx,
                 char *why, size_t why_cap,
                 char *note, size_t note_cap)
{
    char part[1024];
    int red = 0;
    int i;
    int n;

    why[0] = '\0';
    note[0] = '\0';
    if (c == NULL || s == NULL) {
        return 0;
    }
    if (!c->build) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: the build line for %s", s);
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (!c->sweep) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: the lint/docs sweep");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (arm == GV_MUT_MISSING) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: smoke-mutant");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    } else if (arm == GV_MUT_ARM) {
        if (cnt_fail) {
            /* :65-67 the git-failure arm (the prepared reason: the show
             * of scripts/mutant-sweep.sh is a third counting child). */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it swept no"
                " driver, and git could not count the batch's files, or"
                " read scripts/mutant-sweep.sh, from %s to %s)",
                c->mut_mb, s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        } else if (gerr != NULL && gerr[0] != '\0') {
            /* :52-53 (unchanged): a driver git could not read. */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it swept no"
                " driver, and git could not read%s at %s)", gerr, s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        } else if (table_empty) {
            /* :54-55 (NEW): the EXCLUDE table read as empty -- above
             * the green guard (V14 pins that order). */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it swept no"
                " driver, and the EXCLUDE table of"
                " scripts/mutant-sweep.sh at %s read as empty)", s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        } else if (nall > 0 && nd == 0 && nx == c->mut_nex) {
            /* :56-57 the green note (the ONLY green-note arm; the
             * prepared wording names the EXCLUDE table, and the nxm
             * cross-check is the green guard's third half). */
            n = jc_snprintf(
                note, note_cap,
                " (smoke-mutant had nothing to sweep: of the %d file(s)"
                " changed from %s to %s, none of the %d smoke script(s)"
                " is swept by mutant-sweep.sh (its uses_bin rule +"
                " EXCLUDE table), counted again by git)",
                nall, c->mut_mb, s, nsm);
            if (n < 0 || (size_t)n >= note_cap) {
                return 0;   /* a note that cannot fit: say red, not green */
            }
        } else if (nd > 0) {
            /* :58-59 the disagreement (re-worded: + "and are not in
             * mutant-sweep.sh's EXCLUDE table"). A changed driver that
             * invokes "$BIN" AND is NOT excluded still reds -- the
             * safety net the fix must not loosen. */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it swept no"
                " driver, but git counts %d changed smoke driver(s) that"
                " invoke the binary and are not in mutant-sweep.sh's"
                " EXCLUDE table, of %d changed smoke script(s) in %d"
                " changed file(s) from %s to %s)",
                nd, nsm, nall, c->mut_mb, s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        } else if (nx != c->mut_nex) {
            /* :60-61 (NEW): git's excluded count disagrees with the
             * line's (<n> excluded) (the reference's nxm). */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it reports %d"
                " excluded, but git counts %d changed smoke driver(s)"
                " that invoke the binary and are in mutant-sweep.sh's"
                " EXCLUDE table, from %s to %s)",
                c->mut_nex, nx, c->mut_mb, s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        } else {
            /* :62-63 (re-worded): nall == 0 ("file", singular). */
            n = jc_snprintf(
                part, sizeof part,
                "; missing or not clean: smoke-mutant (it swept no"
                " driver, and git counts no changed file from %s to %s)",
                c->mut_mb, s);
            if (n >= 0 && (size_t)n < sizeof part) {
                gv_reason(why, why_cap, &red, part);
            }
        }
    }
    if (!c->san) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: the SAN unit suite");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (!c->o2) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: the -O2 unit suite");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (!c->cf) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: container fetch_ssrf_reuse");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (!c->cr) {
        n = jc_snprintf(part, sizeof part,
                        "; missing or not clean: container"
                        " resolve_timeout_connect");
        if (n >= 0 && (size_t)n < sizeof part) {
            gv_reason(why, why_cap, &red, part);
        }
    }
    if (c->n_flagged > 0) {
        /* The reference's :55-56: `b=$(grep -E 'INVALID|FAILED|SKIPPED|^
         * RED ' | head -3 | cut -c1-120 | tr '\n' '|'); why="$why; flagged:
         * $b"` -- a space after "flagged:", then EVERY line followed by |
         * (tr also converts the final newline, so the pipe trails). */
        n = jc_snprintf(part, sizeof part, "; flagged: ");
        if (n >= 0 && (size_t)n < sizeof part) {
            for (i = 0; i < c->n_flagged; i++) {
                size_t wl = strlen(part);
                n = jc_snprintf(part + wl, sizeof part - wl, "%s|",
                                c->flagged[i]);
                if (n < 0 || (size_t)n >= sizeof part - wl) {
                    break;   /* a flagged line that cannot fit: the rest */
                }
            }
            gv_reason(why, why_cap, &red, part);
        }
    }
    return (red == 0);
}

/* jc_hp_gv_run (the dispatch entry): the second-count repo is the
 * --push-repo flag, else the config's host.pushRepo, else
 * <host.pushHome>/push.git (the M1153 default the note names); all
 * absent -> a repo path git cannot open, so the count arm behaves like
 * the reference's own missing-repo case (:24's children fail -> :45-47's
 * "git could not count", never a skip). */
int
jc_hp_gv_run(const char *config_path, const char *file, const char *s,
             const char *push_repo, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    struct jc_sb text;
    struct jc_vec union_v;
    struct jc_hp_gv_class cls;
    char allcap[GV_CAP_COUNT];
    char owncap[GV_CAP_COUNT];
    char body[GV_CAP_BODY];
    char mswcap[GV_CAP_COUNT];
    char mswout[GV_CAP_COUNT];
    char gerr[GV_CAP_GERR];
    char why[GV_CAP_WHY];
    char note[512];
    const char *repo;
    const char *table;
    FILE *f;
    int arm;
    int cnt_fail = 0;
    int table_empty = 0;
    int nall = 0, nsm = 0, nd = 0, nx = 0;
    int green;
    int rc = 1;

    if (config_path == NULL) {
        fprintf(stderr, "jichi-harness: gate run --stage verdict: "
                        "--config <path> is required\n");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        fprintf(stderr, "jichi-harness: gate run --stage verdict: %s\n",
                msg != NULL ? msg : "could not read the harness config");
        return 2;
    }
    /* The repo resolution (flag -> host.pushRepo -> pushHome/push.git). */
    repo = (push_repo != NULL && push_repo[0] != '\0')
               ? push_repo
               : (cfg.host_push_repo != NULL &&
                  cfg.host_push_repo[0] != '\0')
                     ? cfg.host_push_repo : NULL;
    if (repo == NULL && cfg.host_push_home != NULL &&
        cfg.host_push_home[0] != '\0') {
        char home[256];
        int n = jc_snprintf(home, sizeof home, "%s/push.git",
                            cfg.host_push_home);
        if (n >= 0 && (size_t)n < sizeof home) {
            repo = jc_arena_strdup(a, home);
        }
    }
    if (repo == NULL) {
        /* The reference's missing-repo behaviour: the count's git calls
         * fail on a path that is not a repo. */
        repo = jc_arena_strdup(a, "/.jichi-gv-no-push-repo");
        if (repo == NULL) {
            fprintf(stderr, "jichi-harness: gate run --stage verdict: out"
                            " of memory\n");
            return 1;
        }
    }
    jc_sb_init(&text);
    jc_vec_init(&union_v, sizeof (const char *));
    gerr[0] = '\0';
    f = fopen(file, "rb");
    if (f != NULL) {
        char chunk[8192];
        size_t r;
        int fail = 0;

        while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) {
            if (jc_sb_append_n(&text, chunk, (jc_size)r) != JC_OK) {
                fail = 1;
                break;
            }
        }
        fclose(f);
        if (fail) {
            jc_sb_free(&text);
            jc_vec_free(&union_v);
            return 1;
        }
    }
    jc_hp_gv_classify(text.data != NULL ? text.data : "", s, &cls);
    if (cls.mut_ok) {
        arm = GV_MUT_OK;      /* :21 the OK line: clean, no second count */
    } else if (cls.mut_none) {
        arm = GV_MUT_ARM;     /* :22 the no-changed-drivers line */
    } else {
        arm = GV_MUT_MISSING; /* :48-49 the plain reason */
    }
    if (arm == GV_MUT_ARM) {
        const char *a_diff[5];
        const char *a_tree[5];
        const char *a_msw[2];
        int n;
        int r1, r2, r3;

        /* :32-33 the three counting children (the reference's own argv):
         * the diff, the diff-tree, AND the show of the commit's own
         * scripts/mutant-sweep.sh (M1187-b: the EXCLUDE table is shared,
         * read from the commit, not re-typed). */
        a_diff[0] = "diff";
        a_diff[1] = "--name-only";
        a_diff[2] = cls.mut_mb;
        a_diff[3] = s;
        a_diff[4] = "--";
        a_tree[0] = "diff-tree";
        a_tree[1] = "--no-commit-id";
        a_tree[2] = "--name-only";
        a_tree[3] = "-r";
        a_tree[4] = s;
        n = jc_snprintf(mswcap, sizeof mswcap, "%s:scripts/mutant-sweep.sh",
                        s);
        if (n < 0 || (size_t)n >= sizeof mswcap) {
            mswcap[0] = '\0';
        }
        a_msw[0] = "show";
        a_msw[1] = mswcap;
        r1 = gv_git(repo, a_diff, 5, allcap, (int)sizeof allcap);
        r2 = gv_git(repo, a_tree, 5, owncap, (int)sizeof owncap);
        mswout[0] = '\0';
        r3 = gv_git(repo, a_msw, 2, mswout, (int)sizeof mswout);
        if (r1 != 0 || r2 != 0 || r3 != 0) {
            cnt_fail = 1;   /* :65-67 a git error is red, never a skip */
        } else {
            int i;
            const char *tp;

            /* :35-36 the EXCLUDE table, from the commit's own
             * scripts/mutant-sweep.sh (byte-for-byte the reference's
             * awk; a NO table in the text reads as empty, the table_empty
             * arm -- never a silent green). */
            if (jc_hp_gv_excl_parse(mswout, a, (char **)&table) != 0) {
                jc_sb_free(&text);
                jc_vec_free(&union_v);
                return 1;
            }
            table_empty = 1;
            for (tp = table; *tp != '\0'; tp++) {
                if (*tp != ' ') {
                    table_empty = 0;
                    break;
                }
            }
            nall = gv_count_names(allcap);   /* :42 the diff ALONE */
            if (jc_hp_gv_smoke_union(allcap, owncap, a, &union_v) != 0) {
                jc_sb_free(&text);
                jc_vec_free(&union_v);
                return 1;
            }
            /* :43-50 the union's loop, the reference's order: nsm for
             * every union script / the _smoke|run skip / the cat-file -e
             * deletion check / the show + gerr / uses_bin FIRST / THEN
             * the table membership (nx) or nd. */
            for (i = 0; i < (int)union_v.len; i++) {
                const char *d = *(const char **)jc_vec_at(&union_v, i);
                const char *base;
                char stem[128];
                size_t blen;
                char path[GV_CAP_PATH];
                const char *a_cf[3];
                const char *a_sh[2];
                char tmp[64];
                int n;

                nsm++;
                base = strrchr(d, '/');
                base = (base != NULL) ? base + 1 : d;
                blen = strlen(base) - 3;   /* the .sh suffix */
                /* :49 the table matches `basename "$d" .sh` (stem = the
                 * basename WITHOUT the .sh), so copy it. */
                memcpy(stem, base, blen);
                stem[blen] = '\0';
                if ((blen == 6 && memcmp(base, "_smoke", 6) == 0) ||
                    (blen == 3 && memcmp(base, "run", 3) == 0)) {
                    continue;
                }
                n = jc_snprintf(path, sizeof path, "%s:%s", s, d);
                if (n < 0 || (size_t)n >= sizeof path) {
                    break;
                }
                a_cf[0] = "cat-file";
                a_cf[1] = "-e";
                a_cf[2] = path;
                tmp[0] = '\0';
                if (gv_git(repo, a_cf, 3, tmp, (int)sizeof tmp) != 0) {
                    continue;   /* :46 deleted at the commit: not swept */
                }
                a_sh[0] = "show";
                a_sh[1] = path;
                if (gv_git(repo, a_sh, 2, body, (int)sizeof body) != 0) {
                    n = jc_snprintf(gerr + strlen(gerr),
                                    sizeof gerr - strlen(gerr), " %s", d);
                    if (n < 0 || (size_t)n >= sizeof gerr - strlen(gerr)) {
                        break;
                    }
                    continue;   /* :47 a git error reading a driver */
                }
                if (jc_hp_gv_uses_bin(body)) {
                    /* :49 the table matches `basename "$d" .sh` = stem. */
                    if (jc_hp_gv_excl_contains(table, stem)) {
                        nx++;   /* :49 in the EXCLUDE table: skipped */
                    } else {
                        nd++;   /* :50 swept: invokes the binary */
                    }
                }
            }
        }
    }
    green = jc_hp_gv_verdict(&cls, s, arm, cnt_fail, gerr, table_empty,
                             nall, nsm, nd, nx,
                             why, sizeof why, note, sizeof note);
    if (green) {
        /* :57 `echo "green$note"` */
        printf("green%s\n", note);
        rc = 0;
    } else {
        /* :58 `echo "red:${why#;}"` */
        printf("red:%s\n", why[0] == ';' ? why + 1 : why);
        rc = 1;
    }
    jc_sb_free(&text);
    jc_vec_free(&union_v);
    return rc;
}
