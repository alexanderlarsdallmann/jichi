---
title: Session notes — the record you'll thank yourself for
audience: student
phase: process
difficulty: easy
points: 2
verify: "sh docs/assignments/71-process-session-notes/test.sh"
hints:
  - "Run the grader. It wants at least 3 dated notes (`notes/YYYY-MM-DD.md`), each with the did / decided / next spine."
  - "A useful session note answers three things: what you DID, what you DECIDED (and why), and what's NEXT. Dated, so you can retrace your own path weeks later."
  - "Write three files `notes/2026-08-01.md`, `2026-08-02.md`, `2026-08-03.md`, each with `- Did:`, `- Decided:`, `- Next:` lines. (Edit the given `notes/2026-08-01.md` and add two more.)"
---
Working alone, you have no standup and no teammate's memory — so the record *is*
your memory. Good **session notes** let you pick up where you left off and, later,
reconstruct *why* you did something. The spine that makes a note useful is three
lines: what you **did**, what you **decided** (and why), and what's **next**.

This task's `notes/` folder has one thin entry. Keep a real log: at least three
dated entries, each with the did / decided / next spine.

> **The floor vs. the judgment.** The grader checks the entries are dated and
> carry the spine. Whether they are notes you would actually *thank yourself for*
> — honest, specific, capturing the real decisions — is your judgment.

Write (or grow) `notes/YYYY-MM-DD.md` to at least three dated entries with the
spine. Then:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/71-process-session-notes.md
```

> **If you are stuck alone:** the "Decided" line is the one to get right — future
> you will not remember *why* you chose SQLite over Postgres unless you wrote it
> down. Record the reason, not just the choice.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/71-process-session-notes.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/71-process-session-notes.md` (or `/hint`) gives one rung at a time — free, and recorded. This task needs only jichi (no toolchain).
