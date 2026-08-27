/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tt_mult.c - the shared timeout multiplier for the test-only helper tools.
 *
 * Every deadline in this tier must scale by the same knob, or the knob is a
 * lie. M220 added JC_SMOKE_TIMEOUT_MULT for slow silicon and wired it to
 * run.sh's outer per-driver limit only; M272 found ptydrive's inner
 * expect/waitexit deadlines never scaled; M273 found the third layer the
 * hard way -- mockmodel's own self-watchdog killed the server mid-run on a
 * slow guest (the driver then hung on a reply that could never come, and no
 * expect budget could help, because the layer that died was not the one
 * being raised). So the multiplier lives HERE, once, and every tool with a
 * deadline calls it (enforced by tests/smoke/smoke_lint.sh).
 *
 * tt_mult_parse is pure and unit-tested (tests/test_ttools.c).
 */

#include "tt.h"

#include <stdlib.h>

long tt_mult_parse(const char *s)
{
    long v;
    char *end;

    if (s == NULL || s[0] == '\0') {
        return 1;
    }
    v = strtol(s, &end, 10);
    if (end == s) {
        return 1;               /* not a number at all */
    }
    if (v < 1) {
        return 1;               /* 0 / negative: never shorten a deadline */
    }
    return v;
}

long tt_timeout_mult(void)
{
    const char *e = getenv("JC_SMOKE_TIMEOUT_MULT");

    if (e == NULL || e[0] == '\0') {
        e = getenv("JC_E2E_TIMEOUT_MULT");
    }
    return tt_mult_parse(e);
}
