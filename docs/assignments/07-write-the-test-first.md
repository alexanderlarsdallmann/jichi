---
title: Make it fail first
audience: student
phase: testing
difficulty: medium
points: 3
verify: "sh docs/assignments/07-write-the-test-first/test.sh"
hints:
  - Read clamp.h's contract, then clamp.c. One of the three branches does not do what the contract says. Which input reaches it?
  - Write test_clamp.c BEFORE touching clamp.c, run the runner, and watch your new test fail. A test you have never seen fail proves nothing.
  - "Your test needs a main() that calls clamp with a value above hi -- e.g. clamp(9, 0, 5) -- prints ok/not ok, and returns nonzero on failure. Once it fails for the right reason, fix the branch in clamp.c and re-run."
---

> **Prerequisite: a C compiler (`cc`).** This task grades by compiling C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. The runner now says so clearly if `cc` is missing (rather than looking like a test failure).

`docs/assignments/07-write-the-test-first/clamp.c` contains a bug. This task
is about **order of work**: first write a test that *demonstrates* the bug,
then fix it.

1. Read the contract in `clamp.h` and find the input the implementation gets
   wrong.
2. Write `test_clamp.c` in the same directory — a small C program with a
   `main()` that checks `clamp` against the contract (print `ok`/`not ok`
   lines; return nonzero when any check fails).
3. Run `sh docs/assignments/07-write-the-test-first/test.sh` and watch your
   test **fail**. That failing run is the point of the exercise — it proves
   the test can detect the bug.
4. Now fix `clamp.c` and run again.

The runner insists on the process where a script can: it refuses to pass while
`test_clamp.c` is missing, and it re-checks the fix with its own acceptance
probe — so neither a fix without a test nor a test without a fix will pass.
The step it cannot see is whether you watched your test fail before fixing;
that one is on you, and it is the habit this module exists to build.

Grade with `jichi grade docs/assignments/07-write-the-test-first.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/07-write-the-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/07-write-the-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
