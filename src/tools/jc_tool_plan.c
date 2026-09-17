/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_plan.c - write_plan: the one tool that writes the plan artifact
 * (M631). See jc_plan.h for what the artifact is and why.
 *
 * Two properties matter more than the happy path:
 *   1. ONE PATH. The tool writes .jichi/PLAN.md and nothing else -- the
 *      argument carries no path. It is therefore allowed in plan mode
 *      (`plan_allowed`, honoured by the permission verdict and by the tool
 *      advertiser) without being read-only: it IS a write, declared as the
 *      one write plan mode exists to produce. It does not go through the
 *      jc_app_write_file chokepoint on purpose, so the plan never counts as
 *      a file "the run wrote" when the run is later reconciled against it.
 *   2. AN INCOMPLETE PLAN IS AN ERROR VALUE, NOT A FILE. The five sections are
 *      checked before anything is written; a missing one comes back as a tool
 *      result with is_error set and the section named -- the same shape every
 *      other tool refusal has (errors are values, never control flow), so the
 *      model reads what is missing and writes it. */
#include "jc_tool.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_plan.h"
#include "jc_envelope.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

static const char *plan_root(const struct jc_app *app)
{
    return app->root[0] != '\0' ? app->root : app->cwd;
}

static cJSON *plan_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "claim",
        "What will change and why -- one paragraph a reviewer could disagree with", 1);
    tu_schema_string(s, "rejected",
        "At least one alternative you considered and dropped, each as "
        "'<alternative> -- <why it lost>'; separate several with newlines", 1);
    tu_schema_string(s, "falsifier",
        "The observation that would show this plan wrong (a test going red, a "
        "byte of output differing, a number)", 1);
    tu_schema_string(s, "not_goals",
        "What this deliberately leaves alone", 1);
    tu_schema_string(s, "touches",
        "The files the work expects to change, root-relative, one per line -- "
        "the run is reconciled against this list", 1);
    return s;
}

static jc_status plan_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *claim = tu_arg_str(args, "claim");
    const char *rejected = tu_arg_str(args, "rejected");
    const char *falsifier = tu_arg_str(args, "falsifier");
    const char *notgoals = tu_arg_str(args, "not_goals");
    const char *touches = tu_arg_str(args, "touches");
    char *md;
    struct jc_plan p;
    const char *missing;
    char path[1200];
    char dir[1150];
    char msg[400];

    md = jc_plan_render(claim, rejected, falsifier, notgoals, touches);
    if (md == NULL) {
        tu_err(out, "error: out of memory rendering the plan");
        return JC_OK;
    }
    jc_plan_parse(md, &p, jc_app_tool_scratch(app));
    missing = jc_plan_missing(&p);
    if (missing != NULL) {
        jc_snprintf(msg, sizeof msg, "error: %s", missing);
        tu_err(out, msg);
        jc_plan_free(&p);
        free(md);
        return JC_OK;
    }
    /* Beside the other .jichi records, at the workspace root -- the same root
     * the envelope makes its `wrote` paths relative to, so Touches and wrote
     * compare in one coordinate system. */
    jc_snprintf(dir, sizeof dir, "%s/.jichi", plan_root(app));
    jc_mkdir_p(dir);
    jc_snprintf(path, sizeof path, "%s/%s", plan_root(app), JC_PLAN_PATH);
    if (jc_write_file(path, md, (jc_size)strlen(md)) != JC_OK) {
        tu_err(out, "error: could not write .jichi/PLAN.md");
        jc_plan_free(&p);
        free(md);
        return JC_OK;
    }
    /* Journaled when an envelope is armed: the plan's shape is part of the
     * run's record, the way an ask is (M359). */
    if (app->env != NULL) {
        cJSON *o = jc_env_journal_begin(app->env, "plan");
        if (o != NULL) {
            cJSON_AddNumberToObject(o, "rejected", (double)p.n_rejected);
            cJSON_AddNumberToObject(o, "touches", (double)p.touches.len);
            jc_env_journal_end(app->env, o);
        }
    }
    jc_snprintf(msg, sizeof msg,
                "plan written to %s: %d rejected alternative(s), %lu file(s) "
                "under Touches. The run will be reconciled against Touches; "
                "tell the user to leave plan mode (/plan off) to carry it out.",
                JC_PLAN_PATH, p.n_rejected, (unsigned long)p.touches.len);
    tu_ok_copy(out, msg);
    jc_plan_free(&p);
    free(md);
    return JC_OK;
}

static const struct jc_tool WRITE_PLAN_TOOL = {
    "write_plan",
    "Write the plan for this task to .jichi/PLAN.md -- the only path this tool "
    "can write. Five parts, all required: claim (what changes and why), "
    "rejected (>= 1 alternative with why it lost), falsifier (what would show "
    "the plan wrong), not_goals, and touches (the files the work will change; "
    "the run is reconciled against this list). Allowed in plan mode. An "
    "incomplete plan is refused with the missing part named.",
    plan_schema,
    0, /* NOT read-only: it writes -- one declared path */
    plan_run,
    NULL, NULL, NULL,
    0, /* main_agent_only */
    1  /* plan_allowed (M631): the one write plan mode exists to produce */
};

const struct jc_tool *jc_tool_write_plan(void)
{
    return &WRITE_PLAN_TOOL;
}
