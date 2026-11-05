/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_scopededit.c - unit teeth for the D6 scoped edit check (M975).
 *
 * Tests the threshold (allow first N-1, refuse from Nth), N=0 (off),
 * un-owned path, F1 multi-turn, F3 re-baseline, detection, deleted path,
 * and child-inert cases. */

#include "jc_test.h"
#include "jc_scopededit.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Helper: build a minimal jc_app with arena, scratch, temp cwd, and config.scoped_edit_max */
static void minimal_app(struct jc_app *app, struct jc_arena **arena_out,
                        struct jc_arena **scratch_out, const char *cwd,
                        int scoped_edit_max)
{
    struct jc_arena *arena = jc_arena_new(0);
    struct jc_arena *scratch = jc_arena_new(0);

    memset(app, 0, sizeof(*app));
    app->arena = arena;
    app->scratch = scratch;
    memcpy(app->cwd, cwd, strlen(cwd) + 1);
    memcpy(app->root, cwd, strlen(cwd) + 1);
    app->config.scoped_edit_max = scoped_edit_max;
    app->agent_depth = 0;
    app->telemetry = NULL;  /* must tolerate NULL telemetry */

    if (arena_out) *arena_out = arena;
    else jc_arena_free(arena);
    if (scratch_out) *scratch_out = scratch;
    else jc_arena_free(scratch);
}

/* Helper: create a file with content under cwd */
static void write_file(const char *cwd, const char *path, const char *content)
{
    char full[4096];
    FILE *f;

    jc_snprintf(full, sizeof(full), "%s/%s", cwd, path);
    f = fopen(full, "wb");
    if (!JC_REQUIRE(f != NULL)) return;
    fprintf(f, "%s", content);
    fclose(f);
}

/* Helper: change a file's content on disk (simulating shell edit) */
static void change_file(const char *cwd, const char *path, const char *new_content)
{
    char full[4096];
    FILE *f;

    jc_snprintf(full, sizeof(full), "%s/%s", cwd, path);
    f = fopen(full, "wb");
    if (!JC_REQUIRE(f != NULL)) return;
    fprintf(f, "%s", new_content);
    fclose(f);
}

/* Helper: delete a file on disk (simulating rm) */
static void delete_file(const char *cwd, const char *path)
{
    char full[4096];
    jc_snprintf(full, sizeof(full), "%s/%s", cwd, path);
    unlink(full);
}

/* Forward declarations for test functions */
static void test_normalisation_spellings(void);

/* ------------------------------------------------------------
 * Case 1: TRIP THRESHOLD - allow-first-(N-1) refuse-from-Nth
 * At N=1 (1st edit refused), N=2 (1st allowed, 2nd refused),
 * N=3 (1st+2nd allowed, 3rd refused)
 * ----------------------------------------------------------- */

static void test_trip_threshold_n1(void)
{
    /* N=1: first edit is refused */
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_n1");
    const char *msg;
    int ret;

    /* Setup temp dir */
    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 1);

    /* Turn start + owner */
    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* First pre_write should be REFUSED (N=1: from 1st) */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "a.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    /* Clean up */
    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

static void test_trip_threshold_n2(void)
{
    /* N=2: first allowed, second refused */
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_n2");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* First pre_write: allowed (NULL) */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);

    /* Second pre_write: refused */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "a.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

static void test_trip_threshold_n3(void)
{
    /* N=3: first two allowed, third refused */
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_n3");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 3);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* First two: allowed */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);

    /* Third: refused */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "a.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 2: N=0 (scoped_edit_max=0) - all off, 5 pre_writes all NULL
 * ----------------------------------------------------------- */

static void test_n_zero_off(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_n0");
    const char *msg;
    int ret;
    int i;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 0);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* 5 pre_writes on owned path: all NULL (off) */
    for (i = 0; i < 5; i++) {
        msg = jc_scopededit_pre_write(&app, "a.txt");
        JC_CHECK(msg == NULL);
    }

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 3: UN-OWNED path - owner() never called; 5 pre_writes all NULL
 * ----------------------------------------------------------- */

static void test_unowned_path(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_unowned");
    const char *msg;
    int ret;
    int i;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    /* NO owner() call - path is not owned */
    write_file(cwd, "unowned.txt", "base\n");

    /* 5 pre_writes on unowned path: all NULL (check 2) */
    for (i = 0; i < 5; i++) {
        msg = jc_scopededit_pre_write(&app, "unowned.txt");
        JC_CHECK(msg == NULL);
    }

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 4: F1 multi-turn - arena reset between turns
 * ----------------------------------------------------------- */

static void test_f1_multi_turn(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_f1");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    /* Turn 1 */
    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* Force scratch arena reset (like agent loop does at turn start) */
    jc_arena_reset(scratch);

    /* Turn 2 - the owned set should be FRESH, 'a' is no longer owned */
    jc_scopededit_turn_start(&app);

    /* pre_write on 'a' should be NULL (not owned in turn 2) */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);

    /* owner('a') again + the N=2 trip must work from a clean count */
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* First edit allowed */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);

    /* Second edit refused */
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "a.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 5: F3 re-baseline - post_write after allowed edit
 * ----------------------------------------------------------- */

static void test_f3_re_baseline(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_f3");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "p.txt", "x\n");
    jc_scopededit_owner(&app, "p.txt", 1, "x\n", 2);

    /* Allowed edit */
    msg = jc_scopededit_pre_write(&app, "p.txt");
    JC_CHECK(msg == NULL);

    /* Simulate the write by changing file on disk */
    change_file(cwd, "p.txt", "y\n");

    /* post_write should re-baseline the snapshot */
    jc_scopededit_post_write(&app, "p.txt");

    /* post_tool should return NULL (the lead's own edit is not a detection) */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg == NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 6: DETECTION - shell edit reported once, then re-baselined
 * ----------------------------------------------------------- */

static void test_detection(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_detect");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "p.txt", "x\n");
    jc_scopededit_owner(&app, "p.txt", 1, "x\n", 2);

    /* Simulate shell edit (sed -i style) */
    change_file(cwd, "p.txt", "y\n");

    /* post_tool should report the change */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "changed outside the file tools") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "p.txt") != NULL);

    /* A SECOND post_tool returns NULL (re-baselined) */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg == NULL);

    /* Change disk again, a THIRD post_tool reports again */
    change_file(cwd, "p.txt", "z\n");
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "changed outside the file tools") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "p.txt") != NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 7: DELETED owned path - post_tool reports it (F5c)
 * ----------------------------------------------------------- */

