/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_confbench.c - the M113 config-benchmark scorer (pure). */

#include "jc_test.h"
#include "jc_confbench.h"
#include "jc_str.h"

#include <string.h>

static void set_all(struct jc_confbench_facts *f, int v)
{
    int *p = (int *)f;
    unsigned i;
    for (i = 0; i < sizeof(*f) / sizeof(int); i++) p[i] = v;
}

void test_confbench(void)
{
    struct jc_confbench_facts f;
    struct jc_confbench_report r;
    struct jc_sb sb;

    /* weights sum to exactly 100 */
    set_all(&f, 1);
    jc_confbench_score(&f, &r);
    JC_CHECK(r.max == 100);
    JC_CHECK(r.got == 100);
    JC_CHECK(r.score == 100);
    JC_CHECK(r.grade == 'A');
    JC_CHECK(r.n > 0 && r.n <= JC_CONFBENCH_MAX_ITEMS);

    /* nothing configured -> 0, grade E */
    set_all(&f, 0);
    jc_confbench_score(&f, &r);
    JC_CHECK(r.got == 0);
    JC_CHECK(r.score == 0);
    JC_CHECK(r.grade == 'E');

    /* a realistic middling config: models + safety, no assets */
    set_all(&f, 0);
    f.has_active_model = 1; f.has_embed = 1; f.snapshots = 1;
    f.verify_set = 1; f.path_fence = 1; f.context_declared = 1;
    jc_confbench_score(&f, &r);
    JC_CHECK(r.score > 0 && r.score < 100);
    /* got should equal the sum of those weights (10+8+8+8+4+3 = 41) */
    JC_CHECK(r.got == 41);

    /* render mentions the score + a missing-item hint */
    jc_sb_init(&sb);
    jc_confbench_render(&r, 0, 0, &sb);
    JC_CHECK(strstr(sb.data, "score:") != NULL);
    JC_CHECK(strstr(sb.data, "grade") != NULL);
    /* a missing item (skills) shows its hint */
    JC_CHECK(strstr(sb.data, "skills") != NULL);
    jc_sb_free(&sb);

    /* NULL facts -> zeroed report, no crash */
    jc_confbench_score(NULL, &r);
    JC_CHECK(r.n == 0 && r.max == 0);
}
