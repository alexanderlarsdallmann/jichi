/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_ask.c - the ask_user tool (M34d / F4).
 *
 * Lets the agent pause and put a focused clarifying question to the user instead
 * of guessing, with optional suggested answers. It routes through the optional
 * front-end delegate app->ask (the TUI installs a blocking prompt). When no
 * delegate is installed -- headless, ACP, --auto -- the tool returns a note that
 * no interactive user is available so the model proceeds on its own judgment;
 * an unattended run therefore never blocks waiting for input.
 *
 * Read-only (it changes nothing on disk), so it is not permission-gated.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_envelope.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

#define ASK_MAX_OPTIONS 16

/* M359: cap for the question text as journaled (bytes, UTF-8-safe cut). */
#define ASK_JOURNAL_QMAX 240

/* M359: an unanswered blocking question is the dual of an operator inject
 * (M161): the model reached for a human, and in a bounded run whether anyone
 * was there belongs in the run's record -- jc_runsview renders unanswered=N
 * beside steered=N, so a reviewer learns the task was under-specified without
 * replaying the transcript. Both outcomes are journaled (`answered` says
 * which); the ANSWER text never is -- it lands in the history anyway, and the
 * journal stays lean. No envelope, no event: the journal IS the surface. */
static void ask_journal(struct jc_app *app, const char *question,
                        int answered)
{
    cJSON *o;
    char q[ASK_JOURNAL_QMAX + 1];
    jc_size n;

    if (app == NULL || app->env == NULL || question == NULL) {
        return;
    }
    o = jc_env_journal_begin(app->env, "ask");
    if (o == NULL) {
        return;
    }
    n = jc_utf8_trunc_len(question, (jc_size)ASK_JOURNAL_QMAX);
    memcpy(q, question, (size_t)n);
    q[n] = '\0';
    cJSON_AddStringToObject(o, "question", q);
    cJSON_AddBoolToObject(o, "answered", answered);
    jc_env_journal_end(app->env, o);
}

static cJSON *ask_user_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props;
    cJSON *opt;
    cJSON *items;

    tu_schema_string(s, "question",
                     "The clarifying question to ask the user", 1);
    /* options: an optional array of suggested answers. */
    props = cJSON_GetObjectItem(s, "properties");
    opt = cJSON_CreateObject();
    cJSON_AddStringToObject(opt, "type", "array");
    cJSON_AddStringToObject(opt, "description",
                            "Optional list of suggested answers to offer");
    items = cJSON_CreateObject();
    cJSON_AddStringToObject(items, "type", "string");
    cJSON_AddItemToObject(opt, "items", items);
    if (props != NULL) {
        cJSON_AddItemToObject(props, "options", opt);
    } else {
        cJSON_Delete(opt);
    }
    return s;
}

static jc_status ask_user_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    const char *question = tu_arg_str(args, "question");
    const cJSON *opts;
    const char *optv[ASK_MAX_OPTIONS];
    int noptv = 0;
    struct jc_sb sb;
    jc_status st;

    if (question == NULL || question[0] == '\0') {
        tu_err(out, "error: 'question' argument is required");
        return JC_OK;
    }

    /* Only the top-level agent may prompt: a subagent (especially a forked
     * spawn_parallel child) shares app->ask pointing at the parent's terminal,
     * so reading it there would clash. Below top level, proceed instead.
     * No front-end able to prompt (headless / ACP / --auto) -> also proceed,
     * rather than hang an unattended run. */
    if (app->agent_depth != 0 || app->ask == NULL || app->ask->ask == NULL) {
        /* M443: count it for the terminal `degraded` object. The journal already
         * records answered:false, but only the offline `runs` reader counts those --
         * a supervisor reading the run's own result learned nothing. Counted at
         * every depth on purpose: a delegate's unanswered question is still a
         * question the run answered for itself. */
        app->deg_unanswered++;
        ask_journal(app, question, 0);
        tu_ok_owned(out, jc_strdup(
            "No interactive user is available to answer right now. Proceed "
            "with your best judgment and state any assumptions you make."));
        return JC_OK;
    }

    opts = cJSON_GetObjectItem((cJSON *)args, "options");
    if (cJSON_IsArray(opts)) {
        cJSON *it;
        for (it = opts->child; it != NULL && noptv < ASK_MAX_OPTIONS;
             it = it->next) {
            if (cJSON_IsString(it) && it->valuestring != NULL &&
                it->valuestring[0] != '\0') {
                optv[noptv++] = it->valuestring;
            }
        }
    }

    jc_sb_init(&sb);
    st = app->ask->ask(app->ask->ctx, question, optv, noptv, &sb);
    if (st != JC_OK) {
        jc_sb_free(&sb);
        ask_journal(app, question, 0);
        tu_ok_owned(out, jc_strdup(
            "The user did not provide an answer. Proceed with your best "
            "judgment and state any assumptions you make."));
        return JC_OK;
    }
    ask_journal(app, question, 1);
    tu_ok_owned(out, jc_sb_finish(&sb));
    jc_sb_free(&sb);
    return JC_OK;
}

static const struct jc_tool ASK_USER_TOOL = {
    "ask_user",
    "Pause and ask the user a focused clarifying question (with optional "
    "suggested answers) instead of guessing, when a choice materially changes "
    "what you should do. Returns the user's answer. In a non-interactive run "
    "it returns a note to proceed, so use it only for genuinely blocking "
    "decisions.",
    ask_user_schema,
    1, /* read-only: no disk changes, so not permission-gated */
    ask_user_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_ask_user(void)
{
    return &ASK_USER_TOOL;
}
