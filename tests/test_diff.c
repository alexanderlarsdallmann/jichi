/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_diff.c - line-level unified-diff rendering (jc_diff.c). */

#include "jc_test.h"
#include "jc_diff.h"
#include "jc_str.h"

#include <string.h>

/* Count lines in `s` beginning with `pre`. */
static int count_prefix(const char *s, char pre)
{
    int n = 0;
    int bol = 1;
    const char *p;
    for (p = s; *p != '\0'; p++) {
        if (bol && *p == pre) {
            n++;
        }
        bol = (*p == '\n');
    }
    return n;
}

static void test_identical(void)
{
    struct jc_sb sb;
    int changed;
    jc_sb_init(&sb);
    changed = jc_diff_unified("a\nb\nc\n", "a\nb\nc\n", 3, 0, 0, &sb);
    JC_CHECK(changed == 0);
    JC_CHECK(sb.len == 0); /* nothing appended when identical */
    jc_sb_free(&sb);
}

static void test_single_change(void)
{
    struct jc_sb sb;
    int changed;
    jc_sb_init(&sb);
    /* One line changed in the middle. */
    changed = jc_diff_unified("a\nb\nc\n", "a\nB\nc\n", 3, 0, 0, &sb);
    JC_CHECK(changed == 2); /* one del + one add */
    JC_CHECK(strstr(sb.data, "-b\n") != NULL);
    JC_CHECK(strstr(sb.data, "+B\n") != NULL);
    JC_CHECK(strstr(sb.data, " a\n") != NULL); /* context */
    JC_CHECK(strstr(sb.data, " c\n") != NULL);
    JC_CHECK(strstr(sb.data, "@@") != NULL);
    jc_sb_free(&sb);
}

static void test_add_and_delete(void)
{
    struct jc_sb sb;
    int changed;

    /* Pure insertion into a new (empty) file. */
    jc_sb_init(&sb);
    changed = jc_diff_unified("", "x\ny\n", 3, 0, 0, &sb);
    JC_CHECK(changed == 2);
    JC_CHECK(count_prefix(sb.data, '+') == 2);
    JC_CHECK(count_prefix(sb.data, '-') == 0);
    JC_CHECK(strstr(sb.data, "@@ -0,0 +1,2 @@") != NULL);
    jc_sb_free(&sb);

    /* Pure deletion (file emptied). */
    jc_sb_init(&sb);
    changed = jc_diff_unified("x\ny\n", "", 3, 0, 0, &sb);
    JC_CHECK(changed == 2);
    JC_CHECK(count_prefix(sb.data, '-') == 2);
    JC_CHECK(count_prefix(sb.data, '+') == 0);
    jc_sb_free(&sb);
}

static void test_context_window(void)
{
    struct jc_sb sb;
    /* A change far from the edges: with context=1 only one context line on each
     * side of the change is shown, not the whole file. */
    const char *o = "1\n2\n3\n4\n5\n6\n7\n";
    const char *n = "1\n2\n3\nX\n5\n6\n7\n";
    int eq;
    jc_sb_init(&sb);
    jc_diff_unified(o, n, 1, 0, 0, &sb);
    eq = count_prefix(sb.data, ' ');
    JC_CHECK(eq == 2);                       /* lines 3 and 5 only */
    JC_CHECK(strstr(sb.data, " 1\n") == NULL); /* trimmed away */
    JC_CHECK(strstr(sb.data, "-4\n") != NULL);
    JC_CHECK(strstr(sb.data, "+X\n") != NULL);
    jc_sb_free(&sb);
}

static void test_two_hunks(void)
{
    struct jc_sb sb;
    /* Two changes far apart -> two separate @@ hunks (context=1). */
    const char *o = "a\nb\nc\nd\ne\nf\ng\nh\ni\n";
    const char *n = "a\nB\nc\nd\ne\nf\ng\nH\ni\n";
    const char *q;
    int hunks = 0;
    jc_sb_init(&sb);
    jc_diff_unified(o, n, 1, 0, 0, &sb);
    for (q = sb.data; (q = strstr(q, "@@ -")) != NULL; q += 3) {
        hunks++;
    }
    JC_CHECK(hunks == 2);
    jc_sb_free(&sb);
}

static void test_color_and_truncate(void)
{
    struct jc_sb sb;

    /* Color adds ANSI escapes around changed lines. */
    jc_sb_init(&sb);
    jc_diff_unified("a\n", "b\n", 3, 1, 0, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[") != NULL);
    jc_sb_free(&sb);

    /* max_out_lines caps the output with a truncation note. */
    jc_sb_init(&sb);
    jc_diff_unified("", "1\n2\n3\n4\n5\n6\n7\n8\n", 3, 0, 3, &sb);
    JC_CHECK(strstr(sb.data, "diff truncated") != NULL);
    jc_sb_free(&sb);
}

void test_diff(void)
{
    test_identical();
    test_single_change();
    test_add_and_delete();
    test_context_window();
    test_two_hunks();
    test_color_and_truncate();
}
