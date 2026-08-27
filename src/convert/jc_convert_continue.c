/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_continue.c - map a Continue config (modern config.yaml or legacy
 * config.json) onto the neutral IR. Models and config-level docs here; rules,
 * prompts, and command assets are added by the asset pass. */

#include "jc_convert.h"
#include "convert_internal.h"
#include "jc_yaml.h"
#include "jc_json.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

/* --- YAML (modern config.yaml) --- */

static int yaml_has_chat_role(const struct jc_yaml *m)
{
    struct jc_yaml *roles = jc_yaml_get(m, "roles");
    jc_size i;
    if (roles == NULL || roles->type != JC_YAML_SEQ) {
        return -1;
    }
    for (i = 0; i < jc_yaml_seq_len(roles); i++) {
        struct jc_yaml *r = jc_yaml_seq_at(roles, i);
        if (r != NULL && r->type == JC_YAML_SCALAR && r->scalar != NULL &&
            strcmp(r->scalar, "chat") == 0) {
            return 1;
        }
    }
    return 0;
}

static void yaml_fill_model(struct jc_ir *ir, struct jc_ir_model *out,
                            const struct jc_yaml *m, struct jc_arena *a)
{
    struct jc_yaml *dco;
    struct jc_yaml *roles;
    const char *s;

    out->name = jc_yaml_get_str(m, "name", NULL);
    out->model = jc_yaml_get_str(m, "model", NULL);
    out->api_base = jc_yaml_get_str(m, "apiBase", NULL);
    jc_convert_fill_provider(ir, out, jc_yaml_get_str(m, "provider", NULL),
                             jc_yaml_get_str(m, "apiKey", NULL), NULL);

    roles = jc_yaml_get(m, "roles");
    if (roles != NULL && roles->type == JC_YAML_SEQ) {
        jc_size i;
        for (i = 0; i < jc_yaml_seq_len(roles); i++) {
            struct jc_yaml *r = jc_yaml_seq_at(roles, i);
            if (r != NULL && r->type == JC_YAML_SCALAR && r->scalar != NULL) {
                /* Continue's "subagent" role has no jichi equivalent. */
                if (strcmp(r->scalar, "subagent") != 0) {
                    jc_ir_model_add_role(out, jc_arena_strdup(a, r->scalar));
                }
            }
        }
    }

    dco = jc_yaml_get(m, "defaultCompletionOptions");
    s = jc_yaml_get_str(dco, "maxTokens", NULL);
    if (s == NULL) {
        s = jc_yaml_get_str(m, "maxTokens", NULL);
    }
    if (s != NULL) {
        out->has_max = 1;
        out->max_tokens = strtol(s, NULL, 10);
    }
    s = jc_yaml_get_str(dco, "temperature", NULL);
    if (s == NULL) {
        s = jc_yaml_get_str(m, "temperature", NULL);
    }
    if (s != NULL) {
        out->has_temp = 1;
        out->temperature = strtod(s, NULL);
    }
    s = jc_yaml_get_str(m, "contextLength", NULL);
    if (s != NULL) {
        out->has_ctx = 1;
        out->context_length = strtol(s, NULL, 10);
    }
}

static void yaml_docs(struct jc_ir *ir, const struct jc_yaml *root,
                      struct jc_arena *a)
{
    struct jc_yaml *docs = jc_yaml_get(root, "docs");
    jc_size i;
    if (docs == NULL || docs->type != JC_YAML_SEQ) {
        return;
    }
    for (i = 0; i < jc_yaml_seq_len(docs) && ir->doc_count < JC_IR_MAX_DOCS;
         i++) {
        struct jc_yaml *d = jc_yaml_seq_at(docs, i);
        const char *name = jc_yaml_get_str(d, "name", NULL);
        const char *url = jc_yaml_get_str(d, "startUrl", NULL);
        struct jc_ir_doc *out;
        if (name == NULL || url == NULL) {
            continue;
        }
        out = (struct jc_ir_doc *)jc_arena_calloc(a, sizeof(*out));
        out->name = jc_arena_strdup(a, name);
        out->url = jc_arena_strdup(a, url);
        ir->docs[ir->doc_count++] = out;
    }
}

static void yaml_add_str(const char *arr[], int *n, const char *s)
{
    if (s != NULL && *n < JC_IR_MAX_LIST) {
        arr[(*n)++] = s;
    }
}

