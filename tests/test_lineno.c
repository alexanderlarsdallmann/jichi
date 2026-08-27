/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_lineno.c - the line-number gutter formatter (jc_format_numbered). */

#include "jc_test.h"
#include "jc_lineno.h"
#include "jc_str.h"

#include <string.h>

void test_lineno(void)
{
    struct jc_sb sb;
    int total = -1;
    int shown;

    /* Whole file: each line gets a 6-wide number + tab; trailing newline does
     * not create a phantom final line. */
    jc_sb_init(&sb);
    shown = jc_format_numbered("a\nbb\nccc\n", 1, 0, &sb, &total);
    JC_CHECK(shown == 3 && total == 3);
    JC_CHECK(strcmp(sb.data,
        "     1\ta\n     2\tbb\n     3\tccc\n") == 0);
    jc_sb_free(&sb);

    /* No trailing newline: the last line still counts. */
    jc_sb_init(&sb);
    shown = jc_format_numbered("x\ny", 1, 0, &sb, &total);
    JC_CHECK(shown == 2 && total == 2);
    JC_CHECK(strcmp(sb.data, "     1\tx\n     2\ty\n") == 0);
    jc_sb_free(&sb);

    /* offset + limit: a slice keeps absolute line numbers. */
    jc_sb_init(&sb);
    shown = jc_format_numbered("l1\nl2\nl3\nl4\nl5\n", 2, 2, &sb, &total);
    JC_CHECK(shown == 2 && total == 5);
    JC_CHECK(strcmp(sb.data, "     2\tl2\n     3\tl3\n") == 0);
    jc_sb_free(&sb);

    /* offset past the end: nothing emitted, total still reported. */
    jc_sb_init(&sb);
    shown = jc_format_numbered("one\ntwo\n", 9, 0, &sb, &total);
    JC_CHECK(shown == 0 && total == 2);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* offset < 1 clamps to 1; NULL text is empty. */
    jc_sb_init(&sb);
    JC_CHECK(jc_format_numbered("hi\n", 0, 0, &sb, &total) == 1);
    JC_CHECK(strstr(sb.data, "     1\thi\n") != NULL);
    jc_sb_free(&sb);

    jc_sb_init(&sb);
    JC_CHECK(jc_format_numbered(NULL, 1, 0, &sb, &total) == 0);
    JC_CHECK(total == 0);
    jc_sb_free(&sb);
}

/* M200: control characters in a name must not break the line-oriented output
 * list_files hands the model. A filename may legally contain a newline, and
 * `evil\nplanted.txt` used to render as TWO entries -- the second a file that
 * does not exist -- which both misleads the model and gives a directory jichi
 * did not author (a cloned repo, an unpacked archive) a way to inject a
 * plausible-looking entry into trusted tool output. */
/* M594: jc_count_lines must agree with jc_format_numbered's *total_lines on
 * the same bytes -- that agreement is the whole point, since read_file reports
 * one where the other used to be. Every case below is checked BOTH ways. */
void test_count_lines(void)
{
    static const char *cases[] = {
        "", "a", "a\n", "a\nbb\nccc\n", "a\nbb\nccc", "\n", "\n\n", "a\n\nb\n"
    };
    jc_size i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct jc_sb sb;
        int total = -1;
        const char *t = cases[i];
        jc_sb_init(&sb);
        (void)jc_format_numbered(t, 1, 0, &sb, &total);
        JC_CHECK(jc_count_lines(t, strlen(t)) == total);
        jc_sb_free(&sb);
    }

    /* A NUL ends the scan, matching the formatter's strchr/strlen view. */
    JC_CHECK(jc_count_lines("a\nb\0c\nd", 8) == 2);
    /* NULL is zero, not a crash. */
    JC_CHECK(jc_count_lines(NULL, 10) == 0);
    /* The length bounds the scan: the same buffer, read shorter. */
    JC_CHECK(jc_count_lines("a\nb\nc", 6) == 3);
    JC_CHECK(jc_count_lines("a\nb\nc", 4) == 2);
    JC_CHECK(jc_count_lines("a\nb\nc", 2) == 1);
}

void test_escape_ctrl(void)
{
    char out[64];

    /* Ordinary names pass through byte-for-byte. */
    JC_CHECK(jc_escape_ctrl("plain.txt", out, sizeof out) == 9);
    JC_CHECK_STR(out, "plain.txt");

    /* The record separator is the case that mattered. */
    JC_CHECK(jc_escape_ctrl("evil\nplanted.txt", out, sizeof out) == 17);
    JC_CHECK_STR(out, "evil\\nplanted.txt");
    /* ...and the result contains no raw newline, which is the actual contract. */
    JC_CHECK(strchr(out, '\n') == NULL);

    /* The other whitespace controls, and a backslash so the escaping is
     * unambiguous (an existing literal backslash must not read as an escape). */
    JC_CHECK(jc_escape_ctrl("a\rb", out, sizeof out) == 4);
    JC_CHECK_STR(out, "a\\rb");
    JC_CHECK(jc_escape_ctrl("a\tb", out, sizeof out) == 4);
    JC_CHECK_STR(out, "a\\tb");
    JC_CHECK(jc_escape_ctrl("a\\b", out, sizeof out) == 4);
    JC_CHECK_STR(out, "a\\\\b");

    /* Arbitrary control bytes become \xNN, including DEL. */
    JC_CHECK(jc_escape_ctrl("a\001b", out, sizeof out) == 6);
    JC_CHECK_STR(out, "a\\x01b");
    JC_CHECK(jc_escape_ctrl("a\177b", out, sizeof out) == 6);
    JC_CHECK_STR(out, "a\\x7fb");
    /* An ESC would otherwise let a name emit ANSI into the transcript. */
    JC_CHECK(jc_escape_ctrl("\033[31mred", out, sizeof out) == 11);
    JC_CHECK_STR(out, "\\x1b[31mred");

    /* UTF-8 is left alone: these are not control bytes. */
    JC_CHECK(jc_escape_ctrl("caf\303\251.txt", out, sizeof out) == 9);
    JC_CHECK_STR(out, "caf\303\251.txt");

    /* Truncation is safe and never emits half an escape. */
    {
        char tiny[4];
        jc_size n = jc_escape_ctrl("ab\ncd", tiny, sizeof tiny);
        JC_CHECK(n <= 3);
        JC_CHECK(tiny[n] == '\0');
        JC_CHECK(strchr(tiny, '\n') == NULL);
        /* 4 bytes hold "ab" + NUL and cannot hold the 2-byte "\\n" as well. */
        JC_CHECK_STR(tiny, "ab");
    }
    /* Degenerate inputs. */
    JC_CHECK(jc_escape_ctrl(NULL, out, sizeof out) == 0);
    JC_CHECK(out[0] == '\0');
    JC_CHECK(jc_escape_ctrl("x", out, 0) == 0);
    JC_CHECK(jc_escape_ctrl("", out, sizeof out) == 0);
    JC_CHECK_STR(out, "");
}
