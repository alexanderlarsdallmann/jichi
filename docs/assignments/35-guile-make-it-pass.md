---
title: Make the failing test pass (Guile)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/35-guile-make-it-pass/test.sh"
hints:
  - "Run it first from the task folder: `cd docs/assignments/35-guile-make-it-pass && guile -L . test-clamp.scm` — read which check failed and the input it used."
  - Trace `clamp` by hand on `(clamp 9 0 5)`. The `cond` has three arms; exactly one returns the wrong value.
  - "The `((> x hi) x)` arm returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test file alone."
---

> **Prerequisite: GNU Guile (`guile`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install GNU Guile (Debian/Ubuntu: `sudo apt install guile-3.0`).

The graded **Guile functional course** (the reading track is
[GUILE_PARADIGM.md](../GUILE_PARADIGM.md); this is where you *do* it) opens the
way the Racket one does — with a failing test and the fix-forward loop, now in
Guile's dialect of Scheme. `docs/assignments/35-guile-make-it-pass/clamp.scm`
holds a small pure function; `test-clamp.scm` beside it is an **SRFI-64** suite
(Guile's cousin of Racket's `rackunit`). Run it from the task's own folder — so
Guile finds the module with `-L .` and srfi-64's `.log` stays out of your
project root:

```sh
# in the jichi checkout (repository root)
cd docs/assignments/35-guile-make-it-pass
guile -L . test-clamp.scm
```

One check fails. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again. The fix belongs in the
**function** (`clamp.scm`) — leave `test-clamp.scm` exactly as it is, because the
test is the truth here, not the obstacle.

> **The dialect seam.** Unlike `raco test`, SRFI-64 does not set the process
> exit code for you — that is what the `(test-runner-create)` + final
> `(exit (zero? (test-runner-fail-count r)))` lines in the suite are for. Same
> paradigm as Racket, cousin plumbing; the reading track calls out these drifts.

When it is green, grade it from the project root:

```sh
# in this assignment's directory, from the block above (cd - returns to the root)
cd -      # back to the project root
jichi grade docs/assignments/35-guile-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/35-guile-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/35-guile-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
