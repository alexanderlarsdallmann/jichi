---
title: Refactor it to C++
audience: student
phase: implementation
difficulty: advanced
points: 4
verify: "sh docs/assignments/28-refactor-to-cpp/test.sh"
hints:
  - Refactor under a green light -- capture the tool's outputs (including the empty string and doubled spaces) BEFORE moving anything.
  - "Port one function at a time: stats.cpp can export wt_count_words behind extern \"C\" while stats.c still owns wt_longest_word -- two objects, one header. Delete the C side of a function only after the C++ side answers identically."
  - "The grader wants wordtool.h unchanged, stats.c GONE, stats.cpp present, output byte-identical. Idiom upgrade to reach for once green: std::string_view + a range-for replaces the raw pointer walk without changing a single observable byte."
---

> **Prerequisite: a C++ compiler (`c++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

Step three of the C++ arc: **replace** the C module behind the *unchanged*
C header. In `docs/assignments/28-refactor-to-cpp/`, move both functions
of `stats.c` into `stats.cpp` — `extern "C"` at the boundary, real C++
inside — delete `stats.c`, and leave every observable behavior
byte-identical, edge cases included.

Same discipline as the Zig twin (assignment 26), different lesson at the
seam: C++ *contains* most of C, which makes half-migrated states cheap —
and makes it dangerously easy to write "C with a .cpp extension" and call
it migrated. The refactor is only worth its diff if the inside actually
becomes C++ (the hints name the idioms); the grader pins the outside so
you can afford to make the inside honest.

Grade with `jichi grade docs/assignments/28-refactor-to-cpp.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/28-refactor-to-cpp.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/28-refactor-to-cpp.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
