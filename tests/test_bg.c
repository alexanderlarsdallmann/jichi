/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_bg.c - background process registry (M26). Forks real /bin/sh children. */

#include "jc_test.h"
#include "jc_bg.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "jc_fcntl.h"

/* Poll+read up to ~2s, waiting for `id` to exit; ACCUMULATES every read in
 * `sb` (cleared once, at entry).
 *
 * It must accumulate rather than overwrite, because jc_bg_read is an
 * INCREMENTAL reader by design: it advances read_off and returns only the
 * bytes new since the last call ("(no new output)" when there are none), so
 * that read_background_output hands the model a tail instead of re-sending
 * the whole buffer -- and the token bill -- on every poll. Clearing `sb` each
 * iteration therefore DISCARDED output an earlier poll had already consumed,
 * and the caller's assertions held only when a child's output and its exit
 * happened to land in the SAME 20ms window.
 *
 * That is true of `echo x` on a fast idle host and false the moment anything
 * is slow: found on an Android tablet whose build had drifted from 27s to 43s
 * between runs, where "background_hello" arrived one poll before "exited"
 * (M459). The two adjacent assertions are what identified it -- "exited,
 * status 0" passed, so the shell had run and echo had succeeded, which left
 * only the reader. */
static void wait_exit(struct jc_bg_mgr *m, int id, struct jc_sb *sb)
{
    struct jc_sb chunk;
    int i;

    jc_sb_init(&chunk);
    jc_sb_clear(sb);
    for (i = 0; i < 100; i++) {
        jc_sb_clear(&chunk);
        jc_bg_read(m, id, &chunk);
        if (chunk.data != NULL) {
            jc_sb_append_n(sb, chunk.data, chunk.len);
        }
        if (chunk.data != NULL && strstr(chunk.data, "exited") != NULL) {
            break;
        }
        jc_sleep_ms(20, NULL);
    }
    jc_sb_free(&chunk);
}

/* Same as wait_exit, but with a caller-specified poll count bound, so the
 * def-326 block can pass a generous bound (e.g. 1000 polls x 20 ms = 20 s)
 * to avoid a load-induced flake: on a busy host, the 30000-line shell loop
 * can run longer than the default 2 s, and the "exited" marker would arrive
 * only after the loop finishes, so the caller's check for the final line
 * bgspool_line_29999 would fail before the "exited" marker is seen. */
static void wait_exit_long(struct jc_bg_mgr *m, int id, struct jc_sb *sb,
                           int max_polls)
{
    struct jc_sb chunk;
    int i;

    jc_sb_init(&chunk);
    jc_sb_clear(sb);
    for (i = 0; i < max_polls; i++) {
        jc_sb_clear(&chunk);
        jc_bg_read(m, id, &chunk);
        if (chunk.data != NULL) {
            jc_sb_append_n(sb, chunk.data, chunk.len);
        }
        if (chunk.data != NULL && strstr(chunk.data, "exited") != NULL) {
            break;
        }
        jc_sleep_ms(20, NULL);
    }
    jc_sb_free(&chunk);
}

/* Read `id` to exit, ACCUMULATING every read in `sb` as wait_exit does, then
 * count how many times the line `marker` (exact, including its newline) occurs
 * in what was delivered. The count is the L-20 assertion: at the cap's edge
 * the head buffer and the 8 KiB tail ring overlap, so the pre-fix code
 * delivers the overlapping lines TWICE (and at exactly the cap, marks the
 * output truncated although nothing was dropped). Each line of the run's
 * output must reach the model exactly once. */
/* Count exact (including the newline) occurrences of `marker` in `sb`. */
static long count_in_sb(const struct jc_sb *sb, const char *marker)
{
    long count = 0;
    jc_size mlen = (jc_size)strlen(marker);
    jc_size off;

    if (sb->data == NULL) {
        return 0;
    }
    for (off = 0; off + mlen <= sb->len; off++) {
        if (memcmp(sb->data + off, marker, mlen) == 0) {
            count++;
        }
    }
    return count;
}

/* Read `id` to exit, accumulating every read in `sb` as wait_exit does, then
 * count the marker's occurrences in what was delivered. */
