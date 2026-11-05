/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_subagent.c - offline tests for the spawn_subagent seams.
 *
 * A full nested run needs a provider (network), so these cover the pure/early
 * pieces: model-selector resolution, the tool-exclusion build, the depth guard,
 * argument validation (all of which return before any provider is used), and
 * final-answer extraction.
 */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_agent.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_message.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Build a config with two models for selector tests. Strings are literals
 * (only read, never freed). */
static void make_models(struct jc_config *cfg)
{
    struct jc_model_cfg m;

    memset(cfg, 0, sizeof(*cfg));
    jc_vec_init(&cfg->models, sizeof(struct jc_model_cfg));

    memset(&m, 0, sizeof(m));
    m.name = "Main"; m.model = "big-model"; m.provider = "openai";
    m.roles = JC_ROLE_CHAT;
    jc_vec_push(&cfg->models, &m);

    memset(&m, 0, sizeof(m));
    m.name = "Fast"; m.model = "small-model"; m.provider = "openai";
    m.roles = JC_ROLE_SUMMARIZE;
    jc_vec_push(&cfg->models, &m);

    cfg->active = 0;
    cfg->max_subagent_depth = 1;
    cfg->max_subagent_iters = 5;
}

static void test_resolve_model(void)
{
    struct jc_config cfg;
    int found;
    make_models(&cfg);

    /* Empty selector => active model. */
    found = 0;
    JC_CHECK(jc_subagent_resolve_model(&cfg, NULL, &found)
             == jc_config_model_at(&cfg, 0));
    JC_CHECK(found == 1);

    /* Name/id substring. */
    JC_CHECK(jc_subagent_resolve_model(&cfg, "small", &found)
             == jc_config_model_at(&cfg, 1));
    JC_CHECK(found == 1);

    /* 1-based index. */
    JC_CHECK(jc_subagent_resolve_model(&cfg, "2", &found)
             == jc_config_model_at(&cfg, 1));

    /* Role name. */
    JC_CHECK(jc_subagent_resolve_model(&cfg, "summarize", &found)
             == jc_config_model_at(&cfg, 1));

    /* No match. */
    found = 1;
    JC_CHECK(jc_subagent_resolve_model(&cfg, "no-such-model", &found) == NULL);
    JC_CHECK(found == 0);

    /* M745: the words the tool's own description uses. On the 2026-09-24
     * drive a parent passed "model": "default" and was refused. */
    found = 0;
    JC_CHECK(jc_subagent_resolve_model(&cfg, "default", &found)
             == jc_config_model_at(&cfg, 0));
    JC_CHECK(found == 1);
    found = 0;
    JC_CHECK(jc_subagent_resolve_model(&cfg, "Current", &found)
             == jc_config_model_at(&cfg, 0));
    JC_CHECK(found == 1);
    /* ...and a real role nobody declares runs on the current model, flagged
     * (the schema's own example, "summarize", failed where no model had it). */
    found = 0;
    JC_CHECK(jc_subagent_resolve_model(&cfg, "embed", &found)
             == jc_config_model_at(&cfg, 0));
    JC_CHECK(found == 2);

    jc_vec_free(&cfg.models);
}

/* M745: a refusal names the choices. */
static void test_model_menu(void)
{
    struct jc_config cfg;
    struct jc_sb sb;
    make_models(&cfg);
    jc_sb_init(&sb);
    jc_subagent_model_menu(&cfg, &sb);
    JC_CHECK(sb.data != NULL &&
             strcmp(sb.data, "1 'Main' (big-model), 2 'Fast' (small-model)") == 0);
    jc_sb_free(&sb);
    jc_vec_free(&cfg.models);

    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.models, sizeof(struct jc_model_cfg));
    jc_sb_init(&sb);
    jc_subagent_model_menu(&cfg, &sb);
    JC_CHECK(sb.data != NULL && strcmp(sb.data, "(none configured)") == 0);
    jc_sb_free(&sb);
    jc_vec_free(&cfg.models);
}

