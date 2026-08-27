---
title: Read a real project
audience: student
phase: review
difficulty: advanced
points: 4
verify: "sh docs/assignments/24-read-a-real-project/test.sh"
hints:
  - Start with the contract (journal.h), then the tests -- coverage is a MAP of what the authors trusted. Which function has no test at all?
  - Read journal_trim with a concrete example on paper -- five notes, keep two, which INDEX is the first survivor? Now read what the loop actually copies.
  - "The grader holds your proof-test to the two-sided bar: it must FAIL when compiled against journal/pristine/journal.c (the as-found snapshot) and PASS against your fixed journal.c. Write test_trim.c that pins WHICH notes survive, not just how many."
---

> **Prerequisite: a C compiler (`cc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

This is the reading track's graded floor
([`READING_OPEN_SOURCE.md`](../READING_OPEN_SOURCE.md) is the method and
the road beyond). `docs/assignments/24-read-a-real-project/journal/` is a
small library you did not write, arriving the way real code arrives: tests
green, README-grade comments, and **no reason to trust any of it**.

Somewhere in it, one function does not do what its header says. The
authors' tests never noticed — read their coverage as evidence of what
they trusted without proof. Your deliverables:

1. **The fix** in `journal.c` — behavior matching the header's contract.
2. **The proof**: `journal/test_trim.c`, a test the grader compiles twice —
   against the as-found snapshot (must **fail**) and against your fix (must
   **pass**). A test green on both convicts nobody; that asymmetry is
   Module 3's lesson applied to *someone else's* code.
3. **`ANALYSIS.md`** with `## The map` (what the pieces are and what the
   tests actually cover), `## The suspect` (how reading led you there), and
   `## Proof` (what your test pins).

Drive the agent as your reading partner — "which functions have no test?",
"trace journal_trim with count=5, keep=2" — but the conviction standard is
mechanical and yours.

Grade with `jichi grade docs/assignments/24-read-a-real-project.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/24-read-a-real-project.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/24-read-a-real-project.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
