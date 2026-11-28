# 自治（じち） jichi — just code

A complete AI coding agent in **C, conforming to the C89 (ANSI C / C90)
standard**, for Linux/POSIX: chat with a model over an OpenAI-compatible or the
Anthropic API, an agentic tool-use loop, retrieval, snapshots and undo, a bounded
autonomy envelope, MCP/LSP/ACP integration, an interactive terminal UI and a
scriptable headless mode. It is three small programs that link libcurl and the C
library and nothing else: **`jichi`**, the agent; **`jichi-convert`**, which turns
a Continue, opencode or Claude Code configuration into a jichi one; and
**`jichi-harness`**, which drives long unattended runs of jichi or of another agent
(Linux with systemd only, for now).

## Start here

Five doors. Take the one that matches why you opened this file — none of them
needs the rest of this page first.

| You want to… | Go to | First command |
|---|---|---|
| **Run it** — build from source, then a guided setup and a first session | [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) → [`docs/TUTORIAL_BEGINNER.md`](docs/TUTORIAL_BEGINNER.md) | `make && ./jichi setup && ./jichi doctor` |
| **Find the right page** — one routing table, no guessing | [`docs/README.md`](docs/README.md), the documentation map | — |
| **Know what it believes** — why C89, why craftsmanship, what this owes to others, and **how jichi approaches building software** | [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md) → [`docs/APPROACH.md`](docs/APPROACH.md) | — |
| **Learn the craft** — a self-paced course with graded assignments, or read jichi's own source with a guide | [`docs/CURRICULUM.md`](docs/CURRICULUM.md) · [`docs/reading/ANNAI.md`](docs/reading/ANNAI.md) | `./jichi setup --preset learner` |
| **Run it for a group** — a shared machine, or JupyterHub for a class: what was measured, what a notebook costs, and the one configuration that loses work | [`docs/JUPYTERHUB.md`](docs/JUPYTERHUB.md) | `./jichi doctor` |

**Three things worth knowing before you invest an hour:**

- **You need a model.** A server on your own machine (LM Studio, llama.cpp,
  Ollama; [`docs/LOCAL_MODELS.md`](docs/LOCAL_MODELS.md)) costs nothing; a hosted
  one needs an API key ([`docs/MODELS.md`](docs/MODELS.md)). `jichi setup` asks, and
  `jichi doctor --live` confirms that the model really calls tools.
- **Platforms.** jichi is developed on x86-64 Linux and tested by running its own gate
  on each platform, not by assuming. **Verified** — compiled and gate-run, the numbers
  kept per row — on x86-64; on 64- and 32-bit ARM and on RISC-V, on real boards
  (Raspberry Pi, Arduino UNO Q, VisionFive 2, Banana Pi BPI-F3); on 32-bit x86 among the
  14 architectures of an emulated sweep; and on Linux and WSL2. **Partly verified**,
  each with its gap named: FreeBSD, NetBSD and OpenBSD (verified until their latest
  runs failed some tests), macOS (Apple Silicon), illumos, GNU/Hurd, Haiku, Guix System,
  old Linux userlands, Windows through Cygwin and MSYS2, and a 256 MB RISC-V board.
  In all, **20 rows Verified**, **12 Partly verified** and **2 Never compiled**.
  **Never compiled: FreeMiNT and FreeDOS**, which are cross-built only.
  A row counts as *driven* only when a model was really called there;
  [`docs/PLATFORMS.md`](docs/PLATFORMS.md) says which rows are, and what was measured,
  when.
