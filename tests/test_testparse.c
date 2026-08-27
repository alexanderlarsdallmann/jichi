/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_testparse.c - unit tests for the pure test-output parser. */

#include "jc_test.h"
#include "jc_testparse.h"

#include <stdlib.h>
#include <string.h>

/* Find the i-th failure, or NULL. */
static struct jc_test_failure *fail_at(struct jc_test_report *r, int i)
{
    if (i < 0 || (jc_size)i >= r->failures.len) return NULL;
    return (struct jc_test_failure *)jc_vec_at(&r->failures, (jc_size)i);
}

static void test_junit(void)
{
    const char *xml =
        "<testsuite name=\"s\" tests=\"3\" failures=\"1\">\n"
        "  <testcase name=\"test_ok\" classname=\"Suite\"/>\n"
        "  <testcase name=\"test_bad\" classname=\"Suite\">\n"
        "    <failure message=\"expected 1 &lt; 0\">at foo.c:42</failure>\n"
        "  </testcase>\n"
        "  <testcase name=\"test_ok2\"></testcase>\n"
        "</testsuite>\n";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(xml, &r);
    JC_CHECK_STR(r.format, "junit");
    JC_CHECK(r.total == 3);
    JC_CHECK(r.failed == 1);
    JC_CHECK(r.passed == 2);
    JC_CHECK(r.failures.len == 1);
    f = fail_at(&r, 0);
    if (f != NULL) {
        JC_CHECK_STR(f->name, "Suite.test_bad");
        /* entity decoded */
        JC_CHECK(f->message != NULL && strstr(f->message, "1 < 0") != NULL);
        /* location harvested from the message text */
        JC_CHECK_STR(f->file, "foo.c");
        JC_CHECK(f->line == 42);
    }
    jc_test_report_free(&r);
}

static void test_junit_inner_text(void)
{
    /* No message attr: fall back to inner text. */
    const char *xml =
        "<testcase name=\"t\"><error>boom in bar.py:7</error></testcase>";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(xml, &r);
    JC_CHECK(r.failed == 1);
    f = fail_at(&r, 0);
    if (f != NULL) {
        JC_CHECK(f->message != NULL && strstr(f->message, "boom") != NULL);
        JC_CHECK_STR(f->file, "bar.py");
        JC_CHECK(f->line == 7);
    }
    jc_test_report_free(&r);
}

static void test_tap(void)
{
    const char *tap =
        "TAP version 13\n"
        "1..3\n"
        "ok 1 - connects\n"
        "not ok 2 - login fails\n"
        "ok 3 - logout\n";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(tap, &r);
    JC_CHECK_STR(r.format, "tap");
    JC_CHECK(r.total == 3);
    JC_CHECK(r.passed == 2);
    JC_CHECK(r.failed == 1);
    JC_CHECK(r.failures.len == 1);
    f = fail_at(&r, 0);
    if (f != NULL) JC_CHECK_STR(f->name, "login fails");
    jc_test_report_free(&r);
}

static void test_generic_compiler(void)
{
    const char *out =
        "gcc -c foo.c\n"
        "foo.c:42:7: error: 'x' undeclared (first use in this function)\n"
        "make: *** [foo.o] Error 1\n";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(out, &r);
    JC_CHECK_STR(r.format, "generic");
    JC_CHECK(r.failures.len >= 1);
    f = fail_at(&r, 0);
    if (f != NULL) {
        JC_CHECK_STR(f->file, "foo.c");
        JC_CHECK(f->line == 42);
    }
    jc_test_report_free(&r);
}

static void test_generic_pytest(void)
{
    const char *out =
        "=================== FAILURES ===================\n"
        "FAILED tests/test_x.py::test_y - AssertionError: 1 != 2\n"
        "============ 1 failed, 3 passed in 0.12s ============\n";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(out, &r);
    JC_CHECK(r.failed == 1);
    JC_CHECK(r.passed == 3);
    JC_CHECK(r.total == 4);
    f = fail_at(&r, 0);
    if (f != NULL) {
        JC_CHECK(f->name != NULL &&
                 strstr(f->name, "test_x.py::test_y") != NULL);
        JC_CHECK_STR(f->file, "tests/test_x.py");
    }
    jc_test_report_free(&r);
}

