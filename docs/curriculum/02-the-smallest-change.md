# Module 2 — The smallest change that works

*Stage 1 (守（しゅ） Shu) · ~4–5 h · assignments:
[`03-the-smallest-change`](../assignments/03-the-smallest-change.md) (1 pt),
[`04-two-places-one-truth`](../assignments/04-two-places-one-truth.md) (2 pt),
[`05-the-ambiguous-edit`](../assignments/05-the-ambiguous-edit.md) (2 pt) ·
map: [CURRICULUM.md](../CURRICULUM.md)*

Now you change things. The skills are **diff literacy** — the approval prompt
shows you exactly what will happen before it happens, and reading it must
never become reflex — and **reversibility**: learning in your hands that every
mistake can be walked back.

## The work

**1. The approval prompt is the contract.** Ask for a small edit and stop at
the preview. `y` approves once; `n` declines (and costs nothing — declining a
too-big diff and sharpening your request is the *expected* move, not a
failure); `e` lets you edit the proposed change; `v` shows the full arguments
([EDITING.md](../EDITING.md)). The right diff for a small request is small.
Big diff, small request — say no.

**2. Break something on purpose — then `/undo`.** This is
[JOURNEY.md](../JOURNEY.md)'s practice, and you should do it *now*, before any
real work is at stake. Pick any fixture file in the set, have the agent mangle
it, watch the damage — then:

```
/undo
```

jichi checkpoints your files before the first change of each turn
([SNAPSHOTS.md](../SNAPSHOTS.md)); `/checkpoints` lists the history,
`/rewind` walks conversation *and* files back together
([REWIND.md](../REWIND.md)). No grader can check this exercise — a restored
file is indistinguishable from an untouched one, and we do not pretend
otherwise — but do not skip it. Fearlessness is downstream of reversibility,
and everything after this module assumes you have both.

**3. Multi-file changes are one request.** Assignment 04's constant lives in
a header and is *described* in a README; both must move together. Name both
edits in one request and read a two-file diff before approving it.

**4. Ambiguity is your problem, not the agent's.** Assignment 05's target
line exists twice, identically. An edit is find-and-replace on text
([EDITING.md](../EDITING.md) explains the matching); text that appears twice
cannot be addressed by itself. The fix is in your request: give the edit an
anchor (the section header) that makes it unique — then verify *which* line
moved.

## The assignments

| Spec | Practices | Pts |
|---|---|---|
| [`03-the-smallest-change`](../assignments/03-the-smallest-change.md) | one-line diffs; rejecting oversized ones | 1 |
| [`04-two-places-one-truth`](../assignments/04-two-places-one-truth.md) | multi-file edits that must agree | 2 |
| [`05-the-ambiguous-edit`](../assignments/05-the-ambiguous-edit.md) | addressing a non-unique target | 2 |

## The gate

All three rows `passed`, **and** you have done the break-and-undo practice at
least once (on your honor — it is for you, not for us).

## Reflection

*(from [JOURNEY.md](../JOURNEY.md))* — The virtue trained here is the
strength to forgive, practiced on yourself, mechanically, until it is
character. `/undo` is how the tool teaches it.

> **If you are stuck alone:** `/hint`, then `/tutor`. If an edit went wrong
> and the workspace feels tangled: `/undo` (or
> `git checkout -- docs/assignments/<task>/` from the shell) resets the
> board — see the INDEX's reset section. A tangled workspace is never a
> reason to stop; untangling it is the module.

---

[◀ Prev](01-reading-before-writing.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](03-tests-are-the-truth.md)
