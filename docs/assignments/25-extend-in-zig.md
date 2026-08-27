---
title: Extend it in Zig
audience: student
phase: implementation
difficulty: advanced
points: 4
verify: "sh docs/assignments/25-extend-in-zig/test.sh"
hints:
  - Build and run the tool as-is first. Read build.sh -- zig cc is already the compiler, so the toolchain you need is already in the file.
  - "A Zig function joins a C program as an object file: `export fn wt_count_vowels(text: [*:0]const u8) c_long` in vowels.zig, `zig build-obj vowels.zig`, then add vowels.o to the link line and the prototype to wordtool.h."
  - "The output the test wants is `words=N longest=M vowels=K` -- count both cases of aeiou. Wire main.c to call your export exactly like it calls the C functions; the header cannot tell the difference, and that is the lesson."
---

> **Prerequisite: Zig (`zig`).** Install it from ziglang.org. The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

The migration arc — **compile, extend, refactor** — starts here, on step
two ([`ZIG_INTEROP.md`](../ZIG_INTEROP.md) is the track page; assignment
19 was step one). `docs/assignments/25-extend-in-zig/` holds a small,
working C tool. Grow it a feature — a vowel counter — **implemented in
Zig**, called through the same C header as everything else.

This is how real migrations begin: not by rewriting, but by writing the
*next* piece in the new language while the old pieces keep working. Zig
makes the seam unusually thin — `export fn` gives a C-ABI symbol, and the
compiler that builds your C already speaks Zig. The grader checks the
seam honestly: the new behavior must exist, a `.zig` translation unit
must exist, and `build.sh` must actually compile it — a vowel counter
smuggled in as C fails the floor.

Have the agent draft the Zig if you like — but read the seam it writes:
which types cross the boundary, and what does `[*:0]const u8` promise
that `const char *` merely hopes?

Grade with `jichi grade docs/assignments/25-extend-in-zig.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/25-extend-in-zig.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/25-extend-in-zig.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
