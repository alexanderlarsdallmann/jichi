---
title: Make the failing test pass (Racket)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/31-racket-make-it-pass/test.sh"
hints:
  - "Run it first: `raco test docs/assignments/31-racket-make-it-pass/clamp.rkt`, and read which check failed and the input it used."
  - Trace `clamp` by hand on `(clamp 9 0 5)`. The `cond` has three arms; exactly one returns the wrong value.
  - "The `[(> x hi) x]` arm returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test module alone."
---

> **Prerequisite: Racket (`raco`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Racket (racket-lang.org) -- `raco` ships with it.

The graded **Racket functional course** (the reading track is
[RACKET_PARADIGM.md](../RACKET_PARADIGM.md); this is where you *do* it) opens
the way the C course does — with a failing test and the fix-forward loop.
`docs/assignments/31-racket-make-it-pass/clamp.rkt` holds a small pure function
and a `rackunit` test module. Run it:

```sh
# in the jichi checkout (repository root)
raco test docs/assignments/31-racket-make-it-pass/clamp.rkt
```

One check fails. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again. The fix belongs in the
**function** — leave the `(module+ test ...)` block exactly as it is, because
the test is the truth here, not the obstacle. When it is green:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/31-racket-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/31-racket-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/31-racket-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
