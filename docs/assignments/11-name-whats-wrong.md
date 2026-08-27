---
title: Name what's wrong — and why it matters
audience: student
phase: implementation
difficulty: intermediate
points: 2
verify: "sh docs/assignments/11-name-whats-wrong/test.sh"
hints:
  - Read the file top to bottom once before judging anything. Which function is never called? Which lines have you read twice?
  - A smell is not "I would have written it differently" -- it is a property that makes the NEXT change riskier or costlier. Name that consequence for each finding.
  - Look for exact repetition (would a fix be needed in two places?), for numbers whose meaning lives only in the author's head, and for code no path reaches.
---
`docs/assignments/11-name-whats-wrong/smelly.c` compiles cleanly and works.
It also contains **three classic code smells**, seeded deliberately. Review
it — read it yourself first, then use the agent as a second reviewer, not a
first one — and write your findings to
`docs/assignments/11-name-whats-wrong/REVIEW.md`, one section per smell:

```markdown
## Smell 1: <short name>
where: smelly.c:<line>
why it matters: <the consequence — what future change does this make
riskier or more expensive, and for whom?>
```

Code-review discipline is the skill: naming *what* is wrong is the cheap
half; naming **why it matters** — the concrete future cost — is what makes a
review actionable rather than a style opinion.

**How this is graded — honestly.** The floor checks structure only: three
`## Smell` sections, each carrying a `smelly.c:<line>` reference. A script
cannot judge whether you found the *right* three or argued the *why* well —
that is what the reference review
([`11-name-whats-wrong.solution.md`](11-name-whats-wrong.solution.md)) is
for: pass the floor first, then compare your findings against it honestly
(and `/check` gives rubric feedback on your argumentation). If you named a
real smell the reference missed — that happens, and it is a win, not an
error.

Grade with `jichi grade docs/assignments/11-name-whats-wrong.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/11-name-whats-wrong.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/11-name-whats-wrong.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
