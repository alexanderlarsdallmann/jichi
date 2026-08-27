# Workflows — pick your journey

jichi serves several very different audiences from one portable binary. This
hub maps each **role → a getting-started path → the docs that go deeper**. Start
with your row; every link is a full guide.

New to jichi entirely? Do [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md) first (empty
directory → a working session in minutes), then come back here.

| You are… | Start with | Then read |
|----------|-----------|-----------|
| A **learner / self-teacher** | `setup --preset learner`, then Module 0 of the curriculum | [CURRICULUM.md](CURRICULUM.md) · [Self-teaching](#self-teaching) · [LEARNING.md](LEARNING.md) |
| An **instructor** (classroom) | `setup --preset instructor`, then the guide | [curriculum/INSTRUCTOR.md](curriculum/INSTRUCTOR.md) · [Classroom](#classroom-teaching) · [TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) |
| A **university** researcher/teacher | `setup --preset reviewer` or `technical-writer` | [University](#university-research--teaching) · [presentations/04-university.md](presentations/04-university.md) |
| A **solo developer** | `setup --preset developer` | [TUTORIAL_ADVANCED.md](TUTORIAL_ADVANCED.md) · [AUTONOMY.md](AUTONOMY.md) · [CONSTRAINTS.md](CONSTRAINTS.md) |
| Entering a **specific project situation** — starting small, contributing to OSS, refactoring, rewriting in another language, or architecting something complex | one of the five journeys — on its own (`setup --preset refactor`) or **layered on your role** (`setup --preset developer --journey refactor`), which is what the interactive wizard now asks for separately | [SDLC.md](SDLC.md) |
| A **team / multisite** | headless `-p` over SSH per box | [Multisite over SSH](#multisite-over-ssh) · [REMOTE_SSH.md](REMOTE_SSH.md) · [DAEMON.md](DAEMON.md) |
| A **supervisor of many agents** | one jichi (or you) driving N workers | [Supervisor of many](#supervisor-of-many) · [AGENT_COLLABORATION.md](AGENT_COLLABORATION.md) · [SUPERVISOR_OF_MANY.md](SUPERVISOR_OF_MANY.md) · [PARALLEL.md](PARALLEL.md) |
| A **mentor tuning jichi itself** | run `--auto`, then `learn analyze` | [Mentor loop](#mentor--self-improvement) · [LEARNING.md](LEARNING.md) · [SELF_IMPROVEMENT.md](SELF_IMPROVEMENT.md) |

---

## Self-teaching

Use jichi as a patient tutor that knows your codebase.

1. `jichi setup --preset learner` — the study environment in one command:
   the assignments pack (learner tiers, hint ladder, `/assign`-family commands),
   `assignments: true`, snapshots on, chat mode. The **taught road built on it
   is [CURRICULUM.md](CURRICULUM.md)** — all four stages ship, with graded
   sets A+B+C under `docs/assignments/`; the design is
   [proposals/2026-07-curriculum.md](proposals/2026-07-curriculum.md).
2. Ask in plain language: *"explain what src/x.c does"*, *"show me how errors flow
   through this function"*. Use `@file` / `@sym:name` to point at exact code
   ([REFERENCES.md](REFERENCES.md)).
3. Let it **remember** what you've learned: durable notes accrue in
   `.jichi/memory.md` ([MEMORY.md](MEMORY.md)); a **glossary** captures domain terms.
4. Keep it read-only while you learn: it can read + explain without changing files
   (plan mode / a `read-only` constraint — [CONSTRAINTS.md](CONSTRAINTS.md)).

## Classroom teaching

An instructor authors assignments; students solve them with scaffolded help; a
read-only checker grades. Everything is designed for **humans learning**, not
agent-vs-agent.

1. `jichi init assignments` + set `assignments: true` in config.
2. `/assign` writes an assignment to `docs/assignments/`; `/solve` and `/check`
   drive the solution + grading agents.
3. Full flow, roles, and the learner-support scaffolding: **[TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md)**
   and [ASSIGNMENTS.md](ASSIGNMENTS.md).

## University (research & teaching)

- **Teaching:** the classroom flow above, plus the `technical-writer` preset for
  course materials (audience-aware writer/proofreader agents).
- **Research:** the `reviewer` preset (plan mode + architecture/analysis agents)
  for reading unfamiliar codebases; external-docs indexing ([DOCS.md](DOCS.md)) and
  retrieval ([RAG.md](RAG.md)) to ground answers in papers/specs; the autonomy
  envelope + telemetry ([AUTONOMY.md](AUTONOMY.md), [TELEMETRY.md](TELEMETRY.md))
  for reproducible, bounded, auditable runs. See
  [presentations/04-university.md](presentations/04-university.md).

## Multisite over SSH

Drive jichi headlessly on each machine where the code (and compute) live.

1. On each box: a config with the right models; `jichi -p "task" --output json`.
2. Keep a warm process per box with the **daemon** ([DAEMON.md](DAEMON.md)) so runs
   don't cold-start.
3. Compose prompts locally, pipe over SSH, collect structured results:
   **[REMOTE_SSH.md](REMOTE_SSH.md)**.

## Supervisor of many

One coordinator (a human, a script, or a jichi agent) fans work out to several jichi
**workers** — local (fork pool / daemon) or remote (SSH) — and merges the results.
This is the pattern behind large migrations, audits, and multi-repo work. It's its
own guide: **[SUPERVISOR_OF_MANY.md](SUPERVISOR_OF_MANY.md)** (builds on
[PARALLEL.md](PARALLEL.md), [SUBAGENTS.md](SUBAGENTS.md), [REMOTE_SSH.md](REMOTE_SSH.md),
[DAEMON.md](DAEMON.md), [SCRIPTING.md](SCRIPTING.md)).

## Mentor / self-improvement

Make jichi better at your project by mining its own runs.

1. Run tasks (especially `--auto`) with telemetry on (`--log-level metrics`).
2. `jichi learn analyze` ranks recurring problems offline; `/learn` drafts
   durable lessons; `learn apply` commits them to memory/skills.
3. Design + rationale: [LEARNING.md](LEARNING.md), [SELF_IMPROVEMENT.md](SELF_IMPROVEMENT.md).

---

## Cross-cutting foundations (every workflow benefits)

- **Safety:** snapshots/undo ([SNAPSHOTS.md](SNAPSHOTS.md)), the autonomy envelope +
  edit-scope + verify gate ([AUTONOMY.md](AUTONOMY.md)), enforced constraints
  ([CONSTRAINTS.md](CONSTRAINTS.md)), the memory watchdog (`--mem-budget`).
- **Grounding:** repo map ([REPOMAP.md](REPOMAP.md)), retrieval ([RAG.md](RAG.md)),
  `@`-references ([REFERENCES.md](REFERENCES.md)), external docs ([DOCS.md](DOCS.md)).
- **Fit & cost:** models + roles ([MODELS.md](MODELS.md)), routing
  ([ROUTING.md](ROUTING.md)), prompt caching ([PROMPT_CACHING.md](PROMPT_CACHING.md)),
  compaction ([COMPACTION.md](COMPACTION.md)), low-memory targets
  ([LOW_MEMORY.md](LOW_MEMORY.md)).
- **Setup health:** `jichi doctor` (health), `benchmark` (best-practice
  coverage), `setup` ([SETUP_WIZARD.md](SETUP_WIZARD.md)).
