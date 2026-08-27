# Parallel agent swarm

`spawn_subagent` delegates one subtask to a nested agent and blocks until it
finishes. **`spawn_parallel`** runs several subtasks *at once* — a fork-based
worker pool sized to the CPU — and returns their aggregated answers. Read-only
subtasks investigate/review/plan against the live workspace; **write** subtasks
each edit an isolated **git worktree** and the parent merges their file changes
back. It's the "systems of agents" tool: fan out independent work, join the
results.

A hands-on tutorial (the `/investigate` worked example, budgets, worktrees,
and the counter-cases) is [TUTORIAL_ORCHESTRATION.md](TUTORIAL_ORCHESTRATION.md).

```
spawn_parallel({ "tasks": [
  { "task": "audit the auth code for bugs" },
  { "task": "review error handling", "model": "qwen3-coder-next" },
  { "task": "refactor module A to use the new API", "write": true },
  { "task": "refactor module B likewise",          "write": true }
]})
```

## When to use it vs `spawn_subagent`

- **`spawn_parallel`** — several *independent* subtasks that can run at the same
  time (broad investigation from many angles; editing disjoint parts of the
  codebase concurrently).
- **`spawn_subagent`** — a single subtask, or work that must happen in sequence
  / edit the live tree directly.

## How it runs

- **Fork pool.** One child process per task, at most `maxParallelAgents` live at
  once (default `min(CPU cores, 8)`; set `"maxParallelAgents"` in config). Extra
  tasks queue and start as slots free. Up to 32 tasks per call.
- **Per-task options:** `task` (required), `model` (selector — name / index /
  role), `agent` (a `.jichi/agents/*.md` profile), `write` (default false).
- **Each child** runs an autonomous subagent (its own model, its own HTTP) and
  returns its final answer over a pipe. libcurl is fork-safe (a fresh handle per
  request); the arena is copy-on-write, so children never corrupt the parent.
- **Token budget.** Each child is reserved a `1/ntasks` slice of the run's
  remaining token and tool-call budget, **which it enforces itself** while it runs;
  afterwards the parent adds each child's reported usage to the
  [autonomy envelope](AUTONOMY.md) budget, so a swarm still counts. Before **M431**
  the slice was computed and then never consulted — `jc_env_over_budget` was gated
  on being the top-level agent, and a child runs one level down — so the per-child
  watchdog was in practice the only thing bounding a child. If you are on an older
  build, size a fan-out on the watchdog, not on the budget.
- **Abort.** Ctrl-C terminates and reaps every child; nothing is left running.

## Live status board (TUI)

Children stream lightweight progress over their result pipe — a message when they
start a tool and a running token count — followed by a final result message
(newline-framed compact JSON). In the **interactive TUI** the parent renders a
live board as these arrive:

```
▸ spawn_parallel  3 tasks
  [1] qwen3-coder  running
  [2] gemma         running
  [1] qwen3-coder  read_file (1.8k tok)
  [2] gemma         done (0.9k tok)
  [1] qwen3-coder  done (4.2k tok)
```

This is **display-only and TUI-only**: the progress lines never leave the pipe,
and the board is emitted through the optional `on_status` callback that only the
TUI provides. In headless / over SSH (no `on_status`), and under `-q`/`/quiet`,
there is no board — stdout still carries just the final merged answer, unchanged.
The pure `jc_parallel_parse_msg` classifier (in `jc_parallel.c`) is unit-tested.

## Read-only vs write

- **Read-only** (default) tasks run in the live workspace. Concurrent reads are
  safe; the workspace is never modified.
- **Write** (`"write": true`) tasks each run in their own **git worktree** — an
  isolated checkout created from the snapshot shadow repo at a fresh checkpoint,
  so the agents can't trample each other or the live tree while they work. Needs
  snapshots (git) available; otherwise the task degrades to read-only with a
  note.

### Merge: file-level, first-wins, conflicts reported

After all write children finish, the parent collects each one's changed files
and merges them into the live workspace **one file at a time**:

- Touched by exactly one child → applied (added / modified / deleted).
- Touched by more than one child → the **first** child's change is kept and the
  rest are **skipped and reported** as conflicts. Changes are never
  auto-resolved or line-merged.

So **disjoint** edits (the intended pattern — give each task its own files) apply
cleanly; overlapping edits are reported for you to resolve. The merge summary is
included in the tool's result, and the [envelope](AUTONOMY.md)'s verify gate (if
configured) checks the merged tree when the main agent finishes.

### Per-child verify gate (M144, opt-in)

The top-level verify gate checks the tree only *after* the merge — by then an
unverified child's breakage is already in. With `"parallelVerify": true`
(or `--parallel-verify`; `--no-parallel-verify` overrides the config), the
parent runs the configured verifier — the envelope's command, else config
`verify`, else `testCommand` — **inside each write child's worktree before
merging it**. Green children merge exactly as before; a **red child is
quarantined**: its changes are not merged, its task section is flagged, the
merge summary counts it, and a bounded tail of its verifier output is included
so the caller sees *why* without re-running anything (plus a
`parallel_verify` journal event when the envelope is active). The live board
shows `verify (worktree)` / `verify red: not merged` per child. Off by
default: the verifier runs once per write child (cost/latency), and a
project whose tests can't run inside a fresh worktree (absolute paths,
running services) would quarantine everything — opt in when the verifier is
worktree-clean.

