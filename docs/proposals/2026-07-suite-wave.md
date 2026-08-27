# Design: the "suite" feature wave — toward a public learning/teaching/development agentic suite

**Status:** design-ready, for review (2026-07-13). Covers the 20-feature list toward
the first public release (August 2026, see `docs/ROADMAP.md` north star). Plan first;
implement in the recommended order. Milestones **M107+**.

## Vision & guiding principles

jichi becomes a *meaningful, helpful open-source agentic suite* for **learning,
teaching, self-teaching, and complex project development** on most Linux systems. The
C89/POSIX core stays deliberately portable so others can compile, port, or rewrite it
in modern C, C++, Zig, or Rust. Every feature below is designed to preserve the
existing invariants:

- **Portability:** C89 + libcurl + cJSON, *nothing else* in the core. Any new
  dependency (e.g. curses) must be **compile-time optional** and gated like `HAVE_CURL`.
- **Two-tier design:** a **pure, unit-tested core** + a **thin I/O shell** — the
  pattern behind every milestone.
- **Prompt-cache prefix stability (M31):** anything injected into the system prompt
  changes the cached prefix. New system-prompt content must be stable across turns
  (change only on an explicit event), or it defeats caching on cacheable backends.
- **Compaction-proof by construction (M73/M76):** the system prompt is never
  compacted. Durable state (rules, memory, glossary, **constraints**) belongs there —
  not in history, which compaction rewrites.
