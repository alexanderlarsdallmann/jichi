/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_codebase_search.c - semantic codebase search tool.
 *
 * Embeds the workspace (cached on disk), retrieves the chunks most similar to
 * the query, reranks them when a rerank model is configured, and returns the
 * best file:line snippets. Read-only.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_search.h"

#include <stdlib.h>

static cJSON *codebase_search_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "query",
                     "Natural-language description of the code to find", 1);
    /* top_k is an optional integer; described in the property list. */
    {
        cJSON *props = cJSON_GetObjectItem(s, "properties");
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "type", "integer");
        cJSON_AddStringToObject(p, "description",
                                "Number of results to return (default 5)");
        cJSON_AddItemToObject(props, "top_k", p);
    }
    return s;
}

static jc_status codebase_search_run(const cJSON *args,
                                     struct jc_tool_result *out,
                                     struct jc_app *app)
{
    const char *query = tu_arg_str(args, "query");
    cJSON *tk = cJSON_GetObjectItem(args, "top_k");
    int top_k = cJSON_IsNumber(tk) ? (int)tk->valuedouble : 5;
    char *text = NULL;
    jc_status st;

    if (query == NULL || query[0] == '\0') {
        tu_err(out, "error: 'query' argument is required");
        return JC_OK;
    }

    st = jc_search_run(app, query, top_k, &text);
    if (st != JC_OK) {
        /* jc_search_run fills `text` with a human-readable reason. */
        if (text != NULL) {
            tu_ok_owned(out, text);
            out->is_error = 1;
        } else {
            tu_err(out, "error: codebase search failed");
        }
        return JC_OK;
    }
    tu_ok_owned(out, text);
    return JC_OK;
}

static const struct jc_tool CODEBASE_SEARCH_TOOL = {
    "codebase_search",
    "Semantic search over the current workspace. Given a natural-language "
    "query, returns the most relevant code chunks as file:line snippets, "
    "ranked by an embedding model (and a reranker when configured). Prefer "
    "this over search_code when you do not know the exact keywords.",
    codebase_search_schema,
    1, /* readonly */
    codebase_search_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_codebase_search(void)
{
    return &CODEBASE_SEARCH_TOOL;
}
