/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_reach.c - the reach footer is derived from counts, never from prose,
 * and every absence is STATED (M630). */

#include "jc_test.h"
#include "jc_reach.h"
#include "cJSON.h"

#include <string.h>

static void test_no_envelope(void)
{
    struct jc_reach r;
    char buf[700];
    memset(&r, 0, sizeof r);
    r.tool_calls = 3; r.tool_errors = 1;
    jc_reach_line(&r, buf, sizeof buf);
    /* a zero-or-more count is a claim, printed; the absence is a sentence */
    JC_CHECK(strstr(buf, "checked: 3 tool calls, 1 error (0 refused by a fence)")
             != NULL); /* M638: the zero is a claim too */
    JC_CHECK(strstr(buf, "not checked: no envelope armed") != NULL);
    JC_CHECK(strstr(buf, "verify") == NULL); /* no verifier: no colour claimed */
}

static void test_verifier_green_and_scope(void)
{
    struct jc_reach r;
    char buf[700];
    cJSON *o;
    memset(&r, 0, sizeof r);
    r.envelope = 1; r.verifier_armed = 1; r.verify_green = 1;
    r.scope_armed = 1; r.tool_calls = 2;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "checked: verify green") != NULL);
    JC_CHECK(strstr(buf, "writes in scope") != NULL);
    JC_CHECK(strstr(buf, "0 test edits") != NULL);
    JC_CHECK(strstr(buf, "not checked: (nothing") != NULL);
    o = jc_reach_json(&r);
    JC_CHECK(o != NULL);
    JC_CHECK(strcmp(cJSON_GetObjectItem(o, "verify")->valuestring, "green") == 0);
    JC_CHECK(strcmp(cJSON_GetObjectItem(o, "scope")->valuestring, "clean") == 0);
    JC_CHECK(cJSON_GetObjectItem(o, "tool_errors")->valueint == 0);
    cJSON_Delete(o);
}

static void test_red_violation_shell(void)
{
    struct jc_reach r;
    char buf[700];
    cJSON *o;
    memset(&r, 0, sizeof r);
    r.envelope = 1; r.verifier_armed = 1; r.verify_red = 1; r.rolled_back = 1;
    r.scope_armed = 1; r.scope_violations = 2; r.shell_ran = 1;
    r.tool_calls = 5; r.tool_errors = 2; r.test_edits = 1;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "verify RED (work rolled back") != NULL);
    JC_CHECK(strstr(buf, "2 writes OUTSIDE the edit scope") != NULL);
    JC_CHECK(strstr(buf, "1 test edit ") != NULL || strstr(buf, "1 test edit\n") != NULL
             || strstr(buf, "1 test edit ·") != NULL);
    JC_CHECK(strstr(buf, "a shell command ran") != NULL);
    o = jc_reach_json(&r);
    JC_CHECK(strcmp(cJSON_GetObjectItem(o, "verify")->valuestring, "red") == 0);
    JC_CHECK(strcmp(cJSON_GetObjectItem(o, "scope")->valuestring, "violated") == 0);
    JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "shell_ran")));
    cJSON_Delete(o);
}

static void test_envelope_without_verifier(void)
{
    struct jc_reach r;
    char buf[700];
    memset(&r, 0, sizeof r);
    r.envelope = 1; r.tool_calls = 1;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "no verifier armed -- the answer's claim of success was not tested") != NULL);
    JC_CHECK(strstr(buf, "no edit scope") != NULL);
}

/* M638: the two changes the footer-in-anger reading asked for. A refusal is
 * the fence WORKING and must not swell the number meant to worry the reader;
 * and when the shell ran, the plan clause says on its own line why it is
 * short, instead of leaving the reader to pair it with the not-checked half. */
static void test_refused_and_plan_shell(void)
{
    struct jc_reach r;
    char buf[900];
    cJSON *o;
    memset(&r, 0, sizeof r);
    r.envelope = 1; r.verifier_armed = 1; r.scope_armed = 1; r.shell_ran = 1;
    r.tool_calls = 80; r.tool_errors = 23; r.tool_refused = 4;
    r.plan_present = 1; r.plan_named = 5; r.plan_touched = 3;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "80 tool calls, 23 errors (4 refused by a fence)") != NULL);
    JC_CHECK(strstr(buf, "plan: 3 of 5 predicted files touched (2 unaccounted "
                         "for -- a shell command ran; its writes are not "
                         "attributed)") != NULL);
    o = jc_reach_json(&r);
    JC_CHECK(o != NULL);
    JC_CHECK(cJSON_GetObjectItem(o, "tool_refused") != NULL &&
             cJSON_GetObjectItem(o, "tool_refused")->valueint == 4);
    cJSON_Delete(o);
    /* the shell did not run: the plan clause carries no excuse */
    r.shell_ran = 0;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "plan: 3 of 5 predicted files touched ·") != NULL);
    JC_CHECK(strstr(buf, "unaccounted") == NULL);
    /* the shell ran but every predicted file was touched: nothing to excuse */
    r.shell_ran = 1; r.plan_touched = 5;
    jc_reach_line(&r, buf, sizeof buf);
    JC_CHECK(strstr(buf, "unaccounted") == NULL);
}

void test_reach(void)
{
    test_refused_and_plan_shell();
    test_no_envelope();
    test_verifier_green_and_scope();
    test_red_violation_shell();
    test_envelope_without_verifier();
}
