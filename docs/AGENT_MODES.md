# Agent Modes & Bounded Autonomy

This document describes how `jichi` lets the agent operate autonomously
*within constraints*: named operating **modes** (plan / chat / auto) plus a
per-tool **permission policy**. The model is intentionally close to Claude
Code's plan-mode / auto-accept concepts, adapted to this C89 codebase.

## Your first hour

If you have never run an agent before, do these seven things in order. It takes
about ten minutes and it teaches the whole permission model by using it.

```sh
cd /path/to/a/scratch/copy/of/your/project
jichi
```

1. **Ask a question that only reads.** *"What does this project build, and which
   file is the entry point?"* Watch the tool lines appear (`▸ read_file`,
   `▸ search_code`). Nothing asked your permission, because reading does not
   change anything — that is the chat-mode baseline.
2. **Ask for a change, then say no.** *"Add a comment at the top of the README
   saying hello."* Now you get an approval prompt naming the tool and the file,
   with the keys `[y]`es `[n]`o `[a]`lways `[e]`dit `[v]`iew. Press **`n`**.
   The model is told you declined and continues without the edit.
3. **Ask again and say yes.** Same request, press **`y`**. The edit lands.
4. **Look at what changed:** type `/diff`. This is the workspace diff, not the
   model's description of it — read the diff, not the summary.
5. **Take it back:** type `/undo`. The file returns to its previous state. This
   works because jichi checkpointed the tree *before* the first mutating call, in
   a shadow git repository that never touches your own `.git`.
6. **Look at the session:** `/context` shows what the model is being told and
   what it costs; `/checkpoints` lists the points you can return to.
7. **Now try the other two postures.** Restart with `jichi --plan` and ask for
   the same edit: it is **refused**, because plan mode is a read-only fence — the
   model answers with a plan instead. Then restart with `jichi --auto` and ask
   again: it edits **without asking**, which is exactly why `--auto` belongs with
   the bounded envelope described in [AUTONOMY.md](AUTONOMY.md).

**What you have learned by step 7** is the whole shape: a *mode* sets the
baseline, an *approval* is the interactive escape hatch, and `/undo` means the
worst case of a wrong "yes" is thirty seconds. If you want to know exactly who
else can veto a call after you approve it — a constraint, the privileged gate, the
edit-scope fence, a hook — that chain is
[TOOL_DECISIONS.md](TOOL_DECISIONS.md).

## Motivation

The agent calls tools (read/write files, run commands, MCP tools) in a loop.
Two questions decide how autonomous it is:

1. **What can it see?** — which tools are advertised to the model.
2. **What runs without asking?** — which calls execute vs. prompt vs. refuse.

Originally only two coarse, startup-only switches existed: `--auto`
(auto-approve everything) and `--readonly` (hide mutating tools). That left no
"investigate-then-plan" mode, no way to change posture mid-session, and no
middle tier where the agent works freely on safe actions but asks before risky
ones. Modes + a permission policy fill that gap.

## Modes

A **mode** is a posture that sets the low-level switches (`readonly`,
`auto_approve`) via `jc_app_set_mode()`. Mode is held on `jc_app` as `int mode`
(`enum jc_agent_mode`).

| Mode  | readonly | auto_approve | System prompt | Use it to… |
|-------|----------|--------------|---------------|------------|
| **chat** (default) | 0 | 0 | normal | Work interactively; safe tools run, changes ask. |
| **plan** | 1 | 0 | + "produce a plan, change nothing" | Have the agent investigate and propose a plan first. |
| **auto** | 0 | 1 | + "run autonomously within the budget" | Let the agent execute on its own, bounded by limits. |

Modes are not a substitute for the permission policy — they set its *baseline*.

## Whatever the mode allows, a turn can be taken back

New here, and nervous about what the agent might do to your files? That is the
right instinct, and the answer is not the mode — it is the checkpoint. Before the
first file-changing tool of **each turn**, jichi snapshots your workspace, and
`/undo` (or `jichi undo`) restores it. One checkpoint per turn, so `/undo` takes
back everything that turn changed, not just the last edit.

Two honest caveats, both in [SNAPSHOTS.md](SNAPSHOTS.md): files your
`.gitignore` excludes are **not** captured and cannot be restored, and snapshots
switch themselves off in a few situations (including the low-RAM profile) — so
confirm with `/status` or `jichi doctor` before relying on them.

**If you are starting out:** stay in **chat** mode, and use **plan** when you want
to see the agent's intentions before any action. Reach for **auto** only in a
git-managed directory, and only with a verifier — see [AUTONOMY.md](AUTONOMY.md).

