/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_proc.c - shared child-capture helper jc_proc_capture (M-quality). */

#include "jc_test.h"
#include "jc_snprintf.h"
#include <sys/types.h> /* pid_t: MiNTLib's <unistd.h> declares it only under an X/Open level (M723) */
#include <stdio.h>
#include "jc_select.h"  /* M834: <sys/select.h> where it exists */
#include <sys/time.h> /* struct timeval: MiNTLib's <sys/select.h> only forward-declares it (M723) */
#include <signal.h>
#include "jc_proc.h"
#include "jc_str.h"
#include "jc_platform.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h> /* getpgrp, pid_t */
#include <sys/wait.h> /* waitpid, WIFEXITED, WEXITSTATUS, WNOHANG, WIFSIGNALED, WTERMSIG */
#include <errno.h> /* errno, ECHILD */
#include <time.h> /* struct timespec: the passthrough poll below needs a sub-second pause (usleep is not in C89) */

/* E-01: a reaper that retries on EINTR and returns -1 on any failure
 * (including ECHILD when SIGCHLD was inherited ignored). */
static void test_waitpid_helper(void);

/* M989 (B-01, B-06): bounded, interrupt-aware reap helper unit tests. */
static void test_reap_bounded_passthrough(void);
static void test_reap_bounded_deadline(void);
static void test_reap_bounded_echild_unknown(void);
static void test_reap_bounded_abort(void);
static void test_capture_closes_then_lives(void);

static void test_waitpid_helper(void)
{
    int st;
    int dummy;
    int rc;

    /* (a) the helper on a normal child: fork a child that exits 1;
     * after jc_proc_waitpid, check that it returned 0 AND that
     * WIFEXITED(*st) and WEXITSTATUS(*st)==1 (the control proving
     * the helper passes a real status through). */
    {
        pid_t pid = fork();
        if (pid == 0) {
            _exit(1);
        } else if (pid > 0) {
            rc = jc_proc_waitpid(pid, &st);
            JC_CHECK(rc == 0);
            JC_CHECK(WIFEXITED(st));
            JC_CHECK(WEXITSTATUS(st) == 1);
        }
    }

    /* (b) the helper under ignored SIGCHLD. Deterministic construction:
     * ignore SIGCHLD (the inherited-supervisor state), fork a child that
     * exits 1, then POLL with WNOHANG until the child is gone -- reaped by
     * the kernel (the usual case) or collected by this probe (a ~1-in-2000
     * kernel race measured on this box; both are "gone"). After the child is
     * gone the disposition is RESTORED to SIG_DFL, so every subsequent
     * blocking waitpid on that pid fails ECHILD by contract, not by timing:
     * the teeth below hold on any conforming system. */
    {
        void (*old)(int);
        pid_t pid;
        int i;
        int st0;

        old = signal(SIGCHLD, SIG_IGN);
        pid = fork();
        if (pid == 0) {
            _exit(1);
        } else if (pid > 0) {
            for (i = 0; i < 1000; i++) {
                struct timespec one_ms;
                if (waitpid(pid, NULL, WNOHANG) == -1 && errno == ECHILD) {
                    break;   /* gone: kernel-reaped or collected by the probe */
                }
                one_ms.tv_sec = 0;
                one_ms.tv_nsec = 1000000;   /* 1 ms; a 1s poll bound is generous */
                nanosleep(&one_ms, NULL);
            }
            /* The child is gone; restore SIG_DFL. A blocking waitpid on a
             * pid with no child left is now ECHILD by contract. */
            signal(SIGCHLD, old);

            /* THE DEFECT this helper removes: the pre-fix shape
             * (`int status = 0; waitpid(pid, &status, 0);` unchecked, then
             * WIFEXITED on the zero) decodes "no child" as "exited normally
             * with code 0" -- a failing verifier read as passing. */
            st0 = 0;
            (void)waitpid(pid, &st0, 0);
            JC_CHECK(st0 == 0);
            JC_CHECK(WIFEXITED(st0) && WEXITSTATUS(st0) == 0);

            /* The helper says UNKNOWN instead, and leaves the status
             * untouched. */
            dummy = 999;
            rc = jc_proc_waitpid(pid, &dummy);
            JC_CHECK(rc == -1);
            JC_CHECK(dummy == 999);
        }
    }
}

