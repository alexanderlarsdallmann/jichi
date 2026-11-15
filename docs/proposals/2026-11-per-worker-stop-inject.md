# Proposal: the per-worker stop and the per-worker inject (M1149, drive batch after H7)

**Status:** implemented (M1149, the harness-port drive of 2026-11-05; the code
batch landed in the H7+ batch, run 0023 — the red driver is
`tests/smoke/parallel_abnormal_endings.sh` cells 8-14, the unit test
`tests/test_control_stopinject.c`, and the describe surface its M1149-c
follow-up). The operator's decision (relayed in SV:1752,
`inbox-delivered/20261106-1752-sv-build-stop-and-inject-task.md`): "Build both,
the stop and the per-worker inject." The batch is of its own, lands after H7
(the H7 gate is in flight on 16819897) and before H8, because every later batch
is supervised through this channel (SV:1752; the re-order SV:1138 of run 0017).
Decision row: DD10 of the drive's `DECISIONS.md`. The code's red-first tests and
the live-model run come with the build.

## 1. The problem

A `spawn_parallel` batch runs its children as forked jichi processes. Today the
supervisor can read the whole run (`status`, the board lines) and end the whole
run (`abort`), but nothing reaches **one task**:

- **The stop.** A wedged or drifting task burns the batch's time and the shared
  budget while the supervisor's only levers are watching, or `kill <pid>` from
  outside the container — a PID hunt, with no reason recorded and no note the
  lead reads. The b2 incident (SV:1842, recorded in the H7 close entry) is the
  evidence: a stalled task, no per-worker stop, the batch waited on it, and the
  whole pool's merge was held.
- **The inject.** New information — "stop touching the parser", "the fixture is
  at a different path now" — has no route into a running child. The task text
  is fixed at fork; each child has only its child-to-parent result pipe
  (`wfd`, `src/tools/jc_tool_parallel.c`), so there is no parent-to-child
  channel at all.

What already exists and this proposal **reuses, never replaces**:

- **M854** (`tests/smoke/parallel_abnormal_endings.sh` checks 1-3) already
  handles a SIGTERM sent to one write child: the child answers with its last
  text plus "This task was stopped from outside (SIGTERM) after N tool call(s)";
  its work merges only after a green worktree verify, else it is kept as
  `parallel-unverified`; the run goes on. The stop verb sends exactly that
  SIGTERM — it adds no second ending path (SV:1752 supersedes SV:1750's wrong
  claim that `jichi control abort` was the only lever).
- **M159/M1045-b** already serve the lead's control socket at tool boundaries,
  and — **M801** (`jc_control_poll_during_call`, `jc_tool_parallel.c:1542`) —
  while a batch runs. The new verbs enter through that same socket, so a
  supervisor never needs a PID: the lead's run is the socket.
- **M304's rule holds:** the control channel only narrows. Neither verb
  changes a fence, a budget or a scope; both are recorded.

## 2. The verbs

Three verbs on the existing one-line JSON protocol (`include/jc_control.h`,
`v:1`): `status` (extended), `stop-task`, `inject-task`. The client is the
already-shipped `jichi control <sock> <verb>` (`src/main.c`, the M159 client).

### 2.1 `status` lists the live batch

The current reply is run-level only (elapsed, tokens, tools, paused, mode,
queued injects). While a batch is live the reply gains a `batch` object,
computed at read time from a per-task snapshot the pool publishes to the app
while it runs (the `ptask` array is local to the pool's gather loop, so the
status handler reads the snapshot, not the array):

```
"batch": {"total": 3, "done": 1, "tasks": [
  {"i": 0, "model": "coder-next", "pid": 4711, "live": true,
   "tool": "run_terminal_command", "tools": 41, "tokens": 81200,
   "idle_s": 3, "wt": ".jichi/parallel/…/t0"} ,
  {"i": 1, "live": false, "ending": "merged", "tools": 12, "tokens": 30410},
  ...]}
```

Every field already exists on `ptask`: index, `model_name`, `pid`,
`tool_calls`, `tokens`, `cur_tool`, `wt`, the M854 `ending`. Two additions:
`launch_ms` (the monotonic fork stamp, already there) is joined by a
`last_ms`, set whenever the parent parses a child's progress line (the existing
`{"t":"tool"}` / `{"t":"tok"}` lines), so `idle_s` (seconds since the child's
last progress) is the wedge indicator the board cannot give — the board shows
what a child did, `idle_s` shows that it stopped doing it. A batch that is not
live carries no `batch` field (the reply is byte-identical today outside one,
so every existing status consumer is untouched).

