# jichi — just code

A complete AI coding agent in **C, conforming to the C89 (ANSI C / C90)
standard**, for Linux/POSIX: chat with any LLM, an agentic tool-use loop,
retrieval, snapshots and undo, a bounded autonomy envelope, MCP/LSP/ACP
integration, an interactive terminal UI and a scriptable headless mode —
one small binary that links libcurl and nothing else.

## Start here

Four doors. Take the one that matches why you opened this file — none of them
needs the rest of this page first.

| You want to… | Go to | First command |
|---|---|---|
| **Run it** — build from source, then a guided setup and a first session | [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) → [`docs/TUTORIAL_BEGINNER.md`](docs/TUTORIAL_BEGINNER.md) | `make && ./jichi setup && ./jichi doctor` |
| **Find the right page** — 40+ guides, one routing table, no guessing | [`TUTORIAL.md`](TUTORIAL.md) | — |
| **Know what it believes** — why C89, why craftsmanship, what this owes to others, and **how jichi approaches building software** | [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md) → [`docs/APPROACH.md`](docs/APPROACH.md) | — |
| **Learn the craft** — a self-paced course with graded assignments, or read jichi's own source with a guide | [`docs/CURRICULUM.md`](docs/CURRICULUM.md) · [`docs/reading/ANNAI.md`](docs/reading/ANNAI.md) | `./jichi setup --preset learner` |
| **Run it for a group** — a shared machine, or JupyterHub for a class: what was measured, what a notebook costs, and the one configuration that loses work | [`docs/JUPYTERHUB.md`](docs/JUPYTERHUB.md) | `./jichi doctor` |

**Two things worth knowing before you invest an hour:**

- **Platforms.** **22 rows Verified** — compiled *and* gate-run on each, with the
  numbers kept per row — plus **9 Partly verified** (a named gap each) and
  **2 Never compiled**. Linux across **14 architectures** and five libcs, down to a
  256 MB VM and, in containers, to **Debian 5's glibc 2.7** (2007); **FreeBSD, NetBSD and OpenBSD** all run the full gate; **WSL2** runs
  the whole of `make ci`, Cygwin the unit and smoke tiers; Android both cross-built
  and built on-device. **illumos is partly verified and driven** (OmniOS under KVM; M658, re-measured M703 2026-09-22: clean build, 13,458 unit checks, **317 of 317** smoke drivers, both model turns driven. *Partly* names the gate — `make ci` has never run there).
  **Guix System** is partly verified and driven too (M738): built with the store's own
  toolchain inside the published VM image, headless, and both model turns run there.
  **GNU/Hurd** is partly verified and driven (M767, M768): a clean build on a microkernel
  on the first try, **342 of 342** smoke drivers, both model turns driven. One unit check
  that times a process-group kill fails there.
  **macOS (Apple Silicon)** is partly verified and driven too (M817): the first verified Mac
  build, contributed by Dima Mellnik — clean `WERROR=1` after one Darwin-only build guard,
  the unit and smoke tiers green after three test-tolerance fixes, and both model turns driven
  over the HRZ gateway. *Partly* names the gate: `make ci` has never run there.
  **Haiku** is partly verified and driven too (M830): the first Haiku build (R1~beta6, under
  qemu) is `WERROR=1` clean after a single Makefile line (its sockets are in `libnetwork`) and
  **no source guard at all** — 14,119 unit checks pass, 364 of 370 smoke drivers, both model
  turns driven over the HRZ gateway.
  **Never compiled: FreeMiNT and FreeDOS.** **FreeDOS** (DJGPP) has only been cross-compiled: since M835 the tree builds for it with no workaround (0 warnings, both binaries link; DOS has no sockets, so no daemon or control channel), and since M836 its unit suite runs in FreeDOS under QEMU (103 failures since M839, 79 of them programs DOS cannot start yet) and `jichi --version` runs there. No model has been called from DOS. **FreeMiNT**, the Atari's
  multitasking system, has no compiler of its own that has seen the tree — but jichi is
  cross-built for it and runs under the **ARAnyM** emulator: its unit suite runs to the end
  there (its failures are mostly utilities the minimal guest lacks), and since M737 it is **Driven**, calling a model from inside the Atari guest over the
  emulator's network link. [`docs/PLATFORMS.md`](docs/PLATFORMS.md) owns every verdict and says
  exactly what was measured, in Verified / Partly verified / Never compiled.
