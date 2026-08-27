---
title: The array that outgrew itself
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/52-the-array-that-outgrew-itself/test.sh"
hints:
  - "`ivec_push` writes `data[len]` and increments `len`, but nothing ever grows `data`. It has room for 4. What happens on the 5th push? The bug hides *past the initial capacity* — small tests never reach it."
  - "Write `test_ivec.c` — a `main` that `ivec_init`s, pushes MORE than 4 values, then `assert`s `ivec_get` returns each one. Compile it under ASan (the grader does) and watch the overflow trap. At least three `assert()`s."
  - "In `ivec_push`, when `len == cap`, double `cap` and `realloc` `data` before writing. That is the growable-array move jichi's own `jc_vec` makes. Write the failing test first, then fix."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/52-the-array-that-outgrew-itself/ivec.c` is a growable array of
ints — `init`, `push`, `get`, `free` behind `ivec.h`. It works on the handful of
values people usually push, and it is wrong: `ivec_push` never grows `data`, so
the 5th push writes past a 4-int block. The bug hides **just past the initial
capacity** — the same shape as the C course's `stats_max`, but in the heap.

This task is **tests as proof**, in C. There is no test file yet; you write it.
Create `test_ivec.c` next to `ivec.c` — a `main` that pushes well past the
initial capacity and `assert`s each value reads back correctly. Start from this
skeleton:

```c
#include "ivec.h"
#include <assert.h>

int main(void)
{
    ivec v;
    int i;
    ivec_init(&v);
    /* ... push MORE than the initial capacity, then assert each get().
       at least three assert()s ... */
    ivec_free(&v);
    return 0;
}
```

The grader compiles your test under **AddressSanitizer**, so a test that grows
the vector will **trap** on the pristine `ivec.c` — that is the proof the bug is
real. Then fix `ivec_push` to grow (double the capacity and `realloc`), and both
your test and an independent acceptance probe run clean.

The grader checks all three: your test exists and passes under ASan, it has at
least three `assert()`s (a hollow suite is not proof), and an independent probe
that pushes far past the initial capacity confirms the vector now grows safely.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/52-the-array-that-outgrew-itself.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/52-the-array-that-outgrew-itself.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/52-the-array-that-outgrew-itself.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