static void test_deleted_path(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_deleted");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "p.txt", "content\n");
    jc_scopededit_owner(&app, "p.txt", 1, "content\n", 8);

    /* Delete the file (simulating rm) */
    delete_file(cwd, "p.txt");

    /* post_tool should report the deletion */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "changed outside the file tools") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "p.txt") != NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 8: CHILD inert - agent_depth=1 makes everything NULL
 * ----------------------------------------------------------- */

static void test_child_inert(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_child");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    /* Set agent_depth = 1 (child) */
    app.agent_depth = 1;

    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* pre_write past the limit: NULL (child is inert) */
    jc_scopededit_pre_write(&app, "a.txt");
    jc_scopededit_pre_write(&app, "a.txt");
    msg = jc_scopededit_pre_write(&app, "a.txt");
    JC_CHECK(msg == NULL);

    /* post_tool: NULL (child is inert) */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg == NULL);

    /* Reset to 0 */
    app.agent_depth = 0;

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 11: TWO changed paths in one post_tool (the arena-free)
 * This test sets up TWO owned files and changes BOTH on disk,
 * then calls jc_scopededit_post_tool ONCE. In the UNFIXED code,
 * this triggers the append branch which does `free((void*)notice)`
 * on arena memory, causing ASan invalid free.
 * ----------------------------------------------------------- */

static void test_two_changed_paths_one_call(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_two_paths");
    const char *msg;
    int ret;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);

    /* Create two owned files with baselines */
    write_file(cwd, "a.txt", "base a\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base a\n", 7);

    write_file(cwd, "b.txt", "base b\n");
    jc_scopededit_owner(&app, "b.txt", 2, "base b\n", 7);

    /* Change BOTH files on disk (simulating shell edits) */
    change_file(cwd, "a.txt", "changed a\n");
    change_file(cwd, "b.txt", "changed b\n");

    /* post_tool should report BOTH changes in ONE notice */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg != NULL);
    /* Verify the notice contains both paths - use null-guard form */
    JC_CHECK(strstr(msg ? msg : "", "a.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "b.txt") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "changed outside the file tools") != NULL);

    /* A second post_tool should return NULL (both are re-baselined) */
    msg = jc_scopededit_post_tool(&app);
    JC_CHECK(msg == NULL);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}

/* ------------------------------------------------------------
 * Case 12: FOOTPRINT: post_tool does not grow the turn arena per call
 * This test proves that the footprint of app->scratch does NOT
 * grow unboundedly with each post_tool call. The per-call reads
 * go to tool_scratch (reset per call), not scratch (turn-lived).
 * ----------------------------------------------------------- */

static void test_footprint_post_tool_no_growth_per_call(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL, *tool_arena = NULL;
    const char *cwd = jc_test_tmp("scoped_footprint");
    const char *msg;
    int ret;
    int i;
    char file_content[1024];
    jc_size before, after;

    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 5);
    /* CRUCIAL: set up a separate tool_scratch distinct from scratch */
    tool_arena = jc_arena_new(0);
    app.tool_scratch = tool_arena;

    jc_scopededit_turn_start(&app);

    /* Create 5 owned files, each ~1 KiB of content */
    memset(file_content, 'x', sizeof(file_content) - 1);
    file_content[sizeof(file_content) - 1] = '\0';

    for (i = 0; i < 5; i++) {
        char path[32];
        jc_snprintf(path, sizeof(path), "file%d.txt", i);
        write_file(cwd, path, file_content);
        jc_scopededit_owner(&app, path, 1, file_content, sizeof(file_content) - 1);
    }

    /* Record the arena usage before the loop */
    before = jc_arena_used(scratch, NULL);

    /* Loop 50 times: reset tool_scratch (mimic agent loop), call post_tool
     * NO on-disk change, so nothing re-baselines */
    for (i = 0; i < 50; i++) {
        jc_arena_reset(tool_arena);  /* mimics per-tool-call reset */
        msg = jc_scopededit_post_tool(&app);  /* no on-disk change */
        /* Should return NULL (nothing changed) */
        JC_CHECK(msg == NULL);
    }

    /* Record the arena usage after the loop */
    after = jc_arena_used(scratch, NULL);

    /* The growth should be BOUNDED SMALL (per-call reads go to tool_scratch) */
    JC_CHECK(after - before < 64);

    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
    jc_arena_free(tool_arena);
}

