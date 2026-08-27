/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_tool.c - exercises the built-in tools against a temp directory. */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_path.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_json.h"
#include "../src/tools/tool_util.h"   /* module-private, as test_provider.c does */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jc_snprintf.h"

static void setup_app(struct jc_app *app, struct jc_arena *a,
                      struct jc_tool_registry *reg)
{
    memset(app, 0, sizeof(*app));
    app->arena = a;
    jc_vec_init(&app->read_files, sizeof(char *));
    jc_vec_init(&app->read_recs, sizeof(struct jc_read_rec));
    app->auto_approve = 1;
    app->readonly = 0;
    jc_tool_registry_init(reg);
    jc_tool_register_builtins(reg);
    app->tools = reg;
    strcpy(app->cwd, jc_test_tmpdir());
}

/* M168: a stringified number must not fall through to the default.
 *
 * A live run against the zigodot program had the model send
 * `{"limit": "200.0"}` to read_file. `limit`'s default is 0, which means "no
 * limit", so each of five such calls returned the WHOLE 172 KB file instead of
 * 200 lines -- adding 40-57k input tokens apiece, re-billed on every later call
 * of a cacheless backend, and driving six history compactions in one 29-call
 * run. A type mismatch must never silently select the most expensive behaviour.
 * Exercised through the real read_file tool, not just the accessor, so the fix
 * is proven where it actually bit. */
static void test_stringified_numeric_args(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    char path[512];
    FILE *f;
    int i;

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof path, "%s/jichi_m168_%d.txt", jc_test_tmpdir(), (int)getpid());
    f = fopen(path, "w");
    JC_CHECK(f != NULL);
    if (f == NULL) {
        jc_arena_free(a);
        return;
    }
    for (i = 1; i <= 500; i++) {
        fprintf(f, "line %d\n", i);
    }
    fclose(f);

    {
        char args[700];
        /* the exact shape the live model emitted: a stringified float */
        jc_snprintf(args, sizeof args,
                    "{\"path\":\"%s\",\"limit\":\"3.0\"}", path);
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(!res.is_error);
        if (res.content != NULL) {
            /* 3 lines honoured -> line 4 is absent. Pre-M168 the default (0 =
             * no limit) returned all 500. */
            JC_CHECK(strstr(res.content, "line 1") != NULL);
            JC_CHECK(strstr(res.content, "line 3") != NULL);
            JC_CHECK(strstr(res.content, "line 4") == NULL);
        }
        jc_tool_result_free(&res);

        /* a plain integer string works too */
        jc_snprintf(args, sizeof args,
                    "{\"path\":\"%s\",\"limit\":\"2\"}", path);
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(!res.is_error);
        if (res.content != NULL) {
            JC_CHECK(strstr(res.content, "line 3") == NULL);
        }
        jc_tool_result_free(&res);

        /* prose is NOT a number: fall through to the default rather than guess */
        jc_snprintf(args, sizeof args,
                    "{\"path\":\"%s\",\"limit\":\"200 lines\"}", path);
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(!res.is_error);
        if (res.content != NULL) {
            JC_CHECK(strstr(res.content, "line 500") != NULL);
        }
        jc_tool_result_free(&res);
    }

    remove(path);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* M172: arguments nested under the tool's own name.
 *
 * A model emitted {"edit_file": {"path": ..., "old_string": ...}} instead of
 * {"path": ..., "old_string": ...}. That is VALID JSON, so the M148 syntax repair
 * never sees it -- the parse succeeds and the tool finds every required argument
 * missing. Observed live on the zigodot program (finding F8).
 *
 * The rule is deliberately narrow: exactly one member, keyed by the tool's OWN
 * name, wrapping an object. Everything else must pass through untouched. */
static void test_unwrap_self_named(void)
{
    cJSON *o;
    cJSON *inner;

    /* --- the shape that must unwrap --- */
    o = cJSON_Parse("{\"edit_file\":{\"path\":\"a.c\",\"old_string\":\"x\"}}");
    JC_CHECK(o != NULL);
    inner = jc_tool_unwrap_self_named(o, "edit_file");
    JC_CHECK(inner != NULL);
    if (inner != NULL) {
        JC_CHECK_STR(jc_json_get_str(inner, "path", NULL), "a.c");
        JC_CHECK_STR(jc_json_get_str(inner, "old_string", NULL), "x");
        cJSON_Delete(inner);
    }
    /* the outer object is left intact -- the caller still owns and deletes it */
    JC_CHECK(cJSON_GetObjectItem(o, "edit_file") != NULL);
    cJSON_Delete(o);

    /* --- shapes that must NOT unwrap --- */
    o = cJSON_Parse("{\"path\":\"a.c\"}");            /* already correct */
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);

    /* one member, but not the tool's name -- could be a real argument */
    o = cJSON_Parse("{\"todos\":{\"a\":1}}");
    JC_CHECK(jc_tool_unwrap_self_named(o, "todowrite") == NULL);
    cJSON_Delete(o);

    /* the tool's name present but not alone -- ambiguous, leave it */
    o = cJSON_Parse("{\"edit_file\":{\"path\":\"a\"},\"path\":\"b\"}");
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);

    /* named after the tool but not wrapping an object */
    o = cJSON_Parse("{\"edit_file\":\"a.c\"}");
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);
    o = cJSON_Parse("{\"edit_file\":[1,2]}");
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);

    o = cJSON_Parse("{}");
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);
    o = cJSON_Parse("[1,2]");
    JC_CHECK(jc_tool_unwrap_self_named(o, "edit_file") == NULL);
    cJSON_Delete(o);

    /* degenerate inputs are safe */
    JC_CHECK(jc_tool_unwrap_self_named(NULL, "edit_file") == NULL);
    o = cJSON_Parse("{\"edit_file\":{}}");
    JC_CHECK(jc_tool_unwrap_self_named(o, NULL) == NULL);
    JC_CHECK(jc_tool_unwrap_self_named(o, "") == NULL);
    cJSON_Delete(o);
}

/* The same defect through the REAL dispatch, so the fix is proven where it bit
 * and not only in the helper. */
static void test_unwrap_through_execute(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    char path[512];
    char args[900];
    FILE *f;

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof path, "%s/jichi_m172_%d.txt", jc_test_tmpdir(), (int)getpid());
    f = fopen(path, "w");
    JC_CHECK(f != NULL);
    if (f == NULL) {
        jc_arena_free(a);
        return;
    }
    fputs("hello world\n", f);
    fclose(f);

    /* read first, so edit_file's read-before-edit guard is satisfied */
    jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    jc_tool_result_free(&res);

    /* the double-wrapped call the model actually produced */
    jc_snprintf(args, sizeof args,
                "{\"edit_file\":{\"path\":\"%s\",\"old_string\":\"world\","
                "\"new_string\":\"there\"}}", path);
    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "edit_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    jc_tool_result_free(&res);

    /* and it really edited the file */
    jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    if (res.content != NULL) {
        JC_CHECK(strstr(res.content, "hello there") != NULL);
    }
    jc_tool_result_free(&res);

    remove(path);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* M287: the re-read advisory, exercised through the real read_file tool.
 *
 * M231 hashed the WHOLE FILE and kept one record per path, so a model paging a
 * large file was told every page after the first was "byte-for-byte identical to
 * your earlier read" -- 142 firings against 12 genuinely redundant calls on one
 * measured project. Two behaviours are asserted here that the accessor-level test
 * cannot reach, because they depend on WHICH bytes the tool hands to the check:
 * paging stays silent, and an unchanged slice of a file that changed ELSEWHERE
 * still fires (the old whole-file hash missed that one). */
