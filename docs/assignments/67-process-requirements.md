---
title: Requirements — say what it must do, testably
audience: student
phase: planning
difficulty: easy
points: 2
verify: "sh docs/assignments/67-process-requirements/test.sh"
hints:
  - "Run the grader: `jichi grade docs/assignments/67-process-requirements.md`. It wants at least 5 requirements, each with an id (R1, R2, ...) and a testable 'shall'/'must'."
  - "'It should be fast' is a wish, not a requirement — you cannot check it. 'R1: the service shall respond within 200ms' you can. Give each requirement an id and a verifiable 'shall'."
  - "Rewrite each vague line as `R<n>: The <thing> shall <do something specific and checkable>`. Numbers and conditions make it testable."
---
The **process curriculum** opens where every project should: deciding *what it
must do* before deciding *how*. `REQUIREMENTS.md` in this task's folder holds a
beginner's first attempt — a list of vague wishes ("it should be fast and nice").
Turn it into real requirements.

A requirement is a **testable statement** of what the thing must do. The two
marks of a good one: it has an **id** (so a design and a test can point back at
it — R1, R2, …), and it is phrased so you could actually *check* whether it holds
(a "shall"/"must", a number, a condition) — not a feeling.

> **The floor vs. the judgment.** The grader checks the *structure* — ≥5 ids, each
> verifiably phrased. It cannot check whether these are the **right** requirements,
> or complete — that is your judgment, and the harder, more valuable half. A doc
> that passes the floor can still be the wrong requirements; the floor only stops
> the vaguest mistakes.

Rewrite `REQUIREMENTS.md` so at least five requirements each carry an id and a
testable "shall"/"must". Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/67-process-requirements.md
```

> **If you are stuck alone:** for each wish, ask "how would I *test* that it's
> done?" If you can't, it isn't a requirement yet — make it specific enough to
> check.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/67-process-requirements.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/67-process-requirements.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
