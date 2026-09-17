---
title: Decisions — criteria before options
audience: student
phase: design
stage: process
difficulty: medium
points: 3
verify: "sh docs/assignments/75-process-decisions/test.sh"
hints:
  - "Run the grader. `REQUIREMENTS.md` (R1-R4) and `DESIGN.md` are given; your `DECISIONS.md` needs >= 3 decisions headed `## D1 -- ...`, each with a `Chose:` line, a `Rejected: <alternative> -- <why it lost>` line, and a `Because:` line naming the criterion, citing an R-id."
  - "The `Because:` line is the whole lesson. 'Rejected: SQLite' is a list; 'Because: R3 caps the install at nothing but the binary -- the criterion is zero dependencies' is an argument a reader can weigh. Name the SCALE, not just the winner."
  - "Pick the three choices the given design made without saying so: how notes are stored, how ids are made, what delete means. For each: what else could it have been, which requirement decides between them, and why did the alternative lose ON THAT criterion?"
---
Task 69 made you trace every requirement into a design. This task asks the
question 69 never did: **why this design and not another?** The given
`DESIGN.md` made at least three choices silently — a storage shape, an id
scheme, a meaning for *delete* — and a design that does not say what it
rejected has not decided; it has defaulted.

A decision is an argument with three parts, and the grader checks all three:

```markdown
## D1 — Storage: one JSON file, not a database (R1, R3)
Chose: a single `notes.json`, rewritten whole on every change
Rejected: SQLite — a dependency, a schema and a migration story for four requirements
Because: R3 lists all notes and nothing in R1–R4 asks for search or concurrency; the
         criterion that decided this is zero dependencies, and only the file meets it
```

- **`Chose:`** — the option you took.
- **`Rejected:`** — an alternative *and why it lost*, separated by `--` or `—`.
  An alternative with no reason is a list, not an argument.
- **`Because:`** — the **criterion**: the requirement or property the options
  were weighed on. This line is what turns "I rejected X" into something a
  reader can disagree with — it names the scale, not just the winner. Cite the
  `R<n>` the criterion comes from.

Criteria come *before* options: decide what would make one choice better than
another, then look at the choices. Done the other way round, the criterion is
reverse-engineered from the winner, and the register reads as evidence for a
decision that was really a habit.

> **The floor vs. the judgment.** The grader checks the *shape*: ≥3 decisions,
> each with the three lines, the rejection carrying a reason, the criterion
> traced to a requirement. It cannot check whether the criterion is the one
> that *should* decide, or whether the rejected alternative is a fair one —
> a straw man passes this floor and fails a reader, and that judgment is the
> harder half. This is the graded floor of the practice
> [`PROJECT_RECORDS.md`](../PROJECT_RECORDS.md) §3 teaches: *if there was no
> alternative, it was not a decision.*

Write `DECISIONS.md` in this task's folder with at least three decisions the
given design made. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/75-process-decisions.md
```

> **If you are stuck alone:** for each choice in the design, ask "what is the
> *other* obvious way to do this, and which requirement makes me prefer mine?"
> If no requirement does, you have found a preference — write it down as one,
> and say which requirement *would* have to exist for it to matter.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/75-process-decisions.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/75-process-decisions.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
