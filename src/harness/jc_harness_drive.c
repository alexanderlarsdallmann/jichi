/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_drive.c - `jichi-harness drive` (M1059, H5): the drive's outer
 * loop until its end, the C89 port of the drive's loop.sh (read line by line;
 * its comments name the incidents). Mode A (the plan §7.1): the harness and
 * the agent run as the same user, so the container half of the reference is
 * DROPPED (the one `podman kill --signal INT` becomes a SIGINT to the run's
 * process group, CHANGED 1) and the loop, the snapshot and the three-quick-
 * failure pause are kept, re-expressed over the harness primitives M1053/
 * M1055/M1056/M1057 built (the spawn, the in-process mirror fetch/push/head,
 * the run as a child).
 *
 * Design note (the contract -- the sixteen numbered behaviours, the ten
 * CHANGED lines, the nine driver cells D1-D8 plus D6d/D6e):
 * docs/internal/drive/2026-10-23-harness/notes/M1059.md. Reference:
 * docs/internal/drive/2026-10-23-harness/harness/loop.sh (155 lines).
 *
 * THE ORDER IS THE BEHAVIOUR (behaviour 3, the loop's hunk-audit invariant):
 * refusals -> deadline parse (exit 1, BEFORE the start line) -> start line +
 * the counter read ONCE -> the while: STOP check at the TOP -> deadline check
 * (BOTH before the increment) -> mirror fetch -> `run N: start` + t0 -> the
 * run as a child in its OWN process group -> the 10-s poll (the tick's
 * save_team + partial snapshot, the per-poll deadline SIGINT once) -> the
 * wait (the rc is the child's wait status, never masked) -> `run N: end rc=
 * after s` -> save_team -> `run N: team head <sha>` (the MIRROR's main,
 * CHANGED 6: the run's own team_head_after= is NOT re-appended) -> the
 * `final` snapshot -> the quick-failure count + the sliced pause -> `--once`
 * break / the end-of-iteration sleep 10.
 *
 * THE SECRETS NEVER MOVE HERE (behaviour 16): the model key and the git token
 * reach their consumers only through the run child's OWN existing delivery
 * (the run reads modelKeyFile at spawn time, passes it in the spawn env) and
 * the push's existing credential-helper (git reads gitTokenFile at push
 * time). This loop puts neither on a command line, in harness.log, in a
 * snapshot, nor in a child's environment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>   /* pid_t (portability_lint 29, M723) */
#include <sys/wait.h>
#include <sys/time.h>    /* struct timeval (portability_lint 28, M723) */
#include <signal.h>      /* the deadline SIGINT to the run's group (CHANGED 1) */
#include "jc_select.h"   /* select() through the portability shim, never
                          * <sys/select.h> directly (M834, posix_utils 6a) */
#include "jc_fcntl.h"    /* JC_O_BINARY (portability_lint 38) */

#include "jc_platform.h"   /* jc_status, jc_mkdir_p, jc_read_file, ... */
#include "jc_mem.h"        /* struct jc_arena */
#include "jc_str.h"        /* struct jc_sb */
#include "jc_vec.h"        /* struct jc_vec (the git child's env slot) */
#include "jc_snprintf.h"   /* jc_snprintf, never sprintf */
#include "jc_proc.h"       /* jc_proc_capture, jc_proc_waitpid */
#include "jc_json.h"       /* the drive's tick + git identity re-read */
#include "jc_spawn.h"      /* jc_spawn, jc_spawn_req, JC_SPAWN_DEVNULL */
#include "cJSON.h"
#include "jc_harness_config.h"
#include "jc_harness_mirror.h"  /* save_team's fetch (in-process) */
#include "jc_harness_push.h"    /* save_team's push (in-process) */
#include "jc_harness_drive.h"

/* Paths hang off the absolute dir; give the joins room (the run's and the
 * mirror's bound). */
#define PATH_CAP 4352

/* The reference's threshold (loop.sh: `wc -c` over 1 000 000 bytes ->
 * gzip, the plain twin removed; otherwise cp -p, the gzipped twin removed). */
#define SNAP_GZ_THRESHOLD 1000000L

/* The git children's output is small (one rev-parse / a commit line). */
#define GIT_OUT_CAP (256 * 1024)

/* The 600 s pause, SLEPT IN 10-s SLICES (CHANGED 8: the reference's solid
 * sleep cannot see a deadline or STOP arrive during it; the slice is its own
 * 10-s poll granularity). */
#define PAUSE_TOTAL_S 600
#define PAUSE_SLICE_S 10

/* The tick's poll granularity and the end-of-iteration spacing (the
 * reference's shared `sleep 10`). */
#define POLL_S 10

/* The quick-failure arithmetic (behaviour 12, kept EXACTLY): a run that
 * exits non-zero in under 120 s (a pre-run refusal is the fastest failure)
 * counts; three in a row pause the loop. */
#define QUICK_LIMIT_S 120
#define QUICK_COUNT 3

/* A named refusal on stderr (the exit-2 usage class, D8). */
static void
refuse(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: drive: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

static time_t
utc_from_tm(const struct tm *tmv);   /* defined below: the hand-rolled timegm */

/* A given epoch as "YYYY-MM-DD HH:MM:SS +HHMM" (behaviour 14's log prefix,
 * the `date '+%F %T %z'` equivalent without a date child): strftime for the
 * wall clock, then the LOCAL offset = utc_of(wall) - t, where utc_of is the
 * file's own hand-rolled timegm, utc_from_tm (glibc's timegm is not exposed
 * under the build's global _POSIX_C_SOURCE): local = UTC + off, so reading
 * the local wall clock as UTC lands off SECONDS from t. Verified against
 * date(1) at UTC, -0400 (America/New_York) and +1400 (Pacific/Kiritimati).
 * A negative offset (west of UTC) formats "-HHMM". */
static void
stamp_of(char *out, size_t cap, time_t t)
{
    struct tm tmv;
    struct tm tmc;
    long off;
    long mag;
    int oh, om;
    size_t n;

    if (localtime_r(&t, &tmv) == NULL) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
        tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
        off = 0;
    } else {
        tmc = tmv;
        off = (long)utc_from_tm(&tmc) - (long)t;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        out[0] = '\0';
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

static void
now_stamp(char *out, size_t cap)
{
    stamp_of(out, cap, time(NULL));
}

/* Append `<stamp> <line>\n` to the harness log (behaviour 14: every line
 * APPENDED, the reference's `>>`). A log that cannot be opened is dropped,
 * never fatal to the loop's exit class. */
static void
log_line(const char *log_path, const char *line)
{
    char stamped[512];
    char stamp[40];   /* the offset's +HHMM / -HHMM needs room past the 19-dt */
    FILE *f;

    if (log_path == NULL || line == NULL) {
        return;
    }
    now_stamp(stamp, sizeof stamp);
    if (jc_snprintf(stamped, sizeof stamped, "%s %s", stamp, line)
        >= (int)sizeof stamped) {
        return;   /* a line that cannot be stamped cannot be logged */
    }
    f = fopen(log_path, "ab");
    if (f == NULL) {
        return;
    }
    fputs(stamped, f);
    fputc('\n', f);
    fclose(f);
}

/* M1064 (card 93, attempt 2, the run-0085 root-cause correction): run the
 * in-process mirror fetch in a FORK CHILD whose fd 1 is the write end of a
 * pipe; the parent drains the pipe (select, 200 ms) and reaps the child
 * (waitpid WNOHANG) CONCURRENTLY, log_line()'s each complete line STAMPED,
 * and returns the fetch's rc. The loop's OWN fd 1 and fd 2 are NEVER dup2'd
 * here (attempt 1's dup2-the-loop-stdout poisoned the snapshot's gzip child's
 * execvp -- EFAULT, D6 class 2). The child owns its fresh arena (the config
 * the in-process function loads is arena-owned); it dies with it. */
#define FETCH_LINE 512    /* log_line's stamped buffer is 512; one line room */
#define FETCH_DEADLINE_MS 120000.0 /* this file's git-capture timeout, 120 s */

static int
fetch_logged_to_harness_log(const char *config_path, const char *mirror,
                            const char *log_path)
{
    int pd[2];
    pid_t child;
    int status = 0;
    int rc;
    int eof;
    int reaped;
    char line[FETCH_LINE];
    size_t line_len;
    int dropping;   /* 1 while dropping an over-FETCH_LINE physical line */
    double deadline_ms;

    /* log_path NULL in theory only (the loop always has one): fetch
     * directly, no replay. */
    if (log_path == NULL || log_path[0] == '\0') {
        struct jc_arena *ra = jc_arena_new(0);
        if (ra == NULL) {
            return 1;
        }
        rc = jc_harness_mirror_fetch_run(config_path, mirror, ra);
        jc_arena_free(ra);
        return rc;
    }
    /* M1064-c: jc_pipe_cloexec, never a bare pipe() (posix_utils_lint 8):
     * both ends get FD_CLOEXEC, so the fetch child and its git
     * grandchildren inherit NONE of the loop's other open fds (sockets,
     * the log file, the harness's own). dup2() does not copy the flag,
     * so the child installing pd[1] as its fd 1 below still works. */
    if (jc_pipe_cloexec(pd) != 0) {
        /* Tool errors are values: the fetch STILL runs (its report goes to
         * the loop's stdout), a stamped failure line is logged. */
        log_line(log_path, "mirror fetch capture failed: "
                 "the fetch pipe could not be made (the fetch ran anyway)");
        {
            struct jc_arena *ra = jc_arena_new(0);
            rc = 1;
            if (ra != NULL) {
                rc = jc_harness_mirror_fetch_run(config_path, mirror, ra);
                jc_arena_free(ra);
            }
            if (rc != 0) {
                char ln[FETCH_LINE];
                if (jc_snprintf(ln, sizeof ln,
                                "mirror fetch failed (rc=%d)", rc)
                    < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
            }
            return rc;
        }
    }
    child = fork();
    if (child < 0) {
        close(pd[0]);
        close(pd[1]);
        log_line(log_path, "mirror fetch capture failed: "
                 "the fetch child could not be forked (the fetch ran anyway)");
        {
            struct jc_arena *ra = jc_arena_new(0);
            rc = 1;
            if (ra != NULL) {
                rc = jc_harness_mirror_fetch_run(config_path, mirror, ra);
                jc_arena_free(ra);
            }
            if (rc != 0) {
                char ln[FETCH_LINE];
                if (jc_snprintf(ln, sizeof ln,
                                "mirror fetch failed (rc=%d)", rc)
                    < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
            }
            return rc;
        }
    }
    if (child == 0) {
        /* CHILD ONLY: fd 1 -> the pipe's write end, stdin/stderr -> devnull.
         * The child runs nothing else and never restores fd 1. */
        struct jc_arena *ra;
        int dn;
        int frc;
        close(pd[0]);
        if (dup2(pd[1], 1) < 0) {
            _exit(1);
        }
        close(pd[1]);
        dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) {
            (void)dup2(dn, 0);
            (void)dup2(dn, 2);
            if (dn > 2) {
                close(dn);
            }
        }
        ra = jc_arena_new(0);
        if (ra == NULL) {
            _exit(1);
        }
        frc = jc_harness_mirror_fetch_run(config_path, mirror, ra);
        _exit(frc);
    }
    /* PARENT: close the write end BEFORE the drain loop (a leaked write end
     * makes the read-to-EOF block forever -- the classic fork+pipe
     * deadlock). Drain and reap CONCURRENTLY (jc_proc_capture's discipline,
     * kept for safety in case a future fetch writer inherits this pipe
     * directly -- today's only direct writer is say(), <1KB; the fetch's git
     * grandchildren are captured through jc_proc_capture's own pipes). */
    close(pd[1]);
    deadline_ms = jc_now_millis() + FETCH_DEADLINE_MS;
    line[0] = '\0';
    line_len = 0;
    rc = 1;
    eof = 0;      /* PERSISTENT: set once on read()==0, never reset */
    reaped = 0;   /* PERSISTENT: set once on waitpid()==child, never reset */
    dropping = 0; /* set once on an over-FETCH_LINE line; reset at its \n */
    /* Drain AND reap CONCURRENTLY (jc_proc_capture's discipline, kept for
     * safety in case a future fetch writer inherits this pipe directly --
     * today's only direct writer is say(), <1KB; the fetch's git
     * grandchildren are captured through jc_proc_capture's own pipes). The
     * exit condition is eof AND reaped; both are latched, so they need not
     * be true in the SAME iteration (a read()==0 that lands one iteration
     * before the child's exit would otherwise spin the loop forever). */
    while (!eof || !reaped) {
        fd_set rfds;
        struct timeval tv;
        int sret;
        int n;
        int i;
        int k;
        int wrc;

        if (jc_now_millis() > deadline_ms) {
            /* The 120 s deadline (this file's git-capture timeout): kill the
             * child (its git grandchildren die with it), reap, and continue
             * to the failure line; rc is the kill class. */
            if (kill(child, SIGKILL) != 0) {
                /* ECHILD: the child already died; the status is unknown. */
            }
            wrc = (int)waitpid(child, &status, 0);
            if (wrc == child) {
                if (WIFSIGNALED(status)) {
                    rc = 128 + WTERMSIG(status);
                } else if (WIFEXITED(status)) {
                    rc = WEXITSTATUS(status);
                } else {
                    rc = 1;
                }
            } else {
                rc = 1;
            }
            break;
        }
        if (!eof) {
            FD_ZERO(&rfds);
            FD_SET(pd[0], &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            sret = select(pd[0] + 1, &rfds, NULL, NULL, &tv);
            if (sret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                rc = 1;   /* a select error: stop reading, still reap below */
                eof = 1;
            } else if (sret > 0) {
                if (FD_ISSET(pd[0], &rfds)) {
                    n = (int)read(pd[0], line + line_len,
                                  sizeof line - 1 - line_len);
                    if (n > 0) {
                        if (dropping) {
                            /* Dropping an over-FETCH_LINE physical line:
                             * discard its bytes until the newline; the bytes
                             * after that newline (if any) are the start of
                             * the next, normal line -- recover them as the
                             * tail. k == n: no newline in this chunk; keep
                             * dropping (line_len stays 0). */
                            for (k = 0; k < n; k++) {
                                if (line[k] == '\n') {
                                    break;
                                }
                            }
                            if (k < n) {
                                dropping = 0;
                                line_len = (size_t)(n - k - 1);
                                memmove(line, line + k + 1, line_len);
                            }
                        } else {
                            line_len += (size_t)n;
                        }
                        if (line_len > 0) {
                            line[line_len] = '\0';
                        }
                        for (i = 0; i < (int)line_len; i++) {
                            if (line[i] == '\n') {
                                line[i] = '\0';
                                if (i > 0) {
                                    log_line(log_path, line);
                                }
                                if (i + 1 < (int)line_len) {
                                    memmove(line, line + i + 1,
                                            line_len - i - 1);
                                    line_len -= (size_t)(i + 1);
                                    i = -1;
                                } else {
                                    line_len = 0;
                                    break;
                                }
                            }
                        }
                        /* Bounded-line guard: a single physical line that
                         * outgrew the buffer (no newline, buffer full) is
                         * truncated -- log the marker, drop the rest of that
                         * line (keep draining until its newline). Bounded
                         * and observable; never an unbounded buffer, never
                         * a false EOF from read(count 0). */
                        if (line_len >= (size_t)(sizeof line - 1)) {
                            dropping = 1;
                            line_len = 0;
                            log_line(log_path,
                                     "mirror fetch line truncated (over "
                                     "FETCH_LINE bytes)");
                        }
                    } else if (n == 0) {
                        eof = 1;   /* read() == 0: every writer has closed */
                    } else if (errno != EINTR && errno != EAGAIN) {
                        rc = 1;
                        eof = 1;
                    }
                }
            }
            if (eof) {
                close(pd[0]);
                pd[0] = -1;
            }
        }
        if (!reaped) {
            /* Reap EVERY iteration, concurrently with the drain. */
            wrc = (int)waitpid(child, &status, WNOHANG);
            if (wrc == child) {
                reaped = 1;
            } else if (wrc < 0 && errno != EINTR) {
                reaped = 1;   /* ECHILD etc.: it is gone */
            }
        }
        if (reaped) {
            if (WIFEXITED(status)) {
                rc = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                rc = 128 + WTERMSIG(status);
            } else {
                rc = 1;
            }
        }
        /* sret == 0 (the 200 ms select timeout) loops to the deadline
         * check; the latch guarantees forward progress once both are done. */
    }
    if (pd[0] >= 0) {
        close(pd[0]);
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        log_line(log_path, line);   /* a partial line the child left behind */
    }
    if (rc != 0) {
        char ln[FETCH_LINE];
        if (jc_snprintf(ln, sizeof ln, "mirror fetch failed (rc=%d)", rc)
            < (int)sizeof ln) {
            log_line(log_path, ln);
        }
    }
    return rc;
}

/* Two empty files the harness owns, in the drive root, for
 * GIT_CONFIG_GLOBAL / GIT_CONFIG_SYSTEM (CHANGED 10's M2 surface for the
 * recs children, the run's/mirror's make_empty_env_files idiom). The child
 * reads an empty global and an empty system, so a plant in the agent's
 * ~/.gitconfig is not seen by the recs git children. */
static int
make_empty_git_files(const char *dir, char *g_out, size_t g_cap,
                     char *s_out, size_t s_cap)
{
    char gpath[PATH_CAP];
    char spath[PATH_CAP];
    const char zero[] = "";
    if (jc_snprintf(gpath, sizeof gpath, "%s/.harness-empty-global", dir)
        >= (int)sizeof gpath) {
        return 0;
    }
    if (jc_snprintf(spath, sizeof spath, "%s/.harness-empty-system", dir)
        >= (int)sizeof spath) {
        return 0;
    }
    if (jc_write_file(gpath, zero, 0) != JC_OK) {
        return 0;
    }
    if (jc_write_file(spath, zero, 0) != JC_OK) {
        return 0;
    }
    if (jc_snprintf(g_out, g_cap, "GIT_CONFIG_GLOBAL=%s", gpath)
        >= (int)g_cap) {
        return 0;
    }
    if (jc_snprintf(s_out, s_cap, "GIT_CONFIG_SYSTEM=%s", spath)
        >= (int)s_cap) {
        return 0;
    }
    return 1;
}

/* behaviour 5 (CHANGED 4): the deadline, parsed ONCE at start. The two
 * forms the drive's documented config uses, and NOTHING else (any other
 * form is the reference's own refusal -- exit 1, the line named -- not a
 * guess): `YYYY-MM-DD HH:MM:SS +ZZZZ` (drive.env's shape, +0100) and a bare
 * `YYYY-MM-DD HH:MM:SS` (= UTC). Hand-rolled, no locale, no date child:
 * timegm over the broken-down fields, minus the explicit offset (hours and
 * minutes of +ZZZZ). Returns 1 when *end_out is set. */
/* timegm (glibc's UTC mktime), but the build's _POSIX_C_SOURCE does not
 * expose it, so this hand-rolls it the way the note names (CHANGED 4: no
 * locale, no date child). The day count is the standard proleptic-Gregorian
 * days-from-civil algorithm (era / year-of-era / day-of-year); it is checked
 * against 1970-01-01 = 0 and 2000-01-01 = 10957 (the century leap). */
static time_t
utc_from_tm(const struct tm *tmv)
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

static int
parse_deadline(const char *s, time_t *end_out)
{
    int Y, M, D, h, m, sec;
    int off = 0;
    time_t utc;
    struct tm tmv;
    int matched;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    matched = sscanf(s, " %d-%d-%d %d:%d:%d %d", &Y, &M, &D, &h, &m, &sec,
                     &off);
    if (matched != 7) {
        /* Retry without the offset: a bare YYYY-MM-DD HH:MM:SS (= UTC). A
         * string that matched 7 above is kept (the offset form); a string
         * that matched 6 here is the UTC form. Anything else refuses. */
        Y = M = D = h = m = sec = off = 0;
        matched = sscanf(s, " %d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec);
        if (matched != 6) {
            return 0;   /* CHANGED 4: any other form is the refusal */
        }
        off = 0;
    }
    if (Y < 1970 || Y > 2999 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59 ||
        off < -9900 || off > 9900) {
        return 0;
    }
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = Y - 1900;
    tmv.tm_mon = M - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = h;
    tmv.tm_min = m;
    tmv.tm_sec = sec;
    utc = utc_from_tm(&tmv);
    if (utc < 0) {
        return 0;
    }
    /* off is +ZZZZ: sign * 100 for the hour part, the rest minutes. The
     * +0100 in drive.env is a +1:00 offset (UTC+1), so +0100 -> subtract
     * 3600 s. */
    {
        int sign = (off >= 0) ? 1 : -1;
        int mag = (off >= 0) ? off : -off;
        int oh = mag / 100;
        int om = mag % 100;
        if (om > 59) {
            return 0;
        }
        *end_out = utc - (time_t)sign * ((time_t)oh * 3600 + (time_t)om * 60);
    }
    return 1;
}

/* The deadline's value, extracted from the RAW config text, before the
 * strict JSON load (loop.sh 31's order: the reference sources drive.env and
 * date-parses ZD_END at line 31, BEFORE it sources mirror.sh and runs any
 * precheck -- the end is a setting of the drive, not a field the loader
 * validates, and a broken end is the reference's own exit-1, independent of
 * whether the rest of the config parses). The scan finds the "drive" object
 * and the first "end" key after it, then the quoted string that follows
 * (the value is copied NUL-terminated; end values carry no escapes, and a
 * string that cannot be found is the missing-end shape, the same exit-1
 * the caller takes for an unparseable one). Returns NULL when there is no
 * drive.end. */
static char *
extract_drive_end(const char *text, char *buf, size_t cap)
{
    const char *drv;
    const char *p;
    const char *q;
    jc_size len;

    if (text == NULL || cap == 0) {
        return NULL;
    }
    drv = strstr(text, "\"drive\"");
    if (drv == NULL) {
        return NULL;
    }
    for (p = drv; (p = strstr(p, "\"end\"")) != NULL; p++) {
        q = p + 5;   /* past "end" */
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
            q++;
        }
        if (*q != ':') {
            continue;
        }
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
            q++;
        }
        if (*q != '"') {
            continue;   /* an "end" that is not a string value */
        }
        q++;
        len = 0;
        while (q[len] != '\0' && q[len] != '"' && len + 1 < cap) {
            buf[len] = q[len];
            len++;
        }
        buf[len] = '\0';
        return buf;
    }
    return NULL;
}

/* behaviour 2: the run counter, read from disk ONCE. A name counts only
 * when EVERY byte is a digit (0008 -> 8), parsed in BASE 10 (the reference's
 * incident: a leading zero is octal in shell arithmetic, where 0008 is an
 * error). No runs/ and no numeric entry: 0. */
static int
read_run_counter(const char *dir)
{
    char runs_dir[PATH_CAP];
    DIR *d;
    struct dirent *e;
    int n = 0;

    if (jc_snprintf(runs_dir, sizeof runs_dir, "%s/runs", dir)
        >= (int)sizeof runs_dir) {
        return 0;
    }
    d = opendir(runs_dir);
    if (d == NULL) {
        return 0;   /* no runs/: 0 (the reference's `2>/dev/null` no-op) */
    }
    while ((e = readdir(d)) != NULL) {
        const char *p = e->d_name;
        int all_digits = 1;
        int value;
        int i;
        if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0 || p[0] == '\0') {
            continue;
        }
        for (i = 0; p[i] != '\0'; i++) {
            if (p[i] < '0' || p[i] > '9') {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits) {
            continue;
        }
        value = atoi(p);   /* p is all digits: base 10, no octal */
        if (value > n) {
            n = value;
        }
    }
    closedir(d);
    return n;
}

/* The drive's settings the shared loader does not carry for this command:
 * tick (lenient, the reference's `ZD_TICK`, default 1200) and the records
 * commit's EXPLICIT identity (drive.gitName / drive.gitEmail; the unset
 * ones stay NULL -- a commit without them fails with git's own message,
 * behaviour 11's SV:1838.5a). Re-reads the config file the loader already
 * validated (the run's read_run_extras idiom). */
static void
read_drive_extras(const char *config_path, int *tick,
                  char **git_name, char **git_email, struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root;
    const cJSON *drive;
    double tv;

    *tick = 1200;
    *git_name = NULL;
    *git_email = NULL;
    if (config_path == NULL ||
        jc_read_file(config_path, &text, &len, a) != JC_OK) {
        return;   /* the loader just said this is valid JSON */
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        return;
    }
    drive = cJSON_GetObjectItem(root, "drive");
    if (cJSON_IsObject(drive)) {
        const cJSON *v;
        tv = jc_json_get_num_lenient(drive, "tick", 1200.0);
        if (tv > 0 && tv < 2147483647.0) {
            *tick = (int)tv;
        }
        v = cJSON_GetObjectItem(drive, "gitName");
        if (cJSON_IsString(v) && v->valuestring != NULL &&
            v->valuestring[0] != '\0') {
            *git_name = jc_arena_strdup(a, v->valuestring);
        }
        v = cJSON_GetObjectItem(drive, "gitEmail");
        if (cJSON_IsString(v) && v->valuestring != NULL &&
            v->valuestring[0] != '\0') {
            *git_email = jc_arena_strdup(a, v->valuestring);
        }
    }
    cJSON_Delete(root);
}

/* Copy one REGULAR file (the caller lstat-verified), cp -p shape: the bytes
 * plus the mode and the mtime/atime. Returns 1 on success. */
static int
copy_file_p(const char *src, const char *dst)
{
    struct stat st;
    FILE *in;
    FILE *out;
    char chunk[65536];
    size_t r;
    struct timeval tv[2];

    if (lstat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    in = fopen(src, "rb");
    if (in == NULL) {
        return 0;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((r = fread(chunk, 1, sizeof chunk, in)) > 0) {
        if (fwrite(chunk, 1, r, out) != r) {
            fclose(in);
            fclose(out);
            (void)unlink(dst);
            return 0;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        (void)unlink(dst);
        return 0;
    }
    (void)chmod(dst, (mode_t)(st.st_mode & 07777));
    tv[0].tv_sec = st.st_atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = st.st_mtime;
    tv[1].tv_usec = 0;
    (void)utimes(dst, tv);
    return 1;
}

/* Gzip `src` to `dst` (the reference's `gzip -c src > dst`), the mode and
 * the atime/mtime preserved afterwards. The child's stdout is the opened dst
 * fd, so the compressed bytes go straight to the file (no capture, no
 * copy-through). Returns 1 on success; a spawn failure or a non-zero child
 * removes dst (the stale-twin removal is the caller's, the XOR). */
static int
gzip_file_p(const char *src, const char *dst, mode_t mode)
{
    char *argv[4];
    char src_arg[PATH_CAP];
    struct jc_spawn_req req;
    int dfd;
    int srfd;
    pid_t pid = -1;
    int status = 0;
    struct stat st;
    struct timeval tv[2];
    int rc;

    if (jc_snprintf(src_arg, sizeof src_arg, "%s", src)
        >= (int)sizeof src_arg) {
        return 0;
    }
    dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC | JC_O_BINARY,
               (mode != 0) ? mode : 0644);
    if (dfd < 0) {
        return 0;
    }
    srfd = open(src, O_RDONLY | JC_O_BINARY);
    if (srfd < 0) {
        close(dfd);
        (void)unlink(dst);
        return 0;
    }
    memset(&req, 0, sizeof req);
    argv[0] = (char *)"gzip";
    argv[1] = (char *)"-c";
    argv[2] = src_arg;
    req.argv = argv;
    req.fd_stdin = srfd;            /* the child reads its own source */
    req.fd_stdout = dfd;            /* the compressed bytes go to the file */
    req.fd_stderr = JC_SPAWN_DEVNULL;
    rc = (jc_spawn(&req, &pid) == JC_OK) ? 0 : -1;
    close(dfd);
    close(srfd);
    if (rc != 0) {
        (void)unlink(dst);
        return 0;
    }
    /* The child's stdout went straight to the file; no drain, no copy. */
    if (jc_proc_waitpid(pid, &status) != 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        (void)unlink(dst);
        return 0;
    }
    /* Restore the source's stamp (the -p shape, the record's identity). */
    if (lstat(src, &st) != 0) {
        return 1;
    }
    (void)chmod(dst, (mode_t)(st.st_mode & 07777));
    tv[0].tv_sec = st.st_atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = st.st_mtime;
    tv[1].tv_usec = 0;
    (void)utimes(dst, tv);
    return 1;
}

/* The recs git child (CHANGED 10): the M2 set (core.fsmonitor= / log.
 * showSignature=false / commit.gpgsign=false / core.hooksPath=/dev/null /
 * core.alternateRefsCommand=), `-C <recs>`, and the empty GIT_CONFIG_
 * GLOBAL/SYSTEM the caller built. `extra`/`nextra` carry the per-call flags
 * (the commit's identity). Returns jc_proc_capture's code (0 on a clean
 * `diff --cached --quiet`, the child's exit otherwise, -1 spawn, -2 timeout). */
static int
git_recs(struct jc_sb *out, const char *recs, const char *e_global,
         const char *e_system, char *const extra[], int nextra,
         char *const tail[], int ntail)
{
    char *argv[24];
    char *env[3];
    struct jc_vec envv;
    int k = 0;
    int i;

    if (nextra + ntail > 8) {
        return -1;
    }
    argv[k++] = (char *)"git";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.fsmonitor=";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"log.showSignature=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"commit.gpgsign=false";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.hooksPath=/dev/null";
    argv[k++] = (char *)"-c"; argv[k++] = (char *)"core.alternateRefsCommand=";
    argv[k++] = (char *)"-C";
    argv[k++] = (char *)recs;
    for (i = 0; i < nextra; i++) {
        argv[k++] = (char *)extra[i];
    }
    for (i = 0; i < ntail; i++) {
        argv[k++] = (char *)tail[i];
    }
    argv[k] = NULL;
    env[0] = (char *)e_global;
    env[1] = (char *)e_system;
    env[2] = (char *)"";
    jc_vec_init(&envv, sizeof(char *));
    for (i = 0; i < 2; i++) {
        if (jc_vec_push(&envv, &env[i]) != JC_OK) {
            jc_vec_free(&envv);
            return -1;
        }
    }
    (void)env[2];
    i = (int)jc_proc_capture(argv, &envv, NULL, out, GIT_OUT_CAP, 120, NULL);
    jc_vec_free(&envv);
    return i;
}

/* The first line of a capture, NUL-trimmed, or NULL when empty (the mirror
 * head's rev-parse shape, reused for the records commit's failure line). */
static char *
first_line(struct jc_sb *out, struct jc_arena *a)
{
    const char *p;
    const char *nl;
    jc_size len;
    char *s;
    if (out->data == NULL) {
        return NULL;
    }
    p = out->data;
    nl = strchr(p, '\n');
    len = (nl != NULL) ? (jc_size)(nl - p) : strlen(p);
    if (len == 0) {
        return NULL;
    }
    s = (char *)jc_arena_calloc(a, len + 1);
    if (s == NULL) {
        return NULL;
    }
    memcpy(s, p, len);
    s[len] = '\0';
    return s;
}

/* behaviour 11: the snapshot, `src=<dir>/runs/<N> -> dst=<dir>/recs/runs/
 * <N>`, the reference's shape. Top-level REGULAR files over the threshold
 * are gzipped (the plain twin removed), under it cp -p (the gzipped twin
 * removed) -- the XOR keeps a re-snapshot from holding BOTH forms. The four
 * subdirs' files are ALWAYS gzipped (no threshold, no plain twin in dst).
 * A SYMLINK is SKIPPED BY NAME (CHANGED 7: the reference's -f/cp -p/gzip -c
 * follow links, and in Mode A a planted link to the key file would ride the
 * commit), so are FIFOs and any non-regular file. A missing/empty src (a
 * REFUSED run created nothing) copies nothing and commits nothing: opendir
 * NULL is NOT a refusal (the reference's glob no-op; the loop still logs its
 * `run N: final`).
 *
 * Then the recs git children (the M2 set + empty GIT_CONFIG, CHANGED 10):
 * `add -A runs/<N>`; when the index is not clean, a commit with the EXPLICIT
 * identity (drive.gitName/gitEmail; an identity-less config makes git's OWN
 * failure, logged by name -- the loop never guesses, never skips silently,
 * SV:1838.5a). The PUSH is the CHANGED 2 NO-OP: a named line, the commit
 * stays in dir/recs, nothing is pushed (def-360), never --force, never to
 * master. `kind` is the log's (partial|final). */
static void
snapshot(const char *dir, const char *log_path, const char *run,
         const char *kind, const char *git_name, const char *git_email,
         const char *e_global, const char *e_system, struct jc_arena *a)
{
    char src[PATH_CAP];
    char dst[PATH_CAP];
    char recs[PATH_CAP];
    char runs_arg[PATH_CAP];
    char msg[PATH_CAP];
    char id_name[512];
    char id_email[512];
    char *extra[4];
    int nextra = 0;
    DIR *d;
    struct dirent *e;
    struct jc_sb out;
    const char *subdirs[4] = { "notes", "lmstudio", "sessions", "attempts" };
    int si;
    int rc;

    if (jc_snprintf(src, sizeof src, "%s/runs/%s", dir, run)
        >= (int)sizeof src) {
        return;
    }
    if (jc_snprintf(dst, sizeof dst, "%s/recs/runs/%s", dir, run)
        >= (int)sizeof dst) {
        return;
    }
    if (jc_snprintf(recs, sizeof recs, "%s/recs", dir)
        >= (int)sizeof recs) {
        return;
    }
    jc_mkdir_p(dst);
    d = opendir(src);
    if (d == NULL) {
        /* no runs/<N>: a refused run created nothing (SV:1838.5a). The
         * reference's glob no-ops; the loop still logs its `run N: final`,
         * and there is nothing to add, so no commit either. */
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sp[PATH_CAP];
        char dp[PATH_CAP];
        char gz[PATH_CAP];
        struct stat st;
        const char *nm = e->d_name;
        long sz;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) {
            continue;
        }
        if (jc_snprintf(sp, sizeof sp, "%s/%s", src, nm)
            >= (int)sizeof sp) {
            continue;
        }
        /* CHANGED 7: lstat, never stat. A symlink is skipped BY NAME
         * (the driver's D6d holds it: a link to the model-key file must
         * not reach recs/ by name or by content); so are FIFOs and devices. */
        if (lstat(sp, &st) != 0) {
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            {
                char ln[512];
                if (jc_snprintf(ln, sizeof ln,
                                "snapshot %s (%s): skipped the symlink %s",
                                run, kind, nm) < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
            }
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        if (jc_snprintf(dp, sizeof dp, "%s/%s", dst, nm)
            >= (int)sizeof dp) {
            continue;
        }
        if (jc_snprintf(gz, sizeof gz, "%s/%s.gz", dst, nm)
            >= (int)sizeof gz) {
            continue;
        }
        sz = (long)st.st_size;
        if (sz > SNAP_GZ_THRESHOLD) {
            if (gzip_file_p(sp, gz, (mode_t)(st.st_mode & 07777))) {
                (void)unlink(dp);   /* the XOR: remove the plain twin */
            }
        } else {
            if (copy_file_p(sp, dp)) {
                (void)unlink(gz);   /* the XOR: remove the stale gzipped twin */
            }
        }
    }
    closedir(d);
    /* The four subdirs: every file ALWAYS gzipped, no threshold (the
     * reference's `for sub in ...; do gzip -c ... > $subd/$sub/$f.gz`). A
     * missing subdir is skipped (the loop's glob no-ops it). */
    for (si = 0; si < 4; si++) {
        char sub_src[PATH_CAP];
        char sub_dst[PATH_CAP];
        DIR *sd;
        struct dirent *se;
        if (jc_snprintf(sub_src, sizeof sub_src, "%s/%s", src, subdirs[si])
            >= (int)sizeof sub_src) {
            continue;
        }
        if (jc_snprintf(sub_dst, sizeof sub_dst, "%s/%s", dst, subdirs[si])
            >= (int)sizeof sub_dst) {
            continue;
        }
        sd = opendir(sub_src);
        if (sd == NULL) {
            continue;
        }
        jc_mkdir_p(sub_dst);
        while ((se = readdir(sd)) != NULL) {
            char sp[PATH_CAP];
            char gz[PATH_CAP];
            char ln[512];
            struct stat st;
            const char *nm = se->d_name;
            if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) {
                continue;
            }
            if (jc_snprintf(sp, sizeof sp, "%s/%s", sub_src, nm)
                >= (int)sizeof sp) {
                continue;
            }
            if (lstat(sp, &st) != 0) {
                continue;
            }
            if (S_ISLNK(st.st_mode)) {
                if (jc_snprintf(ln, sizeof ln,
                                "snapshot %s (%s): skipped the symlink %s",
                                run, kind, nm) < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
                continue;
            }
            if (!S_ISREG(st.st_mode)) {
                continue;
            }
            if (jc_snprintf(gz, sizeof gz, "%s/%s.gz", sub_dst, nm)
                >= (int)sizeof gz) {
                continue;
            }
            (void)gzip_file_p(sp, gz, (mode_t)(st.st_mode & 07777));
        }
        closedir(sd);
    }
    /* The manager's own record and the harness's log travel with every
     * snapshot (loop.sh 75-76, `for f in supervision-log.md harness.log`):
     * a missing one is skipped, not a refusal; each copied file is staged
     * by name so it rides the same commit. */
    {
        static const char *top_files[2] = { "supervision-log.md",
                                            "harness.log" };
        int tf;
        for (tf = 0; tf < 2; tf++) {
            char tp[PATH_CAP];
            char dp2[PATH_CAP];
            struct stat tstat;
            if (jc_snprintf(tp, sizeof tp, "%s/%s", dir, top_files[tf])
                >= (int)sizeof tp) {
                continue;
            }
            if (stat(tp, &tstat) != 0 || !S_ISREG(tstat.st_mode)) {
                continue;   /* missing: skipped, not a refusal */
            }
            if (jc_snprintf(dp2, sizeof dp2, "%s/recs/%s", dir,
                            top_files[tf]) >= (int)sizeof dp2) {
                continue;
            }
            if (copy_file_p(tp, dp2)) {
                char *add_tail2[3];
                jc_sb_init(&out);
                add_tail2[0] = (char *)"add";
                add_tail2[1] = (char *)"-A";
                add_tail2[2] = (char *)top_files[tf];
                (void)git_recs(&out, recs, e_global, e_system, NULL, 0,
                               add_tail2, 3);
                jc_sb_free(&out);
            }
        }
    }
    /* The recs git children (CHANGED 10). `add -A runs/<N>`; if the index is
     * not clean, commit with the EXPLICIT identity (a missing identity makes
     * git's OWN failure, logged by name -- never a guess, never a silent
     * skip, SV:1838.5a). The push is the CHANGED 2 NO-OP (def-360). */
    if (jc_snprintf(runs_arg, sizeof runs_arg, "runs/%s", run)
        >= (int)sizeof runs_arg) {
        return;
    }
    jc_sb_init(&out);
    {
        char *add_tail[3];
        add_tail[0] = (char *)"add";
        add_tail[1] = (char *)"-A";
        add_tail[2] = runs_arg;
        rc = git_recs(&out, recs, e_global, e_system, NULL, 0,
                      add_tail, 3);
    }
    jc_sb_free(&out);
    if (rc == 0) {
        /* `git diff --cached --quiet` is 0 when the index is CLEAN (nothing
         * new) and 1 when it is dirty (something to commit) -- the reference's
         * `if ! git diff --cached --quiet` shape. */
        int dirty;
        jc_sb_init(&out);
        {
            char *diff_tail[3];
            diff_tail[0] = (char *)"diff";
            diff_tail[1] = (char *)"--cached";
            diff_tail[2] = (char *)"--quiet";
            dirty = git_recs(&out, recs, e_global, e_system, NULL, 0,
                             diff_tail, 3);
        }
        jc_sb_free(&out);
        if (dirty != 1) {
            /* clean (or the check could not run): nothing to commit. */
            return;
        }
        /* dirty: commit with the explicit identity. */
        if (jc_snprintf(msg, sizeof msg, "records: run %s (%s)", run, kind)
            >= (int)sizeof msg) {
            return;
        }
        nextra = 0;
        if (git_name != NULL &&
            jc_snprintf(id_name, sizeof id_name, "user.name=%s", git_name)
            < (int)sizeof id_name) {
            extra[nextra++] = (char *)"-c";
            extra[nextra++] = id_name;
        }
        if (git_email != NULL &&
            jc_snprintf(id_email, sizeof id_email, "user.email=%s", git_email)
            < (int)sizeof id_email) {
            extra[nextra++] = (char *)"-c";
            extra[nextra++] = id_email;
        }
        {
            char *cm_tail[4];
            cm_tail[0] = (char *)"commit";
            cm_tail[1] = (char *)"-q";
            cm_tail[2] = (char *)"-m";
            cm_tail[3] = msg;
            jc_sb_init(&out);
            rc = git_recs(&out, recs, e_global, e_system, extra, nextra,
                          cm_tail, 4);
            if (rc != 0) {
                char *fl = first_line(&out, a);
                char ln[512];
                if (jc_snprintf(ln, sizeof ln,
                                "snapshot %s (%s): the records commit failed: "
                                "%s",
                                run, kind, (fl != NULL) ? fl : "(no output)")
                    < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
            }
            jc_sb_free(&out);
        }
    }
    /* The CHANGED 2 NO-OP: the records push is not built (def-360). The
     * commit is in dir/recs; nothing is pushed, never --force, never master. */
    {
        char ln[512];
        if (jc_snprintf(ln, sizeof ln,
                        "snapshot %s (%s): the records push is not built "
                        "(def-360; the commit is in dir/recs)",
                        run, kind) < (int)sizeof ln) {
            log_line(log_path, ln);
        }
    }
}

/* save_team: the in-process mirror fetch + mirror push (M1055/M1056).
 *
 * M1064 (attempt 2, the run-0085 root-cause correction) reshaped the FETCH:
 * it now runs through fetch_logged_to_harness_log (a fork child whose fd 1
 * is a pipe; the loop's OWN fd 1/fd 2 are never dup2'd for it) and its
 * report lines land in harness.log STAMPED via log_line (behaviour 14) --
 * before, not under, the push's redirect window. The window that remains
 * covers the PUSH only, exactly the M1056 idiom: dup(1), dup(2), dup2 both
 * onto the log, call the push, restore, close (its `mirror push:` say lines
 * are captured by the loop per CHANGED 9; the D3 cell asserts they are in
 * harness.log, the D3f cell the `team push off` line). A fresh arena for the
 * push: the config the in-process function loads is arena-owned and freed
 * with it (the fetch child owns its own). The rcs are report lines, not the
 * loop's exit class (a failed save_team is logged by the functions
 * themselves, the loop continues -- the reference's `save_team || log`
 * shape).
 *
 * The team push's switch (behaviour 3, SV:2037.1, `drive.teamPush`, the port
 * of loop.sh's `TEAM_PUSH=${ZD_TEAM_PUSH:-1}`): when `team_push` is false the
 * FETCH still runs but the push is SKIPPED and the line
 * `team push off: save_team fetched only` is logged (one line, the cell's
 * teeth). The switch sits in save_team, where loop.sh's CALLER reads
 * ZD_TEAM_PUSH -- the M1056 `mirror push` function itself is unchanged.
 */
static void
save_team(const char *config_path, const char *dir,
          const char *log_path, int team_push)
{
    char mirror[PATH_CAP];
    struct jc_arena *ra;
    int so = -1;
    int se = -1;
    int lf = -1;
    int ok = 0;

    if (jc_snprintf(mirror, sizeof mirror, "%s/mirror.git", dir)
        >= (int)sizeof mirror) {
        return;
    }
    /* The fetch FIRST, through the fork-child capture (the loop's own
     * fd 1/fd 2 are untouched by it); its lines are stamped by log_line. */
    (void)fetch_logged_to_harness_log(config_path, mirror, log_path);
    if (!team_push) {
        /* the push is the switch's teeth: fetch happens, push does not.
         * No redirect window at all in this branch (the fetch no longer
         * needs one and the push is skipped). */
        log_line(log_path, "team push off: save_team fetched only");
        return;
    }
    ra = jc_arena_new(0);
    if (ra == NULL) {
        return;
    }
    if (log_path != NULL && log_path[0] != '\0') {
        lf = open(log_path, O_WRONLY | O_APPEND | O_CREAT | JC_O_BINARY,
                  0644);
    }
    if (lf >= 0) {
        so = dup(1);
        se = dup(2);
        ok = (so >= 0 && se >= 0);
        if (ok) {
            (void)dup2(lf, 1);
            (void)dup2(lf, 2);
        }
    }
    /* The redirect is best-effort; the push runs either way. */
    (void)jc_harness_mirror_push_run(config_path, mirror, ra);
    jc_arena_free(ra);
    if (ok) {
        (void)dup2(so, 1);
        (void)dup2(se, 2);
    }
    if (so >= 0) {
        close(so);
    }
    if (se >= 0) {
        close(se);
    }
    if (lf >= 0) {
        close(lf);
    }
}

/* The MIRROR's main, `rev-parse --short refs/heads/<prefix>/main` in the
 * mirror (behaviour 10 / CHANGED 6, SV:1838.5b): this reads the MIRROR's
 * main, NOT the run's own team_head_after= (which is never re-appended). An
 * unresolvable ref is the EMPTY string (the mirror head's shape), so the
 * loop logs `run N: team head (none)`. */
static char *
mirror_main(const char *dir, const char *prefix, const char *e_global,
            const char *e_system, struct jc_arena *a)
{
    char mirror[PATH_CAP];
    char ref[512];
    char *tail[3];
    struct jc_sb out;
    int rc;

    if (jc_snprintf(mirror, sizeof mirror, "%s/mirror.git", dir)
        >= (int)sizeof mirror) {
        return NULL;
    }
    if (jc_snprintf(ref, sizeof ref, "refs/heads/%s/main", prefix)
        >= (int)sizeof ref) {
        return NULL;
    }
    tail[0] = (char *)"rev-parse";
    tail[1] = (char *)"--short";
    tail[2] = ref;
    jc_sb_init(&out);
    rc = git_recs(&out, mirror, e_global, e_system, NULL, 0, tail, 3);
    {
        char *h = (rc == 0) ? first_line(&out, a) : NULL;
        jc_sb_free(&out);
        return h;
    }
}

int
jc_harness_drive(const char *config_path, const char *tenant,
                 int once, int tick_override, struct jc_arena *a)
{
    struct jc_harness_config cfg;
    const struct jc_harness_tenant *t = NULL;
    char *self;
    char *argv[9];
    int k;
    char log_path[PATH_CAP];
    char dir_runs[PATH_CAP];
    time_t end;
    time_t t0 = 0;
    int n = 0;
    int tick;
    char e_global[PATH_CAP];
    char e_system[PATH_CAP];
    int quick = 0;
    int ti;
    const char *prefix;
    char *git_name = NULL;
    char *git_email = NULL;

    /* ---- the refusals (D8), ALL before any log line / run start -------- */
    if (config_path == NULL || config_path[0] == '\0') {
        refuse("needs --config <file>");
        return 2;
    }
    if (tenant == NULL || tenant[0] == '\0') {
        refuse("no --tenant named (drive needs one to run)");
        return 2;
    }
    if (tick_override == 0) {
        /* CHANGED 7: a 0 tick is a cap-on-housekeeping, not the reference's
         * value. A named refusal, exit 2 (the reference's TICK=0 would
         * save_team every poll; the port refuses instead). */
        refuse("--tick 0 is not a valid tick (a 0 tick is a cap-on-"
               "housekeeping, not the reference's value)");
        return 2;
    }
    /* behaviour 5 (CHANGED 4): the deadline, parsed once, BEFORE the rest of
     * the config is validated. The reference's order (loop.sh 29-31): it
     * sources drive.env and date-parses ZD_END at line 31, before it sources
     * mirror.sh (line 47) or runs the prechecks (line 92) -- the end is a
     * setting of the drive, read with the drive's own parsing, not a field
     * the loader validates. So the port extracts drive.end from the RAW
     * config text and parses it before the JSON load: a broken end is the
     * reference's own exit 1 ('the end time does not parse' on stderr,
     * BEFORE any `loop start` -- the start line and the counter are both
     * after it, so the log carries no start line at all), independent of
     * whether the rest of the file parses (the D8d cell's sed leaves a
     * malformed file; the reference would still have refused at its line
     * 31). */
    {
        char *raw = NULL;
        char end_buf[256];
        jc_size raw_len = 0;
        if (jc_read_file(config_path, &raw, &raw_len, a) != JC_OK ||
            raw == NULL) {
            refuse("the harness config could not be read (%s)", config_path);
            return 2;
        }
        if (extract_drive_end(raw, end_buf, sizeof end_buf) == NULL ||
            !parse_deadline(end_buf, &end)) {
            refuse("the end time does not parse");
            return 1;
        }
    }
    memset(&cfg, 0, sizeof cfg);
    if (jc_harness_config_load(config_path, &cfg, a) != JC_OK) {
        const char *err = jc_harness_config_error(&cfg);
        refuse("%s", (err != NULL) ? err : "the harness config is invalid");
        return 2;
    }
    t = NULL;
    for (ti = 0; ti < cfg.n_tenants; ti++) {
        if (strcmp(cfg.tenants[ti].name, tenant) == 0) {
            t = &cfg.tenants[ti];
            break;
        }
    }
    if (t == NULL) {
        /* the run's own refusal, reused by name (D8b: 'no tenant named ...'). */
        refuse("no tenant named %s (the config has %d tenant(s))",
               tenant, cfg.n_tenants);
        return 2;
    }
    if (t->workdir == NULL || t->workdir[0] == '\0') {
        refuse("tenant %s has no workdir (a run needs a place to chdir)",
               tenant);
        return 2;
    }
    prefix = (cfg.drive_branch_prefix != NULL) ? cfg.drive_branch_prefix
                                               : "";

    /* The drive's tick + identity (lenient; defaults 1200 / unset). Read
     * once: the snapshot commits carry the same identity every time, so the
     * arena strings outlive the loop. */
    {
        int ct;
        read_drive_extras(config_path, &ct, &git_name, &git_email, a);
        tick = (tick_override > 0) ? tick_override : ct;
    }

    if (!make_empty_git_files(cfg.dir, e_global, sizeof e_global,
                              e_system, sizeof e_system)) {
        refuse("could not create the empty GIT_CONFIG_GLOBAL/SYSTEM files "
               "in the harness's dir %s", cfg.dir);
        return 1;
    }
    /* The harness's own path (the M1053 idiom): the run child is this binary
     * with the `run` argv. A failure is exit 1 (the loop cannot start a run). */
    self = (char *)jc_arena_calloc(a, PATH_CAP);
    if (self == NULL) {
        refuse("out of memory");
        return 1;
    }
    {
        ssize_t rl = readlink("/proc/self/exe", self, PATH_CAP - 1);
        if (rl <= 0 || rl >= PATH_CAP - 1) {
            refuse("could not resolve the harness's own path (readlink "
                   "/proc/self/exe failed); the loop cannot start a run");
            return 1;
        }
        self[rl] = '\0';
    }
    if (jc_snprintf(log_path, sizeof log_path, "%s/harness.log", cfg.dir)
        >= (int)sizeof log_path) {
        log_path[0] = '\0';
    }
    if (jc_snprintf(dir_runs, sizeof dir_runs, "%s/runs", cfg.dir)
        >= (int)sizeof dir_runs) {
        refuse("the config's dir is too long to build the runs path");
        return 2;
    }
    /* The start line + the counter, read ONCE (behaviours 1/2). Both are
     * AFTER the deadline parse, so an unparseable end never logs a start. */
    {
        char ln[512];
        char end_iso[40];
        stamp_of(end_iso, sizeof end_iso, end);   /* loop.sh:102's shape */
        if (jc_snprintf(ln, sizeof ln,
                        "loop start (pid %ld), deadline %s",
                        (long)getpid(), end_iso) < (int)sizeof ln) {
            log_line(log_path, ln);
        }
    }
    n = read_run_counter(cfg.dir);

    /* ---- the outer loop (behaviour 3, in the note's order) ------------- */
    for (;;) {
        char runs_n[PATH_CAP];
        char stop_path[PATH_CAP];
        pid_t run_pid = -1;
        int status = 0;
        int child_rc = 1;
        time_t t1 = 0;
        char run_str[16];
        int i;
        int signalled = 0;
        int wait_done = 0;

        /* behaviour 4: the STOP file, at the TOP (before the increment). */
        if (jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", cfg.dir)
            < (int)sizeof stop_path) {
            if (jc_file_exists(stop_path)) {
                log_line(log_path, "STOP file present: loop ends");
                log_line(log_path, "loop end");
                return 0;
            }
        }
        /* the deadline check, at the TOP (before the increment). */
        if (time(NULL) >= end) {
            log_line(log_path, "deadline reached: loop ends");
            log_line(log_path, "loop end");
            return 0;
        }
        /* the run-number increment (n+1, 4 digits). */
        n = n + 1;
        if (jc_snprintf(run_str, sizeof run_str, "%04d", n)
            >= (int)sizeof run_str) {
            refuse("the run number is too long");
            return 2;
        }
        /* mirror fetch (in-process, M1055): the run's records are carried
         * into the mirror before the run starts (behaviour 1). M1064: the
         * fetch runs in a fork child whose fd 1 is a pipe (the loop's OWN
         * fd 1 is never dup2'd); each report line is log_line()'d stamped
         * and the rc is kept. */
        {
            char mirror[PATH_CAP];
            if (jc_snprintf(mirror, sizeof mirror, "%s/mirror.git", cfg.dir)
                < (int)sizeof mirror) {
                (void)fetch_logged_to_harness_log(config_path, mirror,
                                                  log_path);
            }
        }
        /* `run N: start` + t0. */
        {
            char ln[512];
            if (jc_snprintf(ln, sizeof ln, "run %s: start", run_str)
                < (int)sizeof ln) {
                log_line(log_path, ln);
            }
        }
        t0 = time(NULL);
        /* behaviour 6 (CHANGED 5): the run as a CHILD, this binary via
         * /proc/self/exe, args `run --config <f> --tenant <t> --run N`, a
         * new process group (so the deadline SIGINT takes the run AND its
         * jichi), devnull stdin, and stdout/stderr on the log (CHANGED 9:
         * the run's report lines are captured into harness.log). The
         * model key / git token reach the run child ONLY through the run's
         * own existing env delivery (the run reads modelKeyFile at spawn
         * time) -- the loop puts neither on argv, in the log, nor in the
         * child's env. */
        if (jc_snprintf(runs_n, sizeof runs_n, "%s/%s", dir_runs, run_str)
            >= (int)sizeof runs_n) {
            refuse("the runs path is too long");
            return 2;
        }
        k = 0;
        argv[k++] = self;
        argv[k++] = (char *)"run";
        argv[k++] = (char *)"--config";
        argv[k++] = (char *)config_path;
        argv[k++] = (char *)"--tenant";
        argv[k++] = (char *)tenant;
        argv[k++] = (char *)"--run";
        argv[k++] = (char *)run_str;
        argv[k] = NULL;
        {
            int logfd;
            struct jc_spawn_req req;
            logfd = open(log_path, O_WRONLY | O_APPEND | O_CREAT |
                         JC_O_BINARY, 0644);
            if (logfd < 0) {
                refuse("could not open the harness log for the run child");
                return 1;
            }
            (void)fcntl(logfd, F_SETFD, FD_CLOEXEC);
            memset(&req, 0, sizeof req);
            req.argv = argv;
            req.fd_stdin = JC_SPAWN_DEVNULL;
            req.fd_stdout = logfd;
            req.fd_stderr = logfd;
            req.new_pgroup = 1;   /* the run leads its own group; kill(-pid) */
            if (jc_spawn(&req, &run_pid) != JC_OK) {
                close(logfd);
                refuse("could not start the run child %s", run_str);
                return 1;
            }
            close(logfd);
        }
        /* behaviour 7 (CHANGED 7): the tick/poll, every 10 s, while the run
         * is alive. When i >= tick: save_team (in-process fetch + push) + a
         * `partial` snapshot + the `periodic push done` line, re-accumulating
         * i. A per-poll deadline check (now >= end AND not already signalled)
         * sends the run group a SIGINT ONCE (CHANGED 1, behaviour 8). */
        i = 0;
        while (!wait_done) {
            char ln[512];
            int drc;
            sleep(POLL_S);
            i = i + POLL_S;
            /* the per-poll deadline check (BEFORE the reap): send the group
             * SIGINT once. The run's own handler forwards it to the jichi
             * group and the run stays inside its waitpid (behaviour 9). */
            if (!signalled && time(NULL) >= end) {
                signalled = 1;
                /* loop.sh:126's shape: the line is logged BEFORE the kill. */
                if (jc_snprintf(ln, sizeof ln,
                                "run %s: deadline, SIGINT to the lead",
                                run_str) < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
                kill(-run_pid, SIGINT);
            }
            /* Is the run child still alive? */
            drc = waitpid(run_pid, &status, WNOHANG);
            if (drc == run_pid) {
                wait_done = 1;
                break;
            }
            /* the tick: save_team + the partial snapshot, re-accumulate i. */
            if (i >= tick) {
                i = 0;
                save_team(config_path, cfg.dir, log_path,
                          cfg.drive_team_push);
                snapshot(cfg.dir, log_path, run_str, "partial",
                         git_name, git_email, e_global, e_system, a);
                if (jc_snprintf(ln, sizeof ln,
                                "run %s: periodic push done", run_str)
                    < (int)sizeof ln) {
                    log_line(log_path, ln);
                }
            }
        }
        /* the wait is done: rc = the child's wait status (never masked). */
        if (WIFEXITED(status)) {
            child_rc = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            child_rc = 128 + WTERMSIG(status);
        } else {
            child_rc = 1;   /* unknown: a non-zero, never masked to 0 */
        }
        t1 = time(NULL);
        /* `run N: end rc=<rc> after <t1-t0> s`. */
        {
            char ln[512];
            if (jc_snprintf(ln, sizeof ln, "run %s: end rc=%d after %ld s",
                            run_str, child_rc, (long)(t1 - t0))
                < (int)sizeof ln) {
                log_line(log_path, ln);
            }
        }
        /* save_team (in-process fetch + push, M1055/M1056; the push is the
         * teamPush switch's, behaviour 3). */
        save_team(config_path, cfg.dir, log_path, cfg.drive_team_push);
        /* behaviour 10 (CHANGED 6, SV:1838.5b): the MIRROR's main, NOT the
         * run's own team_head_after= (which is never re-appended). */
        {
            char *head = mirror_main(cfg.dir, prefix, e_global, e_system, a);
            char ln[512];
            if (jc_snprintf(ln, sizeof ln,
                            "run %s: team head %s",
                            run_str, (head != NULL) ? head : "(none)")
                < (int)sizeof ln) {
                log_line(log_path, ln);
            }
        }
        /* the `final` snapshot (behaviour 11). */
        {
            char *gn = NULL;
            char *ge = NULL;
            int ct;
            read_drive_extras(config_path, &ct, &gn, &ge, a);
            snapshot(cfg.dir, log_path, run_str, "final",
                     (gn != NULL) ? gn : NULL, (ge != NULL) ? ge : NULL,
                     e_global, e_system, a);
        }
        /* behaviour 12 (CHANGED 8): the quick-failure count + pause. */
        if (child_rc != 0 && (t1 - t0) < QUICK_LIMIT_S) {
            quick = quick + 1;
        } else {
            quick = 0;
        }
        if (quick >= QUICK_COUNT) {
            int slept = 0;
            log_line(log_path,
                     "three quick failures in a row: waiting 10 minutes");
            while (slept < PAUSE_TOTAL_S) {
                sleep(PAUSE_SLICE_S);
                slept = slept + PAUSE_SLICE_S;
                /* a slice that sees the deadline or the STOP file EXITS THE
                 * PAUSE (CHANGED 8: the reference's solid sleep cannot). */
                if (time(NULL) >= end) {
                    break;
                }
                if (jc_snprintf(stop_path, sizeof stop_path,
                                "%s/STOP", cfg.dir)
                    < (int)sizeof stop_path) {
                    if (jc_file_exists(stop_path)) {
                        break;
                    }
                }
            }
            quick = 0;   /* at the END OF THE PAUSE, in every case (hunk) */
        }
        if (once) {
            break;
        }
        sleep(POLL_S);   /* the end-of-iteration spacing (the reference's 10) */
    }
    log_line(log_path, "loop end");
    return 0;
}
