/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_help.c - the `ask_for_help` tool (learner-support layer, B3).
 *
 * A learner (human or agent) working an assignment can ask for a clarification
 * or a nudge. Routing, per the design:
 *   - Interactive + top-level: route to the human via the app->ask delegate
 *     (same primitive ask_user uses).
 *   - Otherwise (headless / --auto / no human): delegate to a read-only
 *     `assignment-helper` sub-agent, seeded with the question + the active
 *     assignment's context, which answers with a hint-level nudge (never the
 *     full solution). Falls back to a "proceed" note when neither is available.
 * Read-only (it changes nothing on disk itself), so not permission-gated.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_assign.h"
#include "jc_agent.h"
#include "jc_agentdef.h"
#include "jc_sysmsg.h"
#include "jc_message.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>

static cJSON *ask_help_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "question",
                     "What you want clarified, or where you are stuck", 1);
    return s;
}

/* Build the helper sub-agent's prompt: the question + the assignment context,
 * with an explicit instruction to give a nudge, not the solution. */
static char *build_helper_prompt(struct jc_app *app, const char *question)
{
    const struct jc_assign_spec *spec = app->assignment;
    struct jc_sb sb;
    jc_sb_init(&sb);
    jc_sb_append(&sb,
        "A learner is working on the assignment below and has asked for help. "
        "Give a brief, hint-level nudge: clarify their question or point them at "
        "what to read or reconsider. Do NOT write the solution or the exact code "
        "-- guide them to find it.\n\n");
    if (spec != NULL) {
        if (spec->title != NULL) {
            jc_sb_append(&sb, "Assignment: ");
            jc_sb_append(&sb, spec->title);
            jc_sb_append(&sb, "\n");
        }
        if (spec->task != NULL) {
            jc_sb_append(&sb, "Task: ");
            jc_sb_append(&sb, spec->task);
            jc_sb_append(&sb, "\n");
        }
        if (spec->verify != NULL) {
            jc_sb_append(&sb, "Checked by: ");
            jc_sb_append(&sb, spec->verify);
            jc_sb_append(&sb, "\n");
        }
        jc_sb_append(&sb, "\n");
    }
    jc_sb_append(&sb, "Their question: ");
    jc_sb_append(&sb, question);
    jc_sb_append(&sb, "\n");
    return jc_sb_finish(&sb);
}

/* Run the read-only `assignment-helper` tutor sub-agent on `question` and
 * return its answer (malloc'd) via *answer_out. Factored out of the tool so the
 * TUI's /tutor can reach the SAME tutor without going through the interactive
 * ask-the-human routing (which would ask the learner to help themselves --
 * exactly backwards for a self-learner). Returns JC_ERR_NOTFOUND when no
 * helper can run (no provider, or subagent depth exhausted). M173b. */
jc_status jc_tool_help_tutor(struct jc_app *app, const char *question,
                             char **answer_out)
{
    const struct jc_agentdef *def;
    const char *sysmsg;
    char *prompt;
    struct jc_history sub;
    jc_status st;

    if (answer_out == NULL) {
        return JC_ERR_INVALID;
    }
    *answer_out = NULL;
    if (!jc_subagent_can_spawn(app->agent_depth, app->config.max_subagent_depth)
        || app->provider == NULL) {
        return JC_ERR_NOTFOUND;
    }

    def = jc_agentdef_find(&app->agents, "assignment-helper");
    /* M596: a profile body is the identity paragraph, and the sections jichi
     * enforces at depth (untrusted rule, constraints, edit scope -- M434) are
     * appended after it. Before M596 the bare profile text WAS the whole prompt,
     * so a profiled delegate was fenced by rules it had never seen. */
    sysmsg = jc_sysmsg_build_sub_as(app,
                                    (def != NULL) ? def->system_prompt : NULL,
                                    NULL);
    prompt = build_helper_prompt(app, question);

    jc_history_init(&sub);
    jc_history_add(&sub, JC_ROLE_USER, prompt != NULL ? prompt : question);
    free(prompt);

    app->agent_depth++;
    st = jc_agent_run_subagent(app, &sub, app->provider, sysmsg,
                               0 /* read-only helper */,
                               app->config.max_subagent_iters,
                               (def != NULL && def->tools.len > 0)
                                   ? &def->tools : NULL,
                               NULL, answer_out);
    app->agent_depth--;
    jc_history_free(&sub);
    return st;
}

static jc_status ask_help_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    const char *question = tu_arg_str(args, "question");
    char *answer = NULL;
    jc_status st;

    if (question == NULL || question[0] == '\0') {
        tu_err(out, "error: 'question' argument is required");
        return JC_OK;
    }

    /* Interactive human path: only at top level with a delegate that can prompt
     * (the TUI). A non-JC_OK/declined answer falls through to the helper agent. */
    if (app->agent_depth == 0 && app->ask != NULL && app->ask->ask != NULL) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        if (app->ask->ask(app->ask->ctx, question, NULL, 0, &sb) == JC_OK &&
            sb.data != NULL && sb.data[0] != '\0') {
            tu_ok_owned(out, jc_sb_finish(&sb));
            jc_sb_free(&sb);
            return JC_OK;
        }
        jc_sb_free(&sb);
        /* no human answer -> try the helper agent below */
    }

    /* Helper-agent path (headless, or the human declined). */
    st = jc_tool_help_tutor(app, question, &answer);
    if (st == JC_ERR_NOTFOUND) {
        tu_ok_owned(out, jc_strdup(
            "No helper is available right now. Re-read the task and the code it "
            "names, try the `hint` tool, and proceed with your best judgment."));
        return JC_OK;
    }

    if (st == JC_OK && answer != NULL && answer[0] != '\0') {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_sb_append(&sb, "A helper responds (a nudge, not the full "
                          "solution):\n\n");
        jc_sb_append(&sb, answer);
        tu_ok_owned(out, jc_sb_finish(&sb));
        jc_sb_free(&sb);
    } else {
        tu_ok_owned(out, jc_strdup(
            "The helper had nothing to add. Re-read the task, try the `hint` "
            "tool, and proceed with your best judgment."));
    }
    return JC_OK;
}

static const struct jc_tool ASK_HELP_TOOL = {
    "ask_for_help",
    "Ask for help or a clarification while solving an assignment. Interactively "
    "this reaches the user; otherwise a read-only helper gives a hint-level "
    "nudge (never the full solution). Use it when a genuine ambiguity or a "
    "sticking point is blocking you -- not for routine steps.",
    ask_help_schema,
    1, /* read-only */
    ask_help_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_ask_for_help(void)
{
    return &ASK_HELP_TOOL;
}
