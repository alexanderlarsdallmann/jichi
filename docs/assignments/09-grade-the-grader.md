---
title: Grade the grader
audience: student
phase: testing
difficulty: advanced
points: 4
verify: "sh docs/assignments/09-grade-the-grader/test.sh"
hints:
  - A checker that only tries median3(1,2,3) accepts all four candidates. What CLASSES of input exist for three integers?
  - Think in classes, not cases -- the orderings of three distinct values (there are six), values that tie, and values below zero. Each wrong candidate is wrong in exactly one class.
  - "Write a main() that checks all six orderings of {1,2,3}, a tie case like median3(2,2,5)==2, and a negative case like median3(-1,0,1)==0. Compile the candidate with it; exit nonzero on any miss."
---
Until now, someone else's `verify` graded you. This task inverts the seats:
**you write the checker**, and the checker is what gets graded — because a
verifier that passes wrong code is worse than no verifier at all (it
manufactures false confidence; see ANECDOTES #17 for the day that bit this
project).

> **Prerequisite: a C compiler (`cc`).** You compile C yourself in this task. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

`docs/assignments/09-grade-the-grader/` contains the contract
(`median3.h`: return the middle of three ints) and **four candidate
implementations** in `candidates/` — exactly one is correct; the other three
are *subtly* wrong, each in a different way. All four look plausible. All
four pass a lazy test.

Write `docs/assignments/09-grade-the-grader/check.sh`: a POSIX shell script
that takes one argument — the path of a candidate `.c` file — and exits **0**
if that candidate implements the contract, **nonzero** otherwise. Compile it
together with a test `main` of your own design (`cc -std=c89 -I. …` from the
task directory finds `median3.h`); temporary files in the task directory are
fine.

You are graded mechanically on discrimination, not on style: the runner
executes your `check.sh` against all four candidates and passes only if you
**accept the one correct candidate and reject the other three**.

Reading the candidates is allowed — real reviewers read code. Cheating by
*name* is not possible: the runner hands your checker each candidate under a
neutral filename with its header comment stripped, so only its **behaviour**
can tell you which one it is. (Matching on the remaining code text would
still work and still teach you nothing; you would only be grading yourself
hollow — the exact disease this task exists to cure.)

Grade with `jichi grade docs/assignments/09-grade-the-grader.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/09-grade-the-grader.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/09-grade-the-grader.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
