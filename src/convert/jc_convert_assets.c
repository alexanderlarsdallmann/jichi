/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_assets.c - build the .jichi/ asset markdown (agents, commands) and
 * manage the IR asset list. The per-source mappers call these to turn inline
 * agents/commands/rules/prompts into files jichi can read. */

#include "jc_convert.h"
#include "convert_internal.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_assetval.h"

#include <string.h>

void jc_ir_add_asset(struct jc_ir *ir, const char *relpath,
                     const char *contents)
{
    struct jc_ir_asset *as;
    if (ir->asset_count >= JC_IR_MAX_ASSETS || relpath == NULL) {
        return;
    }
    as = (struct jc_ir_asset *)jc_arena_calloc(ir->a, sizeof(*as));
    if (as == NULL) {
        return;
    }
    as->relpath = relpath;
    as->contents = (contents != NULL) ? contents : "";
    ir->assets[ir->asset_count++] = as;
}

const char *jc_ir_unique_relpath(struct jc_ir *ir, const char *dir,
                                 const char *slug, const char *ext)
{
    char buf[512];
    int n;
    int i;

    /* A converted command whose slug matches a jichi built-in (/review, /compact,
     * ...) would shadow it. Warn at convert time -- doctor also flags it later,
     * but the operator should know before wiring the project up. */
    if (dir != NULL && strcmp(dir, "commands") == 0 &&
        jc_assetval_is_builtin_command(slug)) {
        jc_ir_warn(ir, "command '%s' shadows a built-in jichi command; rename it "
                   "in the source to avoid the conflict", slug);
    }

    for (n = 1; n < 1000; n++) {
        int taken = 0;
        if (n == 1) {
            jc_snprintf(buf, sizeof(buf), "%s/%s.%s", dir, slug, ext);
        } else {
            jc_snprintf(buf, sizeof(buf), "%s/%s-%d.%s", dir, slug, n, ext);
        }
        for (i = 0; i < ir->asset_count; i++) {
            if (strcmp(ir->assets[i]->relpath, buf) == 0) {
                taken = 1;
                break;
            }
        }
        if (!taken) {
            break;
        }
    }
    return jc_arena_strdup(ir->a, buf);
}

/* Append `s` as a double-quoted, escaped YAML scalar to `sb`. Newlines in the
 * value are collapsed to spaces (frontmatter values are single-line). */
static void append_quoted(struct jc_sb *sb, const char *s)
{
    const char *p;
    jc_sb_append_char(sb, '"');
    for (p = (s != NULL) ? s : ""; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            jc_sb_append_char(sb, '\\');
            jc_sb_append_char(sb, *p);
        } else if (*p == '\n' || *p == '\r' || *p == '\t') {
            jc_sb_append_char(sb, ' ');
        } else {
            jc_sb_append_char(sb, *p);
        }
    }
    jc_sb_append_char(sb, '"');
}

const char *jc_asset_agent_md(struct jc_arena *a, const char *description,
                              const char *model, int readonly,
                              const char *const *tools, int ntools,
                              const char *body)
{
    struct jc_sb sb;
    const char *out;
    int i;

    jc_sb_init(&sb);
    jc_sb_append(&sb, "---\n");
    if (description != NULL && description[0] != '\0') {
        jc_sb_append(&sb, "description: ");
        append_quoted(&sb, description);
        jc_sb_append_char(&sb, '\n');
    }
    if (model != NULL && model[0] != '\0') {
        jc_sb_append(&sb, "model: ");
        append_quoted(&sb, model);
        jc_sb_append_char(&sb, '\n');
    }
    if (readonly >= 0) {
        jc_sb_append(&sb, readonly ? "readonly: true\n" : "readonly: false\n");
    }
    if (tools != NULL && ntools > 0) {
        jc_sb_append(&sb, "tools:\n");
        for (i = 0; i < ntools; i++) {
            jc_sb_append(&sb, "  - ");
            jc_sb_append(&sb, tools[i]);
            jc_sb_append_char(&sb, '\n');
        }
    }
    jc_sb_append(&sb, "---\n");
    if (body != NULL) {
        jc_sb_append(&sb, body);
    }
    if (sb.len == 0 || sb.data[sb.len - 1] != '\n') {
        jc_sb_append_char(&sb, '\n');
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}

const char *jc_asset_command_md(struct jc_arena *a, const char *description,
                                const char *agent, const char *model,
                                const char *body)
{
    struct jc_sb sb;
    const char *out;

    jc_sb_init(&sb);
    jc_sb_append(&sb, "---\n");
    if (description != NULL && description[0] != '\0') {
        jc_sb_append(&sb, "description: ");
        append_quoted(&sb, description);
        jc_sb_append_char(&sb, '\n');
    }
    if (agent != NULL && agent[0] != '\0') {
        jc_sb_append(&sb, "agent: ");
        append_quoted(&sb, agent);
        jc_sb_append_char(&sb, '\n');
    }
    if (model != NULL && model[0] != '\0') {
        jc_sb_append(&sb, "model: ");
        append_quoted(&sb, model);
        jc_sb_append_char(&sb, '\n');
    }
    jc_sb_append(&sb, "---\n");
    if (body != NULL) {
        jc_sb_append(&sb, body);
    }
    if (sb.len == 0 || sb.data[sb.len - 1] != '\n') {
        jc_sb_append_char(&sb, '\n');
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}