/* M989 (B-01, B-06): test_reap_bounded_passthrough -- normal exit passes
 * through with its real status, not reported as deadline exceeded. */
static void test_reap_bounded_passthrough(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";
    char prog[] = "exit 1";
    char *argv[4];
    pid_t pid;
    int st;
    int rc;

    /* Ensure SIGCHLD is SIG_DFL. */
    signal(SIGCHLD, SIG_DFL);

    /* Fork a child using exec like the M987 test to exercise the helper. */
    pid = fork();
    if (pid == 0) {
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid > 0) {
        /* Wait for child to start (50ms delay). */
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 50000;
        select(0, NULL, NULL, NULL, &tv);
        rc = jc_proc_reap_bounded(pid, jc_now_millis() + 5000.0, NULL, &st);
        JC_CHECK(rc == 0);
        JC_CHECK(WIFEXITED(st));
        /* The shell exits with the exit code. */
        JC_CHECK(WEXITSTATUS(st) == 1);
    }
}

/* M989 (B-01): test_reap_bounded_deadline -- deadline actually kills. */
static void test_reap_bounded_deadline(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";
    /* Child that closes its output then sleeps 5s (EOF+alive is the B-01 trigger). */
    char prog[] = "exec >/dev/null 2>&1; sleep 5";
    char *argv[4];
    pid_t pid;
    int st;
    int rc;
    int i;

    /* Ensure SIGCHLD is SIG_DFL. */
#ifdef SIGCHLD
    signal(SIGCHLD, SIG_DFL);
#endif

    pid = fork();
    if (pid == 0) {
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid > 0) {
        /* A short 200 ms bound against a child that lives 5 s: the deadline
         * must fire and stop it (not wait out the 5 s). */
        rc = jc_proc_reap_bounded(pid, jc_now_millis() + 200.0, NULL, &st);
        JC_CHECK(rc == -2);  /* stopped by deadline */
        /* Confirm the child is dead. */
        for (i = 0; i < 40 && kill(pid, 0) == 0; i++) {
            struct timeval tv;
            tv.tv_sec = 0; tv.tv_usec = 50000;
            select(0, NULL, NULL, NULL, &tv);
        }
        JC_CHECK(kill(pid, 0) != 0);  /* child must be gone */
    }
}

/* M989 (M987): test_reap_bounded_echild_unknown -- SIGCHLD=SIG_IGN -> ECHILD -> -1. */
static void test_reap_bounded_echild_unknown(void)
{
    pid_t pid;
    int st;
    int rc;
    int i;

    /* Set SIGCHLD=SIG_IGN as the inherited state. */
    signal(SIGCHLD, SIG_IGN);

    pid = fork();
    if (pid == 0) {
        _exit(1);
    } else if (pid > 0) {
        /* Let the child exit and be auto-reaped by the kernel under SIG_IGN.
         * Poll kill(pid, 0) until it reports ESRCH -- the child FULLY reaped --
         * so the helper's FIRST waitpid deterministically hits ECHILD. kill does
         * NOT waitpid, so it does not consume the ECHILD state this test needs.
         * A fixed 10 ms sleep was here; it assumed the kernel reaps within 10 ms,
         * which a native run does but valgrind does not -- under valgrind the child
         * is still a zombie at 10 ms, so the helper reaped it (rc 0, not -1):
         * `FAIL tests/test_proc.c:211` in make ci's valgrind stage (supervisor
         * 2026-10-19 20:40, measured: valgrind + 10 ms gives waitpid=0; polling
         * to gone gives ECHILD). 1 ms steps, 5 s bound, a plain JC_CHECK on the
         * bound. */
        for (i = 0; i < 5000 && kill(pid, 0) == 0; i++) {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 1000;
            select(0, NULL, NULL, NULL, &tv);
        }
        JC_CHECK(kill(pid, 0) != 0);  /* child must be fully reaped before the helper */

        /* Restore disposition BEFORE calling the helper, so the helper sees
         * SIGCHLD=SIG_DFL and waitpid returns ECHILD on its first poll. */
        signal(SIGCHLD, SIG_DFL);

        /* Now call the bounded reap. The child is already gone (auto-reaped
         * by the kernel under SIG_IGN), so the helper's FIRST waitpid returns
         * ECHILD, which the helper's ECHILD arm maps to -1 (UNKNOWN). */
        rc = jc_proc_reap_bounded(pid, 5000.0, NULL, &st);
        /* ECHILD -> -1 (UNKNOWN), NOT 0 and NOT a decodable exit. */
        JC_CHECK(rc == -1);
    }
}

