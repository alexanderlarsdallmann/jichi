# Curriculum: learning software development with an agent at your side

**Status:** design for review — 2026-07-27. Nothing here is built; the milestone
candidates at the end are sized and ordered. Fulfils the release-checklist item
"Curriculum" (docs/ROADMAP.md top).

**Follows:** `docs/JOURNEY.md` (the stage map this makes measurable),
`docs/ASSIGNMENTS.md` + `docs/TEACHING_ASSIGNMENTS.md` (the machinery),
`docs/presentations/04-university.md` + `05-school.md` (the institutional pitch),
`docs/LOCAL_MODELS.md` (the weak-hardware path), `docs/PHILOSOPHY.md` (why).

---

## 1. Who this is for, and the inversion that follows

Three audiences, in the order that matters:

1. **The self-learner** — no instructor, no cohort, maybe no budget and a
   fifteen-year-old laptop. The primary audience, for a reason that decides the
   whole design: **at one point or another, every student is a self-learner.**
   Homework is self-learning. Revision is self-learning. The evening after the
   lecture where nothing made sense is self-learning.
2. **The classroom** — a teacher with a mixed-ability group, limited time, and a
   reasonable fear that "the AI writes the code, so nobody learns anything."
3. **The university course** — a semester, credit, grading at scale, academic
   integrity, and provenance.

**Design decision — classroom and course are *layers over* self-study, not the
other way round.** Traditional course design assumes delivery (lecture) plus
reinforcement (exercises) plus external assessment. We invert it: every module
must be completable *alone*, with the agent itself as the tutor, the `verify`
command as the examiner, and the visible rubric as the marking scheme. The
instructor's layer adds calibration (tuning hint ladders to the cohort),
curation (which modules, which pace), and human judgment on the artifacts that
machines can't grade. The university's layer adds the syllabus mapping,
provenance, and integrity policy. Nothing in either layer is load-bearing for
the learner working alone at midnight — that is the test every module has to
pass.

Three consequences, applied everywhere below:

- **Do first, read second.** Every module opens with a task, not an exposition.
- **Assessment is self-administrable.** `jichi grade` on a spec with a strict
  `verify` is the examiner; the rubric is printed in the brief. "The assessment
  criteria are not a secret" is already this project's stated stance — the
  curriculum leans on it hard.
- **Help is built in and graduated.** The hint ladder and `ask_for_help` mean a
  stuck learner never *requires* a human. (This currently doesn't work for a
  human in the TUI — see the C2 milestone, which is the single most important
  enabler in this document.)

## 2. What is actually being taught

Not "how to use jichi." The subject is **software development in the agentic
era**, with jichi as the medium. Every module therefore fuses two skills:

| Axis | Examples |
|---|---|
| **Agent skill** — driving the tool | modes and leash-length, reading a diff before `y`, checkpoints and `/undo`, the envelope, reading a run journal |
| **Craft skill** — the discipline itself | code comprehension, minimal verified changes, testing, scientific debugging, design-before-code, review, delegation with verification |

The fusion is not decoration; it is the thesis. The one meta-lesson this whole
codebase teaches — in its verify gates, its honest-gate discipline, its
anecdotes — is: **trust what is checked, not what is claimed.** The curriculum's
arc is that lesson internalised in stages: first *the tests tell the truth*,
then *write the check yourself*, finally *check the checker, and teach someone
else*. That maps exactly onto the 守破離（しゅはり） (shu-ha-ri) stages `docs/JOURNEY.md`
already articulates — so the curriculum reuses that frame rather than inventing
a new one.

**Relationship to JOURNEY.md, stated once:** JOURNEY.md says who you are
becoming (virtues, dispositions — deliberately unmeasurable). The curriculum
says what you can *do* next and how you'll *know* (artifacts, graded gates —
deliberately measurable). They are companions; neither replaces the other. Each
module ends with a measurable gate *and* quotes the relevant JOURNEY reflection
as exactly that — reflection, not requirement.

