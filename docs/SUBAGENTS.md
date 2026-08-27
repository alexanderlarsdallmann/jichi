# Subagents (`spawn_subagent`)

`jichi` supports **synchronous subagent delegation**: the main agent can
call the built-in `spawn_subagent` tool to hand a scoped subtask to a fresh
nested agent that runs to completion and returns its final answer as the tool
result. This is the same idea as Claude Code's Task/subagent tool, adapted to
this single-threaded C89 codebase — the nested run sits on the call stack; there
is no concurrency. A hands-on, step-by-step introduction — including when NOT
to delegate — is [TUTORIAL_ORCHESTRATION.md](TUTORIAL_ORCHESTRATION.md).

Use it for self-contained subtasks: research a question, review a change,
or implement an isolated piece — optionally on a cheaper/faster model and/or
fenced to read-only tools.

## The tool

`spawn_subagent` arguments:

| Arg | Type | Required | Meaning |
|-----|------|----------|---------|
| `task` | string | yes | The instruction the subagent completes autonomously, then reports a concise final answer on. |
| `model` | string | no | Which configured model the subagent runs on: a name/id substring, a 1-based index, or a role (e.g. `"summarize"`). Defaults to the current model. |
| `readonly` | bool | no | If true, the subagent sees only read-only tools (good for research/review). Default false. |
| `agent` | string | no | A named agent profile to run under (see *Named profiles* below). |
| `skill` | string | no | A skill to seed the subagent with: its body is prepended to the task. When that skill declares `restrict-tools: true`, its `tools` list also becomes the subagent's allow-list — the one place a skill *enforces* tools (W2). |

