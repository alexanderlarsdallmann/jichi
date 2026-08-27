/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_workflow.c - workflow spec parse + prompt expansion (M101). */

#include "jc_test.h"
#include "jc_workflow.h"
#include "jc_str.h"
#include "jc_mem.h"
#include <string.h>

static const char *SPEC =
    "{\n"
    "  // a review workflow (JSONC comment)\n"
    "  \"name\": \"review\",\n"
    "  \"stages\": [\n"
    "    { \"type\": \"map\", \"prompt\": \"Review $ITEM for bugs\",\n"
    "      \"items\": [\"a.c\", \"b.c\"], \"readonly\": true },\n"
    "    { \"type\": \"synthesize\", \"prompt\": \"Combine the reviews\" },\n"
    "  ],\n"
    "}\n";

static void test_parse(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_workflow wf;

    JC_CHECK(jc_workflow_parse(SPEC, &wf, a) == JC_OK);
    JC_CHECK_STR(wf.name, "review");
    JC_CHECK(wf.nstages == 2);
    JC_CHECK(wf.stages[0].type == JC_WF_MAP);
    JC_CHECK(wf.stages[0].readonly == 1);
    JC_CHECK(wf.stages[0].nitems == 2);
    JC_CHECK_STR(wf.stages[0].items[0], "a.c");
    JC_CHECK_STR(wf.stages[0].prompt, "Review $ITEM for bugs");
    JC_CHECK(wf.stages[1].type == JC_WF_SYNTHESIZE);

    /* Errors. */
    JC_CHECK(jc_workflow_parse("not json", &wf, a) == JC_ERR_PARSE);
    JC_CHECK(jc_workflow_parse("{\"stages\":[]}", &wf, a) == JC_ERR_INVALID);
    /* Unknown stage types are skipped, leaving no usable stages => INVALID.
     * M610: and the drop is COUNTED, not silent. */
    JC_CHECK(jc_workflow_parse("{\"stages\":[{\"type\":\"bogus\"}]}", &wf, a)
             == JC_ERR_INVALID);
    JC_CHECK(wf.stages_dropped == 1);

    /* M610: over-cap stages and items are dropped, and the counts say so, so
     * run_workflow can warn instead of running a truncated spec as a success.
     * Build a spec with JC_WF_MAX_STAGES + 3 map stages, the first carrying
     * JC_WF_MAX_ITEMS + 5 items. Pure string build (no Date/rand). */
    {
        struct jc_sb sb;
        int i;
        jc_sb_init(&sb);
        jc_sb_append(&sb, "{\"stages\":[");
        for (i = 0; i < JC_WF_MAX_STAGES + 3; i++) {
            if (i > 0) jc_sb_append(&sb, ",");
            jc_sb_append(&sb, "{\"type\":\"map\",\"prompt\":\"p\",\"items\":[");
            if (i == 0) {
                int k;
                for (k = 0; k < JC_WF_MAX_ITEMS + 5; k++) {
                    if (k > 0) jc_sb_append(&sb, ",");
                    jc_sb_append(&sb, "\"x\"");
                }
            }
            jc_sb_append(&sb, "]}");
        }
        jc_sb_append(&sb, "]}");
        JC_CHECK(jc_workflow_parse(sb.data, &wf, a) == JC_OK);
        JC_CHECK(wf.nstages == JC_WF_MAX_STAGES);
        JC_CHECK(wf.stages_dropped == 3);        /* the 3 over the stage cap */
        JC_CHECK(wf.items_dropped == 5);         /* the 5 over the item cap  */
        JC_CHECK(wf.stages[0].nitems == JC_WF_MAX_ITEMS);
        jc_sb_free(&sb);
    }

    jc_arena_free(a);
}

static void test_expand(void)
{
    struct jc_arena *a = jc_arena_new(0);

    JC_CHECK_STR(jc_workflow_expand("Review $ITEM now", "a.c", a),
                 "Review a.c now");
    /* Multiple occurrences. */
    JC_CHECK_STR(jc_workflow_expand("$ITEM vs $ITEM", "x", a), "x vs x");
    /* No item => unchanged. */
    JC_CHECK_STR(jc_workflow_expand("no marker", NULL, a), "no marker");
    /* No marker => unchanged. */
    JC_CHECK_STR(jc_workflow_expand("plain", "x", a), "plain");

    jc_arena_free(a);
}

void test_workflow(void)
{
    test_parse();
    test_expand();
}
