# Handling files — what jichi does, where it is wrong, and what else there is

Every program reads and writes files, and jichi's curriculum teaches it
**nowhere**: 0 of 79 graded tasks, 0 of 16 source-reading chapters, counted
2026-09-16 ([the coverage review](analysis/2026-09-16-language-teaching-coverage.md)
§3b.1). Meanwhile jichi's own source does it in **26 files**. This page closes
that gap on the reading side; the graded half is
[plans/2026-09-files-and-structures.md](plans/2026-09-files-and-structures.md).

**How to read it.** Every section makes the same three moves, in order:

1. **What jichi does, and why** — with the code, the milestone, and what it cost.
2. **When that decision is wrong** — stated as plainly as the decision.
3. **What you would reach for instead.**

The second move is the one a project's own documentation usually omits. It is
also the one that makes the first believable, so if a section here ever loses it,
the section is broken.

**Prerequisites:** C, a terminal, and the jichi checkout. No bench, no board, no
network — and if you have no compiler today, **[Appendix A](#appendix-a--the-read-only-twin)**
runs the whole page as a reading exercise.

---

## 1. Reading a file: the three things beginners skip

### What jichi does

`jc_read_file` (`include/jc_platform.h:109`, its cap at `:104`, implemented in
`src/platform/jc_platform_posix.c`) slurps a whole file into an **arena**, not
into `malloc`, and refuses anything over a cap:

```c
/* Hard upper bound on a single jc_read_file slurp (bytes). Above this the read
 * is refused with JC_ERR_TOOBIG rather than allocating an arena block that could
 * exhaust memory. Far above any plausible source file (M24). */
#define JC_READ_FILE_MAX (64L * 1024L * 1024L)
```

Three decisions are packed into that, and each is a thing a first draft gets
wrong:

- **The size is checked before the allocation**, not after. A 2 GB file must not
  become a 2 GB allocation that then fails.
- **The failure is a value, not a crash.** `JC_ERR_TOOBIG` travels back as a
  `jc_status`; the caller decides. (jichi's whole error discipline: *tool errors
  are values, never control flow*.)
- **The buffer is NUL-terminated**, and `*len` excludes the terminator — because
  every caller wants to treat it as a string and one of them will forget.

There are **tighter caps above it**, per use: 32 KB for a rules file
(`JC_RULES_MAX`, `src/chat/jc_rules.c:13`), 32 KB for hook output, 512 KB per
file in the repo-map scanner (`RM_MAX_FILE_BYTES`), 5 MB for an attached image.
A cap is a **design decision about what the program is for**, not laziness: a
model's context is finite, so a file that cannot fit is a file that must be
refused loudly rather than truncated quietly.

### And the thing that is not a size at all

```c
int jc_is_regular_file(const char *path);
```

**Opening a FIFO with no writer blocks forever.** A stray `pipe.json` in the
session store used to hang `/sessions`, `/resume` and `--continue` at startup —
*no output, no timeout, no error*. The fix is not a timeout; it is asking whether
the thing you are about to open is a regular file.

Now the subtle half, quoted from the header because the reasoning is the lesson:

> Deliberately NOT enforced inside `jc_read_file`: a path the user NAMED
> (`--config`, `read_file`, `@path`) may legitimately be a pipe, e.g.
> `--config <(jq ...)` which resolves to `/dev/fd/N`. Scanned paths are garbage
> when they are not regular files; named paths are the user's business.

**A path the program found and a path the user typed are different kinds of
object.** Scanning a directory and finding a FIFO means your scan is wrong;
being handed one means the user meant it. Same syscall, opposite policy.

### When jichi's reading is wrong

- **A cap is wrong when the job is the whole file.** A checksummer, a compressor,
  a log analyser must not refuse a 2 GB input — it must **stream** it, reading a
  bounded window at a time. jichi slurps because its files are source files and
  its consumer is a context window; yours may be neither.
- **Arena-allocating the whole file is wrong when the data outlives the arena**,
  or when you need it mutable in place for a long time.
- **`jc_read_file` has no encoding opinion.** It returns bytes. If your input is
  text in an unknown encoding, that is your problem to solve, one layer up.

### What to reach for instead

| You need | Reach for |
|---|---|
| A file too big for memory | `fread` in a loop over a fixed buffer, or `getline` per line |
| Random access to a large file | `mmap` — the kernel pages it in; beware `SIGBUS` on truncation |
| Reading a growing file | `open` + `O_NONBLOCK`, or re-`stat` and read the delta |
| Lines, portably | `fgets` (bounded, awkward) or POSIX `getline` (allocates, easy) |

> **Something to do.** Find the three call sites of `jc_read_file` that pass a
> *scanned* path and the ones that pass a *named* path. Which of them calls
> `jc_is_regular_file` first? Does the split match the header's rule?

---

## 2. Writing a file: the truncation that eats your data

### What jichi does — and it does two different things on purpose

The naive save is one line, and it is a data-loss bug:

```c
f = fopen(path, "w");   /* TRUNCATES path to zero, immediately */
```

Between that call and a successful `fclose`, the old contents are **gone**. Crash
there, fill the disk there, get killed there — the user's file is empty. jichi's
answer is `jc_write_file_atomic` (`src/platform/jc_platform_posix.c:300`), and it
is worth reading whole:

```c
jc_snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
...
put = fwrite(data, 1, len, f);
if (fclose(f) != 0 || put != len) { remove(tmp); return JC_ERR_IO; }
if (rename(tmp, path) != 0)       { remove(tmp); return JC_ERR_IO; }
```

Six decisions, none decorative:

1. **The temp is in the same directory.** `rename()` is atomic only within a
   filesystem; `/tmp` may be a different one, and then it is a copy.
2. **`O_EXCL`** refuses to open an existing temp — a live collision is an error,
   not something to overwrite. A leftover from a crashed run is removed and
   retried *once*.
3. **`S_IRUSR | S_IWUSR` at creation**, not `chmod` afterwards. There is no window
   in which the file is world-readable.
4. **`fclose` is checked, not just `fwrite`.** Buffered data is flushed at close;
   a disk that fills up reports it *there*, and code that checks only `fwrite`
   reports success on a truncated file.
5. **The byte count is compared.** `fwrite` returning short is not an error flag.
6. **`rename` last.** A concurrent reader sees the old file or the new one, never
   a torn half-write.

And a comment that tells you why a familiar function is absent:

> `mkstemp` is XSI, not strict POSIX-200112, so it is deliberately not used.

### When atomic replace is wrong — jichi keeps a second function for this

This is the most useful distinction on the page, and it surprises people.
`rename()` **replaces the inode**. So an atomic replace:

- **breaks hard links** — other names for the old inode no longer see your write;
- **replaces a symlink** instead of writing through it to its target;
- **discards the original's mode and owner**, substituting the temp's.

For jichi's own private state, all three are *desirable*. For a file in the
user's workspace, all three are **wrong**. So jichi keeps both, and the header
says which is which (M141/M146):

> The 0600 mode CARRIES to the final file; use this only for jichi's own private
> sinks (sessions, calibration), where owner-only is correct — workspace files
> keep `jc_write_file`, whose in-place write preserves the target's
> mode/symlink/hard-link semantics.

**Two write functions is not duplication. It is two different promises.**

### The honest part: jichi never calls `fsync`

Counted 2026-09-16: **zero occurrences of `fsync` or `fdatasync` in the entire
source.**

`rename()` gives you *atomicity* — no reader sees a half-file. It does **not**
give you *durability*. After `rename` returns, the data may still be in the page
cache; a power cut can lose it, and on some filesystems can leave you with the
new name and the old (or empty) contents.

jichi accepts this deliberately: its sinks are sessions, telemetry and
calibration — recreatable state on a developer's workstation, where the cost of
an `fsync` per write is real and the loss is an inconvenience. **Write the same
code in a database, a mail server, or anything that has told a user "saved", and
it is a bug.**

The durable sequence is longer than most people expect:

```c
/* write tmp ... */
fsync(fd);                 /* the FILE's data is on the medium        */
close(fd);
rename(tmp, path);
dirfd = open(dirname, O_RDONLY);
fsync(dirfd);              /* the DIRECTORY ENTRY is on the medium    */
close(dirfd);
```

That second `fsync`, on the *directory*, is the one everybody forgets.

### What to reach for instead

| You need | Reach for |
|---|---|
| Append-only logging | `O_APPEND` — atomic for small writes, no read-modify-write |
| Durability | `fsync` on file **and** parent directory, as above |
| Preserve links/modes | In-place write (`jc_write_file`'s choice) |
| Exclusive creation | `O_CREAT \| O_EXCL` — the lock-file idiom |
| Huge sequential output | `write(2)` directly; skip stdio's buffer copy |

> **Something to do.** `jc_write_file_atomic` retries `open` exactly once after
> `remove(tmp)`. Construct the case that retry is for. Then ask: what happens if
> *two* jichi processes with different pids write the same path at the same time
> — and why does the pid suffix make that safe?

---

## 3. The race jichi has not fixed

Most teaching examples of TOCTOU are invented. This one is real, current, and
written down in this repository:

> **All of it is fixed as of M472**, except the path fence's check-then-open
> window — [`HARDENING.md`](HARDENING.md)

The path fence (M24) keeps an autonomous run inside its workspace: it resolves a
path, checks that it is inside the fence, and then opens it. Between the check
and the open, the name can be replaced — with a symlink pointing anywhere.

```
  check: is /work/notes.txt inside the fence?   -> yes
  ... attacker replaces /work/notes.txt with a symlink to /etc/shadow ...
  open:  /work/notes.txt                        -> opens /etc/shadow
```

**Why it is not fixed:** the robust answer is to stop working with *paths* and
work with *file descriptors* — `openat` relative to a directory fd, with
`O_NOFOLLOW` — which is a real change to a fence used everywhere, for a threat
model (a hostile local process racing jichi inside the user's own workspace) that
sits below the ones already closed. That is a judgement, and it is written down
as a judgement rather than an omission.

**What you should take from it:** a check and the use of what was checked must be
the *same operation*, or there is a window. When you cannot close it, say where
it is — `HARDENING.md` names this one in the same sentence as the fixes.

> **Something to do.** Find the fence check in `src/` and the `open` it guards.
> How many lines apart are they? Does the distance matter, or is any distance
> equally fatal?

---

## 4. Permissions, and a milestone that means it was wrong first

`jc_make_private` sets **0600 for a file, 0700 for a directory** (M132), on
sessions, the event log, the envelope journal and calibration — every sink that
holds conversation content. It is **best-effort**: a `chmod` failure returns
`JC_ERR_IO` and callers ignore it.

Two honest readings of that:

- **M132 is a milestone number, which means these files were world-readable
  before it.** On a shared host, another local user could read your transcripts.
  The fix is four lines; noticing was the work.
- **Best-effort is a real trade-off.** A filesystem without POSIX modes (a FAT
  stick, some network mounts) would otherwise make jichi refuse to run. The
  decision is that a usable agent with a warning beats a correct agent that
  cannot start — and if your data is regulated, that trade is wrong for you.

`jc_write_file_atomic` avoids the question entirely by creating the file `0600`
in the first place. **Creating with the right mode beats fixing the mode
afterwards**, because "afterwards" is a window.

> **Something to do.** `jc_make_private` is best-effort and ignored by callers.
> Write the two-line change that makes it fatal. Now argue the other side: which
> platform in [`PLATFORMS.md`](PLATFORMS.md) would stop working?

---

## 5. A checklist you can actually use

For any file your program writes:

- [ ] Does a crash mid-write lose the old contents? (Truncating open: yes.)
- [ ] Is the temp in the **same directory** as the target?
- [ ] Is `fclose` checked as well as `fwrite`, and the **byte count** compared?
- [ ] Is the mode set **at creation**, not after?
- [ ] Does anything downstream depend on hard links, symlinks, or the existing
      mode? (If so, atomic replace is wrong.)
- [ ] Have you told a user "saved"? (If so, you owe them two `fsync`s.)

For any file your program reads:

- [ ] What happens when it does not exist — message, or crash?
- [ ] What happens on a **short read**?
- [ ] What is the largest input you accept, and what do you do above it?
- [ ] Could this path be a FIFO, a device, or a directory?
- [ ] Did the user name this path, or did you find it? (Different policies.)

---

## Appendix A — the read-only twin

Everything above can be done with `less` and `grep`, no compiler. Each answer is
a line or two in the source; the point is to find it yourself.

1. **The cap.** `include/jc_platform.h` — what is `JC_READ_FILE_MAX`, and what
   error does exceeding it produce? Find two *tighter* caps elsewhere and say
   what each is protecting.
2. **The two writers.** Read the comments on `jc_write_file` and
   `jc_write_file_atomic`. Write one sentence naming which to use for a file in
   the user's workspace, and why the other would be wrong.
3. **The six decisions.** In `jc_write_file_atomic`, find where each of §2's six
   numbered decisions appears. One of them is a comparison you might not notice.
4. **The absence.** `grep -rn "fsync" src/` returns nothing. Write the one
   sentence you would add to the header if jichi ever stored something a user
   could not recreate.
5. **The race.** Find the sentence in `HARDENING.md` naming the check-then-open
   window. Does it say *why* it is unfixed, or only *that* it is?
6. **The pipe.** Read the `jc_is_regular_file` comment. Then find one caller that
   checks and one that deliberately does not, and say which is which by the
   header's own rule.

*See also: [`plans/2026-09-files-and-structures.md`](plans/2026-09-files-and-structures.md)
(the graded tasks this page precedes) · [`DATA_STRUCTURES.md`](DATA_STRUCTURES.md)
(its sibling) · [`HARDENING.md`](HARDENING.md) · [`C_STANDARDS.md`](C_STANDARDS.md).*
