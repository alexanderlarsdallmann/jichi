/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_daemon_proto.c - pure request codec for the warm-process daemon
 * (see jc_daemon.h). No sockets, no libcurl -- unit-tested offline. */

#include "jc_daemon.h"
#include "jc_json.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

const char *jc_daemon_type_name(enum jc_daemon_req_type t)
{
    switch (t) {
    case JC_DREQ_PROMPT:   return "prompt";
    case JC_DREQ_PING:     return "ping";
    case JC_DREQ_SHUTDOWN: return "shutdown";
    case JC_DREQ_HELLO:    return "hello";
    case JC_DREQ_ASSIGN_LIST:  return "assignment.list";
    case JC_DREQ_ASSIGN_GET:   return "assignment.get";
    case JC_DREQ_ASSIGN_GRADE: return "assignment.grade";
    default:               return "unknown";
    }
}

static enum jc_daemon_req_type type_from_name(const char *s)
{
    if (s == NULL) {
        return JC_DREQ_UNKNOWN;
    }
    if (strcmp(s, "prompt") == 0) {
        return JC_DREQ_PROMPT;
    }
    if (strcmp(s, "ping") == 0) {
        return JC_DREQ_PING;
    }
    if (strcmp(s, "shutdown") == 0) {
        return JC_DREQ_SHUTDOWN;
    }
    if (strcmp(s, "hello") == 0) {
        return JC_DREQ_HELLO;
    }
    if (strcmp(s, "assignment.list") == 0) {
        return JC_DREQ_ASSIGN_LIST;
    }
    if (strcmp(s, "assignment.get") == 0) {
        return JC_DREQ_ASSIGN_GET;
    }
    if (strcmp(s, "assignment.grade") == 0) {
        return JC_DREQ_ASSIGN_GRADE;
    }
    return JC_DREQ_UNKNOWN;
}

jc_status jc_daemon_parse_request(const char *line, struct jc_daemon_req *out,
                                  struct jc_arena *a)
{
    cJSON *root;
    const char *fmt;

    memset(out, 0, sizeof(*out));
    out->type = JC_DREQ_UNKNOWN;
    if (line == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(line);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return JC_ERR_PARSE;
    }
    out->type = type_from_name(jc_json_get_str(root, "type", "prompt"));
    out->prompt = jc_json_dup_str(root, "prompt", a);
    out->cwd = jc_json_dup_str(root, "cwd", a);
    out->mode = jc_json_dup_str(root, "mode", a);
    out->name = jc_json_dup_str(root, "name", a);   /* M529: ASSIGN_* verbs */
    fmt = jc_json_get_str(root, "format", NULL);
    /* M431g: three values, mapped to run_headless's codes. An unknown or absent
     * `format` is TEXT, which is what every pre-M431g client sent and what a
     * hand-written client omitting the field reasonably expects. */
    out->fmt = 0;
    if (fmt != NULL) {
        if (strcmp(fmt, "jsonl") == 0) {
            out->fmt = 2;
        } else if (strcmp(fmt, "json") == 0) {
            out->fmt = 1;
        }
    }
    cJSON_Delete(root);

    if (out->type == JC_DREQ_PROMPT &&
        (out->prompt == NULL || out->prompt[0] == '\0')) {
        return JC_ERR_INVALID;
    }
    return JC_OK;
}

char *jc_daemon_build_prompt(const char *prompt, const char *cwd,
                             const char *mode, int fmt)
{
    cJSON *root = cJSON_CreateObject();
    struct jc_sb sb;
    char *text;

    cJSON_AddNumberToObject(root, "v", JC_DAEMON_PROTO_V);
    cJSON_AddStringToObject(root, "type", "prompt");
    cJSON_AddStringToObject(root, "prompt", prompt != NULL ? prompt : "");
    if (cwd != NULL && cwd[0] != '\0') {
        cJSON_AddStringToObject(root, "cwd", cwd);
    }
    if (mode != NULL && mode[0] != '\0') {
        cJSON_AddStringToObject(root, "mode", mode);
    }
    cJSON_AddStringToObject(root, "format",
                            (fmt == 2) ? "jsonl" : (fmt == 1) ? "json" : "text");

    text = jc_json_print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return NULL;
    }
    /* Append a newline so the line can be written to the socket as-is. */
    jc_sb_init(&sb);
    jc_sb_append(&sb, text);
    jc_sb_append_char(&sb, '\n');
    free(text);
    return jc_sb_finish(&sb);
}

