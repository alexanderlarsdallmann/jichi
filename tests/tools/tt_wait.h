/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tt_wait.h - a deadline as a finding's OWN outcome, not a false FAIL (M1164).
 *
 * The smoke tier already does this (run.sh's with_deadline, JC_SMOKE_TIMEOUT_
 * MULT): a deadline rather than a finding. The unit suite had none of it -- a
 * FIXED 40 x 100 ms poll window measured the machine's process start-up and
 * read like a product defect (SV:1750). tt_wait_for lets the caller own the
 * CONDITION and the helper own the CLOCK and the VERDICT, so a slow machine
 * makes a test LONGER, never DIFFERENT, and a genuine hang still prints
 * TIMEOUT (never a silent hang, never a false FAIL on a slow box).
 */
#ifndef TT_WAIT_H
#define TT_WAIT_H

/* The caller supplies the predicate: non-zero means the condition is met.
 * `ctx` is passed through untouched (the test defines a NAMED static
 * predicate and passes its address -- C89 has no anonymous `void` parameter). */
typedef int (*tt_wait_pred)(void *ctx);

/* tt_mult_parse lives in tt_mult.c and has no header of its own today (its
 * declaration is shared from tt.h, the tool-side header, which tt_wait.c does
 * not need). It is re-declared here -- identical prototype, so there is no
 * conflict if a file sees both -- with a comment, as the M1164 note requires:
 * the helper scales its window by the tier's existing knob. tt_mult.o is
 * already linked into run_tests via TT_CORE_OBJ, so this resolves at link. */
long tt_mult_parse(const char *s);

/* Wait for pred(ctx) to become true. The window is base_ms * mult, where
 * mult = tt_mult_parse(getenv("JC_SMOKE_TIMEOUT_MULT")) (the existing rule:
 * unset/empty/0/negative all mean 1, so a deadline is NEVER shortened). On
 * expiry it re-runs the SAME predicate once at 2x the deadline. Outcomes:
 *   - true within the window:      return 1, print nothing;
 *   - false at window, true at 2x: return 1, print a NOTE line
 *                                  "NOTE <what> took <2x ms> (mult <m>, retry at 2x)"
 *                                  (a one-off slow start, not a finding);
 *   - false at both:                return 0, print exactly one
 *                                  "TIMEOUT <file>:<line>: <what> not reached
 *                                  within <2x ms> (mult <m>, retried at 2x)"
 *                                  and count the failure (jc_test_fails++) so a
 *                                  reproducing hang or 10x slowdown stays red.
 * It NEVER hangs: both windows are bounded. <what>/<file>/<line> are supplied by
 * the call site so the TIMEOUT line names the test. */
int tt_wait_for(long base_ms, tt_wait_pred pred, void *ctx,
                const char *what, const char *file, int line);

#endif /* TT_WAIT_H */