static void test_build_neutral_excludes_spawn(void)
{
    struct jc_tool_registry reg;
    cJSON *arr;
    cJSON *e;
    int saw_spawn;
    int saw_read;

    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);

    /* spawn_subagent IS registered as a built-in. */
    JC_CHECK(jc_tool_registry_find(&reg, "spawn_subagent") != NULL);

    /* Excluded by name even with mutating tools included. */
    arr = jc_tool_build_neutral_ex(&reg, 1, NULL, "spawn_subagent", NULL,
                                   NULL, 1);
    saw_spawn = 0; saw_read = 0;
    for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
        const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
        if (strcmp(nm, "spawn_subagent") == 0) { saw_spawn = 1; }
        if (strcmp(nm, "read_file") == 0) { saw_read = 1; }
    }
    JC_CHECK(saw_spawn == 0);
    JC_CHECK(saw_read == 1);
    cJSON_Delete(arr);

    /* It is mutating, so include_mutating=0 also hides it. */
    arr = jc_tool_build_neutral_ex(&reg, 0, NULL, NULL, NULL, NULL, 0);
    saw_spawn = 0;
    for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
        const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
        if (strcmp(nm, "spawn_subagent") == 0) { saw_spawn = 1; }
    }
    JC_CHECK(saw_spawn == 0);
    cJSON_Delete(arr);

    jc_tool_registry_free(&reg);
}

/* Minimal app for executing spawn_subagent's early-return paths. */
static void setup_app(struct jc_app *app, struct jc_arena *a,
                      struct jc_tool_registry *reg)
{
    memset(app, 0, sizeof(*app));
    app->arena = a;
    jc_vec_init(&app->read_files, sizeof(char *));
    jc_vec_init(&app->read_recs, sizeof(struct jc_read_rec));
    make_models(&app->config);
    jc_tool_registry_init(reg);
    jc_tool_register_builtins(reg);
    app->tools = reg;
    strcpy(app->cwd, jc_test_tmpdir());
}

