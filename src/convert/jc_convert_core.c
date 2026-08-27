/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_core.c - source-format detection, the neutral IR, shared model
 * helpers, and the IR -> jichi config emitter. Per-source mapping lives in
 * jc_convert_continue.c and jc_convert_opencode.c. */

#include "jc_convert.h"
#include "convert_internal.h"
#include "jc_yaml.h"
#include "jc_json.h"
#include "jc_jsonc.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* --- IR bookkeeping --- */

void jc_ir_init(struct jc_ir *ir, struct jc_arena *a)
{
    memset(ir, 0, sizeof(*ir));
    ir->a = a;
    ir->active_model = 0;
    ir->mode = NULL;
}

void jc_ir_warn(struct jc_ir *ir, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (ir->warning_count >= JC_IR_MAX_LIST) {
        return;
    }
    va_start(ap, fmt);
    jc_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ir->warnings[ir->warning_count++] = jc_arena_strdup(ir->a, buf);
}

struct jc_ir_model *jc_ir_new_model(struct jc_ir *ir)
{
    struct jc_ir_model *m;
    if (ir->model_count >= JC_IR_MAX_MODELS) {
        return NULL;
    }
    m = (struct jc_ir_model *)jc_arena_calloc(ir->a, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    ir->models[ir->model_count++] = m;
    return m;
}

struct jc_ir_mcp *jc_ir_new_mcp(struct jc_ir *ir)
{
    struct jc_ir_mcp *m;
    if (ir->mcp_count >= JC_IR_MAX_MCP) {
        return NULL;
    }
    m = (struct jc_ir_mcp *)jc_arena_calloc(ir->a, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    ir->mcp[ir->mcp_count++] = m;
    return m;
}

struct jc_ir_lsp *jc_ir_new_lsp(struct jc_ir *ir)
{
    struct jc_ir_lsp *l;
    if (ir->lsp_count >= JC_IR_MAX_LSP) {
        return NULL;
    }
    l = (struct jc_ir_lsp *)jc_arena_calloc(ir->a, sizeof(*l));
    if (l == NULL) {
        return NULL;
    }
    ir->lsp[ir->lsp_count++] = l;
    return l;
}

void jc_ir_model_add_role(struct jc_ir_model *m, const char *role)
{
    int i;
    if (role == NULL || role[0] == '\0' || m->role_count >= JC_IR_MAX_ROLES) {
        return;
    }
    for (i = 0; i < m->role_count; i++) {
        if (strcmp(m->roles[i], role) == 0) {
            return;
        }
    }
    m->roles[m->role_count++] = role;
}

/* --- shared provider/key mapping --- */

const char *jc_convert_map_provider(const char *p, int *mapped)
{
    *mapped = 0;
    if (p == NULL) {
        return "anthropic";
    }
    if (strcmp(p, "anthropic") == 0) {
        return "anthropic";
    }
    if (strcmp(p, "openai") == 0) {
        return "openai";
    }
    *mapped = 1;
    return "openai";
}

const char *jc_convert_provider_key_env(const char *provider)
{
    if (provider != NULL && strcmp(provider, "anthropic") == 0) {
        return "ANTHROPIC_API_KEY";
    }
    return "OPENAI_API_KEY";
}

int jc_convert_key_is_literal(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return 0;
    }
    if (strstr(key, "${") != NULL) {
        return 0;          /* Continue ${{ secrets.X }} template   */
    }
    if (key[0] == '$') {
        return 0;          /* $VAR reference                       */
    }
    if (strncmp(key, "{env:", 5) == 0) {
        return 0;          /* opencode {env:VAR} reference         */
    }
    return 1;
}

void jc_convert_fill_provider(struct jc_ir *ir, struct jc_ir_model *m,
                              const char *raw_provider, const char *raw_key,
                              const char *raw_key_env)
{
    int mapped = 0;
    const char *prov = jc_convert_map_provider(raw_provider, &mapped);
    m->provider = prov;
    if (mapped) {
        jc_ir_warn(ir, "provider '%s' is not native; mapped to 'openai' "
                       "(OpenAI-compatible). Verify apiBase.",
                   raw_provider ? raw_provider : "(none)");
    }
    if (jc_convert_key_is_literal(raw_key)) {
        m->api_key = jc_arena_strdup(ir->a, raw_key);
    } else if (raw_key_env != NULL && raw_key_env[0] != '\0') {
        m->api_key_env = jc_arena_strdup(ir->a, raw_key_env);
    } else {
        m->api_key_env = jc_convert_provider_key_env(prov);
    }
}

const char *jc_convert_slug(struct jc_arena *a, const char *name)
{
    struct jc_sb sb;
    char *out;
    const char *p;
    int prev_dash = 1; /* trim leading dashes */

    jc_sb_init(&sb);
    if (name != NULL) {
        for (p = name; *p != '\0'; p++) {
            char c = *p;
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                jc_sb_append_char(&sb, c);
                prev_dash = 0;
            } else if (!prev_dash) {
                jc_sb_append_char(&sb, '-');
                prev_dash = 1;
            }
        }
    }
    /* trim a trailing dash */
    if (sb.data != NULL && sb.len > 0 && sb.data[sb.len - 1] == '-') {
        sb.data[--sb.len] = '\0';
    }
    out = jc_arena_strdup(a, (sb.data != NULL && sb.len > 0) ? sb.data
                                                             : "item");
    jc_sb_free(&sb);
    return out;
}

