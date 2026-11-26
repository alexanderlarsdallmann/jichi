/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateref.c - the `gate run --stage redfirst` body (M1157, batch H9b,
 * wave 3, task 9): the C89 port of the 2026-10-27 hardening-host drive's
 * redfirst.sh, read line by line. See the .h for the contract. The line-
 * assembly + the SELECTION + the VERDICTS are PURE (no git, no I/O) and
 * exported through jc_hp_gateref.h, unit-tested by tests/test_gateref.c.
 *
 * CHANGED set (every behaviour kept unless named):
 *   :19-20  -- the reference's shell-unset API keys become the keyless child
 *              env (the sweep/ci 16-name idiom; children take it explicitly).
 *   :23     -- the `trap 'rm -rf "$W"' EXIT` is DROPPED (nothing removed);
 *              the scratch clone + out files stay under --work.
 *   :18     -- `P=$H/push.git` becomes --push-repo (else host.pushRepo).
 *   :16     -- `BASE HEAD OUTDIR` positionals become `BASE HEAD` + --out /
 *              --records / --work flags.
 *   :36-41  -- the build/check surface is shimmable (--redfirst-run /
 *              --redfirst-unit-run); the reference inlines the make + sh forms.
 *
 * mkdtemp is POSIX-2008: the build-wide -D_POSIX_C_SOURCE=200112L does NOT
 * declare it (the M1134 / T5 / T8 idiom: the per-TU _XOPEN_SOURCE 700 guard
 * BEFORE the first header that reaches features.h). */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#elif _XOPEN_SOURCE < 700
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
/* M1177: <time.h> FIRST, before any header that can pull in <sys/stat.h>
 * (<fcntl.h> does, on glibc 2.11). With _XOPEN_SOURCE 700, glibc 2.11-2.24's
 * <sys/stat.h> declares struct timespec fields (st_atim, ...) but includes
 * <time.h> only for time_t, so timespec stays incomplete and the build failed on
 * Debian 6-9 and CentOS 6-7 (the userland ladder, 2026-11-13). A full <time.h>
 * first defines it; newer glibcs, musl and the BSDs define it either way. */
#include <time.h>

#include "jc_hp_gateref.h"

#include <ctype.h>   /* tolower (the SKIP match's case-insensitive prefix) */
#include <dirent.h>  /* opendir / readdir / closedir (the scratch clear walk) */
#include <errno.h>   /* EEXIST (gref_mkdir_p) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h> /* WIFEXITED / WEXITSTATUS (the run child) */
#include <unistd.h>   /* close (the per-check output file's fd) */

#include "jc_fcntl.h" /* JC_O_BINARY (the per-check output file's flags) */
#include "jc_spawn.h" /* jc_spawn (the per-check run child, fd to the file) */

#include "jc_harness_config.h" /* the config load (the host block's fields) */
#include "jc_mem.h"            /* jc_arena_strdup */
#include "jc_platform.h"       /* jc_size, JC_OK, jc_shell_path */
#include "jc_proc.h"           /* jc_proc_capture (the keyless child-runner) */
#include "jc_snprintf.h"       /* jc_snprintf, never sprintf */
#include "jc_str.h"            /* struct jc_sb */
#include "jc_vec.h"            /* struct jc_vec */

#define GREF_LINE_MAX 8192
#define GREF_CAP_TEXT  (1024 * 1024) /* a command's combined output */

/* The API-key envs the reference unsets (redfirst.sh:19-20); the port sets
 * them EMPTY in each spawn's CHILD env (the sweep/ci 16-name idiom). */
static const char *const gref_keys[] = {
    "JICHI_API_KEY", "JLU_API_KEY", "ANTHROPIC_API_KEY", "OPENAI_API_KEY",
    "OPENROUTER_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY",
    "MISTRAL_API_KEY", "DEEPSEEK_API_KEY", "TOGETHER_API_KEY",
    "FIREWORKS_API_KEY", "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
    "ZD_TOKEN_FILE"
};
#define GREF_NKEYS (int)(sizeof gref_keys / sizeof gref_keys[0])

/* ---------------------------------------------------------------------------
 * Pure text helpers (bounded -- every walk stops at the NUL).
 * ----------------------------------------------------------------------- */

/* The count of lines of `text` that START with `prefix` (the reference's
 * `grep -c '^not ok'` / `grep -c '^ok'`). The last line counts when it is a
 * non-empty run with no trailing newline. */
static int
gref_count_prefix(const char *text, const char *prefix)
{
    const char *p = text;
    const char *pl = text;
    size_t plen = strlen(prefix);
    int n = 0;

    for (;;) {
        if (p[0] == '\n' || p[0] == '\0') {
            if (p > pl && strncmp(pl, prefix, plen) == 0) {
                n++;
            }
            pl = p + 1;
        }
        if (p[0] == '\0') {
            break;
        }
        p++;
    }
    return n;
}

/* The LAST line of `text` that CONTAINS `substr` (the reference's
 * `grep -E '[0-9]+ checks, [0-9]+ failures' | tail -1`); "" for none. The
 * match may span the NUL-terminated end. `out` is NUL-terminated, cut to
 * `cap` (including the NUL). */
static const char *
gref_last_line_containing(const char *text, const char *substr,
                          char *out, size_t cap)
{
    const char *line_start = text;
    const char *p = text;

    out[0] = '\0';
    if (cap == 0 || substr[0] == '\0' || text[0] == '\0') {
        return out;
    }
    for (;;) {
        int at_end = (p[0] == '\0');
        if (strstr(line_start, substr) != NULL) {
            size_t len = at_end ? strlen(line_start) : (size_t)(p - line_start);
            if (len >= cap) {
                len = cap - 1;
            }
            memcpy(out, line_start, len);
            out[len] = '\0';
        }
        if (at_end) {
            break;
        }
        if (p[0] == '\n') {
            line_start = p + 1;
        }
        p++;
    }
    return out;
}

/* 1 when `text` has a SKIP line (the reference's :43 `grep -q -i -E
 * '^1\.\.0 *# *skip|# SKIP'`): a line whose (possibly lower-case) prefix is
 * "1..0" followed by ` *# *skip`, OR a line starting with "# SKIP" (the case
 * is insensitive on the first form; the second is matched literally). */
static int
gref_skip_match(const char *text)
{
    const char *p = text;
    const char *pl = text;

    for (;;) {
        int at_end = (p[0] == '\0');
        const char *line = pl;
        size_t ln = at_end ? strlen(line) : (size_t)(p - line);
        /* a case-insensitive "1..0" prefix, then optional spaces, '#',
         * optional spaces, "skip" (the first alternation). */
        if (ln >= 4 && tolower((unsigned char)line[0]) == '1' &&
            line[1] == '.' && line[2] == '.' &&
            tolower((unsigned char)line[3]) == '0') {
            const char *q = line + 4;
            while ((size_t)(q - line) < ln && *q == ' ') {
                q++;
            }
            if ((size_t)(q - line) < ln && *q == '#') {
                q++;
                while ((size_t)(q - line) < ln && *q == ' ') {
                    q++;
                }
                if ((size_t)(q - line) + 4 <= ln &&
                    tolower((unsigned char)q[0]) == 's' &&
                    tolower((unsigned char)q[1]) == 'k' &&
                    tolower((unsigned char)q[2]) == 'i' &&
                    tolower((unsigned char)q[3]) == 'p') {
                    return 1;
                }
            }
        }
        /* the second alternation: a line starting "# SKIP". */
        if (ln >= 6 && line[0] == '#' && line[1] == ' ' &&
            line[2] == 'S' && line[3] == 'K' && line[4] == 'I' &&
            line[5] == 'P') {
            return 1;
        }
        if (at_end) {
            break;
        }
        if (p[0] == '\n') {
            pl = p + 1;
        }
        p++;
    }
    return 0;
}

