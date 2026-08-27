# examples/scheduling — the tenth domain-scaffold slice

A ready-to-use **time-management bench** for jichi: the agents, commands, skills,
and conventions that turn an empty `.jichi/` into a guided workspace for managing
your own time — planning realistic weeks, blocking focused work, tracking
deadlines, and learning how long things actually take. It is the tenth pilot from
the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
built in `examples/` first so the beginner-support contract is exercised before it
becomes a compiled-in `init scheduling` pack.

It **pairs** with the `project-management` bench — that one is about the *work*
(charter, kanban, done); this one is about the *time* (weeks, deadlines, estimates).

## The idea at its heart

**You underestimate how long things take, and the fix is data, not optimism.** The
whole bench is built around estimate-then-measure: guess a task's time, record the
actual, and learn your personal fudge factor so your plans become honest. Two
companions: always leave buffer (a wall-to-wall plan breaks on the first surprise),
and protect real focused-work blocks. These are baked into the `AGENTS.md`, the
`planner` agent, and the `START_HERE`.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — estimate-then-measure, leave buffer, protect focus, plain files no app required |
| `START_HERE.md` | a numbered first-session walkthrough — deadlines → plan → block → review-the-plan → measure |
| `agents/planner.md` | a guide that plans *realistic* weeks, insists on honest estimates and buffer, and matches work to energy |
| `agents/schedule-reviewer.md` | a **read-only** reviewer that flags an overpacked schedule, no buffer, optimistic estimates, deadlines the plan won't hit — the pack's soul |
| `commands/{schedule,timeblock,deadline-check,estimate-review}.md` | the weekly loop as slash commands |
| `skills/{time-blocking,estimation,buffer-planning}/SKILL.md` | protected focus blocks, the planning fallacy and how to beat it, and why/how much slack |
| `config.example.json` | a template config (a single chat model) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-schedule && cd my-schedule
mkdir -p .jichi
cp -r /path/to/jichi/examples/scheduling/agents   .jichi/agents
cp -r /path/to/jichi/examples/scheduling/commands .jichi/commands
cp -r /path/to/jichi/examples/scheduling/skills   .jichi/skills
cp    /path/to/jichi/examples/scheduling/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and plan a week you can actually keep.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_scheduling.sh`](../../tests/smoke/example_scheduling.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_scheduling.sh
```

## Status

**Pilot, in `examples/`.** Once exercised over a few real weeks, it graduates into
a compiled-in `init scheduling` pack + a setup preset (proposal §7), alongside the
sibling domain slices.
