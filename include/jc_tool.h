/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool.h - tool registry, schema emission, and execution.
 *
 * A tool exposes: a name, a description, a JSON-schema "parameters" object,
 * a readonly flag, and a run() function. run() receives parsed arguments and
 * the app context and returns a result string (errors are returned as values
 * with is_error set, not as control flow).
 */
#ifndef JC_TOOL_H
#define JC_TOOL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "cJSON.h"

struct jc_app;         /* jc_app.h  */
struct jc_permissions; /* jc_config.h */
struct jc_config;      /* jc_config.h */
struct jc_model_cfg;   /* jc_config.h */

struct jc_tool_result {
    char *content; /* malloc'd; caller frees */
    int   is_error;
    /* M168: for tools that RUN something (run_terminal_command, run_tests), the
     * command's own exit status; -1 when the tool does not run a command.
     *
     * `is_error` alone conflates two very different events: the tool failed
     * (command not found, timeout, fence denial, a refused permission) versus
     * the tool worked perfectly and the command it ran reported failure -- a red
     * build or a red test. In a fix-forward loop the agent runs a red gate on
     * purpose, so the second case is the agent doing its job. Measured on 30 MB
     * of real dogfood telemetry, 199 of 294 apparent shell/test "failures" were
     * red gates, which made `run_tests` look like a 73%-reliable tool when its
     * tool-level success rate was 97%. Telemetry needs the two apart, or a
     * healthy run reads as a broken tool. See docs/TELEMETRY.md. */
    int   exit_status;
    /* M291: this result is a POLICY refusal -- the path fence said no -- not a
     * malfunction and not a failure of capability. The distinction matters because
     * routing consumes it: escalating to a stronger model cannot help, since the
     * stronger model faces the identical policy. Observed live the first time the
     * fence was enabled on a real project: the very first `route` event that
     * project ever produced was `reason: tool_error` on a denied read.
     *
     * This is the same shape of defect as the one M286 fixed one milestone
     * earlier -- a single flag standing for two different things -- so the fix is
     * a field rather than a string match on the message. */
    int   policy_refusal;
};

/* True when this result represents the TOOL malfunctioning (command not found,
 * timeout, bad arguments, an unknown tool) as opposed to either of the two
 * things that are NOT malfunctions: a command the tool ran correctly reporting
 * failure (a red build or a red test -- M168/M286), or a POLICY refusal (the
 * path fence said no -- M291). Both of those are the system working as designed.
 *
 * M168 taught TELEMETRY that difference but left every other consumer reading
 * `is_error` alone, and routing was one of them: `escalateOnError` switched the
 * model on any red gate, which in a fix-forward loop is the agent doing its job.
 * Measured on one project's telemetry, 300 of 447 tool errors across 151 turns
 * were build/test failures, so the flag escalated on nearly every turn and had to
 * be left off -- which in turn meant the strong tier was never reached at all
 * (routes=0 over 174 turns).
 *
 * M291 then found the mirror-image error in that fix: a POLICY refusal is not a
 * capability failure either. The first `route` event a newly fenced project ever
 * produced was `reason: tool_error` on a denied read -- an escalation that could
 * not possibly help. Pure; unit-tested. */
int jc_tool_result_is_malfunction(const struct jc_tool_result *r);

/* Set an error result that is a POLICY refusal (see policy_refusal above): the
 * tool worked, the request was not allowed. Used by the path-fence checks. */
void tu_err_policy(struct jc_tool_result *out, const char *msg);