/* M989 (B-06): test_reap_bounded_abort -- abort flag stops the child. */
static void test_reap_bounded_abort(void)
{
    pid_t pid;
    int st;
    int i;
    int rc;
    volatile int abort_flag = 0;

    /* Ensure SIGCHLD is SIG_DFL. */
#ifdef SIGCHLD
    signal(SIGCHLD, SIG_DFL);
#endif

    pid = fork();
    if (pid == 0) {
        sleep(5);  /* long sleep */
        _exit(0);
    } else if (pid > 0) {
        /* Deadline 0 = no time bound, only abort can stop. */
        abort_flag = 1;  /* set BEFORE calling */
        rc = jc_proc_reap_bounded(pid, 0.0, &abort_flag, &st);
        JC_CHECK(rc == -3);  /* stopped by abort */
        /* Confirm child is dead. */
        for (i = 0; i < 40 && kill(pid, 0) == 0; i++) {
            struct timeval tv;
            tv.tv_sec = 0; tv.tv_usec = 50000;
            select(0, NULL, NULL, NULL, &tv);
        }
        JC_CHECK(kill(pid, 0) != 0);
    }
}

/* M989 (R06-#3): test_capture_closes_then_lives -- pins the capture site.
 *
 * The child closes its output (EOF) then sleeps past the deadline. The
 * read-loop detects EOF and then fires the deadline; the deterministic -2
 * branch (not the post-EOF bounded helper) is what stops it. */
static void test_capture_closes_then_lives(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";
    /* Child that closes output then sleeps 3s. 1s deadline means the
     * read-loop's deadline fires while the child is still alive. */
    char prog[] = "exec >/dev/null 2>&1; sleep 3";
    char *argv[4];
    struct jc_sb out;
    int code;

    /* Ensure SIGCHLD is SIG_DFL. */
#ifdef SIGCHLD
    signal(SIGCHLD, SIG_DFL);
#endif

    argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
    jc_sb_init(&out);
    /* 1s deadline, child lives 3s, so the read-loop's deadline fires and the
     * deterministic -2 branch (not the slow post-EOF helper) is what stops it. */
    code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 1, NULL);
    /* Must return -2 (stopped), NOT 0 (would mean child ran to completion). */
    JC_CHECK(code == -2);
    jc_sb_free(&out);
}


/* E-01: the start-up signal normaliser resets SIGCHLD to SIG_DFL and
 * unblocks inherited signals. */
