---
title: Make the failing test pass (Clojure)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/47-clojure-make-it-pass/test.sh"
hints:
  - "Run it first from the task folder: `cd docs/assignments/47-clojure-make-it-pass && clojure test_clamp.clj` — read which check failed and the input it used."
  - Trace `clamp` by hand on `(clamp 9 0 5)`. The `cond` has three arms; exactly one returns the wrong value.
  - "The `(> x hi) x` arm returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test file alone."
---

> **Prerequisite: Clojure (`clojure`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install the Clojure CLI (clojure.org/guides/install_clojure).

The graded **Clojure functional course** (the reading track is
[CLOJURE_PARADIGM.md](../CLOJURE_PARADIGM.md); this is where you *do* it) opens
the way the others do — with a failing test and the fix-forward loop, now in
Clojure, a Lisp on the JVM. `docs/assignments/47-clojure-make-it-pass/clamp.clj`
holds a small pure function; `test_clamp.clj` beside it is a **clojure.test**
suite (it ships with the language). Run it from the task's own folder:

```sh
# in the jichi checkout (repository root)
cd docs/assignments/47-clojure-make-it-pass
clojure test_clamp.clj
```

One check fails. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again. The fix belongs in the
**function** (`clamp.clj`) — leave `test_clamp.clj` exactly as it is.

> **The dialect seam.** Like Guile's SRFI-64 (and unlike ExUnit), Clojure's
> `run-tests` reports pass/fail but does **not** set the process exit code — so
> the suite ends with `(System/exit (if (successful? (run-tests)) 0 1))`, which
> is what turns a failing run into a nonzero exit a grader can read. Same
> paradigm, its own plumbing.

When it is green, grade it from the project root:

```sh
# in this assignment's directory, from the block above (cd - returns to the root)
cd -      # back to the project root
jichi grade docs/assignments/47-clojure-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/47-clojure-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/47-clojure-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
