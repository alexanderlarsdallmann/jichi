/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lexical.c - pure BM25-lite lexical scoring (see jc_lexical.h). */

#include "jc_lexical.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* Standard BM25 free parameters. */
#define K1 1.2
#define B  0.75

#define MAX_QTERMS 64   /* distinct query terms scored (extra are ignored)   */
#define TOK_MAX    64   /* longest token kept (longer ones are truncated)    */

/* Copy the next alphanumeric run of *pp into buf (lowercased, NUL-terminated,
 * truncated to cap-1). Returns 1 if a token was produced, 0 at end of string.
 * Advances *pp past the token. */
static int tok_next(const char **pp, char *buf, int cap)
{
    const char *p = *pp;
    int n = 0;

    while (*p != '\0' && !isalnum((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *pp = p;
        return 0;
    }
    while (*p != '\0' && isalnum((unsigned char)*p)) {
        if (n < cap - 1) {
            buf[n++] = (char)tolower((unsigned char)*p);
        }
        p++;
    }
    buf[n] = '\0';
    *pp = p;
    return n > 0;
}

/* Number of times query term qi occurs in `doc`. */
static int term_freq(const char *doc, const char *term)
{
    const char *p = doc;
    char tok[TOK_MAX];
    int f = 0;

    if (doc == NULL) {
        return 0;
    }
    while (tok_next(&p, tok, TOK_MAX)) {
        if (strcmp(tok, term) == 0) {
            f++;
        }
    }
    return f;
}

/* Token count of `doc`. */
static int doc_length(const char *doc)
{
    const char *p = doc;
    char tok[TOK_MAX];
    int n = 0;

    if (doc == NULL) {
        return 0;
    }
    while (tok_next(&p, tok, TOK_MAX)) {
        n++;
    }
    return n;
}

int jc_lexical_topn(const char *const *docs, int count, const char *query,
                    int top_n, int *out_idx, double *out_score)
{
    char qterms[MAX_QTERMS][TOK_MAX];
    int  df[MAX_QTERMS];
    double idf[MAX_QTERMS];
    int  nq = 0;
    int  i, q;
    int  filled = 0;
    long total_len = 0;
    double avgdl;
    int *doclen = NULL;
    const char *p;
    char tok[TOK_MAX];

    if (docs == NULL || count <= 0 || top_n <= 0 || query == NULL) {
        return 0;
    }

    /* Distinct query terms (linear dedup; the set is tiny). */
    p = query;
    while (nq < MAX_QTERMS && tok_next(&p, tok, TOK_MAX)) {
        int dup = 0;
        for (q = 0; q < nq; q++) {
            if (strcmp(qterms[q], tok) == 0) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            strcpy(qterms[nq], tok);
            df[nq] = 0;
            nq++;
        }
    }
    if (nq == 0) {
        return 0;
    }

    doclen = (int *)malloc((jc_size)count * sizeof(int));
    if (doclen == NULL) {
        return 0;
    }

    /* Pass 1: document lengths + document frequency per query term. */
    for (i = 0; i < count; i++) {
        const char *doc = docs[i];
        doclen[i] = doc_length(doc);
        total_len += doclen[i];
        for (q = 0; q < nq; q++) {
            if (term_freq(doc, qterms[q]) > 0) {
                df[q]++;
            }
        }
    }
    avgdl = (count > 0) ? (double)total_len / (double)count : 1.0;
    if (avgdl <= 0.0) {
        avgdl = 1.0;
    }
    for (q = 0; q < nq; q++) {
        idf[q] = log(1.0 + ((double)count - (double)df[q] + 0.5) /
                           ((double)df[q] + 0.5));
    }

    /* Pass 2: BM25-lite score per document, descending top-n insertion. */
    for (i = 0; i < count; i++) {
        const char *doc = docs[i];
        double score = 0.0;
        double dl = (double)doclen[i];
        int j;
        for (q = 0; q < nq; q++) {
            int f = term_freq(doc, qterms[q]);
            if (f > 0) {
                double denom = (double)f + K1 * (1.0 - B + B * dl / avgdl);
                score += idf[q] * ((double)f * (K1 + 1.0)) / denom;
            }
        }
        if (score <= 0.0) {
            continue;
        }
        if (filled < top_n) {
            j = filled++;
        } else if (score > out_score[top_n - 1]) {
            j = top_n - 1;
        } else {
            continue;
        }
        while (j > 0 && out_score[j - 1] < score) {
            out_score[j] = out_score[j - 1];
            out_idx[j] = out_idx[j - 1];
            j--;
        }
        out_score[j] = score;
        out_idx[j] = i;
    }

    free(doclen);
    return filled;
}
