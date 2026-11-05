# Preserve agent work, liveness, and redaction — implementation plan (v2)

*A working plan **and** a teaching document. Written to be followed by a junior developer or a
self-learner: every milestone says what it is, **why** it exists (the real incident behind it), the
design decision and the alternatives weighed, the exact place in the code, the change, and the
red-first test with how to run it. v2 folds in threadwork's review of v1
(`docs/internal/2026-09-27-impl-plan-comments.md`). Where v1 was wrong, this says so plainly —
getting a touchpoint wrong on paper is cheap; the point of review is to catch it here.*

*Sources: the design plan `docs/plans/2026-09-preserve-agent-work.md`; the handoff
`docs/internal/2026-09-27-preserve-agent-work-handoff.md`; threadwork's comments
`docs/internal/2026-09-27-impl-plan-comments.md`; the review
`docs/analysis/2026-09-27-preserve-agent-work-review.md`. Status: **PLAN, not yet implemented.***

---

## Part A — What you need to know first (concepts)

If you have not worked in jichi's internals, read this once. Every term below appears later.

- **jichi** is an AI coding agent written in C89. A *turn* is one round of: send the conversation to
  a model, get back text and/or tool calls, run the tools, repeat until the model gives a final
  answer. `make ci` (~20 min) is **the gate**: nothing merges to `master` that it has not passed.
- **Lead, child, pool.** The top-level agent is the **lead**. It can run several **child** agents at
  once with the `spawn_parallel` tool; the code that launches and reaps them is the **pool**
  (`src/tools/jc_tool_parallel.c`). Each child runs in its own **git worktree** — a separate working
  directory backed by the same repository — so two children editing the same file do not collide.
- **agent_depth.** `0` means "this is the lead"; `> 0` means "this is a child." jichi fences
  behaviour on depth: e.g. a child must not answer the supervisor's control socket (Part B, L1).
- **The shadow repository.** jichi keeps its own private git repo under `~/.jichi.d`, separate from
  your project's git. Snapshots, discarded states (`refs/jichi/discarded/…`), and the worktrees live
  there. Preserving work means committing into the shadow repo, never into your repo.
- **The control socket.** A supervising process connects to a Unix socket to ask the running agent
  for `status`, or to `pause`, `abort`, or `inject` a message. It is served by
  `src/chat/jc_control.c`. **Today it is served only between tool calls** — that is the liveness
  problem in Part B.
- **The redactor.** `jc_redact_secrets` (`src/util/jc_log.c`) replaces **registered** secret values
  (the model API keys, the search key, `@`-reference values) with `***`. It matches *values it was
  given*, not patterns — so it cannot catch a secret it was never told about (this limit matters in
  M-S).
- **SSE deltas.** The model streams its reply as many small chunks ("deltas") over Server-Sent
  Events; jichi assembles them into the final text. A secret the model emits can be **split across
  two deltas** (matters in M-S).
- **curl's low-speed window.** libcurl can abort a transfer that receives fewer than N bytes/sec for
  T seconds (`CURLOPT_LOW_SPEED_LIMIT` / `_TIME`). jichi's "stall" timeout uses this. **A handle has
  exactly one such window** — you cannot set two (matters in M-L2).
- **Red-first.** Before writing a fix, run a test that *fails* because the fix is missing, and watch
  it fail. A test never seen failing has never been shown to work. threadwork has already written the
  red drivers (`docs/internal/drives/2026-09-26-zigodot/agentic/e1/`); each one reproduces a real
  defect from the weekend drive.

---

## Part B — The order of work, and why it is this order

There are three groups: **attribution and liveness** (small, unblock the lead), the **preservation
line** (P1–P6, the original plan), and the **security fix** (redaction). v1 ordered them A, L1, S,
L2, P1…. v2 changes the order for one hard reason, then keeps the rest.

**The hard reason (threadwork §1.1): abort must not come before preserve.** Today, an `abort` inside
the pool runs `kill_live()` then the teardown removes *every* child worktree, merged or not
(`jc_tool_parallel.c:565-568` then `:1035`). So if we teach the pool to serve `abort` *during* a
batch (part of L1) before P1 can preserve worktrees, an operator's abort would **destroy all
unmerged child work** — the exact loss the whole project exists to prevent. Therefore:

