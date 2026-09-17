/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_plan.c - the plan artifact's parser, its missing-section order, and
 * drift (M631). */

#include "jc_test.h"
#include "jc_plan.h"

#include <string.h>
#include <stdlib.h>

static const char *FULL =
    "# Plan\n\n## Claim\nRename stage to track without changing output.\n\n"
    "## Rejected\n- a --track alias beside --stage -- two spellings for one thing\n"
    "- doing nothing -- the name misleads a reader\n\n"
    "## Falsifier\nassignments_stages.sh red, or any output byte differing.\n\n"
    "## Not-goals\nThe JSON field name (a wire value).\n\n"
    "## Touches\n- src/main.c\n- `src/util/jc_assignlist.c`\ntests/smoke/assignments_stages.sh\n";

static void test_parse_full(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_plan p;
    jc_plan_parse(FULL, &p, a);
    JC_CHECK(jc_plan_missing(&p) == NULL);
    JC_CHECK(p.n_rejected == 2);
    JC_CHECK(p.n_rejected_bare == 0);
    JC_CHECK(p.touches.len == 3);
    JC_CHECK_STR(*(char **)jc_vec_at(&p.touches, 1), "src/util/jc_assignlist.c");
    jc_plan_free(&p);
    jc_arena_free(a);
}

static void test_missing_in_order(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_plan p;
    const char *m;

    jc_plan_parse("# Plan\n", &p, a);
    m = jc_plan_missing(&p); JC_CHECK(m != NULL && strstr(m, "Claim") != NULL);
    jc_plan_free(&p);

    jc_plan_parse("## Claim\nx\n## Rejected\n- SQLite\n", &p, a);
    m = jc_plan_missing(&p);
    JC_CHECK(m != NULL && strstr(m, "reason it lost") != NULL); /* bare bullet */
    JC_CHECK(p.n_rejected_bare == 1 && p.n_rejected == 0);
    jc_plan_free(&p);

    jc_plan_parse("## Claim\nx\n## Rejected\n- SQLite -- heavy\n", &p, a);
    m = jc_plan_missing(&p); JC_CHECK(m != NULL && strstr(m, "Falsifier") != NULL);
    jc_plan_free(&p);

    /* an em dash counts as the reason separator too */
    jc_plan_parse("## Claim\nx\n## Rejected\n- SQLite \xE2\x80\x94 heavy\n## Falsifier\ny\n"
                  "## Not-goals\nz\n## Touches\n", &p, a);
    m = jc_plan_missing(&p); JC_CHECK(m != NULL && strstr(m, "Touches") != NULL);
    JC_CHECK(p.n_rejected == 1);
    jc_plan_free(&p);

    /* a heading with an empty body is not a section */
    jc_plan_parse("## Claim\n\n## Rejected\n- a -- b\n## Falsifier\ny\n## Not-goals\nz\n## Touches\nf\n", &p, a);
    m = jc_plan_missing(&p); JC_CHECK(m != NULL && strstr(m, "Claim") != NULL);
    jc_plan_free(&p);
    jc_arena_free(a);
}

static void test_render_roundtrip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_plan p;
    char *md = jc_plan_render("Do X.", "A -- lost; B -- also lost",
                              "test red", "the UI", "src/a.c\nsrc/b.c");
    JC_CHECK(md != NULL);
    jc_plan_parse(md, &p, a);
    JC_CHECK(jc_plan_missing(&p) == NULL);
    JC_CHECK(p.n_rejected == 2);
    JC_CHECK(p.touches.len == 2);
    jc_plan_free(&p);
    free(md);
    jc_arena_free(a);
}

static void test_drift(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_plan p;
    struct jc_vec wrote, out;
    const char *w1 = "src/main.c", *w2 = "./src/util/jc_assignlist.c",
               *w3 = "src/tui/jc_tui.c", *w4 = ".jichi/PLAN.md";
    jc_plan_parse(FULL, &p, a);
    jc_vec_init(&wrote, sizeof(char *)); jc_vec_init(&out, sizeof(char *));
    jc_vec_push(&wrote, &w1); jc_vec_push(&wrote, &w2);
    jc_vec_push(&wrote, &w3); jc_vec_push(&wrote, &w4);
    JC_CHECK(jc_plan_drift(&p, &wrote, &out, a) == 1);   /* only jc_tui.c */
    JC_CHECK(out.len == 1);
    JC_CHECK_STR(*(char **)jc_vec_at(&out, 0), "src/tui/jc_tui.c");
    jc_vec_free(&wrote); jc_vec_free(&out); jc_plan_free(&p);
    jc_arena_free(a);
}

void test_plan(void)
{
    test_parse_full();
    test_missing_in_order();
    test_render_roundtrip();
    test_drift();
}
