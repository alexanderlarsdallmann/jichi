/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateci.c - the `gate run --stage ci` body (M1154, batch H9b,
 * wave 3, task 8): the C89 port of the 2026-10-27 hardening-host drive's
 * ci-gate.sh, read line by line: :13-14 the clone home (CHANGED: a dead
 * clone is MOVED ASIDE to <home>.dead-<UTC yyyymmdd-HHMMSS> -- M1191-a:
 * to the FIRST FREE name of that shape (the stamp, then the stamp plus
 * -1, -2, ...) via rename(), the marker `  ci: moved a dead clone aside:
 * <home> -> <new>` on the record (naming the target actually used; a
 * failed rename() gets its own record line naming both paths and the
 * error, rc 1, never silent), and the stage continues -- the reference's
 * :18 refusal exits 1), :17 the
 * mkdir of the log dir, :19 the clone + detach checkout (ONE keyless shell
 * child, the sweep's gsw_run shape; failure -> `  ci: clone/checkout
 * failed: INVALID`, rc 1, M1154.b11), :20 the checkout verify (a
 * rev-parse in the clone vs a keyless git READ rev-parse of <s>^{commit}
 * in the push repo; mismatch -> `  ci: checkout mismatch: INVALID`, rc 1),
 * :21-25 the origin/master + merge-base block (four keyless READ children;
 * the two >&2 diagnostics go to the RECORD, the sweep idiom; no
 * origin/master / no merge-base / merge-base not in the clone are INVALID
 * rc 1; the :24 warning and the :25 MUTANT_BASE line are KEPT with their
 * `ci-gate.sh: ` prefix, verbatim), :26 the short TMPDIR <work>/tmp
 * (CHANGED: the reference's /tmp/jfx-ci-$C), :27 the meta start line, :28
 * the run (the --ci-run command as ONE child, workdir = the clone, stdin
 * /dev/null, stdout+stderr -> the .log; CHANGED: NO setsid -- the port's
 * child IS the unit's process -- and NO timeout -- the drive's rule;
 * the child env is the 16 keyless names PLUS TMPDIR / MUTANT_BASE /
 * JC_SMOKE_KEEP_GOING), :30 the meta end line, :31 NOT ported (the
 * ORCHESTRATOR's diagnostic half; the record gets the gate-run.sh:77 rc
 * line +, on FAIL, the :79 RED half with the SV:1238 excerpt attach +
 * the records line).
 *
 * The PURE seam (the rc line, the RED join's exact tr bytes, the RED
 * line, the excerpt block selection + text, the two KEPT-prefix
 * diagnostics, the key names, the run env) is exported through
 * jc_hp_gateci.h and unit-tested by tests/test_gateci.c.
 *
 * The stage's record is its assembled STDOUT: the driver's C-cells
 * redirect it to a file and grep it; the orchestrator (wave 4) redirects
 * it to <gate-home>/ci.txt. The .log / .log.meta / .excerpt are the
 * stage's other FILES (the reference's own paths under the work and log
 * dirs). The .records dir is created (the sweep idiom) so the records
 * line names a real dir; no per-cell file is written there. */

/* mkdtemp is POSIX-2008: the build-wide -D_POSIX_C_SOURCE=200112L does NOT
 * declare it (the M1134 / T5 idiom: the per-TU _XOPEN_SOURCE 700 guard
 * BEFORE the first header that reaches features.h exposes it). */
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

#include "jc_hp_gateci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "jc_fcntl.h"      /* JC_O_BINARY (portability_lint 38: file opens) */

#include "jc_harness_config.h" /* the config load (the host block's fields) */
#include "jc_mem.h"            /* jc_arena_strdup */
#include "jc_platform.h"       /* jc_size, JC_OK, jc_shell_path */
#include "jc_proc.h"           /* jc_proc_capture, jc_proc_waitpid */
#include "jc_snprintf.h"       /* jc_snprintf, never sprintf */
#include "jc_spawn.h"          /* jc_spawn (the :28 uncapped run child) */
#include "jc_str.h"            /* struct jc_sb */
#include "jc_vec.h"            /* struct jc_vec */

#define GCI_LINE_MAX  (256 * 1024) /* one captured line / read */
#define GCI_LOG_MAX   (32 * 1024 * 1024) /* the .log read for the step-10 lines */
#define GCI_REC_MAX   (256 * 1024) /* the stage's assembled record */
#define GCI_LINE_CUT  300          /* the excerpt's per-line cut (SV:1238) */
#define GCI_JOIN_CUT  160          /* gate-run.sh:79 cut -c1-160 */

/* The API-key envs the reference unsets (ci-gate.sh:28's 16 names, the
 * sweep's gsw_keys byte-for-byte). The port sets them EMPTY in the run
 * child's env (the sweep's KEYS UNSET, behaviour 3 CHANGED idiom): setting
 * them to "" also overrides a registered-secret name, so the scrub cannot
 * re-add the value. */
static const char *const gci_keys[] = {
    "JICHI_API_KEY", "JLU_API_KEY", "ANTHROPIC_API_KEY", "OPENAI_API_KEY",
    "OPENROUTER_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY",
    "MISTRAL_API_KEY", "DEEPSEEK_API_KEY", "TOGETHER_API_KEY",
    "FIREWORKS_API_KEY", "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
    "ZD_TOKEN_FILE"
};
#define GCI_NKEYS (int)(sizeof gci_keys / sizeof gci_keys[0])

/* ---------------------------------------------------------------------------
 * Small text helpers (pure, bounded -- every walk is bounded by the NUL).
 * ----------------------------------------------------------------------- */

/* Number of lines in `text` (the gci_lines count, NO storage -- the
 * streaming excerpt passes need the total only to clip the twenty-after
 * window at the log's end). F2: the fixed `long ls[512]` line split is
 * gone from the excerpt path; these helpers walk the log's newlines
 * directly. */
static int
gci_count_lines(const char *text)
{
    size_t off;
    size_t len;
    int n = 0;

    if (text == NULL) {
        return 0;
    }
    len = strlen(text);
    off = 0;
    while (off < len) {
        size_t end = off;
        while (end < len && text[end] != '\n') {
            end++;
        }
        if (end > off) {
            n++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return n;
}

/* The line range of the excerpt block anchored at line `i`: the ONE line
 * before (clipped to 0 when the match is the first line) through up to the
 * TWENTY lines after (clipped to `total` at the log's end). */
static void
gci_block_range(int i, int total, int *first, int *last)
{
    *first = (i > 0) ? i - 1 : 0;
    *last = i + 1 + 20;
    if (*last > total) {
        *last = total;
    }
}

/* The LAST line of `log` matching `^ci: OK` (the gate-run.sh:76
 * `grep '^ci: OK' | tail -1`): copied into `out` (NUL-terminated, cut to
 * `cap`); out is "" when there is no match (the reference's ${okl:-}). */
static void
gci_last_okl(const char *log, char *out, size_t cap)
{
    size_t off;
    size_t len;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (log == NULL) {
        return;
    }
    len = strlen(log);
    off = 0;
    while (off < len) {
        size_t start = off;
        size_t end = start;
        size_t n;
        while (end < len && log[end] != '\n') {
            end++;
        }
        if (strncmp(log + start, "ci: OK", 6) == 0) {
            n = end - start;
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, log + start, n);
            out[n] = '\0';
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
}

/* Does the line (text + start, len) match the gate-run.sh:79 grep set:
 * `^smoke: FAILED --` OR `^not ok` OR `^\*\* \[` OR `^ *FAIL ` (anchored
 * at the line start; `^ *` = any spaces then FAIL then a space)? */
static int
gci_red_match(const char *text, size_t start, size_t len)
{
    if (len >= 16 && memcmp(text + start, "smoke: FAILED --", 16) == 0) {
        return 1;
    }
    if (len >= 6 && memcmp(text + start, "not ok", 6) == 0) {
        return 1;
    }
    if (len >= 5 && memcmp(text + start, "*** [", 5) == 0) {
        return 1;
    }
    if (len >= 5 && memcmp(text + start, "FAIL ", 5) == 0) {
        return 1;
    }
    {
        size_t i = 0;
        while (i < len && text[start + i] == ' ') {
            i++;
        }
        if (i + 5 <= len && memcmp(text + start + i, "FAIL ", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Does the line match `^not ok` (the excerpt's block anchor)? */
static int
gci_notok_match(const char *text, size_t start, size_t len)
{
    return len >= 6 && memcmp(text + start, "not ok", 6) == 0;
}

/* ---------------------------------------------------------------------------
 * The PURE seam (exported, unit-tested).
 * ----------------------------------------------------------------------- */

/* jc_gateci_rc_line: gate-run.sh:77's `say "ci: rc=$cr ${okl:-no 'ci: OK'
 * line}"` CHANGED into the record (`  ci: ` prefix). */
int
jc_gateci_rc_line(int r, const char *okl, char *out, size_t cap)
{
    const char *tail = (okl != NULL && okl[0] != '\0')
                           ? okl
                           : "no 'ci: OK' line";
    return jc_snprintf(out, cap, "  ci: rc=%d %s\n", r, tail);
}

/* jc_gateci_red_join: gate-run.sh:79's EXACT bytes -- the first four lines
 * of `log` matching the grep set, each cut to 160, joined by ';' with no
 * added space. The reference's `tr '\n' ';'` turns EVERY matched line's
 * newline into a ';', so the join carries a TRAILING ';' (and is EMPTY
 * when nothing matches -- the reference's `$( ... )` substitution then
 * leaves its own double space beside "Log on the host"). */
int
jc_gateci_red_join(const char *log, char *out, size_t cap)
{
    size_t off;
    size_t len;
    size_t pos = 0;
    int k = 0;
    int need;

    if (cap == 0) {
        return -1;
    }
    out[0] = '\0';
    if (log == NULL) {
        return 0;
    }
    len = strlen(log);
    off = 0;
    while (off < len && k < 4) {
        size_t start = off;
        size_t end = start;
        size_t n;
        while (end < len && log[end] != '\n') {
            end++;
        }
        if (gci_red_match(log, start, end - start)) {
            n = end - start;
            if (n > GCI_JOIN_CUT) {
                n = GCI_JOIN_CUT;
            }
            /* The reference's `tr '\n' ';'` turns EVERY matched line's own
             * trailing newline into a ';', so each matched line is followed
             * by exactly one ';' (including the last -> the trailing ';').
             * No leading ';'. */
            need = (int)(pos + n + 1);
            if ((size_t)need >= cap) {
                return -1;
            }
            memcpy(out + pos, log + start, n);
            pos += n;
            out[pos++] = ';';
            k++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    out[pos] = '\0';
    return (int)pos;
}

/* jc_gateci_red_line: the note's b6 RED half (the stage's own copy of the
 * orchestrator's `red ci` text, so wave 4 lifts it verbatim) + the SV:1238
 * ask-3 excerpt attach: `  ci: RED make ci rc=<r>. <join> Log on the host:
 * <log path>. (excerpt: <excerpt path>)\n`. The reference's `$(...)`
 * substitution leaves the join's surrounding space -- an EMPTY join is the
 * reference's double space, kept. */
int
jc_gateci_red_line(int r, const char *log, const char *log_path,
                   const char *excerpt_path, char *out, size_t cap)
{
    char join[GCI_LINE_MAX];
    int n;

    n = jc_gateci_red_join(log, join, sizeof join);
    if (n < 0) {
        return -1;
    }
    return jc_snprintf(out, cap,
                       "  ci: RED make ci rc=%d. %s Log on the host: %s. "
                       "(excerpt: %s)\n",
                       r, join, log_path, excerpt_path);
}

/* jc_gateci_excerpt_blocks: every `^not ok` line of `log`: `starts[i]` is
 * its byte offset, `counts[i]` the lines in its block (the ONE line before
 * it + the line + up to the TWENTY lines after it, clipped at the log's
 * end). Returns the match count, -1 when a match would exceed `cap`.
 * F2: the lines are walked in a single streaming pass (the gci_red_join
 * shape, no fixed 512-line split): a `^not ok` line past line 512 is
 * found exactly like the first 512. */
int
jc_gateci_excerpt_blocks(const char *log, long *starts, int *counts, int cap)
{
    size_t off;
    size_t len;
    int total;
    int i = 0;
    int nmatch = 0;
    int first;
    int last;

    if (cap < 1 || (log != NULL && (starts == NULL || counts == NULL))) {
        return -1;
    }
    if (log == NULL) {
        return 0;
    }
    total = gci_count_lines(log);
    len = strlen(log);
    off = 0;
    while (off < len) {
        size_t s = off;
        size_t end = s;
        while (end < len && log[end] != '\n') {
            end++;
        }
        if (end > s) {
            if (gci_notok_match(log, s, end - s)) {
                if (nmatch >= cap) {
                    return -1; /* more matches than the caller's arrays hold */
                }
                gci_block_range(i, total, &first, &last);
                starts[nmatch] = (long)s;
                counts[nmatch] = last - first;
                nmatch++;
            }
            i++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return nmatch;
}

/* F2: the excerpt block anchored at line `match` (of `total` lines),
 * EMITTED to `out` (bounded by `cap`, `*pos` the running offset): the
 * lines first..last-1 (gci_block_range), each cut to GCI_LINE_CUT,
 * newline-terminated, preceded by ONE blank line when `blank` (a block
 * after the first). The block's lines are re-scanned from the log's start
 * (bounded: `first` skips + `last-first` emits; the re-scan is what makes
 * the single streaming walk enough -- no fixed line array). 0 ok, -1 when
 * the text would exceed `cap`. */
static int
gci_emit_block_text(const char *log, int match, int total, int blank,
                    char *out, size_t cap, size_t *pos)
{
    int first;
    int last;
    int i;
    size_t off;
    size_t len = strlen(log);

    gci_block_range(match, total, &first, &last);
    off = 0;
    for (i = 0; i < first && off < len; i++) {
        size_t end = off;
        while (end < len && log[end] != '\n') {
            end++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    if (blank) {
        if (*pos + 1 >= cap) {
            return -1;
        }
        out[(*pos)++] = '\n'; /* one blank line between blocks */
    }
    for (i = first; i < last && off < len; i++) {
        size_t end = off;
        size_t n;
        while (end < len && log[end] != '\n') {
            end++;
        }
        n = end - off;
        if (n > GCI_LINE_CUT) {
            n = GCI_LINE_CUT;
        }
        if (*pos + n + 1 >= cap) {
            return -1;
        }
        memcpy(out + *pos, log + off, n);
        *pos += n;
        out[(*pos)++] = '\n';
        off = end;
        if (end < len) {
            off++;
        }
    }
    return 0;
}

/* F2: the SAME block EMITTED to the file (the stage's unbounded half: one
 * line at a time, a per-LINE buffer, the file size uncapped). 0 ok, -1 on
 * a write fault. */
static int
gci_emit_block_file(FILE *f, const char *log, int match, int total,
                    int blank)
{
    char ln[GCI_LINE_MAX];
    int first;
    int last;
    int i;
    size_t off;
    size_t len = strlen(log);

    gci_block_range(match, total, &first, &last);
    off = 0;
    for (i = 0; i < first && off < len; i++) {
        size_t end = off;
        while (end < len && log[end] != '\n') {
            end++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    if (blank) {
        if (fputc('\n', f) == EOF) {
            return -1; /* one blank line between blocks */
        }
    }
    for (i = first; i < last && off < len; i++) {
        size_t end = off;
        size_t n;
        while (end < len && log[end] != '\n') {
            end++;
        }
        n = end - off;
        if (n > GCI_LINE_CUT) {
            n = GCI_LINE_CUT;
        }
        if (n > 0) {
            memcpy(ln, log + off, n);
            if (fwrite(ln, 1, n, f) != n) {
                return -1;
            }
        }
        if (fputc('\n', f) == EOF) {
            return -1;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    return 0;
}

/* F2: the single streaming walk over the log's lines (the gci_red_join
 * shape, no fixed line array): the open block is the last `^not ok`
 * match seen (`pending`); it is EMITTED -- in log order -- when a new
 * match appears (a block before its successor) or the walk passes its
 * last line (`i + 1 >= last`, the 20-after window read), and the still-
 * open one at the log's end. `emit` is gci_emit_block_text (the buffer
 * half) or gci_emit_block_file (the file half). 0 ok, -1 on a fault. */
static int
gci_excerpt_walk(const char *log, int (*emit)(const char *, int, int,
                                              int, void *), void *arg)
{
    int total = gci_count_lines(log);
    size_t off = 0;
    size_t len = strlen(log);
    int i = 0;
    int b = 0;
    int pending = -1;

    while (i < total && off < len) {
        size_t s = off;
        size_t end = s;
        int last;
        while (end < len && log[end] != '\n') {
            end++;
        }
        if (end > s) {
            if (gci_notok_match(log, s, end - s)) {
                if (pending >= 0) {
                    if (emit(log, pending, total, b > 0, arg) != 0) {
                        return -1;
                    }
                    b++;
                }
                pending = i;
            } else if (pending >= 0) {
                int bfirst;
                gci_block_range(pending, total, &bfirst, &last);
                if (i + 1 >= last) {
                    if (emit(log, pending, total, b > 0, arg) != 0) {
                        return -1;
                    }
                    b++;
                    pending = -1;
                }
            }
            i++;
        }
        off = end;
        if (end < len) {
            off++;
        }
    }
    if (pending >= 0) {
        if (emit(log, pending, total, b > 0, arg) != 0) {
            return -1;
        }
    }
    return 0;
}

/* The walk's buffer half: out/cap/pos ride in the arg struct. */
struct gci_walk_buf {
    char *out;
    size_t cap;
    size_t pos;
};

static int
gci_emit_text_cb(const char *log, int match, int total, int blank, void *arg)
{
    struct gci_walk_buf *w = (struct gci_walk_buf *)arg;
    return gci_emit_block_text(log, match, total, blank, w->out, w->cap,
                               &w->pos);
}

/* jc_gateci_excerpt_text: the blocks' TEXT -- for every `^not ok` line,
 * that line with the ONE line before it and the TWENTY lines after it
 * (each line cut to 300 bytes), the blocks in log order separated by one
 * blank line, a trailing newline. The writer's pure half (the stage
 * writes the file through jc_gateci_excerpt_file on FAIL only). Empty (0)
 * when there is no `^not ok` line. F2: streamed (no fixed 512-line split:
 * a match past line 512 is found exactly like the first 512). `cap`
 * still bounds the text: -1 when the text would exceed it. */
int
jc_gateci_excerpt_text(const char *log, char *out, size_t cap)
{
    struct gci_walk_buf w;

    if (cap == 0) {
        return -1;
    }
    out[0] = '\0';
    if (log == NULL) {
        return 0;
    }
    w.out = out;
    w.cap = cap;
    w.pos = 0;
    if (gci_excerpt_walk(log, gci_emit_text_cb, &w) != 0) {
        out[0] = '\0';
        return -1;
    }
    out[w.pos] = '\0';
    return (int)w.pos;
}

/* The walk's file half: the FILE rides in the arg. */
static int
gci_emit_file_cb(const char *log, int match, int total, int blank, void *arg)
{
    FILE *f = (FILE *)arg;
    return gci_emit_block_file(f, log, match, total, blank);
}

/* jc_gateci_excerpt_file: the STAGE's file half (F2: the pre-F2 64KB
 * `exc[GCI_EXC_CAP]` buffer + gci_write_file is gone -- the file is
 * streamed one line at a time and is UNBOUNDED in size): the SAME blocks
 * as jc_gateci_excerpt_text (for every `^not ok` line, the ONE line
 * before it + the line + up to the TWENTY lines after it, each cut to 300
 * bytes, the blocks in log order separated by one blank line), written to
 * `path` (create/truncate). `log` is the log TEXT (the stage passes its
 * read .log). JC_OK on success (an empty excerpt still creates and
 * truncates the file, as gci_write_file did when the text was empty), -1
 * on a write fault. */
int
jc_gateci_excerpt_file(const char *log, const char *path)
{
    FILE *f;

    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    if (log != NULL && gci_excerpt_walk(log, gci_emit_file_cb, f) != 0) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return JC_OK;
}

/* jc_gateci_mergebase_warn: ci-gate.sh:24 KEPT with its prefix (the
 * reference's diagnostic sentence, verbatim -- do not strip it). */
int
jc_gateci_mergebase_warn(const char *b8, const char *s, char *out,
                         size_t cap)
{
    return jc_snprintf(out, cap,
                       "ci-gate.sh: GitLab master %s is not an ancestor of "
                       "%s; the sweep uses their merge-base\n",
                       b8, s);
}

/* jc_gateci_mutant_base_line: ci-gate.sh:25 KEPT with its prefix (the
 * reference's own printf: the FULL 40-hex MB, the 8-hex B). */
int
jc_gateci_mutant_base_line(const char *mb, const char *b8, char *out,
                           size_t cap)
{
    return jc_snprintf(out, cap,
                       "ci-gate.sh: MUTANT_BASE=%s (GitLab master as "
                       "push.sh fetched it: %s)\n",
                       mb, b8);
}

/* jc_gateci_key_names: the 16 names (the sweep's unit-pin idiom). */
int
jc_gateci_key_names(const char **out, int cap)
{
    int i;

    if (out == NULL || cap < GCI_NKEYS) {
        return -1;
    }
    for (i = 0; i < GCI_NKEYS; i++) {
        out[i] = gci_keys[i];
    }
    return GCI_NKEYS;
}

/* jc_gateci_run_env: the 16 names set EMPTY (the keyless scrub) PLUS
 * TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING, as "KEY=VALUE" entries
 * pushed into the caller-initialized `out` (persist in `a`). */
int
jc_gateci_run_env(struct jc_arena *a, const char *td, const char *mb,
                  const char *n, struct jc_vec *out)
{
    char slot[GCI_NKEYS][128];
    char extra[3][512];
    int i;

    if (a == NULL || out == NULL || td == NULL || mb == NULL || n == NULL) {
        return -1;
    }
    for (i = 0; i < GCI_NKEYS; i++) {
        char *s;
        char *cp;
        if (jc_snprintf(slot[i], sizeof slot[i], "%s=", gci_keys[i]) < 0) {
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
    if (jc_snprintf(extra[0], sizeof extra[0], "TMPDIR=%s", td) < 0 ||
        jc_snprintf(extra[1], sizeof extra[1], "MUTANT_BASE=%s", mb) < 0 ||
        jc_snprintf(extra[2], sizeof extra[2],
                    "JC_SMOKE_KEEP_GOING=%s", n) < 0) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        char *s;
        char *cp = jc_arena_strdup(a, extra[i]);
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

/* ---------------------------------------------------------------------------
 * The stage body's small helpers.
 * ----------------------------------------------------------------------- */

/* s is 7-40 lowercase hex (the reference's $C; the orchestrator, wave 4,
 * passes the full sha -- the echoed value, so the paths and lines match
 * either). */
static int
gci_hex8(const char *s)
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

/* --ci-smoke-keep-going's numeric check (the brief: a non-numeric value is
 * a usage refusal exit 2 naming the flag -- the loader does not refuse, the
 * command does, the sweep idiom). All digits, at least one. */
static int
gci_all_digits(const char *s)
{
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    return 1;
}

/* mkdir -p `dir` (the work / log / records / tmp homes). 0 ok. */
static int
gci_mkdir_p(const char *dir)
{
    char buf[2048];
    char *slash;
    size_t n = strlen(dir);
    int rc;

    if (n == 0 || n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, dir, n);
    buf[n] = '\0';
    for (slash = buf + 1; *slash != '\0'; slash++) {
        if (*slash == '/') {
            *slash = '\0';
            if (mkdir(buf, 0700) != 0 && errno != EEXIST) {
                return -1;
            }
            *slash = '/';
        }
    }
    rc = mkdir(buf, 0700);
    return (rc == 0 || errno == EEXIST) ? 0 : -1;
}

/* Read a whole file into a sb (the .log read for the step-10 lines, the
 * meta's start shape the driver greps). 0 ok, -1 on a read fault. */
static int
gci_read_file(struct jc_sb *out, const char *path)
{
    FILE *f = fopen(path, "rb");
    int c;

    if (f == NULL) {
        return -1;
    }
    while ((c = fgetc(f)) != EOF) {
        if (jc_sb_append_char(out, (char)c) != JC_OK) {
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}

/* Write `text` (NUL-terminated) to `path`, truncating. 0 ok. */
static int
gci_write_file(const char *path, const char *text)
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

/* Does `path` exist (the dead-clone test, the excerpt-absence assert)? */
static int
gci_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

/* One keyless git READ child: `git -C <repo> <args>`, combined output
 * captured. The keyless env is the 16 names set EMPTY (the reads see no
 * keys; the sweep's T6 fix: route the READ children through the explicit
 * env, not the default environ). Returns the child's exit status, -1 on
 * spawn failure. */
static int
gci_git_read(const char *repo, const char *args, char *out, int cap)
{
    char *argv[4];
    char cmd[GCI_LINE_MAX];
    char slot[GCI_NKEYS][128];
    struct jc_vec envv;
    struct jc_sb capture;
    int i;
    int rc;

    if (jc_snprintf(cmd, sizeof cmd, "git -C '%s' %s", repo, args) < 0) {
        return -1;
    }
    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    argv[0] = (char *)jc_shell_path();
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;
    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < GCI_NKEYS; i++) {
        char *s;
        if (jc_snprintf(slot[i], sizeof slot[i], "%s=", gci_keys[i]) < 0) {
            jc_vec_free(&envv);
            return -1;
        }
        s = slot[i];
        if (jc_vec_push(&envv, &s) != JC_OK) {
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

/* The :28 run child, UNCAPPED (the brief's NO timeout: the reference's
 * setsid -w ran without a deadline; the drive's rule -- a cap that fires
 * manufactures a plausible different answer). ONE keyless shell child
 * (`sh -c <cmd>`, NO setsid: the port's child IS the unit's process),
 * workdir = the clone, stdin /dev/null, stdout+stderr -> the .log's fd
 * (the stage keeps NO output of its own: the record's step-10 lines are
 * assembled AFTER the run, from the .log's bytes), the CHILD env = the 16
 * keyless names PLUS TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING. Returns
 * the child's exit status, -1 on spawn failure, -2 on the (practically
 * unreachable) bound. */
static int
gci_run_child(const char *workdir, const char *cmd, int logfd,
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
    req.fd_stdin = JC_SPAWN_DEVNULL;   /* the reference's < /dev/null */
    req.fd_stdout = logfd;
    req.fd_stderr = logfd;             /* the reference's 2>&1 */
    req.scrub_env = 1;                 /* the 16 names set "" in env win
                                         * over any registered secret */
    req.env = env;
    req.cwd = (char *)workdir;         /* the reference's cd $CI */
    if (jc_spawn(&req, &pid) != JC_OK) {
        return -1;
    }
    /* The UNCAPPED reap (the reference's setsid -w waited forever; the port's
     * child IS the unit's process, so no kill deadline: jc_proc_reap_bounded
     * with deadline 0 = interrupt-aware, no time bound). */
    if (jc_proc_reap_bounded(pid, 0.0, NULL, &status) != 0) {
        return -1;
    }
    if (!WIFEXITED(status)) {
        return 128 + WTERMSIG(status); /* the reference's $? on a signal */
    }
    return WEXITSTATUS(status);
}

/* One keyless shell child (`sh -c <cmd>`; the cmd carries its own -C /
 * cd -- the workdir that the RUN child uses lives in gci_run_child),
 * combined output captured into `out` (NULL = discard, cap bounds it).
 * The CHILD env is the 16 keyless names set EMPTY (the sweep's gsw_run
 * shape: the explicit env, not the default environ). F5: the dead
 * workdir param is dropped (both callers passed NULL). Returns the
 * child's exit status, -1 on spawn failure. */
static int
gci_child(const char *cmd, char *out, int cap)
{
    char *argv[4];
    char slot[GCI_NKEYS][128];
    struct jc_vec envv;
    struct jc_sb capture;
    int i;
    int rc;

    argv[0] = (char *)jc_shell_path();
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;
    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < GCI_NKEYS; i++) {
        char *s;
        if (jc_snprintf(slot[i], sizeof slot[i], "%s=", gci_keys[i]) < 0) {
            jc_vec_free(&envv);
            return -1;
        }
        s = slot[i];
        if (jc_vec_push(&envv, &s) != JC_OK) {
            break;
        }
    }
    jc_sb_init(&capture);
    rc = (int)jc_proc_capture(argv, &envv, NULL, &capture, (jc_size)cap,
                              600L, NULL);
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
    jc_vec_free(&envv);
    return rc;
}

/* Trim leading/trailing whitespace from `in` into `out` (a git read's
 * one-line result, the reference's command-substitution). */
static void
gci_trim(const char *in, char *out, size_t cap)
{
    const char *p;
    size_t n;

    if (cap == 0) {
        return;
    }
    if (in == NULL) {
        out[0] = '\0';
        return;
    }
    p = in;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }
    n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\n' ||
                     p[n - 1] == '\r' || p[n - 1] == '\t')) {
        n--;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* The UTC timestamp the reference's `date -u '+%Y-%m-%dT%H:%M:%SZ'`
 * prints (the meta's start / end lines; strftime is the port). */
static void
gci_utc(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm *u = gmtime(&t);
    if (u == NULL || strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", u) == 0) {
        out[0] = '\0';
    }
}

/* The stage body. */
int
jc_hp_gateci_run(const char *config_path, const char *s, const char *work,
                 const char *records, const char *log_dir,
                 const char *ci_run, const char *keep_going,
                 const char *push_repo, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    struct jc_sb record;
    char home[GCI_LINE_MAX];
    char logdir[GCI_LINE_MAX];
    char recdir[GCI_LINE_MAX];
    char logpath[GCI_LINE_MAX];
    char metapath[GCI_LINE_MAX];
    char excpath[GCI_LINE_MAX];
    char tmpdir[GCI_LINE_MAX];
    const char *pr;
    const char *cr;
    const char *kg;
    char line[GCI_LINE_MAX];
    char slot[512];
    char b_full[GCI_LINE_MAX];
    char mb_full[GCI_LINE_MAX];
    char b8[16];
    char head[GCI_LINE_MAX];
    char want[GCI_LINE_MAX];
    int rc = 0;
    if (config_path == NULL || a == NULL) {
        return 2;
    }
    /* Usage: the SHA8 (7-40 lowercase hex, the sweep idiom). Checked
     * BEFORE the config load (a bad SHA8 is a usage refusal, not a config
     * refusal -- the driver's C8 cell passes both bad values at once). */
    if (s == NULL || !gci_hex8(s)) {
        fprintf(stderr,
                "jichi-harness: gate run --stage ci: the <SHA8> must be "
                "7-40 lowercase hex (got '%s')\n",
                s != NULL ? s : "");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        fprintf(stderr, "jichi-harness: gate run --stage ci: %s\n",
                jc_harness_config_error(&cfg));
        return 2;
    }
    /* --ci-smoke-keep-going: flag, else config, else "0"; a non-numeric
     * value is a usage refusal exit 2 naming the flag (the loader does
     * not refuse, the command does -- the sweep idiom). */
    kg = keep_going != NULL ? keep_going : cfg.host_ci_smoke_keep_going;
    if (kg == NULL) {
        kg = "0";
    }
    if (!gci_all_digits(kg)) {
        fprintf(stderr,
                "jichi-harness: gate run --stage ci: "
                "--ci-smoke-keep-going must be numeric (got '%s')\n", kg);
        return 2;
    }
    /* --push-repo: REQUIRED (flag, else config host.pushRepo, else exit 2
     * naming the flag -- the sweep idiom). */
    pr = push_repo != NULL ? push_repo : cfg.host_push_repo;
    if (pr == NULL) {
        fprintf(stderr,
                "jichi-harness: gate run --stage ci: --push-repo is "
                "required (the clone's source; no host.pushRepo in the "
                "config)\n");
        return 2;
    }
    /* --ci-run: flag, else config, else `make ci` (the reference). */
    cr = ci_run != NULL ? ci_run : cfg.host_ci_run;
    if (cr == NULL) {
        cr = "make ci";
    }
    /* The homes: --work else mkdtemp /tmp/jhp-ci-XXXXXX; --records else
     * <work>/records; the log dir else --ci-log-dir else config
     * host.ciLogDir else <work>/ci. A home that cannot be made is rc 1
     * (the brief: the INVALID class, not a stage RED). */
    {
        char workbuf[GCI_LINE_MAX];
        const char *w = work;

        if (w == NULL) {
            memcpy(workbuf, "/tmp/jhp-ci-XXXXXX",
                   sizeof "/tmp/jhp-ci-XXXXXX");
            if (mkdtemp(workbuf) == NULL) {
                fprintf(stderr,
                        "jichi-harness: gate run --stage ci: cannot make "
                        "the work home\n");
                return 1;
            }
            w = workbuf;
        }
        if (jc_snprintf(home, sizeof home, "%s/ci-%s", w, s) < 0 ||
            jc_snprintf(tmpdir, sizeof tmpdir, "%s/tmp", w) < 0) {
            return 1;
        }
        if (gci_mkdir_p(w) != 0 || gci_mkdir_p(tmpdir) != 0) {
            fprintf(stderr,
                    "jichi-harness: gate run --stage ci: cannot make the "
                    "work home\n");
            return 1;
        }
        if (records == NULL) {
            if (jc_snprintf(recdir, sizeof recdir, "%s/records", w) < 0) {
                return 1;
            }
            records = recdir;
        }
        if (gci_mkdir_p(records) != 0) {
            fprintf(stderr,
                    "jichi-harness: gate run --stage ci: cannot make the "
                    "records dir\n");
            return 1;
        }
        if (log_dir == NULL) {
            log_dir = cfg.host_ci_log_dir;
        }
        if (log_dir == NULL) {
            if (jc_snprintf(logdir, sizeof logdir, "%s/ci", w) < 0) {
                return 1;
            }
            log_dir = logdir;
        }
        if (gci_mkdir_p(log_dir) != 0) {
            fprintf(stderr,
                    "jichi-harness: gate run --stage ci: cannot make the "
                    "log dir\n");
            return 1;
        }
        if (jc_snprintf(logpath, sizeof logpath, "%s/ci-%s.log", log_dir,
                        s) < 0 ||
            jc_snprintf(metapath, sizeof metapath, "%s/ci-%s.log.meta",
                        log_dir, s) < 0 ||
            jc_snprintf(excpath, sizeof excpath, "%s/ci-%s.excerpt",
                        log_dir, s) < 0) {
            return 1;
        }
    }
    jc_sb_init(&record);
    {
        /* Step 1 (:13-14, CHANGED): a dead clone home is MOVED ASIDE to
         * <home>.dead-<UTC yyyymmdd-HHMMSS>, the marker on the record, and
         * the stage continues (the reference's :18 refusal exits 1).
         * M1191-a (def-396), the same-second name collision:
         * B1 -- the target is the FIRST FREE name of that shape: the
         * stamp, then the stamp plus -1, -2, ... until gci_exists says
         * no; the record line names the target ACTUALLY used.
         * B2 -- the move is rename() (ISO C), not the mv shell child:
         * rename() fails on an existing target instead of nesting the
         * clone INTO an existing directory (POSIX mv has no -T; GNU's
         * is not portable to the BSD rows).
         * B3 -- a failed rename() is never silent: the record gets a
         * line naming BOTH paths and the error (errno, via strerror),
         * and the stage ends rc 1 with the record PRINTED (finish).
         * B4 -- nothing is ever removed. */
        if (gci_exists(home)) {
            char newhome[GCI_LINE_MAX];
            char ts[32];
            time_t t = time(NULL);
            struct tm *u = gmtime(&t);
            int k = 0;
            if (u == NULL ||
                strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", u) == 0) {
                ts[0] = '\0';
            }
            if (jc_snprintf(newhome, sizeof newhome, "%s.dead-%s", home,
                            ts) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            while (gci_exists(newhome)) {
                k++;
                if (k >= 1000) {
                    if (jc_snprintf(line, sizeof line,
                                    "  ci: could not find a free "
                                    "dead-clone name for %s: INVALID\n",
                                    home) < 0) {
                        jc_sb_free(&record);
                        return 1;
                    }
                    if (jc_sb_append(&record, line) != JC_OK) {
                        jc_sb_free(&record);
                        return 1;
                    }
                    rc = 1;
                    goto finish;
                }
                if (jc_snprintf(newhome, sizeof newhome, "%s.dead-%s-%d",
                                home, ts, k) < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
            }
            if (rename(home, newhome) != 0) {
                int e = errno;
                if (jc_snprintf(line, sizeof line,
                                "  ci: could not move a dead clone aside: "
                                "%s -> %s: %s (errno %d): INVALID\n",
                                home, newhome, strerror(e), e) < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK) {
                    jc_sb_free(&record);
                    return 1;
                }
                rc = 1;
                goto finish;
            }
            if (jc_snprintf(line, sizeof line,
                            "  ci: moved a dead clone aside: %s -> %s\n",
                            home, newhome) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            if (jc_sb_append(&record, line) != JC_OK) {
                jc_sb_free(&record);
                return 1;
            }
        }
        /* Step 3 (:19): the clone + detach checkout as ONE keyless shell
         * child (the sweep's gsw_run shape: an explicit keyless env, not
         * the default environ). Failure: the INVALID line, rc 1 (b11). */
        if (jc_snprintf(slot, sizeof slot,
                        "git clone -q --no-local --no-checkout '%s' '%s' "
                        "&& git -C '%s' checkout -q --detach '%s'",
                        pr, home, home, s) < 0) {
            jc_sb_free(&record);
            return 1;
        }
        if (gci_child(slot, NULL, 0) != 0) {
            (void)jc_sb_append(&record, "  ci: clone/checkout failed: "
                                                 "INVALID\n");
            rc = 1;
            goto finish;
        }
        /* Step 4 (:20): the checkout verify -- `rev-parse HEAD` in the
         * clone vs `rev-parse '<s>^{commit}'` in the push repo, both
         * keyless READ children. Mismatch: the INVALID line, rc 1. */
        {
            char wantcmd[GCI_LINE_MAX];
            char ts[48];
            char okl[GCI_LINE_MAX];
            char cmd[GCI_LINE_MAX];
            struct jc_vec runenv;
            int logfd;
            int cr_rc;

            if (gci_git_read(home, "rev-parse HEAD", head,
                             (int)sizeof head) != 0) {
                head[0] = '\0';
            }
            gci_trim(head, head, sizeof head);
            if (jc_snprintf(wantcmd, sizeof wantcmd, "rev-parse '%s^{commit}'",
                            s) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            if (gci_git_read(pr, wantcmd, want, (int)sizeof want) != 0) {
                want[0] = '\0';
            }
            gci_trim(want, want, sizeof want);
            if (strcmp(head, want) != 0) {
                if (jc_snprintf(line, sizeof line,
                                "  ci: checkout mismatch: INVALID\n") < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK ||
                    jc_sb_append(&record, "records: ") != JC_OK ||
                    jc_sb_append(&record, records) != JC_OK ||
                    jc_sb_append(&record, "\n") != JC_OK) {
                    jc_sb_free(&record);
                    return 1;
                }
                rc = 1;
                goto finish;
            }
            /* Step 5 (:21-25): the origin/master + merge-base block (all
             * four git calls keyless READ children in the push repo; the
             * two KEPT-prefix diagnostics go to the RECORD). */
            b_full[0] = '\0';
            if (gci_git_read(pr,
                             "rev-parse --verify "
                             "refs/remotes/origin/master^{commit}",
                             b_full, (int)sizeof b_full) != 0) {
                b_full[0] = '\0';
            }
            gci_trim(b_full, b_full, sizeof b_full);
            if (b_full[0] == '\0') {
                if (jc_snprintf(line, sizeof line,
                                "  ci: no origin/master in push.git: "
                                "INVALID\n") < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK ||
                    jc_sb_append(&record, "records: ") != JC_OK ||
                    jc_sb_append(&record, records) != JC_OK ||
                    jc_sb_append(&record, "\n") != JC_OK) {
                    jc_sb_free(&record);
                    return 1;
                }
                rc = 1;
                goto finish;
            }
            memcpy(b8, b_full, 8);
            b8[8] = '\0';
            if (jc_snprintf(cmd, sizeof cmd, "merge-base %s '%s^{commit}'",
                            b_full, s) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            mb_full[0] = '\0';
            if (gci_git_read(pr, cmd, mb_full, (int)sizeof mb_full) != 0) {
                mb_full[0] = '\0';
            }
            gci_trim(mb_full, mb_full, sizeof mb_full);
            if (mb_full[0] == '\0') {
                if (jc_snprintf(line, sizeof line,
                                "  ci: no merge-base of %s and %s: INVALID\n",
                                b8, s) < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK ||
                    jc_sb_append(&record, "records: ") != JC_OK ||
                    jc_sb_append(&record, records) != JC_OK ||
                    jc_sb_append(&record, "\n") != JC_OK) {
                    jc_sb_free(&record);
                    return 1;
                }
                rc = 1;
                goto finish;
            }
            {
                char mbcmd[GCI_LINE_MAX];

                if (jc_snprintf(mbcmd, sizeof mbcmd,
                                "cat-file -e '%s^{commit}'", mb_full) < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (gci_git_read(home, mbcmd, NULL, 0) != 0) {
                    char mb8[16];

                    memcpy(mb8, mb_full, 8);
                    mb8[8] = '\0';
                    if (jc_snprintf(line, sizeof line,
                                    "  ci: the merge-base %s is not in the "
                                    "clone: INVALID\n",
                                    mb8) < 0) {
                        jc_sb_free(&record);
                        return 1;
                    }
                    if (jc_sb_append(&record, line) != JC_OK ||
                        jc_sb_append(&record, "records: ") != JC_OK ||
                        jc_sb_append(&record, records) != JC_OK ||
                        jc_sb_append(&record, "\n") != JC_OK) {
                        jc_sb_free(&record);
                        return 1;
                    }
                    rc = 1;
                    goto finish;
                }
            }
            if (strcmp(mb_full, b_full) != 0) {
                /* :24 KEPT with its prefix (the reference's own
                 * diagnostic sentence, verbatim -- do not strip). */
                if (jc_gateci_mergebase_warn(b8, s, line, sizeof line) < 0) {
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK) {
                    jc_sb_free(&record);
                    return 1;
                }
            }
            /* :25 KEPT with its prefix (the reference's printf: the full
             * 40-hex MB, the 8-hex B). */
            if (jc_gateci_mutant_base_line(mb_full, b8, line,
                                           sizeof line) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            if (jc_sb_append(&record, line) != JC_OK) {
                jc_sb_free(&record);
                return 1;
            }
            /* Step 7 (:27): the meta start line (truncate-then-write;
             * the stage makes it before the run, like the reference). */
            gci_utc(ts, sizeof ts);
            if (jc_snprintf(line, sizeof line, "start %s\n", ts) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            if (gci_write_file(metapath, line) != 0) {
                jc_sb_free(&record);
                return 1;
            }
            /* Step 8 (:28): the run -- the --ci-run command as ONE
             * keyless shell child (NO setsid, NO timeout: the port's
             * child IS the unit's process, the reference's setsid -w
             * ran uncapped), workdir = the clone, stdin /dev/null,
             * stdout+stderr -> the .log, the CHILD env = the 16 keyless
             * names PLUS TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING. */
            jc_vec_init(&runenv, sizeof(char *));
            if (jc_gateci_run_env(a, tmpdir, mb_full, kg, &runenv) != 0) {
                jc_vec_free(&runenv);
                jc_sb_free(&record);
                return 1;
            }
            logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC | JC_O_BINARY, 0600);
            if (logfd < 0) {
                jc_vec_free(&runenv);
                jc_sb_free(&record);
                return 1;
            }
            cr_rc = gci_run_child(home, cr, logfd, &runenv);
            (void)close(logfd);
            jc_vec_free(&runenv);
            /* Step 9 (:30): the meta end line (appended). */
            gci_utc(ts, sizeof ts);
            if (jc_snprintf(line, sizeof line, "end %s rc=%d\n", ts,
                            cr_rc) < 0) {
                jc_sb_free(&record);
                return 1;
            }
            {
                FILE *mf = fopen(metapath, "ab");

                if (mf != NULL) {
                    if (fputs(line, mf) == EOF || fclose(mf) != 0) {
                        jc_sb_free(&record);
                        return 1;
                    }
                }
            }
            /* Step 10 (:31 CHANGED): the reference's `cat meta; grep
             * FAILED; tail -3` is the ORCHESTRATOR's diagnostic half
             * (gate-run.sh:74-81) -- NOT ported. The record gets the
             * note's b6 lines: the gate-run.sh:77 rc line +, on FAIL
             * (rc != 0 OR no `ci: OK` line), the :79 RED half with the
             * SV:1238 excerpt attach. */
            {
                const char *logtext;
                struct jc_sb lsb;

                jc_sb_init(&lsb);
                (void)gci_read_file(&lsb, logpath);
                logtext = lsb.data != NULL ? lsb.data : "";

                gci_last_okl(logtext, okl, sizeof okl);
                if (jc_gateci_rc_line(cr_rc, okl, line, sizeof line) < 0) {
                    jc_sb_free(&lsb);
                    jc_sb_free(&record);
                    return 1;
                }
                if (jc_sb_append(&record, line) != JC_OK) {
                    jc_sb_free(&lsb);
                    jc_sb_free(&record);
                    return 1;
                }
                if (cr_rc != 0 || okl[0] == '\0') {
                    if (jc_gateci_red_line(cr_rc, logtext, logpath,
                                           excpath, line,
                                           sizeof line) < 0) {
                        jc_sb_free(&lsb);
                        jc_sb_free(&record);
                        return 1;
                    }
                    if (jc_sb_append(&record, line) != JC_OK) {
                        jc_sb_free(&lsb);
                        jc_sb_free(&record);
                        return 1;
                    }
                    /* The excerpt: written on FAIL only (C1 asserts its
                     * absence on SUCCESS). F2: the file is STREAMED via
                     * the seam (the pre-F2 64KB `exc[GCI_EXC_CAP]`
                     * buffer + gci_write_file is gone -- the file is
                     * unbounded in size). T8-f2 R1b: an EMPTY excerpt (a
                     * FAIL log with no `^not ok` line) does NOT create the
                     * file -- the RED line above still names excpath, so
                     * the byte shape is unchanged (C2 pins it); guard the
                     * write on the match count. T8-f2 R1: on a write fault
                     * the already-assembled record (the rc line + the RED
                     * line) is NOT discarded: name the fault on stderr,
                     * set rc = 1, free lsb (the normal path's jc_sb_free
                     * at step-10's end is skipped by the goto), and
                     * `goto finish` (which PRINTS the record) -- NO
                     * records: line (the stage did not complete its
                     * output). */
                    {
                        long st1;
                        int ct1;
                        int nm;

                        nm = jc_gateci_excerpt_blocks(logtext, &st1, &ct1, 1);
                        /* nm: 0 = no `not ok` line (skip the write), 1 = one
                         * match, -1 = two or more (cap 1 overflow). Only the
                         * ZERO case skips; 1 and -1 both write. */
                        if (nm != 0) {
                            if (jc_gateci_excerpt_file(logtext, excpath) !=
                                JC_OK) {
                                rc = 1;
                                fprintf(stderr,
                                        "jichi-harness: gate run ci: the "
                                        "excerpt write failed (%s): INVALID\n",
                                        excpath);
                                jc_sb_free(&lsb);
                                goto finish;
                            }
                        }
                    }
                }
                jc_sb_free(&lsb);
            }
        }
        /* The records line: the record's LAST line (the sweep idiom). */
        if (jc_sb_append(&record, "records: ") != JC_OK ||
            jc_sb_append(&record, records) != JC_OK ||
            jc_sb_append(&record, "\n") != JC_OK) {
            jc_sb_free(&record);
            return 1;
        }
    finish:
        /* The stage's stdout: the record, verbatim (the driver's C-cells
         * redirect it to a file and grep it; the orchestrator, wave 4,
         * to <gate-home>/ci.txt). A truncated record is impossible for
         * the stage's inputs, but refuse to print a partial one. */
        if (record.data != NULL) {
            size_t n = record.len;

            (void)(fwrite(record.data, 1, n, stdout) == n);
        }
        jc_sb_free(&record);
        /* rc: 0 when the run finished (the stage COMPLETED -- the
         * ORCHESTRATOR decides RED/GREEN from the record, the sweep
         * idiom), 1 on the INVALID markers above. */
        return rc;
    }
}