- **L1 is split.** *L1a* (serve `status` during long calls) is harmless and goes early. *L1b* (serve
  `abort` during a batch) waits until **P1** has landed, so an abort preserves before it kills.

The landing order (each milestone takes the next free number from **M800** when it lands, per the
project rule; the numbers below are provisional and run in landing order):

| # | milestone | provisional M# | v1's label (for cross-ref) | needs a decision? |
|---|---|---|---|---|
| 1 | **A** — telemetry names the child | M800 | M800 | no |
| 2 | **L1a** — serve `status` during long tool calls | M801 | part of M801 | no |
| 3 | **S** — redact the session and the jsonl stream | M802 | M802 | decided: redact |
| 4 | **P1** — preserve at the worktree chokepoint | M803 | M804 | default after measuring |
| 5 | **L1b** — serve `abort`/`pause` in the pool | M804 | part of M801 | no |
| 6 | **L2** — a dead-stream bound that stays on | M805 | M803 | decided: fence, 900 s |
| 7 | **P4a** — measure free disk per tool call | M806 | M805 | no |
| 8 | **P2** — end records + transcripts + loop fix | M807 | M806 | no |
| 9 | **P3** — salvage orphaned worktrees | M808 | M807 | no |
| 10 | **P4b** — the disk guard | M809 | M808 | floors after P4a |
| 11 | **P5** — `attempts show` and `from:` resume | M810 | M809 | no |
| 12 | **P6** — WIP commits (optional) | M811 | M810 | only if measured |
| — | **O8/O9/O10** — three more drive defects | deferred | not in v1 | see Part D |

> **Note on numbering vs. threadwork's comments.** threadwork's review uses v1's labels (there
> M804 = P1, M801 = L1). This plan renumbers by landing order because P1 must precede the abort half
> of L1. The column above maps the two so cross-references still resolve.

The two settled operator decisions (2026-09-27): **sessions are redacted** (M-S), and the
**dead-stream window is a fence at 900 s that stays on** even in measurement runs (M-L2, which also
amends CLAUDE.md's caps-versus-fences rule).

---

## Part C — The milestones

Each has: **Why · Decision · Where · Change · Test · Escape route.**

### M800 — A: telemetry names the child

- **Why.** In the weekend drive, two children ran at once, and *every* depth-1 telemetry row carried
  the **lead's** session id, run, turn and model name. You cannot tell two concurrent children
  apart, and P4a (below) measures disk from telemetry — so bad attribution poisons that measurement
  too. (Driver `e1/e1_s7_attrib.sh` shows 4 of 4 child `model_call` rows naming the lead's model;
  both children emit `seq` 2,3,4.)
- **Decision.** Every record from a child carries `task` (its index in the batch), `task_run`, its
  **own** model name and id, and its **own** `seq` counter space. Readers (`insights`,
  `learn analyze`) must tolerate old rows without these fields — default them, never assume presence.
- **Where.** The child's event-log context (spawned in `jc_tool_parallel.c`) and the emit site
  (`src/util/jc_eventlog.c`). **v1 was imprecise; threadwork's key correction:** take the model name
  **from the request that was actually made**, *not* from `app->config.model` — the child's config
  mirror still holds the lead's model. Give each fork its own `seq` space (it continues the parent's
  counter from the fork point).
- **Change.** (1) When the pool forks a child, stamp its event context with `task`/`task_run` and its
  chosen model id. (2) At the model-call emit, read the model from the request object. (3) On the
  read side, version the schema so old rows parse.
- **Test.** `e1/e1_s7_attrib.sh` (red today): two concurrent mock children → distinct `task` values
  and each child's own model; no row carries the lead's model for a child's call. Plan T7.
- **Escape route.** If a reader (`insights`) breaks on old telemetry, that is the "assume presence"
  bug — add the default and re-run the reader over a pre-M800 corpus.

### M801 — L1a: serve `status` while the lead is busy

- **Why.** A supervisor could not reach the lead during any long tool call. Measured: `status`
  answered in 0–1 s between quick calls, but **15 s** into a 20 s shell call and 15 s into a 20 s
  `spawn_parallel` batch — because the control socket is served only at boundaries. (Driver
  `e1/e1_s1_reach.sh`.)