### 2.2 `stop-task N [reason]` — "stop task 2 (drifting on the parser)"

The parent sets a stop request for task N and the pool loop answers it at its
next wake: the child gets the SIGTERM **`kill_one` already sends**
(`jc_tool_parallel.c:1398`), grace `JC_PAR_TERM_GRACE_MS`, then reap — the
M854 path, no second ending. Then, and only then, the difference from a hand
`kill`:

1. **The reason.** The request carries an optional `reason`; the child's
   report line becomes "This task was stopped by the supervisor (SIGTERM):
   <reason> after N tool call(s)" — the lead's transcript names why, and the
   M885 "stopped from outside" class stays distinct (a supervisor stop is
   never "from outside"; it is journaled by the parent as `stop-task N
   reason=…`, telemetry-logged, and shown in the batch report).
2. **No PID hunting inside the container** (SV:1752): the verb is addressed by
   task index over the socket the lead already exposes.
3. **The record:** journal + telemetry + the lead's batch report all carry it.

Refusals, by name (the error is a value, the M159 convention): no live batch →
`error: no batch is live`; index ≥ total → `error: task 3 is not live (2
tasks)`. The run and the other tasks continue; merge/verify/keep are M854's
decision, unchanged.

### 2.3 `inject-task N <text>` — the per-worker inject

**The channel: a second pipe per child, created before the fork.**

SV:1752 lists three candidates. Decided:

- **A file in the child's private temp dir — rejected.** The temp dir
  (`worktree_tmpdir`, M852) exists only for WRITE children; a read-only task —
  the class a supervisor most often steers — has no writable private dir at
  all. And a file gives the child no *read point*: it would need a poll loop of
  its own, duplicating the socket's boundary discipline inside every child.
- **A per-child socket — rejected.** A unix socket the parent binds means the
  parent `bind()`s before the fork, and for WRITE children the M831 Landlock
  fence (applied post-fork, `apply_shell_fence`) covers the child's `write()`s
  to the socket's path — it would need a per-child grant, i.e. the fence gets
  wider for the steer it is supposed to be independent of. SV:1752's note that
  an *inherited* fd is not bound by the fence is exactly why the pipe wins.
- **The second pipe — chosen.** The pair is made with `pipe()` before the
  fork (the existing `wfd` is made the same way), so in the child it is an
  inherited readable fd — the M831 fence binds `open`/`bind`, not an inherited
  fd's `read`, and nothing widens. The child has no existing poll loop to
  extend (its model call is a blocking HTTP, and the 200 ms watchdog `select`
  is the **parent's** pool loop over the result pipes): the child reads the
  inject fd non-blocking at its own boundary — before each model call, the
  same discipline as the lead's `jc_control_poll` — and folds any queued text
  into its history there, so it reaches the **next model call** as one
  user-role message, exactly as the lead's M1045-b inject is folded. The
  child's transcript shows it, and no other task can receive it (the fd
  belongs to one child; the parent closes its write end when the task ends).
  The parent's pool loop writes each queued line to the fd at its 200 ms wake;
  the fd never competes with the result pipe, which the child writes and the
  parent reads as today.

The parent side is a bounded queue per task: `inject-task` appends the text to
task N's queue and replies `queued for task N's next model call` — the same
wording class as the lead's `inject`. The pool loop writes each queued line to
the child's fd at its wake (a 200 ms cadence already exists,
`jc_control_poll_during_call`'s select). **Bounded like the lead's queue**
(the M1045 cap on total queued steering text applies per task); over the cap
the verb refuses, `error: task N's queue is full`. Refusals by name: no live
batch, index out of range, a task already `done`.

What the verb does **not** do: it does not restart a child's model call mid
stream (the boundary rule of M159 — a steer can never interleave with a
streaming response), and it does not widen anything (M304).

## 3. Where it lands (files, for the build)

- `include/jc_control.h` + `src/util/jc_control_proto.c` + `src/chat/jc_control.c`:
  the two verb types, the request fields, the refusal strings, the `batch`
  object in the status reply.
