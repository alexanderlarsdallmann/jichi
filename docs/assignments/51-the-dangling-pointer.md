---
title: The dangling pointer
audience: student
phase: testing
difficulty: intro
points: 2
verify: "sh docs/assignments/51-the-dangling-pointer/test.sh"
hints:
  - "Run the grader: `sh docs/assignments/51-the-dangling-pointer/test.sh`. It compiles with AddressSanitizer and shows you the exact line where the freed memory is touched — read the `READ of size ... freed by ...` report."
  - "`shout` documents that the *caller* owns and frees the result — but `shout` frees it too, then returns the dangling pointer. `main` reads it (use-after-free) and frees it again (double-free)."
  - "Delete the `free(out);` line inside `shout` (the one marked as the bug). The result is freed exactly once, by `main`. Do not touch `main`."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

The graded **C systems course** (its companion reading is
[C_STANDARDS.md](../C_STANDARDS.md) and the *reasoning* half is
[Set D — memory & lifetimes](INDEX.md); this course is where you build the
machinery those reason about) opens with the bug at the heart of C: a pointer
that outlives the memory it points at.

`docs/assignments/51-the-dangling-pointer/shout.c` uppercases a string into a
fresh heap block. The contract is that the **caller** frees the result — but
`shout` frees it *itself* and then returns the now-dangling pointer, which `main`
reads and frees again.

> **The instrument is AddressSanitizer** — the same tool jichi's own CI runs
> (`make SAN=1`). A use-after-free often "works" at `-O0` on a good day, which is
> exactly why a plain compile-and-run proves nothing. ASan poisons freed memory
> and traps the moment you touch it. Run the grader and read the report:

```sh
# in the jichi checkout (repository root)
sh docs/assignments/51-the-dangling-pointer/test.sh
```

Fix the **function** so the memory is freed exactly once (by its owner), leave
`main` alone, and the sanitizer goes quiet and the program prints `HELLO`.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/51-the-dangling-pointer.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/51-the-dangling-pointer.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/51-the-dangling-pointer.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
