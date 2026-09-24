/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* ptydrive - expect-lite PTY driver for the smoke tier (tests/tools, M209).
 *
 *   ptydrive [--rows 24] [--cols 80] [--deadline 60] [--log FILE]
 *            SCRIPT -- PROG [ARGS...]
 *
 * Spawns PROG on a real pseudo-terminal (posix_openpt; the child gets the
 * slave as its controlling tty, stdin/out/err and TIOCSWINSZ) and executes
 * SCRIPT against it -- see pd_core.h for the command set. `expect` matches
 * plain substrings over the whole accumulated transcript (bounded; the
 * oldest half is dropped past 256 KB), so a pattern split across reads is
 * always found; there is no regex and no VT interpretation (M209 decision
 * D7). Everything read is also appended to --log FILE (flushed as it
 * arrives) so the calling sh driver can grep the transcript afterwards.
 *
 * Environment hygiene (NO_COLOR/LC_ALL/TERM) is the CALLER's job,
 * mirroring tests/e2e/_e2e.py spawn().
 *
 * JC_SMOKE_TIMEOUT_MULT (fallback JC_E2E_TIMEOUT_MULT, default 1) scales
 * every DEADLINE -- each `expect`/`waitexit` timeout and --deadline -- but
 * never `delay`/`drain` (those pace sends; they cannot fail). Before M272
 * the knob only reached run.sh's outer per-driver limit, so on a slow
 * machine an inner expect could fire while the run under it was healthy
 * (found on the V2f old-kernel guest: turn_scratch's 99 mock turns outran
 * a fixed 90 s expect while progressing at ~1 turn/s).
 *
 * --presend TEXT writes TEXT to the pty master BEFORE the child is spawned, so
 * the bytes are already in the line discipline when the child first looks. Use
 * it for a property about input that arrived before the program was ready; a
 * script `send` cannot express that, because it runs after the fork and races
 * the child. ptydrive prints `presend ok` or `presend unsupported` on stderr --
 * OpenBSD refuses a write to a master with no slave open, and a driver must
 * branch on that rather than assume its fixture worked.
 *
 * Exit codes: 0 script completed; 2 usage / script parse error; 3 expect
 * timeout (the transcript tail is dumped to stderr) or --deadline hit;
 * 4 pty/spawn failure; 5 waitexit timeout (child SIGKILLed); 6 assertexit
 * mismatch. Test-only; never installed.
 */

#include "pd_core.h"
#include "tt.h"

#include <sys/types.h> /* pid_t: MiNTLib's <unistd.h> declares it only under an X/Open level (M723) */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
/* FIONREAD, used below to prove the pre-send bytes are READABLE and not merely
 * written, is not a POSIX ioctl and each libc files it somewhere else. Measured
 * on Cygwin 3.6.10 under this build's own -D_XOPEN_SOURCE=600: <sys/ioctl.h>
 * does NOT declare it (with or without the feature macro) and neither does
 * <sys/filio.h>; <sys/socket.h> does. It is a POSIX header present everywhere
 * this tree builds, so it is included unconditionally rather than behind a
 * platform name. Without it the smoke TOOLING does not compile on Cygwin at
 * all, so the row cannot run its tier -- found while re-measuring that row. */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#ifdef JC_HAVE_STREAMS_PTY
#include <stropts.h>
/* FIONREAD is not in <sys/ioctl.h> on illumos; <sys/filio.h> has it. But the
 * probe cannot tell illumos from glibc before 2.30, which ships a stub
 * <stropts.h> with I_PUSH defined and has no <sys/filio.h> at all -- so on
 * Debian 9 the smoke TOOLING did not compile (V2f, 2026-09-24). Ask for the
 * header only where the macro is still missing, which is the question. */
#ifndef FIONREAD
#include <sys/filio.h>
#endif
#endif
#include <time.h>
#include <unistd.h>

#define PT_EXIT_EXPECT   3
#define PT_EXIT_SPAWN    4
#define PT_EXIT_WAITEXIT 5
#define PT_EXIT_ASSERT   6

#define PT_TRANSCRIPT_MAX (256u * 1024u)
#define PT_TAIL_DUMP 2048

static const char *g_prog = "ptydrive";
static volatile sig_atomic_t g_child_pid = 0;

