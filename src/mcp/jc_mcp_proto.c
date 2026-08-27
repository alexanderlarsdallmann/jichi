/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mcp_proto.c - pure JSON-RPC 2.0 / MCP framing and result parsing.
 *
 * No I/O: every function here works on in-memory strings and cJSON trees, so
 * the protocol is exercised offline by the test suite. Transports (stdio,
 * http) provide the bytes; this file turns them into requests and decodes the
 * responses.
 */

#include "jc_mcp.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

void jc_mcp_tool_desc_free(struct jc_mcp_tool_desc *d)
{
    if (d == NULL) {
        return;
    }
    free(d->name);
    free(d->description);
    free(d->input_schema_json);
    d->name = NULL;
    d->description = NULL;
    d->input_schema_json = NULL;
}

void jc_mcp_resource_desc_free(struct jc_mcp_resource_desc *d)
{
    if (d == NULL) {
        return;
    }
    free(d->uri);
    free(d->name);
    free(d->description);
    free(d->mime_type);
    d->uri = NULL;
    d->name = NULL;
    d->description = NULL;
    d->mime_type = NULL;
}

void jc_mcp_prompt_desc_free(struct jc_mcp_prompt_desc *d)
{
    int i;
    if (d == NULL) {
        return;
    }
    free(d->name);
    free(d->description);
    for (i = 0; i < d->nargs; i++) {
        free(d->args[i].name);
    }
    free(d->args);
    d->name = NULL;
    d->description = NULL;
    d->args = NULL;
    d->nargs = 0;
}

