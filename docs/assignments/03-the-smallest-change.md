---
title: The smallest change that works
audience: student
phase: implementation
difficulty: intro
points: 1
verify: "grep -q 'Welcome to the bench' docs/assignments/03-the-smallest-change/greet.c && ! grep -q 'Welcome to jichi' docs/assignments/03-the-smallest-change/greet.c && grep -q 'return 0' docs/assignments/03-the-smallest-change/greet.c"
hints:
  - Name the file, the old text, and the new text. Vague requests produce big diffs.
  - Read the diff preview before approving. It should touch exactly one line.
  - "Ask: in docs/assignments/03-the-smallest-change/greet.c, change the greeting string `Welcome to jichi` to `Welcome to the bench`. Change nothing else. Reject any diff that touches more."
---
In `docs/assignments/03-the-smallest-change/greet.c` the program greets with
`Welcome to jichi`. Have the agent change the greeting to
`Welcome to the bench`. **Change nothing else.**

The skill is *diff literacy*: the approval prompt shows you exactly what will
change before it changes. The right diff here is one line. If the preview
touches two, say no and sharpen the request — rejecting a diff costs nothing;
un-approving one is what `/undo` is for (next module).

Check with `jichi grade docs/assignments/03-the-smallest-change.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/03-the-smallest-change.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/03-the-smallest-change.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
