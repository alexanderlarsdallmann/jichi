# B1 — non-blocking `spawn_parallel` (design note)

*Batch 14 item (b), moved here from batch 12 (the operator's 18:24 order). Design
note FIRST; the operator decides. Nothing is implemented: this page is the
question, with the seams named so the decision has a shape. The finding is
[def-304](../DEFERRED.md#def-304) and the candidate text is
§B B1 of the drive's ORCHESTRATION.md (the full text with the qwen-code
citations is there). Written 2026-10-23 (run 0070, UTC).

**Status:** design note only, not built. The operator's 18:24 order makes (b)
design-first and the 23:34 grant says to write the design notes for
(b)/(f)/(g)/(h) but NOT implement them tonight. The open questions below are
the decision this page asks for.

## The claim

`spawn_parallel` should become **background by default**: the call dispatches
the wave and returns handles immediately; **completion notifications are
injected between the lead's tool rounds**, each naming remaining/all-done, so
the lead verifies wave N while wave N+1 runs. It is the largest lever on the
drive's lead-occupancy cost (P1: run 0090's card 214 held a lead 4 h 27 min in
three one-task batches), and it retires "injects are read only after the
batch" (H6/def-304) as a side effect, because the notification uses the
inject path.

## What the tree does today (the seams, measured 2026-10-23)

- **The block.** `parallel_run` (`src/tools/jc_tool_parallel.c:2007`) calls
  `run_pool` synchronously (`:2427`). The pool is **fork-based, single-threaded
  in the parent** (no `pthread_create` anywhere in `src/`): a refill loop
  (`:1248`) keeps ≤ cap children live (cap = `min(ntasks,
  config.maxParallelAgents, jc_cpu_count(), 8)`), and the parent loops
  `while (done < ntasks)` (`:1233`) over a 200 ms-sliced `select()` on the
  children's pipes (`:1321`). The lead's agent loop does not run until every
  child EOFs. While blocked, only supervisor status/abort is serviced
  (`jc_control_poll_during_call`, `:1338`).
- **The children.** Each task is a forked jichi process: `run_child` (`:681`)
  chdirs into a per-task worktree of the snapshot shadow repo (write tasks;
  `:2292`), runs `jc_agent_run_subagent` (`:892`), and answers through its
  pipe. Read-only tasks run against the live workspace.
- **The merge.** AFTER all children, still inside the blocked call: per-child
  verify in the worktree, then **file-level first-wins copy** — `apply_change`
  (`:1012`) overwrites `app->cwd/<path>` for the first writer to claim a
  path (`jc_parallel_claim`, `:2769`); the change list is diffed against the
  **pre-batch base commit** (`:2663`). There is no git merge and no 3-way here
  (the only 3-way in the tree is `jc_snapshot_resume_apply`, the `from:`
  resume, `:2328`).
- **The records.** Teardown (`:2851`) writes the preserved refs
  (`refs/jichi/discarded/<why>/<secs>-<n>`), the per-child transcripts
  (`~/.jichi.d/attempts/<run>/t<N>.jsonl`), and the board lines (display-only,
  `:1058`).
