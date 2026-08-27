/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rewind.h - map workspace checkpoints to conversation turns (M34c).
 *
 * `/rewind` returns to an earlier point in the conversation *and* the matching
 * file state in one step -- the missing half of `/undo` (which reverts files but
 * leaves history inconsistent). A workspace checkpoint is taken before the first
 * file-changing tool of a turn, labelled with that turn's user request; to undo
 * the turn cleanly we must also truncate the history back to the user message
 * that started it.
 *
 * These pure helpers do the snapshot<->turn mapping by matching each checkpoint
 * label to its triggering user message in chronological order. They are network-
 * and I/O-free, so they are unit-tested directly; the restore + truncate + save
 * orchestration lives in the TUI `/rewind` command and the `rewind` subcommand.
 */
#ifndef JC_REWIND_H
#define JC_REWIND_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Does `user_content` (a raw user message) correspond to checkpoint `label`
 * (already whitespace-collapsed/truncated when stored)? True iff, after the same
 * whitespace collapsing, one is a prefix of the other (both non-empty). The
 * prefix test tolerates the two label forms: a freshly-taken checkpoint stores
 * the collapsed full message (≤255 chars); one reloaded from git history stores
 * only the commit subject (the first line). */
int jc_rewind_label_match(const char *user_content, const char *label);

/* Ordered greedy match: for each checkpoint label `labels[c]` (c in
 * [0,nlabels), oldest-first) find the index into `users` (user messages in
 * chronological order) of its triggering message, advancing monotonically so
 * each checkpoint maps to a distinct, increasing user index. Writes nlabels
 * results into `out_user` (-1 where no match). */
void jc_rewind_match(const char *const *users, int nusers,
                     const char *const *labels, int nlabels, int *out_user);

#ifdef __cplusplus
}
#endif
#endif /* JC_REWIND_H */