- **Licence.** **Apache-2.0**, decided 2026-08-27: the `LICENSE` file at the root is
  the verbatim text, every source carries the SPDX header, and the copyright is held by
  Justus-Liebig-Universität Gießen with Alexander-Lars Dallmann as the author (§ 69b
  UrhG separates the two — see [License](#license) and
  [`docs/LICENSING.md`](docs/LICENSING.md)).

What state the project is in, and what changed when: [Status](#status) ·
[`CHANGELOG.md`](CHANGELOG.md) (per version) · [`docs/ROADMAP.md`](docs/ROADMAP.md)
(per milestone, with the reasoning). Everything below this line is reference —
read it when you need it, not in order.

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
one, not only for one university. Nothing of the university is baked into jichi:
it reads whatever environment variable your config's `apiKeyEnv` names, and it
talks to whatever models your server advertises. `JLU_API_KEY` and the `jlu/…`
model ids are just the conventions of the **JLU HRZ gateway** the maintainer
happens to develop against — one OpenAI-compatible provider among many, whose
key-variable name and model ids its administrators may rename at any time.
Point `apiBase` / `apiKeyEnv` / `model` at *your* provider (a hosted endpoint,
a local model, LM Studio, LocalAI) and jichi is none the wiser. The two configs to
start from are
[`examples/config.openai.json`](examples/config.openai.json) and
[`examples/config.anthropic.json`](examples/config.anthropic.json) — each is the
smallest file that works with one environment variable and nothing else; see also
[docs/MODELS.md](docs/MODELS.md) and the other `examples/config.*.json` files.

*(The `jlu_continue` → `jichi` rename is long done, and by now virtually no
install still needs migrating; the old two-`mv` steps and the deprecated
`jlu_continue` symlink are kept in [docs/MIGRATION.md](docs/MIGRATION.md) only
for the historical record.)*

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

**Never compiled from source before?** [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) walks you from an empty terminal to a working build on Linux, macOS, or Windows/WSL. Linux and **WSL2** are both verified paths — the WSL2 walkthrough has been executed end to end, by a non-root user, against pristine HEAD. **macOS (Apple Silicon) is partly verified and driven** since M817 (the first Mac build, contributed by Dima Mellnik; `make ci` has not run on a Mac), and [`docs/PLATFORMS.md`](docs/PLATFORMS.md) is the one page that states, per platform, what was actually compiled and gate-run.

Built incrementally in milestones — **1143 of them**, 970 written up in full (the
gap is numbers merged, split or skipped) — each with its design and its failures
recorded. **The documentation ships in full, on purpose** — the analyses, plans,
dialogues and anecdotes, including every recorded failure, mis-diagnosis and dead
end: a project that publishes only its successes teaches nothing about how
software is actually built, so the honest record is part of the product
(decided 2026-07-28; [`docs/plans/2026-08-public-snapshot.md`](docs/plans/2026-08-public-snapshot.md) §3).
The full record is
[`docs/ROADMAP.md`](docs/ROADMAP.md) (the reasoning) and
[`CHANGELOG.md`](CHANGELOG.md) (what changed, per version). The
milestone-by-milestone table below is a summary of those two, folded away because
nobody needs the whole roadmap before their first build — click to open it,
nothing has been removed:

<details>
<summary><b>Every milestone band, M0 → now</b> (long; the canonical record is ROADMAP.md)</summary>

| Milestone | Scope | State |
|-----------|-------|-------|
| **M0** | Skeleton, build system, in-tree JSON | done |
| **M1** | Utilities (arena/sb/vec/log/snprintf/uuid), JSON wrapper, config | done |
| **M2** | HTTP (libcurl) + SSE + provider abstraction + headless chat | done |
| **M3** | Tool registry + 6 builtins + agent loop | done |
| **M4** | Session persistence (`~/.jichi.d/sessions/*.json`) | done |
| **M5** | Interactive TUI (raw ANSI line editor) | done |
| **M6** | Embedding/rerank models + semantic codebase search | done |
| **M7–M23** | User tools, `@`-refs, autocomplete/FIM, ACP server, scaffolding + packs, doc agents, assignments, MCP, subagents + parallel agents, low-resource mode, telemetry, model-call timeouts + stall-aware routing (see ROADMAP) | done |
| **M24** | Workspace path fence + resource bounds + secret redaction | done |
| **M25** | Lifecycle hooks (PreToolUse/PostToolUse/UserPromptSubmit/Stop/SessionStart) | done |
| **M26** | Background commands (`run_in_background` + read/kill) | done |
| **M27** | `web_search` tool (configurable backend) | done |
| **M28** | Command `model:`/`subtask:` frontmatter + custom output styles | done |
| **M29** | Vision input (image attachments for multimodal models) | done |
| **Quality pass** | Code review of M24–M29 + fixes: background-slot reclaim, fork/`setpgid` race, fence-before-`mkdir`, no-silent-image-drop, hard SSE field cap, shared `jc_proc_capture` (fixes the hook stdin/stdout deadlock), and `jc_logf` secret redaction | done |
| **M30** | Context-aware (chunked) auto-compaction: summarize the prefix in summarizer-context-sized windows so a small-context `summarize` model isn't sent a prefix sized to the big active model (fixes the summarizer HTTP 400) | done |
| **M31** | Prompt caching: report cached-token usage (both providers) + Anthropic `cache_control` breakpoints (system + history tail) + OpenAI `prompt_cache_key` + cache-aware cost, gated by a `promptCache` tri-state (`--prompt-cache`/`/cache`) | done |
| **M32** | Media generation tools: `generate_image` and `generate_audio` (TTS) call OpenAI-compatible endpoints (selected by `image`/`audio` model roles) and save the result into the workspace; path-fenced, permission-gated, size-capped | done |
| **M33** | Audio input: `transcribe_audio` uploads a workspace audio file (multipart) to an OpenAI-compatible transcription endpoint (selected by the `transcribe` model role) and returns the text; read-only, path-fenced, size-capped | done |
| **M34a** | External documentation index: config `docs: [{name, path}]` sources, indexed via the embeddings stack; the read-only `search_docs` tool, the `@docs:<name>` reference, and a `docs [list\|index\|search]` subcommand ground answers in external reference material | done |
| **M34b** | Session export: render any saved session to a Markdown (or self-contained HTML) transcript via the pure `jc_session_render`; the read-only `export [id] [--html] [-o file]` subcommand and the TUI `/export` for PR write-ups, handoffs, and teaching artifacts | done |
| **M34c** | Conversational rewind: `/rewind` (and the `rewind [n] [--dry-run]` subcommand) returns files **and** the conversation to an earlier checkpoint's turn — restore via the snapshot machinery + truncate history on a user-message boundary + re-save — the missing half of `/undo` | done |
| **M34d** | `ask_user` tool: the agent pauses and asks the user a focused clarifying question (optional suggested answers) via the `app->ask` front-end delegate; the TUI prompts and blocks, headless/ACP/`--auto` return a "proceed" note so an unattended run never hangs | done |
| **M34e** | `@problems` / `@folder:<dir>` references: `@problems` inlines current LSP diagnostics for the files touched this session; `@folder:<dir>` inlines a scoped repository map (source files + top-level symbols) for fast onboarding — both in the existing `@`-reference shape | done |
| **M34f** | Completion notification: opt-in terminal bell (`notifyBell`/`--bell`) and/or a `notify`/`--notify <cmd>` command run on turn (TUI) or `--auto` completion, exposing `$JICHI_NOTIFY`/`$JICHI_CWD` — step away from a long task and get pinged | done |
| **M35** | Tier-3 polish: `/cost` (live session token + estimated-cost rollup), `/fork` (branch a new session from the current point, original preserved), and a `.jichi/glossary.md` glossary of domain terms injected into the system prompt | done |
| **M36** | Emacs integration (`editors/emacs/jichi.el`): send a region or buffer to jichi and get the answer back in a side buffer, at point, appended, or replacing the region; read-only text commands + a guarded agentic `jichi-task`. Drives the headless contract (no C changes) | done |
| **M37–M109** | A long steady-state band (see [`docs/ROADMAP.md`](docs/ROADMAP.md)): resilient/fuzzy editing + `apply_patch`, mutating git tools + self-review, LSP refactors (rename/format/code-actions) + `executeCommand`, hybrid RAG (BM25 + RRF) + query rewrite + auto-context, PDF ingestion, mid-turn compaction + token-estimate calibration, the tool profile, prompt-cache TTL, the setup wizard + import/advisor, config-safety lints, the reference-root read fence, per-workspace telemetry + session timelines, the learning/mentor loop, the warm-process **daemon**, and a self-improvement band (assign/grade/dream/loop). | done |
| **2026-07 wave** | This bundle: subagent depth 2 + budget taper, opt-in skills `restrict-tools` (subagent-scoped), a hardened daemon **worker pool**, RSS/Atom feeds in `@rss:`/RAG, `setup --from-global`/`--inherit` config reuse, the `onboarding` pack + `setup --onboard`, a no-Docker **LocalAI** media-gen path (verified image gen), **Vim/nano** editor support, and a documentation refresh. | done |
| **M135–M137** | Natural-language band: the `language` key / `--language` / `/language` (answer in the user's language, one stable system-prompt line) + a compiled-in **runtime UI message catalog** (the tool-approval prompt and working indicator in en/de/es/ja/zh; `$JICHI_LANG` override, English fallback on non-UTF-8 terminals) + an **endianness tag** in the vector-index manifest (a cache written on a foreign-endian machine rebuilds instead of being silently misread). See [`docs/LANGUAGE.md`](docs/LANGUAGE.md). | done |
| **M138–M143** | Integrity + resources band: `apply_patch` **write-phase rollback** (a mid-write failure reverts already-written files and reports every file's state); the **journey map** ([`docs/JOURNEY.md`](docs/JOURNEY.md), five languages, like [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md)); **memory hygiene** (arena-lifetime fixes, unformatted session saves, an `Arenas:` gauge in `/context`) + **index residency** (clean cache loads mmap `vectors.f32` read-only; `--lite` frees the index per search); opt-in **out-of-scope auto-revert** (`--revert-out-of-scope`: shell-introduced changes outside `--edit-scope` are individually restored to the run-start baseline); and the **memory-budget overflow surfaced** (load warning + `remember`-tool note + `doctor` flag instead of silently skipping the oldest notes). | done |
| **M144–M151** | Small-model tool-calling band: native-call nudges + argument repair, a `small-local` preset + language packs, per-child parallel **verify** gate (a red write-child is quarantined, not merged), toolProfile core set. | done |
| **M152–M156** | Privileged-command safety: a `sudo`/`doas`/`pkexec` gate **below** the permission verdict + an always-on audit log; multiline paste into the TUI prompt. | done |
| **M157–M162** | Autonomous-operations arc: a loop supervisor over a task queue, `runs`/`audit` observability readers, `doctor --unattended`, and a mid-run **control socket** (`status`/`inject`/`pause`/`resume`/`abort`). | done |
| **M163** | Robotics: jichi as a robot's deliberative layer — devices as tools, a **kinetic-safety gate** below the verdict, sound I/O; hardware-free simulator in `examples/robot-sim/`. | done |
| **M164–M165** | Localized decks + timeline (de/es/ja/zh); web front-end **enablers** (`ls`/`export --output json`, `--heartbeat`) + a runnable stdlib bridge — no HTTP server enters jichi. | done |
| **M166–M169** | Findings from a live **local-GPU bench** + zigodot telemetry: fixed an empty-assistant-turn bug that silenced tool calls, split a red gate from a broken tool, coerced stringified numeric args, and made prompt-inferred constraints session-scoped. `doctor --live` probes tool calling end to end. | done |
| **M170–M173** | Release push: the rename to **jichi** (name/binaries/paths/remote/dependent projects); provenance cleanup (no vendored third-party source); the self-learner-first **curriculum** designed + its enablers built (learner/instructor presets, TUI assignment+hint loop, a from-nothing build walkthrough). | done |
| **M174–M179** | The curriculum shipped whole: all four shu-ha-ri stages, twelve module pages, **18 graded assignments** with two-sided-proven graders, progress tooling, the starter glossary, the institutional-gateway page, and the instructor guide (C1–C7 complete). Plus **versioning + CHANGELOG.md** (semver, `-V` on both binaries). | done |
| **M180–M185** | The 2026-07-28 program, first half: the **memory truth** band (self-RSS in telemetry/heartbeat/`/context`, a soak harness, four arena-lifetime fixes, the honest 12 GB verdict), the **footprint comparison** vs opencode/Claude Code, the collaboration + ML maps, multi-pack `init`, the five **SDLC journey presets** + `sdlc` pack, **accessibility advisors in every pack**, and the **stress-test fleet harness**. | done |
| **M186–M190** | New domains + boundary surveys: the **music pack** (LilyPond/MIDI/Ardour, the engraving gate as verify), **Windows mapped honestly** (WSL + the where-POSIX-ends survey assignment), **C++ and zig cc builds verified** (extern "C" guards, `cpp-check`, the honest verdicts), and the no-curl build repaired — yielding a **fully static musl jichi** from one `zig cc -target` command. | done |
| **M191–M208** | Telemetry-driven correctness + the **memory-lifetime wave**: the 12.5 GB long-run RSS **reproduced and fixed** (session-store + per-tool-call **arena lifetimes**, with the lints that keep them), UTF-8-safe truncation, token attribution, and a run of small robustness fixes surfaced by real logs (stringified-arg coercion, listing without a parse tree 193 MB → 13.6 MB, red-baseline/read-only-scope corrections). | done |
| **M209–M217** | **Python-free testing tier**: the whole portable e2e suite (71 drivers) ported to POSIX-sh **smoke** drivers backed by four C89 test helpers (mockmodel/ptydrive/jsonq/sockq), so `make check-target` is a full build gate on any POSIX box with **no python3**. | done |
| **M218–M220** | **Hardening for long unattended runs**: `mallopt`/`malloc_trim` heap-return + mid-turn args elision + buffer-shrink (probed, ASan/Valgrind-clean); robustness from the error profile (transparent tool-name aliases, `prune` + a store-size `doctor` warning, a network fault-injection site); and **small-device prep** (aarch64 musl cross-build, `JC_E2E_TIMEOUT_MULT`). | done |
| **M221–M225** | The **curriculum grows**: set D (memory & lifetimes), the C89-vs-modern-C extra, the open-source reading track, and C→Zig / C→C++ **migration tracks**; plus two complete **source-reading guide** editions — 案内（あんない） *Annai* (the guided tour) and 深掘り（ふかぼり） *Fukabori* (the deep dive), with a drift lint. | done |
| **M226–M229** | The **run-command wall-clock timeout** (+ the `supervise-long-command` skill) and **libcurl handle reuse** (connection keep-alive, fork-safe); the C-standards assignment **trilogy** completed — undefined behaviour (a sanitizer catches it) and implementation-defined behaviour (a compile-both-ways diff catches it) — all green under the full `make ci` on the low-resource reference box. | done |
| **M230–M237** | **Small-run robustness + the paradigm reading tracks.** A nudge on a byte-identical **re-read** (telemetry showed one 80–93 KB file read dozens of times in a session, each a full round-trip) and a **streaming session serializer**; plus the five paradigm tracks that teach the functional shift by contrast with jichi's own pure-core C — [Racket](docs/RACKET_PARADIGM.md), [Guile](docs/GUILE_PARADIGM.md) (the one whose runtime embeds in C, doubling as the honest story of why jichi hosts no language), [Elixir](docs/ELIXIR_PARADIGM.md) (the actor model jichi's fork pool hand-rolls), [Haskell](docs/HASKELL_PARADIGM.md) (purity as a *checked* law), [Clojure](docs/CLOJURE_PARADIGM.md) (persistent data; the hosted bet as jichi's opposite). | done |
| **M239–M243** | **Dogfooding jichi on jichi**, plus onboarding polish: the `examples/self-hosting` dev pack in two slices (read-only, then **write-enabled**), the *Python and C* curriculum track (a systems lens, not a Python course), the **`JICHI_API_KEY`** default (the university env-var name is back-compatible, not required), and a **beginner-review pass** over the tutorials + curriculum. | done |
| **M252–M253** | **Domain scaffolds surfaced** (`init --list` points at the twelve `examples/` benches + an index) and the **graded process track**: seven two-sided assignments for the half of software development no compiler checks — requirements, use-cases, design, documentation, session notes, kanban, scheduling — graded on a structural floor by pure `sh`, so it needs **no toolchain but jichi**. | done |
| **M254–M258** | **Type-ahead: you can type while the agent works.** The human at the TUI was the only operator with no mid-run channel (a supervisor had the control socket, an editor had ACP; the keyboard had Ctrl-C) — and keystrokes were echoed into the streamed answer and then discarded. Now held, echoed live, and applied at the next tool boundary as one `[operator]` message (the control channel's own convention). **Opt-in, off by default**, because input you cannot see is input you cannot correct: visibility is guaranteed while the model thinks *and* while a shell command runs (M258's idle tick), not while prose streams. Ctrl-K un-queues; `/typeahead` toggles live. Includes two pre-existing fixes the work surfaced: a **use-after-free** in the empty-answer diagnostic (M255) and a **CI gate that could not report why it failed** (M256). | done |
| **M238, M244–M251** | **Two complete graded course families.** Nine standalone language courses, each four two-sided assignments teaching the same craft (fix-forward → tests-as-proof → refactor → capstone) through one language's clearest facet: **functional** — Racket, Guile, Elixir, Haskell, Clojure (paradigm/immutability); **systems** — C (manual memory/allocators under ASan), Zig (`defer` + leak-detecting allocator), C++ (RAII), Rust (the borrow checker as compile-time safety). 36 assignments, every grader proven red-first in CI. | done |
| **M259–M263** | **The claims audit, and the lints that replaced it.** Four passes over what the project *says* about itself — docs (M259), every checkable number (M260), the code comments (M261), and the 31 scaffold packs + `examples/` materialised into a throwaway workspace and read as a user receives them (M262, which found two promises the binary did not keep). The curriculum counts had drifted because each milestone **incremented the previous claim instead of recounting** (68/41 → the measured 74/46), so the fix was a lint, not a correction. M263 then *kept* one of M262's dead promises by building it: `formatCommand`, a second `format_file` backend for languages with no LSP formatter. | done |
| **M264–M271** | **A hardware-testing plan with a virtualized tier, and the defects it flushed out.** Tier V — old glibc/kernels, big-endian, 256 MB machines, real terminal emulators — executed rather than imagined: three **pre-existing portability defects** (M265), aarch64 and **big-endian** both green (M266), and the terminal matrix driven **without a human** (M268/M274/M275). Alongside it the fuzz/fault band: a path-fence **security property**, **libFuzzer** targets, a wedged-run fix, and harness parity (M269–M271). | done |
| **M272–M277, M282** | **Real hardware.** The whole-VM rows ran on threadwork (**V2e**: gcc builds the tree *inside* a 256 MB ceiling; **V2f**: a 4.9 kernel, which took twelve guest passes and proved **three product fixes in-row** — `git stash push` on pre-2.13 git, a silent worktree leak on pre-2.17, and an `Expect: 100-continue` second per model call). Then physical silicon: **Pi Zero 2 W** on aarch64 (M272) *and*, re-flashed, **armhf** (M276 — the first real test of the `%lu`-with-casts convention where `long` is four bytes, retiring the last 32-bit risk), and the **Arduino UNO Q** (M282, a non-Pi arm64 SoC). Two boards, three full `make check-target` runs, all green; each board's passing timeout multiplier is itself a deliverable. | done |
| **M278–M283b** | **Edge AI and two composing gestures.** A plan for a local small model on the UNO Q plus the network tier (M278, plan only); Ctrl-G ghost text that **demonstrates rather than instructs** (an instruction a model may ignore is weaker than an example it can pattern-match) and advice that is printed rather than assumed (M280); edge-AI use cases and the curriculum reframed for self-learners over SSH (M283/M283b, docs only). | done |
| **M284–M285** | **Two fences nobody could see were empty.** A model selector (an agent profile's `model:`, a command's `model:`, `routing.fast`/`strong`) resolved only at *use* time, so a typo surfaced as a mid-run subagent error — or, for routing, as a run that silently never escalated; `doctor` now lints them at config time through the *same* resolver it predicts (M284). And a profile's `tools:` list is an **enforced** fence, so a dead entry silently shrinks what a specialist can do — found by reading 31 MB of telemetry, then turned into a check (M285). | done |
| **M286–M291** | **A telemetry-driven fix wave.** One honest basis for the token estimate, and a routing flag that could not be armed (M286); *paging is not repetition* — two defects hiding behind a "re-read" number (M287); the escalation trigger that was missing because it should key off **room, not difficulty** (M288); the elision placeholder **the model copied back as tool arguments** — 18 of 19 argument-shape failures on one run, each an uncached round-trip answered by a message that explained nothing (M289); the version stamp the logs never carried (M290); and a **policy refusal is not a malfunction** (M291). | done |
| **M292–M296** | **The learn loop reaches the TUI, and the model gets named.** `/learn analyze` (M292) and `/learn apply` (M293) — the latter for correctness, not convenience: applying from a *second* process cannot refresh a live session, so it went on serving notes a `## Corrections` section had already superseded. `learn corrections` (M294) makes real an operation two shipped warnings had named for months. M295 lints that every `/command` in a source string resolves — with **no exception list**, narrowing on facts about C and paths rather than guesses about English. M296 makes the TUI name the *model* and not only the routing tier, and stops a config without a `name` key printing `(null)` through `"%s"`. | done |
| **M297–M400** | Not itemised here — this table stopped being extended at M296, and adding 104 rows to a summary nobody reads is not the fix. The band covers the notice family (the agent is told *why* it was stopped), the elision claim-ticket, the registry lints (flags, config keys, telemetry events, keybindings, tool names, slash commands), the fence-integrity work, the design-and-modelling tutorials, two source-reading guide passes, and M400's never-compiled platform verdict. Per milestone with the reasoning: [`docs/ROADMAP.md`](docs/ROADMAP.md); per version: [`CHANGELOG.md`](CHANGELOG.md). | done |

</details>

Since M6, many further capabilities have landed: tiered model routing,
multi-server fallback, workspace snapshots/undo, the autonomy envelope,
structured test integration, the repository map, read-only + mutating git tools +
self-review, LSP navigation + refactors, a friendlier TUI, choose-and-resume
sessions, MCP + subagents/parallel agents, a bounded-worker-pool daemon,
low-resource mode, telemetry + a learning loop, timeouts + stall-aware routing, a
hardening band (path fence, hooks, background commands, web search, a
privileged-command and a kinetic-safety gate below the permission verdict),
command frontmatter + output styles, image (vision) input, media generation,
editor integrations (Emacs, Vim, nano, ACP), an autonomous-operations arc
(loop supervisor + observability readers + a mid-run control socket), a
teaching/curriculum layer, and — for the first public release — the rename to
**jichi**. All build under strict C89 with zero warnings; `make test` runs the
unit suite (**over 13,000 checks** — a growing figure, so stated as a bound
per the M307 rule), `make smoke` adds a **python-free** tier that makes
`make check-target` a full gate on any POSIX box, and `make ci` additionally runs
the suite under two compilers, AddressSanitizer + UndefinedBehaviorSanitizer,
Valgrind, and a fuzzer. Green end to end at **M1142** on the host gate
(2026-11-05, 06:55:06Z-07:44:23Z, on exactly 832c7930, rc 0): the unit suite is
**37,482 checks / 0 failures** in each of the nine builds (gcc, gcc -O2, clang, g++,
clang with ASan + UBSan, the gcc unit binary under valgrind, the curl-free build,
the release build, the 32-bit build) and **37,580 / 0** under FAULT=1, the
python-free smoke is **542 drivers / 3,270 checks**, and e2e, the mutant sweep and the
curl-free link are OK. The `make ci` stamp is **832c7930 / M1142** of 2026-11-05,
superseding the development box's bbbh9732 / M1075 of 2026-10-29; its numbers move
with the next full `make ci` on their head. The **0.13.0 release** is cut from the
commit that carries this record (M1143).

Each verified platform is **kept as its own stamped datum** rather than
overwritten, because "it passes on a small machine" and "it passes on that
architecture" are different claims from "it passes here". Twenty of them — a 256 MB
one-core VM, a kernel 4.9 userland, two physical ARM boards (aarch64 *and* armhf),
s390x big-endian, a static musl build, three BSD kernels, WSL2, a phone — with their
check counts and passing timeout multipliers: **[`docs/PLATFORMS.md`](docs/PLATFORMS.md)**,
which is also where the honest converse lives (**FreeMiNT has never been compiled**
on its own hardware, and the page says so in those words; **macOS joined the matrix
at M817**, first built and driven by Dima Mellnik, and **illumos joined at M658** —
both *partly verified and driven*, `make ci` having run on neither).

A live API key (or a local model) is required to exercise actual model calls. **What changed, per version, without
parsing git history: [`CHANGELOG.md`](CHANGELOG.md)** (`jichi --version`
prints where you stand). Planned next steps are tracked in
[`docs/ROADMAP.md`](docs/ROADMAP.md). The principles behind the project — why
C89, craftsmanship, and the debts this work owes — are in
[`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md).

## Building

> **Manuals.** For step-by-step install + system requirements (minimum and
> recommended), see [`docs/INSTALL.md`](docs/INSTALL.md). For running jichi over
> SSH, on embedded/low-resource devices, and headless for scripts and agents, see
> [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md). For the minimal RAM footprint,
> budget tiers, and build-time reduction on small/embedded/phone targets, see
> [`docs/LOW_MEMORY.md`](docs/LOW_MEMORY.md). For running fully offline against
> local model servers (llama.cpp, Ollama, LocalAI), see
> [`docs/LOCAL_MODELS.md`](docs/LOCAL_MODELS.md); for answering (and prompting)
> in your own language, see [`docs/LANGUAGE.md`](docs/LANGUAGE.md). For the
> road from first step to mastery — the features and docs sequenced as a
> learning path — see [`docs/JOURNEY.md`](docs/JOURNEY.md); the same road
> taught as a course with graded, self-checkable assignments is
> [`docs/CURRICULUM.md`](docs/CURRICULUM.md) — all four stages ship in-repo,
> 18 tasks from "hello, bench" to the capstone. For **driving jichi unsupervised at
> a real project** — what 28 measured runs cost, why a tight budget cap is a coin
> flip rather than a safety margin, and how to build a gate that cannot be
> satisfied without doing the work — see [`docs/DRIVING.md`](docs/DRIVING.md).

jichi compiles under **four toolchains** — gcc and clang (the CI-gated
pair, strict `-std=c89 -pedantic`), **g++** (as C++17; `make CC=g++`, see
[`docs/CPP_BUILD.md`](docs/CPP_BUILD.md)), and **zig cc** (`make CC="zig
cc"`, including a fully static `-target x86_64-linux-musl` build — see
[`docs/ZIG_BUILD.md`](docs/ZIG_BUILD.md)). The shipped build remains
gcc/clang C89; the other two are maintained *properties* of the codebase.

**ARM cross-builds** ride the same `zig cc -target` path — one command each,
zero warnings, for the single-board Linux targets (Raspberry Pi, Arduino UNO
Q; see [`docs/plans/2026-07-hardware-testing.md`](docs/plans/2026-07-hardware-testing.md)).
Both **word-sizes** compile and link clean, which exercises the 32-bit `long`
portability axis the C-standards assignments teach. Latest static-musl
`SIZE=1 HAVE_CURL=` binaries (built on the x86-64 reference box, zig 0.16.0):

| target | `jichi` | `run_tests` |
|---|---|---|
| `aarch64-linux-musl` (64-bit) | 934 KB | 1.30 MB |
| `arm-linux-musleabihf` (32-bit) | 879 KB | 1.26 MB |

Both static + stripped. This proves the arch builds off-target; the on-device
runtime numbers (footprint, the `check-target` timeout multiplier) are gathered
natively on the boards — with libcurl, since the static `HAVE_CURL=` build runs
only the offline unit + smoke suite.

```sh
make          # build both binaries: jichi and jichi-convert
make jichi   # build just the agent
make jichi-convert    # build just the config converter
make test     # build and run the unit test suite
make smoke    # POSIX-sh smoke tier -- validates a build, needs no python3
make check-target  # on-target validation: test + smoke (old/small systems)
make ci       # full gate: gcc+clang -Werror, ASan/UBSan, valgrind
make info     # show detected toolchain features
make clean
```

First-party code compiles under `-std=c89 -pedantic -Wall -Wextra` with zero
warnings — with no exemptions, and that includes the JSON implementation
(`src/json/cJSON.c`), which is ours rather than vendored and is pedantic-clean.
The exemption it used to carry was removed at M171.

### Installing

```sh
make && sudo make install          # /usr/local by default
make install PREFIX=~/.local       # or a user prefix (no sudo)
make uninstall                     # remove it
```

This installs both binaries to `$PREFIX/bin`, a man page
(`man jichi`), and bash/zsh completions. `DESTDIR` is honored for
staged/packaged builds.

### Dependencies

- **libcurl** — for HTTPS/TLS/SSE in the networking milestones (M2+). The
  build auto-detects it; the core and test suite build fine without it.
  On Debian/Ubuntu install the development headers with:

  ```sh
  sudo apt-get install libcurl4-openssl-dev
  ```

  (The runtime library is usually already present; only the `-dev` headers
  and the `libcurl.so` linker symlink are needed to compile against it.)

- **A JSON library** — `src/json/cJSON.{c,h}` is a compact, C89-clean
  implementation of the cJSON **API**. It is original code, not a vendored copy
  of that library: jichi ships no third-party source at all. Only the API is
  shared, deliberately, so upstream
  [cJSON](https://github.com/DaveGamble/cJSON) can replace those two files as a
  pair without changing the rest of the tree.

## Usage

```sh
# Guided one-shot project setup: role preset -> assets + config + start-script
# + validation. New here? Start with this. (docs/SETUP_WIZARD.md)
./jichi setup                 # interactive; or pass --preset/--provider/--model

# Scaffold a starter set of project assets (agents/skills/commands/AGENTS.md)
./jichi init                  # writes ./.jichi (non-destructive; --dry-run, --force)

# Interactive agent (TUI): edit lines, history, tool-permission prompts
./jichi

# Headless: run one prompt and stream the reply to stdout
./jichi -p "explain what this repo does" < /dev/null

# Headless prompt from stdin (handy for piping files or scripts)
cat spec.md | ./jichi -p -

# Pipe data as context to a prompt; machine-readable output; quiet
cat error.log | ./jichi -p "what caused this error?"
./jichi --output json -p "hi" < /dev/null | jq -r .text
./jichi -q -p "..." < /dev/null            # no diagnostics on stderr

# Conversations: continue this dir's latest, or resume a specific one
./jichi -c                    # continue the most recent chat here
./jichi ls                    # list this project's saved chats (newest first)
./jichi ls --all              # every project
./jichi --session 3f9a -p "…" < /dev/null # resume a specific chat by id/prefix

# Operating modes (see "Agent modes" below)
./jichi --plan                # investigate + propose a plan, change nothing
./jichi --auto                # run approved tools without asking (see the note below)
./jichi --readonly            # forbid mutating tools

# Bounded unsupervised run: verify, budget, auto-rollback (see "Autonomous runs")
./jichi --auto --verify 'make && make test' --budget-tokens 200k \
  -p "fix the failing tests" < /dev/null

# Semantic codebase search (embedding + rerank models)
./jichi index                 # build/update the index cache
./jichi index --reindex       # rebuild from scratch
./jichi embed "some text"     # print an embedding vector summary
./jichi rerank "query" "doc a" "doc b"   # score documents
```

> **On bare `--auto`:** it removes the approval prompt and nothing else — no
> budget, no verifier, and therefore **no rollback** (rollback needs a verifier
> to declare the tree broken and a green checkpoint to return to). Its only net is
> `/undo`, and only while snapshots are on. For anything unattended use the
> bounded form above, and read [`docs/AUTONOMY.md`](docs/AUTONOMY.md) first.

Configure a model first — `jichi setup` asks your endpoint which models it
serves, or write four lines of JSON yourself ([`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md) §0a).
**An API key alone is not enough:** jichi chooses no provider and no model for you,
so `jichi doctor` will say `no model is configured` until one is named. A server
on your own machine needs no key at all.
In the interactive UI, `/help` lists commands and `/status` shows the session at
a glance (model, mode, routing, snapshots, tokens, cwd); `/exit` quits. The
prompt shows `[mode·model]`. When the agent wants to run a tool it shows the
action and a single-keypress prompt:

```
▸ write_file  notes.txt
  Allow? [y]es  [n]o  [a]lways  [v]iew
  ›
```

`y` allows it once, `a` allows that tool for the rest of the session, `v` shows
the full arguments, anything else declines (the safe default). Color follows the
terminal but honors `NO_COLOR` and `--color`/`--no-color`; non-UTF-8 locales get
ASCII markers.

The assistant's reply is rendered with **markdown + light syntax highlighting**
as it streams (headings, bold/italic, lists, fenced code blocks), preceded by a
dim `model · mode` line so you can see which model answered. Toggle it with
`/markdown`, `--no-markdown`, or `"markdown": false`; it's TUI-only (headless
output stays raw). See [`docs/TUI_RENDER.md`](docs/TUI_RENDER.md).

**You can type while it works** — opt in with `--type-ahead` or
`"typeAhead": true`. Whatever you write during a turn is collected and echoed
beside the working indicator; press **Enter** and it is queued, then applied at
the agent's next step — as a mid-turn course correction if the turn is still
running, or as your next message if it has finished. Ctrl-C still aborts, Ctrl-U
clears the line you are typing, **Ctrl-K** un-queues a line you already
committed, and a line you typed without pressing Enter is printed back rather than
silently dropped. Your typing stays visible while the model thinks *and* while a
shell command runs — a build or a test suite — which is where the long waits are;
it is not shown while prose streams (the echo returns as soon as the next model
call starts). It is **off by default** because "not shown" is not "nothing":
input you cannot see is input you cannot correct, so this is a choice you make
rather than inherit. Toggle it live with `/typeahead`. The visibility contract and
the design decisions are in [`docs/TYPE_AHEAD.md`](docs/TYPE_AHEAD.md).

Press **Tab** to complete the token under the cursor: slash-command names
(built-in and custom), session ids after `/resume`, model names after `/model`,
`chat`/`plan`/`auto` after `/mode`, and `@file` paths. It is instant and offline
(one match completes, several list). Press **Ctrl-G** at the end of a line for a
model **ghost-text** suggestion that continues your prompt (dim; **Tab** accepts,
any other key dismisses). For an LLM continuation of arbitrary text there is a
headless `jichi complete [text]` (or text on stdin), and for editor "tab
autocomplete" `jichi fim <file> <offset>` fills in the code at a cursor — all
using the `autocomplete`-role model. See
[`docs/AUTOCOMPLETE.md`](docs/AUTOCOMPLETE.md).

In headless mode the answer goes to stdout and all diagnostics to stderr, so it
pipes cleanly; exit codes are 0 ok / 1 error / 2 usage / 130 interrupted (a
closed downstream pipe like `| head` exits 0). With no `-p` and a non-terminal
stdin, the piped input is the prompt. See [`docs/SCRIPTING.md`](docs/SCRIPTING.md)
for the full routing, `--output json`, `-q`, `--no-stdin`, and `--no-session`.
Note: this changes the old behavior where `printf 'a\nb\n' | jichi` ran a
line-by-line REPL — it now runs one headless turn over all of stdin.

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
stricter one -- a saved mode never widens a run (M845), so to resume in auto mode,
start with `--auto`. The agent's tools act on your current directory. In the TUI: `/sessions` lists, `/resume <id>` switches conversations,
and `/title <text>` names the current one. `--no-session` disables saving.

## Migrating from Continue

With the [upstream Continue repository](https://github.com/continuedev/continue)
archived after its final 2.0.0 release, this path matters more than it used
to: `jichi-convert` turns a Continue configuration into a jichi config:

```sh
# Modern assistant config.yaml or legacy config.json, auto-detected
./jichi-convert ~/.continue/config.yaml            # prints to stdout
./jichi-convert config.yaml -o ~/.jichi

# With no argument it looks for ~/.continue/config.yaml then config.json
./jichi-convert
```

It selects the first chat-capable model (by `roles`, falling back to the first
model), copies the provider/model/apiBase and completion options, and keeps a
literal `apiKey` or substitutes `apiKeyEnv` when the key is templated or
absent. Providers other than `anthropic`/`openai` are mapped to the
OpenAI-compatible path (preserving `apiBase`) with a note on stderr. Diagnostics
go to stderr so stdout stays pipe-clean.

## Configuration

Configuration is JSON (no YAML). Resolution order: `--config <path>`, then
`$JC_CONFIG`, then `./local/config.json` (project-local, git-ignored — handy for
dev), then `~/.jichi`. New to the model/routing/autonomy options? See the
step-by-step [`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md). The shortest
config that works costs nothing — a model server on your own machine (LM Studio,
`llama.cpp`, Ollama):

```json
{
  "models": [
    { "name": "local", "provider": "openai",
      "model": "<the id your server lists>",
      "apiBase": "http://localhost:1234/v1" }
  ],
  "maxToolIters": 25,
  "maxRetries": 4
}
```

No key line: a server on your own machine usually wants none. **`provider` is
the wire dialect, not a company** — `"openai"` means any OpenAI-compatible server,
`"anthropic"` the Anthropic Messages API — and jichi guesses neither the dialect
nor a key: an entry that names no `provider` is refused, and a vendor's key
variable is read without being named only for that vendor's own endpoint
([`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md) §1.5). A paid endpoint takes
its key from the environment, named with `apiKeyEnv`, and should declare what it
costs, so `/cost`, the exit total and any budget mean something:

```json
{
  "models": [
    { "name": "Claude", "provider": "anthropic", "model": "claude-opus-4-8",
      "apiKeyEnv": "ANTHROPIC_API_KEY",
      "inputCostPer1M": 15.0, "outputCostPer1M": 75.0 }
  ]
}
```

Both dialects are supported through a pluggable provider abstraction.

### Multiple models

List several entries in `models` to switch between them at runtime (a single
`model` object works too). The first entry is active on startup.

```json
{
  "models": [
    { "name": "local", "provider": "openai",
      "model": "<the id your server lists>",
      "apiBase": "http://localhost:1234/v1" },
    { "name": "Claude", "provider": "anthropic", "model": "claude-opus-4-8",
      "apiKeyEnv": "ANTHROPIC_API_KEY",
      "inputCostPer1M": 15.0, "outputCostPer1M": 75.0 }
  ],
  "maxToolIters": 25
}
```

In the interactive UI, `/model` lists the configured models and `/model <n>` or
`/model <name>` switches (recreating the provider). `jichi-convert` emits a
`models` array containing every model from the Continue config, with the first
chat-capable model placed first/active.

### Model roles

Each model may declare a `roles` array. The agent's chat turns always use the
active model, but the **semantic search** features pick a model by role:

- `"embed"` — used to embed code chunks and queries (codebase search / `index`
  / `embed`).
- `"rerank"` — when present, reorders search candidates by relevance.

Two more role strings are acted on today: `summarize` selects the compaction
summarizer and `autocomplete` the completion model. The rest (`chat`, `edit`,
`apply`) are parsed and preserved by `jichi-convert` but not yet acted on. Example:

```json
{
  "models": [
    { "name": "local", "provider": "openai",
      "model": "<the id your server lists>",
      "apiBase": "http://localhost:1234/v1", "roles": ["chat", "edit"] },
    { "name": "Embedder", "provider": "openai", "model": "<an embedding model>",
      "apiBase": "https://my-host/v1", "apiKeyEnv": "MY_HOST_API_KEY",
      "roles": ["embed"] },
    { "name": "Reranker", "provider": "openai", "model": "<a rerank model>",
      "apiBase": "https://my-host/v1", "apiKeyEnv": "MY_HOST_API_KEY",
      "roles": ["rerank"] }
  ]
}
```

`my-host`'s key has a name of its own: a vendor's variable such as
`OPENAI_API_KEY` sent to a host that is not that vendor's is exactly what
`jichi doctor` warns about.

### Multiple servers & fallback

Each model names its own server via `apiBase`, so one config can mix a remote
endpoint with a **local** OpenAI-compatible server (Ollama / LM Studio / vLLM,
etc.). A model may declare a **`fallback`** selector; when it's used and its
server is unreachable, jichi probes (`GET {apiBase}/models`, short
timeout) and transparently switches to the first reachable model in the chain,
logging `[fallback] local-gemma unreachable -> jichi-gemma`. Combine with routing
(`fast` = local + `fallback`, `strong` = remote) so routine turns run locally
when the local server is up and on the remote when it isn't.

```sh
jichi models     # list models + a live [reachable]/[UNREACHABLE] probe
jichi doctor      # full setup health check (exit 1 if anything's broken)
```

`doctor` is the fastest way to diagnose a setup: it checks libcurl, the config
and active model, the API key (present/absent — never printed), every server's
reachability, embed/rerank role coverage, git + snapshots, and MCP/LSP servers,
printing a `✓`/`!`/`✗` checklist with fix hints. See [`docs/DOCTOR.md`](docs/DOCTOR.md).

End `apiBase` in `/v1` (e.g. `http://127.0.0.1:1234/v1`) so chat *and*
embeddings resolve. For a **dev** config, drop it at `./local/config.json`
(git-ignored) — it's auto-discovered ahead of `~/.jichi`. A secret-free
template lives at `examples/config.multi-server.json`. See
[`docs/MODELS.md`](docs/MODELS.md).

Optional fields:
- `maxRetries` — retries on transient HTTP failures (429, 5xx, transport
  errors) with exponential backoff. Non-transient errors (e.g. 401) are not
  retried. Default `4`.
- `inputCostPer1M` / `outputCostPer1M` — USD per million tokens. When set,
  jichi reports an estimated cost alongside token counts.

## Tools

Built-in tools available to the agent: `read_file` (line-numbered, with
`offset`/`limit` for slices), `write_file`, `edit_file` (exact-string replace,
read-first guard; returns a diff), `apply_patch` (many edits across many files in
one **atomic** call, returns per-file diffs — see *Editing & diff preview*),
`list_files` (directories marked `/`), `search_code` (optional `context` lines)
(grep-based), `run_terminal_command`, `run_tests` (runs the tests and returns a
*parsed* failure summary — see *Structured test integration*), `fetch_url`
(HTTP/HTTPS GET, capped output), `codebase_search` (semantic retrieval — see
below), `spawn_subagent` (delegate a subtask — see *Subagents*),
`git_status`/`git_diff`/`git_log`/`git_blame` (read-only, in a git repo — see
*Git tools & self-review*), `todowrite`/`todoread` (track a task list across
a run), and `remember` (save a durable note — see *Persistent memory*). Token
usage (and cost, if priced) is printed after each turn and summarized for the
session.

## Editing & diff preview

Edits use **exact-string replacement** (find a unique snippet, replace it) rather
than line numbers, which is robust against line drift. `edit_file` does one
replacement; **`apply_patch`** does many across many files in a single **atomic**
call (every edit validated and applied in memory first, files written only if all
succeed) — fewer round-trips and no half-applied multi-file change.

Before you approve an edit, the TUI shows a **colored unified diff** of exactly
what will change (for `write_file`/`edit_file`/`apply_patch`), computed with the
same core the tool uses — so what you see is what gets written:

```
▸ edit_file  notes.md
@@ -1,3 +1,3 @@
 # Notes
-TODO: write this
+Done.
  Allow? [y]es  [n]o  [a]lways  [v]iew
```

`/diff` in the TUI shows everything changed since the last checkpoint. See
[`docs/EDITING.md`](docs/EDITING.md).

## User-defined tools

Add your own tools without writing C or running an MCP server: declare them in
config and the agent can call them. Each `tools` entry maps a name + JSON Schema
to a local command; the model's arguments arrive as JSON on the command's stdin
and as `JICHI_ARG_<NAME>` env vars (never on the command line), and the command's
output comes back as the tool result.

```jsonc
"tools": [
  { "name": "countbytes",
    "description": "Count the bytes in the given text.",
    "schema": { "type": "object",
                "properties": { "text": { "type": "string" } },
                "required": ["text"] },
    "shell": "printf '%s' \"$JICHI_ARG_TEXT\" | wc -c",
    "readonly": true }
]
```

They flow through the normal permission system (chat asks for mutating ones,
`--auto` runs them, plan/read-only hides them, `permissions.deny` applies) and
can't shadow a built-in. See [`docs/USER_TOOLS.md`](docs/USER_TOOLS.md) and
[`examples/config.user-tools.json`](examples/config.user-tools.json).

## References (@-context)

Mention context in a message with `@` and it's pulled into the turn (bounded)
before the model runs: `@<path>` inlines a file, `@diff` the working-tree git
diff, `@url:<url>` a fetched page, and `@sym:<name>` a symbol's definition (via
the language server, or a code-search fallback).

```sh
./jichi -p "Summarize @src/main.c and explain @diff" < /dev/null
```

Recognition is conservative — an `@` only counts at a word boundary and a file
`@token` only expands if it exists — so emails and `@decorators` stay literal.
Custom commands keep their own template expansion; `"references": false`
disables it. See [`docs/REFERENCES.md`](docs/REFERENCES.md).

## Git tools & self-review

In a git repository the agent gets four **read-only** git tools — `git_status`,
`git_diff`, `git_log`, `git_blame` — to inspect the working tree and history
(run argv-style against your own repo, no shell). It also gets four **mutating**
ones — `git_add`, `git_commit`, `git_branch`, `git_stash` — so it can record its
own work as reviewable commits. Those are permission-gated like any write (asked
in chat, automatic under `--auto`, hidden in plan mode); `git_commit` refuses an
empty message, and **nothing is ever pushed**. And **self-review**: before
finishing a top-level turn that changed files, the agent reviews its own diff
once and fixes problems before returning. Self-review defaults to `--auto` runs
only (configurable via `selfReview` and `--review`/`--no-self-review`; TUI
`/review`). See [`docs/GIT.md`](docs/GIT.md).

## Repository map

At startup jichi builds a **repository map** — a compact index of the
project's source files and each file's top-level symbols (functions/types/
classes) — and injects it into the system prompt so the agent knows the layout
before it starts, rather than grep-guessing. Symbol extraction is a fast,
language-keyed heuristic (C/C++, Python, Go, Rust, JS/TS, Java, Ruby, shell — no
LSP), the output is byte-bounded (`repoMapLimit`), and the whole thing is one
flag from off (`"repoMap": false`). Print it with `jichi map` (no API
key) or the TUI `/map`. See [`docs/REPOMAP.md`](docs/REPOMAP.md).

## Structured test integration

jichi parses test/build output (JUnit-XML, TAP, or a generic
`file:line` + failure-marker scan) into a structured summary — pass/fail counts
and per-failure name/`file:line`/message — instead of dumping raw logs. This
powers the `run_tests` tool, the `test` CLI subcommand
(`jichi test "make test"`, exits with the command's own code), and the
autonomy envelope's fix-forward loop (failures fed back precisely, not a blind
truncated tail). Configure the default command with `testCommand` (else the
`verify` command is used). See [`docs/TESTING.md`](docs/TESTING.md).

## Project rules & custom commands

- **Rules** — `AGENTS.md` files (a global one, every `AGENTS.md`/`CLAUDE.md` from
  the git root down to the cwd, and config `instructions` paths) are loaded into
  the system prompt automatically, so the agent follows your project's
  conventions. `AGENTS.md` is the primary channel for teaching the in-loop model
  how your project works — see [`docs/AGENTS_GUIDE.md`](docs/AGENTS_GUIDE.md) for
  how to write an effective one, and [`docs/RULES.md`](docs/RULES.md) for the
  loading mechanics. (To *drive* jichi from another tool/agent, the machine-facing
  contract is `jichi describe --output json`.)
- **Custom commands** — markdown files in `.jichi/commands/` (or
  `~/.config/jichi/commands/`) define `/name` prompt templates with
  `$ARGUMENTS`, `$1`/`$2`, `` !`cmd` `` (shell output), and `@file` substitution.
  Run them in the TUI or as the `-p` prompt. See
  [`docs/COMMANDS.md`](docs/COMMANDS.md).
- **Skills** — folders `.jichi/skills/<name>/SKILL.md` (or under
  `~/.config/jichi/skills/`) define model-invoked, progressively-disclosed
  instruction sets. Only each skill's name + description is in the system prompt;
  when a task matches, the agent calls the `load_skill` tool to pull the full
  instructions into context. A skill may list `allowed-tools`, but at the top
  level that is **advisory** — it is rendered as a suggestion and nothing is
  blocked; it becomes an enforced fence only inside a subagent whose skill also
  sets `restrict-tools: true`. List them with `/skills` or
  `jichi skills`. See [`docs/SKILLS.md`](docs/SKILLS.md).
- **Memory** — durable notes the agent saves with the `remember` tool to
  `.jichi/memory.md`, loaded into the system prompt every session (so it remembers
  project conventions, decisions, and your preferences across conversations).
  View with `/memory` or `jichi memory`; it's plain markdown you can edit.
  See [`docs/MEMORY.md`](docs/MEMORY.md).

## Subagents

The agent can delegate a scoped subtask to a fresh **subagent** via the built-in
`spawn_subagent` tool — a synchronous, Task-style nested agent that runs to
completion and returns its final answer. A subagent can run on a different
configured model (`model`: a name/id substring, index, or role) and be fenced to
read-only tools (`readonly: true`), e.g. for research or review.

The spawn goes through the normal permission gate (so it asks in chat mode, runs
in auto, and is hidden in plan/read-only mode); the subagent then runs
auto-approved within its sandbox, but config/MCP `deny` still applies and a
read-only parent forces a read-only child.

Orchestration is **depth-bounded**, not single-level: `maxSubagentDepth`
(default **2**) lets a subagent spawn one grandchild and no deeper, while
`spawn_parallel` stays top-level only — no nested fork pools. Set it to `1` for
strictly single-level, or `0` for none (which is the `--lite` default).
`maxSubagentIters` bounds each subagent's tool loop, halving per level so raising
the ceiling cannot multiply the total budget. See
[`docs/SUBAGENTS.md`](docs/SUBAGENTS.md).

## Parallel agents

For *independent* subtasks, the built-in `spawn_parallel` tool runs several
subagents **concurrently** in a fork-based worker pool sized to the CPU (default
`min(cores, 8)`; set `"maxParallelAgents"`), then aggregates their answers:

```jsonc
spawn_parallel({ "tasks": [
  { "task": "audit the auth code for bugs" },           // read-only
  { "task": "review error handling", "model": "qwen3" },
  { "task": "refactor module A", "write": true },        // edits a worktree
  { "task": "refactor module B", "write": true }
]})
```

Read-only tasks investigate the live workspace; **write** tasks (`write: true`)
each edit an isolated **git worktree**, and the parent merges their changes back
**file-level, first-wins** — disjoint edits apply cleanly, overlapping files keep
the first writer and report the rest as conflicts (never auto-merged). Token
usage counts toward the [envelope](docs/AUTONOMY.md) budget, Ctrl-C reaps every
child, and write tasks need git (else they run read-only). Use it over multiple
`spawn_subagent` calls when the subtasks are independent. See
[`docs/PARALLEL.md`](docs/PARALLEL.md).

## Model routing

Nominate a **fast** and a **strong** model and the agent runs **fast-first,
escalating to strong** when a turn proves hard (by default, when the
[envelope](docs/AUTONOMY.md) verifier fails) — cheap by default, strong when it
counts. Configure it three ways:

```jsonc
// config:
"routing": { "fast": "qwen3-coder", "strong": "opus",
             "escalateOnVerify": true, "escalateOnError": false }
```

```sh
# CLI (overrides config):
jichi --auto --route-fast qwen3-coder --route-strong opus \
  --verify 'make test' -p "fix the failing tests" < /dev/null
jichi --no-route --model opus -p "..." < /dev/null   # pin one model
```

```
# TUI:
/route                     show state (tiers, escalation, current model)
/route on | off
/route fast <model> | strong <model>
```

A selector is a model name/index/role. Routing is enabled by default but inert
until `fast` and `strong` name two distinct models, so out of the box nothing
changes. Each switch logs `[route] -> <model> (<reason>)` and emits a journal
`route` event. Escalation is the main agent's only; subagents keep their own
model. See [`docs/ROUTING.md`](docs/ROUTING.md).

## Agent modes & permissions

The agent runs in one of three **modes**, controlling how autonomously it acts:

- **chat** (default) — read-only tools run freely; mutating tools (write/edit/run)
  ask for permission first.
- **plan** — read-only only: the agent investigates and proposes a step-by-step
  plan without changing anything. Review it, then switch modes to execute.
- **auto** — approved tools run without prompting, bounded by `maxToolIters`.

Set the startup mode with `--plan` / `--auto`, or `"mode"` in the config. In the
TUI, switch live with `/mode chat|plan|auto`, `/plan` (`/plan off`), or `/auto`;
the prompt shows the current mode (`[plan] >`).

Independently of mode, a top-level `permissions` block fences individual tools by
their registered name (a built-in's short name or an MCP `server__tool` name):

```json
"permissions": { "allow": ["edit_file"], "deny": ["run_terminal_command"] }
```

Each list is tool names or `"*"`/`true` for all; `deny` wins and **hides** the
tool from the model entirely. In headless mode (`-p`), a tool that would
otherwise prompt is refused (re-run with `--auto`). The full model, precedence,
and truth table are in [`docs/AGENT_MODES.md`](docs/AGENT_MODES.md).

## MCP servers

jichi is an [MCP](https://modelcontextprotocol.io) client: it connects to
Model Context Protocol servers and exposes their **tools** to the agent alongside
the built-ins, their **resources** via `@mcp:<uri>` and the `read_mcp_resource`
tool, and their **prompts** as slash commands. Full reference, including the
approval policy and the security posture: [`docs/MCP.md`](docs/MCP.md). Declare
servers in an `mcpServers` **array** (not an object — jichi warns if you use the
Claude Code / Continue shape) in your config:

```json
{
  "model": { "provider": "openai", "model": "<the id your server lists>",
             "apiBase": "http://localhost:1234/v1" },
  "mcpServers": [
    { "name": "fs", "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "."],
      "env": { "FOO": "bar" } },
    { "name": "remote", "type": "http",
      "url": "https://example.com/mcp",
      "headers": ["Authorization: Bearer TOKEN"],
      "autoApprove": "*" }
  ]
}
```

Each server's tools are registered as `<name>__<tool>` (so `fs`'s `read_file`
becomes `fs__read_file`). Two transports are supported:

- **stdio** — the server is spawned as a subprocess (`command` + `args`, with
  optional `env`); JSON-RPC is exchanged over its stdin/stdout. The default when
  no `type`/`url` is given.
- **http** — the server is an HTTP endpoint (`url`, with optional `headers`).
  Responses may be JSON or Server-Sent Events; a returned `Mcp-Session-Id` is
  echoed on later requests. Requires libcurl.

### Approval policy

By default, calling an MCP tool prompts for permission (like any mutating tool).
Per server, `autoApprove` and `deny` override that for named tools — each is a
list of (un-namespaced) tool names, or `"*"`/`true` to mean every tool:

- **autoApprove** — these tools run without a prompt.
- **deny** — these tools are not advertised to the model at all, and are
  refused if named anyway (`deny` wins over `autoApprove`, and overrides even
  `--auto`).

Anything in neither list is asked as usual. The `mcp` listing reports how many
tools are advertised to the model and tags each tool `[auto]` or `[deny]` so
you can see the resolved policy.

A server that fails to connect is logged and skipped — the agent still runs with
whatever connected. Diagnose your config without starting a chat:

```sh
./jichi mcp                       # connect, list servers + their tools
./jichi mcp call fs__read_file '{"path":"README.md"}'   # invoke one tool
```

In the interactive UI, `/mcp` lists the connected servers and tool counts.

## LSP diagnostics & code navigation

Configure language servers and jichi surfaces real compiler/type errors
(after an edit, the matching server's diagnostics are appended so the model can
fix them) **and** gives the agent symbol-accurate **code navigation** — far more
precise than grep.

```json
{
  "lspServers": [
    { "name": "clangd", "command": "clangd", "extensions": ["c", "h", "cpp"] }
  ]
}
```

When `lspServers` is set, three read-only tools are offered to the agent:
`find_definition`, `find_references`, `list_symbols` (by symbol name; a file is
optional — the project is searched otherwise). Same from the CLI (no API key
needed):

```sh
./jichi lsp src/main.c                 # diagnostics; exit 1 if any
./jichi lsp symbols src/main.c         # file outline
./jichi lsp def  src/main.c run_headless   # go to definition
./jichi lsp refs src/main.c run_headless   # find references
```

Servers start lazily and are matched by extension. For cross-file C/C++ accuracy,
generate a `compile_commands.json` (e.g. `bear -- make`). See
[`docs/LSP.md`](docs/LSP.md).

## Editor integration (ACP server)

Run jichi as an **Agent Client Protocol** server so an editor (e.g. Zed) can drive
it as an agent — opening sessions, sending prompts, and rendering the streamed
reply, tool activity, and tool-permission requests:

```sh
./jichi --acp           # JSON-RPC 2.0 over stdin/stdout
```

The editor spawns that command. jichi handles `initialize` / `session/new` /
`session/prompt` / `session/cancel`, streams `session/update` notifications
(`agent_message_chunk`, `tool_call`, `tool_call_update`), and asks for tool
approval with `session/request_permission` — the same `jc_perm` policy as the
TUI. All normal config (model, tools, MCP, LSP, snapshots, permissions) applies.
See [`docs/ACP.md`](docs/ACP.md).

## Auto-compaction

Long sessions are kept within the model's context window automatically. Between
turns, when the history approaches the context budget, jichi summarizes
the older messages into a single note and keeps the most recent turns verbatim —
so a session can run (and `--resume`) indefinitely without overflowing context.

```json
{
  "autoCompact": true,
  "contextLimit": 0,
  "models": [
    { "provider": "openai", "model": "gpt-x", "contextLength": 128000 }
  ]
}
```

- `autoCompact` (default `true`) is the master switch.
- The token budget is `contextLimit` if set, else the active model's
  `contextLength`, else a built-in default.
- A model with the `summarize` role does the summary call when present;
  otherwise the active model summarizes itself.

In the TUI, `/compact` forces it immediately. See
[`docs/COMPACTION.md`](docs/COMPACTION.md).

## Workspace snapshots & undo

Before the agent's first file change in a turn, jichi checkpoints your
workspace, so you can take edits back:

```
/undo          revert the workspace to the last checkpoint
/checkpoints   list this session's snapshots
```

The same checkpoints are reachable from the command line (e.g. after a headless
run, or across sessions):

```sh
jichi checkpoints   # list snapshots, newest first
jichi undo [N]      # restore the workspace to the N-th most recent (default 1)
jichi undo --dry-run  # preview what undo would change, without applying
```

It uses a **shadow git repository** under `~/.jichi.d/checkpoints/` whose
work tree is your project — your own `.git` is never touched. Undo restores a
checkpoint (`git reset --hard` + `clean`, so files the agent created are removed
too). Checkpoints are workspace-scoped and **persist across sessions**; the
shadow repo is auto-pruned to the most recent `"snapshotLimit"` (default 100)
checkpoints. Set `"snapshots": false` to disable; it also disables itself if
`git` isn't on `PATH` or the workspace is huge and un-git-managed. Files only — undo reverts the
workspace, not the conversation. See [`docs/SNAPSHOTS.md`](docs/SNAPSHOTS.md).

## Autonomous runs (the envelope)

`--auto` lets the agent run unsupervised. The **autonomy envelope** makes that
safe to leave running: declare limits, gate the result on a verifier, and have
the workspace roll back automatically if the agent can't make it pass.

```sh
jichi --auto --verify 'make && make test' \
  --budget-tokens 200k --deadline 20m --max-tool-calls 60 \
  --edit-scope 'src/**' --edit-scope 'tests/**' \
  -p "Fix the failing parser tests" < /dev/null
```

- **Budgets** — `--budget-tokens` (e.g. `200k`/`1m`), `--deadline` (`30s`/`20m`/
  `2h`), `--max-tool-calls`. Hitting one stops the run.
- **Verify gate** — `--verify <cmd>` must exit 0 when the agent finishes. On
  failure the agent fix-forwards (`--verify-retries`, default 3; `0` rolls back
  on the first failure); when those run out the workspace is rolled back to the
  last known-good checkpoint and the run exits 1 (`--no-rollback` leaves it as-is
  to inspect). `--verify-timeout <dur>` kills a hung verifier;
  `--verify-baseline` checks (and records) whether the tree passes before the
  run.
- **Edit-scope** — `--edit-scope <glob>` keeps the `edit_file`/`write_file`/
  `apply_patch` tools inside given paths, at every agent depth (subagents and
  parallel write children included, M133). (Rollback is the total guarantee; it
  reverts every change however it was made.) `--strict-scope` also forbids
  `run_terminal_command`, so the scope is a hard boundary with no shell escape.
- **Audit journal** — every decision is written to
  `~/.jichi.d/runs/<id>.jsonl` (`--journal <path|->`).

In headless mode the envelope flags imply `--auto`. `verify` and `editScope` can
also be set in the config file. The TUI `/verify` runs the verifier on demand.
See [`docs/AUTONOMY.md`](docs/AUTONOMY.md).

### Security hardening (M130–M134)

A review-driven hardening pass tightens the boundary between the agent's own
secrets/host and model-directed execution: provider API keys are scrubbed from
every child process environment (so a model-issued shell command can't read
them), `fetch_url` blocks SSRF to loopback/link-local/private addresses (with a
connect-time check that also defeats DNS-rebinding and redirects), on-disk sinks
(sessions, telemetry, journal, calibration) are written owner-only (`0600`) with
secret redaction on the full-tier event log, `--edit-scope` is enforced for
delegated agents, and `learn analyze` now mines verify-failure / budget-rollback
/ hard-error signals. Full design rationale and diagrams — plus the larger
follow-on recommendations — are in [`docs/HARDENING.md`](docs/HARDENING.md).

## Semantic codebase search

When an `embed`-role model is configured, jichi can index the workspace
and answer "where is the code that …?" queries by meaning rather than exact
keywords. The agent calls the `codebase_search` tool; the same machinery is
exposed on the command line via `index`, `embed`, and `rerank`.

How it works:

1. **Chunking** — text files under the working directory are split into
   line-bounded chunks (~1500 chars), skipping `.git`, `node_modules`, build
   output, binaries, and files over 1 MB.
2. **Embedding** — chunks are embedded in batches via the `embed` model
   (`POST {apiBase}/embeddings`).
3. **Index cache** — vectors are persisted under
   `~/.jichi.d/index/<workspace>/` (`manifest.json` + a raw `vectors.f32`
   blob). Rebuilds re-embed only files whose modification time changed; a cache
   built with a different model or dimension is rebuilt automatically. The blob
   is host-endian and single-machine.
4. **Search** — the query is embedded, the cosine-nearest chunks are taken, and
   if a `rerank` model is configured they are reordered by relevance
   (`POST {apiBase}/rerank`, accepting both `data[]`/`results[]` shapes). The
   best `file:line` snippets are returned.

```sh
./jichi index            # build/update the cache (prints file/chunk counts)
./jichi index --reindex  # ignore the cache and re-embed everything
./jichi embed "text"     # one embedding: dim, L2 norm, preview
./jichi rerank "q" "a" "b"   # relevance scores, best first
```

Use `--model <selector>` to override the role-default model for `embed`/`rerank`.

## Roadmap

**Where we stand: latest milestone M1143** (the resumed hardening drive of 2026-10-27: batch 12c, SV:0850 -- M1143 (run 0186, batch 12c, SV:0850): the release-record correction before the 0.13.0 cut: the four public pages that stated a present-tense release claim (README's version line and `make ci` stamp, CHATBOT's 'latest release' clause, the install page's sample `--version` output) now state the version this tree reads, 0.13.0, true in both trees; docs only, no code, no new driver (count stays 542). -- M1142 (run 0167, batch 12b, SV:0419 -- the M1125 retry-gate fix): the retry gate no longer retries an in-stream context-overflow provider error -- M1125's `is_transient` arm (provider error at http 200) also matched M909's in-stream over-context shape (the LM Studio message is known only to `jc_text_is_context_overflow`, so `ctx_overflow` stayed 0): measured 4 model calls / 3 retries with `maxRetries` 3. The in-stream M909 reclassification now sets `ctx_overflow` when EITHER classifier matches, the gate gains `!app->ctx_overflow` (stale-flag safe: reset in the run preamble; the setting ends the run), and the M1125 engine-error cut still retries. Red-first: `stream_error` leg 7 ("not ok 7 ... 4 requests captured, 3 model_retry rows"), M1142-b leg 8 pins the jc_http-only arm (teeth probed red); `stream_error` 8/8, `stream_cut_retry` 7/7, gate 37482/0. The independent review's M1125 WRONG verdict is resolved; 0.13.0 is cut from the landing commit. -- M1140 (run 0158, batch 10b-6d, D-10): 32-bit builds get large-file support (`-D_FILE_OFFSET_BITS=64` behind a Makefile probe; a file over 2 GiB is no longer reported missing, `stat`/`fopen` used to fail EOVERFLOW) and a mid-listing `readdir` failure in `jc_list_dir` now returns `JC_ERR_IO` instead of a short listing with success (the `JICHI_FAULT_LISTDIR` probe makes the branch testable; red-first witnessed on this head: reverting the errno check fails the tooth at `test_platform.c:708`). The 64-bit `time_t` and the `long`-typed size interface stay E-09's card; the 32-bit ci stage (M1129) is green with the flag on; unit-only, no new driver (count 542), gate 37482/0 normal, 37580/0 FAULT, 37482/0 at `-m32`. -- M1139 (run 0156/0157, batch 10b-6d, D-07): `jc_session_load_by_id` and `jc_session_delete` verify the id against the same `[A-Za-z0-9_-]` gate the command line uses, BEFORE it becomes a path -- the editor protocol `session/load` used to hand the client's `sessionId` straight into `~/.jichi.d/sessions/<id>.json`, where a traversal id loaded a foreign file as a conversation (the next save would overwrite it) and a session delete could remove any file. A malformed `sessionId` now gets the protocol's refusal, with no content loaded. Red-first (lead re-witnessed after M1139-b hardened the tooth to witness the actual destruction): 3 failures (load, delete, and the fixture-removed assert), green after; gate 37482/0; unit-only, no new driver (count 542). -- M1138 (run 0155, batch 10b-6d, D-05): `jc_read_file` (and its `jc_read_file_bounded` sibling) no longer reports a procfs file as empty, with success -- on procfs the seek-to-end succeeds and `ftell` says 0 for a file that HAS bytes, so the size==0 path read 0 and returned JC_OK with an empty buffer (`/proc/cpuinfo` -> len 0). The size==0 window now probes 1 byte (procfs pseudo-files are stat-identical to a genuine empty file, so the probe, not stat, discriminates) and, when content exists, fread-loops to EOF capped at JC_READ_FILE_MAX (the sibling, `max`), ferror -> JC_ERR_IO; a genuine empty file keeps the old shape and the size>0 path is byte-identical. M770 checked: the loop runs only after a successful open, so a hardened build keeps `/proc/self/environ` refused at the open and the key stays hidden (proc_key_hidden.sh 8/8). Red-first (lead re-witnessed): 4 FAIL at test_platform.c:196/:197/:202/:208 on the unfixed tree, green after; gate 37470/0; no new driver (count 542). -- M1137 (run 0154, batch 10b-6d, C-19): the six secret-redaction sinks that failed OPEN on an allocation failure now fail CLOSED -- a shared helper `jc_redact_scrub` returns a redacted copy or a degrade, and each of the six sinks (the over-cap tool-output spill, the `--dump-requests` body, the full-tier eventlog value, the session save, the `--output jsonl` stream, and the relay handover baton) routes the degrade to its pre-existing failure arm, so a redaction that cannot allocate never writes the unredacted buffer; a `JC_FAULT_REDACT` probe makes the arms testable, and the relay baton defect (write the original length from the shorter redacted buffer) is folded in. Red-first (lead re-witnessed): the two sink swaps reverted under `make WERROR=1 FAULT=1 test` -> 3 failures (test_session.c:320, test_eventlog.c:426), green after; plain 37458/0, FAULT 37547/0, redact_sinks 9/9; no new driver (count 542). -- M1136 (run 0153, batch 10b-6d, C-18): the raw-string-call lint now covers CLAUDE.md's full list (sprintf, strcpy, strcat, gets) instead of sprintf only -- its pattern could never match a `strcpy`, although the rule names all four. The 71 strcpy/strcat sites in the universe (src 17 incl. the 2 cJSON-pair, tests+tools 54; bench stays data) were audited site-by-site (each bounded by an explicit check) and allowlisted by EXACT line beside the 4 cJSON sprintf lines, and a new check 3 pins completeness (every allowlist entry matches a live raw match this run; raw floor 75), so a moved, removed or newly unbounded site turns the driver red. Red-first: a probe `strcpy` in src went `not ok 2`, removed; gate 37448/0, driver 3/3; no source change, no new driver (count 542). -- M1135 (run 0152, batch 10b-6d, C-17): the rerank parser's out-of-range `index` is a dropped row, pinned by a unit test and the teeth witnessed under UBSan -- the finding's `(int)idx_node->valuedouble` at `jc_rerank.c:71` was already converted to `jc_json_clamp_int` by A-02's M1077 sweep (the mechanism lint-pinned by `json_cast_lint.sh`, MAX_HITS=0); the remaining gap was the behaviour (the embed sibling had pinned it at M725, the rerank parser had not), so the new `test_rerank_parse_out_of_range_index` feeds `"index":1e300` and asserts the row is dropped (s[1]==0.0) while the in-range rows land, and the lead re-witnesses that reverting the one line makes `make SAN=1 test` (float-cast-overflow, recover off) print `jc_rerank.c:71:55: runtime error: 1e+300 is outside the range of representable values of type 'int'` and abort the suite. Gate 37448/0; no new driver.; before it, M1134 (run 0151): the pid-scoped HOME fallback directory is CHECKED like its uid-scoped sibling, and a fresh `mkdtemp` directory is the last resort -- `jc_home_dir`'s third step mkdir'd and used `/tmp/jichi-<uid>-<pid>` without the lstat/owner/mode check its sibling got, on the false comment "a pid-scoped name cannot have been waiting for us" (pids are predictable, and a hostile local user can pre-create them). The four-predicate check is now one helper applied to BOTH candidates; a planted pid path falls back to a fresh 0700 `mkdtemp` dir (no TOCTOU), and if even that fails it degrades loudly, never NULL, never exit (M472's contract). The deterministic probe `JICHI_FAULT_HOME_NOPASSWD` (a compile-time 0 in a release build) makes the branch testable on a host with a passwd entry; `mkdtemp` needed a per-TU `_XOPEN_SOURCE 700` guard (POSIX-2008; measured). Red-first: a FAULT-gated unit test in `test_platform.c` + the new `state_root_pid_fallback` driver (3 checks, plants the child's EXACT pid dir via `sh -c` + `exec`), driver count 541 -> 542, gate 37514/0 (FAULT) / 37444/0 (normal), `state_root` 4/4 (M472 unregressed); before it, M1133 (run 0150): `jichi --version` and `doctor` now report what the LINKED libcurl can do
 -- its version, TLS backend and default CA path (one `curl_version_info` + `CURLINFO_CAINFO` snapshot, the doctor's ok label stays byte-identical to M472's and gains a detail line); `doctor` FAILs when a model's `https://` endpoint meets a library without a TLS backend (the deterministic no-TLS probe `JICHI_FAULT_NO_TLS` makes that arm reachable on this TLS host, the https-meets-no-TLS verdict cannot false-positive on a release build); and the one missing CA knob -- portability recommendation #13 -- is now a config key `caBundle` that sets `CURLOPT_CAINFO` (PEER/HOST verification stay on: a named bundle repoints the trust anchors, it never loosens a check), and the doctor FAILs a `caBundle` path that is not an existing regular file so a typo no longer passes the report and dies at the first https transfer; red-first `libcurl_capability` 5/5, driver count 540 -> 541; before it, M1132 (run 0149): the doctor's plaintext-transport check (M472) exempts LOOPBACK only (the new pure jc_net_host_is_loopback classifier, not the SSRF guard's "loopback or private" -- a private-range apiBase used to pass in silence) and now checks every endpoint a credential can travel over http:// -- model apiBases, MCP server urls and the search url (red-first transport_posture extended 4 -> 8, gate 37435/0); before it, M1131 (run 0148): a registered secret that straddles the log line's 4095-byte cut no longer leaks its prefix -- jc_logf's secrets path redacts the FULL text and the five network clients cap the error body they log at 2048; before it, M1130 (run 0147): the three config-held credential slots (an MCP credential-marked header value, an MCP stdio `env` value, a user-tool `env` value) join the redaction registry at the startup key block, so a model-issued `cat config.json` no longer prints them unredacted into the session, telemetry and `--output jsonl` stream (red-first `config_credentials`, driver count 539 -> 540; M1130-b folds the whole `Bearer ` prefix, not just its first letter); before it, batch 11 "Phase 2 findings" -- M1129 (run 0142, the RC's final row per SV:1043/1057): `make ci` gains two stages at the END of the gate -- the optimised fortified release (RELEASE=1, -Werror, both binaries + the unit suite) and the 32-bit build (gcc -m32, curl-free; a SKIP line names why where multilib is absent) -- whose red-first exposed the two RC build defects: three harness warnings visible only at fortify+optimised (the redundant `realpath` declaration, the ignored `write` return in the drain loop -- fixed by the `_XOPEN_SOURCE 600` guard and a write-return capture, the `jc_path.c`/`jc_term.c` precedents) and two unit-test literals that overflow a 32-bit `long` (fixed by width-aware preprocessor test constants, the 64-bit arms byte-identical); driver count unchanged. Before it, M1128 (R12-N2, run 0141, the fourth RC row per the SV:1044 assignment): the topology-B relay's task no longer dangles on the per-turn scratch arena -- `relay_task` is copied onto the session arena (one line), so every in-process leg reseeds from the operator's prompt verbatim instead of reset-and-reused bytes (leg 2's and leg 3's first user message was the dangling pointer's byte, and the batons' "## 1. The task" sections carried garbage), pinned by `relay_topo_b.sh` check 5 (extracted and prefix-tested, not body-grep'd -- the baton's fenced notes carry leg 1's task text anyway); driver count 538 unchanged (extended in place). Before it, M1127 (R01-N2, run 0140, the third RC row per the SV:1044 assignment): the M919 stranded-call loop no longer reads the assistant message through a pointer its own appends invalidate -- the `am` re-fetch moves inside the loop -- a heap use-after-free under ASan on the n2 shape (three stranded calls; the 2nd append reallocs the history vector), pinned by the new SAN-only `stranded_call_uaf.sh` driver; driver count 537 -> 538. Before it, M1126 (R01-N1, run 0139, the second RC row per the SV:1044 assignment): the relay account call's leaf fallback (the labelled quote of the last real answer when the handover note's model call is refused) no longer writes through a garbage pointer -- one line, (*out)[b.len] instead of *out[b.len]; red-first relay_account.sh check 5 (the n1 shape, rc 139 and no baton on the unfixed tree); driver count 537 unchanged. Before it, M1124 (C-08, run 0138): the redirect private-address guard no longer false-refuses a legitimate feed -- M1120's per-socket count read a feed's own same-host:port 302 and a dual-stack name (dead ::1 then the v4 address) as "a second connection to a private address"; it is now hop-aware (a 3xx status line is a hop; every (family, address, port) opened at hop 0 is allowed again, a NEW private address is refused -- the port in the key keeps the M1120 fence refused; the one accepted limitation, SV:1437: a redirect to another port on the same private host, e.g. http 80 -> https 443, is refused -- name the final URL); driver check 6 by effect (the self-redirecting loopback feed's body reaches the model; red-first on the M1120 counter) + a unit test on the extracted decision; the RC's first fix (SV:1043); before it, M1123 (C-11): the SSRF address classifier now covers the reserved IPv6 ranges it missed -- NAT64 (well-known + RFC 8215 local-use) and 6to4 classified by their embedded IPv4 address, the deprecated IPv4-compatible form by its embedded v4, Teredo refused by its 2001:0000::/32 prefix (the audit's Finding 1 HIGH: the client field is XOR-obfuscated, so classifying raw bytes fails open), site-local and multicast refused outright (M1123-f2, SV:1117: the Teredo test is the real /32 -- it matched /16 and would have refused every 2001:xx host; negatives 2001:4860:4860::8888 / 2001:db8::1 / 2001:1::1 stay allowed, the /32 edge addresses stay refused); unit-only, no driver; gate 37329/0); M1122, the **0.13.0 RELEASE CANDIDATE** (SV:0833 -- the snapshot of 2026-11-04 is cut from the RC head that carries the M1124 C-08 false-refusal fix, moved by SV:1043/SV:1044): the version bump cascaded through include/jc_version.h (0.12.0 -> 0.13.0, the eighth release), the CHANGELOG [Unreleased] retitled to [0.13.0] (fresh [Unreleased] above, no entry moved), the two sample outputs that print the version (DAEMON.md's hello.ok line, INTERFACE_TUTORIAL.md's contract header), and the PROJECT_TIMELINE release re-count (M1122, 3,150 commits, 138 days / 101 active, 367/143 source/unit files, 537 drivers, ~153,100 / ~161,300 / ~225,300 lines, ~540,000 authored) + the ja mirror -- red-first on i18n_tracks_lint check 5 (the ja page carried the M1038-era English figures M1121-c moved out of the English page, declared=8); the version-pinning smoke drivers read the version from the binary, not a hardcoded literal, so the bump reds none of them; the historical 0.12.0 references stay untouched; BATCH 10b-6d READY at this head; M1121: the multipart upload's file payload is now checked against its boundary (a payload containing `--<boundary>` regenerates the boundary, capped at 5, payload unmodified) so it cannot split the part and inject a fake one (RFC 7578), and `doctor` now reports WHICH entropy source the identifiers use so the time-seeded `rand()` fallback is no longer silent (C-10; the finding's prefer-`getrandom` bullet deferred to platform work; pinned by the new `test_multipart_payload_boundary` unit test + `multipart_boundary.sh` driver, driver count 536 -> 537; M1121-f1 closes the review's two non-blocking comment/driver findings); M1120: the `@rss:` feed fetch and the docs URL fetch now guard their REDIRECT hops against private addresses while still allowing the user-named first hop -- a feed/doc host the user named can no longer be redirected into an address inside the machine or network (a 302 to link-local/cloud-metadata is refused at connect), while a legitimate internal feed on the first hop still works (C-08; the finding's doc-only half rejected -- fences on; a blanket `block_private_addrs` and a same-origin rule both rejected because they would refuse the first hop / block the normal local-feed case; the new `block_redirect_private` flag's connect-time guard allows hop 1 and refuses every later one, the M1026/M1027 authority triple keeping the hop count honest; pinned by the new `feed_redirect_guard.sh` driver, 5 checks, driver count 535 -> 536; M1120-f1 closes the review's two source findings -- the driver's M465-shape env prefix, and the header comment no longer promising both guards); before it, M1119: the image tool's fetch of a server-named reply URL carries the connect-time private-address guard (`fetch_url`'s standard), so a reply the image server (or anyone able to alter its replies) names can no longer point jichi at an address inside the machine or network, and a refused reply writes no file -- the configured POST leg is the operator's trusted endpoint and stays unguarded (C-07; the cap half was already closed by M1092/M1093; pinned by the new `imagegen_url_guard.sh` driver, 5 checks, driver count 534 -> 535; M1119-f1 closes the qwen27b review's two one-line doc findings); M1116-d: the no-fchdir FAULT build compiles again -- M1116-c's test-only `gone_st` is declared only where it is used (SV:0152), M1116-d-f1: the M1116-c fault-site comment's "suite is green" clause, now false, says what actually happens; before it, M1118: the over-cap tool-output spill (M339) is written through the secret scrub, into an owner-only directory, so a registered secret a command prints is no longer readable from `~/.jichi.d/tool-output/` by another user of the machine (C-04; pinned by the new `toolout_private.sh` driver, 4 checks, driver count 533 -> 534; M1118-b clears the make-ci gate red on the 10b-6c wave head -- the two docs drivers no longer carry the binary skip-guard and are exempt from smoke_lint check 5 by name, so they stop staying green against a binary that does nothing, SV:1737); before it, M1117: docs/HOOKS.md now states the depth-0 hook rule's consequence for delegated writes -- a hook never fires for a `spawn_subagent` child (in-process, depth 1) or a `spawn_parallel` write child (depth 1), so a project believing "every write is gated" is wrong for delegated writes (the per-tool fence covers them; the hook is the operator's top-level veto), pinned by the new `hooks_docs.sh` driver (3 checks; driver count 532 -> 533; the env-var half of B-10 was already closed at 267189de before this drive); before it, M1116: the posix_spawn backend restores jichi's own working directory through a HELD DIRECTORY HANDLE (an `open(".")` descriptor + `fchdir`, the `JC_HAVE_FCHDIR` link probe) instead of re-chdir'ing the saved path, so the original directory vanishing mid-call can no longer leave jichi running in a silently-moved cwd (B-09; the path-based chdir + WARN stays the honest fallback where fchdir is unavailable, and `make info`/docs/BUILD.md now say which restore a build took, SV:1057); before it, M1115: the per-workspace lease's degradation is documented by class in SECURITY.md and docs/HARDENING.md 7.6 (B-07: where the filesystem's file locking is unavailable, or the holder keeps the lock past its wait, `--lease fail` is advisory; the no-locking case logs only at debug level), pinned by the new driver `lease_advisory_docs.sh` (2 checks; driver count 531 -> 532); before it, M1114: a non-regular config path (a FIFO, device or socket) is now refused BEFORE the open -- a FIFO at `local/config.json` or `~/.jichi` no longer blocks start-up (it held the open until a writer arrived; a checked-in symlink to a device could carry it from a clone), a following-stat guard names each shape (a directory keeps its M198 message, M1114-b) and the run falls back; before it, M1113: the JSON object accessor returns the LAST matching member (last-write-wins), so a peer object carrying a duplicate key (`{"x":1,"x":2}`) reads its last value instead of the first (A-13, the one product change of the 10b-6b wave -- the minimal-altitude accessor fix; parse-time dedup rejected as an O(n^2) parse + tree/iteration change for a LOW finding; the three SV rows close in the same window: M1106-b the `--from-global` inherit strip extends to `mcpServers[].env` / `.headers` + `tools[].env`, M1108-b the D-33 WARN's false "(FIFO/device/socket)" parenthetical for a directory, M1110-d search_option_path leg 5's vacuous found-key; gate `make WERROR=1 test` 37304/0); before it, jc_testparse no longer overflows on a test runner's attacker-sized number: the four unguarded digit accumulations (a line number, the TAP plan, "N failed/passed/tests", "out of") cap at INT_MAX through one read_num_capped helper (M1112, A-12; the finding's O(n*m) clause does not reproduce -- linear, recorded with benchmarks), and M1112-b checks the two downstream sums the cap feeds; the SV follow-ups M1107-d (the M1107 test's two arena frees moved after their checks -- make ci's -O2/ASan UAF) and M1107-e (config set's .bak is now written through the atomic temp+rename writer -- a planted symlink/hardlink/FIFO at the .bak is replaced, not followed/opened) landed in the same window; before it, M1111: jc_group_num clamps its staged number length to what was written (A-11; latent, a no-op for the 40-byte current callers -- the five 10b mediums are 10b-5 READY at d294b8da, full smoke green 531/3187); before it, search_code no longer hangs when its path is a present-but-unsearchable file (a FIFO, device or socket): the guard lstats the path after the dash check and refuses it with a tool error instead of letting grep block in open(), so a model pointing the tool at a named pipe gets an error it can read (M1110, D-36; M1110-b: the guard stats (follows) instead of lstats, so a symlink to a regular file searches again, and the sp[1024] copy is gone); before it, a prompt-asset file is no longer read whole onto the session arena before its cap is cut: the rules, glossary and command @-reference read chokes read at most the asset's cap (head or the combined tail) through a new bounded read, so a planted 60 MB file no longer sits resident for the process lifetime (M1109, D-35); before it, a FIFO (or other non-regular file) planted in a prompt-asset position no longer hangs start-up: the rules and glossary read chokes skip it with one WARN before the open can block, covering the global rules, the explicit config `instructions`, and the project/global glossary (M1108, D-33); before it, `config set`'s write is safe: a symlinked config is refused (the name and its target untouched), the pre-edit original is backed up by COPY carrying the original's mode, and the replacement is written temp-first with the original mode, so a failed save leaves the config in place byte- and mode-identical (M1107, D-25, with M1107-b the .bak mode and M1107-c no partial .bak); before it, `setup --from-global` no longer copies a literal `apiKey` from the global config into the project config: when inheriting, the exact member `apiKey` is dropped from every model and from `search` (an `apiKeyEnv` name is kept) and a one-time warning names the affected model, so a secret no longer moves into a 0644 project file (M1106, D-24); before it, init/setup and setup --import no longer write through a symlink planted in the project: one guard resolves the leaf parent under the project root and lstats the leaf, refusing a symlink leaf even under --force and a symlinked parent, while a regular file is still force-overwritten (M1105, D-23); before it, jichi's own workspace files (.jichi/memory.md, the rules file) are rewritten through a mode-preserving atomic replace, so a failed write no longer loses the pre-existing content (M1104, D-18); before it, the control channel answers mid in-process subagent and `--daemon` refuses a control socket (M1103/M1103-b); before it, `learn apply`'s rules refusal names the real file (M1102); before it, one lease record per holder (M1101); and M1100 -- a present-but-unreadable .jichi/memory.md or rules.md (over the 64 MiB read cap) is now refused instead of silently replaced: the new jc_read_existing tri-state means the remember and learn writers proceed only on a genuine does-not-exist, and the remember tool returns the refusal to the model (M1100, card 132 / D-16, with M1100-b the helper's lstat-the-leaf nit); before it, the parallel merge no longer follows symlinks at either end (M1099, card 131 / D-12, with M1099-c the SV:1144 residuals); before it, batch 10 -- the batch-10 full-smoke gate reds are cleared: the batch-10 drivers are POSIX (`http_resp_cap`'s GNU-only `head -c`, `side_call_fences_lint`'s `grep --include` + the SV:2138 `/dev/null` operand) and the M1096 state-root creation no longer runs ahead of the legacy-migration advice, so an old-only install is still told the safe plain `mv` (M1098); the state root is owner-only, the checkpoints and worktrees under it unreachable on a traversable home, and doctor names a permissive pre-existing root (M1096, D-06); the out-of-scope fence no longer misfires for a C-quoted path and no longer records a revert that did not happen: the edit-scope sweep's changed-since list goes NUL-separated raw (`-z`) and every consumer splits on NUL, pinned for the non-ASCII and the quote/backslash/tab/newline name shapes (M1095, D-03); a node-capped SSE event no longer vanishes silently: the parser's per-parse cap (M1089) now tells the provider the refusal, and a dropped event marks the streamed reply truncated and logs a line (M1094, the G3 residual of the supervisor's M1089 effect-check); every side (non-model) HTTP call now carries the connect and dead-stream fences with its total named per call class and a lint holds it (M1093), a default 32 MiB ceiling bounds every collected jc_http response and the audio/image caps ride it (M1092), the JSON parser bounds the node count of one parse at 1,000,000, a large-but-valid reply no longer expands ~40x its bytes into unbounded memory (M1089), streamed provider text and per-tool-call args are capped with the cut marked truncated (M1090), and the privileged-launcher detector skips a wrapper's own option tokens and matches a launcher by its final path component (M1091); before it, batch 9, the resources findings -- the repo map's build arena peaks at one scanned file (M1081), a failed session save now says so (M1082), a SIGKILL mid-turn loses at most the in-flight round (M1083), the request-path printer and UTF-8 sanitiser get byte-identical fast paths (M1084), cJSON appends in O(1) via a cached tail (M1085), and the telemetry readers are honest past the 64 MiB slurp cap (M1086); before it, batch 8, the numbers-and-casts findings -- the JSON printer is exact (M1076), the 117 out-of-range casts of untrusted JSON clamp (M1077), cJSON_Parse refuses trailing content (M1078), the C89 snprintf fallback consumes the right va_arg (M1079), and the harness --help BUILT marks are pinned (M1080); before it, a documented static build is no longer silently not-PIE: `STATIC=1` is the named static mode (probes -static-pie, falls back to -static with -pie AND -fPIE dropped, says which in `make info`) M1075); before it, the Makefile's project flags stop shadowing the user's CFLAGS/CPPFLAGS/LDFLAGS (M1074, card 105 / E-06; the user-flag position and the D1 probe/fallback exclusion are pinned, M1074-b); before it, the install path points at a named release build, RELEASE=1, -O2 at the probed fortify level (M1073, card 104 / L-01); before it, the push size check's biggest-file version now reads the total's one-decimal number (M1072, card 99 / SV:1419); before it, _FORTIFY_SOURCE is keyed on an actual optimisation level, its level probed 3-then-2 (M1071, E-05), and jichi's own printf formatters are checked by -Wformat (M1070, E-04); before it, the constraint scanner no longer infers a blanket rule from prose that is a path scope, a named file object, or a description of a subject's habits (M1069, card 77, the run-0068/0069 prose misreadings -- a deontic or a reporting verb before "never" is still an order, measured over 30 briefs and a 669-message corpus); before it, portability_lint check 5 refuses a libcurl guard token that is not exactly 6 hex digits instead of decoding it to a fake version (M1068, the 0x073B00 -> 7.3.0 misread, card 98); a dead-stream fence on a REUSED connection no longer reads as a connect timeout (M1062), a cold resolve timeout no longer reads as a reused stream death (M1062-c), and the hang shape is a FAULT=1 tooth that runs where make ci runs (M1062-e); before them M1063 (a late inbox note is delivered, not consumed undelivered), M1061 (the credential-helper answers real git's identity), M1060 (the dirty-dispatch absence checks grep the event form), M1059 (the jichi-harness drive of 2026-10-23, batch H5: `jichi-harness drive`, the **outer loop**, the C89 port of the drive's `loop.sh` -- until the operator stops it or the config's `drive.end` is reached; the run is a child the loop polls every 10 s, the deadline mid-run sends the run's group a SIGINT -- the way Ctrl-C stops it -- and never starts the next run, the three-quick-failure pause slept in 10-s slices, the records snapshot, the `drive.teamPush` switch, and the Mode A records-push no-op (def-360) -- **P2 is now complete**, every P2 command built; red-first driver `harness_drive` (11 cells), qwen27b-reviewed; before it, M1058 (the jichi-harness drive of 2026-10-23, batch H4: `jichi-harness run`'s **handoff body**, the C89 port of the drive's `handoff.py` -- before the spawn, the run builds the `## What the previous run left you` section of the prompt from the PREVIOUS run's own records (its `meta.txt`: how it ended -- `rc`, the minutes, the `team_head_after` commit; its `stream.jsonl`: the model's last message, its todo list as last written, the files it read or changed, its `spawn_parallel` batches, the manager's notes it was given -- at most four, the rest counted) and keeps it as `runs/N/handoff.md` (empty when there is no section); every line capped, the whole section cut at 6000 bytes with a marker; it FAILS OPEN (a previous run with missing or unreadable records starts the next without the section, exit class unchanged, the reason filed to `runs/N/handoff.err`); port differences named: the run writes `team_head_after` itself (the reference's loop did), the notes' "full text is in" path is the Mode A records dir, `apply_patch` edits are counted per `edits[].path` (the reference never counts one), the batches pair with their `tool_result` BY ID, a `.gz` note is `(unreadable)` by name (def-358); the SAN stage now runs every `tests/smoke/harness_*.sh` driver (SV:1158/SV:1200: the H0 gap had run only `--version`, missing team_head's `argv[14]` index-out-of-bounds and two `closedir`-before-`refuse(e->d_name)` heap-use-after-free, all fixed) -- red-first driver `harness_run` (17 cells), qwen27b-reviewed; before it, M1057 (the jichi-harness drive of 2026-10-23, batch H4: `jichi-harness run --config <f> --tenant <n> --run <N>`, one lead run in Mode A -- the C89 port of the drive's `run-lead.sh` with the container half dropped (Mode A, D4) and the record and key-delivery half kept: the model key is read from the file the configuration names at run start and handed to jichi through the spawn's env vector under the fixed `JICHI_API_KEY`, never on argv, a URL, a log or a record; caps off (no `--deadline`, no `--budget-tokens`), fences on (`--max-tool-calls`, `--verify-timeout`); stdin at /dev/null (a blocked spawn would hang); the journal, telemetry, stream, stderr, prompt and meta all land in `<dir>/runs/N/`, OUTSIDE the workspace; a `drive.leadModel` the tenant's config does not name warns on stderr and omits `--model` (the cost fence against a raw id reaching the gateway as a priced model); the supervisor's notes are moved into the run's records only after its first model call succeeds -- red-first driver `harness_run` (11 cells), def-355/356/357 deferred); before it, M1056 (batch H3c: `jichi-harness mirror push` + the C `credential-helper` subcommand -- the C89 port of `mirror.sh`'s push: the five prechecks git-free, the URL guard (`get-url --push` vs the stored-config-copy `url` byte-exact, the first git child), the C key scan + size check (both secrets read from the files at the moment of use, fail-closed, the blobs the push adds -- the remote's current tips from `git ls-remote <stored URL>` because a URL push leaves the mirror's `refs/remotes/origin/*` stale, the 8 MiB per file-version / 20 MiB per-push limits, a `.gz`'s decompressed content is def-341), and the push by the stored absolute URL (the M2 set, `core.hooksPath=/dev/null` -- the guards are C logic, not hooks -- the `credential.helper=` reset then the `<ABS> credential-helper` two-word spec, `credential.useHttpPath=true`, no `--force`, no `refs/kept/*`); the credential-helper answers `get` with the token read at the moment git asks and REFUSES a `get` whose stdin protocol/host/path identity is not the configured remote URL (the race-free layer: a URL push is check-then-use, a same-uid process can plant between the guard's check and the push) -- red-first driver `harness_mirror_push` (26 cells), the driver count re-measured 495→496 with the Japanese count page carried in the same commit (the i18n carry), def-341/342/343/344/345/346/347/348 deferred, def-338/340 restored, qwen27b review APPROVED-WITH-FINDINGS (F1/F2/F5 fixed, their own cells P2c/P11g/P11h/P4e); before it, M1055 (batch H3b: `jichi-harness mirror fetch` + `mirror head`, the C89 port of `mirror.sh`'s fetch (the push is H3c, built at M1056) -- the mirror is the FETCH CLIENT: `git -C <mirror> fetch <the team clone>`, the refset `<prefix>/main` + `<prefix>/work/*`, no `+`, no prune, `--no-tags`; a non-fast-forward rejection of one refspec is a report line, not a failure; the kept-attempt checkpoint loop runs even after a team-fetch transport failure (the script's `|| log` order); the five integrity prechecks run BEFORE any git call in the mirror (bare git-free via `HEAD`, ownership, config byte-identical to the stored copy `init` now writes, `hooks/` nothing but git's `*.sample`, no `objects/info/alternates` -- the measured `core.alternateRefsCommand` vector); every harness git child carries the M2 set + `GIT_CONFIG_GLOBAL`/`SYSTEM` at empty files + `-c core.alternateRefsCommand=`, and the child's env is the DENY-LIST scrub, measured by a git-wrapper W8-env driver cell -- red-first driver `harness_mirror_fetch` (13 cells), the driver count re-measured to 495, def-339 + def-340 deferred, qwen27b-reviewed); before it, M1054 (batch H3a: the design note M1055 builds from -- how Mode A reads the agent's clone WITHOUT running git in it: the M2 read set is a deny-list, the measured `core.alternateRefsCommand` + `objects/info/alternates` vector, the serve-side `GIT_CONFIG_GLOBAL`/`SYSTEM` empty-file isolation, and the five mirror-integrity checks; note only, no code); before it, M1053 (the jichi-harness drive of 2026-10-23, batch H2: `jichi-harness init --config <file> <drive|rehearsal> DIR`, the C89 port of the drive's `setup-dir.sh` -- the first harness command that runs git, every step through the M2-neutralised spawn with the secret-env scrub; it lays out the plain directories, the pinned `bin/jichi` (mode 755, reported by its `build:` stamp), a drive's `key.env` carrying the key-file REFERENCE line (mode 600, never a copy of the key) or a rehearsal's random canaries and local bare `standin.git` (a sink with no origin, never GitLab), `team/` on `<prefix>/main` with a dead origin and no `*.sample` hooks, `mirror.git` (the initial fetch REPORTED, not fatal, credential helper cleared), and `recs/` on an orphan `<prefix>/records`; IDEMPOTENT -- a part that exists is "left as it is" and init never deletes; a usage refusal exits 2 before any part is written -- deliberately CHANGED away from the script: no `harness/` archive, no container rewrite (def-336, Mode D), no pre-push hooks installed (def-337, H3); red-first driver `harness_init`, qwen27b-reviewed); before it, M1052 (the jichi-harness drive of 2026-10-23, batch H1 (H1b): `jichi-harness doctor`, the offline report on the drive's own configuration -- the M1051 loader's verdict plus six checks in jichi's doctor shape (the `%d ok, %d warning(s), %d problem(s)` line, exit 1 on a problem, the `--output json` form); `--config` is a global option, a broken or missing config is a PROBLEM (exit 1) not the exit-2 not-built class; doctor never opens a secret file or prints its contents, and an absent reference is reported by setting name; the D13 settings table lands in `docs/HARNESS.md` and is linted from the loader header; `doctor --live` is def-335, deferred to H4 -- red-first driver `harness_doctor`, qwen27b-reviewed); before it, M1051 (the jichi-harness drive of 2026-10-23, batch H1 (H1a): the harness's JSON configuration file and its C89 loader -- a credential is a reference, never a value; the six server kinds fix the user-name rule; the JICHI_HARNESS_* env settings override their config twins; a malformed config is a value, never a crash; red-first unit + per-guard teeth); before it, M1050 (the jichi-harness drive of 2026-10-23, batch H0: M1049 makes `jichi-harness` jichi's third C89 program -- `make`/`all`/`smoke` build it beside `jichi` and `jichi-convert`, `--version`/`--help` print, and every not-yet-built command refuses with exit 2, never a silent success -- and M1050 is its learner page `docs/HARNESS.md`, D4's limit on the first screen; red-first driver `harness_third_program`); before it, M1048 (batch G18, finding G18, HIGH: a "verify green" claim now comes from a check that ran -- a run that took no pre-edit checkpoint used to never execute its completion verifier yet still reported "checked: verify green" / "[envelope] verified ok" with exit 0; the completion and `--verify-every` verifiers now run whenever armed and the run completes, a "did nothing" run with a failing check ends red, the footer says "ok, not verified (verifier did not run)" for an armed stop-first finish, the `end` journal record gains `verified: true|false|"not_run"`, and `doctor` warns when a verifier meets snapshots off; red-first `verify_without_snapshot` + `capped_turn_says_so`); before it, M1047 (batch 14, item (e): liveness at every depth -- a heartbeat row at least every `--heartbeat` interval while the lead blocks on `spawn_parallel` (the batch's select loop now ticks a heartbeat naming its live children) and on an in-process `spawn_subagent` (headless forwards a minimal on_progress-only callback into the subagent's model calls, so the stream stays alive without leaking subagent content; M1047-b gates that forward to the headless jsonl heartbeat path so ACP and quiet TUI are unchanged); red-first driver `parallel_liveness`, qwen27b-reviewed); before it, M1046 (batch 14, item (d) residual X2: `control.sh` run 7 pins the control channel's fold-reset of the inject note count -- a two-note inject folded at a tool boundary reads `queued_injects=0` while `injected` stays 2; M1006 and M1006-a, the dirty-tree dispatch guard and its three refinements, land with this entry; before it, the control `status` reports the inject delivery state, `injected` + `queued_injects`, both always emitted, so a supervisor can tell whether a queued note is still pending rather than trusting the ack; M1045-b counts queued notes, not the pending buffer's lines -- a multi-line note no longer reads as N; M1044 (def-332) lands with this entry -- a `spawn_parallel` worker's root report is kept out of the file-level merge -- stored redacted with the run's records and named in the result's `## Reports` section, so a report-only change no longer drops the second writer's evidence or forces a spurious parallel-conflict Kept state; M1044-b sizes the report's redaction buffer from the input (the 8 KB cut on the redaction-active path is gone) and pins the redaction itself, red-first `parallel_reports`; before it, **M1043** (batch 14, def-329's `secretEnv` register + the `-C` workdir flag): a config `"secretEnv"` name is removed from EVERY child jichi forks -- the model's shell, hooks, MCP servers and the verify command -- on top of the built-in scrub list, which gains the four usual CI/forge names (`CI_JOB_TOKEN`, `GITLAB_TOKEN`, `GITHUB_TOKEN`, `GH_TOKEN`), and `jichi doctor` warns for a credential-looking variable in the environment that is in neither list; the class M894 measured -- "a credential under another name reaches every child" -- is closed for named credentials, red-first (`secret_env_register`); before it, **M1042** (batch 13, the M1039 residual): the `fail_class` vocabulary now has ONE writer -- every `req.fail_class` write goes through the classifier, and a smoke lint pins that, so the classes are reached at runtime and the vocabulary cannot drift between writers; before it, **M1041** (a shell command's own output is no longer filed as a policy refusal -- the classifier takes the tool name and shell results are DENIED only on jichi's own refusal wording; M1041-b adds the executor's refusal flag so a read-only run's own "tool disabled" refusal is a policy refusal again) and **M1040** (a registered key cut at the 2047-byte error-body capture boundary is redacted before it reaches the error note, the telemetry, or the log); before them, **M1039** (batch 13, MO-2): the failed `model_call` telemetry row gains a stable `fail_class` (connect / timeout-connect / timeout-tls / timeout-stall / dns / refused / http-status / provider-error / aborted), classified at the `jc_http` layer from the same structured inputs that decide the free-text `transport` prose, so an outage is greppable by one token; before it, **M1038** (batch 13, MO-1, the HRZ-outage resilience: a model call that fails at **connect** -- no HTTP status -- now **re-probes** the endpoint and, still down, **walks the active model's fallback chain** and routes the turn to a reachable fallback; a 4xx/5xx is a reachable server and keeps retrying in place; no fallback configured = no probes, byte-identical to today; landed at M1038/M1038b, supervisor-verified; **M1037** (the last two batch-6 residuals: M1036 closes the 512-copy-boundary leak -- a provider error body's `last_model_error` now redacts the FULL body window before the 120-byte cut, so a registered secret long enough to straddle the old 512-byte copy no longer leaks its near half into a child agent's report, red-first; M1037 records the empty-proxy `CURLOPT_PROXY ""` = go-direct semantics as measured-not-toothed -- the container's only non-loopback address is blocked by the guard's own classifier, so no in-process tooth is achievable offline. Before them, the three batch-6 security teeth: M1034 pins the LSP scrub's config-registration leg with a non-built-in key name (`lsp_env_scrub` check 5); M1033 pins the doctor proxy var list with an HTTPS_PROXY-only leg (`fetch_ssrf` check 10); M1029 makes the pre-fix relay baton overflow visible to ASan (a 2056-byte note whose 179-byte tail shift writes 9 bytes past the 2048-byte cap); previously M1032, C-02, the proxy and the SSRF guard: `jichi doctor` now strips a proxy value's credentials for every scheme libcurl accepts -- scheme-less, `socks5://`, not only `http://` -- and lists the exact variables libcurl reads, red-first drivers; M1030, C-03's residual, pins the error-note redaction at the altitude where the leak was actually reachable -- a secret straddling the 120-byte cut no longer leaks its near half into a child agent's report; M1026, C-01: a guarded `fetch_url` no longer rides the provider's pooled keep-alive connection, so the connect-time private-address check can no longer be skipped by reuse; previously M1021, the D-15 review-4 residuals: the state-file guard now covers the leaf, not only `.jichi` -- the three progress appenders lstat the leaf before they `fopen` it, so a symlinked `progress.jsonl` can no longer carry a line outside the workspace, and a symlinked `.jichi` no longer spawns an outside `board.lock`; M996, the L-27 drain follow-up, made the reap-time spool rewrite lossless -- a short write keeps the original spool and the file is never NUL-padded -- and the gate compiles at -O2 fortified; M995, B-02's design note, is the M2 "last piece" for the operator; before them, the 2026-10-17 hardening drive: a command that closes its output no longer escapes its per-call timeout or an operator interrupt -- both the run-tool and the capture helper's post-EOF waits are bounded and interrupt-aware, through one shared checked reap helper that says "unknown" on ECHILD, never a decoded zero (M989, B-01+B-06); a terminal hang-up (SIGHUP) is handled like SIGTERM -- graceful abort, teardown, session saved, exit 129 (M988, E-03); a supervisor that ignores SIGCHLD can no longer make a failing `--verify` gate or a watched shell command read as passing -- jichi resets the inherited signal state at start-up and reaps through a checked helper that says "unknown", never a decoded zero (M987, E-01); the --edit-scope fence and the executor now share one argument normalisation, so a ..-spelled path and a self-named or stringified wrapper no longer bypass the scope (M981, B-04+A-01); search_code refuses a dash-spelled path (M982, D-31) and git_branch a leading-dash name (M983, B-03); previously a background read's on-disk spool cap now binds at reap only, so the rewrite can no longer race a running child and lose its last lines (M984, L-27), on top of run_terminal_command's `timeout` description no longer inviting a time limit on a run that measures or reproduces something (M976, L-25), a lead agent that supervises worker batches now gets a scoped edit check — its own edits to worker-owned paths are refused from the Nth and shell changes to them are reported (M975, D6), on top of a background read at the output cap's edge no longer delivers its last lines twice (M963, L-20), and a constraint inferred from your brief stays inside the clause it came from (M966, L-21 with L-19) — all red-first and review-approved, on top of the M3 "background read keeps the tail" feature recorded at M960; previously M980, the data-loss findings; M959, the 30 s stall default's DECISIONS row): the engineering loop is healthy; the
**first public release shipped 2026-08-27**: **v0.9.0**, one curated commit,
published to the HRZ GitLab (`jichi-public/jichi`) and to GitHub, tag `v0.9.0`
on both ([`docs/plans/2026-08-public-snapshot.md`](docs/plans/2026-08-public-snapshot.md),
executed as written); the public tree was **advanced to the M639 state on
2026-09-17** (public commit `0790755`, hosted CI green on the first run; no new tag,
so it still read 0.9.0), and then **to the M655 state on 2026-09-17/18** — public
commit `573ccce` = private `a64cc4b3`, 1,968 files compared byte for byte with 0
differing, **tag `v0.9.1`** on both remotes, and both the branch push and the tag
push green on the hosted runner, and then **to the M668 state on 2026-09-19** — public commit
`3e6a242` = private `f8ead53a`, **1,987 files compared byte for byte with 0
differing**, **tag `v0.9.2`** on both remotes, and both the branch push and the
tag push green on the hosted runner. The public tree was **advanced again to the
M690 state on 2026-09-21** — public commit `0f86415` = private `a61d832d`,
covering M669–M690, **2,026 files compared byte for byte with 0 differing**, and
the hosted runner green on the first run. **No new tag**: `JC_VERSION` did not
move, and the rule is to tag only when it does, so that cut is a state advance
rather than a release. The tree was then **advanced to the M709 state on
2026-09-22** — public commit `7180954` = private `02ef9c25`, covering M691–M709,
**2,063 files compared byte for byte with 0 differing**, and **tag `v0.10.0`**
on both remotes, because `JC_VERSION` moved 0.9.2 → 0.10.0 for the one breaking
change in it (M709 removed the vendor default). The tree was then **advanced to
the M716 state on 2026-09-23** — public commit `cd62411` = private `f23dd158`,
covering M710–M716, **2,073 files compared byte for byte with 0 differing**, and
**tag `v0.10.1`** on both remotes, a PATCH for two fixes a user could hit (M714's
`search_code` dialect, M715's resumed-turn answer), with the hosted runner green
on the branch before the tag was pushed and then on the tag. The tree was then
**advanced to the M741 state on 2026-09-24** — public commit `ed087f1` = private
`087120af`, covering M717–M741, **2,101 files compared byte for byte with 0
differing**, and **tag `v0.12.0`** on both remotes. It took two public commits: the first,
`3c4b405` = private `21f27e36`, went red on the hosted runner (a test that needed qemu),
so the tag waited for the second, whose run was green. **This tree reads
0.13.0** -- the hardening wave: redirect and multipart fences, validated session ids,
and 32-bit large files (the [0.13.0] heading in CHANGELOG.md) -- a MINOR over 0.12.0,
which was a MINOR over 0.10.1 by way of 0.11.0, cut on 2026-09-24 and never published
on its own: 0.11.0 removed two defaults (M718, M719 -- a config may need one
line) and landed two safety fixes (M724, M728); 0.12.0 makes a rule jichi infers from an
`--auto` prompt advise rather than refuse (M734, the MINOR), names a call that keeps
returning the same result (M733), and fixes the smoke tier's `/tmp` leak (M739) and an
example that let a label write files (M740) -- the state the public tree carried until
the 0.13.0 advance; the public tree reaches 0.13.0 with the curated state that carries
this sentence. The release checklist, as it
landed:

- **done** — the rename to jichi (name, binaries, paths, remote, dependent
  projects); the **curriculum**, complete and still growing (all four shu-ha-ri
  stages, the nine standalone language courses — five functional: Racket, Guile,
  Elixir, Haskell, Clojure; four systems: C, Zig, C++, Rust — and a toolchain-free
  process track from requirements through scheduling, for **90 graded tasks and 75
  trap cases**, every grader proven red-first in CI, plus the instructor guide);
  **versioning + a user-facing CHANGELOG** (0.9.0, with 1.0.0 reserved for the
  release); and the **platform verdict**, stated honestly
  ([`docs/PLATFORMS.md`](docs/PLATFORMS.md)).
- **done — the licence** (M619, 2026-08-27): **Apache-2.0**, copyright
  **Justus-Liebig-Universität Gießen**, author **Alexander-Lars Dallmann** —
  § 69b UrhG separates the two. The three-line header is stamped in every tracked
  source, `jichi --version` prints all three lines, and the licence lint pins
  them; [License](#license) has the full position.
- **done — the public snapshot** (M620, 2026-08-27): produced by
  `scripts/make-snapshot.sh --commit` exactly as the plan prescribed, verified
  standalone, and **published** as above. The first hosted CI run of the
  published tree then failed in the snapshot lint itself — the gate had assumed
  a configured git identity — mended at M621
  ([`docs/ANECDOTES.md`](docs/ANECDOTES.md) #76).
- **open** — presentation slides for the release, and identity & delight: a
  **logo**, a jingle.

Full status and themed design history: [`docs/ROADMAP.md`](docs/ROADMAP.md).

<details>
<summary><b>Selected highlights from the history</b> (long; the complete record is <a href="docs/ROADMAP.md">ROADMAP.md</a> per milestone and <a href="CHANGELOG.md">CHANGELOG.md</a> per version)</summary>

**Recent & significant:**

- **M284–M296 — checks that see what reading cannot, and the learn loop reaches
  the TUI.** Two enforced fences turned out to be silently empty: a **model
  selector** resolved only at *use* time, so a typo surfaced as a mid-run subagent
  error — or, for `routing.fast`/`strong`, as a run that never escalated at all;
  and a profile's `tools:` allow-list, where a dead entry quietly shrinks what a
  specialist can do (found by reading 31 MB of telemetry, then made a lint so
  nobody has to again). A **telemetry-driven fix wave** followed: one honest basis
  for the token estimate, *paging is not repetition*, an escalation trigger keyed
  off **room rather than difficulty**, and the elision placeholder **the model
  copied back as tool arguments** — 18 of 19 argument-shape failures on a single
  run, each an uncached round-trip answered by an error that explained nothing.
  Then the learning loop's own surfaces: `/learn analyze`, `/learn apply` (for
  correctness — applying from a *second* process cannot refresh a live session, so
  it kept serving notes a `## Corrections` section had superseded), and
  `learn corrections`, an operation two shipped warnings had promised for months
  before it existed. Closing with a lint that every `/command` in a source string
  resolves — **no exception list**, narrowing on facts about C and paths rather
  than guesses about English — and the TUI finally naming the *model* rather than
  only the routing tier.
- **M230–M283b — the claims audit, real hardware, and two graded course
  families.** Four passes over what the project *says* about itself (docs, every
  checkable number, the code comments, and the 31 scaffold packs materialised and
  read as a user receives them) found counts that had drifted because each
  milestone **incremented the previous claim instead of recounting** — so the fix
  was a lint, not a correction. The hardware-testing plan was then *executed*
  rather than imagined: old glibc, old kernels, big-endian, a 256 MB machine, real
  terminal emulators driven without a human — and three full `make check-target`
  runs on physical ARM (Pi Zero 2 W on both word sizes, an Arduino UNO Q), which
  proved three portability defects in-row on a 4.9-kernel guest. Alongside:
  **nine graded language courses** in two families (functional and systems), the
  five paradigm reading tracks, a path-fence **security property** under
  libFuzzer, and `examples/self-hosting` — jichi developing jichi.
- **M191–M229 — telemetry-hardening, a Python-free test tier, and the
  curriculum's second wave.** A private downstream workspace's long unattended
  runs drove a **memory-lifetime wave** (the 12.5 GB long-run RSS reproduced
  and fixed — session-store + per-tool-call **arena lifetimes**, with lints
  that keep them; listing without a parse tree 193 MB → 13.6 MB) and further
  hardening (`malloc_trim` heap-return, mid-turn args elision, transparent
  tool-name aliases, `prune`, **libcurl connection keep-alive**, an optional
  **shell-command wall-clock timeout**). The whole portable e2e suite was
  ported to a **python3-free smoke tier** (POSIX-sh drivers + four C89 helpers,
  so `make check-target` gates any POSIX box). The curriculum gained set D
  (memory & lifetimes), C→Zig / C→C++ **migration tracks**, two complete
  **source-reading guide** editions (案内（あんない） *Annai* + 深掘り（ふかぼり） *Fukabori*), and a
  C-standards **trilogy** — porting to C89, plus the undefined-behaviour and
  implementation-defined-behaviour traps a sanitizer and a compile-both-ways
  diff respectively catch. Full `make ci` green on the low-resource reference
  box (9,619 checks / 0 failures, Valgrind clean, smoke + e2e OK).
- **M180–M190 — the 2026-07-28 program.** Fifteen requested items in eleven
  milestones, one day: the **memory-truth band** (self-RSS in
  telemetry/heartbeat/`/context`, a soak harness, four arena-lifetime fixes,
  the honest verdict on a historical 12 GB report), the measured **footprint
  comparison** (1.4 MB vs 150–263 MB on disk against opencode/Claude Code),
  the **agent-collaboration and ML maps**, five **SDLC journey presets** with
  a full-lifecycle `sdlc` pack, **accessibility advisors in every scaffold
  pack from day one**, an LLM-server **stress-test fleet**, the **music
  pack** (engraving gate as verify), and three boundary surveys — Windows
  (where POSIX ends), **C++** and **zig cc** builds verified — closing with a
  repaired no-curl build and a **fully static musl jichi** from one command.
- **M174–M179 — the curriculum, complete.** All four shu-ha-ri stages ship
  in-repo: twelve module pages, **18 graded assignments (47 points)** whose
  graders are two-sided-proven in CI, learner progress tooling, the starter
  glossary, the institutional-gateway page, and the
  [instructor guide](docs/curriculum/INSTRUCTOR.md). Plus **semantic
  versioning + [CHANGELOG.md](CHANGELOG.md)** so nobody parses git history.
- **M170–M173 — release push.** The project was renamed **jichi** ("just code";
  自治（じち）, *autonomy*) end to end — binaries, config/state paths, env vars, the git
  remote, and the projects that drive it — and a **self-learner-first
  [curriculum](docs/proposals/2026-07-curriculum.md)** was designed with its
  enablers built: `setup --preset learner|instructor`, a TUI
  assignment/tutor/hint loop, and a [from-nothing build
  walkthrough](docs/PREPARE_AND_BUILD.md). A provenance cleanup (M171) confirmed
  there is **no vendored third-party source** in the tree.
- **M166–M169 — hardening from real use.** A live **local-GPU small-model
  bench** ([`docs/BENCH_LOCAL_GPU.md`](docs/BENCH_LOCAL_GPU.md)) and telemetry
  from a large dogfood port surfaced concrete bugs: an empty-assistant-turn that
  silenced tool calls on small models, a red gate misread as a broken tool, a
  stringified numeric argument that read a whole file, and prompt-inferred
  constraints that outlived their turn — each fixed and guarded by a test. A new
  `doctor --live` probes tool calling end to end.
- **M157–M163 — unattended operation & the physical world.** A loop supervisor
  over a task queue, `runs`/`audit` observability readers, a mid-run **control
  socket** (`status`/`inject`/`pause`/`resume`/`abort`), and jichi as a robot's
  deliberative layer with a **kinetic-safety gate** below the permission verdict
  ([`docs/ROBOTICS.md`](docs/ROBOTICS.md), hardware-free simulator included).
- **M152–M155 — privileged-command safety.** A model-issued `sudo`/`doas`/`pkexec`
  is gated **below** the permission verdict (a blanket auto-approve can't satisfy
  it) and every attempt is written to an always-on audit log.
- **M100+ — the self-improvement band** ([`docs/SELF_IMPROVEMENT.md`](docs/SELF_IMPROVEMENT.md)):
  a warm-process **daemon**, machine-checkable **assign/grade** evals, a
  propose-only **learning loop** that feeds the agent's own logs back as durable
  lessons, and an idle "dream" reflection.
- **M37–M99 — the steady climb:** resilient/fuzzy multi-file editing +
  `apply_patch`, hybrid **RAG** (BM25 + embeddings + rerank) over repo *and* docs,
  **LSP** navigation + refactors, the **autonomy envelope** (budgets + verify gate
  + edit-scope + audit journal) with snapshots/undo/rewind, mid-turn compaction +
  token calibration, and the setup wizard.

**Earlier foundations (M11–M36):**

- **M11–M15** — project scaffolding (`init`), project-archetype packs,
  audience-aware documentation agents, an enforced per-agent `tools:` allow-list,
  and asset introspection (`agents`/`commands`/`rules`/`sysmsg`). See
  [`docs/SCAFFOLDING.md`](docs/SCAFFOLDING.md).
- **M48** — the guided [`setup` wizard](docs/SETUP_WIZARD.md): role presets
  (developer / technical-writer / tester / reviewer / generic / devops / support
  / data / small-local / learner / instructor) compose a scaffold pack + config
  + executable start-script + a
  validation pass in one flow. New to jichi? Read
  [`docs/TUTORIAL_BEGINNER.md`](docs/TUTORIAL_BEGINNER.md), then
  [`docs/TUTORIAL_ADVANCED.md`](docs/TUTORIAL_ADVANCED.md). If the dense pages are
  themselves the barrier, start with
  [`docs/PLAIN_LANGUAGE.md`](docs/PLAIN_LANGUAGE.md) or, in German,
  [Einfache Sprache](docs/i18n/de/EINFACHE_SPRACHE.md) (M305) — separate plain-register
  pages, not simplifications of these.
- **M16** — the TUI colours the responding model, the mode (mode-keyed, so `auto`
  flags the unattended posture), and the token in/out counts.
- **M17** — an optional [SDLC assignment workflow](docs/ASSIGNMENTS.md): with the
  `assignments` pack + config flag, agents write design/coding/review assignments
  (mermaid, pseudo-code, research hints, algorithms, toolchain) plus reference
  solutions, so you can compare your own work against the recommended one.
- **M18** — bounded multi-level subagents (`maxSubagentDepth`).
- **M19** — ACP client-side `terminal/*` delegation (run commands in the editor's
  terminal). See [`docs/ACP.md`](docs/ACP.md).
- **M20** — an optional low-resource mode: `--lite`/`lowResource`, a per-turn
  scratch arena, configurable tool-output caps, streaming request body, and a
  `make SIZE=1` size build. See [`docs/LOW_MEMORY.md`](docs/LOW_MEMORY.md).

- **M21** — an optional event-logging / telemetry sink (`--log-level
  metrics|full`): structured JSONL of model latency/tokens/cost, tool usage,
  routing, and (opt-in) prompt/response content, plus a `telemetry` summary
  subcommand — for refining and optimizing jichi offline. See
  [`docs/TELEMETRY.md`](docs/TELEMETRY.md).
- **M22–M23** — configurable model-call timeouts (`connect`/`stall`/`request`)
  with stall detection, and stall-aware routing that escalates a frozen fast
  tier to the strong tier. See [`docs/MODELS.md`](docs/MODELS.md) and
  [`docs/ROUTING.md`](docs/ROUTING.md).
- **M24** — a workspace **path-containment fence** (realpath + root check on the
  file tools, default-on under `--auto`), file-size / path-length / SSE field
  bounds, and defensive secret redaction. See [`docs/AUTONOMY.md`](docs/AUTONOMY.md).
- **M25** — config-driven **lifecycle hooks** at SessionStart / UserPromptSubmit
  / PreToolUse / PostToolUse / Stop (a PreToolUse hook can block a tool). Opt-in,
  top-level only. See [`docs/HOOKS.md`](docs/HOOKS.md).
- **M26** — **background commands**: `run_terminal_command{run_in_background}`
  plus `read_background_output` / `kill_background` for dev servers, watchers,
  and long builds. See [`docs/BACKGROUND.md`](docs/BACKGROUND.md).
- **M27** — a **`web_search`** tool over a configurable search backend
  (registered only when `search.url` is set). See
  [`docs/WEBSEARCH.md`](docs/WEBSEARCH.md).
- **M28** — honored command `model:` / `subtask:` frontmatter, plus custom
  **output styles** that augment the persona for a session
  (`/output-style`, `--output-style`). See
  [`docs/OUTPUT_STYLES.md`](docs/OUTPUT_STYLES.md).
- **M29** — **vision input**: attach images (`--image`, `@photo.png` /
  `@img:<path>`, or ACP image prompt blocks) for vision-capable models, gated by
  a per-model `vision` flag. See [`docs/VISION.md`](docs/VISION.md).
- **M30** — **context-aware auto-compaction**: the prefix is summarized in
  windows sized to the *summarize* model's own context (folding the partials),
  so a small-context summarizer paired with a large active model no longer
  overflows and returns HTTP 400. See [`docs/COMPACTION.md`](docs/COMPACTION.md).
- **M31** — **prompt caching**: cached-token usage is reported for both
  providers (telemetry `cache_read_in`/`cache_write_in`, the TUI `cached=N`
  line, and a `telemetry` hit-rate); the Anthropic provider places explicit
  `cache_control` breakpoints on the system block and the conversation tail, the
  OpenAI provider emits a stable `prompt_cache_key`, and cost accounting bills
  cached reads/writes at their own rates. Gated by a `promptCache` tri-state
  (`--prompt-cache`/`--no-prompt-cache`, the TUI `/cache`). See
  [`docs/PROMPT_CACHING.md`](docs/PROMPT_CACHING.md).
- **M32** — **media generation**: `generate_image` and `generate_audio` (TTS)
  tools call OpenAI-compatible endpoints — selected by `image`/`audio` model
  roles — and save the result into the workspace (path-fenced, permission-gated,
  size-capped); returns the saved path, never inline data. See
  [`docs/MEDIA_GEN.md`](docs/MEDIA_GEN.md).
- **M33** — **audio input**: the `transcribe_audio` tool uploads a workspace
  audio file (multipart) to an OpenAI-compatible transcription endpoint —
  selected by the `transcribe` model role — and returns the transcript
  (read-only, path-fenced, size-capped). Adds the audio provider plumbing the
  ACP audio-prompt deferral was waiting on. See
  [`docs/TRANSCRIBE.md`](docs/TRANSCRIBE.md).
- **M34a** — **external documentation index**: config `docs: [{name, path}]`
  sources are indexed via the embeddings stack (a sibling of codebase search over
  a named directory). The read-only `search_docs` tool, the `@docs:<name>`
  reference, and the `docs [list|index|search]` subcommand let the agent ground
  answers in a library's docs, a style guide, or a spec — not just the workspace.
  See [`docs/DOCS.md`](docs/DOCS.md).
- **M34b** — **session export**: the pure `jc_session_render` turns any saved
  session into a clean Markdown or self-contained HTML transcript (title +
  metadata, role-labeled turns, fenced tool calls/results). The read-only
  `export [id] [--html] [-o file]` subcommand (stdout-friendly) and the TUI
  `/export` make conversations shareable as PR write-ups, decision/handoff
  records, and teaching artifacts. See [`docs/EXPORT.md`](docs/EXPORT.md).
- **M34c** — **conversational rewind**: `/rewind` (and the `rewind [n]
  [--dry-run]` subcommand) returns the workspace *and* the conversation to an
  earlier checkpoint's turn in one step — restoring files via the snapshot
  machinery and truncating history on a user-message boundary (the pure
  `jc_rewind_match` maps checkpoints to turns), then re-saving. The missing half
  of `/undo`. See [`docs/REWIND.md`](docs/REWIND.md).
- **M34d** — **`ask_user`**: the agent can pause and put a focused clarifying
  question (with optional suggested answers) to the user instead of guessing,
  via the `app->ask` front-end delegate. The TUI prompts and blocks; headless /
  ACP / `--auto` leave it unset, so the tool returns a "proceed" note and an
  unattended run never hangs. See [`docs/ASK.md`](docs/ASK.md).
- **M34e** — **`@problems` / `@folder` references**: `@problems` pulls current
  language-server diagnostics for the files touched this session into the turn;
  `@folder:<dir>` inlines a scoped repository map (the directory's source files +
  their top-level symbols, via a refactored `jc_repomap_build_dir`) for fast
  onboarding. Both ride the existing `@`-reference machinery. See
  [`docs/REFERENCES.md`](docs/REFERENCES.md).
- **M35** — **Tier-3 polish**: `/cost` (a live session token + estimated-cost
  rollup), `/fork` (branch a new session from the current point via
  `jc_session_fork`, leaving the original resumable), and a **glossary**
  (`.jichi/glossary.md` + a global one) of domain terms injected into the system
  prompt so the agent speaks the project's vocabulary. See
  [`docs/GLOSSARY.md`](docs/GLOSSARY.md).
- **M36** — **Emacs integration**: `editors/emacs/jichi.el` sends a marked region
  or the whole buffer to jichi and returns the answer into a side buffer, at
  point, appended, or replacing the region — read-only text commands plus a
  guarded agentic `jichi-task`. It drives the headless contract (no C changes) and
  ships with an offline ERT suite. See [`docs/EMACS.md`](docs/EMACS.md).
- **M34f** — **completion notification**: opt-in terminal bell
  (`notifyBell`/`--bell`) and/or a `notify`/`--notify <cmd>` command run when a
  turn (TUI) or an `--auto` run finishes (with `$JICHI_NOTIFY`/`$JICHI_CWD` in the
  environment) — so you can step away from a long task and be pinged. See
  [`docs/NOTIFY.md`](docs/NOTIFY.md).

</details>

## Layout

```
include/        public headers (jc_*.h)
src/            implementation, grouped by subsystem
                (src/json/cJSON.* implements the cJSON API; it is ours)
tests/          unit tests (run via `make test`)
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
  by the employer, while authorship remains with the author. Every source file
  carries both above its SPDX line, and `jichi --version` prints all three.
- **Continue's Apache-2.0 is a coincidence, not an inheritance.** Continue was the
  *specification* — feature set, interaction model, config format — and **no code is
  shared** (it is ~39k lines of TypeScript/React/Node; this tree is C89 throughout).
- **Credit is not a copyright line.** Claude is credited as the implementing agent in
  [`CREDITS.md`](CREDITS.md) and in `NOTICE` — not as a holder, because copyright
  generally requires human authorship. The debt to Continue is there too.
- **The identifier is pinned everywhere it appears.** `tests/smoke/license_lint.sh`
  holds all 499 headers, `LICENSE`, `NOTICE`, `--version`, `describe` and
  [`docs/LICENSING.md`](docs/LICENSING.md) to one identifier; a later, deliberate
  switch (should the review choose differently) is `scripts/set-license.sh <spdx-id>`
  again — one command, same lint.
