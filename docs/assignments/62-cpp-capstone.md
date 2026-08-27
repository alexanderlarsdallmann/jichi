---
title: The C++ capstone — a postfix calculator
audience: student
phase: implementation
difficulty: medium
points: 4
verify: "sh docs/assignments/62-cpp-capstone/test.sh"
hints:
  - "Read `test_rpn.cpp` first — it is the spec. `rpn_eval({{Token::Num,2},{Token::Num,3},{Token::Add,0}})` is 5. There is also a test that a malformed expression throws `std::runtime_error`. Do not edit the suite."
  - "A postfix expression is a stack machine. Use a `std::vector<long>` as the stack: a `Num` pushes its value; an operator needs two on the stack (else `throw std::runtime_error(\"...\")`), pops them with `back()`/`pop_back()`, and pushes the result. The answer is `back()` at the end."
  - "Order matters for `Sub`: the *second* pop is the left operand, so `{4, 2, Sub}` is `4 - 2 = 2`. Then write one line in DESIGN.md naming the shape (a fold over a stack)."
---

> **Prerequisite: a C++ compiler with AddressSanitizer (`g++`/`clang++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

The capstone pulls the course together into one small, complete function, over
the two C++ pillars: a **standard container** for state and **exceptions** for
errors. `docs/assignments/62-cpp-capstone/rpn.hpp` is a stub; implement
`rpn_eval`, which evaluates a reverse-Polish (postfix) expression.

> **`std::vector` for the stack, an exception for the error.** A `Token` is a
> `kind` tag plus a value (C++ has no built-in sum type; this is the common
> encoding, and the `Token` type is given). Hold the working stack in a
> `std::vector<long>` — it owns its memory, so there is nothing to leak. And when
> the expression is malformed, **throw `std::runtime_error`** rather than return
> a wrong number: exceptions are C++'s error channel, the counterpart of the
> error values the Zig and Haskell capstones return.

`test_rpn.cpp` is the spec — **do not edit it**; make your implementation pass
it, including the throws-on-malformed test. A postfix expression is a stack
machine, and a stack machine is a **fold**. Then leave a one-line `DESIGN.md`
naming that shape.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/62-cpp-capstone.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/62-cpp-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/62-cpp-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