- **The injection path B1 would reuse.** Mid-run notes are already folded at
  the agent loop's **round boundary**: before each model request
  (`jc_agent.c:2510`, M438 — "a queued inject folds in here and reaches the
  request about to be built") and at the round tail with the TUI type-ahead
  queue (`:5147`). Mid-round, injects are deliberately NOT folded (`:5117` —
  a user message between two tool results of the same round is malformed,
  M364's contract). A batch-completion note folded at the same boundary is a
  user-role message after the round's tool results — the wire shape the
  existing check enforces.
- **The closest existing analog.** The background shell (`jc_bg.c`):
  `run_in_background` returns `"Started background process %d…"` immediately
  (`jc_tool_run.c:89`), the spool lives in `~/.jichi.d/bgspool`, and
  `jc_bg_poll` reaps opportunistically at the per-call boundary
  (`jc_agent.c:3213`) — **with no completion notification**; the model must
  poll `read_background_output`. B1's notification is precisely what the bg
  shell also lacks.
- **The shape that cannot outlive the call.** The `tasks[32]` pool state,
  the child pipes, and the fcntl base lock all live on `parallel_run`'s
  stack and are released in its teardown — a background form must lift batch
  state out of the call (an `app`-lived manager, as `app->bg` does).

## The design (the shape the decision should take)

**1. Dispatch returns a handle.** `spawn_parallel` forks the wave exactly as
today (the pool, the worktrees, the fences, the child loop are unchanged) and
returns, in the same round: a **batch id**, the per-task handles (task i →
worktree ref, model, title), and the cap. Batch state (the `ptask` array,
pipes, base lock, base commit) moves to an `app`-lived manager — the `app->bg`
shape — so the tool call can end. `run_pool`'s select loop survives as a
**standalone reaper** driven at the per-call boundary (`jc_agent.c:3213`'s
neighbor) instead of inside the tool call: each boundary poll reaps what
exited, collects finished answers, and updates the batch's
`done/ntasks` count. No threads are introduced: the reaper is
`waitpid(WNOHANG)`-style over pipes, the bg precedent.

**2. Notifications at the round boundary.** When a poll reaps ≥1 child, a note
is queued for the next round boundary (`:2510`) in the inject shape:
`[parallel <batch-id>: task 3 done (2 remaining)]` per completion, and one
`[parallel <batch-id>: all done (4 tasks, 3 answered, 1 kept-unmerged)]` at
the end. The lead's next model request sees the note after the current round's
tool results — M364's pairing holds because the note is user-role at the tail.
A new tool verb (`spawn_wait` / `parallel_status <batch-id>`, name open)
returns the finished tasks' answers **on demand**, so the lead that wants wave
N's diff while N+1 runs calls it; the all-done note carries the refs, the
answers stay fetchable (capped, as today's 16 KB).

**3. The merge moves to completion, and becomes 3-way.** Today's first-wins
copy is sound only because the lead is blocked; with the lead free to edit the
same tree, a kept copy would silently overwrite its in-flight edits (and a
`'D'` change deletes them). The merge must therefore run **when the batch
completes and its notification is delivered** (not at a later arbitrary
moment) and **3-way against the HEAD at merge time** — the
`jc_snapshot_resume_apply` primitive is the tree's only 3-way and the
worktree's diff base (the pre-batch commit) is already recorded. Conflicts are
reported in the all-done note (the "Dropped N change(s)" shape, extended to
"conflicted with the lead's edits: kept the lead, re-applied from
`<ref>`"), and the child's state stays recoverable from its preserved ref —
exactly how `from:` resume works today, so the lead's "re-apply the second
worker's change" practice is unchanged. Read-only and `merge: false` tasks
merge nothing (today's semantics).

**4. Board, records, fences.** The per-task board lines render as children
complete (the manager, not the call). The preserved refs, the transcripts, the
base lock: written/released at completion, in the manager's teardown. The
child fences (shell fence, write scope, tool caps) are per-child and
unchanged. The fcntl base lock now guards **the merge window**, not the whole
batch — two waves can run concurrently if their write sets are disjoint,
which is the B3 (declared write sets) benefit arriving for free in the shape;
concurrent waves with overlapping claimed paths take the same 3-way
conflict path.

## Rejected

- **A thread per child** (the "obvious" backgrounding). The tree is
  fork-everywhere by design (COW children, no shared-arena hazard, the
  `jc_workerpool.c` fork-supervision primitives); threading the pool
  introduces the arena/lifetime bugs the fork model was built to avoid, for
  no gain — the parent's select loop is already multiplexed.
- **Keep blocking, add a "poll" escape hatch** (a `parallel_status` that
  reaps one child while `run_pool` waits on the rest). It leaves the lead's
  model call parked inside a tool call (the occupancy is in the tool call,
  not the select), changes nothing about P1, and forks a second code path
  over the same state.
- **Notifications mid-round** (a pipe into the running request). The M364
  contract forbids a user message between two tool results of one round; the
  round boundary already exists and already carries injects — reusing it is
  the whole point of "it also retires H6/def-304".

## Open questions for the operator (the decision this note asks for)

1. **Default.** Background-by-default for every `spawn_parallel`, with an
   explicit `sync: true` (or `--sync` config) to keep today's blocking call —
   or blocking-by-default with `background: true` opt-in (less surprise for
   one-task waves, the drive's current dominant shape, which today cost 4 h
   27 min)?
2. **Merge timing.** Merge at the all-done notification (design §3) vs. an
   explicit `parallel_merge <batch-id>` the lead calls when it has read the
   diffs (the lead controls the moment; a wave's work sits unmerged longer,
   and two lead edits between "read" and "merge" are both inside the 3-way
   base — still sound, but the conflict report names a further-moving base).
3. **Notification granularity.** Per-task notes (the lead sees each child land)
   vs. one note per boundary poll batching what finished (fewer model-visible
   lines; the drive's 4-5 wave shape makes per-task ~4-5 notes anyway).
4. **Concurrent waves.** Allow (cap still global, base lock per merge window —
   design §4) or refuse a new wave while one runs (simpler; the lead
   serializes what it wants to serialize).

## Out of scope (stays for later items)

B2 (per-task stages), B3 (declared write sets as a *refused-at-dispatch*
feature — the shape arrives incidentally in §4), B4 (steer-a-running-worker),
B5, B6 are separate findings. The harness itself (jichi-harness) is the
operator's in-between drive; this item is one of its prerequisites.

## Test strategy (for when it is built, not tonight)

Red-first per the drive's rules: a driver dispatching a one-task wave with a
child that answers after a sleep, asserting the lead's NEXT model request
happened before the child ended (the mock model logs request order) — red on
today's tree (the lead blocks, so no request precedes the child's EOF). The
merge leg: the lead edits a file between dispatch and completion; the merge
3-way keeps the lead's version and reports the conflict — red on a
first-wins merge. The notification leg: the round-boundary note is present in
the request the child's answer is first visible in, and absent mid-round.
`jc_bg`'s spool/reap legs are the reuse precedent for the reaper teeth.
## Decided (the operator, 2026-10-23 08:36: "Yes, follow your recommendations for both, please")

The four open questions, settled:

1. **Default: OPT-IN.** `background: true` on the call; today's blocking call stays
   the default until the harness drive has measured background mode -- then the
   default is revisited (provisional defaults, the operator's 2026-10-20
   direction).
2. **Merge timing: AUTOMATIC** at the all-done notification, 3-way against the
   HEAD at merge time (design 3); no explicit `parallel_merge` verb.
3. **Notifications: ONE note per tool round**, listing every task that finished
   since the last note (per-task news, coalesced per round).
4. **Concurrent waves: REFUSED** in the first version (a second wave while one
   runs is refused, with a message naming the running batch); allowing them is a
   later, measured change.

B1 is NOT built tonight (it is not needed for the harness core, P2); it is built
after the switch, when its slot comes.
