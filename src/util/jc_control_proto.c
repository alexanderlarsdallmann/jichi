/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_control_proto.c - the pure control-channel codec (M159).
 * See jc_control.h. Request/response lines are one JSON object each, newline
 * terminated, versioned with "v". No I/O here -- unit-tested offline. */

#include "jc_control.h"
#include "jc_perm.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

const char *jc_control_type_name(enum jc_control_cmd_type t)
{
    switch (t) {
    case JC_CTL_STATUS: return "status";
    case JC_CTL_INJECT: return "inject";
    case JC_CTL_PAUSE:  return "pause";
    case JC_CTL_RESUME: return "resume";
    case JC_CTL_ABORT:  return "abort";
    case JC_CTL_MODE:        return "mode";
    case JC_CTL_STOP_TASK:   return "stop-task";    /* M1149 */
    case JC_CTL_INJECT_TASK: return "inject-task";  /* M1149 */
    case JC_CTL_CANCEL_CMD:  return "cancel-cmd";   /* M1192 B3 */
    default:                 return "unknown";
    }
}

enum jc_control_cmd_type jc_control_type_parse(const char *s)
{
    if (s == NULL) return JC_CTL_UNKNOWN;
    if (strcmp(s, "status") == 0) return JC_CTL_STATUS;
    if (strcmp(s, "inject") == 0) return JC_CTL_INJECT;
    if (strcmp(s, "pause") == 0)  return JC_CTL_PAUSE;
    if (strcmp(s, "resume") == 0) return JC_CTL_RESUME;
    if (strcmp(s, "abort") == 0)  return JC_CTL_ABORT;
    if (strcmp(s, "mode") == 0)   return JC_CTL_MODE;   /* M304 */
    if (strcmp(s, "stop-task") == 0)   return JC_CTL_STOP_TASK;   /* M1149 */
    if (strcmp(s, "inject-task") == 0) return JC_CTL_INJECT_TASK; /* M1149 */
    if (strcmp(s, "cancel-cmd") == 0)  return JC_CTL_CANCEL_CMD;  /* M1192 B3 */
    return JC_CTL_UNKNOWN;
}

void jc_control_verb_list(char *buf, jc_size cap, const char *sep)
{
    int t;
    jc_size len = 0;

    if (buf == NULL || cap == 0) {
        return;
    }
    buf[0] = '\0';
    for (t = (int)JC_CTL_STATUS; t < (int)JC_CTL_UNKNOWN; t++) {
        jc_snprintf(buf + len, cap - len, "%s%s",
                    len > 0 ? (sep != NULL ? sep : "|") : "",
                    jc_control_type_name((enum jc_control_cmd_type)t));
        len = (jc_size)strlen(buf);
        if (len + 1 >= cap) {
            break;
        }
    }
}

jc_status jc_control_parse_request(const char *line,
                                   struct jc_control_cmd *out,
                                   struct jc_arena *a)
{
    cJSON *o;
    const char *t;
    cJSON *tk;   /* M1149: the `task` field (stop-task / inject-task) */

