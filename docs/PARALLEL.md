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
  role), `agent` (a `.jichi/agents/*.md` profile), `write` (default false),
  `from` (resume a kept attempt, below), `max_tool_calls` (the task's own
  tool-call budget, below), `merge` (`false` keeps a write task's work and never
  merges it, below).
- **Each child** runs an autonomous subagent (its own model, its own HTTP) and
  returns its final answer over a pipe. Since **M864** a child's stderr log lines
  begin with the UTC time and its id, the `task_run` its telemetry rows carry
  (`2026-10-02T08:12:34Z <sid>-b1-t1 [jichi warn] ...`); the lead's lines do not. libcurl is fork-safe (a fresh handle per
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

Two things about the merge are easy to miss, and both can lose work quietly:

- **A shared registry loses all but the first writer (O8).** First-wins is by whole
  file, so if two children each *append* a line to the same file — a test index, a
  `mod.rs`/`__init__.py`, a generated manifest — only the first child's version is
  merged; the second's line is dropped even though the two do not overlap. The
  dropped change is **surfaced at the top of the result** since M824 (a "Dropped N
  change(s)" callout), and it is preserved (below), so re-apply it or resume the
  kept attempt — do not assume the tree has both.
- **The merge copies a child's *untracked* files too, debris included (N8).** A file
  a child created but did not need — a `REPORT.md`, a stray `*.log`, an editor
  scratch file — is an "added" change like any other and merges into the live tree.
  Give write tasks disjoint files, and tell them not to leave scratch files in the
  worktree root; a shared clean-up is on the roadmap (a config skip-list).

### What happens to work that is NOT merged (M803, M807–M809, M815–M816)

A write child whose changes do not all merge — a conflict loser, a verify-red child
(above), a child killed by its watchdog or a mid-batch `abort`, one that did not end
on its own and had no worktree verify (below, M854) — does not simply lose its worktree. Since **M803** the parent **preserves**
that worktree first: it commits the child's content and pins it under
`refs/jichi/discarded/<reason>/…` before removing the tree (if preservation fails,
the tree is kept, never destroyed). Around it:

- **M807** — each write child writes a **transcript** (`~/.jichi.d/attempts/<run>/t<k>.jsonl`,
  where `<run>` is the batch's id, `<sid>-b<k>` since M860 -- before it, every batch of a
  session shared one directory and one id;
  one line per model reply and tool call, born redacted), the reasoning record a
  successor reads.
- **M808** — the preserved commit's message is the **end record** (task, reason,
  model, run, counters, the loop count, the transcript path).
- **M809** — the result opens with a **Kept** section listing each preserved state
  and its ref, so the lead reads them first.
- **M815** — `jichi attempts show <ref>` prints one kept state's end record, a
  diffstat against its base, and its transcript tail, without materialising it.
- **M816** — a later `spawn_parallel` task can **resume** a kept state with
  `"from":"<ref>"`: its work is 3-way merged onto the current base and its
  outside-tree effects are noted, so a successor builds on the work. Since
  **M856** that work is the whole range from the base it started from -- the
  predecessor's own commits, then what it left uncommitted -- and the note says
  how many files it applied, or that nothing was. Before M856 only the kept
  commit's own diff was applied, so a predecessor that committed as it went was
  resumed without its commits.

The team-scale walkthrough of all of this is
[DRIVING_A_TEAM.md](DRIVING_A_TEAM.md) §2.4.

### Per-child verify gate (M144, opt-in)

The top-level verify gate checks the tree only *after* the merge — by then an
unverified child's breakage is already in. With `"parallelVerify": true`
(or `--parallel-verify`; `--no-parallel-verify` overrides the config), the
parent runs the configured verifier — the envelope's command, else config
`verify`, else `testCommand` — **inside each write child's worktree before
merging it**. Green children merge exactly as before; a **red child is
quarantined**: its changes are not merged, its task section is flagged, the
merge summary counts it, and an excerpt of its verifier output is included
so the caller sees *why* without re-running anything (plus a
`parallel_verify` journal event when the envelope is active). Since **M862** the
excerpt lists first the lines that look like failures (at most 12, each cut at 200
bytes, with a count of the rest), then the last 500 bytes, and the result names a
file holding the whole output, a claim ticket in the lead's store: zig prints its
failing tests before its summary, so a tail alone showed "3 failed" and no names. The live board
shows `verify (worktree)` / `verify red: not merged` per child. Off by
default: the verifier runs once per write child (cost/latency), and a
project whose tests can't run inside a fresh worktree (absolute paths,
running services) would quarantine everything — opt in when the verifier is
worktree-clean.

### A child that did not end on its own (M854)

