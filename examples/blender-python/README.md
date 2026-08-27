# examples/blender-python — the fifth domain-scaffold slice

A ready-to-use **Blender Python (`bpy`) scripting bench** for jichi: the agents,
commands, skills, and conventions that turn an empty `.jichi/` into a guided
workspace for scripting Blender — procedural geometry, custom operators, add-ons.
It is the fifth pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
after `data-analysis`, `game-design`, `project-management`, and `personal-finance`,
built in `examples/` first so the beginner-support contract is exercised before it
becomes a compiled-in `init blender-python` pack.

## The lesson that shapes this pack

**`bpy` runs inside Blender, not with plain `python`.** That single boundary — the
one every beginner trips on (`ModuleNotFoundError: No module named 'bpy'`) — is
named first in the `AGENTS.md`, the `START_HERE`, the `/run` command, and the
reviewer. The pack also pins the **Blender version** everywhere, because the
`bpy` API changes between releases and version-mismatched code is the other great
source of "it doesn't work." This is the proposal's §5 rule — *name the system
boundaries* — applied to its marquee example.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — **what runs where** (inside Blender, three ways), pin your version, data-API-over-ops, mind the context |
| `START_HERE.md` | a numbered first-session walkthrough — run → explore → make → tool → review → package, opening with the plain-`python` trap |
| `agents/blender-scripter.md` | a guide that confirms *where* the code runs and *which* Blender version before any code, and works in small testable steps |
| `agents/bpy-reviewer.md` | a **read-only** reviewer that flags deprecated/version-mismatched `bpy` API, wrong run context, and broken add-on register/unregister — the pack's soul |
| `commands/{run,operator,addon,geometry-script}.md` | the workflow as slash commands (`/run` first — how to actually execute a `bpy` script) |
| `skills/{bpy-basics,mesh-ops,addon-manifest}/SKILL.md` | the mental model, mesh creation/editing, and the add-on manifest + registration |
| `config.example.json` | a template config (a coding chat model; a headless-Blender `testCommand`) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-blender-scripts && cd my-blender-scripts
mkdir -p .jichi
cp -r /path/to/jichi/examples/blender-python/agents   .jichi/agents
cp -r /path/to/jichi/examples/blender-python/commands .jichi/commands
cp -r /path/to/jichi/examples/blender-python/skills   .jichi/skills
cp    /path/to/jichi/examples/blender-python/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and get one tiny script running in
Blender.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. (Blender itself is not needed to validate a *scaffold* — the smoke
driver checks the assets, not a `bpy` run.) The smoke driver
[`tests/smoke/example_blender_python.sh`](../../tests/smoke/example_blender_python.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_blender_python.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real script, it graduates into a
compiled-in `init blender-python` pack + a `3d-artist` setup preset (proposal §7),
alongside the sibling domain slices.