static void yaml_mcp(struct jc_ir *ir, const struct jc_yaml *root,
                     struct jc_arena *a)
{
    struct jc_yaml *servers = jc_yaml_get(root, "mcpServers");
    jc_size i;
    if (servers == NULL || servers->type != JC_YAML_SEQ) {
        return;
    }
    for (i = 0; i < jc_yaml_seq_len(servers); i++) {
        struct jc_yaml *s = jc_yaml_seq_at(servers, i);
        const char *name = jc_yaml_get_str(s, "name", NULL);
        const char *url = jc_yaml_get_str(s, "url", NULL);
        struct jc_ir_mcp *im;
        if (s == NULL || s->type != JC_YAML_MAP) {
            continue;
        }
        if (jc_yaml_get(s, "uses") != NULL) {
            jc_ir_warn(ir, "skipped a hub-reference MCP server ('uses:').");
            continue;
        }
        im = jc_ir_new_mcp(ir);
        if (im == NULL) {
            break;
        }
        im->name = jc_arena_strdup(a, name ? name : "");
        if (url != NULL) {
            const char *key = jc_yaml_get_str(s, "apiKey", NULL);
            im->url = jc_arena_strdup(a, url);
            if (jc_convert_key_is_literal(key)) {
                struct jc_sb sb;
                jc_sb_init(&sb);
                jc_sb_append(&sb, "Authorization: Bearer ");
                jc_sb_append(&sb, key);
                yaml_add_str(im->headers, &im->header_count,
                             jc_arena_strdup(a, sb.data ? sb.data : ""));
                jc_sb_free(&sb);
            } else if (key != NULL) {
                jc_ir_warn(ir, "MCP server '%s' uses a templated apiKey; add "
                               "an Authorization header manually.",
                           im->name);
            }
        } else {
            struct jc_yaml *args = jc_yaml_get(s, "args");
            struct jc_yaml *env = jc_yaml_get(s, "env");
            const char *cmd = jc_yaml_get_str(s, "command", NULL);
            im->command = (cmd != NULL) ? jc_arena_strdup(a, cmd) : NULL;
            if (args != NULL && args->type == JC_YAML_SEQ) {
                jc_size j;
                for (j = 0; j < jc_yaml_seq_len(args); j++) {
                    struct jc_yaml *v = jc_yaml_seq_at(args, j);
                    if (v != NULL && v->type == JC_YAML_SCALAR &&
                        v->scalar != NULL) {
                        yaml_add_str(im->args, &im->arg_count,
                                     jc_arena_strdup(a, v->scalar));
                    }
                }
            }
            if (env != NULL && env->type == JC_YAML_MAP) {
                jc_size j;
                for (j = 0; j < env->keys.len &&
                            im->env_count < JC_IR_MAX_LIST; j++) {
                    char *k = *(char **)jc_vec_at((struct jc_vec *)&env->keys,
                                                  j);
                    struct jc_yaml *v = *(struct jc_yaml **)jc_vec_at(
                        (struct jc_vec *)&env->vals, j);
                    if (v != NULL && v->type == JC_YAML_SCALAR) {
                        im->env_keys[im->env_count] = jc_arena_strdup(a, k);
                        im->env_vals[im->env_count] =
                            jc_arena_strdup(a, v->scalar ? v->scalar : "");
                        im->env_count++;
                    }
                }
            }
            if (jc_yaml_get(s, "cwd") != NULL) {
                jc_ir_warn(ir, "MCP server '%s' cwd is not supported and was "
                               "dropped.", im->name);
            }
        }
    }
}

static void yaml_rules(struct jc_ir *ir, const struct jc_yaml *root,
                       struct jc_arena *a)
{
    struct jc_yaml *rules = jc_yaml_get(root, "rules");
    struct jc_sb sb;
    jc_size i;
    if (rules == NULL || rules->type != JC_YAML_SEQ) {
        return;
    }
    jc_sb_init(&sb);
    for (i = 0; i < jc_yaml_seq_len(rules); i++) {
        struct jc_yaml *r = jc_yaml_seq_at(rules, i);
        if (r == NULL) {
            continue;
        }
        if (r->type == JC_YAML_SCALAR && r->scalar != NULL) {
            if (sb.len > 0) {
                jc_sb_append_char(&sb, '\n');
            }
            jc_sb_append(&sb, r->scalar);
            jc_sb_append_char(&sb, '\n');
        } else if (r->type == JC_YAML_MAP) {
            const char *name = jc_yaml_get_str(r, "name", NULL);
            const char *rule = jc_yaml_get_str(r, "rule", NULL);
            if (jc_yaml_get(r, "uses") != NULL) {
                jc_ir_warn(ir, "skipped a hub-reference rule ('uses:').");
                continue;
            }
            if (sb.len > 0) {
                jc_sb_append_char(&sb, '\n');
            }
            if (name != NULL) {
                jc_sb_append(&sb, "## ");
                jc_sb_append(&sb, name);
                jc_sb_append(&sb, "\n\n");
            }
            if (rule != NULL) {
                jc_sb_append(&sb, rule);
                jc_sb_append_char(&sb, '\n');
            }
        }
    }
    if (sb.len > 0) {
        jc_ir_add_asset(ir, jc_arena_strdup(a, "AGENTS.md"),
                        jc_arena_strdup(a, sb.data));
    }
    jc_sb_free(&sb);
}

