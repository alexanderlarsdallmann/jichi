/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_patch.h - string edit primitives, shared by edit_file, apply_patch, and
 * the TUI diff preview.
 *
 * These are the pure (no I/O) core of jichi's editing: count occurrences of a
 * search string, build the post-edit text, and resolve+apply a single edit with
 * an exact-first / fuzzy-fallback strategy (M38). Keeping them in one place
 * means the single-edit tool (edit_file), the atomic multi-edit tool
 * (apply_patch), and the pre-approval diff preview all compute identical
 * results.
 */
#ifndef JC_PATCH_H
#define JC_PATCH_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Non-overlapping occurrences of `needle` in `hay` (0 when `needle` is empty or
 * either pointer is NULL). */
int jc_patch_count(const char *hay, const char *needle);

/* Append to `out` the result of replacing `old_s` with `new_s` in `text`: the
 * first occurrence only, or every occurrence when `replace_all` is nonzero. If
 * `old_s` does not occur, `text` is appended unchanged. Caller should normally
 * check jc_patch_count() first to report not-found / not-unique. */
void jc_patch_build(const char *text, const char *old_s, const char *new_s,
                    int replace_all, struct jc_sb *out);

/* How jc_patch_apply resolved an edit. Ordered so that any successful match is
 * >= JC_PATCH_EXACT (callers test `strategy >= JC_PATCH_EXACT` for success). */
enum jc_patch_strategy {
    JC_PATCH_NONE = 0,    /* old_string not found at all                       */
    JC_PATCH_AMBIGUOUS,   /* matched more than one location (caller refuses)   */
    JC_PATCH_EXACT,       /* exact byte match                                  */
    JC_PATCH_WS,          /* whitespace-insensitive line match (M38)           */
    JC_PATCH_ANCHOR       /* first/last-line anchor match (M38)                */
};

/* Short human label for a strategy ("exact" / "whitespace-insensitive" /
 * "anchored" / "none"). Never NULL. */
const char *jc_patch_strategy_name(enum jc_patch_strategy s);

/* Resolve one find/replace of `old_s` -> `new_s` against `text` and, on success,
 * append the edited text to `out`.
 *
 * Exact byte match is tried first. When exact is NOT found and `fuzzy` is
 * nonzero, two line-oriented fallbacks are tried, each requiring a UNIQUE hit:
 *   1. whitespace-insensitive  - every line equal after trimming leading/
 *      trailing whitespace and normalizing CR/LF (tolerates indentation,
 *      trailing-space, and line-ending drift);
 *   2. anchored                - the first and last non-blank lines of `old_s`
 *      match, with the span covering `old_s`'s line count (tolerates a
 *      misquoted interior line).
 * `replace_all` applies to EXACT matches only (the fuzzy tiers never bulk-
 * replace). *nmatches (may be NULL) gets the match/replacement count.
 *
 * Returns the strategy used (EXACT/WS/ANCHOR) on success, JC_PATCH_AMBIGUOUS
 * when a tier matched more than one place, or JC_PATCH_NONE when nothing
 * matched. On a non-success return `out` is left unchanged. Pure. */
enum jc_patch_strategy jc_patch_apply(const char *text, const char *old_s,
                                      const char *new_s, int replace_all,
                                      int fuzzy, struct jc_sb *out,
                                      int *nmatches);

/* When `old_s` was not found in `text`, append a "did you mean" hint to `out`: a
 * small, line-numbered excerpt of the region of `text` most similar to the first
 * non-blank line of `old_s` (scored by shared identifier tokens). Appends nothing
 * when there is no reasonable near-match or on empty input. Lets the model fix a
 * stale old_string instead of retrying blind (the measured edit-failure loop).
 * Pure; unit-tested. */
void jc_patch_nearmatch_hint(const char *text, const char *old_s,
                             struct jc_sb *out);

/* When `old_s` matched MORE than once, append the 1-based line numbers where it
 * occurs (up to 8, then ", ...") plus what to do about it.
 *
 * M208: the sibling of the hint above, for the other half of the measured
 * edit-failure loop. Across four dogfood drives, 11 of 14 failed edits were
 * "old_string is not unique" -- the file held 21 near-identical test blocks, so
 * every short snippet matched several of them. The old message gave a COUNT and
 * told the model to "add more surrounding context", which is advice it cannot act
 * on without knowing which places collided: each retry was a fresh guess, and
 * some collided again. Naming the lines turns that loop into one targeted edit.
 * Pure; unit-tested. */
void jc_patch_matchlines_hint(const char *text, const char *old_s,
                              struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_PATCH_H */
