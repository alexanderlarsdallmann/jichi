/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_repin.c - `jichi-harness repin <SHA8>` (M1203, H11, wave 2): the
 * C89 port of the 2026-10-27 hardening-host drive's repin.sh (103 lines),
 * read line by line (notes/M1203.md, b1-b24; the reference's byte sequences
 * are the contract). The reference's order: resolve (:36) -> master (:38-39)
 * -> ci meta (:40-41) -> current link (:42-43) -> summary say (:44) -> clone
 * (:46-49) -> build (:50) -> porcelain (:51-52) -> stamps (:53-55) ->
 * pins/$s (:56-57) -> backup (:59-60, ONLY when absent, from the RUNNING
 * bin/) -> cmp backup-vs-pins/old (:61-63) -> swap cp .new + mv (:64-67) ->
 * pins/current ln -sfn .new + mv -T (:68) -> swap say (:69) -> src/harness
 * diff count (:70-71) -> harness doctor (:77-82, summary line test
 * ` 0 problems`) -> per-model live doctor (:83-101, key by env JICHI_API_KEY,
 * the 12-char prefix withheld in memory, WITHHELD say + continue) -> final
 * say (:102-103, `repin: done $s, doctors clean|NOT clean`, exit $fail).
 *
 * say() (:32) stamps `date '+%F %T %z'` and tee -a's to the out file (b23:
 * every say line goes to stdout AND is appended to the out file with the
 * local stamp prefix; the LAST line of the whole run is always
 * `repin: done <s8>, doctors clean|NOT clean`).
 *
 * Refusals are rc 1 and print `repin: <text>` on stderr (the reference's
 * die() says+exits 1; the port prints the say line to the out file too, the
 * driver asserts them on stderr); the usage refusal is rc 2
 * (`usage: jichi-harness repin <SHA8>`, b1: a tool error is a value, never a
 * :? abort).
 *
 * The git children are all local reads through jc_hp_git_run's credential-
 * less READ form (b18: stdin /dev/null, the M1056 idiom). The build / clone /
 * harness-doctor / live-doctor commands are shimmable host.* fields (b7/b8/
 * b19/b20) so the container's stand-ins run where the host's real make /
 * podman forms would (CHANGED: the transport, not the check). b16 NEVER rm:
 * no removal call on the clone dirs, pin dirs, backups or the out file.
 * b20/b21: the model key is read ONCE from modelKeyFile (whitespace-trimmed,
 * the run verb's idiom), passed to each live-doctor child ONLY as
 * JICHI_API_KEY=<key> in the spawn's env vector (the parent never setenv's
 * it; it is in NO child's argv), and its 12-char prefix is matched IN MEMORY
 * (strstr, not a grep child -- the prefix must not land on any argv) against
 * each live-doctor's captured output; on a hit the say line names the model,
 * the output file is NOT written, fail=1, and the loop CONTINUES to the next
 * model. b24: every child goes through the harness's built-in secret-name
 * scrub (jc_spawn / jc_proc_capture scrub the registered names).
 *
 * CHANGED (documented, the driver is the contract): the reference's live-
 * doctor `o` is a shell variable never printed; the port PASSES THROUGH each
 * non-withheld live-doctor's captured output (+ its `rc=<n>` line) to the
 * verb's stdout, because the driver's cell 17 (b20) asserts the stand-in's
 * `JICHI_API_KEY length=<n>` env-delivery line on the verb's stdout. The
 * harness doctor's output is NOT passed through (the reference's :79 writes
 * it to the file only).
 */

#include "jc_harness_repin.h"

#include <errno.h>        /* EEXIST (rr_mkdir_p, F6) */
#include <stdio.h>
#include <string.h>
#include <time.h>         /* say() stamp (b23: date '+%F %T %z') */
#include <unistd.h>      /* readlink, lstat, rename, symlink, mkdir */

#include "jc_harness_config.h"  /* the config load (host.* fields) */
#include "jc_harness_push.h"    /* jc_hp_git_run (the read form, b18) */
#include "jc_mem.h"             /* struct jc_arena */
#include "jc_platform.h"        /* jc_status, jc_size, jc_read_file */
#include "jc_proc.h"            /* jc_proc_capture (the shimmable children) */
#include "jc_str.h"             /* struct jc_sb (the captures) */
#include "jc_vec.h"             /* struct jc_vec (the child env vectors) */
#include "jc_snprintf.h"        /* jc_snprintf, never sprintf */
#include "jc_utf8.h"            /* jc_utf8_trunc_len (M889 byte-counted cuts) */

#include <sys/stat.h>   /* struct stat, lstat, S_ISLNK, mkdir, 0755 */

#define RR_CAP_PATH  (4 * 1024)  /* a path the cells name + slack */
#define RR_CAP_SHA   128         /* a full 40-hex id + slack */
#define RR_CAP_SMALL 512         /* an s8 / m8 / short reading + slack */
#define RR_CAP_LINE  8192        /* one captured line / summary / meta */
#define RR_CAP_CMD   16384       /* a shimmable command line */
#define RR_CAP_KEY   8192        /* the model key (b20) */
#define RR_LIVE_CAP  (64 * 1024) /* a live-doctor's combined output */
/* i-4 (M1203-i, SV:1241): the named deadline for every rr_run_cmd child.
 * The reference's sh -c "$@" and make -j8 have NO deadline; the port used
 * to pass 0L, which include/jc_proc.h resolves to the unmeasured 30 s
 * JC_PROC_DEF_TIMEOUT kill (landed 2026-06-25, 1370dd9b, with no
 * measurement; SV:1241's measurement: a sweep child killed at 31/32 s).
 * 600 s is the 600-s-list scale (tests/smoke/run.sh's wider bound) and
 * out-runs the measured 2-3 min cold build of the 219-compile harness
 * (run 0086). A child that out-runs the NAMED bound is a refusal the verb
 * names (the -2 path below), not a silent -2. */
#define RR_CHILD_DEADLINE 600L
#define RR_BINARIES  2
#define RR_ARGC      16

static const char *const rr_bins[RR_BINARIES] = { "jichi", "jichi-harness" };

/* The say/die stamp's UTC-of-wall (the sibling verbs' hand-rolled timegm:
 * glibc's timegm is not exposed under the build's global _POSIX_C_SOURCE, so
 * the local wall clock is read back as UTC by the era arithmetic -- the SAME
 * proleptic-Gregorian days-from-civil algorithm drive.c / notify.c use). */
static time_t
rr_utc_from_tm(const struct tm *tmv)
{
    long y = tmv->tm_year + 1900;
    long m = tmv->tm_mon + 1;
    long d = tmv->tm_mday;
    long era, yoe, doy, doe, days;
    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + doe - 719468;
    return (time_t)(days * 86400L + tmv->tm_hour * 3600L
                    + tmv->tm_min * 60L + tmv->tm_sec);
}

/* A C89-clean `date '+%F %T %z'` (b23's say() stamp): the local date+time and
 * a `±HHMM` zone offset (the reference's gnu_strftime %F/%T/%z are not ISO
 * C90, so the port assembles them). Fills out (>= 24) with the stamp; returns
 * 1 on success, 0 when the clock is unusable (the caller then omits the
 * out-file line, never fatal -- the stdout is the orchestrator's channel). */
