/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_search.h - semantic codebase search orchestration.
 *
 * Ties together the index (jc_index), the embedding client (jc_embed) and the
 * reranker (jc_rerank): ensure an index exists for the workspace, embed the
 * query, take the cosine-nearest chunks, optionally rerank them, and format the
 * best matches as text. Shared by the `codebase_search` tool and the `embed`/
 * `rerank` CLI subcommands.
 */
#ifndef JC_SEARCH_H
#define JC_SEARCH_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app;

/* Run a semantic search over app->cwd for `query`, returning the `top_k` best
 * chunks formatted as text via *out_text (malloc'd; caller frees). The index
 * is built on first use and cached on app->index. Requires an embed-role model
 * in the config; a rerank-role model is used when present. Returns:
 *   JC_OK           results written (may be an empty-index notice)
 *   JC_ERR_NOTFOUND no embedding model configured
 *   other           build/embed/transport failure (message also in *out_text) */
jc_status jc_search_run(struct jc_app *app, const char *query, int top_k,
                        char **out_text);

#ifdef __cplusplus
}
#endif
#endif /* JC_SEARCH_H */
