# The mid-run control channel (`--control`, M159)

A bounded `--auto` run could always be *observed* — `--output jsonl`, the
envelope journal, [`runs`](OBSERVABILITY.md) — but not *steered*: once it
started, your only interventions were watching and SIGTERM. The control
channel closes that gap. It is what turns an [autonomous loop](AUTONOMOUS_LOOPS.md)
from "unsupervised" into genuinely **semi-supervised**.

Design history: [proposals/2026-07-control-channel.md](proposals/2026-07-control-channel.md)
(the accepted design, including the alternatives considered). This page is the
operator manual.

**Not on every build.** The channel is a Unix socket, so a jichi built on a
system without sockets has none. Today that is DJGPP on FreeDOS, where
`make info` reports `SOCKET_LIBS = none`. There `--control` prints a warning
naming the reason and the run continues without a channel, and `jichi control`
refuses with exit 2 (M835).

## Quick start

```sh
# Start a bounded run with a control socket (off by default):
jichi --auto --control \
    --budget-tokens 400k --deadline 30m \
    -p "refactor the parser" < /dev/null &
# stderr prints: [control] listening on ~/.jichi.d/control/<pid>.sock

# From any other shell (same user), find it (M759) and steer it:
jichi ps                     # every live run, with its socket
jichi control ~/.jichi.d/control/<pid>.sock status
jichi control <sock> inject "skip the tests; write the report now"
jichi control <sock> pause            # deadline keeps running
jichi control <sock> pause --extend   # deadline clock stopped (M162)
jichi control <sock> resume
jichi control <sock> abort
```

`--control` takes an optional explicit path (`--control /run/jichi/task.sock`);
config `control: true` (or a path string) does the same per project (`1`, `"on"` and `"yes"`
mean true and `"off"` false since M843; before, an `"on"` or `"off"` was taken as a socket path). The
socket is unlinked at teardown.

## The six verbs

A program can read the same six, with their fields, from `jichi describe --output json`
under `control` (M752). That list is generated from the codec's own verb table, so it cannot
fall behind this one.

| Verb | Effect | Answered |
|---|---|---|
| `status` | one JSON snapshot: run id, **`mode`** (M304), elapsed, `tokens_used`/`budget_tokens`, `tool_calls`/`max_tool_calls`, `deadline_secs`, outcome-so-far, `last_tool`, `paused`, and the **inject delivery state** `injected`/`queued_injects` (M1045, always emitted) | at the next tool boundary |
| `inject <text>` | queued; lands as **one user-role message prefixed `[operator]`** before the next model call | ack at the boundary; applied at the next model call |
| `pause` | the loop blocks at the boundary until `resume`/`abort` — **the deadline clock keeps running** | at the boundary |
| `pause --extend` | as `pause`, but the paused time is **credited back to the deadline** on exit (M162) — for a deliberate human inspection that shouldn't burn the run's budgeted time | at the boundary |
| `resume` | wakes a paused loop (an `--extend` pause settles its credit here, journaled as `credited`) | immediately (served by the pause-wait) |
| `abort` | graceful stop — identical to Ctrl-C/SIGTERM (M80 keep-or-rollback semantics, exit 130) | at the boundary |
| `mode <chat\|plan>` | **narrows** the operating posture (M304) and persists past the turn | at the boundary |

```mermaid
sequenceDiagram
    participant Op as operator / supervisor
    participant CS as control socket
    participant L as agent loop
    participant M as model

    L->>M: request
    M-->>L: tool call (e.g. a long build)
    L->>L: execute tool
    Op->>CS: inject "wrap up and report now"
    Note over L: tool boundary: one zero-timeout poll
    L->>CS: serve queued commands
    CS-->>Op: {"ok":true,"note":"applied at next model call"}
    L->>M: next request (history now carries the [operator] message)
    M-->>L: final answer
```

## Semantics that matter (the accepted design decisions)

