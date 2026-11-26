/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sup_stop.c - def-377 W4 (M1194, A4b): the soft-stop SENTINEL recognizer.
 *
 * RED-FIRST. The A4 wave's recognizer was the loop-top history scan in
 * jc_agent.c (a strstr for `JICHI-CONTROL:supervise-stop:` over the last few
 * user-role messages). Two of its arms were wrong (review finding R:1):
 *   (b) a task PROMPT (user-role, no [operator] prefix) that merely contains
 *       the control word mid-prose self-armed the child's soft stop;
 *   (c) a HUMAN inject that mentioned the word mid-prose armed it too.
 * `jc_sup_stop_strstr` below reproduces that exact substring rule so the test
 * can show, at the head, that the old logic fires on (b) and (c) -- and that
 * the new exact-line rule `jc_sup_stop_line` does not.
 *
 * What is asserted:
 *   (a) the EXACT sentinel line arms (control word + one seq digit, nothing
 *       else);
 *   (b) a task-prompt-like line carrying the substring mid-prose does NOT arm
 *       (the old strstr WOULD -- this is the red);
 *   (c) a human inject `... JICHI-CONTROL:supervise-stop:1 more work ...`
 *       does NOT arm (the old strstr WOULD -- this is the red).
 *
 * The recognizer is unit-reachable here (extracted into jc_sup_stop_line);
 * the arm's placement (at the operator fold, not a history re-scan) and the
 * stamp's determinism (R:3) are the driver's live-pool cells, not this one.
 */
#include "jc_test.h"
#include "jc_sup_stop.h"

#include <string.h>   /* strstr -- the LEGACY rule under comparison, not the new one */

/* The A4 wave's recognition rule, isolated: a SUBSTRING hit anywhere in the
 * line arms it. This is what the loop-top scan did (modulo the role gate,
 * which the scan itself did not enforce on pool-child task prompts). It is
 * the RED oracle: where it fires and jc_sup_stop_line must not, the fix has
 * teeth. */
static int jc_sup_stop_strstr(const char *line)
{
    if (line == NULL) {
        return 0;
    }
    return strstr(line, JC_SUP_STOP_PREFIX) != NULL;
}

void test_sup_stop(void)
{
    const char *exact1 = "JICHI-CONTROL:supervise-stop:1";
    const char *exact2 = "JICHI-CONTROL:supervise-stop:2";
    const char *midprose =
        "Before you stop, note the sentinel JICHI-CONTROL:supervise-stop:hello "
        "was in the task prompt.";
    const char *humaninject =
        "please stop: JICHI-CONTROL:supervise-stop:1 more work";

    /* --- (a) the exact line arms ----------------------------------------- */
    JC_CHECK(jc_sup_stop_line(exact1) == 1);
    JC_CHECK(jc_sup_stop_line(exact2) == 1);

    /* --- (b) a TASK PROMPT containing the substring mid-prose does not ----
     * The old rule fired here (strstr hit) -- the red that proves the fix. */
    JC_CHECK(jc_sup_stop_strstr(midprose) == 1);   /* the legacy logic WAS armed */
    JC_CHECK(jc_sup_stop_line(midprose) == 0);     /* the exact line is not */

    /* --- (c) a HUMAN INJECT mentioning the word does not ------------------
     * The word is the FIRST token's line but has other words on it; and the
     * bare substring is present. The old rule fired; the exact line does not. */
    JC_CHECK(jc_sup_stop_strstr(humaninject) == 1);  /* the legacy logic WAS armed */
    JC_CHECK(jc_sup_stop_line(humaninject) == 0);    /* the exact line is not */

    /* --- more exact-line edges, so the rule cannot drift back to substring-
     * matching: nothing but the single seq digit is allowed after the prefix. */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:") == 0);   /* bare */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:3") == 0);  /* not a seq */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:10") == 0); /* two digits */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:1 ") == 0); /* trailing space */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop: 1") == 0); /* space after : */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:x") == 0);  /* letter */
    JC_CHECK(jc_sup_stop_line("a JICHI-CONTROL:supervise-stop:1") == 0);/* leading word */
    JC_CHECK(jc_sup_stop_line("JICHI-CONTROL:supervise-stop:1\n") == 0);/* newline */
    JC_CHECK(jc_sup_stop_line("") == 0);
    JC_CHECK(jc_sup_stop_line(NULL) == 0);
    JC_CHECK(jc_sup_stop_strstr(NULL) == 0);
}
