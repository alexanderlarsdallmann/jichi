---
title: Scheduling — estimate, then measure
audience: student
phase: process
difficulty: medium
points: 3
verify: "sh docs/assignments/73-process-scheduling/test.sh"
hints:
  - "Run the grader. It wants a PLAN.md with at least 3 milestones each carrying a size estimate (S/M/L or days), and a retrospective comparing estimate vs actual."
  - "The point isn't a perfect schedule — it's learning how wrong your estimates are. Give each milestone a rough size, then in a Retro compare what you ESTIMATED against what it ACTUALLY took."
  - "Write `## Milestones` with 3+ items each ending in a size (`— M (est 3d)`), then a `## Retro` section with lines like 'M1 estimate 3d, actual 5d — underestimated X'."
---
The last process phase is time. You will estimate badly — everyone does (the
planning fallacy) — so the goal of a plan is not to be right, but to **learn how
wrong you are**, and get better. That means two things a script can check: every
milestone carries a **size estimate**, and a **retrospective** compares the
estimate against the **actual**.

`PLAN.md` here is "a few weeks, should be quick" — the plan that teaches nothing.
Turn it into milestones with sizes and a retro that faces the real numbers.

> **The floor vs. the judgment.** The grader checks for sized milestones and a
> retro that compares estimate vs actual. Whether your *estimates got better* —
> the actual point — only shows over several projects. This task builds the habit.

Rewrite `PLAN.md` with ≥3 sized milestones and a retro comparing estimate vs
actual. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/73-process-scheduling.md
```

> **If you are stuck alone:** the retro line "estimate 3d, actual 5d" is the whole
> lesson — over a few of those you learn your personal fudge factor (usually
> ~1.5x), and your next plan finally holds.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/73-process-scheduling.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/73-process-scheduling.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
