/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_edit.c - the edit_file tool (exact-string find/replace).
 *
 * Mirrors the original CLI's edit semantics: the file must have been read
 * earlier in the session (the read-before-edit guard), the search string must
 * be found, and -- unless replace_all is set -- it must be unique. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_str.h"
#include "jc_patch.h"
#include "jc_diff.h"
#include "jc_snprintf.h"
#include "jc_envelope.h"
#include "jc_eventlog.h"
#include "jc_log.h"

#include <stdlib.h>
#include <string.h>

static cJSON *edit_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path", "Path to the file to edit", 1);
    tu_schema_string(s, "old_string", "Exact text to find and replace", 1);
    tu_schema_string(s, "new_string", "Replacement text", 1);
    {
        cJSON *props = cJSON_GetObjectItem(s, "properties");
        cJSON *prop = cJSON_CreateObject();
        cJSON_AddStringToObject(prop, "type", "boolean");
        cJSON_AddStringToObject(prop, "description",
                                "Replace all occurrences (default false)");
        cJSON_AddItemToObject(props, "replace_all", prop);
    }
    return s;
}

static jc_status edit_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    char gpnote[600];   /* M435: the moved-goalpost note, empty when none */
    const char *path = tu_arg_str(args, "path");
    const char *old_s = tu_arg_str(args, "old_string");
    const char *new_s = tu_arg_str(args, "new_string");
    int replace_all = tu_arg_bool(args, "replace_all", 0);
    char *text;
    jc_size len;
    int count = 0;
    enum jc_patch_strategy strat;
    struct jc_sb sb;
    char msg[1100];

    if (path == NULL || old_s == NULL || new_s == NULL) {
        tu_err(out, "error: 'path', 'old_string', and 'new_string' are required");
        return JC_OK;
    }
    if (old_s[0] == '\0') {
        tu_err(out, "error: 'old_string' must not be empty");
        return JC_OK;
    }
    if (!jc_app_was_read(app, path)) {
        tu_err(out, "error: read the file before editing it");
        return JC_OK;
    }
    /* M197: scratch -- the pre-edit text is a per-call transient (used for the
     * match, the near-match hint and the diff, all inside this function). On
     * app->arena, read->edit->edit->edit retained one full copy per edit. */
    if (jc_app_read_file(app, path, &text, &len,
                         jc_app_tool_scratch(app)) != JC_OK) {
        jc_snprintf(msg, sizeof(msg), "error: could not read '%s'", path);
        tu_err(out, msg);
        return JC_OK;
    }

    /* Resolve + apply via the shared core: exact first, then (unless disabled)
     * the whitespace/anchor fuzzy fallback. The TUI preview uses the same call,
     * so the preview matches what is written. */
    jc_sb_init(&sb);
    strat = jc_patch_apply(text, old_s, new_s, replace_all,
                           app->config.fuzzy_edit, &sb, &count);
    if (strat == JC_PATCH_NONE) {
        struct jc_sb es;
        jc_sb_free(&sb);
        jc_sb_init(&es);
        jc_sb_append(&es, "error: old_string not found in file");
        jc_patch_nearmatch_hint(text, old_s, &es);
        tu_err(out, es.data != NULL ? es.data : "error: old_string not found in file");
        jc_sb_free(&es);
        return JC_OK;
    }
    if (strat == JC_PATCH_AMBIGUOUS) {
        struct jc_sb es;
        jc_sb_free(&sb);
        jc_sb_init(&es);
        jc_snprintf(msg, sizeof(msg),
                    "error: old_string is not unique (%d matches)", count);
        jc_sb_append(&es, msg);
        /* M208: name WHERE they are. "add more surrounding context" alone is
         * advice the model cannot act on without knowing which places collided,
         * so each retry was a fresh guess -- 11 of 14 measured edit failures. */
        jc_patch_matchlines_hint(text, old_s, &es);
        tu_err(out, es.data != NULL ? es.data : msg);
        jc_sb_free(&es);
        return JC_OK;
    }

    {
        jc_status wst = jc_app_write_file(app, path,
                                         sb.data != NULL ? sb.data : "",
                                         sb.len);
        if (wst != JC_OK) {
            jc_sb_free(&sb);
            /* M291: a fence denial is a POLICY refusal, not an I/O failure.
             * Collapsing both into "could not write" was misleading twice over:
             * the model reads it as a disk problem and retries, and routing
             * counted it as a malfunction and escalated -- which cannot help,
             * since the stronger model meets the same fence. */
            if (wst == JC_ERR_DENIED) {
                jc_snprintf(msg, sizeof(msg),
                    "error: refused by safety fence (path outside workspace): "
                    "'%s'", path);
                tu_err_policy(out, msg);
            } else {
                jc_snprintf(msg, sizeof(msg), "error: could not write '%s'",
                            path);
                tu_err(out, msg);
            }
            return JC_OK;
        }
    }

    gpnote[0] = '\0';
    /* M88 + M435: one reporter, six destinations (journal, telemetry, WARN,
     * on_status, the counter, and now the MODEL). See tu_report_test_edit. */
    if (app->env != NULL && jc_env_test_assertion_edit(path, old_s, new_s)) {
        tu_report_test_edit(app, "edit_file", path, gpnote, sizeof gpnote);
    }

    /* Result: the summary plus a unified diff of the change, so the model sees
     * exactly what changed. A non-exact match is flagged so the behaviour is
     * observable (and the model learns its old_string drifted). */
    {
        struct jc_sb res;
        if (strat == JC_PATCH_EXACT) {
            jc_snprintf(msg, sizeof(msg), "Edited %s (%d replacement%s)", path,
                        count, count == 1 ? "" : "s");
        } else {
            jc_snprintf(msg, sizeof(msg),
                        "Edited %s (1 replacement, matched %s)", path,
                        jc_patch_strategy_name(strat));
        }
        jc_sb_init(&res);
        jc_sb_append(&res, msg);
        jc_sb_append_char(&res, '\n');
        jc_diff_unified(text, sb.data != NULL ? sb.data : "", 3, 0, 200, &res);
        /* M435: after the diff, so the model sees WHAT it changed and then what
         * that change means. */
        if (gpnote[0] != '\0') {
            jc_sb_append(&res, gpnote);
        }
        tu_ok_owned(out, jc_sb_finish(&res));
        jc_sb_free(&res);
    }
    jc_sb_free(&sb);
    tu_append_diagnostics(out, app, path);
    return JC_OK;
}

static const struct jc_tool EDIT_TOOL = {
    "edit_file",
    "Replace an exact string in a file. The file must have been read first. "
    "Unless replace_all is true, the search string must be unique.",
    edit_schema,
    0, /* mutating */
    edit_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_edit(void)
{
    return &EDIT_TOOL;
}
