/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_improve.h - the self-improvement loop's measurement core (M109).
 *
 * The synthesis loop needs one honest metric: "am I getting better?" -- the
 * pass-rate over a suite of machine-checkable assignment specs, tracked across
 * runs. This header is the pure part: read the pass-rate history and describe
 * the trend. The `improve` subcommand (main.c) grades the suite, appends a
 * history line, and writes a propose-only report.
 */
#ifndef JC_IMPROVE_H
#define JC_IMPROVE_H


#ifdef __cplusplus
extern "C" {
#endif
/* The most recent pass-rate percentage recorded in a history JSONL (each line
 * a {"pct":N,...} object), or -1 if none is found. Pure. */
int jc_improve_last_pct(const char *history_jsonl);

/* A one-word trend describing `cur` vs `prev` (a prev of -1 means there is no
 * prior run). Returns a static string: "baseline"|"improved"|"regressed"|
 * "unchanged". Never NULL. Pure. */
const char *jc_improve_trend_word(int prev, int cur);

#ifdef __cplusplus
}
#endif
#endif /* JC_IMPROVE_H */
