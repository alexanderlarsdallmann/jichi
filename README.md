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
| **Learn the craft** — a self-paced course with graded assignments, or read jichi's own source with a guide | [`docs/CURRICULUM.md`](docs/CURRICULUM.md) · [`docs/reading/ANNAI.md`](docs/reading/ANNAI.md) | `./jichi init learner` |
| **Run it for a group** — a shared machine, or JupyterHub for a class: what was measured, what a notebook costs, and the one configuration that loses work | [`docs/JUPYTERHUB.md`](docs/JUPYTERHUB.md) | `./jichi doctor` |

**Two things worth knowing before you invest an hour:**

- **Platforms.** **19 rows verified** — compiled *and* gate-run on each, with the
  numbers kept per row. Linux across **14 architectures** and five libcs, down to a
  256 MB VM; **FreeBSD, NetBSD and OpenBSD** all run the full gate; **WSL2** runs
  the whole of `make ci`, Cygwin the unit and smoke tiers; Android both cross-built
  and built on-device. **Never compiled: macOS and illumos** — there is no Mac on
  this project, and its one Darwin-specific line went months un-compilable because
  of it. [`docs/PLATFORMS.md`](docs/PLATFORMS.md) owns every verdict and says
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

**Never compiled from source before?** [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) walks you from an empty terminal to a working build on Linux, macOS, or Windows/WSL. Linux and **WSL2** are both verified paths — the WSL2 walkthrough has been executed end to end, by a non-root user, against pristine HEAD. **macOS is the one door nobody has opened**, and [`docs/PLATFORMS.md`](docs/PLATFORMS.md) is the one page that states, per platform, what was actually compiled and gate-run.

Built incrementally in milestones — **620 of them**, 588 written up in full (the
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
unit suite (**over 11,000 checks** — a growing figure, so stated as a bound
per the M307 rule), `make smoke` adds a **python-free** tier that makes
`make check-target` a full gate on any POSIX box, and `make ci` additionally runs
the suite under two compilers, AddressSanitizer + UndefinedBehaviorSanitizer,
Valgrind, and a fuzzer. Green end to end at **M486** on the development box:
**12,422 checks / 0 failures**, smoke **211 drivers / 1,141 checks**.

Each verified platform is **kept as its own stamped datum** rather than
overwritten, because "it passes on a small machine" and "it passes on that
architecture" are different claims from "it passes here". Nineteen of them — a 256 MB
one-core VM, a kernel 4.9 userland, two physical ARM boards (aarch64 *and* armhf),
s390x big-endian, a static musl build, three BSD kernels, WSL2, a phone — with their
check counts and passing timeout multipliers: **[`docs/PLATFORMS.md`](docs/PLATFORMS.md)**,
which is also where the honest converse lives (**macOS and illumos have never been
compiled**, and the page says so in those words).

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
warnings — with no exemptions. The JSON implementation (`src/json/cJSON.c`) is
without `-pedantic`.

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
./jichi -p "explain what this repo does"

# Headless prompt from stdin (handy for piping files or scripts)
cat spec.md | ./jichi -p -

# Pipe data as context to a prompt; machine-readable output; quiet
cat error.log | ./jichi -p "what caused this error?"
./jichi --output json -p "hi" | jq -r .text
./jichi -q -p "..."            # no diagnostics on stderr

# Conversations: continue this dir's latest, or resume a specific one
./jichi -c                    # continue the most recent chat here
./jichi ls                    # list this project's saved chats (newest first)
./jichi ls --all              # every project
./jichi --session 3f9a -p "…" # resume a specific chat by id/prefix

# Operating modes (see "Agent modes" below)
./jichi --plan                # investigate + propose a plan, change nothing
./jichi --auto                # run approved tools without asking (see the note below)
./jichi --readonly            # forbid mutating tools

# Bounded unsupervised run: verify, budget, auto-rollback (see "Autonomous runs")
./jichi --auto --verify 'make && make test' --budget-tokens 200k \
  -p "fix the failing tests"

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

Set an API key first: `export ANTHROPIC_API_KEY=...` (or `OPENAI_API_KEY`).
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
title. Resume restores the saved mode; the agent's tools act on your current
directory. In the TUI: `/sessions` lists, `/resume <id>` switches conversations,
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
step-by-step [`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md). Example:

```json
{
  "model": {
    "provider": "anthropic",
    "model": "claude-opus-4-8",
    "apiKeyEnv": "ANTHROPIC_API_KEY",
    "maxTokens": 4096,
    "temperature": 0.0,
    "inputCostPer1M": 15.0,
    "outputCostPer1M": 75.0
  },
  "maxToolIters": 25,
  "maxRetries": 4
}
```

Both the **Anthropic Messages API** and **OpenAI-compatible** chat completions
are supported through a pluggable provider abstraction.

### Multiple models

Provide a `models` array instead of a single `model` to configure several and
switch between them at runtime. The first entry is active on startup.

```json
{
  "models": [
    { "name": "Claude", "provider": "anthropic", "model": "claude-opus-4-8",
      "apiKeyEnv": "ANTHROPIC_API_KEY" },
    { "name": "GPT-4o", "provider": "openai", "model": "gpt-4o",
      "apiKeyEnv": "OPENAI_API_KEY" }
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

Other role strings (`chat`, `edit`, `autocomplete`, `summarize`, `apply`) are
parsed and preserved by `jichi-convert` but not yet acted on. Example:

```json
{
  "models": [
    { "name": "GPT-4o", "provider": "openai", "model": "gpt-4o",
      "apiKeyEnv": "OPENAI_API_KEY", "roles": ["chat", "edit"] },
    { "name": "Embedder", "provider": "openai", "model": "text-embedding-3-small",
      "apiBase": "https://my-host/v1", "apiKeyEnv": "OPENAI_API_KEY",
      "roles": ["embed"] },
    { "name": "Reranker", "provider": "openai", "model": "rerank-1",
      "apiBase": "https://my-host/v1", "apiKeyEnv": "OPENAI_API_KEY",
      "roles": ["rerank"] }
  ]
}
```

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
./jichi -p "Summarize @src/main.c and explain @diff"
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
  --verify 'make test' -p "fix the failing tests"
jichi --no-route --model opus -p "..."   # pin one model
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
  "model": { "provider": "anthropic", "model": "claude-opus-4-8" },
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
./jichi serve --acp     # JSON-RPC 2.0 over stdin/stdout
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
  -p "Fix the failing parser tests"
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

**Where we stand: latest milestone M621.** The engineering loop is healthy; the
**first public release shipped 2026-08-27**: **v0.9.0**, one curated commit,
published to the HRZ GitLab (`jichi-public/jichi`) and to GitHub, tag `v0.9.0`
on both ([`docs/plans/2026-08-public-snapshot.md`](docs/plans/2026-08-public-snapshot.md),
executed as written). The release checklist, as it landed:

- **done** — the rename to jichi (name, binaries, paths, remote, dependent
  projects); the **curriculum**, complete and still growing (all four shu-ha-ri
  stages, the nine standalone language courses — five functional: Racket, Guile,
  Elixir, Haskell, Clojure; four systems: C, Zig, C++, Rust — and a toolchain-free
  process track from requirements through scheduling, for **77 graded tasks and 55
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
  holds all 486 headers, `LICENSE`, `NOTICE`, `--version`, `describe` and
  [`docs/LICENSING.md`](docs/LICENSING.md) to one identifier; a later, deliberate
  switch (should the review choose differently) is `scripts/set-license.sh <spdx-id>`
  again — one command, same lint.
