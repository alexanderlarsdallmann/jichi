---
title: Write the test first (Guile)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/36-guile-test-first/test.sh"
hints:
  - "`list-max` passes on the lists people usually try. Which list would it get *wrong*? Think about what value it starts folding from."
  - "Write `test-list-max.scm` — start from the SRFI-64 skeleton in the task body, `(use-modules (srfi srfi-64) (list-max))`, and add at least three `(test-equal …)` checks. Include one for a list of *only negative* numbers, and watch it fail first."
  - "`(fold max 0 lst)` seeds the fold at 0, so an all-negative list can never beat 0. Seed from the data: `(fold max (car lst) (cdr lst))`. Write the failing test, then fix."
---

> **Prerequisite: GNU Guile (`guile`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install GNU Guile (Debian/Ubuntu: `sudo apt install guile-3.0`).

`docs/assignments/36-guile-test-first/list-max.scm` returns the largest element
of a list. It looks right and passes on the lists people usually try. It is
wrong — the same bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test-list-max.scm` next to `list-max.scm` and write an **SRFI-64** suite that
pins the correct behaviour — including the case that exposes the bug. Start from
this skeleton (the runner + `(exit …)` plumbing is given, so you focus on the
checks; that plumbing is the Guile-vs-Racket dialect seam):

```scheme
(use-modules (srfi srfi-64) (list-max))
(define r (test-runner-create))
(test-with-runner r
  (test-begin "list-max")
  ;; ... at least three (test-equal …) checks, incl. an all-negative list ...
  (test-end "list-max"))
(exit (zero? (test-runner-fail-count r)))
```

Watch it **fail first** (that is the proof the bug is real), then fix
`list-max.scm` so your test goes green.

The grader checks all three: your test exists and passes, it has at least three
checks (a hollow suite is not proof), and an independent probe confirms the bug
is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/36-guile-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/36-guile-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/36-guile-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
