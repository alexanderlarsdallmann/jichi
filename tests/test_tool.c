/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_tool.c - exercises the built-in tools against a temp directory. */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_message.h"   /* M949: struct jc_tool_call */
#include "jc_app.h"
#include "jc_path.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_json.h"
#include "jc_eventlog.h"   /* def-317 R2: the copy-back journal kind */
#include "jc_proc.h"   /* M832: the ERE-probe streams use the product primitive */
#include "../src/tools/tool_util.h"   /* module-private, as test_provider.c does */
#include "cJSON.h"
#include "jc_fault.h"   /* D-04/M979: fault injection tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>   /* M987: the run-tool SIGCHLD site pin sets/restores SIGCHLD */
#include "jc_snprintf.h"

/* L-25: the suite builds with -Werror=missing-prototypes, so a non-static
 * test function needs a prior prototype even when nothing calls it yet. */
void test_run_timeout_schema(void);

/* D-01/M978: test_tool_nul_* functions are called from test_tool, so they need
 * forward declarations even though they're static. */
static void test_tool_nul_edit_refused(void);
static void test_tool_nul_read_marker(void);
static void test_tool_nul_apply_patch_refused(void);
static void test_normalize_args_repair_telemetry(void);
static void test_run_tool_sigchld_site(void);   /* M987: the run-tool reap pin */
#ifdef JC_FAULT
static void test_tool_write_restore_fault(void);
static void test_tool_write_restore_once_fault(void);
static void test_tool_edit_restore_fault(void);
static void test_tool_edit_restore_once_fault(void);
#endif

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
    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f == NULL) {
        jc_tool_registry_free(&reg);
        jc_vec_free(&app.read_files);
        jc_vec_free(&app.read_recs);
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
    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f == NULL) {
        jc_tool_registry_free(&reg);
        jc_vec_free(&app.read_files);
        jc_vec_free(&app.read_recs);
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

/* M981 (B-04 + A-01): face two — the helper normalises self-named wrappers and
 * stringified arrays exactly as the executor does. The fence MUST see the same
 * tree the executor will use, or an argument-shape bypass opens. This test
 * verifies the helper's normalisation produces the same tree the executor sees. */
static void test_normalize_args_helper(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    cJSON *sch_write = NULL;
    cJSON *sch_apply = NULL;
    cJSON *norm = NULL;
    int repaired = -1;

    setup_app(&app, a, &reg);

    /* Get the schemas for unstringing. */
    sch_write = (cJSON *)jc_tool_write()->schema();
    JC_CHECK(sch_write != NULL);
    sch_apply = (cJSON *)jc_tool_apply_patch()->schema();
    if (JC_REQUIRE(sch_apply != NULL)) {

    /* Case 1: self-named wrapper (M172). The executor unwraps this, so the fence
     * must see the inner `path` field, not the wrapper structure. */
    norm = jc_tool_normalize_args("write_file", sch_write,
        "{\"write_file\":{\"path\":\"src/a.txt\",\"content\":\"hello\"}}",
        &app, &repaired);
    if (JC_REQUIRE(norm != NULL)) {
        const char *path = jc_json_get_str(norm, "path", NULL);
        /* The helper should unwrap and expose the inner path. */
        JC_CHECK_STR(path, "src/a.txt");
        /* No JSON repair was done (only unwrapping). */
        JC_CHECK(repaired == 0);
        cJSON_Delete(norm);
    }

    /* Case 2: stringified edits (M193). apply_patch expects `edits` as an array,
     * but a model might send it as a JSON string. The executor unstrings it. */
    norm = jc_tool_normalize_args("apply_patch", sch_apply,
        "{\"edits\":\"[{\\\"path\\\":\\\"src/b.txt\\\",\\\"old_string\\\":\\\"x\\\",\\\"new_string\\\":\\\"y\\\"}]\"}",
        &app, &repaired);
    if (JC_REQUIRE(norm != NULL)) {
        cJSON *edits = cJSON_GetObjectItem(norm, "edits");
        /* The helper should unstring and produce a real array. */
        JC_CHECK(cJSON_IsArray(edits));
        /* Check the array has the correct structure. */
        if (cJSON_IsArray(edits) && edits->child != NULL) {
            const char *path = jc_json_get_str(edits->child, "path", NULL);
            JC_CHECK_STR(path, "src/b.txt");
        }
        /* No JSON repair was done (only unstringing). */
        JC_CHECK(repaired == 0);
        cJSON_Delete(norm);
    }

    /* Case 3: both wrapper and stringified. A single call with both transformations. */
    norm = jc_tool_normalize_args("apply_patch", sch_apply,
        "{\"apply_patch\":{\"edits\":\"[{\\\"path\\\":\\\"src/c.txt\\\"}]\"}}",
        &app, &repaired);
    if (JC_REQUIRE(norm != NULL)) {
        const char *path = NULL;
        /* First unwrap: inner is {"edits":"[...]"} */
        cJSON *inner_edits = cJSON_GetObjectItem(norm, "edits");
        /* Then unstring: inner_edits should be an array */
        if (cJSON_IsArray(inner_edits) && inner_edits->child != NULL) {
            path = jc_json_get_str(inner_edits->child, "path", NULL);
        }
        JC_CHECK_STR(path, "src/c.txt");
        JC_CHECK(repaired == 0);
        cJSON_Delete(norm);
    }

    /* Case 4: a call with a JSON repair (trailing comma). The M353 note should
     * only fire when repaired == 1. */
    norm = jc_tool_normalize_args("write_file", sch_write,
        "{\"path\":\"src/d.txt\",\"content\":\"hello\",}",  /* trailing comma */
        &app, &repaired);
    if (JC_REQUIRE(norm != NULL)) {
        const char *path = jc_json_get_str(norm, "path", NULL);
        JC_CHECK_STR(path, "src/d.txt");
        /* A JSON repair should have occurred. */
        JC_CHECK(repaired == 1);
        cJSON_Delete(norm);
    }

    if (sch_write != NULL) cJSON_Delete(sch_write);
    if (sch_apply != NULL) cJSON_Delete(sch_apply);
    }
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* M981 (hunk audit, attempt-3): the helper is the SOLE emitter of the
 * args_repair events, and the fence's call must be telemetry-quiet. The red
 * this guards against (proven on the kept 22c355d5, where make WERROR=1 test
 * was 15692/0 and yet the journal lost events):
 *   (a) the helper called jc_tool_unstring_args but ignored its return, so a
 *       call that un-strings a stringified `edits` produced NO kind:"unstring"
 *       event in the journal;
 *   (b) the fence passed the real app, so a wrapped/repaired call under an
 *       armed scope logged its repair from the fence AND the executor -- twice.
 * test_telemetry.c cannot catch either: it is a parser over a hardcoded string.
 * So assert the events against a REAL eventlog the helper writes into. */
static void test_normalize_args_emits_once(void);

/* M981 telemetry drift (b): restore the failed-repair ok:false event and name
 * the M148 syntax-repair kind "json". Before the fix:
 *   (a) FAILED repair: unquoted key input "{path: \"a.c\"}" returns NULL, but
 *       the helper emits NO args_repair event at all (pre-M981 emitted ok:false).
 *       Red: the eventlog contains no "args_repair" substring.
 *   (b) SUCCESSFUL repair: trailing-comma input '{"path": "a.c",}' returns
 *       non-NULL, but the helper emits kind:"repair" (M148 docs say "json").
 *       Red: the eventlog contains "repair" instead of "json". */
static void test_normalize_args_repair_telemetry(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_eventlog log;
    cJSON *sch_write = NULL;
    cJSON *norm = NULL;
    int repaired = 0;
    char path[256];
    char *content = NULL;
    jc_size len = 0;

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof(path), "%s/jichi_normrepair_%ld.jsonl",
                jc_test_tmpdir(), (long)getpid());
    JC_CHECK(jc_eventlog_open(&log, path, NULL, JC_EVENTLOG_FULL) == JC_OK);
    app.telemetry = &log;
    sch_write = (cJSON *)jc_tool_write()->schema();
    JC_REQUIRE(sch_write != NULL);

    /* (a) FAILED repair: unquoted key is unrepairable. Returns NULL and emits
     * args_repair with ok:false (and kind:"json" after the fix). */
    norm = jc_tool_normalize_args("write_file", sch_write,
        "{path: \"a.c\"}", &app, &repaired);
    JC_CHECK(norm == NULL);  /* unrepairable */
    if (norm == NULL) {
        jc_eventlog_close(&log);
        JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
        if (content != NULL) {
            /* Red before fix: no event emitted at all for failed repair */
            JC_CHECK(strstr(content, "args_repair") != NULL);
            JC_CHECK(strstr(content, "\"ok\":false") != NULL);
            JC_CHECK(strstr(content, "\"kind\":\"json\"") != NULL);
            JC_CHECK(strstr(content, "\"kind\":\"repair\"") == NULL);
            content[0] = '\0';
        }
    }

    /* (b) SUCCESSFUL repair: trailing comma is repairable. Returns non-NULL and
     * emits args_repair with ok:true and kind:"json" (not "repair"). */
    if (content != NULL) content[0] = '\0';
   JC_CHECK(jc_eventlog_open(&log, path, NULL, JC_EVENTLOG_FULL) == JC_OK);
    app.telemetry = &log;
    norm = jc_tool_normalize_args("write_file", sch_write,
        "{\"path\": \"a.c\",}", &app, &repaired);
    JC_CHECK(norm != NULL);  /* repaired successfully */
    if (norm != NULL) {
        cJSON_Delete(norm);
        jc_eventlog_close(&log);
        JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
        if (content != NULL) {
            JC_CHECK(strstr(content, "args_repair") != NULL);
            JC_CHECK(strstr(content, "\"ok\":true") != NULL);
            JC_CHECK(strstr(content, "\"kind\":\"json\"") != NULL);
            JC_CHECK(strstr(content, "\"kind\":\"repair\"") == NULL);
            content[0] = '\0';
        }
    }

    if (sch_write != NULL) cJSON_Delete(sch_write);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    remove(path);
    jc_arena_free(a);
}

static void test_normalize_args_emits_once(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_eventlog log;
    cJSON *sch_apply = NULL;
    cJSON *norm = NULL;
    int repaired = 0;
    char path[256];
    char path2[256];
    char *content = NULL;
    jc_size len = 0;

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof(path), "%s/jichi_normargs_%ld.jsonl",
                jc_test_tmpdir(), (long)getpid());
    jc_snprintf(path2, sizeof(path2), "%s/jichi_normargs2_%ld.jsonl",
                jc_test_tmpdir(), (long)getpid());
    JC_CHECK(jc_eventlog_open(&log, path, NULL, JC_EVENTLOG_FULL) == JC_OK);
    app.telemetry = &log;
    sch_apply = (cJSON *)jc_tool_apply_patch()->schema();
    JC_REQUIRE(sch_apply != NULL);

    /* (a) The executor's path (app != NULL) MUST emit exactly one unstring
     * event for a stringified `edits` array, carrying the field count. */
    norm = jc_tool_normalize_args("apply_patch", sch_apply,
        "{\"edits\":\"[{\\\"path\\\":\\\"src/b.txt\\\",\\\"old_string\\\":\\\"x\\\","
        "\\\"new_string\\\":\\\"y\\\"}]\"}",
        &app, &repaired);
    JC_REQUIRE(norm != NULL);
    JC_CHECK(cJSON_IsArray(cJSON_GetObjectItem(norm, "edits")));
    cJSON_Delete(norm);
    jc_eventlog_close(&log);
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    if (content != NULL) {
        /* Red before the fix: the helper emitted no unstring event at all. */
        JC_CHECK(strstr(content, "\"kind\":\"unstring\"") != NULL);
        JC_CHECK(strstr(content, "\"fields\":1") != NULL);
        /* Unwrapping did not fire for this shape: no unwrap event. */
        JC_CHECK(strstr(content, "\"kind\":\"unwrap\"") == NULL);
        content[0] = '\0';
    }

    /* (b) The fence's path (app == NULL) performs the SAME transform but emits
     * NOTHING -- this is what stops the double-log under an armed scope. A
     * call with a NULL app and a FRESH log (path2) must leave it empty. */
    JC_CHECK(jc_eventlog_open(&log, path2, NULL, JC_EVENTLOG_FULL) == JC_OK);
    norm = jc_tool_normalize_args("apply_patch", sch_apply,
        "{\"apply_patch\":{\"edits\":\"[{\\\"path\\\":\\\"src/c.txt\\\"}]\"}}",
        NULL, NULL);
    JC_REQUIRE(norm != NULL);
    /* The transform still happened (app-independent): the inner `edits` is
     * unwrapped AND unstringed to a real array. */
    JC_CHECK(cJSON_IsArray(cJSON_GetObjectItem(norm, "edits")));
    cJSON_Delete(norm);
    jc_eventlog_close(&log);
    JC_CHECK(jc_read_file(path2, &content, &len, a) == JC_OK);
    /* Red before the NULL-app fix: the fence passed the real app, so this
     * would contain the unwrap+unstring events. */
    JC_CHECK(len == 0 || content == NULL ||
             strstr(content, "args_repair") == NULL);

    if (sch_apply != NULL) cJSON_Delete(sch_apply);
    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    remove(path);
    remove(path2);
    jc_arena_free(a);
}

