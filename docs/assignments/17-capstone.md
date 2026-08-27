---
title: Capstone — a bounded real project
audience: student
phase: implementation
difficulty: advanced
points: 3
verify: "sh docs/assignments/17-capstone/test.sh"
hints:
  - Scope fear is normal. The template's non-goals section exists so the project can be SMALL -- a capstone that ships beats a vision that doesn't. One week of evenings, one real itch.
  - Write the verify strategy before writing code, like Module 6 taught -- if you cannot say what command proves your project works, the project is not yet defined.
  - "The portfolio assembles itself if you work the way the course taught: the proposal is Module 6's discipline, the journal falls out of --journal, and the record entries are whatever actually went wrong (something will; that is not failure, that is material)."
---
Everything, together, on something **real**: a bounded project in a
repository of your own — a tool you actually want, an itch in a codebase you
actually have. The curriculum's fixtures end here; the subject is now yours,
and only the *evidence* comes back.

**The shape of the work** (each step is a module you have passed):

1. **Propose** — copy
   [`17-capstone/proposal-template.md`](17-capstone/proposal-template.md) to
   `docs/assignments/17-capstone/portfolio/PROPOSAL.md` and fill it in: the
   goal, the scope *and non-goals*, the envelope you will run under, and —
   before any code — the verify strategy (M6).
2. **Set up** the project bench: `jichi setup` / `init` in your repo, rules,
   and at least one `.jichi/` asset you authored because *this* project
   needs it — a glossary of its terms, a skill, a review command (M10's
   practice half).
3. **Build** under supervision and under the envelope: interactive turns
   for the shape, bounded `--auto` runs for the grind — with a real
   verifier, a real edit scope, and `--journal` (M8). Review and refactor
   as you go (M7); when the agent is confidently wrong — it will be — catch
   it and write it down (M9).
4. **Assemble the portfolio** in `docs/assignments/17-capstone/portfolio/`:
   `PROPOSAL.md`; `journal.jsonl` — one representative bounded run's
   journal, copied from your project (ended `ok`, a passing verify, no
   out-of-scope writes); and `RECORD.md` — at least **two** new entries from
   this project in the four-section format. The journal is the provenance;
   the record is the learning.

**How this is graded — honestly, one last time.** The floor below checks the
portfolio's structure and evidence. The *project* is judged by the rubric —
by `/check` if you are alone, by your instructor in a course, and best of
all live: explain a decision, change something on request, show the check
that would catch its removal. Provenance is process, not artifacts.

## Rubric

| Dimension | What good looks like |
|---|---|
| Scope | small, real, shipped; non-goals held |
| Verification | the verify strategy predates the code; the gate is two-sided (you have seen it red) |
| Delegation | bounded runs with sensible budgets/scopes *you* chose; journals read and understood |
| Craft | diffs minimal; refactors green-to-green; review findings argued by consequence |
| The record | honest entries where something actually went wrong — including at least one where the root cause was you |

Grade the floor with `jichi grade docs/assignments/17-capstone.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/17-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/17-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
