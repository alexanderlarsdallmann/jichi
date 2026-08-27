/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_hint.c - the `hint` tool: on-demand escalating hint ladder (B2). */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_assign.h"
#include "jc_mem.h"

#include <stdlib.h>
#include <string.h>

static const char *SPEC =
    "---\n"
    "title: T\n"
    "audience: junior\n"
    "hints:\n"
    "  - Read the parser.\n"
    "  - Handle the empty case.\n"
    "---\n"
    "Body.\n";

void test_hint(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    struct jc_assign_spec spec;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_tool_registry_init(&reg);
    jc_tool_registry_register(&reg, jc_tool_hint());
    app.tools = &reg;

    /* No active assignment -> graceful no-op, not an error. */
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(strstr(res.content, "No hints") != NULL);
    jc_tool_result_free(&res);

    /* Load a 2-hint spec as the active assignment. */
    JC_CHECK(jc_assign_parse(SPEC, &spec, a) == JC_OK);
    JC_CHECK(spec.nhints == 2);
    app.assignment = &spec;
    app.hints_used = 0;

    /* M617: in TUTOR mode the ladder belongs to the HUMAN learner. The tool
     * used to serve the model anyway, so a tutor-stance model could burn the
     * human's rungs, writing hints.jsonl rows attributed to the learner --
     * the prompt said "guide, never solve" and nothing fenced the ladder
     * (caps-vs-fences, CLAUDE.md). Fenced: the rung is not revealed, the
     * counter does not move, and the reply says whose ladder it is. */
    app.assignment_tutor = 1;
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(strstr(res.content, "tutor") != NULL);
    JC_CHECK(strstr(res.content, "Hint 1 of 2") == NULL);
    JC_CHECK(app.hints_used == 0);
    jc_tool_result_free(&res);
    app.assignment_tutor = 0;

    /* First call reveals hint 1 (and reports one remaining). */
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(strstr(res.content, "Hint 1 of 2") != NULL);
    JC_CHECK(strstr(res.content, "Read the parser") != NULL);
    JC_CHECK(strstr(res.content, "1 more hint") != NULL);
    jc_tool_result_free(&res);

    /* Second call escalates to hint 2 (the last). */
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(strstr(res.content, "Hint 2 of 2") != NULL);
    JC_CHECK(strstr(res.content, "empty case") != NULL);
    JC_CHECK(strstr(res.content, "last hint") != NULL);
    jc_tool_result_free(&res);

    /* Exhausted -> a friendly "no more" message, still not an error. */
    jc_tool_execute(&reg, "hint", "{}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(strstr(res.content, "No more hints") != NULL);
    JC_CHECK(app.hints_used == 2);
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}