/* --- IR -> config emitter --- */

static cJSON *build_model_obj(const struct jc_ir_model *m)
{
    cJSON *o = cJSON_CreateObject();
    if (m->name != NULL && m->name[0] != '\0') {
        cJSON_AddStringToObject(o, "name", m->name);
    }
    cJSON_AddStringToObject(o, "provider", m->provider ? m->provider : "openai");
    cJSON_AddStringToObject(o, "model", m->model ? m->model : "");
    if (m->api_base != NULL && m->api_base[0] != '\0') {
        cJSON_AddStringToObject(o, "apiBase", m->api_base);
    }
    if (m->api_key != NULL) {
        cJSON_AddStringToObject(o, "apiKey", m->api_key);
    } else if (m->api_key_env != NULL) {
        cJSON_AddStringToObject(o, "apiKeyEnv", m->api_key_env);
    }
    if (m->has_max && m->max_tokens > 0) {
        cJSON_AddNumberToObject(o, "maxTokens", (double)m->max_tokens);
    }
    if (m->has_temp) {
        cJSON_AddNumberToObject(o, "temperature", m->temperature);
    }
    if (m->has_ctx && m->context_length > 0) {
        cJSON_AddNumberToObject(o, "contextLength", (double)m->context_length);
    }
    if (m->role_count > 0) {
        cJSON *roles = cJSON_AddArrayToObject(o, "roles");
        int i;
        for (i = 0; i < m->role_count; i++) {
            cJSON_AddItemToArray(roles, cJSON_CreateString(m->roles[i]));
        }
    }
    return o;
}

static void add_str_array(cJSON *obj, const char *key,
                          const char *const *items, int n)
{
    cJSON *arr;
    int i;
    if (n <= 0) {
        return;
    }
    arr = cJSON_AddArrayToObject(obj, key);
    for (i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(items[i]));
    }
}

static cJSON *build_mcp_obj(const struct jc_ir_mcp *m)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", m->name ? m->name : "");
    if (m->url != NULL) {
        cJSON_AddStringToObject(o, "url", m->url);
        add_str_array(o, "headers", m->headers, m->header_count);
    } else {
        if (m->command != NULL) {
            cJSON_AddStringToObject(o, "command", m->command);
        }
        add_str_array(o, "args", m->args, m->arg_count);
        if (m->env_count > 0) {
            cJSON *env = cJSON_AddObjectToObject(o, "env");
            int i;
            for (i = 0; i < m->env_count; i++) {
                cJSON_AddStringToObject(env, m->env_keys[i], m->env_vals[i]);
            }
        }
    }
    return o;
}

static cJSON *build_lsp_obj(const struct jc_ir_lsp *l)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", l->name ? l->name : "");
    if (l->command != NULL) {
        cJSON_AddStringToObject(o, "command", l->command);
    }
    add_str_array(o, "args", l->args, l->arg_count);
    add_str_array(o, "extensions", l->exts, l->ext_count);
    return o;
}