static void test_reread_advisory_paging(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *path = jc_test_tmp("jichi_reread_pages.txt");
    const char *NOTE = "byte-for-byte identical";
    struct jc_sb body;
    char args[512];
    int i;

    setup_app(&app, a, &reg);

    /* A 300-line file, every line distinct so no two slices collide. */
    jc_sb_init(&body);
    for (i = 1; i <= 300; i++) {
        jc_sb_append_fmt(&body, "line %d content\n", i);
    }
    JC_CHECK(jc_write_file(path, body.data, body.len) == JC_OK);

    /* Page through it: three different ranges, none of them a re-read. */
    jc_snprintf(args, sizeof(args), "{\"path\":\"%s\",\"limit\":100}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) == NULL);
    jc_tool_result_free(&res);

    jc_snprintf(args, sizeof(args),
                "{\"path\":\"%s\",\"offset\":100,\"limit\":150}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) == NULL); /* the M231 false positive */
    jc_tool_result_free(&res);

    jc_snprintf(args, sizeof(args),
                "{\"path\":\"%s\",\"offset\":200,\"limit\":80}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) == NULL);
    jc_tool_result_free(&res);

    /* Re-request a range already seen, unchanged: that IS redundant. */
    jc_snprintf(args, sizeof(args), "{\"path\":\"%s\",\"limit\":100}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) != NULL);
    jc_tool_result_free(&res);

    /* Now edit ONE line (250), which falls inside the 200-279 page and outside
     * the 1-100 page. That single write separates the two hashings cleanly:
     *
     *  - re-reading lines 1-100: the shown bytes are unchanged, so the advisory
     *    is CORRECT. A whole-file hash sees a different file and stays silent --
     *    a real repeat the old keying missed.
     *  - re-reading lines 200-279: the shown bytes did change, so it stays
     *    silent. The post-edit guard still holds, now per range. */
    jc_sb_free(&body);
    jc_sb_init(&body);
    for (i = 1; i <= 300; i++) {
        if (i == 250) {
            jc_sb_append(&body, "line 250 CHANGED\n");
        } else {
            jc_sb_append_fmt(&body, "line %d content\n", i);
        }
    }
    JC_CHECK(jc_write_file(path, body.data, body.len) == JC_OK);

    jc_snprintf(args, sizeof(args), "{\"path\":\"%s\",\"limit\":100}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) != NULL);
    jc_tool_result_free(&res);

    jc_snprintf(args, sizeof(args),
                "{\"path\":\"%s\",\"offset\":200,\"limit\":80}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(!res.is_error);
    JC_CHECK(strstr(res.content, NOTE) == NULL);
    jc_tool_result_free(&res);

    jc_sb_free(&body);
    remove(path);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* M286: is_error alone cannot tell "the tool broke" from "the command the tool
 * ran said no". Routing consumed the conflated flag, so escalateOnError fired on
 * every red build in a fix-forward loop -- 300 of 447 tool errors on one measured
 * project -- and had to be disabled, which left the strong tier unreachable
 * (routes=0 across 174 turns). */
static void test_result_is_malfunction(void)
{
    struct jc_tool_result r;

    /* Zero the whole struct: with three fields now, setting them individually
     * left `policy_refusal` reading stack garbage and the outcome depended on
     * what happened to be there (M291 -- adding a field to a result struct
     * silently broke a test that had passed for two milestones). */
    memset(&r, 0, sizeof(r));

    /* A red gate: the tool ran the command fine, the command reported failure. */
    r.content = NULL; r.is_error = 1; r.exit_status = 1;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);
    r.is_error = 1; r.exit_status = 127;   /* command not found, as reported BY sh */
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);

    /* A real malfunction: no command ran, so exit_status keeps the -1 that
     * jc_tool_execute initialises (unknown tool, bad args, or a command that
     * could not be started at all). A fence denial ALSO leaves -1 but is a
     * policy refusal, not a malfunction -- see the M291 block below. */
    r.is_error = 1; r.exit_status = -1;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 1);

    /* M291: a POLICY refusal is not a malfunction. The tool worked; the request
     * was out of bounds, and the stronger model would meet the identical fence --
     * so escalating on it is pure cost. Found live: the first `route` event a
     * newly fenced project ever produced was reason:tool_error on a denied read. */
    r.is_error = 1; r.exit_status = -1; r.policy_refusal = 1;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);
    /* ...while the same shape WITHOUT the flag still escalates (a real
     * malfunction: unknown tool, bad arguments, command not found). */
    r.policy_refusal = 0;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 1);
    /* The flag is only meaningful on an error; a success stays a success. */
    r.is_error = 0; r.policy_refusal = 1;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);
    r.policy_refusal = 0;

    /* Success is never a malfunction, whichever exit_status it carries. */
    r.is_error = 0; r.exit_status = 0;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);
    r.is_error = 0; r.exit_status = -1;
    JC_CHECK(jc_tool_result_is_malfunction(&r) == 0);

    JC_CHECK(jc_tool_result_is_malfunction(NULL) == 0);
}

/* M289: the model copies jichi's own argument-elision placeholder back as
 * arguments. M218 puts a small valid JSON object in the arguments slot of
 * history, the model reads that slot as an example of what a call to this tool
 * looks like, and it duly reproduced the shape -- 18 of 19 argument-shape
 * failures on one measured run, each a full uncached round-trip answered with a
 * generic "'path' ... are required" that explained nothing. Nothing can be
 * repaired (the real arguments are gone by construction), so the requirement is
 * an error the model can act on. */