static int
rr_stamp(char *out, size_t cap)
{
    struct tm tmv;
    struct tm *tmr;
    time_t t;
    int n;

    if (cap < 24) {
        return 0;
    }
    t = time(NULL);
    tmr = localtime_r(&t, &tmv);
    if (tmr == NULL) {
        return 0;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", tmr);
    if (n == 0) {
        return 0;
    }
    /* the zone offset: +HHMM / -HHMM / +0000 (the %z form). */
    {
        int off_min;
        int sign;
        int off_h;
        int off_m;
        off_min = (int)((long)rr_utc_from_tm(tmr) - (long)t) / 60;
        sign = (off_min < 0) ? 1 : 0;
        if (off_min < 0) {
            off_min = -off_min;
        }
        off_h = off_min / 60;
        off_m = off_min % 60;
        n += jc_snprintf(out + n, cap - (size_t)n, " %c%02d%02d",
                         sign ? '-' : '+', off_h, off_m);
    }
    return 1;
}

/* b23 say(): one line to stdout AND appended to the out file, prefixed with
 * the drive's local stamp `date '+%F %T %z'` (the reference's :32 tee -a).
 * The out file GROWS, never truncates (b23). A failed open is never fatal
 * (the stdout is the orchestrator's channel). `msg` is the say argument
 * WITHOUT the leading "repin: " (say prints `repin: <msg>`). */
static void
rr_say(const char *out, const char *msg)
{
    printf("repin: %s\n", msg);
    if (out == NULL || out[0] == '\0') {
        return;
    }
    {
        char stamp[64];
        char line[RR_CAP_LINE];
        if (rr_stamp(stamp, sizeof stamp)) {
            FILE *f = fopen(out, "ab");
            if (f != NULL) {
                if (jc_snprintf(line, sizeof line, "%s repin: %s\n", stamp,
                                 msg) >= 0) {
                    fputs(line, f);
                    fflush(f);
                }
                fclose(f);
            }
        }
    }
}

/* die() (the reference's :33): `repin: <text>` on stderr + the out file, rc 1. */
static int
rr_die(const char *out, const char *text)
{
    fprintf(stderr, "repin: %s\n", text);
    if (out != NULL && out[0] != '\0') {
        FILE *f = fopen(out, "ab");
        if (f != NULL) {
            char stamp[64];
            char line[RR_CAP_LINE];
            if (rr_stamp(stamp, sizeof stamp)) {
                if (jc_snprintf(line, sizeof line, "%s repin: %s\n", stamp,
                                 text) >= 0) {
                    fputs(line, f);
                    fflush(f);
                }
            }
            fclose(f);
        }
    }
    return 1;
}

/* A git read child in `workdir` (no credential wiring, b18). Returns
 * jc_hp_git_run's code; leaves the combined output in out. */
static int
rr_git_read(struct jc_arena *a, const char *workdir, const char *args[],
            int n_args, int max_out, char *out)
{
    return jc_hp_git_run(a, workdir, NULL, 0, NULL, 0, args, n_args,
                         max_out, out, NULL);
}

/* Is `path` a symlink? (b5: readlink needs the link to EXIST as a link, so a
 * broken symlink passes; a plain file / absent does not.) */
static int
rr_is_link(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return 0;
    }
    return S_ISLNK(st.st_mode);
}

/* readlink `path` into out (NUL-terminated). 1 on success, 0 otherwise. */
static int
rr_readlink(const char *path, char *out, size_t cap)
{
    ssize_t n;
    if (cap == 0) {
        return 0;
    }
    n = readlink(path, out, (size_t)cap - 1);
    if (n < 0) {
        return 0;
    }
    out[n] = '\0';
    return 1;
}

/* Does `path` exist (any type)? (b7's `-e` / b11's `-e`.) */
static int
rr_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

/* mkdir -p (repin.sh :47 / :60, the reference's `-p` semantics; the gref_
 * mkdir_p loop, EEXIST tolerated at each level). F6 (wave-2c): on a fresh
 * layout (parent absent) the port used to refuse where the reference
 * succeeds. 1 when the dir exists as a directory afterwards (made or
 * already there), 0 on a real fault (the caller keeps its NAMED refusal
 * bytes). */
static int
rr_mkdir_p(const char *dir)
{
    char buf[RR_CAP_PATH];
    size_t n;
    size_t i;
    if (dir == NULL || dir[0] == '\0' || strlen(dir) >= sizeof buf) {
        return 0;
    }
    n = strlen(dir);
    memcpy(buf, dir, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char save = buf[i];
            buf[i] = '\0';
            if (i > 1 && mkdir(buf, 0755) != 0 && errno != EEXIST) {
                return 0;
            }
            buf[i] = save;
        }
    }
    {
        struct stat st;
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 1;
        }
    }
    return 0;
}

/* Read `path` fully into the arena. 1 on success (even an empty file),
 * 0 on failure. */
static int
rr_read_file(struct jc_arena *a, const char *path, char **out, jc_size *len)
{
    char *t = NULL;
    jc_size l = 0;
    if (jc_read_file(path, &t, &l, a) != JC_OK) {
        return 0;
    }
    *out = t;
    *len = l;
    return 1;
}

/* Strip the child's trailing newline(s) from a captured reading (a rev-parse
 * SHA: git ends its output in \n, the shell's $(...) strips it -- the port
 * must, or the arg is "a path" not a revision). */
static void
rr_strip_nl(char *s)
{
    size_t n;
    if (s == NULL || s[0] == '\0') {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        n--;
    }
    s[n] = '\0';
}

/* cmp -s (b12/b13): 1 when both files read and the bytes are equal. */
static int
rr_cmp(struct jc_arena *a, const char *p1, const char *p2)
{
    char *b1 = NULL, *b2 = NULL;
    jc_size l1 = 0, l2 = 0;
    int ok;
    if (!rr_read_file(a, p1, &b1, &l1) || !rr_read_file(a, p2, &b2, &l2)) {
        return 0;
    }
    ok = (l1 == l2) && ((l1 == 0) || (memcmp(b1, b2, l1) == 0));
    return ok;
}

/* cp (b11/b12/b13): copy src's bytes onto dst, and (M1203-i5, i-5) dst takes
 * the source's permission bits (the reference's cp does this for a fresh
 * destination); a failed close is a failed copy. 1 on success. */
static int
rr_cp(struct jc_arena *a, const char *src, const char *dst)
{
    char *b = NULL;
    jc_size l = 0;
    FILE *f;
    struct stat st;
    if (stat(src, &st) != 0) {
        return 0;
    }
    if (!rr_read_file(a, src, &b, &l)) {
        return 0;
    }
    f = fopen(dst, "wb");
    if (f == NULL) {
        return 0;
    }
    if (l > 0 && (fwrite(b, 1, l, f) != l)) {
        fclose(f);
        return 0;
    }
    if (fclose(f) != 0) {
        return 0;
    }
    if (chmod(dst, (unsigned int)(st.st_mode & (unsigned int)(S_IRWXU | S_IRWXG | S_IRWXO))) != 0) {
        return 0;
    }
    return 1;
}

/* Write `text` (+ a trailing newline when absent) to path (b19/b20's
 * `printf '%s\n' "$o" > <file>`). */
static void
rr_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    if (text != NULL && text[0] != '\0') {
        fputs(text, f);
        if (text[strlen(text) - 1] != '\n') {
            fputc('\n', f);
        }
    }
    fclose(f);
}

