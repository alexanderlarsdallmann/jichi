/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_hint.c - the `hint` tool (learner-support layer).
 *
 * While solving an assignment (app->assignment set), reveals the NEXT hint from
 * the spec's graded ladder, escalating one level per call (nudge -> approach ->
 * worked step). On-demand, capped at the ladder length, and it reports how many
 * remain so a learner (human or agent) can decide whether to ask for another.
 * Read-only (changes nothing on disk), so not permission-gated. Registered only
 * when the assignments feature is enabled; a graceful no-op when no assignment
 * is active or the spec carries no hints.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_assign.h"
#include "jc_progress.h"
#include "jc_log.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>

static cJSON *hint_schema(void)
{
    /* No arguments: each call reveals the next hint in order. */
    return tu_schema_begin();
}

static jc_status hint_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const struct jc_assign_spec *spec = app->assignment;
    struct jc_sb sb;
    char head[64];

    (void)args;
    if (spec == NULL || spec->nhints <= 0 || spec->hints == NULL) {
        tu_ok_owned(out, jc_strdup(
            "No hints are available right now (no active assignment, or it "
            "carries no hints). Proceed with your own approach."));
        return JC_OK;
    }
    /* M617: in tutor mode the HUMAN is the learner and the ladder is theirs.
     * The prompt says "guide, never solve", but a prompt binds only the model
     * that reads it (caps-vs-fences): nothing stopped a tutor-stance model
     * from burning the human's rungs -- advancing hints_used and writing
     * hints.jsonl rows attributed to the learner. Fenced: no rung revealed,
     * no counter moved, and the reply says whose ladder it is. */
    if (app->assignment_tutor) {
        tu_ok_owned(out, jc_strdup(
            "The hint ladder belongs to the human learner (tutor mode) -- do "
            "not spend their rungs. Offer a nudge in your own words instead; "
            "the learner reveals hints themselves with /hint."));
        return JC_OK;
    }
    if (app->hints_used >= spec->nhints) {
        char msg[256];
        jc_snprintf(msg, sizeof(msg),
            "No more hints -- you have seen all %d. You have everything the "
            "assignment offers; try ask_for_help for a clarification, or work "
            "it through.", spec->nhints);
        tu_ok_copy(out, msg);
        return JC_OK;
    }

    jc_sb_init(&sb);
    jc_snprintf(head, sizeof(head), "Hint %d of %d:\n\n",
                app->hints_used + 1, spec->nhints);
    jc_sb_append(&sb, head);
    jc_sb_append(&sb, spec->hints[app->hints_used]);
    app->hints_used++;
    /* M536: keep the promise this tool's own description makes. The rung is
     * 1-based, matching `jichi hint <spec> N` and jc_progress_hints_scan's
     * reader; app->hints_used is that number now, after the increment.
     *
     * It lands in a SEPARATE sink (.jichi/hints.jsonl), never progress.jsonl,
     * because every reader there counts a line as a graded attempt -- that
     * separation is what keeps "never silently penalised" true by construction
     * rather than by everyone remembering (M502's reasoning, unchanged).
     *
     * assignment_dir, not "." and not app->cwd: `attempt` runs the whole turn
     * inside a git worktree that is deleted afterwards, so the obvious spelling
     * would write the teacher's diagnostic into the sandbox and destroy it.
     *
     * A write failure is not worth failing the tool over -- the learner still
     * got their hint -- but it IS worth saying so, because the description
     * promised a record and silence about a missing record is how this defect
     * survived from M174 to now. */
    if (app->assignment_spec != NULL && app->assignment_dir[0] != '\0') {
        if (jc_progress_hint_append(app->assignment_dir, app->assignment_spec,
                                    app->hints_used) != JC_OK) {
            jc_logf(JC_LOG_WARN, "hint: could not record rung %d of '%s' in "
                    "%s/.jichi/hints.jsonl -- the hint stands, the teacher's "
                    "ladder record does not", app->hints_used,
                    app->assignment_spec, app->assignment_dir);
        }
    } else {
        jc_logf(JC_LOG_DEBUG, "hint: no spec path armed, so this pull is not "
                "recorded (assignment set without assignment_spec)");
    }
    {
        int remaining = spec->nhints - app->hints_used;
        if (remaining > 0) {
            jc_snprintf(head, sizeof(head),
                        "\n\n(%d more hint%s available if you stay stuck.)",
                        remaining, remaining == 1 ? "" : "s");
        } else {
            jc_snprintf(head, sizeof(head),
                        "\n\n(That was the last hint.)");
        }
        jc_sb_append(&sb, head);
    }
    tu_ok_owned(out, jc_sb_finish(&sb));
    jc_sb_free(&sb);
    return JC_OK;
}

static const struct jc_tool HINT_TOOL = {
    "hint",
    "Reveal the next hint for the assignment you are solving, escalating from "
    "a gentle nudge toward a worked step. Use it when genuinely stuck -- hints "
    "are limited and their use is recorded (never silently penalised). Returns "
    "the hint plus how many remain.",
    hint_schema,
    1, /* read-only */
    hint_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_hint(void)
{
    return &HINT_TOOL;
}
