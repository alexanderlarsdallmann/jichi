/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_opencode.c - map an opencode config (opencode.json/.jsonc) onto
 * the neutral IR. Models, MCP, LSP, instructions, and permissions/mode here;
 * agent/command/skill assets are added by the asset pass. */

#include "jc_convert.h"
#include "convert_internal.h"
#include "jc_json.h"
#include "jc_str.h"

#include <string.h>

/* Split "provider/model" into its parts (arena copies; either may be NULL). */
static void split_model(const char *slug, struct jc_arena *a,
                        const char **prov, const char **model)
{
    const char *slash;
    *prov = NULL;
    *model = NULL;
    if (slug == NULL) {
        return;
    }
    slash = strchr(slug, '/');
    if (slash == NULL) {
        *model = jc_arena_strdup(a, slug);
        return;
    }
    *prov = jc_arena_strndup(a, slug, (jc_size)(slash - slug));
    *model = jc_arena_strdup(a, slash + 1);
}

/* If `key` is an env reference ("{env:VAR}" or "$VAR"), return VAR; else NULL. */
static const char *extract_env_ref(const char *key, struct jc_arena *a)
{
    if (key == NULL) {
        return NULL;
    }
    if (strncmp(key, "{env:", 5) == 0) {
        const char *start = key + 5;
        const char *end = strchr(start, '}');
        if (end != NULL && end > start) {
            return jc_arena_strndup(a, start, (jc_size)(end - start));
        }
    }
    if (key[0] == '$' && key[1] != '\0') {
        return jc_arena_strdup(a, key + 1);
    }
    return NULL;
}

static cJSON *provider_options(const cJSON *root, const char *prov_id)
{
    cJSON *provider = cJSON_GetObjectItem(root, "provider");
    cJSON *p;
    if (!cJSON_IsObject(provider) || prov_id == NULL) {
        return NULL;
    }
    p = cJSON_GetObjectItem(provider, prov_id);
    if (!cJSON_IsObject(p)) {
        return NULL;
    }
    return cJSON_GetObjectItem(p, "options");
}

static struct jc_ir_model *add_model(struct jc_ir *ir, const cJSON *root,
                                     const char *slug, struct jc_arena *a)
{
    const char *prov;
    const char *model;
    struct jc_ir_model *m;
    cJSON *opts;
    const char *base;
    const char *key;

    if (slug == NULL || slug[0] == '\0') {
        return NULL;
    }
    m = jc_ir_new_model(ir);
    if (m == NULL) {
        return NULL;
    }
    split_model(slug, a, &prov, &model);
    m->model = model;
    m->name = jc_arena_strdup(a, slug);
    opts = provider_options(root, prov);
    base = jc_json_get_str(opts, "baseURL", jc_json_get_str(opts, "apiBase",
                                                            NULL));
    if (base != NULL) {
        m->api_base = jc_arena_strdup(a, base);
    }
    key = jc_json_get_str(opts, "apiKey", NULL);
    jc_convert_fill_provider(ir, m, prov, key, extract_env_ref(key, a));
    return m;
}

static void map_models(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    const char *main_model = jc_json_get_str(root, "model", NULL);
    const char *small = jc_json_get_str(root, "small_model", NULL);
    struct jc_ir_model *m;

    m = add_model(ir, root, main_model, a);
    if (m != NULL) {
        jc_ir_model_add_role(m, "chat");
        jc_ir_model_add_role(m, "edit");
        jc_ir_model_add_role(m, "apply");
        ir->active_model = ir->model_count - 1;
    }
    m = add_model(ir, root, small, a);
    if (m != NULL) {
        jc_ir_model_add_role(m, "summarize");
        jc_ir_model_add_role(m, "autocomplete");
    }
    if (ir->model_count == 0) {
        jc_ir_warn(ir, "no top-level model/small_model; set one in the jichi "
                       "config.");
    } else if (ir->models[ir->active_model]->api_key == NULL) {
        jc_ir_warn(ir, "no literal API key for the active model; set %s in "
                       "your environment.",
                   ir->models[ir->active_model]->api_key_env);
    }
}

