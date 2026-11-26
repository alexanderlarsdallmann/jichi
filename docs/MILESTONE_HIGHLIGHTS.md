# Milestone highlights: the bands from M0, the history to M296, and the public cuts

> **Who this is for.** Readers who want the long view of how jichi grew, without reading the
> roadmap's four megabytes and more. If you want to build or use jichi, read the
> [README](../README.md) instead; if you want one milestone's design and evidence, read its
> entry in [`ROADMAP.md`](ROADMAP.md).
>
> **Where this text came from.** Until 2026-11-23 the README carried it: the milestone-band
> table in its Status section, and the release history, the release checklist and the selected
> highlights in its Roadmap section. On a 150 KB front page they took 42 KB, and the Roadmap
> section's milestone banner took 46 KB more. The banner was dropped (every milestone it named
> has its own entry in [`ROADMAP.md`](ROADMAP.md)); the rest moved here unchanged, except that
> the table became a list, the links were rewritten for this folder, the release history starts
> with the word it shared with the dropped banner ("The"), and the folding `<details>` wrappers
> were removed.
>
> **None of it is kept current.** Where jichi stands now is in the README's Roadmap section, in
> [`ROADMAP.md`](ROADMAP.md) (each milestone) and in [`../CHANGELOG.md`](../CHANGELOG.md) (each
> version).

## Releases

