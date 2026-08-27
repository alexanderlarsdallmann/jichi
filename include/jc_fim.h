/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fim.h - fill-in-the-middle (FIM) prompt assembly.
 *
 * "Tab autocomplete" for editors: given the code immediately before the cursor
 * (the prefix) and immediately after it (the suffix), ask the autocomplete-role
 * model for the code that belongs at the cursor. jichi speaks chat/messages APIs
 * (not a raw FIM-token completion endpoint), so the prompt is model-agnostic:
 * the prefix and suffix are wrapped in <BEFORE>/<AFTER> markers and the system
 * prompt instructs the model to emit only the bridging code.
 *
 * The pieces here are pure (no I/O) and unit-tested; the one-shot model call and
 * the `fim` subcommand live in src/main.c (mirroring `complete`).
 */
#ifndef JC_FIM_H
#define JC_FIM_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Default per-side context budget (bytes) when the caller passes 0. */
#define JC_FIM_DEFAULT_BUDGET 4000u

/* System prompt for the FIM call. */
#define JC_FIM_SYSTEM \
    "You are a fill-in-the-middle code completion engine. The user gives the " \
    "code immediately before the cursor inside <BEFORE>...</BEFORE> and the " \
    "code immediately after the cursor inside <AFTER>...</AFTER>. Output ONLY " \
    "the code that should be inserted at the cursor so that the before-text, " \
    "your output, and the after-text join into correct code. Do not repeat any " \
    "of the before or after text. Do not wrap your output in markdown code " \
    "fences. Do not add explanations."

/* Bound one side of the FIM window to at most `budget` bytes. For a prefix
 * (keep_tail=1) the LAST `budget` bytes are kept (the text nearest the cursor);
 * for a suffix (keep_tail=0) the FIRST `budget` bytes are kept. Returns the
 * start offset into the side; *out_len receives the kept length. A `budget` of 0
 * means "no limit" (the whole side is kept). */
jc_size jc_fim_bound(jc_size len, jc_size budget, int keep_tail,
                     jc_size *out_len);

/* Assemble the FIM user message into `sb` from the (already-bounded) prefix and
 * suffix substrings. The markers carry the cursor boundary exactly (no stray
 * whitespace is inserted around the code). */
void jc_fim_build_user(const char *prefix, const char *suffix,
                       struct jc_sb *sb);

/* Append `in` to `out` with a wrapping markdown code fence removed, if present:
 * a leading ```[lang]\n and the matching trailing ``` (and the newline before
 * it). Models tend to fence code even when asked not to; FIM output must be raw
 * insertion text, so this de-fences it. Text without a leading fence is copied
 * verbatim. */
void jc_fim_strip_fences(const char *in, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_FIM_H */
