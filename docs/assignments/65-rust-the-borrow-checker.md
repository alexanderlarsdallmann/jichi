---
title: The borrow checker (Rust)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/65-rust-the-borrow-checker/test.sh"
hints:
  - "Run the grader: `sh docs/assignments/65-rust-the-borrow-checker/test.sh`. It does not fail a test — it fails to COMPILE: `error[E0515]: cannot return value referencing local variable \"owned\"`. Read the compiler; it is telling you the exact bug."
  - "`first_word` builds a throwaway `String` (`owned`) and returns a slice into it — but `owned` dies when the function returns, so the slice would dangle. The caller already owns the data behind `s`; borrow *that* instead."
  - "Delete the `let owned = s.to_string();` line and slice the input directly: `s.split(' ').next().unwrap()`. The returned `&str` now borrows `s`, whose lifetime outlives the call, so it compiles — and the tests pass."
---

> **Prerequisite: Rust (`rustc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Rust (rustup.rs).

`docs/assignments/65-rust-the-borrow-checker/words.rs` should return the first
word of a string. It **does not compile** — and that is the entire lesson.

> **Rust catches at compile time what C catches with a sanitizer.** The C course
> opens with a dangling pointer (task 51): a use-after-free you find later, if
> you are lucky, with AddressSanitizer. Rust's **borrow checker refuses to
> compile** the same mistake. `first_word` returns a slice that points into a
> *local* `String` — which is destroyed when the function returns, so the slice
> would dangle. The compiler is the instrument here; there is no test to reach
> until it builds.

This is a **fix**, guided by the error. Do not fight the checker — listen to it:
the caller already owns the string data behind `s`, so borrow *that* (its
lifetime outlives the call) instead of making a throwaway copy. Then the suite
(`test_words.rs`) compiles and passes.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/65-rust-the-borrow-checker.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/65-rust-the-borrow-checker.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/65-rust-the-borrow-checker.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
