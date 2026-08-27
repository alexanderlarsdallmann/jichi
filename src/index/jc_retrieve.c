/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_retrieve.c - shared retrieval core (see jc_retrieve.h). */

#include "jc_retrieve.h"
#include "jc_index.h"
#include "jc_embed.h"
#include "jc_rerank.h"
#include "jc_lexical.h"
#include "jc_config.h"

#include <stdlib.h>

/* Built-in candidate sizing (matches the historical jc_search defaults). */
#define DEF_TOP_K       5
#define DEF_CAND_FACTOR 4
#define DEF_CAND_MIN    20
#define DEF_RRF_K       60

void jc_retrieve_opts_default(struct jc_retrieve_opts *out, int top_k)
{
    if (out == NULL) {
        return;
    }
    out->top_k = (top_k > 0) ? top_k : DEF_TOP_K;
    out->hybrid = 1;
    out->rrf_k = DEF_RRF_K;
    out->cand_factor = DEF_CAND_FACTOR;
    out->cand_min = DEF_CAND_MIN;
}

void jc_retrieve_opts_from_config(const struct jc_config *c, int top_k,
                                  struct jc_retrieve_opts *out)
{
    if (out == NULL) {
        return;
    }
    jc_retrieve_opts_default(out, top_k);
    if (c == NULL) {
        return;
    }
    /* hybrid is tri-state: -1 auto (=on), 0 off, 1 on. */
    out->hybrid = (c->retrieval.hybrid == 0) ? 0 : 1;
    if (c->retrieval.rrf_k > 0) {
        out->rrf_k = c->retrieval.rrf_k;
    }
}

/* Descending insertion sort of indices by score (small n). */
static void sort_by_score(int *idx, double *score, int n)
{
    int i;
    for (i = 1; i < n; i++) {
        int ki = idx[i];
        double ks = score[i];
        int j = i - 1;
        while (j >= 0 && score[j] < ks) {
            score[j + 1] = score[j];
            idx[j + 1] = idx[j];
            j--;
        }
        score[j + 1] = ks;
        idx[j + 1] = ki;
    }
}

int jc_rrf_fuse(const int *a_idx, int na, const int *b_idx, int nb,
                int rrf_k, int *out_idx, double *out_score, int cap)
{
    int *tid;
    double *tsc;
    int n = 0;
    int i, j;
    int total;

    if (out_idx == NULL || out_score == NULL || cap <= 0) {
        return 0;
    }
    if (rrf_k <= 0) {
        rrf_k = DEF_RRF_K;
    }
    if (na < 0) {
        na = 0;
    }
    if (nb < 0) {
        nb = 0;
    }
    total = na + nb;
    if (total == 0) {
        return 0;
    }
    tid = (int *)malloc((jc_size)total * sizeof(int));
    tsc = (double *)malloc((jc_size)total * sizeof(double));
    if (tid == NULL || tsc == NULL) {
        free(tid);
        free(tsc);
        return 0;
    }

    /* Accumulate RRF contributions, folding duplicates by id. */
    for (i = 0; i < na; i++) {
        int id = a_idx[i];
        double c = 1.0 / (double)(rrf_k + i + 1);
        for (j = 0; j < n; j++) {
            if (tid[j] == id) {
                break;
            }
        }
        if (j == n) {
            tid[n] = id;
            tsc[n] = c;
            n++;
        } else {
            tsc[j] += c;
        }
    }
    for (i = 0; i < nb; i++) {
        int id = b_idx[i];
        double c = 1.0 / (double)(rrf_k + i + 1);
        for (j = 0; j < n; j++) {
            if (tid[j] == id) {
                break;
            }
        }
        if (j == n) {
            tid[n] = id;
            tsc[n] = c;
            n++;
        } else {
            tsc[j] += c;
        }
    }

    sort_by_score(tid, tsc, n);
    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; i++) {
        out_idx[i] = tid[i];
        out_score[i] = tsc[i];
    }
    free(tid);
    free(tsc);
    return n;
}

/* Collect the chunk texts of `idx` into a freshly malloc'd const char* array of
 * length jc_index_count(idx). Caller frees the array (not the strings). */
static const char **all_chunk_texts(struct jc_index *idx, int total)
{
    const char **docs = (const char **)malloc((jc_size)total *
                                              sizeof(char *));
    int i;
    if (docs == NULL) {
        return NULL;
    }
    for (i = 0; i < total; i++) {
        docs[i] = jc_index_chunk_text(idx, i);
    }
    return docs;
}

