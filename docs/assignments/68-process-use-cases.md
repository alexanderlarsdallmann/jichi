---
title: Use-cases — who does what, and what if it fails
audience: student
phase: planning
difficulty: easy
points: 2
verify: "sh docs/assignments/68-process-use-cases/test.sh"
hints:
  - "Run the grader. It wants at least 3 use-cases, each naming an Actor, a Trigger, and BOTH a success and a failure path."
  - "A use-case is a little story: an *actor* (who), a *trigger* (what starts it), the *success* path, and — the part beginners forget — a *failure* path (what happens when it goes wrong)."
  - "For each use-case write four labelled lines: `Actor:`, `Trigger:`, `Success:`, `Failure:`. The failure path is half the value — the happy path alone is not a use-case."
---
A requirement says *what* the system must do; a **use-case** shows *how someone
uses it* to get value — and, crucially, what happens when things go wrong.
`USE_CASES.md` here is a vague sketch; turn it into real use-cases that cover the
system's main jobs.

Each use-case is a small story with four parts: an **Actor** (who), a **Trigger**
(what starts it), a **Success** path (the happy outcome), and a **Failure**
path (what happens when it doesn't work — the part beginners always skip, and the
part that catches the real design questions).

> **The floor vs. the judgment.** The grader checks that you have ≥3 use-cases
> each with an actor, a trigger, and a failure path. Whether they actually *cover*
> the requirements — that no important interaction is missing — is your judgment.

Rewrite `USE_CASES.md` with at least three use-cases, each carrying all four
parts. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/68-process-use-cases.md
```

> **If you are stuck alone:** for each use-case, ask "what's the worst that
> happens here?" — that is your failure path, and it usually reveals a
> requirement you missed.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/68-process-use-cases.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/68-process-use-cases.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