/* Has ANY byte ever arrived from the child? Until it has, a zero-length read or
 * an EIO on the pty master is ambiguous -- "the slave is not open yet" and "the
 * child is gone" look identical, and the two platforms pick different ones. See
 * pt_read for the measurement. (M467; it cost 21 OpenBSD drivers.) */
static int g_saw_output = 0;
/* Zero-length reads tolerated before the first byte, at 20 ms each (~2 s). */
#define PT_SLAVE_WAIT 100
static int g_zero_reads = 0;

/* Deadline scale, from the shared helper (tt_mult.c, M273 -- one
 * implementation for every tool with a deadline). Applied to
 * expect/waitexit/--deadline only, never delay/drain. */
static long g_mult = 1;

static void on_alarm(int sig)
{
    (void)sig;
    if (g_child_pid > 0)
        kill((pid_t)g_child_pid, SIGKILL);
    _exit(TT_EXIT_DEADLINE);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: %s [--rows N] [--cols N] [--deadline SECS] "
            "[--log FILE]\n"
            "       SCRIPT -- PROG [ARGS...]\n", g_prog);
}

/* --- transcript ----------------------------------------------------------- */

struct pt_buf {
    char *data;
    size_t len;
    size_t cap;
};

static int pt_buf_add(struct pt_buf *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t ncap = (b->cap == 0) ? 8192 : b->cap;
        char *nd;
        while (ncap < b->len + n + 1)
            ncap *= 2;
        nd = (char *)realloc(b->data, ncap);
        if (nd == NULL)
            return -1;
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    /* bound the matcher's window: drop the oldest half past the cap */
    if (b->len > PT_TRANSCRIPT_MAX) {
        size_t keep = PT_TRANSCRIPT_MAX / 2;
        memmove(b->data, b->data + (b->len - keep), keep);
        b->len = keep;
        b->data[b->len] = '\0';
    }
    return 0;
}

/* --- time ----------------------------------------------------------------- */

static long pt_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

/* --- pty reading ---------------------------------------------------------- */

/* Wait up to wait_ms for output; append what arrives to the transcript
 * (unless discard) and to the log. Returns >0 bytes read, 0 on timeout,
 * -1 on EOF/EIO (child gone). */
static int pt_read(int master, long wait_ms, struct pt_buf *transcript,
                   int discard, FILE *logf)
{
    fd_set rf;
    struct timeval tv;
    char chunk[4096];
    long n;
    int rc;

    FD_ZERO(&rf);
    FD_SET(master, &rf);
    tv.tv_sec = wait_ms / 1000;
    tv.tv_usec = (wait_ms % 1000) * 1000;
    rc = select(master + 1, &rf, NULL, NULL, &tv);
    if (rc <= 0)
        return 0;
    n = (long)read(master, chunk, sizeof(chunk));
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            return 0;           /* nothing yet; the caller owns the deadline */
        /* THE PORTABILITY BUG THIS REPLACES (M467). This was
         * `if (n <= 0) return -1;` under the comment "EOF, or EIO after child
         * exit (Linux)" -- and the parenthesis was the whole defect. Before the
         * child has opened the slave, the two platforms disagree about what the
         * master looks like, MEASURED side by side with the identical program:
         *
         *   OpenBSD 7.9  t=0..2100ms  select=READABLE, read()==0, errno 0
         *                             (the child was alive and wrote at 1 s)
         *   Linux 7.0    t=0..600ms   select()==0, not readable
         *                t=900ms      read()==18, the data
         *
         * So a zero read means "the slave is not open YET" on one platform and
         * "the child is gone" on the other, with nothing in the return value to
         * tell them apart. Every smoke driver whose script opens with `expect`
         * -- most of them -- therefore raced the child's open on OpenBSD, took
         * the first zero read for death, stopped reading, and reported
         * `expect timed out (child exited)` over `0 of 0 bytes`. Twenty-one
         * drivers, and it reproduced with `cat` as the child, so it was never
         * about the program under test.
         *
         * Tolerate the ambiguity ONLY before the first byte, and only for a
         * BOUNDED number of attempts. Both halves are load-bearing: after any
         * output the slave has certainly been opened, so a later zero read or
         * EIO really is death and must stay fast; and a child that exits having
         * written nothing is a genuine EOF that Linux reports through this same
         * path, so unbounded tolerance would make it fail by timeout instead of
         * at once -- trading one platform's bug for every platform's
         * diagnostics. Measured: without the bound a silently-dying child took
         * the full 30 s expect; with it, ~2 s.
         *
         * The nap matters too. OpenBSD keeps calling the master readable, so
         * returning straight to the caller spins hot until its deadline. */
        if (!g_saw_output && g_zero_reads < PT_SLAVE_WAIT) {
            struct timeval nap;
            g_zero_reads++;
            nap.tv_sec = 0;
            nap.tv_usec = 20000;
            (void)select(0, NULL, NULL, NULL, &nap);
            return 0;
        }
        return -1;              /* the child is gone */
    }
    g_saw_output = 1;
    if (!discard)
        pt_buf_add(transcript, chunk, (size_t)n);
    if (logf != NULL) {
        fwrite(chunk, 1, (size_t)n, logf);
        fflush(logf);
    }
    return (int)n;
}