static void test_signal_reset_inherited(void)
{
    void (*old_sigint)(int);
    void (*old_sigterm)(int);
    void (*old_sighup)(int);
#ifdef SIGCHLD
    void (*old_sigchld)(int);
#endif
    sigset_t oldset, newset, checkset;

    /* Save current dispositions. */
    old_sigint = signal(SIGINT, SIG_IGN);
    signal(SIGINT, old_sigint);
    old_sigterm = signal(SIGTERM, SIG_IGN);
    signal(SIGTERM, old_sigterm);
    old_sighup = signal(SIGHUP, SIG_IGN);
    signal(SIGHUP, old_sighup);
#ifdef SIGCHLD
    old_sigchld = signal(SIGCHLD, SIG_IGN);
    signal(SIGCHLD, old_sigchld);
#endif

    /* Block all four signals to verify unblocking. */
    sigemptyset(&newset);
    sigaddset(&newset, SIGINT);
    sigaddset(&newset, SIGTERM);
    sigaddset(&newset, SIGHUP);
#ifdef SIGCHLD
    sigaddset(&newset, SIGCHLD);
#endif
    sigprocmask(SIG_BLOCK, &newset, &oldset);

    /* Set SIGCHLD to IGNORE to verify reset. */
#ifdef SIGCHLD
    signal(SIGCHLD, SIG_IGN);
#endif

    /* Run the normaliser. */
    jc_signal_reset_inherited();

    /* Verify SIGCHLD is SIG_DFL. */
#ifdef SIGCHLD
    {
        struct sigaction sa;
        sigaction(SIGCHLD, NULL, &sa);
        JC_CHECK(sa.sa_handler == SIG_DFL);
    }
#endif

    /* Verify signals are unblocked. */
    sigprocmask(SIG_UNBLOCK, NULL, &checkset);
    JC_CHECK(sigismember(&checkset, SIGINT) == 0);
    JC_CHECK(sigismember(&checkset, SIGTERM) == 0);
    JC_CHECK(sigismember(&checkset, SIGHUP) == 0);
#ifdef SIGCHLD
    JC_CHECK(sigismember(&checkset, SIGCHLD) == 0);
#endif

    /* Restore original dispositions. */
    signal(SIGINT, old_sigint);
    signal(SIGTERM, old_sigterm);
    signal(SIGHUP, old_sighup);
#ifdef SIGCHLD
    signal(SIGCHLD, old_sigchld);
#endif
    sigprocmask(SIG_SETMASK, &oldset, NULL);
}

static void test_memwatch(void)
{
    /* budget off -> always OK */
    JC_CHECK(jc_memwatch_decision(999999, 0) == JC_MEMWATCH_OK);
    JC_CHECK(jc_memwatch_decision(999999, -1) == JC_MEMWATCH_OK);
    /* 1000 MB budget = 1024000 KB: warn at >=80%, kill at >=100% */
    JC_CHECK(jc_memwatch_decision(500L * 1024, 1000L * 1024) == JC_MEMWATCH_OK);
    JC_CHECK(jc_memwatch_decision(850L * 1024, 1000L * 1024) == JC_MEMWATCH_WARN);
    JC_CHECK(jc_memwatch_decision(1000L * 1024, 1000L * 1024) == JC_MEMWATCH_KILL);
    JC_CHECK(jc_memwatch_decision(1200L * 1024, 1000L * 1024) == JC_MEMWATCH_KILL);
    /* Group RSS of our own group is > 0 (this process is in it) -- but only
     * where this process HAS a process group. Measured on Guix (M450): inside
     * `guix shell -C` every process has **pgrp 0** (field 5 of
     * /proc/self/stat reads `3 (cat) R 2 0 0 ...`), so getpgrp() returns 0,
     * jc_proc_group_rss_kb honours its own `pgid <= 0 => 0` contract -- the
     * very contract the line below pins -- and the check reddened for an
     * environment property rather than a defect. A red that is not a defect
     * costs a reader exactly what a real one does, and this suite is meant to
     * be runnable on a new platform to find out whether jichi works there.
     * The guard is the pgid itself, not /proc: /proc IS mounted and readable
     * in that container, so jc_have_proc_rss() would not have fired. */
    /* M459: and only where procfs EXISTS. FreeBSD does not mount /proc by
     * default, so jc_proc_group_rss_kb's `opendir("/proc") == NULL => 0`
     * contract fires and this check reddened on the first non-Linux row --
     * again for an environment property rather than a defect.
     *
     * The comment above says jc_have_proc_rss() "would not have fired" on
     * Guix, and that was right: /proc IS mounted there, so the pgid was the
     * correct guard. FreeBSD is the other case, and needs the other guard.
     * Both are needed, for different reasons, which is why neither alone was
     * enough. */
    if ((long)getpgrp() > 0 && jc_have_proc_rss()) {
        JC_CHECK(jc_proc_group_rss_kb((long)getpgrp()) > 0);
    }
    JC_CHECK(jc_proc_group_rss_kb(0) == 0);
}