- **Decision.** Serve **`status` only** (poll-only, non-destructive) inside the two long-wait loops:
  the shell tool's wait and the pool's `select`. `status` gains a `children[]` array. **`abort` and
  `pause` are deferred to M804** (see Part B's hard reason, and the `pause`-starves-pipes hazard
  below). `inject` stays queued to the round boundary (that is by design, M364).
- **Where — this is v1's biggest correction.** The shell half is **not** in
  `src/tools/jc_tool_run.c`. The shell tool calls `jc_app_run_command_ex`
  (`jc_tool_run.c:118`), whose wait lives in `src/chat/jc_app.c` and already has two `select` paths:
  - **watched fork** (used when a `runTimeout` or memory budget is set — the drive's
    `runTimeout: 900` forces it): `select` with a 300 ms tick, then `jc_app_tick(app)` when idle
    (`jc_app.c:672-673`). Add one poll-only control check in the idle branch (or inside
    `jc_app_tick`, `:767`).
  - **popen** (used when neither is set): it only uses a finite (200 ms) timeout when a progress
    callback is present (`:904`, `:916`); otherwise `select` blocks forever (`:918`). Add the poll
    **and** give it a finite timeout whenever a control socket is active — otherwise the loop never
    wakes to serve `status`.
  The pool half is the `select` at `jc_tool_parallel.c:563`.
- **The `pause`-in-the-pool hazard (why it is not here).** If `pause` blocked inside the pool's
  `select` loop, the parent would stop reading the children's output pipes; the children would then
  block on full pipes while their per-child watchdog clock keeps running — you would starve and then
  kill the very work you paused. So in the pool, `pause` will either queue to the next round boundary
  or mean "launch no new task"; M804 will state which. (This is why L1a serves *status only*.)
- **The depth guard (keep it).** Forked children inherit the lead's `app->control`, including the
  listening socket's descriptor. `jc_control_boundary` already returns immediately when
  `agent_depth != 0` (`src/chat/jc_control.c:410`). The new poll **must** keep that guard: a child
  that answered the socket would steal the lead's supervisor connection.
- **Change.** Add a poll-only control check (reusing `jc_control`'s dispatch, so behaviour matches
  the between-calls path) in: the `jc_app.c` idle branch (both wait paths), and the pool `select`.
  Behind the depth guard. Give popen a finite timeout when control is active.
- **Test.** `e1/e1_s1_reach.sh` (red): `status` answered ≤3 s during a 20 s shell call and a 20 s
  batch. **Plus a byte-exact test** that a normal command's captured output is unchanged — the poll
  reads the control socket, never the command's pipe, and the idle branch runs only after 200–300 ms
  of silence, so caution 2 holds *by construction*; test it anyway (the console/PTY rigs assert exact
  bytes).
- **Escape route.** If a normal command's output changes by one byte, the poll touched the wrong fd —
  revert and check you added the control fd to the `select` set, not the child's pipe.

### M802 — S: redact the session writer and the jsonl stream (security)

- **Why (class only; mechanism is internal).** Some on-disk sinks that store tool output do **not**
  pass it through the redactor, so a tool that prints a **registered** secret (e.g. a command that
  dumps an environment) can write that secret verbatim into a record that is later pushed. This is an
  **open security finding**; per `SECURITY.md` and `docs/internal/README.md`, the exact sinks, source
  lines, and the reproduction stay in the internal note
  (`docs/internal/2026-09-27-tool-output-secrets-unredacted.md`) until the fix lands, and this public
  plan names only the class. *(v1 spelled the mechanism out in this public file — corrected here, per
  threadwork §1.2; the detail returns to this plan with M802's commit.)*
- **Decision (operator-settled 2026-09-27).** **Redact.** The model has already seen the value, so
  redacting the on-disk record loses nothing it needs on resume. Route the session writer and the
  jsonl stream through the redactor, following the established sink pattern (check
  `jc_redact_active()`, copy `strlen+1` because the redactor never lengthens, then
  `jc_redact_secrets`).
- **Honest limit (state it in code and docs).** The redactor is **value-based**: it redacts secrets
  it was *registered with*. A secret the model prints that jichi never registered
  (`cat ~/.aws/credentials`) is **not** caught by this or any sink. So M802 closes the registered-key
  leak; the container boundary and the harness's pre-push key scan remain the real backstop. Do not
  imply "sessions are now safe."
- **Two subtleties threadwork surfaced (both need handling):**
  1. **A secret split across two SSE deltas** escapes per-delta redaction. Fix: at each delta
     boundary, hold back up to *(longest registered secret length − 1)* bytes, and flush the
     remainder at the message's end. Test with the mock's `sse-file` action, which sends a verbatim
     SSE body (`tests/tools/mm_core.h:53`), splitting the canary across two deltas.
  2. **Sessions store more than tool results:** also the assistant messages' **tool-call arguments**
     and the **todos**. Redact those too. (A session is one JSON line, so the driver's line-count
     check, once green, proves *every* occurrence is gone.)
- **Test.** threadwork's `e1/e1_r3_redact.sh` — checks 6 (stream) and 7 (session) are red today and
  must go green; checks 1–5 are the **instrument** (they prove a session/stream/telemetry row was
  actually produced). Perturb per sink: remove the redaction call from each sink in turn and watch
  *that* sink's check go red. *(Note to self: my earlier "ad-hoc run didn't fire checks 1/2/5" was
  the instrument doing its job — the scenario had not run. Run each driver exactly as
  `e1-results.md` says: copy it into `tests/smoke/` of a checkout with `make smoke-tools` built, then
  from the root run `E1_RES=<dir> JC_SMOKE_BIN=<jichi> sh tests/smoke/e1_r3_redact.sh`.)*
