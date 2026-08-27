---
title: Dispatch interpolation on the Interpolation mode
phase: implementation
difficulty: intermediate
audience: junior
domain: animation, interpolation
prerequisites: none
estimated_time: 1h
verify: "zig build test -Dtest-filter='gate: animation' -Dtest-filter='interpolateWithMode'"
points: 2
hints:
  - "The function receives the mode as a parameter: `interpolation: keyframes.Interpolation`. A `switch` on it is the whole shape of the solution."
  - "`.nearest` never blends -- it snaps to whichever endpoint is closer in time (t < 0.5 means `from`). `.linear` is already solved elsewhere in this codebase; delegate, do not re-implement."
  - "`.cubic` needs the neighboring keyframes (pre/post points) and this function does not receive them -- fall back to linear and SAY SO in a comment. A silent wrong cubic would be worse than an honest linear."
---

# Dispatch interpolation on the Interpolation mode

## Context & background

The animation system can blend two `Variant`s linearly
(`keyframes.interpolate`, extended to vectors in the previous assignment) —
but nothing dispatches on the **`Interpolation` mode** a keyframe carries.
`keyframes.interpolateWithMode` is the new pure function that does: given two
`Variant` values, a blend factor `t`, and a mode, return the interpolated value
**for that mode**. It starts as a `@panic` stub, per this project's rule that a
panic beats a plausible default.

*(Why not implement `AnimationEvaluator.interpolate_value` directly? Writing
this assignment's gate discovered that the evaluator struct has never compiled
when actually used — its `std.HashMap` field predates the current API and no
test had ever constructed the type. Repairing the evaluator is its own task;
this function is the dispatch it will delegate to once it compiles.)*

## Requirements

1. `.nearest` returns `from` when `t < 0.5`, else `to` — no blending.
2. `.linear` delegates to `interpolate(from, to, t)` in the same file.
3. `.cubic` (and every other mode, including the `_angle` variants) **falls
   back to linear for now**, with a comment stating why: this function receives
   no pre/post neighbor points, so true cubic interpolation is impossible at
   this signature. An honest linear beats a silent wrong cubic.
4. The function no longer panics for any input.

## Constraints & non-goals

- **Out of scope, stated:** wiring `AnimationEvaluator.interpolate_value` to
  delegate here — the evaluator does not currently compile when constructed
  (see above), and repairing it is a separate task.
- Do not change the function's signature.
- Do not edit the gate tests.

## How your work is checked

```sh
# in the zigodot checkout (repository root)
zig build test -Dtest-filter='gate: animation' -Dtest-filter='interpolateWithMode'
```

The gate tests live in `src/animation/test.zig` with names containing
`interpolateWithMode (assignment gate)`. They fail today (the panic); they pass
on a correct dispatch. The filter names the `gate: animation` block too —
zig's lazy analysis would otherwise never compile the test file (see
`build.zig`'s comment on `-Dtest-filter`).
