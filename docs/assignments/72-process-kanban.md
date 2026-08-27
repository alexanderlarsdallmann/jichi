---
title: Kanban — an honest board, WIP limited
audience: student
phase: process
difficulty: medium
points: 3
verify: "sh docs/assignments/72-process-kanban/test.sh"
hints:
  - "Run the grader. It wants a BOARD.md with Todo / Doing / Done columns, a declared WIP limit, no more than 3 cards in Doing, and every Doing card tracing to a requirement (R<n>)."
  - "The board's job is honesty about where things stand — and its discipline is the WIP limit: keep Doing tiny (finish before you start). Every card in Doing should serve a requirement, so tag it with an R<n>."
  - "Fix `BOARD.md`: keep the three columns, state a WIP limit, move all but 1-2 cards out of Doing (to Todo), and make each remaining Doing card reference the requirement it serves (R1, R2, ...)."
---
A kanban board makes work **visible** and, with a **WIP (work-in-progress)
limit**, honest — you cannot pretend to be doing ten things when the board only
allows two. The board in this folder is the classic beginner failure: everything
dumped in `Doing`, nothing finished, no cards tied to a goal. Fix it.

The floor: **Todo / Doing / Done** columns; a **declared WIP limit**; **Doing not
overflowing it** (finish before you start); and **every Doing card tracing to a
requirement** (R<n>) — because work in progress should serve the goal, not drift.

> **The floor vs. the judgment.** The grader checks the columns, the WIP limit,
> and the traces. Whether the board is *honest* — actually reflecting reality —
> is between you and yourself; a board that lies is worse than none.

Rewrite `BOARD.md` to pass the floor. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/72-process-kanban.md
```

> **If you are stuck alone:** the fix is almost always "move things out of Doing."
> One or two cards in Doing is right; ten is a wish list. Stop starting, start
> finishing.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/72-process-kanban.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/72-process-kanban.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
