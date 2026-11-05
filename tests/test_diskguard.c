/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_diskguard.c - the disk-space fence (M813). The two invariants -- UNKNOWN
 * never stops, and the throttle is by time not call count -- live in the pure
 * `_below`/`_due` and are checked here without a filesystem or a clock; `_check`
 * is exercised against the real (non-full) filesystem the tests run on. */

#include "jc_test.h"
#include "jc_diskguard.h"
#include <string.h>

void test_diskguard(void)
{
    struct jc_diskguard g;

    /* --- _below: the pure floor decision -------------------------------------- */
    JC_CHECK(jc_diskguard_below(50.0, 1e9, 100.0, 0.0) == 1);   /* below MiB floor */
    JC_CHECK(jc_diskguard_below(100.0, 1e9, 100.0, 0.0) == 0);  /* at the floor    */
    JC_CHECK(jc_diskguard_below(200.0, 1e9, 100.0, 0.0) == 0);  /* above           */
    /* UNKNOWN (a negative reading) NEVER trips -- the core invariant. */
    JC_CHECK(jc_diskguard_below(-1.0, -1.0, 100.0, 1000.0) == 0);
    JC_CHECK(jc_diskguard_below(50.0, -1.0, 100.0, 10.0) == 1); /* mb known+low    */
    JC_CHECK(jc_diskguard_below(-1.0, 5.0, 100.0, 10.0) == 1);  /* files known+low */
    /* a floor <= 0 disables that dimension. */
    JC_CHECK(jc_diskguard_below(0.0, 1e9, 0.0, 0.0) == 0);
    /* the inode floor stands on its own. */
    JC_CHECK(jc_diskguard_below(1e9, 5.0, 0.0, 10.0) == 1);
    JC_CHECK(jc_diskguard_below(1e9, 50.0, 0.0, 10.0) == 0);

    /* --- _due: the time throttle ---------------------------------------------- */
    JC_CHECK(jc_diskguard_due(-1.0, 0.0, 0) == 1);       /* never measured */
    JC_CHECK(jc_diskguard_due(1000.0, 1000.0, 1) == 1);  /* force bypasses it */
    JC_CHECK(jc_diskguard_due(1000.0, 1500.0, 0) == 0);  /* 500 ms < 10 s */
    JC_CHECK(jc_diskguard_due(1000.0, 1000.0 + JC_DISKGUARD_INTERVAL_MS, 0) == 1);

    /* --- _check: stateful, against the real filesystem ------------------------ */
    /* both floors 0 => disabled, never stops. */
    jc_diskguard_init(&g, 0.0, 0.0);
    JC_CHECK(jc_diskguard_check(&g, ".", 1, 0.0) == 0);

    /* an impossibly high floor (1 EiB) trips on any real filesystem. */
    jc_diskguard_init(&g, 1e15, 0.0);
    JC_CHECK(jc_diskguard_check(&g, ".", 1, 0.0) == 1);
    JC_CHECK(g.tripped == 1);
    JC_CHECK(strstr(g.why, "disk floor") != NULL);
    /* latched: keeps returning 1 even after the floor is lowered to disabled. */
    g.floor_mb = 0.0;
    JC_CHECK(jc_diskguard_check(&g, ".", 1, 0.0) == 1);

    /* an UNKNOWN path never stops, even with an impossible floor. */
    jc_diskguard_init(&g, 1e15, 0.0);
    JC_CHECK(jc_diskguard_check(&g, "/nonexistent/deep/xyz", 1, 0.0) == 0);
    JC_CHECK(g.tripped == 0);

    /* a modest 1 MiB floor does not trip on a filesystem with room. */
    jc_diskguard_init(&g, 1.0, 0.0);
    JC_CHECK(jc_diskguard_check(&g, ".", 1, 0.0) == 0);

    /* the throttle: after a measure, a non-forced check inside the window is
     * skipped -- it does not re-measure and cannot trip, even if the floor became
     * impossible; once the window passes, it measures and trips. */
    jc_diskguard_init(&g, 1.0, 0.0);
    JC_CHECK(jc_diskguard_check(&g, ".", 1, 1000.0) == 0);   /* measured: room */
    g.floor_mb = 1e15;                                        /* now impossible */
    JC_CHECK(jc_diskguard_check(&g, ".", 0, 1500.0) == 0);   /* throttled: skip */
    JC_CHECK(jc_diskguard_check(&g, ".", 0,
                                1000.0 + JC_DISKGUARD_INTERVAL_MS) == 1); /* due */
}
