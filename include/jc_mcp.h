/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mcp.h - Model Context Protocol client.
 *
 * Connects to MCP servers declared in the config's "mcpServers" array, runs
 * the initialize -> tools/list handshake, and registers each remote tool into
 * the agent's tool registry as a dynamic jc_tool (see jc_tool.h: ctx +
 * schema_ctx/run_ctx). Calling such a tool issues a tools/call request and
 * returns the server's content as the tool result. It also discovers a server's
 * resources/list and prompts/list (M43): resources are exposed to the agent via
 * the read_mcp_resource tool and to the user via `mcp resources`/`mcp read`;
 * prompts via `mcp prompts`/`mcp prompt`.
 *
 * Two transports are supported:
 *   - stdio: the server is a subprocess; JSON-RPC messages are newline-framed
 *     over its stdin/stdout (POSIX fork/exec/pipe).
 *   - http:  the server is an HTTP endpoint; each request is POSTed and the
 *     response is read as JSON or as a Server-Sent-Events stream. Requires
 *     libcurl (compiled only when JC_HAVE_CURL is defined).
 *
 * The JSON-RPC framing and the tools/list and tools/call result parsers are
 * pure (no I/O) and unit-tested offline; the transports are thin shims over
 * those.
 */
#ifndef JC_MCP_H
#define JC_MCP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_config.h"
#include "cJSON.h"

struct jc_app;             /* jc_app.h  */
struct jc_tool_registry;   /* jc_tool.h */
struct jc_mcp_conn;        /* mcp_internal.h (opaque here) */

/* ----- pure protocol layer (unit-tested offline) ----------------------- */

/* A tool as advertised by a server's tools/list. All fields are malloc'd.
 * The schema is kept as serialised JSON (the in-tree cJSON has no deep-copy);
 * callers re-parse it to obtain an owned tree. Free with
 * jc_mcp_tool_desc_free. */
struct jc_mcp_tool_desc {
    char *name;
    char *description;
    char *input_schema_json; /* the server's "inputSchema" as JSON, or NULL */
    int   readonly;          /* from annotations.readOnlyHint; default 0    */
};

void jc_mcp_tool_desc_free(struct jc_mcp_tool_desc *d);

/* A resource as advertised by a server's resources/list. All fields malloc'd
 * (any may be NULL/empty except uri). Free with jc_mcp_resource_desc_free. */
struct jc_mcp_resource_desc {
    char *uri;
    char *name;
    char *description;
    char *mime_type;
};
void jc_mcp_resource_desc_free(struct jc_mcp_resource_desc *d);

/* A single declared argument of a prompt (from prompts/list arguments[]). */
struct jc_mcp_prompt_arg {
    char *name;
    int   required;
};

/* A prompt as advertised by a server's prompts/list. Free with
 * jc_mcp_prompt_desc_free. `args`/`nargs` carry the declared arguments in
 * order (used to map positional invocation args to named ones). */
struct jc_mcp_prompt_desc {
    char *name;
    char *description;
    struct jc_mcp_prompt_arg *args;
    int   nargs;
};
void jc_mcp_prompt_desc_free(struct jc_mcp_prompt_desc *d);

/* Map a raw argument string to a prompts/get `arguments` object using the
 * prompt's declared argument names (`argnames`, in order; may be NULL/empty).
 * Whitespace-separated tokens: a `key=value` token sets that named argument
 * (whether declared or not); any other token fills the next not-yet-set
 * declared argument positionally. Extra positionals (beyond the declared set)
 * are ignored. Returns a new cJSON object (caller owns via cJSON_Delete);
 * never NULL. Pure + unit-tested. Quoting/spaces in values are not parsed (v1).
 */
cJSON *jc_mcp_build_prompt_args(const char *const *argnames,
                                int nargnames, const char *raw);

/* Build a compact (newline-free) JSON-RPC 2.0 request / notification line.
 * `params` is consumed (added to the message or deleted); it may be NULL.
 * Returns a malloc'd NUL-terminated string (free with free()), or NULL on
 * allocation failure. */
