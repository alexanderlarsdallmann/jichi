---
title: The Zig capstone — a postfix calculator
audience: student
phase: implementation
difficulty: medium
points: 4
verify: "sh docs/assignments/58-zig-capstone/test.sh"
hints:
  - "Read `test_rpn.zig` first — it is the spec. `rpnEval(&.{ .{ .num = 2 }, .{ .num = 3 }, .add })` is 5. There is also a test that a malformed expression returns `error.StackUnderflow` — not a crash. Do not edit the suite."
  - "A postfix expression is a stack machine. A fixed array is enough: `var stack: [64]i64 = undefined; var sp: usize = 0;`. `switch (tok)`: a `.num` pushes; an operator needs two on the stack (else `return error.StackUnderflow`), pops them, and pushes the result. The answer is `stack[sp - 1]` at the end."
  - "Order matters for `.sub`: the *second* pop is the left operand, so `[4, 2, .sub]` is `4 - 2 = 2`. Zig's `switch` on the `Token` union is exhaustive — handle `.num` in the operator branch with `unreachable`. Then write one line in DESIGN.md naming the shape (a fold over a stack)."
---

> **Prerequisite: Zig (`zig`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Zig (ziglang.org).

The capstone pulls the course together into one small, complete function, and
leans on the two features that make Zig a *systems* language with guardrails.
`docs/assignments/58-zig-capstone/rpn.zig` is a stub; implement `rpnEval`, which
evaluates a reverse-Polish (postfix) expression.

> **A tagged union for the tokens, an error union for the result.** A `Token` is
> `union(enum) { num: i64, add, sub, mul }` — Zig's typed sum type, so "a number
> or an operator" is a real type and the `switch` on it is **exhaustive** (miss a
> case and it will not compile). And because a malformed expression is a real
> possibility, `rpnEval` returns `RpnError!i64` — an **error union**: errors as
> values, checked by the compiler. That is jichi's house rule (`CLAUDE.md`), made
> a language feature. Both types are given; implement the function.

`test_rpn.zig` is the spec — **do not edit it**; make your implementation pass
it (including the malformed-input test). A postfix expression is a stack machine,
and a stack machine is a **fold** — carry a stack, push numbers,
pop-two-and-push on an operator, and the answer is the top of the final stack.
Then leave a one-line `DESIGN.md` naming that shape.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/58-zig-capstone.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/58-zig-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/58-zig-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