/* def-317 R2: the M289 copy-back trap must fire for the NEW marker shape -- the
 * one that now carries a `ticket` field and says the content IS preserved.
 * Before R1 the args marker had no `ticket` field, so a model echoing back the
 * ticket-bearing placeholder was never a shape jichi produced and never tested.
 * The trap keys on the `elided` key (a parameter no tool declares), so it is
 * marker-agnostic by construction -- but the journal kind is what makes the
 * re-send count attributable, so it is asserted against a REAL eventlog rather
 * than only the reply text: `args_repair` with `kind: elided_placeholder`. */
static void test_elided_placeholder_ticket(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    struct jc_eventlog log;
    char path[256];
    char *content = NULL;
    jc_size len = 0;
    const char *log_path;

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof(path), "%s/jichi_elide_ticket_%ld.jsonl",
                jc_test_tmpdir(), (long)getpid());
    JC_CHECK(jc_eventlog_open(&log, path, NULL, JC_EVENTLOG_FULL) == JC_OK);
    app.telemetry = &log;

    /* The marker as jichi now writes it: the preserved note, a ticket field
     * naming the 0600 store, and the surviving path. */
    jc_tool_execute(&reg, "write_file",
        "{\"elided\":\"PLACEHOLDER, not arguments: 4023 bytes of arguments "
        "were dropped to save space; the complete arguments are preserved "
        "-- read the file named by \\\"ticket\\\" to recover them instead of "
        "re-sending.\",\"ticket\":\"/home/u/.jichi.d/tool-output/1234/"
        "elided-7.txt\",\"path\":\"src/foo.c\"}", &res, &app);
    JC_CHECK(res.is_error);
    JC_CHECK(strstr(res.content, "not arguments") != NULL);
    JC_CHECK(strstr(res.content, "src/foo.c") != NULL);
    jc_tool_result_free(&res);

    /* The journal records the kind that makes the re-send count attributable.
     * Read the file directly (not via jc_eventlog_path, which the close resets). */
    log_path = jc_eventlog_path(&log);
    JC_CHECK(log_path != NULL && strcmp(log_path, path) == 0);
    jc_eventlog_close(&log);
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    if (content != NULL) {
        JC_CHECK(strstr(content, "\"event\":\"args_repair\"") != NULL);
        JC_CHECK(strstr(content, "\"kind\":\"elided_placeholder\"") != NULL);
        JC_CHECK(strstr(content, "\"tool\":\"write_file\"") != NULL);
    }

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    remove(path);
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
    test_elided_placeholder_ticket();
    test_normalize_args_helper();
    test_normalize_args_emits_once();
    test_normalize_args_repair_telemetry();
    test_tool_nul_edit_refused();
    test_tool_nul_read_marker();
    test_tool_nul_apply_patch_refused();
    test_run_tool_sigchld_site();   /* M987: the run-tool reap pin (in-process SIGCHLD) */
#ifdef JC_FAULT
    test_tool_write_restore_fault();      /* D-04/M979: monotone, FAULT build only */
    test_tool_write_restore_once_fault(); /* D-04/M979: one-shot, FAULT build only */
    test_tool_edit_restore_fault();       /* D-04/M979: monotone, FAULT build only */
    test_tool_edit_restore_once_fault();  /* D-04/M979: one-shot, FAULT build only */
