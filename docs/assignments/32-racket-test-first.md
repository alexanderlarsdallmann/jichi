---
title: Write the test first (Racket)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/32-racket-test-first/test.sh"
hints:
  - "`list-max` passes on the lists people usually try. Which list would it get *wrong*? Think about what value it starts folding from."
  - "Write `test-list-max.rkt` — `(require \"list-max.rkt\")`, then a `(module+ test ...)` with `rackunit` checks. Include one for a list of *only negative* numbers, and watch it fail first."
  - "`(foldl max 0 lst)` seeds the fold at 0, so an all-negative list can never beat 0. Seed from the data: `(foldl max (car lst) (cdr lst))`. Write the failing test, then fix."
---

> **Prerequisite: Racket (`raco`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Racket (racket-lang.org) -- `raco` ships with it.

`docs/assignments/32-racket-test-first/list-max.rkt` returns the largest
element of a list. It looks right and passes on the lists people usually try.
It is wrong — the same bug the C course's `stats_max` had, in a functional
coat.

This task is **tests as proof**. There is no test file yet; you write it.
Create `test-list-max.rkt` next to `list-max.rkt`, `(require "list-max.rkt")`,
and write a `rackunit` `(module+ test ...)` that pins the correct behaviour —
including the case that exposes the bug. Watch it **fail first** (that is the
proof the bug is real), then fix `list-max.rkt` so your test goes green.

The grader checks all three: your test exists and passes, it has at least three
checks (a hollow suite is not proof), and an independent probe confirms the bug
is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/32-racket-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/32-racket-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/32-racket-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