static void test_depth_guard_and_args(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;

    setup_app(&app, a, &reg);

    /* Depth at the cap => refuse before doing anything (no provider needed). */
    app.agent_depth = app.config.max_subagent_depth;
    jc_tool_execute(&reg, "spawn_subagent", "{\"task\":\"do x\"}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL && strstr(res.content, "depth") != NULL);
    jc_tool_result_free(&res);

    /* Below the cap: missing task => error (returns before any model use). */
    app.agent_depth = 0;
    jc_tool_execute(&reg, "spawn_subagent", "{}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL && strstr(res.content, "task") != NULL);
    jc_tool_result_free(&res);

    /* Unmatched model selector => error (returns before any provider). */
    jc_tool_execute(&reg, "spawn_subagent",
                    "{\"task\":\"x\",\"model\":\"no-such\"}", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL && strstr(res.content, "model") != NULL);
    jc_tool_result_free(&res);

    jc_vec_free(&app.config.models);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

static void test_can_spawn(void)
{
    /* Default single-level (max_depth=1): only the top-level agent (depth 0)
     * may spawn; a depth-1 subagent may not. */
    JC_CHECK(jc_subagent_can_spawn(0, 1) == 1);
    JC_CHECK(jc_subagent_can_spawn(1, 1) == 0);

    /* Multi-level (max_depth=2): depth 0 and depth 1 may spawn, depth 2 not. */
    JC_CHECK(jc_subagent_can_spawn(0, 2) == 1);
    JC_CHECK(jc_subagent_can_spawn(1, 2) == 1);
    JC_CHECK(jc_subagent_can_spawn(2, 2) == 0);

    /* Disabled (max_depth=0): no spawning at all. */
    JC_CHECK(jc_subagent_can_spawn(0, 0) == 0);

    /* Already past the cap stays refused (defensive). */
    JC_CHECK(jc_subagent_can_spawn(3, 2) == 0);
}

static void test_iters_taper(void)
{
    /* Depth 0 (top level, not a subagent) is unchanged. */
    JC_CHECK(jc_subagent_iters_at_depth(25, 0) == 25);
    /* Each level halves. */
    JC_CHECK(jc_subagent_iters_at_depth(25, 1) == 12);
    JC_CHECK(jc_subagent_iters_at_depth(25, 2) == 6);
    /* Floored at JC_SUBAGENT_MIN_ITERS so a deep chain still makes progress. */
    JC_CHECK(jc_subagent_iters_at_depth(25, 3) == JC_SUBAGENT_MIN_ITERS);
    JC_CHECK(jc_subagent_iters_at_depth(25, 10) == JC_SUBAGENT_MIN_ITERS);
    /* A small base is floored immediately. */
    JC_CHECK(jc_subagent_iters_at_depth(4, 1) == JC_SUBAGENT_MIN_ITERS);
    /* Negative depth is treated as top-level. */
    JC_CHECK(jc_subagent_iters_at_depth(25, -1) == 25);
}

static void test_last_assistant_text(void)
{
    struct jc_history h;
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "question");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "an early answer");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "the final answer");
    jc_history_add_tool_result(&h, "id1", "tool output", 0);
    /* Most recent assistant text, skipping the trailing tool result. */
    JC_CHECK_STR(jc_agent_last_assistant_text(&h), "the final answer");
    jc_history_free(&h);

    /* No assistant message => NULL. */
    {
        struct jc_history h2;
        jc_history_init(&h2);
        jc_history_add(&h2, JC_ROLE_USER, "only a question");
        JC_CHECK(jc_agent_last_assistant_text(&h2) == NULL);
        jc_history_free(&h2);
    }

    /* M715: THIS TURN'S answer, in a history that holds more than one turn --
     * the resumed session in which the structured outputs reported a capped
     * turn's predecessor's answer as its own. Turn 1 answered; turn 2 made a
     * tool call and wrote no text. Searching from turn 2's first message finds
     * nothing, and must not reach back into turn 1. */
    {
        struct jc_history h3;
        jc_size turn2;
        jc_history_init(&h3);
        jc_history_add(&h3, JC_ROLE_USER, "first question");
        jc_history_add(&h3, JC_ROLE_ASSISTANT, "turn one's answer");
        turn2 = jc_history_len(&h3);
        jc_history_add(&h3, JC_ROLE_USER, "second question");
        jc_history_add(&h3, JC_ROLE_ASSISTANT, "");
        jc_history_add_tool_result(&h3, "id2", "tool output", 0);
        JC_CHECK(jc_agent_turn_answer(&h3, turn2) == NULL);
        /* The whole-history form still finds turn 1's -- which is exactly why
         * the per-turn callers stopped using it. */
        JC_CHECK_STR(jc_agent_last_assistant_text(&h3), "turn one's answer");
        /* And a turn that DID answer is found from its own boundary. */
        jc_history_add(&h3, JC_ROLE_ASSISTANT, "turn two's answer");
        JC_CHECK_STR(jc_agent_turn_answer(&h3, turn2), "turn two's answer");
        jc_history_free(&h3);
    }
}


/* M436: a tool the depth gate will refuse must not be ADVERTISED at depth.
 *
 * THE MEASURED WASTE. todowrite/todoread/board are `readonly: 1` (they touch no
 * filesystem, only agent state), so they survived every filter in
 * jc_tool_build_neutral_ex and were advertised to every subagent -- which then
 * called them and was refused at runtime by an `agent_depth > 0` check inside each
 * tool body. On a cacheless backend at the measured 25-42k input tokens per call,
 * each of those round trips is real money, and the model learns nothing from the
 * refusal that omission would not have told it sooner.
 *
 * The GATE is right: a delegate must not stomp the user's task list, which is shared
 * with the human and outlives the subtask. Only the advertisement was wrong.
 *
 * The one fact now lives on `struct jc_tool` as `main_agent_only`, read by the
 * builder (omission) and by jc_tool_execute (backstop) -- the M296 shape, one
 * mechanism rather than a field for advertising plus three hand-written checks. */