static void add_list(const char *arr[], int *n, const char *s)
{
    if (s != NULL && *n < JC_IR_MAX_LIST) {
        arr[(*n)++] = s;
    }
}

static void map_mcp(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    cJSON *mcp = cJSON_GetObjectItem(root, "mcp");
    cJSON *entry;
    if (!cJSON_IsObject(mcp)) {
        return;
    }
    cJSON_ArrayForEach(entry, mcp) {
        const char *name = entry->string;
        const char *type;
        struct jc_ir_mcp *im;
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        if (jc_json_get_bool_lenient(entry, "enabled", 1) == 0) {
            jc_ir_warn(ir, "MCP server '%s' is disabled; skipped.",
                       name ? name : "(unnamed)");
            continue;
        }
        im = jc_ir_new_mcp(ir);
        if (im == NULL) {
            break;
        }
        im->name = jc_arena_strdup(a, name ? name : "");
        type = jc_json_get_str(entry, "type", NULL);
        if (type != NULL && strcmp(type, "remote") == 0) {
            const char *url = jc_json_get_str(entry, "url", NULL);
            cJSON *headers = cJSON_GetObjectItem(entry, "headers");
            cJSON *h;
            if (url != NULL) {
                im->url = jc_arena_strdup(a, url);
            }
            if (cJSON_IsObject(headers)) {
                cJSON_ArrayForEach(h, headers) {
                    if (cJSON_IsString(h) && h->string != NULL &&
                        im->header_count < JC_IR_MAX_LIST) {
                        struct jc_sb sb;
                        jc_sb_init(&sb);
                        jc_sb_append(&sb, h->string);
                        jc_sb_append(&sb, ": ");
                        jc_sb_append(&sb, h->valuestring);
                        im->headers[im->header_count++] =
                            jc_arena_strdup(a, sb.data ? sb.data : "");
                        jc_sb_free(&sb);
                    }
                }
            }
        } else {
            cJSON *cmd = cJSON_GetObjectItem(entry, "command");
            cJSON *env = cJSON_GetObjectItem(entry, "environment");
            if (cJSON_IsArray(cmd)) {
                int idx = 0;
                cJSON *c;
                cJSON_ArrayForEach(c, cmd) {
                    if (!cJSON_IsString(c)) {
                        continue;
                    }
                    if (idx == 0) {
                        im->command = jc_arena_strdup(a, c->valuestring);
                    } else {
                        add_list(im->args, &im->arg_count,
                                 jc_arena_strdup(a, c->valuestring));
                    }
                    idx++;
                }
            }
            if (cJSON_IsObject(env)) {
                cJSON *e;
                cJSON_ArrayForEach(e, env) {
                    if (cJSON_IsString(e) && e->string != NULL &&
                        im->env_count < JC_IR_MAX_LIST) {
                        im->env_keys[im->env_count] =
                            jc_arena_strdup(a, e->string);
                        im->env_vals[im->env_count] =
                            jc_arena_strdup(a, e->valuestring);
                        im->env_count++;
                    }
                }
            }
        }
    }
}

static void map_lsp(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    cJSON *lsp = cJSON_GetObjectItem(root, "lsp");
    cJSON *entry;
    if (!cJSON_IsObject(lsp)) {
        return;
    }
    cJSON_ArrayForEach(entry, lsp) {
        cJSON *cmd;
        cJSON *exts;
        cJSON *c;
        struct jc_ir_lsp *il;
        int idx;
        if (!cJSON_IsObject(entry) || jc_json_get_bool_lenient(entry, "disabled", 0)) {
            continue;
        }
        cmd = cJSON_GetObjectItem(entry, "command");
        if (!cJSON_IsArray(cmd) || cJSON_GetArraySize(cmd) == 0) {
            continue; /* builtin-by-id; jichi needs an explicit command */
        }
        il = jc_ir_new_lsp(ir);
        if (il == NULL) {
            break;
        }
        il->name = jc_arena_strdup(a, entry->string ? entry->string : "");
        idx = 0;
        cJSON_ArrayForEach(c, cmd) {
            if (!cJSON_IsString(c)) {
                continue;
            }
            if (idx == 0) {
                il->command = jc_arena_strdup(a, c->valuestring);
            } else {
                add_list(il->args, &il->arg_count,
                         jc_arena_strdup(a, c->valuestring));
            }
            idx++;
        }
        exts = cJSON_GetObjectItem(entry, "extensions");
        if (cJSON_IsArray(exts)) {
            cJSON_ArrayForEach(c, exts) {
                if (cJSON_IsString(c)) {
                    const char *e = c->valuestring;
                    if (e[0] == '.') {
                        e++; /* jichi extensions carry no leading dot */
                    }
                    add_list(il->exts, &il->ext_count, jc_arena_strdup(a, e));
                }
            }
        }
    }
}

