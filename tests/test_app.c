/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_app.c - jc_app helper functions. */

#include "jc_test.h"
#include "jc_app.h"
#include "jc_reread.h"
#include "jc_mem.h"
#include "jc_path.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

/* jc_app_scratch returns the scratch arena when one is installed, else falls
 * back to the session arena so callers never get a NULL arena (M20a). */
static void test_scratch_accessor(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);
    struct jc_arena *scratch = jc_arena_new(0);

    memset(&app, 0, sizeof(app));
    app.arena = arena;

    /* No scratch installed (the subcommand / test default) => session arena. */
    app.scratch = NULL;
    JC_CHECK(jc_app_scratch(&app) == arena);

    /* Scratch installed (the run-loop case) => scratch arena. */
    app.scratch = scratch;
    JC_CHECK(jc_app_scratch(&app) == scratch);

    jc_arena_free(scratch);
    jc_arena_free(arena);
}

/* A scratch arena stays usable across resets: allocate, reset, allocate again.
 * This is the per-turn cycle M20a relies on (reset at each top-level turn). */
static void test_scratch_reset_cycle(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *p;
    int i;

    for (i = 0; i < 3; i++) {
        /* A chunk larger than one default block, to force block growth. */
        p = (char *)jc_arena_alloc(a, 32 * 1024);
        JC_CHECK(p != NULL);
        memset(p, 'x', 32 * 1024); /* would trip ASan if the alloc were short */
        jc_arena_reset(a);         /* reclaim before the next "turn" */
    }

    /* Still usable after the reset cycle. */
    p = jc_arena_strdup(a, "after-reset");
    JC_CHECK(p != NULL && strcmp(p, "after-reset") == 0);

    jc_arena_free(a);
}

/* M54: with the path fence ON, reads are permitted under a configured reference
 * root while writes stay confined to the workspace.
 *
 * M461: this used <repo>/src and <repo>/include as the two disjoint directories,
 * guarded by "skip rather than false-fail if not run from the repo root". The
 * guard did not work, and could not: jc_path_resolve SUCCEEDS for a path that
 * does not exist as long as its parent does -- that is its documented
 * write-target behaviour -- so off-repo it returned <cwd>/src happily and the
 * fence then denied a path under a root that was not there. Three checks failed
 * on an Android 12 phone, where the binary is pushed without its source tree.
 *
 * Depending on the repo layout also quietly contradicts what `make check-target`
 * is FOR: validating a build on a target you pushed a binary to. The fixtures
 * are now created under $TMPDIR, so the test runs everywhere instead of failing
 * in the one place the suite exists to be run. */
