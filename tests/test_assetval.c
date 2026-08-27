/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_assetval.c - asset frontmatter validation (jc_assetval.c). */

#include "jc_test.h"
#include "jc_assetval.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_mem.h"
#include "jc_vec.h"

#include <string.h>

/* Parse `text` and validate it as `kind`; returns the issue count. */
static int check(struct jc_arena *a, int kind, const char *text)
{
    struct jc_md_doc doc;
    struct jc_vec issues;
    int n;

    memset(&doc, 0, sizeof(doc));
    jc_md_parse(text, a, &doc);
    jc_vec_init(&issues, sizeof(char *));
    n = jc_assetval_check(kind, text, doc.front, a, &issues);
    jc_vec_free(&issues);
    jc_md_free(&doc);
    return n;
}

void test_assetval(void)
{
    struct jc_arena *a = jc_arena_new(0);

    /* Built-in command collision predicate. */
    JC_CHECK(jc_assetval_is_builtin_command("review") == 1);
    JC_CHECK(jc_assetval_is_builtin_command("undo") == 1);
    JC_CHECK(jc_assetval_is_builtin_command("assign") == 0);
    JC_CHECK(jc_assetval_is_builtin_command("") == 0);
    JC_CHECK(jc_assetval_is_builtin_command(NULL) == 0);

    /* A well-formed agent: no issues. */
    JC_CHECK(check(a, JC_ASSET_AGENT,
        "---\ndescription: A reviewer\nreadonly: true\ntools:\n  - read_file\n"
        "---\nYou review code.\n") == 0);

    /* Unknown key + missing description => 2 issues. */
    JC_CHECK(check(a, JC_ASSET_AGENT,
        "---\ndescripton: typo\nreadony: true\n---\nbody\n") == 3);
    /* (descripton unknown, readony unknown, and no 'description' => 3) */

    /* A well-formed skill (name + description + allowed-tools): no issues. */
    JC_CHECK(check(a, JC_ASSET_SKILL,
        "---\nname: commit-message\ndescription: Write a commit\n"
        "allowed-tools:\n  - git_diff\n---\nbody\n") == 0);

    /* `agent:` is valid on a command but not on an agent. */
    JC_CHECK(check(a, JC_ASSET_COMMAND,
        "---\ndescription: route\nagent: reviewer\n---\n$ARGUMENTS\n") == 0);
    JC_CHECK(check(a, JC_ASSET_AGENT,
        "---\ndescription: x\nagent: reviewer\n---\nbody\n") == 1); /* agent: */

    /* Unterminated frontmatter (opened with --- but never closed). */
    JC_CHECK(check(a, JC_ASSET_AGENT,
        "---\ndescription: oops\nno closing fence here\n") == 1);

    /* No frontmatter at all (body-only) is allowed: no issues. */
    JC_CHECK(check(a, JC_ASSET_COMMAND, "Just a template body $ARGUMENTS\n")
             == 0);

    jc_arena_free(a);
}
