# examples/research-notes — the eighth domain-scaffold slice

A ready-to-use **research-reading bench** for jichi: the agents, commands, skills,
and conventions that turn an empty `.jichi/` into a guided workspace for reading
research critically and organizing it into a literature review — annotation,
critical summaries, a literature matrix, a synthesis. It is the eighth pilot from
the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
a Group C pack, built in `examples/` first so the beginner-support contract is
exercised before it becomes a compiled-in `init research-notes` pack.

It **pairs** with jichi's source-reading guides
([`docs/reading/ANNAI.md`](../../docs/reading/ANNAI.md),
[`docs/reading/FUKABORI.md`](../../docs/reading/FUKABORI.md)) — those for reading
code/sources, this for reading academic literature — and with the
`academic-writing` bench when your notes become a paper.

## Reads with you, not for you

The reading, understanding, and judgment are yours; jichi makes them sharper and
your notes honest. Its most important rule is an integrity one: **keep the source's
ideas separate from your own in every note** (quote vs paraphrase vs your thought),
with a citation attached — because muddled notes are how honest students
accidentally plagiarize months later. This is baked into the `AGENTS.md`, both
agents, and the `START_HERE`.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — read critically not passively, keep source-ideas separate from your own, organize before you write |
| `START_HERE.md` | a numbered first-session walkthrough — annotate → summarize → matrix → synthesize → review |
| `agents/research-reader.md` | a guide that helps you read critically, note honestly, and organize toward a synthesis |
| `agents/critical-reading-reviewer.md` | a **read-only** reviewer that flags an uncritical summary, notes that blur source and self, a matrix that is just a list, a serial-summary "synthesis" — the pack's soul |
| `commands/{annotate,summarize-paper,lit-matrix,synthesize}.md` | the reading-to-review loop as slash commands |
| `skills/{critical-reading,literature-matrix,synthesis-not-summary}/SKILL.md` | how to read a paper critically, the matrix technique, and synthesis vs serial summary |
| `config.example.json` | a template config (a chat model; an optional embed model for searching your notes) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-lit-review && cd my-lit-review
mkdir -p .jichi
cp -r /path/to/jichi/examples/research-notes/agents   .jichi/agents
cp -r /path/to/jichi/examples/research-notes/commands .jichi/commands
cp -r /path/to/jichi/examples/research-notes/skills   .jichi/skills
cp    /path/to/jichi/examples/research-notes/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and start annotating your first source.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_research_notes.sh`](../../tests/smoke/example_research_notes.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_research_notes.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real set of sources, it graduates
into a compiled-in `init research-notes` pack + a setup preset (proposal §7),
alongside the sibling domain slices.
