/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_app.h - top-level application context.
 *
 * Bundles the long-lived state shared across the chat/agent/tool/TUI layers:
 * the arena, configuration, the active provider, the tool registry, the
 * working directory, the set of files that have been read (for the edit
 * guard), and the interrupt flag set by the SIGINT handler.
 */
#ifndef JC_APP_H
#define JC_APP_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_config.h"
#include "jc_vec.h"
#include "jc_todo.h"
#include "jc_board.h"
#include "jc_agentdef.h"
#include "jc_command.h"
#include "jc_skill.h"
#include "jc_output_style.h"
#include "jc_reread.h"
#include "jc_calib.h"
#include "jc_prefix.h"
#include "jc_constraint.h"

struct jc_telemetry_summary; /* jc_telemetry.h */
struct jc_provider;       /* jc_provider.h */
struct jc_envelope;       /* jc_envelope.h */
struct jc_agent_callbacks;/* jc_agent.h    */
struct jc_tool_registry;  /* jc_tool.h     */
struct jc_index;          /* jc_index.h    */
struct jc_mcp_manager;    /* jc_mcp.h      */
struct jc_lsp_manager;    /* jc_lsp.h      */
struct jc_snapshot_mgr;   /* jc_snapshot.h */
struct jc_sb;             /* jc_str.h      */
struct jc_eventlog;       /* jc_eventlog.h */
struct cJSON;             /* cJSON.h -- named struct, so a forward decl works */
struct jc_assign_spec;    /* jc_assign.h   */
struct jc_bg_mgr;         /* jc_bg.h       */
struct jc_control;        /* jc_control.h  */
struct jc_message;        /* jc_message.h  */

/* Largest image accepted by jc_app_load_image (pre-encode bytes, M29). */
#define JC_IMAGE_MAX_BYTES (5L * 1024L * 1024L)

/* Cap on the --design/--spec doc injected into the system prompt (M-C). Head-kept
 * (a design's seam is usually near the front) + a truncation note when exceeded,
 * so a large spec can't blow the always-sent prefix on a cacheless backend. */
#define JC_DESIGN_MAX (16 * 1024)
/* M462: how many design docs may be merged. The cap is on the TOTAL bytes
 * above, not per document, so this bounds only the dedupe table. Eight is
 * far past the sane case (a standing architecture doc plus a task spec);
 * more than that is a smell the byte cap, not this number, protects against. */
#define JC_DESIGN_MAX_DOCS 8

/* Optional client-side filesystem delegate. When an ACP client (an editor)
 * advertises fs capabilities, the ACP server installs one of these on the app
 * so the file tools route reads/writes through the editor instead of touching
 * disk directly -- which lets jichi see the editor's *unsaved* buffers and lets
 * edits land in them. NULL (the TUI/headless default) keeps the plain disk
 * path. A NULL `read`/`write` member (or a non-JC_OK return) means "I can't do
 * this", and the app helper falls back to disk. Content is owned by `a`
 * (arena), so callers never free it; `path` is as the tool received it (the
 * delegate resolves it). */
struct jc_fs_delegate {
    jc_status (*read)(void *ctx, const char *path, char **out, jc_size *len,
                      struct jc_arena *a);
    jc_status (*write)(void *ctx, const char *path, const char *data,
                       jc_size len);
    void      *ctx;
};

/* Optional client-side command (terminal) delegate. When an ACP client
 * advertises the `terminal` capability, the ACP server installs one of these so
 * run_terminal_command / run_tests execute in the *editor's* terminal (shown in
 * its UI, killable by it, in its environment) instead of a local subprocess.
 * NULL (the TUI/headless default) keeps the local popen path. `run` appends the
 * combined stdout+stderr to `out` bounded by `byte_limit` (setting *truncated
 * when capped), reports the process exit code in *exit_code (128+signal if
 * killed), and returns JC_OK when the command ran; a non-JC_OK return makes
 * jc_app_run_command fall back to local execution. */
struct jc_cmd_delegate {
    jc_status (*run)(void *ctx, const char *command, jc_size byte_limit,
                     struct jc_sb *out, int *exit_code, int *truncated);
    void      *ctx;
};