- **Escape route.** If check 4 (telemetry redaction, already green) breaks, you changed the shared
  redactor, not just a sink — back it out.

### M803 — P1: preserve at the worktree chokepoint

- **Why.** Four code paths destroy child/attempt worktrees, and none preserves. In the drive, a
  child that made 165 tool calls over three hours had its work deleted at teardown. (The plan's §3
  G1; drivers `e1/e1_s5_merge.sh` check 5: the child's own commit exists as a git object but no ref
  reaches it, so the next gc deletes it.)
- **Decision.** Preserve **inside** the one chokepoint every path already calls,
  `jc_snapshot_worktree_remove`. Change its signature to require a `why` and a `record`, so the
  **compiler enumerates every caller** (a call site can be forgotten; a signature cannot). The only
  skip is a child merged in full (`JC_WT_MERGED`). **When preservation itself fails** (e.g. disk
  full), **keep the tree** and report it — never let housekeeping destroy the only copy. A `T5` lint
  pins the caller set + every `rm -rf` that can reach a worktree.
- **Where.** `jc_snapshot_worktree_remove` (`src/snapshot/jc_snapshot.c:868`) and its four callers
  (`jc_tool_parallel.c:1038`, `src/main.c:10244/10628/11009`). The two `rm -rf`-of-base-dir sites
  (`main.c:10272`, `:10631`) and the pool's `rmdir(wtbase)` (`:1042`) must become kept-tree-aware,
  or they delete what the chokepoint just kept.
