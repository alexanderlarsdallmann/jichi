# examples/game-dev — the ninth domain-scaffold slice

A ready-to-use **game-development bench** for jichi: the agents, commands, skills,
and conventions that turn an empty `.jichi/` into a guided workspace for *building*
a game — the code half. It is the ninth pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
built in `examples/` first so the beginner-support contract is exercised before it
becomes a compiled-in `init game-dev` pack.

It **pairs** with the `game-design` bench (where you designed the fun) and with
jichi's compiled-in **`godot`** pack (the Godot-specific companion). This bench is
**engine-agnostic** — the method holds for Godot/GDScript (the default), LÖVE
(Lua), or Pygame (Python).

## The craft it teaches

Not fancy algorithms — the discipline that keeps a game *buildable*: keep it
runnable at all times, add small playable slices, separate game logic from
rendering so it can be unit-tested, keep the per-frame loop lean, handle input as
named actions, compose behaviours instead of a collapsing inheritance tree, and
reproduce a bug before fixing it. These are the habits that let a game grow without
falling over.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — what runs where (inside an engine), keep-it-runnable, small slices, logic-vs-rendering, the frame budget |
| `START_HERE.md` | a numbered first-session walkthrough — build → feature → bug → review → release |
| `agents/gameplay-programmer.md` | a guide that builds in small playable slices, keeps the game runnable, and separates testable logic from the engine |
| `agents/code-reviewer.md` | a **read-only** reviewer that flags per-frame performance traps, logic tangled with rendering, untestable gameplay, frame-rate dependence, magic numbers — the pack's soul |
| `commands/{feature,bug,build,release-notes}.md` | the build loop as slash commands |
| `skills/{entity-component,game-testing,input-handling}/SKILL.md` | composition over inheritance, what you can test (logic) vs playtest (feel), and clean input as named actions |
| `config.example.json` | a template config (a coding chat model; per-engine `testCommand` notes) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-game && cd my-game
mkdir -p .jichi
cp -r /path/to/jichi/examples/game-dev/agents   .jichi/agents
cp -r /path/to/jichi/examples/game-dev/commands .jichi/commands
cp -r /path/to/jichi/examples/game-dev/skills   .jichi/skills
cp    /path/to/jichi/examples/game-dev/AGENTS.md .
# then set up a model + your engine (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and get a game running.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. (A game engine is not needed to validate a *scaffold*.) The smoke
driver
[`tests/smoke/example_game_dev.sh`](../../tests/smoke/example_game_dev.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_game_dev.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real game, it graduates into a
compiled-in `init game-dev` pack + a `game-developer` setup preset (proposal §7),
alongside the sibling domain slices.
