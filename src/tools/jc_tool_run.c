/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_run.c - the run_terminal_command tool.
 *
 * Output is captured and capped. Execution goes through jc_app_run_command, so
 * it runs in the editor's terminal when an ACP client delegates one, else a
 * local /bin/sh subprocess. This is a mutating tool by definition (commands can
 * change the system).
 */

#include "jc_toolcaps.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_bg.h"
#include "jc_toolout.h"
#include "jc_str.h"
#include "jc_snprintf.h"


static cJSON *run_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "command", "Shell command to execute", 1);
    tu_schema_bool(s, "run_in_background",
                   "Run detached and return immediately with a background id "
                   "(use read_background_output / kill_background to manage it)",
                   0);
    tu_schema_int(s, "timeout",
                  "Optional wall-clock limit in seconds for a command you "
                  "expect could hang or run long (a build, a test suite). If "
                  "it is exceeded the command is terminated and you get a "
                  "timeout result. Omit for quick commands.",
                  0);
    return s;
}

/* Capture combined stdout+stderr of `command`. */
static jc_status run_run(const cJSON *args, struct jc_tool_result *out,
                         struct jc_app *app)
{
    const char *command = tu_arg_str(args, "command");
    struct jc_sb sb;
    struct jc_sb full;
    int truncated = 0;
    jc_size show = 0;
    jc_size cap = 0;
    int status = -1;

    if (command == NULL) {
        tu_err(out, "error: 'command' argument is required");
        return JC_OK;
    }

    /* Background mode: start detached and return an id immediately. Only the
     * local fork path supports this; an editor terminal delegate has no
     * poll/kill surface, so fall through to a normal (blocking) run there. */
    if (tu_arg_bool(args, "run_in_background", 0)) {
        if (app->cmd != NULL) {
            tu_err(out, "error: run_in_background is not available through the "
                        "editor terminal; run it in the foreground instead");
            return JC_OK;
        }
        if (app->bg == NULL) {
            tu_err(out, "error: background processes are not available");
            return JC_OK;
        }
        {
            int id = jc_bg_start(app->bg, command);
            char msg[160];
            if (id < 0) {
                tu_err(out, "error: failed to start the background command");
                return JC_OK;
            }
            if (id == 0) {
                tu_err(out, "error: too many background processes already "
                            "running (kill one with kill_background)");
                return JC_OK;
            }
            jc_snprintf(msg, sizeof(msg),
                "Started background process %d. Use read_background_output with "
                "id %d to see its output, kill_background to stop it.", id, id);
            tu_ok_copy(out, msg);
            return JC_OK;
        }
    }

    jc_sb_init(&sb);
    {
        /* per-call `timeout` arg > --run-timeout > config runTimeout. */
        long timeout = jc_config_run_timeout(
            (long)tu_arg_int(args, "timeout", 0),
            app->config.run_timeout_cli, app->config.run_timeout);
        /* M339: capture up to the spill ceiling, not the display cap, so the
         * remainder EXISTS to be preserved -- the cap is enforced below, on what
         * the model is shown. Bounded and per-call: tool_scratch is reset right
         * after this tool returns. */
        show = jc_config_cap(app->config.run_max_bytes, JC_CAP_RUN_DEFAULT);
        cap = (jc_size)JC_TOOLOUT_SPILL_MAX;
        if (cap < show) {
            cap = show;
        }
        if (jc_app_run_command_ex(app, command, cap,
                                  timeout, &sb, &status, &truncated) != JC_OK) {
            jc_sb_free(&sb);
            tu_err(out, "error: failed to start command");
            return JC_OK;
        }
    }

    jc_sb_init(&full);
    if (sb.len > 0) {
        /* Under the cap this appends the text unchanged, so the common case is
         * byte-identical to before M339. */
        (void)jc_toolout_spill(app, "run", sb.data, sb.len, show, &full);
    }
    if (truncated) {
        /* The CAPTURE hit its ceiling, which is a different fact from the
         * display cap: even the spill file is incomplete. Say so. */
        jc_sb_append(&full, "\n... [output truncated at the capture limit]");
    }
    if (sb.len == 0 && !truncated) {
        jc_sb_append(&full, "(no output)");
    }
    jc_sb_append_fmt(&full, "\n[exit status: %d]", status);
    jc_sb_free(&sb);

    out->content = jc_sb_finish(&full);
    out->is_error = (status != 0);
    /* M168: keep the command's own status, so telemetry can tell a red gate
     * (the tool worked; the command said no) from a broken tool. */
    out->exit_status = status;
    jc_sb_free(&full);
    return JC_OK;
}

static const struct jc_tool RUN_TOOL = {
    "run_terminal_command",
    "Run a shell command and return its combined output and exit status.",
    run_schema,
    0, /* mutating */
    run_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_run(void)
{
    return &RUN_TOOL;
}
