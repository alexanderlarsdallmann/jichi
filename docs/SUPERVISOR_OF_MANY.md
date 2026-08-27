# One jichi supervising many

The largest jobs — a migration across dozens of files, an audit of a multi-repo
system, a change that must land on several machines — are best done by a
**supervisor** that fans work out to many jichi **workers** and merges the results.
This guide covers the three layers of that pattern and how to keep it safe.

The supervisor can be **you** (a shell script), an **automation** (CI), or a
**jichi agent** itself (driving workers through the shell or a user-defined tool).
The workers are always plain `jichi -p` invocations — local or remote — so
nothing here is a special mode; it's composition of existing pieces.

## Layer 1 — local fan-out (one machine, one process)

`spawn_parallel` runs several subtasks at once in a fork pool inside a single jichi
run. Best when the subtasks share a workspace and you want the coordinator's own
model to plan + merge.

- Read-only subtasks run in the live tree; `write:true` subtasks each run in an
  isolated **git worktree** and the parent merges changed files file-level,
  first-wins (conflicts reported, never auto-merged).
- Bounded by `maxParallelAgents`, a per-child watchdog, and a shared budget slice
  so N siblings can't overspend N×.
- Full mechanics + the live board: **[PARALLEL.md](PARALLEL.md)**; the synchronous
  single-delegate variant is **[SUBAGENTS.md](SUBAGENTS.md)**.

Use this when: the work-list fits one machine and one context can plan it.

## Layer 2 — many processes on many machines (SSH)

When the code or compute lives elsewhere, the supervisor drives a **separate jichi
process per box** over SSH and collects structured results.

```sh
# supervisor.sh — fan one task out to N hosts, collect JSON results
for host in build-a build-b gpu-1; do
  ssh "$host" 'cd ~/project && jichi -p "run the test suite and report \
    failures" --auto --output json --budget-tokens 1M' > "result.$host.json" &
done
wait
# merge: each result.$host.json has { text, stop_reason, cost, ... }
```

- Each worker is bounded by its own **autonomy envelope** (`--auto`
  `--budget-*` `--edit-scope` `--verify` `--journal`) so a runaway worker can't
  exceed its slice — see [AUTONOMY.md](AUTONOMY.md).
- `--output json` (one object) or `--output jsonl` (streamed events) makes results
  machine-mergeable — see [SCRIPTING.md](SCRIPTING.md). The terminal object carries
  `stop_reason`, `cost`, `session_id`, and a structured `error`.
- Keep each remote process **warm** with the daemon so repeated tasks don't
  cold-start (reload config/MCP/LSP/index): [DAEMON.md](DAEMON.md).
- The SSH plumbing (prompt composition, quoting, key handling):
  [REMOTE_SSH.md](REMOTE_SSH.md).

Use this when: the work is naturally partitioned by machine or repo.

## Layer 3 — a jichi agent as the supervisor

A jichi agent can itself be the coordinator: it plans the split, dispatches workers,
and reviews their reports — all as ordinary tool calls.

- **Dispatch via the shell:** the supervisor calls `run_terminal_command` to run
  `jichi -p "<subtask>" --output json` (locally) or
  `ssh host 'jichi -p …'` (remotely), then reads the JSON result back.
- **Dispatch via a user-defined tool:** wrap the worker invocation in a
  `tools[]` entry ([USER_TOOLS.md](USER_TOOLS.md)) named e.g. `delegate_to_worker`,
  so the model calls it like any other tool and gets the worker's answer as the
  result. This is cleaner than raw shell and lets you fix the safety flags.
- **Review + merge:** the supervisor reads each worker's report, resolves
  conflicts (or asks you via `ask_user`), and produces the combined change/answer.

Keep the supervisor's *own* run bounded too: a modest `--budget-tokens`, a
`read-only` or narrow `editScope` if it should only coordinate (workers do the
edits), and enforced constraints ([CONSTRAINTS.md](CONSTRAINTS.md)) so it can't,
say, run builds itself when you only want workers to.

## Safety when fanning out

Every worker inherits the same guardrails you'd give a single run — set them
per worker, not just on the supervisor:

- **Budget per worker** (`--budget-tokens`, `--max-reads`) — the pool/SSH split is
  not itself a spend cap on each child's model calls.
- **Edit scope** (`--edit-scope`) so a worker writes only where intended; worktree
  isolation (Layer 1) or a separate checkout (Layer 2) prevents cross-worker
  clobbering.
- **Verify gate** (`--verify`) so a worker's work is only kept if it stays green;
  budget-exhaustion keeps partial work unless the verifier is red (M80).
- **Memory watchdog** (`--mem-budget`) so a heavy build in one worker can't take
  down a shared machine.
- **Constraints** ("do not push", "do not deploy") are enforced at the tool gate in
  every worker that loads them — pass them via `.jichi/constraints.md` in each
  workspace.
- **Observability outside the blast radius:** write journals/telemetry outside the
  workspace (the default `~/.jichi.d/…`) so a rollback never eats your logs.

## Choosing a layer

| Work shape | Layer |
|------------|-------|
| Fits one machine; one plan; shared tree | 1 — `spawn_parallel` |
| Partitioned by machine/repo; independent | 2 — SSH + `--output json` |
| Needs a model to plan the split + review | 3 — a jichi agent supervisor (over 1 or 2) |

Start at the lowest layer that fits — it's the least to coordinate and the easiest
to make safe. Escalate only when the work genuinely spans machines or needs a model
in the coordinator's seat.

See also: [WORKFLOWS.md](WORKFLOWS.md) (all the role journeys),
[BOARD.md](BOARD.md) (tracking multi-step work), and
[proposals/2026-07-web-frontend.md](proposals/2026-07-web-frontend.md) — the
design for giving this pattern *eyes*: a web supervisor (Phoenix, or a
single-file bridge) that owns the workers, renders the fan-out as a live
board, and maps jichi's approval prompt into the browser.
