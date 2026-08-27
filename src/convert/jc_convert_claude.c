/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_claude.c - convert a Claude Code project/global config tree into a
 * jichi config + .jichi/ asset tree (#1).
 *
 * Unlike the Continue/opencode converters (one input file), Claude Code is a
 * DIRECTORY of files: settings.json (model / mcpServers / permissions / hooks /
 * env), CLAUDE.md (project rules), and .claude agents + commands markdown.
 * jc_convert_run_claude reads them from a base dir, fills the shared IR, and
 * emits via the same jc_ir_to_config path as the other converters. .claude
 * agent/command markdown is near-compatible with jichi's, so it is carried over
 * verbatim as assets. permissions/hooks/env differ structurally and are noted
 * as warnings for manual review rather than mis-mapped.
 */

#include "convert_internal.h"
#include "jc_jsonc.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_vec.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

/* Read a file if it exists; NULL otherwise (arena-owned). */
static char *read_opt(const char *path, struct jc_arena *a)
{
    char *text = NULL;
    if (jc_file_exists(path) &&
        jc_read_file(path, &text, NULL, a) == JC_OK) {
        return text;
    }
    return NULL;
}

/* Map Claude's mcpServers object into IR mcp entries. */
static void claude_map_mcp(struct jc_ir *ir, const cJSON *settings,
                           struct jc_arena *a)
{
    cJSON *servers = cJSON_GetObjectItem(settings, "mcpServers");
    cJSON *entry;
    if (!cJSON_IsObject(servers)) {
        return;
    }
    cJSON_ArrayForEach(entry, servers) {
        struct jc_ir_mcp *im;
        const char *url;
        cJSON *cmd, *args, *env, *it;
        if (!cJSON_IsObject(entry)) {
            continue;
        }
        im = jc_ir_new_mcp(ir);
        if (im == NULL) {
            break;
        }
        im->name = jc_arena_strdup(a, entry->string ? entry->string : "");
        url = jc_json_get_str(entry, "url", NULL);
        if (url != NULL) {
            im->url = jc_arena_strdup(a, url);
            continue;
        }
        cmd = cJSON_GetObjectItem(entry, "command");
        if (cJSON_IsString(cmd)) {
            im->command = jc_arena_strdup(a, cmd->valuestring);
        }
        args = cJSON_GetObjectItem(entry, "args");
        if (cJSON_IsArray(args)) {
            cJSON_ArrayForEach(it, args) {
                if (cJSON_IsString(it) && im->arg_count < JC_IR_MAX_LIST) {
                    im->args[im->arg_count++] =
                        jc_arena_strdup(a, it->valuestring);
                }
            }
        }
        env = cJSON_GetObjectItem(entry, "env");
        if (cJSON_IsObject(env)) {
            cJSON_ArrayForEach(it, env) {
                if (cJSON_IsString(it) && it->string != NULL &&
                    im->env_count < JC_IR_MAX_LIST) {
                    im->env_keys[im->env_count] = jc_arena_strdup(a, it->string);
                    im->env_vals[im->env_count] =
                        jc_arena_strdup(a, it->valuestring);
                    im->env_count++;
                }
            }
        }
    }
}

/* Copy each .md file under <base>/<subdir> into an asset at <dest>/<name>.md.
 * Claude's agent/command frontmatter (description/model/tools) is compatible
 * with jichi's, so the file is carried over verbatim. */
static void claude_copy_md_dir(struct jc_ir *ir, const char *base,
                               const char *subdir, const char *dest,
                               struct jc_arena *a)
{
    char dir[1024];
    struct jc_vec names;
    jc_size i;
    jc_snprintf(dir, sizeof dir, "%s/%s", base, subdir);
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, a) != JC_OK) {
        jc_vec_free(&names);
        return;
    }
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        char path[1200];
        char rel[1100];
        char *body;
        jc_size ln = nm != NULL ? (jc_size)strlen(nm) : 0;
        if (ln < 4 || strcmp(nm + ln - 3, ".md") != 0) {
            continue;
        }
        jc_snprintf(path, sizeof path, "%s/%s", dir, nm);
        body = read_opt(path, a);
        if (body == NULL) {
            continue;
        }
        jc_snprintf(rel, sizeof rel, "%s/%s", dest, nm);
        jc_ir_add_asset(ir, jc_arena_strdup(a, rel), body);
    }
    jc_vec_free(&names);
}

