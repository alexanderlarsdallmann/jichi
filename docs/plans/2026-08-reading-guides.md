# Plan: the jichi source reading guides — two editions

*Status: COMPLETE — M222–M225 all shipped (skeleton + lint, Annai 1–10 +
Appendix A, Fukabori 1–12). This document is the executed design.
Companion surfaces it will join: [CURRICULUM.md](../CURRICULUM.md) (the
guides are supplementary reading for every track),
[READING_OPEN_SOURCE.md](../READING_OPEN_SOURCE.md) (which names jichi's own
source "the first text" — these guides are that text's companion),
[C_STANDARDS.md](../C_STANDARDS.md), [ZIG_INTEROP.md](../ZIG_INTEROP.md),
[CPP_INTEROP.md](../CPP_INTEROP.md).*

## 1. What is being built, and for whom

Two complete reading guides over the jichi C89 source (~60k lines across
`src/`) **and its test suite** (114 unit files + the smoke/e2e/bench/measure
tiers) — same codebase, two very different readers:

| | **案内（あんない） *Annai*** — the guided tour | **深掘り（ふかぼり） *Fukabori*** — the deep dive |
|---|---|---|
| audience | junior developers, beginners (curriculum Stage 0–2) | advanced developers, experts (Stage 3 and beyond) |
| organizing principle | **one turn, end to end** — follow a single user request through the system | **one decision at a time** — each chapter is an architectural decision and its consequences |
| unit of explanation | pseudo code first, then the real C, then "what would break if…" | invariants first, then the code that carries them, then the failure that taught them |
| C literacy assumed | little: every C idiom gets a sidebar the first time it appears | full: idioms unremarked, trade-offs argued |
| AI literacy assumed | none: tokens, context, reasoning, tool calling built from zero | working: the chapters go to wire formats, caching economics, small-model failure modes |
| length target | ~10 chapters, 150–250 lines each | ~12 chapters, 200–350 lines each |

Both are **reading** guides, not reference manuals: each chapter tells the
reader which files to open, in which order, what question to hold while
reading, and ends with something to *do* (an experiment, a `/context` or
telemetry observation, a curriculum task). They must never duplicate what
`docs/` already says — they **route** into ROADMAP/analysis/ANECDOTES
entries instead of restating them (maintenance argument in §7).

## 2. Placement and shape

- `docs/reading/` directory (the `docs/curriculum/` precedent: chaptered
  files + an index), two subtrees by prefix — Japanese stage names as the
  titles, the English working titles kept as subtitles (decided
  2026-08-01), romaji in filenames (the curriculum's convention: Japanese
  in prose, ASCII on disk):
  - `docs/reading/ANNAI.md` — "案内（あんない） *Annai* — the guided tour"; chapters
    `annai-01-*.md` … `annai-10-*.md` + appendix `annai-a-*.md`.
  - `docs/reading/FUKABORI.md` — "深掘り（ふかぼり） *Fukabori* — the deep dive";
    chapters `fukabori-01-*.md` … `fukabori-12-*.md`.
- Every chapter carries the same skeleton: *Why this exists* (the design
  decision, with its ROADMAP/analysis citation) → *The shape* (one mermaid
  diagram) → *The idea* (pseudo code) → *The C* (annotated excerpts with
  `file.c:function` anchors, never line numbers — see §7) → *Prove it to
  yourself* (an experiment or curriculum task) → *Where this bit us* (the
  ANECDOTES cross-reference, when one exists).
- English canonical, same i18n policy as everything else.

## 3. 案内（あんない） Annai (the guided tour) — chapter map (beginner edition)

The spine is narrative: *you type a sentence; a file changes; how?*

1. **What you are holding** — what an AI coding agent *is*; the honest
   framing that jichi was itself built with agent support and documents
   that history; how to build it and poke it (`make`, `doctor`, `-p`).
   First AI sidebar: what a language model does with text.
2. **A turn, from the outside** — drive one `-p` turn with `--output
   jsonl` and read the event stream before reading any C. Mermaid: the
   request/response flow from CLAUDE.md, redrawn gently. AI sidebar:
   tokens, the context window, why the model must be *sent* everything it
   "knows".
3. **main() to the loop** — `src/main.c` (setup only), `jc_app` as "the
   world", `jc_agent_run_turn`. C sidebars: opaque structs, `jc_status`
   and returns-by-pointer, why no exceptions.
4. **Tool calling, the whole idea** — the heart of the beginner guide and
   of "what reasoning and tool calling mean": the model cannot touch your
   disk; it can only *ask*. Schemas as the menu, `jc_tool_execute` as the
   waiter, results as new conversation text, the loop as the meal. Pseudo
   code of `run_agent_loop`'s tool round; mermaid sequence diagram
   user→model→tool→model. AI sidebar: reasoning = intermediate text the
   model produces for itself; a "call" is just structured output the
   program promises to honor — and what jichi does when the model narrates
   a call as prose instead (M147) or invents a tool name (M91/M219).
5. **One tool, all the way down** — `read_file` from schema to `cat -n`
   gutter; then `edit_file` and the read-before-edit guard. C sidebars:
   function-pointer tables (the registry), bounded buffers, `jc_sb`.
6. **The wire** — `jc_http` + `jc_sse`: what streaming is, why the answer
   arrives in pieces, what a provider is (`jc_provider` vtable — the
   "same conversation, two dialects" picture). C sidebar: callbacks.
7. **Memory, the jichi way** — the three arenas as *lifetimes you can
   point at*; `/context` as the gauge. Routes into curriculum set D
   (tasks 20–22) as the exercises. C sidebar: why `malloc/free` isn't the
   whole story (the high-water lesson, gently).
8. **When the conversation gets too long** — compaction for beginners:
   the context window as a whiteboard that fills up; summarize-the-old,
   elide-the-bulky; what the `[elided]` markers in a session file are.
9. **How jichi knows it works** — the test suite as a *reading* subject:
   `tests/test_*.c` pure cores, the smoke tier's TAP drivers, "a new test
   must be shown to fail" as a philosophy, two-sided graders (route into
   assignment 09/22). This chapter is the curriculum's Module 3/5 made
   concrete on a real suite.