Three endings used to leave a write child "without a result", its committed work kept
but never merged: **stopped from outside** (a SIGTERM to the child, a supervisor's
move), **out of context** (the provider refused a request as larger than the model's
window), and **capped** (`maxSubagentIters`). Such a child has often finished: a
drive's worker that had committed its fix was stopped while it looped, and two of
four overflowed children had committed finished work. Since **M854** the child
**answers with a note** — its last text, if it wrote any, and one line saying how it
ended — and its work merges **only after a green worktree verify** (the gate above).
With no verify (`parallelVerify` off, or no verifier) it is **kept**, not merged,
under the reason `parallel-unverified`, and the Kept list names its ref.

- **Changed behaviour:** a capped child that *had* written text merged unverified
  before M854. It now needs a green verify like the others.
- **Since M858, a fourth ending:** a turn that ended on a reply **cut off at the
  output ceiling** (jichi's own `max_tokens`, a fifth of `contextLength` capped at
  16,384 when no `maxTokens` is set). Its text stops mid-sentence and used to read like an
  answer; now the note says it was cut, with the ceiling, and the same gate applies.
  A reply counts as cut when its `finish_reason` is `length` or its
  `completion_tokens` reached `max_tokens` (a cut tool call reports `tool_calls` on
  some gateways). The delegate report says so too, for `spawn_subagent` as well.
  Since **M859** a delegate cut that way is first asked once to continue
  (`continueCutReply`, default `"delegates"`); only a second cut is such an ending.
- **Since M861, a budget:** a child that reached its task's `max_tool_calls`
  ("reached the max_tool_calls set for it", with the number), or its share of the
  run's budget ("reached its share of the run's budget"). The second is a **changed
  behaviour**: before M861 such a child merged unverified whenever it had written text.
- **Since M885, the report agrees with the note.** For a child stopped from outside, the
  `[delegate]` block said "the operator interrupted the run -- stop and report, do not
  restart it", and gave the call the SIGTERM killed a limit's advice ("split it"). It now
  says the child was stopped from outside the run (SIGTERM), that the lead's own run goes
  on, and that the call was killed because the run was being stopped. The demos drive's
  run 0102 is why: its lead wrote the operator's interruption into the team's log.
- A child the **batch** stops is not one of these: the parent kills a child on its
  `parallelTaskTimeout` or a batch abort with the pipe already closed, so it never
  reports, and a batch abort never merges. A timed-out child is kept as before
  (DEFERRED def-300).

### A task's own tool-call budget (M861)

A lead can bound one task with `max_tool_calls`: `{"task": "...", "write": true,
"max_tool_calls": 150}`. A worker cannot see its own call count, so a budget in the task
text ("past about 150 calls, stop") is one it cannot keep: the demos drive's workers went
to 189 and 302 calls under that sentence.
- **It binds in the child,** with or without `--max-tool-calls`. The child counts its
  attempted calls, including those of a delegate it starts. With an envelope, the task's
  share of the run's remaining calls binds as well, and the smaller of the two wins.
- **The task is told once,** at four-fifths of the budget it actually has: "You have used
  120 of the 150 tool calls this task allows. Finish the step you are on, then end your
  turn with a short report: what is done, and what is left." A budget under 5 gets no
  note. A task without the key gets none, even when the run's share bounds it: M347 keeps
  the run's budget notice out of a delegate's history.
- **At the budget the task ends** with stop `task_budget`, its work kept, and it is one of
  the endings above: it merges only after a green worktree verify. Its note counts the calls
  as the budget did, a delegate's included ("after 6 tool call(s), its delegates' included").
- **Read leniently:** a number, or a string of digits. Any other value (0, a negative, a
  fraction, words) refuses that task with an error that names the key, so a lead never
  believes a budget binds when it does not.

### A child's `git stash` (M855)

A write child's worktree is a *linked* worktree of jichi's shadow repository, and linked
worktrees share **one** `refs/stash`: measured with git 2.43, one worktree's `git stash pop`
applied another worktree's entry. So every write child of a batch stashes onto the same
stack, a stashed change is not in the worktree that jichi keeps or merges, and an entry
outlives its batch, where a later child's `git stash pop` would apply it. Since **M855**:

- before a batch with write tasks starts, and after it ends, every entry on that stack is
  **kept** as a ref (`refs/jichi/discarded/parallel-stash/…`, or `parallel-stash-earlier/…`
  for one found at the start) and the stack is emptied; the batch result lists them under
  **Kept stash entries**, and `jichi attempts show <ref>` reads one;
- a write child that runs `git stash` (in its shell, or with the `git_stash` tool) is told in
  the result that the stash is shared and not kept, and to commit instead.

An entry cannot be traced to the child that made it: its parent is the batch's shared base
unless that child had committed. Within one batch, two children that stash can still pop
each other's entries (DEFERRED def-301). With `preserveDiscarded: false` nothing is kept, so
the stack is left as it is.

### A child that changed an existing test assertion (M865)

