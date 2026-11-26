/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sup_stop.h - def-377 W4 (M1194, A4b): the SOFT-STOP SENTINEL recognizer.
 *
 * The parent's `supervise` action (wrapup/stop) hands a flagged pool child its
 * soft stop by writing ONE sentinel line through the child's inject pipe; the
 * child folds it in as an "[operator] " user message at a model-call boundary
 * and arms the stop there (jc_agent.c). This file is the recognition itself,
 * extracted so it is UNIT-TESTABLE (tests/test_sup_stop.c) rather than buried
 * inline in the agent loop.
 *
 * THE RULE IS EXACT-LINE. The A4 wave's recognizer (jc_agent.c loop-top history
 * scan) matched a SUBSTRING (strstr) of `JICHI-CONTROL:supervise-stop:`, which
 * meant (a) a task prompt containing the control word mid-prose self-armed a
 * child that never received one, and (b) a human inject that merely mentioned
 * the word armed it too. That was review finding R:1. The fix recognizes the
 * sentinel ONLY at the fold, on the just-folded [operator] message (the
 * implementation, jc_sup_stop_line, lives in src/chat/jc_sup_stop.c next to
 * its only caller, so it is unit-testable without a Makefile edit), and ONLY
 * when the line is exactly the control word with its seq -- nothing else on the
 * line. A control that is a substring is a control that can be tripped by the
 * text the run is supervising.
 *
 * The seq is part of the exact line (1 for wrapup, 2 for stop): it is NOT
 * matched against a per-child expectation (the child cannot distinguish the
 * two -- both end the run the same way), but it does make the exact line
 * self-describing and keeps the sentinel from colliding with a human who
 * typed the bare prefix. See the emit-site comment in jc_tool_parallel.c for
 * the true safety case.
 */
#ifndef JC_SUP_STOP_H
#define JC_SUP_STOP_H

#ifdef __cplusplus
extern "C" {
#endif

/* The sentinel prefix the parent writes, seq excluded. The exact line is
 * `JICHI-CONTROL:supervise-stop:` + one digit (1 or 2). */
#define JC_SUP_STOP_PREFIX "JICHI-CONTROL:supervise-stop:"

/* 1 iff `line` is EXACTLY a soft-stop sentinel line: it begins with
 * JC_SUP_STOP_PREFIX, is followed by one digit (1 or 2), and has nothing after
 * it. No substring match, no other words on the line. NULL / empty /
 * mid-prose mentions all return 0. C89: no <string.h> substring scan -- the
 * check is a prefix + trailing-character comparison by hand. */
int jc_sup_stop_line(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* JC_SUP_STOP_H */