/* M130: the child must not inherit registered secret env vars. */
static void test_secret_env_scrub(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";

    /* The prefix helper enumerates a built-in default plus any registered
     * name, as a shell `unset ... ; ` clause. */
    {
        char pfx[1024];
        int wrote;
        jc_proc_secret_env_add("MYCORP_LLM_KEY");
        wrote = jc_proc_secret_env_prefix(pfx, sizeof(pfx));
        JC_CHECK(wrote == 1);
        JC_CHECK(strncmp(pfx, "unset ", 6) == 0);
        JC_CHECK(strstr(pfx, "ANTHROPIC_API_KEY") != NULL);
        JC_CHECK(strstr(pfx, "MYCORP_LLM_KEY") != NULL);
        JC_CHECK(strstr(pfx, "; ") != NULL);
    }

    /* Invalid names (shell metacharacters) are refused, so they can't be
     * spliced into the unset clause. */
    {
        char pfx[1024];
        jc_proc_secret_env_add("BAD;rm -rf /");
        jc_proc_secret_env_prefix(pfx, sizeof(pfx));
        JC_CHECK(strstr(pfx, "rm -rf") == NULL);
    }

    /* End-to-end: a registered var set in the parent is gone in the child. */
    {
        char prog[] = "printf %s \"$MYCORP_LLM_KEY\"";
        char *argv[4];
        struct jc_sb out;
        int code;
        setenv("MYCORP_LLM_KEY", "sk-secret-value-123", 1);
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 5, NULL);
        JC_CHECK(code == 0);
        JC_CHECK(out.len == 0); /* scrubbed: nothing echoed */
        jc_sb_free(&out);
        unsetenv("MYCORP_LLM_KEY");
    }

    /* M608: jichi's OWN key names are built in too. The list carried thirteen
     * third-party names and neither JICHI_API_KEY (the wizard's default) nor
     * JLU_API_KEY (the HRZ onboarding name), so a stray export of the one key
     * this project's users actually have reached every child. Unregistered
     * here on purpose: only the built-in list can drop them. */
    {
        char prog[] = "printf %s%s \"$JICHI_API_KEY\" \"$JLU_API_KEY\"";
        char *argv[4];
        struct jc_sb out;
        int code;
        setenv("JICHI_API_KEY", "canary-own-1", 1);
        setenv("JLU_API_KEY", "canary-own-2", 1);
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 5, NULL);
        JC_CHECK(code == 0);
        JC_CHECK(out.len == 0); /* both dropped, without registration */
        jc_sb_free(&out);
        unsetenv("JICHI_API_KEY");
        unsetenv("JLU_API_KEY");
    }

    /* A built-in provider var is dropped even without explicit registration. */
    {
        char prog[] = "printf %s \"$ANTHROPIC_API_KEY\"";
        char *argv[4];
        struct jc_sb out;
        int code;
        setenv("ANTHROPIC_API_KEY", "sk-ant-should-not-leak", 1);
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 5, NULL);
        JC_CHECK(code == 0);
        JC_CHECK(out.len == 0);
        jc_sb_free(&out);
        unsetenv("ANTHROPIC_API_KEY");
    }
}

/* M724: however many names a config registers, and however long, every one is
 * dropped -- and a prefix that does not fit is refused, not emptied. The
 * registry held 32 names of under 128 bytes and ignored the rest, and a prefix
 * that did not fit its buffer came back as 0, "nothing to drop", so the popen
 * path ran the command with every key in place. */
static void jc_m724_name(char *name, jc_size cap, int i)
{
    jc_size len;
    jc_snprintf(name, cap, "JC_M724_U%02d_", i);
    len = strlen(name);
    memset(name + len, 'K', 200 - len);  /* 200 bytes: past the old 127 */
    name[200] = '\0';
}

