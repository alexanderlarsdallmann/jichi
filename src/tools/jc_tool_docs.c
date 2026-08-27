/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_docs.c - the search_docs tool (M34a).
 *
 * Retrieves the most relevant chunks of an *external* documentation source (a
 * config `docs: [{name, path}]` entry) for a query, via jc_docs_run. Read-only.
 * Registered only when at least one `docs` source is configured (like
 * web_search). When more than one source is configured the `name` argument
 * selects which; with a single source it is optional.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_docs.h"
#include "jc_str.h"

#include <stdlib.h>

#define DOCS_DEF_RESULTS 5

static cJSON *search_docs_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "query", "What to look for in the documentation", 1);
    tu_schema_string(s, "name",
                     "Which documentation source to search (required when more "
                     "than one is configured)", 0);
    tu_schema_int(s, "max_results", "Maximum results to return (optional)", 0);
    return s;
}

static jc_status search_docs_run(const cJSON *args, struct jc_tool_result *out,
                                 struct jc_app *app)
{
    const char *query = tu_arg_str(args, "query");
    const char *name = tu_arg_str(args, "name");
    const struct jc_docs_cfg *src;
    int want;
    char *text = NULL;
    jc_status st;

    if (query == NULL || query[0] == '\0') {
        tu_err(out, "error: 'query' argument is required");
        return JC_OK;
    }
    if (app->config.docs.len == 0) {
        tu_err(out, "error: no documentation sources configured (set \"docs\")");
        return JC_OK;
    }

    src = jc_docs_find(app, name);
    if (src == NULL) {
        struct jc_sb sb;
        jc_size i;
        jc_sb_init(&sb);
        if (name != NULL && name[0] != '\0') {
            jc_sb_append_fmt(&sb, "error: unknown docs source '%s'. ", name);
        } else {
            jc_sb_append(&sb, "error: more than one docs source is configured; "
                              "pass 'name'. ");
        }
        jc_sb_append(&sb, "Available:");
        for (i = 0; i < app->config.docs.len; i++) {
            const struct jc_docs_cfg *d =
                (const struct jc_docs_cfg *)jc_vec_at(&app->config.docs, i);
            jc_sb_append_fmt(&sb, " %s", d->name != NULL ? d->name : "?");
        }
        tu_ok_owned(out, jc_sb_finish(&sb));
        jc_sb_free(&sb);
        out->is_error = 1;
        return JC_OK;
    }

    want = tu_arg_int(args, "max_results", 0);
    if (want <= 0) {
        want = DOCS_DEF_RESULTS;
    }

    st = jc_docs_run(app, src, query, want, &text);
    if (text == NULL) {
        tu_err(out, "error: documentation search failed");
        return JC_OK;
    }
    if (st != JC_OK) {
        tu_ok_owned(out, text);
        out->is_error = 1;
        return JC_OK;
    }
    tu_ok_owned(out, text);
    return JC_OK;
}

static const struct jc_tool SEARCH_DOCS_TOOL = {
    "search_docs",
    "Search an external documentation source (library docs, a style guide, a "
    "spec) configured under \"docs\" and return the most relevant passages. Use "
    "to ground answers in reference material outside the workspace.",
    search_docs_schema,
    1, /* read-only */
    search_docs_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_search_docs(void)
{
    return &SEARCH_DOCS_TOOL;
}
