---
title: Make the failing test pass (Rust)
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/63-rust-make-it-pass/test.sh"
hints:
  - "Run it: `rustc --test --edition 2021 docs/assignments/63-rust-make-it-pass/test_clamp.rs -o t && ./t`. Read which test failed."
  - Trace `clamp` by hand on `clamp(9, 0, 5)`. Three returns; exactly one is wrong.
  - "The `return x;` inside `if x > hi` returns `x` when `x` is above `hi` — it should return `hi`. Fix `clamp.rs`; leave the test alone."
---

> **Prerequisite: Rust (`rustc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Rust (rustup.rs).

The graded **Rust systems course** (Rust is the systems family's fourth language;
[RUST_INTEROP.md](../RUST_INTEROP.md) is the reading track — Rust has no *gradual*
C-migration arc, but it has a systems model all its own worth doing) opens with a
failing test and the fix-forward loop.

`docs/assignments/63-rust-make-it-pass/clamp.rs` holds a function; `test_clamp.rs`
is the suite. Rust needs no cargo here — `rustc --test` builds a runner from the
`#[test]` functions:

```sh
# in the jichi checkout (repository root)
sh docs/assignments/63-rust-make-it-pass/test.sh
```

Fix the **function** so every test passes; leave `test_clamp.rs` alone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/63-rust-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/63-rust-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/63-rust-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
