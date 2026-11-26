/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_tick.c - the `push tick` subcommand body (B2, M1147, batch H7;
 * the push.git refresh is M1211, batch H7 follow-up): ONE host-push tick
 * under a lock, with paging and a consecutive-failure state. The C89 port
 * of the 2026-10-27 hardening-host drive's push-tick.sh (the state machine)
 * plus push.sh's lock (push.sh L41-43) and push.sh's push.git refresh
 * (push.sh L53-58, L70-72).
 *
 * WHAT A TICK DOES, in order (the reference verbatim where quoted):
 *   1. The lock: <pushHome>/push.lock, ONE call `lockf(fd, F_LOCK, 0)` after
 *      opening it O_RDWR|O_CREAT. env PUSH_LOCKED set -> skip the lock
 *      entirely (the reference's re-entrancy guard, push.sh L41-42). F_LOCK
 *      BLOCKS until the lock is free: a tick that starts while another holds
 *      the lock WAITS (a lock, not a cap -- the reference's `flock -o`, a
 *      WAIT; there is no timeout and no polling loop here).
 *   2. The push, with the M1211 push.git refresh (b1-b4). The refresh runs
 *      the reference's push.sh git operations on push.git, which the tick's
 *      two in-process pushes do not touch:
 *        b1 (push.sh L53-55, FATAL): after `push: start`, a credential-
 *      wired fetch of the remote's `master` and the `<BR>` branch family
 *      into push.git's TRACKING refs (the `+`: the tracking refs always
 *      advance). The
 *      remote URL comes from `git remote get-url --push origin` in the
 *      mirror (the same command the team push's URL guard runs; CHANGED:
 *      used directly, no `origin` remote in push.git). On failure the tick
 *      logs the UNtimestamped line `push: the fetch of the remote's refs
 *      failed (rc=N): nothing pushed` and NOTHING is pushed: rc is 1, the
 *      team push, the records push, the b3/b4 lines AND the timestamped
 *      `push: end rc=` are all skipped (the reference's `exit 1` leaves no
 *      end line), and the tick falls through to the state machine (TICK
 *      FAIL, the page can fire) via `goto tick_state`.
 *        b2 (push.sh L57-58, non-fatal): a fetch of the mirror's
 *      `<BR>/main` and the `<BR>/work` family into push.git's LOCAL
 *      branches, NO
 *      `+` (a non-fast-forward update is REFUSED by git: a rewritten ref is
 *      not carried over). CHANGED: the mirror is read directly as a local
 *      path (`git fetch <mirror_path>`), not through a podman upload-pack
 *      container -- the mirror is at `<dir>/mirror.git`, local to the tick
 *      in both the rehearsal and the real drive. No credential flags (a
 *      local read). On failure the tick logs the UNtimestamped line
 *      `push: the fetch from the drive's mirror failed (rc=N)` and sets
 *      the tick's rc to 1 (b2_failed), but the team push STILL RUNS
 *      (the reference's rc=1 is non-fatal).
 *      THEN the TEAM push (`jc_harness_mirror_push_run`, the mirror
 *      positional -- exactly what push.sh L65 calls). THEN b3 (push.sh
 *      L70-71, LOG-ONLY) and b4 (push.sh L72):
 *        b3: a credential-wired re-fetch of the remote's `<BR>` branch
 *      family into push.git's tracking refs (the `+`: always advance, so
 *      push.git's
 *      tracking refs say what the remote holds NOW). On failure the tick
 *      logs the UNtimestamped line `push: the fetch of the remote's team
 *      refs after the push failed (rc=N)` and the tick's rc is UNCHANGED.
 *        b4: the UNtimestamped line `push: team main is <short> (push.git's
 *      copy)`, where <short> is `git rev-parse --short refs/heads/<BR>/main`
 *      in push.git; if the ref is absent the line carries the empty string
 *      (the reference's `2>/dev/null`).
 *      THEN the RECORDS push (`jc_harness_push_records_run`, the recs
 *      positional). The tick's rc is the OR of the three (team, records,
 *      b2_failed), normalised: any non-zero -> 1, all zero -> 0.
 *   3. The logs: <pushHome>/push.log carries the tick's push lines in the
 *      reference's timestamp shape (`date '+%F %T %z'`): `push: start`, the
 *      team push's outcome line, the records push's outcome line, and
 *      `push: end rc=N`. The two in-process entries print their own lines to
 *      stdout (the mirror push's `say`, the records push's lines), so the tick
 *      redirects its OWN stdout to push.log (append) for the duration of the
 *      two calls -- the entries' stdout IS the log (the reference's push.sh
 *      appends to the log; the in-process entries print, so the tick tees).
 *   4. The state: <pushHome>/push-tick.state, a one-line int; the read is the
 *      reference's case pattern (non-numeric/empty -> 0).
 *   5. Success: `TICK ok | <last push.log line, cut to 200>`; if the failure
 *      count was > 0, `RECOVERED after <fails> failed tick(s)`; state <- 0.
 *   6. Failure: fails++; state <- fails; `TICK FAIL rc=<rc> (#<fails>) |
 *      <last line>`; on the FIRST failure and every 6th (`fails == 1 ||
 *      fails % 6 == 0`) the PAGE: `msg = jichi drive: the host push failed
 *      (<fails> tick(s) in a row); see <pushHome>/push.log` (the reference's
 *      exact wording), append `PAGE: <msg>` to push-tick.out, and run
 *      host.pageCommand with msg as its argument (spawn it: the reference's
 *      notify shape -- a failure is logged, never fatal). host.pageCommand
 *      ABSENT -> the PAGE line is written and nothing runs, no error.
 *   7. The rc is the push's rc (0 or 1) passed through (push-tick.sh L35):
 *      the systemd oneshot sees the failure and the page fires.
 *
 * CHANGED (the lock; the b2 incident): the reference's `flock -o` (flock(2))
 * and this port's lockf(POSIX fcntl) are INVISIBLE TO EACH OTHER on Linux
 * (measured by the supervisor 2026-11-06, SV:1440 point 2) -- the two lock
 * types never contend. This port therefore serialises a tick with ITSELF
 * (another `push tick`), NOT with a still-running reference push.sh. The
 * switch-over runs the port against its OWN pushHome (PLAN section 6).
 *
 * CHANGED (the lock's CONSTANT; measured this run 2026-11-06): the brief
 * named `lockf(fd, F_TLOCK, 0)` as the blocking wait. On this glibc/Linux
 * platform that is NOT so: glibc's <fcntl.h> documents F_LOCK as "Lock a
 * region for exclusive use" (the blocking request, F_SETLKW) and F_TLOCK as
 * "Test and lock a region for exclusive use" (the NON-blocking try,
 * F_SETLK). Measured against a concurrent lockf holder: F_TLOCK returned
 * errno=11 (EAGAIN) in 0 s (a cap / non-blocking poll), F_LOCK waited the
 * holder's full 3 s. The brief's "F_TLOCK BLOCKS ... no LOCK_NB" states the
 * WANT (a blocking WAIT, not a cap) with the WRONG constant -- the same
 * constant-naming-slip class as the b2 incident (there LOCK_EX/LOCK_NB,
 * flock(2) values, not lockf's). This port therefore uses F_LOCK -- the one
 * lockf command that waits -- and there is no F_TLOCK, no LOCK_NB, no
 * timeout, no polling loop. (F_RLOCK, the BSD name for the blocking request,
 * is not defined under this tree's _POSIX_C_SOURCE 200112L + _XOPEN_SOURCE
 * 600, so it is not portable to this build.)
 *
 * RC MAPPING (documented): the tick's own preconditions (a config the loader
 * refuses, no host block, a pushHome missing push.git / recs-push.git) are
 * the tick's and refuse with 2 BEFORE the push (the WIRE/usage class). A
 * SUBCOMMAND that returns 2 (its own usage refusal -- e.g. the mirror push
 * naming a precheck, or the records push refusing) means the tick is
 * misconfigured: the tick logs it and makes the tick's rc 1, never 2 -- the
 * timer service must see a FAILURE (exit 1) and the page must fire, not a
 * silent usage code. The OR above normalises any non-zero subcommand rc to 1.
 *
 * This body does not scan itself: it CALLS the two other subcommand entries
 * (the mirror push and the records push), which carry the key/size scans and
 * the credential machinery. It codes against the shared helpers exported
 * from jc_harness_push.c for the M1211 refresh's own git children (b1/b2
 * before the team push, b3/b4 after): jc_hp_git_run for the spawns,
 * jc_hp_cred_flags / jc_hp_cred_env for the credential wiring (rebuilt per
 * child, immediately before the spawn -- jc_hp_records.c's decision 2);
 * the page command is spawned via the platform's jc_proc capture.
 */

/* lockf(3) and its command codes (F_LOCK / F_TLOCK / F_ULOCK) are XSI, not
 * POSIX.2001: under this tree's -D_POSIX_C_SOURCE=200112L glibc hides them.
 * _XOPEN_SOURCE 600 exposes them in every build mode (the jc_harness_run.c /
 * jc_path.c precedent), before any system header is pulled in. The port's
 * lock is EXACTLY one blocking call, `lockf(fd, F_LOCK, 0)` -- the reference's
 * `flock -o` WAIT translated to POSIX file locking (see the header note and
 * the CHANGED-constant measurement: F_TLOCK is glibc's NON-blocking try and
 * would be a cap; F_LOCK is the one that waits). The reference's flock(2) is
 * a different lock type, invisible to this one on Linux. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "jc_harness_push.h"
#include "jc_harness_config.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_str.h"        /* struct jc_sb */
#include "jc_proc.h"       /* jc_proc_capture: the page command's spawn */
#include "jc_fcntl.h"      /* JC_O_BINARY (portability_lint 38) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>        /* va_list: tick_refuse */
#include <time.h>          /* time / localtime_r / strftime: the tick's `now` */
#include <unistd.h>        /* dup, dup2, close, open, lockf, PATH_MAX */
#include <fcntl.h>         /* O_RDWR / O_CREAT / F_LOCK (the blocking request) */
#include <sys/stat.h>      /* S_IRUSR / S_IWUSR / S_IRGRP / S_IROTH */

/* Paths hang off the config's dir and the pushHome; give the joins room, the
 * same bound the mirror push (jc_harness_push.c) and init use. */
#define TICK_PATH_CAP 4352

/* A page command line (host.pageCommand) is short; cap the arg count. */
#define TICK_PAGE_ARGS 16

static void
tick_refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: push tick: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* timegm (glibc's UTC mktime), but the build's _POSIX_C_SOURCE does not
 * expose it, so this hand-rolls it -- the SAME proleptic-Gregorian
 * days-from-civil algorithm jc_harness_drive.c's utc_from_tm uses (kept in
 * behaviour with the drive's verified `date '+%F %T %z'` equivalent). */
static time_t
tick_utc_from_tm(const struct tm *tmv)
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

/* The reference's timestamp shape: `date '+%F %T %z'` (push.sh L36,
 * push-tick.sh L15), e.g. `2026-11-05 10:23:45 +0100`. `%F %T %z` are GNU
 * strftime specifiers this C89 build rejects, so the same bytes are assembled
 * from the ISO `%Y-%m-%d %H:%M:%S` plus the LOCAL offset -- the offset is
 * utc_of(local wall) - now (reading the local wall clock as UTC lands
 * `off` SECONDS from now; local = UTC + off), formatted ` +HHMM` / ` -HHMM`
 * (the drive's stamp_of idiom, verified against date(1) at UTC / -0400 /
 * +1400). A clock that cannot format is not a reason to stop a tick (the
 * reference would print an empty stamp): say "unknown", never crash. The
 * driver greps the log lines by content (`push: start`, `push: end rc=`),
 * not the stamp. */
static void
tick_ts(char *out, size_t cap)
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
        off = (long)tick_utc_from_tm(&tmc) - (long)t;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        jc_snprintf(out, cap, "unknown");
        return;
    }
    mag = (off < 0) ? -off : off;
    oh = (int)(mag / 3600);
    om = (int)((mag % 3600) / 60);
    /* date(1) '+%F %T %z' prints the offset with a LEADING SPACE
     * ("2026-10-27 00:33:29 +0000"); match it. */
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

/* Append one line to `path`, in the reference's `<ts> <line>` shape, or with
 * no timestamp when `ts` is NULL (the TICK/PAGE/RECOVERED lines already
 * carry the tick's own `now`). Returns 0 on a write that could not open. */
static int
tick_append(const char *path, const char *ts, const char *line)
{
    FILE *f;
    int ok = 1;
    f = fopen(path, "ab");
    if (f == NULL) {
        return 0;
    }
    if (ts != NULL) {
        fputs(ts, f);
        fputc(' ', f);
    }
    fputs(line, f);
    fputc('\n', f);
    if (ferror(f)) {
        ok = 0;
    }
    fclose(f);
    return ok;
}

/* Read push-tick.state as an int, the reference's case pattern: a missing,
 * empty or non-numeric file reads as 0 (push-tick.sh L19-20). */
static long
tick_read_state(const char *path)
{
    FILE *f;
    long n = 0;
    int c;
    int any = 0;
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    c = fgetc(f);
    while (c != EOF) {
        if (c >= '0' && c <= '9') {
            if (n > 1000000000L) {
                n = 1000000000L;
            } else {
                n = n * 10 + (c - '0');
            }
            any = 1;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            /* skip; the file is one line, so these are just the newline */
        } else {
            any = 0;
            break;
        }
        c = fgetc(f);
    }
    fclose(f);
    return any ? n : 0;
}

/* Write the one-line state file (`echo $n > push-tick.state`, push-tick.sh
 * L24/L27): OVERWRITE, not append -- the reference's `>`. The state file is
 * always a single line, so it must not accumulate (an appending state would
 * read back a multi-line value the case pattern rejects to 0 every tick). */
static int
tick_write_state(const char *path, long n)
{
    char buf[32];
    FILE *f;
    jc_snprintf(buf, sizeof buf, "%ld\n", n);
    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    if (fputs(buf, f) == EOF) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

/* push.log's last non-empty line, cut to 200 bytes (the reference's
 * `tail -1 push.log | cut -c1-200`, push-tick.sh L18). Reads only the tail (a
 * 256 KB bound, into the tick's arena -- the log is small in practice), so a
 * large log does not bloat the process. */
static void
tick_last_log_line(const char *path, char *out, size_t cap,
                   struct jc_arena *a)
{
    char *data = NULL;
    jc_size n = 0;
    size_t i, start, len;
    out[0] = '\0';
    if (cap == 0) {
        return;
    }
    if (jc_read_file_bounded(path, &data, &n, a, 256 * 1024, 0) != JC_OK
        || data == NULL) {
        return;
    }
    /* The last non-empty line: the final '\n' ends it; a file without a
     * trailing newline still has a final line. */
    i = n;
    while (i > 0 && (data[i - 1] == '\n' || data[i - 1] == '\r')) {
        i--;
    }
    if (i == 0) {
        return;
    }
    start = i;
    while (start > 0 && data[start - 1] != '\n') {
        start--;
    }
    len = i - start;
    if (len == 0) {
        return;
    }
    if (len > 200) {
        len = 200;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, data + start, len);
    out[len] = '\0';
}

/* Run host.pageCommand with `msg` as its single argument. The reference's
 * notify shape (push-tick.sh L32): a failure is logged, never fatal. An ABSENT
 * command is the caller's case (it runs nothing); here the command is present
 * but its first word is parsed from a space-separated `argv0 [args]` string. */
static void
tick_run_page(const char *page_command, const char *msg, const char *push_log)
{
    char line[TICK_PATH_CAP];
    char *argv[TICK_PAGE_ARGS + 1];
    struct jc_sb cap;
    int n = 0;
    int rc;

    jc_snprintf(line, sizeof line, "%s", page_command);
    argv[0] = strtok(line, " \t");
    if (argv[0] == NULL) {
        return;
    }
    n = 1;
    while (n <= TICK_PAGE_ARGS) {
        argv[n] = strtok(NULL, " \t");
        if (argv[n] == NULL) {
            break;
        }
        n++;
    }
    if (n >= TICK_PAGE_ARGS) {
        tick_refuse("pageCommand is too long to run (>%d words); not run: "
                    "the push is failing -- see %s", TICK_PAGE_ARGS - 1,
                    push_log);
        return;
    }
    argv[n] = (char *)msg;
    n++;
    argv[n] = NULL;

    jc_sb_init(&cap);
    rc = (int)jc_proc_capture(argv, NULL, NULL, &cap, 4096, 30L, NULL);
    if (rc < 0) {
        /* spawn failure (-1) or timeout (-2): logged, never fatal. */
        tick_refuse("pageCommand could not be run (rc=%d); the PAGE line was "
                    "still written -- the push is failing, see %s", rc,
                    push_log);
    } else if (rc != 0) {
        /* M1179: a page command that ran and failed was silent. On the
         * default fork backend a command that cannot be executed at all is
         * ALSO here, as the child's exit 127 (jc_spawn's _exit(127) after a
         * failed exec), so a misspelt pageCommand paged nobody and said
         * nothing. Any non-zero exit is reported, never fatal; 127 is named,
         * because it is the one a broken configuration produces. */
        tick_refuse("pageCommand exited %d%s; the PAGE line was still "
                    "written -- the push is failing, see %s", rc,
                    rc == 127 ? " (127: the command could not be started, "
                                "or it exited 127 itself)" : "",
                    push_log);
    }
    jc_sb_free(&cap);
}

/* The harness's own executable path (readlink /proc/self/exe, the M1053
 * idiom, jc_hp_records.c's self_exe): the credential.helper flag needs the
 * helper's absolute path. 1 on success, 0 on failure. */
static int
tick_self_exe(char *out, size_t cap)
{
    char buf[TICK_PATH_CAP];
    ssize_t n;
    n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0 || n >= (ssize_t)sizeof buf) {
        return 0;
    }
    buf[n] = '\0';
    if (jc_snprintf(out, cap, "%s", buf) >= (int)cap) {
        return 0;
    }
    return 1;
}

/* The b1/b3 git children: the fixed credential wiring built ONCE for the
 * tick (jc_hp_cred_flags' SET flag is arena-stable, the SV:1939 fix; the env
 * is the same identity for both children, and git -c values are byte-matched
 * against the stored copy, so the one-build is the port's discipline -- the
 * reference's push.sh exports $CRED once and reuses it). A too-long URL (>
 * 4096) or a credential build failure is refused with a name (the caller
 * returns 2). Runs through jc_hp_git_run (the five M2 -c flags, the
 * GIT_CONFIG_=/dev/null isolation, the 120 s cap); the child's combined
 * output goes to `out` (at least TICK_GIT_OUT bytes). Returns jc_hp_git_run's
 * code: the child's exit status, -1 on a build failure, -2 on timeout. */
#define TICK_GIT_OUT 4096
static int
tick_cred_fetch(struct jc_arena *a, const char *workdir,
                const char *harness_path, const char *git_token_file,
                const char *remote_url, const char *const args[],
                int n_args, char *out)
{
    char *cflags[4];
    char env0[512];
    char env1[512];
    char env2[512];
    char *penv[4];
    const char *penv_c[4];
    const char *argv[32];
    int i;
    int rc;

    if (remote_url == NULL || strlen(remote_url) > TICK_GIT_OUT) {
        tick_refuse("the remote's URL is too long to wire the credential "
                    "identity (>%d bytes)", TICK_GIT_OUT);
        return -1;
    }
    if (git_token_file == NULL) {
        tick_refuse("the config names no gitTokenFile: the refresh cannot "
                    "wire the credential identity");
        return -1;
    }
    rc = jc_hp_cred_flags(a, harness_path, cflags);
    if (rc < 3) {
        tick_refuse("the credential -c flags could not be built (the "
                    "harness path is too long)");
        return -1;
    }
    penv[0] = env0;
    penv[1] = env1;
    penv[2] = env2;
    penv[3] = NULL;
    jc_hp_cred_env(git_token_file, NULL, remote_url, penv);
    if (env0[0] == '\0' || env1[0] == '\0' || env2[0] == '\0') {
        tick_refuse("the credential env could not be built (a value is "
                    "too long for its 512-byte buffer)");
        return -1;
    }
    penv_c[0] = env0;
    penv_c[1] = env1;
    penv_c[2] = env2;
    penv_c[3] = NULL;
    if (n_args >= 32) {
        tick_refuse("the fetch child's argument list is too long (>= 32)");
        return -1;
    }
    for (i = 0; i < n_args; i++) {
        argv[i] = args[i];
    }
    argv[n_args] = NULL;
    out[0] = '\0';
    return jc_hp_git_run(a, workdir, cflags, 3, penv_c, 3, argv, n_args,
                         TICK_GIT_OUT, out, NULL);
}

/* `push tick`: one tick under the lock, with paging (B2). The dispatch
 * requires --config (config_path non-NULL); the mirror and recs dirs default
 * to the config's `<dir>/mirror.git` and `<dir>/recs` (positional / rest are
 * NULL for a bare `push tick`), but an explicit positional is honoured. */
int
jc_harness_push_tick_run(const char *config_path, const char *positional,
                         struct jc_arena *a, const char **rest, int n_rest)
{
    struct jc_harness_config cfg;
    char mirror[TICK_PATH_CAP];
    char recs[TICK_PATH_CAP];
    char ph[TICK_PATH_CAP];
    char lock_path[TICK_PATH_CAP];
    char log_path[TICK_PATH_CAP];
    char out_path[TICK_PATH_CAP];
    char state_path[TICK_PATH_CAP];
    char ts[64];
    char line[TICK_PATH_CAP + 64];
    char last[TICK_PATH_CAP];
    long fails;
    int lock_fd;
    int save_out;
    int team_rc;
    int recs_rc;
    int rc;
    int b2_failed;

    (void)rest;
    b2_failed = 0;
    (void)n_rest;

    /* --config is required (the dispatch refuses a bare `push <sub>`); a NULL
     * here is a caller bug, not a driver path. */
    if (config_path == NULL) {
        tick_refuse("needs --config <path>");
        return 2;
    }
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *m = jc_harness_config_error(&cfg);
        tick_refuse("could not read the config: %s",
                    (m != NULL) ? m : "unknown error");
        return 2;
    }
    /* Precondition (the tick's own, before the lock): the host block. */
    if (!cfg.host_dir_present || cfg.host_push_home == NULL
        || cfg.host_push_home[0] == '\0') {
        tick_refuse("no host block in the config (host.pushHome is required "
                    "for push tick)");
        return 2;
    }
    if (jc_snprintf(ph, sizeof ph, "%s", cfg.host_push_home)
        >= (int)sizeof ph) {
        tick_refuse("the host push home path is too long");
        return 2;
    }
    /* Precondition: the push home must hold the two bare repos (init B5 makes
     * them); refuse naming what is missing, before the lock. */
    if (!jc_is_dir(ph)) {
        tick_refuse("the host push home %s does not exist (run `init` with "
                    "the host block; it creates push.git and recs-push.git)",
                    ph);
        return 2;
    }
    {
        char pg[TICK_PATH_CAP];
        char rp[TICK_PATH_CAP];
        if (jc_snprintf(pg, sizeof pg, "%s/push.git", ph)
            >= (int)sizeof pg) {
            tick_refuse("the host push home path is too long");
            return 2;
        }
        if (!jc_is_dir(pg)) {
            tick_refuse("the host push home %s has no push.git (run `init` "
                        "with the host block)", ph);
            return 2;
        }
        if (jc_snprintf(rp, sizeof rp, "%s/recs-push.git", ph)
            >= (int)sizeof rp) {
            tick_refuse("the host push home path is too long");
            return 2;
        }
        if (!jc_is_dir(rp)) {
            tick_refuse("the host push home %s has no recs-push.git (run "
                        "`init` with the host block)", ph);
            return 2;
        }
    }
    /* The two push targets (the mirror positional, the recs positional). The
     * driver invokes a bare `push tick`, so both default off the config's
     * dir; an explicit positional overrides the mirror. */
    if (cfg.dir == NULL || cfg.dir[0] == '\0') {
        tick_refuse("the config has no dir (the push targets derive from it)");
        return 2;
    }
    if (positional != NULL && positional[0] != '\0') {
        if (jc_snprintf(mirror, sizeof mirror, "%s", positional)
            >= (int)sizeof mirror) {
            tick_refuse("the mirror path is too long");
            return 2;
        }
    } else if (jc_snprintf(mirror, sizeof mirror, "%s/mirror.git", cfg.dir)
               >= (int)sizeof mirror) {
        tick_refuse("the mirror path is too long");
        return 2;
    }
    if (jc_snprintf(recs, sizeof recs, "%s/recs", cfg.dir)
        >= (int)sizeof recs) {
        tick_refuse("the recs path is too long");
        return 2;
    }
    if (jc_snprintf(lock_path, sizeof lock_path, "%s/push.lock", ph)
        >= (int)sizeof lock_path) {
        tick_refuse("the lock path is too long");
        return 2;
    }
    if (jc_snprintf(log_path, sizeof log_path, "%s/push.log", ph)
        >= (int)sizeof log_path) {
        tick_refuse("the log path is too long");
        return 2;
    }
    if (jc_snprintf(out_path, sizeof out_path, "%s/push-tick.out", ph)
        >= (int)sizeof out_path) {
        tick_refuse("the tick out path is too long");
        return 2;
    }
    if (jc_snprintf(state_path, sizeof state_path, "%s/push-tick.state", ph)
        >= (int)sizeof state_path) {
        tick_refuse("the state path is too long");
        return 2;
    }

    /* 1. The lock: PUSH_LOCKED set -> skip (the reference's re-entrancy
     * guard, push.sh L41). Otherwise open push.lock O_RDWR|O_CREAT and take
     * it with ONE blocking lockf(fd, F_LOCK, 0) -- a WAIT, no timeout, no
     * LOCK_NB (see the CHANGED-constant note in the header: F_LOCK is the
     * blocking request on this platform; F_TLOCK is the non-blocking try and
     * would be a cap). The lock rides on fd; the kernel drops it when the
     * process exits, so a crash leaves none behind. */
    lock_fd = -1;
    if (getenv("PUSH_LOCKED") == NULL) {
        lock_fd = open(lock_path, O_RDWR | O_CREAT | JC_O_BINARY,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (lock_fd < 0) {
            tick_refuse("could not open the lock %s (the push home is not "
                        "writable?)", lock_path);
            return 2;
        }
        if (lockf(lock_fd, F_LOCK, 0) != 0) {
            close(lock_fd);
            tick_refuse("could not take the lock %s (F_LOCK is a WAIT, so a "
                        "failure here is an open/permission error, not a "
                        "timeout)", lock_path);
            return 2;
        }
    }

    /* 3. The log: redirect the tick's OWN stdout to push.log (append) for the
     * duration of the two in-process pushes -- their stdout IS the log. The
     * reference's push.sh appends to the log; the in-process entries print to
     * stdout, so this tee is the port of that. */
    save_out = dup(1);
    if (save_out < 0) {
        if (lock_fd >= 0) close(lock_fd);
        tick_refuse("could not save stdout (dup failed); the push was not run");
        return 2;
    }
    {
        int log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT | JC_O_BINARY,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (log_fd < 0) {
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            tick_refuse("could not open the log %s for append", log_path);
            return 2;
        }
        if (dup2(log_fd, 1) < 0) {
            close(log_fd);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            tick_refuse("could not redirect stdout to the log (dup2 failed)");
            return 2;
        }
        close(log_fd);
    }
    /* Now stdout (fd 1) is push.log. */
    tick_ts(ts, sizeof ts);
    if (!tick_append(log_path, ts, "push: start")) {
        (void)dup2(save_out, 1);
        close(save_out);
        if (lock_fd >= 0) close(lock_fd);
        return 2;
    }

    /* M1211 (b1-b2): the push.git refresh BEFORE the team push. b1 (FATAL):
     * the credential-wired fetch of the remote's `master` and the `<BR>`
     * branch family into push.git's TRACKING refs (push.sh L53-55); the
     * remote URL is the
     * mirror's `remote get-url --push origin` read DIRECTLY (CHANGED: no
     * `origin` remote in push.git). On failure: the UNtimestamped line
     * `push: the fetch of the remote's refs failed (rc=N): nothing pushed`,
     * NOTHING is pushed -- the team push, the records push, b3/b4 AND the
     * timestamped `push: end rc=` are all skipped (the reference's `exit 1`
     * at push.sh L55 leaves no end line), and the state machine still runs
     * (TICK FAIL, the page can fire) via `goto tick_state` with rc 1. b2
     * (non-fatal): the fetch of the mirror's `<BR>/main` and the
     * `<BR>/work` family into push.git's LOCAL branches, NO `+` (a
     * non-FF update is REFUSED by git: a rewritten ref is not carried over,
     * push.sh L57-58). CHANGED: the
     * mirror is read directly as a local path, not through a podman
     * upload-pack container. No credential flags (a local read). On failure:
     * the UNtimestamped line `push: the fetch from the drive's mirror failed
     * (rc=N)`, b2_failed 1 -- the team push STILL RUNS. */
    {
        char pg[TICK_PATH_CAP];
        char self_path[TICK_PATH_CAP];
        char urlout[4096];
        char fspec1[TICK_PATH_CAP + 64];
        char fspec2[TICK_PATH_CAP + 64];
        char git_out[TICK_GIT_OUT];
        char *url;
        char *remote_url;
        char *e;
        const char *b1_argv[8];
        const char *b2_argv[7];
        int b1_rc;
        int b2_rc;

        if (jc_snprintf(pg, sizeof pg, "%s/push.git", ph)
            >= (int)sizeof pg) {
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        if (!tick_self_exe(self_path, sizeof self_path)) {
            tick_refuse("could not resolve the harness's own path (readlink "
                        "/proc/self/exe failed); the refresh cannot name the "
                        "credential helper; the tick is refused");
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        /* b1: the remote's URL, the same command the team push's URL guard
         * runs (the mirror's origin is the standin in a rehearsal, the
         * GitLab URL in the real drive). It must succeed: without the URL
         * the remote fetch cannot be named (a misconfiguration, not a
         * fetch failure). */
        b1_argv[0] = (char *)"remote";
        b1_argv[1] = (char *)"get-url";
        b1_argv[2] = (char *)"--push";
        b1_argv[3] = (char *)"origin";
        url = NULL;
        if (jc_hp_git_run(a, mirror, NULL, 0, NULL, 0, b1_argv, 4,
                          (int)sizeof urlout, urlout, NULL) == 0) {
            urlout[sizeof urlout - 1] = '\0';
            remote_url = urlout;
            while (remote_url != NULL && (*remote_url == '\n' ||
                                          *remote_url == '\r')) {
                remote_url++;
            }
            if (remote_url != NULL && remote_url[0] != '\0') {
                /* Truncate at the end of line IN PLACE, keeping url on the
                 * URL's first byte (a get-url line carries a trailing \n). */
                url = remote_url;
                e = url;
                while (*e != '\0' && *e != '\n' && *e != '\r') {
                    e++;
                }
                *e = '\0';
            }
        }
        if (url == NULL) {
            tick_refuse("could not read the mirror's push URL (`git remote "
                        "get-url --push origin`); the refresh cannot run");
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        if (jc_snprintf(fspec1, sizeof fspec1,
                        "+refs/heads/master:refs/remotes/origin/master")
            >= (int)sizeof fspec1 ||
            jc_snprintf(fspec2, sizeof fspec2,
                        "+refs/heads/%s/*:refs/remotes/origin/%s/*",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof fspec2) {
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        b1_argv[0] = (char *)"fetch";
        b1_argv[1] = (char *)"-q";
        b1_argv[2] = (char *)"--no-tags";
        b1_argv[3] = url;
        b1_argv[4] = fspec1;
        b1_argv[5] = fspec2;
        b1_rc = tick_cred_fetch(a, pg, self_path, cfg.git_token_file, url,
                                b1_argv, 6, git_out);
        if (b1_rc != 0) {
            (void)dup2(save_out, 1);
            close(save_out);
            jc_snprintf(line, sizeof line,
                        "push: the fetch of the remote's refs failed "
                        "(rc=%d): nothing pushed", b1_rc);
            (void)tick_append(log_path, NULL, line);
            rc = 1;
            goto tick_state;
        }
        /* b2: the mirror's branches -> push.git's LOCAL branches, NO `+`
         * (a non-FF update is refused by git), a local path read with no
         * credential flags (CHANGED: no podman upload-pack). */
        if (jc_snprintf(fspec1, sizeof fspec1,
                        "refs/heads/%s/main:refs/heads/%s/main",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof fspec1 ||
            jc_snprintf(fspec2, sizeof fspec2,
                        "refs/heads/%s/work/*:refs/heads/%s/work/*",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof fspec2) {
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        b2_argv[0] = (char *)"fetch";
        b2_argv[1] = (char *)"-q";
        b2_argv[2] = (char *)"--no-tags";
        b2_argv[3] = mirror;
        b2_argv[4] = fspec1;
        b2_argv[5] = fspec2;
        b2_rc = jc_hp_git_run(a, pg, NULL, 0, NULL, 0, b2_argv, 6,
                              (int)sizeof git_out, git_out, NULL);
        if (b2_rc != 0) {
            git_out[sizeof git_out - 1] = '\0';
            fputs(git_out, stdout);
            fflush(stdout);
            jc_snprintf(line, sizeof line,
                        "push: the fetch from the drive's mirror failed "
                        "(rc=%d)", b2_rc);
            (void)tick_append(log_path, NULL, line);
            b2_failed = 1;
        }
    }

    /* 2. The push: the TEAM push (the mirror positional) THEN the RECORDS
     * push (the recs positional). The tick does not scan itself: it calls the
     * two entries, which carry the key/size scans and credential machinery. */
    team_rc = jc_harness_mirror_push_run(config_path, mirror, a);
    tick_ts(ts, sizeof ts);
    if (team_rc == 0) {
        tick_append(log_path, ts, "push: the team push landed");
    } else {
        jc_snprintf(line, sizeof line, "push: the team push failed (rc=%d)",
                    team_rc);
        tick_append(log_path, ts, line);
        if (team_rc == 2) {
            tick_refuse("the team push returned a usage code (2): the tick is "
                        "misconfigured -- mapped to the tick's rc 1 (the "
                        "service must see the failure and the page fires)");
        }
    }
    /* M1211 (b3-b4): the push.git refresh between the team push and the
     * records push (push.sh L70-72). b3 (LOG-ONLY): the credential-wired
     * re-fetch of the remote's branch family into push.git's tracking refs
     * (the `+`: always advance, so push.git's tracking refs say what the
     * remote holds NOW); a failure is logged (the UNtimestamped line `push:
     * the fetch of the remote's team refs after the push failed (rc=N)`) and
     * the tick's rc is UNCHANGED.
     * b4: the UNtimestamped line `push: team main is <short> (push.git's
     * copy)`, the short from `git rev-parse --short refs/heads/<BR>/main`
     * in push.git; the ref absent -> the line carries the empty string (the
     * reference's `2>/dev/null`).
     */
    {
        char pg[TICK_PATH_CAP];
        char self_path[TICK_PATH_CAP];
        char urlout[4096];
        char fspec[TICK_PATH_CAP + 64];
        char short_sha[128];
        char git_out[TICK_GIT_OUT];
        char *url;
        char *remote_url;
        char *e;
        const char *b3_argv[7];
        const char *b4_argv[5];
        int b3_rc;
        int b4_rc;

        if (jc_snprintf(pg, sizeof pg, "%s/push.git", ph)
            >= (int)sizeof pg) {
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        if (!tick_self_exe(self_path, sizeof self_path)) {
            tick_refuse("could not resolve the harness's own path (readlink "
                        "/proc/self/exe failed); the refresh cannot name the "
                        "credential helper; the tick is refused");
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        b3_argv[0] = (char *)"remote";
        b3_argv[1] = (char *)"get-url";
        b3_argv[2] = (char *)"--push";
        b3_argv[3] = (char *)"origin";
        url = NULL;
        if (jc_hp_git_run(a, mirror, NULL, 0, NULL, 0, b3_argv, 4,
                          (int)sizeof urlout, urlout, NULL) == 0) {
            urlout[sizeof urlout - 1] = '\0';
            remote_url = urlout;
            while (remote_url != NULL && (*remote_url == '\n' ||
                                          *remote_url == '\r')) {
                remote_url++;
            }
            if (remote_url != NULL && remote_url[0] != '\0') {
                /* Truncate at the end of line IN PLACE (a get-url line
                 * carries a trailing \n); url stays on the URL's first
                 * byte. */
                url = remote_url;
                e = url;
                while (*e != '\0' && *e != '\n' && *e != '\r') {
                    e++;
                }
                *e = '\0';
            }
        }
        if (url == NULL) {
            tick_refuse("could not read the mirror's push URL (`git remote "
                        "get-url --push origin`); the refresh cannot run");
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        if (jc_snprintf(fspec, sizeof fspec,
                        "+refs/heads/%s/*:refs/remotes/origin/%s/*",
                        cfg.drive_branch_prefix, cfg.drive_branch_prefix)
            >= (int)sizeof fspec) {
            (void)dup2(save_out, 1);
            close(save_out);
            if (lock_fd >= 0) close(lock_fd);
            return 2;
        }
        b3_argv[0] = (char *)"fetch";
        b3_argv[1] = (char *)"-q";
        b3_argv[2] = (char *)"--no-tags";
        b3_argv[3] = url;
        b3_argv[4] = fspec;
        b3_rc = tick_cred_fetch(a, pg, self_path, cfg.git_token_file, url,
                                b3_argv, 5, git_out);
        if (b3_rc != 0) {
            git_out[sizeof git_out - 1] = '\0';
            fputs(git_out, stdout);
            fflush(stdout);
            jc_snprintf(line, sizeof line,
                        "push: the fetch of the remote's team refs after the "
                        "push failed (rc=%d)", b3_rc);
            (void)tick_append(log_path, NULL, line);
        }
        short_sha[0] = '\0';
        b4_argv[0] = (char *)"rev-parse";
        b4_argv[1] = (char *)"--short";
        jc_snprintf(short_sha, sizeof short_sha,
                    "refs/heads/%s/main", cfg.drive_branch_prefix);
        b4_argv[2] = short_sha;
        b4_rc = jc_hp_git_run(a, pg, NULL, 0, NULL, 0, b4_argv, 3,
                              (int)sizeof git_out, git_out, NULL);
        if (b4_rc == 0) {
            char *s;
            git_out[sizeof git_out - 1] = '\0';
            s = git_out;
            e = s;
            while (*e != '\0' && *e != '\n' && *e != '\r') {
                e++;
            }
            *e = '\0';
            if (jc_snprintf(short_sha, sizeof short_sha, "%s", s)
                >= (int)sizeof short_sha) {
                short_sha[sizeof short_sha - 1] = '\0';
            }
        } else {
            /* The ref is absent (or rev-parse failed): the line carries the
             * empty string (the reference's `2>/dev/null`). */
            short_sha[0] = '\0';
        }
        jc_snprintf(line, sizeof line, "push: team main is %s "
                    "(push.git's copy)", short_sha);
        (void)tick_append(log_path, NULL, line);
    }

    recs_rc = jc_harness_push_records_run(config_path, recs, a, NULL, 0);
    tick_ts(ts, sizeof ts);
    if (recs_rc == 0) {
        tick_append(log_path, ts, "push: the records push landed");
    } else {
        jc_snprintf(line, sizeof line, "push: the records push failed (rc=%d)",
                    recs_rc);
        tick_append(log_path, ts, line);
        if (recs_rc == 2) {
            tick_refuse("the records push returned a usage code (2): the tick "
                        "is misconfigured -- mapped to the tick's rc 1 (the "
                        "service must see the failure and the page fires)");
        }
    }


    /* rc = the OR of the three (team, records, the b2 fetch), normalised:
     * any non-zero -> 1, all zero -> 0. */
    rc = (team_rc != 0 || recs_rc != 0 || b2_failed != 0) ? 1 : 0;

    /* Restore stdout BEFORE writing the `push: end` line (and reading it
     * back), so the tick's own lines do not interleave the log. */
    (void)dup2(save_out, 1);
    close(save_out);
    tick_ts(ts, sizeof ts);
    jc_snprintf(line, sizeof line, "push: end rc=%d", rc);
    tick_append(log_path, ts, line);

    /* The b1-fail path jumps here (the reference's `exit 1` at push.sh L55
     * leaves no `push: end` line): the state machine still runs (TICK FAIL,
     * the page can fire) and the tick returns rc 1. */
tick_state:
    /* 4-6. The state machine, to push-tick.out. */
    fails = tick_read_state(state_path);
    tick_last_log_line(log_path, last, sizeof last, a);
    if (rc == 0) {
        tick_ts(ts, sizeof ts);
        jc_snprintf(line, sizeof line, "TICK ok | %s", last);
        tick_append(out_path, ts, line);
        if (fails > 0) {
            tick_ts(ts, sizeof ts);
            jc_snprintf(line, sizeof line, "RECOVERED after %ld failed "
                        "tick(s)", fails);
            tick_append(out_path, ts, line);
        }
        tick_write_state(state_path, 0);
    } else {
        fails++;
        tick_write_state(state_path, fails);
        tick_ts(ts, sizeof ts);
        jc_snprintf(line, sizeof line, "TICK FAIL rc=%d (#%ld) | %s", rc,
                    fails, last);
        tick_append(out_path, ts, line);
        if (fails == 1 || fails % 6 == 0) {
            char msg[TICK_PATH_CAP + 64];
            tick_ts(ts, sizeof ts);
            jc_snprintf(msg, sizeof msg, "jichi drive: the host push failed "
                        "(%ld tick(s) in a row); see %s/push.log", fails, ph);
            jc_snprintf(line, sizeof line, "PAGE: %s", msg);
            tick_append(out_path, ts, line);
            if (cfg.host_page_command != NULL
                && cfg.host_page_command[0] != '\0') {
                tick_run_page(cfg.host_page_command, msg, log_path);
            }
            /* ABSENT -> the PAGE line is written and nothing runs (H7's stop). */
        }
    }

    /* The lock rides on lock_fd; dropping it on process exit is the release.
     * Close it explicitly so a long-lived process (none here, but the discipline)
     * does not hold it past the tick. */
    if (lock_fd >= 0) {
        close(lock_fd);
    }

    /* 7. The rc is the push's rc (0 or 1) passed through. */
    return rc;
}
