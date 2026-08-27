---
title: From recursion to Enum (Elixir)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/41-elixir-loops-to-folds/test.sh"
hints:
  - "Run the tests first from the task folder (`cd docs/assignments/41-elixir-loops-to-folds && elixir test_squares.exs`) — they are green. Your job is to keep them green while changing *how* the answer is computed."
  - "Read the function as a pipeline: keep the even numbers, square each, add them up. That is `Enum.filter`, then `Enum.map`, then `Enum.sum` — fed left to right with the pipe `|>`. No private helper, no manual `[h | t]` recursion."
  - "`list |> Enum.filter(&(rem(&1, 2) == 0)) |> Enum.map(&(&1 * &1)) |> Enum.sum()`. The grader rejects any hand-rolled recursion (a `defp` helper or a `[h | t]` clause, comments stripped first), so the smell must be gone, not hidden."
---

> **Prerequisite: Elixir (`elixir`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Elixir/OTP (elixir-lang.org).

`docs/assignments/41-elixir-loops-to-folds/squares.exs` sums the squares of the
even numbers in a list — and it works. But it is written the way you would in C:
a `loop` that walks the list by hand, threading an accumulator through a private
recursive helper. This is a **refactor**: change *how*, not *what*.

> **Elixir has no `set!` to remove.** The Racket and Guile versions of this task
> delete a mutable accumulator; Elixir has no mutable variables at all, so there
> is nothing to strip. The imperative smell here is subtler and just as real:
> **hand-rolled recursion where `Enum` already does the job.** Reaching for a
> manual `[h | t]` loop is the C habit; reaching for `Enum` + the pipe is the
> Elixir one.

Rewrite it functionally — no private recursive helper, no `[h | t]` clause — as
an `Enum` pipeline (`Enum.filter |> Enum.map |> Enum.sum`), with the tests
(`test_squares.exs`) still green. The grader passes only when **both** hold:
every check still passes, *and* the manual recursion is gone (it strips comments,
then rejects a `defp` helper or a cons-pattern clause, and requires `Enum`). A
list comprehension (`for x <- list, …`) is fine too — anything but hand-rolling
the loop. Behaviour identical, smell removed — that is the whole discipline of
refactoring under a green suite.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/41-elixir-loops-to-folds.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/41-elixir-loops-to-folds.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/41-elixir-loops-to-folds.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
