/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_acp_proto.c - pure JSON-RPC / ACP message shaping.
 *
 * No I/O: every function works on in-memory strings and cJSON trees, so the
 * protocol is exercised offline by the test suite. The server loop (jc_acp.c)
 * provides the bytes. The JSON-RPC envelope builders (request/notification) are
 * reused from jc_mcp_proto.c; this file adds the response/error envelopes and
 * the ACP-specific param/result shapes.
 */

#include "jc_acp.h"
#include "jc_mcp.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_message.h"

#include <stdlib.h>
#include <string.h>

/* Build {jsonrpc,id,result} or {jsonrpc,id,error}. `payload` is consumed and is
 * attached under `key` ("result" or "error"). */
static char *build_reply(long id, const char *key, cJSON *payload)
{
    cJSON *root;
    char *out;

    root = cJSON_CreateObject();
    if (root == NULL) {
        if (payload != NULL) {
            cJSON_Delete(payload);
        }
        return NULL;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", (double)id);
    if (payload == NULL) {
        payload = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(root, key, payload);
    out = jc_json_print(root);
    cJSON_Delete(root);
    return out;
}

char *jc_acp_build_response(long id, cJSON *result)
{
    return build_reply(id, "result", result);
}

char *jc_acp_build_error(long id, long code, const char *message)
{
    cJSON *err = cJSON_CreateObject();
    if (err != NULL) {
        cJSON_AddNumberToObject(err, "code", (double)code);
        cJSON_AddStringToObject(err, "message",
                                message != NULL ? message : "error");
    }
    return build_reply(id, "error", err);
}

cJSON *jc_acp_build_init_result(int protocol_version, int image_supported,
                                int audio_supported)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *caps;
    cJSON *prompt_caps;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "protocolVersion", (double)protocol_version);

    caps = cJSON_CreateObject();
    /* We persist ACP sessions to the same store as the CLI (the ACP sessionId
     * is the jichi session id), so a client can reload one and we replay it.
     * We still run tools ourselves (no client-side fs/terminal needed). */
    cJSON_AddBoolToObject(caps, "loadSession", 1);
    prompt_caps = cJSON_CreateObject();
    cJSON_AddBoolToObject(prompt_caps, "image", image_supported ? 1 : 0);
    cJSON_AddBoolToObject(prompt_caps, "audio", audio_supported ? 1 : 0);
    cJSON_AddBoolToObject(prompt_caps, "embeddedContext", 1);
    cJSON_AddItemToObject(caps, "promptCapabilities", prompt_caps);
    cJSON_AddItemToObject(root, "agentCapabilities", caps);

    /* No authentication required. */
    cJSON_AddItemToObject(root, "authMethods", cJSON_CreateArray());
    return root;
}

char *jc_acp_build_update(const char *session_id, cJSON *update)
{
    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        if (update != NULL) {
            cJSON_Delete(update);
        }
        return NULL;
    }
    cJSON_AddStringToObject(params, "sessionId",
                            session_id != NULL ? session_id : "");
    if (update == NULL) {
        update = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(params, "update", update);
    return jc_mcp_build_notification("session/update", params);
}

/* A {type:"text",text:...} content block. */
static cJSON *text_content(const char *text)
{
    cJSON *c = cJSON_CreateObject();
    if (c != NULL) {
        cJSON_AddStringToObject(c, "type", "text");
        cJSON_AddStringToObject(c, "text", text != NULL ? text : "");
    }
    return c;
}

