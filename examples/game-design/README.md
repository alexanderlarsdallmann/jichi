# examples/game-design — the second domain-scaffold slice

A ready-to-use **game-design bench** for jichi: the agents, commands, skills, and
conventions that turn an empty `.jichi/` into a guided workspace for *designing*
a game — the design half, before any code. It is the second pilot from the
domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
built in `examples/` first (after `data-analysis`), so the beginner-support
contract can be exercised before it becomes a compiled-in `init game-design` pack.

**Design, not code.** This bench produces documents — a Game Design Document,
mechanic specs, playtest notes. You build the game later, from these documents;
the `godot` pack is the code half. Designing before building is the whole point,
because a sentence is cheaper to change than a codebase.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — design-before-code, core-fun-first, fight-scope; and up front, that this bench makes *documents*, not a build |
| `START_HERE.md` | a numbered first-session walkthrough — core fun → mechanic → scope → review → playtest |
| `agents/game-designer.md` | a guide that refuses a feature list until the core fun is one clear sentence, and fights scope creep |
| `agents/mechanics-reviewer.md` | a **read-only** reviewer that hunts the beginner-killers: no core fun, scope too big to build, a mechanic with no "why", a balance break visible on paper — the pack's soul |
| `commands/{gdd,mechanic,scope,playtest-notes}.md` | the design loop as slash commands |
| `skills/{game-loop,balancing,mda-framework}/SKILL.md` | how-tos for the core loop, balance-on-paper, and the Mechanics→Dynamics→Aesthetics lens |
| `config.example.json` | a template config (a single chat model; a vision-model note for reference art) — secret-free, adapt to your provider |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-game && cd my-game
mkdir -p .jichi
cp -r /path/to/jichi/examples/game-design/agents   .jichi/agents
cp -r /path/to/jichi/examples/game-design/commands .jichi/commands
cp -r /path/to/jichi/examples/game-design/skills   .jichi/skills
cp    /path/to/jichi/examples/game-design/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and design your first (small) game.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_game_design.sh`](../../tests/smoke/example_game_design.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_game_design.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real design, it graduates into a
compiled-in `init game-design` pack + a `game-designer` setup preset (proposal
§7), alongside the sibling `data-analysis` slice and the remaining Group A/B packs.
