# Case 3 — vector interpolation: the author hallucinated an API, the junior built it

**Task.** Close case 1's gate-vs-spec gap: `keyframes.interpolate` handled only
`.float` while its callers animate `Vector2`/`Vector3` properties. Extend it
component-wise. This is the first case with the **complete bundle**: authored
spec, proven-red gate, prepared reference solution, and the junior's solution.

**Authoring (qwen3-coder-next, pinned — the deliberate contrast to gemma
authoring case 2).** The content was good — the hints referenced real files and
the spec adopted the spec-specific verify — and the *file discipline* was not,
three ways:

1. it **overwrote case 1's assignment** instead of creating a new file
   (restored from git);
2. it **dropped the frontmatter's closing `---`**, so nothing parsed — jichi's
   M409 note (*"1 hint line could not be read"*) surfaced the second half of
   that once the fence was restored;
3. its hints **confidently described a `.lerp` method on the Variant's
   vectors** — a method that exists only on the *other* `Vector2`, in
   `core/math`. A learner following hint 2 as written would have hit a compile
   error. Repaired against the real API, with the two-types trap stated.

**The gate (hand-written, per the campaign protocol).** Three tests filtered by
name — and getting the filter to *reach* them took its own measurement:
zigodot's test files are imported from inside named `"gate: …"` test blocks, and
zig's lazy analysis means a leaf-only filter excludes the block that imports the
file — **0 tests, green, the hollow shape**. The verify therefore names both:

```
zig build test -Dtest-filter='gate: animation' -Dtest-filter='interpolate vectors'
```

`jichi grade --expect-fail` notarised the red state (2 of 4 filtered tests
failing) before any token was spent — and earlier, under the *broad* verify, it
had exposed its own documented limit by reporting "RED as expected" for this
spec courtesy of a sibling's red gates.

**Reference solution** (`reference-solution.diff`): component-wise math inline
in `keyframes.zig` using the file's own `lerp`, with a comment naming the
two-Vector2s trap. Proven 4/4 filtered + whole suite green, then reverted.

**The junior (qwen3, `attempt --keep-worktree`, PASS, 1,356k tokens, 0 hints,
0 test edits).** The arc that makes this case worth presenting: the junior
never read the repaired hints (0 used) — and *independently reproduced the
author's instinct*, then went one step further: **it made the hallucinated API
real**, adding clean `.lerp` methods to the Variant's `Vector2`/`Vector3` in
`src/core/variant/variant.zig` and switching `interpolate` over them. Same
model, author and junior, same wrong belief about the API — one wrote it into
prose, the other wrote it into existence.

**The review.** The core-type change is *outside the file the task named* —
nothing flagged it, because `attempt`'s sandbox is the whole worktree (no edit
scope). Reviewed and **accepted**: the methods are additive, mirror the sibling
`core/math` API exactly, and the whole suite stayed green in the worktree
(checked before merging — the filtered gate alone could not have seen a
regression elsewhere). A stricter tutor could reasonably have required the
component-wise form; the difference between the two diffs in this directory is
a ready-made discussion exercise.

**What this case teaches.**

1. **Two agents sharing a model share its blind spots.** Authoring and solving
   with the same model means the solver may *ratify* the author's hallucination
   instead of catching it — a reason to split author and solver across models,
   or to have a human own the hints.
2. **A filtered gate sees only itself.** The junior's out-of-task edit was
   invisible to its own verify; the reviewer's whole-suite run is not optional.
3. **Compare the diffs.** `reference-solution.diff` (local math) vs
   `junior-solution.diff` (extend the core type): both green, different design
   philosophies, and *neither is wrong* — which is exactly the conversation a
   grading rubric cannot have and a tutor can.