The tool returns the subagent's final assistant text — and **only** that text. The
subagent's token spend, tool-call count and the files it changed are known to the
process and are not part of the result, so the parent model cannot see what a
delegate cost or touched. A run that stopped early rather than finishing says so in
a bracketed note (its iteration cap, or the run's exhausted budget).

The subagent's intermediate tool calls never enter the parent's *conversation* —
that is the point of delegating, and it is what keeps the detail out of the parent's
context window. They are shown to the **human** in the TUI, indented by depth, when
the front end opts in (`app->stream_subagents`); see *Live status* below. Headless
and ACP pass no status sink, so there they stay entirely internal.

## Semantics

- **The spawn is gated, the subagent is sandboxed.** `spawn_subagent` is a
  *mutating* tool, so calling it goes through the parent's normal permission
  gate (it asks in chat mode, runs in auto mode, and is hidden in plan/read-only
  mode — you can only delegate-to-act from an acting posture). Once the spawn is
  allowed, the subagent runs **auto-approved within its tool sandbox**.
- **DENY still dominates.** A subagent never runs a tool that the config
  `permissions.deny` or an MCP server's deny list forbids — those are refused
  inside the subagent exactly as for the main agent.
- **Read-only floor.** A read-only parent (plan mode / `--readonly`) forces a
  read-only subagent regardless of the `readonly` arg; `readonly:true` can only
  tighten, never loosen.
- **Its own model.** With a `model` arg the subagent streams against a separate
  provider for the chosen model; otherwise it reuses the parent's provider.
- **Final answer.** The tool result is the subagent's last assistant message.
  If it produced none (e.g. it only made tool calls and hit its iteration cap),
  the tool reports an error result.

## Named profiles

Instead of passing `model`/`readonly`/a system prompt every time, define a
reusable **agent profile** as a markdown file:

- Project: `.jichi/agents/*.md`; Global: `~/.config/jichi/agents/*.md`.
- The basename is the profile name; a project profile overrides a global one.

```markdown
---
description: Terse code reviewer
model: fast
readonly: true
tools:
  - read_file
  - search_code
  - git_diff
---
You are a code reviewer. Report only concrete issues, most important first.
```

Then `spawn_subagent { "agent": "reviewer", "task": "Review the diff" }` runs the
subagent under that profile's body (its system prompt), model, and read-only
setting. Explicit `model`/`readonly` args on the call **override** the profile.
The body is the delegate's *identity paragraph*; the sections jichi enforces at
any depth — the environment, the untrusted-content rule, active constraints and
the edit-scope globs — are appended after it (M596). Before M596 the bare profile
text was the whole prompt, so a profiled delegate was fenced by rules it had
never been shown — the M434 defect, one path over.

The `tools` frontmatter key is an **enforced allow-list**: when the profile runs
as a subagent, only those tools are advertised to the model, and any other tool
the model tries is refused as a backstop (`load_skill` is always exempt). An
empty/absent `tools` list means no restriction. It composes with `readonly` (a
tool must pass both), so the reviewer above is limited to exactly
`read_file`/`search_code`/`git_diff` — genuinely least-privilege, not just
"can't write". The same enforcement applies to profiles used by
`spawn_parallel`. Internally the check is the pure `jc_tool_allowed`, shared with
the skill fence (see [`SKILLS.md`](SKILLS.md)).

### Tools a delegate is never offered (M436)

Three tools act on state that belongs to the **main agent** — `todowrite`, `todoread`
and `board`. The task list is shown to the human, outlives the subtask and is saved
with the session (M606), and the
board is a shared project artifact, so a delegate overwriting either would be
overwriting something it cannot see the whole of. They are therefore **omitted from a
subagent's tool list** at any depth, and a call made anyway is refused with a message
naming the alternative: report what you would have recorded in your final answer, and
the agent that delegated the task can act on it.

This is one field on the tool definition (`main_agent_only`), read by the tool-array
builder for the omission and by `jc_tool_execute` for the backstop — the same shape as
`readonly`. Until M436 only the runtime refusal existed, so all three were advertised
to every subagent and then refused when called: on a backend without prompt caching,
at 25–42k input tokens per call, an entirely avoidable cost for information the
omission carries for free.

## What the delegation returns (M437)

Both delegation tools append a bounded `[delegate]` block to their tool result:

```
[delegate] stop=max_iters · 34 tool calls · 41.2k tokens
[delegate] last failing call: write_file (denied) -- refused: docs/x.md is outside
           this run's edit scope
[delegate] a policy refusal, not an accident: re-delegating the same task will be
           refused again. Either do that part within what you are permitted, or say
           in your final answer what the task needs and was denied.
[delegate] it ran out of iterations, so this answer may be partial -- re-delegate a
           NARROWER subtask, or finish the remainder yourself.
```

`stop` is one of `done`, `max_iters`, `budget`, `no_answer`, `aborted`, `error`. Before
M437 a failed delegation returned one of two fixed strings, so a parent could not tell
an edit-scope denial from a tool error from a refusal — and its only moves were to
re-delegate identically, paying the whole subtask twice, or give up. Each line names the
parent's **next move**, since a message stating only a cause is what makes a caller
retry.

**A clean, unmeasured delegation prints nothing.** An arriving answer already says the
delegate finished; a block repeating that on every call is cost with no information.

Three limits, stated rather than implied:

- `tool_calls` and `tokens` come from the envelope, so they exist under `--auto` and are
  **absent otherwise** — never reported as 0, which would assert the delegate made no
  calls.
- A call the edit-scope fence **blocked** counts as 0 *executed* calls while still being
  reported as denied. Both facts are true and the report says both.
- `files changed` is populated for a `spawn_parallel` **write child** only. Its worktree
  gives a per-delegate baseline; a nested in-process subagent has none, and creating one
  would mean a shadow checkpoint per delegation showing up in the user's `/undo` stack.

## Recursion safety

Orchestration is **depth-bounded** — `maxSubagentDepth` (config, default **2**)
sets how deep nesting may go. At the default the top-level (main) agent may spawn
a subagent that itself spawns one grandchild (bounded two-level nesting); set it
to `1` for the historical single-level behaviour, or `0` (the `--lite` default) to
disable spawning entirely. One pure predicate is the single gate:

```c
int jc_subagent_can_spawn(int agent_depth, int max_depth)
{
    return agent_depth < max_depth;          /* child runs at agent_depth+1 */
}
```

Two coordinated guards consult it:

1. **Tool exclusion (the real gate)** — `jc_agent_run_subagent` builds a
   subagent's tool set with `exclude_tool = "spawn_parallel"` (always — no nested
   fork pools) and `exclude_tool2 = jc_subagent_can_spawn(app->agent_depth,
   max_depth) ? NULL : "spawn_subagent"`. So a subagent below the cap *does* see
   `spawn_subagent` advertised and can delegate one synchronous sub-subagent;
   once at the cap it never sees it.
2. **Depth cap (backstop)** — `spawn_subagent` / `spawn_parallel` re-check the
   same predicate against `app->agent_depth` before running and refuse with
   "depth limit reached" if it returns false.

