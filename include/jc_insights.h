/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_insights.h - mine recurring problems from observability data (M70).
 *
 * The deterministic, model-free half of the learning loop: it turns the
 * telemetry summary (jc_telemetry) and a session's edit sequence into a ranked
 * list of recurring problems a mentor can then turn into durable lessons. Pure
 * (no I/O); the `learn analyze` subcommand does the file reading and feeds these.
 * See docs/LEARNING.md.
 */
#ifndef JC_INSIGHTS_H
#define JC_INSIGHTS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_telemetry_summary; /* jc_telemetry.h */
struct jc_sb;                /* jc_str.h */

enum jc_insight_kind {
    JC_INSIGHT_TOOL_FAIL = 0, /* a tool errors often (low ok-rate)         */
    JC_INSIGHT_MODEL_TIMEOUT, /* a model stalls/times out repeatedly       */
    JC_INSIGHT_RETRY,         /* model retries pile up (transient failures) */
    JC_INSIGHT_ROUTE,         /* frequent routing escalations              */
    JC_INSIGHT_COMPACT,       /* heavy history compaction (context pressure)*/
    JC_INSIGHT_REDO_LOOP,     /* the same file edited again and again       */
    JC_INSIGHT_STALE_NOTE,    /* remembered notes to review for staleness   */
    JC_INSIGHT_VERIFY_FAIL,   /* autonomous runs fail the verify gate (M134)*/
    JC_INSIGHT_BUDGET_REVERT, /* runs hit budget and roll back, losing work */
    JC_INSIGHT_ERROR,         /* model calls fail outright (not just retry) */
    JC_INSIGHT_TEST_EDIT      /* a test assertion was modified during an
                               * autonomous run (M88 fired) -- the gate may
                               * have been moved to force green (M417)       */
};

/* One mined problem. `subject` is the tool/model/path it concerns; `count` is
 * the salient frequency; `detail` is a ready-to-read phrasing.
 *
 * `source`/`origin` are PROVENANCE (M474), and they exist because the output was
 * misleading without them. `learn analyze <path>` mines three places -- the
 * telemetry file you named, the global session store, and the workspace's
 * memory.md -- and printed all of it as one flat list. Measured: of the two
 * findings it produced for one run, the second came from a DIFFERENT CHECKOUT of
 * the same project (`development/adventure/chrtext`: 0 occurrences in the file
 * named on the command line, 25 in the session store). A reader has no way to
 * tell, and would reasonably go looking for a file that is not in their tree.
 * Documented behaviour -- `--help` does say "telemetry + recent sessions" -- but a
 * miner whose whole job is to say what went wrong must not make you check whether
 * the finding is yours. See docs/analysis/2026-08-18-dogfooding-on-chrtext.md §3.
 *
 * `source` is a short tag ("telemetry", "session", "memory"); `origin` is the
 * specific place within it (a session's workspace path), empty when the source
 * alone says enough. Both empty means unstamped, which renders as before. */
struct jc_insight {
    int  kind;
    char subject[96];
    long count;
    char detail[200];
    char source[16];
    char origin[160];
};

/* Default recency window (seconds) for jc_insights_from_telemetry_ex: a tool
 * whose last call is older than this (relative to the log's newest event) has
 * gone quiet and is not treated as a current problem. Three days spans a normal
 * gap between work sessions while still aging out weeks-old failures. */
#define JC_INSIGHTS_RECENT_SEC (3.0 * 24.0 * 3600.0)

/* Append telemetry-derived findings to `out` (an initialised jc_vec of struct
 * jc_insight), highest-signal first: tools below the ok-rate floor (with at
 * least a minimum number of calls), models with repeated timeouts, and
 * retry/route/compaction pressure over their thresholds. Pure.
 *
 * Equivalent to jc_insights_from_telemetry_ex with recent_window_sec == 0 (no
 * quiet-tool aging; the recovered-tool suppression below still applies whenever
 * the summary carries recency data). */
void jc_insights_from_telemetry(const struct jc_telemetry_summary *s,
                                struct jc_vec *out);

/* Recency-aware variant. A cumulative telemetry log spans many sessions, so a
 * tool that failed a lot historically but has since been fixed still shows a low
 * cumulative ok-rate; ranking on that alone re-surfaces already-solved problems
 * (ANECDOTES #15). This variant, given per-tool recency (last_ts/last_ok_ts/
 * last_fail_ts) and the log's max_ts, skips a would-be TOOL_FAIL finding when:
 *   - RECOVERED: the tool's most recent call SUCCEEDED (last_ok_ts >=
 *     last_fail_ts, with last_fail_ts > 0) -- it works now; or
 *   - QUIET: recent_window_sec > 0 and the tool's last call is older than
 *     (max_ts - recent_window_sec) -- it stopped being exercised.
 * When the summary has no recency data (last_fail_ts == 0, e.g. a pre-recency
 * log or a hand-built summary), neither gate fires, so behavior is unchanged.
 * Pure. */
void jc_insights_from_telemetry_ex(const struct jc_telemetry_summary *s,
                                   struct jc_vec *out,
                                   double recent_window_sec);

/* Append redo-loop findings: any path that appears at least JC_INSIGHT_REDO_MIN
 * times in `paths` (the in-order sequence of edited/written file paths from one
 * session). Pure. */
void jc_insights_redo_loops(const char *const *paths, int n, struct jc_vec *out);

/* Append a staleness-review advisory (M78) when `memory` (the .jichi/memory.md
 * bullet list) holds any notes: one finding reminding the mentor to check the
 * notes against the current code and supersede now-false ones via a
 * "## Corrections" section. Notes citing a specific line/range (most prone to
 * drift) are counted separately. No-op for NULL/empty memory. Pure. */
void jc_insights_stale_review(const char *memory, struct jc_vec *out);

/* M600: the same review, with two more things a mechanical reader CAN check
 * about a prose note. `exists` (may be NULL) answers whether a workspace path
 * still resolves; every note naming a path-shaped token (a '/' and a file
 * extension, e.g. `src/x.c`, `tests/smoke/y.sh`) that `exists` rejects is
 * counted and the first three are named -- the drift `jc_insights_stale_review`
 * could only warn about in general. Notes carrying a "[pins: …]" trailer are
 * counted as PINNED (a test, lint or constraint holds them); the rest are the
 * unpinned share the report states, so a claim with no test to cite is visibly
 * that (the doc_claims_lint convention, applied to lessons). Pure apart from
 * the caller-supplied `exists`. */
typedef int (*jc_insights_exists_fn)(const char *path, void *ctx);
void jc_insights_stale_review_ex(const char *memory,
                                 jc_insights_exists_fn exists, void *ctx,
                                 struct jc_vec *out);

/* Render `findings` (a jc_vec of struct jc_insight) as a human-readable report
 * into `out`. Pure. Emits an "all clear" note when empty. */
/* Stamp `source` (and optional `origin`) onto every finding added to `out` at or
 * after index `from` (M474). Called by the miner around each source so a finding
 * carries where it came from; findings already stamped are left alone, so an inner
 * scan that knows more (a session's workspace) can label its own before an outer
 * sweep applies the general tag. `from` beyond the vector's length is a no-op. */
void jc_insights_stamp(struct jc_vec *out, jc_size from, const char *source,
                       const char *origin);

void jc_insights_render(const struct jc_vec *findings, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_INSIGHTS_H */
