/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_subagent.c - the spawn_subagent tool.
 *
 * Delegates a scoped subtask to a fresh nested agent that runs synchronously to
 * completion (see jc_agent_run_subagent) and returns its final answer as the
 * tool result. The subagent may run on a different configured model and/or be
 * restricted to read-only tools. Recursion is bounded by config.max_subagent_depth
 * and the subagent never sees this tool advertised.
 */

#include "jc_delegreport.h"
#include "jc_envelope.h"
#include "tool_util.h"
#include "jc_output_style.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_provider.h"
#include "jc_sysmsg.h"
#include "jc_message.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

struct jc_model_cfg *jc_subagent_resolve_model(struct jc_config *cfg,
                                               const char *selector, int *found)
{
    int idx;
    unsigned role;

    if (found != NULL) {
        *found = 1;
    }
    /* No selector => the currently active model (a stable models-vector ptr). */
    if (selector == NULL || selector[0] == '\0') {
        return jc_config_model_at(cfg, cfg->active);
    }
    idx = jc_config_find_model(cfg, selector);
    if (idx >= 0) {
        return jc_config_model_at(cfg, idx);
    }
    role = jc_config_role_flag(selector);
    if (role != 0u) {
        struct jc_model_cfg *m = jc_config_model_for_role(cfg, role);
        if (m != NULL) {
            return m;
        }
    }
    if (found != NULL) {
        *found = 0;
    }
    return NULL;
}

static cJSON *subagent_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "task",
        "The subtask for the sub-agent to complete autonomously and report "
        "a concise final answer on.", 1);
    tu_schema_string(s, "model",
        "Optional model selector: a name/id substring, a 1-based index, or a "
        "role such as \"summarize\". Defaults to the current model.", 0);
    tu_schema_bool(s, "readonly",
        "If true, restrict the sub-agent to read-only tools (good for research "
        "or review). Default false.", 0);
    tu_schema_string(s, "agent",
        "Optional named agent profile (from .jichi/agents/*.md): uses its system "
        "prompt, model, and readonly setting. Explicit model/readonly override "
        "the profile.", 0);
    tu_schema_string(s, "skill",
        "Optional skill name (from load_skill's catalog): the sub-agent starts "
        "with that skill's full instructions loaded. If the skill declares "
        "restrict-tools, the sub-agent is fenced to the skill's allowed-tools "
        "for its whole run.", 0);
    return s;
}