/* Optional front-end "ask the user" delegate (M34d / F4). The ask_user tool
 * calls this to pause and put a focused clarifying question to the user mid-turn
 * (with optional suggested answers). The TUI installs one (a blocking line
 * prompt); headless/ACP leave it NULL, so the tool reports that no interactive
 * user is available and the model proceeds -- an autonomous run never hangs.
 * `ask` appends the user's answer to `out` (caller-owned jc_sb) and returns
 * JC_OK; a non-JC_OK return (or NULL member) means "no answer available". */
struct jc_ask_delegate {
    jc_status (*ask)(void *ctx, const char *question,
                     const char *const *options, int noptions,
                     struct jc_sb *out);
    void      *ctx;
};

struct jc_app {
    struct jc_arena         *arena;       /* session-lived allocations         */
    struct jc_arena         *scratch;     /* per-turn transient arena, or NULL */
    struct jc_arena         *tool_scratch;/* per-TOOL-CALL transient arena (M199)*/
    struct jc_config         config;
    struct jc_provider      *provider;
    struct jc_tool_registry *tools;
    struct jc_mcp_manager   *mcp;         /* MCP client, or NULL                 */
    struct jc_lsp_manager   *lsp;         /* LSP diagnostics client, or NULL     */
    struct jc_snapshot_mgr  *snapshots;   /* git snapshot/undo manager, or NULL  */
    struct jc_envelope      *env;         /* autonomy envelope, or NULL          */
    struct jc_eventlog      *telemetry;   /* opt-in event-log sink, or NULL (M21) */
    struct jc_index         *index;       /* lazily-built codebase index, or NULL */
    struct jc_bg_mgr        *bg;          /* background-process registry, or NULL */
    struct jc_control       *control;     /* mid-run control socket (M159), NULL */
    char                     cwd[1024];
    char                     root[4096];  /* canonical workspace root (M24)    */
    char                    *rules;       /* concatenated AGENTS.md etc., or NULL */
    char                    *repo_map;    /* repository map section, or NULL     */
    char                    *memory;      /* persisted .jichi/memory.md, or NULL   */
    char                    *glossary;    /* .jichi/glossary.md domain terms, NULL */
    char                    *design;      /* --design/--spec doc (M-C), or NULL  */
    struct jc_constraint     constraints[JC_CONSTRAINT_MAX]; /* enforced (M110)  */
    int                      n_constraints;
    int                      constraints_on;       /* feature enabled (dflt 1)   */
    int                      constraints_autoadopt;/* AUTO: scan+enforce (dflt 1)*/
    struct jc_todo_list     *todos;       /* the LIVE SESSION's task list
                                           * (todowrite/todoread), or NULL before
                                           * a session exists. M606: the list is
                                           * owned by struct jc_session and saved
                                           * with it; whoever makes a session live
                                           * (headless open, TUI open/resume/fork,
                                           * ACP new/load) points this at it.    */
    struct jc_board          board;       /* persisted kanban phase board (#7)    */
    struct jc_agentdef_set   agents;      /* named subagent profiles             */
    struct jc_command_set    commands;    /* custom slash commands               */
    struct jc_skill_set      skills;      /* agent skills (load_skill)           */
    struct jc_output_style_set output_styles; /* custom output styles (M28)      */
    struct jc_vec            read_files;  /* of char*: paths read this session */
    struct jc_vec            read_recs;   /* of jc_read_rec: re-read detection (M231) */
    int                      auto_approve;/* skip tool permission prompts      */
    int                      readonly;    /* refuse mutating tools             */
    int                      mode;        /* enum jc_agent_mode (jc_perm.h)    */
    int                      mode_pinned; /* mode set on the CLI; ignore resume */
    int                      quiet;       /* suppress stderr diagnostics        */
    int                      heartbeat_secs; /* --heartbeat jsonl cadence (M165) */
    int                      no_session;  /* do not persist this run            */
    int                      agent_depth; /* 0 at top level; +1 per subagent   */
    int                      last_run_capped; /* last run_agent_loop hit its
                                           * iteration cap (M62 #5); read right
                                           * after the run returns              */
    /* M431: the last run_agent_loop stopped because the ENVELOPE budget was
     * exhausted, not because it finished. Same family and same read-right-after
     * contract as last_run_capped, and set only at agent_depth > 0: at depth 0 a
     * budget stop goes through env_stop_for_budget, which settles the whole run
     * (journal, verifier, rollback decision) and needs no flag. A delegate must
     * not run any of that, so it stops and lets the top level settle -- but a
     * budget-stopped delegate that reported as success would be exactly the
     * silent false success this milestone exists to remove. */
    int                      last_run_budget_stopped;
    /* M437: the LAST FAILING tool call of the run that just returned -- the field
     * a parent was missing. `spawn_subagent` used to answer a failure with one of
     * two fixed strings, so a parent could not tell an edit-scope denial from a
     * tool error from a refusal, and its only moves were to re-delegate
     * identically (paying the subtask twice) or give up.
     *
     * Same read-right-after-the-run contract as last_run_capped, and kept true at
     * depth by the same mechanism: run_agent_loop SAVES and RESTORES all of these
     * around every jc_tool_execute, so a nested delegation cannot leave its own
     * failure -- or its cap -- attributed to the run that contained it. That also
     * fixes the hazard this header documented for the top-level turn (M322) and
     * left open for intermediate depths: before M437 a capped GRANDCHILD poisoned
     * a clean child's report, because the loop cleared the flags on entry only. */
    char                     last_fail_tool[64];
    int                      last_fail_cls;   /* enum jc_fail_class */
    char                     last_fail_msg[200];
    /* M443: decisions this run made ON THE OPERATOR'S BEHALF because no operator
     * was there to make them. A headless run reports them only obliquely today --
     * a synthetic tool error the model sees, an `ask` journal event with
     * answered:false that only the offline `runs` reader counts -- so a supervisor
     * cannot tell "ran clean" from "ran having refused two escalations and answered
     * three of its own questions".
     *
     * NOT counted here: a tool auto-approved under `--auto`. That is the operator's
     * explicit instruction, not a decision taken in their absence, and counting it
     * would make every --auto run degraded and the flag worthless. The line is
     * "would a present operator have been consulted?" -- see docs/SCRIPTING.md. */
    int                      deg_unanswered;   /* ask_user with no delegate     */
    int                      deg_approval;     /* ASK verdict, nobody to ask    */
    int                      deg_privilege;    /* priv/kinetic gate, unattended */
    /* M322: the TOP-LEVEL turn hit its iteration cap. Distinct from
     * last_run_capped, which describes whichever run finished most recently --
     * including a subagent's. Because run_agent_loop clears last_run_capped only
     * on ENTRY, a subagent that caps leaves it set, and a top-level turn that
     * then finishes cleanly would read as capped. Set only at agent_depth 0 and
     * cleared by jc_agent_run_turn, so it answers exactly one question: did the
     * turn the caller asked for stop early? Drives the "max_iters" stop_reason. */
    /* M334: the last assistant message was cut off at the model's output-token
     * ceiling. Read by the tool layer, which must NOT try to repair a tool call
     * from a truncated response -- the repair succeeds and produces a valid
     * object with missing fields, hiding the real cause. */
    int                      last_response_truncated;
    int                      turn_capped;
    int                      turn;        /* top-level turn counter (telemetry) */
    int                      routed_for_context; /* M298: this turn escalated to
                                           * the strong tier because the FAST tier
                                           * ran out of room (M288) -- not because
                                           * a verify or tool call failed. Only a
                                           * context-caused escalation may be
                                           * undone when mid-turn compaction gives
                                           * the room back; a capability-caused one
                                           * must stay. Set by route_on_context,
                                           * cleared at each top-level turn start
                                           * and on de-escalation.               */
    long                     last_prompt_tokens; /* real prompt_tokens of the most
                                           * recent model call (from usage; 0 if
                                           * none) -- calibrates mid-turn
                                           * compaction's optimistic byte estimate
                                           * to the model's actual tokenizer (M76) */
    long                     max_prompt_tokens; /* M494: the LARGEST prompt_tokens
                                           * the server has ACCEPTED this run.
                                           * Evidence that a declared window
                                           * understates the model is MONOTONE --
                                           * once an oversized request is served,
                                           * the real window is provably at least
                                           * that big, for the rest of the run --
                                           * so the under-declaration notice reads
                                           * this high-water mark rather than
                                           * `last_prompt_tokens`, which is a
                                           * momentary sample. Measured: a run
                                           * with 9 accepted requests over its
                                           * declared limit printed the notice 0
                                           * times, because the short-fall warning
                                           * it hangs off fires ONCE PER TURN and
                                           * the latest request at that instant
                                           * happened to be small. */
    long                     last_nonhist_est; /* M286: byte-estimate of the most
                                           * recent request's NON-history part
                                           * (system prompt + tool schemas),
                                           * measured rather than assumed. The
                                           * calibration basis and the compaction
                                           * triggers use it; 0 means "no request
                                           * yet", and those callers fall back to
                                           * the flat SYS_TOOLS_OVERHEAD. Written
                                           * at every depth, so between a
                                           * subagent's call and the parent's next
                                           * one it describes the subagent's
                                           * request -- an imprecision, but a far
                                           * smaller one than the flat constant it
                                           * replaces (measured 7.4k-11.2k vs the
                                           * assumed 2k; see docs/COMPACTION.md) */
    struct jc_calib          calib;       /* persisted per-model estimate->real
                                           * token ratio, learned from usage and
                                           * applied to every context decision
                                           * that reads the byte estimate (M77) */
    struct jc_prefix_watch   prefix_watch;/* M365: system-prompt churn sentinel
                                           * -- fires once per session when the
                                           * prompt hash changes on 3 straight
                                           * turns (no legitimate cause does) */
    long fit_held;                        /* M365: deadband-held fit budget   */
    long fit_held_limit;                  /* ...and the limit it was held for */
    const struct jc_agent_callbacks *cb;  /* active top-level callbacks, or NULL*/
    int                      stream_subagents; /* forward cb into subagents (TUI)*/
    const struct jc_fs_delegate *fs;      /* client-side fs delegate, or NULL  */
    const struct jc_cmd_delegate *cmd;    /* client-side terminal delegate, NULL*/
    const struct jc_ask_delegate *ask;    /* front-end ask_user delegate, NULL  */
    const struct jc_assign_spec *assignment; /* active assignment while solving,
                                           * or NULL -- gives the `hint` tool its
                                           * ladder + the solver its context */
    int                      hints_used;  /* hints revealed this attempt (B2)  */
    /* M536: what the `hint` TOOL needs in order to keep the promise its own
     * description makes -- "hints are limited and their use is recorded". Only
     * the `jichi hint` CLI ever wrote .jichi/hints.jsonl (M502); the tool, which
     * is how a MODEL and a TUI learner pull a rung, recorded nothing, so the
     * teacher-facing ladder diagnostic was empty for every path but one.
     *
     * Two fields, not one, and the second is the interesting one: `attempt`
     * chdirs into a throwaway git worktree for the duration, so recording
     * against "." or app->cwd would file the row inside the sandbox and delete
     * it with the sandbox -- a writer naming a path its reader cannot read, the
     * shape M533 fixed twice. assignment_dir is therefore the REAL workspace,
     * captured by whoever arms the assignment, and it is the directory the
     * grade lands in too, so the two sinks agree about which project this was.
     * Both NULL/empty when no assignment is active. */
    const char              *assignment_spec; /* spec path, as the CLI names it */
    char                     assignment_dir[1024]; /* real workspace for records */
    int                      assignment_tutor; /* M173b: 1 while a HUMAN works the
                                           * active assignment interactively --
                                           * flips the sysmsg to tutor stance
                                           * (guide, never solve). 0 during
                                           * `attempt`, where the model IS the
                                           * learner and must solve.           */
    const char              *persona_override; /* transient: a command `agent:`
                                           * profile body replacing the base
                                           * persona for one turn, or NULL     */
    int                      model_pinned; /* transient: a command's `model:`
                                           * resolved for this turn, so
                                           * turn-start routing must NOT
                                           * override it. Without this, a
                                           * command that declares a model had it
                                           * applied and then silently replaced by
                                           * routing's fast tier -- the
                                           * declaration did nothing whenever
                                           * routing was enabled (measured
                                           * 2026-08-07). Set on RESOLUTION, not
                                           * on switching: a selector naming the
                                           * already-active model is still a pin. */
    const char              *style_override; /* M302: transient: the NAME of an
                                           * output style from a profile's or a
                                           * skill's `style:` key, overriding the
                                           * session style while that specialist
                                           * runs, or NULL. A name resolved
                                           * against app->output_styles, not
                                           * prose -- one persona mechanism, not
                                           * two. Precedence: session style <
                                           * this < a command `agent:` BODY,
                                           * which stays authoritative because it
                                           * already replaces the whole persona. */
    int                      resume;      /* resume the most recent session    */
    const char              *session_id;  /* --session <id|prefix>, or NULL    */
    const char              *dump_requests; /* M341: --dump-requests <dir>, or
                                             * $JICHI_DUMP_REQUESTS. NULL = off.
                                             * A diagnostic, never a default:
                                             * every request body on disk. */
    const char *const       *image_args;  /* --image paths (argv-owned), or NULL*/
    int                      image_args_n;/* count of --image paths (M29)       */
    int                      session_all; /* --all: don't scope to the cwd     */
    int                      color_mode;  /* -1 auto / 0 off / 1 on (ANSI color)*/
    signed char             *reach;       /* per-model reachability cache:
                                           * 0 unknown / 1 up / -1 down, or NULL */
    volatile int             abort_flag;  /* set by SIGINT/SIGTERM (abort turn) */
    volatile int             term_flag;   /* set by SIGTERM (M146): end the
                                           * session gracefully, not just the
                                           * current turn (TUI exits its REPL) */
    int                      noop_warned; /* M147: the once-per-session "model
                                           * narrated a call but never invoked
                                           * one" warning has fired            */
    int                      tc_none_noticed; /* M149: the once-per-session
                                           * toolCalling:none notice has fired */
    int                      empty_warned; /* M167: the once-per-session "tools
                                           * advertised, no call, empty answer"
                                           * warning has fired. That triple is
                                           * the signature of a malformed
                                           * request, not of a lazy model --
                                           * M166 produced it silently six runs
                                           * in a row (docs/ANECDOTES.md #19) */
};

