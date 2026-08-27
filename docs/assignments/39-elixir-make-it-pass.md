---
title: Make the failing test pass (Elixir)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/39-elixir-make-it-pass/test.sh"
hints:
  - "Run it first from the task folder: `cd docs/assignments/39-elixir-make-it-pass && elixir test_clamp.exs` — read which check failed and the input it used."
  - Trace `clamp` by hand on `clamp(9, 0, 5)`. There are three function clauses; exactly one returns the wrong value.
  - "The `when x > hi, do: x` clause returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test file alone."
---

> **Prerequisite: Elixir (`elixir`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Elixir/OTP (elixir-lang.org).

The graded **Elixir functional course** (the reading track is
[ELIXIR_PARADIGM.md](../ELIXIR_PARADIGM.md); this is where you *do* it) opens the
way the Racket and Guile ones do — with a failing test and the fix-forward loop,
now in Elixir. `docs/assignments/39-elixir-make-it-pass/clamp.exs` holds a small
pure function written as **guarded function clauses**; `test_clamp.exs` beside it
is an **ExUnit** suite (Elixir's built-in test framework). Run it from the task's
own folder so `Code.require_file` resolves the module:

```sh
# in the jichi checkout (repository root)
cd docs/assignments/39-elixir-make-it-pass
elixir test_clamp.exs
```

One check fails. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again. The fix belongs in the
**function** (`clamp.exs`) — leave `test_clamp.exs` exactly as it is, because the
test is the truth here, not the obstacle.

> **The dialect seam.** Where Guile's SRFI-64 needed an explicit runner and
> `(exit …)`, ExUnit's autorun sets the process exit code for you — so an
> Elixir suite is just `ExUnit.start()` and `test` blocks. Same paradigm, its
> own plumbing; the reading track calls out these drifts.

When it is green, grade it from the project root:

```sh
# in this assignment's directory, from the block above (cd - returns to the root)
cd -      # back to the project root
jichi grade docs/assignments/39-elixir-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/39-elixir-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/39-elixir-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
