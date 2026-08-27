/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_mdrender.c - the streaming markdown + light-syntax renderer. */

#include "jc_test.h"
#include "jc_mdrender.h"
#include "jc_str.h"

#include <string.h>

/* Render a whole string through the renderer (color on), return the output. */
static void render_all(const char *s, int color, struct jc_sb *out)
{
    struct jc_mdr r;
    jc_mdr_init(&r, color);
    jc_mdr_feed(&r, s, (jc_size)strlen(s), out);
    jc_mdr_flush(&r, out);
    jc_mdr_free(&r);
}

static void test_blocks(void)
{
    struct jc_sb sb;

    /* Heading -> bold cyan; the text is preserved. */
    jc_sb_init(&sb);
    render_all("# Title\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[1;36m") != NULL);
    JC_CHECK(strstr(sb.data, "# Title") != NULL);
    jc_sb_free(&sb);

    /* Inline bold + inline code get styled. */
    jc_sb_init(&sb);
    render_all("a **b** and `c` end\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[1m") != NULL);   /* bold */
    JC_CHECK(strstr(sb.data, "\x1b[36m") != NULL);  /* inline code cyan */
    jc_sb_free(&sb);

    /* List marker kept, remainder inline-rendered. */
    jc_sb_init(&sb);
    render_all("- item **x**\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "- item ") != NULL);
    JC_CHECK(strstr(sb.data, "\x1b[1m") != NULL);
    jc_sb_free(&sb);
}

static void test_code_fence(void)
{
    struct jc_sb sb;
    jc_sb_init(&sb);
    render_all("```c\nint x = 42; // note\nchar *s = \"hi\";\n```\n", 1, &sb);
    /* Fence lines dimmed. */
    JC_CHECK(strstr(sb.data, "\x1b[2m") != NULL);
    /* Code block color, number, and comment styling present. */
    JC_CHECK(strstr(sb.data, "\x1b[32m") != NULL); /* code green */
    JC_CHECK(strstr(sb.data, "\x1b[35m") != NULL); /* number magenta */
    JC_CHECK(strstr(sb.data, "\x1b[33m") != NULL); /* string yellow */
    /* 'int' is keyword-colored now, so it is no longer contiguous with " x". */
    JC_CHECK(strstr(sb.data, "int") != NULL && strstr(sb.data, "x = ") != NULL);
    jc_sb_free(&sb);

    /* A '#' line inside a fence is a comment, NOT a heading. */
    jc_sb_init(&sb);
    render_all("```\n# not a heading\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[1;36m") == NULL); /* no heading style */
    JC_CHECK(strstr(sb.data, "# not a heading") != NULL);
    jc_sb_free(&sb);
}

static void test_syntax_highlight(void)
{
    struct jc_sb sb;

    /* C: 'int'/'return' keyword-colored (blue), and #include is NOT a comment. */
    jc_sb_init(&sb);
    render_all("```c\nint x = 42;\n#include <stdio.h>\nreturn 0;\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[34mint\x1b[32m") != NULL); /* kw blue */
    JC_CHECK(strstr(sb.data, "\x1b[34mreturn") != NULL);
    /* #include must not be dimmed as a comment (no M_CMT right before it). */
    JC_CHECK(strstr(sb.data, "\x1b[2m#include") == NULL);
    JC_CHECK(strstr(sb.data, "#include <stdio.h>") != NULL);
    jc_sb_free(&sb);

    /* Python: 'def' keyword + trailing '#' comment. */
    jc_sb_init(&sb);
    render_all("```python\ndef f(): # note\n    pass\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[34mdef") != NULL);
    JC_CHECK(strstr(sb.data, "\x1b[2m# note") != NULL); /* # comment dimmed */
    jc_sb_free(&sb);

    /* Lisp family: ';' comment + 'define' keyword. */
    jc_sb_init(&sb);
    render_all("```scheme\n(define x 1) ; a comment\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[34mdefine") != NULL);
    JC_CHECK(strstr(sb.data, "\x1b[2m; a comment") != NULL);
    jc_sb_free(&sb);

    /* Haskell: '--' line comment. */
    jc_sb_init(&sb);
    render_all("```haskell\nx = 1 -- a comment\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[2m-- a comment") != NULL);
    jc_sb_free(&sb);

    /* Unknown/no language tag: generic fallback still highlights // + numbers,
     * and does not keyword-color (no M_KW). */
    jc_sb_init(&sb);
    render_all("```\nfoo = 42; // bar\n```\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[35m42") != NULL);       /* number */
    JC_CHECK(strstr(sb.data, "\x1b[2m// bar") != NULL);    /* // comment */
    JC_CHECK(strstr(sb.data, "\x1b[34m") == NULL);         /* no keywords */
    jc_sb_free(&sb);
}