/* Set the operating mode (an enum jc_agent_mode value), updating the low-level
 * auto_approve/readonly switches to match: PLAN => readonly + ask; AUTO =>
 * auto-approve; CHAT => neither. */
void jc_app_set_mode(struct jc_app *app, int mode);

/* Honor a custom command's `agent:` frontmatter: run the command's turn "as"
 * that named agent profile by transiently injecting the profile's system prompt
 * (as system_prompt_extra) and applying its readonly flag. Saved state goes into
 * `*save`; call jc_app_command_agent_restore after the turn to undo it. Returns
 * the applied profile, or NULL when `name` is NULL/empty or names no profile (a
 * no-op the caller can ignore). */
struct jc_command_agent_save {
    const char *prev_persona;  /* prior app->persona_override (restored) */
    const char *prev_style;    /* prior app->style_override (M302)        */
    int         prev_readonly; /* prior app->readonly                    */
    int         applied;       /* whether a profile was applied          */
};
const struct jc_agentdef *jc_app_command_agent_apply(
    struct jc_app *app, const char *name, struct jc_command_agent_save *save);
void jc_app_command_agent_restore(struct jc_app *app,
                                  const struct jc_command_agent_save *save);

/* Honor a custom command's `model:` frontmatter: switch the active model to the
 * one named by `selector` (a name/id substring or 1-based index, resolved via
 * jc_config_find_model) for the duration of the turn, recreating the provider.
 * Saved state goes into `*save`; call jc_app_command_model_restore afterward to
 * switch back. A NULL/empty/unresolvable selector, or one naming the already
 * active model, is a graceful no-op. */