struct jc_tool {
    const char *name;
    const char *description;
    /* Returns a freshly allocated JSON-schema object describing parameters
     * ({"type":"object","properties":{...},"required":[...]}). Caller owns. */
    cJSON *(*schema)(void);
    int readonly;
    jc_status (*run)(const cJSON *args, struct jc_tool_result *out,
                     struct jc_app *app);
    /* Dynamic-tool support (MCP). Builtins leave these zero-initialised and
     * use the context-free schema()/run() above. A dynamic tool sets `ctx` to
     * its own state and provides schema_ctx()/run_ctx(), which take precedence
     * when non-NULL. This keeps instance data (a server connection, the remote
     * tool name, a cached input schema) out of the otherwise-static vtable. */
    void *ctx;
    cJSON *(*schema_ctx)(void *ctx);
    jc_status (*run_ctx)(void *ctx, const cJSON *args,
                         struct jc_tool_result *out, struct jc_app *app);
    /* M436: this tool acts on state that belongs to the MAIN agent (the user's
     * todo list, the project board), so a delegate must not touch it. Declared
     * here rather than checked inside each `run`, so the one fact is *advertised*
     * on (jc_tool_build_neutral_ex omits it at depth) and *enforced* on
     * (jc_tool_execute refuses it) from a single source -- the M296 shape.
     *
     * TRAILING ON PURPOSE: every other tool definition is a positional C89 brace
     * list ending `NULL, NULL, NULL`, and C89 zero-initialises members a partial
     * initialiser omits, so adding this at the end leaves 40-odd definitions
     * untouched and correct. */
    int main_agent_only;
};

struct jc_tool_registry {
    struct jc_vec tools; /* of const struct jc_tool* */
};

void jc_tool_registry_init(struct jc_tool_registry *r);
void jc_tool_registry_free(struct jc_tool_registry *r);
void jc_tool_registry_register(struct jc_tool_registry *r,
                               const struct jc_tool *t);
const struct jc_tool *jc_tool_registry_find(const struct jc_tool_registry *r,
                                            const char *name);

/* If `args` is a single-member object whose key is `tool_name` and whose value is
 * an object, DETACH and return that inner object (caller owns it, and should then
 * delete the outer `args`). Returns NULL when no unwrap applies, leaving `args`
 * untouched. Pure; unit-tested.
 *
 * M172: models sometimes nest a call's arguments under the tool's own name --
 * {"edit_file": {"path": ...}} instead of {"path": ...}. That is valid JSON, so
 * the M148 syntax repair never sees it; the tool just finds every required
 * argument missing. Requiring the key to equal the tool's own name makes the
 * unwrap unambiguous: no tool has a parameter named after itself. */
cJSON *jc_tool_unwrap_self_named(cJSON *args, const char *tool_name);

/* M193: coerce a stringified nested structure back to the shape the schema
 * declares. Returns the number of parameters substituted (0 = nothing to do).
 * Mutates `args` in place.
 *
 * A model sometimes serialises a nested array or object into a JSON *string*:
 *   {"todos": "[{\"content\": \"x\", \"status\": \"pending\"}]"}
 * instead of {"todos": [{"content": "x", ...}]}. Observed live: 28 of 36
 * todo_write calls in the zigodot log failed this way, every one with
 * `error: 'todos' must be an array`
 * (docs/analysis/2026-07-27-zigodot-telemetry.md).
 *
 * This is NOT jc_jsonrepair's remit (M148): that repairs syntactically broken
 * almost-JSON after a parse FAILURE, whereas this JSON parses cleanly and is
 * semantically the wrong shape. It is the sibling of jc_tool_unwrap_self_named
 * (M172), applied in the same slot in jc_tool_execute.
 *
 * Conservative by construction: it fires only where the tool's OWN schema
 * declares "type":"array" or "type":"object" for that parameter AND the received
 * value is a string that parses as exactly that type. A string parameter is never
 * touched, a plausible-looking string is never reinterpreted, and the tool's own
 * validation still runs afterwards on the substituted value. `schema` may be NULL
 * (nothing is coerced).
 *
 * Pure apart from the mutation; unit-tested in tests/test_tool.c. */
int jc_tool_unstring_args(cJSON *args, const cJSON *schema);

/* Run the read-only assignment-helper tutor sub-agent on `question`; *answer_out
 * is malloc'd (caller frees). JC_ERR_NOTFOUND when no helper can run. Used by
 * the ask_for_help tool and the TUI /tutor command (M173b). */
jc_status jc_tool_help_tutor(struct jc_app *app, const char *question,
                             char **answer_out);

/* Map a common but unregistered tool-name variant to its canonical registered
 * name (e.g. models often emit `todoadd`/`todo_write` for `todowrite`); returns
 * `name` unchanged when there is no alias. Consulted by jc_tool_registry_find so
 * a plausible guess resolves instead of failing as an unknown tool. Pure;
 * unit-tested. */
const char *jc_tool_canonical_name(const char *name);

