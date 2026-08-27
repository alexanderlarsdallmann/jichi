---
title: The missing defer (Zig)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/57-zig-the-missing-defer/test.sh"
hints:
  - "Run it: `zig test docs/assignments/57-zig-the-missing-defer/test_shout.zig`. It says `1 tests leaked memory` — the test allocator caught a buffer that was allocated and never freed."
  - "`shout` allocates two blocks: `out` (returned, the caller frees it) and `scratch` (a temporary, used only inside `shout`). Which one has no owner once `shout` returns?"
  - "Add `defer allocator.free(scratch);` on the line right after `scratch` is allocated. `defer` runs the free when the function returns, no matter which path it takes — the Zig idiom for exactly this."
---

> **Prerequisite: Zig (`zig`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Zig (ziglang.org).

`docs/assignments/57-zig-the-missing-defer/shout.zig` returns an uppercased heap
copy of a string — and it **leaks**. It allocates an internal temporary
(`scratch`) and never frees it.

> **Zig's leak detector is its AddressSanitizer.** In C a leak is invisible
> until a footprint gauge or valgrind finds it (that is Set D's whole subject).
> Zig builds the check into the test runner: `std.testing.allocator` **fails the
> test** the instant a test body leaks a byte. Run it and read the report:

```sh
# in the jichi checkout (repository root)
zig test docs/assignments/57-zig-the-missing-defer/test_shout.zig
```

The fix is one line, and it is the Zig habit — `defer`, which schedules a
cleanup to run when the function returns, on every path. Add the missing
`defer` so `scratch` is freed, with the suite (`test_shout.zig`) still green.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/57-zig-the-missing-defer.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/57-zig-the-missing-defer.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/57-zig-the-missing-defer.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