struct jc_command_model_save {
    int prev_idx;   /* the active model index before the switch */
    int switched;   /* whether a switch happened (and must be undone) */
};
void jc_app_command_model_apply(struct jc_app *app, const char *selector,
                                struct jc_command_model_save *save);
void jc_app_command_model_restore(struct jc_app *app,
                                  const struct jc_command_model_save *save);

/* Record that a file has been read (for the read-before-edit guard). */
void jc_app_mark_read(struct jc_app *app, const char *path);
int  jc_app_was_read(const struct jc_app *app, const char *path);

/* M231: record a read of `path` and report whether it is a byte-for-byte
 * identical re-read of the SAME RANGE this session (1 = the caller should emit
 * the redundant-read advisory, 0 = first read of this range, or its content
 * changed). Keyed on (path, offset, limit) plus size + a hash of `data`/`len`,
 * which must be the bytes actually SHOWN to the model -- so any edit returns 0,
 * and so does paging through a file with a different offset/limit (M287). */
int  jc_app_reread_check(struct jc_app *app, const char *path,
                         const char *data, jc_size len,
                         long offset, long limit);

/* Read/write a file, honoring app->fs (the client-side delegate) when set and
 * able, else falling back to the local disk. `path` is passed through to the
 * delegate as-is (it resolves it); the disk fallback uses it verbatim. On read,
 * *out is owned by `a` and *len (if non-NULL) gets the byte length. */