static void test_main_agent_only_advertising(void)
{
    struct jc_tool_registry reg;
    cJSON *arr;
    cJSON *e;
    int at0_todo, at0_board, at1_todo, at1_board, at1_read;

    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    /* `board` is registered only when config.board is on (jc_tool.c), so the
     * unconditional builtins do not include it -- registering it by hand here is
     * what lets this test cover all three gated tools. The first cut omitted this
     * and SEGFAULTED on a NULL registry lookup, which is a better failure than the
     * alternative: a find that quietly returned NULL and skipped the assertion. */
    jc_tool_registry_register(&reg, jc_tool_board());

    /* --- depth 0: all three are advertised, as before ---------------------- */
    arr = jc_tool_build_neutral_ex(&reg, 1, NULL, NULL, NULL, NULL, 0);
    at0_todo = 0; at0_board = 0;
    for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
        const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
        if (strcmp(nm, "todowrite") == 0) { at0_todo = 1; }
        if (strcmp(nm, "board") == 0) { at0_board = 1; }
    }
    JC_CHECK(at0_todo == 1);
    JC_CHECK(at0_board == 1);
    cJSON_Delete(arr);

    /* --- depth 1: omitted, and the rest of the toolset is untouched -------- */
    arr = jc_tool_build_neutral_ex(&reg, 1, NULL, NULL, NULL, NULL, 1);
    at1_todo = 0; at1_board = 0; at1_read = 0;
    for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
        const char *nm = cJSON_GetObjectItem(e, "name")->valuestring;
        if (strcmp(nm, "todowrite") == 0) { at1_todo = 1; }
        if (strcmp(nm, "todoread") == 0) { at1_todo = 1; }
        if (strcmp(nm, "board") == 0) { at1_board = 1; }
        if (strcmp(nm, "read_file") == 0) { at1_read = 1; }
    }
    JC_CHECK(at1_todo == 0);
    JC_CHECK(at1_board == 0);
    /* The check that makes the two above mean something: a filter that dropped
     * everything would satisfy them and be a catastrophe. */
    JC_CHECK(at1_read == 1);
    cJSON_Delete(arr);

    /* --- the field is set on exactly the tools that had a hand-written gate -
     * Asserted on the definitions rather than on the array, so a future tool that
     * grows a depth gate without the field is not silently mis-advertised. Three
     * tools had one: todowrite, todoread (via a shared `main_agent_only` helper)
     * and board (its own inline check). */
    JC_CHECK(jc_tool_registry_find(&reg, "todowrite")->main_agent_only == 1);
    JC_CHECK(jc_tool_registry_find(&reg, "todoread")->main_agent_only == 1);
    JC_CHECK(jc_tool_registry_find(&reg, "board")->main_agent_only == 1);
    /* and NOT on a tool a delegate legitimately needs */
    JC_CHECK(jc_tool_registry_find(&reg, "read_file")->main_agent_only == 0);
    JC_CHECK(jc_tool_registry_find(&reg, "write_file")->main_agent_only == 0);
    JC_CHECK(jc_tool_registry_find(&reg, "run_terminal_command")
                 ->main_agent_only == 0);

    jc_tool_registry_free(&reg);
}


/* def-318: a subagent's final answer is the parent's tool result -- capped
 * with a claim ticket at the boundary (M911). The measured wound (2026-10-05
 * audit): 14 of 17 sibling-log hand-backs over 10 KB, max 31 KB, 301 KB
 * total, landing in the parent's context verbatim. */