char *jc_mcp_build_request(long id, const char *method, cJSON *params);
char *jc_mcp_build_notification(const char *method, cJSON *params);

/* If `json` is a single JSON-RPC message carrying a numeric "id", store it in
 * *id_out and return 1; otherwise (notification, malformed, non-numeric id)
 * return 0. Used to pair responses with requests. */
int jc_mcp_message_id(const char *json, long *id_out);

/* Parse a tools/list JSON-RPC response (its result.tools[] array) into `out`,
 * an initialised jc_vec of struct jc_mcp_tool_desc. Returns JC_ERR_PROVIDER if
 * the response is a JSON-RPC error, JC_ERR_PARSE if malformed. */
jc_status jc_mcp_parse_tools(const char *resp_json, struct jc_vec *out);

/* Parse a tools/call JSON-RPC response. On success *text_out receives a
 * malloc'd concatenation of the result's text content blocks (caller frees)
 * and *is_error_out the result's isError flag. A JSON-RPC error response is
 * reported as text + *is_error_out = 1 (so the model sees it as a tool error),
 * still returning JC_OK. Returns JC_ERR_PARSE only on malformed JSON. */
jc_status jc_mcp_parse_call_result(const char *resp_json, char **text_out,
                                   int *is_error_out);

/* Namespace a remote tool name as "<server>__<tool>", sanitised to
 * [A-Za-z0-9_-] and bounded to fit a provider tool-name limit. Writes into
 * `buf` (capacity `cap`). */
void jc_mcp_tool_fqname(const char *server, const char *tool, char *buf,
                        jc_size cap);

/* Parse a resources/list response (result.resources[]) into `out`, an
 * initialised jc_vec of struct jc_mcp_resource_desc. Same return contract as
 * jc_mcp_parse_tools (JC_ERR_PROVIDER on a JSON-RPC error, JC_ERR_PARSE on
 * malformed; JC_OK with no entries for a server that lists none). */
jc_status jc_mcp_parse_resources(const char *resp_json, struct jc_vec *out);

/* Parse a resources/read response: concatenate result.contents[] text blocks
 * (each {uri,mimeType,text}; a non-text {blob} block notes its presence) into
 * *text_out (malloc'd, caller frees). JSON-RPC errors become text. JC_ERR_PARSE
 * only on malformed JSON. */
jc_status jc_mcp_parse_resource_read(const char *resp_json, char **text_out);

/* Parse a prompts/list response (result.prompts[]) into `out`, a jc_vec of
 * struct jc_mcp_prompt_desc. Same return contract as jc_mcp_parse_tools. */
jc_status jc_mcp_parse_prompts(const char *resp_json, struct jc_vec *out);

/* Parse a prompts/get response: render result.messages[] as "<role>: <text>"
 * lines into *text_out (malloc'd, caller frees). JSON-RPC errors become text.
 * JC_ERR_PARSE only on malformed JSON. */
jc_status jc_mcp_parse_prompt_get(const char *resp_json, char **text_out);

/* ----- manager --------------------------------------------------------- */

struct jc_mcp_manager {
    struct jc_app *app;
    struct jc_vec  conns;  /* of struct jc_mcp_conn* (heap, owned) */
    struct jc_vec  tools;  /* of struct jc_tool*     (heap, owned) */
    struct jc_vec  ctxs;   /* of void* tool contexts (heap, owned) */
    struct jc_vec  resources; /* of struct mcp_resource_entry (M43) */
    struct jc_vec  prompts;   /* of struct mcp_prompt_entry   (M43) */
};

void jc_mcp_manager_init(struct jc_mcp_manager *m, struct jc_app *app);

/* Connect to every server in `cfg->mcp_servers`: spawn/open the transport, run
 * the initialize + tools/list handshake, and register each discovered tool
 * into `reg`. A server that fails to connect is logged and skipped (the agent
 * still runs with whatever succeeded). Returns the number of tools registered.
 */
int jc_mcp_connect_all(struct jc_mcp_manager *m, struct jc_config *cfg,
                       struct jc_tool_registry *reg);

