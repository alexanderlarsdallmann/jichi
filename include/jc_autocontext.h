/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_autocontext.h - automatic retrieval-augmented context injection (M61).
 *
 * The missing half of RAG: retrieval today is explicit (the model calls a search
 * tool, or the user types an @-reference). When enabled (config `autoContext`,
 * opt-in/off by default), jc_autocontext_expand retrieves the chunks most
 * relevant to a plain user turn from the codebase index and the configured docs
 * sources, and appends them as a bounded "automatically retrieved context"
 * block — the same shape jc_refs_expand uses.
 *
 * It rides on the USER message (not the system prompt) so the cacheable
 * system+tools prefix (M31 prompt caching) stays byte-stable across turns.
 *
 * Contract mirrors jc_refs_expand: returns arena-owned text; on any no-op (off,
 * a subagent, a slash command, no embed model, nothing retrieved, or a message
 * already carrying @-references) *out is `raw` unchanged. Always returns JC_OK.
 */
#ifndef JC_AUTOCONTEXT_H
#define JC_AUTOCONTEXT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app;
struct jc_arena;

/* Token budget for the injected block: cap (0 => built-in default) clamped to a
 * third of the context limit (so retrieval never crowds out the conversation).
 * limit <= 0 => just the cap. Pure; unit-testable. */
long jc_autocontext_budget(long limit, long cap);

/* Expand `raw` with retrieved context per the contract above. */
jc_status jc_autocontext_expand(struct jc_app *app, const char *raw,
                                struct jc_arena *a, char **out);

#ifdef __cplusplus
}
#endif
#endif /* JC_AUTOCONTEXT_H */
