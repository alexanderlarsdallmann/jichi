/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_skill.c - the load_skill tool.
 *
 * Returns the full SKILL.md body for a named skill so the agent can follow its
 * instructions. Read-only (it only reads skill text). Registered (and so
 * advertised) only when at least one skill is configured; the system prompt's
 * "Available skills" catalog tells the model which names exist.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_skill.h"
#include "jc_str.h"

#include <stdlib.h>

static cJSON *load_skill_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "name",
                     "The skill name from the Available Skills list.", 1);
    return s;
}

/* Build an error result listing the available skill names. */
static void no_such_skill(struct jc_app *app, const char *name,
                          struct jc_tool_result *out)
{
    struct jc_sb sb;
    int n = jc_skill_count(&app->skills);
    int i;

    jc_sb_init(&sb);
    jc_sb_append_fmt(&sb, "error: no skill named '%s'. ",
                     name != NULL ? name : "");
    if (n == 0) {
        jc_sb_append(&sb, "No skills are configured.");
    } else {
        jc_sb_append(&sb, "Available skills:");
        for (i = 0; i < n; i++) {
            const struct jc_skill *sk = jc_skill_at(&app->skills, i);
            jc_sb_append_char(&sb, ' ');
            jc_sb_append(&sb, sk->name);
            if (i + 1 < n) {
                jc_sb_append_char(&sb, ',');
            }
        }
    }
    out->content = jc_sb_finish(&sb);
    out->is_error = 1;
    jc_sb_free(&sb);
}

static jc_status load_skill_run(const cJSON *args, struct jc_tool_result *out,
                                struct jc_app *app)
{
    const char *name = tu_arg_str(args, "name");
    const struct jc_skill *sk;
    struct jc_sb sb;

    if (name == NULL || name[0] == '\0') {
        tu_err(out, "error: 'name' is required");
        return JC_OK;
    }
    sk = jc_skill_find(&app->skills, name);
    if (sk == NULL) {
        no_such_skill(app, name, out);
        return JC_OK;
    }

    /* Loading a skill injects its guidance; it does NOT restrict the agent's
     * tools. A skill's `allowed-tools` is advisory only -- rendered below as a
     * hint. Tool restriction lives in subagent profiles (.jichi/agents `tools:`)
     * and modes/permissions, not in skills (skills are never "deactivated", so
     * a restriction set on load would linger for the whole turn). See
     * docs/SKILLS.md "Design note" for the rationale and the revival recipe. */

    jc_sb_init(&sb);
    jc_sb_append_fmt(&sb, "# Skill: %s\n\n", sk->name);
    jc_sb_append(&sb, (sk->body != NULL && sk->body[0] != '\0')
                          ? sk->body : "(this skill has no instructions)");
    jc_sb_append_fmt(&sb,
        "\n\n(Skill directory: %s - any bundled scripts or resources "
        "referenced above live here; run them with run_terminal_command.)\n",
        (sk->dir != NULL) ? sk->dir : "");
    if (sk->tools.len > 0) {
        jc_size k;
        jc_sb_append(&sb, "(Suggested tools for this skill:");
        for (k = 0; k < sk->tools.len; k++) {
            const char *tn =
                *(char **)jc_vec_at((struct jc_vec *)&sk->tools, k);
            jc_sb_append_char(&sb, ' ');
            jc_sb_append(&sb, tn);
            if (k + 1 < sk->tools.len) {
                jc_sb_append_char(&sb, ',');
            }
        }
        if (sk->restrict_tools) {
            jc_sb_append(&sb, ". These are recommendations here; when this skill "
                              "is run inside a sub-agent (spawn_subagent skill=\"");
            jc_sb_append(&sb, sk->name);
            jc_sb_append(&sb, "\") it declares restrict-tools, so the sub-agent is "
                              "fenced to exactly these tools.)\n");
        } else {
            jc_sb_append(&sb,
                         ". These are recommendations, not restrictions.)\n");
        }
    }
    /* M302: report a declared `style:` rather than applying it. Same
     * advisory/enforced split as `tools` above, and for a concrete reason: this is
     * a TOOL RESULT, and a tool result cannot retroactively change the system
     * prompt of the turn that called it. Inside a subagent the style IS applied
     * (spawn_subagent skill="..."), where it can be part of the prompt from the
     * start. Naming the style here still helps -- the model can adopt the tone by
     * reading it, which is exactly how the tools hint works. */
    if (sk->style != NULL && sk->style[0] != '\0') {
        jc_sb_append(&sb, "(Suggested output style for this skill: ");
        jc_sb_append(&sb, sk->style);
        jc_sb_append(&sb, ". A recommendation here; when this skill is run inside "
                          "a sub-agent (spawn_subagent skill=\"");
        jc_sb_append(&sb, sk->name);
        jc_sb_append(&sb, "\") that style governs the sub-agent's tone.)\n");
    }
    out->content = jc_sb_finish(&sb);
    out->is_error = 0;
    jc_sb_free(&sb);
    return JC_OK;
}

static const struct jc_tool SKILL_TOOL = {
    "load_skill",
    "Load the full step-by-step instructions for a named skill from the "
    "Available Skills list, then follow them. Call this when the user's task "
    "matches a skill's description.",
    load_skill_schema,
    1, /* readonly: only reads skill instructions */
    load_skill_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_skill(void)
{
    return &SKILL_TOOL;
}