## 3. The module map

Twelve units in four stages. **Each stage is severable**: Stage 0+1 alone is a
complete short course ("safe, productive agentic work"); adding Stage 2 makes it
"software craft with an agent"; the full sequence ends in teaching others.
Self-learner time estimates are honest ranges, not promises.

### Stage 0 — 仕度（したく） *Shitaku* (Preparation) — 1–3 h

| | |
|---|---|
| **M0 — A working bench** | Install, `jichi setup`, `doctor` — and *read every line of doctor's output* (JOURNEY's first practice). Forks by hardware, converging on an identical environment: **(a)** HRZ / institutional gateway (key + `jlu/…` model ids), **(b)** local model (LM Studio / llama.cpp / Ollama via `--preset small-local`), **(c)** any API provider. |
| Gate | `doctor` exits 0; assignment `00-hello` passes (`verify` greps a file the agent was asked to produce — the learner has completed one full turn: prompt → diff → approve → grade). |

### Stage 1 — 守（しゅ） *Shu* (Follow the form) — 15–20 h

| Module | Agent skill | Craft skill | Assignment set A |
|---|---|---|---|
| **M1 — Reading before writing** | chat/plan modes, `@file` refs, `search_code`, "what can it see?" | code comprehension; asking precise questions | comprehension tasks with grep-able answer files ("which file defines X — write its path into `found.txt`") |
| **M2 — The smallest change that works** | edit approval, reading the diff, checkpoints, `/undo` (incl. *break something on purpose, then undo* — JOURNEY's practice, now graded) | minimal targeted edits; diff literacy | single-/multi-file edits, the ambiguous-match task |
| **M3 — Tests are the truth** | `run_tests`, the parsed failure summary, fix-forward | reading failures; make-it-fail-first | fix-the-failing-test tasks; write-a-failing-test-first tasks |
| **M4 — Debugging as science** | the fix-forward loop under supervision | symptom → dead ends → root cause → lesson; **the learner's record begins** (the ANECDOTES format, prescribed by JOURNEY, now an artifact with a template) | seeded-bug tasks where the *first* hypothesis is wrong by construction |

**Stage gate:** set A cumulative ≥ threshold via `jichi grade`, plus the record
contains ≥ 2 entries. *Reflection (JOURNEY):* "you can predict what the agent
will do before it does it."

### Stage 2 — 破（は） *Ha* (Break the form) — 18–24 h

| Module | Agent skill | Craft skill | Assignment set B |
|---|---|---|---|
| **M5 — Write the check yourself** | `grade`/`attempt` against your *own* spec | authoring strict `verify` commands; hollow-gate hunting (ANECDOTES #17 as a taught unit) | **the meta-assignment:** given a task and four candidate solutions (one right, three subtly wrong), write a `verify` that accepts exactly the right one — mechanically graded by running it against all four. Grading the grader, as pedagogy. |
| **M6 — Design first** | plan mode, `--design`, design docs as input | requirements and design writing | design-doc tasks with **artifact-check** verify floors (§5) + `/check` for quality |
| **M7 — Review and refactor** | reviewer flow, `/check`, self-review, the git tools | code-review discipline; naming what's wrong *and why it matters* | find-the-three-seeded-smells tasks; refactor-without-behaviour-change (verify = tests still green + smell gone) |
| **M8 — Bounded autonomy** | the envelope: budgets, `--edit-scope`, `--verify-every`, journals, `runs`, telemetry | delegation **with** verification; reading logs as evidence | run a bounded `--auto` task and submit the journal — `verify` checks the journal's fields (outcome, budget respected, no out-of-scope writes). Machine-checkable delegation. |

**Stage gate:** set B ≥ threshold, *and* the M5 meta-assignment passed.
*Reflection:* "you disagree with a default and can defend why."

### Stage 3 — 離（り） *Ri* (Leave the form) — 12–16 h + capstone

| Module | What it is | Set C |
|---|---|---|
| **M9 — The agent is sometimes wrong** | The signature module; nobody else teaches this. Adversarial tasks: plausible-but-wrong agent output, hollow gates that pass while testing nothing, confident misdiagnoses. The grade is *detection*: did you catch it, and can you show the check that exposed it? Source material already exists — ANECDOTES #17 (green because `cat` succeeded), #19 (blamed the model, was the request), #20 (a self-test that tested a sibling), #21 (`ok:true` is not "did what was asked"). | seeded-trap tasks |
| **M10 — Teach it, teach with it** | Author `.jichi/` assets (memory, glossary, rules, a skill), then **author a complete assignment for a peer** — brief, rubric, strict verify, hint ladder. "Tune the ladder per level — that tuning *is* the teaching" (deck 05) becomes the graded exercise. | meta-graded: their spec's verify must be two-sided (rejects the pristine fixture, accepts a reference solution) — the same discipline `tests/bench/check_graders.py` enforces on our own corpus |
| **M11 — Capstone** | A bounded real project in the learner's own repository: scaffolded (`init`/`setup`), driven under the envelope, with a portfolio: `docs/assignments/INDEX.md`, the learner's record, and run journals as provenance. | capstone template + rubric; assessed by `/check` (self-learner) or instructor (course) |

*Arrived when (JOURNEY):* "your students break your forms and it pleases you."

## 4. Content: what exists, what must be authored

**Design decision — seed set A from the bench corpus.** `tests/bench/corpus/`
already contains 11 graded specs, ordered by difficulty, each with a
**two-sided-verified** `verify` (proven to reject the pristine fixture and
accept a reference solution — a property most hand-written coursework never
has). They are `audience: agent`, hintless, and live in `tests/` as a model
benchmark. Re-authoring them as `audience: student` with hint ladders, `phase`,
and `points` gives Set A a running start of ~10 tasks whose graders are already
trustworthy. The bench keeps its own copies untouched — the corpus is an
append-only measuring stick and must not be reworded (its README says so).

To author from scratch: `00-hello`, the M4 seeded-bug set, the M5 meta-verify
set, M6 design tasks, M7 smell tasks, the M8 journal task, the M9 traps, the
M10/M11 templates. Rough count: **~35 specs + ~8 reference solutions + 12
module pages**. The `/assign` command (assignment-writer agent) drafts;
`check_graders.py`-style two-sidedness is the acceptance bar for every spec
that claims a mechanical grade — we eat our own discipline.

**Language of exercises.** Early sets are language-neutral (files, text, shell)
or tiny C (a C compiler is present on effectively every target, including the
weak-laptop case; C is also this project's own language, which lets advanced
learners graduate to reading jichi's real source as a text). The instructor
guide documents the pattern for regenerating a set in another language via
`/assign` — the *structure* is the curriculum, the language is a parameter.
*(Confirmed 2026-07-28: C stays, no parallel Python sets; the expansion
horizon — C standards/portability extras, then the systems and functional
course families — is recorded in §9 answer 4.)*

## 5. Assessment, honestly

`verify` grades code superbly and grades prose not at all. Six phases are
offered (`requirements | use-case | design | implementation | testing |
documentation` — the canonical list from the shipped scaffold) but only
implementation/testing are mechanically gradeable today, and the docs currently
don't admit this. The curriculum states a **three-layer model** and never
pretends a layer does another's job:

| Layer | Instrument | Answers | Available to |
|---|---|---|---|
| **Floor** | `verify` (mechanical) | "is the required structure/behaviour present?" — for prose artifacts this is an *artifact check*: sections exist, an example is present, length bounds (grep-able) | everyone, alone |
| **Feedback** | `/check` (model, rubric-keyed) | "where does this fall short, and how to improve?" — formative, encouraging by design | everyone, alone |
| **Judgment** | a human (instructor, peer) | quality, originality, credit | classroom / course layers |

A self-learner gets floor + feedback and that is a *complete* formative loop.
A course adds judgment where credit depends on it.

**Design decision — hints are free; hint *counts* are visible.** Five places in
the current docs say hints "may cost points"; no code has ever deducted one.
Rather than implement the fiction, invert it: for a self-learner the ladder is
the tutor and must never punish (a penalised hint teaches hint-avoidance, which
is learning-avoidance). What *is* recorded — already, by `attempt` — is the
count. The classroom layer decides its own policy with that visibility. The
five fictional sentences get corrected to match.

**Progress, minimally.** No gradebook server, no accounts. `grade --record`
appends one JSON line (spec, pass, pct, hints, timestamp) to a per-workspace
progress file; `jichi assignments` learns to show attempted/passed/points
columns and to honour the already-documented-but-never-parsed `phase` and
`difficulty` keys. A self-learner's "am I ready for Stage 2?" becomes a command,
not a feeling. An instructor's gradebook is `grade --output json` over
submissions, as TEACHING_ASSIGNMENTS.md already sketches.

## 6. The three delivery layers

**Self-learner (the spine).** `docs/CURRICULUM.md` is the map; `docs/curriculum/`
holds one page per module (task-first, ~2 pages each, ending in the gate);
`docs/assignments/` ships the sets. Entry: `jichi setup --preset learner` (C1)
then module 0. Everything else is optional reading.

**Classroom.** An instructor guide (`docs/curriculum/INSTRUCTOR.md`) with, per
module: timing for a 90-minute lab, which learner tier to set, how many rungs
the ladder needs for which level, what to demo live vs. assign, and the
failure modes to expect. Plus the cross-cutting sections the survey found
missing: a verify-patterns cookbook per phase (including honest "this phase
has no mechanical floor beyond structure" notes), what to do when `/check` and
the human disagree (the human wins; `/check` is feedback), and how to spot
prompt-passthrough (ask the student to *change* their solution live — process
provenance beats artifact inspection).

**University course — three formats (decided 2026-07-27).**

| Format | Scope | Notes |
|---|---|---|
| **Introduction course** | Stage 0 + Stage 1, **opening with compiling jichi from source** | Source-only distribution makes the build step curriculum, not friction: `make` on a bare POSIX box is the first exercise, and `doctor` grades it. Ends at the Stage-1 gate. |
| **Block course** | Stages 0–2 compressed (5–8 full days) | Module gates unchanged; the instructor guide gets per-day pacing. |
| **14-week semester** | full sequence: Stage 0+1 → weeks 1–4; Stage 2 → 5–9; Stage 3 → 10–13; capstone defense week 14 | Lecture skeletons from decks 00–06. |

**Self-learners get extras** the taught formats omit: the JOURNEY reflections
inlined per module, "if you are stuck alone" escalation boxes (hint ladder →
`/tutor` → the ordered debugging procedure from LOCAL_MODELS.md), and optional
side-quests (read a named jichi source file as a text; reproduce an ANECDOTES
entry). Integrity stance for all formats builds on what the tool already
produces: **the journal is the provenance.** Assessment weight
shifts from "the artifact exists" toward "defend it, modify it live, show the
check that would catch its removal" — the M9 skills, examined. Deck
`04-university.md`'s reproducibility slide (the `--auto --verify --journal
--output json` one-liner) is the lab-report format.

**The key story (answered 2026-07-27).** `JLU_API_KEY` is a **placeholder**, not
a requirement — `apiKeyEnv` names whichever variable holds the learner's key.
Three cases, one config pattern:

- **JLU staff and students** request an HRZ key, or are handed one in class.
- **Everyone else brings their own key** for any OpenAI-compatible provider —
  the bring-your-own-key case is exactly what jichi is built for.
- **No key at all** → the small-local path (a local model needs none).

Curriculum pages and examples use a generic name (`LLM_API_KEY`) with the JLU
case as a note, not the default. The institutional page still needs writing
(model-id table, the **no-prompt-caching** cost fact currently buried in an
analysis note) but is now `docs/curriculum/INSTITUTIONAL.md` in spirit: the JLU
HRZ section is one instance of "your institution hands you a gateway".

## 7. Infrastructure: build vs. work around

Sized against the survey's gaps. The principle: build only what unblocks the
self-learner or removes a lie; defer everything that is merely nice.

| # | What | Size | Why it can't be worked around |
|---|---|---|---|
| **C1** | ~~`learner` + `instructor` setup presets~~ **BUILT** (M173a): both scaffold the assignments pack and emit `assignments: true`; verified end to end — one command from an empty directory to a study bench with all four learner tiers. `asks_language` is 0 for both, deliberately: the language-pack swap would replace the assignments pack. | S | Was: three commands plus a JSON hand-edit, and WORKFLOWS.md pointed at a preset that didn't exist. |
| **C2** | **TUI assignment sessions — approved 2026-07-27, surface below** | M | `app->assignment` is set only by `attempt` (verified in code) — so the hint ladder, the centrepiece of the pedagogy, is **unreachable by a human in the TUI**. The single most important enabler here. |
| **C3** | Content: module pages, sets A/B/C, `INDEX.md`, one worked example **in-repo** with `.solution.md` — **Stage 0+1 BUILT** (M174b): `docs/CURRICULUM.md` + five module pages + set A (9 specs, 17 pts, fixtures in per-task dirs) + `INDEX.md` + the 06 worked solution. **Stage 2 BUILT** (M176): four module pages + set B (5 specs, 15 pts) — the M5 meta-assignment (a checker meta-graded on discrimination over four candidates handed over under neutral names), the M6 design task (artifact-check floor, honestly framed), the M7 review+refactor pair (reference review sibling; smell-gone proven mechanically), the M8 delegation task (the grader reads the run journal). **Stage 3 BUILT (M177) — C3 COMPLETE**: three module pages + set C (4 specs, 15 pts) — the M9 traps seeded from ANECDOTES #17/#19/#21 (a green gate over wrong code, graded on detection both ways; a fluent misdiagnosis whose patch fixes only the reported symptom), the M10 teach-a-peer meta-task (the learner's own check held to the two-sided bar, solved on a throwaway copy), and the M11 capstone (proposal template + portfolio floor + rubric). All 18 graders two-sided through `jichi grade` incl. nine trap cases (`tests/e2e/curriculum_graders.py`). | L | Was: the pedagogy shipped with zero assignments; the only worked example lived in another repository. |
| **C4** | ~~Parse `phase`/`difficulty` (documented keys the parser ignores); `assignments` listing gains phase/points/status columns~~ **BUILT** (M174a) | S | The self-learner's map needs ordering the tooling can see. |
| **C5** | ~~`grade --record` + progress display~~ **BUILT** (record M173b; read side M174a — the listing's status column, `attempts`/`passed`/`best_pct` in JSON, TUI `/grade` auto-records incl. hint count) | S | The measurable gates of §3 need somewhere to stand. |
| **C6** | ~~Docs: a from-nothing build walkthrough (PREPARE_AND_BUILD.md, M173); HRZ page; a shipped glossary of jichi's ~30 terms; fix the hint-cost fiction (M173b); tutorial consolidation~~ **COMPLETE** (M175: the assignments pack ships a ~35-term jichi glossary to `.jichi/glossary.md`; `docs/curriculum/INSTITUTIONAL.md` carries the gateway pattern + the JLU instance + the no-caching cost fact; root `TUTORIAL.md` is now a router page) | S–M | Each was a place a learner got lost. |
| **C7** | ~~Instructor guide~~ **BUILT** (M179): `docs/curriculum/INSTRUCTOR.md` — per-module lab plans (timing / demo-live / failure modes / gates), tier + ladder tuning, pacing for all three formats, the per-phase verify-patterns cookbook with honest floor limits, grading ops, `/check`-vs-human, provenance-is-process integrity, the set-regeneration pattern, and an explicit v1 feedback loop (it predates its first classroom and says so). | M | The classroom layer, after the spine stabilised — and it had. |

### C2 in full: what the TUI and headless mode must expose

Approved. The learner's loop in the TUI, five commands:

| Command | Does | Why it can't be model-mediated |
|---|---|---|
| `/assignments` | list `docs/assignments/` with phase/points/status (needs C4/C5) | picking a task is the human's move |
| `/assignment <spec>` | load the brief (rendered for the configured audience) into the session; set `app->assignment`, arming `hint`/`ask_for_help`; **flip the model into tutor stance** (below) | activation is session state, not a chat request |
| `/hint` | pull the next rung directly; `/assignment` with no argument shows the active spec + hints used | a stuck learner must not need to *prompt well* to get help — that is the failure mode the ladder exists for |
| `/grade` | run the active spec's `verify`, show PASS/FAIL + parsed failures | self-assessment without leaving the session |
| `/tutor <question>` | route the question through the read-only `assignment-helper` profile (nudges, never the code) | see the hazard below |

**The tutor-stance hazard (found in code, 2026-07-27).** The current
`assignments: true` system-prompt addendum is pure **authoring stance**: it
instructs the model to produce assignments *and reference solutions*. A learner
in a plain TUI session who asks about their task is therefore talking to a model
primed to hand over the answer — the exact failure deck 05 names as "the worry."
C2 must split the stance: authoring stance only when **no** assignment is active
(the instructor's flow); **tutor stance** while `app->assignment` is set — guide,
hint, check reasoning, never write the solution; `/assignment off` restores
normal jichi. Without this, C2's other commands decorate a session that defeats
the assignment anyway.

Headless mode, for instructors, scripts, and editor/bridge front-ends:

| Surface | Does |
|---|---|
| `jichi hint <spec> [N]` | print rung N (or the next per the progress record) — the ladder for editor integrations and the web bridge |
| `jichi grade --record` | append one JSON line (spec, pass, pct, hints, ts) to the progress file (C5) |
| `jichi grade --output json` / `assignments --output json` | the machine-readable gradebook TEACHING_ASSIGNMENTS.md already gestures at |

Deliberately deferred: ACP assignment blocks (editors can shell out to `hint`),
an emacs/vim assignment mode, `/doctor` in the TUI, `/glossary <term>` lookup.

**Build order:** ~~C1 → C2 (the unlock)~~ **both done (M173a/b)** →
~~C3 Stage 0+1 content with C4/C5 alongside (ship "the short course" as a
complete usable thing)~~ **done (M174a/b)** → ~~C6~~ **done (M175)** →
~~C3 Stage 2~~ **done (M176)** → ~~C3 Stage 3 (set C)~~ **done (M177)** →
~~C7~~ **done (M179)**. **C1–C7: complete.** Each step left a shippable
whole; what remains is feedback from the first real runs, and the recorded
expansion horizon (§9 answer 4).

Drift fixes folded into the first pass (found during the survey, three are
plain factual errors): deck 05 says three learner tiers where four ship;
TEACHING_ASSIGNMENTS.md's phase list disagrees with the shipped canonical one
(`review` vs `use-case`); WORKFLOWS.md sends learners to a nonexistent
`--profile beginner`.

## 8. What this deliberately does not do

- **No LMS, no accounts, no server.** Files, git, and exit codes. A classroom
  that wants Moodle exports has `--output json`.
- **No certification claims.** The gates certify *readiness for the next
  module*, nothing else.
- **No age-specific school material yet.** Deck 05's Chromebook-class vision is
  real but needs a teacher partner to calibrate against; the instructor guide
  gets a "younger learners" section only when someone has run it.
- **No translation up front.** English-canonical, matching the docs/i18n phased
  policy; German (then further languages, e.g. Japanese) waits for the trigger —
  Alexander-Lars's call, probably later in August 2026 or after the initial
  public release (decided 2026-07-28; see §9).

## 9. Open questions (for Alexander-Lars)

1. ~~**HRZ key acquisition**~~ — **answered:** `JLU_API_KEY` is a placeholder for
   any LLM key; use a generic name in curriculum material. JLU staff/students
   request one or receive one in class; everyone else brings their own key
   (jichi's core case); no key at all → the small-local path.
2. ~~**German timing**~~ — **answered (2026-07-28):** trigger-based, no
   hurry. German — and further languages such as Japanese after it — waits
   for Alexander-Lars's call, probably later in August 2026 or after the
   initial public release. Until then the spine stays English-canonical per
   the docs/i18n phased policy; a German learner already has the *agent*
   tutoring in German via `language`/`/language` over the English briefs.
   When the trigger fires: Stage 0+1 (the intro-course subset) translates
   first.
3. ~~**Course length**~~ — **answered:** three formats — an **introduction
   course including compiling the source code**, a **block course**, and a
   **14-week semester course**; self-learners get extras. See §6.
4. ~~**Exercise language**~~ — **answered (2026-07-28): C stays; no Python
   sets.** The focus is software development as **craft and engineering**,
   not a single language — but C is small, C is everywhere, and the sources
   are open (jichi's own, Linux's, many more) to study; there are already
   plenty of Python tutorials on the net. Two directions follow:
   - **Expansion curriculum** (post-C7): another block course and a 14-week
     course — or modular extras on special topics — covering the C89/90
     standards, modern C, and the portability implications between them.
     jichi must support learners in **reading, analyzing, testing, and
     refining open-source C projects**; jichi's own source is the first
     text (it already is, for advanced learners — §4).
   - **The long-range course families:** the world needs better *systems
     programming* courses (C, C++, Zig, Rust) and *functional programming*
     courses (Guile/Scheme, Elixir, Haskell, Clojure, Racket) far more than
     another Python tutorial. The structure is the curriculum; the language
     is a parameter — these families are where that parameter eventually
     points. *jichi — just code.*
5. ~~**Where the curriculum lives**~~ — **answered (2026-07-28): in-repo,
   and so does everything it leans on.** Not merely "no reason to separate":
   the project plan, the design decisions, the analysis notes, and the
   anecdotes belong in the repository and in the public release, because
   jichi must provide the one thing most software projects lack — the
   **complete, honest project documentation**: requirements, design
   decisions, notes, the failures, the lessons, the long journey — not just
   the glorious presentation for the marketing campaign. This resolves the
   M9 coupling (the module's textbook is ANECDOTES #17/#19/#20/#21) in the
   strongest way: the anecdotes ship. The public-snapshot curation concerns
   the *git history* (a fresh first commit, the still-unchosen license) —
   not the documentation.

## Recommendations

- Approve the shape (stages, gates, three-layer assessment) before any content
  is authored — content is the expensive part and follows the shape.
- Build C1+C2 first regardless of content decisions; both are small and repair
  today's broken promises (a preset that doesn't exist, a hint ladder no human
  can reach).
- Answer open question 1 early; it blocks the page every JLU student would
  read first.

See also: `docs/JOURNEY.md`, `docs/ASSIGNMENTS.md`,
`docs/TEACHING_ASSIGNMENTS.md`, `docs/LOCAL_MODELS.md`,
`docs/presentations/04-university.md`, `docs/presentations/05-school.md`,
`docs/PHILOSOPHY.md`, `docs/SELF_IMPROVEMENT.md` §5.
