---
title: Make the failing test pass (C++)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/59-cpp-make-it-pass/test.sh"
hints:
  - "Run the grader: `sh docs/assignments/59-cpp-make-it-pass/test.sh`. It compiles test_clamp.cpp with a C++ compiler + AddressSanitizer and runs it; a failing `assert` aborts."
  - Trace `clamp` by hand on `clamp(9, 0, 5)`. Three returns; exactly one is wrong.
  - "The `if (x > hi) return x;` line returns `x` when `x` is above `hi` — it should return `hi`. Fix `clamp.hpp`; leave the test alone."
---

> **Prerequisite: a C++ compiler with AddressSanitizer (`g++`/`clang++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

The graded **C++ systems course** (the migration track is
[CPP_INTEROP.md](../CPP_INTEROP.md); this is a standalone course in C++'s own
systems model — **RAII** and ownership, the standard containers, and exceptions)
opens with a failing test and the fix-forward loop.

`docs/assignments/59-cpp-make-it-pass/clamp.hpp` holds a small function;
`test_clamp.cpp` is a base-only suite (no gtest/Catch2 needed — `<cassert>` plus
a `main`, compiled with AddressSanitizer). Run it and fix the **function**:

```sh
# in the jichi checkout (repository root)
sh docs/assignments/59-cpp-make-it-pass/test.sh
```

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/59-cpp-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/59-cpp-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/59-cpp-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