static void map_instructions(struct jc_ir *ir, const cJSON *root,
                             struct jc_arena *a)
{
    cJSON *ins = cJSON_GetObjectItem(root, "instructions");
    cJSON *s;
    if (!cJSON_IsArray(ins)) {
        return;
    }
    cJSON_ArrayForEach(s, ins) {
        if (cJSON_IsString(s) && ir->instr_count < JC_IR_MAX_LIST) {
            ir->instructions[ir->instr_count++] =
                jc_arena_strdup(a, s->valuestring);
        }
    }
}

/* Map an opencode permission category to jichi tool names, appending to `arr`. */
static void add_perm_tools(const char *arr[], int *n, const char *cat)
{
    if (strcmp(cat, "read") == 0) {
        add_list(arr, n, "read_file");
    } else if (strcmp(cat, "edit") == 0 || strcmp(cat, "write") == 0) {
        add_list(arr, n, "edit_file");
        add_list(arr, n, "write_file");
        add_list(arr, n, "apply_patch");
    } else if (strcmp(cat, "bash") == 0) {
        add_list(arr, n, "run_terminal_command");
    } else if (strcmp(cat, "webfetch") == 0) {
        add_list(arr, n, "fetch_url");
    } else if (strcmp(cat, "websearch") == 0) {
        add_list(arr, n, "web_search");
    } else if (strcmp(cat, "list") == 0 || strcmp(cat, "glob") == 0) {
        add_list(arr, n, "list_files");
    } else if (strcmp(cat, "grep") == 0) {
        add_list(arr, n, "search_code");
    }
    /* Unknown categories are left unmapped (advisory). */
}

static void map_permission(struct jc_ir *ir, const cJSON *root)
{
    cJSON *perm = cJSON_GetObjectItem(root, "permission");
    int bash_deny = 0;
    int edit_deny = 0;

    if (cJSON_IsString(perm)) {
        if (strcmp(perm->valuestring, "allow") == 0) {
            ir->mode = "auto";
        } else if (strcmp(perm->valuestring, "deny") == 0) {
            ir->mode = "plan";
        }
        return;
    }
    if (!cJSON_IsObject(perm)) {
        return;
    }
    {
        cJSON *e;
        cJSON_ArrayForEach(e, perm) {
            const char *cat = e->string;
            const char *act = cJSON_IsString(e) ? e->valuestring : NULL;
            if (cat == NULL || act == NULL) {
                continue; /* per-pattern object form: skip (advisory) */
            }
            if (strcmp(act, "deny") == 0) {
                add_perm_tools(ir->perm_deny, &ir->deny_count, cat);
                if (strcmp(cat, "bash") == 0) {
                    bash_deny = 1;
                }
                if (strcmp(cat, "edit") == 0) {
                    edit_deny = 1;
                }
            } else if (strcmp(act, "allow") == 0) {
                add_perm_tools(ir->perm_allow, &ir->allow_count, cat);
            }
        }
    }
    if (bash_deny && edit_deny) {
        ir->mode = "plan";
    }
}

