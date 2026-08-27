# Proposal — the *process* curriculum: how software is actually made

*Status: proposal (2026-08-02). Design + decisions for a new curriculum layer
that teaches the **software-development process** — documentation, requirements,
use-cases, protocols, session notes, kanban, scheduling — not just the code.
Self-learner-first. Author: the jichi maintainers with the agent.*

---

## 1. Why this exists

The shipped curriculum (`docs/CURRICULUM.md`) teaches **craft**: reading code,
minimal changes, tests, debugging, bounded autonomy — plus nine standalone
language courses. It is deliberately code-centric, and it is good at that.

But a self-learner who can write and test code is still missing the half of
software development that no compiler checks and no test suite grades: **the
process.** How do you turn a vague idea into a *requirement* someone can build
against? How do you write documentation a stranger can follow? How do you keep a
project moving when you are the only person on it — no manager, no standup, no
deadline but your own? A professional learns these by osmosis from a team. **A
self-learner, alone, has no team to absorb them from.** That is the gap this
layer fills.

The design case, as always (`CURRICULUM.md`): *a self-learner with a laptop,
alone.* They need guidance and support, not a reading list.

### The honest limit (stated first, as jichi always does)

Process artifacts — a requirements doc, a use-case, a kanban board — cannot be
graded the way `rpn_eval` can. There is no `assert` for "is this a good
requirement?" So this layer's graders check a **floor** (the artifact exists,
has the required *structure*, and is internally consistent — which a script
*can* verify), and the **quality** is graded by the learner against a rubric,
with `/check` and `/tutor` as the model-keyed feedback (`CURRICULUM.md` §"How
grading works — honestly"). This is the same three-layer honesty the code
curriculum already uses: **floor (mechanical) + feedback (model) + judgment
(human)**. We do not pretend a script can judge prose.

---

## 2. What it teaches — seven process phases

Each phase is a short module with **one graded artifact** (mechanical floor) and
a worked example the learner reads first. They are ordered so each feeds the
next — the output of one is the input of another, which is itself the lesson:
*process artifacts are a chain, not a pile.*

| # | Phase | The artifact you produce | The floor a script checks | The judgment you keep |
|---|---|---|---|---|
| P1 | **Requirements** | `REQUIREMENTS.md` — what the thing must do, as testable statements | ≥N requirements, each with an id + a *verifiable* phrasing (a "shall", a number, a condition) | are these the *right* requirements? complete? |
| P2 | **Use-cases** | `USE_CASES.md` — actor → goal → steps → outcome | ≥N use-cases, each naming an actor, a trigger, and a success + one failure path | do they cover the requirements? |
| P3 | **Design / protocol** | `DESIGN.md` (+ a `PROTOCOL.md` when there's an interface) | design references each requirement id; a protocol names its message shapes + error cases | is the design the simplest that meets the reqs? |
| P4 | **Documentation** | `README.md` for *your own* small project | has install + run + one worked example; a stranger-follows-it check | can someone else actually use it? |
| P5 | **Session notes** | `notes/YYYY-MM-DD.md` — what you did, why, what's next | dated, has a "did / decided / next" spine; ≥K entries over the project | are these notes *you* would thank yourself for? |
| P6 | **Kanban / tracking** | `BOARD.md` (or `.jichi/board.md`) — todo / doing / done | valid columns; every "doing" item traces to a requirement; WIP-limit respected | is the board honest about where things stand? |
| P7 | **Scheduling / estimation** | `PLAN.md` — milestones, a rough estimate, a retrospective | milestones with a size guess; a *retro* comparing guess vs actual | did you learn how wrong your estimates were? |

The **capstone** ties them: take a small real idea, walk it P1→P7, and produce
the whole artifact chain — the portfolio a self-learner can show (and, not
incidentally, exactly the shape of the code curriculum's M11 capstone portfolio,
so the two capstones rhyme).

### Why these seven, and not others

- They are the **irreducible** process artifacts: every methodology (agile,
  waterfall, lean, XP) is a *rearrangement* of requirements → design → build →
  track → reflect. Teach the atoms; the learner can assemble any methodology.
- Each has a **checkable floor.** We deliberately excluded process ideas with no
  mechanical floor at a beginner level (e.g. "stakeholder management",
  "estimation poker") — they belong in the instructor layer, not a self-learner's
  graded path.
- They are **tool-agnostic on purpose.** BOARD.md is a markdown kanban, not
  Jira. PLAN.md is a table, not MS Project. The lesson is the *practice*; the
  tool is a parameter (the same stance `CURRICULUM.md` takes on the exercise
  language). A learner who kanbans in markdown will kanban in anything.

---

## 3. How jichi teaches it — the agent as process partner

This layer leans on capabilities jichi already ships, which is *why* it is a
natural fit rather than a bolt-on:

- **Custom slash commands** (`.jichi/commands/*.md`) drive each phase:
  `/requirements`, `/usecases`, `/design`, `/session-note`, `/board`, `/plan`.
  Each is a scaffolded command (M17 pattern, like `/assign` `/solve` `/check`)
  that runs a phase-specific agent persona.
- **Scaffolded agents** (`.jichi/agents/*.md`) give each phase a *readonly*
  reviewer (a `requirements-reviewer` that flags an untestable "shall be fast",
  a `docs-proofreader` — the M13 audience-aware writers already exist) and a
  *guide* persona that asks the clarifying questions a beginner does not yet know
  to ask themselves.
- **`todowrite`/`todoread`** already back a task list; a `board` command renders
  it as kanban and persists it, so the learner's real work *is* the board.
- **Session notes** are a natural fit for **`jichi export`** + the **run
  journal**: the agent can draft the "did / decided / next" note from the actual
  session transcript, which the learner edits — teaching that good notes are
  *curated*, not auto-generated.
- **The learning loop** (`/learn`, `docs/LEARNING.md`) already turns a session's
  telemetry into durable lessons; a process retrospective (P7) is the same move
  aimed at *estimates* instead of *tool errors*.

Nothing here needs new C. It is a **scaffold pack** (`process`) + a set of
commands/agents + a curriculum module page, exactly the shape `docs/SCAFFOLDING.md`
already supports. That is the point: the process curriculum is *content*, and
jichi's content mechanisms already carry it.

---

## 4. The self-learner-first design rules

Every phase page follows the beginner-support rules the M243 review established
(`docs/plans/2026-08-tutorial-curriculum-review.md`):

1. **A worked example first.** Before you write a requirement, you read three
   good ones and three bad ones, with *why* each is which. Show, then ask.
2. **The context is always explicit** — what to produce, in which file, for whom,
   and when it is "done enough". No phase says "write requirements"; every phase
   says "create `REQUIREMENTS.md` in your project root, with at least five items
   in the id + shall form; here is the template; here is what the floor checks."
3. **Every phase has a `> If you are stuck alone` box** — the self-learner's
   substitute for asking a colleague — and a free `/hint` ladder.
4. **The artifact chain is visible.** A diagram at the top of the module shows
   P1→P7 feeding each other, so the learner sees they are building *one* thing
   from seven angles, not doing seven disconnected exercises.
5. **It is severable and short.** P1–P3 (requirements → use-cases → design) is a
   complete mini-course on "think before you build"; P4–P7 (document → notes →
   board → plan) is "run a project alone". A learner can take either half.

---

## 5. Grading — the floors a script can actually check

The mechanical floors are deliberately *structural*, never *semantic*:

- **Presence + shape**: the file exists, parses, and has the required sections
  (reuse `jc_md` frontmatter/section splitting; a `verify` shell script greps for
  the spine).
- **Internal consistency**: a use-case references a requirement id that exists; a
  "doing" board item traces to a requirement; a design mentions each requirement.
  These are *cross-file id checks* — a script's sweet spot.
- **Count + threshold**: ≥5 requirements, ≥K session notes, a WIP limit not
  exceeded — the same "not a hollow suite" bar the code courses use.
- **Two-sided by construction**: each grader ships with a pristine fixture that
  *fails* the floor (a `REQUIREMENTS.md` with three vague "should be nice" lines)
  and a reference that passes — proven in `curriculum_graders.py`, red-first,
  like every other grader.

What a script **cannot** check — is the requirement *correct*, is the doc
*clear*, is the estimate *honest* — is exactly what `/check` (model feedback) and
the learner's own rubric judgment cover. We say so on every page.

---

## 6. Placement in the curriculum

This is a **new track**, parallel to the language courses, not a new stage in the
shu-ha-ri spine (the spine is about *craft*; this is about *process*). It slots
into `docs/assignments/INDEX.md` as a **"Process track"** section, and into
`CURRICULUM.md` as an optional cross-cutting layer a learner runs *alongside*
Stage 2–3 (once they can build something worth documenting). Recommended
sequencing note on the page: "do P1–P3 before your first real project, P4–P7
during it."

Prerequisite: **none but jichi** — no compiler, no toolchain. That is a feature:
it is the one graded track a learner can start on day one, before they can code,
and the one that keeps paying off in every other domain (§ the domain-scaffolds
proposal).

---

## 7. Build plan (scoped, to the project's verified bar)

1. **`process` scaffold pack** — the commands/agents/skills above, as compiled-in
   asset tables in `src/scaffold/jc_scaffold.c` (chunked <509-char literals),
   with `test_scaffold.c` asserting every asset parses (the existing gate).
2. **A `setup --preset process`** recipe (`src/setup/jc_setup.c`) that scaffolds
   the pack + a `process`-flavored output style + the module page.
3. **Seven graded assignments** (`docs/assignments/`) — P1–P7 — each with a
   pristine fixture, a `verify` script checking the structural floor, and a
   reference solution + a trap (a hollow artifact that has the shape but no
   substance the cross-file check catches), registered two-sided in
   `curriculum_graders.py`, red-first.
4. **A module page** `docs/curriculum/process/` with the seven phases, the
   artifact-chain diagram, worked examples, and the stuck-alone boxes.
5. **Docs**: `docs/PROCESS_CURRICULUM.md` (the operator/learner guide) + INDEX +
   CURRICULUM wiring.

Every step is content + existing mechanisms; no new C subsystem. Each grader is
shown red-first before it is trusted, exactly as the 36 language-course graders
were. Estimated size: comparable to one language course family (a few sessions),
because the *mechanisms* already exist — this proposal is the design so the build
is mechanical.

---

## 8. Decision log

- **Structural floors only, never semantic grading of prose.** Locks in the
  honesty stance; avoids a hollow gate that "grades" writing quality it cannot
  measure. (Non-negotiable — it is the curriculum's founding integrity rule.)
- **Tool-agnostic artifacts (markdown), not tool training (Jira/Project).**
  Teaches the practice, keeps it portable, needs no external service.
- **A parallel track, not a new stage.** Process is orthogonal to craft; forcing
  it into shu-ha-ri would distort both.
- **No new C.** Proves the layer rides existing content mechanisms — and keeps
  the release-critical core untouched.
- **Seven phases, chosen for a checkable floor + irreducibility.** Excludes
  process ideas that only a human can assess at the beginner level.
