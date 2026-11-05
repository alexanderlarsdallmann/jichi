# Custom slash commands

Define reusable prompt templates as markdown files and invoke them as `/name`
in the TUI (or as the prompt in headless `-p` mode). Inspired by opencode's
custom commands.

## Where they live

- Project: `.jichi/commands/*.md` (under the working directory).
- Global: `~/.config/jichi/commands/*.md`.

The file basename (without `.md`) is the command name; a project command
overrides a global one of the same name. Built-in slash commands (`/help`,
`/model`, `/mode`, …) always take precedence, so they can't be shadowed.

## File format

Optional YAML frontmatter, then a body that is the prompt template:

```markdown
---
description: Review the working diff
agent: reviewer
---
Review the current changes. Focus on: $ARGUMENTS
```

Frontmatter keys:
- `description` — shown in `/help`.
- `agent` — **honored**: the command runs *as* that named agent profile (from
  `.jichi/agents/`). The profile's system prompt is injected for the turn and its
  `readonly` flag is applied, so e.g. a command pointing at a read-only reviewer
  can't modify files. If the named profile doesn't exist, the command just runs
  as a normal turn (no error). The profile's `tools:` list fences the command
  only when it runs on its own (`subtask: true`, below); in the main turn it does
  not apply, so the tool definitions stay byte-stable for the prompt cache. The
  profile's `model:` never applies to a command — the command's own `model:` key
  below does (the rule decided on 2026-09-30, built at M848; `attempt --agent`
  and subagents take the profile's `model:`). On a `subtask: true` command the
  profile body is the subtask's
  **identity paragraph** — it replaces the generic "focused sub-agent" framing,
  and the environment, the untrusted-content rule, the constraints and the edit
  scope are appended after it (M596; until then the subtask path dropped the
  profile entirely, so the scaffolded mentor never saw its own instructions).
- `model` — **honored**: switch the active model to the named one (a model
  name/id substring or 1-based index, resolved like `/model`) for just this
  turn, then switch back. Unknown/empty names are a graceful no-op.
- `subtask` — **honored**: when `true`, the command runs as a sub-agent on a
  throwaway history, so its intermediate tool calls and reasoning stay out of the
  main conversation; only the prompt and the subtask's final answer are
  recorded. The *history* is isolated, not the effects: a subtask runs under
  **your** posture — in chat a change asks first, headless without `--auto` it is
  refused, under `--auto` it runs (M848; before it, a subtask ran with every tool
  and asked nothing). Its `agent:` profile's `tools:` fences it, and a refused tool
  names the list. Composes with `model:` / `agent:`. A subtask receives the
  session's answer-language directive (M597) — its answer goes to you, not to a
  parent agent that could translate it.
- `language` — **honored** (M597): the answer language for this command's run,
  replacing config `language` (`"Deutsch"`, `"English"`, any name the model
  knows). Absent means the session language. Its purpose is the
  **English-canonical lessons** option: `language: English` on `learn.md` makes
  the mentor draft in English while the session still answers you in your
  language ([LANGUAGE.md](LANGUAGE.md)).

## Template substitutions

Applied once (no re-scanning of inserted text):

| Token | Expands to |
|-------|-----------|
| `$ARGUMENTS` | the full argument string after the command name |
| `$1`, `$2`, … | positional arguments (whitespace-split); missing => empty |
| `` !`cmd` `` | the combined stdout/stderr of running `cmd` (capped at 16 KB) |
| `@path` | the contents of a file, relative to cwd unless absolute (capped at 32 KB) |

## Examples

`.jichi/commands/review.md`:

```markdown
---
description: Review the current diff
---
Review this diff for bugs and style issues:

!`git diff`
```

Invoke with `/review`. Or with arguments:

`.jichi/commands/explain.md`:

```markdown
---
description: Explain a file
---
Explain what @$1 does, in plain language.
```

`/explain src/main.c` expands to "Explain what <contents of src/main.c> does…".

`/help` lists your custom commands alongside the built-ins.