static void pt_dump_tail(const struct pt_buf *b)
{
    size_t from = (b->len > PT_TAIL_DUMP) ? b->len - PT_TAIL_DUMP : 0;
    fprintf(stderr, "%s: transcript tail (%lu of %lu bytes):\n",
            g_prog, (unsigned long)(b->len - from), (unsigned long)b->len);
    if (b->data != NULL)
        fwrite(b->data + from, 1, b->len - from, stderr);
    fputc('\n', stderr);
}

/* --- spawn ---------------------------------------------------------------- */

/* `presend`: bytes written to the MASTER before the fork, so they are already in
 * the line discipline when the child first looks (M672).
 *
 * WHY IT EXISTS. A `send` in the script runs in the parent AFTER the fork, so it
 * RACES the child's startup. For a driver testing what a program does with input
 * that arrived BEFORE it was ready -- `preprompt_discard` is the one -- that race
 * is the whole experiment, and it resolves differently per platform: on Linux the
 * write lands first, on OpenBSD the child reaches its terminal setup first, every
 * time. The driver there failed while jichi was behaving perfectly, and jichi's
 * own instrumentation said so: `enter_raw fd=0 pending=0`, five runs out of five.
 *
 * *presend_ok is 0 when the pre-write could not be done, and that is NOT an error:
 * **on OpenBSD a write to a master with no slave open returns -1** (measured:
 * Linux accepts it and the bytes survive until the slave opens). A caller that
 * needs the precondition must therefore check, and say so, rather than assert a
 * property its fixture could not create.
 *
 * THE THIRD CASE, AND THE ONLY ONE THAT LIES (M683). illumos accepts the write,
 * returns the full length, and DISCARDS the bytes. Measured on OmniOS r151058
 * with a 14-line probe: write 6 bytes to the master with no slave open, then
 * open the slave and ask FIONREAD -- Linux answers 6, illumos answers 0, with
 * and without the STREAMS module push, so the push is not the mechanism. So
 * `*presend_ok = (write returned plen)` reported the ATTEMPT, and this project's
 * own platform rule is "report the EFFECT, never the attempt". preprompt_discard
 * has a skip path for exactly this precondition and FAILED instead of taking it,
 * because the only thing it could ask was whether the write returned.
 *
 * TWO CHANGES, and the second is what makes the first honest:
 *
 *   1. The slave is opened IN THE PARENT before the pre-write, which is also
 *      the more faithful model -- a real terminal exists before anyone types
 *      into it; nothing types at a pty that has not been opened. Measured: the
 *      bytes then survive on illumos exactly as on Linux (6 of 6). The child
 *      INHERITS that descriptor across fork and closes it only after opening
 *      its own, so the slave open count never reaches zero and neither the
 *      flush-on-last-close nor the EOF-on-master semantics change.
 *   2. *presend_ok is now set from ioctl(FIONREAD) on the slave -- the bytes
 *      are readable, or they are not. If FIONREAD itself fails we report 0:
 *      a caller that cannot verify the precondition must skip, not assume.
 *
 * Only the presend path opens the slave early; the other eighteen pty drivers
 * pass no --presend and take the original path untouched. */
