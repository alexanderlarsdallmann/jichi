---
title: The invisible growth
audience: student
phase: implementation
difficulty: intermediate
points: 3
verify: "sh docs/assignments/21-the-invisible-growth/test.sh"
hints:
  - Run the test. A leak checker would report zero here -- so what, exactly, is the gauge counting that a leak checker does not?
  - Read buf_clear() and its header comment. "Capacity kept" is the right design for uniform traffic. Look at what actually flows through the spooler's one buffer -- is the traffic uniform?
  - "The fix is a policy, not a plumbing change: when clearing, release a capacity that one outlier inflated (pick a keep-bound and free above it), or pay a fresh buffer per message. Either passes; changing what the spooler outputs does not."
---

> **Prerequisite: a C compiler (`cc`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. On Debian/Ubuntu: `sudo apt install build-essential`; on macOS: `xcode-select --install`.

`docs/assignments/21-the-invisible-growth/` holds a message spooler with a
memory problem no leak checker can see: **every allocation is freed** (at
exit), nothing is unreachable — and while it idles, a megabyte sits resident
because one oversized message early in the batch grew the reusable buffer
and `buf_clear` keeps capacity by design.

This is the second of this project's real bug classes: **high-water
retention**. jichi's own provider stream buffers had exactly this shape —
one huge assistant message pinned its capacity ×2 until the provider was
destroyed (fixed with a shrink-on-clear bound; see
`docs/analysis/2026-08-01-telemetry-memory.md`). The gauge in `buf.h` is the
instrument: not "is anything leaked?" but "what is *live* right now, and
should it be?"

Have the agent diagnose why the idle footprint is ~1 MB and fix the
*policy*. The spooled output (pinned by a checksum) must not change.

Check with `jichi grade docs/assignments/21-the-invisible-growth.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/21-the-invisible-growth.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/21-the-invisible-growth.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
