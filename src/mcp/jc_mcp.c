/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mcp.c - MCP client manager: connection lifecycle and tool registration.
 *
 * Transport-agnostic. For each configured server it opens a transport (stdio
 * or http), runs the initialize + notifications/initialized + tools/list
 * handshake, and registers every discovered tool into the agent's registry as
 * a dynamic jc_tool whose run_ctx issues a tools/call. The manager owns the
 * connections and the heap-allocated tool/ctx objects and frees them on
 * shutdown; the registry only borrows pointers.
 */

#include "jc_mcp.h"
#include "mcp_internal.h"
#include "jc_app.h"
#include "jc_tool.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define JC_MCP_CLIENT_NAME    "jichi"
#define JC_MCP_CLIENT_VERSION "0.2.0-dev"
#define JC_MCP_FQNAME_CAP     65 /* provider tool-name limit (64) + NUL */

/* ----- connection helpers (shared by the transports) ------------------- */

struct jc_mcp_conn *jc_mcp_conn_alloc(const char *name, volatile int *abort)
{
    struct jc_mcp_conn *c = (struct jc_mcp_conn *)calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->name = jc_strdup(name != NULL ? name : "mcp");
    c->next_id = 1;
    c->tool_count = 0;
    c->vt = NULL;
    c->t = NULL;
    c->abort = abort;
    return c;
}

long jc_mcp_conn_next_id(struct jc_mcp_conn *c)
{
    return c->next_id++;
}

/* ----- dynamic-tool implementation ------------------------------------- */

struct mcp_tool_ctx {
    struct jc_mcp_conn *conn;
    char               *tool_name;   /* remote (un-namespaced) name */
    char               *schema_json; /* serialised inputSchema, or NULL */
    int                 approval;    /* enum jc_mcp_approval */
    int                 kinetic;     /* M163a: server marked kinetic:true */
};

static cJSON *mcp_tool_schema(void *vctx)
{
    struct mcp_tool_ctx *ctx = (struct mcp_tool_ctx *)vctx;
    cJSON *s = NULL;
    if (ctx->schema_json != NULL) {
        s = jc_json_parse(ctx->schema_json);
    }
    if (s == NULL) {
        /* A tool without a declared schema still needs a valid object. */
        s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "type", "object");
        cJSON_AddItemToObject(s, "properties", cJSON_CreateObject());
    }
    return s;
}

/* Deep-copy a cJSON value via serialise + parse (the in-tree cJSON subset has no
 * deep-copy). Returns NULL on failure. */
static cJSON *clone_json(const cJSON *v)
{
    char *s;
    cJSON *copy;
    if (v == NULL) {
        return NULL;
    }
    s = jc_json_print(v);
    if (s == NULL) {
        return NULL;
    }
    copy = jc_json_parse(s);
    free(s);
    return copy;
}

static jc_status mcp_tool_run(void *vctx, const cJSON *args,
                              struct jc_tool_result *out, struct jc_app *app)
{
    struct mcp_tool_ctx *ctx = (struct mcp_tool_ctx *)vctx;
    cJSON *params;
    cJSON *args_copy;
    char *line;
    char *resp = NULL;
    long id;
    jc_status st;

