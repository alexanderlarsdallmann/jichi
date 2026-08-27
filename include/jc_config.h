/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_config.h - JSON configuration loading.
 *
 * Replaces the original CLI's YAML/hub-driven config with a flat JSON file.
 * Resolution order for the path:
 *   1. explicit path argument (from --config)
 *   2. $JC_CONFIG
 *   3. ~/.jichi
 * If no file is found, a built-in default is returned (provider from env).
 *
 * Example config:
 *   {
 *     "model": {
 *       "provider": "anthropic",
 *       "model": "claude-opus-4-8",
 *       "apiKeyEnv": "ANTHROPIC_API_KEY",
 *       "maxTokens": 4096,
 *       "temperature": 0.0,
 *       "contextLength": 200000
 *     },
 *     "maxToolIters": 25,
 *     "autoCompact": true,
 *     "contextLimit": 0,
 *     "snapshots": true,
 *     "snapshotLimit": 100,
 *     "maxSubagentDepth": 1,
 *     "mode": "chat",
 *     "instructions": ["docs/conventions.md"],
 *     "permissions": { "allow": ["edit_file"], "deny": ["run_terminal_command"] },
 *     "mcpServers": [
 *       { "name": "fs", "command": "npx",
 *         "args": ["-y", "@modelcontextprotocol/server-filesystem", "."],
 *         "env": { "FOO": "bar" },
 *         "autoApprove": ["read_file"], "deny": ["delete_file"] },
 *       { "name": "remote", "type": "http",
 *         "url": "https://example.com/mcp",
 *         "headers": ["Authorization: Bearer TOKEN"],
 *         "autoApprove": "*" }
 *     ],
 *     "lspServers": [
 *       { "name": "clangd", "command": "clangd", "extensions": ["c", "h"] }
 *     ]
 *   }
 *
 * Each mcpServers entry connects to an MCP server and registers its tools as
 * "<name>__<tool>". Transport is "stdio" (spawn command/args, with optional
 * env) or "http" (POST to url, with optional headers); when "type" is omitted
 * it is inferred (a url => http, else stdio). "autoApprove"/"deny" set the
 * per-tool approval policy: each is a list of (un-namespaced) tool names, or
 * "*"/true for every tool. autoApprove'd tools run without a permission
 * prompt; deny'd tools are not advertised to the model and refused if named
 * anyway (deny wins); anything else is asked.
 *
 * "mode" is the startup operating mode ("chat" | "plan" | "auto"; default
 * "chat"). Top-level "permissions" (allow/deny lists of registered tool names,
 * same shape as a server's autoApprove/deny) apply to every tool. Together they
 * drive the per-tool verdict; see docs/AGENT_MODES.md.
 *
 * "maxSubagentDepth" (default 1) caps spawn_subagent nesting; "maxSubagentIters"
 * (default = maxToolIters) caps a subagent run's tool iterations. See
 * docs/SUBAGENTS.md. "instructions" lists extra rules-file paths loaded into the
 * system prompt alongside AGENTS.md (see docs/RULES.md). "lspServers" configures
 * language servers (command/args/extensions) whose diagnostics are surfaced
 * after edits and via the `lsp` subcommand (see docs/LSP.md). "autoCompact"
 * (default true) summarizes the older history once it approaches the context
 * budget; "contextLimit" overrides that budget in tokens (0 => use the active
 * model's "contextLength", else a built-in default). See docs/COMPACTION.md.
 * "snapshots" (default true) checkpoints the workspace before the agent edits
 * files so the TUI `/undo` can revert a turn's changes; "snapshotLimit"
 * (default 100, 0 => unlimited) bounds how many checkpoints the shadow repo
 * retains. See docs/SNAPSHOTS.md.
 */
#ifndef JC_CONFIG_H
#define JC_CONFIG_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

/* Model-call timeouts (M22), all in seconds. The sentinel -1 means "unset"
 * (inherit the next tier); 0 means explicitly disabled (no bound). `connect`
 * maps to the TCP connect timeout, `stall` to the low-speed/frozen-stream
 * timeout (the key control for a hung model), `request` to a hard overall cap
 * (off by default). Resolved by precedence CLI > per-model > global > built-in
 * via jc_config_resolve_timeouts. See docs/MODELS.md and ROADMAP M22. */
struct jc_timeouts_cfg {
    long connect;
    long stall;
    long request;
};

/* Role flags (a bitmask in jc_model_cfg.roles). A model may serve several
 * roles. Unknown role strings in the config are ignored. */
#define JC_ROLE_CHAT         0x01u
#define JC_ROLE_EDIT         0x02u
#define JC_ROLE_AUTOCOMPLETE 0x04u
#define JC_ROLE_EMBED        0x08u
#define JC_ROLE_RERANK       0x10u
#define JC_ROLE_SUMMARIZE    0x20u
#define JC_ROLE_APPLY        0x40u
#define JC_ROLE_IMAGE        0x80u  /* generate_image backend (M32) */
#define JC_ROLE_AUDIO        0x100u /* generate_audio (TTS) backend (M32) */
#define JC_ROLE_TRANSCRIBE   0x200u /* transcribe_audio (STT) backend (M33) */

struct jc_model_cfg {
    char    *name;        /* display name (optional)                 */
    char    *description; /* free-text workflow hint (config "description",
                           * optional); surfaced in the generate_image tool menu
                           * + /status so users/agents pick a model per workflow */
    char    *provider;    /* "anthropic" | "openai"                  */
    char    *model;       /* API model id                            */
    char    *api_base;    /* base URL; NULL => provider default      */
    char    *api_key;     /* resolved key (literal or from apiKeyEnv) */
    char    *api_key_env; /* configured apiKeyEnv NAME (not value), or NULL.
                            * Registered for child-env scrubbing (M130). */
    int      api_key_literal; /* 1 => the key came from a literal "apiKey" field
                            * in the config (a security smell; doctor warns, M55) */
    int      model_defaulted; /* M505: 1 => the config named NO model id and one
                            * was substituted by default_model(provider).
                            *
                            * Found by reviewing this project's own new pages:
                            * `{"models":[{"name":"a"}]}` reported
                            * `config validate: OK` and doctor's `configuration
                            * loaded -- active: a (claude-opus-4-8)` as a GREEN
                            * line, indistinguishable from a config that named
                            * that model. The substitution reaches for a PRICED
                            * frontier id, which is exactly the hazard
                            * ANECDOTES #63 is about, so the value being right
                            * matters less than its provenance being visible --
                            * the same argument M503's verify_source makes. */
    double   temperature; /* < 0 => omit from request                */
    long     max_tokens;  /* <= 0 => provider default                */
    double   input_cost;  /* USD per 1M input tokens; 0 => unknown    */
    double   output_cost; /* USD per 1M output tokens; 0 => unknown   */
    double   cache_read_cost;  /* USD/1M cached-read input tokens; 0 => input_cost */
    double   cache_write_cost; /* USD/1M cache-write input tokens; 0 => input_cost */
    long     context_limit; /* context window in tokens; 0 => unknown */
    unsigned roles;       /* OR of JC_ROLE_* flags; 0 => unspecified  */
    int      vision;      /* model accepts image input (config "vision", M29) */
    int      tool_calling;/* config "toolCalling" (M149): 0 = "native"
                           * (default), 1 = "none" (tools are not advertised;
                           * degraded Q&A/plan agent). "text" is a RESERVED
                           * value for a future prompt-based fallback --
                           * parsed as native with a warning today.           */
    int      prompt_cache; /* EFFECTIVE prompt caching (M31b): emit Anthropic
                            * cache_control breakpoints + OpenAI prompt_cache_key.
                            * Resolved by jc_config_resolve_prompt_cache from the
                            * per-model key (prompt_cache_cfg) over the global
                            * default. Inert for OpenAI (caching is automatic). */
    int      prompt_cache_cfg; /* raw per-model "promptCache" key: -1 unset (M31d) */
    int      prompt_cache_1h;  /* EFFECTIVE 1-hour cache TTL (M31e): 0 => 5-min
                                * default, 1 => emit ttl:"1h" on breakpoints.
                                * Resolved from the global promptCacheTtl. */
    char    *fallback;    /* selector of a model to use if this one's
                           * server is unreachable; NULL => none       */
    struct jc_timeouts_cfg timeouts; /* per-model overrides (-1 => unset) */
};

/* One configured MCP server. A server is reached either over stdio (spawn
 * `command` with `args`/`env`) or over HTTP (`url` with optional `headers`).
 * `type` is "stdio" or "http"; when absent it is inferred (a `url` => http,
 * else stdio). All strings/elements are arena-owned; the jc_vec handles are
 * heap-owned and released by jc_config_free. Vector elements are char*. */
struct jc_mcp_server_cfg {
    char *name;            /* logical name; used to namespace its tools     */
    char *type;            /* "stdio" | "http"                              */
    char *command;         /* stdio: executable to spawn                    */
    struct jc_vec args;    /* stdio: argv after command (of char*)          */
    struct jc_vec env;     /* stdio: "KEY=VALUE" entries (of char*)         */
    char *url;             /* http: endpoint URL                            */
    struct jc_vec headers; /* http: "Header: Value" lines (of char*)        */
    /* Per-tool approval policy. "autoApprove"/"deny" are each either a string
     * array of (un-namespaced) tool names or "*"/true to mean every tool;
     * deny wins over autoApprove, and anything in neither is left to ask. */
    struct jc_vec auto_approve; /* of char*: tool names to run without asking */
    int           auto_approve_all;
    struct jc_vec deny;         /* of char*: tool names to always refuse      */
    int           deny_all;
    int           kinetic;      /* M163a: all of this server's tools are
                                 * kinetic (physical actuation)               */
};

/* One configured Language Server (config "lspServers"). The server is spawned
 * (command + args) and matched to files by extension; its diagnostics are
 * surfaced after edits and via the `lsp` subcommand. Strings/elements are
 * arena-owned; the jc_vec handles are heap-owned (freed by jc_config_free). */
struct jc_lsp_server_cfg {
    char *name;                /* logical name (for messages)               */
    char *command;             /* executable to spawn                       */
    struct jc_vec args;        /* argv after command (of char*)             */
    struct jc_vec extensions;  /* file extensions without dot (of char*)    */
};

/* One user-defined tool (config "tools"): a name + JSON Schema mapped to a local
 * command, registered as a dynamic tool. The model's arguments are delivered to
 * the command on stdin (as JSON) and as JICHI_ARG_<NAME> env vars — never on the
 * command line. Strings/elements are arena-owned; the jc_vec handles are
 * heap-owned (freed by jc_config_free). */
struct jc_user_tool_cfg {
    char *name;          /* tool name (required; must not shadow a builtin)  */
    char *description;   /* shown to the model                               */
    char *schema_json;   /* the "schema" object, re-serialised, or NULL      */
    char *command;       /* executable for the argv form, or NULL            */
    struct jc_vec args;  /* of char*: argv after command                     */
    char *shell;         /* "/bin/sh -c <shell>" form, or NULL               */
    struct jc_vec env;   /* of char*: "KEY=VALUE"                            */
    long  timeout;       /* seconds; <=0 => default                          */
    int   readonly;      /* usable in plan/read-only; ALLOW baseline         */
    int   kinetic;       /* M163a: moves mass/energy -> kinetic gate; forces
                          * readonly=0 (a kinetic tool is never a safe read)  */
};

/* Lifecycle hooks (config "hooks", M25). Each event holds a list of matchers;
 * each matcher (a tool-name glob with '|' alternation; empty/absent => match
 * all) holds a list of commands run when the event fires. A command mirrors a
 * user tool's argv/shell form. Strings are arena-owned; jc_vec handles are
 * heap-owned (freed by jc_config_free). See docs/HOOKS.md. */
enum jc_hook_event {
    JC_HOOK_PRE_TOOL = 0,   /* before a tool runs; may block it          */
    JC_HOOK_POST_TOOL,      /* after a tool runs; observe / add context  */
    JC_HOOK_USER_PROMPT,    /* on user prompt submit; may block / augment*/
    JC_HOOK_STOP,           /* a top-level turn finished                 */
    JC_HOOK_SESSION_START,  /* once at startup                           */
    JC_HOOK_EVENT_COUNT
};

struct jc_hook_cmd_cfg {
    char *command;       /* executable for the argv form, or NULL            */
    struct jc_vec args;  /* of char*: argv after command                     */
    char *shell;         /* "/bin/sh -c <shell>" form, or NULL               */
    long  timeout;       /* seconds; <=0 => default                          */
};

struct jc_hook_matcher_cfg {
    char *matcher;          /* tool-name glob ('|'-alternated); NULL => all  */
    struct jc_vec commands; /* of struct jc_hook_cmd_cfg                      */
};

/* One external documentation source (config "docs", M34a). `path` is a local
 * directory of docs (markdown/text) indexed by the embeddings stack for the
 * search_docs tool / @docs:<name> reference. Strings are arena-owned; the vec
 * is heap-owned (freed by jc_config_free). */
struct jc_docs_cfg {
    char *name;   /* selector used by search_docs / @docs:<name>        */
    char *path;   /* local directory to index (NULL if a url source)    */
    char *url;    /* http(s) page to fetch + index (NULL if a dir; M51) */
    int   feed;   /* url is an RSS/Atom feed: reduce with jc_rss (W4)    */
};

/* A named reference alias (#6), expandable as @ref:<name>. `type` selects how
 * `value` resolves: "file"/"dir" (a path), "url" (fetched), "ssh" (a
 * user@host[:path] connection string, injected as text), "text" (a literal
 * snippet), or "key"/"token" (a SECRET -- `value` is an env-var name unless
 * `is_cmd`, then a shell command; never inlined into prompt context, only a
 * redacted presence note, and registered with jc_redact). */
struct jc_alias_cfg {
    char *name;
    char *type;
    char *value;
    int   is_secret; /* type is key/token */
    int   is_cmd;    /* secret: `value` is a command to run (else an env name) */
};

struct jc_hooks_cfg {
    struct jc_vec events[JC_HOOK_EVENT_COUNT]; /* each: jc_hook_matcher_cfg   */
};

/* Web-search backend (config "search", M27). The web_search tool POSTs
 * {"query","max_results"} (and "api_key" when set) to `url` with a Bearer header
 * and renders the returned results. Registered only when `url` is set. Strings
 * are arena-owned. See docs/WEBSEARCH.md. */
struct jc_search_cfg {
    char *url;          /* full search endpoint URL (NULL => tool disabled) */
    char *api_key;      /* resolved key (literal apiKey or apiKeyEnv), or NULL */
    char *api_key_env;  /* configured apiKeyEnv NAME (not value), or NULL     */
    char *provider;     /* informational (e.g. "tavily"); may be NULL        */
    long  max_results;  /* result cap; <=0 => built-in default               */
};

/* Sound I/O (config "sound", M163b). Each half mirrors a hook command's
 * argv/shell form; the tools (play_audio/record_audio) register only when the
 * matching command is set. jichi shells OUT (aplay/arecord/ffplay/...), never
 * links an audio library -- the M42 external-extractor pattern. */
struct jc_sound_cfg {
    char *play_command;      /* argv form: executable, else NULL             */
    struct jc_vec play_args; /* of char*: argv after play_command            */
    char *play_shell;        /* "/bin/sh -c <shell>" form, else NULL         */
    char *record_command;
    struct jc_vec record_args;
    char *record_shell;
    long  play_timeout;      /* seconds; <=0 => default 120                   */
    long  record_max;        /* max record seconds; <=0 => default 60, cap 600*/
};

/* Top-level per-tool permission policy (config "permissions"). Same shape as a
 * server's autoApprove/deny but applied to every tool by registered name (a
 * built-in's short name or an MCP "<server>__<tool>" name). deny wins. */
struct jc_permissions {
    struct jc_vec allow; /* of char*: tool names to run without asking */
    int           allow_all;
    struct jc_vec deny;  /* of char*: tool names to always refuse      */
    int           deny_all;
};

/* Tiered model routing: run on `fast` and escalate to `strong` on a hard
 * signal. Selectors are model name/index/role strings (arena-owned). Acts only
 * when enabled and both resolve to distinct models. See docs/ROUTING.md. */
/* Default context-pressure escalation threshold, as a percentage of the fast
 * tier's effective window. Deliberately below the compaction trigger's 80%, so a
 * turn that is running out of room moves to a wider model BEFORE history starts
 * being summarized or elided away (M288). */
#define JC_ROUTE_CONTEXT_PCT 75

/* Hysteresis for coming back DOWN (M298). Escalation-on-context is triggered by
 * room, and mid-turn compaction (M76) can give that room back -- but returning to
 * the fast tier the moment the estimate dips below the escalation point would
 * oscillate escalate -> compact -> de-escalate -> escalate every few tool calls,
 * and each switch rebuilds the provider and discards the cached prompt prefix
 * (M31). So de-escalation needs a gap, not a threshold: it fires only below
 * (escalate_pct - this), i.e. 55% by default against a 75% escalation point. */
#define JC_ROUTE_DEESCALATE_GAP 20

struct jc_routing_cfg {
    int   enabled;            /* default 1 (inert unless fast+strong resolve) */
    char *fast;               /* selector for routine turns, or NULL          */
    char *strong;             /* selector to escalate to, or NULL             */
    int   escalate_on_verify; /* escalate after a verify failure (default 1)  */
    int   escalate_on_error;  /* escalate after a tool error (default 0)      */
    int   escalate_on_stall;  /* escalate after a model stall (default 1)     */
    /* M288: escalate when the history is about to outgrow the FAST tier's
     * window, expressed as a percentage of it (0 = off, default 75).
     *
     * The other three triggers all react to something going wrong. This one
     * reacts to running out of room, which is the reason a wide-window strong
     * tier gets configured in the first place -- and it is the trigger that was
     * missing: on one measured project `routes=0` across 174 turns, because
     * verify never failed, nothing stalled, and escalateOnError had to be off.
     * Set below the compaction trigger's 80% so a roomier model is preferred
     * over discarding history. Inert unless the strong tier's effective window
     * is strictly LARGER -- escalating for room you do not gain is pure cost. */
    int   escalate_on_context;
};

/* Retrieval tuning (config "retrieval", M60): hybrid lexical+dense fusion and
 * optional query rewriting, shared by codebase + docs search via jc_retrieve. */
enum jc_query_rewrite { JC_QR_OFF = 0, JC_QR_HYDE = 1, JC_QR_MULTIQUERY = 2 };

struct jc_retrieval_cfg {
    int hybrid;        /* fuse BM25-lite with dense: -1 auto (=on) /0 off /1 on */
    int query_rewrite; /* enum jc_query_rewrite (default JC_QR_OFF)             */
    int rrf_k;         /* Reciprocal Rank Fusion constant; 0 => built-in 60     */
};

/* Source set for automatic context injection (config "autoContextSources"). */
enum jc_autoctx_src { JC_ACTX_BOTH = 0, JC_ACTX_CODEBASE = 1, JC_ACTX_DOCS = 2 };

/* Model-issued privileged-command policy (M153, config `privilegedCommands`). */
enum jc_priv_policy { JC_PRIVPOL_ASK = 0, JC_PRIVPOL_DENY = 1,
                      JC_PRIVPOL_ALLOW = 2 };

struct jc_config {
    struct jc_vec models;      /* of struct jc_model_cfg: all configured      */
    struct jc_model_cfg model; /* the active model (a copy of models[active]) */
    int   active;              /* index of the active model                   */
    int   from_defaults;       /* 1 => no config file found; built-in defaults */
    char  config_sources[512]; /* human summary of file(s) loaded (for doctor) */
    char *system_prompt_extra; /* appended to the system message; may be NULL */
    int   max_tool_iters;      /* agent-loop safety cap (def 25; lite 12)    */
    int   max_retries;         /* transient-HTTP retries (def 4; lite 2)     */
    int   max_subagent_depth;  /* spawn_subagent nesting (def 2; lite 0)     */
    int   max_subagent_iters;  /* tool-iteration cap for a subagent run       */
    struct jc_vec mcp_servers; /* of struct jc_mcp_server_cfg                  */
    struct jc_vec lsp_servers; /* of struct jc_lsp_server_cfg                  */
    struct jc_vec user_tools;  /* of struct jc_user_tool_cfg (config "tools")  */
    struct jc_hooks_cfg hooks; /* lifecycle hooks (config "hooks", M25)        */
    int   hooks_enabled;       /* config "hooksEnabled" (default 0; --no-hooks)*/
    int   config_editable;     /* config "configEditable" (default 0; M112):    */
                               /*   allow mutating config from the TUI/CLI      */
    long  mem_budget_mb;       /* config "memBudgetMb" (default 0=off; M117):    */
                               /*   RSS ceiling for build/test subprocesses     */
    long  run_timeout;         /* config "runTimeout" seconds (default 0=off):   */
                               /*   wall-clock cap on a model-issued command     */
    long  run_timeout_cli;     /* --run-timeout seconds (0=unset; wins over cfg) */
    int   accessible;          /* config "accessible" (default 0; M118):        */
                               /*   reduce motion (no spinner) + screen-reader   */
                               /*   friendly, linear TUI output                  */
    struct jc_search_cfg search; /* web_search backend (config "search", M27)  */
    struct jc_sound_cfg sound;   /* play_audio/record_audio backends (M163b)   */
    char *output_style;          /* active output-style name (config, M28)     */
    char *language;              /* answer language (config "language", M135);
                                  * free-form ("Japanese", "Deutsch", "zh");
                                  * NULL => the model's default              */
    struct jc_permissions permissions; /* top-level allow/deny tool lists      */
    int   default_mode;        /* startup mode (enum jc_agent_mode value)      */
    struct jc_vec instructions; /* of char*: extra instruction-file paths      */
    int   auto_compact;        /* summarize old history when it grows large    */
    long  context_limit;       /* compaction budget (def 0 = per-model; lite
                                16384)                                  */
    int   snapshots;           /* checkpoints + /undo (def 1; lite 0 -- so a
                                low-RAM box has NO undo unless set)      */
    int   snapshot_limit;      /* checkpoints to retain; 0 => unlimited        */
    char *verify;              /* autonomy: verifier command; NULL => none     */
    char *verify_kind;         /* M343 "verifyKind": "invariant"|"goal" as the
                                * operator wrote it; parsed at envelope setup
                                * (jc_env_verify_kind_parse), unknown => warn +
                                * ignore. NULL => undeclared (pre-M343 rules). */
    char *test_command;        /* test runner for run_tests/`test`; NULL=>none */
    char *pdf_command;         /* PDF text extractor (M42); NULL => "pdftotext" */
    char *format_command;      /* formatter shell command for format_file when
                                * no language server formats the file (M263);
                                * NULL => LSP only. See docs/FORMATTING.md   */
    char *time_format;         /* strftime pattern for the TUI timestamp; NULL=>"%X" */
    char  group_sep;           /* thousands separator for token display; 0 => none  */
    char *notify;              /* command run when a turn/run finishes; NULL=off */
    int   notify_bell;         /* emit a terminal bell on completion (def 0)    */
    struct jc_vec edit_scope;  /* autonomy: of char*: edit-scope path globs    */
    struct jc_vec ignore_dirs; /* of char*: EXTRA directory NAMES the repo map
                                * and the index never descend into, on top of
                                * the built-ins (node_modules, target, build,
                                * dist, __pycache__, any dotdir). M520: measured
                                * on a project whose Python virtualenv was named
                                * `advenv` -- not a dotdir, so both walkers went
                                * in, and the VISIBLE repo map was 87 lines of
                                * pip internals with the project's own Zig
                                * sources below the truncation line. .gitignore
                                * is not consulted (it lists build products, not
                                * "uninteresting to read"), so the operator says
                                * so here. Matched by name at any depth, like
                                * the built-ins.                              */
    int   preserve_discarded;  /* M336/M338 TRI-STATE: -1 unset, 0 off, 1 on.
                                * Unset is not "off": an interactive undo/rewind
                                * /revert preserves (irreversible, user present,
                                * healthy tree) while the envelope's rollback does
                                * not (it writes to disk while a run is already
                                * failing -- its failure mode overlaps its
                                * trigger). One key, two defaults, because the
                                * risk differs; two keys could contradict.
                                * Measured: 5 KB per preserved state once packed,
                                * bounded by `checkpoints gc`'s retention. */
    int   strict_green;        /* M332: refuse a passing verify when the run
                                * changed a file outside the edit scope. Off by
                                * default -- it turns a zero exit code non-zero,
                                * which is a documented stable interface. See
                                * docs/GATE_INTEGRITY.md. */
    /* M431f: the ambient budget panel. OFF by default -- it is a flag so that
     * M347's "no per-round nag" decision is MEASURED rather than overruled. The
     * cadence is every Nth tool call plus each quintile of the token budget. */
    int   budget_panel;        /* config "budgetPanel" / --budget-panel        */
    int   budget_panel_every;  /* cadence in tool calls (default 5)            */
    int   revert_out_of_scope; /* auto-revert out-of-scope changes at turn end
                                * (M142; needs editScope + snapshots; def 0)  */
    struct jc_vec design_docs; /* of char*: `design` -- authoritative spec docs
                                * injected into the system prompt (M462). A LIST
                                * because a project pins a standing architecture
                                * doc while a task adds its own spec; v1's single
                                * --design forced a choice between them. */
    struct jc_vec reference_roots; /* of char*: read-allowed roots outside the
                                * workspace (M54); writes stay workspace-only   */
    int   privileged_commands; /* M153: JC_PRIVPOL_* -- model-issued sudo/doas/
                                * pkexec/su policy (ask default / deny / allow) */
    struct jc_vec privileged_allow; /* of char*: operator prefix-allowlist of
                                * exact privileged commands pre-approved (M153) */
    int   privileged_audit;    /* M154: 1 = on (default), 0 = off (doctor warns);
                                * always-on audit of privileged commands        */
    int   kinetic_commands;    /* M163a: JC_PRIVPOL_* for kinetic (physical)
                                * actions (ask default / deny / allow)          */
    struct jc_vec kinetic_allow; /* of char*: tool names + command prefixes
                                * pre-approved (e.g. a safe-state stop_all)     */
    struct jc_vec kinetic_prefixes; /* of char*: extra shell command prefixes
                                * treated as kinetic (hardware outside a tool)  */
    int   kinetic_audit;       /* M163a: 1 = on (default), 0 = off; always-on
                                * audit of kinetic actions                      */
    int   max_parallel_agents; /* spawn_parallel (def 0 = auto; lite 1)         */
    int   parallel_verify;     /* M144: verify each write child's worktree
                                * before merging (opt-in; needs a verifier)    */
    long  parallel_task_timeout; /* per-child watchdog (s, M62); 0 => built-in  */
    int   daemon_workers;      /* daemon concurrent request workers; 0 => auto  */
    long  daemon_worker_timeout; /* daemon per-request watchdog (s); 0 => 300   */
    int   control_on;          /* M159: open the mid-run control socket        */
    char *control_path;        /* M159: socket path; NULL => default           */
    int   repo_map;            /* repository map in the prompt (def 1; lite 0)  */
    long  repo_map_limit;      /* repo-map byte budget; 0 => built-in default   */
    int   references;          /* expand @file/@diff/@url (def 1; lite 0)       */
    int   markdown;            /* markdown/syntax in the TUI (def 1; lite 0)    */
    int   type_ahead;          /* collect keys typed mid-turn in the TUI (def 0,
                                * opt-in: M257, see docs/TYPE_AHEAD.md)         */
    int   wisdom;              /* show idle proverbs from wisdom.json (def 1)   */
    int   board;               /* kanban board tool + focus injection (def 0)   */
    int   fuzzy_edit;          /* edit_file/apply_patch fuzzy fallback (M38, def 1)*/
    int   assignments;         /* SDLC assignment-authoring nudge (def 0/off)   */
    int   voice;               /* M303: speak replies, approval prompts and
                                * errors aloud (def 0). Opt-in: it needs an
                                * audio-role model and a sound.play command, and
                                * silence would be a worse failure than a
                                * refusal -- see jc_voice.h.                    */
    int   craft;               /* (def 1; lite 0) M299: design-first + honesty
                                * section (def 1). Off gives the terser pre-M299
                                * prompt, for a small context window or a
                                * token-cost measurement.                       */
    int   self_review;         /* -1 auto (AUTO mode only) / 0 off / 1 on       */
    int   path_fence;          /* workspace containment fence (M24):
                                * -1 auto (autonomous postures only) /0 off /1 on*/
    int   prompt_cache;        /* prompt caching (M31d): -1 auto (=on) /0 off /1 on;
                                * the global default for each model's effective
                                * jc_model_cfg.prompt_cache (per-model key wins) */
    int   prompt_cache_1h;     /* global cache TTL (M31e): 0 => 5-min default,
                                * 1 => 1-hour (config "promptCacheTtl":"1h") */
    int   cost_model;          /* M440: the `# Cost model` prompt section --
                                * -1 auto / 0 off / 1 on, resolved by the pure
                                * jc_config_cost_model_on against the ACTIVE
                                * model's effective prompt_cache. Auto = on only
                                * when caching is off, because that is the case
                                * where docs/TOOL_OUTPUT_COST.md §1's multiplier
                                * applies to every byte; with a cached prefix the
                                * same prose is billed once and buys little. */
    int   low_resource;        /* lite mode (--lite / "lowResource"): 1 when on */
    int   log_level;           /* event-log tier (jc_eventlog_level): metrics
                                * unless config/CLI say otherwise (M599); 0 off */
    char *log_path;            /* event-log file path; NULL => built-in default */
    long  read_max_bytes;      /* read_file cap; (def 0 = built-in; lite 64k)   */
    long  run_max_bytes;       /* run_* cap; (def 0 = built-in; lite 16k)       */
    long  fetch_max_bytes;     /* fetch_url cap; (def 0 = built-in; lite 32k)   */
    long  search_max_bytes;    /* search_code cap; (def 0 = built-in; lite 16k) */
    long  git_max_bytes;       /* git_* cap; (def 0 = built-in; lite 8k)        */
    long  image_gen_max_bytes; /* generate_image output cap; 0 => built-in (M32)*/
    long  audio_gen_max_bytes; /* generate_audio output cap; 0 => built-in (M32)*/
    long  transcribe_max_bytes;/* transcribe_audio input cap; 0 => built-in (M33)*/
    struct jc_vec docs;        /* of struct jc_docs_cfg: external doc sources (M34a) */
    struct jc_vec aliases;     /* of struct jc_alias_cfg: @ref:<name> aliases (#6) */
    struct jc_routing_cfg routing; /* tiered fast/strong model routing          */
    struct jc_retrieval_cfg retrieval; /* hybrid/query-rewrite tuning (M60)      */
    int   auto_context;        /* auto-RAG context injection (M61, def 0/off)   */
    int   auto_context_top_k;  /* chunks injected per turn; 0 => default 5      */
    long  auto_context_max_tokens; /* injection token budget; 0 => default       */
    int   auto_context_sources;/* enum jc_autoctx_src (default JC_ACTX_BOTH)    */
    int   learn_on_stop;       /* run the mentor after a completed --auto run
                                * (M71, config "learnOnStop"; default 0/off)    */
    int   tool_profile;        /* tool advertisement profile (M74, "toolProfile"):
                                * -1 auto (core when small/lite) / 0 full / 1 core */
    struct jc_timeouts_cfg timeouts;     /* global "timeouts" block (-1 unset)  */
    struct jc_timeouts_cfg timeouts_cli; /* --timeout-* CLI overrides (-1 unset)*/
};

/* Release resources owned by the config (the models vector). The model
 * strings themselves live in the arena and are freed with it. */
void jc_config_free(struct jc_config *c);

/* Number of configured models. */
int jc_config_model_count(const struct jc_config *c);

/* Model at index `i`, or NULL if out of range. */
struct jc_model_cfg *jc_config_model_at(struct jc_config *c, int i);

/* Make model `i` active (copies it into c->model). Returns JC_ERR_INVALID if
 * the index is out of range. */
jc_status jc_config_set_active(struct jc_config *c, int i);

/* Resolve each model's effective prompt_cache (M31d): the per-model key
 * (prompt_cache_cfg) when set, else the global c->prompt_cache, else on (auto).
 * Idempotent; call after config load and after applying CLI overrides. Covers
 * both c->models[*] and the active c->model copy. */
void jc_config_resolve_prompt_cache(struct jc_config *c);

/* M440: is the `# Cost model` prompt section emitted?
 *
 * `cfg` is the tri-state config value; `prompt_cache_on` is the ACTIVE model's
 * effective prompt_cache. An explicit 0/1 wins; -1 (auto) means "on iff caching is
 * off". Pure and unit-tested, because this is the whole of the policy the row
 * `docs/DEFERRED.md` carried: the correct read policy is OPPOSITE on the two
 * backend classes, so a single unconditional block of frugality prose would be
 * wrong on one of them -- and permanently billed in the cached prefix.
 *
 * Deliberately keyed off the CONFIGURED cache setting, not the observed hit rate.
 * The observed rate is a running statistic; putting it in the system prompt would
 * change the cached prefix from turn to turn and destroy the very caching it
 * describes (M31's prefix-stability invariant). The known gap that follows -- a
 * backend that silently ignores a caching request, which is a measured case -- is
 * what the explicit `1` is for, and what `doctor` / `telemetry --cache-audit`
 * exist to surface. See docs/TOOL_OUTPUT_COST.md §6. */
int jc_config_cost_model_on(int cfg, int prompt_cache_on);

/* Resolve a model selector: a 1-based decimal index, or a case-insensitive
 * substring of the model's name or id. Returns the index, or -1 if no match. */
int jc_config_find_model(const struct jc_config *c, const char *selector);

/* Index of the first model declaring `role` (a single JC_ROLE_* flag), or -1
 * if none is configured. */
int jc_config_find_by_role(const struct jc_config *c, unsigned role);

/* How a model selector resolves against this config (M284). Selectors appear in
 * agent-profile and command frontmatter (`model:`) and in the routing tiers, and
 * all three resolve the same way: 1-based index, else a case-insensitive
 * substring of a name/id, else a role name. Until M284 an unresolvable selector
 * was discovered only when the subagent was spawned -- a tool error mid-run,
 * inside an --auto turn that then burns budget recovering -- so this classifies
 * one at config time for the `doctor` lint. Pure. */
enum jc_selector_status {
    JC_SEL_OK = 0,      /* resolves to exactly one model (empty => the active) */
    JC_SEL_AMBIGUOUS,   /* substring-matches more than one configured model    */
    JC_SEL_ROLE_EMPTY,  /* names a real role, but no configured model holds it */
    JC_SEL_NONE         /* resolves to nothing at all                          */
};

/* Classify `selector` exactly as jc_config_find_model + the role fallback would
 * resolve it. `nmatch`, when non-NULL, receives the number of models whose name
 * or id the selector substring-matches (0 on the index and role paths). Note
 * AMBIGUOUS is still *resolvable* -- the first match wins -- so it is a warning,
 * not an error. Pure. */
enum jc_selector_status jc_config_selector_check(const struct jc_config *c,
                                                const char *selector,
                                                int *nmatch);

/* Resolve the routing tiers. Writes the model indices for the fast and strong
 * selectors (each a name/index/role) and returns 1 iff routing is enabled and
 * the two resolve to distinct valid models; otherwise returns 0. The single
 * predicate the agent loop and TUI consult before routing. */
int jc_config_routing_resolve(const struct jc_config *c, int *fast_idx,
                              int *strong_idx);

/* M288: should context pressure escalate to the strong tier right now? True iff
 * the trigger is enabled (`pct` > 0), both windows are known, the strong tier's
 * window is STRICTLY LARGER (escalating for room you do not gain is pure cost),
 * and `est_tokens` has reached `pct`% of the fast window.
 *
 * `est_tokens` must already be calibrated (M286) and `*_limit` must be the
 * EFFECTIVE limits -- i.e. after a global `contextLimit` override. That override
 * flattens per-model windows, which correctly makes this inert: a user who pins
 * one budget for every model has said the tiers are equally roomy, and jichi
 * must not quietly reinterpret an explicit budget. `doctor` reports the
 * contradiction instead. Pure; unit-tested. */
int jc_config_context_escalate(long est_tokens, long fast_limit,
                               long strong_limit, int pct);

/* M298: should a context-caused escalation be UNDONE right now? True iff the
 * trigger is enabled (`pct` > 0), the fast window is known, and `est_tokens` has
 * fallen below (`pct` - JC_ROUTE_DEESCALATE_GAP)% of it.
 *
 * The gap is the point. M288 escalates when the fast tier runs out of room, and
 * mid-turn compaction can hand that room back -- but a bare threshold would
 * oscillate, and each switch costs a provider rebuild and the cached prefix. The
 * caller must ALSO have recorded that the escalation was caused by context: a
 * verify-fail or tool-error escalation said the fast model was not capable enough,
 * which no amount of freed context changes, so those must never come back down
 * this way. Pure; unit-tested. */
int jc_config_context_deescalate(long est_tokens, long fast_limit, int pct);

/* Effective context (tokens) below which the `auto` tool profile picks the lean
 * core set. */
#define JC_TOOL_PROFILE_AUTO_BELOW 12000

/* Resolve whether to advertise only the lean core tool set: config 1 => core,
 * 0 => full, -1 (auto) => core when lite mode is on or the effective context
 * budget `context_limit` is known and below JC_TOOL_PROFILE_AUTO_BELOW. Pure;
 * unit-tested (M74). */
int jc_config_tool_profile_core(const struct jc_config *c, long context_limit);

/* Built-in output-token cap when neither a per-model maxTokens nor a
 * contextLength is known. */
#define JC_DEFAULT_MAX_TOKENS 4096

/* Effective output token cap (max_tokens) to send on a request. ALWAYS returns
 * > 0 so the request carries an explicit max_tokens: some OpenAI-compatible
 * proxies (litellm/vLLM) default an *omitted* max_tokens to the full context
 * window, which then leaves no room for the prompt and 400s every request
 * (M84 -- see docs/ANECDOTES.md). Precedence: configured `maxTokens` >
 * a bounded fraction of `context_limit` (~1/5, so max_tokens + a compaction-
 * bounded prompt still fits the window; clamped to [512, 16384]) > the built-in
 * JC_DEFAULT_MAX_TOKENS. Pure; unit-tested. */
long jc_config_effective_max_tokens(long configured, long context_limit);

/* Walk model `idx`'s fallback chain to the first model whose endpoint is marked
 * reachable. `reachable` is a per-model byte array (length = model count; NULL
 * treats every model as reachable). Writes the chosen index to *out and returns
 * 1 when it lands on a reachable model; on a dead end (no/cyclic/unresolvable
 * fallback, or all unreachable) writes `idx` to *out and returns 0. Cycle- and
 * hop-bounded. Pure. */
int jc_config_fallback_chain(const struct jc_config *c, int idx,
                             const unsigned char *reachable, int *out);

/* The first model declaring `role`, or NULL. The pointer references the models
 * vector and is stable for the life of the config. */
struct jc_model_cfg *jc_config_model_for_role(struct jc_config *c,
                                              unsigned role);

/* Append a human/agent-readable menu of the models declaring `role` to `sb`,
 * one per line as "<name> — <description>" (the model id when unnamed; the
 * "— <description>" suffix omitted when none). Used by the generate_image tool
 * schema and `/status` so a workflow-appropriate model can be picked by name.
 * Appends nothing when no model declares the role. Pure. */
struct jc_sb; /* jc_str.h */
void jc_config_models_for_role_list(const struct jc_config *c, unsigned role,
                                    struct jc_sb *sb);

/* Parse a single role name ("chat", "embed", ...) to its JC_ROLE_* flag, or 0
 * if unrecognised. */
unsigned jc_config_role_flag(const char *name);

/* Estimated cost in USD for a model call, or 0 if the model has no pricing
 * configured. `in_tok` is the full-price (uncached) input tokens; `cache_read`
 * and `cache_write` are the prompt-cache read/write tokens, billed at
 * `cache_read_cost` / `cache_write_cost` per 1M, each falling back to the normal
 * `input_cost` when unpriced (so configs without cache pricing are unaffected
 * beyond the more-accurate accounting). M31c. */
double jc_config_cost(const struct jc_model_cfg *m, double in_tok,
                      double out_tok, double cache_read, double cache_write);

/* The `low_resource` hint passed to the loaders. Resolution precedence for
 * the lean profile (M272): an explicit CLI flag wins outright (ON = --lite,
 * OFF = --no-lite), else an explicit config `lowResource` key wins, else the
 * AUTO hint (main.c's low-RAM detection) applies. NONE = no flag, no
 * detection. Before M272 the key was OR-ed with a boolean hint, so a config
 * carrying `"lowResource": false` could not veto auto-lite on a small
 * machine -- found when the smoke tier first ran on a 256 MB Tier V guest
 * and auto-lite silently reshaped the drivers' environment. */
#define JC_LITE_HINT_OFF  (-1)
#define JC_LITE_HINT_NONE 0
#define JC_LITE_HINT_ON   1
#define JC_LITE_HINT_AUTO 2

/* Load configuration. `path_or_null` overrides the resolution order above.
 * `low_resource` (a JC_LITE_HINT_* value; see the precedence note there)
 * requests or vetoes the lean profile; the resolved flag shifts the *defaults*
 * for the resource-heavy keys (snapshots/repoMap/references/markdown off,
 * maxParallelAgents 1, maxSubagentDepth 0, smaller contextLimit/iters/retries)
 * while any explicitly-set config key still wins. All strings are allocated from
 * `a`. Always succeeds with sensible defaults unless an explicitly named file is
 * malformed. */
jc_status jc_config_load(const char *path_or_null, int low_resource,
                         struct jc_config *out, struct jc_arena *a);

/* Load configuration from an inline JSON string (the `--config-json` flag)
 * instead of a file: `json_text` is parsed as THE config -- a single explicit
 * source, with no global/project merge (like an explicit --config <path>). All
 * strings are allocated from `a`. Returns JC_ERR_INVALID for empty input and
 * JC_ERR_PARSE for malformed JSON; otherwise behaves like jc_config_load. */
jc_status jc_config_load_json(const char *json_text, int low_resource,
                              struct jc_config *out, struct jc_arena *a);

/* Overlay a project config tree onto a global one, in place (project wins
 * scalars; array keys union with project items first). Used by jc_config_load
 * to merge ~/.jichi with a project config; exposed (pure) for testing.
 * `overlay` is not modified. */
struct cJSON;
void jc_config_merge_json(struct cJSON *base, const struct cJSON *overlay);

/* Find a named @ref alias (#6) by exact name, or NULL. */
const struct jc_alias_cfg *jc_config_find_alias(const struct jc_config *c,
                                                const char *name);

/* A tool's output cap in bytes: the `configured` config value when > 0, else the
 * tool's built-in default. Pure; used by the read/run/fetch/search tools so the
 * lean profile (and explicit config keys) can shrink their buffers. */
jc_size jc_config_cap(long configured, jc_size builtin);

/* Effective wall-clock timeout (seconds) for a model-issued shell command:
 * the first of (per_call, cli, cfg) that is > 0, else 0 (no limit). Precedence
 * per-call `timeout` arg > `--run-timeout` > config `runTimeout`. A value <= 0
 * means "unset" (cfg 0 = the off default). Pure; unit-tested. */
long jc_config_run_timeout(long per_call, long cli, long cfg);

/* Resolve the effective model-call timeouts (seconds) for model `m` under
 * config `c`, by precedence: CLI override > per-model > global block > built-in
 * default. A tier value of -1 means "unset" (fall through); 0 means explicitly
 * disabled (the resolved value is then 0 => no bound). Any out pointer may be
 * NULL. `m` NULL uses only the global/CLI tiers; `c` NULL yields the built-in
 * defaults. Pure. */
void jc_config_resolve_timeouts(const struct jc_config *c,
                                const struct jc_model_cfg *m,
                                long *connect, long *stall, long *request);

/* Resolve the default base URL for a provider name. Never NULL. */
const char *jc_config_default_base(const char *provider);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONFIG_H */
