/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tt_wait.c - a deadline as a finding's OWN outcome, not a false FAIL (M1164).
 *
 * The unit suite's old poll blocks measured a DURATION: a fixed 40 x 100 ms
 * window, and on slow silicon (the Athlon II X3, uutils coreutils -- `yes|head`
 * takes 4.85-6.43 s vs 0.88 s on a workstation) the condition simply had not
 * arrived by the window's end, so the check read like a product defect
 * (test_bg.c:318 cap_exceeded_while_running, SV:1750). The instrument, not the
 * product, was at fault.
 *
 * tt_wait_for inverts that: the CALLER owns the condition (a predicate), the
 * HELPER owns the clock and the verdict. A slow machine makes the wait LONGER,
 * never DIFFERENT, because it polls until the condition is true under a SCALED
 * deadline -- and a deadline is NEVER shortened (mult floors at 1). A genuine
 * hang still prints TIMEOUT (and counts one failure), so it stays red, never
 * silently passes and never hangs the suite forever: both windows are bounded.
 *
 * Linked into run_tests via TT_CORE_OBJ (tt_mult.o, which provides
 * tt_mult_parse, is already there). jc_test_fails is the suite's failure
 * counter (tests/jc_test.h); the helper increments it on the double expiry --
 * the ONLY place it counts -- so the summary's `K failures` reflects the
 * timeout, while the NOTE case adds neither a check nor a failure. */

#include "tt_wait.h"

#include <stdio.h>
#include <stdlib.h>

#include "jc_platform.h"  /* jc_sleep_ms (the suite's own bounded sleep) */
#include "../jc_test.h"   /* extern int jc_test_fails (the suite counter) */

/* The poll cadence inside each window: 50 ms between predicate calls. It is
 * SCALED by mult so a slow box gets a coarser-but-still-bounded cadence; the
 * window itself is iteration-bounded (base_ms / 50), not clock-bounded, so the
 * helper NEVER hangs and the NOTE/TIMEOUT thresholds the teeth rely on are
 * deterministic regardless of how long the sleeps actually take. */
#define TT_WAIT_POLL_MS 50L

int tt_wait_for(long base_ms, tt_wait_pred pred, void *ctx,
                const char *what, const char *file, int line)
{
    const char *e;
    long mult;
    long win;
    long retry_win;
    long i;
    long j;

    /* mult = tt_mult_parse(getenv("JC_SMOKE_TIMEOUT_MULT")): unset/empty/0/
     * negative all mean 1, so a deadline is NEVER shortened. */
    e = getenv("JC_SMOKE_TIMEOUT_MULT");
    mult = tt_mult_parse(e);

    win = base_ms * mult;
    retry_win = win * 2;
    if (win <= 0) {
        win = 1; /* a non-positive base is a caller bug; still bounded, never 0 */
    }
    if (retry_win <= 0) {
        retry_win = win;
    }

    /* First window: poll until the condition is met or the window's
     * iteration count is spent. */
    for (i = 0; i < win / TT_WAIT_POLL_MS; i++) {
        if (pred(ctx) != 0) {
            return 1;   /* true within the window: print nothing */
        }
        jc_sleep_ms(TT_WAIT_POLL_MS * mult, NULL);
    }

    /* Expired: re-run the SAME predicate once at 2x the deadline. */
    for (j = 0; j < retry_win / TT_WAIT_POLL_MS; j++) {
        if (pred(ctx) != 0) {
            /* false at the window, true at the retry: a one-off slow start,
             * not a finding. No check, no failure. */
            printf("NOTE %s took %ld ms (mult %ld, retry at 2x)\n",
                   what, retry_win, mult);
            return 1;
        }
        jc_sleep_ms(TT_WAIT_POLL_MS * mult, NULL);
    }

    /* False at BOTH windows: a reproducing hang or a 10x slowdown. This is
     * the only place the helper counts a failure, so the suite's summary
     * reflects the timeout as a real red. Exactly one TIMEOUT line. */
    printf("TIMEOUT %s:%d: %s not reached within %ld ms (mult %ld, retried at 2x)\n",
           file, line, what, retry_win, mult);
    jc_test_fails++;
    return 0;
}