- **Enforcement over advice:** guidance the model *should* follow (skills, "suggested
  tools") has repeatedly failed to bind (ANECDOTES #3). Anything that *must* hold is
  enforced at the permission/envelope layer, not merely written into a prompt.
- **Works headless and interactive:** every capability must have a headless/agent
  surface (`-p`/`--auto`/ACP), not only a TUI one — the suite drives remote/automated
  agents too.

## Grounding facts (verified in the current tree, 2026-07-13)

- **No config write path exists.** `jc_config` is load-only (precedence:
  `--config`/`$JC_CONFIG`/`./local/config.json`/`~/.jichi`). Features #3/#15
  require a new **config serializer/writer** — the single biggest prerequisite.
- **Interrupt seam already present.** `on_sigint` sets `app->abort_flag`; the agent
  loop + `jc_http` honor it to abort a turn. The TUI just needs to *stop-and-return*
  rather than exit, plus a keypress path while streaming (#2).
- **Sessions:** `jc_session_meta {id,title,workspace,nmsgs,mtime}`, `jc_session_list`
  (newest-first), `jc_session_resolve_prefix`, `jc_session_load_recent_scoped`. No
  alias field yet (#7); list is newest-first so #5 is a display reversal.
- **System prompt** (`jc_sysmsg_build`) already injects rules → repo map → memory →
  glossary → skills catalog → output style. Constraints (#13) slot in here.
- **Setup/scaffold:** `jc_setup` preset table (8 roles) + `jc_scaffold` packs
  (`default`, `c-cli`, `zig-cli`, `python-cli`, `go-cli`, `rust-cli`, `godot`, `docs`,
  `systems-analysis`, `assignments`, `devops`, `data`). `jc_doctor` already checks LSP
  binaries on PATH. These are the seams for #8/#9/#11/#12.
- **Docs:** ~60 `docs/*.md` + `docs/presentations/` (incl. university/school) +
  `TUTORIAL_BEGINNER/ADVANCED`, `TEACHING_ASSIGNMENTS`, `REMOTE_SSH`. ~11 files mention
  Rust (#18). No localization structure yet (#1).

---

## Cross-cutting foundations (build these first)

### F0-a. Config serializer/writer — prerequisite for #3, #15, and parts of #9/#11/#12

A pure `jc_config_serialize(cfg) -> cJSON`/string and a thin
`jc_config_save(cfg, path)` (atomic temp-file + `rename`). Round-trips the in-memory
`jc_config` back to JSON at the resolved active path.

- **Pure core:** `jc_config_to_json` (models list + roles + routing + subsystem
  toggles + timeouts + permissions). Unit-tested for round-trip: `load → serialize →
  load` yields an equal config (`tests/test_config.c`).
- **Safety:** never serialize a literal `apiKey` back if it came from `apiKeyEnv`
  (preserve the env indirection; M55 lint already discourages literals). Write to the
  *project-local* or the file that was loaded, and confirm the target.
- **Risk:** JSON has no comments; a hand-commented config loses comments on rewrite.
  **Mitigation:** only rewrite when the user opts into config-editing; warn once that
  formatting/comments are normalized; keep a `.bak`.

### F0-b. Documentation localization structure — prerequisite for #1, and #20 reach

Establish `docs/i18n/<lang>/` (`de`, `en` stays at root as source of truth, `ja`, `es`,
`zh`) mirroring the English tree, plus a `docs/i18n/README.md` describing the
source→translation sync rule (English is canonical; translations note the source
commit they track). Don't translate all 60 docs at once — see #1 recommendation.

---

## Feature designs (grouped into implementation waves)

Each: **What → Seam → Core/shell → Surfaces → Tests → Risk → Recommendation.**

### Wave 1 — Quick UX wins (small, high-satisfaction, low-risk)

#### #2 — Interrupt / pause (stop the task, keep the session)
- **What:** a way to stop/pause the *current task* without exiting jichi — beyond a raw
  Ctrl+C that many users expect to quit.
- **Seam:** `app->abort_flag` (already aborts the turn) + the TUI loop + a keypress
  read during streaming (reuse the `on_progress` tick that drives the spinner).
- **Core/shell:** two-level semantics — **single Ctrl+C / `Esc`** = graceful stop
  (finish the in-flight tool, then stop with a `[stopped by user]` note, return to the
  prompt; reuse `last_run_capped`-style messaging); **double Ctrl+C within ~1s** =
  exit. During a run, poll stdin in the progress tick for the stop key. Headless:
  SIGINT = graceful stop of the current turn then print the partial result (like M80
  budget-keep); `--auto` already stops on SIGTERM.
- **Surfaces:** TUI (keys + a hint line), headless (signal + a `stop_reason:"interrupted"`
  in the jsonl `done` event — already exists).
- **Tests:** pure "graceful vs hard stop" decision helper; PTY e2e (`tests/e2e/`).
- **Risk:** the TUI isn't reading line input mid-stream; need a non-blocking key read.
  Bounded — the spinner already ticks.
- **Recommendation:** **build first.** High value, small, and it makes every later
  long-running feature safer to demo.

#### #5 — `/sessions` recent-last + `/sessions clear`
- **What:** show recent sessions **last** (no scroll to reach the newest), and a
  command to delete sessions.
- **Seam:** `print_sessions` (`jc_tui.c`), `jc_session_list` (newest-first).
- **Core/shell:** reverse the render order (newest at the bottom, nearest the prompt);
  add `jc_session_delete(id)` (unlink the file) + `/sessions clear [--all|<id>|--older <dur>]`
  with a confirm. Pure `jc_session_clear_plan` (which ids match a filter) unit-tested;
  the unlink is the thin shell.
- **Surfaces:** TUI `/sessions`, `/sessions clear`; CLI `sessions clear`.
- **Risk:** destructive — require confirmation; never touch checkpoints (separate).
- **Recommendation:** build with #6/#7 as one "session UX" milestone.

#### #6 — `/resume` last (bare = most recent)
- **What:** bare `/resume` resumes the most recent session for this workspace.
- **Seam:** `jc_session_load_recent_scoped` already exists; wire bare `/resume` (and
  bare `resume` subcommand) to it.
- **Recommendation:** trivial; fold into the session-UX milestone.

#### #7 — Session quick-find name (alias) + resume by it
- **What:** attach a short alias to a session; resume via the alias.
- **Seam:** add `alias` to the session JSON + `jc_session_meta`; `jc_session_find_by_alias`.
- **Core/shell:** pure alias match (exact, then unique-prefix, reusing the
  `jc_id_prefix_unique` pattern); `/name <alias>` (TUI) writes it; `/resume <alias>`
  and `resume <alias>` resolve alias → id (alias wins over id-prefix, or `@alias` to
  disambiguate). Back-compatible (absent alias = today's behavior).
- **Surfaces:** TUI `/name`, `/resume`, `/sessions` (show alias column); CLI `resume`.
- **Recommendation:** fold into the session-UX milestone.

#### #4 — Easter eggs (hidden commands, documented only in code)
- **What:** a few small delightful hidden TUI commands, absent from `/help` and docs,
  documented in code comments.
- **Seam:** TUI command dispatch (`jc_tui.c`); `/wisdom` already hints at the tone.
- **Core/shell:** e.g. a hidden `/credits` (lead developers + Continue/opencode
  credit), `/tea` (a gentle pause message), a Konami-style key sequence → a tiny ASCII
  flourish. Pure content; gated behind exact hidden strings; never in completion/help.
- **Risk:** must not collide with real commands or leak into `--output json`.
- **Recommendation:** tiny; add last in Wave 1 for morale. Keep tasteful + on-brand
  (learning/teaching).

### Wave 2 — Constraints (the flagship; the user's real pain)

#### #13 — Constraint capture, enforcement, and compaction-proofing
- **What:** identify rules/constraints/allows in user messages, document them, and
  **enforce** them immediately — so an agent *cannot* violate "do not run the build or
  tests," even after a failed compaction or a lost context window. The user has had
  agents "go wild" despite explicit instructions; advisory text is not enough.
- **Seam:** three layers, composing existing subsystems —
  1. **Extraction** — a pure `jc_constraint_scan(msg) -> [candidates]` (keyword/pattern
     classifier: "do not / never / don't / must not + <verb/tool>", "read-only",
     "don't touch <path>", "allow only <x>"). Conservative; emits typed candidates
     (`DENY_TOOL`, `DENY_CMD_PATTERN`, `READ_ONLY`, `PATH_FENCE`, `FREEFORM`).
  2. **Durable store** — active constraints persist to `.jichi/constraints.md` (like
     memory) and are injected by `jc_sysmsg_build` under `# Active constraints`
     (**system prompt ⇒ compaction-proof + re-sent every turn**, per M73). Stable text
     ⇒ cache-friendly (changes only when a constraint is added/removed).
  3. **Enforcement** — recognized constraints map to the **permission layer**
     (`jc_perm`) and/or the **envelope**: `DENY_TOOL run_tests` → `jc_perm` DENY;
     `DENY_CMD_PATTERN "build|make|zig build"` → a new command-pattern guard on
     `run_terminal_command` (refused as a backstop, message: "blocked by an active
     constraint"); `READ_ONLY` → the plan-mode fence; `PATH_FENCE` → edit-scope. This
     is the fix: **the model forgetting no longer matters** because the tool call is
     refused mechanically.
- **Core/shell:** `jc_constraint.{c,h}` — pure `jc_constraint_scan`,
  `jc_constraint_match_cmd(pattern, argv)`, `jc_constraint_render` (system-prompt
  block), `jc_constraint_to_perm` (constraint → `jc_approval`), all unit-tested; the
  file store + wiring into `jc_perm`/`run_terminal_command` is the shell.
- **Adoption model:** extraction is **propose-then-confirm** in the TUI ("I noticed a
  constraint: *don't run tests*. Enforce it? [y/N]"), **auto-adopt** in `--auto` when
  `constraints.autoAdopt` is on (default on for AUTO — the risky mode is where it's
  needed most). Explicit `/constraint add "no builds"` and config `constraints[]`
  always work regardless of extraction.
- **Rescope/relearn:** `/constraints` lists active ones; `/constraint clear` /
  `remove`; a constraint can be scoped to a path or duration; when the workspace or
  task changes the store is re-consulted (not silently dropped).
- **Surfaces:** TUI `/constraints` (list/add/remove); config `constraints`,
  `constraintsAutoAdopt`; CLI `constraints` subcommand; a `constraint` journal/telemetry
  event on every enforcement hit (so a driver sees "blocked N build attempts").
- **Tests:** heavy pure coverage of `scan` (true positives + must-not-false-positive
  on "should I run the build?"), `match_cmd`, `to_perm`; integration test that a denied
  command is refused; a compaction test that the constraint block survives a forced
  compaction (`tests/test_constraint.c`, `tests/test_compact.c`).
- **Risk:** false positives (blocking something the user wanted) — **mitigate** with
  conservative patterns, confirm-in-chat, and an easy `/constraint remove`; enforcement
  can only *narrow* (never widen) like hooks. Extraction NLP in C89 is keyword-based,
  not semantic — acceptable; the explicit-add path is always exact.
- **Recommendation:** **the flagship of this wave — highest value.** Build right after
  Wave 1. This is what makes autonomous runs trustworthy for teaching/classroom use.

### Wave 3 — Config mutation (built on F0-a)

#### #3 + #15 — Configure jichi from the TUI (and headless), reading + editing existing config
- **What:** view + edit config from the TUI and in agent/headless mode: start/stop
  telemetry, add models, change routing, edit rules/skills/commands. Read the existing
  config; recommend edits. Off by default.
- **Seam:** F0-a serializer + `jc_config` mutators + `jc_app_switch_model`/reload.
- **Core/shell:** a `/config` command tree (TUI) and a `config` subcommand (headless) —
  `config show`, `config telemetry on|off|full`, `config model add <name> …`,
  `config routing fast|strong <sel>|off`, `config get/set <key> <value>` for scalar
  toggles. Rules/skills/commands are *files* → `config edit rules|skills|commands`
  opens `$EDITOR` (TUI) or, in agent mode, routes an edit through the existing file
  tools within `.jichi/`. Each mutation → validate (reuse `jc_doctor`) → `jc_config_save`
  → hot-reload the affected subsystem. Gated by `configEditable` (default **off**);
  in AUTO, further gated so a runaway agent can't rewrite its own guardrails (constraints
  #13 interact: editing `permissions`/`constraints`/`editScope` from within a run is
  refused unless explicitly allowed).
- **Surfaces:** TUI `/config …`; CLI `config …`; a `config_change` telemetry event.
- **Tests:** serializer round-trip; each mutator pure where possible; e2e that
  `config telemetry on` then a run produces telemetry.
- **Risk:** an agent editing its own guardrails (see gating above); config corruption
  (atomic write + `.bak`); comment loss (warned).
- **Recommendation:** merge #3 and #15 into one "config mutation" milestone on top of
  F0-a. Ship **read/show + safe scalar toggles first**, then model/routing add, then
  file-editing.

### Wave 4 — Setup, onboarding & benchmarking (cohesive cluster)

#### #8 — Complete TUI guidance with language-server setup + scaffold + test
- **Seam:** `jc_setup` + `jc_scaffold` language packs + `jc_doctor` (already checks LSP
  on PATH).
- **Core/shell:** a pure `jc_lsp_suggest(lang) -> {server, install-hint}` table
  (clangd, zls, pyright/pylsp, gopls, rust-analyzer, guile/…); setup detects the
  project language (existing `jc_setup_lang_for_ext`), checks the server on PATH, and
  offers to write `lspServers`. Language packs' `config.example.json` gains the matching
  `lspServers` entry. Test: e2e that a scaffolded pack's config validates + doctor
  reports LSP status; unit test the suggestion table.
- **Recommendation:** build early in Wave 4 — it feeds #9/#11/#12.

#### #9 — Package browser + LLM-assisted recommendations
- **What:** in the TUI, list all available agents/skills/tool packages; show
  best-practice preset combinations; if an LLM is connected, let it recommend a set for
  the project; fallback = the defined presets.
- **Seam:** `jc_scaffold` packs + `jc_agentdef`/`jc_skill` discovery + `jc_setup`
  presets + the one-shot summarize-call pattern.
- **Core/shell:** pure `jc_packages_list` (enumerate packs/agents/skills/tools +
  descriptions) and `jc_preset_combos` (curated best-practice combinations). A
  `/packages` (or `/recommend`) command renders them; when a model is configured, one
  non-streaming call (`jc_app_model_for_role`) with the project's file summary +
  available packages → a recommended set (parsed defensively); on no model / failure,
  fall back to preset combos. Headless: a `recommend` subcommand emitting JSON.
- **Tests:** pure list/combos; the recommendation call reuses the tested one-shot path;
  a golden fallback test (no model → presets).
- **Recommendation:** medium; depends on #8 for LSP-aware suggestions.

#### #11 — Beginner-friendly setup (users + headless agents) + test/benchmark
#### #12 — Advanced setup (all options) + test/benchmark
- **Seam:** `jc_setup` preset table + #10 benchmark.
- **Core/shell:** two new preset *profiles* layered over existing role presets —
  **beginner** (minimal, guided, safe defaults: constraints on, `--lite`-friendly,
  snapshots on, verify gentle, plain output) and **advanced** (all subsystems, routing,
  parallel, MCP, LSP, telemetry full). Both drivable interactively and flag-only
  (`--profile beginner|advanced`), and testable/benchmarkable via #10.
- **Recommendation:** thin once #10 + #9 exist; merge #11+#12 as one "onboarding
  profiles" milestone.

#### #10 — Config benchmark
- **What:** benchmark/validate a project's configuration (agents, skills, tools,
  assignments, telemetry, and other features).
- **Seam:** `jc_doctor` (correctness) + `jc_telemetry` (economics) + a scored checklist.
- **Core/shell:** pure `jc_confbench_score(app) -> report` — a weighted checklist of
  best-practice features present/absent (models+roles, embed/rerank, snapshots, verify,
  constraints, LSP, skills/agents count, telemetry tier, prompt-cache pricing) + an
  optional **timed canned task** through the agent (latency/tokens/cost/tool-ok-rate)
  when `--live`. Offline scorer first; live timing opt-in. `benchmark` subcommand +
  `/benchmark` TUI.
- **Tests:** pure scorer against fixture configs; the live path reuses tested agent
  plumbing.
- **Recommendation:** build before #11/#12 (they consume it to prove a profile).

### Wave 5 — Safety & operations

#### #16 — Memory watchdog for build/test subprocesses
- **What:** run memory checks; pause/abort/kill a memory-hogging build/test process; a
  watch that informs a supervising agent / the user / a headless agent.
- **Seam:** `run_terminal_command`/`run_tests`/background commands (`jc_bg`) + the
  fork/exec path; reuse the M62 parallel-pool watchdog pattern (watch → SIGTERM →
  SIGKILL after grace).
- **Core/shell:** a pure `jc_memwatch_decision(rss, budget, grace) -> {ok|warn|kill}`;
  the shell polls `/proc/<pgid>/…` (or `getrusage` on reaped children) during a run,
  emits `on_status` + a `mem` telemetry event on `warn`, and kills the process group on
  `kill`. Config `memBudgetMb`, `--mem-budget`; off by default (0 = unlimited).
- **Surfaces:** TUI banner, headless status event, envelope journal entry.
- **Tests:** pure decision helper; an integration test with a small memory-growing
  child (bounded).
- **Risk:** `/proc` is Linux-specific — fine (Linux/POSIX-only project). RSS sampling is
  approximate; use a generous default + hysteresis.
- **Recommendation:** medium; valuable for classroom/shared machines where a runaway
  build hurts everyone.

#### #17 — Accessibility audit + support
- **What:** check accessibility for impaired users; recommend + implement support.
- **Seam:** color gating (`NO_COLOR`/`--color` already honored), the spinner/animation
  (`on_progress`), markdown renderer, key handling.
- **Core/shell:** deliver (a) an **audit doc** (`docs/ACCESSIBILITY.md`) covering
  screen-reader friendliness, color-only signaling, motion, contrast, keyboard-only
  operation; (b) a concrete **`--reduce-motion`/`accessible` mode** (config `accessible`)
  that disables spinners/animations, avoids color-only meaning (adds text labels to
  `✓/✗`, already has ASCII fallbacks), emits screen-reader-friendly linear output, and
  a plain prompt. Pure helpers where formatting decisions live; the rest is rendering
  flags.
- **Tests:** snapshot tests that accessible mode emits no ANSI motion/color-only cues.
- **Recommendation:** audit doc first (cheap, high-signal), then the mode. Pairs
  naturally with #19 (curses can offer a high-contrast, structured layout).

### Wave 6 — Documentation & reach

#### #18 — De-Rust the docs; use C/C++/Zig/Guile/Clojure/Elixir examples
- **What:** remove Rust *references/examples in prose docs*; use the portable-target
  languages instead.
- **Clarification/recommendation:** Rust remains a legitimate **port target** (the C89
  core exists so others can rewrite in modern C/C++/Zig/**Rust**), and the `rust-cli`
  scaffold pack is a shipped feature — **keep both**. The task is to stop *featuring
  Rust as the example language in documentation prose*. Audit the ~11 doc files; replace
  Rust snippets/examples with C/C++/Zig/Guile/Clojure/Elixir; keep the single "port
  targets include Rust" mention where it states the portability goal.
- **Core/shell:** a docs audit pass; no code. A `tests/`-style grep guard optional
  (fail CI if a doc gains a Rust *example* fence — low priority).
- **Recommendation:** cheap, do alongside #20.

#### #20 — Comprehensive documentation & tutorials for all workflows
- **What:** mentors, self-teaching, classroom, university (research + teaching),
  projects, multisite SSH remote agent-driven, and **one jichi supervising several local
  or remote jlus** in complex projects.
- **Seam:** many docs exist (`TEACHING_ASSIGNMENTS`, `TUTORIAL_*`, `REMOTE_SSH`,
  `LEARNING`, `presentations/university|school`, `DAEMON`, `PARALLEL`). Gap =
  cross-cutting *workflow* guides + an index.
- **Core/shell:** a `docs/WORKFLOWS.md` hub linking role-based journeys; new/expanded
  guides for **supervisor-of-many** (one jichi orchestrating remote jlus over SSH —
  builds on `REMOTE_SSH` + `spawn_parallel`/subagents + the daemon), **self-teaching
  loop**, **classroom setup**, **university research/teaching**. Draft with jichi itself
  (docs pack writers), human-reviewed.
- **Recommendation:** large; sequence after the features land so tutorials describe
  real behavior. Write the hub + supervisor-of-many first (highest novelty).

#### #1 — Localized docs (de/en/ja/es/zh)
- **What:** complete docs localized in five languages under subdirectories.
- **Recommendation (scope):** translating all ~60 docs × 5 languages and keeping them
  in sync is a large, ongoing maintenance burden that will drift. **Recommend a phased
  approach:** (1) establish `docs/i18n/<lang>/` structure (F0-b); (2) translate the
  **core onboarding set** first — `README`, `INSTALL`, `TUTORIAL_BEGINNER`, a
  getting-started, the setup wizard's user-facing strings — into all five languages;
  (3) mark deeper reference docs "English canonical" with a translation-welcome note.
  Draft translations with jichi (it has the models), **human-review each** (esp. ja/zh).
  A `docs/i18n/README.md` records which source commit each translation tracks.
- **Risk:** drift + review burden; mistranslation of safety-critical instructions.
  Mitigate by scoping to onboarding + canonical-English for reference.

### Wave 7 — Raw-TUI layout enhancements (DECIDED: no curses)

#### #19 — Richer raw-TUI layout (curses deferred)
- **Decision (2026-07-13):** *curses is deferred.* Rather than a second ncurses
  frontend (a new dependency + a whole frontend to maintain), invest the effort in the
  existing hand-rolled raw-mode TUI (`jc_term`/`jc_tui`) — **zero new dependencies**,
  one frontend, headless untouched.
- **What:** a richer layout in the raw TUI via ANSI — a persistent **status bar**
  (mode·model·ctx%·$cost, already partly present), an at-a-glance **indicator line**
  for active constraints (#13) / todos / background jobs, cleaner scrollback separation
  between turns, and (with #17) a high-contrast / reduce-motion presentation.
- **Seam:** `jc_term` (raw-mode redraw, already wrap-aware via `TIOCGWINSZ`) +
  `jc_tui` callbacks. No alternate frontend; enhance the render + a reserved status
  region.
- **Core/shell:** pure layout helpers (compose the status/indicator lines, width-aware
  truncation) are unit-testable; the ANSI draw is the shell. Reuse the shared
  markdown/syntax renderer.
- **Tests:** pure line-composition helpers unit-tested; PTY smoke test.
- **Risk:** a reserved status region with scrolling output needs careful cursor
  management (save/restore, scroll-region). Bounded; incremental.
- **Recommendation:** build **last**, and **pair it with #17** (accessibility) — the
  same render path serves both. Keep each enhancement small and independently revertible.

### #21 — Rename (just_c / just_code) — NOTE ONLY, do not change
- Recorded as a **deferred decision** for the public release. `just_code` reads well and
  the `jc_` prefix already matches. A rename touches the binary names, `JC_*` env vars,
  config paths (`~/.jichi`), man page, and every doc — a mechanical but wide
  sweep best done as a single dedicated milestone near release, with back-compat
  symlinks/aliases for the old names. **No changes now**; noted in ROADMAP.

---

## Recommended build order

| Order | Milestone | Feature(s) | Size | Why here |
|------|-----------|-----------|------|----------|
| 1 | M107 | #2 interrupt/pause | S | Safety for every later demo/run |
| 2 | M108 | #5/#6/#7 session UX (recent-last, clear, resume-last, alias) | S | Cohesive, low-risk, daily-use |
| 3 | M109 | #4 easter eggs | XS | Morale; tiny |
| 4 | M110 | **#13 constraints (capture + enforce + compaction-proof)** | L | **Flagship — the trust foundation** |
| 5 | M111 | F0-a config serializer | M | Prerequisite for config mutation |
| 6 | M112 | #3/#15 config mutation (TUI + headless) | L | Builds on M111 |
| 7 | M113 | #10 config benchmark | M | Feeds onboarding profiles |
| 8 | M114 | #8 LSP setup + scaffold + test | M | Feeds #9/#11/#12 |
| 9 | M115 | #9 package browser + LLM recommend | M | Onboarding richness |
| 10 | M116 | #11/#12 beginner + advanced profiles | M | Thin once M113–M115 exist |
| 11 | M117 | #16 memory watchdog | M | Ops safety |
| 12 | M118 | #17 accessibility (audit doc → mode) | M | Cheap audit, then mode |
| 13 | M119 | #18 de-Rust docs | S | Cheap doc pass |
| 14 | M120 | #20 workflow tutorials (hub + supervisor-of-many first) | L | After features land |
| 15 | M121 | #1 localization (structure + onboarding set) | L | Phased; after #20 hub |
| 16 | M122 | #19 raw-TUI layout enhancements (no curses) | M | Last; pairs with #118 |
| — | (rel.) | #21 rename | — | Deferred to a release milestone |

Rationale: **safety/UX quick wins → the constraint trust-foundation → config plumbing →
onboarding cluster → ops/accessibility → docs/reach → the big optional frontend.** Each
milestone is independently shippable, testable, and committable.

## Key recommendations (summary)

1. **Build #13 (constraints) early and enforce, don't advise.** It's the highest-value
   item and directly fixes "agents went wild despite being told." Enforcement lives in
   `jc_perm`/envelope; the durable text lives in the system prompt (compaction-proof).
2. **A config serializer (F0-a) is the gate for #3/#15** — it doesn't exist yet; build
   it as its own tested milestone before any config-editing UI.
3. **Merge** #3+#15 (config mutation), #11+#12 (onboarding profiles); **#8/#9/#10 form
   the onboarding cluster.**
4. **Scope #1 (localization) and #20 (comprehensive docs) to phases** — onboarding set
   + workflow hub first — to avoid an unmaintainable translation/doc sprawl.
5. **No new dependencies** — curses is deferred; #19 becomes raw-TUI enhancements.
6. **#18: keep Rust as a port target + the rust-cli pack; de-Rust only doc prose
   examples.**
7. **#21 rename: note only; do it as one dedicated release-time sweep with back-compat.**

## Decisions (resolved 2026-07-13)

1. **Constraint auto-adopt in AUTO: ON.** Extracted constraints are auto-enforced in
   `--auto` (where agents go wild, no human watching); the interactive TUI still
   proposes-then-confirms; explicit `/constraint add` always enforces.
2. **Curses: DEFERRED.** #19 is reframed as raw-TUI layout enhancements (no new
   dependency), paired with #17 accessibility. Revisit curses after release if wanted.
3. **Cadence: WAVE-BY-WAVE.** Implement a wave, commit each milestone, then report and
   let the user steer before the next wave.

## Standing defaults (my call unless you object)

- **Config editing from within a running agent (#3 headless):** allow non-guardrail
  edits, but hard-refuse mid-run edits to `permissions`/`constraints`/`editScope`.
- **Localization order** for the onboarding set: en (canonical) → de → es → ja → zh
  (German first alongside English, matching the JICHI context).