#endif
    test_fence_denial_is_policy();

    setup_app(&app, a, &reg);
    jc_snprintf(path, sizeof path, "%s/x.txt", dir);

    /* Registry lookup. */
    JC_CHECK(jc_tool_registry_find(&reg, "read_file") != NULL);
    JC_CHECK(jc_tool_registry_find(&reg, "no_such_tool") == NULL);

    /* write_file creates the file (and its parent dir). */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\",\"content\":\"hello world\"}", path);
        jc_tool_execute(&reg, "write_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        jc_tool_result_free(&res);
    }

    /* read_file returns the content. */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
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
        jc_snprintf(big, sizeof big, "%s/big.txt", dir);
        {
            char wargs[600];
            jc_snprintf(wargs, sizeof wargs, "{\"path\":\"%s\",\"content\":"
                    "\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ\"}", big);
            jc_tool_execute(&reg, "write_file", wargs, &res, &app);
            jc_tool_result_free(&res);
        }
        app.config.read_max_bytes = 10; /* below the 36-byte content */
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", big);
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
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\",\"old_string\":\"world\","
                      "\"new_string\":\"there\"}", path);
        jc_tool_execute(&reg, "edit_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        /* The result now carries a unified diff of the change. */
        JC_CHECK(strstr(res.content, "@@") != NULL);
        JC_CHECK(strstr(res.content, "-hello world") != NULL);
        JC_CHECK(strstr(res.content, "+hello there") != NULL);
        jc_tool_result_free(&res);

        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(strstr(res.content, "hello there") != NULL);
        jc_tool_result_free(&res);
    }

    /* edit_file refuses when old_string is absent. */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\",\"old_string\":\"absent\","
                      "\"new_string\":\"z\"}", path);
        jc_tool_execute(&reg, "edit_file", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        jc_tool_result_free(&res);
    }

    /* list_files includes our file, and marks directories with a trailing '/'. */
    {
        char args[512];
        char sub[300];
        jc_snprintf(sub, sizeof sub, "%s/sub", dir);
        jc_mkdir_p(sub);
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", dir);
        jc_tool_execute(&reg, "list_files", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(strstr(res.content, "x.txt") != NULL);
        JC_CHECK(strstr(res.content, "sub/") != NULL); /* dir marker */
        jc_tool_result_free(&res);
    }

    /* M991a (D-30): list_files with a pattern must NOT enumerate a file that
     * lives outside the workspace through a symlink. Non-vacuous: the in-root
     * file's NAME must be PRESENT (proves the pattern walk actually matched
     * something) and the outside file's NAME must be ABSENT. Without the
     * walker, the pattern walk descends into `linked` and lists it. Uses the
     * shared app/reg from setup_app (builtins registered; app->root empty so
     * the path fence is off). */
    {
        const char *wroot = jc_test_tmp("m991_ls_wroot");
        const char *wout = jc_test_tmp("m991_ls_out");
        const char *wlink = jc_test_tmp("m991_ls_wroot/linked");
        const char *inroot_f = jc_test_tmp("m991_ls_wroot/in_root.c");
        const char *out_f = jc_test_tmp("m991_ls_out/outside_marker_file.c");
        char wargs[512];
        (void)jc_test_rm_rf(wroot);
        (void)jc_test_rm_rf(wout);
        if (!JC_REQUIRE(jc_mkdir_p(wroot) == JC_OK && jc_mkdir_p(wout) == JC_OK))
            return;
        if (!JC_REQUIRE(jc_write_file(inroot_f, "int in_root;\n", 13) == JC_OK))
            return;
        if (!JC_REQUIRE(jc_write_file(out_f, "int outside;\n", 13) == JC_OK))
            return;
        if (!JC_REQUIRE(symlink(wout, wlink) == 0))
            return;
        jc_snprintf(wargs, sizeof wargs, "{\"path\":\"%s\",\"pattern\":\"**\"}", wroot);
        jc_tool_execute(&reg, "list_files", wargs, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(res.content != NULL);
        JC_CHECK(res.content != NULL && strstr(res.content, "in_root.c") != NULL);
        JC_CHECK(res.content == NULL || strstr(res.content, "outside_marker_file.c") == NULL);
        /* DEFECT B pin: the symlink's OWN NAME ("linked") must ALSO be absent (a refused entry is not surfaced) */
        JC_CHECK(res.content == NULL || strstr(res.content, "linked") == NULL);
        jc_tool_result_free(&res);
        (void)jc_test_rm_rf(wroot);
        (void)jc_test_rm_rf(wout);
    }

    /* run_terminal_command captures output. */
    {
        jc_tool_execute(&reg, "run_terminal_command",
                        "{\"command\":\"echo jichi_marker\"}", &res, &app);
        JC_CHECK(strstr(res.content, "jichi_marker") != NULL);
        jc_tool_result_free(&res);
    }

    /* The grep prefix, both branches. The -I one is what every platform in the
     * matrix except illumos takes; the other is the one that cannot be reached
     * here at all, which is exactly why it is asserted rather than trusted
     * (M658 -- illumos grep rejects -I and every search_code call failed). */
    {
        const char *with_i = jc_search_grep_prefix(1);
        const char *no_i   = jc_search_grep_prefix(0);
        JC_CHECK(strstr(with_i, "-rnI") != NULL);
        JC_CHECK(strstr(no_i, "-rn") != NULL);
        /* Not strchr(no_i, 'I'): GREP_OPTIONS itself carries one, which is
         * how this assertion first failed. The claim is about the FLAG. */
        JC_CHECK(strstr(no_i, "-rnI") == NULL);
    }

    /* search_code finds a match. */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"pattern\":\"there\",\"path\":\"%s\"}", dir);
        jc_tool_execute(&reg, "search_code", args, &res, &app);
        JC_CHECK(strstr(res.content, "x.txt") != NULL);
        jc_tool_result_free(&res);
    }

    /* M714: search_code speaks EXTENDED regular expressions, the dialect models
     * write. Measured in the 2026-09-23 zigodot corpus: 230 of 888 patterns used
     * a bare `|` against 5 using GNU-basic `\|`, and in grep's basic mode those
     * came back "(no matches)" -- 11 of the pilot's 45 searches were false
     * negatives, re-run against the same tree. Each check below is one thing
     * basic mode got wrong, run through the real tool and this platform's grep. */
    {
        char wargs[512];
        char sargs[512];
        const char *e_i = jc_search_grep_prefix(1);
        const char *e_no = jc_search_grep_prefix(0);
        JC_CHECK(strstr(e_i, "-rnIE") != NULL);
        JC_CHECK(strstr(e_no, "-rnE") != NULL);

        jc_snprintf(wargs, sizeof wargs,
                    "{\"path\":\"%s/ere.txt\",\"content\":"
                    "\"pub const Kind = enum {\\nfn alpha_one() void {}\\n"
                    "call(x);\\n\"}", dir);
        jc_tool_execute(&reg, "write_file", wargs, &res, &app);
        JC_CHECK(res.is_error == 0);
        jc_tool_result_free(&res);

        /* alternation: `|` separates alternatives, it is not a literal bar */
        jc_snprintf(sargs, sizeof sargs,
                    "{\"pattern\":\"Kind|TokenKinds\",\"path\":\"%s/ere.txt\"}",
                    dir);
        jc_tool_execute(&reg, "search_code", sargs, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(strstr(res.content, "pub const Kind") != NULL);
        jc_tool_result_free(&res);

        /* a quantifier: `+` repeats, it is not a literal plus */
        jc_snprintf(sargs, sizeof sargs,
                    "{\"pattern\":\"alph[a]_on+e\",\"path\":\"%s/ere.txt\"}",
                    dir);
        jc_tool_execute(&reg, "search_code", sargs, &res, &app);
        JC_CHECK(strstr(res.content, "alpha_one") != NULL);
        jc_tool_result_free(&res);

        /* an escaped paren is a LITERAL paren (in basic mode it opened a group
         * and matched "callx", so the call site was never found) */
        jc_snprintf(sargs, sizeof sargs,
                    "{\"pattern\":\"call\\\\(x\\\\)\",\"path\":\"%s/ere.txt\"}",
                    dir);
        jc_tool_execute(&reg, "search_code", sargs, &res, &app);
        JC_CHECK(strstr(res.content, "call(x);") != NULL);
        jc_tool_result_free(&res);

        /* an invalid pattern is an error that says the PATTERN is the problem,
         * not the generic "grep exited with an error" the model could not act on.
         *
         * M956: this is a PORTABLE assertion -- it holds under GNU grep AND
         * under busybox/Alpine grep, which is exactly the point. The two greps
         * fail a bad pattern DIFFERENTLY: GNU's /dev/null probe reports it
         * (rc 2, "Unmatched ( or \("), busybox only reports it once it has read
         * real content (rc 2, "bad regex '<pat>'"). The tool's pattern_error()
         * therefore probes against a readable line (printf x | grep), so both
         * greps land on the invalid-pattern branch. The substring below is
         * jichi's OWN composed message (src/tools/jc_tool_search.c), not grep's
         * wording, so it is identical on every platform. ere.txt has content,
         * so the main search reads a file and busybox exits 2 as well. */
        jc_snprintf(sargs, sizeof sargs,
                    "{\"pattern\":\"unbalanced(\",\"path\":\"%s/ere.txt\"}", dir);
        jc_tool_execute(&reg, "search_code", sargs, &res, &app);
        JC_CHECK(res.is_error == 1);
        JC_CHECK(strstr(res.content, "not a valid extended regular expression")
                 != NULL);
        jc_tool_result_free(&res);

        /* M732: a pattern grep ACCEPTS WITH A WARNING must come back with the
         * warning. `(?:alpha|beta)` is Perl's dialect; GNU grep says "? at start
         * of expression", exits 1, and the tool used to answer a bare
         * "(no matches)" -- bonsai 08 of the M731 drive then repeated such a
         * search thirteen times. This platform's grep is asked first, so the
         * check is two-sided: a grep that warns must have its warning passed
         * on, a grep that stays silent must leave a bare "(no matches)", and a
         * grep that rejects the pattern takes the error path checked above. */
        {
            char gline[256];
            int warns = 0;
            int accepts = 0;   /* M817: grep took (?:...) AND matched real text */
            /* M832: the probe uses the product's own stream primitive, not libc
             * popen -- under the NOFORK measuring build libc popen is poisoned
             * while search_code (through jc_spawn) still runs grep for real, and
             * this probe answered "grep cannot" about a grep that just did. An
             * instrument must share the capability of the thing it measures. */
            FILE *g = jc_proc_popen("GREP_OPTIONS= grep -E -e '(?:alpha|beta)' "
                                    "/dev/null 2>&1", "r");
            gline[0] = '\0';
            if (g != NULL) {
                if (fgets(gline, (int)sizeof gline, g) != NULL &&
                    strstr(gline, "warning") != NULL) {
                    warns = 1;
                }
                while (fgets(gline, (int)sizeof gline, g) != NULL) {
                    /* drain */
                }
                jc_proc_pclose(g);
            }
            /* M817 (Dima Mellnik, macOS): BSD grep 2.6.0 is a FOURTH case -- it
             * accepts the Perl `(?:...)` group without a warning AND matches on it,
             * so `search_code` returns grep's hit ("alpha_one" is in ere.txt), not
             * "(no matches)". For the model that is the better behaviour; the test
             * must not demand a miss from a grep that legitimately finds one. Probe
             * whether THIS grep matches the group against real text. */
            g = jc_proc_popen("printf 'fn alpha_one\\n' | GREP_OPTIONS= grep -E -e "
                              "'(?:alpha|beta)' >/dev/null 2>&1; echo rc=$?", "r");
            if (g != NULL) {
                while (fgets(gline, (int)sizeof gline, g) != NULL) {
                    if (strstr(gline, "rc=0") != NULL) {
                        accepts = 1;
                    }
                }
                jc_proc_pclose(g);
            }
            jc_snprintf(sargs, sizeof sargs,
                        "{\"pattern\":\"(?:alpha|beta)\",\"path\":\"%s/ere.txt\"}",
                        dir);
            jc_tool_execute(&reg, "search_code", sargs, &res, &app);
            if (!res.is_error) {
                if (accepts) {
                    /* grep took the group and found the line: the tool passes the
                     * hit through, so it names the matched text, not a miss. */
                    JC_CHECK(strstr(res.content, "alpha_one") != NULL);
                } else {
                    JC_CHECK(strstr(res.content, "(no matches)") != NULL);
                    if (warns) {
                        JC_CHECK(strstr(res.content, "grep warned") != NULL);
                        JC_CHECK(strstr(res.content, "? at start of expression")
                                 != NULL);
                    } else {
                        JC_CHECK(strcmp(res.content, "(no matches)") == 0);
                    }
                }
            }
            jc_tool_result_free(&res);
        }
    }

    /* D-31: search_code refuses a path that begins with `-` to prevent it being
     * read as a grep option. The error message must name the problem and the fix. */
    {
        char args[512];
        /* A path that starts with `-` should be refused as a tool error. */
        jc_snprintf(args, sizeof args, "{\"pattern\":\"there\",\"path\":\"--version\"}");
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "search_code", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        /* The error message must name the dash and suggest the fix. */
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "dash") != NULL);
            JC_CHECK(strstr(res.content, "./") != NULL);
        }
        /* The tool should NOT have run grep (no "(no matches)" text). */
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "(no matches)") == NULL);
        }
        jc_tool_result_free(&res);

        /* A path that starts with `./` should work fine (the `--` separator in
         * the grep command should not break `./sub` style paths). */
        {
            char subpath[256];
            char sub[300];
            jc_snprintf(sub, sizeof sub, "%s/sub", dir);
            jc_mkdir_p(sub);
            jc_snprintf(subpath, sizeof subpath, "%s/x.txt", sub);
            {
                char wargs[512];
                jc_snprintf(wargs, sizeof wargs, "{\"path\":\"%s\",\"content\":\"hello sub\"}", subpath);
                jc_tool_execute(&reg, "write_file", wargs, &res, &app);
                JC_CHECK(res.is_error == 0);
                jc_tool_result_free(&res);
            }
            /* Search using `./sub` style path should find the match. */
            {
                char subargs[512];
                jc_snprintf(subargs, sizeof subargs, "{\"pattern\":\"hello\",\"path\":\"%s\"}", sub);
                jc_tool_execute(&reg, "search_code", subargs, &res, &app);
                JC_CHECK(res.is_error == 0);
                JC_CHECK(strstr(res.content, "x.txt") != NULL);
                jc_tool_result_free(&res);
            }
        }
    }

    /* M1110-b (D-36 follow-up): a missing path must fall through to grep's
     * usual "reading the path" error, NOT be refused by the D-36 guard.
     * This is a regression tooth: GREEN on the current code (both the
     * pre-fix lstat guard and the post-fix stat guard fall through for a
     * missing path). Its RED is demonstrated by perturbing the guard to
     * also refuse missing paths (see the M1110-b report). */
    {
        char margs[256];
        jc_snprintf(margs, sizeof margs,
                    "{\"pattern\":\"x\",\"path\":\"jichi_missing_1110b/dir/file\"}");
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "search_code", margs, &res, &app);
        JC_CHECK(res.is_error == 1);
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "reading the path") != NULL);
            JC_CHECK(strstr(res.content, "not a regular file") == NULL);
        }
        jc_tool_result_free(&res);
    }

    /* M1110-c (SV:1725 #4): a DANGLING symlink must fall through to grep's
     * usual "reading the path" error, NOT be refused by the D-36 guard.
     * The guard's stat() follows the link, gets ENOENT for the absent
     * target, returns != 0, and falls through to grep -- exactly the
     * stat-fall-through class this tooth pins, and the one the M1110-b
     * missing-path tooth above does not cover (that path is absent
     * entirely; here the NAME is present but the TARGET is not).
     * GREEN-first sibling of M1110-b: HEAD handles it right (the lead
     * measured grep's reading-the-path error, no hang, no over-refusal).
     * Its RED is a FUTURE guard that over-refuses a link whose target is
     * absent -- the way M1110's lstat over-refused a link to a regular
     * file. */
    {
        const char *dlink = jc_test_tmp("m1110c_dangling_link");
        char dargs[512];
        (void)remove(dlink); /* fresh: a leftover link must not skew the tooth */
        if (!JC_REQUIRE(symlink("no_such_target_1110c", dlink) == 0))
            return;
        jc_snprintf(dargs, sizeof dargs,
                    "{\"pattern\":\"x\",\"path\":\"%s\"}", dlink);
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "search_code", dargs, &res, &app);
        JC_CHECK(res.is_error == 1);
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "reading the path") != NULL);
            /* the D-36 over-refusal marker must be absent */
            JC_CHECK(strstr(res.content, "not a regular file") == NULL);
            /* and it is not the D-31 dash refusal */
            JC_CHECK(strstr(res.content, "dash") == NULL);
        }
        (void)remove(dlink);
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
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\",\"content\":\"nope\"}", path);
        jc_tool_execute(&reg, "write_file", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        jc_tool_result_free(&res);
        app.readonly = 0;
    }

    /* M1041-d P3: the read-only gate must set policy_refusal for a refused
     * run_terminal_command call. This is the exact gate that the supervisor
     * named: 'a unit test that runs jc_tool_execute for run_terminal_command
     * with app->readonly set and asserts out.policy_refusal == 1, red with P3'. */
    {
        char args[512];
        app.readonly = 1;
        jc_snprintf(args, sizeof args, "{\"command\":\"echo jichi_marker\"}");
        memset(&res, 0, sizeof(res));
        jc_tool_execute(&reg, "run_terminal_command", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        JC_CHECK(res.policy_refusal == 1);
        JC_CHECK(res.content != NULL);
        JC_CHECK(strstr(res.content, "read-only") != NULL);
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

        /* M848: both non-empty and DISJOINT => a fence that admits nothing.
         * Before M848 `out` was left empty, and an empty list reads as "no
         * fence" to jc_tool_allowed -- so the strictest outcome became the
         * widest, and a profile + skill with nothing in common ran their
         * subagent with every tool. Return 0 (no name in common) and hold the
         * sentinel, never an empty list. */
        {
            struct jc_vec only_tests;
            char *t0 = jc_strdup("run_tests");
            jc_vec_init(&only_tests, sizeof(char *));
            jc_vec_push(&only_tests, &t0);
            jc_vec_init(&out, sizeof(char *));
            JC_CHECK(jc_tool_allow_intersect(&profile, &only_tests, a, &out) == 0);
            JC_CHECK(out.len == 1);
            JC_CHECK_STR(JC_VEC_STR(&out, 0), JC_TOOL_ALLOW_NONE);
            JC_CHECK(jc_tool_allowed(&out, "read_file") == 0);
            JC_CHECK(jc_tool_allowed(&out, "run_tests") == 0);
            JC_CHECK(jc_tool_allowed(&out, "run_terminal_command") == 0);
            jc_vec_free(&out);
            free(t0);
            jc_vec_free(&only_tests);
        }

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

/* D-01/M978: NUL bytes in a file should be detected and refused by edit_file.
 * Flow: create a file with a NUL byte (using fwrite explicit length), read_file
 * it (satisfies the guard), then edit_file. The edit should fail with a NUL
 * refusal message, and the file's content should be unchanged. */
static void test_tool_nul_edit_refused(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_nul_test");
    char path[256];
    char content[16];
    FILE *f;

    /* Create directory and file with NUL byte */
    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/nul.txt", dir);

    /* File: "abc" + NUL + "def" (7 bytes total, strlen gives 3) */
    content[0] = 'a'; content[1] = 'b'; content[2] = 'c';
    content[3] = '\0';
    content[4] = 'd'; content[5] = 'e'; content[6] = 'f';

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite(content, 1, 7, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* Step 1: read_file - should succeed and mark the file as read */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        /* The content should show NUL bytes marker */
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "NUL bytes") != NULL);
            /* Content should be truncated at the NUL */
            JC_CHECK(strstr(res.content, "abc") != NULL);
            JC_CHECK(strstr(res.content, "def") == NULL);
        }
        jc_tool_result_free(&res);
    }

    /* Step 2: edit_file should fail with NUL refusal */
    {
        char args[512];
        jc_snprintf(args, sizeof args,
            "{\"path\":\"%s\",\"old_string\":\"abc\",\"new_string\":\"XYZ\"}", path);
        jc_tool_execute(&reg, "edit_file", args, &res, &app);
        /* edit_file should return an error */
        JC_CHECK(res.is_error == 1);
        if (JC_REQUIRE(res.content != NULL)) {
            JC_CHECK(strstr(res.content, "NUL bytes") != NULL);
        }
        jc_tool_result_free(&res);
    }

    /* Step 3: file should be unchanged (fread and compare) */
    {
        char check[16];
        size_t got;
        f = fopen(path, "rb");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            got = fread(check, 1, 7, f);
            fclose(f);
            JC_CHECK(got == 7);
            JC_CHECK(memcmp(check, content, 7) == 0);
        }
    }

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

