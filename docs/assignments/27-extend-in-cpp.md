---
title: Extend it in C++
audience: student
phase: implementation
difficulty: advanced
points: 4
verify: "sh docs/assignments/27-extend-in-cpp/test.sh"
hints:
  - Build and run the tool as-is first, and read build.sh -- the C compiles as strict C89 and must KEEP compiling that way; only the new file is C++.
  - "The seam is `extern \"C\"`: `extern \"C\" long wt_count_vowels(const char *text)` in vowels.cpp gives the C linker the unmangled symbol the header declares. Compile it with `c++ -std=c++17 -c`, and LINK with c++ so the C++ runtime comes along."
  - "Expected output: `words=N longest=M vowels=K`, both cases of aeiou. Inside the extern \"C\" function you are in full C++ -- std::count_if over the string is the idiomatic one-liner the task is nudging you toward."
---

> **Prerequisite: a C++ compiler (`c++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

The C → C++ migration arc, step two
([`CPP_INTEROP.md`](../CPP_INTEROP.md) is the track page; this repo's own
[CPP_BUILD.md](../CPP_BUILD.md) records step one — jichi compiles as
C++). `docs/assignments/27-extend-in-cpp/` holds the same small C tool as
the Zig track; grow it the vowel counter, **implemented in C++**, behind
the same C header.

Two seams must both hold: the *linker's* (`extern "C"` — name mangling is
the first thing C++ does that C never did) and the *build's* (who links
the C++ runtime in? Watch what happens if you link with `cc`). The grader
checks the floor honestly: the behavior, a real `.cpp` translation unit,
and `build.sh` compiling it — a counter smuggled in as C fails.

Grade with `jichi grade docs/assignments/27-extend-in-cpp.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/27-extend-in-cpp.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/27-extend-in-cpp.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