- **Licence.** **Apache-2.0**, decided 2026-08-27: the `LICENSE` file at the root is
  the verbatim text, every C source and header carries the SPDX header, and the copyright is held by
  Justus-Liebig-Universität Gießen with Alexander-Lars Dallmann as the author (§ 69b
  UrhG separates the two — see [License](#license) and
  [`docs/LICENSING.md`](docs/LICENSING.md)).

What state the project is in, and what changed when: [Status](#status) ·
[`CHANGELOG.md`](CHANGELOG.md) (per version) · [`docs/ROADMAP.md`](docs/ROADMAP.md)
(per milestone, with the reasoning). The next sections say why jichi exists and
where it stands; from [Building](#building) on, the page is reference — read it when
you need it, not in order. [`docs/VOCABULARY.md`](docs/VOCABULARY.md) defines the
words this project leans on.

## Why this exists, and who it is for

**jichi is built for self-learners who want to learn software development.**
That is not a use case discovered afterwards; it is the principle the project
was steered by, and it shows up in what got built. There is a graded curriculum,
where every grader has been shown to reject an unfinished answer as well as
accept a finished one. There are reading guides that walk jichi's own source.
There is a course that teaches you to download a language's official
documentation and work through it with the agent beside you. `doctor` answers
*"you are here, and this is what you can do here"* on a machine that is not set
up yet, instead of refusing to start.

Where the program has to choose between being clever and being explicable, it is
meant to choose explicable — and where it has failed at that, the failure is
written down rather than tidied away.

### The experiment, in the author's words

> I started jichi on my own, out of curiosity and out of necessity.
>
> I needed to learn AI-supported software development — not read about it, but
> do it long enough to have opinions I could defend. I wanted to understand what
> AI coding agents actually are, how they behave over months rather than demos,
> and what they do to a software project: to a new one, and to one that already
> exists and has to keep working.
>
> I had been using the Continue dev CLI and opencode for a while on a complex
> Python project, and that taught me to *use* agentic tools. Building one was a
> different question, and I chose it out of curiosity and out of the necessity
> of walking on as a developer in a landscape of changing systems and tools.
>
> **jichi was written with an AI coding agent — Claude Code, and later jichi
> itself.** That is not incidental to what it is. The program and the method
> were learned at the same time, and the documentation records both.
>
> I am interested in systems programming languages and functional programming
> languages, in user interface design, and in the engineering side of software:
> design, re-use, and refactoring — from C89 towards modern C, or C++, or Zig,
> and how those compare with a rewrite in something else entirely.
>
> jichi is the result of that experiment. It is a real program that does a real
> job, and it is also the notebook I kept while learning.
>
> My background is not the usual one. I trained as a teacher — art education
> and English language teaching — and passed the First State Examination
> (1. Staatsexamen) for Haupt- und Realschulen in Hesse; then picked up music
> education, musicology and philosophy. I did not pass the Second State
> Examination, which is the one that makes you a teacher. So I trained instead
> as a software developer, through a German apprenticeship. I walked that path,
> failed, stumbled, and got up again.
>
> I took Peter Norvig's [*Teach Yourself Programming in Ten
> Years*](https://norvig.com/21-days.html) seriously. I started in earnest in
> 2014, began the apprenticeship in 2016, finished it in 2018, and have worked
> as a software developer since.
>
> Teaching and building are not separate skills here. A program you cannot
> explain to a learner is one you have not finished understanding yourself. And
> I am still learning.
>
> I had begun learning Japanese, and visited Kobe twice in the year before this
> work started. That is where the name comes from, and it is why the
> documentation was translated into other languages. May it help learners
> elsewhere.
>
> jichi is what it is: honest, and frail, and with strengths of its own. I would
> rather you saw all three than only the third.
>
> So, to all self-learners: I would like jichi to be free, as you are free to
> learn. Walk on, step by step.
>
> — Alexander-Lars Dallmann

**What follows from this**, and it is visible throughout the documentation: the
analyses, the plans, the dialogues and the anecdotes ship in full, including
every mis-diagnosis and dead end. A project that only publishes its successes
teaches nobody how the work is actually done.

## The name

**jichi** is Japanese. Written **自治（じち）** it means *autonomy* / *self-government* —
自 (self) + 治 (govern) — an everyday word, as in 自治体（じちたい） (a municipality). That is
the sense meant here: this is an agent you hand a bounded goal to, with a token
and wall-clock budget, an edit-scope fence and a verification gate, and it governs
itself inside those limits. A rarer, literary homophone **自知（じち）** reads as
*self-knowledge*, which suits a tool that keeps its own run journals and mines
them to correct itself.

The tagline **"just code"** is the working title it grew out of, and it is why the
internal symbol prefix is `jc_`: the prefix now does double duty — **j**i**c**hi
and **j**ust **c**ode.

It was built at the **Justus-Liebig-Universität Gießen**, which is where the
original working name `jlu_continue` came from — the university's initials plus
the project it reimplements. Both halves were retired for the public release: this
is a from-scratch reimplementation rather than a fork of Continue, and it is meant
for anyone who needs AI-assisted coding, on a constrained machine or a powerful
one, not only for one university. Nothing of the university is baked into jichi: it reads whatever environment
variable your config's `apiKeyEnv` names and talks to whatever models your server
advertises; `JLU_API_KEY` and the `jlu/…` model ids are only the conventions of the
gateway the maintainer develops against. The `jlu_continue` → `jichi` rename is long
done; [docs/MIGRATION.md](docs/MIGRATION.md) keeps the old steps for the record.

## Status

**Origins, honestly.** jichi began (June 2026) as a from-scratch C89
reimplementation of the open-source
**[Continue CLI](https://github.com/continuedev/continue)**
([continue.dev](https://continue.dev)). Continue's feature set, interaction
model, and configuration format served as the *specification* — the map of
what a coding agent should do — while the implementation shares **no code**
with it: Continue is a ~39k-line TypeScript/React/Node application, and this
tree is C89 throughout. Upstream has since completed its road — the
repository was archived read-only after a final **2.0.0** release, under
**Continue's own Apache-2.0 licence** (status verified 2026-07-28; jichi's licence
is a separate matter — Apache-2.0 as well, as it happens; see [License](#license)) — and
jichi long ago grew past the
reimplementation framing (the autonomy envelope, the curriculum, the
observability stack and much else have no upstream counterpart). The debt is
real and acknowledged with thanks, here and in [`CREDITS.md`](CREDITS.md) and the
`NOTICE`; `jichi-convert` keeps a working migration path for Continue (and
opencode, and Claude Code) configurations —
[Migrating from Continue](#migrating-from-continue) below.

Built incrementally in milestones — **1223 of them** so far — each with its design
and its failures recorded. **The documentation ships in full, on purpose** — the
analyses, plans, dialogues and anecdotes, including every recorded failure,
mis-diagnosis and dead end: a project that publishes only its successes teaches
nothing about how software is actually built, so the honest record is part of the
product (decided 2026-07-28;
[`docs/plans/2026-08-public-snapshot.md`](docs/plans/2026-08-public-snapshot.md) §3).
The full record is [`docs/ROADMAP.md`](docs/ROADMAP.md) (the reasoning) and
[`CHANGELOG.md`](CHANGELOG.md) (what changed, per version); the milestone bands and
the highlights up to M296 are in
[`docs/MILESTONE_HIGHLIGHTS.md`](docs/MILESTONE_HIGHLIGHTS.md).

Every translation unit compiles under strict C89 with zero warnings. `make test`
runs the unit suite (**over 38,000 checks** — a growing figure, so stated as a
bound); `make smoke` adds a python-free tier that makes `make check-target` a full
gate on any POSIX box; and `make ci` is the whole gate: gcc, clang and C++ builds
and tests, AddressSanitizer + UndefinedBehaviorSanitizer, a leak check, Valgrind,
the curl-free link, the release and 32-bit builds, the fallbacks, the fault tier,
the smoke and e2e tiers and a mutant sweep. Green end to end at **M1212** on the
host gate (2026-11-23, on exactly a0a8b4ae): every stage of `make ci` OK, and the
python-free smoke tier at **578 drivers / 3,785 checks**.

## Building

New to building from source? [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md)
walks you from an empty terminal to a working build;
[`docs/INSTALL.md`](docs/INSTALL.md) has every requirement, minimum and
recommended. Small machines: [`docs/LOW_MEMORY.md`](docs/LOW_MEMORY.md); over SSH,
headless and on embedded devices: [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md).

```sh
make                  # build all three programs: jichi, jichi-convert and jichi-harness
make jichi            # just the agent
make jichi-convert    # just the config converter
make jichi-harness    # just the harness
make test             # build and run the unit suite
make smoke            # the POSIX-sh smoke tier -- validates a build, needs no python3
make check-target     # on-target validation: test + smoke (old or small systems)
make ci               # the full gate (every stage under Status above)
make info             # show the detected toolchain features
make clean
```

jichi compiles under **four toolchains**: gcc and clang (strict
`-std=c89 -pedantic`), **g++** (as C++17; `make CC=g++`, see
[`docs/CPP_BUILD.md`](docs/CPP_BUILD.md)) — the three that `make ci` gates — and
**zig cc** (`make CC="zig cc"`, including a fully static musl build and ARM
cross-builds, one command each; see [`docs/ZIG_BUILD.md`](docs/ZIG_BUILD.md)).
First-party code compiles under `-std=c89 -pedantic -Wall -Wextra` with zero
warnings and no exemptions, the JSON implementation (`src/json/cJSON.c`, ours
rather than vendored) included.

### Installing

```sh
make && sudo make install          # /usr/local by default
make install PREFIX=~/.local       # or a user prefix (no sudo)
make uninstall                     # with the same PREFIX (and sudo) you installed with
```

This installs the three programs and `jichi-nano` to `$PREFIX/bin`, the man page
(`man jichi`), bash and zsh completions, the Emacs and Vim plugins, and six pages
for learners under `$PREFIX/share/doc/jichi`. `DESTDIR` is honoured for staged
builds. A binary built from a tree with uncommitted changes is refused unless you
pass `ALLOW_DIRTY=1`.

### Dependencies

- **libcurl** (7.19.4 or newer) for HTTPS, TLS and streaming. Without it jichi still
  builds, tests and runs, but cannot call a model. On Debian/Ubuntu:
  `sudo apt-get install libcurl4-openssl-dev`.
- **GNU make** and any C89 compiler; **git** is recommended (snapshots, undo and the
  git tools use it).
- **No JSON library and no other third-party source.** `src/json/cJSON.{c,h}` is
  original code implementing the cJSON *API*, with a few extensions of its own. Upstream
  [cJSON](https://github.com/DaveGamble/cJSON) is no longer a drop-in replacement: three
  source files use those extensions, and some behaviour differs on purpose
  ([`docs/reading/ireko-01-everything-passes-through-here.md`](docs/reading/ireko-01-everything-passes-through-here.md),
  "A promise nobody checks").

## Usage

```sh
# Set up a project: a role preset, its assets, a config, a first check. New here? Start with this.
jichi setup
# Without a terminal, name the provider, the model and the server (docs/SETUP_WIZARD.md):
jichi setup --provider openai --model <the id your server lists> --api-base http://localhost:1234/v1

# Check the config, the keys and the servers; --live makes one real request with a tool
jichi doctor --live

# The interactive terminal UI
jichi

# Headless: one prompt, the reply on stdout. Close stdin, or it is read as context.
jichi -p "summarize src/main.c" < /dev/null

# Plan first and change nothing; or run approved tools without asking, bounded by a check
jichi --plan
jichi --auto -p "fix the failing test" --verify "make test" < /dev/null
```

In the TUI, a tool that changes something asks first:
`Allow? [y]es  [n]o  [a]lways  [e]dit  [v]iew` — `e` lets you edit the tool's
arguments before you approve. The keys, slash commands and typing ahead while the
agent works: [`docs/TUI_RENDER.md`](docs/TUI_RENDER.md),
[`docs/TYPE_AHEAD.md`](docs/TYPE_AHEAD.md), [`docs/AUTOCOMPLETE.md`](docs/AUTOCOMPLETE.md).
Headless use, JSON output and the exit codes: [`docs/SCRIPTING.md`](docs/SCRIPTING.md).

## Conversations & resume

Every conversation is saved (the full history — messages, tool calls, and their
results — plus the mode and workspace) under `~/.jichi.d/sessions/<id>.json`
after each turn, so you can pick one up later. This is **separate from
checkpoints**: checkpoints (`/undo`, `undo`) snapshot the *workspace files* the
agent changed; sessions store the *chat*.

```sh
jichi ls               # this project's chats, newest first
jichi ls --all         # every project
jichi -c               # continue the most recent chat here
jichi --session 3f9a   # resume a specific chat (id or unambiguous prefix)
```

`ls` shows a short id, when it was last used, message count, workspace, and
title. Resume restores the saved mode when it is the mode you started in or a
stricter one -- a saved mode never widens a run, so to resume in auto mode,
start with `--auto`. The agent's tools act on your current directory. In the TUI: `/sessions` lists, `/resume <id>` switches conversations,
and `/title <text>` names the current one. `--no-session` disables saving.

## Migrating from Continue

Coming from Continue, opencode or Claude Code? `jichi setup --import <config>`
converts a Continue or opencode config into a jichi config and its `.jichi/` tree,
and never overwrites a file without `--force`. `jichi-convert` does the same from the
command line and also reads a Claude Code config tree: it prints to stdout unless you
pass `-o <file>` or `--assets <dir>`, and `--dry-run` shows what it would write. Both
are described in [`docs/CONVERT.md`](docs/CONVERT.md).

## Configuration

Configuration is JSON, comments allowed (JSONC). jichi reads **one** file when you
name it — `--config <path>` or `$JC_CONFIG` — and otherwise **merges** two: your
global `~/.jichi`, then the project's `local/config.json` (or, without it,
`.jichi/config.json`), the project's keys over the global ones. A project config can
arrive with a repository you just cloned, so its powerful keys — the models and
where their requests go, tools that run commands, routing — wait until you run
**`jichi trust`** in that directory (`jichi untrust` takes it back); until then only
its safe settings apply, and it can narrow the mode and the permissions but not
loosen them. `jichi config path` names the files in effect and `jichi config show`
summarises the result. Step by step:
[`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md).

The shortest config that works costs nothing — a model server on your own machine
(LM Studio, `llama.cpp`, Ollama):

```json
{
  "models": [
    { "name": "local", "provider": "openai",
      "model": "<the id your server lists>",
      "apiBase": "http://localhost:1234/v1" }
  ]
}
```

No key line: a server on your own machine usually wants none. **`provider` is the
wire dialect, not a company** — `"openai"` means any OpenAI-compatible server,
`"anthropic"` the Anthropic Messages API — and jichi guesses neither the dialect nor
a key. A hosted endpoint takes its key from the environment variable `apiKeyEnv`
names, and should declare what it costs (`inputCostPer1M`, `outputCostPer1M`) so
`/cost` and any budget mean something. Several models, model roles, several servers
with fallback, and the health check: [`docs/MODELS.md`](docs/MODELS.md),
[`docs/DOCTOR.md`](docs/DOCTOR.md), and the ready-made `examples/config.*.json`.

## Modes & permissions

The agent runs in one of three **modes**:

- **chat** (default) — read-only tools run freely; a tool that changes something
  asks first.
- **plan** — the agent investigates and proposes a plan, and changes nothing in
  your files; its one write is the plan itself, `.jichi/PLAN.md`. Review it, then
  switch modes to execute.
- **auto** — approved tools run without asking.

Start in a mode with `--plan` / `--auto` or `"mode"` in the config; switch in the
TUI with `/mode chat|plan|auto`, `/plan` (`/plan off`) or `/auto`. `--readonly`
removes the tools that change things, in any mode. Independently of mode, a
top-level `permissions` block fences individual tools by their registered name (a
built-in's short name or an MCP `server__tool` name):

```json
"permissions": { "allow": ["edit_file"], "deny": ["run_terminal_command"] }
```

Each list is tool names or `"*"`/`true` for all; `deny` wins and **hides** the tool
from the model entirely. In headless mode (`-p`), a tool that would otherwise prompt
is refused (re-run with `--auto`). The full model and its truth table:
[`docs/AGENT_MODES.md`](docs/AGENT_MODES.md); what a run may do to your machine, and
who decides: [`docs/TOOL_DECISIONS.md`](docs/TOOL_DECISIONS.md).

## What jichi can do

One line each; the page behind each link is the reference. `jichi describe` lists
the tools a model is offered, and [`docs/README.md`](docs/README.md) maps every page.

- **Edit files** — exact and fuzzy edits, `apply_patch`, and a diff preview before
  you approve: [`docs/EDITING.md`](docs/EDITING.md).
- **Your own tools** — a command declared in the config becomes a tool:
  [`docs/USER_TOOLS.md`](docs/USER_TOOLS.md).
- **`@`-references** — pull a file, a folder's map, the current diagnostics, your
  docs and more into a prompt: [`docs/REFERENCES.md`](docs/REFERENCES.md).
- **Git** — read-only and mutating git tools, and an optional self-review of the
  agent's own diff: [`docs/GIT.md`](docs/GIT.md).
- **A repository map** — the files and their top-level symbols, for orientation:
  [`docs/REPOMAP.md`](docs/REPOMAP.md).
- **Tests** — run the suite and read its results as structure, not text:
  [`docs/TESTING.md`](docs/TESTING.md).
- **Project rules, commands, skills and memory** — `AGENTS.md`, your own slash
  commands, skills, and notes that persist between sessions:
  [`docs/RULES.md`](docs/RULES.md), [`docs/AGENTS_GUIDE.md`](docs/AGENTS_GUIDE.md),
  [`docs/COMMANDS.md`](docs/COMMANDS.md), [`docs/SKILLS.md`](docs/SKILLS.md),
  [`docs/MEMORY.md`](docs/MEMORY.md).
- **Subagents and parallel agents** — hand a task to a subagent with its own
  context, or a batch to a pool; a subagent's shell commands follow your own
  approval mode: [`docs/SUBAGENTS.md`](docs/SUBAGENTS.md),
  [`docs/PARALLEL.md`](docs/PARALLEL.md).
- **Model routing** — a fast model for routine turns, a strong one when the fast one
  fails its verification, stalls or runs out of room:
  [`docs/ROUTING.md`](docs/ROUTING.md).
- **MCP servers** — tools jichi does not have, from any MCP server:
  [`docs/MCP.md`](docs/MCP.md).
- **Language servers** — diagnostics, definitions, references, renames and code
  actions: [`docs/LSP.md`](docs/LSP.md).
- **Editors** — Emacs, Vim, nano, and any editor that speaks the Agent Client
  Protocol: [`docs/EDITORS.md`](docs/EDITORS.md), [`docs/ACP.md`](docs/ACP.md).
- **Long conversations** — compaction when the context fills:
  [`docs/COMPACTION.md`](docs/COMPACTION.md).
- **Snapshots and undo** — changes are snapshotted so `/undo` and `/rewind` can go
  back; files your `.gitignore` names are not restored:
  [`docs/SNAPSHOTS.md`](docs/SNAPSHOTS.md), [`docs/REWIND.md`](docs/REWIND.md).
- **Unattended runs** — a token budget, a deadline, an edit scope and a
  verification command bound a run: [`docs/AUTONOMY.md`](docs/AUTONOMY.md).
- **Semantic search** — embeddings, keyword search and reranking over your code:
  [`docs/RAG.md`](docs/RAG.md).
- **Hooks, background commands, web search, output styles** —
  [`docs/HOOKS.md`](docs/HOOKS.md), [`docs/BACKGROUND.md`](docs/BACKGROUND.md),
  [`docs/WEBSEARCH.md`](docs/WEBSEARCH.md), [`docs/OUTPUT_STYLES.md`](docs/OUTPUT_STYLES.md).
- **Images, audio and transcription** — [`docs/VISION.md`](docs/VISION.md),
  [`docs/MEDIA_GEN.md`](docs/MEDIA_GEN.md), [`docs/TRANSCRIBE.md`](docs/TRANSCRIBE.md).
- **A learning loop** — a mentor reads the run journals and proposes lessons for you
  to apply: [`docs/LEARNING.md`](docs/LEARNING.md).
- **What a run leaves behind** — telemetry, journals and how to read them:
  [`docs/TELEMETRY.md`](docs/TELEMETRY.md), [`docs/OBSERVABILITY.md`](docs/OBSERVABILITY.md).
- **Scripting** — pipes, JSON output, exit codes, and a control channel into a
  running session: [`docs/SCRIPTING.md`](docs/SCRIPTING.md),
  [`docs/CONTROL.md`](docs/CONTROL.md).

## Security

What a run may do to your machine is bounded three ways: fences that limit what it
can reach (a workspace path fence, edit scopes, read-only agents), your approvals,
and gates below the approval that no approval lifts (privileged commands, kinetic
safety). [`docs/TOOL_DECISIONS.md`](docs/TOOL_DECISIONS.md) explains them for a user;
[`SECURITY.md`](SECURITY.md) is the policy and how to report a vulnerability;
[`docs/HARDENING.md`](docs/HARDENING.md) is the hardening history; and
[`docs/LIMITS.md`](docs/LIMITS.md) lists every default limit and where its number
came from.

## jichi-harness

`jichi-harness` drives long unattended runs of jichi, or of another agent, the way
this project develops itself: it lays out a drive, fetches the team's work into a
mirror only it writes, starts one run after another and carries the handoff between
them, pushes their records, and runs the gate that decides whether a batch lands. It
is a Linux-with-systemd program for now. Start with
[`docs/HARNESS.md`](docs/HARNESS.md); the reading guide to its source is
[`docs/reading/TAZUNA.md`](docs/reading/TAZUNA.md).

## Roadmap

**Where we stand: latest milestone M1228.** What each milestone did, and why, is in
[`docs/ROADMAP.md`](docs/ROADMAP.md); what changed for users, version by version, is in
[`CHANGELOG.md`](CHANGELOG.md); how the project grew, with its numbers, is in
[`docs/PROJECT_TIMELINE.md`](docs/PROJECT_TIMELINE.md).

**Releases.** The first public release, **v0.9.0**, shipped on 2026-08-27. The current
public release is **0.15.1**. What it changed and its known issues, and what every release
before it changed, are in [`CHANGELOG.md`](CHANGELOG.md). The history of the
public cuts, as this page told it until 2026-11-23, is in
[`docs/MILESTONE_HIGHLIGHTS.md`](docs/MILESTONE_HIGHLIGHTS.md#releases).

**The first-release checklist.** Done: the rename to jichi; the curriculum, with **90
graded tasks and 75 trap cases**, every grader two-sided and checked by `make ci` wherever its
language's toolchain is installed; versioning and a
user-facing CHANGELOG; the platform verdict ([`docs/PLATFORMS.md`](docs/PLATFORMS.md)); the
licence, Apache-2.0 ([License](#license)); the public snapshot. Open: presentation slides
for the release, a logo, a jingle.

## Layout

```
include/        public headers (jc_*.h)
src/            the implementation, by subsystem; src/convert/ is jichi-convert,
                src/harness/ is jichi-harness, and src/json/cJSON.* (ours)
                implements the cJSON API
tests/          the unit suite (tests/*.c), the smoke tier (tests/smoke/), e2e,
                fuzz, bench and the test-only helper programs (tests/tools/)
docs/           the documentation; docs/README.md maps it
examples/       example configs and projects
editors/        the Emacs, Vim and nano integrations
completions/    bash and zsh completions
man/            the man page
scripts/        maintenance scripts
```

See `CONTRIBUTING.md` for the C89 coding rules this project follows.

## License

jichi is licensed under the **Apache License 2.0** (decided **2026-08-27**; the
question had been open since 2026-07-27, and the tree carried a deliberate
`LicenseRef-UNDECIDED` placeholder until the answer landed — the history is in
[`docs/LICENSING.md`](docs/LICENSING.md)).

- **The grant is the `LICENSE` file** at the repository root — the verbatim
  Apache-2.0 text, checksummed against `docs/licenses/`. `NOTICE` travels with
  redistributions (section 4(d)).
- **Copyright is held by Justus-Liebig-Universität Gießen; the author is
  Alexander-Lars Dallmann.** Two lines with two meanings, per § 69b UrhG: the
  economic rights in software written in the course of employment are exercised
  by the employer, while authorship remains with the author. Every C source and
  header carries both, on the two lines after its SPDX line, and `jichi --version`
  prints all three.
- **Continue's Apache-2.0 is a coincidence, not an inheritance.** Continue was the
  *specification* — feature set, interaction model, config format — and **no code is
  shared** (it is ~39k lines of TypeScript/React/Node; this tree is C89 throughout).
- **Credit is not a copyright line.** [`CREDITS.md`](CREDITS.md) and `NOTICE` credit
  the AI coding agents that implemented it — Claude (Anthropic), which wrote the bulk,
  and jichi itself, whose own self-development did the more recent work with a local
  model — as collaborators, not holders, because copyright generally requires human
  authorship. The debt to Continue is there too.
- **The identifier is pinned everywhere it appears.** `tests/smoke/license_lint.sh`
  holds every source header, `LICENSE`, `NOTICE`, `--version`, `describe` and
  [`docs/LICENSING.md`](docs/LICENSING.md) to one identifier; a later, deliberate
  switch (should the review choose differently) is `scripts/set-license.sh <spdx-id>`
  again — one command, same lint.