`spawn_parallel` stays **top-level only** regardless of the cap (it would
otherwise nest fork pools). So `maxSubagentDepth: 2` means: main (depth 0) →
subagent (depth 1) → sub-subagent (depth 2), each level synchronous.

Each level is also bounded by `maxSubagentIters` (the subagent's tool-iteration
cap; defaults to `maxToolIters`), **tapered per depth** so a deep synchronous
chain shares rather than multiplies the total tool-call budget: the pure
`jc_subagent_iters_at_depth(base, depth)` halves the base per level and floors it
at `JC_SUBAGENT_MIN_ITERS` (4). A depth-1 subagent gets ~half the base, a depth-2
grandchild ~a quarter (min 4). The shared `SIGINT` abort flag interrupts a running
subagent at any level, and the TUI banner shows `[depth N]` for a nested spawn.

## Live status (TUI)

In the **interactive TUI**, a spawned subagent is no longer a black box: a dim
banner announces it (`subagent <model> · ro: <task>`), and its nested activity —
tool calls and streamed text — renders live, **indented by depth**, with a
per-subagent token line at the end. This is wired by forwarding the active
callbacks into the nested run (`app->cb`), gated by `app->stream_subagents`.

The gate is set **only by the TUI, and only when not quiet** — so headless and
ACP keep passing `NULL` into the subagent (exactly as before: silent, with only
the final answer reaching stdout), and `-q` / `/quiet` collapses a subagent back
to its banner + result. This preserves the headless/SSH contract that stdout
carries only the top-level answer.

## Configuration

```json
{
  "model": { "...": "..." },
  "models": [ /* a subagent's `model` arg selects from these */ ],
  "maxSubagentDepth": 2,
  "maxSubagentIters": 25
}
```

`maxSubagentDepth` is the nesting ceiling: `1` keeps a single level
(main → subagent); `2` (default) lets a subagent delegate to one sub-subagent; and
so on. `spawn_parallel` is unaffected — it only ever runs at the top level. The
per-subagent iteration budget is tapered by depth (halved per level, floored at 4)
so raising the ceiling can't multiply the total tool-call budget combinatorially.

## Example

```sh
./jichi --auto -p "Use spawn_subagent (model \"fast\", readonly true) to \
  find where SSE events are parsed, then summarize the approach."
```

The main agent calls `spawn_subagent`; the subagent investigates with read-only
tools on the `fast` model and returns a summary, which the main agent relays.

## Limitations

- **Synchronous & single-threaded** — one subagent runs at a time, blocking the
  parent until it finishes. For concurrent fan-out use `spawn_parallel`
  ([PARALLEL.md](PARALLEL.md)), which is top-level only.
- **Fire-and-forget** — the parent cannot send a running subagent a further
  instruction, and cannot see its progress. It writes the task, waits, and reads
  one final answer. Nor can a *human* steer one: type-ahead and the control
  socket are both served at the top level only.
- **Prose, not a report** — see *The tool* above: the answer comes back without
  cost, without a machine-readable stop reason, and without the list of files the
  subagent changed.
- **It does not inherit the parent's context** — no project rules, no memory, no
  glossary, no repo map. Deliberate (that isolation is why delegating saves
  context), but it means the task text must carry everything the subagent needs.
- **Arena growth** — the process arena is not reset per turn, so a subagent's
  allocations add to it for the life of the session. Fine for typical use.

## Implementation

- `src/tools/jc_tool_subagent.c` — the tool, arg handling, model resolution
  (`jc_subagent_resolve_model`), and the run flow.
- `src/chat/jc_agent.c` — the shared `run_agent_loop` core (parameterized by
  `struct jc_run_opts`), with `jc_agent_run_turn` and `jc_agent_run_subagent`
  as thin wrappers, plus `jc_agent_last_assistant_text`.
- `src/tools/jc_tool.c` — `jc_tool_build_neutral_ex` hides `spawn_subagent` from
  a subagent once the depth cap is reached (and `spawn_parallel` always).
- `jc_subagent_can_spawn(agent_depth, max_depth)` (`src/chat/jc_agent.c`,
  declared in `include/jc_agent.h`) — the pure depth predicate, unit-tested in
  `tests/test_subagent.c`.
- `src/chat/jc_sysmsg.c` — `jc_sysmsg_build_sub` gives the subagent its framing.
- `include/jc_app.h` — `agent_depth`; `include/jc_config.h` —
  `maxSubagentDepth`/`maxSubagentIters`.
