---
title: Hello, bench — one full turn
audience: student
phase: implementation
difficulty: intro
points: 1
verify: "grep -qx 'hello from my bench' docs/assignments/00-hello/hello.txt"
hints:
  - You do not write the file yourself -- you ask the agent to do it, then read the diff it shows you before approving.
  - Tell the agent the exact path and the exact line. Precision in, precision out.
  - "Say: create docs/assignments/00-hello/hello.txt containing exactly the line `hello from my bench`. Approve the write when the preview matches."
---
This is the gate of Module 0 (a working bench) and your first complete turn of
the loop this whole curriculum practices: **prompt → diff → approve → grade**.

Ask the agent to create the file `docs/assignments/00-hello/hello.txt`
containing exactly this line and nothing else:

```
hello from my bench
```

Before you approve the write, *read the preview* — the diff is the contract.
Then check your work yourself:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/00-hello.md
```

(In the TUI, load this brief with `/assignment docs/assignments/00-hello.md`
and grade with `/grade`.)

Done when the grade line says PASS. If it says FAIL, look at what the verify
command actually checks — that habit is half the course.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/00-hello.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/00-hello.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