/* M987 (supervisor 2026-10-19 14:37): the run-tool SITE pin.
 * test_run_verify_sigchld_site (test_envelope.c) pins the verifier's checked
 * reap, and the process-level reset is pinned by the sigchld_inherit driver,
 * but run_command_watched (jc_app.c) -- the path a MODEL-issued command takes
 * when a timeout or memory budget is set -- was pinned by neither: its
 * site-layer defect (an inherited SIGCHLD=SIG_IGN makes a failed command read
 * as exit 0) was invisible to the suite, because the start-up reset hides the
 * inherited state and no unit goes through main(). Under an in-process
 * SIGCHLD=SIG_IGN the run tool must not report success or exit 0 for a
 * failing command; the checked reap reports unknown (-1), which the tool
 * renders as [exit status: -1] and is_error. The per-call "timeout" argument
 * (5) is what routes the command through run_command_watched -- without it
 * the popen path runs, and this check would be pinning the wrong site.
 * Red with the jc_app.c reap reverted to a raw waitpid (then
 * [exit status: 0], is_error 0). */
static void test_run_tool_sigchld_site(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    void (*old)(int);

    setup_app(&app, a, &reg);
    old = signal(SIGCHLD, SIG_IGN);
    if (old == SIG_ERR) {
        jc_tool_registry_free(&reg);
        jc_vec_free(&app.read_files);
        jc_vec_free(&app.read_recs);
        jc_arena_free(a);
        return;   /* cannot set it up: nothing to prove */
    }

    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "run_terminal_command",
                    "{\"command\":\"sh -c 'exit 3'\",\"timeout\":5}",
                    &res, &app);
    /* A failing command under ignored SIGCHLD: the checked reap says unknown.
     * Never exit 0 and never a clean success. */
    JC_CHECK(res.is_error != 0);
    if (res.content != NULL) {
        JC_CHECK(strstr(res.content, "[exit status: 0]") == NULL);
    }
    jc_tool_result_free(&res);

    signal(SIGCHLD, SIG_DFL);

    /* The site really ran: exit 3, reaped normally. */
    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "run_terminal_command",
                    "{\"command\":\"sh -c 'exit 3'\",\"timeout\":5}",
                    &res, &app);
    if (res.content != NULL) {
        JC_CHECK(strstr(res.content, "[exit status: 3]") != NULL);
    }
    jc_tool_result_free(&res);

    /* A control command that succeeds is still reported as success. */
    memset(&res, 0, sizeof(res));
    jc_tool_execute(&reg, "run_terminal_command",
                    "{\"command\":\"true\",\"timeout\":5}", &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
}