static void test_secret_env_many_names(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";
    char name[256];
    char prog[320];
    char *argv[4];
    struct jc_sb out;
    char *pfx;
    jc_size need;
    int i, n0, all = 1, code;

    n0 = jc_proc_secret_env_count();
    for (i = 0; i < 40; i++) {                   /* 40: past the old 32 */
        jc_m724_name(name, sizeof(name), i);
        JC_CHECK(jc_proc_secret_env_add(name) == 0);
    }
    JC_CHECK(jc_proc_secret_env_count() == n0 + 40);

    need = jc_proc_secret_env_prefix_size();
    pfx = (char *)malloc(need);
    JC_CHECK(pfx != NULL);
    if (pfx != NULL) {
        JC_CHECK(jc_proc_secret_env_prefix(pfx, need) == 1);
        JC_CHECK(strlen(pfx) + 1 == need);       /* the size is exact */
        for (i = 0; i < 40; i++) {
            jc_m724_name(name, sizeof(name), i);
            if (strstr(pfx, name) == NULL) {
                all = 0;
            }
        }
        JC_CHECK(all);                           /* every one is in the prefix */
        /* One byte short: refused as -1, which "nothing to drop" (0) is not. */
        JC_CHECK(jc_proc_secret_env_prefix(pfx, need - 1) == -1);
        JC_CHECK(pfx[0] == '\0');
        free(pfx);
    }

    /* The fork path: the 40th name, set in the parent, is gone in the child. */
    jc_m724_name(name, sizeof(name), 39);
    setenv(name, "canary-m724", 1);
    jc_snprintf(prog, sizeof(prog), "printf %%s \"$%s\"", name);
    argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
    jc_sb_init(&out);
    code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 5, NULL);
    JC_CHECK(code == 0);
    JC_CHECK(out.len == 0);
    jc_sb_free(&out);
    unsetenv(name);

    jc_proc_secret_env_truncate(n0);             /* leave it as it was found */
    JC_CHECK(jc_proc_secret_env_count() == n0);
}

/* M1043-a (plan 2026-10-03, 4.2): the secretEnv register. A name configured via
 * the "secretEnv" key is registered through the SAME jc_proc_secret_env_add the
 * apiKeyEnv loop uses, so it is listed by jc_proc_env_name_is_secret and carried
 * in the popen unset-prefix (and dropped by the fork+exec scrub). The four
 * built-in CI/forge names are listed with NO registration -- the M608 shape,
 * dropped even when unconfigured. The registry is process-wide, so this test
 * records the count, adds, then truncates back to it (the M724 pattern). */
static void test_secret_env_register(void)
{
    int n0;

    n0 = jc_proc_secret_env_count();
    JC_CHECK(jc_proc_secret_env_add("MYCORP_X_TOKEN") == 0);
    /* the registered name is now a secret: listed, and in the popen prefix */
    JC_CHECK(jc_proc_env_name_is_secret("MYCORP_X_TOKEN", 14) == 1);
    {
        char pfx[1024];
        JC_CHECK(jc_proc_secret_env_prefix(pfx, sizeof(pfx)) == 1);
        JC_CHECK(strstr(pfx, "MYCORP_X_TOKEN") != NULL);
        JC_CHECK(strncmp(pfx, "unset ", 6) == 0);
    }
    jc_proc_secret_env_truncate(n0);          /* leave it as it was found */
    JC_CHECK(jc_proc_secret_env_count() == n0);

    /* the four M1043-a built-ins are listed with NO add (the M608 shape) */
    JC_CHECK(jc_proc_env_name_is_secret("CI_JOB_TOKEN", 12) == 1);
    JC_CHECK(jc_proc_env_name_is_secret("GITLAB_TOKEN", 12) == 1);
    JC_CHECK(jc_proc_env_name_is_secret("GITHUB_TOKEN", 12) == 1);
    JC_CHECK(jc_proc_env_name_is_secret("GH_TOKEN", 8) == 1);
    {
        char pfx[1024];
        JC_CHECK(jc_proc_secret_env_prefix(pfx, sizeof(pfx)) == 1);
        JC_CHECK(strstr(pfx, "CI_JOB_TOKEN") != NULL);
        JC_CHECK(strstr(pfx, "GITLAB_TOKEN") != NULL);
        JC_CHECK(strstr(pfx, "GITHUB_TOKEN") != NULL);
        JC_CHECK(strstr(pfx, "GH_TOKEN") != NULL);
    }
}

