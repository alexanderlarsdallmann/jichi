/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_search.c - semantic codebase search orchestration (see jc_search.h).
 *
 * Thin wrapper over the shared retrieval core (jc_retrieve): ensure the
 * workspace index exists (cached on app->index), run the embed -> hybrid fuse ->
 * rerank pipeline, and format the best chunks as text. The docs index
 * (jc_docs_run) is the sibling wrapper over the same core. */

#include "jc_search.h"
#include "jc_app.h"
#include "jc_index.h"
#include "jc_retrieve.h"
#include "jc_queryrewrite.h"
#include "jc_config.h"
#include "jc_str.h"
#include "jc_snprintf.h"   /* the coverage note (M483) */

#include <stdlib.h>

/* M483: warn, in the tool result itself, when the index has known holes. */
static void append_coverage_note(struct jc_app *app, struct jc_sb *sb)
{
    int nd = jc_index_unreadable_dirs(app->index);
    char line[220];
    if (nd <= 0) {
        return;
    }
    jc_snprintf(line, sizeof(line),
                "\n\nnote: %d director%s under this workspace could not be read "
                "and %s NOT in the index, so these results are incomplete -- "
                "absence here does not mean the code does not exist.",
                nd, nd == 1 ? "y" : "ies", nd == 1 ? "is" : "are");
    jc_sb_append(sb, line);
}

static char *format_results(struct jc_app *app, const int *idx, int n)
{
    struct jc_sb sb;
    int i;

    jc_sb_init(&sb);
    if (n == 0) {
        jc_sb_append(&sb, "No matching code found in the index.");
        /* M483: THE most important place this can be said. "No matching code
         * found" is what a model reads as "the code does not contain this", and
         * it is exactly the wrong conclusion when part of the tree was never
         * indexed because a directory could not be read. Same failure shape as
         * the OpenBSD `search_code` defect (M461): the tool did not fail, it
         * lied quietly. Stated on hits too, below, since an incomplete ranking
         * misleads just as much as an empty one. */
        append_coverage_note(app, &sb);
        return jc_sb_finish(&sb);
    }
    for (i = 0; i < n; i++) {
        const char *path = jc_index_chunk_path(app->index, idx[i]);
        const char *text = jc_index_chunk_text(app->index, idx[i]);
        int s = jc_index_chunk_start(app->index, idx[i]);
        int e = jc_index_chunk_end(app->index, idx[i]);
        jc_sb_append_fmt(&sb, "%s:%d-%d\n", path != NULL ? path : "?", s, e);
        jc_sb_append(&sb, text != NULL ? text : "");
        jc_sb_append(&sb, "\n---\n");
    }
    append_coverage_note(app, &sb);   /* hits can be incomplete too (M483) */
    return jc_sb_finish(&sb);
}

jc_status jc_search_run(struct jc_app *app, const char *query, int top_k,
                        char **out_text)
{
    struct jc_model_cfg *embed_model;
    struct jc_model_cfg *rerank_model;
    struct jc_retrieve_opts opts;
    int *idx = NULL;
    double *score = NULL;
    int n = 0;
    jc_status st;

    *out_text = NULL;
    if (top_k <= 0) {
        top_k = 5;
    }

    embed_model = jc_app_model_for_role(app, JC_ROLE_EMBED);
    if (embed_model == NULL) {
        *out_text = jc_strdup("error: no embedding model configured "
                              "(add a model with role \"embed\")");
        return JC_ERR_NOTFOUND;
    }

    /* Build/load the index once per process. */
    if (app->index == NULL) {
        st = jc_index_build(app->cwd, embed_model, 0, NULL, &app->index, NULL,
                            &app->abort_flag, &app->config.ignore_dirs);
        if (st != JC_OK) {
            *out_text = jc_strdup("error: failed to build the codebase index");
            return st;
        }
    }
    if (jc_index_count(app->index) == 0) {
        *out_text = jc_strdup("The codebase index is empty.");
        return JC_OK;
    }

    rerank_model = jc_app_model_for_role(app, JC_ROLE_RERANK);
    jc_retrieve_opts_from_config(&app->config, top_k, &opts);

    idx = (int *)malloc((jc_size)top_k * sizeof(int));
    score = (double *)malloc((jc_size)top_k * sizeof(double));
    if (idx == NULL || score == NULL) {
        free(idx);
        free(score);
        return JC_ERR_OOM;
    }
    {
        /* Optional query rewrite/HyDE (opt-in): embed an expanded query. */
        char *eff = NULL;
        const char *q = query;
        if (app->config.retrieval.query_rewrite != JC_QR_OFF) {
            eff = jc_queryrewrite_run(app, query,
                                      app->config.retrieval.query_rewrite);
            if (eff != NULL) {
                q = eff;
            }
        }
        st = jc_retrieve_from_index(app->index, embed_model, rerank_model, q,
                                    &opts, idx, score, &n, &app->abort_flag);
        free(eff);
    }
    if (st != JC_OK) {
        free(idx);
        free(score);
        if (st == JC_ERR_PROVIDER) {
            *out_text = jc_strdup("error: query/index embedding dimension "
                                  "mismatch (try: jichi index --reindex)");
        } else {
            *out_text = jc_strdup("error: failed to embed the query");
        }
        return st;
    }

    *out_text = format_results(app, idx, n);
    free(idx);
    free(score);
    /* M141: in lite mode the index (~vectors + every chunk's text) is not
     * kept resident between searches -- rebuild-from-cache per query, like
     * the docs index always does. Trades repeat-search latency for RAM. */
    if (app->config.low_resource) {
        jc_index_free(app->index);
        app->index = NULL;
    }
    return (*out_text != NULL) ? JC_OK : JC_ERR_OOM;
}