static long wait_exit_count(struct jc_bg_mgr *m, int id, struct jc_sb *sb,
                            const char *marker)
{
    wait_exit(m, id, sb);
    return count_in_sb(sb, marker);
}

void test_bg(void)
{
    struct jc_bg_mgr m;
    struct jc_sb sb;
    int id;
    int i;
    struct stat st;
    long file_size;
    char spool_path[700];
    long iter;
    /* M940: the registry now spools child output to ~/.jichi.d/bgspool --
     * a side effect of init AND of every start. This suite must not touch the
     * operator's real home, so redirect HOME to a temp tree for its whole
     * duration (pattern: test_agentdef.c) and restore it afterwards. */
    char home_save[512];
    char home_tmp[600];
    char spool_dir[600];

    home_save[0] = '\0';
    {
        const char *h = getenv("HOME");
        if (h != NULL) {
            jc_snprintf(home_save, sizeof(home_save), "%s", h);
        }
    }
    /* jc_test_tmp's pointer is valid only until 16 further calls, so copy it
     * out immediately. */
    jc_snprintf(home_tmp, sizeof(home_tmp), "%s", jc_test_tmp("jichi_bg_home"));
    (void)mkdir(home_tmp, 0700);
    setenv("HOME", home_tmp, 1);
    jc_snprintf(spool_dir, sizeof(spool_dir), "%s/.jichi.d/bgspool", home_tmp);

    jc_bg_mgr_init(&m);
    jc_sb_init(&sb);

    /* M940: init created the private spool directory. */
    {
        struct stat st;
        JC_CHECK(stat(spool_dir, &st) == 0 && S_ISDIR(st.st_mode));
        JC_CHECK((st.st_mode & (mode_t)077) == 0); /* private (0700/0707) */
    }

    /* A short command runs, produces output, and exits 0. */
    id = jc_bg_start(&m, "echo background_hello");
    JC_CHECK(id > 0);
    if (id > 0) {
        wait_exit(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "background_hello") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited, status 0") != NULL);
    }

    /* Output produced BEFORE the process exits -- the case the feature exists
     * for (a dev server logging while it runs), and the one a SINGLE poll
     * cannot observe. The case above can pass without ever crossing a poll
     * boundary, which is why the helper's bug hid behind it for 400
     * milestones. `sleep 1` is deliberately a whole second: POSIX sleep takes
     * an integer, and this suite runs on busybox and uClibc targets where a
     * fractional argument is not portable. */
    jc_sb_clear(&sb);
    id = jc_bg_start(&m, "echo early_line; sleep 1");
    JC_CHECK(id > 0);
    if (id > 0) {
        char spool[700];
        struct stat st;
        /* M940 teeth: while the child runs, its output lives in the spool
         * FILE (not a pipe) -- the writer never blocks on a reader. */
        jc_snprintf(spool, sizeof(spool), "%s/bg-%d-%ld.out", spool_dir, id,
                    (long)getpid()); /* this jichi is the child's parent */
        JC_CHECK(stat(spool, &st) == 0 && S_ISREG(st.st_mode));
        wait_exit(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "early_line") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited, status 0") != NULL);
        /* ...and it is unlinked once the child is reaped. */
        JC_CHECK(access(spool, F_OK) != 0);
    }

    /* M940 teeth (updated for def-326): ~300 KiB of short lines run past
     * the 256 KiB head cap. The head survives AND the tail (the last
     * JC_BG_TAIL_MAX bytes, where a build/test log prints its verdict) is
     * delivered at exit; only the MIDDLE is dropped, and truncation is flagged. */
    jc_sb_clear(&sb);
    id = jc_bg_start(&m,
                     "i=0; while [ $i -lt 30000 ]; do echo bgspool_line_$i; "
                     "i=$((i+1)); done");
    JC_CHECK(id > 0);
    if (id > 0) {
        /* wait_exit_long with 1000 polls = 20 s: on a loaded box, the
         * 30000-line shell loop may run longer than the default 2 s, so
         * the "exited" marker arrives only after the loop finishes. */
        wait_exit_long(&m, id, &sb, 1000);
        /* The 30000-line loop must exit within the generous bound. */
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "output truncated") != NULL);
        /* the HEAD survived: early lines are present */
        JC_CHECK(strstr(sb.data ? sb.data : "", "bgspool_line_0") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "bgspool_line_1") != NULL);
        /* def-326: the TAIL survived: the final line (the verdict) is present */
        JC_CHECK(strstr(sb.data ? sb.data : "", "bgspool_line_29999") != NULL);
        /* ...but the MIDDLE was dropped: a line far past the head and well
         * before the tail is absent */
        JC_CHECK(strstr(sb.data ? sb.data : "", "bgspool_line_20000") == NULL);
    }

    /* L-20 (M963, red-first): exactly the cap. 32768 lines of 8 bytes each
     * ("l%06d\n") = 262144 B = JC_BG_BUF_MAX, the head's whole capacity.
     * Nothing is dropped, so the tail flush must NOT fire: the pre-fix code
     * set `overflowed` on `n >= room`, so at exit the ring (the same last
     * 8 KiB the head already delivered, lines 31744..32767) was flushed
     * again with no marker -- the overlapping lines reach the reader twice,
     * and the run is marked truncated although the output is complete.
     * The assertion is a COUNT of a boundary line, not a presence check:
     * presence passes on both the pre-fix and the post-fix code. */
    {
        long c;
        jc_sb_clear(&sb);
        id = jc_bg_start(&m,
                         "i=0; while [ $i -lt 32768 ]; do printf 'l%06d\\n' "
                         "$i; i=$((i+1)); done");
        JC_CHECK(id > 0);
        if (id > 0) {
            c = wait_exit_count(&m, id, &sb, "l032500\n");
            JC_CHECK(c == 1); /* delivered exactly once, not twice */
            JC_CHECK(strstr(sb.data ? sb.data : "", "output truncated") == NULL);
        }
    }

    /* L-20 (M963, red-first): 260 KiB. 33280 lines of 8 bytes = 266240 B,
     * 4096 over the cap. The head holds lines 0..32767, the ring holds the
     * last 8192 bytes (lines 32256..33279); the overlap is lines 32256..
     * 32767. The pre-fix flush delivers the whole ring (mid = -4096 < 0,
     * no marker), so the overlapping lines reach the reader twice. Post-fix
     * the flush skips the first -mid bytes, each line arrives exactly once,
     * no middle marker (bytes in neither head nor ring: none), and the run
     * IS marked truncated (the output no longer fits the head). */
    {
        long c;
        jc_sb_clear(&sb);
        id = jc_bg_start(&m,
                         "i=0; while [ $i -lt 33280 ]; do printf 'l%06d\\n' "
                         "$i; i=$((i+1)); done");
        JC_CHECK(id > 0);
        if (id > 0) {
            c = wait_exit_count(&m, id, &sb, "l032500\n");
            JC_CHECK(c == 1); /* the overlap: once, not twice */
            JC_CHECK(count_in_sb(&sb, "l033279\n") == 1); /* the tail end */
            JC_CHECK(strstr(sb.data ? sb.data : "", "middle bytes not shown")
                     == NULL);
            JC_CHECK(strstr(sb.data ? sb.data : "", "output truncated") != NULL);
        }
    }

    /* def-324 / L-27 (operator decision 2026-10-19 08:00): the on-disk
     * cap now binds at REAP, not mid-write. The old block asserted
     * `st_size <= JC_BG_SPOOL_MAX` while the child was still RUNNING
     * (cap_verified) -- that is the def-324 decision the operator openly
     * superseded: while the child runs the spool file is UNBOUNDED (the
     * running-time disk bound is deferred, L-22), and only after reap does the
     * final drain cap it (or unlink it, which this block observes). So the
     * block now asserts what is true: the file grows PAST the cap while the
     * child is alive (no rewrite), and the head + a truncated marker still
     * reach the reader at exit. MARKTAIL sits at ~520 KiB, i.e. in the dropped
     * MIDDLE (the tail ring holds only the last 8 KiB), so it must be ABSENT --
     * the mirror of the M962 middle-drop invariant. No read_off reset is
     * needed: nothing is read before the child exits, so the exit read is the
     * first and sees the head (MARKHEAD) intact. */
    {
        int cap_exceeded_while_running = 0;
        int spool_capped_or_gone = 0;

        jc_sb_clear(&sb);
        id = jc_bg_start(&m,
                         "echo 'MARKHEAD'; "
                         "i=0; while [ $i -lt 130 ]; do "
                         "  yes MARK | head -c 4096; "
                         "  i=$((i+1)); "
                         "  if [ $i -eq 125 ]; then echo 'MARKTAIL'; fi; "
                         "  sleep 0.005; "
                         "done; "
                         "sleep 5");
        JC_CHECK(id > 0);
        if (id > 0) {
            /* Poll the on-disk size while the child is still alive (stat only,
             * no jc_bg_read, so the head is undelivered and the exit read is
             * the first). The writer emits ~532 KiB in ~0.65 s, past the
             * 512 KiB cap, while it sleeps. */
            for (iter = 0; iter < 40 && !cap_exceeded_while_running; iter++) {
                jc_snprintf(spool_path, sizeof(spool_path), "%s/bg-%d-%ld.out",
                            spool_dir, id, (long)getpid());
                if (stat(spool_path, &st) == 0) {
                    file_size = (long)st.st_size;
                    if (file_size > JC_BG_SPOOL_MAX) {
                        cap_exceeded_while_running = 1;
                    }
                }
                jc_sleep_ms(100, NULL);
            }
            /* L-27: with the gate, nothing rewrites the file while the child
             * runs, so it may and does exceed the cap before reap. */
            JC_CHECK(cap_exceeded_while_running);

            /* Reap (kill: the writer would keep sleeping). The final drain
             * then caps the file to JC_BG_SPOOL_MAX and reap unlinks it, so
             * the on-disk size is capped-or-gone either way. */
            JC_CHECK(jc_bg_kill(&m, id) == JC_OK);
            jc_snprintf(spool_path, sizeof(spool_path), "%s/bg-%d-%ld.out",
                        spool_dir, id, (long)getpid());
            if (stat(spool_path, &st) == 0) {
                if ((long)st.st_size <= JC_BG_SPOOL_MAX) {
                    spool_capped_or_gone = 1; /* capped by the reap drain */
                }
            } else {
                spool_capped_or_gone = 1; /* unlinked by reap */
            }
            JC_CHECK(spool_capped_or_gone);

            /* The first (and only) read is at exit: the head survived, the
             * middle was dropped (MARKTAIL at ~520 KiB is not in the 8 KiB
             * tail ring), and the model was told. */
            wait_exit_long(&m, id, &sb, 500);
            JC_CHECK(strstr(sb.data ? sb.data : "", "MARKHEAD") != NULL);
            JC_CHECK(strstr(sb.data ? sb.data : "", "MARKTAIL") == NULL);
            JC_CHECK(strstr(sb.data ? sb.data : "", "[output truncated]")
                     != NULL);
        }
    }

