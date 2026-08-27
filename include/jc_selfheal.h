/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_selfheal.h - runtime self-healing guards for the agent loop (M105).
 *
 * The zigodot dogfooding (docs/ANECDOTES.md #8/#9) showed a real failure mode:
 * the agent re-edits the same file over and over, burning the whole token budget
 * on a cacheless backend without converging. The edit-watch is a tiny, heap-free
 * per-turn counter: when the same path is edited past a threshold, the loop
 * appends a one-time nudge to the tool result telling the model to step back.
 *
 * Fixed-size + stack-allocated on purpose: run_agent_loop has many return points,
 * so a heap structure would need freeing on every one. Overflow (a turn touching
 * more than JC_SELFHEAL_MAX_PATHS distinct files) simply stops tracking new paths.
 */
#ifndef JC_SELFHEAL_H
#define JC_SELFHEAL_H


#ifdef __cplusplus
extern "C" {
#endif
#define JC_SELFHEAL_MAX_PATHS      24
#define JC_SELFHEAL_PATH_MAX       256
#define JC_SELFHEAL_REDO_THRESHOLD 3   /* nudge on the 3rd edit of one file */

struct jc_editwatch {
    char paths[JC_SELFHEAL_MAX_PATHS][JC_SELFHEAL_PATH_MAX];
    int  counts[JC_SELFHEAL_MAX_PATHS];
    int  n;
};

/* Zero the watch (no allocation; safe to leave un-freed). */
void jc_editwatch_init(struct jc_editwatch *w);

/* Record an edit to `path`; return its new cumulative count (>= 1). Returns 0
 * when `path` is NULL/empty, or when the table is full and `path` is new. */
int jc_editwatch_bump(struct jc_editwatch *w, const char *path);

#ifdef __cplusplus
}
#endif
#endif /* JC_SELFHEAL_H */