/* The newest telemetry log under ~/.jichi.d/telemetry/, summarized and filtered
 * to THIS workspace (M317). Returns 1 when a log was found and holds events for
 * this workspace, else 0 (and *out is left initialized-but-empty, so the caller
 * renders a stated absence rather than a column of zeroes -- M314's principle).
 * On 1 the caller owns *out and must jc_telemetry_summary_free it; `label`, when
 * non-NULL, receives the log's basename plus ", this workspace" for a report to
 * name its evidence.
 *
 * Lives here rather than in a report or in jc_telemetry because it is I/O about
 * THIS workspace: jc_telemetry is pure by contract (unit-tested offline), and
 * duplicating this loader in each caller is the two-copies drift M311/M312/M313
 * and M316 each had to undo. Two callers today: `context tools` and the TUI's
 * `/context tools`. */
/* M599: the telemetry log a reader should open for `ws` -- the workspace's own
 * default file (jc_telemetry_default_path) when it exists, else the newest
 * .jsonl under ~/.jichi.d/telemetry (the pre-M599 rule, kept for explicit
 * `--log <path>` users and old logs). Returns 0 and fills `out` when a log was
 * found, -1 when the directory holds none. Used by `telemetry`, `learn analyze`,
 * `dream`, `improve` and jc_app_load_telemetry, so five readers cannot disagree
 * about which project they are describing. */
