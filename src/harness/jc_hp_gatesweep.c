/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gatesweep.c - the `gate run --stage sweep` body (M1154, batch H9b,
 * wave 2, task 5): the C89 port of the 2026-10-27 hardening-host drive's
 * sweep.sh, read line by line: :8 the KEYS UNSET (ported to the CHILD env of
 * every spawn), :14 the clone + detach checkout, :16 the build, :17 the build
 * line (CHANGED to `  build: <sha8>`), :19-26 the driver loop (the red count +
 * the `  RED <b> rc=<r>` + not-ok excerpt), :27 the `git status --short |
 * head -3` (the clone's untracked artifacts, bare, after the count line; an
 * empty status prints nothing), :32-39 the smoke-mutant (the
 * origin/master resolve, the first-`^mutant:` extract cut 150, the no-master
 * INVALID), :43-53 the unit san/o2 (each in its own clone, the last-
 * `N checks, M failures` extract, the NO SUMMARY arm, the FAIL/ASan excerpt),
 * :58-69 the container (the two-userns drivers, the ok/not-ok counts, the
 * SKIPPED INVALID, the podman-absent INVALID -- CHANGED shimmable), :70 the
 * records line.
 *
 * The line-assembly is PURE (no git, no I/O) and exported through
 * jc_hp_gatesweep.h, unit-tested by tests/test_gatesweep.c. The clone
 * DESTINATIONS under the scratch home are the stage's own: a re-gateable
 * --work (M1159 C-b / D2) is not the reference's mktemp-per-run, so a
 * SECOND run into a populated home would fail the :14/:43/:58 clones
 * (git: `destination path ... already exists and is not an empty
 * directory` -> the stage's `  clone failed: INVALID`, measured 2026-11-11);
 * jc_hp_gatesweep_run clears the stage scratch before the :14 clone (a
 * stage-internal cleanup, not the reference's `trap rm -rf $W` EXIT --
 * the work stays re-gateable across runs). The git clone and
 * the child spawns (the driver loop, the build/mutant/unit/container commands)
 * run through a jc_hp_git_run-shaped git helper (the T2/T3 idiom: a read child,
 * no credential wiring) + a generic keyless child-runner (the `--*-run`
 * commands are arbitrary shell strings, spawned via `sh -c <cmd>` with the
 * API-key envs cleared in the CHILD env -- the note's KEYS UNSET). */

/* mkdtemp is POSIX-2008: the build-wide -D_POSIX_C_SOURCE=200112L does NOT
 * declare it (the M1134 idiom, jc_platform_posix.c:29-46). The per-TU
 * _XOPEN_SOURCE 700 guard BEFORE the first header that reaches features.h
 * (the project's jc_platform.h via jc_mem.h, included by the header below)
 * exposes it -- and the dirent the driver loop lists with. The d_type check
 * is NOT ported (DT_REG / DT_UNKNOWN are BSD extensions, not POSIX): the name
 * filter alone decides (the reference's tests/smoke holds only .sh files; a
 * subdirectory's name would not end in .sh). qsort / mkdir / lstat are
 * stdlib / sys/stat (C89). The scratch clearing (M1159 C-b) is a BOUNDED
 * recursive walk: lstat first (a symlink is NEVER followed -- its stat()
 * state is what decides, a symlink-to-dir is removed as a link with the
 * target untouched), then children are removed in reverse name order
 * (a dir is emptied bottom-up before its rmdir; a non-empty rmdir means
 * the pass missed a child -> the fault), so one pass over a fresh home
 * always finishes. The walk is capped by a depth limit + a node count
 * (a pathological scratch degrades to a named clone fault, never a hang).
 */
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

#include "jc_hp_gatesweep.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>       /* rmdir / unlink (the scratch clear) */

#include "jc_harness_config.h"  /* the config load (the host block's fields) */
#include "jc_harness_push.h"    /* jc_hp_git_run (the shared git child) */
#include "jc_mem.h"             /* jc_arena_strdup / _strndup */
#include "jc_platform.h"        /* jc_size, JC_OK */
#include "jc_proc.h"            /* jc_proc_capture (the keyless child-runner) */
#include "jc_snprintf.h"        /* jc_snprintf, never sprintf */

#define GSW_LINE_MAX   8192     /* one captured-output line */
#define GSW_CAP_TEXT   (1024 * 1024) /* a command's combined output */
#define GSW_CAP_DRIVER 1024 * 1024    /* one driver's output (loop cap) */
#define GSW_CAP_OUT    (256 * 1024)   /* the stage's assembled record */
#define GSW_NOTOK_MAX  200     /* sweep.sh:23 cut -c1-200 */
#define GSW_MUT_MAX    150     /* sweep.sh:35-36 cut -c1-150 */
#define GSW_UNIT_MAX   200     /* sweep.sh:52 cut -c1-200 */

/* ---------------------------------------------------------------------------
 * Small text helpers (pure, bounded -- a non-terminating scan is a known
 * failure class, SV:2338; every walk is bounded by the NUL).
 * ----------------------------------------------------------------------- */

/* The first line of `text` whose bytes start with `prefix` (NULL/empty when
 * none). `line` is NUL-terminated, truncated to `cap` (the prefix is never
 * truncated away: cap is assumed >= strlen(prefix)). */
static const char *
gsw_line_prefix(const char *text, const char *prefix, char *line, size_t cap)
{
    size_t plen = strlen(prefix);
    size_t off;
    size_t len;
    size_t n;

    if (text == NULL || line == NULL) {
        line[0] = '\0';
        return NULL;
    }
    off = 0;
    len = strlen(text);
    while (off < len) {
        size_t start = off;
        size_t end = start;
        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end - start >= plen && memcmp(text + start, prefix, plen) == 0) {
            n = end - start;
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(line, text + start, n);
            line[n] = '\0';
            return line;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    line[0] = '\0';
    return NULL;
}

/* The summary the reference's grep -E '[0-9]+ checks, [0-9]+ failures'
 * MATCHES inside the line (an unanchored match: the numeric part, not the
 * whole line -- the `  unit san rc=0 37482 checks, 0 failures` fixture bytes,
 * not the build log's `...: 37482 checks, 0 failures`). Returns the offset of
 * the match in `line`, or -1 when the line has none. A match is: digits,
 * " checks, ", digits, " failures". */
static int
gsw_summary_match(const char *line, size_t len)
{
    size_t i;
    size_t j;
    size_t k;
    size_t m;

    if (len == 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        char c = line[i];
        if (!(c >= '0' && c <= '9')) {
            continue;
        }
        /* a run of digits */
        j = i;
        while (j < len && line[j] >= '0' && line[j] <= '9') {
            j++;
        }
        /* " checks, " */
        if (j + 9 > len ||
            memcmp(line + j, " checks, ", 9) != 0) {
            continue;
        }
        k = j + 9;
        if (k >= len || !(line[k] >= '0' && line[k] <= '9')) {
            continue;
        }
        m = k;
        while (m < len && line[m] >= '0' && line[m] <= '9') {
            m++;
        }
        if (m + 9 <= len && memcmp(line + m, " failures", 9) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The LAST `N checks, M failures` of `text` (the reference's
 * `grep -E ... | tail -1` -- tail -1 over the GREP output, i.e. the last
 * MATCHED substring, not the last line). NULL/empty when none. `line` is
 * NUL-terminated, truncated to `cap`. */
static const char *
gsw_last_summary(const char *text, char *line, size_t cap)
{
    size_t off;
    size_t len;

    if (line == NULL) {
        return NULL;
    }
    line[0] = '\0'; /* clear first: a no-match text must return empty, not
                      the buffer's previous contents (stale state) */
    if (text == NULL) {
        return NULL;
    }
    off = 0;
    len = strlen(text);
    while (off < len) {
        size_t start = off;
        size_t end = start;
        int hit;

        while (end < len && text[end] != '\n') {
            end++;
        }
        hit = gsw_summary_match(text + start, end - start);
        if (hit >= 0) {
            /* tail -1: keep the LAST match -- remember this line, keep
             * scanning (the suite's summary is its final line, and a stray
             * build-log line that happens to match does not win). */
            size_t n = end - start - (size_t)hit;
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(line, text + start + (size_t)hit, n);
            line[n] = '\0';
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    if (line[0] == '\0') {
        return NULL;
    }
    return line;
}

/* Count the lines of `text` starting with `prefix` (grep -c ^prefix). */
static int
gsw_count_prefix(const char *text, const char *prefix)
{
    size_t plen = strlen(prefix);
    size_t off;
    size_t len;
    int n = 0;

    if (text == NULL) {
        return 0;
    }
    off = 0;
    len = strlen(text);
    while (off < len) {
        size_t start = off;
        size_t end = start;
        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end - start >= plen && memcmp(text + start, prefix, plen) == 0) {
            n++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return n;
}

/* Append up to `limit` lines of `text` matching `substr` (anywhere on the
 * line) to the string-buffer `out`, each cut to `cut` bytes and indented `ind`
 * spaces (the reference's grep | head -N | cut -c1-M | sed 's/^/    /' pipes).
 * Bounded by `limit` and by the line walk. */
static void
gsw_excerpt_lines(struct jc_sb *out, const char *text, const char *substr,
                  int limit, size_t cut, const char *ind)
{
    size_t slen = strlen(substr);
    size_t indn = (ind != NULL) ? strlen(ind) : 0;
    size_t off;
    size_t len;
    int k = 0;

    if (text == NULL) {
        return;
    }
    off = 0;
    len = strlen(text);
    while (off < len && k < limit) {
        size_t start = off;
        size_t end = start;
        const char *hit = NULL;
        size_t n;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end - start >= slen) {
            size_t i;
            for (i = start; i + slen <= end; i++) {
                if (memcmp(text + i, substr, slen) == 0) {
                    hit = text + i;
                    break;
                }
            }
        }
        if (hit != NULL) {
            n = end - start;
            if (n > cut) {
                n = cut;
            }
            if (indn > 0) {
                (void)jc_sb_append_n(out, ind, indn);
            }
            (void)jc_sb_append_n(out, text + start, n);
            (void)jc_sb_append_char(out, '\n');
            k++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* The first `limit` lines of `text` starting with `prefix`, each cut to
 * `cut` bytes, appended to `out` (the not-ok excerpt's grep | head -2 |
 * cut -c1-200, with an optional indent). */
static void
gsw_prefix_lines(struct jc_sb *out, const char *text, const char *prefix,
                 int limit, size_t cut, const char *ind)
{
    size_t plen = strlen(prefix);
    size_t indn = (ind != NULL) ? strlen(ind) : 0;
    size_t off;
    size_t len;
    int k = 0;

    if (text == NULL) {
        return;
    }
    off = 0;
    len = strlen(text);
    while (off < len && k < limit) {
        size_t start = off;
        size_t end = start;
        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end - start >= plen && memcmp(text + start, prefix, plen) == 0) {
            size_t n = end - start;
            if (n > cut) {
                n = cut;
            }
            if (indn > 0) {
                (void)jc_sb_append_n(out, ind, indn);
            }
            (void)jc_sb_append_n(out, text + start, n);
            (void)jc_sb_append_char(out, '\n');
            k++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* ---------------------------------------------------------------------------
 * The PURE line-assembly (exported; the unit table pins each byte).
 * ----------------------------------------------------------------------- */

/* jc_gatesweep_build_line: `  build: <sha8>\n`. */
int
jc_gatesweep_build_line(const char *sha8, char *out, size_t cap)
{
    if (sha8 == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    return jc_snprintf(out, cap, "  build: %s\n", sha8);
}

/* jc_gatesweep_count_line: `sweep: ran N drivers on <sha8>;
 * red:<bad|none>\n`. */
int
jc_gatesweep_count_line(int n, const char *sha8, const char *bad,
                        char *out, size_t cap)
{
    const char *b = (bad != NULL && bad[0] != '\0') ? bad : "none";

    if (sha8 == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    return jc_snprintf(out, cap, "sweep: ran %d drivers on %s; red: %s\n",
                       n, sha8, b);
}

/* jc_gatesweep_red_line: `  RED <b> rc=<rc>\n`. */
int
jc_gatesweep_red_line(const char *b, int rc, char *out, size_t cap)
{
    if (b == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    return jc_snprintf(out, cap, "  RED %s rc=%d\n", b, rc);
}

/* jc_gatesweep_notok_excerp: the first two `^not ok` lines, cut 200. */
int
jc_gatesweep_notok_excerp(const char *text, char *out, size_t cap)
{
    struct jc_sb sb;

    if (out == NULL || cap == 0) {
        return -1;
    }
    jc_sb_init(&sb);
    gsw_prefix_lines(&sb, text, "not ok", 2, GSW_NOTOK_MAX, NULL);
    {
        int n;
        if (sb.len < cap) {
            memcpy(out, sb.data != NULL ? sb.data : "", sb.len);
            out[sb.len] = '\0';
            n = (int)sb.len;
        } else {
            memcpy(out, sb.data != NULL ? sb.data : "", cap - 1);
            out[cap - 1] = '\0';
            n = -1;
        }
        jc_sb_free(&sb);
        return n;
    }
}

/* jc_gatesweep_mutant_line: `  smoke-mutant rc=<r> (MUTANT_BASE=<%.8s>):
 * <first ^mutant: line, the `mutant: ` prefix stripped, cut 150>\n`. */
int
jc_gatesweep_mutant_line(int r, const char *mb, const char *text,
                         char *out, size_t cap)
{
    char line[GSW_LINE_MAX];
    const char *ml;
    char cut[160];
    size_t blen;
    size_t n;

    if (mb == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    ml = gsw_line_prefix(text, "mutant:", line, sizeof line);
    if (ml != NULL) {
        /* The reference's extract is the line WITH the "mutant:" prefix
         * (`grep '^mutant:'`), and the :35 embeds it after the ": ". The line
         * reads "...): mutant: OK ..." -- so keep the full matched line. */
        blen = strlen(line);
        if (blen > GSW_MUT_MAX) {
            blen = GSW_MUT_MAX;
        }
        memcpy(cut, line, blen);
        cut[blen] = '\0';
    } else {
        cut[0] = '\0';
    }
    n = (size_t)(jc_snprintf(out, cap,
                             "  smoke-mutant rc=%d (MUTANT_BASE=%.8s): %s\n",
                             r, mb, cut));
    return (n < cap) ? (int)n : -1;
}

/* jc_gatesweep_mutant_nomb_line: `  smoke-mutant: no origin/master in
 * push.git: INVALID\n`. */
int
jc_gatesweep_mutant_nomb_line(char *out, size_t cap)
{
    int n;
    if (out == NULL || cap == 0) {
        return -1;
    }
    n = jc_snprintf(out, cap,
                    "  smoke-mutant: no origin/master in push.git: INVALID\n");
    return (n < (int)cap) ? n : -1;
}

/* jc_gatesweep_mutant_notok: the first four `^not ok` lines, cut 150, indented. */
int
jc_gatesweep_mutant_notok(const char *text, char *out, size_t cap)
{
    struct jc_sb sb;
    int n;

    if (out == NULL || cap == 0) {
        return -1;
    }
    jc_sb_init(&sb);
    gsw_prefix_lines(&sb, text, "not ok", 4, GSW_MUT_MAX, "    ");
    if (sb.len < cap) {
        memcpy(out, sb.data != NULL ? sb.data : "", sb.len);
        out[sb.len] = '\0';
        n = (int)sb.len;
    } else {
        memcpy(out, sb.data != NULL ? sb.data : "", cap - 1);
        out[cap - 1] = '\0';
        n = -1;
    }
    jc_sb_free(&sb);
    return n;
}

/* jc_gatesweep_unit_line: `  unit <v> rc=<r> <last 'N checks, M failures' |
 * NO SUMMARY (build failed or aborted)>\n`. */
int
jc_gatesweep_unit_line(const char *v, int r, const char *text,
                       char *out, size_t cap)
{
    char line[GSW_LINE_MAX];
    const char *s;
    int n;

    if (v == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    s = gsw_last_summary(text, line, sizeof line);
    if (s != NULL) {
        n = jc_snprintf(out, cap, "  unit %s rc=%d %s\n", v, r, line);
    } else {
        n = jc_snprintf(out, cap,
                        "  unit %s rc=%d NO SUMMARY (build failed or aborted)"
                        "\n", v, r);
    }
    return (n < (int)cap) ? n : -1;
}

/* jc_gatesweep_unit_excerp: the first three FAIL/ASan/runtime-error lines,
 * cut 200, indented. */
int
jc_gatesweep_unit_excerp(const char *text, char *out, size_t cap)
{
    struct jc_sb sb;
    int n;

    if (out == NULL || cap == 0) {
        return -1;
    }
    jc_sb_init(&sb);
    gsw_excerpt_lines(&sb, text, "FAIL", 3, GSW_UNIT_MAX, "    ");
    gsw_excerpt_lines(&sb, text, "AddressSanitizer", 3, GSW_UNIT_MAX, "    ");
    gsw_excerpt_lines(&sb, text, "runtime error", 3, GSW_UNIT_MAX, "    ");
    if (sb.len < cap) {
        memcpy(out, sb.data != NULL ? sb.data : "", sb.len);
        out[sb.len] = '\0';
        n = (int)sb.len;
    } else {
        memcpy(out, sb.data != NULL ? sb.data : "", cap - 1);
        out[cap - 1] = '\0';
        n = -1;
    }
    jc_sb_free(&sb);
    return n;
}

/* jc_gatesweep_container_line: `  container <d> rc=<r> <ok> ok, <no> not
 * ok< -- SKIPPED: INVALID (<skip>)>\n`. */
int
jc_gatesweep_container_line(const char *d, int r, int ok_count, int not_count,
                            const char *skip, char *out, size_t cap)
{
    const char *sk = (skip != NULL && skip[0] != '\0') ? skip : NULL;
    int n;

    if (d == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    if (sk != NULL) {
        n = jc_snprintf(out, cap,
                        "  container %s rc=%d %d ok, %d not ok -- SKIPPED: "
                        "INVALID (%s)\n", d, r, ok_count, not_count, sk);
    } else {
        n = jc_snprintf(out, cap, "  container %s rc=%d %d ok, %d not ok\n",
                        d, r, ok_count, not_count);
    }
    return (n < (int)cap) ? n : -1;
}

/* jc_gatesweep_container_podman_line: the note's podman-absent INVALID. */
int
jc_gatesweep_container_podman_line(const char *d, char *out, size_t cap)
{
    int n;
    if (d == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    n = jc_snprintf(out, cap,
                    "  container %s rc=1 0 ok, 0 not ok -- SKIPPED: INVALID "
                    "(podman not available)\n", d);
    return (n < (int)cap) ? n : -1;
}

/* jc_gatesweep_container_notok: the first two `^not ok` lines, indented. */
int
jc_gatesweep_container_notok(const char *text, char *out, size_t cap)
{
    struct jc_sb sb;
    int n;

    if (out == NULL || cap == 0) {
        return -1;
    }
    jc_sb_init(&sb);
    gsw_prefix_lines(&sb, text, "not ok", 2, 4096, "    ");
    if (sb.len < cap) {
        memcpy(out, sb.data != NULL ? sb.data : "", sb.len);
        out[sb.len] = '\0';
        n = (int)sb.len;
    } else {
        memcpy(out, sb.data != NULL ? sb.data : "", cap - 1);
        out[cap - 1] = '\0';
        n = -1;
    }
    jc_sb_free(&sb);
    return n;
}

/* jc_gatesweep_records_line: `records: <out>\n`. */
int
jc_gatesweep_records_line(const char *rec, char *out, size_t cap)
{
    int n;
    if (rec == NULL || out == NULL || cap == 0) {
        if (out != NULL && cap != 0) {
            out[0] = '\0';
        }
        return -1;
    }
    n = jc_snprintf(out, cap, "records: %s\n", rec);
    return (n < (int)cap) ? n : -1;
}

/* jc_gatesweep_buildfail_line: `build failed: INVALID\n`. */
int
jc_gatesweep_buildfail_line(char *out, size_t cap)
{
    int n;
    if (out == NULL || cap == 0) {
        return -1;
    }
    n = jc_snprintf(out, cap, "build failed: INVALID\n");
    return (n < (int)cap) ? n : -1;
}

/* ---------------------------------------------------------------------------
 * The keyless child-runner + the git helper (the stage body uses these).
 * ----------------------------------------------------------------------- */

/* The API-key envs the reference unsets (sweep.sh:8). The port sets them
 * EMPTY in each spawn's CHILD env (the note's KEYS UNSET, behaviour 3 CHANGED:
 * the reference unsets in its own shell; the port's driver loop runs
 * in-process, so the keys are cleared in the child env of each spawn). Setting
 * them to "" also overrides a registered-secret name, so the scrub cannot
 * re-add the value. */
static const char *const gsw_keys[] = {
    "JICHI_API_KEY", "JLU_API_KEY", "ANTHROPIC_API_KEY", "OPENAI_API_KEY",
    "OPENROUTER_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY",
    "MISTRAL_API_KEY", "DEEPSEEK_API_KEY", "TOGETHER_API_KEY",
    "FIREWORKS_API_KEY", "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
    "ZD_TOKEN_FILE"
};
#define GSW_NKEYS (int)(sizeof gsw_keys / sizeof gsw_keys[0])

/* F2 (M1154 T6): the 16 names, exported for the unit pin (the LENGTH + the
 * COMPOSITION, including ZD_TOKEN_FILE -- the one name jc_proc's built-in
 * scrub lacks, jc_proc.c:71-78). `out` receives the name strings (pointers
 * into gsw_keys); returns the count, -1 when `cap` is short. */
int
jc_gatesweep_key_names(const char **out, int cap)
{
    int i;

    if (out == NULL || cap < GSW_NKEYS) {
        return -1;
    }
    for (i = 0; i < GSW_NKEYS; i++) {
        out[i] = gsw_keys[i];
    }
    return GSW_NKEYS;
}

/* F2 (M1154 T6): the env the git READ children receive, as "KEY=VALUE"
 * entries pushed into the caller-initialized `out` (the 16 gsw_keys, each
 * set EMPTY -- the same env gsw_run builds for every child; the read child
 * is a gsw_run child, so the unit pin and the stage share this builder).
 * 0 on success, -1 on a bad argument / OOM. */
int
jc_gatesweep_gitread_env(struct jc_arena *a, struct jc_vec *out)
{
    char slot[GSW_NKEYS][128];
    int i;

    if (a == NULL || out == NULL) {
        return -1;
    }
    for (i = 0; i < GSW_NKEYS; i++) {
        char *s;
        char *cp;
        int n_sk = jc_snprintf(slot[i], sizeof slot[i], "%s=", gsw_keys[i]);
        if (n_sk < 0 || n_sk >= (int)sizeof slot[i]) {
            return -1;
        }
        cp = jc_arena_strdup(a, slot[i]);
        if (cp == NULL) {
            return -1;
        }
        s = cp;
        if (jc_vec_push(out, &s) != JC_OK) {
            return -1;
        }
    }
    return 0;
}

/* Run `cmd` as `sh -c <cmd>` (or exec `argv` when cmd is NULL) in `workdir`,
 * with the keyless child env plus `extra` "KEY=VALUE" entries (the TMPDIR /
 * MUTANT_BASE / BIN envs). The combined stdout+stderr is captured into `out`
 * (truncated to `cap`, NUL-terminated). Returns the child's exit status, -1
 * on spawn failure, -2 on timeout. */
static int
gsw_run(const char *workdir, const char *cmd, const char *extra[],
        int n_extra, int cap, char *out)
{
    char *argv[8];
    char slot[GSW_NKEYS][128]; /* the keyless "KEY=" entries (persist for the
                                  capture: child_apply_env reads them at exec) */
    struct jc_vec envv;
    struct jc_sb capture;
    /* At FUNCTION scope: argv[2] points at it until the child is spawned, far
     * below the block that fills it (declared in that block it was used after
     * its scope; clang -O2 reused the slot and the shell got other bytes). */
    char joined[16384];
    int k = 0;
    int i;
    int rc;

    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    if (cmd != NULL) {
        /* The reference runs each stage from its own directory (cd "$W/j",
         * cd "$W/$v"); the port's child inherits the parent's cwd, so the
         * workdir is prefixed as a cd (the same sh, the keyless env). */
        if (workdir != NULL && workdir[0] != '\0') {
            if (jc_gatesweep_build_cmd(workdir, cmd, joined, sizeof joined) < 0) {
                return -1;
            }
            cmd = joined;
        }
        argv[k++] = (char *)"sh";
        argv[k++] = (char *)"-c";
        argv[k++] = (char *)cmd;
    }
    argv[k] = NULL;

    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < GSW_NKEYS; i++) {
        char *s;
        int n_sr = jc_snprintf(slot[i], sizeof slot[i], "%s=", gsw_keys[i]);
        if (n_sr < 0 || n_sr >= (int)sizeof slot[i]) {
            continue;
        }
        s = slot[i];
        if (jc_vec_push(&envv, &s) != JC_OK) {
            break;
        }
    }
    for (i = 0; i < n_extra && i < 8; i++) {
        if (extra[i] != NULL && jc_vec_push(&envv, &extra[i]) != JC_OK) {
            break;
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
            memcpy(out, capture.data, (jc_size)n);
        }
        out[n] = '\0';
    }
    jc_sb_free(&capture);
    return rc;
}

/* ---------------------------------------------------------------------------
 * The driver set (sweep.sh:19): every tests/smoke/lint-named .sh, docs_*.sh
 * and reading_trace.sh. The port lists the directory (C has no shell glob; the
 * three patterns are checked by hand) and SORTS the names -- the reference's
 * glob expands in LC_ALL=C collation, which is strcmp order, so the count
 * line's `red:` join matches. A pattern with NO match contributes nothing
 * (the reference's unexpanded literal glob cannot happen in a C listing; the
 * driver's synthetic repo has only the *lint* drivers).
 * ----------------------------------------------------------------------- */

static int
gsw_driver_name(const char *name)
{
    size_t n = strlen(name);

    if (n < 4 || strncmp(name + n - 3, ".sh", 3) != 0) {
        return 0;
    }
    if (strcmp(name, "reading_trace.sh") == 0) {
        return 1;
    }
    if (strncmp(name, "docs_", 5) == 0) {
        return 1;
    }
    return (strstr(name, "lint") != NULL);
}

static int
gsw_cmp_str(const void *x, const void *y)
{
    return strcmp(*(char *const *)x, *(char *const *)y);
}

/* The first `limit` NON-EMPTY lines of `text`, each as-is (no cut, no
 * indent) appended to `out` (the reference's sweep.sh:27
 * `git status --short | head -3`, bare). An empty status appends nothing.
 * Bounded by `limit` and by the line walk. */
static void
gsw_status_lines(struct jc_sb *out, const char *text, int limit)
{
    size_t off;
    size_t len;
    int k = 0;

    if (text == NULL) {
        return;
    }
    off = 0;
    len = strlen(text);
    while (off < len && k < limit) {
        size_t start = off;
        size_t end = start;

        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end > start) {
            (void)jc_sb_append_n(out, text + start, end - start);
            (void)jc_sb_append_char(out, '\n');
            k++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* The clone's tests/smoke drivers into `out` (arena-copied, sorted). 0 ok,
 * -1 on OOM; a missing tests/smoke is not an error (the list stays empty --
 * the count line then says `ran 0`, and the verdict reads that as red: the
 * reference's own 2026-11-05 02:29 history, the loop-over-empty-set red). */
static int
gsw_collect_drivers(const char *clone_dir, struct jc_arena *a,
                    struct jc_vec *out)
{
    char ddir[2048];
    DIR *dp;
    struct dirent *e;
    int rc = 0;
    int n_dd;

    jc_vec_clear(out);
    n_dd = jc_snprintf(ddir, sizeof ddir, "%s/tests/smoke", clone_dir);
    if (n_dd < 0 || n_dd >= (int)sizeof ddir) {
        return -1;
    }
    dp = opendir(ddir);
    if (dp == NULL) {
        return 0;
    }
    while ((e = readdir(dp)) != NULL) {
        if (gsw_driver_name(e->d_name)) {
            char *copy = jc_arena_strdup(a, e->d_name);
            const char *key = copy;
            if (copy == NULL || jc_vec_push(out, &key) != JC_OK) {
                rc = -1;
                break;
            }
        }
    }
    closedir(dp);
    if (rc == 0 && out->len > 1) {
        qsort(out->data, out->len, sizeof (const char *), gsw_cmp_str);
    }
    return rc;
}

/* Write `text` to `path` (the per-stage log file, the reference's
 * $OUT/<name> captures). 0 ok; -1 on a fopen failure (the line still prints:
 * a log-file fault is the orchestrator's to read, not the stage's to abort on
 * -- the reference's `> "$OUT/x" 2>&1` would fail the same way). */
static int
gsw_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n;

    if (f == NULL) {
        return -1;
    }
    n = strlen(text);
    if (n > 0 && fwrite(text, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

/* mkdir -p `dir` (the records / work/tmp homes). 0 ok. M1159 B-fix C3: a
 * final component that ALREADY EXISTS as a directory is ok (the old
 * `mkdir == 0 ? 0 : -1` returned -1 on EEXIST, so `--work <existing>`
 * failed with "could not make the work home" -- and the no-`--work`
 * mkdtemp path always failed, since mkdtemp creates the dir BEFORE this
 * call). An EEXIST whose path is NOT a directory is still a fault. */
static int
gsw_mkdir_p(const char *dir)
{
    char buf[2048];
    char *slash;
    size_t n = strlen(dir);
    int rc;
    struct stat st;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, dir, n);
    buf[n] = '\0';
    for (slash = buf + 1; *slash != '\0'; slash++) {
        if (*slash == '/') {
            *slash = '\0';
            (void)mkdir(buf, 0755); /* may already exist: ignore EEXIST */
            *slash = '/';
        }
    }
    rc = (mkdir(buf, 0755) == 0) ? 0 : -1;
    if (rc != 0 && errno == EEXIST &&
        stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
        rc = 0;
    }
    return rc;
}

/* Remove the contents of `dir` (the re-gateable scratch clear, M1159 C-b):
 * the bounded recursive walk the TU header describes. Only the stage
 * scratch -- the clone DESTINATIONS `j`/`san`/`o2`/`ctr`, `tmp`, and
 * `records` under --work -- is ever passed in. 0 when the dir is empty
 * afterwards (or was absent), -1 on a walk / permission fault (a non-empty
 * rmdir is such a fault: the caller treats it as a stage setup fault).
 * Termination: the node + depth caps bound the recursion; the per-level
 * rmdir loop is itself capped (a dir that never empties is a fault, not
 * a hang). */
#define GSW_CLEAR_DEPTH  32
#define GSW_CLEAR_NODES  262144
#define GSW_CLEAR_NAMES  256    /* one dir level (a fresh scratch has <= 6) */

static int
gsw_clear_rec(const char *dir, int depth, int *nodes)
{
    DIR *dp;
    struct dirent *e;
    char names[GSW_CLEAR_NAMES][256];
    int nn = 0;
    int i;

    if (depth > GSW_CLEAR_DEPTH) {
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
        if (nn >= GSW_CLEAR_NAMES) {
            /* A level with more entries than the names array holds:
             * the walk cannot complete it (a fresh stage scratch has
             * at most the six top-level names) -- fault, never a
             * silent partial clear. */
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
        /* lstat: a symlink is NEVER followed (its own state decides;
         * a symlink-to-dir unlinks as a link, target untouched). */
        if (lstat(epath, &st) != 0) {
            if (errno == ENOENT) {
                continue;   /* vanished under us: not a fault */
            }
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            if (gsw_clear_rec(epath, depth + 1, nodes) != 0) {
                return -1;
            }
            /* Capped rmdir: a healthy dir empties in one pass (its
             * children were just removed); repeated EEXIST means the
             * pass is racing or faulting -- name it, do not spin. */
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
gsw_clear_dir(const char *dir)
{
    struct stat st;
    int nodes = GSW_CLEAR_NODES;

    if (stat(dir, &st) != 0) {
        return (errno == ENOENT) ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    return gsw_clear_rec(dir, 0, &nodes);
}

/* ---------------------------------------------------------------------------
 * The stage body (jc_hp_gatesweep_run).
 * ----------------------------------------------------------------------- */

/* s is 7-40 lowercase hex (the reference's $C; the orchestrator, wave 4,
 * passes the full sha -- the echoed value, so the verdict's patterns match
 * either). */
static int
gsw_hex8(const char *s)
{
    size_t n = strlen(s);
    size_t i;

    if (n < 7 || n > 40) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

/* Print one assembled line (the stage's record line, stdout only -- the
 * reference's echoes; a truncated line is impossible for the stage's inputs,
 * but refuse to print a partial line rather than emit a broken byte). */
static int
gsw_print(const char *line)
{
    size_t n = strlen(line);
    int r = (int)fwrite(line, 1, n, stdout);
    return (r == (int)n) ? 0 : -1;
}

/* Append one assembled line to the sb (the record's stdout assembly). */
static int
gsw_sb(struct jc_sb *sb, const char *line)
{
    return jc_sb_append(sb, line);
}

/* The container stage's default command (the reference's sweep.sh:58-69, the
 * drive image's podman run + the inline two-driver loop, $W -> work, $OUT ->
 * records). Built only when the operator's host runs the unstubbed default.
 * The command is assembled in CHUNKS (jc_snprintf into successive halves of
 * the buffer) rather than one giant format: ISO C90 caps a single string
 * literal at 509 bytes, and the reference's inline loop is longer than that
 * once the paths are in. $W and $OUT are the reference's own variables (the
 * -v mount names + the $0 argument podman passes = the records dir). */
static int
gsw_podman_default(char *out, size_t cap, const char *clone_dir,
                   const char *records)
{
    int n1;
    int n2;

    n1 = jc_snprintf(
        out, cap,
        "podman run --rm --init --userns=keep-id "
        "--security-opt=no-new-privileges --cap-drop=all "
        "-v '%s:%s:rw' -v '%s:%s:rw' -w '%s' "
        "-e HOME='%s' -e TMPDIR='%s' localhost/jichi-drive:2026-10-17 "
        "sh -c '",
        clone_dir, clone_dir, records, records, clone_dir,
        clone_dir, clone_dir);
    if (n1 < 0 || (size_t)n1 >= cap) {
        return -1;
    }
    n2 = jc_snprintf(
        out + n1, cap - (size_t)n1,
        "make -j8 WERROR=1 jichi smoke-tools > \"$0/ctr-build.log\" 2>&1 "
        "|| { echo \"  container build FAILED: INVALID\"; exit 1; } "
        "for d in fetch_ssrf_reuse resolve_timeout_connect; do "
        "sh tests/smoke/$d.sh < /dev/null > \"$0/ctr-$d.txt\" 2>&1; r=$? ");
    if (n2 < 0 || (size_t)(n1 + n2) >= cap) {
        return -1;
    }
    return jc_snprintf(
        out + n1 + n2, cap - (size_t)(n1 + n2),
        "s=$(grep -i -E \"^1\\.\\.0|# skip\" \"$0/ctr-$d.txt\" | head -1) "
        "echo \"  container $d rc=$r $(grep -c \"^ok\" "
        "\"$0/ctr-$d.txt\") ok, $(grep -c \"^not ok\" "
        "\"$0/ctr-$d.txt\") not ok${s:+ -- SKIPPED: INVALID ($s)}\" "
        "grep \"^not ok\" \"$0/ctr-$d.txt\" | head -2 | sed \"s/^/    /\" "
        "done' '%s' < /dev/null",
        records);
}

/* jc_gatesweep_build_cmd: the WORKDIR + CMD joined command string (for unit testing).
 * `out` receives `cd '<workdir>' && <cmd>` when workdir is non-empty,
 * or `<cmd>` when workdir is NULL/empty. Returns 0 on success, -1 on overflow.
 * CHANGED in M1221: workdir is single-quoted to prevent word-splitting. */
int
jc_gatesweep_build_cmd(const char *workdir, const char *cmd,
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

int
jc_hp_gatesweep_run(const char *config_path, const char *s,
                    const char *work, const char *records,
                    const char *bin, const char *build_run,
                    const char *mutant_run, const char *unit_run_san,
                    const char *unit_run_o2, const char *container_run,
                    const char *push_repo, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    struct jc_sb record;   /* the stage's assembled stdout (the record) */
    struct jc_vec drivers; /* the clone's driver basenames, sorted */
    char wpath[2048];
    char rpath[2048];
    char jdir[2048];
    char tmpd[2048];
    char clone[2048];
    char line[GSW_LINE_MAX];
    char slot_text[GSW_CAP_TEXT];
    char slot_drv[GSW_CAP_DRIVER];
    char logpath[2048];
    const char *repo;
    const char *bin_path;
    const char *b_cmd, *m_cmd, *u_cmd[2];
    char podman_cmd[2048];
    char mb[128];
    int rc = 0;
    int ended_invalid = 0;
    int n;
    int i;

    if (config_path == NULL) {
        fprintf(stderr, "jichi-harness: gate run --stage sweep: "
                        "--config <path> is required\n");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        fprintf(stderr, "jichi-harness: gate run --stage sweep: %s\n",
                msg != NULL ? msg : "could not read the harness config");
        return 2;
    }
    /* The push repo (the clone's source): the --push-repo flag, else
     * host.pushRepo, else <host.pushHome>/push.git -- REQUIRED (the brief:
     * absent -> exit 2, naming it). */
    repo = (push_repo != NULL && push_repo[0] != '\0')
               ? push_repo
               : (cfg.host_push_repo != NULL &&
                  cfg.host_push_repo[0] != '\0')
                     ? cfg.host_push_repo : NULL;
    if (repo == NULL) {
        fprintf(stderr,
                "jichi-harness: gate run --stage sweep: --push-repo <path> "
                "is required (the clone's source; no host.pushRepo in the "
                "config)\n");
        return 2;
    }
    if (!gsw_hex8(s)) {
        fprintf(stderr,
                "jichi-harness: gate run --stage sweep: the <SHA8> must be "
                "7-40 lowercase hex (got '%s')\n", s);
        return 2;
    }

    jc_sb_init(&record);
    jc_vec_init(&drivers, sizeof (const char *));

    /* The scratch home (the reference's mktemp -d /tmp/jfx-sweep.XXXXXX):
     * the --work flag, else /tmp/jhp-sweep-XXXXXX. */
    if (work != NULL && work[0] != '\0') {
        n = jc_snprintf(wpath, sizeof wpath, "%s", work);
    } else {
        char tmpl[32];
        n = jc_snprintf(tmpl, sizeof tmpl, "%s", "/tmp/jhp-sweep-XXXXXX");
        if (n < 0 || n >= (int)sizeof tmpl) {
            fprintf(stderr, "jichi-harness: gate run --stage sweep: could "
                            "not make the work home\n");
            rc = 1;
            goto fail;
        }
        if (mkdtemp(tmpl) == NULL) {
            fprintf(stderr, "jichi-harness: gate run --stage sweep: could "
                            "not make the work home\n");
            rc = 1;
            goto fail;
        }
        n = jc_snprintf(wpath, sizeof wpath, "%s", tmpl);
    }
    if (n < 0 || (size_t)n >= sizeof wpath || gsw_mkdir_p(wpath) != 0) {
        fprintf(stderr, "jichi-harness: gate run --stage sweep: could not "
                        "make the work home %s\n", wpath);
        rc = 1;
        goto fail;
    }
    /* The records home (the reference's $H/verify/sweep-$C): the --records
     * flag, else <work>/records. */
    if (records != NULL && records[0] != '\0') {
        n = jc_snprintf(rpath, sizeof rpath, "%s", records);
    } else {
        n = jc_snprintf(rpath, sizeof rpath, "%s/records", wpath);
    }
    /* M1159 C-b (the re-gateable scratch). The reference's sweep.sh takes a
     * FRESH `mktemp -d` per run and `rm -rf`s it at EXIT, so its :14/:43/:58
     * clones always land on an EMPTY home. The port keeps the orchestrator's
     * DETERMINISTIC --work (the driver's G23/G25: <gh>/<s8>/sw, D2's
     * dead-clone rationale -- a re-gateable, inspectable home), but the SAME
     * home is reused for EVERY run of one sha8. Without a clear, a second
     * run's :14 clone lands on the first run's populated <work>/j and git
     * refuses (`destination path ... already exists and is not an empty
     * directory`) -> `  clone failed: INVALID` (measured 2026-11-11).
     * Restore the fresh-home guarantee by clearing the stage's OWN scratch
     * children (the clone homes j/san/o2/ctr, the driver TMPDIR, and the
     * stage-INTERNAL records dir) BEFORE they are re-created below. The
     * <work> dir itself, and the rest of <G>/<s8> (run.log, sweep.txt,
     * sweep.verdict, the redfirst/ci homes), are NOT touched. An explicit
     * --records pointing OUTSIDE <work> is not the stage's scratch and is
     * left alone (rpath == <work>/records only in the default case). */
    {
        char gsw_intrec[2048];
        const char *gsw_scratch[6];
        int nscratch = 5;
        int k;

        gsw_scratch[0] = "j";
        gsw_scratch[1] = "san";
        gsw_scratch[2] = "o2";
        gsw_scratch[3] = "ctr";
        gsw_scratch[4] = "tmp";
        /* The internal records dir (the default --records) is the stage's
         * own scratch too; an explicit --records elsewhere is not. */
        if (nscratch < 6 &&
            jc_snprintf(gsw_intrec, sizeof gsw_intrec, "%s/records", wpath)
                >= 0 &&
            strcmp(rpath, gsw_intrec) == 0) {
            gsw_scratch[nscratch++] = "records";
        }
        for (k = 0; k < nscratch; k++) {
            char sp[2048];
            int n_sp;

            n_sp = jc_snprintf(sp, sizeof sp, "%s/%s", wpath, gsw_scratch[k]);
            if (n_sp < 0 || n_sp >= (int)sizeof sp) {
                rc = 1;
                goto fail;
            }
            if (gsw_clear_dir(sp) != 0) {
                fprintf(stderr, "jichi-harness: gate run --stage sweep: "
                                "could not clear the stage scratch %s\n", sp);
                rc = 1;
                goto fail;
            }
        }
    }
    if (n < 0 || (size_t)n >= sizeof rpath || gsw_mkdir_p(rpath) != 0) {
        fprintf(stderr, "jichi-harness: gate run --stage sweep: could not "
                        "make the records home %s\n", rpath);
        rc = 1;
        goto fail;
    }
    n = jc_snprintf(tmpd, sizeof tmpd, "%s/tmp", wpath);
    if (n < 0 || (size_t)n >= sizeof tmpd || gsw_mkdir_p(tmpd) != 0) {
        fprintf(stderr, "jichi-harness: gate run --stage sweep: could not "
                        "make the driver TMPDIR %s\n", tmpd);
        rc = 1;
        goto fail;
    }
    n = jc_snprintf(jdir, sizeof jdir, "%s/j", wpath);
    if (n < 0 || (size_t)n >= sizeof jdir) {
        rc = 1;
        goto fail;
    }

    /* :14 the clone + detach checkout (the reference's own command, a
     * keyless child; the clone is a WRITE child, so it goes through the
     * generic child-runner, not the read-only jc_hp_git_run). */
    n = jc_snprintf(slot_text, sizeof slot_text,
                    "git clone -q --no-local --no-checkout '%s' '%s' && "
                    "git -C '%s' checkout -q --detach '%s'",
                    repo, jdir, jdir, s);
    if (n < 0 || (size_t)n >= sizeof slot_text) {
        rc = 1;
        goto fail;
    }
    /* workdir = wpath (the clone's PARENT): gsw_run cd's into it first, and
     * jdir does not exist yet -- the clone is what creates it. */
    if (gsw_run(wpath, slot_text, NULL, 0, (int)sizeof line, line) != 0) {
        (void)gsw_sb(&record, "  clone failed: INVALID\n");
        ended_invalid = 1;
        rc = 1;
        goto fail;
    }
    (void)jc_snprintf(clone, sizeof clone, "%s", jdir);

    /* The binary the driver loop runs (the reference's ./jichi in the
     * clone): the --bin flag, else host.bin, else the STUB the brief names
     * (<work>/j/jichi, `#!/bin/sh\nexit 0\n`, chmod +x -- the driver loop
     * never needs a real build). */
    if (bin != NULL && bin[0] != '\0') {
        bin_path = bin;
    } else if (cfg.host_bin != NULL && cfg.host_bin[0] != '\0') {
        bin_path = cfg.host_bin;
    } else {
        char stub[2048];
        FILE *f;
        int n_st;
        n_st = jc_snprintf(stub, sizeof stub, "%s/jichi", jdir);
        if (n_st < 0 || n_st >= (int)sizeof stub) {
            rc = 1;
            goto fail;
        }
        f = fopen(stub, "wb");
        if (f != NULL) {
            (void)fputs("#!/bin/sh\nexit 0\n", f);
            (void)fclose(f);
            (void)chmod(stub, 0755);
        }
        bin_path = jc_arena_strdup(a, stub);
        if (bin_path == NULL) {
            rc = 1;
            goto fail;
        }
    }

    /* :16-17 the build (the reference's `make -j8 WERROR=1 all smoke-tools
     * > "$OUT/build.log" 2>&1 || { echo "build failed: INVALID";
     * exit 1; }`; B1, M1197-b: the reference's line 16 build is now
     * `all smoke-tools`, not `jichi smoke-tools`, so a driver that installs
     * the FULL PROGRAM SET -- docs_install, M1197 -- is swept, not skipped:
     * `jichi` alone leaves jichi-convert unbuilt and the driver t_skip's,
     * which the 2026-11-18 04:20 c54767bf gate went RED over), then the
     * port's build line (the note's CHANGED: the `  build: <sha8>` line,
     * not the binary's --version). */
    b_cmd = (build_run != NULL && build_run[0] != '\0')
                ? build_run
                : (cfg.host_build_run != NULL &&
                   cfg.host_build_run[0] != '\0')
                      ? cfg.host_build_run
                      : "make -j8 WERROR=1 all smoke-tools";
    {
        char env_tmp[2064];
        const char *env1[2];
        int ne = 0;

        if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd) >= 0) {
            env1[ne++] = env_tmp;
        }
        env1[ne] = NULL;
        if (gsw_run(clone, b_cmd, env1, ne, (int)sizeof slot_text,
                    slot_text) != 0) {
            (void)jc_snprintf(logpath, sizeof logpath, "%s/build.log", rpath);
            (void)gsw_write_file(logpath, slot_text);
            (void)gsw_sb(&record, "build failed: INVALID\n");
            ended_invalid = 1;
            rc = 1;
            goto fail;
        }
    }
    (void)jc_snprintf(logpath, sizeof logpath, "%s/build.log", rpath);
    (void)gsw_write_file(logpath, slot_text);
    if (jc_gatesweep_build_line(s, line, sizeof line) < 0) {
        rc = 1;
        goto fail;
    }
    if (gsw_sb(&record, line) != JC_OK) {
        rc = 1;
        goto fail;
    }

    /* :19-26 the driver loop (the reference's `for f in tests/smoke/
     * *lint*.sh docs_*.sh reading_trace.sh`, each `TMPDIR="$W/tmp" sh "$f"
     * < /dev/null > "$OUT/$b.txt" 2>&1`; a failure is rc != 0 OR a `^not ok`
     * line). The port's driver set is the same three patterns, listed +
     * sorted; the child env carries TMPDIR + the BIN the drivers invoke
     * (the port's CHANGED: the drivers run with --bin / host.bin, not the
     * clone's own build) and the keyless scrub. */
    {
        char bad[4096];
        int nrun = 0;
        int loop_fail = 0;

        bad[0] = '\0';
        if (gsw_collect_drivers(clone, a, &drivers) != 0) {
            rc = 1;
            goto fail;
        }
        for (i = 0; i < (int)drivers.len; i++) {
            const char *dname = *(const char **)jc_vec_at(&drivers, i);
            char dpath[2048];
            char dbin[256];
            char dcmd[2064];
            char env_tmp[2064];
            char env_bin[2064];
            const char *env1[3];
            int ne = 0;
            int drc;
            int red;

            /* dname is tests/smoke/<basename>; b is the basename minus .sh */
            n = jc_snprintf(dpath, sizeof dpath, "tests/smoke/%s", dname);
            if (n < 0 || (size_t)n >= sizeof dpath) {
                loop_fail = 1;
                break;
            }
            n = jc_snprintf(dbin, sizeof dbin, "%s", dname);
            if (n < 0 || (size_t)n >= sizeof dbin) {
                loop_fail = 1;
                break;
            }
            if (strlen(dbin) >= 4) {
                dbin[strlen(dbin) - 3] = '\0';
            }
            n = jc_snprintf(dcmd, sizeof dcmd, "sh %s", dpath);
            if (n < 0 || (size_t)n >= sizeof dcmd) {
                loop_fail = 1;
                break;
            }
            if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd)
                >= 0) {
                env1[ne++] = env_tmp;
            }
            if (jc_snprintf(env_bin, sizeof env_bin, "BIN=%s", bin_path)
                >= 0) {
                env1[ne++] = env_bin;
            }
            env1[ne] = NULL;
            drc = gsw_run(clone, dcmd, env1, ne, (int)sizeof slot_drv,
                          slot_drv);
            nrun++;
            (void)jc_snprintf(logpath, sizeof logpath, "%s/%s.txt", rpath,
                              dbin);
            (void)gsw_write_file(logpath, slot_drv);
            red = (drc != 0) ||
                  (gsw_count_prefix(slot_drv, "not ok") > 0);
            if (red) {
                size_t bl = strlen(bad);
                /* the space-join WITHOUT a leading space (the count line is
                 * `red: <b1> <b2>`; the empty set is `red: none`). */
                if (bl == 0) {
                    if (strlen(dbin) + 1 <= sizeof bad) {
                        memcpy(bad, dbin, strlen(dbin) + 1);
                    }
                } else if (bl + 1 + strlen(dbin) + 1 <= sizeof bad) {
                    memcpy(bad + bl, " ", 1);
                    memcpy(bad + bl + 1, dbin, strlen(dbin) + 1);
                }
                if (jc_gatesweep_red_line(dbin, drc, line, sizeof line)
                    < 0 ||
                    gsw_sb(&record, line) != JC_OK) {
                    loop_fail = 1;
                    break;
                }
                {
                    char exc[GSW_LINE_MAX];
                    if (jc_gatesweep_notok_excerp(slot_drv, exc,
                                                  sizeof exc) >= 0) {
                        if (gsw_sb(&record, exc) != JC_OK) {
                            loop_fail = 1;
                            break;
                        }
                    }
                }
            }
        }
        if (loop_fail) {
            rc = 1;
            goto fail;
        }
        if (jc_gatesweep_count_line(nrun, s, bad, line, sizeof line) < 0) {
            rc = 1;
            goto fail;
        }
        if (gsw_sb(&record, line) != JC_OK) {
            rc = 1;
            goto fail;
        }
    }

    /* :27 the clone's `git status --short | head -3` (the reference prints
     * the driver artifacts bare, after the count line, before the mutant
     * stage; a clean clone prints nothing). F3 (M1154 T6): the keyless
     * gsw_run child; the first three non-empty lines as-is (no label, no
     * cut -- the reference's bytes). */
    {
        char status_text[GSW_CAP_TEXT];

        (void)gsw_run(clone, "git status --short", NULL, 0,
                      (int)sizeof status_text, status_text);
        gsw_status_lines(&record, status_text, 3);
    }

    /* :32-39 the smoke-mutant (the reference's
     * `MB=$(git -C "$H/push.git" rev-parse --verify -q
     * refs/remotes/origin/master^{commit})`; absent -> the INVALID line, and
     * the port ENDS the stage rc 1 -- the note's behaviour 13b CHANGED: the
     * reference's continue becomes the orchestrator's invalid-<stage> arm).
     */
    mb[0] = '\0';
    {
        /* F2 (M1154 T6): the git READ child is a gsw_run shell child -- the
         * stage's own 16-name keyless env (jc_proc's built-in scrub lacks
         * ZD_TOKEN_FILE, the one name that leaked; the 4-slot cred_env of
         * jc_hp_git_run cannot carry it and is not ours to change). The
         * single quote keeps ^{commit} out of the shell's reach (sh -c). */
        n = jc_snprintf(slot_text, sizeof slot_text,
                        "git -C '%s' rev-parse --verify -q "
                        "'refs/remotes/origin/master^{commit}'",
                        repo);
        if (n < 0 || (size_t)n >= sizeof slot_text ||
            gsw_run(NULL, slot_text, NULL, 0, (int)sizeof mb, mb) != 0) {
            mb[0] = '\0';
        }
        /* strip the trailing newline(s) of the capture */
        {
            size_t ml = strlen(mb);
            while (ml > 0 && (mb[ml - 1] == '\n' || mb[ml - 1] == '\r')) {
                mb[--ml] = '\0';
            }
        }
    }
    if (mb[0] == '\0') {
        if (jc_gatesweep_mutant_nomb_line(line, sizeof line) < 0) {
            rc = 1;
            goto fail;
        }
        if (gsw_sb(&record, line) != JC_OK) {
            rc = 1;
            goto fail;
        }
        ended_invalid = 1;
        rc = 1;
        goto fail;
    }
    m_cmd = (mutant_run != NULL && mutant_run[0] != '\0')
                ? mutant_run
                : (cfg.host_mutant_run != NULL &&
                   cfg.host_mutant_run[0] != '\0')
                      ? cfg.host_mutant_run
                      : "make smoke-mutant";
    {
        char env_tmp[2064];
        char env_mb[2064];
        const char *env1[3];
        int ne = 0;
        int mrc;

        if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd) >= 0) {
            env1[ne++] = env_tmp;
        }
        if (jc_snprintf(env_mb, sizeof env_mb, "MUTANT_BASE=%s", mb) >= 0) {
            env1[ne++] = env_mb;
        }
        env1[ne] = NULL;
        mrc = gsw_run(clone, m_cmd, env1, ne, (int)sizeof slot_text,
                      slot_text);
        (void)jc_snprintf(logpath, sizeof logpath, "%s/mutant.txt", rpath);
        (void)gsw_write_file(logpath, slot_text);
        if (jc_gatesweep_mutant_line(mrc, mb, slot_text, line, sizeof line)
            < 0) {
            rc = 1;
            goto fail;
        }
        if (gsw_sb(&record, line) != JC_OK) {
            rc = 1;
            goto fail;
        }
        {
            char exc[GSW_LINE_MAX];
            if (jc_gatesweep_mutant_notok(slot_text, exc, sizeof exc) >= 0) {
                if (gsw_sb(&record, exc) != JC_OK) {
                    rc = 1;
                    goto fail;
                }
            }
        }
    }

    /* :43-53 the unit san/o2 (each in its OWN scratch clone, the reference's
     * `make -j8 WERROR=1 SAN=1 test` / `make -j8 WERROR=1 CC=gcc OPT=-O2
     * test`; the port's shimmable --unit-run-san / --unit-run-o2). A clone
     * failure prints `  <v> clone failed: INVALID` and SKIPS that suite's
     * line (the reference's `continue`). */
    {
        const char *vars[2];
        int v;

        vars[0] = "san";
        vars[1] = "o2";
        u_cmd[0] = (unit_run_san != NULL && unit_run_san[0] != '\0')
                       ? unit_run_san
                       : (cfg.host_unit_run_san != NULL &&
                          cfg.host_unit_run_san[0] != '\0')
                           ? cfg.host_unit_run_san
                           : "make -j8 WERROR=1 SAN=1 test";
        u_cmd[1] = (unit_run_o2 != NULL && unit_run_o2[0] != '\0')
                       ? unit_run_o2
                       : (cfg.host_unit_run_o2 != NULL &&
                          cfg.host_unit_run_o2[0] != '\0')
                           ? cfg.host_unit_run_o2
                           : "make -j8 WERROR=1 CC=gcc OPT=-O2 test";
        for (v = 0; v < 2; v++) {
            char vdir[2048];
            char vlog[2048];
            char env_tmp[2064];
            const char *env1[2];
            int ne = 0;
            int urc;
            char exc[GSW_LINE_MAX];

            if (jc_snprintf(vdir, sizeof vdir, "%s/%s", wpath, vars[v])
                < 0 || (size_t)(jc_snprintf(vdir, sizeof vdir, "%s/%s", wpath,
                                            vars[v])) >= sizeof vdir) {
                rc = 1;
                goto fail;
            }
            n = jc_snprintf(slot_text, sizeof slot_text,
                            "git clone -q --no-local --no-checkout '%s' "
                            "'%s' && git -C '%s' checkout -q --detach '%s'",
                            repo, vdir, vdir, s);
            if (n < 0 || (size_t)n >= sizeof slot_text ||
                gsw_run(wpath, slot_text, NULL, 0, (int)sizeof line, line)
                    != 0) {
                (void)jc_snprintf(line, sizeof line, "  %s clone failed: "
                                                    "INVALID\n", vars[v]);
                if (gsw_sb(&record, line) != JC_OK) {
                    rc = 1;
                    goto fail;
                }
                continue;
            }
            if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd)
                >= 0) {
                env1[ne++] = env_tmp;
            }
            env1[ne] = NULL;
            urc = gsw_run(vdir, u_cmd[v], env1, ne, (int)sizeof slot_text,
                          slot_text);
            (void)jc_snprintf(vlog, sizeof vlog, "%s/unit-%s.txt", rpath,
                              vars[v]);
            (void)gsw_write_file(vlog, slot_text);
            if (jc_gatesweep_unit_line(vars[v], urc, slot_text, line,
                                       sizeof line) < 0) {
                rc = 1;
                goto fail;
            }
            if (gsw_sb(&record, line) != JC_OK) {
                rc = 1;
                goto fail;
            }
            if (jc_gatesweep_unit_excerp(slot_text, exc, sizeof exc) >= 0) {
                if (gsw_sb(&record, exc) != JC_OK) {
                    rc = 1;
                    goto fail;
                }
            }
        }
    }

    /* :58-69 the container stage (the two-userns drivers). CHANGED (the
     * note's behaviour 3): the port takes a shimmable --container-run
     * (config host.containerRun). With a command: run it in the ctr clone
     * and pass its output through verbatim (the reference's inline script
     * is the one that prints the `  container <d> ...` lines). Without one:
     * the reference's podman command, or -- no podman on PATH -- the two
     * `  container <d> ... INVALID (podman not available)` lines. */
    {
        char cdir[2048];
        const char *ccmd;
        char env_tmp[2064];
        const char *env1[2];
        int ne = 0;
        int crc;

        if (container_run != NULL && container_run[0] != '\0') {
            ccmd = container_run;
        } else if (cfg.host_container_run != NULL &&
                   cfg.host_container_run[0] != '\0') {
            ccmd = cfg.host_container_run;
        } else {
            ccmd = NULL;
        }
        if (ccmd != NULL) {
            int n_cd1 = jc_snprintf(cdir, sizeof cdir, "%s/ctr", wpath);
            if (n_cd1 < 0 || n_cd1 >= (int)sizeof cdir) {
                rc = 1;
                goto fail;
            }
            n = jc_snprintf(slot_text, sizeof slot_text,
                            "git clone -q --no-local --no-checkout '%s' "
                            "'%s' && git -C '%s' checkout -q --detach '%s'",
                            repo, cdir, cdir, s);
            if (n < 0 || (size_t)n >= sizeof slot_text ||
                gsw_run(wpath, slot_text, NULL, 0, (int)sizeof line, line)
                    != 0) {
                (void)gsw_sb(&record, "  container stage did not run: "
                                              "INVALID\n");
            } else {
                if (jc_snprintf(env_tmp, sizeof env_tmp, "TMPDIR=%s", tmpd)
                    >= 0) {
                    env1[ne++] = env_tmp;
                }
                env1[ne] = NULL;
                crc = gsw_run(cdir, ccmd, env1, ne, (int)sizeof slot_text,
                              slot_text);
                (void)jc_snprintf(logpath, sizeof logpath, "%s/container.txt",
                                  rpath);
                (void)gsw_write_file(logpath, slot_text);
                if (crc != 0) {
                    (void)gsw_sb(&record, "  container stage did not run: "
                                                "INVALID\n");
                } else if (slot_text[0] != '\0') {
                    /* verbatim (the reference's inline script's lines) */
                    if (gsw_sb(&record, slot_text) != JC_OK) {
                        rc = 1;
                        goto fail;
                    }
                }
            }
        } else {
            /* The reference's default: probe podman, else the INVALID lines. */
            if (gsw_run(NULL, "command -v podman", NULL, 0,
                        (int)sizeof line, line) != 0) {
                static const char *cd[2];
                int k;
                cd[0] = "fetch_ssrf_reuse";
                cd[1] = "resolve_timeout_connect";
                for (k = 0; k < 2; k++) {
                    if (jc_gatesweep_container_podman_line(cd[k], line,
                                                           sizeof line) < 0) {
                        rc = 1;
                        goto fail;
                    }
                    if (gsw_sb(&record, line) != JC_OK) {
                        rc = 1;
                        goto fail;
                    }
                }
            } else {
                int n_cd2 = jc_snprintf(cdir, sizeof cdir, "%s/ctr", wpath);
                if (n_cd2 < 0 || n_cd2 >= (int)sizeof cdir) {
                    rc = 1;
                    goto fail;
                }
                n = jc_snprintf(slot_text, sizeof slot_text,
                                "git clone -q --no-local --no-checkout '%s' "
                                "'%s' && git -C '%s' checkout -q --detach "
                                "'%s'",
                                repo, cdir, cdir, s);
                if (n < 0 || (size_t)n >= sizeof slot_text ||
                    gsw_run(wpath, slot_text, NULL, 0, (int)sizeof line, line)
                        != 0) {
                    (void)gsw_sb(&record, "  container stage did not run: "
                                                "INVALID\n");
                } else {
                    if (gsw_podman_default(podman_cmd, sizeof podman_cmd,
                                           cdir, rpath) < 0) {
                        rc = 1;
                        goto fail;
                    }
                    crc = gsw_run(cdir, podman_cmd, NULL, 0,
                                  (int)sizeof slot_text, slot_text);
                    (void)jc_snprintf(logpath, sizeof logpath,
                                      "%s/container.txt", rpath);
                    (void)gsw_write_file(logpath, slot_text);
                    if (crc != 0) {
                        (void)gsw_sb(&record, "  container stage did not "
                                                "run: INVALID\n");
                    } else if (slot_text[0] != '\0') {
                        if (gsw_sb(&record, slot_text) != JC_OK) {
                            rc = 1;
                            goto fail;
                        }
                    }
                }
            }
        }
    }

    /* :70 the records line (LAST -- the stage's rc is 0 iff it printed). */
    if (jc_gatesweep_records_line(rpath, line, sizeof line) < 0) {
        rc = 1;
        goto fail;
    }
    if (gsw_sb(&record, line) != JC_OK) {
        rc = 1;
        goto fail;
    }
    n = gsw_print(record.data != NULL ? record.data : "");
    if (n != 0) {
        rc = 1;
        goto fail;
    }
    (void)ended_invalid;
    rc = 0;
    goto cleanup;

fail:
    /* Flush whatever was assembled (a partial record is still the stage's
     * output: the verdict / the orchestrator read the markers in it). */
    if (record.data != NULL) {
        (void)gsw_print(record.data);
    }
cleanup:
    jc_sb_free(&record);
    jc_vec_free(&drivers);
    return rc;
}
