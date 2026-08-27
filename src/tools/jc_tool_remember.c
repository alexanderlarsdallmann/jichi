/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_remember.c - the remember tool: save a durable note.
 *
 * Appends a note to <cwd>/.jichi/memory.md, which is loaded into the system prompt
 * at the start of every session. Lets the agent persist project facts,
 * conventions, decisions, and user preferences across sessions. Marked mutating
 * (it writes a file), so it flows through the permission gate like other edits;
 * users who want it silent can add "remember" to permissions.allow.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_memory.h"
#include "jc_snprintf.h"

#include <string.h>

static cJSON *remember_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "note",
        "A concise, self-contained fact worth remembering across sessions "
        "(a project convention, a decision, a user preference). One idea per "
        "note.", 1);
    return s;
}

static jc_status remember_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    const char *note = tu_arg_str(args, "note");
    int was_new = 0;
    jc_status st;

    if (note == NULL || note[0] == '\0') {
        tu_err(out, "error: 'note' is required");
        return JC_OK;
    }
    st = jc_memory_add(app, note, &was_new);
    if (st == JC_ERR_INVALID) {
        tu_err(out, "error: note is empty after normalization");
        return JC_OK;
    }
    if (st != JC_OK) {
        tu_err(out, "error: could not write .jichi/memory.md");
        return JC_OK;
    }
    /* Refresh the in-memory copy so a later prompt this session sees it. */
    jc_memory_refresh(app); /* M199 */
    /* M143: the file outgrowing the injection budget must not be silent --
     * the oldest notes are no longer loaded into the prompt. */
    if (jc_memory_file_size(app) > JC_MEMORY_MAX) {
        tu_ok_copy(out, was_new
            ? "Noted; saved to .jichi/memory.md. WARNING: memory.md now "
              "exceeds the 8 KB injection budget -- the OLDEST notes are no "
              "longer loaded into the prompt. Prune the file, or supersede "
              "stale notes: add a '## Corrections' section to "
              ".jichi/lessons.draft.md (via /learn) and run `learn "
              "corrections`."
            : "Already remembered. WARNING: memory.md exceeds the 8 KB "
              "injection budget -- the OLDEST notes are no longer loaded "
              "into the prompt.");
    } else {
        tu_ok_copy(out, was_new ? "Noted; saved to .jichi/memory.md."
                                : "Already remembered.");
    }
    return JC_OK;
}

static const struct jc_tool REMEMBER_TOOL = {
    "remember",
    "Save a durable note to project memory (.jichi/memory.md), loaded into context "
    "at the start of every session. Use it for facts worth keeping across "
    "sessions: project conventions, architectural decisions, gotchas, or user "
    "preferences. Keep each note short and self-contained.",
    remember_schema,
    0, /* mutating: it writes a file */
    remember_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_remember(void)
{
    return &REMEMBER_TOOL;
}
