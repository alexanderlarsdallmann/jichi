/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_control.c - the pure control-channel codec (M159). */

#include "jc_test.h"
#include "jc_control.h"

#include <stdlib.h>
#include <string.h>

static void test_type_names(void)
{
    JC_CHECK(strcmp(jc_control_type_name(JC_CTL_STATUS), "status") == 0);
    JC_CHECK(strcmp(jc_control_type_name(JC_CTL_INJECT), "inject") == 0);
    JC_CHECK(strcmp(jc_control_type_name(JC_CTL_ABORT), "abort") == 0);
    JC_CHECK(jc_control_type_parse("pause") == JC_CTL_PAUSE);
    JC_CHECK(jc_control_type_parse("resume") == JC_CTL_RESUME);
    JC_CHECK(jc_control_type_parse("nope") == JC_CTL_UNKNOWN);
    JC_CHECK(jc_control_type_parse(NULL) == JC_CTL_UNKNOWN);
}

/* M752: the verb list is generated from the enum, so it names all six, in
 * order -- `mode` included, which two hand-typed lists had lost. */
static void test_verb_list(void)
{
    char buf[128];
    char tiny[8];
    jc_control_verb_list(buf, sizeof(buf), "|");
    JC_CHECK(strcmp(buf, "status|inject|pause|resume|abort|mode") == 0);
    jc_control_verb_list(buf, sizeof(buf), "/");
    JC_CHECK(strcmp(buf, "status/inject/pause/resume/abort/mode") == 0);
    jc_control_verb_list(tiny, sizeof(tiny), "|");
    JC_CHECK(strlen(tiny) < sizeof(tiny));   /* truncates, stays terminated */
}

static void test_request_roundtrip(void)
{
    struct jc_arena *a;
    struct jc_control_cmd cmd;
    char *line;

    a = jc_arena_new(4096);
    JC_CHECK(a != NULL);
    if (a == NULL) {
        return;
    }

    /* Build -> parse: a bare verb. */
    line = jc_control_build_request("status", NULL, 0, NULL);
    JC_CHECK(line != NULL && strchr(line, '\n') != NULL);
    JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
    JC_CHECK(cmd.type == JC_CTL_STATUS);
    free(line);

    /* Build -> parse: inject carries the text (arena-copied). */
    line = jc_control_build_request("inject", "skip the tests; report now",
                                    0, NULL);
    if (JC_REQUIRE(line != NULL)) { /* a guard (M729) */
        JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
    }
    JC_CHECK(cmd.type == JC_CTL_INJECT);
    JC_CHECK(cmd.text != NULL &&
             strcmp(cmd.text, "skip the tests; report now") == 0);
    free(line);

    /* M162: pause --extend roundtrips; a plain pause does not extend. */
    line = jc_control_build_request("pause", NULL, 1, NULL);
    JC_CHECK(line != NULL && strstr(line, "\"extend\":true") != NULL);
    JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
    JC_CHECK(cmd.type == JC_CTL_PAUSE && cmd.extend == 1);
    free(line);
    line = jc_control_build_request("pause", NULL, 0, NULL);
    JC_CHECK(line != NULL && strstr(line, "extend") == NULL);
    JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
    JC_CHECK(cmd.type == JC_CTL_PAUSE && cmd.extend == 0);
    free(line);

    /* From field on inject: build with from="supervisor", parse and verify. */
    line = jc_control_build_request("inject", "skip the tests; report now",
                                    0, "supervisor");
    if (JC_REQUIRE(line != NULL)) {
        JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
        JC_CHECK(cmd.type == JC_CTL_INJECT);
        JC_CHECK(cmd.text != NULL &&
                 strcmp(cmd.text, "skip the tests; report now") == 0);
        JC_CHECK(cmd.from != NULL && strcmp(cmd.from, "supervisor") == 0);
        free(line);
    }

    /* Backward compat: inject without from field parses to from=NULL. */
    line = jc_control_build_request("inject", "text only", 0, NULL);
    if (JC_REQUIRE(line != NULL)) {
        JC_CHECK(jc_control_parse_request(line, &cmd, a) == JC_OK);
        JC_CHECK(cmd.type == JC_CTL_INJECT);
        JC_CHECK(cmd.from == NULL);
        free(line);
    }

    /* An inject with no text is invalid. */
    JC_CHECK(jc_control_parse_request("{\"v\":1,\"type\":\"inject\"}",
                                      &cmd, a) == JC_ERR_INVALID);
    /* Unknown type parses OK but is UNKNOWN (server replies an error). */
    JC_CHECK(jc_control_parse_request("{\"v\":1,\"type\":\"approve\"}",
                                      &cmd, a) == JC_OK);
    JC_CHECK(cmd.type == JC_CTL_UNKNOWN);
    /* Not JSON at all. */
    JC_CHECK(jc_control_parse_request("garbage", &cmd, a) == JC_ERR_PARSE);
    JC_CHECK(jc_control_parse_request(NULL, &cmd, a) == JC_ERR_PARSE);

    jc_arena_free(a);
}

