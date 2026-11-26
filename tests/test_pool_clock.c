/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_pool_clock.c - def-379 (M1194-d379): the widened pool watchdog deadline.
 *
 * The pool's monotonic clock is `double jc_now_millis()`. On a 32-bit system a
 * `long` reading of it collapses past ~2^31 ms of uptime (~24.86 days): both
 * stamps saturate to LONG_MIN and the watchdog's `now - launch_ms` reads 0, so a
 * task timeout (a fence, not a cap) stops firing (notes/M1194.md MS3; the live
 * -m32 probe is docs/internal/2026-11-11-design-review/ms32-probe.c).
 *
 * The fix widens the clock VALUES to double and extracts the deadline decision
 * into the pure helper `jc_pool_past_deadline` (src/tools/jc_tool_parallel.c),
 * so a test can SET the stamps -- the gate's 32-bit stage cannot see the wrap by
 * waiting. This cell pins the helper's widened contract with two teeth.
 *
 * The OLD arithmetic is modelled with explicit saturation to -2147483648 (the
 * INT32_MIN collapse), not a plain `long` cast, because a plain `long` on THIS
 * (64-bit, LP64) box does not wrap: the clamp is explicit saturation, not 32-bit
 * wrap/truncation (undefined in C89; C89 6.3.1.4 makes an out-of-range
 * double-to-integer conversion undefined), and the measured MS3 wrap
 * result is what is reproduced. Deterministic regardless of the host's long
 * width. That is the whole point of the cell -- it reproduces the 32-bit
 * collapse without needing a 32-bit host.
 */
#include "jc_test.h"
#include "jc_app.h"

void test_pool_clock(void);   /* M1194-d379: file-local prototype (-Wmissing-prototypes) */

/* The OLD `(long)` stamp, on a 32-bit (ILP32) target: a double millisecond
 * reading narrowed to a signed 32-bit long, saturating to -2147483648 when it
 * overflows -- exactly what the pre-fix `t->last_ms = (long)jc_now_millis()` did
 * past the 2^31 ms wrap (notes/M1194.md MS3). */
static long long32_stamp(double ms)
{
    if (ms >= 2147483648.0) {
        return (-2147483647L - 1L);   /* -2147483648: the long clock saturates */
    }
    if (ms <= -2147483648.0) {
        return (-2147483647L - 1L);
    }
    return (long)ms;
}

static void test_pool_past_deadline_gap(void)
{
    /* Tooth (a): a 2 h gap against a 5 min bound is PAST. The widened helper
     * sees the full 7,200,000 ms difference, far above the 300,000 ms bound. */
    JC_CHECK(jc_pool_past_deadline(1000000.0, 1000000.0 + 7200000.0, 300000L) == 1);
    /* and the same stamps just inside the bound are NOT past (the fence is not a
     * cap; the decision is the comparison, not a side effect). */
    JC_CHECK(jc_pool_past_deadline(0.0, 299999.0, 300000L) == 0);
}

static void test_pool_past_deadline_wrap(void)
{
    /* Tooth (b): the 32-bit wrap boundary. Stamps past 2^31 ms (one hour apart),
     * both too big for a `long` on a 32-bit target.
     *
     * The WIDENED helper (the fix) still says PAST: in double, the difference is
     * the true 3,600,000 ms, above the 300,000 ms bound. */
    double launch = 2147483648.0;          /* 2^31 ms: a long cannot hold it */
    double now = launch + 3600000.0;        /* one hour later, still a long cannot hold it */
    long timeout_ms = 300000L;              /* the bound stays long (D379.2) */
    long old_now, old_launch;               /* the OLD (long) stamps, modelled below */
    JC_CHECK(jc_pool_past_deadline(launch, now, timeout_ms) == 1);

    /* Pin the DIVERGENCE: reproduce the OLD `(long)` arithmetic. On a 32-bit
     * target both stamps saturate to -2147483648, their difference reads 0, and
     * the watchdog never fires -- the def-379 defect. The widened helper says
     * PAST where the old long arithmetic says not; that gap is the fix. */
    old_now = long32_stamp(now);
    old_launch = long32_stamp(launch);
    JC_CHECK(old_now == (-2147483647L - 1L));            /* the long clock collapsed */
    JC_CHECK(old_launch == (-2147483647L - 1L));         /* the long clock collapsed */
    JC_CHECK(((old_now - old_launch) > timeout_ms) == 0); /* diff is 0: NOT past */
    JC_CHECK((-2147483647L - 1L) - (-2147483647L - 1L) == 0);    /* the collapse, named */
}

void test_pool_clock(void)
{
    test_pool_past_deadline_gap();
    test_pool_past_deadline_wrap();
}
