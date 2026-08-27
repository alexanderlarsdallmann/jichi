---
title: The signed byte
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/30-the-signed-byte/test.sh"
hints:
  - The program compiles clean and prints a number. That is not the question. Compile it BOTH ways the grader does and compare -- `cc -std=c89 -pedantic -fsigned-char -o s docs/assignments/30-the-signed-byte/bytesum.c && cc -std=c89 -pedantic -funsigned-char -o u docs/assignments/30-the-signed-byte/bytesum.c && ./s && ./u`. Same source, two answers.
  - "`char`'s signedness is implementation-defined (C89 3.1.2.5): signed on x86, unsigned on most ARM. `byte_sum` reads raw bytes through a plain `char`, so a byte >= 0x80 sign-extends on one platform and not the other. This is NOT undefined behaviour -- nothing traps -- it is non-portability."
  - "The rule: a plain `char` is for characters; the moment a byte is a *number* (a checksum, a table index, an image sample), read it through `unsigned char`, whose 0..255 range is guaranteed everywhere. ACCOUNT.md must name the implementation-defined behaviour AND the `unsigned char` fix, and the correct sum is 1023 (the byte values)."
---

> **Prerequisite: a C compiler (`cc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

`docs/assignments/30-the-signed-byte/bytesum.c` sums the raw byte values of a
buffer. It compiles with zero warnings under
`-std=c89 -pedantic -Wall -Wextra -Werror`, runs, and prints a number. On your
x86 machine it prints one answer; on a Raspberry Pi it prints a **different**
one — from the same source, the same standard, and **no undefined behaviour**.

The culprit is the standard's *second* grey category. Task 29 was **undefined
behaviour** — the standard refuses to give the code any meaning, and a
sanitizer traps it. This is **implementation-defined behaviour**: the standard
*does* give it meaning, but lets each compiler pick which. Whether `char` is
signed is exactly such a choice (C89 3.1.2.5) — signed on x86, unsigned on
most ARM — and `byte_sum` reads raw bytes through a plain `char`, so a byte
with the high bit set (`0x80`–`0xFF`) sign-extends to a negative number on one
platform and stays `0`–`255` on the other. Nothing traps. No sanitizer fires.
The program is simply not portable, and the instrument that reveals it is not
`-fsanitize=…` but **compiling both ways and diffing**.

Fix it so the answer is the same *everywhere* — and correct. The fixed program
must compile clean under strict C89 `-Werror`, and print the identical, right
sum whether the compiler is told `-fsigned-char` or `-funsigned-char`. Then
write `docs/assignments/30-the-signed-byte/ACCOUNT.md` naming the
implementation-defined behaviour, the fix, and the rule it generalizes.

The fix is one type. The lesson is the rule behind it: a plain `char` is for
*characters*; the moment a byte is a *number* — a checksum, a table index, an
image sample — it must be `unsigned char`, whose `0`–`255` range every
conforming implementation guarantees. This is why `ctype`-style code, hash
functions, and this project's own byte handling all reach for `unsigned char`,
and why the portability table in [`C_STANDARDS.md`](../C_STANDARDS.md) lists
"signedness of `char`" under *architecture*: it is a real axis your code
crosses the moment it leaves your machine.

Have the agent do the mechanical fix if you like — the account is yours.

Grade with `jichi grade docs/assignments/30-the-signed-byte.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/30-the-signed-byte.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/30-the-signed-byte.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
