# examples/data-analysis — the first domain-scaffold slice

A ready-to-use **data-analysis bench** for jichi: the agents, commands, skills,
and conventions that turn an empty `.jichi/` into a guided data-analysis
workspace. It is the **pilot** for the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
built in `examples/` first — the same "prove it before promoting it" path the
self-hosting slice took — so the beginner-support contract can be exercised
before it becomes a compiled-in `init data-analysis` pack.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — and, up front, **what runs where** (jichi drives your Python env; your data stays local) |
| `START_HERE.md` | a numbered first-session walkthrough — look → question → clean → analyze → check → report |
| `agents/data-analyst.md` | a guide that asks for your *question* before your chart, and works in small reproducible steps |
| `agents/methods-reviewer.md` | a **read-only** reviewer that hunts the classic method mistakes (a claim bigger than the data, p-hacking, misleading charts, non-reproducibility) — the pack's soul |
| `commands/{explore,clean,analyze,report}.md` | the core loop as slash commands |
| `skills/{tidy-data,honest-charts,reproducible-notebook}/SKILL.md` | progressively-disclosed how-tos for the fiddly parts |
| `config.example.json` | a template config (a chat model; an optional embed model for a data dictionary) — secret-free, adapt to your provider |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-analysis && cd my-analysis
mkdir -p .jichi
cp -r /path/to/jichi/examples/data-analysis/agents   .jichi/agents
cp -r /path/to/jichi/examples/data-analysis/commands .jichi/commands
cp -r /path/to/jichi/examples/data-analysis/skills   .jichi/skills
cp    /path/to/jichi/examples/data-analysis/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and work the loop.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_data_analysis.sh`](../../tests/smoke/example_data_analysis.sh)
runs that check on every build, so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_data_analysis.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real dataset, it graduates into a
compiled-in `init data-analysis` pack + a `data-analyst` setup preset (proposal
§7), with the remaining Group A/B packs following the same path.
