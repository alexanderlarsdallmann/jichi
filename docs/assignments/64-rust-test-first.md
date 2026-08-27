---
title: Write the test first (Rust)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/64-rust-test-first/test.sh"
hints:
  - "`list_max` passes on the slices people usually try. Which one would it get *wrong*? Think about what value it starts from."
  - "Write `test_list_max.rs` — start from the skeleton in the task body (`#[path = \"list_max.rs\"] mod list_max;`), and add at least three `assert_eq!` checks. Include one for a slice of *only negative* numbers, and watch it fail first."
  - "`let mut m = 0;` seeds at 0, so an all-negative slice can never beat 0. Seed from the data — `let mut m = xs[0];` and iterate `xs[1..]` (or `*xs.iter().max().unwrap()`). Write the failing test, then fix."
---

> **Prerequisite: Rust (`rustc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Rust (rustup.rs).

`docs/assignments/64-rust-test-first/list_max.rs` returns the largest element of
a slice. It looks right and passes on the usual slices. It is wrong — the same
bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test_list_max.rs` next to `list_max.rs` and pin the correct behaviour, including
the case that exposes the bug:

```rust
#[path = "list_max.rs"]
mod list_max;
use list_max::list_max;

#[test]
fn cases() {
    // ... at least three assert_eq! checks, incl. an all-negative slice ...
}
```

Watch it **fail first**, then fix `list_max.rs`. The grader checks all three:
your test exists and passes, it has at least three `assert`/`assert_eq!` checks
(a hollow suite is not proof), and an independent probe confirms the bug is gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/64-rust-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/64-rust-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/64-rust-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
