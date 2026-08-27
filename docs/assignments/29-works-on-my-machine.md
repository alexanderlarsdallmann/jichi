---
title: Works on my machine
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/29-works-on-my-machine/test.sh"
hints:
  - The program compiles clean and prints a number at `-O0`. That proves nothing. Rebuild it with the sanitizer the grader uses -- `cc -std=c89 -pedantic -fsanitize=undefined -fno-sanitize-recover=all -o fold docs/assignments/29-works-on-my-machine/fold.c` -- and run it. Now read what it says.
  - "The accumulator is a signed `int`, and a hash *wants* to wrap around. Signed overflow is undefined behaviour (C89 6.1.2.5); unsigned overflow is defined to wrap modulo 2^N. Which type does the hash actually want?"
  - "ACCOUNT.md must name two things: that the bug is signed-overflow UB (not a crash, not a wrap-around you can rely on), and why `unsigned` is the correct fix -- the defined modular arithmetic a hash is built on -- not a workaround. Name the cost, too: the result is now an explicit modular value."
---

> **Prerequisite: clang (`clang`).** This task is about a compiler-specific behaviour, so the compiler is the subject, not an interchangeable tool. The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install clang` (**not** `build-essential`, which gives you gcc); on macOS the command-line tools provide clang as `cc`.

`docs/assignments/29-works-on-my-machine/fold.c` is a rolling hash — the
classic `acc = acc * 31 + x`. It compiles with no warnings, runs, and prints
a plausible number. On your machine, at `-O0`, it "works."

It is broken. The accumulator is a signed `int`, and the hash overflows it on
the second fold. **Signed integer overflow is undefined behaviour** in C89
(6.1.2.5) — the standard gives it no meaning at all. It happens to wrap at
`-O0`; an optimizing compiler is entitled to assume it *never* overflows and
miscompile the loop around that assumption. "Works on my machine" is not a
property of a program with undefined behaviour — it is luck that the next
compiler, flag, or platform is free to revoke.

Your job: make the wrap-around **defined**, without changing what a hash is.
The fixed program must compile clean under
`-std=c89 -pedantic -Wall -Wextra -Werror`, run cleanly under
`-fsanitize=undefined`, and print the one well-defined result. Then write
`docs/assignments/29-works-on-my-machine/ACCOUNT.md` naming the undefined
behaviour, the fix, and its cost.

The fix is one word of type. The lesson is the paragraph: a hash *relies* on
modular arithmetic, and C89 gives you that — defined, guaranteed — only for
**unsigned** types. Signed overflow was never the wrap-around you wanted; it
was UB wearing a wrap-around's clothes, and a sanitizer is how you stop
trusting it. This is the difference between a test that runs your code and a
test that runs it *under the tools that refuse to look the other way* — the
same reason this project builds its whole CI under ASan/UBSan and valgrind.

Have the agent do the mechanical fix if you like — the account is yours.

Grade with `jichi grade docs/assignments/29-works-on-my-machine.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/29-works-on-my-machine.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/29-works-on-my-machine.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