## When a child fails (measured, M325)

Two failures dominate, and both used to be reported in a way that sent you looking in the
wrong place. In one real workload **6 of 10 `spawn_parallel` calls failed**: three to the
watchdog, three to `fork()`.

### `sub-agent timed out after Ns (parallelTaskTimeout)`

Each child is killed if it runs longer than **`parallelTaskTimeout`** seconds — config key,
**default 300**. Reaching it does not fail the whole call: that child is reported failed, the
others keep their results.

**This default is easy to be bitten by, and the shape of the evidence is worth recognising.**
In the measured workload the *successful* `spawn_parallel` calls took **300, 309, 362 and 462
seconds** — so children legitimately needed minutes, and a 300-second cap killed roughly half
of them. If your parent-call durations cluster around the timeout, the timeout is the problem,
not the children:

```json
{ "parallelTaskTimeout": 900 }
```

Set it above what a child genuinely needs. It exists to stop a *wedged* child hanging the
swarm, so it should be generous rather than tight — the token/time budgets
([AUTONOMY.md](AUTONOMY.md)) are the right place to bound cost.

### `fork failed: <reason>`

jichi could not create the child process at all. The message now carries the system reason,
because the two common ones want opposite responses:

| Reason | Meaning | Response |
|---|---|---|
| `Resource temporarily unavailable` (EAGAIN) | the process/thread limit is reached | lower `maxParallelAgents`, or raise the `ulimit -u` |
| `Cannot allocate memory` (ENOMEM) | not enough memory for another child | run fewer children; see [LOW_MEMORY.md](LOW_MEMORY.md) |

A failed fork is reported per child and the rest of the pool continues — but if you see it,
the machine is at a limit and the *next* thing to fail may not be so graceful.

### Reading the outcome

The tool's result lists every child with its state, so a partial failure is visible rather
than silent. **Read the result text, not the ok-rate**: in telemetry the whole call appears
as one `tool_call`, and its `ok` is false only when **every** child failed
(`out->is_error = (n_ok == 0)`). A swarm that lost three children of four still records
`ok: true`.

That bit is deliberately coarse, because it is the same bit the *model* reads as
`is_error`: flagging a call in which three of four children succeeded would tell the model
its fan-out failed, and the useful information — which child died, and how — is in the
result text either way. So a supervisor watching the ok-rate alone will miss partial
losses; parse the per-task sections. (This page claimed the opposite until M431.)

> **Beyond one host:** the fork pool is one of three topologies jichi supports,
> and the other two (a queue on one filesystem, a push over ssh across machines)
> plus the honest list of what does not exist are analysed in
> [DISTRIBUTED.md](DISTRIBUTED.md).

## Limitations

- No automatic conflict resolution — overlapping file edits keep the first
  writer only.
- `codebase_search` is best in **read-only** children (they share the workspace
  index, pre-warmed before the fork). A write child runs in a worktree, so its
  index would be cold; prefer `read_file`/`search` there.
- `write` needs git (snapshots). Without it, write tasks run read-only.
- `spawn_parallel` is **top-level only** — a child is never given it, so there are
  no nested fork pools. `spawn_subagent` is different, and this page said otherwise
  until M431: at the default `maxSubagentDepth: 2` a child (which runs one level
  down) *is* given `spawn_subagent` and may delegate one synchronous sub-subagent.
  Set `maxSubagentDepth: 1` if you want a flat fan-out, or 0 to forbid delegation
  entirely (`--lite` does the latter). See [SUBAGENTS.md](SUBAGENTS.md).

## Implementation

`src/tools/jc_tool_parallel.c` is the tool: the fork pool (`run_pool`, modeled
on the snapshot `run_argv` + the MCP `select` loop), the child entry
(`run_child` → `jc_agent_run_subagent`), and the file-level merge. The pure
cores in `src/tools/jc_parallel.c` (`jc_parallel_eff_max`,
`jc_parallel_parse_changes`, `jc_parallel_claim`) are unit-tested; the worktree
helpers (`jc_snapshot_worktree_add`/`_changes`/`_remove` in
`src/snapshot/jc_snapshot.c`) are integration-tested; `jc_cpu_count`
(`src/platform/jc_platform_posix.c`) sizes the pool. The fork pool, merge, and
abort are verified end-to-end with a live model.

## What each child reports (M437)

Every child's section in the aggregated result carries the same `[delegate]` block the
synchronous `spawn_subagent` returns — rendered by the same function, so the two tools
cannot describe one outcome two ways. The child sends its stop reason and last failing
call over its pipe (the reason as a **name**, because a name survives an enum
renumbering and an integer does not) and the parent renders it.

This closed a real gap: a child that hit its **iteration limit** previously got no note
at all, while `spawn_subagent` had carried one since M62 — so a truncated answer from
the fork pool was indistinguishable from a complete one. A write child additionally
reports the files it changed, which the parent already parsed to decide the merge; that
is the one report field the fork pool can fill and the synchronous tool cannot. See
[`SUBAGENTS.md`](SUBAGENTS.md).
