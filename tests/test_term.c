/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_term.c - the pure terminal-width helper (jc_term_str_cols). */

#include "jc_test.h"
#include "jc_term.h"

#include <string.h>

static int cols(const char *s)
{
    return jc_term_str_cols(s, (jc_size)strlen(s));
}

void test_term(void)
{
    /* Plain ASCII: one column per byte. */
    JC_CHECK(cols("hello") == 5);
    JC_CHECK(cols("") == 0);
    JC_CHECK(jc_term_str_cols(NULL, 0) == 0);

    /* ANSI CSI sequences contribute zero columns. */
    JC_CHECK(cols("\x1b[1;32mhi\x1b[0m") == 2);
    JC_CHECK(cols("\x1b[36m[chat]\x1b[0m") == 6); /* [chat] = 6 */

    /* UTF-8 characters count one column each (continuation bytes count 0):
     * middot · = C2 B7, single-arrow > = E2 80 BA. */
    JC_CHECK(cols("\xc2\xb7\xe2\x80\xba") == 2);

    /* A realistic colored prompt: "[chat]" (6) + " " + arrow (1) + " " = 9. */
    JC_CHECK(cols("\x1b[36m[chat]\x1b[0m \xe2\x80\xba ") == 9);

    /* Honors the byte limit n. */
    JC_CHECK(jc_term_str_cols("abcdef", 3) == 3);

    /* M362: the accessible fast-echo predicates. Measured motivation: the
     * full redraw is one ESC[J + ~39 bytes per keystroke, which a screen
     * reader re-announces as a changed line; the fast path emits one byte.
     * Every guard is a case render() handles and the fast path must not. */
    {
        /* The plain case: ASCII at end of a short line -> fast. */
        JC_CHECK(jc_term_fast_echo_ok('a', 1, 10, 5, 80) == 1);
        /* Mid-line insert: the tail must be repainted -> render. */
        JC_CHECK(jc_term_fast_echo_ok('a', 0, 10, 5, 80) == 0);
        /* Multi-byte UTF-8 arrives byte-wise -> render places it. */
        JC_CHECK(jc_term_fast_echo_ok(0xC3, 1, 10, 5, 80) == 0);
        JC_CHECK(jc_term_fast_echo_ok(0xE2, 1, 10, 5, 80) == 0);
        /* Control chars are never fast-echoed. */
        JC_CHECK(jc_term_fast_echo_ok(27, 1, 10, 5, 80) == 0);
        /* Landing exactly on the column boundary = the phantom last
         * column; only render()'s CR/LF resolution handles it. */
        JC_CHECK(jc_term_fast_echo_ok('a', 1, 10, 70, 80) == 0);
        /* ...one short of the boundary is still fast. */
        JC_CHECK(jc_term_fast_echo_ok('a', 1, 10, 69, 80) == 1);
        /* Unknown terminal width -> render. */
        JC_CHECK(jc_term_fast_echo_ok('a', 1, 10, 5, 0) == 0);

        /* Backspace: plain case fast; col-0-of-a-wrapped-row (the \b that
         * cannot cross a row boundary) -> render; UTF-8 tail -> render. */
        JC_CHECK(jc_term_fast_bs_ok('a', 1, 10, 5, 80) == 1);
        JC_CHECK(jc_term_fast_bs_ok('a', 1, 10, 70, 80) == 0);
        JC_CHECK(jc_term_fast_bs_ok(0xB7, 1, 10, 5, 80) == 0);
        JC_CHECK(jc_term_fast_bs_ok('a', 0, 10, 5, 80) == 0);
        JC_CHECK(jc_term_fast_bs_ok('a', 1, 10, 5, 0) == 0);
    }

    /* M363: tab-aware column math. A tab advances to the next 8-column stop
     * of the ABSOLUTE position, so the start column changes its width --
     * before this, a tab counted as width 1 while the terminal jumped up to
     * 8, and every keystroke after a pasted tab repositioned the cursor from
     * wrong geometry. */
    {
        /* From column 0: "\t" -> 8; "ab\t" -> 8 (2 then jump to 8). */
        JC_CHECK(jc_term_str_cols_from(0, "\t", 1) == 8);
        JC_CHECK(jc_term_str_cols_from(0, "ab\t", 3) == 8);
        /* Start at 5: "ab" reaches abs 7, tab jumps to 8 -> width 3. */
        JC_CHECK(jc_term_str_cols_from(5, "ab\t", 3) == 3);
        /* A tab exactly AT a stop advances a full 8. */
        JC_CHECK(jc_term_str_cols_from(8, "\t", 1) == 8);
        /* Two tabs from 0: 8 then 16. */
        JC_CHECK(jc_term_str_cols_from(0, "\t\t", 2) == 16);
        /* Content after the tab counts from the stop. */
        JC_CHECK(jc_term_str_cols_from(0, "a\tb", 3) == 9);
        /* The zero-start wrapper is the same walk. */
        JC_CHECK(jc_term_str_cols("a\tb", 3) ==
                 jc_term_str_cols_from(0, "a\tb", 3));
        /* Negative start clamps to 0; NULL is 0 columns. */
        JC_CHECK(jc_term_str_cols_from(-3, "\t", 1) == 8);
        JC_CHECK(jc_term_str_cols_from(4, NULL, 0) == 0);
        /* Escape sequences still contribute zero columns from any start. */
        JC_CHECK(jc_term_str_cols_from(3, "\x1b[1mx", 5) == 1);
    }
}