/* Count the lines of captured text (b9/b17's `| wc -l`): one per '\n'. */
static int
rr_count_lines(const char *s)
{
    int n = 0;
    if (s == NULL) {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            n++;
        }
    }
    return n;
}

static int
rr_is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

/* The LAST line of `text` matching the summary shape `<n> ok, <n> warning`
 * (b19/b20's `grep -E '[0-9]+ ok, [0-9]+ warning' | tail -1`), in dst (""
 * when none). */
static void
rr_summary_line(const char *text, char *dst, size_t cap)
{
    const char *p;
    const char *end;
    size_t best = 0;
    size_t bestlen = 0;
    size_t n;

    dst[0] = '\0';
    if (cap == 0 || text == NULL) {
        return;
    }
    p = text;
    end = p + strlen(p);
    while (p < end) {
        const char *ls = p;
        const char *le;
        size_t i;
        size_t llen;
        int ok = 0;
        while (p < end && *p != '\n') {
            p++;
        }
        le = p;
        if (p < end) {
            p++;   /* skip the newline */
        }
        llen = (size_t)(le - ls);
        for (i = 0; i + 4 <= llen; i++) {
            size_t j;
            if (!rr_is_digit(ls[i])) {
                continue;
            }
            for (j = i + 1; j < llen && rr_is_digit(ls[j]); j++) {
            }
            if (j + 3 <= llen && ls[j] == ' ' && ls[j + 1] == 'o' &&
                ls[j + 2] == 'k' && ls[j + 3] == ',') {
                /* F4 (wave-2c): EXACTLY one space after `ok,` -- the
                 * reference grep -E `[0-9]+ ok, [0-9]+ warning` form; a
                 * doctor printing `3 ok,  2 warning` must NOT match. */
                size_t k = j + 4;
                if (k + 1 < llen && ls[k] == ' ' && rr_is_digit(ls[k + 1])) {
                    size_t m;
                    for (m = k + 2; m < llen && rr_is_digit(ls[m]); m++) {
                    }
                    if (m + 7 <= llen && ls[m] == ' ' &&
                        strncmp(ls + m + 1, "warning", 7) == 0) {
                        ok = 1;
                        break;
                    }
                }
            }
        }
        if (ok) {
            best = (size_t)(ls - text);
            bestlen = llen;
        }
    }
    if (bestlen > 0) {
        n = bestlen;
        if (n >= cap) {
            n = cap - 1;
        }
        memcpy(dst, text + best, n);
        dst[n] = '\0';
    }
}

/* The ` 0 problems` test (b19/b20: `case $l in *" 0 problems"*)`). */
static int
rr_zero_problems(const char *l)
{
    if (l == NULL) {
        return 0;
    }
    return (strstr(l, " 0 problems") != NULL);
}

/* tr '\n' ' ' (b6: the meta's content, newlines joined by a single space;
 * the reference does NOT append a trailing space). */
static void
rr_join_newlines(const char *in, char *out, size_t cap)
{
    size_t i = 0;
    if (cap == 0 || in == NULL) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return;
    }
    for (; *in != '\0' && i + 1 < cap; in++, i++) {
        out[i] = (*in == '\n') ? ' ' : *in;
    }
    out[i] = '\0';
}

/* b4's tail -1 (the land F3a shape): the file's last CONTENT line, with
 * exactly one trailing newline stripped ("" when the file is empty). */
static void
rr_tail1(const char *text, char *dst, size_t cap)
{
    size_t n;
    size_t k;
    size_t seg;
    dst[0] = '\0';
    if (cap == 0 || text == NULL) {
        return;
    }
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        n--;
    }
    seg = n;
    for (k = 0; k < n; k++) {
        if (text[k] == '\n') {
            seg = n - k - 1;
        }
    }
    if (seg == 0) {
        return;
    }
    if (seg >= cap) {
        seg = cap - 1;
    }
    memcpy(dst, text + (n - seg), seg);
    dst[seg] = '\0';
}

/* b10's `grep -qx` form (repin.sh:54, the reference): does ANY whole line
 * of `text` exactly equal `want`? Walks newline-delimited (a trailing
 * newline tolerated, a CR stripped from each line); a line that merely
 * CONTAINS `want` does not count -- whole-line only, so a
 * `build: <s8> (dirty)` line refuses. i-1 (M1203-i): the port used to
 * read the LAST line (rr_tail1); since M1133 the real `jichi --version`
 * holds `build: <s8>` on a MIDDLE line, then a `libcurl: ...` line last,
 * so the last-line form refused a correctly built real binary that the
 * reference accepts. 1 when found, 0 otherwise. */
static int
rr_any_line(const char *text, const char *want)
{
    const char *p;
    const char *end;
    size_t wn;
    if (text == NULL || want == NULL) {
        return 0;
    }
    wn = strlen(want);
    p = text;
    end = p + strlen(p);
    while (p <= end) {
        const char *ls = p;
        const char *le = p;
        while (le < end && *le != '\n') {
            le++;
        }
        if (le > ls && le[-1] == '\r') {
            le--;
        }
        if ((size_t)(le - ls) == wn && memcmp(ls, want, wn) == 0) {
            return 1;
        }
        if (le >= end) {
            break;
        }
        p = le + 1;
    }
    return 0;
}

/* b20: read the model key file ONCE (whitespace-trimmed, the run-verb
 * read_key_file idiom). The key lands only in out; the harness never
 * setenv's it and no message prints it. 1 on success. */
static int
rr_read_key(struct jc_arena *a, const char *path, char *out, size_t cap)
{
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    const char *end;
    int got = 0;

    out[0] = '\0';
    if (cap == 0 || !rr_read_file(a, path, &text, &len)) {
        return 0;
    }
    end = text + len;
    for (p = text; p < end && !got; ) {
        const char *a0 = p;
        const char *b0;
        while (p < end && *p != '\n') {
            p++;
        }
        b0 = p;
        while (a0 < b0 && (*a0 == ' ' || *a0 == '\t' || *a0 == '\r')) {
            a0++;
        }
        while (b0 > a0 && (b0[-1] == ' ' || b0[-1] == '\t' ||
                           b0[-1] == '\r')) {
            b0--;
        }
        if (b0 > a0) {
            size_t n = (size_t)(b0 - a0);
            if (n + 1 > cap) {
                n = cap - 1;
            }
            memcpy(out, a0, n);
            out[n] = '\0';
            got = 1;
        }
        if (p < end) {
            p++;   /* skip the newline */
        }
    }
    return got;
}

/* Run one shimmable command through the harness's capture idiom (jc_spawn's
 * sh -c + the built-in secret-name scrub, b24). `cmdline` is the full command
 * line (the caller did the <m> / path substitution); the child's combined
 * output is captured (capped at cap_out) and its rc returned. 1 on success,
 * 0 on a spawn fault, -2 on the deadline kill (the child out-ran
 * RR_CHILD_DEADLINE: the caller with a 'spawn failed' text names the
 * deadline there, i-4 (M1203-i)). `key` (NULL = none) is passed to the
 * child ONLY as JICHI_API_KEY=<key> in the env vector (b20), applied AFTER
 * the scrub. */
