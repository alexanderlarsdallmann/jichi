/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_improve.c - the improve loop's pass-rate history/trend core (M109). */

#include "jc_test.h"
#include "jc_improve.h"
#include <string.h>

static void test_last_pct(void)
{
    /* Empty / none. */
    JC_CHECK(jc_improve_last_pct(NULL) == -1);
    JC_CHECK(jc_improve_last_pct("") == -1);
    JC_CHECK(jc_improve_last_pct("not json\n") == -1);

    /* The most recent line's pct wins. */
    JC_CHECK(jc_improve_last_pct(
        "{\"pct\":40,\"total\":10}\n"
        "{\"pct\":70,\"total\":10}\n") == 70);

    /* A trailing line without a newline still counts. */
    JC_CHECK(jc_improve_last_pct("{\"pct\":55}") == 55);

    /* A malformed final line falls back to the last good one. */
    JC_CHECK(jc_improve_last_pct("{\"pct\":33}\ngarbage\n") == 33);
}

static void test_trend(void)
{
    JC_CHECK_STR(jc_improve_trend_word(-1, 50), "baseline");
    JC_CHECK_STR(jc_improve_trend_word(50, 70), "improved");
    JC_CHECK_STR(jc_improve_trend_word(70, 50), "regressed");
    JC_CHECK_STR(jc_improve_trend_word(60, 60), "unchanged");
}

void test_improve(void)
{
    test_last_pct();
    test_trend();
}
