---
title: From loops to folds (Racket)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/33-racket-loops-to-folds/test.sh"
hints:
  - Run the tests first (`raco test docs/assignments/33-racket-loops-to-folds/squares.rkt`) — they are green. Your job is to keep them green while changing *how* the answer is computed.
  - "Read the function as a pipeline: keep the even numbers, square each, add them up. That is `filter`, then `map`, then `foldl` — no accumulator to reassign."
  - "`(foldl + 0 (map (lambda (x) (* x x)) (filter even? lst)))`. The grader fails on any `set!`/mutation, so the smell must be gone, not hidden."
---

> **Prerequisite: Racket (`raco`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Racket (racket-lang.org) -- `raco` ships with it.

`docs/assignments/33-racket-loops-to-folds/squares.rkt` sums the squares of the
even numbers in a list — and it works. But it is written the way you would in
C: a mutable accumulator (`define total 0`) and a loop that reassigns it with
`set!`. This is a **refactor**: change *how*, not *what*.

Rewrite it functionally — no `set!`, no mutation — as a pipeline of
`filter` / `map` / `foldl` (or `for/fold`), with the tests still green. The
grader passes only when **both** hold: every check still passes, *and* the
mutation is gone (it greps for `set!` and friends, so removing the smell must
be real, not disguised). Behaviour identical, smell removed — that is the whole
discipline of refactoring under a green suite.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/33-racket-loops-to-folds.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/33-racket-loops-to-folds.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/33-racket-loops-to-folds.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
