---
title: The register that went stale
audience: student
phase: review
stage: process
difficulty: medium
points: 3
verify: "sh docs/assignments/86-the-register-that-went-stale/test.sh"
hints:
  - "Start with the `Where:` column. Every row names a file or a directory; open it. Most rows resolve in one read, and a row you can settle by reading is a row you should not reason about."
  - "Absence is easier to establish than presence. For a row that says something is missing, look for the mechanism — a timeout, a checksum — and if the tree has nothing that could be it under any name, the row stands."
  - "One row is a word that appears in the tree meaning something else. A grep will say the work is done; the file will say it is not. Read the comment around the match before you decide."
---

Task 71 had you keep session notes and task 75 had you record a decision. This
task is the other half of keeping records: **the maintenance**. A register is
only worth what its last walk was worth, and a row that was true when it was
written goes on looking exactly like one that still is.

## The task

`REGISTER.md` lists five pieces of work consciously *not* done on a small
client, each with a reason and a `Where:` pointer. The code it describes is in
`project/`. The register's preamble says it was last walked eleven months ago.

Some of those rows are no longer true. Decide which, and write
`docs/assignments/86-the-register-that-went-stale/AUDIT.md` with **one verdict
line per row**:

```
R1: STALE — project/upload.c
R2: STANDS
```

- **STALE** — the work is done, or the reason has stopped being true. Name the
  file that makes it so, on the same line.
- **STANDS** — the row is still accurate. No citation needed.

Every row needs a verdict, including the ones that stand. An audit that reports
only its findings has not told you what it looked at.

## What is being graded

Not the shape of a register — the **habit**. Specifically:

- **Both directions.** A stale row you miss and a standing row you wrongly
  retire are the same defect. The grader refuses either.
- **The citation resolves to the right thing.** A `STALE` verdict must name the
  file that carries the evidence, not merely a file that exists. This is
  [`GROUNDED_DISCOURSE.md`](../GROUNDED_DISCOURSE.md)'s *grounds that resolve*,
  met in a register.

## Why a false positive is not the safer mistake

It is tempting to think that over-reporting is the careful direction — you
looked, you were keen, someone will check. It is not.

A missed stale row leaves a wrong claim on a page that somebody will eventually
re-walk. A row you wrongly call stale **is deleted**, and nobody re-checks a row
that is no longer there. The work it deferred goes with it.

## Choosing well

The cheapest audit is the one the register hands you: every row names a
`Where:`. Open it. A row you can settle by reading is a row you should not
reason about.

Where reading cannot settle it, be careful about the direction of the evidence.
*Absence* is nearly establishable by search — if nothing in a small tree could
be a checksum under any name, the row stands. *Presence* is not: a word that
matches is not a mechanism that exists, and one of these five rows is exactly
that trap.

## When you are done

Grade with `jichi grade docs/assignments/86-the-register-that-went-stale.md`.

Then read [the worked example](86-the-register-that-went-stale.solution.md) —
**after**, not before. It shows the check behind each verdict, including the
`grep` that answers *yes* where the file answers *no*.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/86-the-register-that-went-stale.md` in a **terminal**, not inside jichi — leave the agent with `/exit` first, or use a second terminal. Stuck? `jichi hint docs/assignments/86-the-register-that-went-stale.md` (or `/hint`) gives one rung at a time — free, and recorded. No toolchain is needed: the grader reads your `AUDIT.md` and the fixture beside it.