static int pt_spawn(char **child_argv, int rows, int cols, int *master_out,
                    pid_t *pid_out, const char *presend, int *presend_ok)
{
    int master;
    int slave_pre = -1;
    char *slave_name;
    pid_t pid;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return -1;
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        close(master);
        return -1;
    }
    slave_name = ptsname(master);
    if (slave_name == NULL) {
        close(master);
        return -1;
    }

    if (presend_ok != NULL) {
        *presend_ok = 0;
    }
    if (presend != NULL && presend[0] != '\0') {
        size_t plen = strlen(presend);
        ssize_t pw;
        int pending = -1;

        /* Hold the slave open across the write; see the header comment. */
        slave_pre = open(slave_name, O_RDWR | O_NOCTTY);
#ifdef JC_HAVE_STREAMS_PTY
        if (slave_pre >= 0 && !isatty(slave_pre)) {
            (void)ioctl(slave_pre, I_PUSH, "ptem");
            (void)ioctl(slave_pre, I_PUSH, "ldterm");
            (void)ioctl(slave_pre, I_PUSH, "ttcompat");
        }
#endif
        pw = write(master, presend, plen);
        if (pw == (ssize_t)plen && slave_pre >= 0 &&
            ioctl(slave_pre, FIONREAD, &pending) == 0 &&
            pending >= (int)plen && presend_ok != NULL) {
            *presend_ok = 1;         /* the bytes are READABLE, not merely sent */
        }
    }

    pid = fork();
    if (pid < 0) {
        if (slave_pre >= 0)
            close(slave_pre);
        close(master);
        return -1;
    }
    if (pid == 0) {
        int slave;
        struct winsize ws;
        setsid();
        slave = open(slave_name, O_RDWR);   /* becomes the controlling tty */
        if (slave < 0)
            _exit(127);
        /* Only now: the inherited descriptor kept the slave open across the
         * fork so the pre-written bytes could not be flushed by a last close. */
        if (slave_pre >= 0)
            close(slave_pre);
#ifdef JC_HAVE_STREAMS_PTY
        /* On a STREAMS system (illumos/Solaris) a freshly opened pty slave is
         * NOT a terminal: measured on OmniOS r151058 at M661, `tcgetattr` fails
         * and `isatty` returns 0 until the line-discipline modules are pushed
         * onto the stream. Everything downstream then behaves correctly and
         * wrongly -- jichi sees a non-tty and takes its non-interactive path, so
         * all nineteen pty-driven smoke drivers failed while the pty itself
         * "worked". The whole cluster was one cause.
         *
         * Guarded by the PROBE, not by `defined(__sun)`: the Makefile asks
         * whether <stropts.h> and I_PUSH exist, which is the question the
         * compiler will ask (M658). And guarded again at RUNTIME by isatty, so
         * a system that hands out terminals directly is left alone -- the push
         * happens because the slave is not a terminal, not because of who
         * shipped the kernel. */
        if (!isatty(slave)) {
            (void)ioctl(slave, I_PUSH, "ptem");
            (void)ioctl(slave, I_PUSH, "ldterm");
            (void)ioctl(slave, I_PUSH, "ttcompat");
        }
#endif
#ifdef TIOCSCTTY
        ioctl(slave, TIOCSCTTY, 0);         /* be explicit; may be a no-op */
#endif
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = (unsigned short)rows;
        ws.ws_col = (unsigned short)cols;
        ioctl(slave, TIOCSWINSZ, &ws);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2)
            close(slave);
        close(master);
        execvp(child_argv[0], child_argv);
        _exit(127);
    }
    if (slave_pre >= 0)
        close(slave_pre);           /* the child holds its own; see above */
    *master_out = master;
    *pid_out = pid;
    return 0;
}

