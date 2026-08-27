/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_confbench.c - pure config benchmark scorer (see jc_confbench.h). */

#include "jc_confbench.h"
#include "jc_snprintf.h"

#include <string.h>

static void add(struct jc_confbench_report *r, const char *name, int present,
                int weight, const char *hint)
{
    if (r->n >= JC_CONFBENCH_MAX_ITEMS) return;
    r->items[r->n].name = name;
    r->items[r->n].present = present;
    r->items[r->n].weight = weight;
    r->items[r->n].hint = hint;
    r->max += weight;
    if (present) r->got += weight;
    r->n++;
}

void jc_confbench_score(const struct jc_confbench_facts *f,
                        struct jc_confbench_report *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (f == NULL) return;

    /* Models & roles */
    add(out, "active model", f->has_active_model, 10,
        "set a chat model in your config (see docs/MODELS.md)");
    add(out, "embed model (RAG)", f->has_embed, 8,
        "add an embed-role model to enable codebase/docs retrieval");
    add(out, "rerank model", f->has_rerank, 4,
        "add a rerank-role model for sharper retrieval");
    add(out, "summarize model", f->has_summarize, 5,
        "add a summarize-role model for cheaper, better compaction");
    add(out, "tiered routing", f->routing_ok, 6,
        "configure routing.fast + routing.strong (distinct tiers)");
    /* Safety net */
    add(out, "snapshots/undo", f->snapshots, 8,
        "enable snapshots so edits are checkpointed + revertible");
    add(out, "verify gate", f->verify_set, 8,
        "set verify / testCommand so --auto runs are gated on green");
    add(out, "constraints", f->constraints, 5,
        "add hard limits with /constraints (enforced every turn)");
    add(out, "edit-scope fence", f->edit_scope, 4,
        "set editScope to bound where --auto may write");
    add(out, "path fence", f->path_fence, 4,
        "keep the workspace path fence on (pathFence)");
    /* Productivity assets */
    add(out, "skills", f->has_skills, 6,
        "add .jichi/skills for reusable, progressively-disclosed guidance");
    add(out, "subagent profiles", f->has_agents, 6,
        "add .jichi/agents so tasks can be delegated to scoped personas");
    add(out, "custom commands", f->has_commands, 4,
        "add .jichi/commands for repeatable workflows");
    add(out, "memory notes", f->has_memory, 4,
        "let the agent keep durable notes (.jichi/memory.md)");
    add(out, "LSP servers", f->lsp_set, 6,
        "configure lspServers for navigation, diagnostics, refactors");
    add(out, "MCP servers", f->mcp_set, 3,
        "connect mcpServers to extend tools/resources");
    /* Economy */
    add(out, "cache pricing", f->cache_priced, 4,
        "set cacheReadCostPer1M/cacheWriteCostPer1M for accurate cost");
    add(out, "context declared", f->context_declared, 3,
        "declare contextLength so compaction sizes correctly");
    add(out, "telemetry", f->telemetry_on, 2,
        "enable logging to mine runs offline (telemetry/learn)");

    out->score = (out->max > 0)
        ? (int)(((long)out->got * 100 + out->max / 2) / out->max) : 0;
    out->grade = (out->score >= 85) ? 'A'
               : (out->score >= 70) ? 'B'
               : (out->score >= 55) ? 'C'
               : (out->score >= 40) ? 'D' : 'E';
}

void jc_confbench_render(const struct jc_confbench_report *r, int color,
                         int unicode, struct jc_sb *sb)
{
    int i;
    const char *ok, *no;
    char line[256];
    if (r == NULL || sb == NULL) return;
    ok = unicode ? "\xe2\x9c\x93" : "+"; /* check / + */
    no = unicode ? "\xe2\x97\x8b" : "-"; /* circle / - */

    jc_sb_append(sb, "Configuration benchmark\n\n");
    for (i = 0; i < r->n; i++) {
        const struct jc_confbench_item *it = &r->items[i];
        if (it->present) {
            jc_snprintf(line, sizeof line, "  %s %-20s (+%d)\n",
                        ok, it->name, it->weight);
            jc_sb_append(sb, line);
        } else {
            jc_snprintf(line, sizeof line, "  %s %-20s -- %s\n",
                        no, it->name, it->hint != NULL ? it->hint : "");
            jc_sb_append(sb, line);
        }
    }
    (void)color;
    jc_snprintf(line, sizeof line,
                "\n  score: %d/100  (grade %c)  [%d of %d points]\n",
                r->score, r->grade, r->got, r->max);
    jc_sb_append(sb, line);
}
