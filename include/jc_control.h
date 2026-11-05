/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_control.h - the mid-run control channel (M159).
 *
 * A bounded `--auto` run could be observed (jsonl, journal) but not steered:
 * the only mid-run interventions were watching and SIGTERM. `--control [path]`
 * opens a per-run unix-domain socket (0600, under ~/.jichi.d/control/
 * by default) that a supervisor or human drives with one-line, versioned JSON
 * requests -- five verbs:
 *
 *   status   one read-only snapshot (elapsed, tokens/budget, tools, paused)
 *   inject   queue steering text; applied as ONE user-role message prefixed
 *            "[operator]" at the next tool boundary (never a system-prompt
 *            edit -- the M31 cached prefix stays byte-stable)
 *   pause    block the loop at the boundary until resume/abort; the deadline
 *            clock DELIBERATELY keeps running (wall-clock honesty)
 *   resume   wake a paused loop
 *   abort    set the abort flag -- the existing graceful Ctrl-C/SIGTERM path
 *
 * The loop listens only at TOOL-CALL BOUNDARIES (one zero-timeout select()
 * next to jc_bg_poll / mid-turn compaction): no threads, no reentrancy, and a
 * command can never interleave with a streaming response or a half-applied
 * tool. Nothing on this channel can WIDEN what the run may do: no approve
 * verb, no budget/scope changes (see docs/proposals/2026-07-control-channel.md
 * and docs/CONTROL.md).
 *
 * This header holds the PURE codec (unit-tested offline) + the socket manager
 * (src/chat/jc_control.c, select-based, E2E-tested). */

#ifndef JC_CONTROL_H
#define JC_CONTROL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"

/* The protocol version carried as "v" on every request/response. */
#define JC_CONTROL_PROTO_V 1
/* One request is one line; longer input is refused (resource bound). */
#define JC_CONTROL_LINE_MAX 16384

enum jc_control_cmd_type {
    JC_CTL_STATUS,
    JC_CTL_INJECT,
    JC_CTL_PAUSE,
    JC_CTL_RESUME,
    JC_CTL_ABORT,
    JC_CTL_MODE,      /* M304: NARROW the operating posture (never widen) */
    JC_CTL_UNKNOWN
};

struct jc_control_cmd {
    enum jc_control_cmd_type type;
    const char *text; /* INJECT: the steering text (arena-owned) */
    const char *from; /* INJECT: the sender (arena-owned, optional) */
    int extend;       /* PAUSE (M162): credit the paused time back to the
                       * deadline ("stop the clock"); default 0 = the
                       * wall-clock-honest pause */
    int mode;         /* MODE (M304): the requested enum jc_agent_mode. Applied
                       * only when it NARROWS the current posture; a widening is
                       * refused, because the control channel never widens. */
};

/* --- pure codec (src/util/jc_control_proto.c) --------------------------- */

/* Parse one request line into *out (text and from arena-copied). JC_ERR_PARSE
 * if not a JSON object; an unknown/missing "type" yields JC_CTL_UNKNOWN (still
 * JC_OK); JC_ERR_INVALID if an INJECT carries no/empty "text". */
jc_status jc_control_parse_request(const char *line,
                                   struct jc_control_cmd *out,
                                   struct jc_arena *a);

/* Build a request line (client side; malloc'd, trailing newline included).
 * `text` is only used for "inject"; `extend` only for "pause" (M162);
 * `from` is optional (inject only), defaulting to [operator]. */
char *jc_control_build_request(const char *type, const char *text,
                               int extend, const char *from);

/* Build response lines (server side; malloc'd, trailing newline included). */
char *jc_control_build_ok(const char *note);   /* {"v":1,"ok":true[,note]}   */
char *jc_control_build_err(const char *msg);   /* {"v":1,"ok":false,error}   */

/* The status snapshot. Strings may be NULL (omitted from the wire). */
struct jc_control_status {
    const char *run_id;
    long elapsed;          /* seconds since run start                     */
    double tokens_used;
    double budget_tokens;  /* 0 => unbounded                              */
    int tool_calls;
    int max_tool_calls;    /* 0 => unbounded                              */
    long deadline_secs;    /* 0 => none                                   */
    long deadline_credit;  /* M162: seconds credited by pause --extend    */
    const char *outcome;   /* outcome-so-far ("running", ...)             */
    const char *last_tool; /* last executed tool, "" if none yet          */
    const char *mode;      /* M304: the run's CURRENT posture. A supervisor
                            * asking what a run is doing needs to know whether it
                            * is still unattended -- and it is the only way to
                            * observe that a `mode` narrowing took effect rather
                            * than merely being acknowledged.                   */
    int paused;
    long injected;         /* M1045: total inject commands accepted this run.
                            * ALWAYS emitted on the wire, zero included -- 0 is
                            * the observable baseline a cold poller needs,
                            * unlike the other numeric fields' omit-when-zero
                            * convention.                                */
    long queued_injects;   /* M1045-b: notes still queued in the pending buffer
                            * at the moment of the snapshot (0 = none); one per
                            * accepted inject that has not folded yet -- a note's
                            * own newlines are not notes. ALWAYS emitted, zero
                            * included.                                   */
};
char *jc_control_build_status(const struct jc_control_status *s);