static jc_status subagent_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    const char *task = tu_arg_str(args, "task");
    const char *arg_model = tu_arg_str(args, "model");
    const char *agent_name = tu_arg_str(args, "agent");
    const char *skill_name = tu_arg_str(args, "skill");
    int has_arg_ro = (cJSON_GetObjectItem(args, "readonly") != NULL);
    int arg_ro = tu_arg_bool(args, "readonly", 0);
    const struct jc_agentdef *def = NULL;
    const struct jc_skill *sk = NULL;
    const struct jc_vec *allow_fence;
    const char *seed_task;
    struct jc_vec skill_isect;
    int have_isect = 0;
    const char *model_sel;
    const char *sysmsg;
    struct jc_model_cfg *mc;
    struct jc_model_cfg *active;
    struct jc_provider *prov;
    char *answer = NULL;
    struct jc_history sub;
    int include_mutating;
    int ro;
    int temp = 0;
    int found = 1;
    jc_status st;
    struct jc_delegate_report rep;      /* M437 */
    double tok_before = 0.0;
    long calls_before = 0;

    if (!jc_subagent_can_spawn(app->agent_depth,
                               app->config.max_subagent_depth)) {
        tu_err(out, "error: sub-agent depth limit reached");
        return JC_OK;
    }
    if (task == NULL || task[0] == '\0') {
        tu_err(out, "error: 'task' argument is required");
        return JC_OK;
    }

    /* Resolve a named profile (if any) and merge with explicit args. */
    if (agent_name != NULL && agent_name[0] != '\0') {
        def = jc_agentdef_find(&app->agents, agent_name);
        if (def == NULL) {
            char msg[256];
            jc_snprintf(msg, sizeof(msg),
                        "error: no agent profile named '%s'", agent_name);
            tu_err(out, msg);
            return JC_OK;
        }
    }
    jc_agentdef_merge(def, arg_model, has_arg_ro, arg_ro, &model_sel, &ro);

    mc = jc_subagent_resolve_model(&app->config, model_sel, &found);
    if (!found) {
        char msg[256];
        jc_snprintf(msg, sizeof(msg), "error: no model matches '%s'",
                    model_sel != NULL ? model_sel : "");
        tu_err(out, msg);
        return JC_OK;
    }

    /* Reuse the parent provider when the model is unchanged; otherwise spin up a
     * temporary provider for the chosen model (freed below). */
    active = jc_config_model_at(&app->config, app->config.active);
    if (mc == NULL || mc == active) {
        prov = app->provider;
    } else {
        prov = jc_provider_create(mc);
        if (prov == NULL) {
            tu_err(out, "error: could not create a provider for that model");
            return JC_OK;
        }
        temp = 1;
    }

    /* A readonly subagent (or a readonly parent) sees only read-only tools. */
    include_mutating = ro ? 0 : !app->readonly;
    /* A profile body becomes the subagent system prompt; else the generic one. */
    /* M596: a profile body is the identity paragraph, and the sections jichi
     * enforces at depth (untrusted rule, constraints, edit scope -- M434) are
     * appended after it. Before M596 the bare profile text WAS the whole prompt,
     * so a profiled delegate was fenced by rules it had never seen. */
    sysmsg = jc_sysmsg_build_sub_as(app,
                                    (def != NULL) ? def->system_prompt : NULL,
                                    NULL);

    /* Resolve an optional skill: seed the subagent with its instructions, and
     * (when it declares restrict-tools) fence the subagent to its allowed-tools
     * for the whole run -- reusing the same opts.allow path a profile fence uses,
     * so the fence lives exactly as long as this subagent (no top-level linger). */
    seed_task = task;
    allow_fence = (def != NULL && def->tools.len > 0) ? &def->tools : NULL;
    if (skill_name != NULL && skill_name[0] != '\0') {
        sk = jc_skill_find(&app->skills, skill_name);
        if (sk == NULL) {
            char msg[256];
            jc_snprintf(msg, sizeof(msg), "error: no skill named '%s'",
                        skill_name);
            tu_err(out, msg);
            if (temp) { prov->vt->free(prov); }
            return JC_OK;
        }
        {
            struct jc_sb tb;
            jc_sb_init(&tb);
            jc_sb_append(&tb, "# Skill: ");
            jc_sb_append(&tb, sk->name);
            jc_sb_append(&tb, "\n\n");
            jc_sb_append(&tb, sk->body != NULL ? sk->body : "");
            jc_sb_append(&tb, "\n\n---\n\n# Task\n\n");
            jc_sb_append(&tb, task);
            seed_task = jc_arena_strdup(jc_app_scratch(app),
                                        tb.data != NULL ? tb.data : "");
            jc_sb_free(&tb);
        }
        if (sk->restrict_tools && sk->tools.len > 0) {
            if (allow_fence != NULL) {
                /* Both a profile and the skill fence: the effective fence is the
                 * intersection (tools common to both). */
                jc_vec_init(&skill_isect, sizeof(char *));
                jc_tool_allow_intersect(allow_fence, &sk->tools,
                                        jc_app_scratch(app), &skill_isect);
                allow_fence = &skill_isect;
                have_isect = 1;
            } else {
                allow_fence = &sk->tools;
            }
        }
    }

    /* M302: the specialist's TONE, appended to whichever system prompt was chosen
     * above. A skill's `style:` beats a profile's: the skill was named in this
     * call, so it is the more specific instruction for this invocation, whereas
     * the profile describes the worker in general. (Unlike `tools`, styles are not
     * intersected -- there is no meaningful intersection of two tones, so one has
     * to win and it should be the more specific one.) A name that resolves to no
     * style is left to `doctor` rather than silently dropped here. */
    {
        const char *sname = NULL;
        if (sk != NULL && sk->style != NULL && sk->style[0] != '\0') {
            sname = sk->style;
        } else if (def != NULL && def->style != NULL && def->style[0] != '\0') {
            sname = def->style;
        }
        if (sname != NULL) {
            const struct jc_output_style *os =
                jc_output_style_find(&app->output_styles, sname);
            if (os != NULL && os->body != NULL && os->body[0] != '\0') {
                struct jc_sb sm;
                jc_sb_init(&sm);
                jc_sb_append(&sm, sysmsg != NULL ? sysmsg : "");
                jc_sb_append(&sm, "\n\n# Output style\n\n");
                jc_sb_append(&sm, os->body);
                sysmsg = jc_arena_strdup(jc_app_scratch(app),
                                         sm.data != NULL ? sm.data : "");
                jc_sb_free(&sm);
            }
        }
    }

    jc_history_init(&sub);
    jc_history_add(&sub, JC_ROLE_USER, seed_task);

    /* Forward the live callbacks into the nested run only when the front-end
     * opted in (the TUI; headless/ACP leave stream_subagents 0 → silent, exactly
     * as before). */
    {
        const struct jc_agent_callbacks *cb_fwd =
            app->stream_subagents ? app->cb : NULL;
        int sub_iters;
        app->agent_depth++;
        /* Per-depth taper: a deeper synchronous chain gets a smaller iteration
         * budget so nesting shares, not multiplies, the total tool-call budget. */
        sub_iters = jc_subagent_iters_at_depth(app->config.max_subagent_iters,
                                               app->agent_depth);
        if (cb_fwd != NULL && cb_fwd->on_status != NULL) {
            const char *mname = (mc != NULL) ? mc->name
                                : (active != NULL ? active->name : NULL);
            char banner[400];
            char tshort[200];
            char depth_tag[32];
            jc_snprintf(tshort, sizeof(tshort), "%s", task);
            depth_tag[0] = '\0';
            if (app->agent_depth > 1) {
                jc_snprintf(depth_tag, sizeof(depth_tag), " [depth %d]",
                            app->agent_depth);
            }
            jc_snprintf(banner, sizeof(banner), "subagent %s%s%s: %s",
                        mname != NULL ? mname : "?", ro ? " \xc2\xb7 ro" : "",
                        depth_tag, tshort);
            cb_fwd->on_status(cb_fwd->user, banner);
        }
        /* M437: the counters live on the envelope, so they are known exactly when
         * one exists (--auto). Snapshotting the deltas is what jc_tool_parallel
         * already does per child; doing it the same way here is what lets one
         * renderer serve both tools. */
        tok_before = (app->env != NULL) ? app->env->tokens_used : 0.0;
        calls_before = (app->env != NULL)
                         ? (long)app->env->tool_calls_executed : 0;
        st = jc_agent_run_subagent(app, &sub, prov, sysmsg, include_mutating,
                                   sub_iters, allow_fence, cb_fwd, &answer);
        app->agent_depth--;
    }
    jc_delegreport_init(&rep);
    rep.stop = jc_delegreport_stop_from(st == JC_OK, st == JC_ERR_ABORTED,
                                        app->last_run_capped,
                                        app->last_run_budget_stopped,
                                        answer != NULL && answer[0] != '\0');
    if (app->env != NULL) {
        rep.tokens = app->env->tokens_used - tok_before;
        /* EXECUTED, not attempted: this report tells a parent what the
         * delegate actually did, and pairs the number with the denial that
         * explains a zero (M459 split the two meanings). */
        rep.tool_calls = (long)app->env->tool_calls_executed - calls_before;
    }
    /* The delegate's own last failing call, if it had one. Reported even on a
     * SUCCESSFUL delegation: an answer produced after three denials is a different
     * thing from one produced cleanly, and the parent is the only party that can
     * act on the denial. */
    if (app->last_fail_tool[0] != '\0') {
        jc_snprintf(rep.fail_tool, sizeof rep.fail_tool, "%s",
                    app->last_fail_tool);
        jc_snprintf(rep.fail_msg, sizeof rep.fail_msg, "%s",
                    app->last_fail_msg);
        rep.fail_cls = (enum jc_fail_class)app->last_fail_cls;
    }

    /* M437: ONE shape for every outcome. The two old failure strings ("error:
     * sub-agent interrupted" / "error: sub-agent run failed") told a parent
     * nothing it could act on, so its only moves were to re-delegate identically
     * -- paying the subtask twice -- or give up. Now every path carries the stop
     * reason, the measurements when they exist, and the delegate's last failing
     * call with its class. */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        if (rep.stop == JC_DELEG_DONE || rep.stop == JC_DELEG_MAX_ITERS ||
            rep.stop == JC_DELEG_BUDGET) {
            /* An answer exists on all three; the note is an addendum to it. */
            jc_sb_append(&sb, (answer != NULL) ? answer : "");
            jc_delegreport_render(&rep, &sb);
            tu_ok_owned(out, jc_sb_finish(&sb));
        } else {
            jc_sb_append(&sb, (rep.stop == JC_DELEG_ABORTED)
                              ? "error: sub-agent interrupted"
                              : (rep.stop == JC_DELEG_NO_ANSWER)
                                ? "error: sub-agent produced no final answer"
                                : "error: sub-agent run failed");
            jc_delegreport_render(&rep, &sb);
            /* tu_err copies, so the buffer is freed below like the ok path's
             * would be if it did not take ownership. */
            tu_err(out, (sb.data != NULL) ? sb.data : "error: sub-agent failed");
        }
        jc_sb_free(&sb);
    }

    jc_history_free(&sub);
    if (have_isect) {
        jc_vec_free(&skill_isect);
    }
    if (temp) {
        prov->vt->free(prov);
    }
    return JC_OK;
}

static const struct jc_tool SUBAGENT_TOOL = {
    "spawn_subagent",
    "Delegate a self-contained subtask to a fresh sub-agent that works "
    "autonomously and returns one final answer. Reach for it when the subtask "
    "needs no back-and-forth with you, when its detail would flood your "
    "context (read many files, return a short synthesis), or when a named "
    "agent profile (reviewer, tester) fits the job better than you. "
    "Optionally a different model, or read-only. Not for work that depends "
    "on this conversation's context: the sub-agent starts fresh and sees "
    "only the task you write.",
    subagent_schema,
    0, /* mutating: spawning an autonomous agent is a significant action */
    subagent_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_subagent(void)
{
    return &SUBAGENT_TOOL;
}