static void test_reference_roots_fence(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);
    char rootc[JC_PATH_MAX];
    char refc[JC_PATH_MAX];
    char *refptr;
    char in_root[JC_PATH_MAX];
    char in_ref[JC_PATH_MAX];
    char mkroot[JC_PATH_MAX];
    char mkref[JC_PATH_MAX];

    /* Two disjoint sibling directories of our own making, under $TMPDIR. */
    jc_snprintf(mkroot, sizeof(mkroot), "%s/jc_fence_ws", jc_test_tmpdir());
    jc_snprintf(mkref, sizeof(mkref), "%s/jc_fence_ref", jc_test_tmpdir());
    if (jc_mkdir_p(mkroot) != JC_OK || jc_mkdir_p(mkref) != JC_OK ||
        jc_path_resolve(mkroot, rootc, sizeof(rootc)) != JC_OK ||
        jc_path_resolve(mkref, refc, sizeof(refc)) != JC_OK) {
        jc_arena_free(arena);
        return; /* no writable TMPDIR: skip, and this guard DOES hold -- the
                 * mkdir must succeed before the resolve is consulted */
    }

    memset(&app, 0, sizeof(app));
    app.arena = arena;
    app.config.path_fence = 1; /* force the fence on regardless of mode */
    jc_vec_init(&app.config.reference_roots, sizeof(char *));
    memcpy(app.root, rootc, strlen(rootc) + 1);
    refptr = jc_arena_strdup(arena, refc);
    jc_vec_push(&app.config.reference_roots, &refptr);

    /* Under the workspace root: reads and writes both allowed. */
    jc_snprintf(in_root, sizeof(in_root), "%s/main.c", rootc);
    JC_CHECK(jc_app_path_denied_ex(&app, in_root, 0) == 0);
    JC_CHECK(jc_app_path_denied_ex(&app, in_root, 1) == 0);

    /* Under the reference root: reads allowed, writes denied. */
    jc_snprintf(in_ref, sizeof(in_ref), "%s/jc_app.h", refc);
    JC_CHECK(jc_app_path_denied_ex(&app, in_ref, 0) == 0);
    JC_CHECK(jc_app_path_denied_ex(&app, in_ref, 1) == 1);
    /* The write-strict wrapper denies the reference-root path too. */
    JC_CHECK(jc_app_path_denied(&app, in_ref) == 1);

    /* Outside both (a repo-root file): reads and writes denied. */
    JC_CHECK(jc_app_path_denied_ex(&app, "Makefile", 0) == 1);
    JC_CHECK(jc_app_path_denied_ex(&app, "Makefile", 1) == 1);

    /* Fence off: nothing is denied, reference roots irrelevant. */
    app.config.path_fence = 0;
    JC_CHECK(jc_app_path_denied_ex(&app, "Makefile", 1) == 0);

    jc_vec_free(&app.config.reference_roots);
    jc_arena_free(arena);
}

/* M146: atomic replace -- new file created, existing file replaced whole,
 * no temp sibling left behind, and the result is owner-only (mkstemp 0600
 * carries through the rename). */
static void test_write_file_atomic(void)
{
    const char *p = jc_test_tmp("jichi_atomic_test.txt");
    char *content = NULL;
    jc_size len = 0;
    struct jc_arena *a = jc_arena_new(0);
    struct stat st;

    remove(p);

    /* Create. */
    JC_CHECK(jc_write_file_atomic(p, "first\n", 6) == JC_OK);
    JC_CHECK(jc_read_file(p, &content, &len, a) == JC_OK);
    JC_CHECK(len == 6 && strcmp(content, "first\n") == 0);

    /* Replace an existing file: whole new content, old gone. */
    JC_CHECK(jc_write_file_atomic(p, "the second version\n", 19) == JC_OK);
    JC_CHECK(jc_read_file(p, &content, &len, a) == JC_OK);
    JC_CHECK(len == 19 && strcmp(content, "the second version\n") == 0);

    /* Owner-only mode carried from mkstemp. */
    JC_CHECK(stat(p, &st) == 0);
    JC_CHECK((st.st_mode & 0077) == 0);

    /* No temp sibling left behind. */
    {
        char tmpname[128];
        jc_snprintf(tmpname, sizeof(tmpname),
                    "%s/jichi_atomic_test.txt.tmp%ld", jc_test_tmpdir(), (long)getpid());
        JC_CHECK(!jc_file_exists(tmpname));
    }

    /* Invalid input is refused, not crashed. */
    JC_CHECK(jc_write_file_atomic(NULL, "x", 1) == JC_ERR_INVALID);

    remove(p);
    jc_arena_free(a);
}

/* The wall-clock timeout kill path (M226). A blocking command that outlasts
 * its timeout is terminated (process group SIGTERM/SIGKILL) with a marker and
 * exit 124; a fast command under the timeout runs normally; timeout 0 is the
 * unbounded default. Uses a finite `sleep` killed sub-second, so it neither
 * hangs the suite nor leaves an orphan -- the portable in-process counterpart
 * to what a hang-spawning smoke driver could not do in a guarded sandbox. */
