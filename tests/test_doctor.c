/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_doctor.c - the pure health-check report core (jc_doctor.c). */

#include "jc_test.h"
#include "jc_doctor.h"
#include "jc_str.h"

#include <string.h>

static void test_counts_and_exit(void)
{
    struct jc_doctor d;
    jc_doctor_init(&d);

    JC_CHECK(jc_doctor_exit_code(&d) == 0); /* empty: no failures */

    jc_doctor_add(&d, JC_DOC_OK, "config loaded", "3 models");
    jc_doctor_add(&d, JC_DOC_WARN, "no API key", "ok for local servers");
    jc_doctor_add(&d, JC_DOC_OK, "git available", NULL);

    JC_CHECK(jc_doctor_count(&d, JC_DOC_OK) == 2);
    JC_CHECK(jc_doctor_count(&d, JC_DOC_WARN) == 1);
    JC_CHECK(jc_doctor_count(&d, JC_DOC_FAIL) == 0);
    JC_CHECK(jc_doctor_exit_code(&d) == 0); /* warnings do not fail */

    jc_doctor_add(&d, JC_DOC_FAIL, "server unreachable", "check apiBase");
    JC_CHECK(jc_doctor_count(&d, JC_DOC_FAIL) == 1);
    JC_CHECK(jc_doctor_exit_code(&d) == 1); /* a failure -> exit 1 */

    jc_doctor_free(&d);
}

static void test_render(void)
{
    struct jc_doctor d;
    struct jc_sb sb;
    jc_doctor_init(&d);
    jc_doctor_add(&d, JC_DOC_OK, "config loaded", "3 models");
    jc_doctor_add(&d, JC_DOC_FAIL, "server unreachable", "check apiBase");

    /* ASCII, no color. */
    jc_sb_init(&sb);
    jc_doctor_render(&d, 0, 0, &sb);
    JC_CHECK(strstr(sb.data, "config loaded") != NULL);
    JC_CHECK(strstr(sb.data, "    3 models") != NULL);   /* indented detail */
    JC_CHECK(strstr(sb.data, "server unreachable") != NULL);
    JC_CHECK(strstr(sb.data, "1 ok, 0 warnings, 1 problem") != NULL);
    JC_CHECK(strstr(sb.data, "\x1b[") == NULL);          /* no color codes */
    jc_sb_free(&sb);

    /* Color emits ANSI escapes. */
    jc_sb_init(&sb);
    jc_doctor_render(&d, 1, 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[") != NULL);
    jc_sb_free(&sb);

    jc_doctor_free(&d);
}

static void test_pluralization(void)
{
    struct jc_doctor d;
    struct jc_sb sb;
    jc_doctor_init(&d);
    jc_doctor_add(&d, JC_DOC_WARN, "a", NULL);
    jc_doctor_add(&d, JC_DOC_WARN, "b", NULL);
    jc_sb_init(&sb);
    jc_doctor_render(&d, 0, 0, &sb);
    JC_CHECK(strstr(sb.data, "0 ok, 2 warnings, 0 problems") != NULL);
    jc_sb_free(&sb);
    jc_doctor_free(&d);
}

static void test_render_json(void)
{
    struct jc_doctor d;
    struct jc_sb sb;
    jc_doctor_init(&d);
    jc_doctor_add(&d, JC_DOC_OK, "ok check", "fine");
    jc_doctor_add(&d, JC_DOC_WARN, "warn check", NULL);
    jc_doctor_add(&d, JC_DOC_FAIL, "bad \"quote\" \\ and\nnewline", "x");
    jc_sb_init(&sb);
    jc_doctor_render_json(&d, &sb);
    /* Summary counts + exit. */
    JC_CHECK(strstr(sb.data, "\"ok\":1") != NULL);
    JC_CHECK(strstr(sb.data, "\"warn\":1") != NULL);
    JC_CHECK(strstr(sb.data, "\"fail\":1") != NULL);
    JC_CHECK(strstr(sb.data, "\"exit\":1") != NULL);
    /* Status names + escaping. */
    JC_CHECK(strstr(sb.data, "\"status\":\"ok\"") != NULL);
    JC_CHECK(strstr(sb.data, "\"status\":\"fail\"") != NULL);
    JC_CHECK(strstr(sb.data, "\\\"quote\\\"") != NULL);
    JC_CHECK(strstr(sb.data, "\\n") != NULL);
    jc_sb_free(&sb);
    jc_doctor_free(&d);
}

void test_doctor(void)
{
    test_counts_and_exit();
    test_render();
    test_pluralization();
    test_render_json();
}