/* M90: when a tool name is unknown (no exact match and no alias), return the
 * closest REGISTERED tool name by edit distance, but only when it is reasonably
 * close (within ~half the queried name's length, clamped to [2,4]) so a wild
 * guess like `glob` gets no misleading suggestion. Returns NULL when nothing is
 * close enough (or on NULL args). The unknown-tool error surfaces this as a
 * "did you mean 'X'?" hint. Pure; unit-tested. */
/* M147: scan assistant TEXT for a tool call written as prose instead of
 * invoked natively -- the classic small-model failure the loop previously
 * accepted as a final answer. High-precision patterns only: a fenced code
 * block containing a JSON object with a "name"/"tool" key; a line-anchored
 * bare JSON object with "name" + ("arguments"|"args"|"parameters"); or an
 * XML-ish <tool_call>/<function_call>/<invoke name="..."> tag. A hit REQUIRES
 * the extracted name to resolve in the live registry (exact or canonical
 * alias) -- natural-language intent ("I will now run X") is deliberately NOT
 * matched (precision too low; a model describing a PAST call would loop).
 * On a hit, copies the registry's resolved tool name into name_out (cap
 * bytes) and returns 1; else 0. Pure; unit-tested. */
int jc_toolcall_scan(const char *text, const struct jc_tool_registry *r,
                     char *name_out, jc_size cap);

const char *jc_tool_suggest_name(const struct jc_tool_registry *r,
                                 const char *unknown);

/* M91: map a common EXTERNAL tool name (from another agent's toolset -- e.g.
 * `grep`/`rg`, `glob`/`fd`, `bash`, `cat`, `web_fetch`) to the jichi tool that
 * serves the same intent (`search_code`, `list_files`, `run_terminal_command`,
 * `read_file`, `fetch_url`). Returns NULL when there is no known synonym. This is
 * a SUGGESTION source only (the argument schemas differ, so it must never be a
 * silent alias like jc_tool_canonical_name); jc_tool_suggest_name uses it as a
 * fallback when edit distance finds nothing, and only when the mapped tool is
 * actually registered. Pure; unit-tested. */
const char *jc_tool_semantic_alias(const char *name);

/* Register the built-in tools (read/write/edit/ls/run/search). */
void jc_tool_register_builtins(struct jc_tool_registry *r);

/* Register every CONDITIONAL built-in whose gate is a config check (or one cheap
 * probe): background, web search, sound, assignment support, board, docs search,
 * media/transcribe roles, load_skill, the LSP tools, and the git tools.
 *
 * M325b: this exists so the reporting surfaces advertise the same set a real turn
 * does. `context tools` and `doctor` run before main()'s registration and used to
 * build only jc_tool_register_builtins -- 16 tools where a live session had 18+ --
 * so the report under-stated the very cost it exists to measure, and every added
 * conditional tool widened the gap silently.
 *
 * NOT registered here: MCP tools, because discovering them means connecting to
 * every configured server, and a read-only report must not. The caller states that
 * exclusion (M314's rule: an absence is said, not implied). Skills must already be
 * loaded for load_skill to appear -- `context`/`sysmsg` do that via
 * load_prompt_assets; main() loads them on its own path.
 *
 * Registers tools only; it does not initialise the background manager or load the
 * board, which are the caller's lifetime concerns. Order matches main()'s so the
 * advertised array is byte-identical. */
void jc_tool_register_configured(struct jc_tool_registry *r,
                                 struct jc_app *app);

/* Build the neutral tools array for a provider. If `include_mutating` is 0,
 * tools with readonly==0 are omitted. If `perm` is non-NULL, tools on its deny
 * list are also omitted (so denied tools are never advertised to the model).
 * Returns a new cJSON array (caller owns) or NULL if nothing qualifies. */
cJSON *jc_tool_build_neutral(const struct jc_tool_registry *r,
                             int include_mutating,
                             const struct jc_permissions *perm);

/* Like jc_tool_build_neutral, but also omits the tools named `exclude_name`
 * and `exclude_name2` (either NULL => exclude nothing; used to hide
 * spawn_subagent and spawn_parallel from a subagent) and, when `allow` is
 * non-NULL, omits any tool not in that allow-list (a jc_vec of char* tool
 * names) -- except `load_skill`, which is always kept so the agent can switch
 * skills. Used to enforce a skill's allowed-tools fence. */