- **Boundary-served, with two mid-call exceptions.** Commands are served at
  **tool-call boundaries** — the same safe point where budgets are checked and
  background processes are reaped — so a command never interleaves with a
  streaming response or a half-applied tool, and there are no threads. Two verbs
  are now answered without waiting for the boundary, so a supervisor is not left
  waiting out a slow call: since **M801** `status` is answered **mid-call**, in
  the shell wait loop and inside a `spawn_parallel` batch; and since **M804**
  `abort` is served **inside a batch** (the pool stops it in ~200 ms, preserving
  each worker's work) and `pause` there means "launch no new task" while the
  running workers finish. Everything else still waits for the boundary, so behind
  a slow model call an `inject` or a `mode` reply can take as long as that call.
  The client waits up to 300 s (override with `JC_CONTROL_CLIENT_SECS`) and, if
  the reply does not come, exits **3** (delivered, queued) rather than 1 (not
  delivered), so the command's fate is scriptable; it is queued on the listen
  backlog either way. See the exit-code list below.
- **Injection is history, not prompt.** Steering lands as a normal
  **user-role message** with an explicit `[operator] ` prefix — visible to the
  model, preserved in the session transcript, compacted like any turn, and
  never touching the cached system prefix (M31 stays byte-stable). Multiple
  injects between boundaries are folded into one message.
- **Pause is honest about time — extending it is explicit.** `--deadline` is a
  wall-clock promise to the supervisor; a plain pause does **not** stop that
  clock, and if the deadline passes while paused the run resumes just to stop
  properly (budget semantics, M80). When a human inspection *shouldn't* burn
  the run's budgeted time, `pause --extend` (M162) freezes the deadline: the
  paused seconds are credited back (`deadline_credit` — visible live in
  `status`, settled and journaled as `credited` on resume/abort). The credit
  is the **only** thing that may stretch a deadline, it is always operator-
  initiated, and always on the books — a supervisor's outer timeout should
  still be generous enough to tolerate deliberate pauses.
- **Steering is auditable.** Every accepted command (except read-only
  `status`) is journaled — `{"event":"control","cmd":"inject","text":...}`,
  bounded and secret-scrubbed — and the [`runs`](OBSERVABILITY.md) reader
  surfaces it (M161): a steered run carries a `steered=N` note in the triage
  table and a `"steered": N` field in `--output json`, so a post-mortem can
  distinguish steered behavior from model-chosen behavior at a glance.

## Narrowing the posture (`mode`, M304)

```sh
jichi control ~/.jichi.d/control/1234.sock mode plan   # read-only from here
jichi control ~/.jichi.d/control/1234.sock mode chat   # ask before mutating
```

**One-way, by construction.** The permitted moves are exactly those that reduce
what the agent may do unattended:

```
auto  --->  chat  --->  plan
(widest)                (narrowest)
```

Anything else is refused with a reason. `auto` is widest because it approves every
permitted tool; `plan` is narrowest because nothing mutates at all.

Note that this ordering is **not** `enum jc_agent_mode`'s numeric order (it is
declared `CHAT, PLAN, AUTO`), so the decision lives in a pure, exhaustively tested
`jc_perm_mode_narrows` rather than a comparison — a `to > from` would have made
`auto → chat` look like a widening and got the safety property exactly backwards.

**It persists past the turn.** An operator who said "plan mode" meant it; silently
reverting at the next turn boundary would be the same class of surprise as a gate
that forgets. Journaled like every non-status command, so an audit shows when the
posture changed.

**Why a run you can only tighten is safe to expose:** a socket that could loosen a
running agent would be a privilege-escalation surface wearing a convenience hat.
Tightening needs no trust; loosening needs all of it. To widen, restart the run.

## What "at the next tool boundary" costs you in practice (measured M429)

Every verb above is served at a tool-call boundary, which is stated per-row in the
table. The consequence is worth stating separately, because it surprised the person
who wrote that table:

**A run with few tool calls, or one already past its last boundary, will not answer
at all.** Probed on a real run: the socket appeared in 2 seconds with the documented
`0600` permissions, the run made **four** tool calls in its first twenty seconds, and
a `status` sent after that **blocked for the client's full 300-second timeout** and was
never served — the run spent the rest of its life inside model calls and then exited.
The journal recorded no `control` event, correctly: nothing was ever served.

Two things follow.

- **`--control` suits long, tool-heavy runs** — the ones you actually want to steer. On
  a short or read-then-answer run, expect the channel to be unreachable in practice, and
  do not read a client timeout as a hung agent.
- **The client's error message — and its exit status — are the things to trust.** When the
  wait elapses with no reply it says *"no reply within the wait (the run may be inside a long
  model call; the command was delivered and is queued for the next tool boundary) [exit 3]"* and
  **exits 3**, which is distinct from the **1** a failed connect returns (M823). So a supervisor
  script can tell *delivered-and-queued, wait and re-check* from *the run is gone* without parsing
  the message. The client's exit codes: **0** the command was answered `ok`; **1** it was not
  delivered (connect/send failed) or the server answered `ok:false`; **2** a usage error (a bad
  verb or mode); **3** delivered but not answered within the wait (queued). The wait defaults to
  300 s and is overridable with **`JC_CONTROL_CLIENT_SECS`** (≥ 1) — lower it for a supervisor
  that polls and re-checks rather than blocking. `abort` still exits 130.

