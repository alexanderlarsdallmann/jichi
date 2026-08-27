---
title: Write the test first (Clojure)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/48-clojure-test-first/test.sh"
hints:
  - "`list-max` passes on the collections people usually try. Which one would it get *wrong*? Think about what value it starts reducing from."
  - "Write `test_list_max.clj` — start from the clojure.test skeleton in the task body, `(load-file \"list_max.clj\")`, and add at least three `(is ...)` checks. Include one for a list of *only negative* numbers, and watch it fail first."
  - "`(reduce max 0 coll)` seeds the reduce at 0, so an all-negative list can never beat 0. Seed from the data — `(apply max coll)`, or `(reduce max coll)`. Write the failing test, then fix."
---

> **Prerequisite: Clojure (`clojure`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install the Clojure CLI (clojure.org/guides/install_clojure).

`docs/assignments/48-clojure-test-first/list_max.clj` returns the largest element
of a collection. It looks right and passes on the ones people usually try. It is
wrong — the same bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test_list_max.clj` next to `list_max.clj` and write a **clojure.test** suite
that pins the correct behaviour — including the case that exposes the bug. Start
from this skeleton:

```clojure
(load-file "list_max.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])

(deftest list-max-test
  ;; ... at least three (is ...) checks, incl. an all-negative list ...
  )

(System/exit (if (successful? (run-tests)) 0 1))
```

Watch it **fail first** (that is the proof the bug is real), then fix
`list_max.clj` so your test goes green.

The grader checks all three: your test exists and passes, it has at least three
`(is ...)` checks (a hollow suite is not proof), and an independent probe
confirms the bug is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/48-clojure-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/48-clojure-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/48-clojure-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