/* ------------------------------------------------------------
 * Test entry point
 * ----------------------------------------------------------- */

void test_scopededit(void)
{
    printf("  case 1: TRIP THRESHOLD N=1\n");
    test_trip_threshold_n1();

    printf("  case 2: TRIP THRESHOLD N=2\n");
    test_trip_threshold_n2();

    printf("  case 3: TRIP THRESHOLD N=3\n");
    test_trip_threshold_n3();

    printf("  case 4: N=0 (off)\n");
    test_n_zero_off();

    printf("  case 5: UN-OWNED path\n");
    test_unowned_path();

    printf("  case 6: F1 multi-turn\n");
    test_f1_multi_turn();

    printf("  case 7: F3 re-baseline\n");
    test_f3_re_baseline();

    printf("  case 8: DETECTION\n");
    test_detection();

    printf("  case 9: DELETED owned path\n");
    test_deleted_path();

    printf("  case 10: CHILD inert\n");
    test_child_inert();

    printf("  case 11: TWO changed paths in one post_tool (the arena-free)\n");
    test_two_changed_paths_one_call();

    printf("  case 12: FOOTPRINT: post_tool does not grow the turn arena per call\n");
    test_footprint_post_tool_no_growth_per_call();

    printf("  case 13: NORMALISATION spellings\n");
    test_normalisation_spellings();
}

/* ------------------------------------------------------------
 * Case 13: NORMALISATION spellings - different spellings of same path
 * Proves that ./a.txt, a.txt, and <root>/a.txt all canonicalise
 * to the same form and share ONE entry, not two.
 * ----------------------------------------------------------- */

static void test_normalisation_spellings(void)
{
    struct jc_app app;
    struct jc_arena *arena = NULL, *scratch = NULL;
    const char *cwd = jc_test_tmp("scoped_norm");
    const char *msg;
    char abs[4096];
    int ret;

    /* Setup temp dir */
    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    minimal_app(&app, &arena, &scratch, cwd, 2);

    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    /* OWN it by the PLAIN spelling: a.txt */
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* --- Proof 1: pre_write on RELATIVE-DOT spelling (./a.txt) is 1st edit --- */
    msg = jc_scopededit_pre_write(&app, "./a.txt");
    JC_CHECK(msg == NULL);  /* 1st edit, allowed */

    /* --- Proof 2: pre_write on ABSOLUTE spelling trips the N=2 limit --- */
    jc_snprintf(abs, sizeof(abs), "%s/a.txt", cwd);
    msg = jc_scopededit_pre_write(&app, abs);
    /* 2nd edit, REFUSED on the same entry (a.txt and ./a.txt share one entry) */
    JC_CHECK(msg != NULL);
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    /* --- Proof 3: dot spelling shares the SAME entry as plain spelling --- */
    /* Re-setup: own only a.txt, then pre_write("./a.txt") twice. A fresh
     * app on the same locals: free the first pair before re-pointing them
     * (the re-setup used to leak it -- the SAN gate's six allocations). */
    jc_test_rm_rf(cwd);
    ret = jc_mkdir_p(cwd);
    JC_CHECK(ret == JC_OK);

    jc_arena_free(arena);
    jc_arena_free(scratch);
    minimal_app(&app, &arena, &scratch, cwd, 2);
    jc_scopededit_turn_start(&app);
    write_file(cwd, "a.txt", "base\n");
    jc_scopededit_owner(&app, "a.txt", 1, "base\n", 5);

    /* pre_write("./a.txt") first time: should be 1st edit of a.txt (allowed) */
    msg = jc_scopededit_pre_write(&app, "./a.txt");
    JC_CHECK(msg == NULL);  /* 1st edit of shared entry */

    /* pre_write("./a.txt") second time: should be 2nd edit (refused) */
    msg = jc_scopededit_pre_write(&app, "./a.txt");
    JC_CHECK(msg != NULL);  /* 2nd edit, REFUSED */
    JC_CHECK(strstr(msg ? msg : "", "worker-owned path") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "follow-up task") != NULL);
    JC_CHECK(strstr(msg ? msg : "", "jichi attempts") != NULL);

    /* Clean up */
    jc_test_rm_rf(cwd);
    jc_arena_free(arena);
    jc_arena_free(scratch);
}
