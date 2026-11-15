/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_tt_wait.c - the teeth for tt_wait_for (M1164, change 2): a deadline is a
 * finding's OWN outcome, not a false FAIL. Three cells, each a JC_CHECK against
 * the helper's observable contract (tests/tools/tt_wait.h):
 *
 *   (a) a predicate that never becomes true -> returns 0, exactly ONE new
 *       failure (the helper's double-expiry jc_test_fails++), bounded wall time
 *       (it NEVER hangs), and a TIMEOUT line naming the call site;
 *   (b) a predicate that is false through the first window and true only at the
 *       2x retry -> returns 1, a NOTE line, NO failure (a one-off slow start is
 *       not a finding);
 *   (c) a predicate true at once -> returns 1, no line, no failure.
 *
 * The cells are red before tt_wait.c exists: main() references test_tt_wait,
 * which references tt_wait_for, so `make run_tests` fails at link with
 * `undefined reference to tt_wait_for`. Green once the helper lands. */

#include "jc_test.h"
#include "tools/tt_wait.h"

/* Cell (a): always false. */
static int pred_never(void *ctx)
{
    (void)ctx;
    return 0;
}

/* Cell (b): false for the first five calls, true from the sixth. The helper
 * bounds each window by ITERATION count (base_ms 200 / 50 ms cadence = exactly
 * 4 predicate calls per window, deterministic and independent of how long the
 * sleeps actually take and of mult). So the first window makes calls 1-4
 * (all false), the retry window makes call 5 (false) then call 6 (true) -- a
 * NOTE, no failure. Timing-fragile in neither direction. */
static int pred_eventually(void *ctx)
{
    int *c = (int *)ctx;

    (*c)++;
    return (*c >= 6) ? 1 : 0;
}

/* Cell (c): true at once. */
static int pred_true_now(void *ctx)
{
    (void)ctx;
    return 1;
}

void test_tt_wait(void);   /* self-prototype: the file is not in jc_test.h yet */

void test_tt_wait(void)
{
    int c;
    int fails0;
    int after_helper;

    /* (a) never true: returns 0, and the double expiry adds EXACTLY one
     * failure (the helper's own count -- the suite's summary reflects it). We
     * OBSERVE that growth and then roll back ONLY the helper's +1, so the cell
     * is a net-zero contributor to the final `K failures` (the timeout it
     * proves is a genuine red only when it happens to a real, reproducing
     * hang). The helper printed exactly one TIMEOUT line to stdout for this
     * cell.
     *
     * F2-1 (qwen27b review of wave-2): the old shape ended in an UNCONDITIONAL
     * `jc_test_fails = fails0;`, which erased not just the synthetic timeout
     * but ALSO this cell's own JC_CHECK failure -- if the helper ever added 0
     * or 2, the delta check fired (+1), the very next line reset the counter,
     * and `make test` exited 0. A test that never fails. The `after_helper`
     * marker closes the hole: the rollback runs ONLY when the check passed
     * (the counter is still exactly after_helper), so a wrong increment STAYS
     * red. */
    c = 0;
    fails0 = jc_test_fails;
    JC_CHECK(tt_wait_for(200, pred_never, &c, "tt_wait_never",
                         __FILE__, __LINE__) == 0);
    after_helper = jc_test_fails;        /* the helper's side effect landed */
    JC_CHECK(after_helper - fails0 == 1); /* exactly one new failure */
    if (jc_test_fails == after_helper) {  /* check passed: roll back the helper's
                                           * +1 (net-zero); a fired check moved the
                                           * counter past after_helper and is left
                                           * red */
        jc_test_fails = fails0;
    }

    /* (b) true only at the 2x retry: returns 1, NOTE line, NO failure. */
    c = 0;
    fails0 = jc_test_fails;
    JC_CHECK(tt_wait_for(200, pred_eventually, &c, "tt_wait_retry",
                         __FILE__, __LINE__) == 1);
    JC_CHECK(jc_test_fails == fails0);

    /* (c) true at once: returns 1, no line, no failure. */
    c = 0;
    fails0 = jc_test_fails;
    JC_CHECK(tt_wait_for(200, pred_true_now, &c, "tt_wait_now",
                         __FILE__, __LINE__) == 1);
    JC_CHECK(jc_test_fails == fails0);
}