The **first public release shipped 2026-08-27**: **v0.9.0**, one curated commit,
published to the HRZ GitLab (`jichi-public/jichi`) and to GitHub, tag `v0.9.0`
on both ([`docs/plans/2026-08-public-snapshot.md`](plans/2026-08-public-snapshot.md),
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
so the tag waited for the second, whose run was green. The tree was then **advanced to the M1143 state on 2026-11-05** — public commit `aaae2f0` = private `9ada7cd7`, covering M742–M1143, **2,555 files compared byte for byte with 0 differing**, and **tag `v0.13.0`** on both remotes, the hosted runner green on the preflight branch (run 42070751571), on master (run 42078844479) and on the tag (run 42086255516). **This tree reads
0.14.1** -- a PATCH over the 0.14.0 cut (the [0.14.1] heading in CHANGELOG.md),
which is the drive's outer loop (jichi-harness) and the platform batch, a MINOR over
0.13.0 (the [0.14.0] heading in CHANGELOG.md), which reads
0.13.0 -- the hardening wave: redirect and multipart fences, validated session ids,
and 32-bit large files (the [0.13.0] heading in CHANGELOG.md) -- a MINOR over 0.12.0,
which was a MINOR over 0.10.1 by way of 0.11.0, cut on 2026-09-24 and never published
on its own: 0.11.0 removed two defaults (M718, M719 -- a config may need one
line) and landed two safety fixes (M724, M728); 0.12.0 makes a rule jichi infers from an
`--auto` prompt advise rather than refuse (M734, the MINOR), names a call that keeps
returning the same result (M733), and fixes the smoke tier's `/tmp` leak (M739) and an
example that let a label write files (M740) -- the state the public tree carried until
the 0.13.0 advance; the public tree reaches 0.14.1 with the curated state that carries
this sentence (0.14.0 was cut privately on 7f5ff7eb and its public cut was stopped, so
the tree that would have read 0.14.0 reads 0.14.1 instead).

## The first-release checklist, as it landed

- **done** — the rename to jichi (name, binaries, paths, remote, dependent
  projects); the **curriculum**, complete and still growing (all four shu-ha-ri
  stages, the nine standalone language courses — five functional: Racket, Guile,
  Elixir, Haskell, Clojure; four systems: C, Zig, C++, Rust — and a toolchain-free
  process track from requirements through scheduling, for **90 graded tasks and 75
  trap cases**, every grader proven red-first in CI, plus the instructor guide);
  **versioning + a user-facing CHANGELOG** (0.9.0, with 1.0.0 reserved for the
  release); and the **platform verdict**, stated honestly
  ([`docs/PLATFORMS.md`](PLATFORMS.md)).
- **done — the licence** (M619, 2026-08-27): **Apache-2.0**, copyright
  **Justus-Liebig-Universität Gießen**, author **Alexander-Lars Dallmann** —
  § 69b UrhG separates the two. The three-line header is stamped in every tracked
  source, `jichi --version` prints all three lines, and the licence lint pins
  them; [License](../README.md#license) has the full position.
- **done — the public snapshot** (M620, 2026-08-27): produced by
  `scripts/make-snapshot.sh --commit` exactly as the plan prescribed, verified
  standalone, and **published** as above. The first hosted CI run of the
  published tree then failed in the snapshot lint itself — the gate had assumed
  a configured git identity — mended at M621
  ([`docs/ANECDOTES.md`](ANECDOTES.md) #76).
- **open** — presentation slides for the release, and identity & delight: a
  **logo**, a jingle.

## Every milestone band, M0 to M296

This was a table until 2026-11-23; its State column read "done" in every row. It stopped
being extended at M296, and its last entry says why.

- **M0** — Skeleton, build system, in-tree JSON
- **M1** — Utilities (arena/sb/vec/log/snprintf/uuid), JSON wrapper, config
- **M2** — HTTP (libcurl) + SSE + provider abstraction + headless chat
- **M3** — Tool registry + 6 builtins + agent loop
- **M4** — Session persistence (`~/.jichi.d/sessions/*.json`)
- **M5** — Interactive TUI (raw ANSI line editor)
- **M6** — Embedding/rerank models + semantic codebase search
- **M7–M23** — User tools, `@`-refs, autocomplete/FIM, ACP server, scaffolding + packs, doc
  agents, assignments, MCP, subagents + parallel agents, low-resource mode, telemetry,
  model-call timeouts + stall-aware routing (see ROADMAP)
- **M24** — Workspace path fence + resource bounds + secret redaction
- **M25** — Lifecycle hooks (PreToolUse/PostToolUse/UserPromptSubmit/Stop/SessionStart)
- **M26** — Background commands (`run_in_background` + read/kill)
- **M27** — `web_search` tool (configurable backend)
- **M28** — Command `model:`/`subtask:` frontmatter + custom output styles
- **M29** — Vision input (image attachments for multimodal models)
- **Quality pass** — Code review of M24–M29 + fixes: background-slot reclaim, fork/`setpgid` race,
  fence-before-`mkdir`, no-silent-image-drop, hard SSE field cap, shared `jc_proc_capture` (fixes the hook
  stdin/stdout deadlock), and `jc_logf` secret redaction
- **M30** — Context-aware (chunked) auto-compaction: summarize the prefix in
  summarizer-context-sized windows so a small-context `summarize` model isn't sent a prefix sized to
  the big active model (fixes the summarizer HTTP 400)
- **M31** — Prompt caching: report cached-token usage (both providers) + Anthropic `cache_control`
  breakpoints (system + history tail) + OpenAI `prompt_cache_key` + cache-aware cost, gated by a `promptCache`
  tri-state (`--prompt-cache`/`/cache`)
- **M32** — Media generation tools: `generate_image` and `generate_audio` (TTS) call OpenAI-compatible endpoints
  (selected by `image`/`audio` model roles) and save the result into the workspace; path-fenced,
  permission-gated, size-capped
- **M33** — Audio input: `transcribe_audio` uploads a workspace audio file (multipart) to an
  OpenAI-compatible transcription endpoint (selected by the `transcribe` model role) and returns the
  text; read-only, path-fenced, size-capped
- **M34a** — External documentation index: config `docs: [{name, path}]` sources, indexed via the embeddings
  stack; the read-only `search_docs` tool, the `@docs:<name>` reference, and a `docs [list|index|search]` subcommand ground answers in
  external reference material
- **M34b** — Session export: render any saved session to a Markdown (or self-contained HTML)
  transcript via the pure `jc_session_render`; the read-only `export [id] [--html] [-o file]` subcommand and the TUI `/export` for PR
  write-ups, handoffs, and teaching artifacts
- **M34c** — Conversational rewind: `/rewind` (and the `rewind [n] [--dry-run]` subcommand) returns files **and** the
  conversation to an earlier checkpoint's turn — restore via the snapshot machinery +
  truncate history on a user-message boundary + re-save — the missing half of `/undo`
- **M34d** — `ask_user` tool: the agent pauses and asks the user a focused clarifying question
  (optional suggested answers) via the `app->ask` front-end delegate; the TUI prompts and blocks,
  headless/ACP/`--auto` return a "proceed" note so an unattended run never hangs
- **M34e** — `@problems` / `@folder:<dir>` references: `@problems` inlines current LSP diagnostics for the files touched
  this session; `@folder:<dir>` inlines a scoped repository map (source files + top-level symbols) for
  fast onboarding — both in the existing `@`-reference shape
- **M34f** — Completion notification: opt-in terminal bell (`notifyBell`/`--bell`) and/or a `notify`/`--notify <cmd>`
  command run on turn (TUI) or `--auto` completion, exposing `$JICHI_NOTIFY`/`$JICHI_CWD` — step away from a long task
  and get pinged
- **M35** — Tier-3 polish: `/cost` (live session token + estimated-cost rollup), `/fork` (branch a
  new session from the current point, original preserved), and a `.jichi/glossary.md` glossary of domain
  terms injected into the system prompt
- **M36** — Emacs integration (`editors/emacs/jichi.el`): send a region or buffer to jichi and get the answer
  back in a side buffer, at point, appended, or replacing the region; read-only text
  commands + a guarded agentic `jichi-task`. Drives the headless contract (no C changes)
- **M37–M109** — A long steady-state band (see [`docs/ROADMAP.md`](ROADMAP.md)): resilient/fuzzy
  editing + `apply_patch`, mutating git tools + self-review, LSP refactors
  (rename/format/code-actions) + `executeCommand`, hybrid RAG (BM25 + RRF) + query rewrite +
  auto-context, PDF ingestion, mid-turn compaction + token-estimate calibration, the tool
  profile, prompt-cache TTL, the setup wizard + import/advisor, config-safety lints, the
  reference-root read fence, per-workspace telemetry + session timelines, the
  learning/mentor loop, the warm-process **daemon**, and a self-improvement band
  (assign/grade/dream/loop).
- **2026-07 wave** — This bundle: subagent depth 2 + budget taper, opt-in skills `restrict-tools`
  (subagent-scoped), a hardened daemon **worker pool**, RSS/Atom feeds in `@rss:`/RAG, `setup --from-global`/`--inherit`
  config reuse, the `onboarding` pack + `setup --onboard`, a no-Docker **LocalAI** media-gen path (verified image
  gen), **Vim/nano** editor support, and a documentation refresh.
- **M135–M137** — Natural-language band: the `language` key / `--language` / `/language` (answer in the user's
  language, one stable system-prompt line) + a compiled-in **runtime UI message catalog**
  (the tool-approval prompt and working indicator in en/de/es/ja/zh; `$JICHI_LANG` override, English
  fallback on non-UTF-8 terminals) + an **endianness tag** in the vector-index manifest (a
  cache written on a foreign-endian machine rebuilds instead of being silently misread). See
  [`docs/LANGUAGE.md`](LANGUAGE.md).
- **M138–M143** — Integrity + resources band: `apply_patch` **write-phase rollback** (a mid-write
  failure reverts already-written files and reports every file's state); the **journey map**
  ([`docs/JOURNEY.md`](JOURNEY.md), five languages, like [`docs/PHILOSOPHY.md`](PHILOSOPHY.md)); **memory
  hygiene** (arena-lifetime fixes, unformatted session saves, an `Arenas:` gauge in `/context`) + **index
  residency** (clean cache loads mmap `vectors.f32` read-only; `--lite` frees the index per search); opt-in
  **out-of-scope auto-revert** (`--revert-out-of-scope`: shell-introduced changes outside `--edit-scope` are individually
  restored to the run-start baseline); and the **memory-budget overflow surfaced** (load
  warning + `remember`-tool note + `doctor` flag instead of silently skipping the oldest notes).
- **M144–M151** — Small-model tool-calling band: native-call nudges + argument repair, a `small-local`
  preset + language packs, per-child parallel **verify** gate (a red write-child is
  quarantined, not merged), toolProfile core set.
- **M152–M156** — Privileged-command safety: a `sudo`/`doas`/`pkexec` gate **below** the permission
  verdict + an always-on audit log; multiline paste into the TUI prompt.
- **M157–M162** — Autonomous-operations arc: a loop supervisor over a task queue, `runs`/`audit`
  observability readers, `doctor --unattended`, and a mid-run **control socket** (`status`/`inject`/`pause`/`resume`/`abort`).
- **M163** — Robotics: jichi as a robot's deliberative layer — devices as tools, a
  **kinetic-safety gate** below the verdict, sound I/O; hardware-free simulator in `examples/robot-sim/`.
- **M164–M165** — Localized decks + timeline (de/es/ja/zh); web front-end **enablers**
  (`ls`/`export --output json`, `--heartbeat`) + a runnable stdlib bridge — no HTTP server enters jichi.
- **M166–M169** — Findings from a live **local-GPU bench** + zigodot telemetry: fixed an
  empty-assistant-turn bug that silenced tool calls, split a red gate from a broken tool,
  coerced stringified numeric args, and made prompt-inferred constraints session-scoped. `doctor --live`
  probes tool calling end to end.
- **M170–M173** — Release push: the rename to **jichi**
  (name/binaries/paths/remote/dependent projects); provenance cleanup (no vendored
  third-party source); the self-learner-first **curriculum** designed + its enablers built
  (learner/instructor presets, TUI assignment+hint loop, a from-nothing build walkthrough).
- **M174–M179** — The curriculum shipped whole: all four shu-ha-ri stages, twelve module
  pages, **18 graded assignments** with two-sided-proven graders, progress tooling, the
  starter glossary, the institutional-gateway page, and the instructor guide (C1–C7
  complete). Plus **versioning + CHANGELOG.md** (semver, `-V` on both binaries).
- **M180–M185** — The 2026-07-28 program, first half: the **memory truth** band (self-RSS in
  telemetry/heartbeat/`/context`, a soak harness, four arena-lifetime fixes, the honest 12 GB
  verdict), the **footprint comparison** vs opencode/Claude Code, the collaboration + ML
  maps, multi-pack `init`, the five **SDLC journey presets** + `sdlc` pack, **accessibility
  advisors in every pack**, and the **stress-test fleet harness**.
- **M186–M190** — New domains + boundary surveys: the **music pack** (LilyPond/MIDI/Ardour,
  the engraving gate as verify), **Windows mapped honestly** (WSL + the where-POSIX-ends
  survey assignment), **C++ and zig cc builds verified** (extern "C" guards, `cpp-check`, the honest
  verdicts), and the no-curl build repaired — yielding a **fully static musl jichi** from
  one `zig cc -target` command.
- **M191–M208** — Telemetry-driven correctness + the **memory-lifetime wave**: the 12.5 GB
  long-run RSS **reproduced and fixed** (session-store + per-tool-call **arena lifetimes**,
  with the lints that keep them), UTF-8-safe truncation, token attribution, and a run of
  small robustness fixes surfaced by real logs (stringified-arg coercion, listing without a
  parse tree 193 MB → 13.6 MB, red-baseline/read-only-scope corrections).
- **M209–M217** — **Python-free testing tier**: the whole portable e2e suite (71 drivers)
  ported to POSIX-sh **smoke** drivers backed by four C89 test helpers
  (mockmodel/ptydrive/jsonq/sockq), so `make check-target` is a full build gate on any POSIX box with **no
  python3**.
- **M218–M220** — **Hardening for long unattended runs**: `mallopt`/`malloc_trim` heap-return + mid-turn
  args elision + buffer-shrink (probed, ASan/Valgrind-clean); robustness from the error
  profile (transparent tool-name aliases, `prune` + a store-size `doctor` warning, a network
  fault-injection site); and **small-device prep** (aarch64 musl cross-build, `JC_E2E_TIMEOUT_MULT`).
- **M221–M225** — The **curriculum grows**: set D (memory & lifetimes), the C89-vs-modern-C
  extra, the open-source reading track, and C→Zig / C→C++ **migration tracks**; plus two
  complete **source-reading guide** editions — 案内（あんない） *Annai* (the guided tour) and
  深掘り（ふかぼり） *Fukabori* (the deep dive), with a drift lint.
- **M226–M229** — The **run-command wall-clock timeout** (+ the `supervise-long-command` skill) and **libcurl
  handle reuse** (connection keep-alive, fork-safe); the C-standards assignment **trilogy**
  completed — undefined behaviour (a sanitizer catches it) and implementation-defined
  behaviour (a compile-both-ways diff catches it) — all green under the full `make ci` on the
  low-resource reference box.
- **M230–M237** — **Small-run robustness + the paradigm reading tracks.** A nudge on a
  byte-identical **re-read** (telemetry showed one 80–93 KB file read dozens of times in a
  session, each a full round-trip) and a **streaming session serializer**; plus the five
  paradigm tracks that teach the functional shift by contrast with jichi's own pure-core C —
  [Racket](RACKET_PARADIGM.md), [Guile](GUILE_PARADIGM.md) (the one whose runtime
  embeds in C, doubling as the honest story of why jichi hosts no language),
  [Elixir](ELIXIR_PARADIGM.md) (the actor model jichi's fork pool hand-rolls),
  [Haskell](HASKELL_PARADIGM.md) (purity as a *checked* law),
  [Clojure](CLOJURE_PARADIGM.md) (persistent data; the hosted bet as jichi's opposite).
- **M239–M243** — **Dogfooding jichi on jichi**, plus onboarding polish: the `examples/self-hosting` dev pack in
  two slices (read-only, then **write-enabled**), the *Python and C* curriculum track (a
  systems lens, not a Python course), the **`JICHI_API_KEY`** default (the university env-var name is
  back-compatible, not required), and a **beginner-review pass** over the tutorials +
  curriculum.
- **M252–M253** — **Domain scaffolds surfaced** (`init --list` points at the twelve `examples/` benches + an
  index) and the **graded process track**: seven two-sided assignments for the half of
  software development no compiler checks — requirements, use-cases, design, documentation,
  session notes, kanban, scheduling — graded on a structural floor by pure `sh`, so it needs
  **no toolchain but jichi**.
- **M254–M258** — **Type-ahead: you can type while the agent works.** The human at the TUI
  was the only operator with no mid-run channel (a supervisor had the control socket, an
  editor had ACP; the keyboard had Ctrl-C) — and keystrokes were echoed into the streamed
  answer and then discarded. Now held, echoed live, and applied at the next tool boundary as
  one `[operator]` message (the control channel's own convention). **Opt-in, off by default**,
  because input you cannot see is input you cannot correct: visibility is guaranteed while
  the model thinks *and* while a shell command runs (M258's idle tick), not while prose
  streams. Ctrl-K un-queues; `/typeahead` toggles live. Includes two pre-existing fixes the work
  surfaced: a **use-after-free** in the empty-answer diagnostic (M255) and a **CI gate that
  could not report why it failed** (M256).
- **M238, M244–M251** — **Two complete graded course families.** Nine standalone language
  courses, each four two-sided assignments teaching the same craft (fix-forward →
  tests-as-proof → refactor → capstone) through one language's clearest facet:
  **functional** — Racket, Guile, Elixir, Haskell, Clojure (paradigm/immutability);
  **systems** — C (manual memory/allocators under ASan), Zig (`defer` + leak-detecting
  allocator), C++ (RAII), Rust (the borrow checker as compile-time safety). 36 assignments,
  every grader proven red-first in CI.
- **M259–M263** — **The claims audit, and the lints that replaced it.** Four passes over
  what the project *says* about itself — docs (M259), every checkable number (M260), the
  code comments (M261), and the 31 scaffold packs + `examples/` materialised into a throwaway
  workspace and read as a user receives them (M262, which found two promises the binary did
  not keep). The curriculum counts had drifted because each milestone **incremented the
  previous claim instead of recounting** (68/41 → the measured 74/46), so the fix was a
  lint, not a correction. M263 then *kept* one of M262's dead promises by building it: `formatCommand`,
  a second `format_file` backend for languages with no LSP formatter.
- **M264–M271** — **A hardware-testing plan with a virtualized tier, and the defects it
  flushed out.** Tier V — old glibc/kernels, big-endian, 256 MB machines, real terminal
  emulators — executed rather than imagined: three **pre-existing portability defects**
  (M265), aarch64 and **big-endian** both green (M266), and the terminal matrix driven
  **without a human** (M268/M274/M275). Alongside it the fuzz/fault band: a path-fence
  **security property**, **libFuzzer** targets, a wedged-run fix, and harness parity
  (M269–M271).
- **M272–M277, M282** — **Real hardware.** The whole-VM rows ran on threadwork (**V2e**: gcc
  builds the tree *inside* a 256 MB ceiling; **V2f**: a 4.9 kernel, which took twelve guest
  passes and proved **three product fixes in-row** — `git stash push` on pre-2.13 git, a silent worktree
  leak on pre-2.17, and an `Expect: 100-continue` second per model call). Then physical silicon: **Pi Zero 2
  W** on aarch64 (M272) *and*, re-flashed, **armhf** (M276 — the first real test of the
  `%lu`-with-casts convention where `long` is four bytes, retiring the last 32-bit risk), and the
  **Arduino UNO Q** (M282, a non-Pi arm64 SoC). Two boards, three full `make check-target` runs, all green;
  each board's passing timeout multiplier is itself a deliverable.
- **M278–M283b** — **Edge AI and two composing gestures.** A plan for a local small model on
  the UNO Q plus the network tier (M278, plan only); Ctrl-G ghost text that **demonstrates
  rather than instructs** (an instruction a model may ignore is weaker than an example it
  can pattern-match) and advice that is printed rather than assumed (M280); edge-AI use
  cases and the curriculum reframed for self-learners over SSH (M283/M283b, docs only).
- **M284–M285** — **Two fences nobody could see were empty.** A model selector (an agent
  profile's `model:`, a command's `model:`, `routing.fast`/`strong`) resolved only at *use* time, so a typo surfaced
  as a mid-run subagent error — or, for routing, as a run that silently never escalated; `doctor`
  now lints them at config time through the *same* resolver it predicts (M284). And a
  profile's `tools:` list is an **enforced** fence, so a dead entry silently shrinks what a
  specialist can do — found by reading 31 MB of telemetry, then turned into a check (M285).
- **M286–M291** — **A telemetry-driven fix wave.** One honest basis for the token estimate,
  and a routing flag that could not be armed (M286); *paging is not repetition* — two
  defects hiding behind a "re-read" number (M287); the escalation trigger that was missing
  because it should key off **room, not difficulty** (M288); the elision placeholder **the
  model copied back as tool arguments** — 18 of 19 argument-shape failures on one run, each
  an uncached round-trip answered by a message that explained nothing (M289); the version
  stamp the logs never carried (M290); and a **policy refusal is not a malfunction** (M291).
- **M292–M296** — **The learn loop reaches the TUI, and the model gets named.** `/learn analyze` (M292)
  and `/learn apply` (M293) — the latter for correctness, not convenience: applying from a *second*
  process cannot refresh a live session, so it went on serving notes a `## Corrections` section had
  already superseded. `learn corrections` (M294) makes real an operation two shipped warnings had named for
  months. M295 lints that every `/command` in a source string resolves — with **no exception
  list**, narrowing on facts about C and paths rather than guesses about English. M296 makes
  the TUI name the *model* and not only the routing tier, and stops a config without a `name`
  key printing `(null)` through `"%s"`.
- **M297–M400** — Not itemised here — this table stopped being extended at M296, and adding
  104 rows to a summary nobody reads is not the fix. The band covers the notice family (the
  agent is told *why* it was stopped), the elision claim-ticket, the registry lints (flags,
  config keys, telemetry events, keybindings, tool names, slash commands), the
  fence-integrity work, the design-and-modelling tutorials, two source-reading guide passes,
  and M400's never-compiled platform verdict. Per milestone with the reasoning:
  [`docs/ROADMAP.md`](ROADMAP.md); per version: [`CHANGELOG.md`](../CHANGELOG.md).

## Selected highlights from the history

### Recent and significant

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
  [instructor guide](curriculum/INSTRUCTOR.md). Plus **semantic
  versioning + [CHANGELOG.md](../CHANGELOG.md)** so nobody parses git history.
- **M170–M173 — release push.** The project was renamed **jichi** ("just code";
  自治（じち）, *autonomy*) end to end — binaries, config/state paths, env vars, the git
  remote, and the projects that drive it — and a **self-learner-first
  [curriculum](proposals/2026-07-curriculum.md)** was designed with its
  enablers built: `setup --preset learner|instructor`, a TUI
  assignment/tutor/hint loop, and a [from-nothing build
  walkthrough](PREPARE_AND_BUILD.md). A provenance cleanup (M171) confirmed
  there is **no vendored third-party source** in the tree.
- **M166–M169 — hardening from real use.** A live **local-GPU small-model
  bench** ([`docs/BENCH_LOCAL_GPU.md`](BENCH_LOCAL_GPU.md)) and telemetry
  from a large dogfood port surfaced concrete bugs: an empty-assistant-turn that
  silenced tool calls on small models, a red gate misread as a broken tool, a
  stringified numeric argument that read a whole file, and prompt-inferred
  constraints that outlived their turn — each fixed and guarded by a test. A new
  `doctor --live` probes tool calling end to end.
- **M157–M163 — unattended operation & the physical world.** A loop supervisor
  over a task queue, `runs`/`audit` observability readers, a mid-run **control
  socket** (`status`/`inject`/`pause`/`resume`/`abort`), and jichi as a robot's
  deliberative layer with a **kinetic-safety gate** below the permission verdict
  ([`docs/ROBOTICS.md`](ROBOTICS.md), hardware-free simulator included).
- **M152–M155 — privileged-command safety.** A model-issued `sudo`/`doas`/`pkexec`
  is gated **below** the permission verdict (a blanket auto-approve can't satisfy
  it) and every attempt is written to an always-on audit log.
- **M100+ — the self-improvement band** ([`docs/SELF_IMPROVEMENT.md`](SELF_IMPROVEMENT.md)):
  a warm-process **daemon**, machine-checkable **assign/grade** evals, a
  propose-only **learning loop** that feeds the agent's own logs back as durable
  lessons, and an idle "dream" reflection.
- **M37–M99 — the steady climb:** resilient/fuzzy multi-file editing +
  `apply_patch`, hybrid **RAG** (BM25 + embeddings + rerank) over repo *and* docs,
  **LSP** navigation + refactors, the **autonomy envelope** (budgets + verify gate
  + edit-scope + audit journal) with snapshots/undo/rewind, mid-turn compaction +
  token calibration, and the setup wizard.

### Earlier foundations (M11–M36)

- **M11–M15** — project scaffolding (`init`), project-archetype packs,
  audience-aware documentation agents, an enforced per-agent `tools:` allow-list,
  and asset introspection (`agents`/`commands`/`rules`/`sysmsg`). See
  [`docs/SCAFFOLDING.md`](SCAFFOLDING.md).
- **M48** — the guided [`setup` wizard](SETUP_WIZARD.md): role presets
  (developer / technical-writer / tester / reviewer / generic / devops / support
  / data / small-local / learner / instructor) compose a scaffold pack + config
  + executable start-script + a
  validation pass in one flow. New to jichi? Read
  [`docs/TUTORIAL_BEGINNER.md`](TUTORIAL_BEGINNER.md), then
  [`docs/TUTORIAL_ADVANCED.md`](TUTORIAL_ADVANCED.md). If the dense pages are
  themselves the barrier, start with
  [`docs/PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) or, in German,
  [Einfache Sprache](i18n/de/EINFACHE_SPRACHE.md) (M305) — separate plain-register
  pages, not simplifications of these.
- **M16** — the TUI colours the responding model, the mode (mode-keyed, so `auto`
  flags the unattended posture), and the token in/out counts.
- **M17** — an optional [SDLC assignment workflow](ASSIGNMENTS.md): with the
  `assignments` pack + config flag, agents write design/coding/review assignments
  (mermaid, pseudo-code, research hints, algorithms, toolchain) plus reference
  solutions, so you can compare your own work against the recommended one.
- **M18** — bounded multi-level subagents (`maxSubagentDepth`).
- **M19** — ACP client-side `terminal/*` delegation (run commands in the editor's
  terminal). See [`docs/ACP.md`](ACP.md).
- **M20** — an optional low-resource mode: `--lite`/`lowResource`, a per-turn
  scratch arena, configurable tool-output caps, streaming request body, and a
  `make SIZE=1` size build. See [`docs/LOW_MEMORY.md`](LOW_MEMORY.md).

- **M21** — an optional event-logging / telemetry sink (`--log-level
  metrics|full`): structured JSONL of model latency/tokens/cost, tool usage,
  routing, and (opt-in) prompt/response content, plus a `telemetry` summary
  subcommand — for refining and optimizing jichi offline. See
  [`docs/TELEMETRY.md`](TELEMETRY.md).
- **M22–M23** — configurable model-call timeouts (`connect`/`stall`/`request`)
  with stall detection, and stall-aware routing that escalates a frozen fast
  tier to the strong tier. See [`docs/MODELS.md`](MODELS.md) and
  [`docs/ROUTING.md`](ROUTING.md).
- **M24** — a workspace **path-containment fence** (realpath + root check on the
  file tools, default-on under `--auto`), file-size / path-length / SSE field
  bounds, and defensive secret redaction. See [`docs/AUTONOMY.md`](AUTONOMY.md).
- **M25** — config-driven **lifecycle hooks** at SessionStart / UserPromptSubmit
  / PreToolUse / PostToolUse / Stop (a PreToolUse hook can block a tool). Opt-in,
  top-level only. See [`docs/HOOKS.md`](HOOKS.md).
- **M26** — **background commands**: `run_terminal_command{run_in_background}`
  plus `read_background_output` / `kill_background` for dev servers, watchers,
  and long builds. See [`docs/BACKGROUND.md`](BACKGROUND.md).
- **M27** — a **`web_search`** tool over a configurable search backend
  (registered only when `search.url` is set). See
  [`docs/WEBSEARCH.md`](WEBSEARCH.md).
- **M28** — honored command `model:` / `subtask:` frontmatter, plus custom
  **output styles** that augment the persona for a session
  (`/output-style`, `--output-style`). See
  [`docs/OUTPUT_STYLES.md`](OUTPUT_STYLES.md).
- **M29** — **vision input**: attach images (`--image`, `@photo.png` /
  `@img:<path>`, or ACP image prompt blocks) for vision-capable models, gated by
  a per-model `vision` flag. See [`docs/VISION.md`](VISION.md).
- **M30** — **context-aware auto-compaction**: the prefix is summarized in
  windows sized to the *summarize* model's own context (folding the partials),
  so a small-context summarizer paired with a large active model no longer
  overflows and returns HTTP 400. See [`docs/COMPACTION.md`](COMPACTION.md).
- **M31** — **prompt caching**: cached-token usage is reported for both
  providers (telemetry `cache_read_in`/`cache_write_in`, the TUI `cached=N`
  line, and a `telemetry` hit-rate); the Anthropic provider places explicit
  `cache_control` breakpoints on the system block and the conversation tail, the
  OpenAI provider emits a stable `prompt_cache_key`, and cost accounting bills
  cached reads/writes at their own rates. Gated by a `promptCache` tri-state
  (`--prompt-cache`/`--no-prompt-cache`, the TUI `/cache`). See
  [`docs/PROMPT_CACHING.md`](PROMPT_CACHING.md).
- **M32** — **media generation**: `generate_image` and `generate_audio` (TTS)
  tools call OpenAI-compatible endpoints — selected by `image`/`audio` model
  roles — and save the result into the workspace (path-fenced, permission-gated,
  size-capped); returns the saved path, never inline data. See
  [`docs/MEDIA_GEN.md`](MEDIA_GEN.md).
- **M33** — **audio input**: the `transcribe_audio` tool uploads a workspace
  audio file (multipart) to an OpenAI-compatible transcription endpoint —
  selected by the `transcribe` model role — and returns the transcript
  (read-only, path-fenced, size-capped). Adds the audio provider plumbing the
  ACP audio-prompt deferral was waiting on. See
  [`docs/TRANSCRIBE.md`](TRANSCRIBE.md).
- **M34a** — **external documentation index**: config `docs: [{name, path}]`
  sources are indexed via the embeddings stack (a sibling of codebase search over
  a named directory). The read-only `search_docs` tool, the `@docs:<name>`
  reference, and the `docs [list|index|search]` subcommand let the agent ground
  answers in a library's docs, a style guide, or a spec — not just the workspace.
  See [`docs/DOCS.md`](DOCS.md).
- **M34b** — **session export**: the pure `jc_session_render` turns any saved
  session into a clean Markdown or self-contained HTML transcript (title +
  metadata, role-labeled turns, fenced tool calls/results). The read-only
  `export [id] [--html] [-o file]` subcommand (stdout-friendly) and the TUI
  `/export` make conversations shareable as PR write-ups, decision/handoff
  records, and teaching artifacts. See [`docs/EXPORT.md`](EXPORT.md).
- **M34c** — **conversational rewind**: `/rewind` (and the `rewind [n]
  [--dry-run]` subcommand) returns the workspace *and* the conversation to an
  earlier checkpoint's turn in one step — restoring files via the snapshot
  machinery and truncating history on a user-message boundary (the pure
  `jc_rewind_match` maps checkpoints to turns), then re-saving. The missing half
  of `/undo`. See [`docs/REWIND.md`](REWIND.md).
- **M34d** — **`ask_user`**: the agent can pause and put a focused clarifying
  question (with optional suggested answers) to the user instead of guessing,
  via the `app->ask` front-end delegate. The TUI prompts and blocks; headless /
  ACP / `--auto` leave it unset, so the tool returns a "proceed" note and an
  unattended run never hangs. See [`docs/ASK.md`](ASK.md).
- **M34e** — **`@problems` / `@folder` references**: `@problems` pulls current
  language-server diagnostics for the files touched this session into the turn;
  `@folder:<dir>` inlines a scoped repository map (the directory's source files +
  their top-level symbols, via a refactored `jc_repomap_build_dir`) for fast
  onboarding. Both ride the existing `@`-reference machinery. See
  [`docs/REFERENCES.md`](REFERENCES.md).
- **M35** — **Tier-3 polish**: `/cost` (a live session token + estimated-cost
  rollup), `/fork` (branch a new session from the current point via
  `jc_session_fork`, leaving the original resumable), and a **glossary**
  (`.jichi/glossary.md` + a global one) of domain terms injected into the system
  prompt so the agent speaks the project's vocabulary. See
  [`docs/GLOSSARY.md`](GLOSSARY.md).
- **M36** — **Emacs integration**: `editors/emacs/jichi.el` sends a marked region
  or the whole buffer to jichi and returns the answer into a side buffer, at
  point, appended, or replacing the region — read-only text commands plus a
  guarded agentic `jichi-task`. It drives the headless contract (no C changes) and
  ships with an offline ERT suite. See [`docs/EMACS.md`](EMACS.md).
- **M34f** — **completion notification**: opt-in terminal bell
  (`notifyBell`/`--bell`) and/or a `notify`/`--notify <cmd>` command run when a
  turn (TUI) or an `--auto` run finishes (with `$JICHI_NOTIFY`/`$JICHI_CWD` in the
  environment) — so you can step away from a long task and be pinged. See
  [`docs/NOTIFY.md`](NOTIFY.md).

## Read further

- [`ROADMAP.md`](ROADMAP.md): every milestone's design, evidence and decisions.
- [`../CHANGELOG.md`](../CHANGELOG.md): what changed for users, version by version.
- [`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md): how the project grew, with its numbers.
- [`DECISIONS.md`](DECISIONS.md): the decisions that shaped jichi, with the alternatives rejected.
- [`ANECDOTES.md`](ANECDOTES.md): the debugging stories worth remembering.
