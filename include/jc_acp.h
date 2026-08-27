/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_acp.h - Agent Client Protocol (ACP) server.
 *
 * ACP is the editor<->agent JSON-RPC 2.0 protocol (used e.g. by Zed). It lets an
 * editor drive jichi as an *agent server*: the editor sends `initialize`,
 * `session/new`, and `session/prompt`; jichi streams the assistant's reply and tool
 * activity back as `session/update` notifications and asks for tool approval with
 * `session/request_permission`.
 *
 * Transport is newline-delimited JSON-RPC 2.0 over our own stdin/stdout (the same
 * line framing as the MCP stdio transport, inverted: we read requests and write
 * responses). The pure message shaping lives in jc_acp_proto.c (reusing the MCP
 * JSON-RPC builders) and is unit-tested offline; the blocking server loop and the
 * agent-callback -> notification mapping live in jc_acp.c.
 */
#ifndef JC_ACP_H
#define JC_ACP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "cJSON.h"

struct jc_app; /* jc_app.h */

/* The ACP protocol version jichi speaks. */
#define JC_ACP_PROTOCOL_VERSION 1

/* Run the ACP server loop on stdin/stdout until the client closes the
 * connection (EOF) or an unrecoverable I/O error occurs. Returns a process exit
 * code (0 on clean shutdown). Reuses `app`'s provider/tools/permissions. */
int jc_acp_serve(struct jc_app *app);

/* --- pure protocol shaping (jc_acp_proto.c; unit-tested) --- */

/* The outcome the client selected for a `session/request_permission`. */
enum jc_acp_perm_outcome {
    JC_ACP_PERM_ALLOW_ONCE,
    JC_ACP_PERM_ALLOW_ALWAYS,
    JC_ACP_PERM_REJECT,
    JC_ACP_PERM_CANCELLED
};

/* Build a JSON-RPC response `{jsonrpc,id,result}` (result is consumed; NULL =>
 * an empty object). Returns malloc'd compact JSON (no newline), or NULL. */
char *jc_acp_build_response(long id, cJSON *result);

/* Build a JSON-RPC error response `{jsonrpc,id,error:{code,message}}`. */
char *jc_acp_build_error(long id, long code, const char *message);

/* The `initialize` result object: protocol version + agent capabilities.
 * `image_supported` advertises promptCapabilities.image (M29: set when the
 * active model is vision-capable); `audio_supported` advertises
 * promptCapabilities.audio (M33: set when a "transcribe"-role model exists). */
cJSON *jc_acp_build_init_result(int protocol_version, int image_supported,
                                int audio_supported);

/* A `session/update` notification carrying `update` (consumed) for `session_id`.
 * Returns malloc'd compact JSON. */
char *jc_acp_build_update(const char *session_id, cJSON *update);

/* session/update payloads (objects to hand to jc_acp_build_update). */
cJSON *jc_acp_update_message_chunk(const char *text);
/* Like message_chunk but for replaying a *user* turn on session/load. */
cJSON *jc_acp_update_user_message_chunk(const char *text);
cJSON *jc_acp_update_tool_call(const char *tool_id, const char *title,
                               const char *kind, const char *status,
                               const char *raw_input_json);
cJSON *jc_acp_update_tool_call_status(const char *tool_id, const char *status,
                                      const char *text);
/* A tool_call_update whose content embeds a live terminal (so the editor shows
 * the running command's output in its terminal UI). */
cJSON *jc_acp_update_tool_call_terminal(const char *tool_id,
                                        const char *terminal_id);

/* The params object for a `session/request_permission` request (allow once /
 * always / reject options). */
cJSON *jc_acp_permission_params(const char *session_id, const char *tool_id,
                                const char *title, const char *kind);

/* Parse the option the client chose from a request_permission *response*. */
enum jc_acp_perm_outcome jc_acp_parse_permission_outcome(const char *resp_json);

/* Concatenate the text of a `session/prompt` params `prompt[]` content array
 * into a single malloc'd string (text + resource blocks; others noted). */
char *jc_acp_prompt_text(const cJSON *params);

/* Attach any {type:"image",data,mimeType} prompt blocks to message `m` (M29d).
 * The data is already base64 (from the client), so it is attached directly.
 * Returns the number attached. The caller gates this on the active model's
 * vision capability. */
struct jc_message; /* jc_message.h */
int jc_acp_prompt_images(const cJSON *params, struct jc_message *m);

/* Map a tool name to an ACP tool-call `kind` (read/edit/search/execute/...). */
const char *jc_acp_tool_kind(const char *name);

/* Parse the client's fs capabilities from `initialize` params
 * (`clientCapabilities.fs.{readTextFile,writeTextFile}`) into *can_read /
 * *can_write (each set to 0 or 1; either may be NULL). */
void jc_acp_client_fs_caps(const cJSON *params, int *can_read, int *can_write);

/* Params for an `fs/read_text_file` / `fs/write_text_file` request. `path`
 * should be absolute (ACP requires it). */
cJSON *jc_acp_fs_read_params(const char *session_id, const char *path);
cJSON *jc_acp_fs_write_params(const char *session_id, const char *path,
                              const char *content);

/* Extract `result.content` (the file text) from an `fs/read_text_file`
 * response; returns malloc'd text, or NULL if absent/an error response. */
char *jc_acp_parse_fs_read_result(const char *resp_json);

/* Whether the client advertised the `terminal` capability in `initialize`
 * (`clientCapabilities.terminal`). 0 or 1. */
int jc_acp_client_terminal_cap(const cJSON *params);

/* Params for `terminal/create`: runs `command` via `/bin/sh -c`, in `cwd`
 * (omitted when NULL/empty), with `output_byte_limit` (omitted when <= 0). */
cJSON *jc_acp_terminal_create_params(const char *session_id, const char *command,
                                     const char *cwd, long output_byte_limit);

/* Params shared by `terminal/output` / `terminal/wait_for_exit` /
 * `terminal/kill` / `terminal/release`: `{sessionId, terminalId}`. */
cJSON *jc_acp_terminal_id_params(const char *session_id,
                                 const char *terminal_id);

/* Extract `result.terminalId` from a `terminal/create` response; malloc'd, or
 * NULL on an error/absent response. */
char *jc_acp_parse_terminal_id(const char *resp_json);

/* Parse `result.exitStatus` (from `terminal/wait_for_exit`): sets *exit_code
 * (exitCode, or 128+signal mapping unavailable => -1) and returns 1 if the
 * process has exited (exitStatus present + non-null), else 0. */
int jc_acp_parse_exit_status(const char *resp_json, int *exit_code);

/* Parse a `terminal/output` response: *out_text (malloc'd combined output, "" if
 * none), *truncated (client capped the buffer), and the exit_code + exited
 * out-params from the optional exitStatus (exited=0 while the process still
 * runs; signal-only exits report exit_code -1). Returns 1 when a
 * result object was present, else 0. Any out-param may be NULL. */
int jc_acp_parse_terminal_output(const char *resp_json, char **out_text,
                                 int *truncated, int *exit_code, int *exited);

/* The ACP `stopReason` for a finished turn. */
const char *jc_acp_stop_reason(int aborted);

#ifdef __cplusplus
}
#endif
#endif /* JC_ACP_H */
