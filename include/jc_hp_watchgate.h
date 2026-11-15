/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_watchgate.h - the `watch gate` tick (M1153, H9a) and its pure
 * withdrawal engine (behaviour 7, the 13:47 rule).
 *
 * The tick itself (jc_hp_watchgate_run) is wired into the harness dispatch by
 * jc_harness_main.c; it is NOT declared here (its signature names the config
 * struct, and the harness main is the only caller). The three pure functions
 * below ARE the export: the withdrawal engine is unit-tested through them
 * (tests/test_watchgate.c) and linked into run_tests for that reason (the
 * Makefile's $(TEST) target carries jc_hp_watchgate.o).
 */

#ifndef JC_HP_WATCHGATE_H
#define JC_HP_WATCHGATE_H

#include <stddef.h>   /* size_t (jc_gate_withdrawn_n, M1178) */

/* Does the SENTENCE `t` WITHDRAW the READY commit whose first seven hex are
 * `c7`? 1 iff it says WITHDRAWN (case-sensitive), names the READY commit (a
 * commit token whose first seven hex equal c7), and names NO OTHER commit
 * token (7-40 lowercase hex, bounded by non-alphanumerics, with at least one
 * letter a-f). Pure: no I/O. See jc_hp_watchgate.c for the token definition. */
int jc_gate_withdrawn_sentence(const char *t, const char *c7);

/* Does the LINE `t` WITHDRAW `c7`? The line is split into sentences on the
 * reference awk's delimiter [.!?]+[ \t]+ and jc_gate_withdrawn_sentence is
 * applied to each (a withdrawal must name the commit and WITHDRAWN in ONE
 * sentence). Pure: no I/O. */
int jc_gate_withdrawn(const char *t, const char *c7);

/* The same for the first `n` bytes of `t`, which need not be NUL-terminated:
 * the tick passes a line INSIDE the LOG buffer and its length (M1178). Until
 * then it passed only the pointer, so every call read to the END of the LOG:
 * a line saying WITHDRAWN joined the next line's commit token into one
 * sentence (a false withdrawal), and with 64 KiB or more of LOG after a
 * line its withdrawal was never seen. A line of 64 KiB or more is not a
 * withdrawal. Pure: no I/O. */
int jc_gate_withdrawn_n(const char *t, size_t n, const char *c7);

#endif /* JC_HP_WATCHGATE_H */