#ifdef JC_FAULT
    /* L-27 (M984) deterministic red-first: the read->O_TRUNC window. JICHI_FAULT
     * _BGREWRITE_PAUSE_MS makes drain() sleep in exactly the window the old
     * mid-run rewrite destroyed (between reading the last cap bytes and the
     * O_TRUNC open). The child writes >512 KiB (so a drain past the cap would
     * rewrite), then `sleep 0.5; echo L27_VERDICT`: the verdict lands WHILE the
     * parent is paused in the window. With the gate REMOVED, that mid-run drain
     * O_TRUNCs the file after the pause and erases L27_VERDICT (red). With the
     * gate, the mid-run drain returns before the pause (the child is still
     * RUNNING) and the verdict is only written by the race-free reap drain
     * (green). PAUSE_MS is set on the test process via setenv; the FAULT
     * site is no-op in a normal build, so this block is FAULT-only. */
    {
        int verdict_delivered = 0;

        setenv("JICHI_FAULT_BGREWRITE_PAUSE_MS", "1000", 1);
        jc_sb_clear(&sb);
        id = jc_bg_start(&m,
                         "i=0; while [ $i -lt 130 ]; do "
                         "  yes MARK | head -c 4096; "
                         "  i=$((i+1)); sleep 0.005; "
                         "done; "
                         "sleep 0.5; echo L27_VERDICT");
        JC_CHECK(id > 0);
        if (id > 0) {
            /* wait_exit_long polls jc_bg_read (which drains) every 20 ms while
             * the child runs: the ~0.65 s write window plus the 0.5 s sleep
             * cross the 512 KiB cap mid-run, so a drain reaches the pause
             * while the child is still alive. */
            wait_exit_long(&m, id, &sb, 1000);
            verdict_delivered = (strstr(sb.data ? sb.data : "",
                                        "L27_VERDICT") != NULL);
            JC_CHECK(verdict_delivered);
        }
        unsetenv("JICHI_FAULT_BGREWRITE_PAUSE_MS");
    }

    /* M996 (L-27 follow-up): orphan-.tmp discriminator (P2 bar).
     * M996's temp+rename drain is the ONLY code that ever touches a .tmp file:
     * before its rewrite it unlinks the crash-orphan named <spool>.tmp<pid>
     * (jc_bg.c:397). The lossy in-place drain (6fecbdb6) opens the spool O_TRUNC
     * and writes in place; it NEVER touches a .tmp, so a planted orphan survives.
     * The destructive rewrite leg runs at REAP (after the child is gone), and
     * reap UNLINKS the spool itself in both builds -- so the spool is gone either
     * way, but the planted orphan .tmp (a different filename) is removed iff the
     * lossless drain ran. That is the discriminator. */
    {
        int cap_reached = 0;
        int spool_after = 0;
        int orphan_after = 0;
        char orphan_path[800];
        int st_spool;

        jc_sb_clear(&sb);
        id = jc_bg_start(&m,
                         "i=0; while [ $i -lt 130 ]; do "
                         "  yes MARK | head -c 4096; "
                         "  i=$((i+1)); sleep 0.005; "
                         "done; "
                         "sleep 1; echo M996_MARKER");
        JC_CHECK(id > 0);
        if (id > 0) {
            /* Poll (STAT ONLY) the spool file every 50 ms, up to ~200 iters,
             * until its st_size > JC_BG_SPOOL_MAX. */
            for (iter = 0; iter < 200 && !cap_reached; iter++) {
                jc_snprintf(spool_path, sizeof(spool_path), "%s/bg-%d-%ld.out",
                            spool_dir, id, (long)getpid());
                st_spool = stat(spool_path, &st);
                if (st_spool == 0 && S_ISREG(st.st_mode)) {
                    if ((long)st.st_size > JC_BG_SPOOL_MAX) {
                        cap_reached = 1;
                        break;
                    }
                }
                jc_sleep_ms(50, NULL);
            }
            JC_CHECK(cap_reached);

            /* Plant the orphan: <spool>.tmp<pid> */
            jc_snprintf(orphan_path, sizeof(orphan_path), "%s.tmp%ld",
                        spool_path, (long)getpid());
            JC_CHECK(jc_write_file(orphan_path, "orphan", 6) == JC_OK);

            /* SIGKILL: the child dies, so the final drain at reap takes the
             * REWRITE leg in BOTH builds (state is not RUNNING, so drain
             * performs the temp+rename). */
            JC_CHECK(jc_bg_kill(&m, id) == JC_OK);

            /* Reap, runs the final drain (rewrite leg), unlinks the spool */
            wait_exit_long(&m, id, &sb, 500);

            /* spool_after: was the spool file still present after reap?
             * orphan_after: was the orphan .tmp still present after reap? */
            st_spool = stat(spool_path, &st);
            spool_after = (st_spool == 0 && S_ISREG(st.st_mode));
            orphan_after = (stat(orphan_path, &st) == 0 && S_ISREG(st.st_mode));

            /* Sanity: reap unlinked the spool in both builds (the rewrite leg ran) */
            JC_CHECK(!spool_after);

            /* THE DISCRIMINATOR: the lossless M996 drain unlinked the orphan
             * (via unlink(tmp) before the rename); the lossy 6fec in-place
             * drain did NOT touch .tmp files at all, so orphan_after==1 and
             * this check is RED under 6fec. */
            JC_CHECK(!orphan_after);
        }
    }
