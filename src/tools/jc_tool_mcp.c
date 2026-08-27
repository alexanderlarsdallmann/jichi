/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_mcp.c - read_mcp_resource tool (M43).
 *
 * Lets the agent pull a resource exposed by a connected MCP server (read-only
 * context — files, docs, database records). A dynamic (ctx=app) tool so its
 * schema can enumerate the discovered resources; registered only when at least
 * one server advertises resources. Read-only.
 */

#include "tool_util.h"
#include "jc_untrusted.h"
#include "jc_app.h"
#include "jc_mcp.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

static cJSON *mcp_res_schema(void *vctx)
{
    struct jc_app *app = (struct jc_app *)vctx;
    cJSON *s = tu_schema_begin();
    struct jc_sb d;

    jc_sb_init(&d);
    jc_sb_append(&d, "The uri of the MCP resource to read.");
    if (app != NULL && app->mcp != NULL) {
        int n = jc_mcp_resource_count(app->mcp);
        int i;
        if (n > 0) {
            jc_sb_append(&d, " Available resources:\n");
        }
        for (i = 0; i < n; i++) {
            const char *desc = NULL;
            const char *uri = jc_mcp_resource_at(app->mcp, i, NULL, &desc);
            if (uri == NULL) {
                continue;
            }
            jc_sb_append(&d, uri);
            if (desc != NULL && desc[0] != '\0') {
                jc_sb_append(&d, " \xe2\x80\x94 "); /* em dash */
                jc_sb_append(&d, desc);
            }
            jc_sb_append_char(&d, '\n');
        }
    }
    tu_schema_string(s, "uri", d.data != NULL ? d.data : "", 1);
    jc_sb_free(&d);
    return s;
}

static jc_status mcp_res_run(void *vctx, const cJSON *args,
                             struct jc_tool_result *out, struct jc_app *app)
{
    const char *uri = tu_arg_str(args, "uri");
    char *text = NULL;
    jc_status st;
    char msg[1100];

    (void)vctx;
    if (uri == NULL || uri[0] == '\0') {
        tu_err(out, "error: 'uri' is required");
        return JC_OK;
    }
    if (app->mcp == NULL) {
        tu_err(out, "error: no MCP servers are connected");
        return JC_OK;
    }
    st = jc_mcp_read_resource(app->mcp, uri, &text);
    if (st == JC_ERR_INVALID) {
        jc_snprintf(msg, sizeof(msg), "error: no MCP resource matches '%s'",
                    uri);
        tu_err(out, msg);
        return JC_OK;
    }
    if (st != JC_OK || text == NULL) {
        free(text);
        jc_snprintf(msg, sizeof(msg),
                    "error: could not read MCP resource '%s'", uri);
        tu_err(out, msg);
        return JC_OK;
    }
    /* M300: an MCP *resource* is content, so it is fenced. An MCP *tool result* is
     * deliberately not -- the server was configured by hand by the user, which
     * makes it semi-trusted by the same argument that makes the user's own repo
     * semi-trusted. That line is a judgement; see jc_untrusted.h. */
    {
        struct jc_sb fenced;
        jc_sb_init(&fenced);
        jc_untrusted_wrap("MCP resource", uri, text, &fenced);
        free(text);
        tu_ok_owned(out, jc_sb_finish(&fenced));
        jc_sb_free(&fenced);
    }
    return JC_OK;
}

const struct jc_tool *jc_tool_read_mcp_resource(struct jc_app *app)
{
    struct jc_tool *t;
    if (app == NULL || app->arena == NULL) {
        return NULL;
    }
    t = (struct jc_tool *)jc_arena_calloc(app->arena, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->name = "read_mcp_resource";
    t->description =
        "Read a resource exposed by a connected MCP server (read-only context: "
        "files, docs, records). Pass its uri; the schema lists the available "
        "resources. Returns the resource's text content.";
    t->readonly = 1;
    t->ctx = app;
    t->schema_ctx = mcp_res_schema;
    t->run_ctx = mcp_res_run;
    return t;
}
