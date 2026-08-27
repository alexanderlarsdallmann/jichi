---
title: Which file defines it?
audience: student
phase: implementation
difficulty: easy
points: 1
verify: "grep -q 'src/beta.c' docs/assignments/02-where-is-it-defined/found.txt"
hints:
  - A *definition* has a body; a declaration ends in a semicolon and a comment is neither. Ask the agent to search, not to guess.
  - Have it run a search for `widget_init` across the directory and show you every hit, then classify each hit yourself.
  - "The mention in gamma.c is a comment and the one in alpha.c is an extern declaration; the body lives in src/beta.c. Write that path into found.txt."
---
Somewhere under `docs/assignments/02-where-is-it-defined/src/` a function
called `widget_init` is **defined**. Other files mention it — as a declaration,
or only in a comment. Find the file that holds the definition and have the
agent write its path, relative to the project root, into a new file
`docs/assignments/02-where-is-it-defined/found.txt` (one line).

The skill: driving the agent's *search* tools instead of trusting its first
association. A good prompt here names the symbol, asks for every occurrence,
and asks it to distinguish definition from mention.

Check with `jichi grade docs/assignments/02-where-is-it-defined.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/02-where-is-it-defined.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/02-where-is-it-defined.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
