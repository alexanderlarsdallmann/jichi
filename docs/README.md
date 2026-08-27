# The documentation map

138 pages sit beside this one, and they are not all for the same reader. This is
the routing table: **[Start here](#start-here)** if you have just arrived, the
group that matches your question otherwise.

**The project record ships in full, on purpose.** [`analysis/`](analysis/),
[`plans/`](plans/), [`proposals/`](proposals/), [`dialogues/`](dialogues/),
[`DECISIONS.md`](DECISIONS.md), [`DEFERRED.md`](DEFERRED.md) and
[`ANECDOTES.md`](ANECDOTES.md) contain every recorded failure, mis-diagnosis,
retraction and dead end. That is a feature and not residue: a project that
publishes only its successes teaches nothing about how software is actually
built, and the honest half is the part you cannot get anywhere else. If a page
tells you something went wrong, it is there because it did.

Two conventions worth knowing before you read any of it. **Numbers are measured
or they say they are not** — "not measured" appears in these pages more often
than an estimate does. And **`M###` is a milestone**, with its reasoning in
[`ROADMAP.md`](ROADMAP.md) and its user-visible effect in
[`../CHANGELOG.md`](../CHANGELOG.md).

**And a provenance note, which belongs at the front rather than in a footnote.**
These pages — over a million words of them — were written by a language model,
and **no native English speaker has reviewed them**. That caveat was for a long
time attached only to the Japanese and German translations, which was exactly
backwards: the languages nobody here can audit got flagged, and the one the
author writes in did not. What checks the English today is seven smoke lints that
verify *mechanical* claims — that a command-line option exists, that an advertised number
equals a counted one, that a source anchor still resolves — plus one non-native
reader. **No lint can check whether a sentence describing a design decision is
true**, and this repository's own record contains a run of seven fluent, confident
and false statements caught by measurement, by a lint, or by a user, but never by
re-reading. See
[`analysis/2026-08-24-trusting-generated-documentation.md`](analysis/2026-08-24-trusting-generated-documentation.md)
and [`ANECDOTES.md`](ANECDOTES.md) #73. Read accordingly: the prose that sounds
most settled is the prose most worth checking against the code.


## Start here

The shortest path from an empty terminal to a working session.

- [`BUILD.md`](BUILD.md) — Building jichi from source
- [`CJK.md`](CJK.md) — jichi in Japanese, Chinese and Korean
- [`READING_THE_STANDARD.md`](READING_THE_STANDARD.md) — Reading the C standard — a method, with jichi as the instrument
- [`DISTRIBUTED.md`](DISTRIBUTED.md) — jichi distributed: what works today, what does not, and what it would cost
- [`COMPARED.md`](COMPARED.md) — jichi compared: Continue, opencode, Claude Code
- [`CONFIG_TUTORIAL.md`](CONFIG_TUTORIAL.md) — Tutorial: configuring & using the model/agent options
- [`DOCTOR.md`](DOCTOR.md) — `doctor` — setup health check
- [`INSTALL.md`](INSTALL.md) — Installation & System Requirements
- [`LICENSING.md`](LICENSING.md) — Copyright and licensing — who owns jichi, and under what terms
- [`STATE.md`](STATE.md) — Where your state lives — config, secrets, caches, and what `make install` does not ship
- [`PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) — jichi in plain language
- [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) — Preparing your system and building jichi — the complete walkthrough
- [`SETUP_WIZARD.md`](SETUP_WIZARD.md) — Setup wizard (`jichi setup`)
- [`TUTORIAL_BEGINNER.md`](TUTORIAL_BEGINNER.md) — Beginner tutorial: your first jichi project
- [`VOCABULARY.md`](VOCABULARY.md) — Vocabulary — the words this project uses, defined before use
- [`WORKFLOWS.md`](WORKFLOWS.md) — Workflows — pick your journey


## Using jichi day to day

Modes, the things you can teach it, and what the terminal does.

- [`ACCESSIBILITY.md`](ACCESSIBILITY.md) — Accessibility
- [`AGENTS_GUIDE.md`](AGENTS_GUIDE.md) — Writing AGENTS.md — teaching jichi your project
- [`AGENT_MODES.md`](AGENT_MODES.md) — Agent Modes & Bounded Autonomy
- [`ASK.md`](ASK.md) — Asking the user (`ask_user`)
- [`BOARD.md`](BOARD.md) — Kanban phase board (`board`)
- [`COMMANDS.md`](COMMANDS.md) — Custom slash commands
- [`CONSTRAINTS.md`](CONSTRAINTS.md) — Constraints — captured and enforced (M110)
- [`DESIGN_INPUT.md`](DESIGN_INPUT.md) — Design input (`--design` / `--spec`)
- [`ENCODING.md`](ENCODING.md) — Text encoding & internationalization
- [`GLOSSARY.md`](GLOSSARY.md) — Glossary
- [`LANGUAGE.md`](LANGUAGE.md) — Natural language (answers + UI)
- [`MEMORY.md`](MEMORY.md) — Persistent agent memory
- [`NOTIFY.md`](NOTIFY.md) — Completion notification
- [`OUTPUT_STYLES.md`](OUTPUT_STYLES.md) — Output styles (M28)
- [`REFERENCES.md`](REFERENCES.md) — References (@-context)
- [`RULES.md`](RULES.md) — Project rules (AGENTS.md)
- [`SKILLS.md`](SKILLS.md) — Agent skills
- [`TUI_RENDER.md`](TUI_RENDER.md) — TUI rendering: markdown, syntax, and the reply header
- [`TUTORIAL_ADVANCED.md`](TUTORIAL_ADVANCED.md) — Advanced tutorial: power use
- [`TYPE_AHEAD.md`](TYPE_AHEAD.md) — Type-ahead: typing while jichi works (M254, opt-in since M257)
- [`VOICE.md`](VOICE.md) — Voice: speaking and listening


## Models, providers and cost

Which model, pointed where, and what each turn actually spends.

- [`CHOOSING_A_MODEL.md`](CHOOSING_A_MODEL.md) — Choosing a model — tiny, small, or large, and the question you must ask it first
- [`COMPACTION.md`](COMPACTION.md) — Auto-compaction
- [`CONVERT.md`](CONVERT.md) — Converting Continue and opencode configs
- [`GATEWAY_ADMIN.md`](GATEWAY_ADMIN.md) — Configuring an LLM gateway for agentic coding tools
- [`LOCAL_MODELS.md`](LOCAL_MODELS.md) — Local models: the offline study machine
- [`MODELS.md`](MODELS.md) — Models, servers & fallback
- [`PROMPT_CACHING.md`](PROMPT_CACHING.md) — Prompt caching
- [`ROUTING.md`](ROUTING.md) — Tiered model routing
- [`TOOL_OUTPUT_COST.md`](TOOL_OUTPUT_COST.md) — What tool output costs, and how to spend less


## Tools the agent can call

Every capability jichi exposes to the model, and how to add your own.

- [`BACKGROUND.md`](BACKGROUND.md) — Background commands (M26)
- [`DOCS.md`](DOCS.md) — External documentation index (`search_docs` / `@docs`)
- [`EDITING.md`](EDITING.md) — Editing: `apply_patch` and diff preview
- [`FORMATTING.md`](FORMATTING.md) — Formatting: `format_file` and `formatCommand` (M263)
- [`GIT.md`](GIT.md) — Git tools & self-review
- [`HOOKS.md`](HOOKS.md) — Lifecycle hooks (M25)
- [`LSP.md`](LSP.md) — LSP diagnostics & code navigation
- [`MCP.md`](MCP.md) — MCP servers — giving the agent tools jichi does not have
- [`MEDIA_GEN.md`](MEDIA_GEN.md) — Media generation (`generate_image` / `generate_audio`)
- [`ML_SUPPORT.md`](ML_SUPPORT.md) — Machine learning with jichi: what ships, what doesn't, how to extend
- [`MUSIC.md`](MUSIC.md) — Music development with jichi
- [`RAG.md`](RAG.md) — Retrieval-augmented generation (RAG)
- [`REPOMAP.md`](REPOMAP.md) — Repository map
- [`ROBOTICS.md`](ROBOTICS.md) — Robotics: driving sensors, actuators, and fleets with jichi
- [`ROBOTICS_BRINGLIST.md`](ROBOTICS_BRINGLIST.md) — Robotics bring-list, and the order to do things in
- [`SCAFFOLDING.md`](SCAFFOLDING.md) — Project scaffolding (`init`)
- [`SOUND.md`](SOUND.md) — Sound I/O: play_audio and record_audio (M163b)
- [`TESTING.md`](TESTING.md) — Structured test integration
- [`TRANSCRIBE.md`](TRANSCRIBE.md) — Audio transcription (`transcribe_audio`)
- [`USER_TOOLS.md`](USER_TOOLS.md) — User-defined tools
- [`VISION.md`](VISION.md) — Vision input (M29)
- [`WEBSEARCH.md`](WEBSEARCH.md) — Web search (M27)


## Autonomy, safety and what it leaves behind

Running jichi unattended, and being able to say afterwards what it did.

- [`AGENT_COLLABORATION.md`](AGENT_COLLABORATION.md) — Humans and agents, agents and agents: the collaboration map
- [`AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md) — Autonomous loops: running jichi unattended over a set of tasks
- [`AUTONOMY.md`](AUTONOMY.md) — Autonomy envelope
- [`CONTROL.md`](CONTROL.md) — The mid-run control channel (`--control`, M159)
- [`DAEMON.md`](DAEMON.md) — Daemon (warm process)
- [`GATE_INTEGRITY.md`](GATE_INTEGRITY.md) — Gate integrity: protecting the instrument, not just the product
- [`HARDENING.md`](HARDENING.md) — Hardening & extension (M130–M134)
- [`LEARNING.md`](LEARNING.md) — Learning loop (mentor)
- [`OBSERVABILITY.md`](OBSERVABILITY.md) — Observability: what an autonomous jichi leaves behind, and how to read it
- [`PARALLEL.md`](PARALLEL.md) — Parallel agent swarm
- [`REWIND.md`](REWIND.md) — Conversational rewind (`/rewind` / `rewind`)
- [`SNAPSHOTS.md`](SNAPSHOTS.md) — Git snapshots & undo
- [`STRESS_TESTING.md`](STRESS_TESTING.md) — Stress-testing an LLM server with jichi fleets
- [`SUBAGENTS.md`](SUBAGENTS.md) — Subagents (`spawn_subagent`)
- [`SUPERVISOR_OF_MANY.md`](SUPERVISOR_OF_MANY.md) — One jichi supervising many
- [`TELEMETRY.md`](TELEMETRY.md) — Telemetry / event logging
- [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md) — Testing the tests
- [`TEST_TIERS.md`](TEST_TIERS.md) — The project's own test tiers — what each holds, and the incidents that shaped them
- [`TOOL_DECISIONS.md`](TOOL_DECISIONS.md) — What can it do to my machine, and who decides — the six mechanisms in order
- [`TUTORIAL_ORCHESTRATION.md`](TUTORIAL_ORCHESTRATION.md) — Tutorial: delegating to sub-agents — one, many, and when not to


## Driving jichi from something else

Scripts, editors, other machines, other front ends.

- [`ACP.md`](ACP.md) — ACP server
- [`AUTOCOMPLETE.md`](AUTOCOMPLETE.md) — Autocomplete
- [`DEPLOYMENT.md`](DEPLOYMENT.md) — Deployment: embedded devices, SSH, TUI & headless — for users and agents
- [`EDITORS.md`](EDITORS.md) — Editor integrations
- [`EMACS.md`](EMACS.md) — Emacs integration (`jichi.el`)
- [`EMBEDDING.md`](EMBEDDING.md) — Embedding jichi as a component
- [`EXPORT.md`](EXPORT.md) — Session export (`export` / `/export`)
- [`JUPYTERHUB.md`](JUPYTERHUB.md) — jichi under JupyterHub — what was measured, what was not, and how to decide
- [`REMOTE_SSH.md`](REMOTE_SSH.md) — Driving jichi on a remote server over SSH (headless)
- [`SCRIPTING.md`](SCRIPTING.md) — Scripting & pipes
- [`TMUX.md`](TMUX.md) — Running jichi under tmux (on remote servers)
- [`VIM.md`](VIM.md) — Vim / Neovim integration
- [`WEB_FRONTEND.md`](WEB_FRONTEND.md) — Web front-end


## Platforms and building

What has actually been compiled and run, and what it costs to build.

- [`CPP_BUILD.md`](CPP_BUILD.md) — Compiling jichi with a C++ compiler — and what it buys you, honestly
- [`C_STANDARDS.md`](C_STANDARDS.md) — C89/90 vs modern C — and what portability really costs
- [`LOW_MEMORY.md`](LOW_MEMORY.md) — Running jichi on low-RAM / embedded systems
- [`MIGRATION.md`](MIGRATION.md) — Migrating from `jlu_continue` to `jichi`
- [`PLATFORMS.md`](PLATFORMS.md) — Platforms — what has actually been compiled and run
- [`PORTING_WINDOWS.md`](PORTING_WINDOWS.md) — jichi on Windows: where POSIX ends
- [`ZIG_BUILD.md`](ZIG_BUILD.md) — Compiling jichi with the Zig compiler — findings, honestly


## Learning and teaching

The curriculum, the reading guides, and the craft tutorials.

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — jichi's own architecture, layer by layer: the
  reference half of what `CLAUDE.md` used to carry (moved at M516, because 77% of that
  file could not reach a model)
- [`ARCHITECTURE_TUTORIAL.md`](ARCHITECTURE_TUTORIAL.md) — System architecture, and how to show it — a tutorial
- [`ASSIGNMENTS.md`](ASSIGNMENTS.md) — Assignments — practising the whole software lifecycle
- [`BENCH_LOCAL_GPU.md`](BENCH_LOCAL_GPU.md) — The local-GPU bench: measuring jichi against a small model on your own hardware
- [`CURRICULUM.md`](CURRICULUM.md) — The curriculum — learning software development with an agent at your side
- [`DOMAIN_MODELLING_TUTORIAL.md`](DOMAIN_MODELLING_TUTORIAL.md) — Domain modelling — a tutorial
- [`ORG_MODE.md`](ORG_MODE.md) — Emacs org-mode for a software project
- [`PROJECT_RECORDS.md`](PROJECT_RECORDS.md) — Keeping a project's records in plain text
- [`PSEUDOCODE_TUTORIAL.md`](PSEUDOCODE_TUTORIAL.md) — Pseudocode — writing it, and turning it into real code — a tutorial
- [`READING_OPEN_SOURCE.md`](READING_OPEN_SOURCE.md) — Reading open-source C — with the agent as your reading partner
- [`SDLC.md`](SDLC.md) — The software development lifecycle in jichi: five journeys
- [`TEACHING.md`](TEACHING.md) — Teaching with jichi: walk it before you assign it — the teacher's progression
- [`TEACHING_ASSIGNMENTS.md`](TEACHING_ASSIGNMENTS.md) — Teaching with jichi assignments
- [`TESTING_RUNBOOK.md`](TESTING_RUNBOOK.md) — The ten-step procedure for adding a test, each step naming the failure it prevents
- [`TESTING_TUTORIAL.md`](TESTING_TUTORIAL.md) — Writing tests, running them, and reading the result — a tutorial
- [`UML_TUTORIAL.md`](UML_TUTORIAL.md) — UML as mermaid — which diagram answers which question — a tutorial
- [`USE_CASE_TUTORIAL.md`](USE_CASE_TUTORIAL.md) — Writing use cases — a tutorial


## Other languages, by contrast

Reading tracks that teach a paradigm by comparing it with jichi's own C.

- [`CLOJURE_PARADIGM.md`](CLOJURE_PARADIGM.md) — Clojure by contrast — a Lisp on a giant, and immutability without the copy
- [`CPP_INTEROP.md`](CPP_INTEROP.md) — C → C++, gradually — the migration track
- [`ELIXIR_PARADIGM.md`](ELIXIR_PARADIGM.md) — Elixir by contrast — the actor model, and jichi's fork pool by another name
- [`GUILE_PARADIGM.md`](GUILE_PARADIGM.md) — Guile by contrast — Scheme, and the extension-language seam jichi didn't take
- [`HASKELL_PARADIGM.md`](HASKELL_PARADIGM.md) — Haskell by contrast — when the type checker enforces jichi's discipline
- [`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md) — When the model speaks an older dialect of your language
- [`PYTHON_AND_C.md`](PYTHON_AND_C.md) — Python and C — where the dynamic language meets the systems language (and why jichi left it)
- [`RACKET_PARADIGM.md`](RACKET_PARADIGM.md) — Racket by contrast — functional programming for someone who reads jichi
- [`RUST_INTEROP.md`](RUST_INTEROP.md) — C and Rust — the clean-boundary track (why this one is different)
- [`ZIG_INTEROP.md`](ZIG_INTEROP.md) — C → Zig, gradually — the migration track
- [`ZIG_REWRITE_ANALYSIS.md`](ZIG_REWRITE_ANALYSIS.md) — Would rewriting jichi in Zig pay off?


## The project record

Why jichi is the way it is, including everything that went wrong. This ships on purpose.

- [`ANECDOTES.md`](ANECDOTES.md) — Anecdotes & debugging war stories
- [`APPROACH.md`](APPROACH.md) — How jichi approaches building software
- [`DECISIONS.md`](DECISIONS.md) — Decision register
- [`DEFERRED.md`](DEFERRED.md) — Deferred register
- [`DEFERRED_LOCAL_GPU.md`](DEFERRED_LOCAL_GPU.md) — Deferred until a local-model + GPU bench
- [`DOC_REVIEW.md`](DOC_REVIEW.md) — Reviewing the documentation — the rubric, and how to run a pass
- [`DRIVING.md`](DRIVING.md) — Driving jichi at a real project: what 28 runs measured
- [`JOURNEY.md`](JOURNEY.md) — The journey — from first step to master's rest
- [`NOTICES.md`](NOTICES.md) — The bracketed tags — the registry
- [`PHILOSOPHY.md`](PHILOSOPHY.md) — The philosophy of jichi
- [`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md) — jichi — project timeline, development & testing retrospective
- [`ROADMAP.md`](ROADMAP.md) — Roadmap
- [`SELF_IMPROVEMENT.md`](SELF_IMPROVEMENT.md) — Self-improvement & runtime band (M100+) — design & reference
- [`SESSION_RUNBOOK.md`](SESSION_RUNBOOK.md) — Session runbook — the order of execution, and why each step is in it


## Beyond this directory

| Where | What is in it |
|---|---|
| [`analysis/`](analysis/) | Dated post-mortems and measurements. Read one when a page cites it; each says what was measured and what was not. |
| [`plans/`](plans/) | Designs written before the work, kept afterwards so the prediction can be compared with the outcome. |
| [`proposals/`](proposals/) | Designs not yet built, each stating its own discard criteria. |
| [`dialogues/`](dialogues/) | Conversations that decided something. |
| [`curriculum/`](curriculum/) · [`assignments/`](assignments/) | The self-paced course and its graded tasks. Start at [`CURRICULUM.md`](CURRICULUM.md). |
| [`rules/`](rules/) | Per-task rules files, loaded by a config's `instructions` list rather than by the directory walk — the small, task-shaped half of the rules split (M516). |
| [`reading/`](reading/) | Four reading guides. Three are about the **source**: **ANNAI** (案内, beginner), **FUKABORI** (深掘り, expert), **TSUISEKI** (追跡, recorded runs traced back to the code — artifacts in [`reading/traces/`](reading/traces/)). The fourth is about the **record**: **KIROKU** (記録) — how to read 6.8 MB of project history without drowning, and the nine recurring shapes that make it a taxonomy rather than an archive. |
| [`case-studies/`](case-studies/) | Worked examples of jichi driven at a real codebase. |
| [`presentations/`](presentations/) | Marp slide decks. |
| [`licenses/`](licenses/) | **Candidate** licence texts, and the checksums that keep them verbatim. Nothing there grants anything — see [`LICENSING.md`](LICENSING.md). |
| [`i18n/`](i18n/) | Translations. English is the canonical text; see [`i18n/README.md`](i18n/README.md) for what that means. |

Outside `docs/`: [`../README.md`](../README.md) (the front page),
[`../CHANGELOG.md`](../CHANGELOG.md), [`../CONTRIBUTING.md`](../CONTRIBUTING.md),
[`../SECURITY.md`](../SECURITY.md), and [`../CLAUDE.md`](../CLAUDE.md) — the
agent-facing project instructions, which double as the densest architecture
summary that exists.