static void test_run_command_timeout(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);

    memset(&app, 0, sizeof(app)); /* mem budget 0, no cmd delegate, no cb */
    app.arena = arena;

    /* A command that blocks past the 1s cap is killed at ~1s with exit 124. */
    {
        struct jc_sb out;
        int code = -1, trunc = 0;
        double elapsed, t0;
        jc_sb_init(&out);
        /* Measure with the SAME clock the deadline uses (M507). This read
         * gettimeofday() -- CLOCK_REALTIME -- while jc_app_run_command_ex
         * arms its deadline from jc_now_millis(), i.e. CLOCK_MONOTONIC, which
         * does not advance while the system is suspended. On an Android
         * handset that dozes mid-test the two diverge by exactly the
         * suspended interval: the kill still lands after 1s of awake time
         * (code 124 and the marker both pass) while a realtime stopwatch
         * reports 4s+ and only this bound fails. 0, 0, then 1 failure on the
         * same binary and device. The assertion is about jichi's kill being
         * early, not about the host's timekeeping, so it must not be read on
         * a clock the host can jump. */
        t0 = jc_now_millis();
        JC_CHECK(jc_app_run_command_ex(&app, "sleep 5", 4096, 1, &out,
                                       &code, &trunc) == JC_OK);
        elapsed = (jc_now_millis() - t0) / 1000.0;
        JC_CHECK(code == 124);
        JC_CHECK(out.data != NULL && strstr(out.data, "timed out") != NULL);
        JC_CHECK(elapsed < 4.0); /* killed near 1s, nowhere near the 5s sleep */
        jc_sb_free(&out);
    }

    /* A fast command well under the cap runs to completion via the same
     * watched-fork path, exit 0, no timeout marker. */
    {
        struct jc_sb out;
        int code = -1, trunc = 0;
        jc_sb_init(&out);
        JC_CHECK(jc_app_run_command_ex(&app, "printf hi", 4096, 5, &out,
                                       &code, &trunc) == JC_OK);
        JC_CHECK(code == 0);
        JC_CHECK(out.data != NULL && strstr(out.data, "hi") != NULL);
        JC_CHECK(strstr(out.data ? out.data : "", "timed out") == NULL);
        jc_sb_free(&out);
    }

    /* timeout 0 = the unbounded default (the plain popen path). */
    {
        struct jc_sb out;
        int code = -1, trunc = 0;
        jc_sb_init(&out);
        JC_CHECK(jc_app_run_command_ex(&app, "printf ok", 4096, 0, &out,
                                       &code, &trunc) == JC_OK);
        JC_CHECK(code == 0);
        JC_CHECK(out.data != NULL && strstr(out.data, "ok") != NULL);
        jc_sb_free(&out);
    }

    /* No orphan from the killed command (the group was reaped). */
    while (waitpid(-1, NULL, WNOHANG) > 0) { /* drain any strays */ }
    jc_arena_free(arena);
}

/* M231: jc_app_reread_check nudges on a byte-identical re-read, and -- the
 * false-positive guard -- stays silent when the content changed (an edit). */
