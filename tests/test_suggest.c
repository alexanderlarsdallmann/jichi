/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_suggest.c - the pure halves of the TUI's Ctrl-G / Ctrl-Q gestures
 * (M280). Every case below is a thing a model did or predictably does, not a
 * hypothetical: an echoed prefix, an "output:" label copied from the few-shot
 * examples, a quoted continuation, a multi-line answer where one line was
 * asked for. The cleaners exist because the prompt asking nicely is not
 * enough -- which is the whole reason this feature was revisited. */

#include "jc_test.h"
#include "jc_suggest.h"

#include <string.h>

static void ts_system_prompts(void)
{
    char sbuf[JC_SUGGEST_SYS_CAP];
    char abuf[JC_SUGGEST_SYS_CAP];
    const char *s = sbuf;
    const char *a = abuf;

    /* Both must FIT: a truncated prompt would silently drop the examples,
     * which are the entire point of the fix. */
    JC_CHECK(jc_suggest_system(sbuf, sizeof sbuf) > 0);
    JC_CHECK(jc_advice_system(abuf, sizeof abuf) > 0);
    JC_CHECK(sbuf[jc_suggest_system(sbuf, sizeof sbuf) - 1] == '.');

    /* The few-shot examples are the fix; assert they are actually present,
     * including the exact field-reported line, so a well-meaning edit that
     * "tidies" the prompt cannot silently remove the demonstration. */
    JC_CHECK(s != NULL && strlen(s) > 200);
    JC_CHECK(strstr(s, "what is the name of this pr") != NULL);
    JC_CHECK(strstr(s, "oject?") != NULL);
    JC_CHECK(strstr(s, "on aarch64?") != NULL);      /* the question example */
    JC_CHECK(strstr(s, "clarification") != NULL);    /* the anti-example */

    JC_CHECK(a != NULL && strlen(a) > 100);
    JC_CHECK(strstr(a, JC_ADVICE_CLEAR) != NULL);
    /* Advice must not drift into answering the request. */
    JC_CHECK(strstr(a, "not answering") != NULL);
}

static void ts_suggest_clean(void)
{
    char out[128];

    /* The ordinary case: a mid-word continuation, untouched. */
    JC_CHECK(jc_suggest_clean("what is the name of this pr", "oject?",
                              out, sizeof out) == 6);
    JC_CHECK_STR(out, "oject?");

    /* A leading space is PRESERVED -- the ghost is appended verbatim, so
     * eating it would corrupt the line at a word boundary. */
    jc_suggest_clean("add a test for the arena",
                     " allocator reset", out, sizeof out);
    JC_CHECK_STR(out, " allocator reset");

    /* A leading blank line (very common) is skipped. */
    jc_suggest_clean("why does the build fail", "\n\n on aarch64?",
                     out, sizeof out);
    JC_CHECK_STR(out, " on aarch64?");

    /* The few-shot prompt invites this one: the model mimics the example
     * format and prefixes its answer with the label. */
    jc_suggest_clean("list the ", "output: files in src/", out, sizeof out);
    JC_CHECK_STR(out, "files in src/");
    jc_suggest_clean("list the ", "OUTPUT:   files in src/", out, sizeof out);
    JC_CHECK_STR(out, "files in src/");

    /* An echo of the user's own text, which the prompt forbids and models
     * still do. Only the verbatim full prefix goes. */
    jc_suggest_clean("what is the name of this pr",
                     "what is the name of this project?", out, sizeof out);
    JC_CHECK_STR(out, "oject?");

    /* A partial-word overlap is NOT guessed at: dropping "pr" here would be
     * a heuristic that corrupts legitimate continuations, so the reply is
     * kept as-is and the user simply dismisses it. */
    jc_suggest_clean("what is the name of this pr", "project?",
                     out, sizeof out);
    JC_CHECK_STR(out, "project?");

    /* Quotes around the whole continuation are stripped, one layer only. */
    jc_suggest_clean("say ", "\"hello there\"", out, sizeof out);
    JC_CHECK_STR(out, "hello there");
    jc_suggest_clean("say ", "'hi'", out, sizeof out);
    JC_CHECK_STR(out, "hi");

    /* Only ONE line survives: the ghost overlay is single-line by design. */
    jc_suggest_clean("fix the ", "bug in jc_mem.c\nand add a test",
                     out, sizeof out);
    JC_CHECK_STR(out, "bug in jc_mem.c");

    /* Trailing whitespace is never useful. */
    jc_suggest_clean("a", "bc   ", out, sizeof out);
    JC_CHECK_STR(out, "bc");

    /* Degenerate inputs must be safe, and must render nothing. */
    JC_CHECK(jc_suggest_clean("x", NULL, out, sizeof out) == 0);
    JC_CHECK(jc_suggest_clean(NULL, "hello", out, sizeof out) == 5);
    JC_CHECK(jc_suggest_clean("x", "y", NULL, 0) == 0);
    JC_CHECK(jc_suggest_clean("x", "\n\n\n", out, sizeof out) == 0);

    /* The cap is respected, NUL included. */
    {
        char small[5];
        jc_size n = jc_suggest_clean("x", "abcdefgh", small, sizeof small);
        JC_CHECK(n == 4);
        JC_CHECK_STR(small, "abcd");
    }
}

static void ts_advice_clean(void)
{
    char out[128];

    /* The ordinary case. */
    jc_advice_clean("which project? name the repo or path", out, sizeof out);
    JC_CHECK_STR(out, "which project? name the repo or path");

    /* Labels the model adds despite being told not to. */
    jc_advice_clean("advice: name the file to change", out, sizeof out);
    JC_CHECK_STR(out, "name the file to change");
    jc_advice_clean("Hint:  say which test should fail first",
                    out, sizeof out);
    JC_CHECK_STR(out, "say which test should fail first");

    /* Leading whitespace IS trimmed here (unlike ghost text): this line is
     * printed on its own, so indentation is noise rather than meaning. */
    jc_advice_clean("   which branch?", out, sizeof out);
    JC_CHECK_STR(out, "which branch?");

    /* Multi-line replies collapse to the first line -- one line was asked
     * for, and a printed paragraph would push the prompt around. */
    jc_advice_clean("which file?\n- also which branch\n- and the target",
                    out, sizeof out);
    JC_CHECK_STR(out, "which file?");

    /* The sentinel passes through untouched, so the TUI can render a
     * definite "nothing to fix". */
    jc_advice_clean(JC_ADVICE_CLEAR, out, sizeof out);
    JC_CHECK_STR(out, JC_ADVICE_CLEAR);

    jc_advice_clean("\"quoted advice\"", out, sizeof out);
    JC_CHECK_STR(out, "quoted advice");

    JC_CHECK(jc_advice_clean(NULL, out, sizeof out) == 0);
    JC_CHECK(jc_advice_clean("", out, sizeof out) == 0);
    JC_CHECK(jc_advice_clean("\n\n", out, sizeof out) == 0);
}

void test_suggest(void)
{
    ts_system_prompts();
    ts_suggest_clean();
    ts_advice_clean();
}
