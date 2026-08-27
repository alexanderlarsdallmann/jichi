/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_workflow.h - a deterministic multi-agent workflow spec (M101).
 *
 * spawn_subagent/spawn_parallel are *model-driven* -- the model decides to fan
 * out. A workflow is the opposite: a declarative spec drives the stages, so a
 * big audit/review/research pass is repeatable and doesn't depend on the model
 * choosing to orchestrate. The first slice is a read-only fan-out -> synthesize
 * pipeline:
 *   - a "map" stage runs one subagent per item (a `$ITEM`-templated prompt),
 *     collecting each answer;
 *   - a "synthesize" stage folds the collected answers into one result.
 * The harness runs the stages; each subtask is a normal (sandboxed) subagent.
 *
 * This header is the pure core: parse the JSON(C) spec and expand a stage
 * prompt. Execution (subagent/one-shot calls) lives in main.c's run_workflow.
 * Write-workflows (worktree-isolated map stages, like spawn_parallel) are a
 * documented follow-on; the first slice keeps map stages read-only by default.
 */
#ifndef JC_WORKFLOW_H
#define JC_WORKFLOW_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

#define JC_WF_MAX_STAGES 16
#define JC_WF_MAX_ITEMS  64

enum jc_wf_type { JC_WF_MAP = 0, JC_WF_SYNTHESIZE, JC_WF_VERIFY, JC_WF_UNKNOWN };

struct jc_wf_stage {
    int         type;                    /* enum jc_wf_type                 */
    const char *prompt;                  /* template ($ITEM in a map stage) */
    const char *command;                 /* verify: shell command to run    */
    const char *items[JC_WF_MAX_ITEMS];  /* map: the fan-out list           */
    int         nitems;
    const char *model;                   /* optional model selector, or NULL*/
    int         readonly;                /* map: run subagents read-only    */
};

struct jc_workflow {
    const char *name;
    struct jc_wf_stage stages[JC_WF_MAX_STAGES];
    int nstages;
    /* M610: what the parse SILENTLY dropped, so a caller can say so. A spec
     * with 70 items ran 64 and reported success; "no silent caps" (CLAUDE.md)
     * is the rule the task's OWN representation was breaking. `stages_dropped`
     * counts stages past JC_WF_MAX_STAGES plus non-object and unknown-type
     * entries (a stage you wrote that did not take effect); `items_dropped`
     * counts items past JC_WF_MAX_ITEMS across all stages. */
    int stages_dropped;
    int items_dropped;
};

/* Parse a JSON (JSONC-tolerant) workflow spec into *out (strings arena-owned).
 * Shape: {"name":..,"stages":[{"type":"map","prompt":..,"items":[..],
 * "model":..,"readonly":true} | {"type":"synthesize","prompt":..}]}. Returns
 * JC_ERR_PARSE on invalid JSON, JC_ERR_INVALID if there are no usable stages.
 * Pure. */
jc_status jc_workflow_parse(const char *json, struct jc_workflow *out,
                            struct jc_arena *a);

/* Expand a template: every "$ITEM" is replaced by `item` (arena-owned result).
 * A NULL `item` returns a copy of `tmpl` unchanged. Pure. */
char *jc_workflow_expand(const char *tmpl, const char *item,
                         struct jc_arena *a);

/* Wire name for a stage type (never NULL). */
const char *jc_wf_type_name(int type);

#ifdef __cplusplus
}
#endif
#endif /* JC_WORKFLOW_H */