jc_status jc_retrieve_from_index(struct jc_index *idx,
                                 const struct jc_model_cfg *embed,
                                 const struct jc_model_cfg *rerank,
                                 const char *query,
                                 const struct jc_retrieve_opts *opts,
                                 int *out_idx, double *out_score, int *out_n,
                                 volatile int *abort)
{
    struct jc_retrieve_opts def;
    float *qvec = NULL;
    int qdim = 0;
    int total;
    int top_k, n_cand;
    int *dense_idx = NULL;
    double *dense_score = NULL;
    int got;
    jc_status st;

    if (out_n != NULL) {
        *out_n = 0;
    }
    if (idx == NULL || embed == NULL || query == NULL ||
        out_idx == NULL || out_score == NULL || out_n == NULL) {
        return JC_ERR_INVALID;
    }
    if (opts == NULL) {
        jc_retrieve_opts_default(&def, DEF_TOP_K);
        opts = &def;
    }
    top_k = (opts->top_k > 0) ? opts->top_k : DEF_TOP_K;

    total = jc_index_count(idx);
    if (total == 0) {
        return JC_OK;
    }

    /* Embed the query. */
    st = jc_embed_texts(embed, &query, 1, &qvec, &qdim, abort);
    if (st != JC_OK) {
        return st;
    }
    if (qdim != jc_index_dim(idx)) {
        free(qvec);
        return JC_ERR_PROVIDER;
    }

    n_cand = top_k * ((opts->cand_factor > 0) ? opts->cand_factor
                                              : DEF_CAND_FACTOR);
    if (n_cand < ((opts->cand_min > 0) ? opts->cand_min : DEF_CAND_MIN)) {
        n_cand = (opts->cand_min > 0) ? opts->cand_min : DEF_CAND_MIN;
    }
    if (n_cand > total) {
        n_cand = total;
    }

    dense_idx = (int *)malloc((jc_size)n_cand * sizeof(int));
    dense_score = (double *)malloc((jc_size)n_cand * sizeof(double));
    if (dense_idx == NULL || dense_score == NULL) {
        free(qvec);
        free(dense_idx);
        free(dense_score);
        return JC_ERR_OOM;
    }
    got = jc_index_search(idx, qvec, n_cand, dense_idx, dense_score);
    free(qvec);

    /* Hybrid: fuse the dense list with a BM25-lite lexical list. */
    if (opts->hybrid) {
        const char **docs = all_chunk_texts(idx, total);
        int *lex_idx = (int *)malloc((jc_size)n_cand * sizeof(int));
        double *lex_score = (double *)malloc((jc_size)n_cand * sizeof(double));
        if (docs != NULL && lex_idx != NULL && lex_score != NULL) {
            int got_lex = jc_lexical_topn(docs, total, query, n_cand,
                                          lex_idx, lex_score);
            int *fused_idx = (int *)malloc((jc_size)n_cand * sizeof(int));
            double *fused_score =
                (double *)malloc((jc_size)n_cand * sizeof(double));
            if (fused_idx != NULL && fused_score != NULL) {
                int nf = jc_rrf_fuse(dense_idx, got, lex_idx, got_lex,
                                     opts->rrf_k, fused_idx, fused_score,
                                     n_cand);
                int i;
                for (i = 0; i < nf; i++) {
                    dense_idx[i] = fused_idx[i];
                    dense_score[i] = fused_score[i];
                }
                got = nf;
            }
            free(fused_idx);
            free(fused_score);
        }
        free(docs);
        free(lex_idx);
        free(lex_score);
    }

    /* Optionally rerank the fused candidates. */
    if (rerank != NULL && got > 1) {
        const char **docs = (const char **)malloc((jc_size)got *
                                                  sizeof(char *));
        double *scores = (double *)malloc((jc_size)got * sizeof(double));
        if (docs != NULL && scores != NULL) {
            int i;
            for (i = 0; i < got; i++) {
                docs[i] = jc_index_chunk_text(idx, dense_idx[i]);
            }
            if (jc_rerank_score(rerank, query, docs, got, scores,
                                abort) == JC_OK) {
                for (i = 0; i < got; i++) {
                    dense_score[i] = scores[i];
                }
                sort_by_score(dense_idx, dense_score, got);
            }
        }
        free(docs);
        free(scores);
    }

    if (got > top_k) {
        got = top_k;
    }
    {
        int i;
        for (i = 0; i < got; i++) {
            out_idx[i] = dense_idx[i];
            out_score[i] = dense_score[i];
        }
    }
    *out_n = got;
    free(dense_idx);
    free(dense_score);
    return JC_OK;
}