static int
rr_run_cmd(const char *cmdline, const char *key, char *out_text,
           size_t cap_out, int *out_rc)
{
    char *argv[4];
    struct jc_vec envv;
    struct jc_sb capture;
    /* i-3 (M1203-i): the env slot + its pointer are declared at the
     * function scope (the run verb's ACTUAL idiom -- there the env
     * buffers sit in the same block that contains the spawn), so they
     * are live across the fork. Inside the key-present block below the
     * slot was dead before the fork: a stack-use-after-scope, aborting
     * the child in child_apply_env under the project's SAN policy.
     * The vec holds a POINTER to a char* slot (run-verb idiom; jc_vec_
     * free frees only the pointer array): the element width is
     * sizeof(char *), so the push is &kvp (kvp -> ke), not a char address. */
    char ke[14 + RR_CAP_KEY + 1];
    char *kvp = ke;
    int rc;
    size_t len;

    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmdline;
    argv[3] = NULL;
    jc_vec_init(&envv, sizeof(char *));
    if (key != NULL && key[0] != '\0') {
        /* F1 (wave-2c): the slot lives on the STACK, the run-verb idiom
         * (jc_harness_run.c:1380-1385: the vector holds pointers to
         * stack buffers; jc_vec_free frees only the pointer array). A
         * heap slot leaked on every successful live-doctor spawn. Sized
         * 14 (the `JICHI_API_KEY=` name) + RR_CAP_KEY (the read_key_file
         * cap the caller's buffer is capped at) + 1 (NUL). i-3
         * (M1203-i): the declarations sit at the function scope (above),
         * live across the fork -- the comment there is the note. */
        if (14 + strlen(key) + 1 > sizeof ke) {
            jc_vec_free(&envv);
            return 0;   /* never overflow the fixed slot */
        }
        jc_snprintf(ke, sizeof ke, "JICHI_API_KEY=%s", key);
        if (jc_vec_push(&envv, &kvp) != JC_OK) {
            jc_vec_free(&envv);
            return 0;
        }
    }
    jc_sb_init(&capture);
    /* i-4 (M1203-i): the NAMED deadline (RR_CHILD_DEADLINE), not 0L -- 0L
     * resolved to the unmeasured 30 s JC_PROC_DEF_TIMEOUT kill (SV:1241). */
    rc = (int)jc_proc_capture(argv, &envv, NULL, &capture, (jc_size)cap_out,
                              RR_CHILD_DEADLINE, NULL);
    jc_vec_free(&envv);
    if (rc == -1 || rc == -2) {
        jc_sb_free(&capture);
        /* i-4: a deadline kill is NAMED, not folded into the spawn-fault 0:
         * a caller can distinguish `ok == -2` and say the child was killed
         * at the named bound. *out_rc / out_text are left as the caller set
         * them, exactly as on the spawn-fault path. */
        return (rc == -2) ? -2 : 0;
    }
    len = capture.len;
    while (len > 0 && (capture.data[len - 1] == '\n' ||
                       capture.data[len - 1] == '\r')) {
        len--;
    }
    if (len > cap_out - 1) {
        len = cap_out - 1;
    }
    if (capture.data != NULL && len > 0) {
        memcpy(out_text, capture.data, len);
    }
    out_text[len] = '\0';
    jc_sb_free(&capture);
    *out_rc = rc;
    return 1;
}