static void yaml_prompts(struct jc_ir *ir, const struct jc_yaml *root,
                         struct jc_arena *a)
{
    struct jc_yaml *prompts = jc_yaml_get(root, "prompts");
    jc_size i;
    if (prompts == NULL || prompts->type != JC_YAML_SEQ) {
        return;
    }
    for (i = 0; i < jc_yaml_seq_len(prompts); i++) {
        struct jc_yaml *p = jc_yaml_seq_at(prompts, i);
        const char *name = jc_yaml_get_str(p, "name", NULL);
        const char *desc = jc_yaml_get_str(p, "description", NULL);
        const char *body = jc_yaml_get_str(p, "prompt", NULL);
        const char *slug;
        const char *rel;
        if (p == NULL || p->type != JC_YAML_MAP || name == NULL) {
            continue;
        }
        if (jc_yaml_get(p, "uses") != NULL) {
            jc_ir_warn(ir, "skipped a hub-reference prompt ('uses:').");
            continue;
        }
        slug = jc_convert_slug(a, name);
        rel = jc_ir_unique_relpath(ir, "commands", slug, "md");
        jc_ir_add_asset(ir, rel,
                        jc_asset_command_md(a, desc, NULL, NULL,
                                            body != NULL ? body : ""));
    }
}

jc_status jc_convert_continue_yaml(const struct jc_yaml *root,
                                   struct jc_ir *ir, struct jc_arena *a)
{
    struct jc_yaml *models = jc_yaml_get(root, "models");
    jc_size i;
    jc_size total;
    int active = -1;

    if (models == NULL || models->type != JC_YAML_SEQ ||
        jc_yaml_seq_len(models) == 0) {
        return JC_ERR_NOTFOUND;
    }
    total = jc_yaml_seq_len(models);
    for (i = 0; i < total; i++) {
        struct jc_yaml *m = jc_yaml_seq_at(models, i);
        struct jc_ir_model *im;
        if (m == NULL || m->type != JC_YAML_MAP) {
            continue;
        }
        if (jc_yaml_get(m, "uses") != NULL) {
            jc_ir_warn(ir, "skipped a hub-reference model ('uses:'); it "
                           "cannot be resolved offline.");
            continue;
        }
        im = jc_ir_new_model(ir);
        if (im == NULL) {
            break;
        }
        yaml_fill_model(ir, im, m, a);
        if (active < 0 && yaml_has_chat_role(m) == 1) {
            active = ir->model_count - 1;
        }
    }
    if (ir->model_count == 0) {
        return JC_ERR_NOTFOUND;
    }
    ir->active_model = (active < 0) ? 0 : active;
    if (ir->models[ir->active_model]->api_key == NULL) {
        jc_ir_warn(ir, "no literal API key for the active model; set %s in "
                       "your environment.",
                   ir->models[ir->active_model]->api_key_env);
    }
    yaml_docs(ir, root, a);
    yaml_mcp(ir, root, a);
    yaml_rules(ir, root, a);
    yaml_prompts(ir, root, a);
    return JC_OK;
}

/* --- legacy config.json --- */

static void json_commands_from(struct jc_ir *ir, const cJSON *root,
                               const char *key, struct jc_arena *a)
{
    cJSON *arr = cJSON_GetObjectItem(root, key);
    cJSON *c;
    if (!cJSON_IsArray(arr)) {
        return;
    }
    cJSON_ArrayForEach(c, arr) {
        const char *name = jc_json_get_str(c, "name", NULL);
        const char *desc = jc_json_get_str(c, "description", NULL);
        const char *body = jc_json_get_str(c, "prompt", NULL);
        const char *slug;
        const char *rel;
        if (!cJSON_IsObject(c) || name == NULL) {
            continue;
        }
        slug = jc_convert_slug(a, name);
        rel = jc_ir_unique_relpath(ir, "commands", slug, "md");
        jc_ir_add_asset(ir, rel,
                        jc_asset_command_md(a, desc, NULL, NULL,
                                            body != NULL ? body : ""));
    }
}

static void json_assets(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    const char *sm = jc_json_get_str(root, "systemMessage", NULL);
    if (sm != NULL && sm[0] != '\0') {
        jc_ir_add_asset(ir, jc_arena_strdup(a, "AGENTS.md"),
                        jc_arena_strdup(a, sm));
    }
    json_commands_from(ir, root, "customCommands", a);
    json_commands_from(ir, root, "slashCommands", a);
}

