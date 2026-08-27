# Output styles (M28)

An **output style** customises *how* the agent responds — its tone, format, and
verbosity — by appending an authoritative instruction block to the system prompt
for the whole session. This is the jichi equivalent of Claude Code's output styles.

It differs from a command's `agent:` profile: an `agent:` profile *replaces* the
whole persona for a single turn, whereas an output style *augments* the persona
and stays active across turns.

## Defining a style

A style is a markdown file (optional YAML frontmatter `description`, then a body)
discovered under, in order:

- `~/.config/jichi/output-styles/<name>.md` (global)
- `<project>/.jichi/output-styles/<name>.md` (project; overrides global)

The file basename is the style name. Example
`.jichi/output-styles/concise.md`:

```markdown
---
description: Terse, high-signal replies
---
Answer in as few words as possible. Lead with the conclusion. Use bullet points,
not prose. Show code, not explanations of code, unless asked.
```

## Selecting the active style

Precedence (highest first):

- `--output-style <name>` on the CLI.
- `"outputStyle": "<name>"` in the config.
- the TUI `/output-style <name>` command (switch live), `/output-style off`
  (clear), or `/output-style` (list).

An unknown name is a graceful no-op (a warning is printed; no style is applied).
The active style's body is injected into the system prompt under a
`# Output style` heading.

## Per-specialist tone: `style:` on an agent or a skill (M302)

An active style governs the **whole session**. Often what you want is narrower:
*this* reviewer is blunt, *this* tutor is patient, *this* proofreader is gentle. So
an agent profile and a skill may name a style in their frontmatter:

```markdown
---
description: reviews a change and says what is wrong
style: blunt
---
You review changes. Find the defect, not the typo.
```

```markdown
---
name: explain-error
description: explain a compiler error to a beginner
style: patient
---
1. read the error
2. name the one thing that is wrong
```

The value is a **style name, not prose**. That is the whole design: it reuses this
document's mechanism instead of adding a second place where personality lives.
"Blunt" is defined once, in one file, and shared by every specialist that wants it —
so improving it improves all of them, and there is no second persona path to drift
from the first.

### Precedence

```
session style  <  a profile's or skill's `style:`  <  a command's `agent:` BODY
```

(Separately, the config's `systemPrompt` key appends a standing instruction to
*every* prompt including subagents' — it is additive rather than part of this
precedence chain. See [`CONFIG_TUTORIAL.md`](CONFIG_TUTORIAL.md).)

A command's `agent:` body stays authoritative because it already *replaces* the
whole persona; a style is an addendum to a persona, not a competitor. When a
subagent has both a profile style and a skill style, **the skill's wins** — the
skill was named in that call, so it is the more specific instruction, whereas the
profile describes the worker in general. (Unlike `tools`, styles are not
intersected: there is no meaningful intersection of two tones, so one has to win.)

### Enforced in a subagent, advisory at top level

This follows exactly the split `tools` / `restrict-tools` already uses, and for a
concrete reason rather than a symmetry:

| Where | What happens |
|---|---|
| `spawn_subagent agent=… ` / `skill=…` | the style is **applied** — it is part of the subagent's system prompt from the start |
| a command's `agent:` frontmatter | **applied** for that turn |
| `load_skill` at top level | **reported**: "Suggested output style for this skill: …" |

`load_skill` returns a *tool result*, and a tool result cannot retroactively change
the system prompt of the turn that called it. Naming the style still helps — the
model can adopt the tone by reading it, which is exactly how the existing tools hint
works — but jichi does not pretend it is enforced.

### A name that does not exist

The tone falls back to the session's, and **`jichi doctor` reports it**:

```
! asset style names no such style
    2 assets name an output style that does not exist, so the tone silently falls
    back to the session's: agent tutor -> patient, skill tidy -> nonexistent …
```

A WARN, not a FAIL: a specialist with the wrong tone is degraded, not broken. The
check ships in the same milestone as the feature on purpose — a declared-but-dead
name is worse than an absent one, because it looks like it is doing something (the
lesson M285 learned from dead tool-fence entries).

## Inspecting

- `jichi output-styles` lists discovered styles, marking the active one
  with `*`.
- `jichi sysmsg` prints the full system prompt, including the active
  style block.

## Implementation

`src/command/jc_output_style.c` / `include/jc_output_style.h` mirror the
flat-markdown loading of custom commands. The parse / find / `set_active` /
render helpers are pure and unit-tested (`tests/test_output_style.c`). The set
lives on `jc_app.output_styles`; `jc_sysmsg_build` injects the active body. The
config key is parsed in `src/config/jc_config.c`; selection + the subcommand live
in `src/main.c`; the TUI command in `src/tui/jc_tui.c`. E2E:
`tests/e2e/output_style.py`.
