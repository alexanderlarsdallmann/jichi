/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_skill.h - agent skills: model-invoked, progressively-disclosed instructions.
 *
 * A skill is a folder with a SKILL.md (YAML frontmatter `name`/`description`
 * plus a markdown body). Only names + descriptions are injected into the system
 * prompt; the agent calls the load_skill tool to pull a skill's full body into
 * context on demand. Discovered under the project's .jichi/skills and the global
 * ~/.config/jichi/skills directories. See docs/SKILLS.md.
 *
 * The parse / catalog / find helpers are pure and unit-tested; discovery and
 * the tool are verified end-to-end.
 */
#ifndef JC_SKILL_H
#define JC_SKILL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_str.h"

struct jc_skill {
    char *name;        /* identifier passed to load_skill (arena-owned)        */
    char *description; /* one-line summary shown to the agent (may be empty)   */
    char *body;        /* full instructions (the SKILL.md body; may be empty)  */
    char *dir;         /* the skill's folder, for bundled scripts/resources    */
    struct jc_vec tools; /* of char*: allowed-tools fence (empty => no limit)  */
    int restrict_tools;  /* frontmatter `restrict-tools: true`: enforce `tools`
                          * as a fence when the skill is loaded inside a subagent
                          * (advisory at top level; see docs/SKILLS.md)         */
    char *style;         /* M302: names an OUTPUT STYLE for this skill's tone,
                          * or NULL. Follows the SAME advisory/enforced split as
                          * `tools` above -- ENFORCED when the skill seeds a
                          * subagent (spawn_subagent's `skill` arg), and merely
                          * REPORTED by load_skill at top level, because a tool
                          * result cannot retroactively change the system prompt
                          * of the turn that called it.                        */
};

struct jc_skill_set {
    struct jc_vec skills; /* of struct jc_skill */
};

void jc_skill_set_init(struct jc_skill_set *s);
void jc_skill_set_free(struct jc_skill_set *s);

/* Load skills from the global then project dirs (project overrides global on a
 * name collision). Always returns JC_OK (missing dirs => empty set). */
jc_status jc_skill_load(struct jc_skill_set *s, const char *cwd,
                        struct jc_arena *a);

const struct jc_skill *jc_skill_find(const struct jc_skill_set *s,
                                     const char *name);
int jc_skill_count(const struct jc_skill_set *s);
const struct jc_skill *jc_skill_at(const struct jc_skill_set *s, int i);

/* Parse one SKILL.md `text` into *out (pure; exposed for tests). `name` falls
 * back to `default_name` when frontmatter has none; `dir` is recorded so the
 * agent can find bundled files. All fields are arena-owned. */
void jc_skill_parse(const char *text, const char *default_name,
                    const char *dir, struct jc_arena *a, struct jc_skill *out);

/* Append the system-prompt "Available skills" catalog (names + descriptions +
 * usage instruction) to `out`. Appends nothing when the set is empty. */
void jc_skill_render_catalog(const struct jc_skill_set *s, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_SKILL_H */