- **Change.** Preserve with git plumbing (`add -A`, `write-tree`, `commit-tree -p HEAD`,
  `update-ref` under `refs/jichi/discarded/<why>/…`); a file over `preserveMaxFileMB` (default 16) is
  left out and named in the record. **Measure the packed cost on a Zig tree** and record it (it sets
  the §4.5 default; M338's 5 KB was measured on a tiny tree).
- **Test.** Plan T1–T5. T5 is the enumerating lint (floor: 4 callers + 4 `rm -rf` sites).
- **Escape route.** If a worktree with a build cache balloons the shadow repo, the `preserveMaxFileMB`
  cap is not catching build output — check `.gitignore` is honoured by `git add -A` in the worktree.

### M804 — L1b: serve `abort` (and define `pause`) in the pool

- **Why / ordering.** Now that P1 preserves at teardown, an `abort` mid-batch preserves each child's
  work *before* `kill_live` and the worktree removal. This is the half of L1 that was unsafe before
  P1.
- **Decision.** Serve `abort` inside the pool's `select` loop, going through the same preserve-then-
  remove path as a normal teardown (reason `parallel-interrupted`). Define `pause` in the pool as
  **"launch no new task"** (not "freeze mid-task", which starves pipes — see M801's hazard) *or*
  queue it to the round boundary; state the choice in the commit. `status`/`children[]` from M801a
  already work here.
- **Where.** `jc_tool_parallel.c:565-568` (today: `abort_flag` → `kill_live` → return). Insert
  preserve-each-worktree before `kill_live`.
- **Note (for the drive, not this milestone).** SIGINT/Ctrl-C already reaches this abort path today
  (the pool checks `abort_flag` every 200 ms), and the Monday 10:00 harness shutdown sends SIGINT
  (`loop.sh:98`). On the *pinned* drive binary that means lost unmerged work — threadwork/the operator
  handle that before 10:00; it is the first real case M803 would have preserved.
- **Test.** Extend `e1/e1_s1_reach.sh` (or a new arm): an abort mid-batch leaves a
  `parallel-interrupted` kept state per unmerged child (recoverable), and the answered child's merge
  is intact.
- **Escape route.** If abort still loses work, P1's preserve is not being called before `kill_live` —
  the insertion order is the bug.

### M805 — L2: a dead-stream bound that stays on

- **Why.** With caps off (the drive ran `stall 0`), nothing bounded a model call whose stream died:
  one call ran **11,677 s**. Healthy calls: max 540 s over 12,464 (driver `e1/e1_s2_dead.sh`;
  latencies in `e1-results.md`).
- **Decision (operator-settled).** A **fence** (stays on even in measurement runs — it can only fire
  when *nothing* is arriving, the property the caps rule already grants `connect`), default **900 s**,
  above the 540 s healthy max. **This milestone amends CLAUDE.md's caps-versus-fences rule** to list
  the dead-stream window beside `connect`.
- **Where — v1's other correction.** The stall is set at `jc_http.c:754-755`
  (`CURLOPT_LOW_SPEED_LIMIT`/`_TIME`), and `:232-235` is only its *error message*. Because a curl
  handle has **one** low-speed window, the dead-stream window and `stall` are **one setting**, not two
  timers as v1 assumed:
  - `LOW_SPEED_TIME = stall > 0 ? min(stall, dead) : dead;`
  - the error message names whichever knob set the bound (the M325 "name the knob" idiom).
- **Change.** Add a `deadStreamSecs` config (default 900), compute the low-speed time as above, and
  extend the error string.
- **Test.** `e1/e1_s2_dead.sh` OFF arm — but its outer deadline is 45 s, so with the 900 s default the
  fixed binary is still "red." **The test must set the new knob small (e.g. 5 s) in that arm's
  config.** Add a unit test pinning that the default is 900 s and applies when `stall` is 0.
- **Escape route.** If healthy calls start dying, the window is below the real tail — re-measure the
  tail for the current model roster before lowering it.

### M806 — P4a: measure free disk per tool call

- A telemetry field only (free space + free files at each tool boundary), gathered over one drive to
  set M809's floors. Depends on **M800** (clean attribution). No guard, no decision. It exists
  because the plan's rule is *measure the population before building the gate*.

### M807 — P2: end records, child transcripts, and the loop fix (R6)

- **Why.** A successor agent needs to know not just *what* a predecessor changed (that is already in
  the worktree) but *why* it stopped. And jichi's loop detector missed the drive's 3-hour loop.
- **Decision.**
  - **Transcript = the reasoning record.** Each child appends one JSONL line per model reply and per
    tool call, flushed per line (survives SIGKILL), `0600` in `0700`, honouring `--no-session`, gc'd
    with its ref. **Born redacted** (M802's redactor) — it is a third sink of tool output.
  - **End record** per plan §4.2; P3's salvage builds it from the transcript tail when the parent
    died.
  - **Loop fix (R6).** Reuse jichi's existing detector (`jc_toolloop`,
    `JC_TOOLLOOP_EXACT_AT 3`/`CLASS_AT 4`), do **not** add a second counter. Normalise its key for
    shell commands (strip `timeout N`, `cd X &&`, env assignments, trailing `| head/tail/grep`,
    `2>&1`), and add an **"ended at its own bound"** failure class.
- **Where.** New transcript writer (reuse the redactor); `jc_toolloop.c` key derivation;
  `jc_agent.c:2839`/`:3369` (`jc_toolloop_note`) for the failure class.
- **The subtle R6 correction (threadwork §3.1).** The drive's loop calls were *piped into `head`*,
  which returns exit 0 — so they were **successes**, and successes never reach the failure path
  (`jc_agent.c:3353`, `if (res.is_error)`). Therefore the **"ended at its own bound" class must be
  computed on the success path too**: a call whose wall-time is about its inner `timeout N`, or the
  tool's own bound, counts even though it "succeeded".
- **Test.** `e1/e1_s3_loop.sh` (red): exit codes `124 124 124 124 0 0`, one warning at the 4th
  failure, the two pipe-masked runs handled. **And caution 1's negative case in the same driver:**
  two genuinely different commands must **not** trip the detector (over-normalisation would muzzle a
  working agent). Plus plan T6/T7 and review T-new-1 (canary redacted in the transcript).
- **Escape route.** If the negative case trips, your key normalisation collapsed two distinct
  commands — narrow the stripping.

### M808 — P3: salvage orphaned worktrees

- **Why.** If the *parent* dies mid-batch, no teardown runs, so nobody preserves; and in a container
  every run's lead is pid 2, so a dead run's base directory can be the next run's base.
- **First, reproduce G3** (still UNVERIFIED): a one-boot container repro (podman `--init`, lead
  pid 2, a leftover base) confirming the read-only fallback. Do not build on an unconfirmed failure
  mode.
- **Decision.** Name pool bases by **run id** (`worktrees/par-<run>`), not pid, so a dead run's trees
  never collide with a live run's. Use an **fcntl lock** for liveness (it dies with its holder — the
  right tool for "is the holder alive?"), not `kill(pid,0)`. Write the manifest `batch.json`
  **before** the worktrees exist (manifest-first). Salvage is **idempotent**: dedupe by the tree's
  sha or a `.salvaged` marker, so running it twice yields one ref. gc covers attempts + bases.
- **Test.** Plan T8/T9; review T-new-3 (two salvages → one ref).
- **Escape route.** If salvage double-creates refs, the idempotency marker is written after the
  preserve instead of checked before it.

### M809 — P4b: the disk guard

- **Why.** Nothing stops a run as its disk fills; a full disk corrupts the very records a post-mortem
  needs. And `jc_disk_free_mb` (`src/platform/jc_platform_posix.c:747`) is buggy: it returns 0 for
  any fragment size < 1024 bytes (ZFS is often 512) *and* for a failed `statvfs` — so it reads
  "0 MB free" on healthy ZFS today.
- **Decision.** Rewrite the measurement to return a **status, not a sentinel** (`JC_OK` + MiB/files,
  or an error meaning *unknown*); **unknown never stops a run** (the else-branch-must-say-unknown
  rule). Do the arithmetic in `double` (C89 has no `long long`), which also fixes the sub-1024
  fragment and the 32-bit overflow. Guard is a **fence**. Throttle by **time** (≈10 s) at tool
  boundaries — *not* by call count, because one `zig build` writes gigabytes in a single call — with
  unthrottled checks at run start, before a batch allocates worktrees, and before each preservation.
  `doctor` prints per-filesystem free space + floors and **names kept-but-unpreserved trees**.
- **Test.** Plan T10/T11 — and run **T11 on the illumos and FreeBSD rigs**: assert non-zero free
  space on a non-full disk (it reads 0 today — that *is* the bug). This ties to the M796–M799 rig
  work; those rigs already run `doctor` offline.
- **Escape route.** If a rig's `doctor` still shows 0, the `double` path is not taken on that libc —
  check the `f_frsize`/`f_bsize` fallback.

### M810 — P5: analyse, reuse, build upon

- **Why.** The operator asked that stopped work be *reusable* by the same or another agent.
- **Decision.** `attempts show <ref>` prints the end record + diffstat + transcript tail.
  `from:"<ref>"` in `spawn_parallel` creates the task's worktree at the current base and applies the
  kept change with a three-way merge (`cherry-pick --no-commit`); a conflict is a **tool-error value**
  naming the files, not a crash. The predecessor's record is prepended to the task text — but
  **effect-classified** (R2, adjusted): it lists only actions whose effects persist **outside** the
  worktree (writes outside it, network, git refs, installs), as *information*, because the successor
  still must re-run builds, tests and gates. jichi already classifies tools by effect (M535).
- **Test.** Plan T12; review T-new-2 (a `from:` resume does not repeat an outside-tree effect — a mock
  side-effect counter fires once).
- **Escape route.** If the successor re-runs a migration, the effect list is empty or not consulted —
  check the classifier output is reaching the prompt.

### M811 — P6: WIP commits (optional)

- Only if P1–P3's measurement shows the end-only path loses too much reasoning. Off by default; it
  adds git work to every child. The transcript (M807) already carries the reasoning, so this is a
  belt-and-braces optimisation, not a requirement.

---

## Part D — Deferred, with the reason (threadwork §4)

threadwork red-tested three more drive defects that have **no milestone** yet. They are recorded
here so they are not forgotten; schedule them after M811 or move them to `docs/DEFERRED.md`:

- **O10 — arguments** (`e1/e1_s6_args.sh`): `spawn_subagent` silently accepts unknown keys, and a
  task without `write` runs read-only without the result saying why. *Small.* Deferred because it is
  independent of the preservation/liveness lines.
- **O8 — merge** (`e1/e1_s5_merge.sh` checks 3–4): an appended block is dropped (first-writer-wins),
  and an untracked 1 MB file is copied into the live tree. O8(d) — list large untracked additions
  instead of merging them — sits right next to M803's teardown code, so it is the natural follow-on
  to P1. *Medium.*
- **O9 — shell confinement** (`e1/e1_s4_escape.sh`): a write child's shell wrote into the live tree.
  O9(a), a task-text rewrite, is small; O9(b), a lexical guard, is medium.

**Recommendation:** fold **O8(d)** into P1's follow-up (same code), and schedule O9/O10 as their own
small milestones after the preservation line, unless the operator wants them sooner.

---

## Part E — Design decisions, recorded honestly

For a reviewer or a future maintainer, the decisions that a reasonable person could disagree with:

1. **Preserve the git tree, not the child's conversation/heap.** The durable artifact (files in the
   worktree) is what survives a crash; the reasoning goes in the transcript. This matches OTP's
   "rebuild from durable state, not a captured heap" and the crash-stop/crash-recovery distinction.
   *Rejected:* snapshotting process memory (fragile, large, and gone on SIGKILL anyway).