static int json_has_chat_role(const cJSON *m)
{
    cJSON *roles = cJSON_GetObjectItem(m, "roles");
    cJSON *r;
    if (!cJSON_IsArray(roles)) {
        return -1;
    }
    cJSON_ArrayForEach(r, roles) {
        if (cJSON_IsString(r) && r->valuestring != NULL &&
            strcmp(r->valuestring, "chat") == 0) {
            return 1;
        }
    }
    return 0;
}

static void json_fill_model(struct jc_ir *ir, struct jc_ir_model *out,
                            const cJSON *m, struct jc_arena *a)
{
    cJSON *opts;
    cJSON *roles;
    cJSON *n;

    /* cJSON strings are owned by the tree (freed after mapping), so copy
     * everything retained on the IR into the arena. */
    out->name = jc_arena_strdup(a, jc_json_get_str(m, "title",
                                jc_json_get_str(m, "name", NULL)));
    out->model = jc_arena_strdup(a, jc_json_get_str(m, "model", NULL));
    out->api_base = jc_arena_strdup(a, jc_json_get_str(m, "apiBase", NULL));
    jc_convert_fill_provider(ir, out, jc_json_get_str(m, "provider", NULL),
                             jc_json_get_str(m, "apiKey", NULL), NULL);

    roles = cJSON_GetObjectItem(m, "roles");
    if (cJSON_IsArray(roles)) {
        cJSON_ArrayForEach(n, roles) {
            if (cJSON_IsString(n) && n->valuestring != NULL &&
                strcmp(n->valuestring, "subagent") != 0) {
                jc_ir_model_add_role(out, jc_arena_strdup(a, n->valuestring));
            }
        }
    }

    opts = cJSON_GetObjectItem(m, "completionOptions");
    if (opts == NULL) {
        opts = cJSON_GetObjectItem(m, "defaultCompletionOptions");
    }
    n = (opts != NULL) ? cJSON_GetObjectItem(opts, "maxTokens") : NULL;
    if (cJSON_IsNumber(n)) {
        out->has_max = 1;
        out->max_tokens = (long)n->valuedouble;
    }
    n = (opts != NULL) ? cJSON_GetObjectItem(opts, "temperature") : NULL;
    if (cJSON_IsNumber(n)) {
        out->has_temp = 1;
        out->temperature = n->valuedouble;
    }
    n = cJSON_GetObjectItem(m, "contextLength");
    if (cJSON_IsNumber(n)) {
        out->has_ctx = 1;
        out->context_length = (long)n->valuedouble;
    }
}

static void json_docs(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    cJSON *docs = cJSON_GetObjectItem(root, "docs");
    cJSON *d;
    if (!cJSON_IsArray(docs)) {
        return;
    }
    cJSON_ArrayForEach(d, docs) {
        const char *name = jc_json_get_str(d, "title",
                                           jc_json_get_str(d, "name", NULL));
        const char *url = jc_json_get_str(d, "startUrl",
                                          jc_json_get_str(d, "url", NULL));
        struct jc_ir_doc *out;
        if (name == NULL || url == NULL || ir->doc_count >= JC_IR_MAX_DOCS) {
            continue;
        }
        out = (struct jc_ir_doc *)jc_arena_calloc(a, sizeof(*out));
        out->name = jc_arena_strdup(a, name);
        out->url = jc_arena_strdup(a, url);
        ir->docs[ir->doc_count++] = out;
    }
}

jc_status jc_convert_continue_json(const cJSON *root, struct jc_ir *ir,
                                   struct jc_arena *a)
{
    cJSON *models = cJSON_GetObjectItem(root, "models");
    cJSON *m;
    int active = -1;

    if (!cJSON_IsArray(models) || cJSON_GetArraySize(models) == 0) {
        return JC_ERR_NOTFOUND;
    }
    cJSON_ArrayForEach(m, models) {
        struct jc_ir_model *im;
        if (!cJSON_IsObject(m)) {
            continue;
        }
        im = jc_ir_new_model(ir);
        if (im == NULL) {
            break;
        }
        json_fill_model(ir, im, m, a);
        if (active < 0 && json_has_chat_role(m) == 1) {
            active = ir->model_count - 1;
        }
    }
    if (ir->model_count == 0) {
        return JC_ERR_NOTFOUND;
    }
    ir->active_model = (active < 0) ? 0 : active;
    if (ir->models[ir->active_model]->api_key == NULL) {
        jc_ir_warn(ir, "no literal API key for the active model; set %s in "
                       "your environment.",
                   ir->models[ir->active_model]->api_key_env);
    }
    json_docs(ir, root, a);
    json_assets(ir, root, a);
    return JC_OK;
}
