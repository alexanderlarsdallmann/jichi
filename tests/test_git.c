/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_git.c - tests for the git tools: pure helpers (hermetic) + a guarded
 * integration check against the repo's real git when available. */

#include "jc_test.h"
#include "jc_snprintf.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_clamp(void)
{
    JC_CHECK(jc_git_clamp_max(0) == 20);
    JC_CHECK(jc_git_clamp_max(-5) == 20);
    JC_CHECK(jc_git_clamp_max(1) == 1);
    JC_CHECK(jc_git_clamp_max(50) == 50);
    JC_CHECK(jc_git_clamp_max(100) == 100);
    JC_CHECK(jc_git_clamp_max(1000) == 100);
}

static void test_blame_range(void)
{
    char buf[40];

    JC_CHECK(jc_git_blame_range(0, 0, buf, sizeof(buf)) == 0);
    JC_CHECK(buf[0] == '\0');

    JC_CHECK(jc_git_blame_range(10, 20, buf, sizeof(buf)) == 1);
    JC_CHECK(strcmp(buf, "10,20") == 0);

    /* start only (no/invalid end) => "start," (to end of file) */
    JC_CHECK(jc_git_blame_range(10, 0, buf, sizeof(buf)) == 1);
    JC_CHECK(strcmp(buf, "10,") == 0);
    JC_CHECK(jc_git_blame_range(10, 5, buf, sizeof(buf)) == 1);
    JC_CHECK(strcmp(buf, "10,") == 0);
}

/* Guarded integration: only when run inside a git repo (the suite runs from the
 * project root). Exercises the real fork/exec git path; needs no network. */
static void test_integration(void)
{
    struct jc_arena *a = jc_arena_new(1 << 16);
    struct jc_app app;
    const struct jc_tool *t;
    struct jc_tool_result res;
    cJSON *args;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    if (getcwd(app.cwd, sizeof(app.cwd)) == NULL) {
        jc_arena_free(a);
        return;
    }
    if (!jc_tool_git_available(app.cwd)) {
        printf("  (skipping git integration: not a git repo)\n");
        jc_arena_free(a);
        return;
    }

    /* git_status */
    t = jc_tool_git_status();
    args = cJSON_CreateObject();
    res.content = NULL;
    res.is_error = 0;
    t->run(args, &res, &app);
    JC_CHECK(res.content != NULL);
    JC_CHECK(res.is_error == 0);
    free(res.content);
    cJSON_Delete(args);

    /* git_log */
    t = jc_tool_git_log();
    args = cJSON_CreateObject();
    cJSON_AddNumberToObject(args, "max", 3);
    res.content = NULL;
    res.is_error = 0;
    t->run(args, &res, &app);
    JC_CHECK(res.content != NULL && strlen(res.content) > 0);
    JC_CHECK(res.is_error == 0);
    free(res.content);
    cJSON_Delete(args);

    jc_arena_free(a);
}

/* Run one mutating git tool via its run() and return is_error (frees content).
 * `out` (may be NULL) receives a malloc'd copy of the result content. */
static int run_git_tool(const struct jc_tool *t, struct jc_app *app,
                        cJSON *args, char **out)
{
    struct jc_tool_result res;
    int err;
    res.content = NULL;
    res.is_error = 0;
    t->run(args, &res, app);
    err = res.is_error;
    if (out != NULL) {
        *out = NULL;
        if (res.content != NULL) {
            size_t n = strlen(res.content) + 1;
            *out = (char *)malloc(n);
            if (*out != NULL) {
                memcpy(*out, res.content, n);
            }
        }
    }
    free(res.content);
    cJSON_Delete(args);
    return err;
}

/* Guarded integration for the mutating tools (M39), in an ISOLATED temp repo so
 * the project's own repo is never touched. Skips when git is unavailable. */
