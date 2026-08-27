# examples/krita-python — the sixth domain-scaffold slice

A ready-to-use **Krita Python (PyKrita) scripting bench** for jichi: the agents,
commands, skills, and conventions that turn an empty `.jichi/` into a guided
workspace for scripting Krita — document automation, custom actions, plugins,
batch export. It is the sixth pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
the sibling of `blender-python`, built in `examples/` first so the
beginner-support contract is exercised before it becomes a compiled-in `init
krita-python` pack.

## The two lessons that shape this pack

**PyKrita runs inside Krita, not with plain `python`** (the `krita` module only
exists in Krita's embedded Python — via the Scripter or an installed plugin), and
**a "Docker" is a dockable panel, not a container.** Both — the marquee "name the
system boundary" rule from the proposal (§5) — are named first in the `AGENTS.md`,
the `START_HERE`, the `/run` command, and the reviewer. The pack also pins the
plugin `.desktop`/package layout, the other thing beginners get wrong.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — **what runs where** (inside Krita, two ways), a Docker is a panel, make changes stick, the plugin layout |
| `START_HERE.md` | a numbered first-session walkthrough — run → act → automate → review → package, opening with the two traps |
| `agents/krita-scripter.md` | a guide that confirms *where* the code runs, *which* Krita version, and what a Docker is before any code |
| `agents/pykrita-reviewer.md` | a **read-only** reviewer that flags wrong run context, a broken `.desktop`/plugin layout, and PyKrita API misuse — the pack's soul |
| `commands/{run,action,plugin,batch-export}.md` | the workflow as slash commands (`/run` first) |
| `skills/{krita-python-api,docker-plugin,batch-processing}/SKILL.md` | the mental model, the plugin manifest/Docker layout, and the open→process→export→close batch pattern |
| `config.example.json` | a template config (a coding chat model) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-krita-scripts && cd my-krita-scripts
mkdir -p .jichi
cp -r /path/to/jichi/examples/krita-python/agents   .jichi/agents
cp -r /path/to/jichi/examples/krita-python/commands .jichi/commands
cp -r /path/to/jichi/examples/krita-python/skills   .jichi/skills
cp    /path/to/jichi/examples/krita-python/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and get one tiny script running in
Krita's Scripter.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. (Krita is not needed to validate a *scaffold*.) The smoke driver
[`tests/smoke/example_krita_python.sh`](../../tests/smoke/example_krita_python.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_krita_python.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real script, it graduates into a
compiled-in `init krita-python` pack + a `digital-artist` setup preset
(proposal §7), alongside the sibling domain slices.
