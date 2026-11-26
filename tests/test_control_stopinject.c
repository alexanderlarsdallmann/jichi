/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_control_stopinject.c - M1149: the per-worker stop-task / inject-task
 * codec (RED FIRST).
 *
 * This file is committed red: on the six-verb codec the checks below fail with
 * assertion lines (the verbs parse to JC_CTL_UNKNOWN, the verb list names six,
 * the invalid/over-cap request shapes are answered JC_OK-as-unknown). Wave 2
 * (the verbs in the codec) turns every check green and adds the
 * build->parse roundtrip cases (jc_control_build_task_request, the cmd.task /
 * cmd.reason fields) alongside them. The parse cases use raw wire strings so
 * they compile against the pre-M1149 header. */

#include "jc_test.h"
#include "jc_control.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* Entry point (not in jc_test.h, like test_harness_setflag). */
void test_control_stopinject(void);

static void test_type_parse_name(void)
{
    /* M1149: both new verbs parse and name (the roundtrip is the check). */
    JC_CHECK(jc_control_type_parse("stop-task") != JC_CTL_UNKNOWN);
    JC_CHECK(jc_control_type_parse("inject-task") != JC_CTL_UNKNOWN);
    {
        const char *nm = jc_control_type_name(jc_control_type_parse("stop-task"));
        JC_CHECK(nm != NULL && strcmp(nm, "stop-task") == 0);
    }
    {
        const char *nm =
            jc_control_type_name(jc_control_type_parse("inject-task"));
        JC_CHECK(nm != NULL && strcmp(nm, "inject-task") == 0);
    }
}

/* M752 extended: the verb list is generated from the enum, so it names all
 * EIGHT, in order -- the two per-worker verbs appended after mode. */
static void test_verb_list_eight(void)
{
    char buf[192];
    jc_control_verb_list(buf, sizeof(buf), "|");
    JC_CHECK(strcmp(buf, "status|inject|pause|resume|abort|mode"
                          "|stop-task|inject-task|cancel-cmd") == 0);
}

static void test_stop_task_parse(void)
{
    struct jc_arena *a;
    struct jc_control_cmd cmd;
    jc_status st;

    a = jc_arena_new(4096);
    JC_CHECK(a != NULL);
    if (a == NULL) {
        return;
    }
    /* stop-task {task, reason} parses OK and names the verb. */
    st = jc_control_parse_request(
        "{\"v\":1,\"type\":\"stop-task\",\"task\":0,\"reason\":\"fixture\"}",
        &cmd, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cmd.type != JC_CTL_UNKNOWN);
    /* stop-task without a reason is valid (the reason is optional). */
    st = jc_control_parse_request(
        "{\"v\":1,\"type\":\"stop-task\",\"task\":3}", &cmd, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cmd.type != JC_CTL_UNKNOWN);
    /* A negative index is INVALID at the protocol edge. */
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"stop-task\",\"task\":-1}", &cmd, a)
             == JC_ERR_INVALID);
    /* A non-numeric index is INVALID. */
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"stop-task\",\"task\":\"zero\"}", &cmd, a)
             == JC_ERR_INVALID);
    /* A missing index is INVALID. */
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"stop-task\",\"reason\":\"fixture\"}",
                 &cmd, a)
             == JC_ERR_INVALID);
    jc_arena_free(a);
}

static void test_inject_task_parse(void)
{
    struct jc_arena *a;
    struct jc_control_cmd cmd;
    jc_status st;

    a = jc_arena_new(4096);
    JC_CHECK(a != NULL);
    if (a == NULL) {
        return;
    }
    /* inject-task {task, text} parses OK and names the verb. */
    st = jc_control_parse_request(
        "{\"v\":1,\"type\":\"inject-task\",\"task\":1,\"text\":\"M1149-MARKER\"}",
        &cmd, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cmd.type != JC_CTL_UNKNOWN);
    /* A missing or empty text is INVALID (as INJECT's is today). */
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"inject-task\",\"task\":1}", &cmd, a)
             == JC_ERR_INVALID);
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"inject-task\",\"task\":1,\"text\":\"\"}",
                 &cmd, a)
             == JC_ERR_INVALID);
    /* A negative or non-numeric index is INVALID. */
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"inject-task\",\"task\":-2,"
                 "\"text\":\"x\"}",
                 &cmd, a)
             == JC_ERR_INVALID);
    JC_CHECK(jc_control_parse_request(
                 "{\"v\":1,\"type\":\"inject-task\",\"task\":\"one\","
                 "\"text\":\"x\"}",
                 &cmd, a)
             == JC_ERR_INVALID);

    /* The DISTINCT over-cap outcome: a text whose PARSED length alone exceeds
     * the per-task queue cap (the lead's M1045 cap, 4 * JC_CONTROL_LINE_MAX)
     * is JC_ERR_TOOBIG -- the handler maps that to the "queue is full" refusal
     * rather than storing an over-cap line. The expression here is the cap's
     * value in today's terms; the codec's #define (JC_CONTROL_QUEUE_MAX)
     * names it when it lands. A ~70000-char text builds the case (cell 14's
     * request shape); it stays under the codec's request-LINE cap, so the
     * distinction is the parsed text's length, not the line's. */
    {
        char *body = (char *)malloc(JC_CONTROL_LINE_MAX * 4 + 4096);
        char *line = NULL;
        int i;
        if (body != NULL) {
            for (i = 0; i < (int)(JC_CONTROL_LINE_MAX * 4 + 4096 - 200); i++) {
                body[i] = 'x';
            }
            body[i] = '\0';
            line = (char *)malloc(strlen(body) + 128);
            if (line != NULL) {
                jc_snprintf(line, strlen(body) + 128,
                            "{\"v\":1,\"type\":\"inject-task\",\"task\":0,"
                            "\"text\":\"%s\"}",
                            body);
                st = jc_control_parse_request(line, &cmd, a);
                JC_CHECK(st == JC_ERR_TOOBIG);
            }
            free(line);
            free(body);
        }
    }

    /* Under the cap, the same shape is valid: the cap is the boundary. */
    {
        char body[256];
        char line[512];
        int i;
        for (i = 0; i < 200; i++) {
            body[i] = 'x';
        }
        body[i] = '\0';
        jc_snprintf(line, sizeof(line),
                    "{\"v\":1,\"type\":\"inject-task\",\"task\":0,"
                    "\"text\":\"%s\"}",
                    body);
        st = jc_control_parse_request(line, &cmd, a);
        JC_CHECK(st == JC_OK);
    }
    jc_arena_free(a);
}

/* Added with the code (wave 2): the build->parse roundtrip through
 * jc_control_build_task_request and the cmd.task / cmd.reason / cmd.text
 * fields. Registered from test_control_stopinject() below; a no-op here in the
 * red commit because the codec knows neither the builder nor the fields. */
void test_control_stopinject(void)
{
    test_type_parse_name();
    test_verb_list_eight();
    test_stop_task_parse();
    test_inject_task_parse();
}
