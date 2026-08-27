# Agent skills

Skills are **model-invoked, progressively-disclosed instruction sets**. A skill
is a folder with a `SKILL.md` describing a capability (e.g. "release checklist",
"write a conventional commit", "run the data-import pipeline"). The agent sees
only each skill's *name and description*; when a task matches, it calls the
`load_skill` tool to pull the full instructions into context and follow them.

This differs from the repo's other markdown extensions:

- **Custom commands** (`.jichi/commands/*.md`) are **you**-invoked (`/name`).
- **Subagent profiles** (`.jichi/agents/*.md`) are personas for a *delegated*
  sub-agent.
- **Skills** are chosen by the **main agent itself**, on demand, and loaded into
  its own context.

## Your first skill

Four commands, and the one mistake almost everyone makes.

```sh
mkdir -p .jichi/skills/commit-style
```

Write `.jichi/skills/commit-style/SKILL.md`:

```markdown
---
name: commit-style
description: Use when writing a git commit message for this repository.
---

Write the subject in the imperative mood, under 60 characters, with no trailing
period. Then a blank line, then a body explaining WHY, wrapped at 76 columns.
```

Check jichi can see it, then use it:

```sh
jichi skills
jichi -p "Write a commit message for the change I just made."
```

**What you should see:** a tool line reading `▸ load_skill  commit-style` before
the answer. That line is the whole proof — it means the model recognised the task,
pulled your instructions into context, and followed them. No line, no skill.

**The commonest failure is the `description`.** The agent sees *only* the name and
description until it decides to load the skill, so a description that says what
the skill *is* rather than **when it applies** never gets loaded:

| | |
| --- | --- |
| ✗ `description: Commit message conventions.` | a label; nothing tells the model when to reach for it |
| ✓ `description: Use when writing a git commit message for this repository.` | names the trigger |

Write the description as the sentence *"Use when …"*. If a skill never loads, that
line is the first thing to fix — before doubting the mechanism.

## Layout

```
.jichi/skills/<name>/SKILL.md          # project skills (under the workspace)
~/.config/jichi/skills/<name>/SKILL.md   # global skills
```

A project skill overrides a global one with the same name. Each `SKILL.md` is
markdown with optional YAML frontmatter:

```markdown
---
name: changelog
description: Add an entry to CHANGELOG.md following the Keep a Changelog format.
allowed-tools:
  - read_file
  - edit_file
---

# Updating the changelog

1. Open `CHANGELOG.md` and find the `## [Unreleased]` section.
2. Add a bullet under the right heading (Added / Changed / Fixed).
3. Keep entries imperative and one line each.

You can run `scripts/check.sh` in this skill's folder to validate the format.
```

- **name** — the identifier the agent passes to `load_skill`. Defaults to the
  folder name when omitted.
- **description** — the one-line summary shown to the agent. Make it specific so
  the agent knows *when* the skill applies.
- **allowed-tools** (optional; `tools` is accepted as an alias) — a list of tools
  the skill's workflow uses. **Advisory** at the top level; **enforced** when the
  skill runs inside a sub-agent *and* `restrict-tools: true` is set. See below.
- **restrict-tools** (optional, default `false`) — when `true`, the
  `allowed-tools` list becomes an enforced fence for a sub-agent that loads this
  skill (see "Enforcing restrict-tools in sub-agents").
- **style** (optional) — the *name* of an output style
  ([OUTPUT_STYLES.md](OUTPUT_STYLES.md)) to apply while this skill is in use, so
  a specialist can carry its own tone without restating a persona. Like
  `allowed-tools` it is **applied** inside a sub-agent and only **reported** by a
  top-level `load_skill` — a tool result cannot retroactively change the prompt
  of the turn that called it. A name that resolves to no style falls back to the
  session style and is a `doctor` warning.
- The **body** (everything after the frontmatter) is the full instruction set,
  returned by `load_skill`.

## `allowed-tools` is advisory at the top level (skills do not fence the main agent)

A top-level `load_skill` **injects guidance**; it does **not** restrict the main
agent's tools. A skill's `allowed-tools` is advisory there: `load_skill` renders it
as a "Suggested tools" hint, and nothing is hidden from the model or refused.

Top-level tool restriction lives in two places that are *meant* to scope
capability:

- **Subagent profiles** (`.jichi/agents/<name>.md` `tools:`) — a delegated subagent
  is genuinely scoped to its allow-list (the `jc_tool_allowed` path).
- **Modes / permissions** — Plan mode's read-only posture and the config
  `permissions` allow/deny lists.

## Enforcing `restrict-tools` in sub-agents

A skill with `restrict-tools: true` *does* fence tools — but only inside a
**sub-agent**, whose bounded lifetime sidesteps the "lingers all turn" problem that
keeps top-level skills advisory (see the design note below). Spawn one with the
skill:

```
spawn_subagent { "task": "…", "skill": "web-audit" }
```

The sub-agent then (1) starts with `web-audit`'s full instructions pre-loaded as
its first message, and (2) if the skill declares `restrict-tools: true`, is fenced
to exactly its `allowed-tools` for the whole run — via the same `allow_tools` /
`jc_tool_allowed` path a subagent profile uses (advertise-time filter + run-time
backstop). If both an agent profile and the skill fence the sub-agent, the
effective fence is their **intersection** (`jc_tool_allow_intersect`). When the
sub-agent finishes, the fence is gone — it never touches the main agent.

```yaml
---
name: web-audit
description: Audit a page's accessibility, read-only.
restrict-tools: true
allowed-tools:
  - read_file
  - search_code
  - fetch_url
