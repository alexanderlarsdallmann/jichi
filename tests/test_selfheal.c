/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_selfheal.c - the runtime redo-loop edit-watch (M105). */

#include "jc_test.h"
#include "jc_selfheal.h"

static void test_editwatch(void)
{
    struct jc_editwatch w;
    int i;

    jc_editwatch_init(&w);

    /* Same path increments; independent paths are counted separately. */
    JC_CHECK(jc_editwatch_bump(&w, "a.c") == 1);
    JC_CHECK(jc_editwatch_bump(&w, "b.c") == 1);
    JC_CHECK(jc_editwatch_bump(&w, "a.c") == 2);
    JC_CHECK(jc_editwatch_bump(&w, "a.c") == 3);
    JC_CHECK(jc_editwatch_bump(&w, "b.c") == 2);

    /* The threshold is reached exactly once on the crossing edit. */
    JC_CHECK(3 == JC_SELFHEAL_REDO_THRESHOLD);

    /* NULL / empty paths are ignored. */
    JC_CHECK(jc_editwatch_bump(&w, NULL) == 0);
    JC_CHECK(jc_editwatch_bump(&w, "") == 0);

    /* Table overflow: new paths past the cap return 0, existing still bump. */
    jc_editwatch_init(&w);
    for (i = 0; i < JC_SELFHEAL_MAX_PATHS; i++) {
        char p[16];
        p[0] = 'p'; p[1] = (char)('0' + (i % 10));
        p[2] = (char)('0' + (i / 10)); p[3] = '\0';
        JC_CHECK(jc_editwatch_bump(&w, p) == 1);
    }
    JC_CHECK(jc_editwatch_bump(&w, "overflow-new") == 0);
    JC_CHECK(jc_editwatch_bump(&w, "p00") == 2); /* existing still tracked */
}

void test_selfheal(void)
{
    test_editwatch();
}