#endif

    /* def-324: jc_bg_spool_error() tests. */
    {
        const char *err;

        /* Start should clear the error buffer. */
        err = jc_bg_spool_error();
        JC_CHECK(err[0] == '\0');

        /* Start a valid background process - error should remain empty. */
        id = jc_bg_start(&m, "echo test");
        if (id > 0) {
            wait_exit(&m, id, &sb);
            err = jc_bg_spool_error();
            JC_CHECK(err[0] == '\0');
        }
    }

    /* def-324: M970 test_spool_error_names_path -- the spool-open failure
     * error must name the path, not print an empty string. Create a read-only
     * spool directory so open(O_CREAT|O_TRUNC) fails with EACCES, then verify
     * the error message contains the path pattern. */
    {
        char bgspool_path[600];
        const char *err;

        jc_snprintf(bgspool_path, sizeof(bgspool_path), "%s/.jichi.d/bgspool",
                    home_tmp);

        /* Root (CAP_DAC_OVERRIDE) opens a 0555 dir anyway, so the forced
         * failure (and the id<0 assertion that follows) is guarded by
         * geteuid() != 0; the skip is made visible rather than a silent
         * JC_CHECK(1) that would read as a pass. */
        if (geteuid() != 0) {
            /* Force a failure by making the spool directory read-only.
             * chmod 0555 means open(O_CREAT) fails with EACCES. */
            if (chmod(bgspool_path, 0555) == 0) {
                /* Start a background process - the open(O_CREAT) will fail */
                id = jc_bg_start(&m, "echo test");
                /* The start must fail (id < 0) */
                JC_CHECK(id < 0);
                if (id < 0) {
                    /* The error should contain the spool FILENAME pattern */
                    err = jc_bg_spool_error();
                    JC_CHECK(err != NULL);
                    /* The error must contain ".out" (the file extension) */
                    JC_CHECK(strstr(err, ".out") != NULL);
                    /* The error must NOT contain the empty-path shape */
                    JC_CHECK(strstr(err, "failed: :") == NULL);
                }
                /* Restore the spool directory permissions */
                (void)chmod(bgspool_path, 0700);
            } else {
                printf("  SKIP M970 spool-error-names-path: chmod 0555 failed\n");
            }
        } else {
            printf("  SKIP M970 spool-error-names-path: euid 0 opens a 0555 dir (CAP_DAC_OVERRIDE)\n");
        }
    }

    /* An unknown id is reported, not crashed on. */
    jc_sb_clear(&sb);
    JC_CHECK(jc_bg_read(&m, 99999, &sb) == JC_ERR_NOTFOUND);
    JC_CHECK(jc_bg_kill(&m, 99999) == JC_ERR_NOTFOUND);

    /* A long-runner can be killed; afterwards it reads as exited. */
    id = jc_bg_start(&m, "sleep 100");
    JC_CHECK(id > 0);
    if (id > 0) {
        JC_CHECK(jc_bg_kill(&m, id) == JC_OK);
        jc_sb_clear(&sb);
        jc_bg_read(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited") != NULL);
    }

    /* The registry is bounded: once full, start returns 0. */
    {
        int started = 0;
        int full_hit = 0;
        for (i = 0; i < JC_BG_MAX + 3; i++) {
            int r = jc_bg_start(&m, "sleep 100");
            if (r > 0) started++;
            else if (r == 0) full_hit = 1;
        }
        JC_CHECK(started <= JC_BG_MAX);
        JC_CHECK(full_hit == 1);
    }

    jc_sb_free(&sb);
    /* Frees buffers and SIGTERM/KILLs every live child (no zombies under ASan/
     * valgrind). */
    jc_bg_mgr_free(&m);

    /* Finished slots are reclaimed: more than JC_BG_MAX *lifetime* starts
     * succeed as long as prior ones have exited (regression test for the
     * "feature unusable after 8 starts" bug). */
    {
        struct jc_bg_mgr m2;
        struct jc_sb sb2;
        int total = 0;
        jc_bg_mgr_init(&m2);
        jc_sb_init(&sb2);
        for (i = 0; i < JC_BG_MAX + 4; i++) {
            int r = jc_bg_start(&m2, "true"); /* exits immediately */
            JC_CHECK(r > 0);                  /* never "registry full" */
            if (r > 0) {
                total++;
                wait_exit(&m2, r, &sb2);       /* reap so the slot can recycle */
            }
        }
        JC_CHECK(total == JC_BG_MAX + 4);
        jc_sb_free(&sb2);
        jc_bg_mgr_free(&m2);
    }

    if (home_save[0] != '\0') {
        setenv("HOME", home_save, 1);
    }
}
