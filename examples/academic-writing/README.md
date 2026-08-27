# examples/academic-writing — the seventh domain-scaffold slice

A ready-to-use **academic-writing bench** for jichi: the agents, commands, skills,
and conventions that turn an empty `.jichi/` into a guided workspace for writing
essays, lab reports, and theses — where jichi is your **coach, not your ghost**.
It is the seventh pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
a Group C pack, built in `examples/` first so the beginner-support contract is
exercised before it becomes a compiled-in `init academic-writing` pack.

## This bench coaches; it does not write your paper

The words you submit must be your own. jichi helps you plan an argument,
strengthen the draft *you* wrote, cite honestly, and polish for clarity — it does
**not** write your essay for you to hand in. That is a deliberate line: learning to
write is the point, and submitting text you did not write is academic dishonesty
at most institutions. **You** are responsible for academic integrity — cite your
sources, and check your institution's rules on AI assistance (they vary; some
require disclosure, some restrict it). This is baked into the `AGENTS.md`, both
agents, the `academic-integrity` skill, and the `START_HERE`, not fine print.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — coaches-not-ghost-writes, integrity is your responsibility, thesis-first, cite-as-you-go |
| `START_HERE.md` | a numbered first-session walkthrough — thesis → outline → (you write) → feedback → cite → review → proofread |
| `agents/writing-coach.md` | a coach that helps you plan and strengthen *your* writing and refuses to ghost-write |
| `agents/argument-reviewer.md` | a **read-only** reviewer that flags a weak thesis, unsupported claims, missing citations, structure problems — feedback only, never a rewrite — the pack's soul |
| `commands/{outline,feedback,cite,proofread}.md` | the coach loop as slash commands |
| `skills/{argument-structure,citation-formats,academic-integrity}/SKILL.md` | the shape of an argument, how/why to cite, and what integrity (and honest AI use) actually means |
| `config.example.json` | a template config (a chat model) — secret-free, with the coaches-not-writes note |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-paper && cd my-paper
mkdir -p .jichi
cp -r /path/to/jichi/examples/academic-writing/agents   .jichi/agents
cp -r /path/to/jichi/examples/academic-writing/commands .jichi/commands
cp -r /path/to/jichi/examples/academic-writing/skills   .jichi/skills
cp    /path/to/jichi/examples/academic-writing/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and start with your thesis.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_academic_writing.sh`](../../tests/smoke/example_academic_writing.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_academic_writing.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real paper, it graduates into a
compiled-in `init academic-writing` pack + a `student` setup preset (proposal §7),
alongside the sibling domain slices.
