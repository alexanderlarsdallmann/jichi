/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_complete.c - pure Tab-completion helpers. */

#include "jc_test.h"
#include "jc_complete.h"

#include <stdlib.h>
#include <string.h>

static void test_token(void)
{
    jc_size start = 99;
    const char *t;

    t = jc_complete_token("/res", 4, &start);
    JC_CHECK(start == 0 && strcmp(t, "/res") == 0);

    t = jc_complete_token("/model q", 8, &start);
    JC_CHECK(start == 7 && strcmp(t, "q") == 0);

    t = jc_complete_token("see @src/ma", 11, &start);
    JC_CHECK(start == 4 && strcmp(t, "@src/ma") == 0);

    /* cursor right after a space => empty token starting at the cursor */
    t = jc_complete_token("/resume ", 8, &start);
    JC_CHECK(start == 8 && t[0] == '\0');

    /* empty buffer / cursor 0 */
    t = jc_complete_token("", 0, &start);
    JC_CHECK(start == 0 && t[0] == '\0');

    /* cursor mid-line picks the token ending at the cursor */
    t = jc_complete_token("/model qwen here", 8, &start);
    JC_CHECK(start == 7 && strncmp(t, "qw", 2) == 0);
}

static void test_prefix(void)
{
    char out[64];
    const char *a[3];

    a[0] = "resume"; a[1] = "reset";
    JC_CHECK(jc_complete_common_prefix(a, 2, out, sizeof out) == 3);
    JC_CHECK(strcmp(out, "res") == 0);

    a[0] = "/resume";
    JC_CHECK(jc_complete_common_prefix(a, 1, out, sizeof out) == 7);
    JC_CHECK(strcmp(out, "/resume") == 0);

    a[0] = "alpha"; a[1] = "beta";
    JC_CHECK(jc_complete_common_prefix(a, 2, out, sizeof out) == 0);
    JC_CHECK(out[0] == '\0');

    a[0] = "abcdef"; a[1] = "abcxyz";
    JC_CHECK(jc_complete_common_prefix(a, 2, out, 3) == 2); /* capped */
    JC_CHECK(strcmp(out, "ab") == 0);

    JC_CHECK(jc_complete_common_prefix(NULL, 0, out, sizeof out) == 0);
}

static void test_word_bounds(void)
{
    /* "foo bar baz", cursor at end (11) */
    const char *s = "foo bar baz";
    jc_size n = 11;
    /* word-left from end -> start of "baz" (8), then "bar" (4), then "foo" (0) */
    JC_CHECK(jc_line_word_left(s, n, 11) == 8);
    JC_CHECK(jc_line_word_left(s, n, 8) == 4);
    JC_CHECK(jc_line_word_left(s, n, 4) == 0);
    JC_CHECK(jc_line_word_left(s, n, 0) == 0);
    /* word-right from 0 -> past "foo" (3), then past "bar" (7), then end (11) */
    JC_CHECK(jc_line_word_right(s, n, 0) == 3);
    JC_CHECK(jc_line_word_right(s, n, 3) == 7);
    JC_CHECK(jc_line_word_right(s, n, 7) == 11);
    JC_CHECK(jc_line_word_right(s, n, 11) == 11);
    /* punctuation runs are skipped like whitespace */
    {
        const char *p = "a.-b";
        JC_CHECK(jc_line_word_right(p, 4, 1) == 4); /* from after 'a' -> end/'b' */
        JC_CHECK(jc_line_word_left(p, 4, 4) == 3);  /* from end -> start of 'b' */
    }
    /* NULL-safe */
    JC_CHECK(jc_line_word_left(NULL, 0, 0) == 0);
    JC_CHECK(jc_line_word_right(NULL, 5, 0) == 5);
}

/* M156: the pure paste-splice core -- the content that gets submitted after a
 * multiline paste. This is the bug's correctness property: newlines survive. */
static void ps_case(const char *before, const char *after, const char *pasted,
                    const char *want, jc_size want_cursor)
{
    jc_size cur = 999;
    char *got = jc_paste_splice(before, after, pasted, &cur);
    JC_CHECK(got != NULL);
    if (got != NULL) {
        JC_CHECK(strcmp(got, want) == 0);
        JC_CHECK(cur == want_cursor);
        free(got);
    }
}

static void test_paste_splice(void)
{
    /* Into an empty buffer: multiline paste is preserved verbatim (the bug). */
    ps_case("", "", "line1\nline2\nline3", "line1\nline2\nline3", 17);
    /* Single line: a plain splice, cursor past it (today's behaviour). */
    ps_case("", "", "hello", "hello", 5);
    /* CRLF and lone CR both normalize to LF. */
    ps_case("", "", "a\r\nb\rc", "a\nb\nc", 5);
    /* Append at end of existing text. */
    ps_case("pre ", "", "x\ny", "pre x\ny", 7);
    /* Mid-line paste: before + paste + after, cursor between paste and after. */
    ps_case("ab", "cd", "X\nY", "abX\nYcd", 5);
    /* Trailing newline: cursor sits at the boundary (empty editable tail). */
    ps_case("", "", "one\n", "one\n", 4);
    /* Empty paste is a no-op splice. */
    ps_case("keep", "", "", "keep", 4);
    /* NULLs are treated as empty, no crash. */
    ps_case(NULL, NULL, "z", "z", 1);

    /* M363: C0 controls (except newline and tab) and DEL are STRIPPED. The
     * editor re-emits the buffer on every redraw, so a pasted ESC would
     * replay pasted escape sequences into the terminal per keystroke --
     * paste injection on the output side. The cursor lands after the
     * SURVIVING bytes, so stripping keeps it consistent. */
    ps_case("", "", "a\x1b[31mred", "a[31mred", 8);      /* ESC dropped   */
    ps_case("", "", "ding\x07" "dong", "dingdong", 8);    /* BEL dropped   */
    ps_case("", "", "del\x7f" "ete", "delete", 6);        /* DEL dropped   */
    ps_case("", "", "a\x01\x02\x03z", "az", 2);           /* C0 run dropped */
    /* Tab and newline are CONTENT and survive. */
    ps_case("", "", "col1\tcol2", "col1\tcol2", 9);
    ps_case("", "", "a\tb\nc", "a\tb\nc", 5);
    /* UTF-8 high bytes pass untouched (no bytewise C1 stripping). */
    ps_case("", "", "caf\xc3\xa9", "caf\xc3\xa9", 5);
    /* Stripping composes with CRLF normalization and mid-line splice. */
    ps_case("ab", "cd", "X\x1b\r\nY", "abX\nYcd", 5);
}

void test_complete(void)
{
    test_token();
    test_prefix();
    test_word_bounds();
    test_paste_splice();
}
