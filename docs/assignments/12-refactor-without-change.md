---
title: Refactor without changing behaviour
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/12-refactor-without-change/test.sh"
hints:
  - Run the tests FIRST, before touching anything, so you know green -- the refactor's whole promise is that they stay that way.
  - Two smells, two moves -- give the repeated literal one name (the brief names it), and give the duplicated guard block one function both callers use.
  - Extract the helper first, run the tests; then the constant, run the tests. Two small green steps beat one big diff -- and if a step goes red, /undo and retry that step alone.
---
`docs/assignments/12-refactor-without-change/` holds a small duration
library (`dur.c`, `dur.h`), its tests (`test_dur.c`), and a runner
(`test.sh`). The tests are **green right now** — run them and see.

> **Prerequisite: a C compiler (`cc`).** This task's grader compiles C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. Without it the failure can read like a wrong answer rather than a missing tool.

The code carries two of the smells you named in the previous assignment:

1. The literal `86400` (seconds per day) appears in two functions. Name it
   once: `#define SECONDS_PER_DAY 86400` in `dur.c`, and use the name
   everywhere.
2. The argument-validation block is duplicated across `hms_to_seconds` and
   `seconds_remaining`. Extract it into one helper both functions call.

Refactor `dur.c` so both smells are gone and **behaviour is unchanged** —
the runner checks both halves mechanically: the tests must still pass, the
literal must survive only in its `#define`, and the guard block must exist
exactly once. Leave `test_dur.c` exactly as it is.

This is the module's core discipline: a refactor makes *no* observable
change, and the tests are the instrument that proves it. If you find
yourself "improving" behaviour on the way through, that is a different
change and belongs in a different diff.

Grade with `jichi grade docs/assignments/12-refactor-without-change.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/12-refactor-without-change.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/12-refactor-without-change.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
