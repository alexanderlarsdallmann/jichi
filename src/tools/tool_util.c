/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* tool_util.c - helpers shared by the built-in tools (see tool_util.h). */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_envelope.h"
#include "jc_eventlog.h"
#include "jc_log.h"
#include "jc_lsp.h"
#include "jc_str.h"
#include "jc_json.h"
#include <stdlib.h>
#include <string.h>

void tu_ok_owned(struct jc_tool_result *out, char *owned)
{
    out->content = owned;
    out->is_error = 0;
}

void tu_ok_copy(struct jc_tool_result *out, const char *s)
{
    out->content = jc_strdup(s != NULL ? s : "");
    out->is_error = 0;
}

void tu_err(struct jc_tool_result *out, const char *msg)
{
    out->content = jc_strdup(msg);
    out->is_error = 1;
}

void tu_err_policy(struct jc_tool_result *out, const char *msg)
{
    tu_err(out, msg);
    out->policy_refusal = 1;
}

void tu_append_diagnostics(struct jc_tool_result *out, struct jc_app *app,
                           const char *path)
{
    char *rep;
    int cnt = 0;

    if (out->is_error || path == NULL || app->lsp == NULL) {
        return;
    }
    if (!jc_lsp_handles(app->lsp, path)) {
        return;
    }
    rep = jc_lsp_diagnostics(app->lsp, path, &cnt);
    if (rep == NULL) {
        return;
    }
    if (cnt > 0) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        if (out->content != NULL) {
            jc_sb_append(&sb, out->content);
        }
        jc_sb_append(&sb, "\n\n");
        jc_sb_append(&sb, rep);
        free(out->content);
        out->content = jc_sb_finish(&sb);
        jc_sb_free(&sb);
    }
    free(rep);
}

const char *tu_arg_str(const cJSON *args, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(args, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

int tu_arg_bool(const cJSON *args, const char *key, int dflt)
{
    /* M530: one reader for a boolean a MODEL wrote, shared with every other
     * caller that has to interpret one (jc_json_get_bool_lenient, M519).
     *
     * This function accepted a real bool and the stringified spellings a model
     * sometimes sends ("true", "1") -- M168's fix -- but NOT a JSON number, so
     * `{"replace_all": 1}` silently became the caller's default. For
     * `spawn_subagent`'s `readonly` that was a fence: the call site is
     *
     *     has_arg_ro = (cJSON_GetObjectItem(args, "readonly") != NULL);
     *     arg_ro     = tu_arg_bool(args, "readonly", 0);
     *
     * so a model asking for a read-only child with `{"readonly": 1}` produced
     * present=1, value=0 -- read as an EXPLICIT "not read-only", and the child
     * ran WRITABLE. That is M519's `"pathFence": 1` defect verbatim: the
     * presence check fires and the fallback becomes a denial of what was asked.
     *
     * The lenient reader also accepts "yes"/"no", which is a widening of what
     * this function took; the boundary it does NOT cross is the one that
     * matters -- prose still falls through to the caller's default, in both
     * directions, so a typo can never flip a fence either way. */
    return jc_json_get_bool_lenient(args, key, dflt);
}

int tu_arg_int(const cJSON *args, const char *key, int dflt)
{
    /* M168: accept a numeric STRING. A model that sends {"limit": "200.0"}
     * instead of {"limit": 200} used to get the default silently -- and for
     * `read_file`'s limit the default is 0, meaning "no limit", so asking for 200
     * lines returned the WHOLE FILE. Observed live on the zigodot program: five
     * such reads returned 172 KB each, every one adding 40-57k input tokens that
     * were then re-billed on every subsequent call of a cacheless backend, and
     * driving six history compactions in a 29-call run. A type mismatch must
     * never silently select the most expensive behaviour available.
     * See docs/TELEMETRY.md and docs/ANECDOTES.md #21.
     *
     * M287: the lenient parse lives in jc_json now, because the compaction
     * layer's read-identity keying must agree with this about what
     * {"limit": "100.0"} means -- two parsers for one quantity is the drift
     * that cost M286. */
    return (int)jc_json_get_num_lenient(args, key, (double)dflt);
}

cJSON *tu_schema_begin(void)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddItemToObject(schema, "properties", cJSON_CreateObject());
    cJSON_AddItemToObject(schema, "required", cJSON_CreateArray());
    return schema;
}

void tu_schema_string(cJSON *schema, const char *name, const char *desc,
                      int required)
{
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *prop = cJSON_CreateObject();
    cJSON_AddStringToObject(prop, "type", "string");
    cJSON_AddStringToObject(prop, "description", desc);
    cJSON_AddItemToObject(props, name, prop);
    if (required) {
        cJSON *req = cJSON_GetObjectItem(schema, "required");
        cJSON_AddItemToArray(req, cJSON_CreateString(name));
    }
}

void tu_schema_bool(cJSON *schema, const char *name, const char *desc,
                    int required)
{
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *prop = cJSON_CreateObject();
    cJSON_AddStringToObject(prop, "type", "boolean");
    cJSON_AddStringToObject(prop, "description", desc);
    cJSON_AddItemToObject(props, name, prop);
    if (required) {
        cJSON *req = cJSON_GetObjectItem(schema, "required");
        cJSON_AddItemToArray(req, cJSON_CreateString(name));
    }
}

void tu_schema_int(cJSON *schema, const char *name, const char *desc,
                   int required)
{
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *prop = cJSON_CreateObject();
    cJSON_AddStringToObject(prop, "type", "integer");
    cJSON_AddStringToObject(prop, "description", desc);
    cJSON_AddItemToObject(props, name, prop);
    if (required) {
        cJSON *req = cJSON_GetObjectItem(schema, "required");
        cJSON_AddItemToArray(req, cJSON_CreateString(name));
    }
}

void tu_report_test_edit(struct jc_app *app, const char *tool, const char *path,
                         char *note, jc_size note_cap)
{
    cJSON *j;
    cJSON *te;

    if (note != NULL && note_cap > 0) {
        note[0] = '\0';
    }
    if (app == NULL || app->env == NULL) {
        return;
    }
    app->env->test_edits++;   /* M410: counted, so attempt's verdict can say it */

    j = jc_env_journal_begin(app->env, "test_assertion_edit");
    if (j != NULL) {
        cJSON_AddStringToObject(j, "path", (path != NULL) ? path : "");
        cJSON_AddStringToObject(j, "tool", (tool != NULL) ? tool : "");
    }
    jc_env_journal_end(app->env, j);

    /* M417: also telemetry, so the OFFLINE readers see it -- the learn loop mines
     * telemetry, and the measured case (ten warnings, verdict PASS) never reached a
     * single drafted lesson. */
    te = jc_app_telem_begin(app, "test_edit");
    if (te != NULL) {
        cJSON_AddStringToObject(te, "tool", (tool != NULL) ? tool : "");
        cJSON_AddStringToObject(te, "path", (path != NULL) ? path : "");
    }
    jc_app_telem_end(app, te);

    jc_logf(JC_LOG_WARN, "envelope: edited a test assertion in %s during an "
            "autonomous run -- verify this fixes the code, not the goalpost",
            (path != NULL) ? path : "?");
    if (app->cb != NULL && app->cb->on_status != NULL) {
        app->cb->on_status(app->cb->user,
                           "warning: a test assertion was edited");
    }
    /* M435: and the model, at the moment of the act rather than after the verdict. */
    jc_env_test_edit_note(app->env->test_edits, path, note, note_cap);
}