cJSON *jc_tool_build_neutral_ex(const struct jc_tool_registry *r,
                                int include_mutating,
                                const struct jc_permissions *perm,
                                const char *exclude_name,
                                const char *exclude_name2,
                                const struct jc_vec *allow,
                                int agent_depth);

/* Is tool `name` permitted by the allow-list `allow` (a jc_vec of char* tool
 * names)? A NULL/empty allow-list permits everything; otherwise only listed
 * names are permitted, plus `load_skill` (always exempt, so the agent can still
 * switch skills). Pure; the single source of truth for both the skill fence and
 * the per-agent-profile tool fence. Unit-tested. */
int jc_tool_allowed(const struct jc_vec *allow, const char *name);

/* M360: render a fence refusal that names the way forward -- the bounded
 * list of tools that ARE available -- instead of only the cause. The M342
 * lesson: a cause with no way forward amplifies a retry loop; the list ends
 * it. NULL/empty allow falls back to "use only the advertised tools". */
struct jc_sb;
void jc_tool_refusal_render(const struct jc_vec *allow, const char *name,
                            struct jc_sb *out);

/* Intersect two allow-lists into `out` (a freshly jc_vec_init'd vec of char*,
 * whose pushed pointers are copied onto `a`). Used when both an agent profile and
 * a restrict-tools skill fence a subagent: the effective fence is the tools common
 * to both. A NULL/empty list means "no fence" (permits everything), so it acts as
 * the identity: intersecting with it yields a copy of the other. When both are
 * non-empty, `out` holds each name from `a_list` also present in `b_list`. Pure;
 * unit-tested. Returns the number of names pushed. */
int jc_tool_allow_intersect(const struct jc_vec *a_list,
                            const struct jc_vec *b_list,
                            struct jc_arena *a, struct jc_vec *out);

/* True when `name` is in the lean "core" tool set (the minimal read/edit/search/
 * run loop) advertised to small-context models. Pure; unit-tested (M74). */
int jc_tool_is_core(const char *name);

/* True when `name` is a built-in tool name jichi can register (M285). Because
 * registration is conditional (git_* need a repo, the LSP tools need lspServers,
 * media tools need a role, ...), the live registry is a SUBSET of this set -- so
 * this answers "is that a real tool name at all?", not "is it available here".
 * That is the question an agent profile's or skill's `tools:` fence needs: a name
 * that is not a real tool silently narrows the fence, and the model can never
 * call it. Does NOT cover config-declared user tools (their names come from
 * `tools[]`) or MCP tools (namespaced `<server>__<tool>`); a caller linting a
 * fence must check those separately. Pure; unit-tested, and the backing table is
 * kept honest by tests/smoke/tool_names_lint.sh. */
int jc_tool_name_known(const char *name);

/* Enumerate the built-in tool names behind jc_tool_name_known (for tests and the
 * lint). `jc_tool_name_at` returns NULL out of range. Pure. */
int jc_tool_name_count(void);
const char *jc_tool_name_at(int i);

/* Fill `out` (a freshly jc_vec_init'd vec of `char *`, elem_size sizeof(char *))
 * with the core tool names, for use as a run's `allow` list. The pushed pointers
 * are static string literals (program-lifetime); jc_vec_free(out) frees only the
 * backing array, never the strings. M74. */
void jc_tool_core_allow(struct jc_vec *out);

/* Execute a tool call by name. `arguments_json` is parsed as the args object.
 * Always fills *out (with an error result if the tool is unknown or the args
 * are malformed). */
jc_status jc_tool_execute(const struct jc_tool_registry *r,
                          const char *name, const char *arguments_json,
                          struct jc_tool_result *out, struct jc_app *app);

void jc_tool_result_free(struct jc_tool_result *res);

/* Accessors for the built-in tool definitions (used by the registry and by
 * tests). */
