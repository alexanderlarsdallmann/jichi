---
title: The time-traveling C
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/23-the-time-traveling-c/test.sh"
hints:
  - Compile with `cc -std=c89 -pedantic -Wall -Wextra -Werror` and READ the error list top to bottom -- the compiler is handing you the complete inventory of what this task is about.
  - Each construct has a standard C89 replacement, and each replacement has a COST. The two worth thinking hardest about are `long long` (what bounds the total now?) and `snprintf` (who guarantees the buffer now?).
  - "PORT.md is a table: construct, replacement, cost. The grader checks you accounted for every construct AND named what range now bounds the total -- 'no cost' is a wrong answer for long long."
---

> **Prerequisite: a C compiler (`cc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

`docs/assignments/23-the-time-traveling-c/inventory.c` is comfortable
modern C — and this project is written in **C89**, on purpose (see
[`C_STANDARDS.md`](../C_STANDARDS.md) for the why, and `CONTRIBUTING.md`
for the house rules that follow). Port the file to strict C89: it must
compile under `-std=c89 -pedantic -Wall -Wextra -Werror`, produce
**byte-identical output**, and come with
`docs/assignments/23-the-time-traveling-c/PORT.md` — a table naming every
construct you moved, its replacement, and its **cost**.

The cost column is the actual lesson. A port that "just works" has still
changed the program's guarantees: `long` holds less than `long long` on a
32-bit machine; `sprintf` shifts the bounds-promise from callee to caller;
positional initializers couple the table to the struct's field order.
Portability is never free — the engineering is in *naming the price* and
deciding it is worth paying (this project decided it was; the assignment
makes you re-derive why the decision needs the paragraph of justification
it gets).

Have the agent do the mechanical port if you like — the table is yours.

Grade with `jichi grade docs/assignments/23-the-time-traveling-c.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/23-the-time-traveling-c.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/23-the-time-traveling-c.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