    if (out == NULL) {
        return JC_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->type = JC_CTL_UNKNOWN;
    if (line == NULL) {
        return JC_ERR_PARSE;
    }
    o = cJSON_Parse(line);
    if (o == NULL || !cJSON_IsObject(o)) {
        cJSON_Delete(o);
        return JC_ERR_PARSE;
    }
    t = jc_json_get_str(o, "type", NULL);
    out->type = jc_control_type_parse(t);
    if (out->type == JC_CTL_PAUSE) {
        /* M530: lenient -- the control socket's caller is a SUPERVISOR SCRIPT
         * (docs/CONTROL.md), not jichi, so `"extend": 1` from a shell one-liner
         * must mean what it says rather than silently falling back. */
        out->extend = jc_json_get_bool_lenient(o, "extend", 0); /* M162 */
    }
    if (out->type == JC_CTL_MODE) {
        /* M304: the requested posture travels as a NAME, parsed here so an
         * unknown one is rejected at the protocol edge rather than becoming a
         * silent no-op inside the loop. */
        const char *mn = jc_json_get_str(o, "mode", NULL);
        enum jc_agent_mode m;
        if (mn == NULL || !jc_agent_mode_parse(mn, &m)) {
            cJSON_Delete(o);
            return JC_ERR_INVALID;
        }
        out->mode = (int)m;
    }
    if (out->type == JC_CTL_INJECT) {
        const char *txt = jc_json_get_str(o, "text", NULL);
        const char *from = jc_json_get_str(o, "from", NULL);
        if (txt == NULL || txt[0] == '\0') {
            cJSON_Delete(o);
            return JC_ERR_INVALID;
        }
        out->text = jc_arena_strdup(a, txt);
        if (out->text == NULL) {
            cJSON_Delete(o);
            return JC_ERR_OOM;
        }
        if (from != NULL) {
            out->from = jc_arena_strdup(a, from);
            if (out->from == NULL) {
                cJSON_Delete(o);
                return JC_ERR_OOM;
            }
        } else {
            out->from = NULL;
        }
    }
    if (out->type == JC_CTL_STOP_TASK || out->type == JC_CTL_INJECT_TASK) {
        /* M1149: the per-worker verbs. `task` is the 0-based pool index: a
         * whole number >= 0. A missing, non-numeric or negative value is an
         * INVALID request at the protocol edge (the "not live" refusal is for
         * a VALID index past the batch's total -- the pool checks that). */
        tk = cJSON_GetObjectItem(o, "task");
        if (tk == NULL || !cJSON_IsNumber(tk) ||
            tk->valuedouble < 0.0 ||
            (double)jc_json_clamp_long(tk->valuedouble) != tk->valuedouble) {
            cJSON_Delete(o);
            return JC_ERR_INVALID;
        }
        out->task = jc_json_clamp_int(tk->valuedouble);
    }
    if (out->type == JC_CTL_STOP_TASK) {
        /* M1149: the reason is optional; a present-but-empty one is omitted
         * (the report line then carries no ": <reason>" tail). */
        const char *r = jc_json_get_str(o, "reason", NULL);
        if (r != NULL && r[0] != '\0') {
            out->reason = jc_arena_strdup(a, r);
            if (out->reason == NULL) {
                cJSON_Delete(o);
                return JC_ERR_OOM;
            }
        } else {
            out->reason = NULL;
        }
    }
    if (out->type == JC_CTL_INJECT_TASK) {
        /* M1149: the text is required non-empty (as INJECT's), and must not
         * ALONE exceed the per-task queue cap -- the handler maps that
         * JC_ERR_TOOBIG to the "queue is full" refusal rather than the pool
         * ever storing an over-cap line. (The pool's own join-and-check adds
         * the already-queued text on top; this is the request-shape half.) */
        const char *txt = jc_json_get_str(o, "text", NULL);
        if (txt == NULL || txt[0] == '\0') {
            cJSON_Delete(o);
            return JC_ERR_INVALID;
        }
        if ((jc_size)strlen(txt) > JC_CONTROL_QUEUE_MAX) {
            cJSON_Delete(o);
            return JC_ERR_TOOBIG;
        }
        out->text = jc_arena_strdup(a, txt);
        if (out->text == NULL) {
            cJSON_Delete(o);
            return JC_ERR_OOM;
        }
    }
    if (out->type == JC_CTL_CANCEL_CMD) {
        /* M1192 B3: cancel the current command. `task` is OPTIONAL (the LEAD
         * form omits it => task -1, "lead"); when present it must be a whole
         * number >= 0 exactly as stop-task/inject-task validate it. `reason`
         * is optional, like stop-task's (present-but-empty is omitted). */
        out->task = -1;
        tk = cJSON_GetObjectItem(o, "task");
        if (tk != NULL) {
            if (!cJSON_IsNumber(tk) ||
                tk->valuedouble < 0.0 ||
                (double)jc_json_clamp_long(tk->valuedouble) != tk->valuedouble) {
                cJSON_Delete(o);
                return JC_ERR_INVALID;
            }
            out->task = jc_json_clamp_int(tk->valuedouble);
        }
        {
            const char *r = jc_json_get_str(o, "reason", NULL);
            if (r != NULL && r[0] != '\0') {
                out->reason = jc_arena_strdup(a, r);
                if (out->reason == NULL) {
                    cJSON_Delete(o);
                    return JC_ERR_OOM;
                }
            } else {
                out->reason = NULL;
            }
        }
    }
    cJSON_Delete(o);
    return JC_OK;
}

/* Print `o`, append '\n', delete `o`. Malloc'd; NULL on OOM. */
static char *finish_line(cJSON *o)
{
    char *s;
    char *line;
    jc_size n;

    if (o == NULL) {
        return NULL;
    }
    s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    line = (char *)malloc(n + 2);
    if (line == NULL) {
        free(s);
        return NULL;
    }
    memcpy(line, s, n);
    line[n] = '\n';
    line[n + 1] = '\0';
    free(s);
    return line;
}

char *jc_control_build_request(const char *type, const char *text,
                               int extend, const char *from)
{
    cJSON *o = cJSON_CreateObject();

    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddStringToObject(o, "type", type != NULL ? type : "unknown");
    if (text != NULL && text[0] != '\0') {
        /* M304: `mode` carries its posture NAME in a "mode" field, not "text", so
         * the wire says what it means (and a reader cannot mistake a posture for
         * steering prose). The caller passes it through `text` rather than the
         * signature growing a parameter every verb would then ignore. */
        if (type != NULL && strcmp(type, "mode") == 0) {
            cJSON_AddStringToObject(o, "mode", text);
        } else {
            cJSON_AddStringToObject(o, "text", text);
        }
    }
    if (extend) {
        cJSON_AddBoolToObject(o, "extend", 1); /* pause --extend (M162) */
    }
    if (from != NULL && from[0] != '\0') {
        cJSON_AddStringToObject(o, "from", from);
    }
    return finish_line(o);
}

char *jc_control_build_task_request(const char *type, int task,
                                    const char *reason_or_text)
{
    cJSON *o = cJSON_CreateObject();

    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddStringToObject(o, "type", type != NULL ? type : "unknown");
    cJSON_AddNumberToObject(o, "task", (double)task);
    if (reason_or_text != NULL && reason_or_text[0] != '\0') {
        /* M1149: the stop's tail is "reason", the inject's body is "text" --
         * the wire says what it means (the parse reads each by name). */
        if (type != NULL && strcmp(type, "stop-task") == 0) {
            cJSON_AddStringToObject(o, "reason", reason_or_text);
        } else {
            cJSON_AddStringToObject(o, "text", reason_or_text);
        }
    }
    return finish_line(o);
}

/* M1192 B3: build a cancel-cmd request (client side; malloc'd, trailing
 * newline included). `task` is -1 for the LEAD form (the "task" field is
 * OMITTED) or a >= 0 pool index for the worker form. `reason` is optional
 * (NULL/"" omitted). */
char *jc_control_build_cancel_request(int task, const char *reason)
{
    cJSON *o = cJSON_CreateObject();

    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddStringToObject(o, "type", "cancel-cmd");
    if (task >= 0) {
        cJSON_AddNumberToObject(o, "task", (double)task);
    }
    if (reason != NULL && reason[0] != '\0') {
        cJSON_AddStringToObject(o, "reason", reason);
    }
    return finish_line(o);
}

char *jc_control_build_ok(const char *note)
{
    cJSON *o = cJSON_CreateObject();

    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddBoolToObject(o, "ok", 1);
    if (note != NULL && note[0] != '\0') {
        cJSON_AddStringToObject(o, "note", note);
    }
    return finish_line(o);
}

char *jc_control_build_err(const char *msg)
{
    cJSON *o = cJSON_CreateObject();

    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "error", msg != NULL ? msg : "error");
    return finish_line(o);
}