## Permission model

Every tool call is resolved to one verdict by the pure function
`jc_perm_for_tool()` (`include/jc_perm.h`, `src/chat/jc_perm.c`):

```
enum jc_approval { JC_APPROVAL_ASK, JC_APPROVAL_ALLOW, JC_APPROVAL_DENY };
```

It composes three layers — the **mode** baseline, the top-level config
**allow/deny** lists (tool names), and the **MCP** per-server policy — with
**deny dominant**. Rules are applied in order; the first decisive one wins:

1. tool in `permissions.deny` **or** MCP policy is DENY → **DENY**
2. mode is **plan** and the tool is mutating → **DENY** (even if allow-listed —
   plan mode changes nothing)
3. tool in `permissions.allow` → **ALLOW**
4. MCP policy is ALLOW → **ALLOW**
5. baseline: **auto** → ALLOW; **chat**/**plan** → ALLOW if the tool is
   read-only, else ASK

### Truth table

| mode | tool | allow | deny | mcp | verdict |
|------|------|-------|------|-----|---------|
| any  | any        | any | yes | any   | DENY  |
| any  | any        | any | no  | DENY  | DENY  |
| plan | mutating   | any | no  | ASK   | DENY  |
| plan | read-only  | no  | no  | ASK   | ALLOW |
| auto | any        | no  | no  | ASK   | ALLOW |
| chat | read-only  | no  | no  | ASK   | ALLOW |
| chat | mutating   | no  | no  | ASK   | ASK   |
| chat | mutating   | yes | no  | ASK   | ALLOW |
| chat | mutating   | no  | no  | ALLOW | ALLOW |

The agent loop acts on the verdict:

- **DENY** → the tool is hidden from the model *and* refused if named anyway.
- **ASK** → prompt the user (interactive). In **headless** mode (`-p`), where
  there is no prompt, an ASK is **refused** with a message telling the model to
  re-run with `--auto`.
- **ALLOW** → execute.

**Privileged commands sit below the verdict (M153).** A `run_terminal_command`
whose command is launched under `sudo`/`doas`/`pkexec`/`su`/`run0` is gated
*after* the verdict by a separate policy (`privilegedCommands: ask|deny|allow`
-- and every attempt is recorded to the audit log unless `privilegedAudit`
is set to false, which disables that sink; `doctor` warns about it and,
under `--unattended`, fails on it --
default `ask`) that the blanket **AUTO**/allow-list/`always` grant **cannot**
satisfy — because that command-blind grant is exactly what let an agent
escalate privilege unbidden. Under `ask`, an interactive TUI prompts afresh
every time (never covered by a prior "always"); an unattended run (headless
AUTO, a subagent) refuses. Every privileged attempt is recorded to an always-on
audit log. See
[proposals/2026-07-privileged-commands.md](proposals/2026-07-privileged-commands.md).

Built-in tools are read-only (`read_file`, `list_files`, `search_code`,
`fetch_url`, `codebase_search`) or mutating (`write_file`, `edit_file`,
`run_terminal_command`).

## Configuration

```json
{
  "model": { "...": "..." },
  "mode": "chat",
  "permissions": {
    "allow": ["edit_file"],
    "deny": ["run_terminal_command"]
  }
}
```

- **`mode`** — `"chat"` | `"plan"` | `"auto"`; the default mode at startup.
- **`permissions.allow` / `permissions.deny`** — each a list of tool names, or
  `"*"`/`true` for all tools. `deny` wins over `allow`. Names match the
  *registered* name: a built-in's short name (`write_file`) or an MCP tool's
  namespaced name (`server__tool`). This is distinct from a server's own
  `autoApprove`/`deny` (which use bare, un-namespaced names) — both are honored.

### CLI flags (override the config default mode)

- `--plan` — start in plan mode.
- `--auto` — start in auto mode.
- `--readonly` — pin a read-only fence at startup (independent of mode).
- Precedence: `--plan` beats `--auto` (plan is the safer of the two).

## Runtime control (interactive TUI)

The prompt shows the current mode, e.g. `[plan] >`.

- `/mode` — show the current mode.
- `/mode chat|plan|auto` — switch mode.
- `/plan` — enter plan mode; `/plan off` — return to chat.
- `/auto` — toggle between auto and chat.

A mode switch takes effect on the next turn (the system prompt and the
advertised tool list are rebuilt each turn).

## Plan-then-execute workflow

1. Start in plan mode (`--plan`, or `/plan`). Mutating tools are hidden; the
   system prompt asks the model to investigate and produce a step-by-step plan
   without changing anything.
