# M1006 -- the configurable dirty-tree dispatch guard

*Internal design note (the milestone's step 2 of `docs/internal/drive/2026-10-17-hardening/PLAN.md` §4).
The operator chose this on 2026-10-20 17:47 ("M1006, the configurable dirty-tree dispatch guard, queued
after the batch-4 items") and ordered it built 2026-10-23 09:19. It was LOST from the register in the
meantime -- no PLAN row, no DEFERRED row, no ROADMAP entry -- which is why this note re-states the spec in
full (supervisor 09:17) and adds a board card (84).*

## The problem (measured, not hypothesised)

A WRITE dispatch -- `spawn_parallel` with `write: true` tasks, or `spawn_subagent` with write tools --
is the only dispatch that changes the workspace: the parallel children edit isolated worktrees that are
3-way merged back, and an in-process write subagent edits the LIVE workspace directly (a path fence, not
a worktree -- `jc_agent_run_subagent`, the `run_subagent_posture` path). Read-only dispatches (ro
subagents, ro parallel tasks) change nothing. If the tree is DIRTY when such a write dispatch starts,
three things go wrong at once, and this drive has been doing all three:

1. **The base is unidentifiable.** The worktree base is the current HEAD, so the children's 3-way merge
   and the lead's later `git add -A`/commit mix the lead's uncommitted work with the children's -- the
   "Dropped N change(s)" collision class, and the reason a merged result can fail its check for reasons
   no worker saw. *(Corrected 2026-10-23, supervisor 12:14: the premise "the base is HEAD without the
   dirty tree" was wrong -- `jc_snapshot_take` runs `git add -A` and commits in the shadow repo
   (`src/snapshot/jc_snapshot.c:643`), so the parallel base INCLUDES uncommitted and untracked changes.
   The guard's other reasons stand; this one is corrected, not deleted.)*
2. **Unverified work gets dispatched on top.** The operator's rule (2026-10-18, and the 2026-10-22
   supervisor's dirty alert) is *commit verified work, then dispatch*: dispatching from a dirty tree means
   the next worker builds on a tree that has not passed a gate, and a red first-fix or a reverted probe
   can be stranded in the base. This run held M1046 uncommitted ~30 minutes and dispatched M1046-b on top
   of it -- the fourth such slip since 2026-10-22 (M1042 review, M1041-c, M1045 fix-up, this one). The
   guard is the operator's answer to that class.
3. **Evidence is lost with the run.** When a batch is later found to have merged against a dirty base,
   there is no record of whether the base was dirty at dispatch time, so the result cannot be
   interpreted.

A READ-ONLY dispatch does NOT have this problem (the reviewer reads the diff exactly as it is on disk,
and no merge happens), so it is deliberately out of the guard's universe.

## The decision

Before any FORK of a write dispatch, check the workspace for uncommitted changes -- **tracked
modified/staged and untracked non-ignored files, `git status --porcelain` semantics** (the same helper
shape as `relay_git_facts`, `src/main.c:2495`, which already runs `git -C '<root>' status --porcelain`
and collects the path list). Act on the result per a config mode:

- **`refuse`** -- the tool call returns an error **naming the dirty paths (first N) and the fix
  ("commit, then dispatch")**, with `policy_refusal` set (via the existing `tu_err_policy`,
  `src/tools/tool_util.c:36`). No fork happens. This is the mode the DRIVE runs (operator 09:19/09:20):
  the lead's own discipline becomes a fence, and a dirty dispatch -- the class that has happened four
  times in three days -- is refused and named instead of observed afterwards.
- **`warn`** -- the dispatch proceeds; the result carries a warning line (top of the batch result, the
  M852 `fence_note` shape) naming the dirty paths. This is the PROVISIONAL PRODUCT DEFAULT: it makes the
  state VISIBLE in every run (the measurement the default will be decided from) without changing what a
  healthy lead does, and it cannot strand work the way refuse would on a tree that is dirty for a
  legitimate in-progress reason.
- **`off`** -- no check, no telemetry row, no fork-time cost. The escape hatch for a tree that is
  deliberately dirty (a hand-off, a probe).

**Read-only dispatches are not checked, in any mode** -- that is the guard's stated universe, named in a
comment at the check site. A read-only reviewer of an uncommitted diff must stay possible, or the guard
would forbid the review step the drive's own practice ("review the uncommitted diff before commit")
requires.

### Why `strictness.dirtyDispatch` (the key name)

The key is a nested object `strictness` (top-level, beside `timeouts`) holding a single mode value now,
read **leniently** (`jc_json_get_str` + a small case-folding parse of `off|warn|refuse`; a prose/unknown
value falls through to the default, as `jc_json_get_num_lenient` does -- a typo must not silently
tighten or loosen a fence, the M519 class). The name `strictness` is chosen so that **later strictness
knobs can join the same block** (the operator's own instruction: "named so later strictness knobs can
join") without a second top-level key: `editScope`, `pathFence`, and the B1 concurrent-wave refusal all
read as strictness dials, and one block is one thing to point at. `dirtyDispatch` is the specific dial.
Rejected: a flat top-level `dirtyDispatch` key (no room to grow; the operator explicitly wanted the
knob to join a family); a boolean `dirtyDispatchGuard` (cannot express the three modes or the off escape).

## The seams (verified on the head before this note)

- **Check site, `spawn_parallel`:** `src/tools/jc_tool_parallel.c`, `parallel_run` -- the check goes
  **after** the `any_write` flag is computed (the task-resolution loop ends ~line 2215) and **before**
  the first `jc_snapshot_take`/worktree fork (the `if (any_write && ...)` block at 2249/2285). A
  `refuse` returns early with `tu_err_policy`; a `warn` sets a `dirty_note` (the `fence_note`/`kept`
  sb shape) that is folded into the result top. The universe comment ("read-only dispatches are not
  checked; the check reads the LIVE workspace, not the snapshot repo, `git -C '<root>'`") goes here.
- **Check site, `spawn_subagent`:** `src/tools/jc_tool_subagent.c`, `subagent_run` -- the check goes
  after `include_mutating` is computed (line 273: `ro ? 0 : !app->readonly`) and before
  `jc_agent_run_subagent` (line 434). Only when `include_mutating` is set (a read-only subagent is out
  of the universe, same rule).
- **The dirty probe:** a new shared helper (both call sites need the identical behaviour, so one
  function, not two copies -- the M852 "say it once" principle). It runs
  `exec 2>/dev/null; git -C '<root>' status --porcelain` (the exact command `relay_git_facts` uses,
  `src/main.c:2528`), collects the path list (first N, N small -- 5), and returns dirty-yes/no + count +
  path string. It must be reachable from BOTH tool files, so it lives in a small util (e.g.
  `src/util/jc_dispatch.c` + `include/jc_dispatch.h`, or a static in a shared header if the C89
  constraint prefers -- the worker picks, but it is ONE function). A non-git workspace or a git
  failure leaves it "not dirty" (the same "missing repo omits the line rather than asserting a
  falsehood" rule as `relay_git_facts` -- a workspace jichi cannot read is not a dirty workspace, and
  the guard must not refuse work on a non-git dir).
- **Config:** `include/jc_config.h` gains a `strictness` sub-struct (one mode enum/field) beside the
  `timeouts` one; `src/config/jc_config.c` reads it leniently in the top-level parse (the `parse_timeouts`
  call-site at 1956 is the model) and inits it (the 1112/1127 init block). The default is `warn`
  (PROVISIONAL). The trust table (`src/config/jc_config_trust.c`) gets a row if the key is user-config
  (it is -- it is not a wire value and not model-visible, so it is a normal config knob).
- **Telemetry:** one event per CHECKED dispatch, in EVERY mode (off emits nothing -- it does not check).
  Fields: `mode` (off|warn|refuse), `dirty` (bool), `npaths` (int), `outcome` (proceeded|warned|refused).
  Emitted from the check site via `jc_app_telem`/`jc_app_telem_end` (the `app->telemetry` idiom
  `jc_tool_parallel.c:753` already uses), event name `dirty_dispatch`. The READER is required in the
  same milestone (the operator's spec; `telemetry_events_lint` check 10 is the universe): a row for
  `dirty_dispatch` in `docs/TELEMETRY.md`'s "Event types" table (which `telemetry_events_lint` scrapes,
  `tests/smoke/telemetry_events_lint.sh:60`) AND a count in the `jichi telemetry` summary
  (`jc_telemetry_summary` + `jc_telemetry_render`, `include/jc_telemetry.h:149`) -- the M167 precedent
  ("emitted from the start but had no reader here ... had to be counted by hand" is exactly the gap the
  operator's "with a reader in the same milestone" closes). The summary counts `dirty_dispatch` events
  and splits by outcome (refused is the one that matters).

## The red-first test (step 3, before any fix)

A NEW smoke driver, `tests/smoke/dirty_dispatch.sh` (mockmodel, offline, the headless `< /dev/null`
idiom -- no real model, no network). It drives a headless `jichi` whose scripted mock replies force a
`spawn_subagent` (or `spawn_parallel`) WRITE call, and asserts per mode:

- **dirty + refuse:** the call is REFUSED -- the result carries the policy-refusal error naming the dirty
  paths and "commit, then dispatch", and NO worker forked (the mock served the model's reply, but the
  subagent's own model call never happened -- a distinguishable marker in the capture or the result).
- **dirty + warn:** the call PROCEEDS (the worker runs) and the result carries the warning naming the
  dirty paths.
- **dirty + off:** the call proceeds with NO warning line.
- **clean + any mode:** the call proceeds with no warning and no refusal (refuse does not fire on a clean
  tree -- the guard must not refuse healthy work).
- **read-only + dirty:** NOT checked (the read-only subagent proceeds; the telemetry event is absent --
  the universe).
- **telemetry read-back:** the `dirty_dispatch` row's fields (`mode`/`dirty`/`npaths`/`outcome`) are
  present in the run's log and counted by `jichi telemetry` (the reader).

Each mode is its own `t_check` leg (the runbook's per-CHECK perturbation: the unit that can be vacuous is
one mode, so each mode gets its own red line). The dirty state is made by `git -C <scratch>` a real file
and NOT committing it; the scratch is a `smoke_tmp` workspace that IS a git repo (the driver inits it),
so `git status --porcelain` has something to report. **Red first:** on the unfixed tree the driver's
`refuse` leg cannot pass (there is no check to refuse) -- the first commit adds the driver and shows it
red on the exact check that does not exist yet, per `tests/teeth.sh`.

The `refuse`/`warn`/`off` behaviour is then the fix: the probe + the config key + the two check sites +
the telemetry event + the reader. The product default `warn` and the DECISIONS row (below) land with it.

## Rejected alternatives

- **Stash-before-dispatch** (auto-`git stash` the dirty tree, dispatch, unstash): rejected -- a stash is
  itself a WRITE to the working tree and to the stash ref, and a refused/stalled batch would strand the
  lead's uncommitted work in a stash (the M855 "one stash stack for every child" incident is the class).
  The operator's rule is *commit, then dispatch*; the guard enforces that rule, it does not paper over a
  dirty tree.
- **Refuse as the product default:** rejected -- a healthy lead with a legitimate in-progress file would
  be hard-stopped in every environment, and the provisional default exists so the DRIVE (a disciplined,
  supervised context) can run `refuse` while the PRODUCT measures `warn` before deciding.
- **A boolean guard:** rejected (see the key-name section) -- the three modes and the off escape need a
  value, not a bit.
- **Checking only `spawn_parallel`:** rejected -- `spawn_subagent` write is the same class (it merges the
  child's worktree back), and the operator's four slips included a `spawn_subagent`-shaped review. Both
  write-dispatch sites are the universe.

## The DECISIONS row (step: a decision a future reader could disagree with)

`docs/DECISIONS.md` gets a row: **the dirty-tree dispatch guard's product default is PROVISIONAL `warn`**,
rejected `refuse` (hard-stops healthy leads; the measurement will say) and `off` (invisible state, the
defect the operator ordered closed), and the drive runs `refuse` (operator 09:19/09:20). The measurement
that decides it: the `dirty_dispatch` telemetry summary's refused/warned split across the drive's runs
once the drive runs `refuse` -- if `warn`-visible dirty dispatches that then caused a base problem are
rare, `warn` becomes the default; if they are the norm, `refuse`. Named so a future run can close it out.

## Not goals

- Not a general "clean tree before any tool" fence -- only WRITE dispatches (the fork+merge class).
- Not a snapshot/shadow-repo check -- the guard reads the LIVE workspace (`git -C '<root>'`), and the
  snapshot repo is explicitly never the subject (the `relay_git_facts` "reads the USER's repo, never the
  shadow snapshot repo" rule).
- Not B1's concurrent-wave refusal (that is a separate, later, measured change -- the operator's 08:36
  decision 4); it may JOIN the `strictness` block later, which is why the block exists.