2. **One chokepoint + a signature change, not per-call-site preservation.** A signature the compiler
   enforces cannot be forgotten; a call site can. *Rejected:* preserving at each of the four sites.
3. **The dead-stream bound is one setting with `stall`, and it is a fence.** curl allows only one
   low-speed window, so they merge; and because it fires only on zero bytes, it is safe to keep on in
   measurement runs. *This changes a written rule (CLAUDE.md caps-vs-fences), so it carries an
   operator decision and a DECISIONS.md row.*
4. **Redact sessions; accept that redaction is registered-values-only.** The model already saw the
   value, so redaction loses nothing on replay; but a general secret filter is out of scope, and the
   container + pre-push scan remain the boundary. Stated plainly rather than implied away.
5. **Split L1 so abort waits for preserve.** Serving abort before P1 would make an operator's abort
   destroy work — the opposite of the goal. The split costs one extra milestone and is worth it.
6. **Renumber by landing order.** The project's milestone numbers are chronological, and P1 must land
   before L1's abort half, so the numbers cannot follow v1's step order. A mapping table keeps
   cross-references to threadwork's comments valid.

## Part F — How to work a milestone (the loop, for a first-timer)

1. Read the milestone above and the driver it names in
   `docs/internal/drives/2026-09-26-zigodot/agentic/e1/`.
2. Build the test tools: `make smoke-tools`. Copy the driver into `tests/smoke/`, and run it exactly
   as `e1-results.md` says. **Watch it fail** (red-first). If its instrument checks (the low-numbered
   ones) fail, the scenario did not run — fix your invocation, not the code.
3. Make the change at the touchpoints named. Keep the `agent_depth` guard where noted. Match the
   surrounding code's C89 style (declarations at block top, no `//`, `jc_snprintf` not `sprintf`).
4. Run the driver again — **watch it pass**. Then perturb per check (revert one piece, watch that
   check go red) so you know each check is real.
5. Run the full gate: `make ci` (~20 min). It must be green.
6. Bump both milestone banners (ROADMAP + README) to the number you took, commit, push (fast-forward
   only, token redacted), and offer `sudo make install` after a binary change.
7. Re-check the ROADMAP's newest number before you claim your milestone number — take the next free
   one.

**Do not touch the zigodot drive's binary, directory, or branches before 2026-09-28 10:00 +0200.**
Nothing you merge to `master` affects the drive; it runs its pinned copy.