/* Append `n` bytes of `tok` to the space-joined builder `sb` (a leading space
 * when non-empty). 0 on success. */
static int
gref_join_n(struct jc_sb *sb, const char *tok, size_t n)
{
    if (sb->len > 0) {
        if (jc_sb_append_char(sb, ' ') != JC_OK) {
            return -1;
        }
    }
    if (n > 0 && jc_sb_append_n(sb, tok, (jc_size)n) != JC_OK) {
        return -1;
    }
    return 0;
}

/* --- the SELECTION (redfirst.sh:28-33, the reference's rules byte for byte) -
 *   :29  other = the first file NOT under tests/ or docs/internal/ (head -1);
 *   :31  drv   = the lines matching ^tests/smoke/[^/]+\.sh$ MINUS the lines
 *                matching /lib[^/]*\.sh$, /common[^/]*\.sh$, /run\.sh$ or
 *                /_<name>\.sh$ (the reference's two-grep form);
 *   :32  unit  = the FIRST line matching ^tests/test_[^/]+\.c$ (head -1);
 *   :56  the changed: list = ALL lines matching ^tests/test_ (tr '\n' ' ').
 * A commit is RED-FIRST iff other is empty AND (drv non-empty OR unit
 * non-empty). The file-list order is preserved.
 * ----------------------------------------------------------------------- */

/* :29 the exclusion: 1 when the line is NOT under tests/ or docs/internal/. */
static int
gref_file_is_other(const char *line)
{
    return !(strncmp(line, "tests/", 6) == 0 ||
             strncmp(line, "docs/internal/", 14) == 0);
}

/* :31a the driver candidate: 1 when the line matches ^tests/smoke/[^/]+\.sh$
 * (a single component after tests/smoke/, ending in .sh). */
static int
gref_is_driver_path(const char *line)
{
    const char *rest;
    size_t len;

    if (strncmp(line, "tests/smoke/", 12) != 0) {
        return 0;
    }
    rest = line + 12;
    if (rest[0] == '\0' || strchr(rest, '/') != NULL) {
        return 0;
    }
    len = strlen(rest);
    if (len < 4 || strncmp(rest + len - 3, ".sh", 3) != 0) {
        return 0;
    }
    return 1;
}

/* :31b the driver exclusions: 1 when the line matches the reference's
 * `/(lib|common)[^/]*\.sh$|/run\.sh$|/_[^/]*\.sh$` -- a /lib or /common path
 * component (with a .sh tail anywhere after it), a basename of run.sh, or a
 * _-prefixed basename ending in .sh. */