---
Steps: fetch the page, check headings/alt-text/contrast, report findings.
```

### Design note / history (for later revision)

Earlier, a skill's `allowed-tools` was a **hard fence**: loading a skill
restricted the main agent to exactly that tool set for the rest of the turn.
That backfired. A jichi skill is loaded for its *instructions* (progressive
disclosure) and is never "deactivated", so the restriction lingered for the
whole turn — and a turn can run dozens of tool iterations. On the **zigodot**
project, knowledge skills (e.g. `godot-architecture`) listed read-only
`allowed-tools` that omitted `run_terminal_command`; the agent loaded one for
*context*, then lost the ability to run commands / edit for the rest of a
72-iteration turn and flailed on read-only workarounds (`denied (skill fence)`).
See `docs/ANECDOTES.md`.

The top-level fence was therefore removed: a `load_skill` at the main agent is
guidance-only. The per-skill restriction was then reintroduced as **opt-in** and
**scoped to sub-agents** (`restrict-tools: true`, see above): the fence is built
from the already-parsed `sk->tools` but installed only for a sub-agent's bounded
lifetime, so it can never linger across a long main-agent turn the way the old
top-level fence did. Every skill stays permissive by default.

Bundled scripts/resources can live alongside `SKILL.md`; `load_skill` reports
the skill's folder so the agent can run them via `run_terminal_command`.

## Progressive disclosure

Only names + descriptions are injected into the system prompt (an "Available
skills" section). The full body is fetched on demand, so a project can ship many
skills without bloating every request's context — the agent pays for a skill's
detail only when it actually uses it.

## How the agent uses a skill

1. The system prompt lists `name: description` for each available skill.
2. When the user's request matches, the agent calls
   `load_skill {"name": "<skill>"}`.
3. The tool returns the skill's full `SKILL.md` body plus its folder path.
4. The agent follows those instructions, running any bundled scripts with
   `run_terminal_command` (skills do not restrict which tools it may use).

`load_skill` is read-only and is only advertised when at least one skill is
present.

## Listing skills

- TUI: `/skills`
- CLI: `jichi skills` (lists name + description; needs no API key)

## Implementation

`src/skill/jc_skill.c` (+ `include/jc_skill.h`) loads and parses skills (reusing
`jc_md`/`jc_yaml` for frontmatter), `src/tools/jc_tool_skill.c` is the
`load_skill` tool, and `jc_sysmsg_build` injects the catalog via
`jc_skill_render_catalog`. `allowed-tools` is parsed into `sk->tools` and
`restrict-tools` into `sk->restrict_tools`. A top-level `load_skill` renders the
list as an advisory hint only. `spawn_subagent`'s optional `skill` arg
(`src/tools/jc_tool_subagent.c`) resolves the skill via `jc_skill_find`, seeds the
sub-agent's first message with its body, and — when `restrict_tools` is set —
passes `sk->tools` (intersected with any profile fence via
`jc_tool_allow_intersect`) as the run's `allow_tools`, gating
`jc_tool_build_neutral_ex` + the run-time backstop through the shared
`jc_tool_allowed`. The parse/catalog/find/intersect helpers are pure and
unit-tested; discovery + the tool + model usage are verified end-to-end.
