---
title: The gate is green. The code is wrong.
audience: student
phase: testing
difficulty: advanced
points: 4
verify: "sh docs/assignments/14-the-hollow-gate/test.sh"
hints:
  - Run gate.sh, then run the compiled test yourself. Same code -- do they agree? Whatever explains the difference IS the finding.
  - Read gate.sh line by line and ask of each command -- if this fails, what happens to the script's exit status? One line swallows the verdict.
  - A gate is repaired when every failing step fails the gate -- the compile must be able to fail it, and the test's own exit status must BE the gate's. And a repaired gate is only proven when you have watched it go red (Module 5's law).
---
`docs/assignments/14-the-hollow-gate/` is a small library with tests and a
CI-style gate:

> **Prerequisite: a C compiler (`cc`).** This task's grader compiles C. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`. Without it the failure can read like a wrong answer rather than a missing tool.

```sh
# in the jichi checkout (repository root)
sh docs/assignments/14-the-hollow-gate/gate.sh
```

The gate is **green**. The code is **wrong**. Both statements are true at
once, and this project has lived the real version (ANECDOTES #17: a commit
chained on `cat ci.log && git push` — green whenever the log file *existed*,
whatever verdict was written inside it; the push went out on red).

Your work, in the order that matters:

1. **Catch it.** Establish, with your own commands, that the gate does not
   test what it claims. Don't take this brief's word for it either — the
   evidence is one comparison away.
2. **Repair the gate** (`gate.sh`) so that its exit status *is* the
   verdict: a compile failure fails it, a test failure fails it.
3. **Watch it go red.** Your repaired gate, on the shipped code, must fail —
   that red run is the proof the repair means something.
4. **Fix the code** (`rot13.c`), and watch the repaired gate go green for a
   reason this time. Leave `test_rot13.c` exactly as it is — it was always
   telling the truth; nothing was listening.

The grader replays your history mechanically: it runs your gate against your
fixed code (must pass) **and against the original buggy code**, kept in
`original/` (must fail). A gate that cannot fail is what you came here to
never write again. Detection graded both ways — like Module 5, because it is
Module 5, in the wild.

Grade with `jichi grade docs/assignments/14-the-hollow-gate.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/14-the-hollow-gate.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/14-the-hollow-gate.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
