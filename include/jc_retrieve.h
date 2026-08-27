/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_retrieve.h - shared retrieval core for codebase + docs search.
 *
 * jc_search_run (codebase) and jc_docs_run (external docs) used to duplicate the
 * same embed-query -> cosine -> rerank pipeline. This module is the single core
 * they both delegate to, so a quality improvement (hybrid lexical+dense fusion,
 * candidate sizing) is written once and applies everywhere.
 *
 * Pipeline (jc_retrieve_from_index): embed the query and take the cosine-nearest
 * chunks; when `hybrid`, also rank by BM25-lite (jc_lexical_topn) and fuse the
 * two ranked lists with Reciprocal Rank Fusion (jc_rrf_fuse); then optionally
 * rerank the fused candidates and trim to top_k. The candidate *indices* are
 * returned; the caller formats them (each surface keeps its own formatter).
 *
 * jc_rrf_fuse is a pure, unit-testable helper.
 */
#ifndef JC_RETRIEVE_H
#define JC_RETRIEVE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_index;
struct jc_model_cfg;
struct jc_config;

/* Tunables resolved from config `retrieval` (see jc_config). A zero field takes
 * the built-in default noted below. */
struct jc_retrieve_opts {
    int top_k;        /* results to return            (default 5)  */
    int hybrid;       /* fuse lexical + dense          (0/1)       */
    int rrf_k;        /* RRF constant                  (default 60) */
    int cand_factor;  /* candidates per result         (default 4)  */
    int cand_min;     /* candidate floor               (default 20) */
};

/* Fill `out` with the built-in defaults (hybrid on, top_k as given). */
void jc_retrieve_opts_default(struct jc_retrieve_opts *out, int top_k);

/* Fill `out` from config `retrieval` over the built-in defaults: hybrid is the
 * tri-state c->retrieval.hybrid (-1 auto => on), rrf_k the configured value when
 * positive. `top_k` sets opts->top_k. c may be NULL (=> defaults). */
void jc_retrieve_opts_from_config(const struct jc_config *c, int top_k,
                                  struct jc_retrieve_opts *out);

/* Reciprocal Rank Fusion of two ranked lists of document ids (rank 0 = best).
 * score(d) = sum over the lists d appears in of 1/(rrf_k + rank + 1). Writes the
 * fused ids/scores, best first, into out_idx/out_score (up to `cap`). Returns
 * the number written. Pure: no I/O. Duplicates within a single list are folded.
 * rrf_k <= 0 falls back to 60. */
int jc_rrf_fuse(const int *a_idx, int na, const int *b_idx, int nb,
                int rrf_k, int *out_idx, double *out_score, int cap);

/* Retrieve the best chunks of `idx` for `query` into out_idx/out_score (each at
 * least opts->top_k long), writing the count to *out_n. `embed` is required;
 * `rerank` may be NULL. Returns JC_OK on success (including an empty index =>
 * *out_n 0), or a transport/dimension error. `abort` may be NULL. */
jc_status jc_retrieve_from_index(struct jc_index *idx,
                                 const struct jc_model_cfg *embed,
                                 const struct jc_model_cfg *rerank,
                                 const char *query,
                                 const struct jc_retrieve_opts *opts,
                                 int *out_idx, double *out_score, int *out_n,
                                 volatile int *abort);

#ifdef __cplusplus
}
#endif
#endif /* JC_RETRIEVE_H */