This is the same boundary-granularity property that makes `--deadline` a
notice-time rather than a wall-clock bound (see
[AUTONOMY.md](AUTONOMY.md)) — one design decision, two visible consequences.

### Two of those boundaries were added in M438

The measurement above stands, but two of its causes were avoidable and are now fixed:

- **Before the first request.** The socket is served once before each model call is
  built, so a supervisor that connects during startup or during the first round gets an
  answer instead of waiting out the round. This is a clean history point, so a queued
  `inject` reaches the request about to be built rather than the one after it.
- **Between the tool calls of one round.** A round with four calls used to answer
  `status` once, at the end — measured at 8 seconds' delay on a 12-second round, now
  about 1. This service point is **poll-only**: `status`, `pause`, `resume` and `abort`
  are answered immediately, while an `inject` stays queued for the round boundary. That
  is not a limitation to work around but a correctness requirement — folding a user-role
  message between two tool results of the same round makes the request malformed, which
  is exactly what `jc_history_check` (M364) exists to detect.

**What is still true:** the socket is not served *during* a model call, so a run that
spends its life inside model calls remains unreachable. Serving there would mean polling
from libcurl's progress callback, where a `pause` would block the HTTP read and could
time the request out. For liveness across that window, use `--heartbeat` (M165) or tail
the journal — which since M438 carries an `open` record with the run's pid from the
moment the file exists, so it is never an empty file while the process lives.

**Mid-subagent, since M1103.** An in-process `spawn_subagent` runs the child's turn in
the same process that owns the socket, so `status`, `pause` and `abort` are answered
at the child's service points while it runs (previously the run went dark for the whole
child -- measured 300 s of unanswered `status` on a 12-minute child). An `inject`
sent during the child is still queued for the **lead's** next top-level boundary, never
folded into the child's history. (The child's own long model call stays unserved, like
any model call; a child's shell tool call is a service point.) A `--daemon` run
refuses to open a control socket at startup (M1103-b): the daemon forks a worker per
prompt, and after M1103's owner-process rule no worker answers -- a bound socket
nobody serves is worse than none, so the combination is a startup refusal.

## What the channel deliberately cannot do

The socket must never *widen* what a run may do:

- **No widening `mode`.** `mode` only narrows (above); `plan → auto` is refused.
- **No `approve` verb.** Privileged commands (M153) and permission prompts
  cannot be answered remotely; that decision stays with an interactive human.
- **No budget, scope, or permission changes.** Steering can only narrow
  (stop, skip, report) — like hooks (M25), never grant.
- **No TCP, ever.** A unix socket, `0600`, in a `0700` directory: filesystem
  permissions are the whole ACL. Remote control = SSH to the host first,
  inheriting exactly the authentication story we want.

## The keyboard's version of the same thing

The TUI's **type-ahead queue** ([TYPE_AHEAD.md](TYPE_AHEAD.md), M254) reuses this
channel's semantics for the human at the keyboard: a line typed while the agent
works is applied at the same tool-call boundary, as the same single
`[operator]`-prefixed user message (both call `jc_history_add_operator`). One
convention for "text that arrived mid-turn", whether it came over a socket or off
a keyboard.

## With the autonomous loop

The reference supervisor ([AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md),
`examples/autonomous-loop/`) composes naturally: start each task with
`--control "$QUEUE/running/$base.sock"` and a watcher can nudge a drifting
task (`inject`), hold it for inspection (`pause`), or cut a loss early
(`abort` — the loop's exit-code routing treats 130 as an interruption and
requeues). `status` gives the supervisor a cheap progress probe without
parsing the jsonl stream.

## Verification

Pure codec unit tests (`tests/test_control.c`); the full channel is
E2E-driven (`tests/smoke/control.sh`): a mock-model run is steered live —
`status` and `inject` served at the boundary with the `[operator]` message
asserted inside the next model request, `pause`/`status(paused)`/`resume`,
and `abort` → exit 130.

See also: [AUTONOMY.md](AUTONOMY.md), [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md),
[OBSERVABILITY.md](OBSERVABILITY.md), [SCRIPTING.md](SCRIPTING.md),
[DAEMON.md](DAEMON.md).
