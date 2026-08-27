---
title: The Racket capstone — a postfix calculator
audience: student
phase: implementation
difficulty: medium
points: 4
verify: "sh docs/assignments/34-racket-capstone/test.sh"
hints:
  - "Read `test-rpn.rkt` first — it is the spec. `(rpn-eval '(2 3 +))` is 5, `(rpn-eval '(2 3 4 * +))` is 14. Do not edit the suite."
  - "A postfix expression is a stack machine: a number pushes itself; an operator pops the top two and pushes the result. `foldl` a `step` function over the tokens, carrying the stack as the accumulator; the answer is the top of the final stack."
  - "Order matters for `-`: with a stack, the *second* pop is the left operand. `(4 2 -)` is `4 - 2 = 2`. Then write one line in DESIGN.md naming the shape (a fold over a stack)."
---

> **Prerequisite: Racket (`raco`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Racket (racket-lang.org) -- `raco` ships with it.

The capstone pulls the course together into one small, complete **pure module**.
`docs/assignments/34-racket-capstone/rpn.rkt` is a stub; implement `rpn-eval`,
which evaluates a reverse-Polish (postfix) expression — a list of numbers and
the operator symbols `'+ '- '*`:

```racket
(rpn-eval '(2 3 +))       ; => 5
(rpn-eval '(4 2 -))       ; => 2     (order matters)
(rpn-eval '(2 3 4 * +))   ; => 14    (3*4, then +2)
(rpn-eval '(10 2 3 + -))  ; => 5     (10 - (2+3))
```

`test-rpn.rkt` is the spec — a `rackunit` suite. **Do not edit it**; make your
implementation pass it. This is a functional problem with a functional shape: a
postfix expression is a stack machine, and a stack machine is a **fold** — carry
the stack as the accumulator, push numbers, pop-two-and-push on an operator, and
the answer is the top of the final stack:

```mermaid
flowchart LR
  T["tokens: (10 2 3 + -)"] --> S["foldl a step over tokens<br/>(acc = the stack)"]
  S --> N{number or operator?}
  N -->|number| P["push it onto the stack"]
  N -->|"op (+ - *)"| O["pop b, then pop a<br/>push (a op b)"]
  P --> S
  O --> S
  S --> R["answer = top of the final stack"]
```

The operand order is the one gotcha: with a stack, the *second* pop is the left
operand, so `(4 2 -)` is `4 - 2 = 2`. Then leave a one-line `DESIGN.md` naming
that shape (the artifact floor: say what you built before you call it done).

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/34-racket-capstone.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/34-racket-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/34-racket-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
