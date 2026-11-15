/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_notify.c - `jichi-harness notify` (M1152, batch H8): the C89 port
 * of the 2026-10-27 hardening-host drive's gate-note.sh. It delivers ONE host-
 * gate verdict to its two readers, "no session needed":
 *
 *   the LEAD: a note in the drive's inbox/ (the harness's run moves it into the
 *   next run's prompt), plus, when a run is LIVE, an inject of the note body
 *   through that run's control socket (<dir>/bin/jichi control <ctl/lead.sock>
 *   inject <body>).
 *   the OPERATOR: a line appended to <gateHome>/digest.md and a config-named
 *   notification hook that receives the DIGEST PATH (the CHANGED half; B6).
 *
 * Design note (the contract, the eleven numbered behaviours B1-B11):
 *   docs/internal/drive/2026-11-05-harness-port/notes/M1152.md
 * Ported reference (read line by line; this file carries B1-B7, B9-B10):
 *   docs/internal/drive/2026-10-27-hardening-host/gate-note.sh
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO (the note's IS-NOT / CHANGED lines,
 * kept out on purpose):
 *   - the watch.out file (behaviour 8, DROPPED): the reference's third output
 *     moves to a STDOUT line the caller can log, printed instead of appended to
 *     <gateHome>/watch.out (the note's "What this note does NOT decide" half).
 *   - notify-send (behaviour 6, CHANGED): the hook is a config-named command
 *     (host.notifyCommand) that gets the DIGEST PATH as its single argument; no
 *     part of this port calls notify-send.
 *   - verdict logic (behaviour 11): this is a PRINTER and a MESSENGER. The
 *     caller (H9's gate-run) picks the verdict word and calls notify on every
 *     one it writes; a new verdict word costs nothing here.
 *
 * THE FILENAME (B1, pinned by the driver's cell 1): the note's filename is
 * <inbox>/<stamp>-gate-<sha8>.md, byte-identical to the reference's gate-note.sh
 * line 12 `s=${1:?usage: gate-note.sh SHA8 TEXT}` (the `:?` is error-if-unset,
 * NOT a substring, so `$s` is the FULL sha8) and its line 16
 * `f=$INBOX/$stamp-gate-$s.md`.  The note's TITLE, RECORDS line, DIGEST line
 * and WATCH line all carry the full sha8 as well (the reference's printf %s
 * of its argument): the driver asserts "# Host gate: 00000000" and
 * "Records on the host: <gate>/00000000/".
 */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>   /* pid_t (portability_lint 29) */
#include <sys/wait.h>    /* WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG */

#include "jc_platform.h"  /* jc_status, jc_size, jc_mkdir_p, jc_is_dir */
#include "jc_mem.h"       /* struct jc_arena, jc_arena_strdup */
#include "jc_str.h"       /* struct jc_sb */
#include "jc_snprintf.h"  /* jc_snprintf, never sprintf */
#include "jc_proc.h"      /* jc_proc_waitpid, jc_pipe_cloexec */
#include "jc_spawn.h"     /* jc_spawn, jc_spawn_req, JC_SPAWN_DEVNULL */
#include "jc_fcntl.h"     /* JC_O_BINARY (portability_lint 38) */
#include "jc_harness_config.h"
#include "jc_harness_notify.h"

/* Paths hang off the config's dir and the gate home; give the joins room, the
 * same bound the other harness commands use. */
#define NOTIFY_PATH_CAP 4352
/* A note body and a digest line are small in practice. The TEXT is bounded so
 * the note body buffer (mid) stays heap-sized, not unbounded. OVER-CAP TEXT is
 * a NAMED REFUSAL (exit 1), never a silent truncation: a truncated verdict is
 * a wrong verdict (the same class as the four false verdicts this port exists
 * to prevent). The reference prints the TEXT with NO bound (gate-note.sh:15,
 * :21) -- this bound is a deliberate CHANGED (the note's behaviour list). */
#define NOTIFY_TEXT_CAP (128 * 1024)
/* Headroom reserved inside the note body buffer for the fixed "Host gate
 * (automatic, <now>): " prefix (up to its REHEARSAL form). The TEXT is refused
 * when TEXT + this prefix would not fit NOTIFY_TEXT_CAP, i.e. when the old
 * code's jc_snprintf would have silently truncated it. */
#define NOTIFY_TEXT_PREFIX_HEADROOM 256
/* The attach is bounded for the same reason (a review) so the note buffer
 * stays heap-sized, not unbounded. OVER-CAP ATTACH is a NAMED REFUSAL (exit
 * 1), never a silent truncation -- the reference cats the attach with NO bound
 * (gate-note.sh:19) -- a deliberate CHANGED. */
#define NOTIFY_ATTACH_CAP (1024 * 1024)
/* A hook command line is short; cap the arg count (the pageCommand idiom). */
#define NOTIFY_HOOK_ARGS 16
/* The inject's captured reply is cut to 160 (the reference's cut -c1-160). */
#define NOTIFY_INJ_CUT 160

static void
notify_refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: notify: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* timegm (glibc's UTC mktime), but the build's _POSIX_C_SOURCE does not expose
 * it, so this hand-rolls it -- the SAME proleptic-Gregorian days-from-civil
 * algorithm jc_hp_tick.c / jc_harness_drive.c use. */
static time_t
notify_utc_from_tm(const struct tm *tmv)
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

/* The reference's timestamp shape: `date '+%F %T %z'` (gate-note.sh :14),
 * e.g. `2026-11-05 10:23:45 +0100`. `%F %T %z` are GNU strftime specifiers this
 * C89 build rejects, so the same bytes are assembled from the ISO
 * `%Y-%m-%d %H:%M:%S` plus the LOCAL offset (the drive's stamp_of idiom,
 * verified against date(1) at UTC / -0400 / +1400). A clock that cannot format
 * is not a reason to stop a notify (the reference would print an empty stamp):
 * say "unknown", never crash. The driver greps the note/digest by the SHA and
 * the text, not the stamp. */
static void
notify_ts(char *out, size_t cap)
{
    time_t t;
    struct tm tmv;
    struct tm tmc;
    long off;
    long mag;
    int oh, om;
    size_t n;

    t = time(NULL);
    if (localtime_r(&t, &tmv) == NULL) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
        tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
        off = 0;
    } else {
        tmc = tmv;
        off = (long)notify_utc_from_tm(&tmc) - (long)t;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        jc_snprintf(out, cap, "unknown");
        return;
    }
    mag = (off < 0) ? -off : off;
    oh = (int)(mag / 3600);
    om = (int)((mag % 3600) / 60);
    if (oh < 100 && cap > n + 7) {
        out[n] = ' ';
        out[n + 1] = (off < 0) ? '-' : '+';
        out[n + 2] = (char)('0' + oh / 10);
        out[n + 3] = (char)('0' + oh % 10);
        out[n + 4] = (char)('0' + om / 10);
        out[n + 5] = (char)('0' + om % 10);
        out[n + 6] = '\0';
    }
}

/* The reference's `stamp=date '+%Y%m%d-%H%M'` (gate-note.sh :14): the minute-
 * precision name prefix. Same hand-rolled approach as notify_ts. */
static void
notify_stamp(char *out, size_t cap)
{
    time_t t;
    struct tm tmv;
    size_t n;
    t = time(NULL);
    if (localtime_r(&t, &tmv) == NULL) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
        tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
    }
    n = strftime(out, cap, "%Y%m%d-%H%M", &tmv);
    if (n == 0) {
        jc_snprintf(out, cap, "unknown");
    }
}

/* The basename of a path (the reference's $(basename "$f")); on a NULL/empty
 * input, "unknown". Copies into out (cap bound). */
static void
notify_basename(const char *path, char *out, size_t cap)
{
    const char *p;
    size_t n;
    if (path == NULL || path[0] == '\0') {
        jc_snprintf(out, cap, "unknown");
        return;
    }
    p = strrchr(path, '/');
    if (p == NULL) {
        p = path;
    } else {
        p++;
    }
    if (p[0] == '\0') {
        p = path;
    }
    n = strlen(p);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* Write `data` (len bytes) to `path` atomically: <path>.tmp then rename over
 * <path> (behaviour 2; the reference's `> "$f.tmp" && mv "$f.tmp" "$f"`).
 * Returns 0 on failure (a note that cannot be written is a refusal, not a
 * success). */
static int
notify_write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[NOTIFY_PATH_CAP + 8];
    FILE *f;
    int ok = 1;
    if (jc_snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        return 0;
    }
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return 0;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        ok = 0;
    }
    if (ferror(f)) {
        ok = 0;
    }
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (ok && rename(tmp, path) != 0) {
        (void)remove(tmp);
        ok = 0;
    }
    return ok;
}

/* Append one line to `path` (the reference's `>> "$G/digest.md"`); the digest
 * file is created when absent. Returns 0 on a write that could not open. */
static int
notify_append_line(const char *path, const char *line)
{
    FILE *f;
    int ok = 1;
    f = fopen(path, "ab");
    if (f == NULL) {
        return 0;
    }
    if (fputs(line, f) == EOF) {
        ok = 0;
    }
    if (fputc('\n', f) == EOF) {
        ok = 0;
    }
    fclose(f);
    return ok;
}

/* Does `path` hold a line matching ^end= ? (behaviour 4's run-boundary half:
 * the reference's `grep -q '^end=' "$R/meta.txt"`. A run that ENDED carries an
 * end= line the harness writes after its child exits.) Returns 1 when such a
 * line is found. A file that cannot be read is NOT "ended" (returns 0), the way
 * the reference's `2>/dev/null` makes an unreadable meta not match. */
static int
notify_meta_ended(const char *path)
{
    FILE *f;
    int c;
    int i;
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    c = fgetc(f);
    for (;;) {
        if (c != 'e') {
            /* Not the start of an end= line; skip to the next line. */
            while (c != EOF && c != '\n') {
                c = fgetc(f);
            }
            if (c == EOF) {
                break;
            }
            c = fgetc(f);
            continue;
        }
        /* c == 'e': check whether this line begins "end=". */
        i = 1;
        c = fgetc(f);
        while (i < 4 && c != EOF && c == (int)"end="[(int)i]) {
            i++;
            c = fgetc(f);
        }
        if (i == 4) {
            fclose(f);
            return 1;
        }
        /* A different line that merely starts with 'e'; skip to the next line. */
        while (c != EOF && c != '\n') {
            c = fgetc(f);
        }
        if (c == EOF) {
            break;
        }
        c = fgetc(f);
    }
    fclose(f);
    return 0;
}

/* The newest run number under <dir>/runs: the LARGEST 4-digit name (the
 * reference's `ls | sed -n 's/^\([0-9]\{4\}\)$/\1/p' | sort -n | tail -1`):
 * numeric sort, not lexicographic. Returns -1 when there is no such run (an
 * absent or empty runs dir = no run, the reference's empty `n`). */
static int
notify_newest_run(const char *runs_dir)
{
    DIR *d;
    struct dirent *e;
    int best = -1;
    d = opendir(runs_dir);
    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        size_t len = strlen(nm);
        int digits = 0;
        int value = 0;
        size_t j;
        if (len != 4) {
            continue;
        }
        for (j = 0; j < len; j++) {
            if (nm[j] < '0' || nm[j] > '9') {
                break;
            }
            value = value * 10 + (nm[j] - '0');
            digits++;
        }
        if (digits != 4) {
            continue;
        }
        if (best == -1 || value > best) {
            best = value;
        }
    }
    closedir(d);
    return best;
}

/* Run `argv` (NULL-terminated) with stdin from /dev/null and its combined
 * stdout+stderr drained into `sb`, then reap it: `*rc_out` is the child's exit
 * code (128+signal when signalled). Returns -1 when it could not start; else 0.
 * There is no cap and no timeout: a local control call returns fast (the
 * reference has none either). */
static int
notify_spawn_capture(char *const argv[], struct jc_sb *sb, int *rc_out)
{
    struct jc_spawn_req req;
    int outp[2];
    pid_t pid;
    int status;
    int rc;
    unsigned char chunk[4096];
    ssize_t r;

    *rc_out = -1;
    if (jc_pipe_cloexec(outp) != 0) {
        return -1;
    }
    memset(&req, 0, sizeof req);
    req.argv = argv;
    req.fd_stdin = JC_SPAWN_DEVNULL;   /* the reference's < /dev/null */
    req.fd_stdout = outp[1];
    req.fd_stderr = outp[1];          /* the reference's 2>&1 */
    if (jc_spawn(&req, &pid) != JC_OK) {
        close(outp[0]);
        close(outp[1]);
        return -1;
    }
    close(outp[1]);
    for (;;) {
        r = read(outp[0], chunk, sizeof chunk);
        if (r > 0) {
            (void)jc_sb_append_n(sb, (const char *)chunk, (jc_size)r);
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(outp[0]);
    rc = -1;
    if (jc_proc_waitpid(pid, &status) == 0) {
        if (WIFEXITED(status)) {
            rc = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            rc = 128 + WTERMSIG(status);
        }
    }
    *rc_out = rc;
    return 0;
}

/* Flatten `data` EXACTLY as the reference's `tr '\n' ' '`, over the bytes as
 * GIVEN: ONE space for EVERY newline, no coalescing (a blank line -- two
 * newlines -- carries as TWO spaces). The injected string's bytes are carried,
 * not normalised; a prior port collapsed runs of newlines to a single space,
 * which broke them (the qwen27b review's FINDING 1). Then cut to NOTIFY_INJ_CUT
 * bytes (the reference's `cut -c1-160`). The reference's TRAILING newline(s) are
 * stripped BEFORE this (its `o=$(... 2>&1)` command substitution,
 * gate-note.sh:26 -- the caller's job, see the inject path), so they do not
 * reach this function as trailing spaces (M1152-i). */
static void
notify_flatten_cut(const char *data, size_t len, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    if (cap == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        if (o + 1 >= cap) {
            break;   /* cut -c1-160: room for one more byte + the NUL is gone */
        }
        out[o++] = (data[i] == '\n') ? ' ' : data[i];
    }
    out[o] = '\0';
}

/* `notify` (M1152, H8): deliver one host-gate verdict. The positionals (sha8,
 * text) are validated by the DISPATCH (main.c) BEFORE this is called -- a
 * missing SHA8 or TEXT is a usage refusal (exit 2) that happens before any
 * write, so both are non-NULL and non-empty here. `attach` is NULL when absent
 * (skipped silently, the reference's [ -s ]). */
int
jc_harness_notify(const char *config_path, const char *inbox_arg,
                  const char *sha8, const char *text, const char *attach,
                  struct jc_arena *a)
{
    struct jc_harness_config cfg;
    char dir[NOTIFY_PATH_CAP];
    char gate[NOTIFY_PATH_CAP];
    char inbox[NOTIFY_PATH_CAP];
    char note_path[NOTIFY_PATH_CAP + 64];
    char digest_path[NOTIFY_PATH_CAP + 32];
    char runs_dir[NOTIFY_PATH_CAP + 16];
    char run_dir[NOTIFY_PATH_CAP + 24];
    char sock_path[NOTIFY_PATH_CAP + 32];
    char meta_path[NOTIFY_PATH_CAP + 32];
    char jichi_bin[NOTIFY_PATH_CAP + 16];
    char now[64];
    char stamp[32];
    char note_base[256];
    char digest_line[NOTIFY_TEXT_CAP + 64];
    char watch_line[NOTIFY_TEXT_CAP + 128];
    char inj[NOTIFY_INJ_CUT + 96];
    char mid[NOTIFY_TEXT_CAP];          /* the note body text, prefix included */
    char *note;                         /* the assembled note content (heap) */
    size_t note_len;
    size_t note_cap;
    struct jc_sb inject_sb;
    struct stat st;
    int scratch;
    int newest;
    int rrc;
    int hook_rc;
    int live;
    size_t inj_len;

    if (config_path == NULL) {
        notify_refuse("--config <path> is required");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *m = jc_harness_config_error(&cfg);
        notify_refuse("could not read the config: %s",
                      (m != NULL) ? m : "unknown error");
        return 2;
    }
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        notify_refuse("the config has no dir (the inbox and the runs derive "
                      "from it)");
        return 2;
    }
    if (jc_snprintf(dir, sizeof dir, "%s", cfg.dir) >= (int)sizeof dir) {
        notify_refuse("the dir path is too long");
        return 2;
    }

    /* The gate home (behaviour 10): host.gateHome when set, else the DEFAULT
     * <dir>/gate -- named in the code, not silently used. */
    if (cfg.host_gate_home != NULL && cfg.host_gate_home[0] != '\0') {
        if (jc_snprintf(gate, sizeof gate, "%s", cfg.host_gate_home)
            >= (int)sizeof gate) {
            notify_refuse("the gate home path is too long");
            return 2;
        }
    } else if (jc_snprintf(gate, sizeof gate, "%s/gate", dir)
               >= (int)sizeof gate) {
        notify_refuse("the gate home path is too long (the default %s/gate)",
                      dir);
        return 2;
    }
    /* The inbox: --inbox <dir> when given (the scratch inbox), else <dir>/inbox. */
    scratch = (inbox_arg != NULL && inbox_arg[0] != '\0');
    if (scratch) {
        if (jc_snprintf(inbox, sizeof inbox, "%s", inbox_arg)
            >= (int)sizeof inbox) {
            notify_refuse("the inbox path is too long");
            return 2;
        }
    } else if (jc_snprintf(inbox, sizeof inbox, "%s/inbox", dir)
               >= (int)sizeof inbox) {
        notify_refuse("the inbox path is too long");
        return 2;
    }

    /* Over-cap input is a NAMED REFUSAL, never a silent truncation (the
     * qwen27b review's FINDING 2). The reference cats the ATTACH and prints the
     * TEXT with NO bound (gate-note.sh:15, :19, :21); the port BOUNDS them as a
     * memory bound on the note buffer. A silently-truncated verdict is a WRONG
     * verdict (the same class as the four false verdicts this port exists to
     * prevent), and behaviour 1 promises byte-identical <contents> -- so the
     * port refuses instead, with exit 1, the cap and the actual size named,
     * BEFORE the gate-home/inbox mkdir and before any fopen (like the arg
     * refusal above -- a refused notify changes nothing). The caps stay as
     * memory bounds; the refusal is the fix, not the number.
     *
     * The TEXT is refused when it cannot fit the note body buffer (mid) along
     * with the fixed "Host gate (automatic, <now>): " prefix (its REHEARSAL
     * form, the longest) -- i.e. exactly when the old code's jc_snprintf would
     * have silently truncated it. The ATTACH is refused when its size (the
     * whole file is cat'ed) exceeds NOTIFY_ATTACH_CAP -- the old code read only
     * the first cap bytes and dropped the rest. */
    {
        size_t text_len = strlen(text);
        if (text_len > NOTIFY_TEXT_CAP - NOTIFY_TEXT_PREFIX_HEADROOM) {
            notify_refuse("the TEXT is %lu bytes; it would not fit the %d-byte "
                          "note body (NOTIFY_TEXT_CAP) alongside the fixed "
                          "'Host gate (automatic, <now>): ' prefix -- refusing "
                          "before any write (the reference prints the TEXT "
                          "unbounded -- a silently truncated verdict is a "
                          "wrong verdict)",
                          (unsigned long)text_len, NOTIFY_TEXT_CAP);
            return 1;
        }
        if (attach != NULL && attach[0] != '\0') {
            struct stat ast;
            if (stat(attach, &ast) == 0 && S_ISREG(ast.st_mode)
                && (size_t)ast.st_size > NOTIFY_ATTACH_CAP) {
                notify_refuse("the ATTACH %s is %lu bytes, over the %d-byte "
                              "bound (NOTIFY_ATTACH_CAP); refusing before any "
                              "write (the reference cats the attach unbounded "
                              "-- a silently truncated verdict is a wrong "
                              "verdict)", attach, (unsigned long)ast.st_size,
                              NOTIFY_ATTACH_CAP);
                return 1;
            }
        }
    }

    /* mkdir -p the gate home and the inbox (behaviour 10; the reference makes
     * the gate home, the port makes both, idempotent). A gate home that cannot
     * be created REFUSES naming it, before any write. */
    if (jc_mkdir_p(gate) != JC_OK) {
        notify_refuse("could not create the gate home %s (the digest.md lives "
                      "there); refusing before any write", gate);
        return 2;
    }
    if (jc_mkdir_p(inbox) != JC_OK) {
        notify_refuse("could not create the inbox %s (the note lives there); "
                      "refusing before any write", inbox);
        return 2;
    }

    notify_ts(now, sizeof now);
    notify_stamp(stamp, sizeof stamp);

    /* The note body text (behaviour 1, byte-identical): [REHEARSAL prefix when
     * scratch] + "Host gate (automatic, <now>): <TEXT>". This same text is the
     * inject's body (behaviour 5: the NOTE BODY, prefix included). */
    if (scratch) {
        jc_snprintf(mid, sizeof mid,
                    "REHEARSAL (scratch inbox, nothing is delivered to the "
                    "lead) -- Host gate (automatic, %s): %s", now, text);
    } else {
        jc_snprintf(mid, sizeof mid, "Host gate (automatic, %s): %s", now,
                    text);
    }

    /* Assemble the note content (behaviour 1):
     *   # Host gate: <sha>\n\n<body>\n\nRecords on the host: <G>/<sha>/
     *     (the supervisor reads them; you need not).\n
     * plus, when ATTACH is given and non-empty (the reference's [ -s ]),
     *   \n## Attached: <basename>\n\n<contents>.
     * The attach is bounded; a missing or empty attach is skipped silently. */
    {
        char records[NOTIFY_PATH_CAP + 48];
        char header[NOTIFY_PATH_CAP + 64];
        size_t attach_bytes = 0;
        FILE *af = NULL;
        struct stat ast;
        if (attach != NULL && attach[0] != '\0'
            && stat(attach, &ast) == 0 && S_ISREG(ast.st_mode)
            && ast.st_size > 0) {
            attach_bytes = ((size_t)ast.st_size > NOTIFY_ATTACH_CAP)
                             ? NOTIFY_ATTACH_CAP : (size_t)ast.st_size;
            af = fopen(attach, "rb");
        }
        jc_snprintf(header, sizeof header, "# Host gate: %s\n\n%s\n\n",
                    sha8, mid);
        jc_snprintf(records, sizeof records,
                    "Records on the host: %s/%s/ (the supervisor reads them; "
                    "you need not).\n", gate, sha8);
        note_cap = strlen(header) + strlen(records) + attach_bytes + 64;
        note = (char *)malloc(note_cap);
        if (note == NULL) {
            if (af != NULL) {
                fclose(af);
            }
            notify_refuse("out of memory building the note");
            return 1;
        }
        note_len = jc_snprintf(note, note_cap, "%s%s", header, records);
        /* Append the attach section (header + contents), when present. */
        if (af != NULL) {
            char *ap = note + note_len;
            size_t rem = note_cap - (size_t)note_len;
            char abase[256];
            size_t rd;
            notify_basename(attach, abase, sizeof abase);
            jc_snprintf(ap, rem, "\n## Attached: %s\n\n", abase);
            note_len += strlen(ap);
            ap = note + note_len;
            rem = note_cap - (size_t)note_len;
            while (rem > 0 && (rd = fread(ap, 1, rem, af)) > 0) {
                ap += rd;
                rem -= rd;
                note_len += (int)rd;
            }
            *ap = '\0';
            fclose(af);
        }
    }

    /* The note filename (behaviour 1, byte-identical to the reference's
     * gate-note.sh:16 `f=$INBOX/$stamp-gate-$s.md` with $s the FULL sha8):
     * <inbox>/<stamp>-gate-<sha8>.md. */
    if (jc_snprintf(note_path, sizeof note_path, "%s/%s-gate-%s.md",
                    inbox, stamp, sha8) >= (int)sizeof note_path) {
        free(note);
        notify_refuse("the note path is too long");
        return 2;
    }
    if (!notify_write_atomic(note_path, note, (size_t)note_len)) {
        free(note);
        notify_refuse("could not write the note %s (the inbox %s is not "
                      "writable?)", note_path, inbox);
        return 2;
    }
    free(note);

    /* The digest line (behaviour 7, byte-identical): <now> gate <sha>:
     * [REHEARSAL: ]<TEXT> appended to <gate>/digest.md. */
    if (jc_snprintf(digest_path, sizeof digest_path, "%s/digest.md", gate)
        >= (int)sizeof digest_path) {
        notify_refuse("the digest path is too long");
        return 2;
    }
    if (scratch) {
        jc_snprintf(digest_line, sizeof digest_line,
                    "%s gate %s: REHEARSAL: %s", now, sha8, text);
    } else {
        jc_snprintf(digest_line, sizeof digest_line, "%s gate %s: %s", now,
                    sha8, text);
    }
    if (!notify_append_line(digest_path, digest_line)) {
        notify_refuse("could not append the digest line to %s (the gate home "
                      "%s is not writable?)", digest_path, gate);
        return 2;
    }

    /* The inject (behaviours 4-5): the newest run under <dir>/runs, LIVE iff it
     * exists AND its ctl/lead.sock is a REAL SOCKET AND its meta.txt has no
     * end= line AND no scratch inbox. The command (verbatim the reference's
     * <dir>/bin/jichi control <sock> inject <body> < /dev/null) uses the PINNED
     * drive binary (not this program) and the NOTE BODY (mid, prefix included).
     * A failed inject NEVER changes notify's rc -- it is recorded in the inj
     * string only. */
    if (jc_snprintf(runs_dir, sizeof runs_dir, "%s/runs", dir)
        >= (int)sizeof runs_dir) {
        notify_refuse("the runs path is too long");
        return 2;
    }
    newest = notify_newest_run(runs_dir);
    live = 0;
    if (newest >= 0 && !scratch) {
        char runnum[16];
        jc_snprintf(runnum, sizeof runnum, "%04d", newest);
        if (jc_snprintf(run_dir, sizeof run_dir, "%s/%s", runs_dir,
                        runnum) < (int)sizeof run_dir &&
            jc_snprintf(sock_path, sizeof sock_path, "%s/ctl/lead.sock",
                        run_dir) < (int)sizeof sock_path &&
            stat(sock_path, &st) == 0 && S_ISSOCK(st.st_mode) &&
            jc_snprintf(meta_path, sizeof meta_path, "%s/meta.txt",
                        run_dir) < (int)sizeof meta_path &&
            !notify_meta_ended(meta_path)) {
            live = 1;
        }
    }
    if (!live) {
        jc_snprintf(inj, sizeof inj, "no live run");
    } else {
        char runnum[16];
        char *iargv[6];
        jc_sb_init(&inject_sb);
        jc_snprintf(runnum, sizeof runnum, "%04d", newest);
        if (jc_snprintf(jichi_bin, sizeof jichi_bin, "%s/bin/jichi", dir)
            >= (int)sizeof jichi_bin) {
            jc_snprintf(inj, sizeof inj, "no live run");
        } else {
            rrc = -1;
            iargv[0] = jichi_bin;
            iargv[1] = (char *)"control";
            iargv[2] = sock_path;
            iargv[3] = (char *)"inject";
            iargv[4] = mid;
            iargv[5] = NULL;
            if (notify_spawn_capture(iargv, &inject_sb, &rrc) != 0
                || rrc == -1) {
                jc_snprintf(inj, sizeof inj,
                            "run %s inject rc=-1 (the pinned jichi could not "
                            "be run)", runnum);
            } else {
                char flat[NOTIFY_INJ_CUT + 1];
                /* The reference's o=$(... 2>&1) (gate-note.sh:26) strips the
                 * reply's TRAILING newline(s) BEFORE `tr '\n' ' '`, so the port
                 * must strip them here first: an echo-style reply
                 * `\"ok\":true}\n` would otherwise carry a trailing space the
                 * reference's never does (M1152-i; cell 3f pins the byte).
                 * INTERNAL newlines are not stripped -- cell 8 holds one space
                 * per newline, no coalescing. */
                inj_len = (inject_sb.data != NULL) ? inject_sb.len : 0;
                while (inj_len > 0
                       && inject_sb.data[inj_len - 1] == '\n') {
                    inj_len--;
                }
                notify_flatten_cut(
                    (inject_sb.data != NULL) ? inject_sb.data : "",
                    inj_len, flat, sizeof flat);
                jc_snprintf(inj, sizeof inj, "run %s inject rc=%d %s", runnum,
                            rrc, flat);
            }
        }
        jc_sb_free(&inject_sb);
    }

    /* The hook (behaviour 6, CHANGED): host.notifyCommand, run with the DIGEST
     * PATH as its single argument. A failure is LOGGED, never fatal (the rc is
     * named on stderr); an ABSENT hook runs nothing and errors nothing (the
     * pageCommand idiom). */
    hook_rc = 0;
    if (cfg.host_notify_command != NULL
        && cfg.host_notify_command[0] != '\0') {
        char *line = (char *)malloc(strlen(cfg.host_notify_command) + 1);
        char *tok;
        int n = 0;
        char *hook_argv[NOTIFY_HOOK_ARGS + 2];
        struct jc_sb hook_sb;
        if (line == NULL) {
            notify_refuse("out of memory holding the hook command");
            return 1;
        }
        memcpy(line, cfg.host_notify_command,
               strlen(cfg.host_notify_command) + 1);
        tok = strtok(line, " \t");
        if (tok == NULL) {
            free(line);
        } else {
            hook_argv[0] = tok;
            n = 1;
            while (n < NOTIFY_HOOK_ARGS) {
                hook_argv[n] = strtok(NULL, " \t");
                if (hook_argv[n] == NULL) {
                    break;
                }
                n++;
            }
            if (n >= NOTIFY_HOOK_ARGS) {
                fprintf(stderr,
                        "jichi-harness: notify: notifyCommand is too long to "
                        "run (>=%d words); not run\n", NOTIFY_HOOK_ARGS);
                free(line);
            } else {
                hook_argv[n] = digest_path;   /* the DIGEST PATH, not the text */
                n++;
                hook_argv[n] = NULL;
                jc_sb_init(&hook_sb);
                (void)notify_spawn_capture(hook_argv, &hook_sb, &hook_rc);
                jc_sb_free(&hook_sb);
                if (hook_rc != 0) {
                    fprintf(stderr,
                            "jichi-harness: notify: the hook failed (rc=%d); "
                            "logged, never fatal\n", hook_rc);
                }
                free(line);
            }
        }
    }

    /* The stdout watch line (behaviour 8's CHANGED output home): the
     * reference's watch.out content, printed to stdout instead of a file,
     * byte-identical: <now> gate <sha>: note <basename>; <inj>. */
    notify_basename(note_path, note_base, sizeof note_base);
    jc_snprintf(watch_line, sizeof watch_line,
                "%s gate %s: note %s; %s\n", now, sha8, note_base, inj);
    fputs(watch_line, stdout);
    fflush(stdout);

    /* The rc: the verdict was delivered (note + digest written, hook run or
     * absent). The inject's and the hook's rcs are NEVER fatal. */
    return 0;
}
