---
title: The arena
audience: student
phase: implementation
difficulty: medium
points: 4
verify: "sh docs/assignments/54-the-arena/test.sh"
hints:
  - "Read `test_arena.c` first — it is the spec. `arena_new(1024)`, two `arena_alloc`s that must not alias, `arena_used`, then `arena_reset` back to 0 and reuse. Do not edit the suite."
  - "State: `struct arena { char *base; size_t cap; size_t off; };`. `arena_new` mallocs the struct and a `cap`-byte block. `arena_alloc` rounds `off` up to an alignment, bounds-checks `aligned + n <= cap` (return NULL if not), advances `off`, and returns `base + aligned`. `arena_reset` sets `off = 0`. `arena_free` frees both blocks."
  - "Alignment: `size_t a = sizeof(void *); size_t aligned = (off + (a - 1)) & ~(a - 1);` rounds up to a pointer boundary, so an `int*` you hand out is properly aligned. The grader runs under ASan, so a too-loose bounds check is caught when the suite writes into your memory."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

The capstone builds the abstraction the whole course has been circling — and the
one **jichi itself is built on**. `jc_mem` gives jichi a session arena plus a
per-turn scratch arena; picking the shortest lifetime that outlives the data is
the discipline (`CLAUDE.md`), and it is the bug class that cost real milestones.
[Set D](INDEX.md) taught you to *reason* about an arena's lifetime and footprint;
here you **implement** one.

`docs/assignments/54-the-arena/arena.c` is a stub behind `arena.h`. Implement a
**bump allocator**: hand out memory by advancing one offset, and free it all at
once with `reset` (no per-object free). That is the whole idea —

```mermaid
flowchart LR
  subgraph B["one malloc'd block of cap bytes"]
    direction LR
    U["used: 0 to off"] --- F["free: off to cap"]
  end
  A["arena_alloc(n)"] -->|"round off up to alignment,<br/>check aligned+n does not exceed cap"| U
  R["arena_reset()"] -->|"off = 0 (bytes reusable, not freed)"| B
```

`test_arena.c` is the spec — **do not edit it**; make your implementation pass
it under **AddressSanitizer** (a too-loose bounds check or a bad alignment shows
up when the suite writes into the memory you return). Then leave a one-line
`DESIGN.md` naming the shape (a bump/arena allocator: allocation is a pointer
advance, `reset` reclaims without freeing).

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/54-the-arena.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/54-the-arena.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/54-the-arena.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
