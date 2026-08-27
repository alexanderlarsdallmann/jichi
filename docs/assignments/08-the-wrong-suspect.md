---
title: The wrong suspect
audience: student
phase: implementation
difficulty: hard
points: 3
verify: "sh docs/assignments/08-the-wrong-suspect/test.sh"
hints:
  - The comment in main.c is somebody's old suspicion, not evidence. What experiment would tell you which function is actually wrong?
  - Test the two stages separately. Feed split_fields a line by hand (or via a tiny probe program) and count what comes out before you stare at total() any longer.
  - "split_fields only stores a field when it sees a comma, so the final field -- `8` in `3,5,8` -- is never flushed. Handle the end of the string like a separator, then re-run."
---

> **Prerequisite: a C compiler (`cc`).** This task grades by compiling C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. The runner now says so clearly if `cc` is missing (rather than looking like a test failure).

The program in `docs/assignments/08-the-wrong-suspect/` sums a comma-separated
line of integers. It prints the wrong total:

```sh
# in the jichi checkout (repository root)
sh docs/assignments/08-the-wrong-suspect/test.sh
```

Somebody left a comment in `main.c` blaming `total()`. Old comments are
folklore, not evidence — this task is graded on *debugging as science*:
symptom, hypothesis, experiment, and only then the fix.

Two deliverables:

1. **The fix**, wherever the evidence actually leads.
2. **Your debugging record**: a file
   `docs/assignments/08-the-wrong-suspect/NOTES.md` with exactly these four
   sections (this is the ANECDOTES format — the record you will keep for the
   rest of the curriculum starts here):

```markdown
## Symptom
## Dead ends
## Root cause
## Lesson
```

Write the record honestly. If your first hypothesis was right, say so; if it
was the comment's suspect, that dead end is worth a sentence — it is the whole
point of the exercise.

The runner checks both the program's output and the record's structure. Grade
with `jichi grade docs/assignments/08-the-wrong-suspect.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/08-the-wrong-suspect.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/08-the-wrong-suspect.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
