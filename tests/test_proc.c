/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_proc.c - shared child-capture helper jc_proc_capture (M-quality). */

#include "jc_test.h"
#include "jc_snprintf.h"
#include <stdio.h>
#include <sys/select.h>
#include <signal.h>
#include "jc_proc.h"
#include "jc_str.h"
#include "jc_platform.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h> /* getpgrp */

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

void test_proc(void)
{
    char sh[] = "/bin/sh";
    char dashc[] = "-c";

    test_memwatch();
    test_secret_env_scrub();

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

        fh = fopen(pidf, "r");
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
