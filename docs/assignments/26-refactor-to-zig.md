---
title: Refactor it to Zig
audience: student
phase: implementation
difficulty: advanced
points: 4
verify: "sh docs/assignments/26-refactor-to-zig/test.sh"
hints:
  - Module 7's rule holds across languages -- refactor under a green light. Run the tool, capture its outputs for a few inputs, THEN start moving code.
  - Port one function at a time. stats.zig can export wt_count_words while stats.c still provides wt_longest_word -- two object files, one header; delete the C function only after the Zig one answers identically.
  - "The grader wants: wordtool.h unchanged, stats.c GONE, stats.zig present, and byte-identical output including the edge cases (empty string, runs of spaces). Zig's while-with-continue-expression is the C for-loop's cousin; the sentinel-terminated pointer [*:0]const u8 is your `const char *`."
---

> **Prerequisite: Zig (`zig`).** Install it from ziglang.org. The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

Step three of the migration arc: **replace** a C module with a Zig one
behind the *unchanged* C header. In
`docs/assignments/26-refactor-to-zig/`, move both functions of `stats.c`
into `stats.zig`, delete `stats.c`, and leave every observable behavior —
including the empty-string and repeated-space edges the grader probes —
byte-identical.

This is Module 7's "refactor without change" with the stakes raised: the
tests are your only bridge between two languages' idioms, and the header
is the contract that makes the swap invisible to `main.c`. That
invisibility is the entire migration strategy this track teaches —
module by module, seam by seam, green the whole way. (It is also,
deliberately, how you would migrate a real C project: this repo's own
`docs/ZIG_BUILD.md` records step one on a 24k-line codebase.)

Grade with `jichi grade docs/assignments/26-refactor-to-zig.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/26-refactor-to-zig.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/26-refactor-to-zig.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