static void test_reread_check(void)
{
    struct jc_app app;
    struct jc_arena *arena = jc_arena_new(0);
    app.arena = arena;
    jc_vec_init(&app.read_recs, sizeof(struct jc_read_rec));

    /* Whole-file reads (offset 1, limit 0 = "to the end"). */
    /* First read of a path: never a nudge. */
    JC_CHECK(jc_app_reread_check(&app, "a.txt", "hello", 5, 1, 0) == 0);
    /* Identical re-read: nudge. */
    JC_CHECK(jc_app_reread_check(&app, "a.txt", "hello", 5, 1, 0) == 1);
    /* Content changed (an edit): NO nudge -- the false-positive guard. */
    JC_CHECK(jc_app_reread_check(&app, "a.txt", "hello!", 6, 1, 0) == 0);
    /* Identical re-read of the NEW content: nudge again. */
    JC_CHECK(jc_app_reread_check(&app, "a.txt", "hello!", 6, 1, 0) == 1);
    /* A different path is independent: first read, no nudge. */
    JC_CHECK(jc_app_reread_check(&app, "b.txt", "hello", 5, 1, 0) == 0);
    /* NULL app / path are safe no-ops. */
    JC_CHECK(jc_app_reread_check(NULL, "a.txt", "x", 1, 1, 0) == 0);
    JC_CHECK(jc_app_reread_check(&app, NULL, "x", 1, 1, 0) == 0);

    /* --- M287: PAGING is not re-reading. The model reads a large file in
     * ranges -- lines 1-100, then 100-250, then 200-280 -- each a different
     * slice with different bytes. M231 hashed the whole file and kept one record
     * per path, so every page after the first was accused of being identical:
     * 142 firings against 12 genuinely redundant calls on one real project, and
     * an advisory that is usually wrong gets ignored. --- */
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page one", 8, 1, 100) == 0);
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page two", 8, 100, 150) == 0);
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page three", 10, 200, 80) == 0);
    /* Paging back over a range already seen, unchanged: that IS redundant. */
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page one", 8, 1, 100) == 1);
    /* ...and each range keeps its own content history. */
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page two", 8, 100, 150) == 1);
    /* Same range, changed content (an edit landed): silent again. */
    JC_CHECK(jc_app_reread_check(&app, "big.zig", "page 2b", 7, 100, 150) == 0);
    /* Identical bytes at a DIFFERENT offset are not a re-read of that range --
     * a file of repeated lines must not trip the advisory. */
    JC_CHECK(jc_app_reread_check(&app, "dup.txt", "same", 4, 1, 10) == 0);
    JC_CHECK(jc_app_reread_check(&app, "dup.txt", "same", 4, 50, 10) == 0);

    /* The table is bounded: past the cap a new range is simply not recorded, so
     * the failure direction is a MISSED advisory, never a fabricated one. */
    {
        long k;
        for (k = 0; k < JC_READ_RECS_MAX + 50; k++) {
            jc_app_reread_check(&app, "huge.zig", "x", 1, k * 100 + 1, 100);
        }
        JC_CHECK(app.read_recs.len <= (jc_size)JC_READ_RECS_MAX);
    }

    jc_vec_free(&app.read_recs);
    jc_arena_free(arena);
}

/* M622: the newest-.jsonl pick must be a function of directory CONTENT, not of
 * readdir() order. st_mtime is whole seconds (jc_platform_posix.c), so two
 * fixture files written in the same second tie routinely; until M622 a tie
 * kept whichever name the filesystem's hash order listed first, and the second
 * hosted CI run (GitHub Actions run 33101494315) failed on exactly that --
 * ext4 on the runner listed the stale fixture first, while the same tie on the
 * dev box had always listed the fresh one, so three full local gates saw
 * nothing. The comparator is pure so the feed order is controlled HERE,
 * deterministically; a smoke driver cannot force a same-second tie portably. */
static void test_newest_beats(void)
{
    /* the first candidate always beats an empty best */
    JC_CHECK(jc_app_newest_beats("a.jsonl", 100.0, "", -1.0) == 1);
    /* strictly newer wins, whichever way the names sort */
    JC_CHECK(jc_app_newest_beats("a.jsonl", 200.0, "z.jsonl", 100.0) == 1);
    JC_CHECK(jc_app_newest_beats("z.jsonl", 100.0, "a.jsonl", 200.0) == 0);
    /* the tie: the lexicographically greater name wins, fed in EITHER order --
     * both directions asserted, so order-independence is the proven property */
    JC_CHECK(jc_app_newest_beats("clean.jsonl", 100.0,
                                 "aliased.jsonl", 100.0) == 1);
    JC_CHECK(jc_app_newest_beats("aliased.jsonl", 100.0,
                                 "clean.jsonl", 100.0) == 0);
}

void test_app(void)
{
    test_newest_beats();
    test_scratch_accessor();
    test_scratch_reset_cycle();
    test_reference_roots_fence();
    test_write_file_atomic();
    test_run_command_timeout();
    test_reread_check();
}
