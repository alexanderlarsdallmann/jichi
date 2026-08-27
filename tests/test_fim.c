/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_fim.c - pure fill-in-the-middle prompt assembly (jc_fim.c). */

#include "jc_test.h"
#include "jc_fim.h"
#include "jc_str.h"

#include <string.h>

static void test_bound(void)
{
    jc_size start;
    jc_size len;

    /* Under budget: keep everything, start at 0. */
    start = jc_fim_bound(10, 100, 1, &len);
    JC_CHECK(start == 0 && len == 10);
    start = jc_fim_bound(10, 100, 0, &len);
    JC_CHECK(start == 0 && len == 10);

    /* Budget 0 means no limit. */
    start = jc_fim_bound(5000, 0, 1, &len);
    JC_CHECK(start == 0 && len == 5000);

    /* Prefix (keep_tail=1): keep the LAST `budget` bytes. */
    start = jc_fim_bound(100, 30, 1, &len);
    JC_CHECK(start == 70 && len == 30);

    /* Suffix (keep_tail=0): keep the FIRST `budget` bytes. */
    start = jc_fim_bound(100, 30, 0, &len);
    JC_CHECK(start == 0 && len == 30);

    /* Exactly at budget: no trim. */
    start = jc_fim_bound(30, 30, 1, &len);
    JC_CHECK(start == 0 && len == 30);

    /* NULL out_len must not crash. */
    start = jc_fim_bound(100, 30, 1, NULL);
    JC_CHECK(start == 70);
}

static void test_build_user(void)
{
    struct jc_sb sb;

    jc_sb_init(&sb);
    jc_fim_build_user("int x = ", ";\n", &sb);
    JC_CHECK(strcmp(sb.data,
        "<BEFORE>int x = </BEFORE>\n<AFTER>;\n</AFTER>") == 0);
    jc_sb_free(&sb);

    /* NULL sides become empty (no crash). */
    jc_sb_init(&sb);
    jc_fim_build_user(NULL, NULL, &sb);
    JC_CHECK(strcmp(sb.data, "<BEFORE></BEFORE>\n<AFTER></AFTER>") == 0);
    jc_sb_free(&sb);

    /* The exact cursor boundary is preserved (no stray whitespace added around
     * the prefix/suffix text). */
    jc_sb_init(&sb);
    jc_fim_build_user("a", "b", &sb);
    JC_CHECK(strstr(sb.data, "<BEFORE>a</BEFORE>") != NULL);
    JC_CHECK(strstr(sb.data, "<AFTER>b</AFTER>") != NULL);
    jc_sb_free(&sb);
}

static void test_strip_fences(void)
{
    struct jc_sb sb;

    /* Fenced with a language tag: keep only the inner code. */
    jc_sb_init(&sb);
    jc_fim_strip_fences("```python\na + b\n```", &sb);
    JC_CHECK(strcmp(sb.data, "a + b") == 0);
    jc_sb_free(&sb);

    /* Fence without a language tag. */
    jc_sb_init(&sb);
    jc_fim_strip_fences("```\nreturn 1;\n```", &sb);
    JC_CHECK(strcmp(sb.data, "return 1;") == 0);
    jc_sb_free(&sb);

    /* Trailing blank line before the closing fence: only one newline trimmed. */
    jc_sb_init(&sb);
    jc_fim_strip_fences("```python\na + b\n\n```", &sb);
    JC_CHECK(strcmp(sb.data, "a + b\n") == 0);
    jc_sb_free(&sb);

    /* No fence: verbatim. */
    jc_sb_init(&sb);
    jc_fim_strip_fences("a + b", &sb);
    JC_CHECK(strcmp(sb.data, "a + b") == 0);
    jc_sb_free(&sb);

    /* Unterminated opening fence: drop only the opener line. */
    jc_sb_init(&sb);
    jc_fim_strip_fences("```js\nconst x = 1;", &sb);
    JC_CHECK(strcmp(sb.data, "const x = 1;") == 0);
    jc_sb_free(&sb);

    /* NULL is a no-op (leaves the buffer empty). */
    jc_sb_init(&sb);
    jc_fim_strip_fences(NULL, &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
}

void test_fim(void)
{
    test_bound();
    test_build_user();
    test_strip_fences();
}
