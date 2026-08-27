/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agentjson.c - structured headless output builders (see jc_agentjson.h). */

#include "jc_utf8.h"
#include "jc_agentjson.h"
#include "cJSON.h"
#include <string.h>

enum jc_tool_cat jc_agent_tool_category(const char *name)
{
    if (name == NULL) {
        return JC_TOOLCAT_OTHER;
    }
    if (strcmp(name, "write_file") == 0 || strcmp(name, "edit_file") == 0 ||
        strcmp(name, "apply_patch") == 0 || strcmp(name, "rename_symbol") == 0 ||
        strcmp(name, "format_file") == 0 || strcmp(name, "apply_code_action") == 0) {
        return JC_TOOLCAT_WRITE;
    }
    if (strcmp(name, "run_terminal_command") == 0 || strcmp(name, "run_tests") == 0) {
        return JC_TOOLCAT_SHELL;
    }
    if (strcmp(name, "read_file") == 0 || strcmp(name, "list_files") == 0 ||
        strcmp(name, "search_code") == 0 || strcmp(name, "codebase_search") == 0 ||
        strcmp(name, "fetch_url") == 0 || strcmp(name, "search_docs") == 0 ||
        strncmp(name, "find_", 5) == 0 || strncmp(name, "read_", 5) == 0 ||
        strncmp(name, "list_", 5) == 0 || strncmp(name, "git_", 4) == 0) {
        return JC_TOOLCAT_READ;
    }
    return JC_TOOLCAT_OTHER;
}

cJSON *jc_agentjson_event(const char *type)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", (double)JC_AGENTJSON_VERSION);
    cJSON_AddStringToObject(o, "type", type != NULL ? type : "");
    return o;
}

cJSON *jc_agentjson_result(const char *text, const char *model,
                           const char *session_id, const char *run_id,
                           double in_tok,
                           double out_tok, double cost, int tool_calls,
                           int aborted, const char *stop_reason, int work_kept,
                           int err_code, const char *err_type,
                           const char *err_msg,
                           const struct jc_agent_econ *econ)
{
    cJSON *o = jc_agentjson_event("done");
    cJSON *tok;
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "text", text != NULL ? text : "");
    cJSON_AddStringToObject(o, "model", model != NULL ? model : "");
    if (session_id != NULL && session_id[0] != '\0') {
        cJSON_AddStringToObject(o, "session_id", session_id);
    }
    /* M431c: the envelope's run id -- the key the journal stamps on every line
     * and telemetry stamps on every event, so it is what JOINS a worker's stdout
     * to its own audit trail. It appeared in neither machine surface, so a
     * supervisor could not correlate the two unless it had passed --journal
     * itself and remembered the path. Conditional like session_id: no envelope,
     * no run id, and nothing to correlate with either. */
    if (run_id != NULL && run_id[0] != '\0') {
        cJSON_AddStringToObject(o, "run", run_id);
    }
    tok = cJSON_CreateObject();
    cJSON_AddNumberToObject(tok, "input", in_tok);
    cJSON_AddNumberToObject(tok, "output", out_tok);
    cJSON_AddItemToObject(o, "tokens", tok);
    cJSON_AddNumberToObject(o, "cost", cost);
    cJSON_AddNumberToObject(o, "tool_calls", (double)tool_calls);
    cJSON_AddBoolToObject(o, "aborted", aborted ? 1 : 0);
    cJSON_AddStringToObject(o, "stop_reason",
                            stop_reason != NULL ? stop_reason : "done");
    cJSON_AddBoolToObject(o, "work_kept", work_kept ? 1 : 0);
    if (err_msg != NULL) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code", (double)err_code);
        cJSON_AddStringToObject(err, "type", err_type != NULL ? err_type : "");
        cJSON_AddStringToObject(err, "message", err_msg);
        cJSON_AddItemToObject(o, "error", err);
    }
    if (econ != NULL) {
        cJSON *cache;
        cJSON *tools;
        cJSON_AddBoolToObject(o, "starved", econ->starved ? 1 : 0);
        if (econ->budget_kind != NULL && econ->budget_kind[0] != '\0') {
            cJSON_AddStringToObject(o, "budget_kind", econ->budget_kind);
        }
        if (econ->budget_limit > 0.0 || econ->budget_used > 0.0) {
            cJSON *b = cJSON_CreateObject();
            cJSON_AddNumberToObject(b, "used", econ->budget_used);
            cJSON_AddNumberToObject(b, "limit", econ->budget_limit);
            cJSON_AddItemToObject(o, "budget", b);
        }
        cJSON_AddNumberToObject(o, "peak_input", econ->peak_input);
        cache = cJSON_CreateObject();
        cJSON_AddNumberToObject(cache, "read", econ->cache_read);
        cJSON_AddNumberToObject(cache, "write", econ->cache_write);
        cJSON_AddItemToObject(o, "cache", cache);
        tools = cJSON_CreateObject();
        cJSON_AddNumberToObject(tools, "read", (double)econ->reads);
        cJSON_AddNumberToObject(tools, "write", (double)econ->writes);
        cJSON_AddNumberToObject(tools, "shell", (double)econ->shells);
        cJSON_AddNumberToObject(tools, "other", (double)econ->other_tools);
        cJSON_AddItemToObject(o, "tools", tools);
        /* M443: the decisions this run made on the operator's behalf because no
         * operator was there. Emitted ONLY when at least one occurred: an
         * always-present `degraded` object teaches a reader to skip the field on
         * the one run where it matters, which is the M420 argument that gave
         * `goalposts=N` its non-zero gate.
         *
         * A supervisor's test is `if ("degraded" in done)`, not a boolean compare --
         * so the presence of the key IS the flag, and the counts say how bad. */
        if (econ->deg_unanswered > 0 || econ->deg_approval > 0 ||
            econ->deg_privilege > 0) {
            cJSON *d = cJSON_CreateObject();
            if (econ->deg_unanswered > 0) {
                cJSON_AddNumberToObject(d, "unanswered",
                                        (double)econ->deg_unanswered);
            }
            if (econ->deg_approval > 0) {
                cJSON_AddNumberToObject(d, "approval_unavailable",
                                        (double)econ->deg_approval);
            }
            if (econ->deg_privilege > 0) {
                cJSON_AddNumberToObject(d, "privilege_refused",
                                        (double)econ->deg_privilege);
            }
            cJSON_AddItemToObject(o, "degraded", d);
        }
    }
    return o;
}

void jc_agentjson_preview(const char *s, char *out, jc_size cap)
{
    jc_size n;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (s == NULL || s[0] == '\0') {
        return;
    }
    n = (jc_size)strlen(s);
    if (n > cap - 1) {
        /* Only truncation needs the boundary walk. jc_utf8_trunc_len steps back
         * from the first dropped byte to the start of the sequence it would have
         * split -- the same helper M191 used for the compaction markers and
         * jc_tool_ask.c uses for its journal, so there is one answer in the tree
         * to "where may a byte cut land". */
        n = jc_utf8_trunc_len(s, cap - 1);
    }
    if (n > 0) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
}
