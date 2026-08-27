# examples/project-management — the third domain-scaffold slice

A ready-to-use **solo project-management bench** for jichi: the agents, commands,
skills, and conventions that turn an empty `.jichi/` into a guided workspace for
running a project *by yourself* and actually finishing it — in plain markdown, no
external tools. It is the third pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
after `data-analysis` and `game-design`, built in `examples/` first so the
beginner-support contract is exercised before it becomes a compiled-in `init
project-management` pack.

This bench is the **scaffold form** of the P6/P7 phases (kanban + scheduling) in
the companion process-curriculum proposal
([`docs/proposals/2026-08-process-curriculum.md`](../../docs/proposals/2026-08-process-curriculum.md))
— the *doing* to that proposal's *teaching*.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — charter-before-work, one board, limit WIP, keep it light; and up front, that it is all plain markdown, no external tools |
| `START_HERE.md` | a numbered first-session walkthrough — charter → board → standup → review → retro |
| `agents/project-manager.md` | a guide for running a project *alone*: charter it, track it, finish it — light-touch, finish-before-you-start |
| `agents/scope-reviewer.md` | a **read-only** reviewer that flags what sinks solo projects: no definition of done, scope creep, an overloaded board, a hidden stall — the pack's soul |
| `commands/{charter,board,standup,retro}.md` | the loop as slash commands (a solo standup with yourself; a retro to improve) |
| `skills/{kanban,wip-limits,definition-of-done}/SKILL.md` | how-tos for the plain-markdown board, why limiting WIP is the key habit, and defining a checkable "done" |
| `config.example.json` | a template config (a single chat model) — secret-free, adapt to your provider |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-project && cd my-project
mkdir -p .jichi
cp -r /path/to/jichi/examples/project-management/agents   .jichi/agents
cp -r /path/to/jichi/examples/project-management/commands .jichi/commands
cp -r /path/to/jichi/examples/project-management/skills   .jichi/skills
cp    /path/to/jichi/examples/project-management/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and run your first small project to a
real finish.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_project_management.sh`](../../tests/smoke/example_project_management.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_project_management.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real project, it graduates into a
compiled-in `init project-management` pack + a `project-manager` setup preset
(proposal §7), alongside the sibling `data-analysis` and `game-design` slices.