char *jc_control_build_status(const struct jc_control_status *s)
{
    cJSON *o;

    if (s == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", JC_CONTROL_PROTO_V);
    cJSON_AddBoolToObject(o, "ok", 1);
    if (s->mode != NULL) {
        cJSON_AddStringToObject(o, "mode", s->mode);   /* M304 */
    }
    if (s->run_id != NULL) {
        cJSON_AddStringToObject(o, "run", s->run_id);
    }
    cJSON_AddNumberToObject(o, "elapsed", (double)s->elapsed);
    cJSON_AddNumberToObject(o, "tokens_used", s->tokens_used);
    if (s->budget_tokens > 0.0) {
        cJSON_AddNumberToObject(o, "budget_tokens", s->budget_tokens);
    }
    cJSON_AddNumberToObject(o, "tool_calls", (double)s->tool_calls);
    if (s->max_tool_calls > 0) {
        cJSON_AddNumberToObject(o, "max_tool_calls",
                                (double)s->max_tool_calls);
    }
    if (s->deadline_secs > 0) {
        cJSON_AddNumberToObject(o, "deadline_secs", (double)s->deadline_secs);
    }
    if (s->deadline_credit > 0) {
        cJSON_AddNumberToObject(o, "deadline_credit",
                                (double)s->deadline_credit);
    }
    if (s->outcome != NULL) {
        cJSON_AddStringToObject(o, "outcome", s->outcome);
    }
    if (s->last_tool != NULL && s->last_tool[0] != '\0') {
        cJSON_AddStringToObject(o, "last_tool", s->last_tool);
    }
    cJSON_AddBoolToObject(o, "paused", s->paused);
    /* M1045: the inject delivery state, ALWAYS emitted, zero included -- the
     * rest of the numeric fields omit when zero, but a supervisor polling
     * from cold must observe the 0/0 baseline. */
    cJSON_AddNumberToObject(o, "injected", (double)s->injected);
    cJSON_AddNumberToObject(o, "queued_injects", (double)s->queued_injects);
    /* M1149: the live pool's snapshot. ONLY when a batch is live (the pool
     * registered it); with none the reply is byte-identical to today -- no
     * "batch" key at all, not an empty object (driver cell 12's idle half). */
    if (s->batch != NULL) {
        cJSON *b = cJSON_CreateObject();
        int i;
        if (b != NULL) {
            cJSON_AddNumberToObject(b, "total", (double)s->batch->total);
            cJSON_AddNumberToObject(b, "done", (double)s->batch->done);
            {
                cJSON *ts = cJSON_CreateArray();
                for (i = 0; i < s->batch->total; i++) {
                    const struct jc_pool_task_snap *k = &s->batch->tasks[i];
                    cJSON *tk = cJSON_CreateObject();
                    if (tk == NULL) {
                        break;
                    }
                    cJSON_AddNumberToObject(tk, "i", (double)k->i);
                    cJSON_AddNumberToObject(tk, "live", (double)k->live);
                    if (k->model != NULL) {
                        cJSON_AddStringToObject(tk, "model", k->model);
                    }
                    cJSON_AddNumberToObject(tk, "pid", (double)k->pid);
                    if (k->tool != NULL) {
                        cJSON_AddStringToObject(tk, "tool", k->tool);
                    }
                    cJSON_AddNumberToObject(tk, "tools", (double)k->tools);
                    cJSON_AddNumberToObject(tk, "tokens", k->tokens);
                    cJSON_AddNumberToObject(tk, "idle_s", k->idle_s);
                    /* def-377 W2.2 (M1194): the child's progress signals,
                     * beside idle_s. Always emitted (numeric, 0 for a task with
                     * no sig yet). since_edit_s is the PARENT's computation from
                     * the child's last edits change: it reads 0 until the task
                     * has made >= 1 edit (the 0/absent branch), then grows in
                     * seconds -- so a task that never edited never shows a
                     * nonzero since_edit_s. flag is 0/1 (W2.3). A no-live-batch
                     * status still has NO batch key at all (unchanged). */
                    cJSON_AddNumberToObject(tk, "edits", (double)k->edits);
                    cJSON_AddNumberToObject(tk, "since_edit_s", k->since_edit_s);
                    cJSON_AddNumberToObject(tk, "placeholders",
                                            (double)k->placeholders);
                    cJSON_AddNumberToObject(tk, "compactions",
                                            (double)k->compactions);
                    cJSON_AddNumberToObject(tk, "dup24", k->dup24);
                    cJSON_AddNumberToObject(tk, "flag", (double)k->flag);
                    /* M1195 (b): in_cmd beside the W2.2 progress group. ALWAYS
                     * emitted, 0 included (the M1045 zero-emitted convention of
                     * injected/queued_injects, not the omit-when-zero of the
                     * optional fields): a supervisor polling between the child's
                     * commands reads 0, never null -- only then is the
                     * cancel-cmd worker form's fourth refusal, 'task N is not in
                     * a command (tool: X)', observable and testable (B-M1195.2;
                     * the field itself is M1192 B3, filled by the pool at
                     * pool_refresh_snap). */
                    cJSON_AddNumberToObject(tk, "in_cmd", (double)k->in_cmd);
                    if (k->wt != NULL) {
                        cJSON_AddStringToObject(tk, "wt", k->wt);
                    }
                    if (k->ending != NULL) {
                        cJSON_AddStringToObject(tk, "ending", k->ending);
                    }
                    cJSON_AddItemToArray(ts, tk);
                }
                cJSON_AddItemToObject(b, "tasks", ts);
                cJSON_AddItemToObject(o, "batch", b);
            }
        }
    }
    return finish_line(o);
}
