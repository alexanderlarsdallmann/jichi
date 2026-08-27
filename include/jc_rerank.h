/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rerank.h - OpenAI/Cohere-compatible reranking client.
 *
 * Posts a query plus candidate documents to {api_base}/rerank and returns a
 * relevance score per document. Accepts both response shapes seen in the wild:
 * a top-level "data" array (OpenAI/vLLM/Voyage) or "results" (Cohere), each
 * item carrying an "index" and "relevance_score" (or "score").
 */
#ifndef JC_RERANK_H
#define JC_RERANK_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"

/* Rerank `n` documents against `query` with `m` (a rerank-capable model).
 * Fills out_scores[i] with the score for docs[i] (mapped back via the
 * response "index"). Documents not mentioned in the response keep 0.0.
 * `abort` (may be NULL) cancels in-flight transfers. */
jc_status jc_rerank_score(const struct jc_model_cfg *m, const char *query,
                          const char *const *docs, int n, double *out_scores,
                          volatile int *abort);

/* Parse a rerank response body into out_scores[0..n-1] (zeroed first). Accepts
 * "data" or "results" arrays and "relevance_score" or "score" fields. Returns
 * JC_ERR_PARSE if no recognised array is present. Network-free. */
jc_status jc_rerank_parse(const char *json, int n, double *out_scores);

#ifdef __cplusplus
}
#endif
#endif /* JC_RERANK_H */