static void test_generic_go(void)
{
    const char *out =
        "=== RUN   TestZ\n"
        "--- FAIL: TestZ (0.00s)\n"
        "    z_test.go:10: want 3 got 4\n"
        "FAIL\n";
    struct jc_test_report r;
    struct jc_test_failure *f;

    jc_test_report_init(&r);
    jc_testparse(out, &r);
    JC_CHECK(r.failures.len >= 1);
    f = fail_at(&r, 0);
    if (f != NULL) {
        JC_CHECK_STR(f->name, "TestZ");
        JC_CHECK_STR(f->file, "z_test.go");
        JC_CHECK(f->line == 10);
    }
    jc_test_report_free(&r);
}

static void test_empty_and_noise(void)
{
    struct jc_test_report r;

    jc_test_report_init(&r);
    jc_testparse("", &r);
    JC_CHECK(r.failed == -1);
    JC_CHECK(r.failures.len == 0);
    jc_test_report_free(&r);

    jc_test_report_init(&r);
    jc_testparse(NULL, &r);
    JC_CHECK(r.failed == -1);
    jc_test_report_free(&r);

    jc_test_report_init(&r);
    jc_testparse("building project\nlinking\nall done\n", &r);
    JC_CHECK(r.failures.len == 0);
    JC_CHECK(r.failed == -1);
    jc_test_report_free(&r);
}

static void test_cap_and_render(void)
{
    struct jc_sb sb;
    struct jc_test_report r;
    int i;
    struct jc_sb in;

    /* Build many failing TAP lines (> JC_TEST_MAX_FAILURES). */
    jc_sb_init(&in);
    jc_sb_append(&in, "1..60\n");
    for (i = 0; i < 60; i++) {
        jc_sb_append_fmt(&in, "not ok %d - case %d\n", i + 1, i + 1);
    }
    jc_test_report_init(&r);
    jc_testparse(in.data, &r);
    JC_CHECK(r.failed == 60);
    JC_CHECK(r.failures.len == JC_TEST_MAX_FAILURES);
    JC_CHECK(r.truncated == 60 - JC_TEST_MAX_FAILURES);

    jc_sb_init(&sb);
    JC_CHECK(jc_testparse_render(&r, &sb) == JC_TEST_MAX_FAILURES);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "60 failed") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "and 10 more") != NULL);
    jc_sb_free(&sb);

    jc_test_report_free(&r);
    jc_sb_free(&in);
}

/* M86: the "N tests" total scan (Zig's summary line) + jc_test_report_count. */
static void test_count_and_zig(void)
{
    struct jc_test_report r;

    /* Zig's success line: total becomes determinable even with no failures. */
    jc_test_report_init(&r);
    jc_testparse("Build Summary\nAll 91 tests passed.\n", &r);
    JC_CHECK(r.total == 91);
    JC_CHECK(jc_test_report_count(&r) == 91);
    jc_test_report_free(&r);

    /* Explicit zero tests -> a kept 0 (drives the hollow-gate warning). */
    jc_test_report_init(&r);
    jc_testparse("All 0 tests passed.\n", &r);
    JC_CHECK(r.total == 0);
    JC_CHECK(jc_test_report_count(&r) == 0);
    jc_test_report_free(&r);

    /* No test signal at all -> count is -1 (unknown), not 0. */
    jc_test_report_init(&r);
    jc_testparse("building\nlinking\ndone\n", &r);
    JC_CHECK(jc_test_report_count(&r) == -1);
    jc_test_report_free(&r);

    /* passed/failed present but no total -> count sums them. */
    jc_test_report_init(&r);
    jc_testparse("3 passed, 1 failed\n", &r);
    JC_CHECK(jc_test_report_count(&r) == 4);
    jc_test_report_free(&r);

    /* An authoritative TAP plan still wins over the " tests" scan. */
    jc_test_report_init(&r);
    jc_testparse("1..2\nok 1\nok 2\n# ran 2 tests\n", &r);
    JC_CHECK(r.total == 2);
    jc_test_report_free(&r);

    JC_CHECK(jc_test_report_count(NULL) == -1);
}

void test_testparse(void)
{
    test_junit();
    test_junit_inner_text();
    test_tap();
    test_generic_compiler();
    test_generic_pytest();
    test_generic_go();
    test_empty_and_noise();
    test_cap_and_render();
    test_count_and_zig();
}
