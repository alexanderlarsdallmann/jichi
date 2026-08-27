---
title: Find the setting the program actually uses
audience: student
phase: implementation
difficulty: intro
points: 1
verify: "grep -qx '9' docs/assignments/01-find-the-setting/answer.txt"
hints:
  - Ask the agent to read the config file to you before you decide anything. What can it see?
  - One of the `retries` lines is commented out. Which one is real?
  - "Ask: read docs/assignments/01-find-the-setting/config.txt and tell me the effective value of `retries`. Then have it write just that number into answer.txt in the same directory."
---
The directory `docs/assignments/01-find-the-setting/` contains a small
`config.txt`. Find the **effective** value of the `retries` setting and have
the agent write it — the number alone, nothing else — into a new file
`docs/assignments/01-find-the-setting/answer.txt`.

Careful: config files accumulate history. Not every line that mentions
`retries` is live.

This is a *reading* exercise: the skill being practiced is asking the agent a
precise question about something it can see, and checking that its answer is
grounded in the file rather than plausible-sounding. Ask it to quote the lines
it based its answer on.

Check with `jichi grade docs/assignments/01-find-the-setting.md` (or `/grade`
after `/assignment`).

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/01-find-the-setting.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/01-find-the-setting.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
