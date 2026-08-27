---
title: Design before code
audience: student
phase: design
difficulty: intermediate
points: 3
verify: "sh docs/assignments/10-design-before-code/test.sh"
hints:
  - Start from the user, not the file format. Who appends a note, and when do they search? Two sentences of problem statement will pull the requirements out of you.
  - For every design decision, force yourself to write one alternative you REJECTED and why. If you cannot name an alternative, you have not made a decision -- you have made an assumption.
  - "Structure that passes the floor: ## Problem, ## Requirements (bulleted), ## Design (with one fenced diagram or pseudo-code block), ## Alternatives considered (at least two bullets, each with a why-not), ## Test plan (bulleted). Then use /check for the part no script can grade."
---
Design a small tool **before any code exists**: `linelog`, a command-line
note log for a project. One command appends a timestamped one-line note; one
command searches past notes. Multiple people may use it in the same
repository. That is the whole prompt — turning something this vague into
requirements *is* the exercise.

Write `docs/assignments/10-design-before-code/DESIGN.md` with these sections:

```markdown
## Problem
## Requirements
## Design
## Alternatives considered
## Test plan
```

What belongs in them: the problem in the users' terms; requirements as
testable bullets (including the ones you *decline* — non-goals); the design
concrete enough that a stranger could start implementing (storage format,
command surface, failure behaviour — include at least one fenced
```-block: pseudo-code, a file-format example, or a diagram); at least two
alternatives you rejected *with the reason*; and a test plan whose bullets
could each become a real test.

**How this is graded — honestly.** `verify` here is an *artifact check*: it
confirms the structure above exists (sections present, bullets where bullets
belong, a fenced block, not trivially short). It cannot judge whether the
design is any good — prose has no mechanical floor beyond structure. For
quality, run `/check` on your document (rubric-keyed model feedback), and
work in a session so you can ask the agent to attack your requirements
("what did I fail to specify?") — plan mode is ideal for this
(see the module page).

Grade with `jichi grade docs/assignments/10-design-before-code.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/10-design-before-code.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/10-design-before-code.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
