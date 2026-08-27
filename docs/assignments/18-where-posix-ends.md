---
title: Where POSIX ends — a porting survey
audience: student
phase: documentation
difficulty: advanced
points: 4
verify: "sh docs/assignments/18-where-posix-ends/test.sh"
hints:
  - Capture EVERYTHING from the first `make` -- the failure order is data. A build log you didn't save is an experiment you didn't run.
  - Classify before you fix. The classes (missing-header / missing-symbol / semantic / runtime) are defined in docs/PORTING_WINDOWS.md; a finding that fits none of them is worth a note, not a shrug.
  - "The wall is not 'the first error' -- it is the first failure whose fix would be a subsystem rewrite rather than a patch. Argue it: name the subsystem, the Win32-side replacement, and why a shim won't do."
---
**Prerequisite (the one hardware requirement in this curriculum):** a
Windows machine or VM with **Cygwin** or **MSYS2** installed. Everything
else in the curriculum runs on your Linux bench; this task is *about* the
boundary, so it has to stand on it.

Attempt to build jichi where POSIX thins out, and produce the survey report
that [docs/PORTING_WINDOWS.md](../PORTING_WINDOWS.md) Layer 2 specifies —
this is a *portability* exercise wearing a build's clothes: the deliverable
is evidence and classification, not a working binary (you are not expected
to reach one; naming the wall precisely is the success condition).

Write your report to
`docs/assignments/18-where-posix-ends/REPORT.md`:

```markdown
## Environment
<Cygwin or MSYS2, versions, toolchain, jichi commit>

## Method
<what you ran, in order; where the full logs live>

## Findings
| # | file:line | what failed | class |
|---|---|---|---|
<one row per finding; class ∈ missing-header | missing-symbol | semantic | runtime>

## The wall
<the first failure that is a rewrite, not a patch: the subsystem, the
Win32-side replacement, why a shim won't do>

## What WSL gives you
<one paragraph: which of your findings simply vanish under WSL, and why>
```

At least **five findings** classified; the wall argued, not just named.
Compare your table against PORTING_WINDOWS.md's Layer 3 — findings it
misses are contributions (say so in the report).

**Grading, honestly:** the floor below checks the report's structure
(sections, a classified table of ≥ 5 rows, the wall section non-trivial).
Whether your wall is *right* is judgment — your instructor's, or `/check`
plus your own comparison against Layer 3 if you are alone.

Grade with `jichi grade docs/assignments/18-where-posix-ends.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/18-where-posix-ends.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/18-where-posix-ends.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