/* D-01/M978: read_file should show a NUL bytes marker for files with NUL bytes.
 * This is tested indirectly in test_tool_nul_edit_refused, but we also verify
 * the marker format. */
static void test_tool_nul_read_marker(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_nul_read");
    char path[256];
    char content[20];
    FILE *f;

    /* Create file with NUL byte */
    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/nul.txt", dir);

    /* File: "hello" + NUL + "world" */
    content[0] = 'h'; content[1] = 'e'; content[2] = 'l'; content[3] = 'l';
    content[4] = 'o'; content[5] = '\0';
    content[6] = 'w'; content[7] = 'o'; content[8] = 'r'; content[9] = 'l'; content[10] = 'd';

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite(content, 1, 11, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* read_file should show NUL bytes marker */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        if (JC_REQUIRE(res.content != NULL)) {
            /* The marker should be present */
            JC_CHECK(strstr(res.content, "NUL bytes") != NULL);
            /* The marker should contain the truncation offset */
            JC_CHECK(strstr(res.content, "truncated at byte") != NULL);
            /* The visible content should be "hello" */
            JC_CHECK(strstr(res.content, "hello") != NULL);
            /* The content after NUL should NOT be visible */
            JC_CHECK(strstr(res.content, "world") == NULL);
        }
        jc_tool_result_free(&res);
    }

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

/* D-01/M978: apply_patch should refuse a file with NUL bytes.
 * The guard in get_file (jc_tool_apply_patch.c:126-128) checks len != strlen(text)
 * and returns "file contains NUL bytes; edit it with a script".
 * This test creates a file with a NUL byte, reads it first, then attempts apply_patch.
 * The call must fail with an error containing "NUL bytes", and the file must be unchanged. */
static void test_tool_nul_apply_patch_refused(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_nul_apply");
    char path[256];
    char content[16];
    FILE *f;

    /* Create directory and file with NUL byte */
    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/nul.txt", dir);

    /* File: "abc" + NUL + "def" (7 bytes total, strlen gives 3) */
    content[0] = 'a'; content[1] = 'b'; content[2] = 'c';
    content[3] = '\0';
    content[4] = 'd'; content[5] = 'e'; content[6] = 'f';

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite(content, 1, 7, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* Step 1: read_file to satisfy the read-before-edit guard */
    {
        char args[512];
        jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
        jc_tool_execute(&reg, "read_file", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        jc_tool_result_free(&res);
    }

    /* Step 2: apply_patch should fail with NUL refusal */
    {
        char args[512];
        jc_snprintf(args, sizeof args,
            "{\"edits\":[{\"path\":\"%s\",\"old_string\":\"abc\","
            "\"new_string\":\"XYZ\"}]}", path);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        /* apply_patch should return an error */
        JC_CHECK(res.is_error == 1);
        if (JC_REQUIRE(res.content != NULL)) {
            /* The error must mention NUL bytes - the apply_patch refusal phrase */
            JC_CHECK(strstr(res.content, "NUL bytes") != NULL);
        }
        jc_tool_result_free(&res);
    }

    /* Step 3: file should be unchanged (compare with original content) */
    {
        char check[16];
        size_t got;
        f = fopen(path, "rb");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            got = fread(check, 1, 7, f);
            fclose(f);
            JC_CHECK(got == 7);
            JC_CHECK(memcmp(check, content, 7) == 0);
        }
    }

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

#ifdef JC_FAULT
/* D-04/M979: write_file with fault injection should restore original content.
 * This verifies the D-04 restore logic in write_file when a write fails. */
static void test_tool_write_restore_fault(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_write_restore");
    char path[256];
    char args[512];
    char *content = NULL;
    jc_size len = 0;
    FILE *f;

    /* Create directory and original file */
    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/original.txt", dir);

    /* Create file with original content */
    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite("original content\n", 1, 17, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* Arm the in-place write to fail on EVERY call (monotone threshold 0):
     * the primary write truncates the file and fails, and the D-04 restore --
     * which re-enters the same jc_write_file fault site -- fails too. That is
     * the ENOSPC production case (a persistent failure the restore cannot
     * beat). The TOOTH here is that the tool REPORTS THE TRUE OUTCOME:
     * pre-D-04 a failed write said plain "could not write"; the fix adds the
     * restore attempt, so under a persistent fault it says "(restore also
     * failed; the file may be corrupt)" -- proving the restore path RAN and
     * that the tool never falsely claims a success or a restore it did not
     * make. The positive "restore lands the original bytes" case (a transient
     * primary failure the disk recovers from) is test_tool_write_
     * restore_once_fault, armed with the one-shot knob JICHI_FAULT_WRITE_
     * ONCE_AT (added because the monotone model cannot express it without
     * inverting the M503 -1 sentinel). */
    jc_fault_reset();
    setenv("JICHI_FAULT_WRITE_AFTER", "0", 1);
    jc_fault_reset();

    /* write_file replacement - the primary write fails (file truncated) */
    jc_snprintf(args, sizeof args,
        "{\"path\":\"%s\",\"content\":\"replaced content\\n\"}", path);
    jc_tool_execute(&reg, "write_file", args, &res, &app);

    /* Error, and the D-04 restore path ran (the parenthetical is new) */
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content, "could not write") != NULL);
        JC_CHECK(strstr(res.content, "restore also failed") != NULL);
        JC_CHECK(strstr(res.content, "original restored") == NULL);
    }
    jc_tool_result_free(&res);

    /* Both writes faulted, so the file is truncated (empty), not restored */
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    JC_CHECK(len == 0);

    /* Disable fault and cleanup */
    unsetenv("JICHI_FAULT_WRITE_AFTER");
    jc_fault_reset();

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

/* D-04/M979: the positive restore case -- a TRANSIENT write failure the disk
 * recovers from. Only the FIRST jc_write_file faults (one-shot knob
 * JICHI_FAULT_WRITE_ONCE_AT=0, the M269 NET_MID_AT pattern: 0-based, unset is
 * -1 = disabled, never 0-armed -- M503), so the primary write truncates and
 * fails, and the D-04 restore -- the SECOND write -- lands the original
 * bytes. This is the property DEFERRED.md names as unobserved; it needs the
 * monotone model's one-shot sibling because AFTER=0 faults the restore too. */
static void test_tool_write_restore_once_fault(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_write_restore_once");
    char path[256];
    char args[512];
    char *content = NULL;
    jc_size len = 0;
    FILE *f;

    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/original.txt", dir);

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite("original content\n", 1, 17, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* Only the 0-based FIRST in-place write faults; the restore is the
     * second and must succeed. */
    jc_fault_reset();
    setenv("JICHI_FAULT_WRITE_ONCE_AT", "0", 1);
    jc_fault_reset();

    jc_snprintf(args, sizeof args,
        "{\"path\":\"%s\",\"content\":\"replaced content\\n\"}", path);
    jc_tool_execute(&reg, "write_file", args, &res, &app);

    /* Error, and the restore LANDED (not merely attempted) */
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content, "original restored") != NULL);
        JC_CHECK(strstr(res.content, "restore also failed") == NULL);
    }
    jc_tool_result_free(&res);

    /* The original bytes are back -- the property the DEFERRED row named */
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    JC_CHECK(len == 17);
    JC_CHECK(memcmp(content, "original content\n", 17) == 0);

    unsetenv("JICHI_FAULT_WRITE_ONCE_AT");
    jc_fault_reset();

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