int jc_app_pick_telemetry_log(const char *ws, struct jc_arena *a,
                              char *out, jc_size cap);

int jc_app_load_telemetry(struct jc_app *app, struct jc_telemetry_summary *out,
                          char *label, jc_size label_cap);

/* Begin a telemetry event stamped with the three keys every offline reader
 * joins on -- `depth`, `turn` and (M420) the envelope's `run` -- or NULL when
 * telemetry is off, so the caller's field-adds are skipped and there is zero
 * cost when disabled. Pair with jc_app_telem_end().
 *
 * WHY IT IS SHARED, and why that is the whole point (M583, seams D4). This
 * began as a static telem() inside jc_agent.c, so only emitters in that one
 * file were stamped. NINE call sites elsewhere reached jc_eventlog_begin()
 * directly and carried NONE of the three: prefix_churn, hook, retrieve,
 * test_edit, args_truncated, and four args_repair variants. M420 built the
 * join between behaviour and outcome and it was partial by exactly those
 * events -- "which turn did the argument repairs happen on?" had no answer.
 * A shared function travels; a literal stays where it was typed (M566).
 *
 * NULL app is tolerated on purpose: five of the nine sites already wrote
 * `app != NULL ? app->telemetry : NULL` because the argument-repair path can
 * run without one. Folding that guard in here is what lets those sites become
 * one-liners instead of carrying the conditional twice each. */
struct cJSON *jc_app_telem_begin(struct jc_app *app, const char *event);
void          jc_app_telem_end(struct jc_app *app, struct cJSON *o);

jc_status jc_app_read_file(struct jc_app *app, const char *path, char **out,
                           jc_size *len, struct jc_arena *a);
jc_status jc_app_write_file(struct jc_app *app, const char *path,
                            const char *data, jc_size len);

/* Load image file `path`, base64-encode it, and attach it to message `m` (M29).
 * The read goes through jc_app_read_file (so the path fence + 64 MB read cap
 * apply); the media type is sniffed from the extension. Returns JC_OK, or:
 * JC_ERR_INVALID (unsupported extension), JC_ERR_TOOBIG (over
 * JC_IMAGE_MAX_BYTES), JC_ERR_DENIED (outside the fence), JC_ERR_NOTFOUND, or
 * JC_ERR_OOM. */
jc_status jc_app_load_image(struct jc_app *app, const char *path,
                            struct jc_message *m);

/* Whether the workspace-containment path fence is active (M24). Resolves the
 * tri-state config.path_fence: 1 => always on; 0 => off; -1 (auto) => on only in
 * an autonomous posture (AUTO mode or --auto auto-approve). When on,
 * jc_app_read_file/jc_app_write_file refuse paths that resolve outside
 * app->root with JC_ERR_DENIED. */
int jc_app_path_fence_on(const struct jc_app *app);