- `src/tools/jc_tool_parallel.c`: the pool-side — `last_ms` on progress, the
  stop request the loop answers with `kill_one` + the reason note, the
  per-child inject queue + the second pipe + the child's read/fold.
- `src/main.c`: the `jichi control` client's two verbs (it already takes the
  socket + verb).
- Tests: an extended `tests/smoke/parallel_abnormal_endings.sh` (the mockmodel
  pattern it already uses) — see §4.
- Docs: `docs/CONTROL.md` (the operator manual gains the two verbs), this
  proposal's status line moves to implemented, `docs/DEFERRED.md` gains no row
  (nothing is deferred: all of SV:1752's four numbered requirements are built).

## 4. Tests, red first, per CHECK

The driver (red on the tree without the verbs, per the house rule — a stub
that answers `error: not wired yet` is red by construction, as H7's push
subcommands were):

1. **The stop, by effect.** A two-task mockmodel batch (task A sleeps on a
   model rule; task B answers). `stop-task 0 (fixture)`. Assert: the batch
   result carries task 0's line "stopped by the supervisor (SIGTERM):
   fixture", task 1's result is normal, the run's exit is normal. Red
   without the verb: the client refuses the unknown verb, task A keeps
   sleeping, the driver's assertion is `not ok`.
2. **The stop, refused by name.** `stop-task 5` in a two-task batch → the
   reply names task 5 as not live, no child is signalled, both results
   normal.
3. **The inject, by effect — the isolation cell.** A two-task batch;
   `inject-task 1 (marker)`. Assert: task 1's **next model request** carries
   the marker (`--dump-requests` pattern, the gate's own witness), and task 0's
   requests — all of them — never do. Red without the verb: the reply is the
   unknown-verb error and neither request carries it.
4. **The inject into a read-only task** (the case the temp-dir channel
   cannot serve): `inject-task` on a `readonly` child lands — pins the
   second-pipe decision, since the file channel has no dir there.
5. **`status`'s batch object:** live fields present (pid, idle_s, cur_tool),
   absent without a batch (byte-identical reply to today when no batch runs),
   and `idle_s` grows for a child whose model rule sleeps — the wedge
   indicator, not just the last-tool echo.
6. **The record cell:** the stop and the inject each appear in the journal and
   in telemetry (`stop-task 0 reason=fixture`; the inject's `queued`), and in
   the lead's batch report.
7. **The queue cap:** more queued text than the M1045 cap → the over-cap
   `inject-task` is refused by the full-queue error, the earlier queued text
   still lands.

And, per SV:1752 item 5, **ONE live run on a free local model** (a `jlu/*`
model, two real tasks, one stopped and one injected), recorded in the LOG with
the `--dump-requests` witness — the mockmodel cells prove the machinery; the
live cell proves the child's next request to the gateway really carries the
text.

## 5. Rejected

- **A second control socket per child.** Wider fence surface (§2.3), and a
  supervisor would then need to find N sockets instead of addressing N on one
  channel. The lead's socket is the channel; the index is the address.
- **The temp-dir file channel** (SV:1752's middle candidate). No read point
  without a child poll loop; no dir for read-only children at all.
- **A `stop-task` that SIGKILLs or that drops the child's work.** M854 decided
  the ending (SIGTERM → the child's own stop report → merge after a green
  verify, else kept `parallel-unverified`). A second ending path is exactly
  the mistake SV:1750 made, and "keep the work" is the drive's standing rule
  for dead workers.
- **Folding the inject into the child's system prompt.** The M31 cached prefix
  stays byte-stable; the lead's inject is a user-role message and the child's
  is too.
- **Extending `spawn_parallel`'s tool arguments with a pre-queued inject.**
  The whole point is that the information arrives *after* the dispatch.

## 6. Discard criteria (the proposal's own)

This proposal is discarded, and its decision row reversed, if: (a) the
second-pipe inject cannot be made to respect the M831 fence on a WRITE child
(then the socket's grant analysis was wrong and the channel is reconsidered,
not shipped); (b) the `idle_s` field proves uncomputable from the progress
lines (then §2.1 loses that field and the wedge detection stays with the
M1047 liveness rows); (c) the live run shows the marker in a non-target
task's request (isolation broken — the design fails, not the test).