/* D-04/M979: edit_file with fault injection should restore original content.
 * This verifies the D-04 restore logic in edit_file when a write fails. */
static void test_tool_edit_restore_fault(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_edit_restore");
    char path[256];
    char args[512];
    char *content = NULL;
    jc_size len = 0;
    FILE *f;

    /* Create directory and original file */
    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/original.txt", dir);

    /* Create file with original content */
    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite("original content\n", 1, 17, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* First: read the file to satisfy the read-before-edit guard */
    jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);

    /* Arm the in-place write to fail on EVERY call (monotone threshold 0) --
     * the ENOSPC case: the primary write and the D-04 restore both fault.
     * Tooth: the tool reports the restore attempt ("restore also failed"),
     * which pre-D-04 was a plain "could not write". The positive case for
     * edit_file is test_tool_edit_restore_once_fault (one-shot knob). */
    jc_fault_reset();
    setenv("JICHI_FAULT_WRITE_AFTER", "0", 1);
    jc_fault_reset();

    /* edit_file - the primary write fails (file truncated) */
    jc_snprintf(args, sizeof args,
        "{\"path\":\"%s\",\"old_string\":\"original\",\"new_string\":\"replaced\"}", path);
    jc_tool_execute(&reg, "edit_file", args, &res, &app);

    /* Error, and the D-04 restore path ran (the parenthetical is new) */
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content, "could not write") != NULL);
        JC_CHECK(strstr(res.content, "restore also failed") != NULL);
        JC_CHECK(strstr(res.content, "original restored") == NULL);
    }
    jc_tool_result_free(&res);

    /* Both writes faulted, so the file is truncated (empty), not restored */
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    JC_CHECK(len == 0);

    /* Disable fault and cleanup */
    unsetenv("JICHI_FAULT_WRITE_AFTER");
    jc_fault_reset();

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}

/* D-04/M979: edit_file's POSITIVE restore -- the one-shot knob faults only the
 * first in-place write (the primary), so the D-04 restore -- the second write
 * in edit_run's failure path (jc_tool_edit.c:150) -- lands the original bytes.
 * The monotone edit case above can never observe this: AFTER=0 faults the
 * restore too. This is the property the supervisor named as unobserved for
 * edit_file specifically (04:25). */
