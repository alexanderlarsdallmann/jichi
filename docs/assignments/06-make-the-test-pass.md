---
title: Make the failing test pass
audience: student
phase: testing
difficulty: medium
points: 3
verify: "sh docs/assignments/06-make-the-test-pass/test.sh"
hints:
  - Run the test first and *read* the failure line. It names the input that breaks and the value expected.
  - Which inputs does the failing case use? Trace stats_max by hand with exactly those numbers and watch the very first comparison.
  - The bug is the initialization of `best`. Starting at 0 silently assumes the answer is never below zero. Initialize from the data, then re-run the test.
---

> **Prerequisite: a C compiler (`cc`).** This task grades by compiling C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. The runner now says so clearly if `cc` is missing (rather than looking like a test failure).

The directory `docs/assignments/06-make-the-test-pass/` contains a small C
library (`stats.c`), its tests (`test_stats.c`), and a runner (`test.sh`).
Run the tests:

```sh
# in the jichi checkout (repository root)
sh docs/assignments/06-make-the-test-pass/test.sh
```

One test fails. Drive the agent through the loop this module teaches: run the
test, read the parsed failure, form a hypothesis, make the fix, run again.
The fix belongs in `stats.c`. Leave `test_stats.c` exactly as it is — the test
is the truth here, not the obstacle.

When the runner prints all-ok, grade yourself:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/06-make-the-test-pass.md
```

A worked reference solution exists as a sibling
(`06-make-the-test-pass.solution.md`). Solve first, then compare — the
comparison is where the learning is.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/06-make-the-test-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/06-make-the-test-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
