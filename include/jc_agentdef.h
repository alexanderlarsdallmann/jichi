/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agentdef.h - named agent profiles loaded from markdown.
 *
 * Markdown files under the project's .jichi/agents directory and the global
 * ~/.config/jichi/agents directory define reusable sub-agent profiles:
 * frontmatter (description, model, readonly, optional tools list, optional
 * `style:`) plus a body that becomes the agent's system prompt. The
 * spawn_subagent tool can target a profile by name via its `agent` argument.
 */
#ifndef JC_AGENTDEF_H
#define JC_AGENTDEF_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

struct jc_agentdef {
    char         *name;          /* file basename without .md, arena-owned   */
    char         *description;   /* frontmatter, or NULL                      */
    char         *model;         /* model selector, or NULL                   */
    int           readonly;      /* frontmatter readonly value                */
    int           has_readonly;  /* whether readonly was specified            */
    char         *system_prompt; /* body, arena-owned (may be empty)          */
    struct jc_vec tools;         /* of char*: tool allow-list, enforced when the
                                  * profile runs as a subagent (empty => all)  */
    char         *style;         /* M302: names an OUTPUT STYLE (see
                                  * jc_output_style.h) governing this
                                  * specialist's tone/format, or NULL. A name,
                                  * not prose: it reuses the M28 mechanism
                                  * rather than adding a second persona path,
                                  * so "blunt reviewer" and "patient tutor" are
                                  * configured in one place and shared. Applied
                                  * when the profile runs (a subagent, or a
                                  * command's `agent:`), NOT session-wide.     */
};

struct jc_agentdef_set {
    struct jc_vec defs; /* of struct jc_agentdef */
};

void jc_agentdef_set_init(struct jc_agentdef_set *s);
void jc_agentdef_set_free(struct jc_agentdef_set *s);

/* Load profiles from the global then project dirs (project overrides global on
 * name collision). Always returns JC_OK (missing dirs => empty set). */
jc_status jc_agentdef_load(struct jc_agentdef_set *s, const char *cwd,
                           struct jc_arena *a);

const struct jc_agentdef *jc_agentdef_find(const struct jc_agentdef_set *s,
                                           const char *name);

struct jc_sb; /* jc_str.h */

/* Render the set as a human-readable listing into `out` (one entry per profile:
 * name, model/readonly/tools attributes, and description). Pure; unit-tested.
 * Used by the `agents` subcommand. */
void jc_agentdef_render_list(const struct jc_agentdef_set *s, struct jc_sb *out);

/* Merge an agent profile with explicit spawn args (explicit args win).
 * `has_arg_ro` indicates the readonly arg was present. Outputs the effective
 * model selector (may be NULL => active model) and readonly flag. */
void jc_agentdef_merge(const struct jc_agentdef *def, const char *arg_model,
                       int has_arg_ro, int arg_ro,
                       const char **model_out, int *ro_out);

#ifdef __cplusplus
}
#endif
#endif /* JC_AGENTDEF_H */