static void test_tool_edit_restore_once_fault(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_edit_restore_once");
    char path[256];
    char args[512];
    char *content = NULL;
    jc_size len = 0;
    FILE *f;

    (void)mkdir(dir, 0755);
    jc_snprintf(path, sizeof path, "%s/original.txt", dir);

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        fwrite("original content\n", 1, 17, f);
        fclose(f);
    }

    setup_app(&app, a, &reg);

    /* First: read the file to satisfy the read-before-edit guard */
    jc_snprintf(args, sizeof args, "{\"path\":\"%s\"}", path);
    jc_tool_execute(&reg, "read_file", args, &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);

    jc_fault_reset();
    setenv("JICHI_FAULT_WRITE_ONCE_AT", "0", 1);
    jc_fault_reset();

    jc_snprintf(args, sizeof args,
        "{\"path\":\"%s\",\"old_string\":\"original\",\"new_string\":\"replaced\"}", path);
    jc_tool_execute(&reg, "edit_file", args, &res, &app);

    /* Error, and the restore LANDED (not merely attempted) */
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content, "original restored") != NULL);
        JC_CHECK(strstr(res.content, "restore also failed") == NULL);
    }
    jc_tool_result_free(&res);

    /* The 17 original bytes are back */
    JC_CHECK(jc_read_file(path, &content, &len, a) == JC_OK);
    JC_CHECK(len == 17);
    JC_CHECK(memcmp(content, "original content\n", 17) == 0);

    unsetenv("JICHI_FAULT_WRITE_ONCE_AT");
    jc_fault_reset();

    jc_tool_registry_free(&reg);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_arena_free(a);
    (void)remove(path);
    (void)rmdir(dir);
}
#endif

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
        if (JC_REQUIRE(a != NULL)) { /* a guard (M729) */
            JC_CHECK(jc_tool_unstring_args(a, schema) == 1);
        }
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
    if (JC_REQUIRE(res.content != NULL)) { /* a guard (M729) */
        JC_CHECK(strstr(res.content, "no tool name") != NULL);
        JC_CHECK(strstr(res.content, "malformed") != NULL);
    }
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

/* M853 (N32): the names a model calls to end its turn, and the message such a
 * call carries. The predicate takes the six names exactly and nothing near them;
 * the extraction takes only a non-blank `message` string -- the measured shape --
 * so `complete {"id": 3}` can never end a turn with "3". And the unknown-tool
 * answer for those names says how a turn ends, while any other unknown name keeps
 * today's answer: without that control the new branch could swallow every miss
 * and the checks above would still pass. */
void test_tool_finish_alias(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    char *m;

    JC_CHECK(jc_tool_is_finish_alias("finish") == 1);
    JC_CHECK(jc_tool_is_finish_alias("done") == 1);
    JC_CHECK(jc_tool_is_finish_alias("complete") == 1);
    JC_CHECK(jc_tool_is_finish_alias("submit") == 1);
    JC_CHECK(jc_tool_is_finish_alias("task_complete") == 1);
    JC_CHECK(jc_tool_is_finish_alias("final_answer") == 1);
    /* near misses, another case, and non-names */
    JC_CHECK(jc_tool_is_finish_alias("finished") == 0);
    JC_CHECK(jc_tool_is_finish_alias("Finish") == 0);
    JC_CHECK(jc_tool_is_finish_alias("find") == 0);
    JC_CHECK(jc_tool_is_finish_alias("attempt_completion") == 0);
    JC_CHECK(jc_tool_is_finish_alias("") == 0);
    JC_CHECK(jc_tool_is_finish_alias(NULL) == 0);

    /* the measured shape (the demos drive, run 0087) */
    m = jc_tool_finish_alias_message(
        "{\"task_completed\": \"true\", \"message\": \"Card 204 completed.\"}");
    JC_CHECK(m != NULL && strcmp(m, "Card 204 completed.") == 0);
    free(m);
    /* no message, a blank one, a non-string, not an object, not JSON, another
     * key, nothing */
    JC_CHECK(jc_tool_finish_alias_message("{\"task_completed\": \"true\"}") == NULL);
    JC_CHECK(jc_tool_finish_alias_message("{\"message\": \" \\n\\t\"}") == NULL);
    JC_CHECK(jc_tool_finish_alias_message("{\"message\": 42}") == NULL);
    JC_CHECK(jc_tool_finish_alias_message("[\"message\", \"x\"]") == NULL);
    JC_CHECK(jc_tool_finish_alias_message("message: done") == NULL);
    JC_CHECK(jc_tool_finish_alias_message("{\"result\": \"done\"}") == NULL);
    JC_CHECK(jc_tool_finish_alias_message(NULL) == NULL);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_tool_registry_init(&reg);
    jc_tool_registry_register(&reg, jc_tool_hint());
    app.tools = &reg;

    /* the error for a finish-like name says how a turn ends */
    jc_tool_execute(&reg, "finish", "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) { /* a guard (M729) */
        JC_CHECK(strstr(res.content, "There is no 'finish' tool") != NULL);
        JC_CHECK(strstr(res.content, "make no tool call") != NULL);
    }
    jc_tool_result_free(&res);
    jc_tool_execute(&reg, "final_answer", "{\"message\": \"x\"}", &res, &app);
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content, "There is no 'final_answer' tool") != NULL);
    }
    jc_tool_result_free(&res);

    /* CONTROL: any other unknown name keeps today's answer */
    jc_tool_execute(&reg, "definitely_not_a_tool", "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    if (JC_REQUIRE(res.content != NULL)) {
        JC_CHECK(strstr(res.content,
                        "unknown tool 'definitely_not_a_tool'") != NULL);
        JC_CHECK(strstr(res.content, "There is no") == NULL);
    }
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M855 (N34): which shell commands run `git stash` -- the trigger for a parallel write
 * child's hint that the stash is shared and not kept. The drive's spellings, `-C`, a
 * compound command; and what is NOT a stash: another subcommand that mentions the word,
 * a word that merely contains "git", nothing. */
void test_tool_git_stash_cmd(void)
{
    JC_CHECK(jc_cmd_runs_git_stash("git stash") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("git stash pop") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("cd src && git stash -u") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("git -C /work stash list") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("git -c user.name=x stash") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("git status; git stash") == 1);
    JC_CHECK(jc_cmd_runs_git_stash("git log --grep=stash") == 0);
    JC_CHECK(jc_cmd_runs_git_stash("git commit -m 'stash later'") == 0);
    JC_CHECK(jc_cmd_runs_git_stash("legit stash") == 0);
    JC_CHECK(jc_cmd_runs_git_stash("gitstash") == 0);
    JC_CHECK(jc_cmd_runs_git_stash("git") == 0);
    JC_CHECK(jc_cmd_runs_git_stash("") == 0);
    JC_CHECK(jc_cmd_runs_git_stash(NULL) == 0);
}

/* M863 (N44 (a)): which shell commands discard work or move HEAD -- the trigger
 * for keeping a write child's worktree first. Run 0090's spellings; the harmless
 * forms (a new branch, unstaging, a dry run, help); and what is not git at all. */
