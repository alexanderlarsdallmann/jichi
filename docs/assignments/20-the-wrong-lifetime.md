---
title: The wrong lifetime
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/20-the-wrong-lifetime/test.sh"
hints:
  - Run the test and read BOTH gauge numbers. Identical batches should cost identical memory -- what does it mean that batch 2 costs as much again as batch 1?
  - Find every allocation in serve() and ask of each, "how long does this data need to live?" The comment above serve() answers it; the code disagrees with its own comment.
  - "Three correct fixes exist: a second arena that is reset per request, malloc + free around the working copy, or a stack buffer. Pick one, say why, and keep the totals identical -- the test checks behavior AND footprint."
---

> **Prerequisite: a C compiler (`cc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

`docs/assignments/20-the-wrong-lifetime/` holds a tiny request-serving
program built on an **arena allocator** (read `arena.h` first — it is short,
and its one design question is the whole task). The keeper's totals must
live as long as the keeper. Each request's working copy must not — yet the
program survives, the answers are right, and no leak checker would ever
complain: the memory is all still *reachable*. It is simply on the wrong
lifetime, and the footprint grows forever on a long-running keeper.

This is a real bug class from this project's own history: reachable-until-
exit retention that ASan and valgrind both call "zero leaks"
(`docs/analysis/2026-07-29-tool-arena.md` — a session listing once retained
**491 MB** this way). The instrument that catches it is not a leak checker
but a **footprint gauge**: the fixture's selftest serves the same batch
twice and prints the arena's byte count after each. Flat is correct;
growing is the bug.

Have the agent find the misplaced allocation and fix its lifetime. The
totals (`requests=1000 words=10000`) must not change — a fix that breaks
behavior to flatten memory is not a fix.

Check with `jichi grade docs/assignments/20-the-wrong-lifetime.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/20-the-wrong-lifetime.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/20-the-wrong-lifetime.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
