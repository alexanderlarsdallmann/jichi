/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lexical.h - pure lexical (BM25-lite) scoring over in-memory documents.
 *
 * The dense retriever (jc_cosine_topn) captures semantic similarity but blurs
 * exact identifiers and rare keywords. jc_lexical_topn ranks the same chunks by
 * a classic BM25-lite term-overlap score so a query naming a literal symbol
 * (a function name, an error string) surfaces the chunk that contains it. The
 * two ranked lists are fused by jc_rrf_fuse (see jc_retrieve.h).
 *
 * Tokenization lowercases and splits on non-alphanumeric bytes, so an
 * identifier like `jc_lexical_topn` yields the terms jc / lexical / topn and a
 * natural-language query matches its parts. Pure: no I/O, no network.
 */
#ifndef JC_LEXICAL_H
#define JC_LEXICAL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Rank `count` documents (docs[i] may be NULL => treated as empty) by BM25-lite
 * relevance to `query`. Writes up to `top_n` results with a positive score,
 * best first, into out_idx/out_score (each at least top_n long). Returns the
 * number written (0 when the query has no usable terms or nothing matches). */
int jc_lexical_topn(const char *const *docs, int count, const char *query,
                    int top_n, int *out_idx, double *out_score);

#ifdef __cplusplus
}
#endif
#endif /* JC_LEXICAL_H */