/* Whether `path` is refused by the active path fence (fence on + a canonical
 * root set + `path` resolving outside it). jc_app_read_file/jc_app_write_file
 * consult this; tools that act on a path *before* writing (e.g. creating parent
 * directories) can call it first to avoid a side effect on a doomed write.
 * This is write-strict: equivalent to jc_app_path_denied_ex(app, path, 1). */
int jc_app_path_denied(const struct jc_app *app, const char *path);

/* As jc_app_path_denied, but `for_write` distinguishes intent: writes are
 * confined to the workspace root, while reads (`for_write == 0`) are also
 * permitted under any configured `referenceRoots` entry (read-only external
 * trees, M54). Use the read form for tools that only read a path. */
int jc_app_path_denied_ex(const struct jc_app *app, const char *path,
                          int for_write);

/* Run `command` (combined stdout+stderr), appending its output to `out` up to
 * `byte_limit` bytes (sets *truncated when capped; pass 0 for no limit). Routes
 * through app->cmd (the client terminal delegate) when installed and able, else
 * a local /bin/sh subprocess. *exit_code gets the process exit code (128+signal
 * if killed). Returns JC_OK when the command ran (even on a nonzero exit);
 * JC_ERR_* if it could not be started at all. `exit_code`/`truncated` may be
 * NULL. */
/* Give the front-end a liveness tick from inside a long-running operation
 * (M258). A foreground shell command can run for minutes -- the window in which
 * a user is most likely to be typing -- and the agent loop hands out no
 * callbacks while it blocks in the command runner, so the TUI cannot collect or
 * echo type-ahead. The command runner calls this whenever it is idle waiting on
 * output; it forwards `on_progress`, the same tick libcurl drives during a model
 * call. No-op when no front-end (or no on_progress) is installed, so headless,
 * ACP and subagent paths are unchanged. */
void jc_app_tick(struct jc_app *app);

jc_status jc_app_run_command(struct jc_app *app, const char *command,
                             jc_size byte_limit, struct jc_sb *out,
                             int *exit_code, int *truncated);

/* As jc_app_run_command, but with a wall-clock `timeout_sec` (0 = no limit).
 * A nonzero timeout (or a memory budget) routes through the watched fork
 * path; on expiry the command's process group is SIGTERM'd then SIGKILL'd,
 * a "[stopped: command timed out ...]" marker is appended, and `exit_code`
 * is 124 (the timeout(1) convention). jc_app_run_command is the timeout=0
 * wrapper. */
jc_status jc_app_run_command_ex(struct jc_app *app, const char *command,
                                jc_size byte_limit, long timeout_sec,
                                struct jc_sb *out, int *exit_code,
                                int *truncated);

/* The arena for per-turn-transient allocations (the system message and
 * command/@-reference expansion). It is reset at the start of each top-level
 * turn, so a long interactive session doesn't accumulate that scratch on the
 * session arena. Falls back to app->arena when no scratch arena is installed
 * (subcommands, tests), so callers never receive a NULL arena. */
struct jc_arena *jc_app_scratch(struct jc_app *app);

/* The arena for per-TOOL-CALL transients: a file's bytes while a tool formats,
 * matches or uploads them. Reset immediately before every tool call, at every
 * agent depth (jc_agent.c's run_agent_loop).
 *
 * Why it exists (M199). M197 moved these reads off app->arena (freed only at
 * exit) onto scratch, which bounded them to one TURN. That is not enough: a turn
 * is up to `maxToolIters` tool calls -- forced to >= 200 whenever an envelope or
 * verify gate is active (jc_agent.c) -- so a single `--auto` turn re-reading a
 * 250 KB file could still peak at ~50 MB. Per-call reset bounds the peak to the
 * largest single tool call instead.
 *
 * Why not simply reset `scratch` per tool call: scratch holds things that must
 * live for the whole turn -- the system message being sent, command/@-ref
 * expansion, and spawn_subagent's seed task and tool fence, which must survive
 * the nested agent run it starts (jc_tool_subagent.c).
 *
 * INVARIANT, and the one way to break this: no tool may hold tool-scratch data
 * across a NESTED agent run, because the nested run's own tool calls reset this
 * arena. Today's users all consume their bytes within a single call and none of
 * them spawn; a future tool that both reads onto this arena and spawns a
 * subagent must use jc_app_scratch instead. Falls back to scratch, then to
 * app->arena, so callers never receive NULL.
 * See docs/proposals/2026-07-robustness-edge-cases.md. */