static int
gref_driver_is_excluded(const char *line)
{
    const char *base;
    size_t len;
    int is_sh;

    len = strlen(line);
    is_sh = len >= 3 && strncmp(line + len - 3, ".sh", 3) == 0;
    base = line + len;
    while (base > line && base[-1] != '/') {
        base--;
    }
    if (base[0] == '_' && is_sh) {
        return 1;
    }
    if (strcmp(base, "run.sh") == 0) {
        return 1;
    }
    /* /(lib|common)[^/]*\.sh$ : a "/lib" or "/common" with NO other slash
     * between it and the line end, ending in .sh ([^/]* may be empty). */
    {
        const char *hit;
        const char *suf;
        size_t slen;
        hit = strstr(line, "/lib");
        if (hit != NULL && strstr(hit + 4, "/") == NULL) {
            suf = hit + 4;
            slen = strlen(suf);
            if (slen >= 3 && strncmp(suf + slen - 3, ".sh", 3) == 0) {
                return 1;
            }
        }
        hit = strstr(line, "/common");
        if (hit != NULL && strstr(hit + 7, "/") == NULL) {
            suf = hit + 7;
            slen = strlen(suf);
            if (slen >= 3 && strncmp(suf + slen - 3, ".sh", 3) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* jc_gateref_select (redfirst.sh:28-33): classify the file list (one path per
 * line) by the reference's greps. The drivers_out / all_units_out /
 * first_unit_out are space-joined (drivers: basenames without .sh, in file
 * order; units: full paths) and first_unit is the first test path. An
 * over-cap list is CLAMPED (cut to cap-1), not an error: 0 is returned on
 * success (clamped or not); only a NULL argument or an OOM returns -1. */
int
jc_gateref_select(const char *file_list, char *drivers_out, size_t drv_cap,
                  char *first_unit_out, size_t fu_cap,
                  char *all_units_out, size_t au_cap, int *redfirst)
{
    struct jc_sb drv;
    struct jc_sb au;
    char fu[2048];
    const char *p;
    int other = 0;
    int qual = 0;

    if (file_list == NULL || drivers_out == NULL ||
        first_unit_out == NULL || all_units_out == NULL || redfirst == NULL) {
        return -1;
    }
    jc_sb_init(&drv);
    jc_sb_init(&au);
    fu[0] = '\0';
    p = file_list;
    while (*p != '\0') {
        size_t len = 0;
        char buf[2048];
        while (p[len] != '\0' && p[len] != '\n') {
            len++;
        }
        if (len > 0) {
            const char *base;
            size_t blen;
            if (len >= sizeof buf) {
                len = sizeof buf - 1;
            }
            memcpy(buf, p, len);
            buf[len] = '\0';
            if (gref_file_is_other(buf)) {
                other = 1;
            }
            if (gref_is_driver_path(buf) &&
                !gref_driver_is_excluded(buf)) {
                base = buf + len;
                while (base > buf && base[-1] != '/') {
                    base--;
                }
                blen = strlen(base);
                if (blen > 3 && strncmp(base + blen - 3, ".sh", 3) == 0) {
                    blen -= 3;
                }
                if (gref_join_n(&drv, base, blen) != 0) {
                    jc_sb_free(&drv);
                    jc_sb_free(&au);
                    return -1;
                }
                qual = 1;
            }
            if (strncmp(buf, "tests/test_", 11) == 0) {
                if (fu[0] == '\0') {
                    size_t flen = strlen(buf);
                    if (flen >= sizeof fu) {
                        flen = sizeof fu - 1;
                    }
                    memcpy(fu, buf, flen);
                    fu[flen] = '\0';
                    qual = 1;
                }
                if (gref_join_n(&au, buf, strlen(buf)) != 0) {
                    jc_sb_free(&drv);
                    jc_sb_free(&au);
                    return -1;
                }
            }
        }
        if (p[len] == '\n') {
            p += len + 1;
        } else {
            break;
        }
    }
    /* copy the builders into the caller's buffers */
    {
        size_t dl = drv.len;
        size_t al = au.len;
        size_t fl = fu_cap > 0 ? strlen(fu) : 0;
        if (dl >= drv_cap) {
            dl = drv_cap - 1;
        }
        if (al >= au_cap) {
            al = au_cap - 1;
        }
        if (fl >= fu_cap) {
            fl = fu_cap - 1;
        }
        if (drv.data != NULL) {
            memcpy(drivers_out, drv.data, dl);
        } else {
            drivers_out[0] = '\0';
        }
        drivers_out[dl] = '\0';
        if (fu_cap > 0) {
            memcpy(first_unit_out, fu, fl);
            first_unit_out[fl] = '\0';
        }
        if (au.data != NULL) {
            memcpy(all_units_out, au.data, al);
        } else {
            all_units_out[0] = '\0';
        }
        all_units_out[al] = '\0';
    }
    *redfirst = (other == 0 && qual != 0) ? 1 : 0;
    jc_sb_free(&drv);
    jc_sb_free(&au);
    return 0;
}

/* jc_gateref_driver_verdict (redfirst.sh:43-46). ok/no are the grep -c of
 * `^ok` / `^not ok`. SKIP: a skip line AND no == 0 AND ok == 0. RED: rc != 0
 * OR no > 0. GREEN: otherwise. */
int
jc_gateref_driver_verdict(const char *text, int rc, char *word_out,
                          size_t word_cap, int *ok_out, int *no_out)
{
    int ok;
    int no;
    int n_w;

    if (text == NULL || word_out == NULL || ok_out == NULL || no_out == NULL ||
        word_cap == 0) {
        return -1;
    }
    ok = gref_count_prefix(text, "ok");
    no = gref_count_prefix(text, "not ok");
    if (gref_skip_match(text) && no == 0 && ok == 0) {
        n_w = jc_snprintf(word_out, word_cap, "SKIP");
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    } else if (rc != 0 || no > 0) {
        n_w = jc_snprintf(word_out, word_cap, "RED");
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    } else {
        n_w = jc_snprintf(word_out, word_cap, "GREEN");
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    }
    *ok_out = ok;
    *no_out = no;
    return 0;
}

/* jc_gateref_unit_verdict (redfirst.sh:51-55). sm is the last
 * `[0-9]+ checks, [0-9]+ failures` line ("" when none); f is its failure
 * count (the reference's sed extract). RED: f > 0. GREEN: f == 0. NO SUMMARY
 * (counted RED): sm empty -- the reference's "the suite did not finish". */
int
jc_gateref_unit_verdict(const char *text, int rc, char *word_out,
                        size_t word_cap, char *sm_out, size_t sm_cap)
{
    char sm[1024];
    int f = 0;
    int n_w;

    if (text == NULL || word_out == NULL || sm_out == NULL || word_cap == 0) {
        return -1;
    }
    (void)gref_last_line_containing(text, " checks, ", sm, sizeof sm);
    /* the reference's grep is `[0-9]+ checks, [0-9]+ failures` -- require the
     * "failures" tail too (a "checks," line without it is not a summary). */
    if (strstr(sm, "failures") == NULL) {
        sm[0] = '\0';
    }
    if (sm[0] != '\0') {
        /* the reference's sed: `.* \([0-9][0-9]*\) failures.*` -- the digits
         * IMMEDIATELY BEFORE " failures". Walk backward from the "failures"
         * word over the digits. */
        const char *fl = strstr(sm, " failures");
        if (fl == NULL) {
            fl = strstr(sm, "failures");
        }
        if (fl != NULL) {
            const char *e = fl;
            const char *d = fl;
            while (d > sm && d[-1] >= '0' && d[-1] <= '9') {
                d--;
            }
            if (d < e) { /* at least one digit before "failures" */
                f = 0;
                for (; d < e; d++) {
                    f = f * 10 + (*d - '0');
                }
            }
        }
    }
    if (sm_cap > 0) {
        size_t sl = strlen(sm);
        if (sl >= sm_cap) {
            sl = sm_cap - 1;
        }
        memcpy(sm_out, sm, sl);
        sm_out[sl] = '\0';
    }
    if (sm[0] == '\0') {
        n_w = jc_snprintf(word_out, word_cap,
                          "NO SUMMARY (rc=%d: the suite did not finish -- "
                          "a crash or a build error)", rc);
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    } else if (f > 0) {
        n_w = jc_snprintf(word_out, word_cap, "RED");
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    } else {
        n_w = jc_snprintf(word_out, word_cap, "GREEN");
        if (n_w < 0 || n_w >= (int)word_cap) {
            return -1;
        }
    }
    return 0;
}

/* --- the LINE ASSEMBLERS (the reference's printf bytes) --------------------- */

int
jc_gateref_buildfail_line(const char *s8, const char *subj,
                          const char *out,
                          char *line, size_t cap)
{
    if (s8 == NULL || subj == NULL || out == NULL || line == NULL) {
        return -1;
    }
    return jc_snprintf(line, cap, "%s BUILD FAILED at the red-first commit "
                               "(%s): see %s/%s-build.log\n",
                       s8, subj, out, s8);
}

int
jc_gateref_checkoutfail_line(const char *s8, char *line, size_t cap)
{
    if (s8 == NULL || line == NULL) {
        return -1;
    }
    return jc_snprintf(line, cap, "%s CHECKOUT FAILED\n", s8);
}

int
jc_gateref_driver_line(const char *s8, const char *word, const char *b,
                       int rc, int ok, int no, const char *subj,
                       char *line, size_t cap)
{
    if (s8 == NULL || word == NULL || b == NULL || subj == NULL ||
        line == NULL) {
        return -1;
    }
    return jc_snprintf(line, cap, "%s %s driver %s (rc=%d, %d ok, %d "
                               "not ok) -- %s\n",
                       s8, word, b, rc, ok, no, subj);
}

int
jc_gateref_unit_line(const char *s8, const char *word, const char *sm,
                     const char *changed, const char *subj,
                     char *line, size_t cap)
{
    if (s8 == NULL || word == NULL || sm == NULL || changed == NULL ||
        subj == NULL || line == NULL) {
        return -1;
    }
    /* The reference (redfirst.sh:56) builds the changed list with
     * `tr '\n' ' '`, which turns the LAST file's trailing newline into a
     * space, so the line is `... changed: <f1> <f2> )` -- a space BEFORE the
     * closing paren. The unit line is only emitted when a unit test was
     * selected (the changed list is non-empty), so the space is always
     * present. Match the reference byte for byte. */
    return jc_snprintf(line, cap, "%s %s unit suite (%s; changed: %s ) -- %s\n",
                       s8, word, sm, changed, subj);
}

int
jc_gateref_summary_line(int n, int nr, int ng, int ns,
                        char *line, size_t cap)
{
    if (line == NULL) {
        return -1;
    }
    return jc_snprintf(line, cap, "redfirst: %d red-first commit(s), %d "
                               "red, %d green, %d skip\n",
                       n, nr, ng, ns);
}

int
jc_gateref_records_line(const char *dir, char *line, size_t cap)
{
    if (dir == NULL || line == NULL) {
        return -1;
    }
    return jc_snprintf(line, cap, "records: %s\n", dir);
}

/* ---------------------------------------------------------------------------
 * The stage body (git children + the per-check spawns).
 * ----------------------------------------------------------------------- */

/* gref_mkdir_p: mkdir -p (the sweep's idiom). 0 on success. */
static int
gref_mkdir_p(const char *dir)
{
    char buf[4096];
    size_t n;
    int i;

    n = strlen(dir);
    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, dir, n + 1);
    if (buf[0] == '\0') {
        return -1;
    }
    for (i = 1; i <= (int)n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char save = buf[i];
            buf[i] = '\0';
            if (strlen(buf) > 1 && mkdir(buf, 0777) != 0 &&
                errno != EEXIST) {
                return -1;
            }
            buf[i] = save;
        }
    }
    return 0;
}

/* The stage scratch clear (M1159 B2 F3, the C-b idiom from the sweep stage):
 * the bounded recursive walk that empties a stage-scratch dir. Only the
 * redfirst stage's OWN scratch -- the clone DESTINATION `j`, and the
 * `out`/`records`/`tmp` homes under `--work` when they are the stage's
 * defaults -- is ever passed in. 0 when the dir is empty afterwards (or was
 * absent), -1 on a walk / permission fault (a non-empty rmdir is such a
 * fault: the caller treats it as a stage setup fault). Termination: the node
 * + depth caps bound the recursion; the per-level rmdir loop is itself capped
 * (a dir that never empties is a fault, not a hang). lstat-first so symlinks
 * are never followed (a symlink-to-dir unlinks as a link, target untouched);
 * children are removed in reverse name order so one pass always finishes. */
#define GREF_CLEAR_DEPTH  32
#define GREF_CLEAR_NODES  262144
#define GREF_CLEAR_NAMES  256    /* one dir level (a fresh scratch has <= 4) */

static int
gref_clear_rec(const char *dir, int depth, int *nodes)
{
    DIR *dp;
    struct dirent *e;
    char names[GREF_CLEAR_NAMES][256];
    int nn = 0;
    int i;

    if (depth > GREF_CLEAR_DEPTH) {
        return -1;
    }
    dp = opendir(dir);
    if (dp == NULL) {
        if (errno == ENOENT) {
            return 0;  /* absent dir: nothing to clear */
        }
        return -1;
    }
    while ((e = readdir(dp)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 ||
            strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (nn >= GREF_CLEAR_NAMES) {
            closedir(dp);
            return -1;
        }
        memcpy(names[nn], e->d_name, strlen(e->d_name) + 1);
        nn++;
    }
    closedir(dp);
    for (i = nn - 1; i >= 0; i--) {
        char epath[2048];
        struct stat st;
        int attempts;
        int n_ep;

        if (*nodes <= 0) {
            return -1;
        }
        (*nodes)++;
        n_ep = jc_snprintf(epath, sizeof epath, "%s/%s", dir, names[i]);
        if (n_ep < 0 || n_ep >= (int)sizeof epath) {
            return -1;
        }
        if (lstat(epath, &st) != 0) {
            if (errno == ENOENT) {
                continue;   /* vanished under us: not a fault */
            }
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            if (gref_clear_rec(epath, depth + 1, nodes) != 0) {
                return -1;
            }
            attempts = 0;
            while (attempts < 4 && rmdir(epath) != 0) {
                if (errno != EEXIST) {
                    return -1;
                }
                attempts++;
            }
            if (attempts == 4) {
                return -1;   /* still non-empty */
            }
        } else if (unlink(epath) != 0 && errno != ENOENT) {
            return -1;
        }
    }
    return 0;
}

/* Clear the stage scratch `dir` (0 absent, 0 cleared, -1 fault). */
static int
gref_clear_dir(const char *dir)
{
    struct stat st;
    int nodes = GREF_CLEAR_NODES;

    if (stat(dir, &st) != 0) {
        return (errno == ENOENT) ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    return gref_clear_rec(dir, 0, &nodes);
}

/* gref_read_file: read a whole file into `out` (cap-bounded, NUL-terminated).
 * 0 on success (a missing file yields "" + 0), -1 on a read fault. */
static int
gref_read_file(const char *path, char *out, int cap)
{
    FILE *f;
    int n;

    out[0] = '\0';
    if (cap <= 0) {
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0; /* a missing per-check file is not a stage fault */
    }
    n = 0;
    while (n < cap - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

/* gref_hex8: 7-40 lowercase hex (the sweep/ci SHA8 idiom). */
static int
gref_hex8(const char *s)
{
    size_t i;
    if (s == NULL) {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') ||
              (s[i] >= 'a' && s[i] <= 'f'))) {
            return 0;
        }
    }
    return i >= 7 && i <= 40 ? 1 : 0;
}

/* gref_subst: replace every literal occurrence of the tokens <s8>, <d>,
 * <o>, <b>, <subj> in `tpl` with the given values (the shimmable-surface
 * substitution; a token with an empty value is replaced by nothing). Returns
 * the byte count (jc_snprintf semantics) or -1 on truncation. */
static int
gref_subst(char *out, size_t cap, const char *tpl,
           const char *s8, const char *d, const char *o,
           const char *b, const char *subj)
{
    struct jc_sb sb;
    const char *p = tpl;
    int total = 0;

    if (out == NULL || tpl == NULL || cap == 0) {
        return -1;
    }
    jc_sb_init(&sb);
    while (*p != '\0') {
        /* PREFIX match (strncmp), not strcmp: a token is followed by the rest
         * of the template (`<d> <o> <b>`), so strcmp(p, "<d>") is never 0 and
         * the literal "<d>" would be copied into the child's command, which
         * dash then parses as a redirection (the "redirection unexpected"
         * fault that hung the per-check replay). The token lengths: <s8> = 4,
         * <d>/<o>/<b> = 3, <subj> = 6. */
        if (p[0] == '<' && p[1] != '\0') {
            if (strncmp(p, "<s8>", 4) == 0) {
                jc_sb_append(&sb, s8 != NULL ? s8 : "");
                p += 4;
            } else if (strncmp(p, "<d>", 3) == 0) {
                jc_sb_append(&sb, d != NULL ? d : "");
                p += 3;
            } else if (strncmp(p, "<o>", 3) == 0) {
                jc_sb_append(&sb, o != NULL ? o : "");
                p += 3;
            } else if (strncmp(p, "<b>", 3) == 0) {
                jc_sb_append(&sb, b != NULL ? b : "");
                p += 3;
            } else if (strncmp(p, "<subj>", 6) == 0) {
                jc_sb_append(&sb, subj != NULL ? subj : "");
                p += 6;
            } else {
                jc_sb_append_char(&sb, *p);
                p++;
            }
        } else {
            jc_sb_append_char(&sb, *p);
            p++;
        }
    }
    if (sb.data != NULL) {
        total = (int)sb.len;
        if (total > (int)cap - 1) {
            total = (int)cap - 1;
        }
        memcpy(out, sb.data, (size_t)total);
    }
    out[total] = '\0';
    jc_sb_free(&sb);
    return total;
}

/* gref_run: ONE keyless shell child (`sh -c <cmd>`), the sweep's gsw_run
 * shape: workdir is prefixed as a cd, the 16 key names set EMPTY in the child
 * env, combined stdout+stderr captured into `out` (cap-bounded, NUL-
 * terminated), a 600s bound (a git read). Returns the child's exit status,
 * -1 on a spawn fault. */
static int
gref_run(const char *workdir, const char *cmd, int cap, char *out)
{
    char *argv[4];
    char slot[GREF_NKEYS][128];
    struct jc_vec envv;
    struct jc_sb capture;
    /* At FUNCTION scope: argv[2] points at it until the child is spawned, far
     * below the block that fills it (declared in that block it was used after
     * its scope; clang -O2 reused the slot and the shell got other bytes). */
    char joined[16384];
    int i;
    int rc;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    if (workdir != NULL && workdir[0] != '\0') {
        if (jc_gateref_build_cmd(workdir, cmd, joined, sizeof joined) < 0) {
            return -1;
        }
        cmd = joined;
    }
    argv[0] = (char *)jc_shell_path();
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;
    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < GREF_NKEYS; i++) {
        char *s;
        int n_sk = jc_snprintf(slot[i], sizeof slot[i], "%s=", gref_keys[i]);
        if (n_sk < 0 || n_sk >= (int)sizeof slot[i]) {
            jc_vec_free(&envv);
            return -1;
        }
        s = slot[i];
        if (jc_vec_push(&envv, &s) != JC_OK) {
            jc_vec_free(&envv);
            return -1;
        }
    }
    jc_sb_init(&capture);
    rc = (int)jc_proc_capture(argv, &envv, NULL, &capture, (jc_size)cap,
                              600L, NULL);
    jc_vec_free(&envv);
    if (out != NULL && cap > 0) {
        int n = (int)capture.len;
        if (n > cap - 1) {
            n = cap - 1;
        }
        if (capture.data != NULL && n > 0) {
            memcpy(out, capture.data, (size_t)n);
        }
        out[n] = '\0';
    }
    jc_sb_free(&capture);
    return rc;
}

/* Strip the trailing newline(s) of a git read capture (in place). */
static void
gref_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/* gref_child_outfd: ONE keyless shell child (`sh -c <cmd>`), workdir = the
 * clone, stdin /dev/null, stdout+stderr -> `outfd` (the reference's
 * `> <file> 2>&1`), the 16 key names set EMPTY in the child env. UNCAPPED
 * reap (the reference's no-deadline; the port's child IS the per-check
 * process). Returns the child's exit status, -1 on a spawn fault. */
static int
gref_child_outfd(const char *workdir, const char *cmd, int outfd,
                 const struct jc_vec *env)
{
    char *argv[4];
    struct jc_spawn_req req;
    pid_t pid = 0;
    int status;

    argv[0] = (char *)jc_shell_path();
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;
    memset(&req, 0, sizeof req);
    req.argv = argv;
    req.fd_stdin = JC_SPAWN_DEVNULL;
    req.fd_stdout = outfd;
    req.fd_stderr = outfd;
    req.scrub_env = 1;
    req.env = env;
    req.cwd = (char *)workdir;
    if (jc_spawn(&req, &pid) != JC_OK) {
        return -1;
    }
    if (jc_proc_reap_bounded(pid, 0.0, NULL, &status) != 0) {
        return -1;
    }
    if (!WIFEXITED(status)) {
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}

/* gref_record: append a line to the record (the stage's assembled stdout).
 * 0 on success, -1 on OOM. */
static int
gref_record(struct jc_sb *rec, const char *line)
{
    if (rec == NULL || line == NULL) {
        return -1;
    }
    if (jc_sb_append(rec, line) != JC_OK) {
        return -1;
    }
    return 0;
}

/* gref_env: build the run child's env vector into `out` (the 16 key names set
 * EMPTY, each a "KEY=" entry arena-allocated in `a`, plus any `extra`
 * "KEY=VALUE" entries, each arena-allocated). 0 on success, -1 on OOM. */
static int
gref_env(struct jc_arena *a, struct jc_vec *out,
         const char *extra[], int n_extra)
{
    int i;
    char slot[128];

    if (a == NULL || out == NULL) {
        return -1;
    }
    jc_vec_init(out, sizeof(char *));
    for (i = 0; i < GREF_NKEYS; i++) {
        char *s;
        int n_se = jc_snprintf(slot, sizeof slot, "%s=", gref_keys[i]);
        if (n_se < 0 || n_se >= (int)sizeof slot) {
            jc_vec_free(out);
            return -1;
        }
        s = jc_arena_strdup(a, slot);
        if (s == NULL) {
            jc_vec_free(out);
            return -1;
        }
        if (jc_vec_push(out, &s) != JC_OK) {
            jc_vec_free(out);
            return -1;
        }
    }
    for (i = 0; i < n_extra; i++) {
        char *s;
        if (extra[i] == NULL) {
            continue;
        }
        s = jc_arena_strdup(a, extra[i]);
        if (s == NULL) {
            jc_vec_free(out);
            return -1;
        }
        if (jc_vec_push(out, &s) != JC_OK) {
            jc_vec_free(out);
            return -1;
        }
    }
    return 0;
}

/* gref_git: ONE keyless git READ child (`git -C '<repo>' <args>`), the sweep's
 * gitread shape. Combined output captured into `out` (cap-bounded, NUL-
 * terminated, trimmed). Returns the child's exit status, -1 on a spawn fault. */
static int
gref_git(const char *repo, const char *args, int cap, char *out)
{
    char cmd[8192];
    int rc;
    int n_cg;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    n_cg = jc_snprintf(cmd, sizeof cmd, "git -C '%s' %s", repo, args);
    if (n_cg < 0 || n_cg >= (int)sizeof cmd) {
        return -1;
    }
    rc = gref_run(NULL, cmd, cap, out);
    if (rc == 0 && out != NULL && cap > 0) {
        gref_trim(out);
    }
    return rc;
}

/* gref_clone_setup: the reference's :25 clone (ONE keyless git WRITE child),
 * in `wpath` into `jdir`. 0 on success; -1 on a fault (the caller emits the
 * `  redfirst: setup: INVALID` line). */
static int
gref_clone_setup(const char *wpath, const char *repo, const char *jdir)
{
    char cmd[8192];
    int n_cc;

    n_cc = jc_snprintf(cmd, sizeof cmd,
                       "git clone -q --no-local --no-checkout '%s' '%s'",
                       repo, jdir);
    if (n_cc < 0 || n_cc >= (int)sizeof cmd) {
        return -1;
    }
    return gref_run(wpath, cmd, 0, NULL);
}

/* gref_revlist: the reference's :27 range (ONE keyless git READ child). The
 * output is the space-separated list of commits (the reference's `for c in
 * $(git rev-list --reverse B..C)`). 0 on success (may be empty), -1 on a
 * fault (the range does not exist). */
static int
gref_revlist(const char *repo, const char *base, const char *head,
             int cap, char *out)
{
    char args[512];
    int n_ar;

    n_ar = jc_snprintf(args, sizeof args,
                       "rev-list --reverse %s..%s", base, head);
    if (n_ar < 0 || n_ar >= (int)sizeof args) {
        return -1;
    }
    return gref_git(repo, args, cap, out);
}

/* gref_diff_files: the reference's :28 (ONE keyless git READ child): the
 * commit's changed file list (one path per line). */
static int
gref_diff_files(const char *repo, const char *csha, int cap, char *out)
{
    char args[256];
    int n_df;

    n_df = jc_snprintf(args, sizeof args,
                       "diff-tree --no-commit-id --name-only -r %s", csha);
    if (n_df < 0 || n_df >= (int)sizeof args) {
        return -1;
    }
    return gref_git(repo, args, cap, out);
}

/* gref_subject: the reference's :34 (ONE keyless git READ child): the commit's
 * subject cut to 70 (git log -1 --format=%%s | cut -c1-70). */
static int
gref_subject(const char *repo, const char *csha, int cap, char *out)
{
    char args[256];
    char full[GREF_LINE_MAX];
    int rc;
    int n_sb;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    n_sb = jc_snprintf(args, sizeof args, "log -1 --format=%%s %s", csha);
    if (n_sb < 0 || n_sb >= (int)sizeof args) {
        return -1;
    }
    rc = gref_git(repo, args, (int)sizeof full, full);
    if (rc != 0 || out == NULL || cap == 0) {
        return rc;
    }
    {
        size_t n = strlen(full);
        if (n > 70) {
            n = 70; /* cut -c1-70 */
        }
        memcpy(out, full, n);
        out[n] = '\0';
    }
    return 0;
}

/* gref_run_outfile: ONE keyless shell child (`sh -c <cmd>`), workdir = the
 * clone, stdin /dev/null, stdout+stderr -> `path` (created/truncated), the
 * 16 key names set EMPTY in the child env (+ the stage's per-check TMPDIR /
 * extra envs). UNCAPPED reap. Returns the child's exit status, -1 on a spawn
 * or open fault. `extra` is a NULL-terminated array of "KEY=VALUE" (or NULL). */
static int
gref_run_outfile(struct jc_arena *a, const char *workdir, const char *cmd,
                 const char *path, const char *extra[])
{
    int fd;
    struct jc_vec env;
    int ne = 0;
    int rc;

    if (extra != NULL) {
        while (extra[ne] != NULL) {
            ne++;
        }
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | JC_O_BINARY, 0644);
    if (fd < 0) {
        return -1;
    }
    if (gref_env(a, &env, extra, ne) != 0) {
        close(fd);
        return -1;
    }
    rc = gref_child_outfd(workdir, cmd, fd, &env);
    jc_vec_free(&env);
    if (close(fd) != 0) {
        rc = -1;
    }
    return rc;
}

/* jc_gateref_build_cmd: the WORKDIR + CMD joined command string (for unit testing).
 * `out` receives `cd '<workdir>' && <cmd>` when workdir is non-empty,
 * or `<cmd>` when workdir is NULL/empty. Returns 0 on success, -1 on overflow.
 * CHANGED in M1221: workdir is single-quoted to prevent word-splitting. */
int
jc_gateref_build_cmd(const char *workdir, const char *cmd,
                     char *out, size_t cap)
{
    int n;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    if (workdir != NULL && workdir[0] != '\0') {
        n = jc_snprintf(out, cap, "cd '%s' && %s", workdir, cmd);
        if (n < 0 || (size_t)n >= cap) {
            return -1;
        }
    } else {
        if (out != NULL && cap > 0 && cmd != NULL) {
            size_t i;
            for (i = 0; i < cap - 1 && cmd[i] != '\0'; i++) {
                out[i] = cmd[i];
            }
            out[i] = '\0';
        }
    }
    return 0;
}

/* jc_hp_gateref_run: the stage entry. The contract is in the .h. The record
 * is the stage's assembled stdout (every line the reference prints, in order,
 * the LAST line `records: <dir>`); rc 0 = the replay finished, 1 = an INVALID
 * marker, 2 = usage. The shimmable --redfirst-run / --redfirst-unit-run
 * templates carry the <s8> <d> <o> <b> <subj> tokens; the reference's real
 * forms are the defaults. */
int
jc_hp_gateref_run(const char *config_path,
                  const char *base,
                  const char *head,
                  const char *work,
                  const char *records,
                  const char *out_dir,
                  const char *redfirst_run,
                  const char *unit_run,
                  const char *push_repo,
                  struct jc_arena *a)
{
    struct jc_harness_config cfg;
    struct jc_sb record;
    char wpath[2048];
    char rpath[2048];
    char odir[2048];
    char tmpd[2048];
    char jdir[2048];
    char env_tmp[2064]; /* holds "TMPDIR=" + tmpd (<= 2047) + NUL */
    const char *env1[2];
    int ne = 0;
    char line[GREF_LINE_MAX];
    char text[GREF_CAP_TEXT];
    char range[GREF_CAP_TEXT];
    const char *repo;
    const char *rr_t;
    const char *ur_t;
    const char *bt;
    const char *dt;
    const char *ut;
    int n = 0, nr = 0, ng = 0, ns = 0;
    int rc = 0;

    if (config_path == NULL) {
        fprintf(stderr, "jichi-harness: gate run --stage redfirst: "
                        "--config <path> is required\n");
        return 2;
    }
    if (a == NULL) {
        a = jc_arena_new(0);
        if (a == NULL) {
            return 2;
        }
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *m = jc_harness_config_error(&cfg);
        fprintf(stderr, "jichi-harness: gate run --stage redfirst: %s\n",
                m != NULL ? m : "could not read the harness config");
        return 2;
    }
    if (!gref_hex8(base) || !gref_hex8(head)) {
        fprintf(stderr,
                "jichi-harness: gate run --stage redfirst: <BASE> and "
                "<HEAD> must each be 7-40 lowercase hex\n");
        return 2;
    }
    repo = (push_repo != NULL && push_repo[0] != '\0')
               ? push_repo
               : (cfg.host_push_repo != NULL &&
                  cfg.host_push_repo[0] != '\0') ? cfg.host_push_repo : NULL;
    if (repo == NULL) {
        fprintf(stderr,
                "jichi-harness: gate run --stage redfirst: --push-repo "
                "<path> is required (no host.pushRepo in the config)\n");
        return 2;
    }
    /* The shimmable surface (the note's design): when --redfirst-run (or the
     * config's host.redfirstRun) is given, it is ONE template the stage
     * substitutes per check (<s8> <d> <o> <b> <subj> -- the build form is
     * invoked with <d>/<o>/<b> empty, the driver form
     * with <d> = the driver path, <o> = the per-check file, <b> = the
     * basename); when ABSENT, the reference's real forms run (the BUILD
     * child `make -j8 WERROR=1 jichi smoke-tools`, the DRIVER child
     * `TMPDIR=<short> sh <d>`, the UNIT child `make -j8 WERROR=1 test`). */
    rr_t = (redfirst_run != NULL && redfirst_run[0] != '\0')
               ? redfirst_run
               : (cfg.host_redfirst_run != NULL &&
                  cfg.host_redfirst_run[0] != '\0')
                     ? cfg.host_redfirst_run : NULL;
    ur_t = (unit_run != NULL && unit_run[0] != '\0')
               ? unit_run
               : (cfg.host_redfirst_unit_run != NULL &&
                  cfg.host_redfirst_unit_run[0] != '\0')
                      ? cfg.host_redfirst_unit_run : NULL;
    /* The shimmable surface resolves to the per-form templates: --redfirst-run
     * (or host.redfirstRun) serves BOTH the build and the driver (the two
     * forms, the same substituted template), else the reference's real forms;
     * --redfirst-unit-run (or host.redfirstUnitRun) serves the unit, else the
     * reference's real unit form. */
    if (rr_t != NULL) {
        bt = rr_t;
        dt = rr_t;
    } else {
        bt = "make -j8 WERROR=1 jichi smoke-tools";
        dt = "sh <d>";
    }
    if (ur_t != NULL) {
        ut = ur_t;
    } else {
        ut = "make -j8 WERROR=1 test";
    }
    jc_sb_init(&record);

    /* :16-25 the setup: the --work home (else mkdtemp /tmp/jhp-ref-XXXXXX),
     * the out / records / tmp sub-dirs, the scratch clone at exactly HEAD. */
    if (work != NULL && work[0] != '\0') {
        int n_w1 = jc_snprintf(wpath, sizeof wpath, "%s", work);
        if (n_w1 < 0 || n_w1 >= (int)sizeof wpath ||
            gref_mkdir_p(wpath) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    } else {
        char tmpl[32];
        int n_t1;
        int n_w2;
        n_t1 = jc_snprintf(tmpl, sizeof tmpl, "%s", "/tmp/jhp-ref-XXXXXX");
        if (n_t1 < 0 || n_t1 >= (int)sizeof tmpl ||
            mkdtemp(tmpl) == NULL) {
            rc = 1;
            goto invalid_setup;
        }
        n_w2 = jc_snprintf(wpath, sizeof wpath, "%s", tmpl);
        if (n_w2 < 0 || n_w2 >= (int)sizeof wpath ||
            gref_mkdir_p(wpath) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    }
    if (out_dir != NULL && out_dir[0] != '\0') {
        int n_o1 = jc_snprintf(odir, sizeof odir, "%s", out_dir);
        if (n_o1 < 0 || n_o1 >= (int)sizeof odir ||
            gref_mkdir_p(odir) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    } else {
        int n_o2 = jc_snprintf(odir, sizeof odir, "%s/out", wpath);
        if (n_o2 < 0 || n_o2 >= (int)sizeof odir ||
            gref_mkdir_p(odir) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    }
    if (records != NULL && records[0] != '\0') {
        int n_r1 = jc_snprintf(rpath, sizeof rpath, "%s", records);
        if (n_r1 < 0 || n_r1 >= (int)sizeof rpath ||
            gref_mkdir_p(rpath) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    } else {
        int n_r2 = jc_snprintf(rpath, sizeof rpath, "%s/records", wpath);
        if (n_r2 < 0 || n_r2 >= (int)sizeof rpath ||
            gref_mkdir_p(rpath) != 0) {
            rc = 1;
            goto invalid_setup;
        }
    }
    {
        int n_td = jc_snprintf(tmpd, sizeof tmpd, "%s/tmp", wpath);
        int n_jd = jc_snprintf(jdir, sizeof jdir, "%s/j", wpath);
        if (n_td < 0 || n_td >= (int)sizeof tmpd ||
            gref_mkdir_p(tmpd) != 0 ||
            n_jd < 0 || n_jd >= (int)sizeof jdir) {
            rc = 1;
            goto invalid_setup;
        }
    }
    /* The per-check DRIVER + UNIT children receive TMPDIR=<work>/tmp (the
     * reference's redfirst.sh:41/:50); the 16 key names are set empty in the
     * same child env by gref_env. The sweep idiom: build the one
     * "TMPDIR=<tmpd>" extra ONCE, pass it to the driver + unit
     * gref_run_outfile calls below (the BUILD call passes NULL -- the
     * reference's :36-38 build has no TMPDIR). */
    if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd) >= 0) {
        env1[ne++] = env_tmp;
    }
    env1[ne] = NULL;
    /* F3 (M1159 B2): the re-gateable scratch clear (the C-b idiom from the
     * sweep stage). The orchestrator hands the redfirst stage a DETERMINISTIC
     * --work <G>/<s8>/rf, so a 2nd run of the same sha re-clones into the
     * first run's populated <work>/j and the :25 clone fails ("destination
     * path ... already exists and is not an empty directory") -> `redfirst:
     * setup: INVALID`. Clear the stage's OWN scratch under --work BEFORE the
     * re-clone: the clone destination j, the driver TMPDIR tmp, and the
     * DEFAULT records home. The --out home is NEVER cleared: the orchestrator
     * passes --out equal to --work (odir would be the work home itself, and
     * clearing it would remove the j/tmp/records sub-dirs), and a standalone
     * --out may be an external records home the operator named -- an explicit
     * --out / --records the user pointed elsewhere is the operator's, not the
     * stage's scratch, and is left alone. The <work> dir itself is NOT
     * removed -- D2 re-gateability and the driver's <gh>/<s8>/rf path shape
     * are unchanged, and git clone into the now-EMPTY <work>/j succeeds. */
    {
        char defrec[2048];
        const char *clears[3];
        int nclear = 0;
        int k;

        clears[nclear++] = jdir;   /* the clone destination (always) */
        clears[nclear++] = tmpd;   /* the driver TMPDIR (always) */
        if (jc_snprintf(defrec, sizeof defrec, "%s/records", wpath) >= 0 &&
            strcmp(rpath, defrec) == 0) {
            clears[nclear++] = rpath;  /* the DEFAULT records home */
        }
        for (k = 0; k < nclear; k++) {
            if (gref_clear_dir(clears[k]) != 0) {
                fprintf(stderr,
                        "jichi-harness: gate run --stage redfirst: could not "
                        "clear the stage scratch %s\n", clears[k]);
                rc = 1;
                goto invalid_setup;
            }
        }
    }
    /* :21-25 the scratch clone (ONE keyless git WRITE child); a fault is the
     * reference's `  redfirst: setup: INVALID` line + rc 1. */
    if (gref_clone_setup(wpath, repo, jdir) != 0) {
        (void)gref_record(&record, "  redfirst: setup: INVALID\n");
        rc = 1;
        goto fail;
    }
    /* :27 the range (ONE keyless git READ child); a fault (the range does not
     * exist) is the INVALID line + rc 1. */
    if (gref_revlist(repo, base, head, (int)sizeof range, range) != 0) {
        int n_lc;
        n_lc = jc_snprintf(line, sizeof line,
                           "  redfirst: no commits in %s..%s: INVALID\n",
                           base, head);
        if (n_lc < 0 || n_lc >= (int)sizeof line) {
            rc = 1;
            goto fail;
        }
        (void)gref_record(&record, line);
        rc = 1;
        goto fail;
    }
    /* :28-57 the loop over the range's commits (oldest first, the reference's
     * --reverse). */
    {
        char *tok = range;
        while (tok[0] != '\0') {
            char csha[160];
            char files[GREF_CAP_TEXT];
            char s8[16];
            char subj[GREF_LINE_MAX];
            char drv[2048], fu[2048], au[2048];
            int is_ref = 0;
            size_t cplen = 0;
            int n_s8;

            while (tok[cplen] != '\0' && tok[cplen] != ' ' &&
                   tok[cplen] != '\n') {
                cplen++;
            }
            if (cplen >= sizeof csha) {
                cplen = sizeof csha - 1;
            }
            memcpy(csha, tok, cplen);
            csha[cplen] = '\0';
            tok += cplen;
            while (tok[0] == ' ' || tok[0] == '\n') {
                tok++;
            }
            /* :28-33 the SELECTION (UNSTUBBED, real git). */
            if (gref_diff_files(repo, csha, (int)sizeof files, files) != 0) {
                files[0] = '\0';
            }
            if (jc_gateref_select(files, drv, sizeof drv, fu, sizeof fu,
                                  au, sizeof au, &is_ref) != 0) {
                rc = 1;
                goto fail;
            }
            if (!is_ref) {
                continue; /* :30/:33 not red-first */
            }
            n++; /* :34 EVERY red-first commit, before the build */
            n_s8 = jc_snprintf(s8, sizeof s8, "%.8s", csha);
            if (n_s8 < 0 || n_s8 >= (int)sizeof s8) {
                rc = 1;
                goto fail;
            }
            if (gref_subject(repo, csha, (int)sizeof subj, subj) != 0) {
                subj[0] = '\0';
            }
            /* :35 the checkout (ONE keyless git WRITE child); a fault is the
             * CHECKOUT FAILED line + the reference's continue. */
            {
                char cmd[4096];
                int n_co;
                n_co = jc_snprintf(cmd, sizeof cmd,
                                   "git -C '%s' checkout -q --detach -f "
                                   "'%s'", jdir, csha);
                if (n_co < 0 || n_co >= (int)sizeof cmd) {
                    rc = 1;
                    goto fail;
                }
                if (gref_run(wpath, cmd, 0, NULL) != 0) {
                    (void)jc_gateref_checkoutfail_line(s8, line, sizeof line);
                    (void)gref_record(&record, line);
                    continue;
                }
            }
            /* :36-38 the BUILD (per red-first commit); a fault is the
             * BUILD FAILED line + the reference's continue. */
            {
                char bcmd[4096];
                char olog[2048];
                int brc;
                int n_bl;
                n_bl = jc_snprintf(olog, sizeof olog, "%s/%s-build.log", odir,
                                   s8);
                if (n_bl < 0 || n_bl >= (int)sizeof olog) {
                    rc = 1;
                    goto fail;
                }
                if (gref_subst(bcmd, sizeof bcmd, bt, s8, "", "", "",
                               subj) < 0) {
                    rc = 1;
                    goto fail;
                }
                /* The reference's :36-38 build has no TMPDIR: NULL, not env1
                 * (env1 serves the driver :41 + unit :50 only). */
                brc = gref_run_outfile(a, jdir, bcmd, olog, NULL);
                if (brc != 0) {
                    (void)jc_gateref_buildfail_line(s8, subj, odir,
                                                    line, sizeof line);
                    (void)gref_record(&record, line);
                    continue;
                }
            }
            /* :39-48 the DRIVER loop. */
            {
                char *dp = drv;
                while (dp[0] != '\0') {
                    char b[256];
                    char dpath[2048];
                    char oout[2048];
                    char dcmd[4096];
                    char w[64];
                    int ok = 0, no = 0;
                    int drc;
                    int n_dp;
                    int n_od;
                    size_t blen = 0;

                    while (blen < sizeof b - 1 &&
                           dp[blen] != '\0' && dp[blen] != ' ') {
                        b[blen] = dp[blen];
                        blen++;
                    }
                    b[blen] = '\0';
                    dp += blen;
                    while (dp[0] == ' ') {
                        dp++;
                    }
                    n_dp = jc_snprintf(dpath, sizeof dpath,
                                       "tests/smoke/%s.sh", b);
                    if (n_dp < 0 || n_dp >= (int)sizeof dpath) {
                        rc = 1;
                        goto fail;
                    }
                    n_od = jc_snprintf(oout, sizeof oout, "%s/%s-%s.txt", odir,
                                       s8, b);
                    if (n_od < 0 || n_od >= (int)sizeof oout) {
                        rc = 1;
                        goto fail;
                    }
                    if (gref_subst(dcmd, sizeof dcmd, dt, s8, dpath, oout, b,
                                   subj) < 0) {
                        rc = 1;
                        goto fail;
                    }
                    drc = gref_run_outfile(a, jdir, dcmd, oout, env1);
                    (void)gref_read_file(oout, text, (int)sizeof text);
                    if (jc_gateref_driver_verdict(text, drc, w, sizeof w,
                                                  &ok, &no) != 0) {
                        rc = 1;
                        goto fail;
                    }
                    if (gref_skip_match(text) && no == 0 && ok == 0) {
                        ns++;
                    } else if (drc != 0 || no > 0) {
                        nr++;
                    } else {
                        ng++;
                    }
                    if (jc_gateref_driver_line(s8, w, b, drc, ok, no, subj,
                                               line, sizeof line) < 0) {
                        rc = 1;
                        goto fail;
                    }
                    (void)gref_record(&record, line);
                }
            }
            /* :49-57 the UNIT half (when a unit test was selected). */
            if (fu[0] != '\0') {
                char ucmd[4096];
                char oout[2048];
                char w[2048];
                char sm[1024];
                int urc;
                int n_ou;
                n_ou = jc_snprintf(oout, sizeof oout, "%s/%s-unit.txt", odir,
                                   s8);
                if (n_ou < 0 || n_ou >= (int)sizeof oout) {
                    rc = 1;
                    goto fail;
                }
                if (gref_subst(ucmd, sizeof ucmd, ut, s8, "", "", "",
                               subj) < 0) {
                    rc = 1;
                    goto fail;
                }
                urc = gref_run_outfile(a, jdir, ucmd, oout, env1);
                (void)gref_read_file(oout, text, (int)sizeof text);
                if (jc_gateref_unit_verdict(text, urc, w, sizeof w, sm,
                                            sizeof sm) != 0) {
                    rc = 1;
                    goto fail;
                }
                /* The reference's counting: `[ -z "$sm" ]` -> red (NO
                 * SUMMARY), `failures > 0` -> red (the verdict's RED word),
                 * otherwise green. */
                if (w[0] == 'G') {
                    ng++;
                } else {
                    nr++;
                }
                if (jc_gateref_unit_line(s8, w, sm, au, subj, line,
                                         sizeof line) < 0) {
                    rc = 1;
                    goto fail;
                }
                (void)gref_record(&record, line);
            }
        }
    }
    /* :59 the summary (the record's last line before `records:`). */
    if (jc_gateref_summary_line(n, nr, ng, ns, line, sizeof line) < 0) {
        rc = 1;
        goto fail;
    }
    (void)gref_record(&record, line);
    /* The records line (LAST -- the stage's rc is 0 iff it printed). */
    if (jc_gateref_records_line(rpath, line, sizeof line) < 0) {
        rc = 1;
        goto fail;
    }
    (void)gref_record(&record, line);
    if (record.data != NULL &&
        fwrite(record.data, 1, record.len, stdout) != record.len) {
        rc = 1;
    }
    fflush(stdout);
    jc_sb_free(&record);
    return rc;

invalid_setup:
    (void)gref_record(&record, "  redfirst: setup: INVALID\n");
    if (record.data != NULL) {
        (void)fwrite(record.data, 1, record.len, stdout);
        fflush(stdout);
    }
    jc_sb_free(&record);
    return 1;
fail:
    if (record.data != NULL) {
        (void)fwrite(record.data, 1, record.len, stdout);
        fflush(stdout);
    }
    jc_sb_free(&record);
    return rc;
}