    (void)app;
    out->content = NULL;
    out->is_error = 0;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", ctx->tool_name);
    args_copy = clone_json(args);
    if (args_copy == NULL) {
        args_copy = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(params, "arguments", args_copy);

    id = jc_mcp_conn_next_id(ctx->conn);
    line = jc_mcp_build_request(id, "tools/call", params);
    if (line == NULL) {
        out->content = jc_strdup("error: out of memory building MCP request");
        out->is_error = 1;
        return JC_OK;
    }

    st = ctx->conn->vt->request(ctx->conn, line, id, &resp);
    free(line);

    if (st != JC_OK || resp == NULL) {
        char msg[256];
        jc_snprintf(msg, sizeof(msg),
                    "error: MCP tool call to '%s' failed (%s)",
                    ctx->conn->name, jc_status_str(st));
        out->content = jc_strdup(msg);
        out->is_error = 1;
        free(resp);
        return JC_OK;
    }

    if (jc_mcp_parse_call_result(resp, &out->content, &out->is_error)
            != JC_OK) {
        out->content = jc_strdup("error: malformed MCP tool response");
        out->is_error = 1;
    }
    free(resp);
    return JC_OK;
}

/* Discovered resources/prompts keep a back-pointer to the owning connection so
 * read/get can route to the right server (M43). Strings are heap-owned. */
struct mcp_resource_entry {
    struct jc_mcp_conn *conn;
    char *uri;
    char *name;
    char *description;
    char *mime;
};
struct mcp_prompt_entry {
    struct jc_mcp_conn *conn;
    char *name;
    char *description;
    char **argnames;  /* declared argument names, in order (owned) */
    int    nargs;
};

/* ----- manager --------------------------------------------------------- */

void jc_mcp_manager_init(struct jc_mcp_manager *m, struct jc_app *app)
{
    m->app = app;
    jc_vec_init(&m->conns, sizeof(struct jc_mcp_conn *));
    jc_vec_init(&m->tools, sizeof(struct jc_tool *));
    jc_vec_init(&m->ctxs, sizeof(void *));
    jc_vec_init(&m->resources, sizeof(struct mcp_resource_entry));
    jc_vec_init(&m->prompts, sizeof(struct mcp_prompt_entry));
}

/* Issue a request and return its response message (caller frees *resp). */
static jc_status conn_request(struct jc_mcp_conn *c, const char *method,
                              cJSON *params, char **resp)
{
    long id = jc_mcp_conn_next_id(c);
    char *line = jc_mcp_build_request(id, method, params);
    jc_status st;
    *resp = NULL;
    if (line == NULL) {
        return JC_ERR_OOM;
    }
    st = c->vt->request(c, line, id, resp);
    free(line);
    return st;
}

static jc_status conn_notify(struct jc_mcp_conn *c, const char *method,
                             cJSON *params)
{
    char *line = jc_mcp_build_notification(method, params);
    jc_status st;
    if (line == NULL) {
        return JC_ERR_OOM;
    }
    st = c->vt->notify(c, line);
    free(line);
    return st;
}

/* True if `resp` is a JSON-RPC success (has result, no error). */
static int rpc_ok(const char *resp)
{
    cJSON *root = jc_json_parse(resp);
    int ok;
    if (root == NULL) {
        return 0;
    }
    ok = !cJSON_IsObject(cJSON_GetObjectItem(root, "error")) &&
         cJSON_IsObject(jc_json_get_obj(root, "result"));
    cJSON_Delete(root);
    return ok;
}

/* Run initialize + notifications/initialized. Returns JC_OK on success. */
static jc_status do_initialize(struct jc_mcp_conn *c)
{
    cJSON *params;
    cJSON *client;
    char *resp = NULL;
    jc_status st;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2025-06-18");
    cJSON_AddItemToObject(params, "capabilities", cJSON_CreateObject());
    client = cJSON_CreateObject();
    cJSON_AddStringToObject(client, "name", JC_MCP_CLIENT_NAME);
    cJSON_AddStringToObject(client, "version", JC_MCP_CLIENT_VERSION);
    cJSON_AddItemToObject(params, "clientInfo", client);

    st = conn_request(c, "initialize", params, &resp);
    if (st != JC_OK || resp == NULL || !rpc_ok(resp)) {
        jc_logf(JC_LOG_ERROR, "mcp '%s': initialize failed", c->name);
        free(resp);
        return JC_ERR_PROVIDER;
    }
    free(resp);

    st = conn_notify(c, "notifications/initialized", NULL);
    if (st != JC_OK) {
        jc_logf(JC_LOG_WARN, "mcp '%s': could not send initialized", c->name);
    }
    return JC_OK;
}

/* True if `name` appears in a vec of char*. */
static int name_in_list(const struct jc_vec *list, const char *name)
{
    jc_size i;
    for (i = 0; i < list->len; i++) {
        const char *s = *(char **)jc_vec_at((struct jc_vec *)list, i);
        if (strcmp(s, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Resolve a tool's approval policy from its server config (deny wins). */
static int compute_approval(const struct jc_mcp_server_cfg *cfg,
                            const char *tool_name)
{
    if (cfg->deny_all || name_in_list(&cfg->deny, tool_name)) {
        return JC_MCP_APPROVAL_DENY;
    }
    if (cfg->auto_approve_all || name_in_list(&cfg->auto_approve, tool_name)) {
        return JC_MCP_APPROVAL_ALLOW;
    }
    return JC_MCP_APPROVAL_ASK;
}

/* Register one discovered tool. Returns 1 on success. */
static int register_tool(struct jc_mcp_manager *m, struct jc_mcp_conn *c,
                         const struct jc_mcp_server_cfg *cfg,
                         struct jc_tool_registry *reg,
                         const struct jc_mcp_tool_desc *d)
{
    struct mcp_tool_ctx *ctx;
    struct jc_tool *tool;
    char fq[JC_MCP_FQNAME_CAP];

    ctx = (struct mcp_tool_ctx *)calloc(1, sizeof(*ctx));
    tool = (struct jc_tool *)calloc(1, sizeof(*tool));
    if (ctx == NULL || tool == NULL) {
        free(ctx);
        free(tool);
        return 0;
    }
    ctx->conn = c;
    ctx->tool_name = jc_strdup(d->name);
    ctx->schema_json = (d->input_schema_json != NULL)
                       ? jc_strdup(d->input_schema_json) : NULL;
    ctx->approval = compute_approval(cfg, d->name);
    ctx->kinetic = cfg->kinetic; /* M163a: server-level flag */

    jc_mcp_tool_fqname(c->name, d->name, fq, sizeof(fq));
    tool->name = jc_strdup(fq);
    tool->description = jc_strdup(d->description != NULL ? d->description : "");
    tool->readonly = d->readonly;
    tool->schema = NULL;
    tool->run = NULL;
    tool->ctx = ctx;
    tool->schema_ctx = mcp_tool_schema;
    tool->run_ctx = mcp_tool_run;

    /* The manager always tracks the tool (for policy lookup + cleanup), but a
     * deny'd tool is never registered into the agent registry -- so it is not
     * advertised to the model. jc_mcp_tool_policy still reports DENY for it,
     * backstopping a model that names it anyway. */
    jc_vec_push(&m->tools, &tool);
    {
        void *cp = ctx;
        jc_vec_push(&m->ctxs, &cp);
    }
    if (ctx->approval != JC_MCP_APPROVAL_DENY) {
        jc_tool_registry_register(reg, tool);
    } else {
        jc_logf(JC_LOG_DEBUG, "mcp '%s': hiding deny'd tool '%s'",
                c->name, d->name);
    }
    return 1;
}

/* Connect and register tools for one server. Returns the number registered. */
static int connect_one(struct jc_mcp_manager *m,
                       const struct jc_mcp_server_cfg *cfg,
                       struct jc_tool_registry *reg)
{
    struct jc_mcp_conn *c = NULL;
    volatile int *abort = (m->app != NULL) ? &m->app->abort_flag : NULL;
    char *resp = NULL;
    struct jc_vec tools;
    jc_status st;
    int count = 0;
    jc_size i;
    int is_http = (cfg->type != NULL && strcmp(cfg->type, "http") == 0);

    st = is_http ? jc_mcp_http_open(&c, cfg, abort)
                 : jc_mcp_stdio_open(&c, cfg, abort);
    if (st != JC_OK || c == NULL) {
        jc_logf(JC_LOG_WARN, "mcp '%s': could not open transport", cfg->name);
        return 0;
    }

    if (do_initialize(c) != JC_OK) {
        c->vt->close(c);
        free(c->name);
        free(c);
        return 0;
    }

    st = conn_request(c, "tools/list", NULL, &resp);
    if (st != JC_OK || resp == NULL) {
        jc_logf(JC_LOG_WARN, "mcp '%s': tools/list failed", cfg->name);
        free(resp);
        c->vt->close(c);
        free(c->name);
        free(c);
        return 0;
    }

    jc_vec_init(&tools, sizeof(struct jc_mcp_tool_desc));
    if (jc_mcp_parse_tools(resp, &tools) != JC_OK) {
        jc_logf(JC_LOG_WARN, "mcp '%s': could not parse tools", cfg->name);
    }
    free(resp);

    for (i = 0; i < tools.len; i++) {
        struct jc_mcp_tool_desc *d =
            (struct jc_mcp_tool_desc *)jc_vec_at(&tools, i);
        if (register_tool(m, c, cfg, reg, d)) {
            count++;
        }
        jc_mcp_tool_desc_free(d);
    }
    jc_vec_free(&tools);

    /* Discover resources + prompts (optional MCP capabilities; a server that
     * lacks them answers with a JSON-RPC error, which we tolerate + skip). */
    {
        char *rr = NULL;
        struct jc_vec rs;
        jc_size j;
        jc_vec_init(&rs, sizeof(struct jc_mcp_resource_desc));
        if (conn_request(c, "resources/list", NULL, &rr) == JC_OK &&
            rr != NULL && jc_mcp_parse_resources(rr, &rs) == JC_OK) {
            for (j = 0; j < rs.len; j++) {
                struct jc_mcp_resource_desc *d =
                    (struct jc_mcp_resource_desc *)jc_vec_at(&rs, j);
                struct mcp_resource_entry e;
                e.conn = c;
                e.uri = d->uri; d->uri = NULL;          /* transfer ownership */
                e.name = d->name; d->name = NULL;
                e.description = d->description; d->description = NULL;
                e.mime = d->mime_type; d->mime_type = NULL;
                jc_vec_push(&m->resources, &e);
            }
        }
        for (j = 0; j < rs.len; j++) {
            jc_mcp_resource_desc_free(
                (struct jc_mcp_resource_desc *)jc_vec_at(&rs, j));
        }
        jc_vec_free(&rs);
        free(rr);
    }
    {
        char *pr = NULL;
        struct jc_vec ps;
        jc_size j;
        jc_vec_init(&ps, sizeof(struct jc_mcp_prompt_desc));
        if (conn_request(c, "prompts/list", NULL, &pr) == JC_OK &&
            pr != NULL && jc_mcp_parse_prompts(pr, &ps) == JC_OK) {
            for (j = 0; j < ps.len; j++) {
                struct jc_mcp_prompt_desc *d =
                    (struct jc_mcp_prompt_desc *)jc_vec_at(&ps, j);
                struct mcp_prompt_entry e;
                e.conn = c;
                e.name = d->name; d->name = NULL;
                e.description = d->description; d->description = NULL;
                e.argnames = NULL;
                e.nargs = 0;
                if (d->nargs > 0) {
                    e.argnames = (char **)calloc((size_t)d->nargs,
                                                 sizeof(char *));
                    if (e.argnames != NULL) {
                        int k;
                        for (k = 0; k < d->nargs; k++) {
                            e.argnames[e.nargs] = d->args[k].name;
                            d->args[k].name = NULL; /* ownership moved */
                            e.nargs++;
                        }
                    }
                }
                jc_vec_push(&m->prompts, &e);
            }
        }
        for (j = 0; j < ps.len; j++) {
            jc_mcp_prompt_desc_free(
                (struct jc_mcp_prompt_desc *)jc_vec_at(&ps, j));
        }
        jc_vec_free(&ps);
        free(pr);
    }

    c->tool_count = count;
    jc_vec_push(&m->conns, &c);
    jc_logf(JC_LOG_INFO, "mcp '%s': connected, %d tool(s)", cfg->name, count);
    return count;
}

int jc_mcp_connect_all(struct jc_mcp_manager *m, struct jc_config *cfg,
                       struct jc_tool_registry *reg)
{
    jc_size i;
    int total = 0;
    for (i = 0; i < cfg->mcp_servers.len; i++) {
        struct jc_mcp_server_cfg *s =
            (struct jc_mcp_server_cfg *)jc_vec_at(&cfg->mcp_servers, i);
        total += connect_one(m, s, reg);
    }
    return total;
}

int jc_mcp_tool_policy(const struct jc_mcp_manager *m, const char *fqname)
{
    jc_size i;
    if (m == NULL || fqname == NULL) {
        return JC_MCP_APPROVAL_ASK;
    }
    for (i = 0; i < m->tools.len; i++) {
        struct jc_tool *t =
            *(struct jc_tool **)jc_vec_at((struct jc_vec *)&m->tools, i);
        if (t->name != NULL && strcmp(t->name, fqname) == 0) {
            return ((struct mcp_tool_ctx *)t->ctx)->approval;
        }
    }
    return JC_MCP_APPROVAL_ASK;
}

int jc_mcp_tool_kinetic(const struct jc_mcp_manager *m, const char *fqname)
{
    jc_size i;
    if (m == NULL || fqname == NULL) {
        return 0;
    }
    for (i = 0; i < m->tools.len; i++) {
        struct jc_tool *t =
            *(struct jc_tool **)jc_vec_at((struct jc_vec *)&m->tools, i);
        if (t->name != NULL && strcmp(t->name, fqname) == 0) {
            return ((struct mcp_tool_ctx *)t->ctx)->kinetic;
        }
    }
    return 0;
}

int jc_mcp_tool_count(const struct jc_mcp_manager *m)
{
    return (int)m->tools.len;
}

const char *jc_mcp_tool_at(const struct jc_mcp_manager *m, int i,
                           int *policy_out, const char **desc_out)
{
    struct jc_tool *t;
    if (i < 0 || (jc_size)i >= m->tools.len) {
        return NULL;
    }
    t = *(struct jc_tool **)jc_vec_at((struct jc_vec *)&m->tools, (jc_size)i);
    if (policy_out != NULL) {
        *policy_out = ((struct mcp_tool_ctx *)t->ctx)->approval;
    }
    if (desc_out != NULL) {
        *desc_out = t->description;
    }
    return t->name;
}

int jc_mcp_resource_count(const struct jc_mcp_manager *m)
{
    return (int)m->resources.len;
}

const char *jc_mcp_resource_at(const struct jc_mcp_manager *m, int i,
                               const char **server_out, const char **desc_out)
{
    struct mcp_resource_entry *e;
    if (i < 0 || (jc_size)i >= m->resources.len) {
        return NULL;
    }
    e = (struct mcp_resource_entry *)jc_vec_at((struct jc_vec *)&m->resources,
                                               (jc_size)i);
    if (server_out != NULL) {
        *server_out = (e->conn != NULL) ? e->conn->name : "";
    }
    if (desc_out != NULL) {
        *desc_out = (e->description != NULL) ? e->description : "";
    }
    return e->uri;
}

jc_status jc_mcp_read_resource(struct jc_mcp_manager *m, const char *uri,
                               char **text_out)
{
    jc_size i;
    if (text_out != NULL) {
        *text_out = NULL;
    }
    if (m == NULL || uri == NULL || text_out == NULL) {
        return JC_ERR_INVALID;
    }
    for (i = 0; i < m->resources.len; i++) {
        struct mcp_resource_entry *e =
            (struct mcp_resource_entry *)jc_vec_at(&m->resources, i);
        if (e->uri != NULL && strcmp(e->uri, uri) == 0) {
            cJSON *p = cJSON_CreateObject();
            char *resp = NULL;
            jc_status st;
            cJSON_AddStringToObject(p, "uri", uri);
            st = conn_request(e->conn, "resources/read", p, &resp);
            if (st != JC_OK || resp == NULL) {
                free(resp);
                return (st != JC_OK) ? st : JC_ERR_PROVIDER;
            }
            st = jc_mcp_parse_resource_read(resp, text_out);
            free(resp);
            return st;
        }
    }
    return JC_ERR_INVALID; /* no such resource */
}

int jc_mcp_prompt_count(const struct jc_mcp_manager *m)
{
    return (int)m->prompts.len;
}

const char *jc_mcp_prompt_at(const struct jc_mcp_manager *m, int i,
                             const char **server_out, const char **desc_out)
{
    struct mcp_prompt_entry *e;
    if (i < 0 || (jc_size)i >= m->prompts.len) {
        return NULL;
    }
    e = (struct mcp_prompt_entry *)jc_vec_at((struct jc_vec *)&m->prompts,
                                             (jc_size)i);
    if (server_out != NULL) {
        *server_out = (e->conn != NULL) ? e->conn->name : "";
    }
    if (desc_out != NULL) {
        *desc_out = (e->description != NULL) ? e->description : "";
    }
    return e->name;
}

jc_status jc_mcp_get_prompt_args(struct jc_mcp_manager *m, const char *name,
                                 const char *raw_args, char **text_out)
{
    jc_size i;
    if (text_out != NULL) {
        *text_out = NULL;
    }
    if (m == NULL || name == NULL || text_out == NULL) {
        return JC_ERR_INVALID;
    }
    for (i = 0; i < m->prompts.len; i++) {
        struct mcp_prompt_entry *e =
            (struct mcp_prompt_entry *)jc_vec_at(&m->prompts, i);
        if (e->name != NULL && strcmp(e->name, name) == 0) {
            cJSON *p = cJSON_CreateObject();
            cJSON *args;
            char *resp = NULL;
            jc_status st;
            cJSON_AddStringToObject(p, "name", name);
            args = jc_mcp_build_prompt_args((const char *const *)e->argnames,
                                            e->nargs, raw_args);
            cJSON_AddItemToObject(p, "arguments",
                                  args != NULL ? args : cJSON_CreateObject());
            st = conn_request(e->conn, "prompts/get", p, &resp);
            if (st != JC_OK || resp == NULL) {
                free(resp);
                return (st != JC_OK) ? st : JC_ERR_PROVIDER;
            }
            st = jc_mcp_parse_prompt_get(resp, text_out);
            free(resp);
            return st;
        }
    }
    return JC_ERR_INVALID; /* no such prompt */
}

jc_status jc_mcp_get_prompt(struct jc_mcp_manager *m, const char *name,
                            char **text_out)
{
    return jc_mcp_get_prompt_args(m, name, NULL, text_out);
}

int jc_mcp_server_count(const struct jc_mcp_manager *m)
{
    return (int)m->conns.len;
}

const char *jc_mcp_server_name(const struct jc_mcp_manager *m, int i,
                               int *tool_count_out)
{
    struct jc_mcp_conn *c;
    if (i < 0 || (jc_size)i >= m->conns.len) {
        return NULL;
    }
    c = *(struct jc_mcp_conn **)jc_vec_at((struct jc_vec *)&m->conns,
                                          (jc_size)i);
    if (tool_count_out != NULL) {
        *tool_count_out = c->tool_count;
    }
    return c->name;
}

void jc_mcp_manager_shutdown(struct jc_mcp_manager *m)
{
    jc_size i;

    for (i = 0; i < m->conns.len; i++) {
        struct jc_mcp_conn *c =
            *(struct jc_mcp_conn **)jc_vec_at(&m->conns, i);
        if (c->vt != NULL && c->vt->close != NULL) {
            c->vt->close(c);
        }
        free(c->name);
        free(c);
    }
    for (i = 0; i < m->tools.len; i++) {
        struct jc_tool *t = *(struct jc_tool **)jc_vec_at(&m->tools, i);
        /* name/description were malloc'd by register_tool. */
        free((void *)t->name);
        free((void *)t->description);
        free(t);
    }
    for (i = 0; i < m->ctxs.len; i++) {
        struct mcp_tool_ctx *ctx =
            *(struct mcp_tool_ctx **)jc_vec_at(&m->ctxs, i);
        free(ctx->tool_name);
        free(ctx->schema_json);
        free(ctx);
    }
    for (i = 0; i < m->resources.len; i++) {
        struct mcp_resource_entry *e =
            (struct mcp_resource_entry *)jc_vec_at(&m->resources, i);
        free(e->uri);
        free(e->name);
        free(e->description);
        free(e->mime);
    }
    for (i = 0; i < m->prompts.len; i++) {
        struct mcp_prompt_entry *e =
            (struct mcp_prompt_entry *)jc_vec_at(&m->prompts, i);
        int k;
        free(e->name);
        free(e->description);
        for (k = 0; k < e->nargs; k++) {
            free(e->argnames[k]);
        }
        free(e->argnames);
    }
    jc_vec_free(&m->conns);
    jc_vec_free(&m->tools);
    jc_vec_free(&m->ctxs);
    jc_vec_free(&m->resources);
    jc_vec_free(&m->prompts);
}
