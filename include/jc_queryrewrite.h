/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_queryrewrite.h - optional query transformation for retrieval (M60).
 *
 * Recall is bounded by how the user phrases a query. When enabled (config
 * `retrieval.queryRewrite`), one non-streaming model call expands the query
 * before it is embedded: HyDE writes a short hypothetical passage that would
 * match the answer; multiquery lists alternative phrasings. The expansion is
 * appended to the raw query so both the dense and lexical passes benefit.
 *
 * The prompt builder and the response cleaner are pure (unit-testable); only
 * jc_queryrewrite_run performs I/O. Off by default (a model call has latency).
 */
#ifndef JC_QUERYREWRITE_H
#define JC_QUERYREWRITE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app;
struct jc_sb;

/* Build the rewrite request's user message for `query` into `out` (mode is an
 * enum jc_query_rewrite value). Pure; appends to an initialised builder. */
void jc_queryrewrite_prompt(const char *query, int mode, struct jc_sb *out);

/* Clean a model response into a bare expansion: drop ``` fence lines and common
 * list bullets/numbering, and join the remaining lines with single spaces.
 * Pure; appends to `out`. */
void jc_queryrewrite_clean(const char *resp, struct jc_sb *out);

/* Run one rewrite call against the summarize-role model (fallback: the active
 * model) for `query`. Returns a malloc'd effective query (the raw query plus the
 * cleaned expansion), or NULL when disabled (mode JC_QR_OFF) or on any failure —
 * the caller then embeds the raw query unchanged. */
char *jc_queryrewrite_run(struct jc_app *app, const char *query, int mode);

#ifdef __cplusplus
}
#endif
#endif /* JC_QUERYREWRITE_H */
