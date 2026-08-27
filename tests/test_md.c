/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_md.c - markdown frontmatter splitter (jc_md). */

#include "jc_test.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_mem.h"

#include <string.h>

void test_md(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_md_doc doc;

    /* Frontmatter present. */
    {
        const char *text =
            "---\n"
            "description: A test command\n"
            "model: fast\n"
            "---\n"
            "Body line one\nBody line two\n";
        jc_md_parse(text, a, &doc);
        JC_CHECK(doc.front != NULL);
        JC_CHECK_STR(jc_yaml_get_str(doc.front, "description", "?"),
                     "A test command");
        JC_CHECK_STR(jc_yaml_get_str(doc.front, "model", "?"), "fast");
        JC_CHECK(doc.body != NULL &&
                 strcmp(doc.body, "Body line one\nBody line two\n") == 0);
        jc_md_free(&doc);
    }

    /* No frontmatter: whole text is the body. */
    {
        const char *text = "Just a prompt body, no fence.\n";
        jc_md_parse(text, a, &doc);
        JC_CHECK(doc.front == NULL);
        JC_CHECK(doc.body == text);
        jc_md_free(&doc);
    }

    /* Unterminated fence: treated as no frontmatter (graceful). */
    {
        const char *text = "---\ndescription: oops\nno closing fence here\n";
        jc_md_parse(text, a, &doc);
        JC_CHECK(doc.front == NULL);
        JC_CHECK(doc.body == text);
        jc_md_free(&doc);
    }

    /* NULL input. */
    {
        jc_md_parse(NULL, a, &doc);
        JC_CHECK(doc.front == NULL);
        JC_CHECK(doc.body == NULL);
        jc_md_free(&doc);
    }

    /* jc_md_frontmatter_unterminated: the common "forgot the closing ---". */
    {
        /* Opened, never closed -> suspect. */
        JC_CHECK(jc_md_frontmatter_unterminated(
            "---\ntitle: x\nverify: make test\nbody with no close\n") == 1);
        /* Only the opening fence, EOF right after. */
        JC_CHECK(jc_md_frontmatter_unterminated("---\n") == 1);
        JC_CHECK(jc_md_frontmatter_unterminated("---") == 1);
        /* Properly closed -> not suspect. */
        JC_CHECK(jc_md_frontmatter_unterminated(
            "---\ntitle: x\n---\nbody\n") == 0);
        /* Trailing whitespace on the closing fence is tolerated (not suspect). */
        JC_CHECK(jc_md_frontmatter_unterminated(
            "---\ntitle: x\n---  \nbody\n") == 0);
        /* Leading blank lines before the opener, then unterminated -> suspect. */
        JC_CHECK(jc_md_frontmatter_unterminated(
            "\n\n---\ntitle: x\nno close\n") == 1);
        /* No frontmatter intended (plain body) -> not suspect. */
        JC_CHECK(jc_md_frontmatter_unterminated("Just a body.\n") == 0);
        /* A body containing a horizontal rule but no opener -> not suspect. */
        JC_CHECK(jc_md_frontmatter_unterminated("Intro\n\n---\nmore\n") == 0);
        /* Empty / NULL. */
        JC_CHECK(jc_md_frontmatter_unterminated("") == 0);
        JC_CHECK(jc_md_frontmatter_unterminated(NULL) == 0);
    }

    jc_arena_free(a);
}
