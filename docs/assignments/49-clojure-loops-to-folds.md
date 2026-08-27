---
title: From an atom to a fold (Clojure)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/49-clojure-loops-to-folds/test.sh"
hints:
  - "Run the tests first from the task folder (`cd docs/assignments/49-clojure-loops-to-folds && clojure test_squares.clj`) — they are green. Your job is to keep them green while changing *how* the answer is computed."
  - "Read the function as a pipeline: keep the even numbers, square each, add them up. That is `filter`, then `map`, then `reduce +` — threaded with `->>`. No atom, no accumulator to `swap!`."
  - "`(->> coll (filter even?) (map #(* % %)) (reduce + 0))`. The grader greps for `atom`/`swap!`/`reset!` (comments stripped first), so the smell must be gone, not hidden."
---

> **Prerequisite: Clojure (`clojure`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install the Clojure CLI (clojure.org/guides/install_clojure).

`docs/assignments/49-clojure-loops-to-folds/squares.clj` sums the squares of the
even numbers in a collection — and it works. But it is written the way you would
in C: a mutable accumulator — a Clojure **atom** — that a `doseq` loop reassigns
with `swap!`. This is a **refactor**: change *how*, not *what*.

> **The atom is Clojure's `set!`.** Clojure is immutable by default, but it gives
> you `atom` (and `ref`, `agent`) as a *managed-mutation* escape hatch for
> genuinely stateful, concurrent work ([CLOJURE_PARADIGM.md](../CLOJURE_PARADIGM.md)
> § managed references). Reaching for one to sum a list is the imperative habit
> in a functional coat — the exact analog of the `set!` the Racket and Guile
> tasks remove. That is the smell this task removes.

Rewrite it functionally — no `atom`, no `swap!`/`reset!` — as a `reduce`/`->>`
pipeline (`filter` / `map` / `reduce +`), with the tests (`test_squares.clj`)
still green. The grader passes only when **both** hold: every check still passes,
*and* the mutation is gone (it strips comments, then greps for `atom`/`swap!`/
`reset!`, and requires a combinator). Behaviour identical, smell removed — that
is the whole discipline of refactoring under a green suite.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/49-clojure-loops-to-folds.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/49-clojure-loops-to-folds.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/49-clojure-loops-to-folds.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
