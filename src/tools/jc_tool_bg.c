/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_bg.c - background-process tools (M26).
 *
 * read_background_output (Claude Code's BashOutput) and kill_background
 * (KillShell). The processes themselves are started by run_terminal_command
 * with run_in_background:true (see jc_tool_run.c) and live on app->bg.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_bg.h"
#include "jc_str.h"
#include "jc_snprintf.h"

static cJSON *read_bg_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_int(s, "id", "Background process id (from run_terminal_command "
                  "with run_in_background)", 1);
    return s;
}

static jc_status read_bg_run(const cJSON *args, struct jc_tool_result *out,
                             struct jc_app *app)
{
    int id = tu_arg_int(args, "id", 0);
    struct jc_sb sb;

    if (app->bg == NULL) {
        tu_err(out, "error: background processes are not available");
        return JC_OK;
    }
    if (id <= 0) {
        tu_err(out, "error: 'id' must be a positive background process id");
        return JC_OK;
    }
    jc_sb_init(&sb);
    if (jc_bg_read(app->bg, id, &sb) != JC_OK) {
        jc_sb_free(&sb);
        tu_err(out, "error: no such background process");
        return JC_OK;
    }
    tu_ok_owned(out, jc_sb_finish(&sb));
    jc_sb_free(&sb);
    return JC_OK;
}

static cJSON *kill_bg_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_int(s, "id", "Background process id to terminate", 1);
    return s;
}

static jc_status kill_bg_run(const cJSON *args, struct jc_tool_result *out,
                             struct jc_app *app)
{
    int id = tu_arg_int(args, "id", 0);
    char msg[96];

    if (app->bg == NULL) {
        tu_err(out, "error: background processes are not available");
        return JC_OK;
    }
    if (id <= 0) {
        tu_err(out, "error: 'id' must be a positive background process id");
        return JC_OK;
    }
    if (jc_bg_kill(app->bg, id) != JC_OK) {
        tu_err(out, "error: no such background process");
        return JC_OK;
    }
    jc_snprintf(msg, sizeof(msg), "Killed background process %d.", id);
    tu_ok_copy(out, msg);
    return JC_OK;
}

static const struct jc_tool READ_BG_TOOL = {
    "read_background_output",
    "Read new output from a background command started with "
    "run_terminal_command (run_in_background), plus its running/exited status.",
    read_bg_schema,
    1, /* read-only */
    read_bg_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

static const struct jc_tool KILL_BG_TOOL = {
    "kill_background",
    "Terminate a background command started with run_terminal_command "
    "(run_in_background).",
    kill_bg_schema,
    0, /* mutating */
    kill_bg_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_read_background(void)
{
    return &READ_BG_TOOL;
}

const struct jc_tool *jc_tool_kill_background(void)
{
    return &KILL_BG_TOOL;
}
