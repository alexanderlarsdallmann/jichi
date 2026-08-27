/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tt.h - shared bits for the test-only helper tools (tests/tools).
 *
 * These helpers (mockmodel, ptydrive, jsonq) back the Python-free smoke
 * tier (tests/smoke/, M209). They are built by `make smoke-tools`, never
 * installed, and never linked into the product. Their pure cores
 * (mm_core/pd_core/jq_core) are libc-only and unit-tested in run_tests
 * (tests/test_ttools.c); the *.c mains own all I/O.
 */
#ifndef TT_H
#define TT_H

#define TT_VERSION "0.1"

/* Common process exit codes shared by the helper mains. Each helper adds
 * its own specific codes above these (see ptydrive.c). */
#define TT_EXIT_OK    0
#define TT_EXIT_USAGE 2
#define TT_EXIT_DEADLINE 3

/* The shared timeout multiplier (tt_mult.c, M273). EVERY deadline in this
 * tier scales by it -- run.sh's per-driver limit, ptydrive's expect/waitexit
 * and --deadline, mockmodel's and sockq's self-watchdogs -- because a layer
 * that does not scale fails a healthy run on slow silicon, and does it in a
 * way that looks like a product bug. Pacing (delay/drain) is deliberately
 * NOT scaled: it cannot fail. tt_mult_parse is the pure half: NULL, empty,
 * non-numeric, zero and negative all mean 1 (never shorten a deadline). */
long tt_mult_parse(const char *s);
long tt_timeout_mult(void);

#endif /* TT_H */
