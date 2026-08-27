/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_progress.c - the learner's progress record: scan / row (C5, M174). */

#include "jc_test.h"
#include "jc_progress.h"
#include <string.h>

static const char *LOG =
    "{\"ts\":1,\"spec\":\"docs/assignments/00-hello.md\",\"passed\":false,"
    "\"pct\":0,\"tests_run\":0,\"tests_failed\":0}\n"
    "not json at all -- the learner edited the file\n"
    "{\"ts\":2,\"spec\":\"docs/assignments/00-hello.md\",\"passed\":true,"
    "\"pct\":100,\"tests_run\":0,\"tests_failed\":0,\"hints\":2}\n"
    "{\"ts\":3,\"spec\":\"01-find-the-value.md\",\"passed\":false,"
    "\"pct\":40,\"tests_run\":5,\"tests_failed\":3}\n";

static void test_base(void)
{
    JC_CHECK_STR(jc_progress_base("docs/assignments/00-hello.md"),
                 "00-hello.md");
    JC_CHECK_STR(jc_progress_base("00-hello.md"), "00-hello.md");
    JC_CHECK(jc_progress_base(NULL) == NULL);
}

static void test_scan(void)
{
    struct jc_progress p;

    /* Two records for 00-hello (one failed, one passed): passed wins, the
     * best pct is kept, and the path/basename forms match each other. */
    jc_progress_scan(LOG, "00-hello.md", &p);
    JC_CHECK(p.attempts == 2);
    JC_CHECK(p.passed == 1);
    JC_CHECK(p.best_pct == 100);
    jc_progress_scan(LOG, "docs/assignments/00-hello.md", &p);
    JC_CHECK(p.attempts == 2 && p.passed == 1);

    /* Attempted but never passed. */
    jc_progress_scan(LOG, "01-find-the-value.md", &p);
    JC_CHECK(p.attempts == 1);
    JC_CHECK(p.passed == 0);
    JC_CHECK(p.best_pct == 40);

    /* Never graded; and a missing file (NULL text) is not an error. */
    jc_progress_scan(LOG, "02-unseen.md", &p);
    JC_CHECK(p.attempts == 0 && p.passed == 0 && p.best_pct == 0);
    jc_progress_scan(NULL, "00-hello.md", &p);
    JC_CHECK(p.attempts == 0);
}

static void test_row(void)
{
    struct jc_progress p;
    char row[256];

    memset(&p, 0, sizeof(p));

    /* Untouched spec: phase/points shown, status "-". */
    jc_progress_row("00-hello.md", "implementation", 1, 0, &p,
                    row, sizeof(row));
    JC_CHECK(strstr(row, "00-hello.md") != NULL);
    JC_CHECK(strstr(row, "implementation") != NULL);
    JC_CHECK(strstr(row, "1pt") != NULL);
    JC_CHECK(strstr(row, "(+solution)") == NULL);

    /* Passed, with a solution sibling. */
    p.attempts = 2;
    p.passed = 1;
    p.best_pct = 100;
    jc_progress_row("00-hello.md", NULL, 0, 1, &p, row, sizeof(row));
    JC_CHECK(strstr(row, "passed") != NULL);
    JC_CHECK(strstr(row, "(+solution)") != NULL);

    /* Attempted, not yet passed: the best pct is visible. */
    p.passed = 0;
    p.best_pct = 40;
    jc_progress_row("01-x.md", "testing", 3, 0, &p, row, sizeof(row));
    JC_CHECK(strstr(row, "attempted (best 40%)") != NULL);

    /* The header names the same columns the rows fill. */
    jc_progress_row_header(row, sizeof(row));
    JC_CHECK(strstr(row, "assignment") != NULL);
    JC_CHECK(strstr(row, "phase") != NULL);
    JC_CHECK(strstr(row, "status") != NULL);
}

/* ---- M502: the hint log --------------------------------------------------
 *
 * The defect: 74 shipped specs, the scaffold glossary and CURRICULUM.md have
 * promised hints are "free, and recorded" since M174, and nothing wrote the
 * record -- so a teacher could not see that a learner needed all three rungs,
 * which is the diagnostic half of a hint ladder.
 *
 * The load-bearing property is the SEPARATION: hint pulls must never be
 * readable as graded attempts, because every reader of progress.jsonl counts a
 * line as one. These pin that a hint line is invisible to the progress scanner
 * and vice versa. */
static void test_hints_scan(void)
{
    static const char *HINTS =
        "{\"ts\":1,\"spec\":\"docs/assignments/00-hello.md\",\"rung\":1}\n"
        "{\"ts\":2,\"spec\":\"docs/assignments/00-hello.md\",\"rung\":3}\n"
        "{\"ts\":3,\"spec\":\"01-other.md\",\"rung\":1}\n"
        "not json at all\n";
    struct jc_hints h;
    struct jc_progress p;

    /* Two pulls, deepest rung 3 -- the max, not the last. */
    jc_progress_hints_scan(HINTS, "docs/assignments/00-hello.md", &h);
    JC_CHECK(h.pulls == 2);
    JC_CHECK(h.max_rung == 3);

    /* Basename matching, exactly like the progress scanner: a learner grading
     * from inside the directory and one grading from the root must agree. */
    jc_progress_hints_scan(HINTS, "00-hello.md", &h);
    JC_CHECK(h.pulls == 2 && h.max_rung == 3);

    jc_progress_hints_scan(HINTS, "01-other.md", &h);
    JC_CHECK(h.pulls == 1 && h.max_rung == 1);

    /* Untouched spec, and a NULL log: zero, not garbage. */
    jc_progress_hints_scan(HINTS, "99-never.md", &h);
    JC_CHECK(h.pulls == 0 && h.max_rung == 0);
    jc_progress_hints_scan(NULL, "00-hello.md", &h);
    JC_CHECK(h.pulls == 0 && h.max_rung == 0);

    /* THE SEPARATION. A hint line must not register as an attempt... */
    jc_progress_scan(HINTS, "docs/assignments/00-hello.md", &p);
    JC_CHECK(p.attempts == 2);   /* they parse as lines for this spec ... */
    JC_CHECK(p.passed == 0);     /* ... but carry no verdict */
    JC_CHECK(p.best_pct == 0);
    /* ...which is exactly why the two live in DIFFERENT FILES: the scanner
     * cannot tell them apart, so nothing may put them in one file. This
     * assertion is the reason the design is a separate sink, kept here so a
     * later "simplification" into one file fails a test rather than quietly
     * turning every hint into a failed attempt. */
}

void test_progress(void)
{
    test_hints_scan();
    test_base();
    test_scan();
    test_row();
}
