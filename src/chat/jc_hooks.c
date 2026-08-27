/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hooks.c - lifecycle hooks (see jc_hooks.h).
 *
 * The fork/exec + stdin/stdout capture is the shared jc_proc_capture
 * (src/util/jc_proc.c); the glob matcher is jc_glob_match (jc_envelope.h).
 * Hooks are opt-in and top-level only; the agent loop guards each fire site
 * with jc_hooks_active.
 */

#include "jc_hooks.h"
#include "jc_platform.h"
#include "jc_app.h"
#include "jc_envelope.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_proc.h"
#include "jc_eventlog.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

#define HOOK_MAX_OUTPUT (32 * 1024)

void jc_hook_result_init(struct jc_hook_result *r)
{
    r->block = 0;
    jc_sb_init(&r->reason);
    jc_sb_init(&r->context);
}

void jc_hook_result_free(struct jc_hook_result *r)
{
    jc_sb_free(&r->reason);
    jc_sb_free(&r->context);
}

int jc_hook_exit_blocks(int code)
{
    return code == 2;
}

int jc_hook_matches(const char *matcher, const char *tool)
{
    const char *p;
    const char *start;
    char alt[256];

    if (matcher == NULL || matcher[0] == '\0') {
        return 1; /* no matcher => fire for every tool */
    }
    if (tool == NULL) {
        tool = "";
    }
    /* Split on '|' and test each alternative as a glob. */
    start = matcher;
    for (p = matcher;; p++) {
        if (*p == '|' || *p == '\0') {
            jc_size n = (jc_size)(p - start);
            if (n > 0 && n < sizeof(alt)) {
                memcpy(alt, start, n);
                alt[n] = '\0';
                if (jc_glob_match(alt, tool)) {
                    return 1;
                }
            }
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    return 0;
}

int jc_hooks_active(const struct jc_app *app)
{
    int i;
    if (app == NULL || !app->config.hooks_enabled) {
        return 0;
    }
    for (i = 0; i < JC_HOOK_EVENT_COUNT; i++) {
        if (app->config.hooks.events[i].len > 0) {
            return 1;
        }
    }
    return 0;
}


/* Trim trailing whitespace/newlines from a jc_sb in place. */
static void rstrip(struct jc_sb *sb)
{
    while (sb->len > 0) {
        char c = sb->data[sb->len - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            sb->len--;
            sb->data[sb->len] = '\0';
        } else {
            break;
        }
    }
}

static int event_carries_context(enum jc_hook_event ev)
{
    return ev == JC_HOOK_USER_PROMPT || ev == JC_HOOK_SESSION_START ||
           ev == JC_HOOK_POST_TOOL;
}

/* The configured spelling of an event. Shared by the hook's stdin payload and
 * its telemetry so a log names the hook the way the config does (M326v). */
static const char *hook_event_name(enum jc_hook_event ev)
{
    return ev == JC_HOOK_PRE_TOOL ? "PreToolUse" :
           ev == JC_HOOK_POST_TOOL ? "PostToolUse" :
           ev == JC_HOOK_USER_PROMPT ? "UserPromptSubmit" :
           ev == JC_HOOK_STOP ? "Stop" : "SessionStart";
}

/* Build the event JSON fed to a hook on stdin. */
static char *build_stdin(struct jc_app *app, enum jc_hook_event ev,
                         const char *tool, const char *args_json,
                         const char *result, int is_error, const char *prompt)
{
    cJSON *o = cJSON_CreateObject();
    char *txt;
    const char *evname = hook_event_name(ev);

    if (o == NULL) return NULL;
    cJSON_AddStringToObject(o, "event", evname);
    cJSON_AddStringToObject(o, "cwd", app->cwd);
    if (app->session_id != NULL) {
        cJSON_AddStringToObject(o, "session_id", app->session_id);
    }
    if (tool != NULL) {
        cJSON *ti;
        cJSON_AddStringToObject(o, "tool_name", tool);
        ti = (args_json != NULL) ? jc_json_parse(args_json) : NULL;
        if (ti != NULL) {
            cJSON_AddItemToObject(o, "tool_input", ti);
        }
    }
    if (result != NULL) {
        cJSON_AddStringToObject(o, "tool_response", result);
        cJSON_AddBoolToObject(o, "tool_error", is_error ? 1 : 0);
    }
    if (prompt != NULL) {
        cJSON_AddStringToObject(o, "prompt", prompt);
    }
    txt = jc_json_print(o);
    cJSON_Delete(o);
    return txt; /* malloc'd; caller frees */
}

/* Interpret one hook's exit code + output, folding it into `out`. */
/* Returns 1 when the hook communicated DELIBERATELY -- the JSON contract, or the
 * plain exit-2 block -- and 0 otherwise. The caller needs that distinction to
 * decide whether a nonzero exit is a failure worth recording (M584): a hook that
 * exits 3 with a JSON verdict said what it meant, while a hook that exits 127
 * did not run at all. Judging that from the exit code alone is impossible,
 * because the JSON path is chosen by the OUTPUT. */
static int interpret(enum jc_hook_event ev, int code, struct jc_sb *output,
                     struct jc_hook_result *out)
{
    cJSON *j;
    rstrip(output);

    /* Advanced JSON contract first. */
    j = (output->data != NULL && output->data[0] == '{')
            ? jc_json_parse(output->data) : NULL;
    if (j != NULL) {
        const char *decision = jc_json_get_str(j, "decision", NULL);
        const char *reason = jc_json_get_str(j, "reason", NULL);
        const char *ctx = jc_json_get_str(j, "additionalContext", NULL);
        if (decision != NULL && strcmp(decision, "block") == 0) {
            out->block = 1;
            if (reason != NULL) {
                if (out->reason.len > 0) jc_sb_append_char(&out->reason, '\n');
                jc_sb_append(&out->reason, reason);
            }
        }
        if (ctx != NULL && ctx[0] != '\0') {
            if (out->context.len > 0) jc_sb_append_char(&out->context, '\n');
            jc_sb_append(&out->context, ctx);
        }
        cJSON_Delete(j);
        return 1;
    }

    /* Plain contract: exit code 2 blocks (reason = output). */
    if (jc_hook_exit_blocks(code)) {
        out->block = 1;
        if (output->len > 0) {
            if (out->reason.len > 0) jc_sb_append_char(&out->reason, '\n');
            jc_sb_append(&out->reason, output->data);
        }
        return 1;
    }
    if (code == 0) {
        if (event_carries_context(ev) && output->len > 0) {
            if (out->context.len > 0) jc_sb_append_char(&out->context, '\n');
            jc_sb_append(&out->context, output->data);
        }
        return 0;
    }
    /* Any other nonzero code: advisory only -- but SAY WHICH KIND. 126 and 127
     * are not advice: they mean the command was not executable or not found, so
     * the check DID NOT RUN. Measured at M584 on a real project whose config
     * named `.jichi/hooks/zig-fmt-check.sh` after the directory had been
     * removed: every write fired a hook that exited 127, the only trace was
     * this line, and `-q` (which every headless and autonomous run uses)
     * suppressed even that. The project believed it had a formatter gate for
     * its entire recorded history. */
    if (code == 126 || code == 127) {
        jc_logf(JC_LOG_WARN,
                "hook exited %d -- the command was not found or is not "
                "executable, so THIS CHECK DID NOT RUN (check the hooks config)",
                code);
    } else {
        jc_logf(JC_LOG_WARN, "hook exited %d (ignored)", code);
    }
    return 0;
}

static void run_cmd_cfg(struct jc_app *app, const struct jc_hook_cmd_cfg *c,
                        enum jc_hook_event ev, const char *tool,
                        const char *stdin_json, struct jc_hook_result *out)
{
    char *argv[72];
    int n = 0;
    struct jc_sb output;
    int code;
    int deliberate = 0;   /* M584: the hook used the JSON contract or exit 2 */

    if (c->shell != NULL) {
        argv[n++] = (char *)jc_shell_path();
        argv[n++] = (char *)"-c";
        argv[n++] = (char *)c->shell;
    } else {
        jc_size i;
        argv[n++] = (char *)c->command;
        for (i = 0; i < c->args.len && n < 70; i++) {
            argv[n++] = *(char **)jc_vec_at((struct jc_vec *)&c->args, i);
        }
    }
    argv[n] = NULL;

    jc_sb_init(&output);
    code = jc_proc_capture(argv, NULL, stdin_json, &output, HOOK_MAX_OUTPUT,
                           c->timeout, &app->abort_flag);
    if (code == -1) {
        jc_logf(JC_LOG_WARN, "hook failed to start");
    } else if (code == -2) {
        jc_logf(JC_LOG_WARN, "hook timed out and was killed");
    } else {
        deliberate = interpret(ev, code, &output, out);
    }
    /* M326v: a hook that fails or times out was visible ONLY as a stderr line,
     * so it left no trace in telemetry at all. That mattered concretely: asked
     * whether a SessionStart hook's 10s timeout explained a workload's model
     * failures, the honest answer from 36,925 events was "the log cannot say"
     * -- the hook is invisible to the instrument. A hook that quietly eats its
     * timeout every session is exactly the cost nobody measures. Failures only:
     * a healthy hook is not worth a line per tool call. */
    /* M584: the recorded set was start-failure and timeout only, so the case
     * that actually happens in the field -- a configured hook whose script is
     * missing (exit 127) -- reached NO sink at all. A block (exit 2) and a
     * clean exit are deliberate outcomes and stay unrecorded: this is a failure
     * log, not a trace. The outcome vocabulary stays a bounded classifier and
     * never carries the hook's output (proposals/2026-08-observability-seams.md
     * D7). */
    if (code == -1 || code == -2 || (code != 0 && !deliberate)) {
        cJSON *o = jc_app_telem_begin(app, "hook");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "hook", hook_event_name(ev));
            cJSON_AddStringToObject(o, "outcome",
                    code == -2 ? "timeout"
                  : code == -1 ? "start_failed"
                  : (code == 126 || code == 127) ? "not_runnable"
                  : "nonzero_exit");
            if (code > 0) {
                cJSON_AddNumberToObject(o, "code", (double)code);
            }
            cJSON_AddNumberToObject(o, "timeout_s", (double)c->timeout);
            if (tool != NULL) {
                cJSON_AddStringToObject(o, "tool", tool);
            }
        }
        jc_app_telem_end(app, o);
    }
    jc_sb_free(&output);
    (void)tool;
}

