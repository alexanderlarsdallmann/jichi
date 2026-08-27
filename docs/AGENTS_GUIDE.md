# Writing AGENTS.md — teaching jichi your project

`AGENTS.md` is the **primary channel for teaching the in-loop model how your
project works**. jichi prepends it to the system prompt on every turn, so the
agent starts each turn already knowing your build command, conventions, and
gotchas instead of rediscovering them (or guessing wrong) each time.

This guide is about *what to write and why*. For the loading mechanics
(discovery order, precedence, the config `instructions` key) see
[`RULES.md`](RULES.md); this document assumes those and focuses on writing an
effective file.

> **Two different "contracts."** `AGENTS.md` is the **model-facing** project
> knowledge — prose the LLM reads. It is not the **machine-facing** interface
> contract an external tool needs to *drive* jichi (flags, the jsonl event schema,
> the daemon protocol, exit codes) — that lives in `jichi describe
> --output json`. Keep the two separate: don't document CLI flags in AGENTS.md,
> and don't put project prose in `describe`.

## Why it matters

An agent working in your repo has to know things that aren't derivable from a
quick look at the code: which of three build scripts is canonical, that
`make test` must pass before a change is "done", that a subsystem is
C89-only, that a directory is generated and must not be hand-edited. Without
this, the model wastes turns probing — and, as we found repeatedly while
hardening jichi, an agent that can't discover a convention will confidently do
the wrong thing. `AGENTS.md` front-loads that knowledge so it's present on
turn one.

## What to put in it

Aim for **short, concrete, and durable**. Good sections:

- **Build & test** — the *exact* commands. Set `testCommand` in your config
  too, so the `run_tests` tool and the `--verify` gate use the same one.
  ```markdown
  ## Build & test
  - Build: `make`
  - Test:  `make test`   (must pass with zero failures before a change is done)
  - Lint:  `make lint`
  ```
- **Conventions the code follows but doesn't state** — language standard,
  indentation, error-handling style, naming, "prefer the existing `jc_*`
  helpers over new ones." These are the rules a reviewer would give a new hire.
- **Architecture pointers** — one or two lines on where things live, so the
  agent reads the right file first. ("Request flow is in `src/chat/jc_agent.c`;
  providers are behind a vtable in `src/provider/`.")
- **Do / don't** — the sharp edges. "Don't reformat unrelated code." "Never
  edit `generated/`." "This is Linux-only; don't add Windows paths."
- **Gotchas** — the non-obvious traps ("the shadow-repo snapshot lives outside
  the workspace; don't put logs inside it or a rollback eats them").

## What to leave out

- **Secrets.** Never put API keys or tokens here — it goes into the prompt
  verbatim. Use `apiKeyEnv` in config.
- **Volume.** The file is *fitted* to the context budget: compaction never
  trims the system prompt, so on a small-context model jichi caps the instruction
  block (M73, ~45% of the effective budget) and truncates with a marker. A
  3,000-line manifesto mostly gets dropped — and crowds out room for the actual
  work. Keep it to what changes the agent's behavior. The combined instruction
  block is also hard-capped (32 KB) with a truncation marker.
- **Things the repo already states.** Don't restate the code's structure the
  agent can read; state what it *can't* infer (intent, canonical-ness, taboos).
- **Machine/driver details.** Flags, output formats, protocols → `describe`.

## How it composes with the other channels

`AGENTS.md` is one of several ways jichi carries knowledge into a turn. Use the
right one:

| Channel | File(s) | Who writes it | Best for |
|---|---|---|---|
| **Rules** | `AGENTS.md` / `CLAUDE.md` | you | always-on project conventions (this guide) |
| **Memory** | `.jichi/memory.md` | the agent (`remember` tool) + you | durable facts learned mid-work; see [`MEMORY.md`](MEMORY.md) |
| **Glossary** | `.jichi/glossary.md` | you | domain-term definitions; see [`GLOSSARY.md`](GLOSSARY.md) |
| **Skills** | `.jichi/skills/<name>/SKILL.md` | you | on-demand, progressively-disclosed procedures; see [`SKILLS.md`](SKILLS.md) |
| **Commands** | `.jichi/commands/*.md` | you | reusable `/slash` prompts (may pin an agent/model/subtask) |
| **Agents** | `.jichi/agents/*.md` | you | named subagent personas for `spawn_subagent` (may pin a model tier + a tool fence; see [`CONFIG_TUTORIAL.md` §8](CONFIG_TUTORIAL.md#8-specialist-agents-on-specific-models)) |
| **Output styles** | `.jichi/output-styles/*.md` | you | session-wide tone/format augmentation |

They stack in the system prompt in that rough order (rules → repo map → memory
→ glossary → skills catalog → output style). A rule of thumb:

- Applies to **every** turn, unconditionally → **AGENTS.md**.
- A **fact learned while working** ("the build needs `pkg-config` for curl") →
  **memory** (`remember`), so it survives and can be reviewed/corrected.
- A **term** the agent keeps misreading → **glossary**.
- A **multi-step procedure** only sometimes relevant → a **skill** (loaded on
  demand, so it doesn't sit in every prompt).

## Placement & scoping

- **Repo root** `AGENTS.md` — the usual home; loaded from the git root down.
- **Subdirectory** `AGENTS.md` — rules that apply only under that dir (loaded
  root-first, so deeper files refine shallower ones).
- **Global** `~/.config/jichi/AGENTS.md` — your personal cross-project
  preferences (loaded first).
- **`CLAUDE.md`** works as a fallback filename per directory, for
  Claude Code parity.

## A good starting point

```markdown
# Project conventions

## Build & test
- Build: `make`
- Test:  `make test`  (must be green before a change is done)

## Conventions
- C89 / ANSI C; `-Wall -Wextra` clean; declarations at block top; no `//`
  comments (see CONTRIBUTING.md).
- Prefer the existing `src/util` helpers over new ones.

## Architecture
- CLI entry: `src/main.c`; core logic under `src/`. Keep `main.c` thin.

## Do / don't
- Read a file before editing it; keep diffs focused on the task.
- Don't touch `third_party/` (vendored dependencies).
- Don't add new dependencies without asking.
```

(This example uses C — jichi's own language. The same shape works for any language:
`zig build` / `zig build test`, `guile`/`raco test`, `mix test`, etc. Rust remains a
first-class **port target** and ships a `rust-cli` scaffold pack; it's simply not the
language the docs teach with.)

`jichi init <pack>` (or `setup`) scaffolds a starter `AGENTS.md` for you;
edit it down to your project's specifics. See [`SCAFFOLDING.md`](SCAFFOLDING.md).

## Verifying it loaded

- `jichi sysmsg` prints the assembled system prompt — confirm your rules
  are in it and not truncated.
- `jichi context` shows the context-budget breakdown, including the
  instruction-file size, so you can tell if AGENTS.md is being capped.
- `jichi doctor` warns when instruction files are large for the model's
  context.
