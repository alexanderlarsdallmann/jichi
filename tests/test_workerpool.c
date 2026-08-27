/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_workerpool.c - the pure deadline predicate behind the daemon worker pool
 * and the spawn_parallel watchdog (W3). The reap helper waits on real children,
 * so it's exercised end-to-end by the daemon, not here. */

#include "jc_test.h"
#include "jc_workerpool.h"

void test_workerpool(void)
{
    /* A disabled watchdog (timeout <= 0) never fires, whatever the elapsed. */
    JC_CHECK(jc_worker_over_deadline(1000000.0, 0.0, 0) == 0);
    JC_CHECK(jc_worker_over_deadline(1000000.0, 0.0, -1) == 0);

    /* Under budget => not over. */
    JC_CHECK(jc_worker_over_deadline(1000.0, 500.0, 1000) == 0); /* 500ms < 1s */
    /* Exactly at the budget is not yet over (strict >). */
    JC_CHECK(jc_worker_over_deadline(1500.0, 500.0, 1000) == 0); /* 1000ms == */
    /* Past the budget => over. */
    JC_CHECK(jc_worker_over_deadline(1501.0, 500.0, 1000) == 1); /* 1001ms > */

    /* Realistic 300s daemon watchdog. */
    JC_CHECK(jc_worker_over_deadline(300001.0, 0.0, 300000) == 1);
    JC_CHECK(jc_worker_over_deadline(299999.0, 0.0, 300000) == 0);
}