/* Wire name <-> enum (never NULL for known types). */
const char *jc_control_type_name(enum jc_control_cmd_type t);
enum jc_control_cmd_type jc_control_type_parse(const char *s);
/* M752: every verb the codec knows, in enum order, joined by `sep`
 * ("status|inject|pause|resume|abort|mode"). The lists typed by hand beside
 * the socket lost `mode` (M304) twice -- in `describe` and in the server's own
 * unknown-command reply -- so both now come from here. Truncates to `cap`. */
void jc_control_verb_list(char *buf, jc_size cap, const char *sep);

/* --- socket manager (src/chat/jc_control.c) ----------------------------- */

struct jc_app;     /* fwd */
struct jc_history; /* fwd */

struct jc_control {
    int listen_fd;            /* -1 when closed                            */
    char path[1024];          /* bound socket path (unlinked on close)     */
    int paused;
    int deferred_abort;       /* M801: an abort received during a tool call, to
                               * apply at the next boundary (the in-call poll must
                               * not kill_live a pool batch before P1 preserves) */
    int pause_extend;         /* M162: this pause credits the deadline     */
    long pause_started;       /* time(NULL) when the pause was accepted    */
    struct jc_sb pending;     /* queued inject text (newline-joined)       */
    struct jc_sb pending_from; /* queued inject senders (newline-joined)   */
    char last_tool[64];       /* for the status snapshot                   */
    long started;             /* time(NULL) at open                        */
    long injected;            /* M1045: total inject commands ACCEPTED by the
                               * server this run (the live view of the journal's
                               * cmd:inject events where a journal is active; the
                               * counter counts accepts even on a journal-less
                               * run)                                       */
    long pending_notes;       /* M1045-b: one per note currently queued in
                               * `pending` -- set at the two places that own the
                               * buffer (the inject accept and the fold), never
                               * by a second writer of `injected`. */
    long serve_pid;           /* SV:1848 (M1103): the socket's owner: only that
                               * process serves it. Set to getpid() at the end
                               * of jc_control_open. A FORKED pool child
                               * inherits the listening fd and the wrong pid; an
                               * IN-PROCESS subagent raises agent_depth in the
                               * owner and keeps the right pid. This is what the
                               * old agent_depth guard got wrong (SV:1848): it
                               * darkened the whole in-process child run. */
};

/* Bind + listen on `path` (parent dir created 0700; a stale socket file is
 * unlinked; the socket is chmod 0600; the listen fd is non-blocking).
 * On a build without sockets (JC_NO_SOCKETS, M835) it initialises `c` so that
 * jc_control_close is safe, and returns JC_ERR_IO without touching the disk. */
jc_status jc_control_open(struct jc_control *c, const char *path);

/* 1 if this build can open a control channel at all; 0 on a build without
 * sockets (JC_NO_SOCKETS, M835). Lets a caller say WHY --control is unavailable
 * instead of reporting a generic open failure. */
int jc_control_available(void);
void jc_control_close(struct jc_control *c);

/* Resolve the default socket path (~/.jichi.d/control/<pid>.sock). */
void jc_control_default_path(char *out, jc_size cap);

/* The tool-boundary hook: serve any waiting commands (zero-timeout), honor a
 * pause (blocking in 500ms slices, re-serving commands, until resume/abort/
 * deadline), then fold any queued inject text into `hist` as one
 * "[operator] ..." user message. Returns 1 if text was injected, else 0.
 * SV:1848 (M1103): no-op unless app->control is open and THIS process is the
 * socket's owner (serve_pid) -- a forked pool child must not steal the lead's
 * supervisor connection. Serving is depth-agnostic (an in-process subagent IS
 * the owner); the inject FOLD stays depth-0 only. */
int jc_control_boundary(struct jc_app *app, struct jc_history *hist);

/* M438: the SAME service point, poll-only -- called between the individual tool
 * calls of one round, where jc_control_boundary itself must not be.
 *
 * WHY A SEPARATE ENTRY POINT. A round with five tool calls used to yield ONE
 * service point, at its end. A single slow call (a build, a test suite) therefore
 * left `status` and `abort` unanswered for as long as that call took, against a
 * client wait hard-coded to 300s -- so a supervisor could not tell a long tool
 * call from a wedged process.
 *
 * The full boundary cannot simply move inside the per-call loop: it FOLDS queued
 * steering into history as a user-role message, and a user message landing between
 * two tool results of the same round makes the request malformed -- the contract
 * jc_history_check (M364) exists to catch. So this variant serves the socket and
 * honours a pause, and leaves any inject QUEUED for the round boundary, where it
 * folds in a well-formed position. status/pause/resume/abort are all answered
 * immediately; only inject waits, and it waits for a correctness reason.
 *
 * Returns nothing: by construction it cannot have folded steering. */
void jc_control_poll(struct jc_app *app);

/* M801: serve `status` (and queue every other verb to the next boundary) from
 * inside a long tool call or a spawn_parallel batch, so a supervisor is not left
 * waiting ~15 s for the call to end. Depth-guarded, non-blocking, abort-deferred.
 * M827 (N14): `in_pool` non-zero when called from the spawn_parallel pool, whose
 * loop drains a deferred abort and stops the batch ~immediately (work kept), so the
 * abort reply there says so rather than "queued to the next tool boundary". */
void jc_control_poll_during_call(struct jc_app *app, int in_pool);

/* M804 (L1b): apply a deferred abort now (set abort_flag, journal it), from inside
 * the parallel pool where killing the batch early is safe because P1 preserves each
 * worktree first. Returns 1 if an abort was pending. */
int jc_control_apply_deferred_abort(struct jc_app *app);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONTROL_H */