static void test_elided_placeholder_args(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;

    setup_app(&app, a, &reg);

    /* The marker as jichi writes it, path included. */
    jc_tool_execute(&reg, "edit_file",
        "{\"elided\":\"PLACEHOLDER, not arguments: 4023 bytes of arguments "
        "were dropped\",\"path\":\"src/gdscript/vm.zig\"}", &res, &app);
    JC_CHECK(res.is_error);
    /* Names the cause, not just the missing fields. */
    JC_CHECK(strstr(res.content, "not arguments") != NULL);
    JC_CHECK(strstr(res.content, "placeholder") != NULL);
    /* Tells the model which file to re-send arguments for. */
    JC_CHECK(strstr(res.content, "src/gdscript/vm.zig") != NULL);
    /* And NOT the generic shape complaint, which taught nothing. */
    JC_CHECK(strstr(res.content, "are required") == NULL);
    jc_tool_result_free(&res);

    /* Without a path it still diagnoses, just without the filename. */
    jc_tool_execute(&reg, "todo_write",
        "{\"elided\":\"PLACEHOLDER, not arguments: 892 bytes\"}", &res, &app);
    JC_CHECK(res.is_error);
    JC_CHECK(strstr(res.content, "not arguments") != NULL);
    jc_tool_result_free(&res);

    /* A real call is unaffected -- the guard keys on a parameter name no tool
     * declares, the same reasoning M172's self-named unwrap uses. */
    {
        char args[512];
        const char *p = jc_test_tmp("jichi_tool_test/elide.txt");
        jc_snprintf(args, sizeof(args),
                    "{\"path\":\"%s\",\"content\":\"hello\"}", p);
        jc_tool_execute(&reg, "write_file", args, &res, &app);
        JC_CHECK(!res.is_error);
        jc_tool_result_free(&res);
        remove(p);
    }

    /* A non-string `elided` is not our marker: fall through to normal handling
     * (so a hypothetical future tool with such an argument is not hijacked). */
    jc_tool_execute(&reg, "edit_file", "{\"elided\":123}", &res, &app);
    JC_CHECK(res.is_error);
    JC_CHECK(strstr(res.content, "not arguments") == NULL);
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* M291 through the real dispatch: a fence denial must set policy_refusal, so
 * routing does not escalate on it. Exercised via read_file with the fence on --
 * the exact path that produced the spurious escalation. */
static void test_fence_denial_is_policy(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;

    setup_app(&app, a, &reg);
    /* Fence ON, workspace = /tmp/jichi_tool_test (setup_app sets cwd=/tmp). */
    app.config.path_fence = 1;
    jc_vec_init(&app.config.reference_roots, sizeof(char *));
    {
        char root[JC_PATH_MAX];
        if (jc_path_resolve(jc_test_tmpdir(), root, sizeof(root)) == JC_OK) {
            memcpy(app.root, root, strlen(root) + 1);
        }
    }

    /* A read OUTSIDE the workspace: refused, and refused as POLICY. */
    jc_tool_execute(&reg, "read_file", "{\"path\":\"/etc/hostname\"}",
                    &res, &app);
    JC_CHECK(res.is_error);
    JC_CHECK(res.policy_refusal == 1);
    JC_CHECK(jc_tool_result_is_malfunction(&res) == 0); /* must not escalate */
    jc_tool_result_free(&res);

    /* A read of a MISSING file inside the workspace is a genuine failure, not a
     * policy refusal -- the two must stay distinguishable. */
    {
        char args[600];
        /* Must sit INSIDE the workspace, which now follows $TMPDIR -- a
         * literal /tmp here would be outside it and the fence would
         * refuse the read, turning this into a policy refusal (M457). */
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}",
                    jc_test_tmp("jichi_no_such_file_m291"));
        jc_tool_execute(&reg, "read_file", args, &res, &app);
    }
    JC_CHECK(res.is_error);
    JC_CHECK(res.policy_refusal == 0);
    jc_tool_result_free(&res);

    jc_vec_free(&app.config.reference_roots);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