10. **Where to go next** — the curriculum tracks, the Fukabori, and how
    to make a first real change with the agent's help (a guided
    good-first-issue walk using jichi *on* jichi).

Baseline assumption (decided 2026-08-01): the reader has **built jichi**
(Module 0 done) — every chapter's experiments use the living binary.
**Appendix A — reading without a bench** (`annai-a-no-bench.md`): for the
read-only visitor (a repository browser after the public release, a
reader on a locked-down machine), each chapter's *Prove it to yourself*
gets a read-only twin — a captured jsonl transcript to read instead of
producing one, a committed telemetry excerpt instead of a live gauge, and
"what you would have seen" notes. Feasible cheaply because the artifacts
already exist in-repo (docs/analysis fixtures, session exports); the
appendix routes to them rather than duplicating them.

## 4. 深掘り（ふかぼり） Fukabori (the deep dive) — chapter map (expert edition)

The spine is decisions; each chapter defends one against its alternatives.

1. **Why C89, and what it cost** — the full argument (§5 below is its
   outline); the compiled decision record (`C_STANDARDS.md`, CONTRIBUTING
   rules, the probe pattern for anything the libc might not have).
2. **The provider abstraction** — one vtable, two wire dialects; the
   placeholder wire invariant (M166) and why the agent never branches on
   provider; prompt-cache prefix stability (M31) as a *byte-level* design
   constraint on everything that builds requests; `schema_probe` and the
   small-model bench as the empirical check (ANECDOTES #19/#20).
3. **The three-arena lifetime model** — the full M197–M199 story as
   architecture: why "reset per X" beats ownership bookkeeping in C, the
   invariant that a spawning tool must not hold tool-scratch across a
   nested run, the lint that enforces the discipline, and the M218
   residue (malloc high-water, shrink-on-reset) that arenas *cannot* see.
4. **The agent loop as a state machine** — `run_agent_loop` in full:
   verdicts (mode × permissions × MCP), the below-the-verdict gates
   (privileged M153, kinetic M163), hooks, budgets, checkpoints,
   self-review, the control-channel boundary. Mermaid: the per-tool-call
   decision ladder, complete.
5. **Context economics** — estimation (bytes/4), calibration (M77),
   between-turn vs mid-turn compaction, superseded-read dedup, argument
   elision (M218), system-prompt fitting; the arithmetic that decides
   what the model forgets, and its telemetry.
6. **The autonomy envelope** — budgets, edit scope, verify gates,
   fix-forward vs rollback (M80/M207's assumed-green lesson), the
   out-of-scope diff, hollow-gate sanity (M86); why detection is default
   and prevention opt-in.
7. **Fork-based parallelism** — spawn_parallel's pool, COW arenas,
   worktree isolation, first-wins merges, watchdogs and SIGTERM
   escalation; why processes and not threads, and what that costs on
   small RAM (the LOW_MEMORY view).
8. **Streaming and the no-buffering invariants** — SSE framing, body
   ownership transfer (M20e), the retry ladder and its determinism story
   (JC_FAULT_NET), UTF-8 sanitation as a chokepoint (M191).
9. **Sessions, snapshots, and the two histories** — conversation
   persistence vs the shadow git repo; rewind's label matching; why
   observability lives *outside* the rollback blast radius (ANECDOTES #1).
10. **The test architecture as a system** — the tiers and their
    *reasons*: pure cores offline, the Python-free smoke port (M209–M217,
    one-driver-one-tier), e2e residual, bench-as-measurement, the
    measure/ harnesses; TEST_INTEGRITY.md's failures; lints over audits;
    fixture proportionality; the two-sided grader discipline. Includes
    the *reading order for the suite itself*.
11. **AI-supported coding, examined** — the expert cut: what the
    reasoning trace does and does not warrant; tool-call schemas as an
    interface-design problem (what small models get wrong); telemetry →
    insight → lesson loops (M70/M78); the dogfood record as evidence of
    which supervision styles worked (route into docs/dialogues/,
    ANECDOTES). This chapter is honest about failure: the anecdotes where
    the agent's confidence outran the code.
12. **The migration road** — §6's argument in full: modern C, Zig, or
    C++ at the seams; when a Rust (or other) rewrite is the right call
    anyway; jichi's own tracks as the training ground; a closing
    recommendation table by project situation.

## 5. The "why C, not C++ or Zig from the start" argument (outline)

To be written honestly — a decision defense, not language advocacy:

- **Reach is the mission.** The design targets (LOW_MEMORY tiers,
  decade-old boxes, uClibc/musl, the hardware plan's tier B) make "every
  compiler that exists accepts it" a feature requirement, not taste. C89
  is the only candidate all three languages' toolchains *agree on* —
  including `zig cc` and `g++` themselves (CPP_BUILD.md/ZIG_BUILD.md are
  the proofs, kept in-repo on purpose).
- **One idiom, auditable by anyone.** A security-relevant agent that runs
  shell commands wants the *smallest reviewable language surface*; C89
  with house rules is brutally uniform. C++ would fragment reviewers
  across idiom eras; Zig (pre-1.0) moves under a long-lived codebase
  (the fixture-pinning note in ZIG_INTEROP.md is the small-scale taste).
- **The curriculum coupling.** The project doubles as teaching material;
  C89's constraints make invisible things visible (lifetimes, bounds,
  formatting) — the pedagogy chapters lean on this.
- **Honesty clause:** C++ or Zig *would have worked*. The guide must say
  so, and say what each would have bought (RAII against the M197 bug
  class; checked builds against the UB class) — and then show the ledger:
  those bug classes were instead covered by arenas + lints + sanitizers
  + the probe pattern, at the cost of building that machinery ourselves.
  That trade — language guarantees vs owned machinery — is the chapter's
  actual lesson, because it is the trade every C project lives.

## 6. The "gradual refactor is the industry's road" argument (outline)

- **The install base cannot stop shipping.** Kernels, embedded firmware,
  infrastructure daemons, language runtimes — the world's C does not get
  feature freezes for rewrites. Any migration that requires one is
  fiction for most teams; seam-by-seam migration behind stable C headers
  keeps **every intermediate state shippable** (the tracks' arc,
  compile → extend → refactor, is exactly this discipline in miniature).
- **Why modern C / Zig / C++ are the likely destinations:** all three
  consume C headers *natively* — the seam costs one `extern "C"` or one
  `zig build-obj`, no marshalling layer, no ownership-model translation,
  bidirectional calls for free. Migration can proceed function-by-function
  with the linker as the referee, and can *stop* anywhere and still have
  improved the project (modern C alone buys bounds-checked interfaces,
  `stdint`, clearer initialization).
- **Why a whole-program rewrite is the rarer path, honestly argued —
  generalized (decided 2026-08-01), with Rust as the prominent example:**
  the argument is structural, not about any one language. A language
  whose central benefit spans the *whole program graph* — Rust's
  ownership, a GC runtime's memory model (Go, Java, C#), a managed VM's
  scheduler — loses exactly that benefit at a piecewise boundary: every
  seam re-states the contract by hand (`unsafe` FFI, cgo/JNI marshalling,
  duplicated ownership rules), so incremental migration pays the new
  language's costs without its central payoff until late in the
  migration. Hence the observed industry pattern, across all of them:
  such languages succeed as **new components at clean boundaries**
  (kernel drivers, new services, parsers behind narrow APIs) and in
  **genuine rewrites of small, well-specified programs** — and struggle
  as in-place refactors of a large, living C tree. Modern C, Zig, and
  C++ are the exception *because* their benefit does not require
  crossing the graph: they meet C at the header, seam by seam. The
  chapter must carry the counterexamples fairly (component-wise browser
  oxidation with major investment; successful full rewrites of small
  tools) and the cases where the clean-boundary rewrite *is* the
  recommendation (table below).
- **Recommendation table** (to close both the chapter and track pages):

  | situation | likely right move |
  |---|---|
  | living C tree, small team, must keep shipping | modern-C hygiene first; Zig or C++ at the seams next |
  | hermetic builds / cross-compiling pain dominates | Zig track (toolchain first, language later) |
  | team already fluent in C++, ecosystem needs libraries | C++ track, with the rename-trap discipline |
  | new security-critical component at a clean boundary | build it beside the C in a memory-safe language (Rust the usual pick; the argument admits any) |
  | small, well-specified tool + freedom to freeze | a rewrite is honestly on the table — in any of them |

## 7. Maintenance: how the guides stay true (the lint, first)

Prose about code rots; the project's rule is **prefer a lint to an
audit**. Before any chapter is written:

- **`tests/smoke/reading_refs_lint.sh`** (new): every backticked repo path
  (`src/...`, `tests/...`, `include/...`) in `docs/reading/` must exist;
  every `file.c:function` anchor must find that function's definition in
  that file (a grep-level check, comment-aware like arena_lint). Built
  and proven red-able *first*, with a deliberately wrong anchor.
- **No line numbers in guide prose, ever** — anchors are
  `file.c:function` or `file.c` + a search string; line numbers rot
  fastest and the lint cannot defend them.
- Guides route into ROADMAP/analysis/ANECDOTES rather than restating
  them, so drift concentrates in the one place already kept current.
- CURRICULUM.md, READING_OPEN_SOURCE.md, INSTRUCTOR.md, and the four
  track pages each gain one cross-link (the Annai for Stage 0–2 readers,
  the Fukabori for Stage 3+).

## 8. Delivery plan (milestone split, with effort honesty)

This is multiple weeks of authoring if done to the house prose bar; it
must not land as one mega-commit.

- **M222 — skeleton + lint + Annai chapters 1–4.** The directory, both
  indexes (with every chapter listed and its status marked `planned` —
  visible honesty over silent gaps), `reading_refs.sh` proven two-sided,
  and the four chapters that carry the AI-concepts thread (they gate
  everything else; wording here gets reviewed hardest).
- **M223 — Annai 5–10 + Appendix A.** Beginner edition complete; instructor guide
  gains the "assign the Tour alongside Stage 1" note.
- **M224 — Fukabori 1–6.** The decisions half of the expert edition,
  including the §5 chapter (why-C) — written against the real decision
  record, quoted not paraphrased.
- **M225 — Fukabori 7–12.** Including §6 (the migration road, in its
  generalized form) and the AI-examined chapter; the recommendation table
  is mirrored into ZIG_INTEROP.md/CPP_INTEROP.md as their shared closing
  section.
- Each milestone: `make smoke` green (the new lint included), one
  CHANGELOG entry at the end (M225) since the audience is users/learners.

## 9. Recommendations (decided here unless overruled)

1. **Two editions, not one with difficulty markers.** The audiences read
   for different reasons (orientation vs judgment); interleaving serves
   neither. Shared diagrams may be reused across editions verbatim.
2. **Author order: Annai before Fukabori.** The beginner edition forces the
   clearest possible statement of each mechanism; the expert edition can
   then argue against alternatives without re-explaining basics.
3. **Write with jichi, review by hand, and say so.** The guides should
   themselves be a demonstration of AI-supported writing — drafted with
   the agent, verified against the source by a human, with the honesty
   note the curriculum already models. Reading guides that silently
   hallucinate a function name would be a self-refuting artifact; hence
   the lint *before* the prose (§7).
4. **Mermaid over ASCII art** for every diagram (the repo's docs already
   use mermaid; artifacts and GitLab render it).
5. **Pseudo code convention:** typed, C-shaped, but garbage-collected —
   it exists to show the *algorithm*, so it must not re-fight the
   lifetime battles the C sections then show for real.
6. **Do not gate the guides on grading.** They are supplementary
   reading; their "exercises" route into existing graded tasks instead
   of minting new ones (set D and the tracks already cover the hands-on
   side). New assignments only if a later gap analysis shows one.
7. **Scope guard:** if any chapter exceeds ~400 lines it is trying to be
   reference documentation; cut and route into `docs/` instead.

## Open questions — all answered (Alexander-Lars, 2026-08-01)

1. ~~Naming~~ — **Japanese stage names**: 案内（あんない） *Annai* and 深掘り（ふかぼり）
   *Fukabori*, with the English working titles ("the guided tour" / "the
   deep dive") kept as subtitles. Romaji filenames.
2. ~~Build assumption~~ — **both**: the Annai assumes a built jichi
   (Module 0 done), AND Appendix A gives every experiment a read-only
   twin for repository browsers — feasible because it routes to artifacts
   already committed (analysis fixtures, transcripts) instead of minting
   new ones.
3. ~~The rewrite stance~~ — **generalized**: the argument is presented as
   structural (whole-graph-benefit languages vs seam-compatible ones),
   valid beyond Rust — Rust remains the prominent worked example, the
   table's wording admits any memory-safe language at a clean boundary.
