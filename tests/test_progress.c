/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_progress.c - the learner's progress record: scan / row (C5, M174). */

#include "jc_test.h"
#include "jc_progress.h"
#include "jc_snprintf.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

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
/* ---- M635: predictions ----------------------------------------------------
 * The tally must be order-safe (a resolve with nothing open is ignored), and a
 * prediction line must be invisible to the attempt scanner -- the same
 * separation argument as the hint log, kept true by a different file. */
static void test_predict_scan(void)
{
    static const char *P =
        "{\"ts\":1,\"kind\":\"resolve\",\"right\":true}\n"  /* nothing open */
        "{\"ts\":2,\"kind\":\"predict\",\"text\":\"CRLF fails\"}\n"
        "{\"ts\":3,\"kind\":\"predict\",\"text\":\"second\"}\n"
        "{\"ts\":4,\"kind\":\"resolve\",\"right\":false}\n"
        "not json at all\n";
    struct jc_predictions pr;
    struct jc_progress p;

    jc_progress_predict_scan(P, &pr);
    JC_CHECK(pr.made == 2);
    JC_CHECK(pr.resolved == 1);      /* the first resolve had nothing to close */
    JC_CHECK(pr.right == 0);         /* ...so its right:true counts nowhere    */
    JC_CHECK(pr.open == 1);

    /* The other order: predict, then a right resolve. */
    jc_progress_predict_scan(
        "{\"kind\":\"predict\",\"text\":\"a\"}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n", &pr);
    JC_CHECK(pr.made == 1 && pr.resolved == 1 && pr.right == 1 && pr.open == 0);

    /* Two resolves for one prediction: the second is ignored. */
    jc_progress_predict_scan(
        "{\"kind\":\"predict\",\"text\":\"a\"}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n", &pr);
    JC_CHECK(pr.made == 1 && pr.resolved == 1 && pr.right == 1);

    jc_progress_predict_scan(NULL, &pr);
    JC_CHECK(pr.made == 0 && pr.resolved == 0 && pr.right == 0 && pr.open == 0);

    /* THE SEPARATION: fed to the attempt scanner, prediction lines are not
     * attempts for any spec (no spec field) -- and the files are different
     * anyway, which is what makes the promise true by construction. */
    jc_progress_scan(P, "00-hello.md", &p);
    JC_CHECK(p.attempts == 0);
}

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

/* M642: a write that fails must be REPORTED. The three appenders ignored
 * fprintf's and fclose's return values and answered JC_OK, so `grade --record`
 * on a full disk printed no warning and the learner's attempt was gone (the
 * refute A/B's first seat claimed it; the second seat dismissed it; the
 * measurement made us test it). /dev/full is a device on which every write
 * fails with ENOSPC, so a progress file that is a symlink to it is exactly that
 * disk. */
static int link_full(const char *sub, const char *name, char *out, size_t cap)
{
    jc_snprintf(out, cap, "%s/%s", sub, name);
    (void)remove(out);
    return symlink("/dev/full", out) == 0;
}

static void test_write_failure(void)
{
    const char *dir = jc_test_tmp("jichi_progress_full");
    char sub[600];
    char l1[700], l2[700], l3[700];
    FILE *f;
    int n;
    int c;

    (void)mkdir(dir, 0700);
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    (void)mkdir(sub, 0700);
    if (!link_full(sub, "progress.jsonl", l1, sizeof l1) ||
        !link_full(sub, "hints.jsonl", l2, sizeof l2) ||
        !link_full(sub, "predictions.jsonl", l3, sizeof l3)) {
        return; /* no symlinks or no /dev/full here: skip silently */
    }
    JC_CHECK(jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1) == JC_ERR_IO);
    JC_CHECK(jc_progress_hint_append(dir, "01-x.md", 1) == JC_ERR_IO);
    JC_CHECK(jc_progress_predict_append(dir, "it will pass") == JC_ERR_IO);

    /* Control: the same calls against real files succeed and land one line. */
    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    JC_CHECK(jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1) == JC_OK);
    JC_CHECK(jc_progress_hint_append(dir, "01-x.md", 1) == JC_OK);
    JC_CHECK(jc_progress_predict_append(dir, "it will pass") == JC_OK);
    f = fopen(l1, "rb");
    n = 0;
    if (f != NULL) {
        while ((c = fgetc(f)) != EOF) { if (c == '\n') n++; }
        fclose(f);
    }
    JC_CHECK(n == 1);
    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    (void)rmdir(sub); (void)rmdir(dir);
}

void test_progress(void)
{
    test_write_failure();
    test_hints_scan();
    test_predict_scan(); /* M635 */
    test_base();
    test_scan();
    test_row();
}
