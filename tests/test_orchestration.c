/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_orchestration.c - multi-agent control invariants (M62 hardening).
 *
 * Covers the seams where the supervisor could lose control of spawned agents:
 * the child progress/result protocol incl. the new tool-call count (fix #3), the
 * depth/advertisement invariant (a subagent never sees spawn_parallel, and only
 * sees spawn_subagent below the cap), and the tool-call budget trip that the
 * parallel merge feeds. A live fork pool is exercised by tests/e2e/parallel_*.py.
 */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_agent.h"
#include "jc_parallel.h"
#include "jc_envelope.h"
#include "jc_json.h"

#include <stdlib.h>
#include <string.h>

static int has_tool(cJSON *arr, const char *name)
{
    cJSON *e;
    for (e = (arr != NULL) ? arr->child : NULL; e != NULL; e = e->next) {
        cJSON *nm = cJSON_GetObjectItem(e, "name");
        if (nm != NULL && nm->valuestring != NULL &&
            strcmp(nm->valuestring, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Fix #3 wire format: a child's "done" message carries its tool-call count so
 * the parent can charge it against the run's budget. */
static void test_pmsg_tool_count(void)
{
    struct jc_pmsg m;

    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"answer\":\"ok\",\"tokens\":12,\"tools\":3}", &m)
        == JC_PMSG_DONE);
    JC_CHECK(m.tool_calls == 3);
    JC_CHECK(m.tokens == 12.0);
    JC_CHECK_STR(m.answer, "ok");
    free(m.answer);
    free(m.error);

    /* Missing "tools" => 0 (back-compat with a child that predates the field). */
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"done\",\"answer\":\"x\"}", &m)
             == JC_PMSG_DONE);
    JC_CHECK(m.tool_calls == 0);
    free(m.answer);
    free(m.error);
}

/* The child protocol must never crash the parent on malformed input. */
static void test_pmsg_robust(void)
{
    struct jc_pmsg m;

    JC_CHECK(jc_parallel_parse_msg(NULL, &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("{not json", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"bogus\"}", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("{\"no\":\"t\"}", &m) == JC_PMSG_NONE);
    free(m.answer);
    free(m.error);

    /* A tool message with no name defaults safely. */
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"tool\"}", &m) == JC_PMSG_TOOL);
    JC_CHECK(m.tool[0] != '\0');
    free(m.answer);
    free(m.error);
}

/* The supervisor-control invariant: in a subagent run, spawn_parallel is never
 * advertised (no nested fork pools), and spawn_subagent is advertised only while
 * below the depth cap. This mirrors exactly what jc_agent_run_subagent computes. */
static void test_advertise_depth(void)
{
    struct jc_tool_registry reg;
    int max_depth = 2;
    int depth;

    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);

    for (depth = 0; depth <= max_depth; depth++) {
        const char *excl2 =
            jc_subagent_can_spawn(depth, max_depth) ? NULL : "spawn_subagent";
        cJSON *arr = jc_tool_build_neutral_ex(&reg, 1, NULL, "spawn_parallel",
                                              excl2, NULL, 0);
        JC_CHECK(has_tool(arr, "spawn_parallel") == 0); /* always hidden */
        if (depth < max_depth) {
            JC_CHECK(has_tool(arr, "spawn_subagent") == 1); /* may nest */
        } else {
            JC_CHECK(has_tool(arr, "spawn_subagent") == 0); /* at the cap */
        }
        cJSON_Delete(arr);
    }
    jc_tool_registry_free(&reg);
}

/* Fix #3 effect: merging the parallel children's tool calls is what makes the
 * tool-call budget trip — without the merge the cap would be evadable. */
static void test_budget_toolcalls_merge(void)
{
    struct jc_envelope e;

    memset(&e, 0, sizeof(e));
    e.max_tool_calls = 10;
    e.tool_calls = 3;                 /* the parent's own calls */
    JC_CHECK(jc_env_over_budget(&e, 0) == JC_BUDGET_NONE);

    e.tool_calls += 8;                /* + children merged back by the pool */
    JC_CHECK(jc_env_over_budget(&e, 0) == JC_BUDGET_TOOLCALLS);
}

/* The live board line is aligned and carries a fixed-width state tag (M65). */
static void test_board_line(void)
{
    char buf[256];

    jc_parallel_board_line(buf, sizeof(buf), 0, "gpt", JC_BOARD_RUN,
                           "edit_file", 1200.0);
    JC_CHECK(strncmp(buf, "[1] run ", 8) == 0);
    JC_CHECK(strstr(buf, "gpt") != NULL);
    JC_CHECK(strstr(buf, "edit_file") != NULL);
    JC_CHECK(strstr(buf, "(1.2k)") != NULL);

    jc_parallel_board_line(buf, sizeof(buf), 2, "haiku", JC_BOARD_DONE,
                           "", 3400.0);
    JC_CHECK(strncmp(buf, "[3] done", 8) == 0);
    JC_CHECK(strstr(buf, "(3.4k)") != NULL);

    jc_parallel_board_line(buf, sizeof(buf), 1, "m", JC_BOARD_FAIL,
                           "boom", 0.0);
    JC_CHECK(strncmp(buf, "[2] FAIL", 8) == 0);
    JC_CHECK(strstr(buf, "boom") != NULL);
    JC_CHECK(strstr(buf, "(") == NULL);   /* tokens<=0 => no count */

    jc_parallel_board_line(buf, sizeof(buf), 0, NULL, JC_BOARD_TIMEOUT,
                           "timed out", 0.0);
    JC_CHECK(strncmp(buf, "[1] time", 8) == 0);
    JC_CHECK(strstr(buf, "timed out") != NULL);
}

void test_orchestration(void)
{
    test_pmsg_tool_count();
    test_pmsg_robust();
    test_advertise_depth();
    test_budget_toolcalls_merge();
    test_board_line();
}
