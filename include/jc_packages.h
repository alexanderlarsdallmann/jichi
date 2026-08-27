/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_packages.h - browse the available agent/skill/tool packages + get an
 * LLM-assisted (or preset-fallback) recommendation for a project (M115).
 *
 * "Packages" here are the compiled-in scaffold packs (each bundling an AGENTS.md,
 * agents, skills, commands, a config example) and the setup presets (role recipes
 * combining a pack + defaults). This is the pure layer: render the catalog, and
 * build the recommendation prompt. The one-shot model call + reading the project's
 * files live in the thin shell.
 */
#ifndef JC_PACKAGES_H
#define JC_PACKAGES_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Render the catalog of available scaffold packs + setup presets (name +
 * one-line description each) to `sb`. Pure (reads the compiled-in tables). */
void jc_packages_render_catalog(struct jc_sb *sb);

/* Build a recommendation prompt for the model: the available packs/presets plus
 * `project_summary` (e.g. a list of the project's top-level files), asking for a
 * concise, justified recommendation of a preset + a few skills/agents. Pure. */
void jc_packages_recommend_prompt(const char *project_summary, struct jc_sb *sb);

#ifdef __cplusplus
}
#endif
#endif /* JC_PACKAGES_H */
