---
title: Make the failing test pass (Zig)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/55-zig-make-it-pass/test.sh"
hints:
  - "Run it: `zig test docs/assignments/55-zig-make-it-pass/test_clamp.zig`. Read which test failed and the input it used."
  - Trace `clamp` by hand on `clamp(9, 0, 5)`. There are three returns; exactly one is wrong.
  - "The `if (x > hi) return x;` line returns `x` when `x` is above `hi` — it should return `hi`. Fix the function; leave the test file alone."
---

> **Prerequisite: Zig (`zig`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Zig (ziglang.org).

The graded **Zig systems course** (the migration track is
[ZIG_INTEROP.md](../ZIG_INTEROP.md); this is a standalone course in Zig's own
systems model — explicit allocators, `defer`, error unions, and a test runner
built into the compiler) opens with a failing test and the fix-forward loop.

`docs/assignments/55-zig-make-it-pass/clamp.zig` holds a small function;
`test_clamp.zig` is the suite. Zig needs no test framework — `zig test` finds
every `test` block and exits nonzero if any fails:

```sh
# in the jichi checkout (repository root)
zig test docs/assignments/55-zig-make-it-pass/test_clamp.zig
```

Fix the **function** so every test passes; leave `test_clamp.zig` alone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/55-zig-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/55-zig-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/55-zig-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
