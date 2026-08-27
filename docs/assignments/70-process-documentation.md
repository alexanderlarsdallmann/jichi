---
title: Documentation — can a stranger use it?
audience: student
phase: documentation
difficulty: easy
points: 2
verify: "sh docs/assignments/70-process-documentation/test.sh"
hints:
  - "Run the grader. It wants a README with an install section, a run/usage section, and at least one worked example (a fenced code block a reader can copy)."
  - "Write for a stranger who has never seen your project: how do they install it, how do they run it, and one concrete example they can copy and try. 'It does stuff and works well' helps nobody."
  - "Add three things to `README.md`: an `## Install` section, a `## Usage` section, and a fenced code block (triple backticks) showing a real command and its result."
---
Documentation is where a project stops being *yours only* and becomes something
someone else (including future-you) can use. The test of a README is simple and
brutal: **can a stranger install it, run it, and see it work, without asking
you?** `README.md` here is the "it does stuff and works well" non-answer; make it
one a stranger could follow.

The essentials the grader checks: an **install** section, a **run/usage**
section, and at least one **worked example** — a real command in a code block
that a reader can copy and try.

> **The floor vs. the judgment.** The grader checks those three are present.
> Whether the docs are actually *clear* — whether that stranger really could
> follow them — is your judgment. (The best test: hand it to someone and watch.)

Rewrite `README.md` with install, usage, and a worked example. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/70-process-documentation.md
```

> **If you are stuck alone:** pretend you have amnesia and just found this project.
> What would you need to be told, in order, to use it? Write exactly that.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/70-process-documentation.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/70-process-documentation.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