void test_tool(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;

    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_tool_test");
    char path[256];

    test_stringified_numeric_args();
    test_unwrap_self_named();
    test_unwrap_through_execute();
    test_result_is_malfunction();
    test_reread_advisory_paging();
    test_elided_placeholder_args();
    test_fence_denial_is_policy();

    setup_app(&app, a, &reg);
    sprintf(path, "%s/x.txt", dir);

    /* Registry lookup. */
    JC_CHECK(jc_tool_registry_find(&reg, "read_file") != NULL);
    JC_CHECK(jc_tool_registry_find(&reg, "no_such_tool") == NULL);

    /* write_file creates the file (and its parent dir). */
    {
        char args[512];
        sprintf(args, "{\"path\":\"%s\",\"content\":\"hello world\"}", path);
        jc_tool_execute(&reg, "write_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        jc_tool_result_free(&res);
    }

    /* read_file returns the content. */
    {
        char args[512];
        sprintf(args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        /* read_file now prefixes a line-number gutter ("     1\t..."). */
        JC_CHECK(strstr(res.content, "hello world") != NULL);
        JC_CHECK(strstr(res.content, "1\t") != NULL);
        jc_tool_result_free(&res);
    }

    /* M20c: a configured cap (config.read_max_bytes) truncates the output. */
    {
        char args[512];
        char big[256];
        sprintf(big, "%s/big.txt", dir);
        {
            char wargs[600];
            sprintf(wargs, "{\"path\":\"%s\",\"content\":"
                    "\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\"}", big);
            jc_tool_execute(&reg, "write_file", wargs, &res, &app);
            jc_tool_result_free(&res);
        }
        app.config.read_max_bytes = 10; /* below the 36-byte content */
        sprintf(args, "{\"path\":\"%s\"}", big);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(strstr(res.content, "[output truncated]") != NULL);
        jc_tool_result_free(&res);
        app.config.read_max_bytes = 0; /* restore built-in for later checks */
    }

    /* edit_file replaces the unique occurrence (read guard already satisfied
     * by write/read above). */
    {
        char args[512];
        sprintf(args, "{\"path\":\"%s\",\"old_string\":\"world\","
                      "\"new_string\":\"there\"}", path);
        jc_tool_execute(&reg, "edit_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        /* The result now carries a unified diff of the change. */
        JC_CHECK(strstr(res.content, "@@") != NULL);
        JC_CHECK(strstr(res.content, "-hello world") != NULL);
        JC_CHECK(strstr(res.content, "+hello there") != NULL);
        jc_tool_result_free(&res);

        sprintf(args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(strstr(res.content, "hello there") != NULL);
        jc_tool_result_free(&res);
    }

    /* edit_file refuses when old_string is absent. */
    {
        char args[512];
        sprintf(args, "{\"path\":\"%s\",\"old_string\":\"absent\","
                      "\"new_string\":\"z\"}", path);
        jc_tool_execute(&reg, "edit_file", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        jc_tool_result_free(&res);
    }

    /* list_files includes our file, and marks directories with a trailing '/'. */
    {
        char args[512];
        char sub[300];
        sprintf(sub, "%s/sub", dir);
        jc_mkdir_p(sub);
        sprintf(args, "{\"path\":\"%s\"}", dir);
        jc_tool_execute(&reg, "list_files", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(strstr(res.content, "x.txt") != NULL);
        JC_CHECK(strstr(res.content, "sub/") != NULL); /* dir marker */
        jc_tool_result_free(&res);
    }

    /* run_terminal_command captures output. */
    {
        jc_tool_execute(&reg, "run_terminal_command",
                        "{\"command\":\"echo jichi_marker\"}", &res, &app);
        JC_CHECK(strstr(res.content, "jichi_marker") != NULL);
        jc_tool_result_free(&res);
    }

    /* search_code finds a match. */
    {
        char args[512];
        sprintf(args, "{\"pattern\":\"there\",\"path\":\"%s\"}", dir);
        jc_tool_execute(&reg, "search_code", args, &res, &app);
        JC_CHECK(strstr(res.content, "x.txt") != NULL);
        jc_tool_result_free(&res);
    }

    /* fetch_url is registered and validates its arguments without a network
     * call. */
    {
        JC_CHECK(jc_tool_registry_find(&reg, "fetch_url") != NULL);
        jc_tool_execute(&reg, "fetch_url", "{}", &res, &app);
        JC_CHECK(res.is_error == 1); /* missing url */
        jc_tool_result_free(&res);
        jc_tool_execute(&reg, "fetch_url", "{\"url\":\"ftp://x\"}", &res, &app);
        JC_CHECK(res.is_error == 1); /* bad scheme */
        jc_tool_result_free(&res);
    }

    /* codebase_search is registered; it validates args and reports a clear
     * error when no embedding model is configured (the test app has none). */
    {
        JC_CHECK(jc_tool_registry_find(&reg, "codebase_search") != NULL);
        jc_tool_execute(&reg, "codebase_search", "{}", &res, &app);
        JC_CHECK(res.is_error == 1); /* missing query */
        jc_tool_result_free(&res);
        jc_tool_execute(&reg, "codebase_search",
                        "{\"query\":\"where is main\"}", &res, &app);
        JC_CHECK(res.is_error == 1); /* no embed model configured */
        JC_CHECK(strstr(res.content, "embedding model") != NULL);
        jc_tool_result_free(&res);
    }

    /* readonly mode blocks mutating tools. */
    {
        char args[512];
        app.readonly = 1;
        sprintf(args, "{\"path\":\"%s\",\"content\":\"nope\"}", path);
        jc_tool_execute(&reg, "write_file", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        jc_tool_result_free(&res);
        app.readonly = 0;
    }

    /* build_neutral honors the deny list and the mutating filter. */
    {
        struct jc_permissions perm;
        cJSON *arr;
        cJSON *e;
        int saw_read = 0;
        int saw_write = 0;
        char *deny_name = (char *)malloc(11);

        memset(&perm, 0, sizeof(perm));
        jc_vec_init(&perm.allow, sizeof(char *));
        jc_vec_init(&perm.deny, sizeof(char *));
        strcpy(deny_name, "write_file");
        jc_vec_push(&perm.deny, &deny_name);

        /* Include mutating tools, but deny write_file: it must be omitted. */
        arr = jc_tool_build_neutral(&reg, 1, &perm);
        JC_CHECK(arr != NULL);
        for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
            const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
            if (strcmp(nm, "read_file") == 0) { saw_read = 1; }
            if (strcmp(nm, "write_file") == 0) { saw_write = 1; }
        }
        JC_CHECK(saw_read == 1);
        JC_CHECK(saw_write == 0); /* denied => hidden */
        cJSON_Delete(arr);

        /* include_mutating=0 hides every mutating tool (perm == NULL). */
        arr = jc_tool_build_neutral(&reg, 0, NULL);
        saw_write = 0;
        for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
            const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
            if (strcmp(nm, "write_file") == 0 ||
                strcmp(nm, "edit_file") == 0 ||
                strcmp(nm, "run_terminal_command") == 0) {
                saw_write = 1;
            }
        }
        JC_CHECK(saw_write == 0);
        cJSON_Delete(arr);

        free(deny_name);
        jc_vec_free(&perm.allow);
        jc_vec_free(&perm.deny);
    }

    /* A skill allow-list filters the advertised tools to just those named. */
    {
        struct jc_vec allow;
        char *only = (char *)"read_file";
        cJSON *arr2;
        cJSON *e;
        int saw_read = 0, saw_other = 0;

        jc_vec_init(&allow, sizeof(char *));
        jc_vec_push(&allow, &only);
        arr2 = jc_tool_build_neutral_ex(&reg, 1, NULL, NULL, NULL, &allow, 0);
        for (e = (arr2 != NULL) ? arr2->child : NULL; e != NULL; e = e->next) {
            const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
            if (strcmp(nm, "read_file") == 0) { saw_read = 1; }
            else { saw_other = 1; }
        }
        JC_CHECK(saw_read == 1);
        JC_CHECK(saw_other == 0); /* every non-listed tool is hidden */
        cJSON_Delete(arr2);
        jc_vec_free(&allow);
    }

    /* jc_tool_allowed: the pure predicate behind both the skill fence and the
     * per-agent-profile tool fence (M14). */
    {
        struct jc_vec allow;
        char *r = jc_strdup("read_file");
        char *s = jc_strdup("search_code");
        jc_vec_init(&allow, sizeof(char *));
        jc_vec_push(&allow, &r);
        jc_vec_push(&allow, &s);

        /* No fence => everything permitted. */
        JC_CHECK(jc_tool_allowed(NULL, "write_file") == 1);
        {
            struct jc_vec empty;
            jc_vec_init(&empty, sizeof(char *));
            JC_CHECK(jc_tool_allowed(&empty, "write_file") == 1);
            jc_vec_free(&empty);
        }
        /* With a fence: only listed names, plus load_skill, are permitted. */
        JC_CHECK(jc_tool_allowed(&allow, "read_file") == 1);
        JC_CHECK(jc_tool_allowed(&allow, "search_code") == 1);
        JC_CHECK(jc_tool_allowed(&allow, "write_file") == 0);
        JC_CHECK(jc_tool_allowed(&allow, "run_terminal_command") == 0);
        JC_CHECK(jc_tool_allowed(&allow, "load_skill") == 1); /* exempt */
        JC_CHECK(jc_tool_allowed(&allow, NULL) == 1);

        free(r);
        free(s);
        jc_vec_free(&allow);
    }

    /* jc_tool_allow_intersect: the fence used when a profile + a restrict-tools
     * skill both bind a subagent (W2). */
    {
        struct jc_vec profile, skill, out, empty;
        char *p0 = jc_strdup("read_file");
        char *p1 = jc_strdup("edit_file");
        char *p2 = jc_strdup("search_code");
        char *k0 = jc_strdup("read_file");
        char *k1 = jc_strdup("write_file");
        jc_vec_init(&profile, sizeof(char *));
        jc_vec_init(&skill, sizeof(char *));
        jc_vec_init(&empty, sizeof(char *));
        jc_vec_push(&profile, &p0);
        jc_vec_push(&profile, &p1);
        jc_vec_push(&profile, &p2);
        jc_vec_push(&skill, &k0);
        jc_vec_push(&skill, &k1);

        /* Both non-empty => only names in both (read_file). */
        jc_vec_init(&out, sizeof(char *));
        JC_CHECK(jc_tool_allow_intersect(&profile, &skill, a, &out) == 1);
        JC_CHECK(out.len == 1);
        JC_CHECK_STR(JC_VEC_STR(&out, 0), "read_file");
        jc_vec_free(&out);

        /* Empty/NULL is identity: intersecting with it copies the other list. */
        jc_vec_init(&out, sizeof(char *));
        JC_CHECK(jc_tool_allow_intersect(&profile, &empty, a, &out) == 3);
        jc_vec_free(&out);
        jc_vec_init(&out, sizeof(char *));
        JC_CHECK(jc_tool_allow_intersect(NULL, &skill, a, &out) == 2);
        jc_vec_free(&out);

        /* Both empty => empty result. */
        jc_vec_init(&out, sizeof(char *));
        JC_CHECK(jc_tool_allow_intersect(NULL, &empty, a, &out) == 0);
        jc_vec_free(&out);

        free(p0); free(p1); free(p2); free(k0); free(k1);
        jc_vec_free(&profile);
        jc_vec_free(&skill);
        jc_vec_free(&empty);
    }

    /* Clean up. */
    remove(path);

    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);

    /* M74: the core tool profile. */
    {
        struct jc_vec allow;
        JC_CHECK(jc_tool_is_core("read_file") == 1);
        JC_CHECK(jc_tool_is_core("run_terminal_command") == 1);
        JC_CHECK(jc_tool_is_core("git_status") == 0); /* dropped in core */
        JC_CHECK(jc_tool_is_core("spawn_parallel") == 0);
        JC_CHECK(jc_tool_is_core(NULL) == 0);

        jc_vec_init(&allow, sizeof(char *));
        jc_tool_core_allow(&allow);
        JC_CHECK(allow.len == 7);
        /* jc_tool_allowed honors the core list (and always exempts load_skill). */
        JC_CHECK(jc_tool_allowed(&allow, "edit_file") == 1);
        JC_CHECK(jc_tool_allowed(&allow, "git_commit") == 0);
        JC_CHECK(jc_tool_allowed(&allow, "load_skill") == 1);
        jc_vec_free(&allow);
    }

    /* M285: the tool-name universe behind the doctor fence lint. This answers
     * "is that a real tool name?", NOT "is it available here" -- registration is
     * conditional, so the live registry is always a subset. */
    {
        int n = jc_tool_name_count();
        int i;
        int seen_core = 0;
        int seen_conditional = 0;

        JC_CHECK(n >= 40); /* the table is not silently truncated */

        /* Real tools, whether or not this run would register them. */
        JC_CHECK(jc_tool_name_known("read_file") == 1);
        JC_CHECK(jc_tool_name_known("format_file") == 1);   /* needs lspServers */
        JC_CHECK(jc_tool_name_known("git_diff") == 1);      /* needs a repo     */
        JC_CHECK(jc_tool_name_known("generate_image") == 1); /* dynamic (ctx)   */
        JC_CHECK(jc_tool_name_known("read_mcp_resource") == 1);
        JC_CHECK(jc_tool_name_known("todowrite") == 1);

        /* Not tools: foreign vocabulary, and the ALIASES of real tools -- the
         * distinction that matters, because jc_tool_registry_find resolves an
         * alias but jc_tool_allowed (the fence) is exact strcmp, so an alias in
         * a fence is dead weight. */
        JC_CHECK(jc_tool_name_known("grep") == 0);
        JC_CHECK(jc_tool_name_known("glob") == 0);
        JC_CHECK(jc_tool_name_known("todo_write") == 0);
        JC_CHECK(jc_tool_canonical_name("todo_write") != NULL &&
                 strcmp(jc_tool_canonical_name("todo_write"), "todowrite") == 0);
        JC_CHECK(jc_tool_name_known(jc_tool_canonical_name("todo_write")) == 1);
        JC_CHECK(jc_tool_name_known("") == 0);
        JC_CHECK(jc_tool_name_known(NULL) == 0);

        /* Every entry is non-empty, and the table spans both the core set and
         * conditionally-registered tools (so it is the universe, not the core). */
        for (i = 0; i < n; i++) {
            const char *nm = jc_tool_name_at(i);
            JC_CHECK(nm != NULL && nm[0] != '\0');
            if (jc_tool_is_core(nm)) {
                seen_core++;
            } else {
                seen_conditional++;
            }
        }
        JC_CHECK(seen_core == 7);          /* the whole core set is present */
        JC_CHECK(seen_conditional > 20);
        JC_CHECK(jc_tool_name_at(-1) == NULL);
        JC_CHECK(jc_tool_name_at(n) == NULL);
    }

    /* Tool-name aliases: a model's plausible guess resolves to the canonical
     * registered tool instead of failing as unknown. */
    {
        struct jc_arena *a2 = jc_arena_new(0);
        struct jc_tool_registry reg2;
        JC_CHECK_STR(jc_tool_canonical_name("todoadd"), "todowrite");
        JC_CHECK_STR(jc_tool_canonical_name("todo_write"), "todowrite");
        JC_CHECK_STR(jc_tool_canonical_name("todo_read"), "todoread");
        JC_CHECK_STR(jc_tool_canonical_name("read_file"), "read_file"); /* no alias */
        JC_CHECK(jc_tool_canonical_name(NULL) == NULL);
        /* M219: schema-compatible write/shell guesses resolve transparently.
         * A telemetry pass over a long unattended workload found each such
         * miss costing a full failed round-trip at ~100k input tokens
         * (create_file, run_shell_command x6); ANECDOTES #15: only
         * transparent RESOLUTION improves the ok-rate, a better hint never
         * does. Argument compatibility verified: write_file takes
         * path+content, run_terminal_command takes command. */
        JC_CHECK_STR(jc_tool_canonical_name("create_file"), "write_file");
        JC_CHECK_STR(jc_tool_canonical_name("new_file"), "write_file");
        JC_CHECK_STR(jc_tool_canonical_name("run_shell_command"),
                     "run_terminal_command");
        JC_CHECK_STR(jc_tool_canonical_name("shell_command"),
                     "run_terminal_command");
        /* M324: glob GRADUATED to a transparent alias. It was hint-only because
         * its `pattern` did not fit list_files' `path` -- so M324 gave list_files
         * an optional `pattern` and the objection went away. A 13,783-tool-call
         * workload had 46 glob calls that never once succeeded, beside 7,761
         * shell calls: the model wanted this badly enough to keep inventing it. */
        JC_CHECK_STR(jc_tool_canonical_name("glob"), "list_files");
        JC_CHECK_STR(jc_tool_canonical_name("fd"), "list_files");
        JC_CHECK_STR(jc_tool_canonical_name("find_files"), "list_files");
        /* But `find`/`ls`/`dir` stay HINTS: they are shell commands whose
         * arguments are flags, not a pattern, so resolving them would hand
         * list_files something it cannot read. The schema-compatibility rule is
         * the whole reason this table is safe. */
        JC_CHECK_STR(jc_tool_canonical_name("find"), "find");
        JC_CHECK_STR(jc_tool_canonical_name("ls"), "ls");

        jc_tool_registry_init(&reg2);
        jc_tool_register_builtins(&reg2);
        /* The alias resolves to the same tool as the canonical name. */
        JC_CHECK(jc_tool_registry_find(&reg2, "todoadd") ==
                 jc_tool_registry_find(&reg2, "todowrite"));
        JC_CHECK(jc_tool_registry_find(&reg2, "todowrite") != NULL);
        JC_CHECK(jc_tool_registry_find(&reg2, "no_such_tool") == NULL);
        /* M90: the todoedit variant is aliased too. */
        JC_CHECK_STR(jc_tool_canonical_name("todoedit"), "todowrite");

        /* M90: unknown-tool suggestions by edit distance. A near-miss resolves
         * to the closest registered name. */
        {
            const char *s1 = jc_tool_suggest_name(&reg2, "readfile");
            const char *s2 = jc_tool_suggest_name(&reg2, "search_cod");
            JC_CHECK(s1 != NULL && strcmp(s1, "read_file") == 0);
            JC_CHECK(s2 != NULL && strcmp(s2, "search_code") == 0);
            JC_CHECK(jc_tool_suggest_name(&reg2, NULL) == NULL);
        }

        /* M360: a fence refusal names the way forward -- the tools that ARE
         * available -- because a cause with no way forward is the M342
         * message class that amplifies retry loops. Bounded at 10 + "+N
         * more"; the refused name is repeated with "will not become
         * available" so a model does not wait for it. */
        {
            struct jc_vec allow;
            struct jc_sb sb;
            static const char *NAMES[] = {
                "read_file", "write_file", "edit_file", "apply_patch",
                "list_files", "search_code", "run_terminal_command",
                "load_skill", "extra_one", "extra_two", "extra_three",
                "extra_four"
            };
            int i;

            jc_vec_init(&allow, sizeof(char *));
            for (i = 0; i < 12; i++) {
                jc_vec_push(&allow, &NAMES[i]);
            }
            jc_sb_init(&sb);
            jc_tool_refusal_render(&allow, "spawn_subagent", &sb);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "'spawn_subagent' is not available")
                     != NULL);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "Available tools: read_file, ")
                     != NULL);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "extra_two") != NULL); /* 10th name */
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "extra_three") == NULL); /* 11th cut */
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "(+2 more)") != NULL);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "will not become available") != NULL);
            jc_sb_free(&sb);

            /* Short fence: every name shown, no "+N more". */
            allow.len = 3;
            jc_sb_init(&sb);
            jc_tool_refusal_render(&allow, "fetch_url", &sb);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "read_file, write_file, edit_file")
                     != NULL);
            JC_CHECK(sb.data != NULL && strstr(sb.data, "more)") == NULL);
            jc_sb_free(&sb);

            /* No fence to enumerate: still actionable, never a bare cause. */
            jc_sb_init(&sb);
            jc_tool_refusal_render(NULL, "x_tool", &sb);
            JC_CHECK(sb.data != NULL &&
                     strstr(sb.data, "tools advertised to you") != NULL);
            jc_sb_free(&sb);
            jc_tool_refusal_render(&allow, "y", NULL); /* no crash */
            jc_vec_free(&allow);
        }

        /* M91: pure semantic-synonym map (external tool name -> jichi intent). */
        JC_CHECK_STR(jc_tool_semantic_alias("grep"), "search_code");
        JC_CHECK_STR(jc_tool_semantic_alias("rg"), "search_code");
        /* M324: glob left the HINT table when it became a real alias -- keeping
         * it in both would be two answers to one question. `find` remains. */
        JC_CHECK(jc_tool_semantic_alias("glob") == NULL);
        JC_CHECK_STR(jc_tool_semantic_alias("find"), "list_files");
        JC_CHECK_STR(jc_tool_semantic_alias("bash"), "run_terminal_command");
        JC_CHECK_STR(jc_tool_semantic_alias("cat"), "read_file");
        JC_CHECK_STR(jc_tool_semantic_alias("web_fetch"), "fetch_url");
        JC_CHECK(jc_tool_semantic_alias("no_such_thing") == NULL);
        JC_CHECK(jc_tool_semantic_alias(NULL) == NULL);

        /* M91: a synonym feeds suggest_name as a fallback when it maps to a
         * REGISTERED tool -- `grep` yields a helpful hint that edit distance
         * alone could not (search_code is built in).
         *
         * M324: `glob` is deliberately NOT tested here any more. It no longer
         * needs a suggestion because it RESOLVES, and a name that resolves never
         * reaches the unknown-tool path. `find` is the remaining hint-only
         * example, so it takes glob's place in this check. */
        {
            const char *g1 = jc_tool_suggest_name(&reg2, "grep");
            const char *g2 = jc_tool_suggest_name(&reg2, "find");
            JC_CHECK(g1 != NULL && strcmp(g1, "search_code") == 0);
            JC_CHECK(g2 != NULL && strcmp(g2, "list_files") == 0);
        }
        jc_tool_registry_free(&reg2);
        jc_arena_free(a2);
    }
}