/* Common envelope builder; `params` is consumed. */
static char *build_message(long id, int is_request, const char *method,
                           cJSON *params)
{
    cJSON *root;
    char *out;

    root = cJSON_CreateObject();
    if (root == NULL) {
        if (params != NULL) {
            cJSON_Delete(params);
        }
        return NULL;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (is_request) {
        cJSON_AddNumberToObject(root, "id", (double)id);
    }
    cJSON_AddStringToObject(root, "method", method);
    if (params == NULL) {
        params = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(root, "params", params);

    out = jc_json_print(root);
    cJSON_Delete(root);
    return out;
}

char *jc_mcp_build_request(long id, const char *method, cJSON *params)
{
    return build_message(id, 1, method, params);
}

char *jc_mcp_build_notification(const char *method, cJSON *params)
{
    return build_message(0, 0, method, params);
}

int jc_mcp_message_id(const char *json, long *id_out)
{
    cJSON *root;
    cJSON *id;
    int ok = 0;

    root = jc_json_parse(json);
    if (root == NULL) {
        return 0;
    }
    id = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsNumber(id)) {
        if (id_out != NULL) {
            *id_out = (long)id->valuedouble;
        }
        ok = 1;
    }
    cJSON_Delete(root);
    return ok;
}

/* Format a JSON-RPC error object into `sb`. */
static void append_rpc_error(struct jc_sb *sb, const cJSON *err)
{
    const char *msg = jc_json_get_str(err, "message", "unknown error");
    double code = jc_json_get_num(err, "code", 0.0);
    jc_sb_append_fmt(sb, "MCP error %ld: %s", (long)code, msg);
}

jc_status jc_mcp_parse_tools(const char *resp_json, struct jc_vec *out)
{
    cJSON *root;
    cJSON *result;
    cJSON *tools;
    cJSON *t;

    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    if (cJSON_IsObject(cJSON_GetObjectItem(root, "error"))) {
        cJSON_Delete(root);
        return JC_ERR_PROVIDER;
    }
    result = jc_json_get_obj(root, "result");
    if (!cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    tools = jc_json_get_obj(result, "tools");
    if (!cJSON_IsArray(tools)) {
        cJSON_Delete(root); /* a server with no tools is valid */
        return JC_OK;
    }
    cJSON_ArrayForEach(t, tools) {
        struct jc_mcp_tool_desc d;
        const char *name;
        const char *desc;
        cJSON *schema;
        cJSON *ann;

        if (!cJSON_IsObject(t)) {
            continue;
        }
        name = jc_json_get_str(t, "name", NULL);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        memset(&d, 0, sizeof(d));
        d.name = jc_strdup(name);
        desc = jc_json_get_str(t, "description", "");
        d.description = jc_strdup(desc);
        schema = jc_json_get_obj(t, "inputSchema");
        d.input_schema_json = cJSON_IsObject(schema) ? jc_json_print(schema)
                                                     : NULL;
        ann = jc_json_get_obj(t, "annotations");
        d.readonly = cJSON_IsObject(ann)
                     ? jc_json_get_bool_lenient(ann, "readOnlyHint", 0) : 0;
        jc_vec_push(out, &d);
    }
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_mcp_parse_call_result(const char *resp_json, char **text_out,
                                   int *is_error_out)
{
    cJSON *root;
    cJSON *err;
    cJSON *result;
    cJSON *content;
    cJSON *block;
    struct jc_sb sb;
    int is_error = 0;

    *text_out = NULL;
    *is_error_out = 0;

    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    jc_sb_init(&sb);

    err = jc_json_get_obj(root, "error");
    if (cJSON_IsObject(err)) {
        append_rpc_error(&sb, err);
        *text_out = jc_sb_finish(&sb);
        *is_error_out = 1;
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_OK;
    }

    result = jc_json_get_obj(root, "result");
    if (!cJSON_IsObject(result)) {
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    is_error = jc_json_get_bool_lenient(result, "isError", 0);

    content = jc_json_get_obj(result, "content");
    if (cJSON_IsArray(content)) {
        cJSON_ArrayForEach(block, content) {
            const char *type = jc_json_get_str(block, "type", "");
            if (strcmp(type, "text") == 0) {
                const char *txt = jc_json_get_str(block, "text", "");
                if (sb.len > 0) {
                    jc_sb_append_char(&sb, '\n');
                }
                jc_sb_append(&sb, txt);
            } else if (type[0] != '\0') {
                /* Non-text content (image/resource): note its presence. */
                if (sb.len > 0) {
                    jc_sb_append_char(&sb, '\n');
                }
                jc_sb_append_fmt(&sb, "[%s content omitted]", type);
            }
        }
    }
    if (sb.len == 0) {
        jc_sb_append(&sb, "(no content)");
    }

    *text_out = jc_sb_finish(&sb);
    *is_error_out = is_error;
    jc_sb_free(&sb);
    cJSON_Delete(root);
    return JC_OK;
}

/* Shared head: parse `resp_json`, reject a JSON-RPC error, return result[key]
 * array via *arr_out with *root_out to delete. Returns JC_OK / JC_ERR_PROVIDER
 * / JC_ERR_PARSE; on a non-OK return nothing needs freeing. */
static jc_status result_array(const char *resp_json, const char *key,
                              cJSON **root_out, cJSON **arr_out)
{
    cJSON *root = jc_json_parse(resp_json);
    cJSON *result;
    cJSON *arr;
    *root_out = NULL;
    *arr_out = NULL;
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    if (cJSON_IsObject(cJSON_GetObjectItem(root, "error"))) {
        cJSON_Delete(root);
        return JC_ERR_PROVIDER;
    }
    result = jc_json_get_obj(root, "result");
    if (!cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    arr = jc_json_get_obj(result, key);
    *root_out = root;
    *arr_out = cJSON_IsArray(arr) ? arr : NULL; /* a server with none is valid */
    return JC_OK;
}

jc_status jc_mcp_parse_resources(const char *resp_json, struct jc_vec *out)
{
    cJSON *root;
    cJSON *arr;
    cJSON *r;
    jc_status st = result_array(resp_json, "resources", &root, &arr);
    if (st != JC_OK) {
        return st;
    }
    if (arr != NULL) {
        cJSON_ArrayForEach(r, arr) {
            struct jc_mcp_resource_desc d;
            const char *uri;
            if (!cJSON_IsObject(r)) {
                continue;
            }
            uri = jc_json_get_str(r, "uri", NULL);
            if (uri == NULL || uri[0] == '\0') {
                continue;
            }
            memset(&d, 0, sizeof(d));
            d.uri = jc_strdup(uri);
            d.name = jc_strdup(jc_json_get_str(r, "name", ""));
            d.description = jc_strdup(jc_json_get_str(r, "description", ""));
            d.mime_type = jc_strdup(jc_json_get_str(r, "mimeType", ""));
            jc_vec_push(out, &d);
        }
    }
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_mcp_parse_prompts(const char *resp_json, struct jc_vec *out)
{
    cJSON *root;
    cJSON *arr;
    cJSON *p;
    jc_status st = result_array(resp_json, "prompts", &root, &arr);
    if (st != JC_OK) {
        return st;
    }
    if (arr != NULL) {
        cJSON_ArrayForEach(p, arr) {
            struct jc_mcp_prompt_desc d;
            const char *name;
            if (!cJSON_IsObject(p)) {
                continue;
            }
            name = jc_json_get_str(p, "name", NULL);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            memset(&d, 0, sizeof(d));
            d.name = jc_strdup(name);
            d.description = jc_strdup(jc_json_get_str(p, "description", ""));
            {
                cJSON *as = jc_json_get_obj(p, "arguments");
                cJSON *a;
                int na = 0;
                if (cJSON_IsArray(as)) {
                    na = cJSON_GetArraySize(as);
                }
                if (na > 0) {
                    d.args = (struct jc_mcp_prompt_arg *)
                        calloc((size_t)na, sizeof(struct jc_mcp_prompt_arg));
                }
                if (d.args != NULL) {
                    cJSON_ArrayForEach(a, as) {
                        const char *an;
                        cJSON *req;
                        if (!cJSON_IsObject(a)) {
                            continue;
                        }
                        an = jc_json_get_str(a, "name", NULL);
                        if (an == NULL || an[0] == '\0') {
                            continue;
                        }
                        req = jc_json_get_obj(a, "required");
                        d.args[d.nargs].name = jc_strdup(an);
                        d.args[d.nargs].required =
                            cJSON_IsBool(req) ? cJSON_IsTrue(req) : 0;
                        d.nargs++;
                    }
                }
            }
            jc_vec_push(out, &d);
        }
    }
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_mcp_parse_resource_read(const char *resp_json, char **text_out)
{
    cJSON *root;
    cJSON *err;
    cJSON *result;
    cJSON *contents;
    cJSON *block;
    struct jc_sb sb;

    *text_out = NULL;
    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    jc_sb_init(&sb);
    err = jc_json_get_obj(root, "error");
    if (cJSON_IsObject(err)) {
        append_rpc_error(&sb, err);
        *text_out = jc_sb_finish(&sb);
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_OK;
    }
    result = jc_json_get_obj(root, "result");
    if (!cJSON_IsObject(result)) {
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    contents = jc_json_get_obj(result, "contents");
    if (cJSON_IsArray(contents)) {
        cJSON_ArrayForEach(block, contents) {
            const char *txt = jc_json_get_str(block, "text", NULL);
            if (sb.len > 0) {
                jc_sb_append_char(&sb, '\n');
            }
            if (txt != NULL) {
                jc_sb_append(&sb, txt);
            } else {
                jc_sb_append(&sb, "[binary resource content omitted]");
            }
        }
    }
    if (sb.len == 0) {
        jc_sb_append(&sb, "(empty resource)");
    }
    *text_out = jc_sb_finish(&sb);
    jc_sb_free(&sb);
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_mcp_parse_prompt_get(const char *resp_json, char **text_out)
{
    cJSON *root;
    cJSON *err;
    cJSON *result;
    cJSON *messages;
    cJSON *msg;
    struct jc_sb sb;

    *text_out = NULL;
    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    jc_sb_init(&sb);
    err = jc_json_get_obj(root, "error");
    if (cJSON_IsObject(err)) {
        append_rpc_error(&sb, err);
        *text_out = jc_sb_finish(&sb);
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_OK;
    }
    result = jc_json_get_obj(root, "result");
    if (!cJSON_IsObject(result)) {
        jc_sb_free(&sb);
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    messages = jc_json_get_obj(result, "messages");
    if (cJSON_IsArray(messages)) {
        cJSON_ArrayForEach(msg, messages) {
            const char *role = jc_json_get_str(msg, "role", "");
            cJSON *content = jc_json_get_obj(msg, "content");
            const char *txt = NULL;
            if (cJSON_IsObject(content)) {
                txt = jc_json_get_str(content, "text", NULL);
            }
            if (sb.len > 0) {
                jc_sb_append_char(&sb, '\n');
            }
            jc_sb_append_fmt(&sb, "%s: %s", role[0] != '\0' ? role : "user",
                             txt != NULL ? txt : "[non-text content]");
        }
    }
    if (sb.len == 0) {
        jc_sb_append(&sb, "(empty prompt)");
    }
    *text_out = jc_sb_finish(&sb);
    jc_sb_free(&sb);
    cJSON_Delete(root);
    return JC_OK;
}

/* Set obj[key] = val (string), replacing any existing entry. */
static void prompt_arg_set(cJSON *obj, const char *key, const char *val)
{
    cJSON *str = cJSON_CreateString(val);
    if (str == NULL) {
        return;
    }
    if (cJSON_GetObjectItemCaseSensitive(obj, key) != NULL) {
        cJSON_ReplaceItemInObject(obj, key, str);
    } else {
        cJSON_AddItemToObject(obj, key, str);
    }
}

cJSON *jc_mcp_build_prompt_args(const char *const *argnames, int nargnames,
                                const char *raw)
{
    cJSON *obj = cJSON_CreateObject();
    int next = 0; /* next declared-arg index a positional may fill */
    const char *s;

    if (obj == NULL) {
        return NULL;
    }
    if (raw == NULL) {
        return obj;
    }
    s = raw;
    while (*s != '\0') {
        const char *start;
        const char *eq;
        const char *t;
        jc_size len;

        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        start = s;
        while (*s != '\0' && *s != ' ' && *s != '\t' &&
               *s != '\n' && *s != '\r') {
            s++;
        }
        len = (jc_size)(s - start);

        eq = NULL;
        for (t = start; t < s; t++) {
            if (*t == '=') {
                eq = t;
                break;
            }
        }

        if (eq != NULL && eq > start) {
            /* key=value: set by name (declared or not). */
            char *key = (char *)malloc((jc_size)(eq - start) + 1);
            char *val = (char *)malloc((jc_size)(s - (eq + 1)) + 1);
            if (key != NULL && val != NULL) {
                memcpy(key, start, (jc_size)(eq - start));
                key[eq - start] = '\0';
                memcpy(val, eq + 1, (jc_size)(s - (eq + 1)));
                val[s - (eq + 1)] = '\0';
                prompt_arg_set(obj, key, val);
            }
            free(key);
            free(val);
        } else {
            /* positional: fill the next declared arg not already set. */
            while (next < nargnames && argnames != NULL &&
                   (argnames[next] == NULL ||
                    cJSON_GetObjectItemCaseSensitive(obj, argnames[next])
                        != NULL)) {
                next++;
            }
            if (next < nargnames && argnames != NULL &&
                argnames[next] != NULL) {
                char *val = (char *)malloc(len + 1);
                if (val != NULL) {
                    memcpy(val, start, len);
                    val[len] = '\0';
                    prompt_arg_set(obj, argnames[next], val);
                    free(val);
                }
                next++;
            }
            /* extra positional beyond the declared set: ignored */
        }
    }
    return obj;
}

/* Append `c` to buf[*pos] if it fits and is allowed, else '_'. */
static void put_sanitized(char *buf, jc_size cap, jc_size *pos, char c)
{
    int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (*pos + 1 >= cap) {
        return;
    }
    buf[(*pos)++] = ok ? c : '_';
}

void jc_mcp_tool_fqname(const char *server, const char *tool, char *buf,
                        jc_size cap)
{
    jc_size pos = 0;
    const char *p;

    if (cap == 0) {
        return;
    }
    for (p = server; p != NULL && *p != '\0'; p++) {
        put_sanitized(buf, cap, &pos, *p);
    }
    if (pos + 2 < cap) {
        buf[pos++] = '_';
        buf[pos++] = '_';
    }
    for (p = tool; p != NULL && *p != '\0'; p++) {
        put_sanitized(buf, cap, &pos, *p);
    }
    buf[pos] = '\0';
}
