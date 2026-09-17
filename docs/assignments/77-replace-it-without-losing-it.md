---
title: Replace it without losing it
audience: student
phase: implementation
stage: c-io
difficulty: intermediate
points: 3
verify: "sh docs/assignments/77-replace-it-without-losing-it/test.sh"
hints:
  - "Run the grader and read the first FAIL line: the target itself was opened for writing. That is the whole bug -- `fopen(path, \"w\")` truncates the file to zero bytes before your first byte arrives. Where else could the new bytes go first?"
  - "A temporary file in the SAME directory (`path` + \".tmp\" is enough; rename() is atomic only inside one filesystem, so /tmp is wrong), created with `open(tmp, O_CREAT | O_EXCL | O_WRONLY, mode)` so the mode is right from the first instant, then `fdopen`, `fwrite`, and -- check its return value -- `fclose`. Only then `rename(tmp, path)`."
  - "Every error path must `remove(tmp)` and return SAVE_EIO with the old file untouched. The grader fails your FIRST write on purpose and then reads the old file back byte for byte, and counts the directory entries afterwards: one file, or you left a temporary behind. `secret` selects the mode: `S_IRUSR | S_IWUSR` is 0600."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/77-replace-it-without-losing-it/savestate.c` saves a
program's state to a file. It is nine lines, it compiles, and it works every
time nothing goes wrong. Its first line is a data-loss bug:

```c
f = fopen(path, "w");   /* truncates path to zero bytes, right now */
```

From that instant until `fclose` returns, the user's old data exists nowhere.
Crash there, fill the disk there, get killed there, and the file is empty. Task
76 was the reading half of file I/O; this is the writing half, and the contract
is `savestate.h` -- read it before the code.

**What you must do.** Write the new bytes to a temporary file **in the same
directory**, `rename()` it over `path` only once every byte is on it and
`fclose` has confirmed so, and create it with mode **0600** when `secret` is
set -- at creation, not by `chmod` afterwards. On any failure, remove the
temporary and leave the old file exactly as it was.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/77-replace-it-without-losing-it.md
```

## What jichi does, and why

`jc_write_file_atomic` (`src/platform/jc_platform_posix.c`) is this function,
and [`FILE_HANDLING.md`](../FILE_HANDLING.md) §2 reads it line by line: six
decisions, none decorative. The temp is beside the target because `rename()` is
atomic only within one filesystem. `O_EXCL` refuses a live collision. The mode
is set at creation so there is no window in which a secret is world-readable.
`fclose` is checked because a full disk reports itself *there*. The byte count
is compared because a short `fwrite` is not an error flag. `rename` comes last
so a concurrent reader sees the old file or the new one, never a torn one. Four
of jichi's sinks -- sessions, calibration, telemetry indexes -- go through it,
and so does the Makefile's own `cmp -s || mv -f` stamp recipe.

## When that decision is wrong

`rename()` replaces the **inode**. An atomic replace therefore breaks hard
links, replaces a symlink instead of writing through it, and discards the
original's owner and mode in favour of the temp's. For jichi's private state
all three are wanted. For a file in *your* workspace all three are wrong --
which is why jichi keeps a second function, `jc_write_file`, that writes in
place and preserves them, and why its header says which to use when. Two write
functions is not duplication; it is two different promises. This task asks for
the first promise. Know that the second exists.

## What you would reach for instead -- and the honest part

**jichi never calls `fsync`.** Counted 2026-09-16: zero occurrences in the whole
source. `rename()` gives atomicity -- no reader sees a half-file -- and **not
durability**: after it returns the bytes may still be in the page cache, and a
power cut can lose them. jichi accepts that on purpose, because its sinks are
recreatable state on a developer's machine and an `fsync` per save costs real
time. The same code in a database, a mail server, or anything that has told a
user "saved" is a bug. The durable sequence is longer than most people expect:
`fsync` the file *and then the directory* after the rename. This grader
**cannot see durability** -- there is no power cut in a test -- so `fsync` is
not graded. Add it anyway, time a thousand saves with and without it, and write
the two numbers into your DESIGN notes: that measurement is the argument, both
ways.

## How the crash is simulated, and where the grader stops

The plan for this task named jichi's `FAULT=1` fault-injection tier. That tier
is a build of *jichi*, not of your file, so it cannot reach `savestate.c`.
Instead the grader compiles your file with a header that renames the libc calls
(`fopen`, `open`, `fwrite`, `write`, `fclose`, `rename`, `remove`, `mkstemp`,
`fsync`) to hooks: they call the real functions, record which path you opened
for writing, and, when told, make your first write fail the way a full disk
would. That is **coarser** than killing the process mid-write -- your error
path still runs -- and it is exactly what a save must survive. What the hooks
cannot see is durability, and the brief says so rather than pretending.

The grader checks: the target is never opened for writing; the temp lives in
the target's directory; a secret is 0600 at creation; a failed write leaves the
old bytes intact and the directory with one file; a completed save leaves
exactly the new bytes. Under AddressSanitizer throughout.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/77-replace-it-without-losing-it.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/77-replace-it-without-losing-it.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