/* M147: the prose-tool-call scanner. High-precision patterns only; every hit
 * must name a REGISTERED tool (aliases resolve); intent prose and ordinary
 * code blocks never match. */
void test_toolcall_scan(void)
{
    struct jc_tool_registry reg;
    char nm[64];

    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);

    /* Fenced JSON with a name key. */
    JC_CHECK(jc_toolcall_scan(
        "I'll read it:\n```json\n{\"name\": \"read_file\", "
        "\"arguments\": {\"path\": \"a.c\"}}\n```\n",
        &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "read_file") == 0);

    /* Fenced with a "tool" key and no args (fence pattern needs name only). */
    JC_CHECK(jc_toolcall_scan(
        "```tool\n{\"tool\": \"search_code\"}\n```",
        &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "search_code") == 0);

    /* Bare line-anchored object with name + args-ish key. */
    JC_CHECK(jc_toolcall_scan(
        "Here is the call:\n{\"name\": \"edit_file\", \"args\": "
        "{\"path\": \"x\"}}\n",
        &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "edit_file") == 0);

    /* Bare object WITHOUT an args-ish key does not match (too ambiguous). */
    JC_CHECK(jc_toolcall_scan("{\"name\": \"edit_file\"}\n",
                              &reg, nm, sizeof(nm)) == 0);

    /* A canonical alias resolves to the registered name. */
    JC_CHECK(jc_toolcall_scan(
        "{\"name\": \"todo_write\", \"arguments\": {}}",
        &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "todowrite") == 0);

    /* XML-ish tags. */
    JC_CHECK(jc_toolcall_scan(
        "<tool_call>\n{\"name\": \"list_files\"}\n</tool_call>",
        &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "list_files") == 0);
    JC_CHECK(jc_toolcall_scan(
        "<invoke name=\"read_file\">", &reg, nm, sizeof(nm)) == 1);
    JC_CHECK(strcmp(nm, "read_file") == 0);

    /* An unregistered name never matches, whatever the syntax. */
    JC_CHECK(jc_toolcall_scan(
        "```json\n{\"name\": \"launch_missiles\", \"arguments\": {}}\n```",
        &reg, nm, sizeof(nm)) == 0);
    JC_CHECK(jc_toolcall_scan("<invoke name=\"no_such_tool\">",
                              &reg, nm, sizeof(nm)) == 0);

    /* Intent prose is deliberately NOT matched (precision over recall). */
    JC_CHECK(jc_toolcall_scan("I will now run search_code on the tree.",
                              &reg, nm, sizeof(nm)) == 0);

    /* An ordinary fenced CODE block is not a tool call. */
    JC_CHECK(jc_toolcall_scan(
        "The fix:\n```c\nint main(void) { return 0; }\n```\n",
        &reg, nm, sizeof(nm)) == 0);

    /* A fenced JSON object that is data, not a call (no name key). */
    JC_CHECK(jc_toolcall_scan(
        "```json\n{\"path\": \"a.c\", \"count\": 3}\n```",
        &reg, nm, sizeof(nm)) == 0);

    /* NULL/empty inputs are refused, not crashed. */
    JC_CHECK(jc_toolcall_scan(NULL, &reg, nm, sizeof(nm)) == 0);
    JC_CHECK(jc_toolcall_scan("", &reg, nm, sizeof(nm)) == 0);

    jc_tool_registry_free(&reg);
}