char *jc_daemon_build_ctl(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    struct jc_sb sb;
    char *text;

    cJSON_AddNumberToObject(root, "v", JC_DAEMON_PROTO_V);
    cJSON_AddStringToObject(root, "type", type != NULL ? type : "ping");
    text = jc_json_print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return NULL;
    }
    jc_sb_init(&sb);
    jc_sb_append(&sb, text);
    jc_sb_append_char(&sb, '\n');
    free(text);
    return jc_sb_finish(&sb);
}

/* --- M528: the handshake (see jc_daemon.h) -------------------------------- */

/* Render and newline-frame, consuming `o`. Same idiom as jc_daemon_build_ctl,
 * factored out rather than repeated: two builders formatting a line two ways is
 * how a wire grows a dialect. */
static char *frame_line(cJSON *o)
{
    struct jc_sb sb;
    char *text = jc_json_print(o);
    cJSON_Delete(o);
    if (text == NULL) {
        return NULL;
    }
    jc_sb_init(&sb);
    jc_sb_append(&sb, text);
    jc_sb_append_char(&sb, '\n');
    free(text);
    return jc_sb_finish(&sb);
}

char *jc_daemon_build_hello(const char *client)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", (double)JC_DAEMON_PROTO_V);
    cJSON_AddStringToObject(o, "type", "hello");
    if (client != NULL && client[0] != '\0') {
        cJSON_AddStringToObject(o, "client", client);
    }
    return frame_line(o);
}

char *jc_daemon_build_hello_ok(const struct jc_daemon_hello *h)
{
    cJSON *o;
    cJSON *protos;
    cJSON *groups;
    cJSON *limits;
    cJSON *auth;

    if (h == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", (double)JC_DAEMON_PROTO_V);
    cJSON_AddStringToObject(o, "type", "hello.ok");
    cJSON_AddStringToObject(o, "agent",
                            h->agent != NULL ? h->agent : "jichi");

    /* Every version this server accepts, so a client negotiates instead of
     * guessing from a single number. */
    protos = cJSON_CreateArray();
    if (protos != NULL) {
        cJSON_AddItemToArray(protos,
                             cJSON_CreateNumber((double)JC_DAEMON_PROTO_V));
        cJSON_AddItemToObject(o, "proto", protos);
    }

    /* The verb groups this server serves, named as the protocol proposal names
     * them, so a client written against this reply does not have to be
     * rewritten when others appear. M529 added `assignment`; a caller learns
     * the verbs exist from here rather than by trying one and reading an
     * error, which is the whole point of a handshake. */
    groups = cJSON_CreateArray();
    if (groups != NULL) {
        cJSON_AddItemToArray(groups, cJSON_CreateString("session"));
        cJSON_AddItemToArray(groups, cJSON_CreateString("assignment"));
        cJSON_AddItemToObject(o, "groups", groups);
    }

    limits = cJSON_CreateObject();
    if (limits != NULL) {
        cJSON_AddNumberToObject(limits, "maxLine", (double)h->max_line);
        cJSON_AddNumberToObject(limits, "maxConcurrent", (double)h->workers);
        cJSON_AddItemToObject(o, "limits", limits);
    }

    /* The posture, stated rather than implied -- see the header. */
    auth = cJSON_CreateObject();
    if (auth != NULL) {
        cJSON_AddStringToObject(auth, "transport", "unix");
        cJSON_AddStringToObject(auth, "mechanism", "socket-mode");
        cJSON_AddNumberToObject(auth, "uid", (double)h->uid);
        cJSON_AddBoolToObject(auth, "modeVerified", h->mode_verified ? 1 : 0);
        cJSON_AddBoolToObject(auth, "peercred", h->peercred ? 1 : 0);
        cJSON_AddItemToObject(o, "auth", auth);
    }

    return frame_line(o);
}
