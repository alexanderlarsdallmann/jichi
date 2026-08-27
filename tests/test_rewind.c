/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_rewind.c - checkpoint<->turn mapping (M34c). */

#include "jc_test.h"
#include "jc_rewind.h"
#include <string.h>

void test_rewind(void)
{
    /* Label matching: exact, whitespace-collapsed, and prefix (the git-subject
     * form a refreshed checkpoint carries) all match; a different prompt does
     * not; empty inputs never match. */
    JC_CHECK(jc_rewind_label_match("add a hello function",
                                   "add a hello function") == 1);
    JC_CHECK(jc_rewind_label_match("add   a\n  hello   function",
                                   "add a hello function") == 1);
    /* Multi-line message vs a first-line-only (git subject) label: the label is
     * a prefix of the collapsed message. */
    JC_CHECK(jc_rewind_label_match("fix the parser\nand add tests",
                                   "fix the parser") == 1);
    /* And the reverse: a truncated stored label is a prefix of the message. */
    JC_CHECK(jc_rewind_label_match("refactor the whole module carefully",
                                   "refactor the whole") == 1);
    JC_CHECK(jc_rewind_label_match("write the docs", "fix the bug") == 0);
    JC_CHECK(jc_rewind_label_match("", "anything") == 0);
    JC_CHECK(jc_rewind_label_match("something", "") == 0);
    JC_CHECK(jc_rewind_label_match(NULL, "x") == 0);

    /* Ordered greedy assignment: three user turns, two of which took a
     * checkpoint (turns 0 and 2). Each checkpoint maps to its triggering user
     * message; the non-mutating turn (1) is skipped. */
    {
        const char *users[3];
        const char *labels[2];
        int out[2];
        users[0] = "add a hello function";
        users[1] = "explain what it does"; /* read-only turn: no checkpoint */
        users[2] = "now write tests for it";
        labels[0] = "add a hello function";
        labels[1] = "now write tests for it";
        jc_rewind_match(users, 3, labels, 2, out);
        JC_CHECK(out[0] == 0);
        JC_CHECK(out[1] == 2);
    }

    /* A checkpoint whose label matches nothing yields -1, and the scan still
     * resolves the rest. */
    {
        const char *users[2];
        const char *labels[2];
        int out[2];
        users[0] = "first task";
        users[1] = "second task";
        labels[0] = "first task";
        labels[1] = "a label with no matching user message";
        jc_rewind_match(users, 2, labels, 2, out);
        JC_CHECK(out[0] == 0);
        JC_CHECK(out[1] == -1);
    }

    /* Duplicate prompts map to distinct, increasing user indices (monotonic
     * advance), not both to the first. */
    {
        const char *users[2];
        const char *labels[2];
        int out[2];
        users[0] = "retry";
        users[1] = "retry";
        labels[0] = "retry";
        labels[1] = "retry";
        jc_rewind_match(users, 2, labels, 2, out);
        JC_CHECK(out[0] == 0);
        JC_CHECK(out[1] == 1);
    }
}