char *jc_ir_to_config(const struct jc_ir *ir)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    int i;
    char *text;

    arr = cJSON_AddArrayToObject(root, "models");
    if (ir->model_count > 0 && ir->active_model >= 0 &&
        ir->active_model < ir->model_count) {
        cJSON_AddItemToArray(arr, build_model_obj(ir->models[ir->active_model]));
    }
    for (i = 0; i < ir->model_count; i++) {
        if (i != ir->active_model) {
            cJSON_AddItemToArray(arr, build_model_obj(ir->models[i]));
        }
    }

    if (ir->mcp_count > 0) {
        cJSON *m = cJSON_AddArrayToObject(root, "mcpServers");
        for (i = 0; i < ir->mcp_count; i++) {
            cJSON_AddItemToArray(m, build_mcp_obj(ir->mcp[i]));
        }
    }
    if (ir->lsp_count > 0) {
        cJSON *l = cJSON_AddArrayToObject(root, "lspServers");
        for (i = 0; i < ir->lsp_count; i++) {
            cJSON_AddItemToArray(l, build_lsp_obj(ir->lsp[i]));
        }
    }
    if (ir->doc_count > 0) {
        cJSON *d = cJSON_AddArrayToObject(root, "docs");
        for (i = 0; i < ir->doc_count; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name",
                                    ir->docs[i]->name ? ir->docs[i]->name : "");
            if (ir->docs[i]->url != NULL) {
                cJSON_AddStringToObject(o, "url", ir->docs[i]->url);
            } else if (ir->docs[i]->path != NULL) {
                cJSON_AddStringToObject(o, "path", ir->docs[i]->path);
            }
            cJSON_AddItemToArray(d, o);
        }
    }
    add_str_array(root, "instructions", ir->instructions, ir->instr_count);
    if (ir->allow_count > 0 || ir->deny_count > 0) {
        cJSON *perm = cJSON_AddObjectToObject(root, "permissions");
        add_str_array(perm, "allow", ir->perm_allow, ir->allow_count);
        add_str_array(perm, "deny", ir->perm_deny, ir->deny_count);
    }
    if (ir->mode != NULL) {
        cJSON_AddStringToObject(root, "mode", ir->mode);
    }
    cJSON_AddNumberToObject(root, "maxToolIters", 25);

    text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
}

jc_status jc_ir_to_assets(struct jc_ir *ir)
{
    (void)ir; /* assets are populated by the mappers; nothing extra to do */
    return JC_OK;
}

/* --- dispatcher --- */

jc_status jc_convert_run(const char *input_text, enum jc_src_format fmt,
                         struct jc_convert_result *out, struct jc_arena *a)
{
    struct jc_ir *ir;
    jc_status st;

    memset(out, 0, sizeof(*out));
    ir = (struct jc_ir *)jc_arena_alloc(a, sizeof(*ir));
    if (ir == NULL) {
        return JC_ERR_OOM;
    }
    jc_ir_init(ir, a);

    if (fmt == JC_SRC_CONTINUE_YAML) {
        struct jc_yaml *root = jc_yaml_parse(input_text, a);
        if (root == NULL) {
            return JC_ERR_PARSE;
        }
        st = jc_convert_continue_yaml(root, ir, a);
        jc_yaml_free(root);
    } else if (fmt == JC_SRC_CONTINUE_JSON || fmt == JC_SRC_OPENCODE) {
        char *clean = jc_jsonc_strip(input_text, a);
        cJSON *root = (clean != NULL) ? jc_json_parse(clean) : NULL;
        if (root == NULL) {
            return JC_ERR_PARSE;
        }
        st = (fmt == JC_SRC_OPENCODE) ? jc_convert_opencode(root, ir, a)
                                      : jc_convert_continue_json(root, ir, a);
        cJSON_Delete(root);
    } else {
        return JC_ERR_INVALID;
    }
    if (st != JC_OK) {
        return st;
    }

    if (jc_ir_to_assets(ir) != JC_OK) {
        return JC_ERR_INVALID;
    }

