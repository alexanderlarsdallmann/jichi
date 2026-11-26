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
/* M1149: the still-bounded max for an inject-task REQUEST LINE. read_request
 * reads up to this, because a supervisor's steering text for one pool task may
 * legitimately exceed JC_CONTROL_LINE_MAX (driver cell 14 sends a ~70000-byte
 * request line). The other verbs' input stays under the old cap. This only
 * sizes the socket's line buffer; the parsed inject-task TEXT is a separate
 * bound (the per-task queue cap, JC_CONTROL_QUEUE_MAX, below). */
#define JC_CONTROL_LINE_MAX_INJECT (8 * JC_CONTROL_LINE_MAX)
/* M1149: the per-task inject queue cap -- the lead's M1045 cap (4 *
 * JC_CONTROL_LINE_MAX) applied to the TOTAL queued text per pool task,
 * newline-joined, mirroring the lead's CONTROL_PENDING_MAX. The codec knows it
 * only to distinguish an over-cap request text (JC_ERR_TOOBIG) from a valid
 * one; the pool applies the join-and-check against its own per-task queue. */
#define JC_CONTROL_QUEUE_MAX (4 * JC_CONTROL_LINE_MAX)
/* M1149: the pool's per-batch task ceiling, shared by the published snapshot
 * (this header) and the pool itself (jc_tool_parallel.c's JC_PAR_MAX_TASKS,
 * which must equal it). 32. */
#define JC_POOL_MAX_TASKS 32
/* M1149: the fixed fd the POOL's children inherit the per-child inject pipe
 * on (the parent keeps the write end and writes the task's queued text there
 * at its ~200 ms wake; the child reads it non-blocking at the boundary).
 * Chosen 912: far from 0/1/2 and the small-fd range the agent loop, the
 * eventlog/transcript FILE*s, the shell and jichi's own sockets open; the
 * child's only other inherited fds at the dup2 are the two pipe ends made next
 * to the fork (low numbers, one closed by the child before the loop). In the
 * LEAD the fd is never opened, so the number is a dead constant there. The
 * M831 Landlock fence binds open/bind, not the read of an inherited fd, so
 * nothing widens for the steer (behaviour 6). */
#define JC_CTL_INJECT_FD 912

enum jc_control_cmd_type {
    JC_CTL_STATUS,
    JC_CTL_INJECT,
    JC_CTL_PAUSE,
    JC_CTL_RESUME,
    JC_CTL_ABORT,
    JC_CTL_MODE,      /* M304: NARROW the operating posture (never widen) */
    /* M1149: the per-worker verbs, BETWEEN mode and UNKNOWN so the codec's
     * verb list (and the server's unknown-command reply, M752) gains both in
     * enum order: "stop-task", "inject-task". */
    JC_CTL_STOP_TASK,
    JC_CTL_INJECT_TASK,
    /* M1192 B3: cancel the CURRENT command (lead, or one pool task). Last in
     * the enum, just before UNKNOWN (D4), so the codec's verb list gains it
     * as the ninth name. */
    JC_CTL_CANCEL_CMD,
    JC_CTL_UNKNOWN
};

struct jc_control_cmd {
    enum jc_control_cmd_type type;
    const char *text; /* INJECT / INJECT_TASK: the steering text (arena-owned) */
    const char *from; /* INJECT: the sender (arena-owned, optional) */
    int extend;       /* PAUSE (M162): credit the paused time back to the
                       * deadline ("stop the clock"); default 0 = the
                       * wall-clock-honest pause */
    int mode;         /* MODE (M304): the requested enum jc_agent_mode. Applied
                       * only when it NARROWS the current posture; a widening is
                       * refused, because the control channel never widens. */
    int task;         /* M1149: STOP_TASK/INJECT_TASK -- the 0-based pool index
                       * (the target child). Validity is pool-checked.
                       * M1192 B3: CANCEL_TASK -- the target, or -1 (task ABSENT)
                       * meaning the LEAD's own current command. */
    const char *reason; /* M1149: STOP_TASK -- the optional reason (arena-owned) */
};

/* --- M1149: the pool hook (the shared interface) --------------------------
 * The spawn_parallel pool REGISTERS these ops on app->control (and a snapshot
 * pointer) while a batch is live, and NULLs them when it ends. The handler
 * (serve_pending) calls them for the two per-worker verbs and reads the
 * snapshot for `status`; when the hook is NULL (no live batch) the verbs are
 * refused with the EXACT string "no batch is live". The pool owns ptask; the
 * snapshot is the copy the handler reads (behaviour 11). */

/* One published task of the live batch (the fields `status`'s "batch" object
 * carries). Strings may be NULL (omitted from the wire). */
