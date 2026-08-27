# Case 1 — keyframes.interpolate: the moved goalpost

**Task.** `keyframes.interpolate` in the zigodot animation system `@panic`s on
purpose (the house rule: a panic over a plausible default). Implement variant
interpolation so the gate tests pass.

**Gate.** Two hand-written tests in `src/animation/test.zig` (see
`gate-tests.diff`): linear midpoint on floats, exact endpoints. Added *after*
the authored spec shipped with `verify: zig build test` — which was **already
green**, so `jichi grade` reported PASS at 100% with the function still
panicking. That authoring defect is what `jichi grade --expect-fail` (M412) now
catches in one command.

**What happened, in three runs.**

| run | setup | outcome | tokens |
|---|---|---|---|
| `jichi attempt`, unfenced | learner-junior, writes anywhere in its worktree | **"PASS" by editing the gate tests** — the moved-goalpost warning fired ten times and the verdict ignored it; the worktree was then deleted, so the diff was unreviewable | 5,215k |
| `jichi -p --auto`, fenced | `--edit-scope src/animation/keyframes.zig` | correct float implementation, verify green, no test touched | 504k |
| `jichi attempt` (next day, on the sibling assignment) | junior, kept worktree | solved this assignment *too* — the shared suite-wide verify made its red gates part of the deal — cleanly, in f64 | (shared, 973k) |

**The artifacts here.**

- `assignment.md` — the spec as repaired (points rescaled, hints rewritten after
  the ladder-parsing defect; the original authored hints were angle-bracketed
  placeholders).
- `gate-tests.diff` — the hand-written two-sided gate (zigodot commit `736b74a`).
- `junior-solution.diff` — the merged solution (zigodot commit `cc3e457`,
  keyframes half): float-only, done in f64 with no precision loss.
- **No `reference-solution.diff`** — this case predates the campaign protocol,
  so its reference *is* the merged junior solution: the fenced 504k run and the
  day-2 attempt converged on the same float implementation. Cases 3 and 4 are
  the ones with a prepared reference to diff against on screen.

**What this case teaches.**

1. **A gate the worker can edit is a suggestion** — and the fence is what made
   the same model both honest and 10× cheaper. Full story:
   [`ANECDOTES.md` #51](../../../ANECDOTES.md).
2. **Verdicts must not outrank their own warnings** — jichi's `attempt` now
   reports **TAINTED** (exit 1) for a green verify with test-assertion edits,
   and `--keep-worktree` preserves the evidence (M410). Both exist because of
   this case.
3. **The gate is the floor, not the spec** — the merged solution satisfies both
   float tests while the spec's prose asks for vectors too. That gap became
   [case 3](../case-03-vector-interpolation/STORY.md).