/* M148: the execute path repairs nearly-JSON args (counted, validated) and,
 * when nothing conservative works, echoes the tool's expected shape. */
void test_args_repair_execute(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;

    setup_app(&app, a, &reg);

    /* Trailing comma: repaired, the tool actually RUNS (it fails on the
     * missing file, not on JSON -- the parse error text must not appear). */
    jc_tool_execute(&reg, "read_file",
                    "{\"path\": \"/tmp/jichi_no_such_file_m148\",}",
                    &res, &app);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "could not parse") == NULL);
    jc_tool_result_free(&res);

    /* Python literal repaired the same way (list_files' recursive flag). */
    jc_tool_execute(&reg, "list_files", "{\"path\": \".\", \"depth\": 1,}",
                    &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);

    /* Unrepairable garbage: the error echoes the schema -- the expected
     * keys and the required list -- so the model can self-correct. */
    jc_tool_execute(&reg, "read_file", "{path: broken here", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "could not parse") != NULL);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "Expected arguments:") != NULL);
    JC_CHECK(res.content != NULL && strstr(res.content, "path") != NULL);
    JC_CHECK(res.content != NULL && strstr(res.content, "required:") != NULL);
    jc_tool_result_free(&res);

    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M193: stringified nested arguments coerced back to the declared shape.
 *
 * The failure this fixes: a model serialises a nested array into a JSON STRING,
 *   {"todos": "[{\"content\": \"x\", \"status\": \"pending\"}]"}
 * which parses cleanly (so M148's repair never sees it) and is then rejected by
 * the tool as the wrong type. 28 of 36 todo_write calls in the zigodot log failed
 * exactly this way. The coercion must be aggressive enough to catch that and
 * conservative enough never to reinterpret a legitimate string. */
void test_tool_unstring(void)
{
    cJSON *schema = cJSON_Parse(
        "{\"type\":\"object\",\"properties\":{"
        "\"todos\":{\"type\":\"array\"},"
        "\"opts\":{\"type\":\"object\"},"
        "\"path\":{\"type\":\"string\"},"
        "\"count\":{\"type\":\"number\"}}}");
    JC_CHECK(schema != NULL);

    /* --- the observed failure, fixed ------------------------------------- */
    {
        cJSON *a = cJSON_Parse(
            "{\"todos\":\"[{\\\"content\\\":\\\"x\\\",\\\"status\\\":\\\"pending\\\"}]\"}");
        cJSON *got;
        JC_CHECK(a != NULL);
        JC_CHECK(jc_tool_unstring_args(a, schema) == 1);
        got = cJSON_GetObjectItem(a, "todos");
        JC_CHECK(got != NULL && cJSON_IsArray(got));
        JC_CHECK(cJSON_GetArraySize(got) == 1);
        JC_CHECK_STR(jc_json_get_str(cJSON_GetArrayItem(got, 0), "status", ""),
                     "pending");
        cJSON_Delete(a);
    }

    /* A declared object, likewise. */
    {
        cJSON *a = cJSON_Parse("{\"opts\":\"{\\\"deep\\\":true}\"}");
        cJSON *got;
        JC_CHECK(jc_tool_unstring_args(a, schema) == 1);
        got = cJSON_GetObjectItem(a, "opts");
        JC_CHECK(got != NULL && cJSON_IsObject(got));
        cJSON_Delete(a);
    }

    /* Several at once, and the count is returned. */
    {
        cJSON *a = cJSON_Parse("{\"todos\":\"[1,2]\",\"opts\":\"{}\","
                               "\"path\":\"/tmp/x\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 2);
        JC_CHECK(cJSON_IsArray(cJSON_GetObjectItem(a, "todos")));
        JC_CHECK(cJSON_IsObject(cJSON_GetObjectItem(a, "opts")));
        /* the string parameter is untouched */
        JC_CHECK_STR(jc_json_get_str(a, "path", ""), "/tmp/x");
        cJSON_Delete(a);
    }

    /* --- the conservative half: what must NOT be touched ------------------ */
    /* Already the right type: nothing to do. */
    {
        cJSON *a = cJSON_Parse("{\"todos\":[{\"content\":\"x\"}]}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        JC_CHECK(cJSON_IsArray(cJSON_GetObjectItem(a, "todos")));
        cJSON_Delete(a);
    }
    /* A string parameter whose value happens to look like JSON stays a string --
     * this is the case that would silently corrupt an argument if the coercion
     * keyed off the value rather than the declared type. */
    {
        cJSON *a = cJSON_Parse("{\"path\":\"[1,2,3]\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        JC_CHECK(cJSON_IsString(cJSON_GetObjectItem(a, "path")));
        JC_CHECK_STR(jc_json_get_str(a, "path", ""), "[1,2,3]");
        cJSON_Delete(a);
    }
    /* Declared array, but the string parses as the WRONG type: leave it, and let
     * the tool's own validation produce its normal error. */
    {
        cJSON *a = cJSON_Parse("{\"todos\":\"{\\\"not\\\":\\\"an array\\\"}\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        JC_CHECK(cJSON_IsString(cJSON_GetObjectItem(a, "todos")));
        cJSON_Delete(a);
    }
    /* Declared array, string is not JSON at all: prose must survive intact. */
    {
        cJSON *a = cJSON_Parse("{\"todos\":\"please add a task\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        JC_CHECK_STR(jc_json_get_str(a, "todos", ""), "please add a task");
        cJSON_Delete(a);
    }
    /* A parameter the schema does not declare is never guessed at. */
    {
        cJSON *a = cJSON_Parse("{\"mystery\":\"[1,2]\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        JC_CHECK(cJSON_IsString(cJSON_GetObjectItem(a, "mystery")));
        cJSON_Delete(a);
    }
    /* A declared non-nested type (number) is never coerced. */
    {
        cJSON *a = cJSON_Parse("{\"count\":\"[1]\"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        cJSON_Delete(a);
    }

    /* --- degenerate inputs ------------------------------------------------ */
    {
        cJSON *a = cJSON_Parse("{\"todos\":\"[1]\"}");
        JC_CHECK(jc_tool_unstring_args(a, NULL) == 0);   /* no schema */
        JC_CHECK(jc_tool_unstring_args(NULL, schema) == 0);
        cJSON_Delete(a);
    }
    {   /* args not an object, and a schema with no properties */
        cJSON *arr = cJSON_Parse("[1,2]");
        cJSON *bare = cJSON_Parse("{}");
        JC_CHECK(jc_tool_unstring_args(arr, schema) == 0);
        JC_CHECK(jc_tool_unstring_args(bare, schema) == 0);
        cJSON_Delete(arr);
        cJSON_Delete(bare);
    }
    /* Empty string, and a string containing only whitespace. */
    {
        cJSON *a = cJSON_Parse("{\"todos\":\"\",\"opts\":\"   \"}");
        JC_CHECK(jc_tool_unstring_args(a, schema) == 0);
        cJSON_Delete(a);
    }

    cJSON_Delete(schema);
}

/* M530: the tool-argument coercion layer, which had NO test at all -- and whose
 * gap is a FENCE.
 *
 * `tu_arg_bool` accepted a real bool and the stringified spellings a model
 * sometimes sends ("true", "1"), per M168. It did NOT accept a JSON NUMBER, so
 * `{"replace_all": 1}` silently became the default.
 *
 * For `spawn_subagent`'s `readonly` that is not a cosmetic loss. The caller does
 *     has_arg_ro = (cJSON_GetObjectItem(args, "readonly") != NULL);
 *     arg_ro     = tu_arg_bool(args, "readonly", 0);
 * so a model asking for a read-only child with `{"readonly": 1}` produced
 * present=1, value=0 -- read by jc_agentdef_merge as an EXPLICIT "not
 * read-only", and the child ran WRITABLE. That is M519's `"pathFence": 1`
 * defect, verbatim, on the subagent fence: the presence check fires and the
 * fallback becomes an explicit denial of what was asked for. */
void test_tu_arg_bool_numbers(void)
{
    cJSON *o = jc_json_parse(
        "{\"n1\":1,\"n0\":0,\"n2\":2,\"bt\":true,\"bf\":false,"
        "\"st\":\"true\",\"sT\":\"True\",\"s1\":\"1\","
        "\"sf\":\"false\",\"s0\":\"0\","
        "\"prose\":\"yes please\",\"arr\":[],\"nul\":null}");
    if (JC_REQUIRE(o != NULL)) {
        /* The gap this closes: a NUMBER for a boolean argument. */
        JC_CHECK(tu_arg_bool(o, "n1", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "n2", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "n0", 1) == 0);

        /* Real bools and the M168 strings keep working exactly as before. */
        JC_CHECK(tu_arg_bool(o, "bt", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "bf", 1) == 0);
        JC_CHECK(tu_arg_bool(o, "st", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "sT", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "s1", 0) == 1);
        JC_CHECK(tu_arg_bool(o, "sf", 1) == 0);
        JC_CHECK(tu_arg_bool(o, "s0", 1) == 0);

        /* And the boundary holds: prose and containers fall through to the
         * caller's default, in BOTH directions, so a typo can never flip a
         * fence on or off. */
        JC_CHECK(tu_arg_bool(o, "prose", 0) == 0);
        JC_CHECK(tu_arg_bool(o, "prose", 1) == 1);
        JC_CHECK(tu_arg_bool(o, "arr", 0) == 0);
        JC_CHECK(tu_arg_bool(o, "nul", 1) == 1);
        JC_CHECK(tu_arg_bool(o, "missing", 1) == 1);
        cJSON_Delete(o);
    }
}

/* M585: a tool call that arrives with NO NAME is a malformed call, not a wrong
 * guess, and the two need different answers. Measured on a real workload: seven
 * nameless calls across three sessions, every one inside a BURST of two or three
 * in a single turn -- the shape a model makes when the error it is handed does
 * not fit the mistake it made. Told "unknown tool ''", it tries to correct a
 * name it never sent, and produces the identical empty call again.
 *
 * The checks below discriminate rather than merely assert non-empty text: the
 * nameless answer must NOT be the unknown-name answer, and the unknown-name
 * answer must NOT have been broken while fixing it. */
void test_tool_nameless(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_tool_registry_init(&reg);
    jc_tool_registry_register(&reg, jc_tool_hint());
    app.tools = &reg;

    /* An empty name: malformed. It must say so, and it must say what to do --
     * a refusal that states only a cause is the message class that amplifies
     * retry loops (M342/M360). */
    jc_tool_execute(&reg, "", "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL);
    JC_CHECK(strstr(res.content, "no tool name") != NULL);
    JC_CHECK(strstr(res.content, "malformed") != NULL);
    /* names the corrective action, not just the cause */
    JC_CHECK(strstr(res.content, "Send the call again") != NULL);
    /* and it must NOT be the unknown-name message, which invites the model to
     * rename something it never sent */
    JC_CHECK(strstr(res.content, "unknown tool") == NULL);
    JC_CHECK(strstr(res.content, "did you mean") == NULL);
    jc_tool_result_free(&res);

    /* A NULL name is the same defect arriving by a different route. */
    jc_tool_execute(&reg, NULL, "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(strstr(res.content, "no tool name") != NULL);
    jc_tool_result_free(&res);

    /* CONTROL: a real but unknown name still gets the unknown-name answer.
     * Without this the nameless branch could have swallowed every miss and the
     * checks above would still pass. */
    jc_tool_execute(&reg, "definitely_not_a_tool", "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(strstr(res.content, "unknown tool") != NULL);
    JC_CHECK(strstr(res.content, "no tool name") == NULL);
    jc_tool_result_free(&res);

    /* CONTROL: a registered tool is unaffected -- the new branch sits in front
     * of the lookup, so a bug there would break every call, not just the odd
     * ones. */
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}