int
jc_harness_repin_run(const char *config_path,
                     const char *push_repo, const char *ci_log_dir,
                     const char *build_dir, const char *pins_dir,
                     const char *bin_dir, const char *backup_dir,
                     const char *clone_run, const char *repin_out,
                     const char *harness_doctor, const char *live_doctor,
                     const char *build_run, const char *models,
                     const char *sha, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    int rc;
    const char *push, *cilog, *bdir, *pdir, *bindir, *bkdir, *out, *doc,
        *livedoc, *brun, *mmodels, *cclone;
    /* i-2 (M1203-i): each defaulted path owns its OWN storage (one shared
     * buffer aliased by all six locals read the last value written). */
    char def_push[RR_CAP_PATH];
    char def_bdir[RR_CAP_PATH];
    char def_pdir[RR_CAP_PATH];
    char def_bindir[RR_CAP_PATH];
    char def_bkdir[RR_CAP_PATH];
    char def_out[RR_CAP_PATH];
    char full[RR_CAP_SHA];
    char s8[RR_CAP_SHA];
    char mfull[RR_CAP_SHA];
    char m8[RR_CAP_SHA];
    char old8[RR_CAP_SHA];
    char oldfull[RR_CAP_SHA];
    char meta_path[RR_CAP_PATH];
    char B[RR_CAP_PATH];
    char B_buildlog[RR_CAP_PATH];
    char pin_s8[RR_CAP_PATH];
    char bk[RR_CAP_PATH];
    char cur[RR_CAP_PATH];
    char cur_new[RR_CAP_PATH];
    char newcur[RR_CAP_PATH];
    char joined[RR_CAP_LINE];
    char msg[RR_CAP_LINE];
    char cap[RR_LIVE_CAP];
    char o[RR_LIVE_CAP + 64];
    char key[RR_CAP_KEY];
    const char *args[RR_ARGC];
    int an;
    int child_rc;
    int i;
    int fail = 0;
    int key_loaded = 0;

    /* b1: one argument, the COMMIT. The usage refusal (rc 2, the note's
     * exact text) -- a tool error is a value, never an abort (CHANGED). */
    if (sha == NULL || sha[0] == '\0') {
        fprintf(stderr, "usage: jichi-harness repin <SHA8>\n");
        return 2;
    }
    if (a == NULL) {
        return 2;
    }

    /* The config: the host.* fields. A load refusal is rc 2. */
    memset(&cfg, 0, sizeof cfg);
    rc = (int)jc_harness_config_load(config_path, &cfg, a);
    if (rc != JC_OK) {
        fprintf(stderr, "jichi-harness: repin: %s\n",
                jc_harness_config_error(&cfg) != NULL
                    ? jc_harness_config_error(&cfg)
                    : "could not load the config");
        return 2;
    }

    /* Resolve the named paths: flag wins, else the config's host field, else
     * the code-named default (b7's idiom). host.dir else <dir>. */
    {
        const char *hdir = (cfg.host_dir != NULL) ? cfg.host_dir
                         : ((cfg.dir != NULL) ? cfg.dir : "");
        if (push_repo != NULL) {
            push = push_repo;
        } else if (cfg.host_push_repo != NULL) {
            push = cfg.host_push_repo;
        } else if (cfg.host_push_home != NULL) {
            int nrr;
            nrr = jc_snprintf(def_push, sizeof def_push, "%s/push.git",
                              cfg.host_push_home);
            if (nrr < 0 || nrr >= (int)sizeof def_push) {
                return rr_die(NULL, "the push repo path is too long");
            }
            push = def_push;
        } else {
            push = "";
        }
        cilog = (ci_log_dir != NULL) ? ci_log_dir : cfg.host_ci_log_dir;
        if (build_dir != NULL) {
            bdir = build_dir;
        } else if (cfg.host_repin_build_dir != NULL) {
            bdir = cfg.host_repin_build_dir;
        } else {
            int nrr;
            nrr = jc_snprintf(def_bdir, sizeof def_bdir, "%s/pin-build", hdir);
            if (nrr < 0 || nrr >= (int)sizeof def_bdir) {
                return rr_die(NULL, "the build dir path is too long");
            }
            bdir = def_bdir;
        }
        if (pins_dir != NULL) {
            pdir = pins_dir;
        } else if (cfg.host_repin_pins_dir != NULL) {
            pdir = cfg.host_repin_pins_dir;
        } else {
            int nrr;
            nrr = jc_snprintf(def_pdir, sizeof def_pdir, "%s/pins", hdir);
            if (nrr < 0 || nrr >= (int)sizeof def_pdir) {
                return rr_die(NULL, "the pins dir path is too long");
            }
            pdir = def_pdir;
        }
        if (bin_dir != NULL) {
            bindir = bin_dir;
        } else if (cfg.host_repin_bin_dir != NULL) {
            bindir = cfg.host_repin_bin_dir;
        } else {
            int nrr;
            nrr = jc_snprintf(def_bindir, sizeof def_bindir, "%s/bin", hdir);
            if (nrr < 0 || nrr >= (int)sizeof def_bindir) {
                return rr_die(NULL, "the bin dir path is too long");
            }
            bindir = def_bindir;
        }
        if (backup_dir != NULL) {
            bkdir = backup_dir;
        } else if (cfg.host_repin_backup_dir != NULL) {
            bkdir = cfg.host_repin_backup_dir;
        } else {
            int nrr;
            nrr = jc_snprintf(def_bkdir, sizeof def_bkdir, "%s/backups", hdir);
            if (nrr < 0 || nrr >= (int)sizeof def_bkdir) {
                return rr_die(NULL, "the backup dir path is too long");
            }
            bkdir = def_bkdir;
        }
        if (repin_out != NULL) {
            out = repin_out;
        } else if (cfg.host_repin_out != NULL) {
            out = cfg.host_repin_out;
        } else {
            int nrr;
            nrr = jc_snprintf(def_out, sizeof def_out, "%s/repin.out", hdir);
            if (nrr < 0 || nrr >= (int)sizeof def_out) {
                return rr_die(NULL, "the out file path is too long");
            }
            out = def_out;
        }
        doc = (harness_doctor != NULL) ? harness_doctor
                                       : cfg.host_repin_doctor;
        livedoc = (live_doctor != NULL) ? live_doctor
                                        : cfg.host_repin_live_doctor;
        brun = (build_run != NULL) ? build_run : cfg.host_repin_build_run;
        mmodels = (models != NULL) ? models : cfg.host_repin_models;
        cclone = (clone_run != NULL) ? clone_run : cfg.host_repin_clone;
    }

    /* b4: an absent ci log dir is a named refusal (repin never guesses a log
     * dir -- the refusal's <meta path> would name the guessed one). */
    if (cilog == NULL || cilog[0] == '\0') {
        return rr_die(out, "no ci log dir named (host.ciLogDir)");
    }
    if (push == NULL || push[0] == '\0') {
        return rr_die(out, "no push repo named (host.pushRepo)");
    }

    /* b2: the commit resolves in the push repository (a read child). The
     * refusal names the RAW argument as passed (F1's idiom). */
    an = 0;
    args[an++] = (char *)"rev-parse";
    args[an++] = (char *)"--verify";
    args[an++] = (char *)"-q";
    {
        char team_ref[RR_CAP_SMALL + 12];
        jc_snprintf(team_ref, sizeof team_ref, "%s^{commit}", sha);
        args[an++] = team_ref;
    }
    full[0] = '\0';
    rc = rr_git_read(a, push, args, an, RR_CAP_SHA, full);
    if (rc != 0 || full[0] == '\0') {
        char m[544];
        jc_snprintf(m, sizeof m, "%s is not a commit in push.git", sha);
        return rr_die(out, m);
    }
    rr_strip_nl(full);
    jc_snprintf(s8, sizeof s8, "%.*s", (int)jc_utf8_trunc_len(full, 8), full);
    /* b3: the commit is GitLab's master as push.git last fetched it. Two
     * refusals, in order. */
    an = 0;
    args[an++] = (char *)"rev-parse";
    args[an++] = (char *)"--verify";
    args[an++] = (char *)"-q";
    args[an++] = (char *)"refs/remotes/origin/master";
    mfull[0] = '\0';
    rc = rr_git_read(a, push, args, an, RR_CAP_SHA, mfull);
    if (rc != 0 || mfull[0] == '\0') {
        return rr_die(out, "no origin/master in push.git");
    }
    rr_strip_nl(mfull);
    if (strcmp(mfull, full) != 0) {
        char m[256];
        jc_snprintf(m8, sizeof m8, "%.*s", (int)jc_utf8_trunc_len(mfull, 8),
                    mfull);
        jc_snprintf(m, sizeof m,
                    "refused: %s is not GitLab master as push.git last "
                    "fetched it (%s)", s8, m8);
        return rr_die(out, m);
    }

    /* b4: the green `make ci` record for exactly the commit (tail -1 ends in
     * ` rc=0`; the meta path is NAMED). */
    jc_snprintf(meta_path, sizeof meta_path, "%s/ci-%s.log.meta", cilog, s8);
    joined[0] = '\0';
    {
        char *mt = NULL;
        jc_size ml = 0;
        char last[RR_CAP_LINE];
        int green;
        if (rr_read_file(a, meta_path, &mt, &ml)) {
            rr_tail1(mt, last, sizeof last);
            green = (last[0] != '\0' &&
                     (size_t)strlen(last) >= 5 &&
                     strcmp(last + strlen(last) - 5, " rc=0") == 0);
            rr_join_newlines(mt, joined, sizeof joined);
        } else {
            green = 0;
        }
        if (!green) {
            char m[RR_CAP_PATH + 128];
            jc_snprintf(m, sizeof m,
                        "refused: no green make ci record for %s (%s)", s8,
                        meta_path);
            return rr_die(out, m);
        }
    }

    /* b5: the current pin is a link and not already this commit. */
    jc_snprintf(cur, sizeof cur, "%s/current", pdir);
    if (!rr_is_link(cur) || !rr_readlink(cur, old8, sizeof old8)) {
        return rr_die(out, "pins/current is not a link");
    }
    if (strcmp(old8, s8) == 0) {
        char m[RR_CAP_SMALL + 32];
        jc_snprintf(m, sizeof m, "refused: pins/current is %s already", s8);
        return rr_die(out, m);
    }

    /* b6: the summary line, BEFORE the build (repin.sh:44). */
    jc_snprintf(msg, sizeof msg, "%s -> %s (master; make ci: %s)", old8, s8,
                joined);
    rr_say(out, msg);

    /* Resolve oldfull (b17's diff needs it) NOW, before the swap moves
     * pins/current to s8. */
    an = 0;
    args[an++] = (char *)"rev-parse";
    args[an++] = (char *)"--verify";
    args[an++] = (char *)"-q";
    {
        char team_ref[RR_CAP_SMALL + 12];
        jc_snprintf(team_ref, sizeof team_ref, "%s^{commit}", old8);
        args[an++] = team_ref;
    }
    oldfull[0] = '\0';
    if (rr_git_read(a, push, args, an, RR_CAP_SHA, oldfull) != 0 ||
        oldfull[0] == '\0') {
        oldfull[0] = '\0';
    }
    rr_strip_nl(oldfull);

    /* b7: the fresh clone, a directory that must not exist. mkdir -p the
     * build dir, refuse when the per-sha dir exists, then the clone (a
     * shimmable command) + the checkout (the port's own git child). */
    if (!rr_mkdir_p(bdir)) {
        return rr_die(out, "the build dir could not be made");
    }
    jc_snprintf(B, sizeof B, "%s/%s", bdir, s8);
    if (rr_exists(B)) {
        char m[RR_CAP_PATH + 32];
        jc_snprintf(m, sizeof m, "refused: %s exists", B);
        return rr_die(out, m);
    }
    {
        char clone_cmd[RR_CAP_CMD];
        int ok;
        if (cclone != NULL && cclone[0] != '\0') {
            jc_snprintf(clone_cmd, sizeof clone_cmd, "%s", cclone);
        } else {
            jc_snprintf(clone_cmd, sizeof clone_cmd,
                        "git clone -q --no-local --no-checkout '%s' '%s'",
                        push, B);
        }
        cap[0] = '\0';
        ok = rr_run_cmd(clone_cmd, NULL, cap, sizeof cap, &child_rc);
        if (ok != 1 || child_rc != 0) {
            return rr_die(out, "the clone failed");
        }
        /* the checkout (the port's own git child AFTER the clone, repin.sh:49:
         * one checkout; a checkout failure IS a clone failure, `the clone
         * failed`). The reference's push.git is the harness's MIRROR clone,
         * whose refs/heads carry the landed commit, so the plain `--no-local`
         * clone + detach of $full resolves; the driver's fixture push.git
         * behaves like that mirror (set_master tracks refs/heads/master as
         * well as refs/remotes/origin/master, which b3 reads). */
        {
            an = 0;
            args[an++] = (char *)"checkout";
            args[an++] = (char *)"-q";
            args[an++] = (char *)"--detach";
            args[an++] = full;
            if (rr_git_read(a, B, args, an, 4096, cap) != 0) {
                return rr_die(out, "the clone failed");
            }
        }
    }

    /* b8: the build (cwd the clone, combined output to the log OUTSIDE the
     * clone). The command is shimmable (absent = the reference's make). */
    jc_snprintf(B_buildlog, sizeof B_buildlog, "%s.build.log", B);
    {
        char build_cmd[RR_CAP_CMD];
        int ok;
        if (brun != NULL && brun[0] != '\0') {
            jc_snprintf(build_cmd, sizeof build_cmd, "cd '%s' && (%s) > '%s' "
                        "2>&1", B, brun, B_buildlog);
        } else {
            jc_snprintf(build_cmd, sizeof build_cmd,
                        "cd '%s' && (make -j8 WERROR=1 jichi jichi-harness) "
                        "> '%s' 2>&1", B, B_buildlog);
        }
        cap[0] = '\0';
        ok = rr_run_cmd(build_cmd, NULL, cap, sizeof cap, &child_rc);
        if (ok != 1 || child_rc != 0) {
            char m[RR_CAP_PATH + 64];
            jc_snprintf(m, sizeof m, "the build failed: %s", B_buildlog);
            return rr_die(out, m);
        }
    }

    /* b9: the clone is clean after the build (0 porcelain lines). */
    an = 0;
    args[an++] = (char *)"status";
    args[an++] = (char *)"--porcelain";
    cap[0] = '\0';
    rr_git_read(a, B, args, an, RR_CAP_CMD, cap);
    {
        int n = rr_count_lines(cap);
        if (n != 0) {
            char m[RR_CAP_PATH + 64];
            jc_snprintf(m, sizeof m,
                        "the build left %d porcelain line(s) in %s", n, B);
            return rr_die(out, m);
        }
    }

    /* b10: both `--version` stamps say the commit (jichi THEN jichi-
     * harness; ANY whole line of the output equals `build: <s8>`, the
     * reference's `grep -qx` form, repin.sh:54 -- i-1 (M1203-i): NOT the
     * last line, so a `libcurl: ...` line after the build line passes).
     */
    for (i = 0; i < RR_BINARIES; i++) {
        char vcmd[RR_CAP_CMD];
        char want[RR_CAP_SMALL + 16];
        char m[RR_CAP_SMALL + 48];
        int ok;
        jc_snprintf(vcmd, sizeof vcmd, "'%s/%s' --version", B, rr_bins[i]);
        jc_snprintf(want, sizeof want, "build: %s", s8);
        cap[0] = '\0';
        ok = rr_run_cmd(vcmd, NULL, cap, sizeof cap, &child_rc);
        if (ok != 1 || !rr_any_line(cap, want)) {
            jc_snprintf(m, sizeof m, "%s --version does not say "
                        "build: %s", rr_bins[i], s8);
            return rr_die(out, m);
        }
    }

    /* b11: the pin dir is created fresh (immutable once made; NO rm, b16).
     * The reference creates the pin BEFORE the backup (repin.sh:56-57 vs
     * :59-63): a run refused at the cmp leaves the pin in place. */
    jc_snprintf(pin_s8, sizeof pin_s8, "%s/%s", pdir, s8);
    if (rr_exists(pin_s8)) {
        char m[RR_CAP_PATH + 32];
        jc_snprintf(m, sizeof m, "refused: pins/%s exists", s8);
        return rr_die(out, m);
    }
    if (mkdir(pin_s8, 0755) != 0 && !rr_exists(pin_s8)) {
        char m[RR_CAP_SMALL + 48];
        jc_snprintf(m, sizeof m, "pins/%s could not be made", s8);
        return rr_die(out, m);
    }
    for (i = 0; i < RR_BINARIES; i++) {
        char src[RR_CAP_PATH];
        char dst[RR_CAP_PATH];
        jc_snprintf(src, sizeof src, "%s/%s", B, rr_bins[i]);
        jc_snprintf(dst, sizeof dst, "%s/%s", pin_s8, rr_bins[i]);
        if (!rr_cp(a, src, dst)) {
            char m[RR_CAP_SMALL + 48];
            jc_snprintf(m, sizeof m, "pins/%s could not be made", s8);
            return rr_die(out, m);
        }
    }

    /* b12(i): the running pair is snapshotted ONCE per old pin (ONLY when
     * the backup is absent, from the RUNNING bin/). */
    jc_snprintf(bk, sizeof bk, "%s/bin-%s", bkdir, old8);
    if (!rr_exists(bk)) {
        int all = 1;
        if (!rr_mkdir_p(bk)) {
            all = 0;
        }
        for (i = 0; i < RR_BINARIES && all; i++) {
            char src[RR_CAP_PATH];
            char dst[RR_CAP_PATH];
            jc_snprintf(src, sizeof src, "%s/%s", bindir, rr_bins[i]);
            jc_snprintf(dst, sizeof dst, "%s/%s", bk, rr_bins[i]);
            if (!rr_cp(a, src, dst)) {
                all = 0;
            }
        }
        if (!all) {
            return rr_die(out, "the backup failed");
        }
    }

    /* b12(ii): the snapshot must equal the last pin (BACKUP vs PINS/old, per
     * binary; a hand-touched bin/ stops the repin before the swap). */
    for (i = 0; i < RR_BINARIES; i++) {
        char bkb[RR_CAP_PATH];
        char oldb[RR_CAP_PATH];
        jc_snprintf(bkb, sizeof bkb, "%s/%s", bk, rr_bins[i]);
        jc_snprintf(oldb, sizeof oldb, "%s/%s/%s", pdir, old8, rr_bins[i]);
        if (!rr_cmp(a, bkb, oldb)) {
            char m[RR_CAP_PATH + RR_CAP_SMALL + 96];
            jc_snprintf(m, sizeof m,
                        "the backup %s/%s differs from pins/%s/%s: stop "
                        "before the swap", bk, rr_bins[i], old8, rr_bins[i]);
            return rr_die(out, m);
        }
    }

    /* b13: the swap -- cp to .new + mv (rename) per binary, then a cmp back. */
    for (i = 0; i < RR_BINARIES; i++) {
        char src[RR_CAP_PATH];
        char newp[RR_CAP_PATH];
        char binp[RR_CAP_PATH];
        jc_snprintf(src, sizeof src, "%s/%s", pin_s8, rr_bins[i]);
        jc_snprintf(newp, sizeof newp, "%s/%s.new", bindir, rr_bins[i]);
        jc_snprintf(binp, sizeof binp, "%s/%s", bindir, rr_bins[i]);
        if (!rr_cp(a, src, newp) || rename(newp, binp) != 0) {
            char m[RR_CAP_SMALL + 32];
            jc_snprintf(m, sizeof m, "the swap of %s failed", rr_bins[i]);
            return rr_die(out, m);
        }
        if (!rr_cmp(a, binp, src)) {
            char m[RR_CAP_PATH + 64];
            jc_snprintf(m, sizeof m, "bin/%s differs from pins/%s/%s after "
                        "the swap", rr_bins[i], s8, rr_bins[i]);
            return rr_die(out, m);
        }
    }

    /* b14: pins/current -- ln -sfn to .new + mv -T (atomic rename). */
    jc_snprintf(cur_new, sizeof cur_new, "%s/current.new", pdir);
    if (symlink(s8, cur_new) != 0 || rename(cur_new, cur) != 0) {
        return rr_die(out, "pins/current was not moved");
    }

    /* b15: the swap summary, with the new current (the readlink value
     * re-read, not the variable). */
    newcur[0] = '\0';
    if (rr_readlink(cur, newcur, sizeof newcur)) {
        jc_snprintf(msg, sizeof msg,
                    "bin/ == pins/%s (cmp); backup %s; pins/current -> %s",
                    s8, bk, newcur);
    } else {
        jc_snprintf(msg, sizeof msg,
                    "bin/ == pins/%s (cmp); backup %s; pins/current -> ",
                    s8, bk);
    }
    rr_say(out, msg);

    /* b17: the loop-restart measurement -- src/harness changed between the
     * pins (old FULL -> s FULL, on push.git; the line is byte-compatible with
     * the orchestrator's gf_hcn_count reader, M1159). */
    {
        int hc = 0;
        char m[RR_CAP_SMALL + RR_CAP_SMALL + 96];
        an = 0;
        args[an++] = (char *)"diff";
        args[an++] = (char *)"--name-only";
        args[an++] = oldfull;
        args[an++] = full;
        args[an++] = (char *)"--";
        args[an++] = (char *)"src/harness";
        cap[0] = '\0';
        if (rr_git_read(a, push, args, an, RR_CAP_CMD, cap) == 0) {
            hc = rr_count_lines(cap);
        }
        jc_snprintf(m, sizeof m,
                    "src/harness files changed %s..%s: %d (the loop process "
                    "keeps %s's code until it restarts)", old8, s8, hc, old8);
        rr_say(out, m);
    }

    /* b19: the harness doctor (the NEWLY PINNED binary -- the shimmable
     * host.repinDoctor; output file <B>.doctor-harness.txt; the summary's
     * ` 0 problems` test). NOT passed through to stdout (the reference's :79
     * writes it to the file only). */
    {
        char doc_cmd[RR_CAP_CMD];
        char docfile[RR_CAP_PATH];
        char l[RR_CAP_LINE];
        int ok;
        if (doc != NULL && doc[0] != '\0') {
            jc_snprintf(doc_cmd, sizeof doc_cmd, "%s", doc);
        } else {
            /* the reference's podman+image form (never invoked in the
             * container; the host config names the real one). */
            jc_snprintf(doc_cmd, sizeof doc_cmd,
                        "podman run --rm --init --userns=keep-id "
                        "--security-opt=no-new-privileges --cap-drop=all "
                        "--network=pasta:-T,1234 -e HOME -e TMPDIR=/tmp -e "
                        "TZ=Europe/Berlin -w '%s' localhost/jichi-drive "
                        "'%s/jichi-harness' --config '%s/conf/harness.json' "
                        "doctor",
                        bdir, bindir, bindir);
        }
        cap[0] = '\0';
        child_rc = 0;
        ok = rr_run_cmd(doc_cmd, NULL, cap, sizeof cap, &child_rc);
        jc_snprintf(docfile, sizeof docfile, "%s.doctor-harness.txt", B);
        rr_write_file(docfile, cap);
        rr_summary_line(cap, l, sizeof l);
        if (ok == 1) {
            jc_snprintf(msg, sizeof msg, "harness doctor rc=%d: %s", child_rc,
                        (l[0] != '\0') ? l : "NO SUMMARY");
            rr_say(out, msg);
        } else if (ok == -2) {
            /* i-4 (M1203-i): the child out-ran the NAMED bound -- a
             * deadline kill, named (the reference has no deadline; the
             * reference cannot produce this line). fail=1 below. */
            rr_say(out, "harness doctor: killed at the 600 s deadline");
        } else {
            /* F5 (wave-2c): a spawn fault is not `rc=0` -- the helper
             * returned without setting the rc, so printing it would claim
             * a clean doctor. The fault gets its OWN say line (no rc),
             * fail=1; the exit code was already correct. */
            rr_say(out, "harness doctor: the spawn failed");
        }
        if (ok != 1 || !rr_zero_problems(l)) {
            fail = 1;
        }
    }

    /* b20: the per-model `doctor --live` (the key by env, never argv; its
     * 12-char prefix withheld from every output, b21). The model list is
     * shimmable host.repinModels; an absent list is a named refusal (b19's
     * never-guess idiom). The live-doctor command is shimmable
     * host.repinLiveDoctor; absent (or present-but-empty, b19's handling)
     * -> the reference's podman form (F2, wave-2e: b19's default's exact
     * form with the per-model binary/config + `<m>` token, repin.sh:83-101;
     * never invoked in the container -- the host config names it at the
     * switch-over, as for b19). */
    if (mmodels == NULL || mmodels[0] == '\0') {
        return rr_die(out, "no models named (host.repinModels)");
    }
    if (cfg.model_key_file == NULL || cfg.model_key_file[0] == '\0') {
        return rr_die(out, "no model key file named (modelKeyFile)");
    }
    {
        char mlist[RR_CAP_SMALL];
        char def_live[RR_CAP_CMD];
        const char *ltpl;
        if (livedoc == NULL || livedoc[0] == '\0') {
            int nrr;
            /* the reference's podman+image form (b19's default's exact
             * form, + the reference's b20 deltas: -e JICHI_API_KEY, the
             * PATH env, the jichi binary, jichi-config.json, --model
             * `<m>` doctor --live; never invoked in the container; the
             * host config names the real one). */
            nrr = jc_snprintf(def_live, sizeof def_live,
                              "podman run --rm --init --userns=keep-id "
                              "--security-opt=no-new-privileges --cap-drop=all "
                              "--network=pasta:-T,1234 -e JICHI_API_KEY -e "
                              "HOME -e TMPDIR=/tmp -e TZ=Europe/Berlin -e "
                              "PATH='%s:/usr/local/bin:/usr/bin:/bin' -w '%s' "
                              "localhost/jichi-drive '%s/jichi' --config "
                              "'%s/conf/jichi-config.json' --model '<m>' "
                              "doctor --live",
                              bindir, bdir, bindir, bindir);
            if (nrr < 0 || nrr >= (int)sizeof def_live) {
                return rr_die(out, "the live-doctor default path is too long");
            }
            ltpl = def_live;
        } else {
            ltpl = livedoc;
        }
        jc_snprintf(mlist, sizeof mlist, "%s", mmodels);
        for (i = 0; mlist[i] != '\0'; i++) {
            char tok[RR_CAP_SMALL];
            size_t tn = 0;
            /* the model word (up to the next space). */
            while (mlist[i] != '\0' && mlist[i] != ' ' &&
                   tn + 1 < sizeof tok) {
                tok[tn++] = mlist[i++];
            }
            tok[tn] = '\0';
            while (mlist[i] == ' ') {
                i++;
            }
            i--;   /* for-loop re-increments */
            if (tn == 0) {
                continue;
            }
            {
                char live_cmd[RR_CAP_CMD];
                char l[RR_CAP_LINE];
                char rcs2[64];
                char lv2[RR_CAP_LINE];
                char m[RR_CAP_LINE + 96];
                char livefile[RR_CAP_PATH];
                int ok;

                /* The command: the template (named or the default) with
                 * every `<m>` -> the model. */
                live_cmd[0] = '\0';
                {
                    const char *tp = ltpl;
                    size_t used = 0;
                    while (*tp != '\0' && used + RR_CAP_SMALL + 1 <
                           sizeof live_cmd) {
                        if (tp[0] == '<' && tp[1] == 'm' && tp[2] == '>') {
                            int n = jc_snprintf(live_cmd + used,
                                                sizeof live_cmd - used, "%s",
                                                tok);
                            if (n < 0 || n >= (int)(sizeof live_cmd - used)) {
                                break;
                            }
                            used += (size_t)n;
                            tp += 3;
                        } else {
                            live_cmd[used++] = *tp++;
                        }
                    }
                    live_cmd[used] = '\0';
                }
                /* b20: read the key ONCE (whitespace-trimmed). */
                if (!key_loaded) {
                    if (!rr_read_key(a, cfg.model_key_file, key,
                                     sizeof key)) {
                        return rr_die(out,
                                      "the model key could not be loaded "
                                      "(modelKeyFile)");
                    }
                    key_loaded = 1;
                }
                cap[0] = '\0';
                ok = rr_run_cmd(live_cmd, key, cap, sizeof cap, &child_rc);
                /* o = the captured output + a `rc=<n>` line (the reference's
                 * `echo "rc=$?"` appended to $o). */
                o[0] = '\0';
                if (ok == 1) {
                    jc_snprintf(o, sizeof o, "%s\nrc=%d", cap, child_rc);
                } else {
                    /* i-4 (M1203-i): `ok == 1`, not truthy `ok`, so a deadline
                     * kill (ok == -2) is NOT read as a success -- it falls here
                     * like a spawn fault (rc=1, no summary -> not ` 0 problems`
                     * -> fail=1). This site has no `the spawn failed` text, so
                     * its failure shape is unchanged; only the -2 path stops
                     * being a silent success. */
                    jc_snprintf(o, sizeof o, "rc=1");
                    child_rc = 1;
                }
                /* b21: the 12-char prefix matched IN MEMORY (never a grep
                 * child -- the prefix would land on argv, the thing guarded).
                 */
                {
                    char pfx[16];
                    size_t pn = strlen(key);
                    if (pn > 12) {
                        pn = 12;
                    }
                    memcpy(pfx, key, pn);
                    pfx[pn] = '\0';
                    if (pn > 0 && strstr(o, pfx) != NULL) {
                        jc_snprintf(m, sizeof m,
                                    "doctor --live %s: output WITHHELD (it "
                                    "holds the key's prefix)", tok);
                        rr_say(out, m);
                        fail = 1;
                        continue;   /* the output file is NOT written;
                                       CONTINUE to the next model (b21). */
                    }
                }
                /* The output file <B>.doctor-live-<mdl>.txt (the reference's
                 * :95). */
                jc_snprintf(livefile, sizeof livefile,
                            "%s.doctor-live-%s.txt", B, tok);
                rr_write_file(livefile, o);
                /* CHANGED (the driver's cell 17, b20): pass the non-withheld
                 * live-doctor's captured output (+ its rc line) through to
                 * the verb's stdout (the env-delivery teeth read it there). */
                fputs(o, stdout);
                if (o[strlen(o) - 1] != '\n') {
                    fputc('\n', stdout);
                }
                fflush(stdout);
                /* r = the last `rc=<n>` line; l = the summary line; lv =
                 * the first `--live` line (left-trimmed, first 100 chars). */
                {
                    const char *q = o;
                    const char *end = o + strlen(o);
                    size_t best = 0;
                    size_t bestlen = 0;
                    rcs2[0] = '\0';
                    while (q < end) {
                        const char *ls = q;
                        const char *le;
                        while (q < end && *q != '\n') {
                            q++;
                        }
                        le = q;
                        if (q < end) {
                            q++;
                        }
                        if ((size_t)(le - ls) >= 3 && strncmp(ls, "rc=", 3) == 0) {
                            best = (size_t)(ls - o);
                            bestlen = (size_t)(le - ls);
                        }
                    }
                    if (bestlen > 0) {
                        const char *rp = o + best;
                        size_t n;
                        if (strncmp(rp, "rc=", 3) == 0) {
                            rp += 3;   /* the reference's `sed 's/^rc=//'` */
                        }
                        n = strlen(rp);
                        if (n >= sizeof rcs2) {
                            n = sizeof rcs2 - 1;
                        }
                        memcpy(rcs2, rp, n);
                        rcs2[n] = '\0';
                    }
                }
                rr_summary_line(o, l, sizeof l);
                lv2[0] = '\0';
                {
                    const char *q = strstr(o, "--live");
                    if (q != NULL) {
                        const char *ls = q;
                        const char *le = q;
                        while (ls > o && ls[-1] != '\n') {
                            ls--;
                        }
                        while (*le != '\0' && *le != '\n') {
                            le++;
                        }
                        {
                            const char *t = ls;
                            size_t n;
                            while (*t == ' ' || *t == '\t') {
                                t++;
                            }
                            n = (size_t)(le - t);
                            if (n > 100) {
                                n = 100;
                            }
                            if (n >= sizeof lv2) {
                                n = sizeof lv2 - 1;
                            }
                            memcpy(lv2, t, n);
                            lv2[n] = '\0';
                        }
                    }
                }
                jc_snprintf(m, sizeof m,
                            "doctor --live %s rc=%s: %s | %s", tok,
                            (rcs2[0] != '\0') ? rcs2 : "0",
                            (l[0] != '\0') ? l : "NO SUMMARY", lv2);
                rr_say(out, m);
                if (!rr_zero_problems(l)) {
                    fail = 1;
                }
            }
        }
    }
    /* b22: the summary and the exit contract -- the LAST line is always
     * `repin: done <s8>, doctors clean|NOT clean`. */
    if (fail == 0) {
        jc_snprintf(msg, sizeof msg, "done %s, doctors clean", s8);
    } else {
        jc_snprintf(msg, sizeof msg, "done %s, doctors NOT clean", s8);
    }
    rr_say(out, msg);
    return fail;
}
