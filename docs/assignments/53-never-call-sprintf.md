---
title: Never call sprintf
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/53-never-call-sprintf/test.sh"
hints:
  - "Run the grader: `sh docs/assignments/53-never-call-sprintf/test.sh`. `greet` writes into a caller-owned buffer with `sprintf`, which takes no size — the suite's 8-byte `small` buffer overflows, and ASan traps."
  - "The fix is a single bounded call. `snprintf(out, cap, \"Hello, %s!\", name)` writes at most `cap-1` bytes plus a `'\\0'`, truncating instead of overflowing. `cap` is already a parameter — the pristine code even ignores it with `(void)cap;`."
  - "The grader also greps for `sprintf`/`strcpy`/`strcat`/`gets` — so making the buffer bigger while keeping `sprintf` is not a fix; the unbounded call must be gone. Note `snprintf` is C99/POSIX (jichi probes for it, JC_HAVE_VSNPRINTF); the grader exposes it under strict C89 with `-D_POSIX_C_SOURCE`, exactly as jichi's Makefile does."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/53-never-call-sprintf/fmt.c` writes `"Hello, <name>!"` into a
caller-owned buffer — with `sprintf`, which takes **no size**. It works when the
buffer is big enough, and it is a textbook buffer overflow when it is not. This
is exactly the write jichi's house rule forbids — *"never call `sprintf`"*
([CLAUDE.md](../../CLAUDE.md); jichi ships its own bounded `jc_snprintf`).

This is a **refactor**: change *how*, not *what*. The greeting stays the same;
the overflow goes away.

> **Two instruments, because one is not enough here.** AddressSanitizer proves
> the small buffer is no longer overrun — but ASan alone would also pass a "fix"
> that just makes the buffer bigger. So the grader *also* greps the source for
> the unbounded string functions (`sprintf`, `strcpy`, `strcat`, `gets`): the
> dangerous call must be **gone**, not merely out-of-reach. That pairing — a
> dynamic check and a static one — is the same belt-and-suspenders jichi uses on
> itself.

Rewrite `greet` to honour `cap` (the bounded `snprintf` is the one-line move),
with the suite (`test_fmt.c`) still green. The grader passes only when **both**
hold: the suite runs clean under ASan, *and* no unbounded write remains.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/53-never-call-sprintf.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/53-never-call-sprintf.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/53-never-call-sprintf.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