2. Review the plan the model prints.
3. Switch to chat (`/plan off`) or auto (`/auto`) to let it carry the plan out.
   The plan stays in the conversation history as context.

## Edge cases

- **Headless ASK → refuse.** Without an interactive prompt, a tool needing
  approval is refused (re-run with `--auto`). This is safer than silently
  running it.
- **`--readonly` pinning.** `--readonly` sets a read-only fence at startup;
  after that, switching modes in the TUI lets the mode own the fence.
- **deny = hide + refuse.** A denied tool is both omitted from the advertised
  tool list and refused at call time (the same backstop MCP deny uses).
- **Mode persists across `--resume`.** The active mode is saved with the
  session and restored on resume. An explicit `--plan`/`--auto` on the resume
  command line overrides the saved mode for that run.
- **Subagents.** `spawn_subagent` is a mutating tool, so it follows the mode
  rules above (asks in chat, runs in auto, hidden in plan/read-only). Once a
  spawn is allowed, the subagent runs with an auto-approve posture *inside its
  sandbox* — but the same DENY rules still dominate, and a read-only parent
  forces a read-only subagent. See `docs/SUBAGENTS.md`.

## When a turn stops early: the iteration cap (M322)

Every turn is bounded by **`maxToolIters`** (default 25) — the number of tool-call rounds one
turn may take. Hitting it ends the turn. This is a deliberate design with three properties
worth knowing, because they are what make the behaviour useful rather than merely safe:

**1. It is a circuit breaker, not a failure.** The loop returns success, the exit code stays
**0**, and nothing is rolled back. Everything the turn did — every edit, every command — is
real and stays. The cap exists to stop a model looping on the same tool, and a model that
loops has usually still done useful work first. Treating that as an error would invite a
caller to discard or revert work that is fine.

**2. The conversation is intact, so a nudge resumes it.** The assistant's tool calls and their
results are all in the history. Type anything (or send another prompt headlessly with
`--continue`) and the model sees where it was and carries on with a **fresh** iteration
budget. `continue` is enough; a more specific instruction is better. This is why the cap feels
like a pause rather than a wall — and it is the same property `--continue` and `/resume` rely
on.

**3. What you are told depends on the surface:**

| Surface | What you see |
|---|---|
| TUI / headless text | `[jichi warn] hit max tool iterations (25)` on **stderr** |
| `--output json` / `jsonl` | `stop_reason: "max_iters"`, usually with an empty `text` |
| exit code | **0** — nothing failed |
| a subagent that caps | its partial answer is returned with `[stopped at its iteration limit]` (M62) |

The `stop_reason` is the M322 half. Before it, a machine driver was told
`stop_reason: "done"` with an empty answer — indistinguishable from *"finished and had nothing
to say"* — so a supervisor could accept a half-finished task as complete. See
[SCRIPTING.md](SCRIPTING.md#max_iters-the-turn-stopped-the-task-did-not-fail-m322) for the
supervisor pattern, including **why you should count the nudges**: an unbounded resume loop is
a runaway loop with extra steps, which is what the cap existed to prevent.

**Is this "for agentic use"?** The resumability is genuinely designed — it is what lets a
bounded run make progress across turns instead of failing. But it was designed for the
*interactive* case first, where the warning on stderr is enough; the machine signal was
missing until M322. Under an **autonomy envelope** (`--auto` with budgets) the picture is
different again: there the real bound is the token/time/tool-call budget, so jichi raises the
iteration cap to **at least 200** and lets the envelope decide when to stop — see
[AUTONOMY.md](AUTONOMY.md). If you are driving jichi as an agent, prefer an envelope bound
over a tight `maxToolIters`: the envelope reports *why* it stopped and can keep or roll back
work deliberately.

## Implementation map

- `include/jc_perm.h`, `src/chat/jc_perm.c` — the modes, the `jc_approval`
  enum, and the pure `jc_perm_for_tool` resolver (unit-tested in
  `tests/test_perm.c`).
- `src/chat/jc_agent.c` — calls the resolver per tool call; ASK→prompt/refuse.
- `src/tools/jc_tool.c` — `jc_tool_build_neutral` hides denied tools.
- `src/chat/jc_sysmsg.c` — mode-aware system prompt.
- `src/config/jc_config.c` — parses `mode` and `permissions`.
- `src/chat/jc_app.c` — `jc_app_set_mode`.
- `src/main.c` — `--plan` and startup mode resolution.
- `src/tui/jc_tui.c` — `/mode`, `/plan`, `/auto`, and the mode in the prompt.