const struct jc_tool *jc_tool_read(void);
const struct jc_tool *jc_tool_write(void);
const struct jc_tool *jc_tool_edit(void);
const struct jc_tool *jc_tool_apply_patch(void);
const struct jc_tool *jc_tool_ls(void);
const struct jc_tool *jc_tool_run(void);
const struct jc_tool *jc_tool_run_tests(void);
const struct jc_tool *jc_tool_search(void);
const struct jc_tool *jc_tool_fetch(void);
const struct jc_tool *jc_tool_codebase_search(void);
const struct jc_tool *jc_tool_subagent(void);
const struct jc_tool *jc_tool_parallel(void);
const struct jc_tool *jc_tool_find_definition(void);
const struct jc_tool *jc_tool_find_references(void);
const struct jc_tool *jc_tool_list_symbols(void);
const struct jc_tool *jc_tool_rename_symbol(void); /* M40; mutating */
const struct jc_tool *jc_tool_format_file(void);   /* M40; mutating */
const struct jc_tool *jc_tool_list_code_actions(void); /* M44; read-only */
const struct jc_tool *jc_tool_apply_code_action(void); /* M44; mutating */
/* M43; read-only. Dynamic (ctx=app): its schema enumerates discovered MCP
 * resources, so it needs the app at registration. Allocated from app->arena. */
const struct jc_tool *jc_tool_read_mcp_resource(struct jc_app *app);
const struct jc_tool *jc_tool_git_status(void);
const struct jc_tool *jc_tool_git_diff(void);
const struct jc_tool *jc_tool_git_log(void);
const struct jc_tool *jc_tool_git_blame(void);
const struct jc_tool *jc_tool_git_add(void);    /* M39; mutating */
const struct jc_tool *jc_tool_git_commit(void); /* M39; mutating */
const struct jc_tool *jc_tool_git_branch(void); /* M39; mutating */
const struct jc_tool *jc_tool_git_stash(void);  /* M39; mutating */

/* True when `cwd` is inside a git work tree (used to gate git-tool
 * registration). */
int jc_tool_git_available(const char *cwd);

/* Pure helpers (unit-tested): clamp a requested git_log count to [1,100]
 * (<=0 => 20); build a `git blame -L` range value into buf, returning 1 when a
 * range applies (start>0) and 0 otherwise. */
int jc_git_clamp_max(int requested);
int jc_git_blame_range(int start, int end, char *buf, int cap);
const struct jc_tool *jc_tool_todowrite(void);
const struct jc_tool *jc_tool_todoread(void);
const struct jc_tool *jc_tool_board(void); /* #7; mutating (writes .jichi/board.json) */
const struct jc_tool *jc_tool_skill(void);
const struct jc_tool *jc_tool_remember(void);
const struct jc_tool *jc_tool_read_background(void);
const struct jc_tool *jc_tool_kill_background(void);
const struct jc_tool *jc_tool_web_search(void);
const struct jc_tool *jc_tool_ask_user(void);    /* M34d/F4; uses app->ask delegate */
const struct jc_tool *jc_tool_hint(void);        /* learner-support: assignment hint ladder */
const struct jc_tool *jc_tool_ask_for_help(void);/* learner-support: clarify (human) or helper agent */
const struct jc_tool *jc_tool_search_docs(void); /* M34a; needs role "embed" + docs */
/* M32; needs role "image". A dynamic (ctx=app) tool: its schema enumerates the
 * configured image models for per-workflow selection, so it needs the app at
 * registration time. Allocated from app->arena (freed with the session). */
const struct jc_tool *jc_tool_generate_image(struct jc_app *app);
const struct jc_tool *jc_tool_generate_audio(void); /* M32; needs role "audio" */
const struct jc_tool *jc_tool_transcribe_audio(void); /* M33; role "transcribe" */

/* Pure (unit-tested): render up to `max` search results from a backend JSON
 * response into `out` (accepts results|data|web_results arrays; per item
 * title|name, url|link, content|snippet|description|text). Returns the count
 * rendered, or -1 if the JSON did not parse / had no results array. */
struct jc_sb;
int jc_websearch_format(const char *json, int max, struct jc_sb *out);

/* Resolve a subagent model selector (a name/id substring, 1-based index, or a
 * role name) to a stable model-config pointer. Returns the active model when
 * `selector` is NULL/empty (with *found=1). On a non-empty selector that
 * matches nothing, returns NULL with *found=0. Exposed for testing. */
struct jc_model_cfg *jc_subagent_resolve_model(struct jc_config *cfg,
                                               const char *selector,
                                               int *found);

#ifdef __cplusplus
}
#endif
#endif /* JC_TOOL_H */
