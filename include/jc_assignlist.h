/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assignlist.h - THE assignment listing: collect once, render N times
 * (M529's rule, moved out of main.c at M626 for the same reason gradecore
 * moved at M614 -- the TUI could not link main.c's statics, so /assignments
 * was a second copy of the discovery rules and had silently diverged: it read
 * no hints.jsonl and would have stayed a flat list while the CLI grew stage
 * grouping).
 *
 * The listing is an ORIENTATION (M626): rows carry the spec's `stage:`
 * curriculum group, and the fold below sums points and passes per stage so a
 * learner reads their standing instead of doing INDEX.md arithmetic by hand.
 * Deliberately NOT here: the stage GATES. A gate is more than points (two
 * debugging-record entries, task 09 required, all-four-floors), so a binary
 * printing "gate met" from points alone would lie; the totals make the
 * learner's own comparison against INDEX.md trivial instead (DECISIONS,
 * M626). */
#ifndef JC_ASSIGNLIST_H
#define JC_ASSIGNLIST_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_assign.h"
#include "jc_progress.h"

struct jc_assign_row {
    const char *name;                 /* bare filename, arena-owned  */
    struct jc_assign_spec spec;       /* parsed frontmatter + body   */
    int has_sol;                      /* a .solution.md sibling      */
    struct jc_progress prog;          /* the learner's standing      */
    struct jc_hints hints;            /* hint pulls, deepest rung    */
};

/* One stage's standing. `stage` is NULL for the bucket of rows that carry no
 * stage: key -- renderers label it "(no stage)" and it always groups LAST,
 * after every named stage. */
struct jc_stage_total {
    const char *stage;
    int pts_earned;   /* sum of points over passed rows   */
    int pts_avail;    /* sum of points over all rows      */
    int passed;       /* rows passed                      */
    int total;        /* rows in the stage                */
};

/* Collect the assignment set under <cwd>/docs/assignments into `rows`
 * (elements: struct jc_assign_row), name-sorted, with the learner's standing
 * joined from <cwd>/.jichi/progress.jsonl and hints from .jichi/hints.jsonl.
 * The skip rules (non-.md, *.solution.md, INDEX.md) live here and nowhere
 * else. Strings land on `a`. */
void jc_assignlist_collect(const char *cwd, struct jc_arena *a,
                           struct jc_vec *rows);

/* Fold `rows` into per-stage totals (elements: struct jc_stage_total).
 * Named stages appear in FIRST-SEEN order under the rows' existing name sort
 * -- task numbering already encodes curriculum order, so the binary embeds no
 * curriculum table (rejected: a hardcoded order list). The NULL (stage-less)
 * bucket, if any rows feed it, is appended last. Pure over the rows. */
void jc_assignlist_totals(const struct jc_vec *rows, struct jc_vec *totals);

/* 1 iff any row carries a stage: -- grouping activates only then, so a
 * workspace with stage-less specs renders exactly as before M626. */
int jc_assignlist_has_stage(const struct jc_vec *rows);

/* "-- shu  (2/5 pts, 1/2 passed)" into buf; NULL stage renders "(no stage)".
 * Must never contain the word "hints" (hint_record.sh asserts its absence on
 * a no-pulls listing). */
void jc_assignlist_stage_line(const struct jc_stage_total *t, char *buf,
                              jc_size cap);

/* "total: 2/12 pts, 1/4 passed" over all stages, into buf. */
void jc_assignlist_total_line(const struct jc_vec *totals, char *buf,
                              jc_size cap);

/* Print the text table to stdout: the pinned column header, then per group a
 * stage line (wrapped in dim/reset, e.g. the TUI's C_DIM -- pass "" for
 * plain) and the group's rows via jc_progress_row, then the overall line.
 * `stage_filter` NULL prints everything; otherwise only that stage's group.
 * With no staged row at all the output is the flat pre-M626 table: header +
 * rows, no group lines, no total. Rows keep the appended `hints N (deepest
 * rung M)` suffix when pulls > 0. */
void jc_assignlist_print(const struct jc_vec *rows, const char *stage_filter,
                         const char *dim, const char *reset);

#ifdef __cplusplus
}
#endif
#endif /* JC_ASSIGNLIST_H */