/* Read-only if the agent's permission or tools map disables editing/shell. */
static int agent_readonly(const cJSON *agent)
{
    cJSON *perm = cJSON_GetObjectItem(agent, "permission");
    cJSON *tools = cJSON_GetObjectItem(agent, "tools");
    if (cJSON_IsObject(perm)) {
        const char *e = jc_json_get_str(perm, "edit", NULL);
        const char *b = jc_json_get_str(perm, "bash", NULL);
        if ((e != NULL && strcmp(e, "deny") == 0) ||
            (b != NULL && strcmp(b, "deny") == 0)) {
            return 1;
        }
    }
    if (cJSON_IsObject(tools)) {
        if (jc_json_get_bool_lenient(tools, "*", 1) == 0) {
            /* Allow-list mode: read-only unless a mutating tool is enabled. */
            if (!jc_json_get_bool_lenient(tools, "edit", 0) &&
                !jc_json_get_bool_lenient(tools, "write", 0) &&
                !jc_json_get_bool_lenient(tools, "bash", 0)) {
                return 1;
            }
        } else if (jc_json_get_bool_lenient(tools, "edit", 1) == 0 &&
                   jc_json_get_bool_lenient(tools, "write", 1) == 0) {
            return 1;
        }
    }
    return -1;
}

/* Build a jichi tool allow-list only when the agent uses "*": false (allow-list
 * mode); otherwise leave it unrestricted. */
static void agent_tools(const cJSON *agent, const char *out[], int *n)
{
    cJSON *tools = cJSON_GetObjectItem(agent, "tools");
    cJSON *t;
    if (!cJSON_IsObject(tools) || jc_json_get_bool_lenient(tools, "*", 1) != 0) {
        return;
    }
    cJSON_ArrayForEach(t, tools) {
        if (t->string != NULL && strcmp(t->string, "*") != 0 &&
            cJSON_IsTrue(t)) {
            add_perm_tools(out, n, t->string);
        }
    }
}

static void map_agents(struct jc_ir *ir, const cJSON *root, struct jc_arena *a)
{
    cJSON *agents = cJSON_GetObjectItem(root, "agent");
    cJSON *e;
    if (!cJSON_IsObject(agents)) {
        return;
    }
    cJSON_ArrayForEach(e, agents) {
        const char *tools[JC_IR_MAX_LIST];
        int nt = 0;
        const char *slug;
        const char *rel;
        const char *md;
        const char *prompt;
        if (!cJSON_IsObject(e) || e->string == NULL ||
            jc_json_get_bool_lenient(e, "disable", 0)) {
            continue;
        }
        agent_tools(e, tools, &nt);
        prompt = jc_json_get_str(e, "prompt", NULL);
        slug = jc_convert_slug(a, e->string);
        rel = jc_ir_unique_relpath(ir, "agents", slug, "md");
        md = jc_asset_agent_md(a, jc_json_get_str(e, "description", NULL),
                               jc_json_get_str(e, "model", NULL),
                               agent_readonly(e), tools, nt,
                               prompt != NULL ? prompt : "");
        jc_ir_add_asset(ir, rel, md);
    }
}

static void map_commands(struct jc_ir *ir, const cJSON *root,
                         struct jc_arena *a)
{
    cJSON *cmds = cJSON_GetObjectItem(root, "command");
    cJSON *e;
    if (!cJSON_IsObject(cmds)) {
        return;
    }
    cJSON_ArrayForEach(e, cmds) {
        const char *tmpl;
        const char *slug;
        const char *rel;
        const char *md;
        if (!cJSON_IsObject(e) || e->string == NULL) {
            continue;
        }
        tmpl = jc_json_get_str(e, "template", NULL);
        slug = jc_convert_slug(a, e->string);
        rel = jc_ir_unique_relpath(ir, "commands", slug, "md");
        md = jc_asset_command_md(a, jc_json_get_str(e, "description", NULL),
                                 jc_json_get_str(e, "agent", NULL),
                                 jc_json_get_str(e, "model", NULL),
                                 tmpl != NULL ? tmpl : "");
        jc_ir_add_asset(ir, rel, md);
    }
}

jc_status jc_convert_opencode(const cJSON *root, struct jc_ir *ir,
                              struct jc_arena *a)
{
    map_models(ir, root, a);
    map_mcp(ir, root, a);
    map_lsp(ir, root, a);
    map_instructions(ir, root, a);
    map_permission(ir, root);
    map_agents(ir, root, a);
    map_commands(ir, root, a);
    /* An opencode config with no model but with MCP/LSP/etc is still a useful
     * conversion, so this never fails on an empty model set. */
    return JC_OK;
}