A worker whose task lets it edit a test file can turn its verify green by moving an expected
value instead of fixing the code: the demos drive's card 215 got "expected 47, found 43", and
setting the pin to 43 would have merged the drive's tree from 47 to 43. Since **M865**, before a
write child's changes merge, jichi diffs each test-looking file it **modified** (not one it
created) against the base. A hunk whose removed and added lines both carry an assertion marker,
the old text not kept (M88's heuristic), **holds the merge**: the work is kept as
`parallel-test-edit` in the Kept list, and the task's note names the files. It applies after a
green verify too, and to a shell rewrite as much as to `edit_file`. Adding a test, or a new test
file, merges as before. An honest pin update is taken from the Kept list on purpose.

### A task that must not merge: `"merge": false` (M890)

A probe's work is evidence, not a change: on the night of 2026-10-03 the demos drive had a
probe's code reach the live tree four times in three runs -- two of them capped research probes
merging after a green verify, one of those only half (first writer kept by file) -- and each was
taken out by hand. Since **M890** a write task with `"merge": false` is **always kept
and named, never merged** -- at any ending, and after any verify. Its note says so, it appears in
the Kept list as `parallel-no-merge`, and `jichi attempts show <ref>` reads it. The field is read
leniently, so `"false"` counts. A task's agent profile saying "changes no repository file" is
prose; the field is the fence.

### A child's new directories (M888)

A write child's scratch in its worktree used to reach the live tree with the rest of its work:
the demos drive's run 0102 merged `oracle_scratch/`, 15 MB and 189 files of copied demo
projects, and run 0085 a 963-file copy. Since **M888** the batch result names every **new
directory** a write task adds -- for each added path, the highest ancestor the live tree does
not have -- with its file count and size:

```
[jichi] This task added new directories: oracle_scratch/ (189 files, 15.0 MB, HELD); tmp/ (1 file, 12 bytes). ...
```

One over **1 MB or 100 files** is **held**: its files stay out of the merge, the task is kept as
`parallel-new-dir` in the Kept list, and the rest of the task merges as usual. A small new
directory, such as a one-file `tmp/`, is named and merged. Take a held directory on purpose
from the Kept list.

### A metric gate for a moved pin (M875)

M865 holds every moved assertion, and on the demos drive that is every honest flip: each bumps one
pin, seven of seven (N46). The operator's decision of 2026-10-02 keeps the hold as the default and
adds a gate to choose instead, in the configuration:

```json
"parallelTestEdits": "metric",
"parallelMetric": {"command": "zig build census", "label": "L1 in scope:", "better": "higher",
                   "pins": ["src/demos/scoreboard_test.zig"], "timeoutSecs": 1200}
```

In `"metric"` mode a write child whose moved assertions are **all** in the `pins` (paths or globs) is
decided by the metric. jichi runs `command` once per batch in a fresh worktree of the base, and in
the child's worktree. The number is the first one after `label` (or the last one printed). Not
worse than the base (`better`: `higher` or `lower`) merges, and the note gives both numbers. Worse
is kept as `parallel-metric`, and a measurement that fails or finds no number is held. A moved
assertion outside the pins is held, as in `"hold"` mode. `"metric"` without a command or pins holds,
with a warning at config load. Both keys are GATED in a project config. Each verdict is a
`metric_gate` event, which `jichi telemetry` counts. The design, the rejected alternatives and the
limits (the metric runs the worktree's own code) are in
[proposals/2026-10-metric-gate.md](proposals/2026-10-metric-gate.md).

### A child's discarding git commands (M863)

A worker that wants to compare its fix with the base often takes the fix out of its tree to do
it: the demos drive's workers ran `git checkout <file>`, `git reset --hard` and `git checkout
<commit>` against task texts that forbade them, and lost their own work. Since **M863**, before a
write child's shell runs `checkout`, `switch`, `restore`, `reset`, `clean` or `rebase`, jichi
**keeps the worktree as it is** -- its files, tracked and untracked, on its HEAD -- under
`refs/jichi/discarded/pre-discard/<time>-<pid>-<n>`, and the tool result names the ref, with
`git diff <ref>` and `git checkout <ref> -- <path>` to use it. The task's part of the batch
result lists the refs.

- **The command still does what it would have done:** the state is built in a temporary index,
  so the worktree's HEAD, index and files are not touched.
- **Harmless forms are left alone:** a new branch (`checkout -b`, `switch -c`), unstaging
  (`restore --staged`, a bare `reset`), a dry run (`clean -n`), `--help`. So is a state with
  nothing to lose (no changes, HEAD at the base) and one already kept.
- `git stash` is M855's (above). A worker can still see jichi's own checkpoint commits through
  git, and check one out; that is DEFERRED def-306.

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
than silent. Since **M891** each task's section also names the model it ran on, and says when
that was the lead's own because neither the task nor its profile named one -- a probe batch that
named none once ran on the lead's 27B at 7-23 s a call. **Read the result text, not the ok-rate**: in telemetry the whole call appears
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
