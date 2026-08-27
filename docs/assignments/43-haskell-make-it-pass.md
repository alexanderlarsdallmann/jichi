---
title: Make the failing test pass (Haskell)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/43-haskell-make-it-pass/test.sh"
hints:
  - "Run it first from the task folder: `cd docs/assignments/43-haskell-make-it-pass && runghc -i. TestClamp.hs` — read which check failed and the input it used."
  - Trace `clamp` by hand on `clamp 9 0 5`. There are three guards; exactly one returns the wrong value.
  - "The `| x > hi = x` guard returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test file alone."
---

> **Prerequisite: GHC (`runghc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install GHC (Debian/Ubuntu: `sudo apt install ghc`, or ghcup.haskell.org).

The graded **Haskell functional course** (the reading track is
[HASKELL_PARADIGM.md](../HASKELL_PARADIGM.md); this is where you *do* it) opens
the way the others do — with a failing test and the fix-forward loop, now in
Haskell. `docs/assignments/43-haskell-make-it-pass/Clamp.hs` holds a small pure
function written with **guards**; `TestClamp.hs` beside it is the suite. Run it
from the task's own folder so `-i.` lets GHC find the module:

```sh
# in the jichi checkout (repository root)
cd docs/assignments/43-haskell-make-it-pass
runghc -i. TestClamp.hs
```

One check fails. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again. The fix belongs in the
**function** (`Clamp.hs`) — leave `TestClamp.hs` exactly as it is.

> **The dialect seam.** Haskell's real test tools (HUnit, hspec, QuickCheck) need
> a package manager (`cabal`/`stack`); this course needs only `runghc`, so the
> suite is a tiny **base-only** harness — a `check` that prints a mismatch and
> makes `main` end in `exitFailure`. That a test is just *a program that returns
> a failing exit code* is the whole idea a framework wraps; you can read the
> whole harness in the file. (And note Haskell's accent: every function carries a
> **type** above it, and the compiler checks it every run.)

When it is green, grade it from the project root:

```sh
# in this assignment's directory, from the block above (cd - returns to the root)
cd -      # back to the project root
jichi grade docs/assignments/43-haskell-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/43-haskell-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/43-haskell-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
