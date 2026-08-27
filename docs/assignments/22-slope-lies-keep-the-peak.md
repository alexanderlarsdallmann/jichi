---
title: Slope lies — keep the peak
audience: student
phase: testing
difficulty: advanced
points: 4
verify: "sh docs/assignments/22-slope-lies-keep-the-peak/test.sh"
hints:
  - "The contract in digest.h has TWO halves. Your checker needs a test main that reads all three gauges the arithmetic half never touches: the answer, track_live() after the call, and track_peak()."
  - Generate your own input file from your main or a shell loop -- many short lines and ONE long line -- and compute the expected digest independently (awk can run the same recurrence). What peak bound separates "scales with the longest line" from "scales with the file"?
  - "Shape: a main() that writes a ~200 KB file with a known digest and a 1 KB longest line, calls digest_file_lines, and exits nonzero unless answer == expected AND track_live() == 0 AND track_peak() < (a few KB). Compile it with the candidate + track.c; wire it in check.sh."
---
Module 5 taught you to write a checker; this task teaches you what a memory
checker must *measure*. `docs/assignments/22-slope-lies-keep-the-peak/`
holds a contract (`digest.h` — read both halves), a tracked allocator
(`track.h`) and **four candidates** — one correct, three wrong in three
different ways:

- one **answers wrong** (arithmetic);
- one **keeps a buffer forever** (what a leak checker sees — `track_live`);
- one **borrows the whole file and returns it politely** — every
  start-vs-end measurement calls it clean, and only the **peak** convicts
  it. This is the class this project learned to fear: its own tool-arena
  bug held a 50 MB intra-turn peak while the per-turn *slope* measured 0.0
  (`docs/analysis/2026-07-29-tool-arena.md`: "slope alone would have
  reported 'no problem'. Only the peak shows it").

Write `docs/assignments/22-slope-lies-keep-the-peak/check.sh`: it takes one
argument — a candidate `.c` path — and exits **0** iff the candidate meets
the *whole* contract. The runner hands your checker each candidate under a
neutral name, comment stripped; only behaviour can tell you which is which.

Grade with `jichi grade docs/assignments/22-slope-lies-keep-the-peak.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/22-slope-lies-keep-the-peak.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/22-slope-lies-keep-the-peak.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
