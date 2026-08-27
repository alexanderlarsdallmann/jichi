---
title: Two places, one truth
audience: student
phase: implementation
difficulty: easy
points: 2
verify: "grep -q 'RING_CAP 128' docs/assignments/04-two-places-one-truth/ring.h && grep -q '128 entries' docs/assignments/04-two-places-one-truth/README.md && ! grep -q 'RING_CAP 64' docs/assignments/04-two-places-one-truth/ring.h && ! grep -q '64 entries' docs/assignments/04-two-places-one-truth/README.md"
hints:
  - The constant is *used* in ring.c but *defined* in ring.h. Ask the agent where the value lives before asking it to change anything.
  - Two files must change together -- the header and the document that describes it. One request can name both edits.
  - "Ask for exactly two edits: `RING_CAP 64` -> `RING_CAP 128` in ring.h, and `64 entries` -> `128 entries` in README.md. ring.c must not change."
---
The ring buffer in `docs/assignments/04-two-places-one-truth/` must grow: raise
its capacity from **64 to 128**. The capacity is defined in `ring.h` and
described in `README.md`; both must agree afterwards. `ring.c` uses the
constant and must **not** be edited.

The skill: a change that lives in more than one place, made in one deliberate
request — and reading a two-file diff before approving it. Documentation that
contradicts the code is a bug you just haven't hit yet.

Check with `jichi grade docs/assignments/04-two-places-one-truth.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/04-two-places-one-truth.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/04-two-places-one-truth.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
