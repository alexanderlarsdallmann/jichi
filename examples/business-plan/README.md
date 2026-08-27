# examples/business-plan — the eleventh domain-scaffold slice

A ready-to-use **lean business-planning bench** for jichi: the agents, commands,
skills, and conventions that turn an empty `.jichi/` into a guided workspace for
pressure-testing a small venture idea — a one-page model and cheap experiments,
not a 40-page plan. It is the eleventh pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
built in `examples/` first so the beginner-support contract is exercised before it
becomes a compiled-in `init business-plan` pack.

## Educational, not business/financial/legal advice

This bench **teaches a planning method** (lean canvas, unit economics, validation)
and helps you think clearly about your idea. jichi is a *learning tool*, not a
financial advisor, accountant, or lawyer; it does not tell you to start the
business, promise it will work, or make tax, legal, registration, or investment
decisions. For those, consult a qualified professional. Your plans stay in local
files you own — a local model keeps a confidential idea on your machine. This is
baked into the `AGENTS.md`, both agents, all three skills, and the `START_HERE`,
not fine print.

## The idea at its heart

**A business idea is a stack of assumptions; the job is to test the riskiest ones
cheaply, before building.** Test your assumptions cheaply, don't write a long
confident plan full of untested guesses. Produce a lean canvas, do the unit
economics early, and design the cheapest experiment that could prove you wrong.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — educational-not-advice, one-page-not-forty, name-and-rank assumptions, unit economics early, validate cheaply |
| `START_HERE.md` | a numbered first-session walkthrough — canvas → customer → pricing → challenge → cheap test |
| `agents/business-planner.md` | a lean guide that surfaces assumptions and pushes toward the cheapest test — encouraging but never a cheerleader |
| `agents/assumptions-reviewer.md` | a **read-only** reviewer that hunts unstated/untested assumptions, guesses stated as facts, broken unit economics, vanity metrics — the pack's soul |
| `commands/{lean-canvas,market,pricing,milestones}.md` | the loop as slash commands |
| `skills/{lean-canvas,unit-economics,validation}/SKILL.md` | the one-page model, does-one-sale-make-money, and testing assumptions cheaply |
| `config.example.json` | a template config recommending a local model for a confidential idea — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-venture && cd my-venture
mkdir -p .jichi
cp -r /path/to/jichi/examples/business-plan/agents   .jichi/agents
cp -r /path/to/jichi/examples/business-plan/commands .jichi/commands
cp -r /path/to/jichi/examples/business-plan/skills   .jichi/skills
cp    /path/to/jichi/examples/business-plan/AGENTS.md .
# then set up a (preferably local) model, and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and put your idea on one page.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_business_plan.sh`](../../tests/smoke/example_business_plan.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_business_plan.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real idea, it graduates into a
compiled-in `init business-plan` pack + an `entrepreneur` setup preset
(proposal §7), alongside the sibling domain slices.