struct jc_pool_task_snap {
    int    i;          /* the 0-based pool index */
    int    live;       /* 1 = a child is running, 0 = ended (or never launched) */
    const char *model;/* resolved model name, or NULL */
    long   pid;
    const char *tool;  /* cur_tool, "" if none yet */
    int    tools;      /* tool calls so far */
    double tokens;
    double idle_s;     /* seconds since the child's last progress line (behaviour 12) */
    const char *wt;    /* worktree path, "" if none */
    const char *ending;/* how the task ended, or NULL while live */
    int in_cmd;        /* M1192 B3: 1 while the child is INSIDE a shell command
                        * (the child's tool progress line carries in_cmd), 0
                        * otherwise; the cancel-cmd worker form refuses a task
                        * that is live but not in a command. */
    /* def-377 W2.1/W2.2 (M1194): the child's progress signals, published for
     * the supervisor. edits/placeholder/compaction/dup24 are the LATEST sig
     * per task (A2's store + A3's placeholders/compactions); since_edit_s is
     * computed HERE by the PARENT from sig_edits_ms (never on the wire); flag
     * is 0/1 (W2.3/W3.2). All default to 0/absent for a task with no sig yet. */
    int    edits;         /* W2.2: successful edit-tool calls so far (sig) */
    double since_edit_s;  /* W2.2: parent-computed s since the last edits change */
    int    placeholders;  /* W2.2: total copied placeholders (sig) */
    int    compactions;   /* W2.2: total compact events (sig) */
    double dup24;         /* W2.2: near-dup share of the last 24 calls (sig) */
    int    flag;          /* W2.2/W2.3: 1 = the flag fired, 0 = not yet */
};

/* The published batch snapshot, refreshed by the pool every loop iteration. */
struct jc_pool_snap {
    int total;
    int done;
    struct jc_pool_task_snap tasks[JC_POOL_MAX_TASKS];
};

/* M1149: the pool's ops. stop/inject return JC_OK when the action is accepted
 * (the pool acts on stop at its next wake) or JC_ERR_INVALID with the refusal
 * string in *why (the handler replies with it verbatim). The pool owns the
 * per-task state; the handler only calls and reads. */
struct jc_pool_ops {
    jc_status (*stop)(int task, const char *reason, char *why, jc_size whycap);
    jc_status (*inject)(int task, const char *text, char *why, jc_size whycap);
    /* M1192 B3: cancel the CURRENT command of one pool task (SIGUSR1 -> the
     * child's abort flag -> its watched command's cancel kill-arm). Same
     * refusal contract as stop/inject (the pool fills *why, the handler
     * replies with it verbatim); NULL when the pool did not register it. */
    jc_status (*cancel)(int task, const char *reason, char *why, jc_size whycap);
    const struct jc_pool_snap *snap; /* the published snapshot, or NULL */
};

/* --- pure codec (src/util/jc_control_proto.c) --------------------------- */

/* Parse one request line into *out (text, from and reason arena-copied).
 * JC_ERR_PARSE if not a JSON object; an unknown/missing "type" yields
 * JC_CTL_UNKNOWN (still JC_OK); JC_ERR_INVALID if an INJECT carries no/empty
 * "text", or a STOP_TASK/INJECT_TASK carries a missing/negative/non-integer
 * "task" (or INJECT_TASK a missing/empty "text"); JC_ERR_TOOBIG if an
 * INJECT_TASK's "text" ALONE exceeds the per-task queue cap
 * (JC_CONTROL_QUEUE_MAX) -- the handler maps that to the "queue is full"
 * refusal rather than the pool storing an over-cap line. */
jc_status jc_control_parse_request(const char *line,
                                   struct jc_control_cmd *out,
                                   struct jc_arena *a);

/* Build a request line (client side; malloc'd, trailing newline included).
 * `text` is only used for "inject"; `extend` only for "pause" (M162);
 * `from` is optional (inject only), defaulting to [operator]. */
char *jc_control_build_request(const char *type, const char *text,
                               int extend, const char *from);

/* M1149: build a per-worker request line (client side; malloc'd, trailing
 * newline included). `type` is "stop-task" or "inject-task";
 * `reason_or_text` is the stop's optional reason (""/NULL omitted) or the
 * inject's required text. */
char *jc_control_build_task_request(const char *type, int task,
                                    const char *reason_or_text);

/* M1192 B3: build a cancel-cmd request (client side; malloc'd, trailing
 * newline included). `task` is -1 for the LEAD form (the "task" field is
 * omitted) or a >= 0 pool index for the worker form; `reason` is optional
 * (NULL/"" omitted). */
char *jc_control_build_cancel_request(int task, const char *reason);

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
    const struct jc_pool_snap *batch; /* M1149: the live pool's snapshot, or
                            * NULL. Emitted as the "batch" object ONLY when
                            * non-NULL (a batch is live); with no live batch the
                            * reply is byte-identical to today (no "batch" key). */
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
    struct jc_pool_ops *pool_ops; /* M1149: the live spawn_parallel pool's hook
                               * (its stop/inject ops + published snapshot), or
                               * NULL when no batch is live. The pool registers
                               * it while it runs and NULLs it at the end; the
                               * handler reads it for the two per-worker verbs
                               * and `status`'s "batch" object. The ops struct
                               * lives in the POOL, owned by the parent process;
                               * a forked child inherits a COW copy but never
                               * serves (serve_pid), so its copy is dead. */
    struct jc_app *app;         /* M1192 cell 4: the owning app, recorded by
                               * serve_pending. jc_control_close uses it for its
                               * final CLOSING-boundary drain (a supervisor that
                               * connects during teardown), where the app is still
                               * valid. NULL on a NO_SOCKETS init or before the
                               * first serve. */
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