/* Fill the IR from a Claude Code base directory. */
static jc_status jc_convert_claude(const char *base, struct jc_ir *ir,
                                   struct jc_arena *a)
{
    char path[1024];
    char *txt;
    struct jc_ir_model *m;
    cJSON *settings = NULL;
    const char *model_id = NULL;

    /* settings.json (+ settings.local.json overlay for the model). */
    jc_snprintf(path, sizeof path, "%s/.claude/settings.json", base);
    txt = read_opt(path, a);
    if (txt == NULL) {
        /* also accept a bare settings.json at the base (a ".claude" dir arg) */
        jc_snprintf(path, sizeof path, "%s/settings.json", base);
        txt = read_opt(path, a);
    }
    if (txt != NULL) {
        char *clean = jc_jsonc_strip(txt, a);
        settings = clean != NULL ? jc_json_parse(clean) : NULL;
    }
    if (settings != NULL) {
        model_id = jc_json_get_str(settings, "model", NULL);
    }

    /* One model: Claude Code targets the Anthropic API. Default the id when the
     * config didn't pin one; key via the conventional env var. */
    m = jc_ir_new_model(ir);
    if (m == NULL) {
        if (settings != NULL) {
            cJSON_Delete(settings);
        }
        return JC_ERR_OOM;
    }
    m->name = jc_arena_strdup(a, "claude");
    m->model = jc_arena_strdup(a,
        (model_id != NULL && model_id[0] != '\0') ? model_id
                                                   : "claude-sonnet-4-5");
    jc_convert_fill_provider(ir, m, "anthropic", NULL, "ANTHROPIC_API_KEY");
    jc_ir_model_add_role(m, "chat");
    jc_ir_model_add_role(m, "edit");
    jc_ir_model_add_role(m, "apply");
    ir->active_model = 0;

    if (settings != NULL) {
        claude_map_mcp(ir, settings, a);
        if (cJSON_GetObjectItem(settings, "permissions") != NULL) {
            jc_ir_warn(ir, "Claude 'permissions' (Tool(spec) patterns) were not "
                           "mapped; set jichi \"permissions\"/\"editScope\" "
                           "manually if needed.");
        }
        if (cJSON_GetObjectItem(settings, "hooks") != NULL) {
            jc_ir_warn(ir, "Claude 'hooks' were not mapped; port them to jichi "
                           "\"hooks\" (see docs/HOOKS.md) if needed.");
        }
        if (cJSON_GetObjectItem(settings, "env") != NULL) {
            jc_ir_warn(ir, "Claude 'env' was not mapped; export the vars in your "
                           "shell or a start script.");
        }
        cJSON_Delete(settings);
    }

    /* CLAUDE.md -> AGENTS.md (project rules). */
    jc_snprintf(path, sizeof path, "%s/CLAUDE.md", base);
    txt = read_opt(path, a);
    if (txt != NULL) {
        jc_ir_add_asset(ir, "AGENTS.md", txt);
    }

    /* .claude agents + commands markdown -> assets (verbatim). */
    claude_copy_md_dir(ir, base, ".claude/agents", "agents", a);
    claude_copy_md_dir(ir, base, ".claude/commands", "commands", a);
    return JC_OK;
}

jc_status jc_convert_run_claude(const char *base_dir,
                                struct jc_convert_result *out,
                                struct jc_arena *a)
{
    struct jc_ir *ir;
    jc_status st;

    memset(out, 0, sizeof(*out));
    ir = (struct jc_ir *)jc_arena_alloc(a, sizeof(*ir));
    if (ir == NULL) {
        return JC_ERR_OOM;
    }
    jc_ir_init(ir, a);
    st = jc_convert_claude(base_dir, ir, a);
    if (st != JC_OK) {
        return st;
    }
    if (jc_ir_to_assets(ir) != JC_OK) {
        return JC_ERR_INVALID;
    }
    out->ir = ir;
    out->model_count = ir->model_count;
    if (ir->model_count > 0 && ir->models[0]->name != NULL) {
        out->model_name = jc_arena_strdup(a, ir->models[0]->name);
    }
    out->warnings = ir->warnings;
    out->warning_count = ir->warning_count;
    if (ir->warning_count > 0) {
        jc_snprintf(out->warning, sizeof(out->warning), "%s", ir->warnings[0]);
    }
    out->json = jc_ir_to_config(ir);
    return (out->json != NULL) ? JC_OK : JC_ERR_OOM;
}
