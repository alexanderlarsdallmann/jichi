/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sysmsg_fit.c - system-prompt context fitting (M73).
 *
 * jc_sysmsg_fit_caps bounds the growable sections so the system prompt can't
 * overflow a model whose context is smaller than rules + repo map + tools.
 *
 * M462 adds the design doc as a THIRD competitor, with a sacrifice order that
 * is an argument, not an accident: repo map first (regenerable via
 * list_files/search_code), then rules (conventions, restated in review), and
 * the design LAST -- truncating the plan for the work in flight makes the
 * agent follow half a plan while believing it has all of it. */

#include "jc_test.h"
#include "jc_sysmsg.h"

void test_sysmsg_fit(void)
{
    jc_size rc, mc, dc;

    /* Unknown budget (limit <= 0): never shrink. */
    jc_sysmsg_fit_caps(0, 32768, 12512, 0, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0);
    jc_sysmsg_fit_caps(-1, 99999, 99999, 0, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0);

    /* Already fits: a big window (32000 tok -> budget 57600 bytes) easily holds
     * rules (32768) + map (12512), so no caps. */
    jc_sysmsg_fit_caps(32000, 32768, 12512, 0, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0);

    /* Small window (8000 tok): budget = 8000*4*45/100 = 14400 bytes. The real
     * jichi-repo sizes overflow it, so both are capped and the caps sum to the
     * budget; the repo map is held to a third (4800), rules keep the rest. */
    jc_sysmsg_fit_caps(8000, 32768, 12512, 0, &rc, &mc, &dc);
    JC_CHECK(mc == 4800);
    JC_CHECK(rc == 9600);
    JC_CHECK(rc + mc == 14400);

    /* A small repo map (under a third of the budget) is emitted in full; only
     * rules are truncated, and rules get the whole remainder. */
    jc_sysmsg_fit_caps(8000, 14000, 500, 0, &rc, &mc, &dc);
    JC_CHECK(mc == 500);          /* == map_len: append_capped won't cut it */
    JC_CHECK(rc == 13900);        /* 14400 - 500 */

    /* Exactly fitting is not shrunk (boundary): rules+map == budget. */
    jc_sysmsg_fit_caps(8000, 14000, 400, 0, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0); /* 14400 sum == 14400 budget */

    /* Tiny budget must never leave a cap at 0 for a NON-empty section: 0 reads
     * as "no limit" downstream, which would let the section escape truncation
     * in exactly the starved case the fit exists for. limit=1 => budget=1 =>
     * the naive map cap (budget/3) is 0. */
    jc_sysmsg_fit_caps(1, 5000, 5000, 0, &rc, &mc, &dc);
    JC_CHECK(mc >= 1);
    JC_CHECK(rc >= 1);
    /* An empty section still gets cap 0 (nothing to truncate; harmless). */
    jc_sysmsg_fit_caps(1, 5000, 0, 0, &rc, &mc, &dc);
    JC_CHECK(mc == 0);   /* no repo map */
    JC_CHECK(rc >= 1);

    /* Huge limit is clamped so the byte math can't overflow jc_size on a
     * 32-bit target; with a huge budget the sections always fit (caps 0). */
    jc_sysmsg_fit_caps(2000000000L, 5000, 5000, 0, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0);

    /* --- M462: the design as a third competitor -------------------------- */

    /* All three fit: nothing is capped. 8000 tok => budget 14400. */
    jc_sysmsg_fit_caps(8000, 6000, 4000, 4000, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0 && dc == 0);

    /* Over budget, but the map alone can absorb it: the DESIGN IS SPARED IN
     * FULL and rules keep the remainder. This is the ordering claim -- with a
     * naive equal split the design would already be truncated here. */
    jc_sysmsg_fit_caps(8000, 20000, 12000, 3000, &rc, &mc, &dc);
    JC_CHECK(dc == 3000);        /* == design_len: append_capped won't cut it */
    JC_CHECK(mc == 4800);        /* the map still takes its third */
    JC_CHECK(rc == 6600);        /* 14400 - 4800 - 3000 */

    /* The design gives ground ONLY once rules are squeezed to nothing, and it
     * still keeps the largest share of what is left. */
    jc_sysmsg_fit_caps(8000, 2000, 12000, 60000, &rc, &mc, &dc);
    JC_CHECK(mc == 4800);
    JC_CHECK(rc >= 1);           /* non-empty section never left at 0 */
    JC_CHECK(dc == 9600);        /* 14400 - 4800 */
    JC_CHECK(dc > mc);           /* the design outranks the map it displaced */

    /* An unknown budget still never shrinks -- including the design. This is
     * the case that makes the load-time JC_DESIGN_MAX ceiling load-bearing:
     * a purely dynamic cap would leave an unbounded doc uncapped here. */
    jc_sysmsg_fit_caps(0, 99999, 99999, 999999, &rc, &mc, &dc);
    JC_CHECK(rc == 0 && mc == 0 && dc == 0);

    /* A non-empty design never gets cap 0 at a starved budget (0 reads as
     * "no limit" downstream). */
    jc_sysmsg_fit_caps(1, 5000, 5000, 5000, &rc, &mc, &dc);
    JC_CHECK(dc >= 1 && mc >= 1 && rc >= 1);
    /* ...but an ABSENT design still gets 0: nothing to truncate. */
    jc_sysmsg_fit_caps(1, 5000, 5000, 0, &rc, &mc, &dc);
    JC_CHECK(dc == 0);
}
