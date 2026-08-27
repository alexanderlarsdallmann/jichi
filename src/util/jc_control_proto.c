/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_control_proto.c - the pure control-channel codec (M159).
 * See jc_control.h. Request/response lines are one JSON object each, newline
 * terminated, versioned with "v". No I/O here -- unit-tested offline. */

#include "jc_control.h"
#include "jc_perm.h"
#include "jc_json.h"
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
    case JC_CTL_MODE:   return "mode";
    default:            return "unknown";
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
    return JC_CTL_UNKNOWN;
}

jc_status jc_control_parse_request(const char *line,
                                   struct jc_control_cmd *out,
                                   struct jc_arena *a)
{
    cJSON *o;
    const char *t;

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
        if (txt == NULL || txt[0] == '\0') {
            cJSON_Delete(o);
            return JC_ERR_INVALID;
        }
        out->text = jc_arena_strdup(a, txt);
        if (out->text == NULL) {
            cJSON_Delete(o);
            return JC_ERR_OOM;
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
                               int extend)
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
    return finish_line(o);
}
