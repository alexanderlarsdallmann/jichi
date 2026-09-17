---
title: The file that wasn't there
audience: student
phase: implementation
stage: c-io
difficulty: easy
points: 3
verify: "sh docs/assignments/76-the-file-that-wasnt-there/test.sh"
hints:
  - "Run the grader first and read what the sanitizer says. It names a function and a libc file: `SEGV ... in __GI__IO_fread`. Work backwards — what did `cfg_load` hand to `fread`, and what does `fopen` return when the path is not there?"
  - "Three more things are wrong after the NULL check, and none of them crashes. `fread` *returns* how many items it read — `loadcfg.c` throws that away and reports `CFG_MAX_BYTES` as the length. Nothing checks the file's size against the cap. And nothing sets `*out`/`*len` on the failure paths, so a caller that trusts them frees garbage. Read `loadcfg.h` line by line; every rule there is one the code breaks."
  - "Size it with `fseek(f, 0, SEEK_END)` + `ftell`, then `fseek` back. Refuse **before** the `malloc` if it is over `CFG_MAX_BYTES` — the point is that a huge file never becomes a huge allocation. Then `fread` exactly that many bytes, compare the return value, NUL-terminate at the real length, and print a message that contains `path` so the user knows *which* file."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/76-the-file-that-wasnt-there/loadcfg.c` reads a whole small
file into memory. It is 15 lines and it has four defects, and only one of them
crashes — which is the lesson.

The curriculum has never taught this before. Counted 2026-09-16: file I/O
appeared in **0 of 79 graded tasks**, while jichi's own source reads and writes
files in 26. This task is the first half of closing that
([`FILE_HANDLING.md`](../FILE_HANDLING.md) is the reading).

`loadcfg.h` is the contract. Read it before the implementation — every rule in
it is one `loadcfg.c` breaks:

- a **missing file** must return `CFG_ENOFILE`, not crash;
- `*len` must be the file's **real length**, not a capacity — `fread` *returns*
  how much it read, and the pristine code discards it;
- a file over `CFG_MAX_BYTES` must be **refused**, and refused *before* the
  allocation, so a file bigger than memory never becomes a `malloc` attempt;
- on **any** error `*out` must be `NULL` and `*len` `0`, or a caller that trusts
  them frees garbage;
- the error message must **contain the path**. "cannot open file" does not tell
  a user which file.

Only the first of those is visible to AddressSanitizer. The grader checks the
other three by assertion, because a wrong length and a quiet truncation are not
memory errors — they are worse, since nothing traps and the program carries on
with bad data. **A sanitizer is not a substitute for knowing what correct is.**

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/76-the-file-that-wasnt-there.md
```

## What jichi does, and where it is wrong

`jc_read_file` (`include/jc_platform.h:109`) is this function, grown up: it
slurps into an arena, refuses anything over `JC_READ_FILE_MAX` — **64 MB**,
checked before the allocation — and returns `JC_ERR_TOOBIG` as a value rather
than dying. Tighter caps sit above it per use: 32 KB for a rules file, 512 KB
per file in the repo-map scanner, 5 MB for an image.

**A cap is a design decision, not laziness** — a model's context is finite, so a
file that cannot fit must be refused loudly rather than truncated quietly. And
**it is the wrong decision when the job is the whole file**: a checksummer, a
compressor or a log analyser must *stream* a bounded window instead. jichi slurps
because its inputs are source files; yours may not be.

There is one more trap in the real thing that this task does not simulate, and it
is worth knowing: **opening a FIFO with no writer blocks forever.** A stray
`pipe.json` in the session store once hung `/sessions`, `/resume` and
`--continue` with no output, no error and no timeout. `jc_is_regular_file` is the
guard — and it is deliberately *not* applied to paths the user named, because
`--config <(jq ...)` is a pipe on purpose. Same syscall, opposite policy,
depending on who chose the path.

## The floor this grader cannot check

The header says the size check must come **before** the allocation. Proving that
ordering needs a file bigger than memory, which a grader cannot arrange — so the
grader proves the *refusal*, not its *position*. Put the check first anyway; the
reason is in [`FILE_HANDLING.md`](../FILE_HANDLING.md) §1, and the honest limit
is named here so you know exactly which rung rests on your word.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/76-the-file-that-wasnt-there.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/76-the-file-that-wasnt-there.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
