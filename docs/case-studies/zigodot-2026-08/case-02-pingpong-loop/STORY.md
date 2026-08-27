# Case 2 — ping-pong loop mode: gemma authors, the junior delivers

**Task.** The animation evaluator's `.loop_pingpong` branch carried a TODO: it
reflected time once at the top boundary but never reversed direction, never
handled a delta larger than the length, never bounced at zero. Implement a pure
`pingpongWrap(time, length)` helper and use it in `advance()`.

**Authoring (gemma-4-31b-it, pinned — jichi M411 verified live: zero route
events).** The prose is genuinely good and is the reason this case exists as a
positive example of model-authored teaching material: correct deep-bounce
semantics (2.1 → 0.1 *forward again*), a mermaid diagram that carries the
algorithm rather than decorating it, sound pseudo-code, a 3-rung hint ladder
that parses (`authoring-run.journal.jsonl` is the run's envelope journal).

**The gate (two bounded gemma runs, then by hand).** Asked to add a panicking
stub plus failing tests, gemma twice **spliced the insertion into a neighboring
function** — replacing `evaluate_track`'s signature in one file, stealing a
test's closing brace in the other — and its first run ended with a confident
success report over an *empty diff*. Its gate also compared f32s with exact
`==` where `1.1` is not representable: a gate a **correct** implementation
would have failed. All repaired by hand (`gate-and-repair.diff`); the first
gemma run also exposed jichi's inherited-verifier seam (an `--auto` run takes
`testCommand` as its completion gate, which for a *test-first* task points
fix-forward against the brief — see `GATE_INTEGRITY.md` §10a and the inverted
gate that fixed the retry).

**The junior (qwen3-coder-next, `attempt --keep-worktree`).** Clean **PASS in
973k tokens, 0 hints used** — a real datum, since the ladder served 3 of 3 —
with **zero test edits** (the TAINTED machinery attests it). It implemented
`pingpongWrap` correctly (period 2·length, positive `@mod`, reflect in the
second half) *and* the float half of case 1, which the shared suite-wide verify
forced on it. The kept worktree was reviewed before merging
(`junior-solution.diff`, zigodot commit `cc3e457`, evaluator half).

**What is in this bundle:** `assignment.md` · `gate-and-repair.diff` (named for
its contents — the gate tests **and** the hand repair of gemma's splices) ·
`junior-solution.diff` · `authoring-run.journal.jsonl` (the pinned gemma run's
envelope journal). **No `reference-solution.diff`:** like case 1 this predates
the protocol — the junior's was the first correct solution, reviewed and
merged.

**Review notes a tutor would write.**

- `advance()` uses the wrap but discards `reversed`; direction emerges from
  re-wrapping accumulated time each frame, which is a legitimate design — worth
  asking the learner *why* it still works.
- The coupling that dragged case 1 into this run is the measured argument for
  spec-specific gates — which case 3 then used.

**What this case teaches.** Model strengths are per-craft, not per-model:
the 31B instruction-tuned model authored the best prose of the campaign and
could not perform a mechanically simple insertion; the coder model was the
reverse. Scoreboard and selection lessons:
[`CHOOSING_A_MODEL.md`](../../../CHOOSING_A_MODEL.md).
