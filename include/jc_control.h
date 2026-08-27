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
    int extend;       /* PAUSE (M162): credit the paused time back to the
                       * deadline ("stop the clock"); default 0 = the
                       * wall-clock-honest pause */
    int mode;         /* MODE (M304): the requested enum jc_agent_mode. Applied
                       * only when it NARROWS the current posture; a widening is
                       * refused, because the control channel never widens. */
};

/* --- pure codec (src/util/jc_control_proto.c) --------------------------- */

/* Parse one request line into *out (text arena-copied). JC_ERR_PARSE if not
 * a JSON object; an unknown/missing "type" yields JC_CTL_UNKNOWN (still
 * JC_OK); JC_ERR_INVALID if an INJECT carries no/empty "text". */
jc_status jc_control_parse_request(const char *line,
                                   struct jc_control_cmd *out,
                                   struct jc_arena *a);

/* Build a request line (client side; malloc'd, trailing newline included).
 * `text` is only used for "inject"; `extend` only for "pause" (M162). */
char *jc_control_build_request(const char *type, const char *text,
                               int extend);

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
};
char *jc_control_build_status(const struct jc_control_status *s);

/* Wire name <-> enum (never NULL for known types). */
const char *jc_control_type_name(enum jc_control_cmd_type t);
enum jc_control_cmd_type jc_control_type_parse(const char *s);

/* --- socket manager (src/chat/jc_control.c) ----------------------------- */

struct jc_app;     /* fwd */
struct jc_history; /* fwd */

struct jc_control {
    int listen_fd;            /* -1 when closed                            */
    char path[1024];          /* bound socket path (unlinked on close)     */
    int paused;
    int pause_extend;         /* M162: this pause credits the deadline     */
    long pause_started;       /* time(NULL) when the pause was accepted    */
    struct jc_sb pending;     /* queued inject text (newline-joined)       */
    char last_tool[64];       /* for the status snapshot                   */
    long started;             /* time(NULL) at open                        */
};

/* Bind + listen on `path` (parent dir created 0700; a stale socket file is
 * unlinked; the socket is chmod 0600; the listen fd is non-blocking). */
jc_status jc_control_open(struct jc_control *c, const char *path);
void jc_control_close(struct jc_control *c);

/* Resolve the default socket path (~/.jichi.d/control/<pid>.sock). */
void jc_control_default_path(char *out, jc_size cap);

/* The tool-boundary hook: serve any waiting commands (zero-timeout), honor a
 * pause (blocking in 500ms slices, re-serving commands, until resume/abort/
 * deadline), then fold any queued inject text into `hist` as one
 * "[operator] ..." user message. Returns 1 if text was injected, else 0.
 * No-op unless app->control is open and agent_depth == 0. */
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

#ifdef __cplusplus
}
#endif
#endif /* JC_CONTROL_H */
