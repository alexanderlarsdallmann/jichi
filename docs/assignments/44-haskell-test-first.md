---
title: Write the test first (Haskell)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/44-haskell-test-first/test.sh"
hints:
  - "`listMax` passes on the lists people usually try. Which list would it get *wrong*? Think about what value it starts folding from."
  - "Write `TestListMax.hs` — start from the base-only skeleton in the task body, `import ListMax (listMax)`, and add at least three `check` calls. Include one for a list of *only negative* numbers, and watch it fail first."
  - "`foldl max 0` seeds the fold at 0, so an all-negative list can never beat 0. Seed from the data — `maximum`, or `foldl1 max`. Write the failing test, then fix."
---

> **Prerequisite: GHC (`runghc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install GHC (Debian/Ubuntu: `sudo apt install ghc`, or ghcup.haskell.org).

`docs/assignments/44-haskell-test-first/ListMax.hs` returns the largest element
of a list. It looks right and passes on the lists people usually try. It is
wrong — the same bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`TestListMax.hs` next to `ListMax.hs` and write a base-only suite that pins the
correct behaviour — including the case that exposes the bug. Start from this
skeleton (the `check` harness is given, so you focus on the checks):

```haskell
module Main where

import ListMax (listMax)
import System.Exit (exitFailure, exitSuccess)

check :: (Eq a, Show a) => a -> a -> IO Bool
check got want
  | got == want = return True
  | otherwise   = putStrLn ("FAIL: got " ++ show got ++ ", want " ++ show want) >> return False

main :: IO ()
main = do
  results <-
    sequence
      [ -- ... at least three `check` calls, incl. an all-negative list ...
      ]
  if and results then exitSuccess else exitFailure
```

Watch it **fail first** (that is the proof the bug is real), then fix
`ListMax.hs` so your test goes green. (A negative literal needs parentheses in an
argument: `check (listMax [-3,-1,-7]) (-1)`.)

The grader checks all three: your test exists and passes, it has at least three
`check` calls (a hollow suite is not proof), and an independent probe confirms
the bug is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/44-haskell-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/44-haskell-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/44-haskell-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