struct jc_arena *jc_app_tool_scratch(struct jc_app *app);

/* Note: a top-level `load_skill` does not fence tools -- the skill's
 * `allowed-tools` is advisory (rendered as a hint, not enforced), because a
 * loaded skill is never "deactivated" so a fence set on load would linger for the
 * whole turn (docs/SKILLS.md "Design note"). A skill that declares
 * `restrict-tools: true` DOES fence tools, but only when run inside a sub-agent
 * (spawn_subagent skill=...), whose bounded lifetime sidesteps the linger problem;
 * there the skill's tools feed the same `allow_tools` / jc_tool_allowed path a
 * subagent profile fence uses. Top-level tool restriction still lives in subagent
 * profiles and modes/permissions. */

/* Switch the active model to index `i`: updates the config and recreates the
 * provider. Returns JC_ERR_INVALID on a bad index or provider failure. */
jc_status jc_app_switch_model(struct jc_app *app, int i);

/* Reachability-aware model selection. If model `idx` declares a `fallback` and
 * its server is unreachable, walk the fallback chain to the first reachable
 * model (probing endpoints once, cached on app->reach; logging the
 * substitution). Returns the effective index (idx when no fallback is set, no
 * server is reachable, or the build has no libcurl). */
int jc_app_effective_model(struct jc_app *app, int idx);

/* The first model declaring `role`, resolved through jc_app_effective_model.
 * NULL if no model declares the role. */
struct jc_model_cfg *jc_app_model_for_role(struct jc_app *app, unsigned role);

/* Tiered-routing switch: make model `model_idx` active if it isn't already,
 * logging "[route] -> <model> (<reason>)" (unless quiet) and, when an envelope
 * journal is active, emitting a `route` event. A no-op when already on that
 * model or `model_idx < 0`. */
jc_status jc_app_route_to(struct jc_app *app, int model_idx,
                          const char *reason);

/* --- Constraints (M110): captured + ENFORCED user limits ------------------ */

/* Load <cwd>/.jichi/constraints.md into app->constraints (replacing the current
 * set). Safe to call once at startup; no-op when the file is absent. */
void jc_app_constraints_load(struct jc_app *app);

/* Load the design/spec documents into app->design: the config `design: [...]`
 * list first, then each CLI --design/--spec in `paths`. Deduped by resolved
 * path, capped as a whole to JC_DESIGN_MAX. Safe to call again -- the TUI
 * `/design` command re-loads mid-session -- but note that doing so changes the
 * system prompt, and therefore the M31 cached prefix, so the next model call
 * re-bills it. See docs/DESIGN_INPUT.md. */
void jc_app_load_design(struct jc_app *app, const char *const *paths,
                        int npaths);

/* Add a constraint (deduped). Copies its strings onto app->arena and persists
 * the store. Returns 1 when newly added, 0 when a duplicate. */
int jc_app_constraint_add(struct jc_app *app, const struct jc_constraint *c);

/* Scan a user message for constraints and add the new ones (deduped + persisted).
 * Returns the number newly added. Used for AUTO auto-adopt + explicit surfaces. */
int jc_app_constraints_scan_adopt(struct jc_app *app, const char *msg);

/* Scan `msg` and adopt what it finds, choosing provenance (M169).
 *
 * `authored` = 0: the constraints were GUESSED from a prompt -- enforced for this
 * session, never written to .jichi/constraints.md. That is the default for the
 * agent loop's auto-adopt, because a keyword scan over prose is a guess, and a
 * guess must not govern every future run in the directory.
 * `authored` = 1: the operator asked for them explicitly (`/constraints add`) --
 * persisted like any hand-written rule.
 * jc_app_constraints_scan_adopt() is this with authored = 0. */
int jc_app_constraints_adopt(struct jc_app *app, const char *msg, int authored);

/* Persist app->constraints to <cwd>/.jichi/constraints.md. */
void jc_app_constraints_save(struct jc_app *app);

/* Drop all active constraints (and persist the empty store). */
void jc_app_constraints_clear(struct jc_app *app);

/* Gather config-benchmark facts (M113) from the live app/config into *f. */
struct jc_confbench_facts; /* jc_confbench.h */
void jc_app_confbench_facts(const struct jc_app *app,
                            struct jc_confbench_facts *f);

#ifdef __cplusplus
}
#endif
#endif /* JC_APP_H */
