---
title: The ambiguous edit
audience: student
phase: implementation
difficulty: medium
points: 2
verify: "[ \"$(awk '$0==\"[cache]\"{s=1} $0==\"[uploads]\"{s=2} s==1 && /^size/{print $3}' docs/assignments/05-the-ambiguous-edit/settings.ini)\" = \"256\" ] && [ \"$(awk '$0==\"[cache]\"{s=1} $0==\"[uploads]\"{s=2} s==2 && /^size/{print $3}' docs/assignments/05-the-ambiguous-edit/settings.ini)\" = \"512\" ]"
hints:
  - Both sections contain the identical line `size = 256`. An edit request that names only the line is ambiguous -- which one should change?
  - Give the edit an anchor that makes it unique -- the section header above the line is part of the address.
  - "Ask: in settings.ini, in the `[uploads]` section only, change `size = 256` to `size = 512`. The `[cache]` section keeps 256. Check the diff shows the change under [uploads]."
---
`docs/assignments/05-the-ambiguous-edit/settings.ini` has two sections that
currently carry the **identical** line `size = 256`. Change the size in the
`[uploads]` section to **512**. The `[cache]` section must keep 256.

This task exists because of how agent editing actually works: an edit is
find-and-replace on text, and text that appears twice cannot be addressed by
itself. Watch what the agent does with an ambiguous target — a good agent
widens its match to include context; a careless request changes the wrong one.
Your job is to make the request unambiguous and to *verify which line moved*
in the diff.

Check with `jichi grade docs/assignments/05-the-ambiguous-edit.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/05-the-ambiguous-edit.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/05-the-ambiguous-edit.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
