---
title: From recursion to a pipeline (Haskell)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/45-haskell-loops-to-folds/test.sh"
hints:
  - "Run the tests first from the task folder (`cd docs/assignments/45-haskell-loops-to-folds && runghc -i. TestSquares.hs`) — they are green. Your job is to keep them green while changing *how* the answer is computed."
  - "Read the function as a pipeline: keep the even numbers, square each, add them up. That is `filter even`, then `map (^2)`, then `sum` — composed with `.`. No `where` helper, no `(x:xs)` recursion."
  - "`sum . map (^2) . filter even` (point-free), or `sum [x*x | x <- xs, even x]` (a comprehension). The grader rejects any hand-rolled recursion (a `(x:xs)` clause, comments and type sigs excluded), so the smell must be gone, not hidden."
---

> **Prerequisite: GHC (`runghc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install GHC (Debian/Ubuntu: `sudo apt install ghc`, or ghcup.haskell.org).

`docs/assignments/45-haskell-loops-to-folds/Squares.hs` sums the squares of the
even numbers in a list — and it works. But it is written the way you would in C:
a `where` helper that walks the list by hand, threading an accumulator through
recursion. This is a **refactor**: change *how*, not *what*.

> **Haskell has no `set!` to remove — and recursion is not a sin here.** Unlike
> the Racket/Guile task (delete a mutable accumulator) or even the Elixir one,
> Haskell embraces recursion as a first-class tool. But for a *simple aggregate*
> like this, the declarative **pipeline** — `filter`, then `map`, then `sum`,
> composed left to right — is the idiom, and hand-rolling the loop is the C
> habit. That is the smell this task removes.

Rewrite it as a combinator pipeline — no `where` helper, no `(x:xs)` clause —
with the tests (`TestSquares.hs`) still green. The grader passes only when
**both** hold: every check still passes, *and* the manual recursion is gone (it
masks type signatures and strips comments, then rejects a `(x:xs)` cons-pattern
clause, and requires a combinator). A list comprehension counts too — anything
but hand-rolling the loop. Behaviour identical, smell removed — that is the whole
discipline of refactoring under a green suite.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/45-haskell-loops-to-folds.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/45-haskell-loops-to-folds.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/45-haskell-loops-to-folds.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