static void test_subagent_answer_capped(void)
{
    struct jc_app app;
    struct jc_arena *a = jc_arena_new(0);
    struct jc_tool_registry reg;
    struct jc_history h;
    const char *ans = NULL;
    char saved_home[300];
    char home[300];
    char text[13000];
    int i;

    setup_app(&app, a, &reg);
    jc_snprintf(saved_home, sizeof(saved_home), "%s",
                getenv("HOME") != NULL ? getenv("HOME") : "");
    jc_snprintf(home, sizeof(home), "%s/subag_%ld",
                jc_test_tmpdir(), (long)getpid());
    jc_mkdir_p(home);
    setenv("HOME", home, 1);

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "do the thing");

    for (i = 0; i < 12000; i++) {
        text[i] = 'A';
    }
    text[12000] = '\0';
    jc_history_add(&h, JC_ROLE_ASSISTANT, text);

    JC_CHECK(jc_subagent_answer_into(&app, &h, &ans) == JC_OK);

    JC_REQUIRE(ans != NULL);
    /* The parent's context gets the head, not the whole answer. */
    JC_CHECK(strlen(ans) < 12000);
    /* The full answer is on disk, named by the note the parent reads. */
    JC_CHECK(strstr(ans, "complete answer is at") != NULL);
    /* The byte order is "opening of total" -- head (3796) of len (12000).
     * Reversed it would read "12000 of 3796", claiming the note carries more
     * bytes than the whole answer. (F-J4, peer finding.) */
    JC_CHECK(strstr(ans, "3796 of 12000 bytes") != NULL);
    /* The capped answer is ARENA-owned (scratch), as before M911 -- do NOT
     * free it. The per-turn arena is freed by jc_arena_free below. */
    jc_history_free(&h);
    ans = NULL;

    /* A short answer passes through untouched, no note. */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "do the thing");
    jc_history_add(&h, JC_ROLE_ASSISTANT, "done, and here is why");
    JC_CHECK(jc_subagent_answer_into(&app, &h, &ans) == JC_OK);
    JC_CHECK_STR(ans, "done, and here is why");
    jc_history_free(&h);

    setenv("HOME", saved_home, 1);
    jc_vec_free(&app.config.models);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}


/* M965 (O1): test register() warn-on-duplicate vs replace(). */
static void test_registry_register_vs_replace(void)
{
    struct jc_tool_registry reg;
    const struct jc_tool *t;
    const struct jc_tool *first_found;
    int count_dup;
    jc_size i;

    /* Fake tool structs (only name/description are meaningful). Ten positional
     * C89 initializers: name, description, schema, readonly, run, ctx,
     * schema_ctx, run_ctx, main_agent_only, plan_allowed. */
    static const struct jc_tool tool1 = {
        "dup_probe", "first description", 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const struct jc_tool tool2 = {
        "dup_probe", "second description", 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const struct jc_tool tool3 = {
        "dup_probe", "third description", 0, 0, 0, 0, 0, 0, 0, 0
    };

    jc_tool_registry_init(&reg);

    /* Test 1: register() twice with same name should keep FIRST (warns+ignores). */
    jc_tool_registry_register(&reg, &tool1);
    jc_tool_registry_register(&reg, &tool2);

    /* Count how many 'dup_probe' entries exist. */
    count_dup = 0;
    first_found = NULL;
    for (i = 0; i < reg.tools.len; i++) {
        const struct jc_tool *tt =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&reg.tools, i);
        if (strcmp(tt->name, "dup_probe") == 0) {
            count_dup++;
            if (first_found == NULL) {
                first_found = tt;
            }
        }
    }
    JC_CHECK(count_dup == 1);
    JC_CHECK(first_found == &tool1);  /* should be the FIRST one */
    JC_CHECK(strcmp(first_found->description, "first description") == 0);

    /* Test 2: replace() with a third tool should replace in place. */
    jc_tool_registry_replace(&reg, &tool3);

    /* Now there should still be exactly one 'dup_probe', but it should be tool3. */
    count_dup = 0;
    first_found = NULL;
    for (i = 0; i < reg.tools.len; i++) {
        const struct jc_tool *tt =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&reg.tools, i);
        if (strcmp(tt->name, "dup_probe") == 0) {
            count_dup++;
            if (first_found == NULL) {
                first_found = tt;
            }
        }
    }
    JC_CHECK(count_dup == 1);
    JC_CHECK(first_found == &tool3);  /* should now be the REPLACEMENT */
    JC_CHECK(strcmp(first_found->description, "third description") == 0);

    /* Verify find() returns the replacement. */
    t = jc_tool_registry_find(&reg, "dup_probe");
    JC_CHECK(t == &tool3);
    JC_CHECK(strcmp(t->description, "third description") == 0);

    jc_tool_registry_free(&reg);
}

void test_subagent(void)
{
    test_resolve_model();
    test_model_menu();
    test_build_neutral_excludes_spawn();
    test_can_spawn();
    test_iters_taper();
    test_depth_guard_and_args();
    test_last_assistant_text();
    test_subagent_answer_capped();
    test_main_agent_only_advertising();
    test_registry_register_vs_replace();
}
