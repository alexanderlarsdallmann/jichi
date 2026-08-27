---
title: The Rust capstone — a postfix calculator
audience: student
phase: implementation
difficulty: medium
points: 4
verify: "sh docs/assignments/66-rust-capstone/test.sh"
hints:
  - "Read `test_rpn.rs` first — it is the spec. `rpn_eval(&[Num(2), Num(3), Add])` is `Ok(5)`. There is also a test that a malformed expression is `Err(...)`. Do not edit the suite."
  - "A postfix expression is a stack machine. Use a `Vec<i64>` as the stack: `match` on the token — `Token::Num(n)` pushes `n`; an operator pops twice (`st.pop().ok_or(\"underflow\")?` — the `?` turns a `None` into an early `Err`) and pushes the result. Return `st.pop().ok_or(\"empty\".to_string())` at the end."
  - "Order matters for `Sub`: the *second* pop is the left operand, so `[Num(4), Num(2), Sub]` is `4 - 2 = 2`. The `match` on `Token` is exhaustive — handle the operator arms, and the number arm inside them with `unreachable!()`. Then write one line in DESIGN.md naming the shape (a fold over a stack)."
---

> **Prerequisite: Rust (`rustc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Rust (rustup.rs).

The capstone pulls the course together into one small, complete function over
Rust's two safety pillars: a **sum type** and a **`Result`**.
`docs/assignments/66-rust-capstone/rpn.rs` is a stub; implement `rpn_eval`.

> **An enum for the tokens, a `Result` for the outcome.** `Token` is an `enum`
> — a real sum type — so `match` on it is **exhaustive** (miss a variant and it
> will not compile). And a malformed expression is not a crash or a wrong number:
> `rpn_eval` returns `Result<i64, String>`, so the error is a **value** the caller
> must handle — Rust's rule, and jichi's (`CLAUDE.md`), enforced by the type. The
> `?` operator propagates an `Err` for you. Both `Token` and the signature are
> given; implement the body.

`test_rpn.rs` is the spec — **do not edit it**; make your implementation pass it,
including the malformed-input test. A postfix expression is a stack machine, and
a stack machine is a **fold**. Then leave a one-line `DESIGN.md` naming that
shape.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/66-rust-capstone.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/66-rust-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/66-rust-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
