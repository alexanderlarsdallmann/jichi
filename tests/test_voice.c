/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_voice.c - reducing text for speech (M303). */

#include "jc_test.h"
#include "jc_voice.h"
#include "jc_str.h"

#include <string.h>

void test_voice(void)
{
    struct jc_sb sb;

    /* Plain prose survives unchanged. */
    jc_sb_init(&sb);
    jc_voice_speakable("The build is green.", &sb);
    JC_CHECK_STR(sb.data, "The build is green.");
    jc_sb_free(&sb);

    /* A FENCED CODE BLOCK becomes a summary. Reading C aloud line by line is not
     * accessibility, it is punishment -- and it is the difference between a
     * usable spoken reply and thirty seconds of punctuation. */
    jc_sb_init(&sb);
    jc_voice_speakable("Here is the fix:\n"
                       "```c\n"
                       "int a = 1;\n"
                       "int b = 2;\n"
                       "return a + b;\n"
                       "```\n"
                       "That is all.", &sb);
    JC_CHECK(strstr(sb.data, "Here is the fix:") != NULL);
    JC_CHECK(strstr(sb.data, "code block, 3 lines") != NULL);
    JC_CHECK(strstr(sb.data, "That is all.") != NULL);
    /* None of the code itself is spoken. */
    JC_CHECK(strstr(sb.data, "int a") == NULL);
    JC_CHECK(strstr(sb.data, "return") == NULL);
    JC_CHECK(strstr(sb.data, "```") == NULL);
    jc_sb_free(&sb);

    /* Singular line: "1 line", not "1 lines". */
    jc_sb_init(&sb);
    jc_voice_speakable("```\nonly\n```", &sb);
    JC_CHECK(strstr(sb.data, "code block, 1 line)") != NULL);
    jc_sb_free(&sb);

    /* An UNCLOSED fence must not swallow the rest silently -- it still reports a
     * block rather than emitting the code as prose. */
    jc_sb_init(&sb);
    jc_voice_speakable("before\n```\nx\ny\n", &sb);
    JC_CHECK(strstr(sb.data, "before") != NULL);
    JC_CHECK(strstr(sb.data, "code block") != NULL);
    JC_CHECK(strstr(sb.data, "x y") == NULL);
    jc_sb_free(&sb);

    /* Inline decoration is dropped: "star star important star star" is noise. */
    jc_sb_init(&sb);
    jc_voice_speakable("Run `make test` and check **every** _case_.", &sb);
    JC_CHECK_STR(sb.data, "Run make test and check every case.");
    jc_sb_free(&sb);

    /* Headings, bullets and blockquotes lose their furniture but keep their words. */
    jc_sb_init(&sb);
    jc_voice_speakable("# Result\n\n- first item\n- second item\n> a quote\n", &sb);
    JC_CHECK(strstr(sb.data, "Result") != NULL);
    JC_CHECK(strstr(sb.data, "first item") != NULL);
    JC_CHECK(strstr(sb.data, "second item") != NULL);
    JC_CHECK(strstr(sb.data, "a quote") != NULL);
    JC_CHECK(strstr(sb.data, "#") == NULL);
    JC_CHECK(strstr(sb.data, "- ") == NULL);
    JC_CHECK(strstr(sb.data, ">") == NULL);
    jc_sb_free(&sb);

    /* Whitespace runs collapse; blank lines do not become pauses of their own. */
    jc_sb_init(&sb);
    jc_voice_speakable("one\n\n\n   two\t\tthree", &sb);
    JC_CHECK_STR(sb.data, "one two three");
    jc_sb_free(&sb);

    /* A LONG reply is truncated, and SAYS so. With no barge-in a five-minute
     * monologue cannot be escaped, so this cap is a safety property rather than a
     * nicety. */
    {
        char big[4000];
        jc_size i;
        for (i = 0; i + 1 < sizeof(big); i++) {
            big[i] = 'a';
        }
        big[sizeof(big) - 1] = '\0';
        jc_sb_init(&sb);
        jc_voice_speakable(big, &sb);
        JC_CHECK(sb.len < (jc_size)(JC_VOICE_MAX_CHARS + 40));
        JC_CHECK(strstr(sb.data, "(truncated)") != NULL);
        jc_sb_free(&sb);
    }

    /* Truncation prefers a SENTENCE boundary when one is in range, so the last
     * thing heard is a finished thought rather than half a word. */
    {
        struct jc_sb src;
        jc_size i;
        jc_sb_init(&src);
        for (i = 0; i < 200; i++) {
            jc_sb_append(&src, "This is a sentence. ");
        }
        jc_sb_init(&sb);
        jc_voice_speakable(src.data, &sb);
        JC_CHECK(strstr(sb.data, "(truncated)") != NULL);
        /* The character before " (truncated)" should be a full stop. */
        {
            const char *t = strstr(sb.data, " (truncated)");
            JC_CHECK(t != NULL && t > sb.data && t[-1] == '.');
        }
        jc_sb_free(&sb);
        jc_sb_free(&src);
    }

    /* Nothing to say stays nothing: no stray spaces, no crash on NULL. */
    jc_sb_init(&sb);
    jc_voice_speakable("", &sb);
    JC_CHECK(sb.len == 0);
    jc_voice_speakable(NULL, &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_voice_speakable("text", NULL);   /* NULL sink: no-op, not a fault */

    /* A reply that is ONLY a code block still says something -- silence would be
     * indistinguishable from a failure to the user this feature is for. */
    jc_sb_init(&sb);
    jc_voice_speakable("```\na\nb\n```\n", &sb);
    JC_CHECK(sb.len > 0);
    JC_CHECK(strstr(sb.data, "code block, 2 lines") != NULL);
    jc_sb_free(&sb);
}