static void test_mutate(void)
{
    struct jc_arena *a = jc_arena_new(1 << 16);
    struct jc_app app;
    const char *dir = jc_test_tmp("jichi_git_m39");
    cJSON *args;
    char *content;
    FILE *f;

    /* Tool readonly flags: the M39 tools are mutating, the readers are not. */
    JC_CHECK(jc_tool_git_status()->readonly == 1);
    JC_CHECK(jc_tool_git_add()->readonly == 0);
    JC_CHECK(jc_tool_git_commit()->readonly == 0);
    JC_CHECK(jc_tool_git_branch()->readonly == 0);
    JC_CHECK(jc_tool_git_stash()->readonly == 0);

    {
        char cmd[700];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        system(cmd);
    }
    if (system("command -v git >/dev/null 2>&1") != 0) {
        printf("  (skipping git mutation integration: no git)\n");
        jc_arena_free(a);
        return;
    }
    {
        /* The shell side must use the SAME directory the C side writes
         * to. Leaving a literal /tmp here while fopen() followed $TMPDIR
         * put the fixture outside the repo, so the git checks below
         * silently did not run -- 10 fewer checks and no failure to show
         * for it (M457). */
        char cmd[900];
        jc_snprintf(cmd, sizeof cmd,
                    "mkdir -p %s && cd %s && git init -q && "
                    "git config user.email t@example.com && "
                    "git config user.name tester", dir, dir);
        if (system(cmd) != 0) {
            jc_arena_free(a);
            return;
        }
    }
    f = fopen(jc_test_tmp("jichi_git_m39/x.txt"), "wb");
    if (f != NULL) { fputs("hello\n", f); fclose(f); }

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, dir);
    if (!jc_tool_git_available(app.cwd)) {
        jc_arena_free(a);
        return;
    }

    /* git_add x.txt */
    args = cJSON_CreateObject();
    {
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateString("x.txt"));
        cJSON_AddItemToObject(args, "paths", arr);
    }
    JC_CHECK(run_git_tool(jc_tool_git_add(), &app, args, NULL) == 0);

    /* git_add with neither paths nor all => tool error. */
    JC_CHECK(run_git_tool(jc_tool_git_add(), &app, cJSON_CreateObject(),
                          NULL) == 1);

    /* git_commit -m "init x": succeeds and the summary echoes the message. */
    args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "message", "init x");
    JC_CHECK(run_git_tool(jc_tool_git_commit(), &app, args, &content) == 0);
    JC_CHECK(content != NULL && strstr(content, "init x") != NULL);
    free(content);

    /* Blank commit message is refused before running git. */
    args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "message", "   ");
    JC_CHECK(run_git_tool(jc_tool_git_commit(), &app, args, NULL) == 1);

    /* git_branch create:true makes + switches to a new branch. */
    args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "name", "feature");
    cJSON_AddBoolToObject(args, "create", 1);
    JC_CHECK(run_git_tool(jc_tool_git_branch(), &app, args, NULL) == 0);

    /* Modify, stash, pop -- both succeed. */
    f = fopen(jc_test_tmp("jichi_git_m39/x.txt"), "wb");
    if (f != NULL) { fputs("changed\n", f); fclose(f); }
    JC_CHECK(run_git_tool(jc_tool_git_stash(), &app, cJSON_CreateObject(),
                          NULL) == 0);
    args = cJSON_CreateObject();
    cJSON_AddBoolToObject(args, "pop", 1);
    JC_CHECK(run_git_tool(jc_tool_git_stash(), &app, args, NULL) == 0);

    /* Stash with a label: exercises the `save` spelling (git-2.11-safe --
     * `push -m` needs git 2.13; the V2f stretch row caught this). */
    f = fopen(jc_test_tmp("jichi_git_m39/x.txt"), "wb");
    if (f != NULL) { fputs("changed again\n", f); fclose(f); }
    args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "message", "labeled stash");
    JC_CHECK(run_git_tool(jc_tool_git_stash(), &app, args, NULL) == 0);
    args = cJSON_CreateObject();
    cJSON_AddBoolToObject(args, "pop", 1);
    JC_CHECK(run_git_tool(jc_tool_git_stash(), &app, args, NULL) == 0);

    {
        char cmd[700];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        system(cmd);
    }
    jc_arena_free(a);
}

void test_git(void)
{
    test_clamp();
    test_blame_range();
    test_integration();
    test_mutate();
}