static void test_block_comment_multiline(void)
{
    struct jc_sb sb;
    /* A C-style block comment spanning three lines in a C fence: the middle
     * line (plain prose) must be dimmed as a comment, and code after the closing
     * delimiter must highlight normally again. */
    jc_sb_init(&sb);
    render_all("```c\nint a; /* open\nstill comment\nclose */ int b = 2;\n```\n",
               1, &sb);
    /* The bare middle line is comment-dimmed (it would otherwise be plain). */
    JC_CHECK(strstr(sb.data, "\x1b[2mstill comment") != NULL);
    /* 'int' after the close is keyword-colored again, and 2 is a number. */
    JC_CHECK(strstr(sb.data, "\x1b[34mint\x1b[32m b") != NULL);
    JC_CHECK(strstr(sb.data, "\x1b[35m2") != NULL);
    jc_sb_free(&sb);

    /* Closing a fence cancels an open block comment (no bleed past the fence). */
    jc_sb_init(&sb);
    render_all("```c\n/* unterminated\n```\nplain **after**\n", 1, &sb);
    JC_CHECK(strstr(sb.data, "after") != NULL); /* renders, no crash/bleed */
    jc_sb_free(&sb);
}

static void test_streaming_split(void)
{
    /* A bold span and a fence split across feed() calls must still render. */
    struct jc_mdr r;
    struct jc_sb sb;
    jc_sb_init(&sb);
    jc_mdr_init(&r, 1);
    jc_mdr_feed(&r, "he **bo", 7, &sb);   /* no newline yet: buffered */
    JC_CHECK(sb.len == 0);                 /* nothing emitted until a line ends */
    jc_mdr_feed(&r, "ld** x\n", 7, &sb);   /* completes the line */
    JC_CHECK(strstr(sb.data, "\x1b[1m") != NULL);
    JC_CHECK(strstr(sb.data, "bold") != NULL);
    jc_mdr_free(&r);
    jc_sb_free(&sb);
}

static void test_passthrough_and_flush(void)
{
    struct jc_sb sb;

    /* color == 0: text passes through unchanged (no ANSI). */
    jc_sb_init(&sb);
    render_all("# Title\n**b** `c`\n", 0, &sb);
    JC_CHECK(strstr(sb.data, "\x1b[") == NULL);
    JC_CHECK(strstr(sb.data, "# Title") != NULL);
    JC_CHECK(strstr(sb.data, "**b**") != NULL);
    jc_sb_free(&sb);

    /* flush renders a trailing line that has no newline. */
    {
        struct jc_mdr r;
        jc_sb_init(&sb);
        jc_mdr_init(&r, 1);
        jc_mdr_feed(&r, "## Tail", 7, &sb);  /* no newline */
        jc_mdr_flush(&r, &sb);
        JC_CHECK(strstr(sb.data, "Tail") != NULL);
        JC_CHECK(strstr(sb.data, "\x1b[1;36m") != NULL);
        jc_mdr_free(&r);
        jc_sb_free(&sb);
    }
}

void test_mdrender(void)
{
    test_blocks();
    test_code_fence();
    test_syntax_highlight();
    test_block_comment_multiline();
    test_streaming_split();
    test_passthrough_and_flush();
}
