/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_complete.h - pure helpers for Tab completion.
 *
 * The TUI builds candidate lists from these (token under the cursor, longest
 * common prefix); jc_term applies the readline-style replace/list. No I/O.
 */
#ifndef JC_COMPLETE_H
#define JC_COMPLETE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Return a pointer to the whitespace-delimited token that ends at byte offset
 * `cursor` in `buf`, and set *start to its start offset. An empty token (cursor
 * at start or right after whitespace) returns a pointer to "" with *start ==
 * cursor. The token length is `cursor - *start`. */
const char *jc_complete_token(const char *buf, jc_size cursor, jc_size *start);

/* Write the longest common prefix of the `n` candidate strings into `out`
 * (NUL-terminated, capped at `cap`) and return its length. 0 candidates or no
 * shared prefix yields "". */
jc_size jc_complete_common_prefix(const char *const *cands, int n,
                                  char *out, jc_size cap);

/* readline-style word boundaries over `buf[0..len)` with the cursor at byte
 * index `cursor` (a "word char" is [A-Za-z0-9]). `_left` returns the start of
 * the word at/before the cursor (skip preceding non-word chars, then the word);
 * `_right` returns the position just past the next word. Used by the line
 * editor's Alt-B/F, Ctrl-W, Alt-D. Pure; unit-tested (M126). */
jc_size jc_line_word_left(const char *buf, jc_size len, jc_size cursor);
jc_size jc_line_word_right(const char *buf, jc_size len, jc_size cursor);

/* Splice pasted text into the edit buffer at the cursor (M156 multiline paste).
 * Returns a malloc'd string = `before` + normalized(`pasted`) + `after`, where
 * normalization turns CRLF and lone CR into LF (so the submitted content holds
 * exactly the pasted line breaks). `*out_cursor` (if non-NULL) is set to the
 * byte offset just past the inserted text (strlen(before)+len(normalized)).
 * `before`/`after`/`pasted` may be NULL (treated as empty). Caller frees.
 * Pure; unit-tested. The line editor splits the result on '\n' for display
 * (committed rows vs the editable tail); the string itself is what is
 * submitted, so this is the correctness-bearing core. */
char *jc_paste_splice(const char *before, const char *after,
                      const char *pasted, jc_size *out_cursor);

#ifdef __cplusplus
}
#endif
#endif /* JC_COMPLETE_H */
