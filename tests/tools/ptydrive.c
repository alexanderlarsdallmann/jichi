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
 * Exit codes: 0 script completed; 2 usage / script parse error; 3 expect
 * timeout (the transcript tail is dumped to stderr) or --deadline hit;
 * 4 pty/spawn failure; 5 waitexit timeout (child SIGKILLed); 6 assertexit
 * mismatch. Test-only; never installed.
 */

#include "pd_core.h"
#include "tt.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
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

static int pt_spawn(char **child_argv, int rows, int cols, int *master_out,
                    pid_t *pid_out)
{
    int master;
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

    pid = fork();
    if (pid < 0) {
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

    if (pt_spawn(child_argv, rows, cols, &master, &child) != 0) {
        fprintf(stderr, "%s: pty spawn failed\n", g_prog);
        pd_script_free(&script);
        return PT_EXIT_SPAWN;
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