/* Per-tool approval policy, derived from a server's autoApprove/deny config. */
enum jc_mcp_approval {
    JC_MCP_APPROVAL_ASK = 0, /* prompt as usual (the default)        */
    JC_MCP_APPROVAL_ALLOW,   /* run without prompting                */
    JC_MCP_APPROVAL_DENY     /* always refuse, without calling       */
};

/* Approval policy for the registered tool named `fqname` ("<server>__<tool>").
 * Returns JC_MCP_APPROVAL_ASK for unknown names (e.g. built-in tools), so the
 * agent can call this unconditionally. Reports DENY even for tools hidden from
 * the registry, so a model that names one anyway is still refused. */
int jc_mcp_tool_policy(const struct jc_mcp_manager *m, const char *fqname);

/* True iff `fqname` belongs to a server marked kinetic:true (M163a). */
int jc_mcp_tool_kinetic(const struct jc_mcp_manager *m, const char *fqname);

/* Iterate every discovered MCP tool (including deny'd ones hidden from the
 * agent registry). jc_mcp_tool_at returns the i-th tool's namespaced name (or
 * NULL if out of range) and, when the out-params are non-NULL, its approval
 * policy and description. */
int         jc_mcp_tool_count(const struct jc_mcp_manager *m);
const char *jc_mcp_tool_at(const struct jc_mcp_manager *m, int i,
                           int *policy_out, const char **desc_out);

/* ----- resources / prompts (M43) --------------------------------------- */

/* Discovered resources (across all servers). jc_mcp_resource_at returns the
 * i-th resource's uri (NULL if out of range) and, when non-NULL, its owning
 * server name and description. */
int         jc_mcp_resource_count(const struct jc_mcp_manager *m);
const char *jc_mcp_resource_at(const struct jc_mcp_manager *m, int i,
                               const char **server_out, const char **desc_out);

/* Read the resource named `uri` via its server's resources/read. On JC_OK,
 * *text_out is the (malloc'd) concatenated text (caller frees). JC_ERR_INVALID
 * if no resource matches `uri`; a transport/parse error otherwise. */
jc_status jc_mcp_read_resource(struct jc_mcp_manager *m, const char *uri,
                               char **text_out);

/* Discovered prompts. jc_mcp_prompt_at returns the i-th prompt's name and,
 * when non-NULL, its owning server name and description. */
int         jc_mcp_prompt_count(const struct jc_mcp_manager *m);
const char *jc_mcp_prompt_at(const struct jc_mcp_manager *m, int i,
                             const char **server_out, const char **desc_out);

/* Fetch the prompt `name` via its server's prompts/get (no arguments). On JC_OK
 * *text_out is the (malloc'd) rendered messages. JC_ERR_INVALID if no prompt
 * matches; a transport/parse error otherwise. Thin wrapper over _args(.., NULL). */
jc_status jc_mcp_get_prompt(struct jc_mcp_manager *m, const char *name,
                            char **text_out);

/* As jc_mcp_get_prompt, but `raw_args` (may be NULL/empty) is mapped to the
 * prompt's declared arguments via jc_mcp_build_prompt_args and sent in the
 * prompts/get request. */
jc_status jc_mcp_get_prompt_args(struct jc_mcp_manager *m, const char *name,
                                 const char *raw_args, char **text_out);

/* Number of live server connections. */
int jc_mcp_server_count(const struct jc_mcp_manager *m);

/* For the i-th connection, its configured name and tool count (for `/mcp`).
 * Returns NULL name if i is out of range. */
const char *jc_mcp_server_name(const struct jc_mcp_manager *m, int i,
                               int *tool_count_out);

/* Close all connections and free every registered dynamic tool. After this the
 * registry must not be used to look up MCP tools (call before/after registry
 * teardown -- the registry only holds borrowed pointers). */
void jc_mcp_manager_shutdown(struct jc_mcp_manager *m);

#ifdef __cplusplus
}
#endif
#endif /* JC_MCP_H */
