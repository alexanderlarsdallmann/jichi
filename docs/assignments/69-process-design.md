---
title: Design — trace every requirement
audience: student
phase: planning
difficulty: medium
points: 3
verify: "sh docs/assignments/69-process-design/test.sh"
hints:
  - "Run the grader. `REQUIREMENTS.md` is given (R1-R4); your `DESIGN.md` must ADDRESS each one — the grader checks every requirement id appears in the design."
  - "For each requirement, write the piece of the design that satisfies it, and cite the id: 'POST /notes creates a note and returns its id (R1).' That citation is traceability — it proves nothing was dropped."
  - "Go requirement by requirement (R1, R2, R3, R4) and design the smallest thing that meets it, naming the id. A requirement with no design behind it is a gap; a design with no requirement behind it is scope creep."
---
A design is *how* you will meet the requirements — and the discipline that keeps
a design honest is **traceability**: every requirement is addressed by some part
of the design, and every part of the design serves a requirement. This task's
folder gives you `REQUIREMENTS.md` (R1–R4) and a hand-wavy `DESIGN.md`. Write a
real design that traces each requirement.

> **The floor vs. the judgment.** The grader checks that `DESIGN.md` references
> every requirement id from `REQUIREMENTS.md` — nothing dropped. Whether the
> design is the *simplest* one that meets them, or well-structured, is your
> judgment (the harder half).

Rewrite `DESIGN.md` so it addresses R1–R4, citing each id where the design
satisfies it. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/69-process-design.md
```

> **If you are stuck alone:** make a checklist of R1–R4 and tick each off as you
> design the piece that meets it. An untraced requirement is a feature you will
> forget to build.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/69-process-design.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/69-process-design.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