    out->ir = ir;
    out->model_count = ir->model_count;
    if (ir->model_count > 0 && ir->active_model >= 0 &&
        ir->active_model < ir->model_count &&
        ir->models[ir->active_model]->name != NULL) {
        out->model_name = jc_arena_strdup(a, ir->models[ir->active_model]->name);
    }
    out->warnings = ir->warnings;
    out->warning_count = ir->warning_count;
    if (ir->warning_count > 0) {
        jc_snprintf(out->warning, sizeof(out->warning), "%s", ir->warnings[0]);
    }
    out->json = jc_ir_to_config(ir);
    return (out->json != NULL) ? JC_OK : JC_ERR_OOM;
}

/* --- format detection --- */

const char *jc_src_format_name(enum jc_src_format fmt)
{
    switch (fmt) {
    case JC_SRC_CONTINUE_YAML: return "Continue config.yaml";
    case JC_SRC_CONTINUE_JSON: return "Continue config.json";
    case JC_SRC_OPENCODE:      return "opencode config";
    case JC_SRC_CLAUDE:        return "Claude Code config";
    default:                   return "unknown";
    }
}

enum jc_src_format jc_convert_classify_json(const cJSON *root)
{
    const cJSON *v;

    if (root == NULL || !cJSON_IsObject(root)) {
        return JC_SRC_UNKNOWN;
    }
    v = cJSON_GetObjectItem(root, "$schema");
    if (cJSON_IsString(v) && v->valuestring != NULL &&
        strstr(v->valuestring, "opencode") != NULL) {
        return JC_SRC_OPENCODE;
    }
    if (cJSON_IsObject(cJSON_GetObjectItem(root, "provider")) ||
        cJSON_IsObject(cJSON_GetObjectItem(root, "agent")) ||
        cJSON_IsObject(cJSON_GetObjectItem(root, "mcp")) ||
        cJSON_IsObject(cJSON_GetObjectItem(root, "command")) ||
        cJSON_IsObject(cJSON_GetObjectItem(root, "permission")) ||
        cJSON_IsString(cJSON_GetObjectItem(root, "model"))) {
        return JC_SRC_OPENCODE;
    }
    if (cJSON_IsArray(cJSON_GetObjectItem(root, "models"))) {
        return JC_SRC_CONTINUE_JSON;
    }
    return JC_SRC_UNKNOWN;
}

static int has_suffix(const char *s, const char *suf)
{
    jc_size ns = strlen(s);
    jc_size nf = strlen(suf);
    return ns >= nf && strcmp(s + ns - nf, suf) == 0;
}

enum jc_src_format jc_convert_detect(const char *filename, const char *text,
                                     struct jc_arena *a)
{
    int json_hint = -1; /* -1 unknown, 0 YAML, 1 JSON */
    const char *p;

    if (filename != NULL) {
        if (has_suffix(filename, ".yaml") || has_suffix(filename, ".yml")) {
            return JC_SRC_CONTINUE_YAML;
        }
        if (has_suffix(filename, ".json") || has_suffix(filename, ".jsonc")) {
            json_hint = 1;
        }
    }
    if (json_hint < 0 && text != NULL) {
        for (p = text; *p != '\0'; p++) {
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                continue;
            }
            json_hint = (*p == '{' || *p == '[') ? 1 : 0;
            break;
        }
    }
    if (json_hint == 0) {
        return JC_SRC_CONTINUE_YAML;
    }
    {
        char *clean = jc_jsonc_strip(text, a);
        cJSON *root = (clean != NULL) ? jc_json_parse(clean) : NULL;
        enum jc_src_format fmt = jc_convert_classify_json(root);
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return fmt;
    }
}

int jc_convert_is_json(const char *filename, const char *text)
{
    const char *p;
    if (filename != NULL) {
        jc_size n = strlen(filename);
        if (n >= 5 && strcmp(filename + n - 5, ".json") == 0) {
            return 1;
        }
        if (n >= 5 && strcmp(filename + n - 5, ".yaml") == 0) {
            return 0;
        }
        if (n >= 4 && strcmp(filename + n - 4, ".yml") == 0) {
            return 0;
        }
    }
    if (text != NULL) {
        for (p = text; *p != '\0'; p++) {
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                continue;
            }
            return (*p == '{' || *p == '[') ? 1 : 0;
        }
    }
    return 0;
}
