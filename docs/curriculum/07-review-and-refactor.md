# Module 7 — Review and refactor

*Stage 2 (破（は） Ha) · ~4–6 h · assignments:
[`11-name-whats-wrong`](../assignments/11-name-whats-wrong.md) (2 pt, with a
reference [review](../assignments/11-name-whats-wrong.solution.md)),
[`12-refactor-without-change`](../assignments/12-refactor-without-change.md)
(3 pt) · map: [CURRICULUM.md](../CURRICULUM.md)*

Review is the craft of naming what is wrong **and why it matters**; refactor
is the craft of fixing it while provably changing nothing else. They are one
loop — a review that never becomes a diff is opinion; a "cleanup" without a
review behind it is churn — and both are skills you now need *on the agent's
output*, which arrives confident, plentiful, and unreviewed.

## The work

**1. Review with your own eyes first.** Work
[`11-name-whats-wrong`](../assignments/11-name-whats-wrong.md): read the
file top to bottom once *before* asking the agent anything. Then use the
agent as a second reviewer — "review smelly.c; for each finding, name the
future change it makes riskier" — and reconcile the two lists. Where you
disagree with it, say why in your REVIEW.md; where it found what you missed,
that goes in your record. The reference review is the comparison bar, same
ritual as Module 3's worked solution: floor first, compare honestly after.

**2. A smell is a consequence, not a preference.** The discipline the
assignment grades (structurally) and the reference models (fully): every
finding names *what future change* becomes riskier or costlier, and for
whom. "I would have written it differently" is not a finding.

**3. Refactor under a green light.** Work
[`12-refactor-without-change`](../assignments/12-refactor-without-change.md):
the tests are green before you start and must be green — **unchanged** —
after. Make the two moves as two small steps, running the tests between.
This is where Stage 1's habits compound: read the diff (M2), let the tests
arbitrate (M3), and if a step goes red, `/undo` the step, not the task.

**3b. One more reviewer lens, shipped:** `/a11y-review` runs the read-only
accessibility reviewer against a deliverable — the same argued-by-consequence
discipline (who is excluded, what unblocks them). Accessibility is the
review dimension most codebases defer forever; you now know why that is a
smell.

**4. Let jichi review jichi's work.** For real work (not these fixtures),
the same loop is built in: **self-review** feeds the turn's own diff back to
the model before the verify gate (`/review`, [GIT.md](../GIT.md)), and
`/check` runs the read-only reviewer profile against a rubric. Neither
replaces your eyes; both are cheap second passes you should now know how to
weigh — you have been both the reviewer and the reviewed.

## The gate

Both assignments `passed`, and your `11` findings honestly compared against
the reference (differences understood, not just noticed).

## Reflection

*(from [JOURNEY.md](../JOURNEY.md))* — your record should by now contain an
entry where the root cause was *you*. If review keeps finding only other
people's smells, look again.

> **If you are stuck alone:** for 11, the ladder narrows the search
> (repetition, unexplained numbers, unreachable code). For 12, if the tests
> go red mid-refactor, do not push through — `/undo`, take the smaller step.
> The refactor that cannot be done in small green steps is telling you
> something about the code (or the plan); write down which.

---

[◀ Prev](06-design-first.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](08-bounded-autonomy.md)