void jc_hooks_fire(struct jc_app *app, enum jc_hook_event event,
                   const char *tool, const char *args_json,
                   const char *result, int is_error, const char *prompt,
                   struct jc_hook_result *out)
{
    const struct jc_vec *ev;
    char *stdin_json;
    jc_size mi;

    if (!jc_hooks_active(app) || app->agent_depth != 0) {
        return;
    }
    ev = &app->config.hooks.events[event];
    if (ev->len == 0) {
        return;
    }
    stdin_json = build_stdin(app, event, tool, args_json, result, is_error,
                             prompt);
    for (mi = 0; mi < ev->len; mi++) {
        const struct jc_hook_matcher_cfg *mc =
            (const struct jc_hook_matcher_cfg *)
                jc_vec_at((struct jc_vec *)ev, mi);
        jc_size ci;
        if (!jc_hook_matches(mc->matcher, tool)) {
            continue;
        }
        for (ci = 0; ci < mc->commands.len; ci++) {
            const struct jc_hook_cmd_cfg *c =
                (const struct jc_hook_cmd_cfg *)
                    jc_vec_at((struct jc_vec *)&mc->commands, ci);
            run_cmd_cfg(app, c, event, tool,
                        stdin_json != NULL ? stdin_json : "{}", out);
        }
    }
    free(stdin_json);
}
