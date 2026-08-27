# examples/web-basics — the twelfth domain-scaffold slice

A ready-to-use **beginner web bench** for jichi: the agents, commands, skills, and
conventions that turn an empty `.jichi/` into a guided workspace for building your
first web page and putting it online — HTML, CSS, responsive, accessible, deployed.
It is the twelfth pilot from the domain-scaffolds proposal
([`docs/proposals/2026-08-domain-scaffolds.md`](../../docs/proposals/2026-08-domain-scaffolds.md)),
a Group C pack and the classic self-learner first project, built in `examples/`
first so the beginner-support contract is exercised before it becomes a compiled-in
`init web-basics` pack.

## The encouraging truth, and the two habits

**You need no build tools** to make a web page — a text editor and a browser. You
write `.html`/`.css` files and open them in the browser; there is nothing to
compile. And from day one, two habits: **semantic HTML** (the right tags for
meaning, not div-soup) and **accessibility** (alt text, ordered headings,
keyboard-usable, contrast). Both are cheap to build in and painful to retrofit — so
this pack makes them the default, with a dedicated `accessibility-reviewer`.

## What's here

| File | What it is |
|---|---|
| `AGENTS.md` | domain conventions jichi loads automatically — no build tools, HTML→CSS→JS order, semantic HTML + accessibility as habits, works on a phone, relative paths |
| `START_HERE.md` | a numbered first-session walkthrough — page → style → responsive → accessible → live |
| `agents/web-guide.md` | a warm guide: content-first, semantic markup, small steps viewed in the browser, no toolchain |
| `agents/accessibility-reviewer.md` | a **read-only** reviewer that flags missing `alt`, div-soup, broken headings, low contrast, keyboard traps — the pack's soul (a11y as an early habit) |
| `commands/{page,style,responsive,deploy-static}.md` | the build-and-publish loop as slash commands |
| `skills/{semantic-html,css-layout,responsive-design}/SKILL.md` | tags-for-meaning, flexbox/grid layout, and making it work on any screen |
| `config.example.json` | a template config (a coding chat model; no build step) — secret-free |

## Use it

Copy the assets into a workspace's `.jichi/`, drop `AGENTS.md` at the root, and
open jichi there:

```sh
mkdir my-site && cd my-site
mkdir -p .jichi
cp -r /path/to/jichi/examples/web-basics/agents   .jichi/agents
cp -r /path/to/jichi/examples/web-basics/commands .jichi/commands
cp -r /path/to/jichi/examples/web-basics/skills   .jichi/skills
cp    /path/to/jichi/examples/web-basics/AGENTS.md .
# then set up a model (see config.example.json) and follow START_HERE.md
```

Then read [`START_HERE.md`](START_HERE.md) and build your first page — you only need
a browser to see it.

## How it's verified green

The assets are validated the same way jichi validates any `.jichi/` project: the
frontmatter parser (`jc_assetval`, the engine behind `jichi doctor`) checks every
agent/command/skill has a valid `description` and no unknown keys, and the config
is valid JSON. The smoke driver
[`tests/smoke/example_web_basics.sh`](../../tests/smoke/example_web_basics.sh)
runs that check on every build — including a teeth check that a *broken* asset is
rejected — so this slice cannot rot. Run it directly:

```sh
sh tests/smoke/example_web_basics.sh
```

## Status

**Pilot, in `examples/`.** Once exercised on a real page, it graduates into a
compiled-in `init web-basics` pack + a setup preset (proposal §7), alongside the
sibling domain slices — **completing the proposal's pack roster.**
