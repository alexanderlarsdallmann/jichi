/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_confbench.h - score a project's jichi configuration (M113).
 *
 * A "benchmark" of how well-set-up a project is for effective agentic work: a
 * weighted checklist of best-practice features (models + roles, safety net,
 * productivity assets, economy), a 0-100 score + letter grade, and a concrete
 * hint for each missing item. Distinct from `doctor` (which is health: does it
 * work) -- this is coverage (are you using what makes jichi effective).
 *
 * The scoring is a PURE function over a facts struct (gathered from the app by a
 * thin shell), so it is unit-tested without a live app.
 */
#ifndef JC_CONFBENCH_H
#define JC_CONFBENCH_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Observed configuration facts (1 = best-practice satisfied). Gathered from the
 * app/config by the shell; fed to the pure scorer. */
struct jc_confbench_facts {
    int has_active_model;  /* an active chat model is configured */
    int has_embed;         /* an embed-role model (enables RAG) */
    int has_rerank;        /* a rerank-role model (sharper retrieval) */
    int has_summarize;     /* a summarize-role model (better compaction) */
    int routing_ok;        /* routing enabled with distinct fast/strong tiers */
    int snapshots;         /* checkpoints/undo enabled */
    int verify_set;        /* a verify / testCommand gate is configured */
    int constraints;       /* at least one enforced constraint (M110) */
    int edit_scope;        /* an autonomy edit-scope fence is set */
    int path_fence;        /* the workspace path fence is on */
    int has_skills;        /* skills present (.jichi/skills) */
    int has_agents;        /* named subagent profiles present */
    int has_commands;      /* custom slash commands present */
    int has_memory;        /* persisted memory notes present */
    int lsp_set;           /* lspServers configured (navigation/refactor) */
    int mcp_set;           /* mcpServers configured */
    int cache_priced;      /* prompt-cache pricing set on a priced model */
    int context_declared;  /* contextLength / contextLimit declared */
    int telemetry_on;      /* event-log telemetry enabled */
};

#define JC_CONFBENCH_MAX_ITEMS 20

struct jc_confbench_item {
    const char *name;   /* short label (static string) */
    const char *hint;   /* what to do when absent (static string) */
    int present;        /* 1 if satisfied */
    int weight;         /* points contributed when present */
};

struct jc_confbench_report {
    struct jc_confbench_item items[JC_CONFBENCH_MAX_ITEMS];
    int n;
    int score; /* 0..100 (rounded) */
    int got;   /* raw points earned */
    int max;   /* raw points possible */
    char grade; /* 'A'..'E' */
};

/* Score the facts into `out` (pure; deterministic). */
void jc_confbench_score(const struct jc_confbench_facts *f,
                        struct jc_confbench_report *out);

/* Render the report to `sb` (✓/○ per item, hints for the missing, score + grade).
 * `color`/`unicode` pick glyphs; ASCII fallbacks otherwise. Pure. */
void jc_confbench_render(const struct jc_confbench_report *r, int color,
                         int unicode, struct jc_sb *sb);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONFBENCH_H */
