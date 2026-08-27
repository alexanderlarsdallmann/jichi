---
title: Write the test first (Zig)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/56-zig-test-first/test.sh"
hints:
  - "`listMax` passes on the slices people usually try. Which slice would it get *wrong*? Think about what value it starts from."
  - "Write `test_list_max.zig` — start from the skeleton in the task body, `const lm = @import(\"list_max.zig\");`, and add at least three `try std.testing.expectEqual(...)` checks. Include one for a slice of *only negative* numbers, and watch it fail first."
  - "`var m: i64 = 0;` seeds at 0, so an all-negative slice can never beat 0. Seed from the data — `var m = xs[0];` and loop from index 1 (or use `std.mem.max(i64, xs)`). Write the failing test, then fix."
---

> **Prerequisite: Zig (`zig`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Zig (ziglang.org).

`docs/assignments/56-zig-test-first/list_max.zig` returns the largest element of
a slice. It looks right and passes on the usual slices. It is wrong — the same
bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test_list_max.zig` next to `list_max.zig` and write a suite that pins the
correct behaviour — including the case that exposes the bug. Start from this
skeleton:

```zig
const std = @import("std");
const lm = @import("list_max.zig");

test "list_max" {
    // ... at least three expectEqual checks, incl. an all-negative slice ...
}
```

Watch it **fail first** (`zig test test_list_max.zig`), then fix `list_max.zig`.

The grader checks all three: your test exists and passes, it has at least three
`std.testing.expect` checks (a hollow suite is not proof), and an independent
probe confirms the bug is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/56-zig-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/56-zig-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/56-zig-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