static void test_null_out_is_a_discard(void)
{
    char *argv[] = { (char *)"sh", (char *)"-c", (char *)"yes line | head -n 200", (char *)0 };
    int code;
    /* M929 (R06-#1): `out` NULL is a discard path, not a crash. Before this
    * every append dereferenced the NULL (SIGSEGV, exit 139, verified against
    * the pre-fix tree). The drain still runs -- a `yes`-fed child would
    * block on a full pipe and eat the 5 s timeout -- so both the exit code
    * and the no-timeout path are the check.
    */
    code = jc_proc_capture(argv, NULL, NULL, NULL, 0, 5, NULL);
    JC_CHECK(code == 0);
}

void test_proc(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";

    /* M989: run the new bounded reap tests first, before the existing blocks. */
    test_reap_bounded_passthrough();
    test_reap_bounded_deadline();
    test_reap_bounded_echild_unknown();
    test_reap_bounded_abort();
    test_capture_closes_then_lives();

    test_memwatch();
    test_secret_env_scrub();
    test_secret_env_many_names();
    test_secret_env_register();   /* M1043-a: the secretEnv register + 4 built-ins */
    test_null_out_is_a_discard();
    test_waitpid_helper();
    test_signal_reset_inherited();

    /* Basic capture + exit code. */
    {
        char prog[] = "printf hello";
        char *argv[4];
        struct jc_sb out;
        int code;
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 64 * 1024, 5, NULL);
        JC_CHECK(code == 0);
        JC_CHECK(out.data != NULL && strncmp(out.data, "hello", 5) == 0);
        jc_sb_free(&out);
    }

    /* Concurrent stdin->stdout: feed `cat` more stdin than a pipe buffer
     * (200 KB) and read its echo back. The old write-all-stdin-then-read code
     * deadlocked here; jc_proc_capture services both via select. */
    {
        char prog[] = "cat";
        char *argv[4];
        struct jc_sb out;
        jc_size n = 200 * 1024;
        char *big = (char *)malloc(n + 1);
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        if (big != NULL) {
            jc_size i;
            int code;
            for (i = 0; i < n; i++) {
                big[i] = 'x';
            }
            big[n] = '\0';
            jc_sb_init(&out);
            code = jc_proc_capture(argv, NULL, big, &out, 1024 * 1024, 10, NULL);
            JC_CHECK(code == 0);
            JC_CHECK(out.len == n); /* cat echoed every byte, no deadlock */
            jc_sb_free(&out);
            free(big);
        }
    }

    /* Timeout: a slow child is killed near the deadline and returns -2. */
    {
        char prog[] = "sleep 10";
        char *argv[4];
        struct jc_sb out;
        int code;
        double t0 = jc_now_millis();
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 1, NULL);
        JC_CHECK(code == -2);
        /* M368: sleep 10 with a bound of 8 -- the old sleep-5-vs-4.0 left
         * one second of discrimination that a loaded VM ate (observed on
         * the fifth consecutive `make ci`: the kill WORKED, code -2, and
         * the wall margin still blew). The -2 is the discriminating fact;
         * this bound only proves the child did not run to completion, so
         * it sits far from both sides. Monotonic, like the deadline it
         * measures. */
        JC_CHECK(jc_now_millis() - t0 < 8000.0);
        jc_sb_free(&out);
    }

    /* M461: a child must NOT inherit the agent's ignored SIGPIPE.
     *
     * jichi ignores SIGPIPE so a disconnecting client cannot kill it (and
     * test_main mirrors that, deliberately, so this suite runs the product's
     * real disposition). POSIX resets CAUGHT signals across exec but leaves
     * IGNORED ones ignored, so until this was fixed every command the agent
     * ran started with SIGPIPE ignored -- and in a pipeline the producer then
     * stops dying when the consumer exits, spinning on EPIPE until the kill
     * deadline instead.
     *
     * The shape below is the everyday one (`producer | head`) and it is
     * measurably two-sided on Linux as well as BSD: 0.003 s with the default
     * disposition, still running at 8 s with it ignored. Revert
     * jc_proc_child_sigreset() and this returns -2 (timed out) instead of 0.
     *
     * OpenBSD found it, with yes(1) as the producer, because BSD yes does not
     * check its write result; GNU coreutils do, which is the only reason this
     * never showed on Linux. The bug was on every platform the whole time. */
    {
        char prog[] = "while :; do echo aaaa; done | head -n 200";
        char *argv[4];
        struct jc_sb out;
        int code;
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 65536, 5, NULL);
        JC_CHECK(code == 0);   /* -2 here means the child inherited SIG_IGN */
        jc_sb_free(&out);
    }

    /* M461: a timed-out capture must kill the whole PIPELINE, not just the
     * shell it spawned.
     *
     * jc_proc_capture killed only its direct child, so in `a | b` every other
     * member was orphaned to init and kept running. Found by asking why a test
     * VM was pegged at load average 9: eight `yes` processes, all ppid 1, left
     * over from timed-out captures and still spinning three hours and fifty
     * minutes later.
     *
     * The left member records its pid and then execs a long sleep. After the
     * capture times out, that pid must be gone. Without the setpgid/kill(-pid)
     * pair this check finds it alive and the test leaks a sleep(1) of its own,
     * which is the defect demonstrating itself. */
    {
        char prog[512];
        char *argv[4];
        struct jc_sb out;
        const char *pidf = jc_test_tmp("jc_pipeline_pid.txt");
        long victim = 0;
        int i;
        FILE *fh;

        remove(pidf);
        jc_snprintf(prog, sizeof(prog),
                    "sh -c 'echo $$ > %s; exec sleep 30' | cat", pidf);
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        JC_CHECK(jc_proc_capture(argv, NULL, NULL, &out, 4096, 1, NULL) == -2);
        jc_sb_free(&out);

        fh = fopen(pidf, "rb");
        if (fh != NULL) {
            if (fscanf(fh, "%ld", &victim) != 1) { victim = 0; }
            fclose(fh);
        }
        if (victim > 0) {
            /* The kill is asynchronous; give the group a moment to die. */
            for (i = 0; i < 40 && kill((pid_t)victim, 0) == 0; i++) {
                struct timeval tv;
                tv.tv_sec = 0; tv.tv_usec = 50000;
                select(0, NULL, NULL, NULL, &tv);
            }
            JC_CHECK(kill((pid_t)victim, 0) != 0); /* orphan survived = the bug */
        }
        remove(pidf);
    }

    /* Output cap: a child that prints more than the cap is truncated with a
     * note, not overflowed. */
    {
        /* `head -n`, NOT `head -c`: -c is a GNU/FreeBSD extension that OpenBSD
         * does not have ("head: unknown option -- c"), which made the pipeline
         * produce nothing and this whole block fail on the OpenBSD row (M461).
         * POSIX specifies only -n. 25000 lines x 5 bytes = 125 KB, still well
         * over the 4096-byte cap this is here to exercise. */
        char prog[] = "yes aaaa | head -n 25000";
        char *argv[4];
        struct jc_sb out;
        int code;
        argv[0] = sh; argv[1] = dashc; argv[2] = prog; argv[3] = NULL;
        jc_sb_init(&out);
        code = jc_proc_capture(argv, NULL, NULL, &out, 4096, 10, NULL);
        JC_CHECK(code == 0);
        JC_CHECK(out.len <= 4096 + 64); /* cap + the truncation note */
        JC_CHECK(strstr(out.data ? out.data : "", "truncated") != NULL);
        jc_sb_free(&out);
    }
}