cJSON *jc_acp_update_message_chunk(const char *text)
{
    cJSON *u = cJSON_CreateObject();
    if (u == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(u, "sessionUpdate", "agent_message_chunk");
    cJSON_AddItemToObject(u, "content", text_content(text));
    return u;
}

cJSON *jc_acp_update_user_message_chunk(const char *text)
{
    cJSON *u = cJSON_CreateObject();
    if (u == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(u, "sessionUpdate", "user_message_chunk");
    cJSON_AddItemToObject(u, "content", text_content(text));
    return u;
}

cJSON *jc_acp_update_tool_call(const char *tool_id, const char *title,
                               const char *kind, const char *status,
                               const char *raw_input_json)
{
    cJSON *u = cJSON_CreateObject();
    if (u == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(u, "sessionUpdate", "tool_call");
    cJSON_AddStringToObject(u, "toolCallId", tool_id != NULL ? tool_id : "");
    cJSON_AddStringToObject(u, "title", title != NULL ? title : "");
    cJSON_AddStringToObject(u, "kind", kind != NULL ? kind : "other");
    cJSON_AddStringToObject(u, "status", status != NULL ? status : "pending");
    if (raw_input_json != NULL && raw_input_json[0] != '\0') {
        cJSON *raw = jc_json_parse(raw_input_json);
        if (raw != NULL) {
            cJSON_AddItemToObject(u, "rawInput", raw);
        }
    }
    return u;
}

cJSON *jc_acp_update_tool_call_status(const char *tool_id, const char *status,
                                      const char *text)
{
    cJSON *u = cJSON_CreateObject();
    if (u == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(u, "sessionUpdate", "tool_call_update");
    cJSON_AddStringToObject(u, "toolCallId", tool_id != NULL ? tool_id : "");
    cJSON_AddStringToObject(u, "status", status != NULL ? status : "completed");
    if (text != NULL && text[0] != '\0') {
        cJSON *arr = cJSON_CreateArray();
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "content");
        cJSON_AddItemToObject(block, "content", text_content(text));
        cJSON_AddItemToArray(arr, block);
        cJSON_AddItemToObject(u, "content", arr);
    }
    return u;
}

cJSON *jc_acp_update_tool_call_terminal(const char *tool_id,
                                        const char *terminal_id)
{
    cJSON *u = cJSON_CreateObject();
    cJSON *arr;
    cJSON *block;
    if (u == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(u, "sessionUpdate", "tool_call_update");
    cJSON_AddStringToObject(u, "toolCallId", tool_id != NULL ? tool_id : "");
    arr = cJSON_CreateArray();
    block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "terminal");
    cJSON_AddStringToObject(block, "terminalId",
                            terminal_id != NULL ? terminal_id : "");
    cJSON_AddItemToArray(arr, block);
    cJSON_AddItemToObject(u, "content", arr);
    return u;
}

/* One permission option {optionId,name,kind}. */
static cJSON *perm_option(const char *id, const char *name, const char *kind)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "optionId", id);
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddStringToObject(o, "kind", kind);
    return o;
}

cJSON *jc_acp_permission_params(const char *session_id, const char *tool_id,
                                const char *title, const char *kind)
{
    cJSON *params = cJSON_CreateObject();
    cJSON *tc;
    cJSON *opts;

    if (params == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(params, "sessionId",
                            session_id != NULL ? session_id : "");
    tc = cJSON_CreateObject();
    cJSON_AddStringToObject(tc, "toolCallId", tool_id != NULL ? tool_id : "");
    cJSON_AddStringToObject(tc, "title", title != NULL ? title : "");
    cJSON_AddStringToObject(tc, "kind", kind != NULL ? kind : "other");
    cJSON_AddStringToObject(tc, "status", "pending");
    cJSON_AddItemToObject(params, "toolCall", tc);

    opts = cJSON_CreateArray();
    cJSON_AddItemToArray(opts,
        perm_option("allow_once", "Allow", "allow_once"));
    cJSON_AddItemToArray(opts,
        perm_option("allow_always", "Always allow", "allow_always"));
    cJSON_AddItemToArray(opts,
        perm_option("reject_once", "Reject", "reject_once"));
    cJSON_AddItemToObject(params, "options", opts);
    return params;
}

enum jc_acp_perm_outcome jc_acp_parse_permission_outcome(const char *resp_json)
{
    cJSON *root;
    cJSON *result;
    cJSON *outcome;
    const char *kind;
    const char *opt;
    enum jc_acp_perm_outcome ret = JC_ACP_PERM_REJECT;

    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return JC_ACP_PERM_REJECT;
    }
    result = jc_json_get_obj(root, "result");
    outcome = jc_json_get_obj(result, "outcome");
    if (!cJSON_IsObject(outcome)) {
        cJSON_Delete(root);
        return JC_ACP_PERM_REJECT;
    }
    kind = jc_json_get_str(outcome, "outcome", "");
    if (strcmp(kind, "cancelled") == 0) {
        cJSON_Delete(root);
        return JC_ACP_PERM_CANCELLED;
    }
    opt = jc_json_get_str(outcome, "optionId", "");
    if (strcmp(opt, "allow_once") == 0) {
        ret = JC_ACP_PERM_ALLOW_ONCE;
    } else if (strcmp(opt, "allow_always") == 0) {
        ret = JC_ACP_PERM_ALLOW_ALWAYS;
    } else {
        ret = JC_ACP_PERM_REJECT;
    }
    cJSON_Delete(root);
    return ret;
}

char *jc_acp_prompt_text(const cJSON *params)
{
    cJSON *prompt;
    cJSON *block;
    struct jc_sb sb;

    jc_sb_init(&sb);
    prompt = jc_json_get_obj((cJSON *)params, "prompt");
    if (cJSON_IsArray(prompt)) {
        cJSON_ArrayForEach(block, prompt) {
            const char *type = jc_json_get_str(block, "type", "");
            if (strcmp(type, "text") == 0) {
                const char *t = jc_json_get_str(block, "text", "");
                if (sb.len > 0) {
                    jc_sb_append_char(&sb, '\n');
                }
                jc_sb_append(&sb, t);
            } else if (strcmp(type, "resource") == 0) {
                /* Embedded resource: {resource:{text|uri,...}}. */
                cJSON *res = jc_json_get_obj(block, "resource");
                const char *t = jc_json_get_str(res, "text", NULL);
                const char *uri = jc_json_get_str(res, "uri", NULL);
                if (sb.len > 0) {
                    jc_sb_append_char(&sb, '\n');
                }
                if (uri != NULL) {
                    jc_sb_append_fmt(&sb, "%s:\n", uri);
                }
                if (t != NULL) {
                    jc_sb_append(&sb, t);
                }
            } else if (strcmp(type, "resource_link") == 0) {
                const char *uri = jc_json_get_str(block, "uri", "");
                if (sb.len > 0) {
                    jc_sb_append_char(&sb, '\n');
                }
                jc_sb_append_fmt(&sb, "@%s", uri);
            }
            /* image/audio blocks are ignored (not advertised). */
        }
    }
    {
        char *out = jc_strdup(sb.data != NULL ? sb.data : "");
        jc_sb_free(&sb);
        return out;
    }
}

int jc_acp_prompt_images(const cJSON *params, struct jc_message *m)
{
    cJSON *prompt;
    cJSON *block;
    int n = 0;

    if (m == NULL) {
        return 0;
    }
    prompt = jc_json_get_obj((cJSON *)params, "prompt");
    if (!cJSON_IsArray(prompt)) {
        return 0;
    }
    cJSON_ArrayForEach(block, prompt) {
        const char *type = jc_json_get_str(block, "type", "");
        if (strcmp(type, "image") == 0) {
            const char *data = jc_json_get_str(block, "data", NULL);
            const char *mime = jc_json_get_str(block, "mimeType", NULL);
            if (data != NULL && data[0] != '\0' &&
                jc_msg_add_image(m, mime != NULL ? mime : "image/png", data)
                    == JC_OK) {
                n++;
            }
        }
    }
    return n;
}

const char *jc_acp_tool_kind(const char *name)
{
    if (name == NULL) {
        return "other";
    }
    if (strcmp(name, "read_file") == 0 || strcmp(name, "list_files") == 0 ||
        strcmp(name, "list_symbols") == 0 ||
        strcmp(name, "find_definition") == 0 ||
        strcmp(name, "find_references") == 0 ||
        strncmp(name, "git_", 4) == 0) {
        return "read";
    }
    if (strcmp(name, "write_file") == 0 || strcmp(name, "edit_file") == 0) {
        return "edit";
    }
    if (strcmp(name, "search_code") == 0 ||
        strcmp(name, "codebase_search") == 0) {
        return "search";
    }
    if (strcmp(name, "run_terminal_command") == 0 ||
        strcmp(name, "run_tests") == 0) {
        return "execute";
    }
    if (strcmp(name, "fetch_url") == 0) {
        return "fetch";
    }
    return "other";
}

const char *jc_acp_stop_reason(int aborted)
{
    return aborted ? "cancelled" : "end_turn";
}

void jc_acp_client_fs_caps(const cJSON *params, int *can_read, int *can_write)
{
    cJSON *caps;
    cJSON *fs;

    if (can_read != NULL) {
        *can_read = 0;
    }
    if (can_write != NULL) {
        *can_write = 0;
    }
    caps = jc_json_get_obj((cJSON *)params, "clientCapabilities");
    fs = jc_json_get_obj(caps, "fs");
    if (!cJSON_IsObject(fs)) {
        return;
    }
    if (can_read != NULL) {
        *can_read = jc_json_get_bool_lenient(fs, "readTextFile", 0);
    }
    if (can_write != NULL) {
        *can_write = jc_json_get_bool_lenient(fs, "writeTextFile", 0);
    }
}

cJSON *jc_acp_fs_read_params(const char *session_id, const char *path)
{
    cJSON *p = cJSON_CreateObject();
    if (p == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(p, "sessionId",
                            session_id != NULL ? session_id : "");
    cJSON_AddStringToObject(p, "path", path != NULL ? path : "");
    return p;
}

cJSON *jc_acp_fs_write_params(const char *session_id, const char *path,
                              const char *content)
{
    cJSON *p = cJSON_CreateObject();
    if (p == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(p, "sessionId",
                            session_id != NULL ? session_id : "");
    cJSON_AddStringToObject(p, "path", path != NULL ? path : "");
    cJSON_AddStringToObject(p, "content", content != NULL ? content : "");
    return p;
}

char *jc_acp_parse_fs_read_result(const char *resp_json)
{
    cJSON *root;
    cJSON *result;
    const char *content;
    char *out = NULL;

    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return NULL;
    }
    result = jc_json_get_obj(root, "result");
    if (cJSON_IsObject(result)) {
        content = jc_json_get_str(result, "content", NULL);
        if (content != NULL) {
            out = jc_strdup(content);
        }
    }
    cJSON_Delete(root);
    return out;
}

int jc_acp_client_terminal_cap(const cJSON *params)
{
    cJSON *caps = jc_json_get_obj((cJSON *)params, "clientCapabilities");
    return jc_json_get_bool_lenient(caps, "terminal", 0);
}

cJSON *jc_acp_terminal_create_params(const char *session_id, const char *command,
                                     const char *cwd, long output_byte_limit)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *args;
    if (p == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(p, "sessionId",
                            session_id != NULL ? session_id : "");
    /* Run through the shell so the command string keeps its usual semantics.
     *
     * M461 deliberately does NOT resolve this via jc_shell_path(). This is a
     * protocol payload naming a command for the CLIENT to spawn, and this
     * file is the pure, unit-tested protocol layer -- jc_shell_path() calls
     * access(), so probing here would make a pure builder platform-dependent
     * and its golden tests machine-dependent. Known limitation: an ACP client
     * on a host without /bin/sh (Android) gets a command it cannot run. The
     * fix belongs in the caller, which would have to pass the path in. */
    cJSON_AddStringToObject(p, "command", "/bin/sh");
    args = cJSON_CreateArray();
    cJSON_AddItemToArray(args, cJSON_CreateString("-c"));
    cJSON_AddItemToArray(args, cJSON_CreateString(command != NULL ? command : ""));
    cJSON_AddItemToObject(p, "args", args);
    if (cwd != NULL && cwd[0] != '\0') {
        cJSON_AddStringToObject(p, "cwd", cwd);
    }
    if (output_byte_limit > 0) {
        cJSON_AddNumberToObject(p, "outputByteLimit", (double)output_byte_limit);
    }
    return p;
}

cJSON *jc_acp_terminal_id_params(const char *session_id,
                                 const char *terminal_id)
{
    cJSON *p = cJSON_CreateObject();
    if (p == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(p, "sessionId",
                            session_id != NULL ? session_id : "");
    cJSON_AddStringToObject(p, "terminalId",
                            terminal_id != NULL ? terminal_id : "");
    return p;
}

char *jc_acp_parse_terminal_id(const char *resp_json)
{
    cJSON *root;
    cJSON *result;
    const char *tid;
    char *out = NULL;

    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return NULL;
    }
    result = jc_json_get_obj(root, "result");
    if (cJSON_IsObject(result)) {
        tid = jc_json_get_str(result, "terminalId", NULL);
        if (tid != NULL) {
            out = jc_strdup(tid);
        }
    }
    cJSON_Delete(root);
    return out;
}

/* Read an ACP exitStatus object ({exitCode:number|null, signal:string|null}).
 * Returns 1 if it is a present, non-null object (i.e. the process exited),
 * setting *code to exitCode (or -1 when only a signal/no code is reported). */
static int read_exit_status(cJSON *es, int *code)
{
    cJSON *ec;
    if (!cJSON_IsObject(es)) {
        return 0;
    }
    ec = cJSON_GetObjectItem(es, "exitCode");
    if (code != NULL) {
        *code = cJSON_IsNumber(ec) ? (int)ec->valuedouble : -1;
    }
    return 1;
}

int jc_acp_parse_exit_status(const char *resp_json, int *exit_code)
{
    cJSON *root;
    cJSON *result;
    int exited = 0;

    if (exit_code != NULL) {
        *exit_code = -1;
    }
    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return 0;
    }
    result = jc_json_get_obj(root, "result");
    if (cJSON_IsObject(result)) {
        exited = read_exit_status(jc_json_get_obj(result, "exitStatus"),
                                  exit_code);
    }
    cJSON_Delete(root);
    return exited;
}

int jc_acp_parse_terminal_output(const char *resp_json, char **out_text,
                                 int *truncated, int *exit_code, int *exited)
{
    cJSON *root;
    cJSON *result;
    int ok = 0;

    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (truncated != NULL) {
        *truncated = 0;
    }
    if (exit_code != NULL) {
        *exit_code = -1;
    }
    if (exited != NULL) {
        *exited = 0;
    }
    root = jc_json_parse(resp_json);
    if (root == NULL) {
        return 0;
    }
    result = jc_json_get_obj(root, "result");
    if (cJSON_IsObject(result)) {
        const char *output;
        int ex;
        ok = 1;
        output = jc_json_get_str(result, "output", "");
        if (out_text != NULL) {
            *out_text = jc_strdup(output != NULL ? output : "");
        }
        if (truncated != NULL) {
            *truncated = jc_json_get_bool_lenient(result, "truncated", 0);
        }
        ex = read_exit_status(jc_json_get_obj(result, "exitStatus"), exit_code);
        if (exited != NULL) {
            *exited = ex;
        }
    }
    cJSON_Delete(root);
    return ok;
}
