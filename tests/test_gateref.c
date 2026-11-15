/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gateref.c - the gate redfirst stage's PURE selection + verdicts +
 * line-assembly (M1157, H9b, task 9, wave 3): the unit table for the
 * exported seam in jc_hp_gateref.c. The engine is pure (no git, no I/O):
 * it classifies the file list by redfirst.sh:28-33's greps and assembles
 * the EXACT bytes the stage's record prints -- the CHECKOUT FAILED line,
 * the BUILD FAILED line, the driver line (the RED / GREEN / SKIP words,
 * the rc / ok / not ok counts, the subject), the unit line (the NO SUMMARY
 * sentence, the changed: join), and the summary (the n / nr / ng / ns). */

#include <stdio.h>
#include <string.h>

#include "jc_snprintf.h"
#include "jc_test.h"
#include "../src/harness/jc_hp_gateref.h"

/* The -Wmissing-prototypes self-declaration (the T2 / T3 / T5 / T8 test
 * idiom: the function is declared in tests/test_main.c, a different TU). */
void test_gateref(void);

#define TC_BUF (16 * 1024)

static void
tc_eq(const char *buf, const char *want)
{
    JC_CHECK(strcmp(buf, want) == 0);
}

void test_gateref(void)
{
    char buf[TC_BUF];
    char drv[2048];
    char fu[2048];
    char au[2048];
    char w[2048];
    char sm[1024];
    int ok;
    int no;
    int is_ref;

    /* --- the SELECTION (redfirst.sh:28-33) --- */

    /* R1's commit: a single driver under tests/smoke/ -- red-first, the
     * driver's basename without the .sh. */
    JC_CHECK(jc_gateref_select("tests/smoke/gd.sh\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 1);
    tc_eq(drv, "gd");
    tc_eq(fu, "");
    tc_eq(au, "");

    /* R4's commit: src/ (a non tests/|docs/internal/ file) -- NOT
     * red-first, even when a driver is present too. */
    JC_CHECK(jc_gateref_select("src/x.c\ntests/smoke/gd.sh\n", drv,
                               sizeof drv, fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "gd");

    /* R5's commit: the lib-glob exclusion (tests/smoke/lib/common.sh) --
     * NOT red-first (no qualifying driver, no unit). */
    JC_CHECK(jc_gateref_select("tests/smoke/lib/common.sh\n", drv,
                               sizeof drv, fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* R5b: /common.sh under tests/smoke/ (the (lib|common) alternative) --
     * excluded too. */
    JC_CHECK(jc_gateref_select("tests/smoke/common.sh\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au, &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* R5c: run.sh (the /run\.sh exclusion) -- excluded. */
    JC_CHECK(jc_gateref_select("tests/smoke/run.sh\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au, &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* R5d: a _-prefixed driver (the /_[^/]*\.sh exclusion) -- excluded. */
    JC_CHECK(jc_gateref_select("tests/smoke/_smoke.sh\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au, &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* R7's commit: docs/internal/ only -- NOT red-first (no driver, no
     * unit). */
    JC_CHECK(jc_gateref_select("docs/internal/note.md\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au, &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* R2's commit: a unit test -- red-first, the first_unit + the changed:
     * list. */
    JC_CHECK(jc_gateref_select("tests/test_gu.c\n", drv, sizeof drv,
                               fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 1);
    tc_eq(drv, "");
    tc_eq(fu, "tests/test_gu.c");
    tc_eq(au, "tests/test_gu.c");

    /* Two units: the FIRST is first_unit, the changed: join carries both,
     * in file order. */
    JC_CHECK(jc_gateref_select("tests/test_b.c\ntests/test_a.c\n", drv,
                               sizeof drv, fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 1);
    tc_eq(fu, "tests/test_b.c");
    tc_eq(au, "tests/test_b.c tests/test_a.c");

    /* Driver + unit in one commit: both lists fill. */
    JC_CHECK(jc_gateref_select("tests/smoke/gd.sh\ntests/test_gu.c\n", drv,
                               sizeof drv, fu, sizeof fu, au, sizeof au,
                               &is_ref) == 0);
    JC_CHECK(is_ref == 1);
    tc_eq(drv, "gd");
    tc_eq(fu, "tests/test_gu.c");
    tc_eq(au, "tests/test_gu.c");

    /* An empty file list (a commit with no changed files) -- not
     * red-first. */
    JC_CHECK(jc_gateref_select("", drv, sizeof drv, fu, sizeof fu, au,
                               sizeof au, &is_ref) == 0);
    JC_CHECK(is_ref == 0);
    tc_eq(drv, "");

    /* --- the DRIVER verdict (redfirst.sh:43-46) --- */

    /* GREEN: rc 0, one ok, no not ok. */
    JC_CHECK(jc_gateref_driver_verdict("ok 1 - one\n", 0, w, sizeof w,
                                       &ok, &no) == 0);
    tc_eq(w, "GREEN");
    JC_CHECK(ok == 1);
    JC_CHECK(no == 0);

    /* RED by a not ok line (rc 0). */
    JC_CHECK(jc_gateref_driver_verdict("ok 1 - one\nnot ok 2 - two\n", 0, w,
                                       sizeof w, &ok, &no) == 0);
    tc_eq(w, "RED");
    JC_CHECK(ok == 1);
    JC_CHECK(no == 1);

    /* RED by rc (no not ok line, no output -- R4's shape). */
    JC_CHECK(jc_gateref_driver_verdict("", 1, w, sizeof w, &ok, &no) == 0);
    tc_eq(w, "RED");
    JC_CHECK(ok == 0);
    JC_CHECK(no == 0);

    /* SKIP: the `1..0 # skip` line, rc 0, no ok / no not ok (R3's shape). */
    JC_CHECK(jc_gateref_driver_verdict("1..0 # skip\n", 0, w, sizeof w,
                                       &ok, &no) == 0);
    tc_eq(w, "SKIP");
    JC_CHECK(ok == 0);
    JC_CHECK(no == 0);

    /* SKIP: the `# SKIP` form. */
    JC_CHECK(jc_gateref_driver_verdict("# SKIP nothing to do\n", 0, w,
                                       sizeof w, &ok, &no) == 0);
    tc_eq(w, "SKIP");

    /* A not ok line beats the skip line (RED, not SKIP). */
    JC_CHECK(jc_gateref_driver_verdict("1..0 # skip\nnot ok 1 - x\n", 0, w,
                                       sizeof w, &ok, &no) == 0);
    tc_eq(w, "RED");
    JC_CHECK(no == 1);

    /* --- the UNIT verdict (redfirst.sh:51-55) --- */

    /* GREEN: a summary with zero failures. */
    JC_CHECK(jc_gateref_unit_verdict("1 checks, 0 failures\n", 0, w,
                                     sizeof w, sm, sizeof sm) == 0);
    tc_eq(w, "GREEN");
    tc_eq(sm, "1 checks, 0 failures");

    /* RED: a summary with a failure. */
    JC_CHECK(jc_gateref_unit_verdict("37883 checks, 1 failures\n", 0, w,
                                     sizeof w, sm, sizeof sm) == 0);
    tc_eq(w, "RED");
    tc_eq(sm, "37883 checks, 1 failures");

    /* The LAST summary line wins (a mid-output `ok` block before it). */
    JC_CHECK(jc_gateref_unit_verdict("ok 1 - a\nok 2 - b\n1 checks, 2 "
                                     "failures\n", 0, w, sizeof w, sm,
                                     sizeof sm) == 0);
    tc_eq(w, "RED");
    tc_eq(sm, "1 checks, 2 failures");

    /* NO SUMMARY: no summary line (a crash or a build error) -- the
     * sentence carries the rc. */
    JC_CHECK(jc_gateref_unit_verdict("a build error\n", 1, w, sizeof w, sm,
                                     sizeof sm) == 0);
    tc_eq(w, "NO SUMMARY (rc=1: the suite did not finish -- a crash or "
             "a build error)");
    tc_eq(sm, "");

    /* --- the LINE ASSEMBLERS (the reference's printf bytes) --- */

    JC_CHECK(jc_gateref_checkoutfail_line("abcd1234", buf, sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 CHECKOUT FAILED\n");

    JC_CHECK(jc_gateref_buildfail_line("abcd1234", "build the red-first "
            "commit", "/o", buf, sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 BUILD FAILED at the red-first commit (build the "
               "red-first commit): see /o/abcd1234-build.log\n");

    JC_CHECK(jc_gateref_driver_line("abcd1234", "GREEN", "gd", 0, 1, 0,
                                    "add the driver", buf, sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 GREEN driver gd (rc=0, 1 ok, 0 not ok) -- add the "
               "driver\n");

    JC_CHECK(jc_gateref_driver_line("abcd1234", "RED", "gd", 1, 0, 0,
                                    "break the driver", buf, sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 RED driver gd (rc=1, 0 ok, 0 not ok) -- break the "
               "driver\n");

    JC_CHECK(jc_gateref_driver_line("abcd1234", "SKIP", "gk", 0, 0, 0,
                                    "skip the driver", buf, sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 SKIP driver gk (rc=0, 0 ok, 0 not ok) -- skip the "
               "driver\n");

    JC_CHECK(jc_gateref_unit_line("abcd1234", "RED", "2 checks, 1 failures",
                                  "tests/test_gu.c", "add the unit", buf,
                                  sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 RED unit suite (2 checks, 1 failures; changed: "
               "tests/test_gu.c ) -- add the unit\n");

    JC_CHECK(jc_gateref_unit_line("abcd1234", "GREEN", "1 checks, 0 failures",
                                  "tests/test_g.c tests/test_h.c",
                                  "two units", buf,
                                  sizeof buf) >= 0);
    tc_eq(buf, "abcd1234 GREEN unit suite (1 checks, 0 failures; changed: "
               "tests/test_g.c tests/test_h.c ) -- two units\n");

    JC_CHECK(jc_gateref_summary_line(2, 1, 1, 0, buf, sizeof buf) >= 0);
    tc_eq(buf, "redfirst: 2 red-first commit(s), 1 red, 1 green, 0 skip\n");

    JC_CHECK(jc_gateref_summary_line(0, 0, 0, 0, buf, sizeof buf) >= 0);
    tc_eq(buf, "redfirst: 0 red-first commit(s), 0 red, 0 green, 0 skip\n");

    JC_CHECK(jc_gateref_records_line("/tmp/r/records", buf, sizeof buf) >= 0);
    tc_eq(buf, "records: /tmp/r/records\n");
}
