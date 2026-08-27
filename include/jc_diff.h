/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_diff.h - line-level unified-diff rendering.
 *
 * Produces a human-readable unified diff (git-style `@@` hunks with ` `/`-`/`+`
 * line prefixes) of two text buffers, for the TUI's pre-approval edit preview
 * and the `/diff` command. The diff is for *display*, not for `patch` to apply,
 * so hunk headers are approximate where a side is empty.
 *
 * The algorithm is a line LCS with common prefix/suffix trimming: equal leading
 * and trailing lines are stripped first (cheap and exact when an edit is
 * localized), and the LCS dynamic program runs only over the differing middle.
 * A cell-count guard falls back to "delete-all + add-all" for pathologically
 * large middles so the cost stays bounded.
 *
 * Pure (no I/O); unit-tested in tests/test_diff.c.
 */
#ifndef JC_DIFF_H
#define JC_DIFF_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Append a unified diff of `old_text` -> `new_text` to `sb`.
 *   context: unchanged context lines kept around each change (e.g. 3).
 *   color:   emit ANSI color (red deletions, green additions, cyan headers).
 *   max_out_lines: cap on emitted diff lines (<=0 => a sane default); when hit,
 *                  a truncation note is appended and rendering stops.
 * Returns the number of changed lines (additions + deletions); 0 means the two
 * texts are identical and nothing is appended. */
int jc_diff_unified(const char *old_text, const char *new_text,
                    int context, int color, int max_out_lines,
                    struct jc_sb *sb);

#ifdef __cplusplus
}
#endif
#endif /* JC_DIFF_H */