/* --- main ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int rows = 24, cols = 80;
    long deadline = 60;
    const char *log_path = NULL;
    const char *script_path = NULL;
    const char *presend = NULL;
    int presend_ok = 0;
    char **child_argv = NULL;
    struct pd_script script;
    char err[256];
    char *script_text = NULL;
    FILE *logf = NULL;
    struct pt_buf transcript;
    int master = -1;
    pid_t child = -1;
    int have_status = 0;
    int child_status = -1;
    int eof_seen = 0;
    int i;

    memset(&transcript, 0, sizeof(transcript));
    g_mult = tt_timeout_mult();

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            rows = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--presend") == 0 && i + 1 < argc) {
            presend = argv[++i];
        } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
            cols = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--deadline") == 0 && i + 1 < argc) {
            deadline = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (script_path == NULL) {
            script_path = argv[i];
        } else if (strcmp(argv[i], "--") == 0) {
            if (i + 1 >= argc) {
                usage();
                return TT_EXIT_USAGE;
            }
            child_argv = argv + i + 1;
            break;
        } else {
            usage();
            return TT_EXIT_USAGE;
        }
    }
    if (script_path == NULL || child_argv == NULL || rows < 1 || cols < 1) {
        usage();
        return TT_EXIT_USAGE;
    }

    /* load + parse the script before spawning anything */
    {
        FILE *f = fopen(script_path, "rb");
        long sz;
        if (f == NULL) {
            fprintf(stderr, "%s: cannot read %s\n", g_prog, script_path);
            return TT_EXIT_USAGE;
        }
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0)
            sz = 0;
        script_text = (char *)malloc((size_t)sz + 1);
        if (script_text == NULL) {
            fclose(f);
            return TT_EXIT_USAGE;
        }
        if (sz > 0 && fread(script_text, 1, (size_t)sz, f) != (size_t)sz) {
            fclose(f);
            free(script_text);
            fprintf(stderr, "%s: short read on %s\n", g_prog, script_path);
            return TT_EXIT_USAGE;
        }
        script_text[sz] = '\0';
        fclose(f);
    }
    if (pd_script_parse(script_text, &script, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s: %s: %s\n", g_prog, script_path, err);
        free(script_text);
        return TT_EXIT_USAGE;
    }
    free(script_text);

    if (log_path != NULL) {
        logf = fopen(log_path, "wb");
        if (logf == NULL) {
            fprintf(stderr, "%s: cannot write %s\n", g_prog, log_path);
            pd_script_free(&script);
            return TT_EXIT_USAGE;
        }
    }

    if (pt_spawn(child_argv, rows, cols, &master, &child,
                 presend, &presend_ok) != 0) {
        fprintf(stderr, "%s: pty spawn failed\n", g_prog);
        pd_script_free(&script);
        return PT_EXIT_SPAWN;
    }
    if (presend != NULL && presend[0] != '\0') {
        /* Said out loud either way, on stderr, so a driver can BRANCH on it
         * rather than assume. A precondition a fixture could not create is not
         * a failure of the program under test, and a driver that cannot tell
         * the two apart reports the wrong one. */
        fprintf(stderr, "%s: presend %s\n", g_prog,
                presend_ok ? "ok" : "unsupported (write to master before spawn failed)");
    }
    g_child_pid = (sig_atomic_t)child;
    signal(SIGALRM, on_alarm);
    if (deadline > 0)
        alarm((unsigned)(deadline * g_mult));

    for (i = 0; i < script.ncmds; i++) {
        const struct pd_cmd *c = &script.cmds[i];
        switch (c->kind) {
        case PD_CMD_EXPECT: {
            long end = pt_now_ms() + c->a * 1000L * g_mult;
            int found = 0;
            for (;;) {
                if (pd_match(transcript.data,
                             transcript.len, c->text,
                             c->text_len) != NULL) {
                    found = 1;
                    break;
                }
                if (pt_now_ms() >= end)
                    break;
                if (pt_read(master, 200, &transcript, 0, logf) < 0) {
                    eof_seen = 1;
                    /* the child is gone; whatever is buffered is all
                     * there will ever be -- check once more and stop */
                    if (pd_match(transcript.data, transcript.len,
                                 c->text, c->text_len) != NULL)
                        found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr,
                        "%s: line %d: expect \"%s\" timed out (%lds%s)\n",
                        g_prog, c->line, c->text, c->a * g_mult,
                        eof_seen ? ", child exited" : "");
                pt_dump_tail(&transcript);
                kill(child, SIGKILL);
                waitpid(child, NULL, 0);
                pd_script_free(&script);
                return PT_EXIT_EXPECT;
            }
            break;
        }
        case PD_CMD_SEND: {
            size_t off = 0;
            while (off < c->text_len) {
                long n = (long)write(master, c->text + off,
                                     c->text_len - off);
                if (n <= 0)
                    break;
                off += (size_t)n;
            }
            break;
        }
        case PD_CMD_DELAY:
        case PD_CMD_DRAIN: {
            long end = pt_now_ms() + c->a;
            while (pt_now_ms() < end) {
                long left = end - pt_now_ms();
                if (left < 1)
                    break;
                if (pt_read(master, left > 100 ? 100 : left, &transcript,
                            c->kind == PD_CMD_DRAIN, logf) < 0) {
                    eof_seen = 1;
                    /* child gone; just sleep out the remainder */
                    {
                        struct timeval tv;
                        left = end - pt_now_ms();
                        if (left < 1)
                            break;
                        tv.tv_sec = left / 1000;
                        tv.tv_usec = (left % 1000) * 1000;
                        select(0, NULL, NULL, NULL, &tv);
                    }
                    break;
                }
            }
            break;
        }
        case PD_CMD_WINSIZE: {
            struct winsize ws;
            memset(&ws, 0, sizeof(ws));
            ws.ws_row = (unsigned short)c->a;
            ws.ws_col = (unsigned short)c->b;
            ioctl(master, TIOCSWINSZ, &ws);
            /* SIGWINCH is not POSIX -- it is BSD-derived and XSI-ish, and
             * FreeBSD hides it under __BSD_VISIBLE while this tree builds
             * -D_POSIX_C_SOURCE=200112L, so the identifier is undeclared there
             * and the whole smoke tier fails to compile (M459). Third symbol of
             * this shape found by the first non-Linux row, after
             * _SC_NPROCESSORS_ONLN and INADDR_LOOPBACK.
             *
             * Guarded rather than faked: a signal NUMBER cannot be invented the
             * way INADDR_LOOPBACK's constant could, and sending the wrong one
             * would be worse than sending none.
             *
             * The coverage cost is nil for jichi specifically, which is why
             * this is acceptable rather than merely convenient: the ioctl above
             * has already resized the pty, and jichi's TUI POLLS TIOCGWINSZ
             * (jc_term.c) rather than trapping SIGWINCH -- the product mentions
             * the signal only in a comment. A program that did trap it would
             * lose the notification here, so the guard is noted, not silent. */
#ifdef SIGWINCH
            kill(child, SIGWINCH);
#endif
            break;
        }
        case PD_CMD_SIGNAL:
            kill(child, (int)c->a);
            break;
        case PD_CMD_WAITEXIT: {
            long end = pt_now_ms() + c->a * 1000L * g_mult;
            for (;;) {
                int st = 0;
                pid_t w = waitpid(child, &st, WNOHANG);
                if (w == child) {
                    if (WIFEXITED(st))
                        child_status = WEXITSTATUS(st);
                    else if (WIFSIGNALED(st))
                        child_status = 128 + WTERMSIG(st);
                    else
                        child_status = -1;
                    have_status = 1;
                    child = -1;
                    break;
                }
                if (pt_now_ms() >= end) {
                    fprintf(stderr,
                            "%s: line %d: waitexit timed out (%lds)\n",
                            g_prog, c->line, c->a * g_mult);
                    pt_dump_tail(&transcript);
                    kill(child, SIGKILL);
                    waitpid(child, NULL, 0);
                    pd_script_free(&script);
                    return PT_EXIT_WAITEXIT;
                }
                /* keep draining so the child can flush and exit */
                if (pt_read(master, 100, &transcript, 0, logf) < 0) {
                    struct timeval tv;
                    tv.tv_sec = 0;
                    tv.tv_usec = 50 * 1000;
                    select(0, NULL, NULL, NULL, &tv);
                }
            }
            break;
        }
        case PD_CMD_ASSERTEXIT:
            if (!have_status) {
                fprintf(stderr,
                        "%s: line %d: assertexit before waitexit\n",
                        g_prog, c->line);
                if (child > 0) {
                    kill(child, SIGKILL);
                    waitpid(child, NULL, 0);
                }
                pd_script_free(&script);
                return PT_EXIT_ASSERT;
            }
            if (child_status != (int)c->a) {
                fprintf(stderr,
                        "%s: line %d: exit status %d, expected %ld\n",
                        g_prog, c->line, child_status, c->a);
                pt_dump_tail(&transcript);
                pd_script_free(&script);
                return PT_EXIT_ASSERT;
            }
            break;
        }
    }

    /* script done; never leave a stray child behind */
    if (child > 0) {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    if (logf != NULL)
        fclose(logf);
    close(master);
    free(transcript.data);
    pd_script_free(&script);
    return TT_EXIT_OK;
}
