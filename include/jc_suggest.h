/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_suggest.h - the pure halves of the TUI's two model-assisted composing
 * gestures (M280).
 *
 *   Ctrl-G  "ghost text": continue the half-typed line, spliced in at the
 *           cursor when accepted with Tab.
 *   Ctrl-Q  "advice": ONE labelled line below the prompt saying what is
 *           unclear about the request, printed rather than spliced.
 *
 * Both are one non-streaming model call, and both need exactly two pure
 * pieces: the system prompt to send, and a cleaner for the reply. Keeping
 * them here (rather than as string literals in jc_tui.c) makes the cleaners
 * unit-testable, which matters because they encode what models actually do
 * wrong rather than what the prompt asked for.
 *
 * WHY THE FEW-SHOT EXAMPLES EXIST. jichi asked for a continuation and nothing
 * else, in plain words, and a chat-tuned model answered the question instead:
 * typing "what is the name of this pr" and pressing Ctrl-G produced "Could you
 * provide more context or clarify which PR you're referring to?", spliced into
 * the input line (reported 2026-08-04, documented in docs/AUTOCOMPLETE.md).
 * An instruction a model may ignore is a weaker signal than a demonstration it
 * can pattern-match, so the prompt now SHOWS three continuations -- including
 * that exact line -- and names the failure as something the examples do not do.
 *
 * The two gestures deliberately use DIFFERENT models: continuation is the
 * `autocomplete` role's job (it can be a small or FIM-tuned model), while
 * advice wants the model the user is actually talking to. See jc_tui.c.
 */
#ifndef JC_SUGGEST_H
#define JC_SUGGEST_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the system prompt for Ctrl-G into `out`: continue the line, output
 * only the continuation. Carries the few-shot examples; see the note above
 * for why they exist. Returns the length written. JC_SUGGEST_SYS_CAP is a
 * buffer size that always fits. */
#define JC_SUGGEST_SYS_CAP 2048
jc_size jc_suggest_system(char *out, jc_size cap);

/* Build the system prompt for Ctrl-Q into `out`: one short line naming the
 * single most useful thing to clarify, or exactly JC_ADVICE_CLEAR when
 * nothing needs it. Returns the length written. */
jc_size jc_advice_system(char *out, jc_size cap);

/* The sentinel an advising model is told to return when the request is
 * already clear and specific. Rendered as-is, so the user gets a definite
 * "nothing to fix" rather than invented nitpicking. */
#define JC_ADVICE_CLEAR "looks clear"

/* Clean a Ctrl-G reply into ghost text for `typed`.
 *
 * Handles what models do in practice even when told not to: a leading blank
 * line, the whole continuation wrapped in quotes, an "output:" label copied
 * from the few-shot examples, and an echo of the user's own line. Keeps ONE
 * line (a ghost overlay is single-line by design) and never exceeds cap-1
 * bytes. Trailing whitespace is trimmed; LEADING whitespace is preserved,
 * because a continuation at a word boundary legitimately begins with a space.
 *
 * `typed` may be NULL/empty. Returns the length written (0 = show nothing). */
jc_size jc_suggest_clean(const char *typed, const char *reply,
                         char *out, jc_size cap);

/* Clean a Ctrl-Q reply into one advice line: first non-blank line, a leading
 * "advice:"/"hint:" label or surrounding quotes removed, both ends trimmed,
 * capped. Returns the length written (0 = show nothing). */
jc_size jc_advice_clean(const char *reply, char *out, jc_size cap);

#ifdef __cplusplus
}
#endif

#endif /* JC_SUGGEST_H */
