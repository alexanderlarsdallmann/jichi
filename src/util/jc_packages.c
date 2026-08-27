/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_packages.c - package/preset catalog + recommendation prompt (pure). */

#include "jc_packages.h"
#include "jc_scaffold.h"
#include "jc_setup.h"

#include <string.h>

void jc_packages_render_catalog(struct jc_sb *sb)
{
    int i, n;
    if (sb == NULL) return;

    jc_sb_append(sb, "Setup presets (role recipes -- combine a pack + defaults):\n");
    n = jc_setup_preset_count();
    for (i = 0; i < n; i++) {
        const struct jc_setup_preset *p = jc_setup_preset_at(i);
        if (p == NULL) continue;
        jc_sb_append(sb, "  ");
        jc_sb_append(sb, p->name != NULL ? p->name : "?");
        if (p->description != NULL) {
            jc_sb_append(sb, " -- ");
            jc_sb_append(sb, p->description);
        }
        jc_sb_append(sb, "\n");
    }

    jc_sb_append(sb, "\nScaffold packs (assets: AGENTS.md + agents + skills + "
                     "commands + a config example):\n");
    n = jc_scaffold_pack_count();
    for (i = 0; i < n; i++) {
        const struct jc_scaffold_pack *p = jc_scaffold_pack_at(i);
        if (p == NULL) continue;
        jc_sb_append(sb, "  ");
        jc_sb_append(sb, p->name != NULL ? p->name : "?");
        if (p->description != NULL) {
            jc_sb_append(sb, " -- ");
            jc_sb_append(sb, p->description);
        }
        jc_sb_append(sb, "\n");
    }
}

void jc_packages_recommend_prompt(const char *project_summary, struct jc_sb *sb)
{
    if (sb == NULL) return;
    jc_sb_append(sb,
        "You are advising which jichi setup to use for a project.\n"
        "Below are the available presets and scaffold packs, then a summary of "
        "the project's files. Recommend ONE preset and up to three "
        "skills/agents/tools worth enabling, each with a one-line reason. Be "
        "concise (a short bulleted list); do not invent packs that aren't "
        "listed.\n\n");
    jc_packages_render_catalog(sb);
    jc_sb_append(sb, "\nProject summary:\n");
    jc_sb_append(sb, project_summary != NULL ? project_summary : "(unknown)");
    jc_sb_append(sb, "\n\nRecommendation:\n");
}
