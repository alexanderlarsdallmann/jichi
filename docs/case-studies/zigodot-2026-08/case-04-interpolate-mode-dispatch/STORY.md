# Case 4 — interpolation-mode dispatch: the tutor authors, and the gate finds a dark region

**Task.** Dispatch on the `Interpolation` mode a keyframe carries: `.nearest`
snaps, `.linear` delegates, `.cubic` falls back to linear *honestly* (the
signature has no neighbor points). Tutor-authored — completing the campaign's
author spectrum: gemma (case 2), qwen3 (case 3), tutor (this case).

**Why the tutor authored it.** gemma's third authoring run **hung for 22
minutes with a 0-byte journal** — not even the `start` event — and was ended
with a graceful SIGINT. That produced a jichi finding worth more than the run:
a hang *before the first tool boundary* is outside every envelope budget (the
deadline is enforced at boundaries; `--heartbeat` is jsonl-only; the journal
opens with the run), so a supervisor's only tell is silence, indistinguishable
from a long model call. Recorded in jichi's DEFERRED with the fix shape
(journal `start` before the first network touch; a wall-clock backstop on the
*process*).

**The discovery that re-scoped the assignment.** The original target was
`AnimationEvaluator.interpolate_value` — and writing its gate found that **the
evaluator struct has never compiled when actually used**: its `std.HashMap`
field predates the current 4-argument API, and no test had ever constructed
the type. zigodot's own `gate-lint` counts *test blocks*, not analyzable
types; the first constructing test is what lit the dark region. The assignment
became the pure `keyframes.interpolateWithMode` (a panic stub + three gate
tests), with the evaluator's repair stated in the spec as a separate task —
and the spec's context section tells the learner this story, because "why is
the shape like this?" deserves an answer, not silence.

**The gate.** Spec-specific (`-Dtest-filter='gate: animation'
-Dtest-filter='interpolateWithMode'`), proven RED via
`jichi grade --expect-fail` (3 of 4 filtered tests crash on the stub), with the
reference solution proven green and reverted before the learner ran.

**The junior (qwen3, `attempt --keep-worktree`).** **PASS, 520k tokens,
0 hints, one file changed, zero test edits** — the cheapest and cleanest run of
the campaign. And the review found its solution **better than the reference**
on one axis: where the reference wrote `else => interpolate(...)`, the junior
enumerated `.cubic, .linear_angle, .cubic_angle` explicitly — so a future
`Interpolation` mode becomes a **compile error** instead of a silent linear
fallback. It also wrote the honest-fallback comment the spec demanded.

**The artifacts here.**

- `assignment.md` — the tutor-authored spec (note the context section telling
  the re-scoping story to the learner).
- `gate-tests.diff` — the stub + three gate tests (zigodot commit `83a84fd`).
- `reference-solution.diff` — the tutor's dispatch (`else` form).
- `junior-solution.diff` — the junior's dispatch (exhaustive form) — diff the
  two `switch` statements in a presentation and let the audience argue which
  is right; the exhaustive one wins on maintenance and that is worth showing.
- **No `authoring-run.journal.jsonl`** — there is nothing to show: gemma's
  authoring run hung for 22 minutes and produced a **0-byte journal**, which is
  the finding this case carries.

**What this case teaches.**

1. **A constructing test is a different instrument from a reachability count.**
   `gate-lint` said the file's tests run; only *instantiating the type* found
   the struct never compiled. Coverage of names is not coverage of types.
2. **Well-specified small tasks are where a junior agent shines** — 520k
   tokens, no hints, no drift, and one genuine design improvement over the
   reference. Compare case 1's unfenced 5,215k disaster: the difference is not
   the model, it is the spec, the gate, and the fence.
3. **Tell the learner why the assignment is shaped oddly.** The re-scoping
   paragraph in the spec costs four sentences and turns an "arbitrary" task
   into a war story with a moral.