void test_tool_git_discards_cmd(void)
{
    char w[16];

    JC_CHECK(jc_cmd_git_discards("git checkout src/x.zig", w, sizeof w) == 1 &&
             strcmp(w, "checkout") == 0);
    JC_CHECK(jc_cmd_git_discards("git checkout -- src/x.zig", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git checkout 55342dd", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("cd /work && git reset --hard 9b2f3b0", w, sizeof w) == 1 &&
             strcmp(w, "reset") == 0);
    JC_CHECK(jc_cmd_git_discards("git restore .", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git restore --staged --worktree f", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git clean -fd", w, sizeof w) == 1 &&
             strcmp(w, "clean") == 0);
    JC_CHECK(jc_cmd_git_discards("git switch main", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git rebase -i HEAD~3", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git -C /work checkout f", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git status; git checkout f", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("git reset HEAD~1", NULL, 0) == 1);
    /* harmless forms */
    JC_CHECK(jc_cmd_git_discards("git checkout -b feature", w, sizeof w) == 0 &&
             w[0] == '\0');
    JC_CHECK(jc_cmd_git_discards("git switch -c feature", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git restore --staged f", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git reset", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git reset; git status", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git clean -n", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git clean -fdn", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git clean --dry-run -d", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git checkout --help", w, sizeof w) == 0);
    /* not these commands */
    JC_CHECK(jc_cmd_git_discards("git status", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git diff HEAD", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git stash", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("git log --grep=reset", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("echo git checkout", w, sizeof w) == 1);
    JC_CHECK(jc_cmd_git_discards("legit checkout f", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards("", w, sizeof w) == 0);
    JC_CHECK(jc_cmd_git_discards(NULL, w, sizeof w) == 0);
}

/* M949: the text-tool-call recovery pass. The teeth here are PRECISION
 * (data fences, unregistered names and intent prose are not calls) and ORDER
 * (two blocks run in document order); failed extractions must still let the
 * other blocks land. Every assertion guards its dereference so a red run
 * fails cleanly instead of crashing. */
static void tcr_free_calls(struct jc_vec *out)
{
    jc_size i;
    for (i = 0; i < out->len; i++) {
        struct jc_tool_call *tc = (struct jc_tool_call *)jc_vec_at(out, i);
        free(tc->id);
        free(tc->name);
        free(tc->arguments_json);
    }
    jc_vec_free(out);
}

void test_toolcall_recover(void)
{
    struct jc_tool_registry reg;

    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);

    /* Shape 2: a fenced json block with name + arguments. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        struct jc_tool_call *tc;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "I'll read it:\n```json\n{\"name\": \"read_file\", "
            "\"arguments\": {\"path\": \"a.c\"}}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 1);
        tc = (n == 1 && out.len == 1)
                ? (struct jc_tool_call *)jc_vec_at(&out, 0) : NULL;
        JC_CHECK(tc != NULL);
        if (tc != NULL) {
            JC_CHECK(tc->name != NULL && strcmp(tc->name, "read_file") == 0);
            JC_CHECK(tc->id != NULL &&
                     strncmp(tc->id, "rec-read_file-", 14) == 0);
            JC_CHECK(tc->arguments_json != NULL &&
                     strstr(tc->arguments_json, "a.c") != NULL);
        }
        JC_CHECK(reason.len == 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* Shape 1: a fence tagged with the tool name, body = the arguments. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        struct jc_tool_call *tc;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "call:\n```run_terminal_command\n"
            "{\"command\": \"echo hi\"}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 1);
        tc = (n == 1 && out.len == 1)
                ? (struct jc_tool_call *)jc_vec_at(&out, 0) : NULL;
        JC_CHECK(tc != NULL);
        if (tc != NULL) {
            JC_CHECK(strcmp(tc->name, "run_terminal_command") == 0);
        }
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* Shape 3: a bare line that is exactly the call object. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "Doing it now\n{\"name\": \"list_files\", \"args\": "
            "{\"path\": \".\"}}\nand then done\n",
            &reg, &out, &reason);
        JC_CHECK(n == 1);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* Two blocks: document order is preserved. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        struct jc_tool_call *tc0;
        struct jc_tool_call *tc1;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "```json\n{\"name\": \"write_file\", \"arguments\": "
            "{\"path\": \"one\", \"content\": \"x\"}}\n```\n"
            "```json\n{\"name\": \"write_file\", \"arguments\": "
            "{\"path\": \"two\", \"content\": \"y\"}}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 2);
        tc0 = (n == 2 && out.len == 2)
                  ? (struct jc_tool_call *)jc_vec_at(&out, 0) : NULL;
        tc1 = (n == 2 && out.len == 2)
                  ? (struct jc_tool_call *)jc_vec_at(&out, 1) : NULL;
        if (tc0 != NULL) {
            JC_CHECK(tc0->arguments_json != NULL &&
                     strstr(tc0->arguments_json, "one") != NULL);
        }
        if (tc1 != NULL) {
            JC_CHECK(tc1->arguments_json != NULL &&
                     strstr(tc1->arguments_json, "two") != NULL);
        }
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* A DATA fence (no name key) is not a call. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "The config:\n```json\n{\"path\": \"a.c\", \"count\": 3}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* An unregistered tool name is never a call. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "```json\n{\"name\": \"launch_missiles\", \"arguments\": {}}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* Intent prose is never a call. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "I will now use read_file to look at a.c.",
            &reg, &out, &reason);
        JC_CHECK(n == 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* A fence tagged with an unregistered tool is a prose example: skipped
     * WITHOUT a reason note, and it must not shadow a real call after it. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        struct jc_tool_call *tc;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "Example shape:\n```some_external_tool\n"
            "{\"arg\": 1}\n```\n"
            "```json\n{\"name\": \"list_files\", \"arguments\": {}}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 1);
        tc = (n == 1 && out.len == 1)
                ? (struct jc_tool_call *)jc_vec_at(&out, 0) : NULL;
        if (tc != NULL) {
            JC_CHECK(strcmp(tc->name, "list_files") == 0);
        }
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* Arguments that are not an object: rejected with a reason note. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "```json\n{\"name\": \"list_files\", \"arguments\": [1, 2]}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 0);
        JC_CHECK(reason.len > 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* One bad block does not poison a good one after it. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        n = jc_toolcall_recover(
            "```json\n{\"name\": \"no_such_tool\", \"arguments\": {}}\n```\n"
            "```json\n{\"name\": \"list_files\", \"arguments\": {}}\n```\n",
            &reg, &out, &reason);
        JC_CHECK(n == 1);
        JC_CHECK(reason.len > 0); /* the unregistered name is noted */
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* NULL / empty inputs are refused, not crashed. */
    {
        struct jc_vec out;
        struct jc_sb reason;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        JC_CHECK(jc_toolcall_recover(NULL, &reg, &out, &reason) == 0);
        JC_CHECK(jc_toolcall_recover("", &reg, &out, &reason) == 0);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* M1016 A-05 red-first: unclosed fence at EOF without trailing newline */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        char *text;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        /* Reply ends on an opening json fence, no trailing newline. */
        text = (char *)malloc(strlen("hello\n```json") + 1);
        strcpy(text, "hello\n```json");
        n = jc_toolcall_recover(text, &reg, &out, &reason);
        free(text);
        JC_CHECK(n == 0);
        JC_CHECK(reason.len > 0);
        JC_CHECK(strstr(reason.data, "a code block is not closed") != NULL);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    /* M1016 A-05 red-first: unclosed fence at EOF with registered tool tag */
    {
        struct jc_vec out;
        struct jc_sb reason;
        int n;
        char *text;
        jc_vec_init(&out, sizeof(struct jc_tool_call));
        jc_sb_init(&reason);
        /* Fence tag IS a registered tool name, but no following line. */
        text = (char *)malloc(strlen("x\n```read_file") + 1);
        strcpy(text, "x\n```read_file");
        n = jc_toolcall_recover(text, &reg, &out, &reason);
        free(text);
        JC_CHECK(n == 0);
        JC_CHECK(reason.len > 0);
        JC_CHECK(strstr(reason.data, "a code block is not closed") != NULL);
        tcr_free_calls(&out);
        jc_sb_free(&reason);
    }

    jc_tool_registry_free(&reg);
}

/* L-25: run_terminal_command's timeout parameter must not invite a cap on measurement runs */
void test_run_timeout_schema(void)
{
    const struct jc_tool *run_tool;
    cJSON *schema;
    cJSON *props, *timeout_prop, *desc;

    /* Build the tool registry the way test_memory.c does */
    struct jc_tool_registry reg;
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);

    /* Take the 'run_terminal_command' tool */
    run_tool = jc_tool_registry_find(&reg, "run_terminal_command");
    JC_CHECK(run_tool != NULL);

    /* Call its schema() function */
    schema = run_tool->schema();
    JC_CHECK(schema != NULL);

    /* Read the 'timeout' property's description */
    props = cJSON_GetObjectItem(schema, "properties");
    JC_CHECK(props != NULL);
    timeout_prop = cJSON_GetObjectItem(props, "timeout");
    JC_CHECK(timeout_prop != NULL);
    desc = cJSON_GetObjectItem(timeout_prop, "description");
    JC_CHECK(desc != NULL && desc->type == cJSON_String);
    {
        const char *d = desc->valuestring;
        JC_CHECK(d != NULL);

        /* (1) the description does NOT contain "test suite" */
        JC_CHECK(strstr(d, "test suite") == NULL);

        /* (2) it does NOT contain "run long" */
        JC_CHECK(strstr(d, "run long") == NULL);

        /* (3) it DOES contain "might hang" (the purpose) */
        JC_CHECK(strstr(d, "might hang") != NULL);

        /* (4) it DOES contain a measurement caveat: "measure" or "reproduce" */
        JC_CHECK(strstr(d, "measure") != NULL || strstr(d, "reproduce") != NULL);
    }

    cJSON_Delete(schema);
    jc_tool_registry_free(&reg);
}