static void test_responses(void)
{
    char *line;

    line = jc_control_build_ok("applied at next model call");
    JC_CHECK(line != NULL);
    if (line != NULL) {
        JC_CHECK(strstr(line, "\"ok\":true") != NULL);
        JC_CHECK(strstr(line, "applied at next model call") != NULL);
        free(line);
    }
    line = jc_control_build_err("steering queue full");
    JC_CHECK(line != NULL);
    if (line != NULL) {
        JC_CHECK(strstr(line, "\"ok\":false") != NULL);
        JC_CHECK(strstr(line, "steering queue full") != NULL);
        free(line);
    }
}

static void test_status_snapshot(void)
{
    struct jc_control_status s;
    char *line;

    memset(&s, 0, sizeof(s));
    s.run_id = "run-1";
    s.elapsed = 42;
    s.tokens_used = 12345.0;
    s.budget_tokens = 400000.0;
    s.tool_calls = 7;
    s.max_tool_calls = 80;
    s.deadline_secs = 1800;
    s.deadline_credit = 42; /* M162 */
    s.outcome = "running";
    s.last_tool = "read_file";
    s.paused = 1;
    s.injected = 3;        /* M1045 */
    s.queued_injects = 1;  /* M1045 */
    line = jc_control_build_status(&s);
    JC_CHECK(line != NULL);
    if (line != NULL) {
        JC_CHECK(strstr(line, "\"run\":\"run-1\"") != NULL);
        JC_CHECK(strstr(line, "\"tokens_used\":12345") != NULL);
        JC_CHECK(strstr(line, "\"budget_tokens\":400000") != NULL);
        JC_CHECK(strstr(line, "\"last_tool\":\"read_file\"") != NULL);
        JC_CHECK(strstr(line, "\"deadline_credit\":42") != NULL);
        JC_CHECK(strstr(line, "\"paused\":true") != NULL);
        JC_CHECK(strstr(line, "\"injected\":3") != NULL);      /* M1045 */
        JC_CHECK(strstr(line, "\"queued_injects\":1") != NULL); /* M1045 */
        free(line);
    }

    /* Unbounded run: zero caps are omitted from the wire. */
    memset(&s, 0, sizeof(s));
    s.outcome = "running";
    line = jc_control_build_status(&s);
    JC_CHECK(line != NULL);
    if (line != NULL) {
        JC_CHECK(strstr(line, "budget_tokens") == NULL);
        JC_CHECK(strstr(line, "max_tool_calls") == NULL);
        JC_CHECK(strstr(line, "deadline_secs") == NULL);
        JC_CHECK(strstr(line, "deadline_credit") == NULL);
        JC_CHECK(strstr(line, "last_tool") == NULL);
        JC_CHECK(strstr(line, "\"paused\":false") != NULL);
        /* M1045: the delivery pair is ALWAYS emitted, zero included -- the
         * always-emit contract, unlike the caps above that omit when zero. */
        JC_CHECK(strstr(line, "\"injected\":0") != NULL);
        JC_CHECK(strstr(line, "\"queued_injects\":0") != NULL);
        free(line);
    }
    JC_CHECK(jc_control_build_status(NULL) == NULL);
}

void test_control(void)
{
    test_type_names();
    test_verb_list();
    test_request_roundtrip();
    test_responses();
    test_status_snapshot();
}
