# Case studies — real agent campaigns, kept as study material

> **What this is.** Honest artifact bundles from driving jichi's assignment
> machinery against a real codebase with real models: the assignment as authored,
> the gate as proven, the **junior agent's solution**, a story per case naming
> what went wrong and what it taught — and, for the two cases run under the full
> protocol, a **prepared reference solution** to diff against. Each case's story
> lists exactly what its directory holds *and what it does not*: cases 1–2
> predate the protocol (no prepared reference) and case 4 has no authoring
> journal, because gemma's run produced a 0-byte one. Kept for two audiences: **learners and
> tutors** studying how graded agent work actually behaves, and **presenters**
> who need measured numbers rather than claims.
>
> Nothing here is synthetic. Every diff was produced by the run it is
> attributed to; every token count comes from the envelope journal; the
> failures are kept alongside the successes because the failures carry most of
> the lessons.

## The campaign: zigodot, 2026-08-12/13

Two days of driving jichi headless on the zigodot repository (a from-scratch
Godot-family engine in Zig) with the two HRZ JLU models —
`jlu/qwen3-coder-next` (coder tier) and `jlu/gemma-4-31b-it` (31B
instruction-tuned). The pipeline per case: a model **authors** the assignment →
the gate is **proven red** (`jichi grade --expect-fail`) → a **reference
solution** is prepared, proven green, and reverted → the **junior agent**
(`jichi attempt --agent learner-junior --keep-worktree`) works it → the kept
worktree is **reviewed** and, if clean, merged.

| Case | Task | Author | Junior outcome |
|---|---|---|---|
| [01 — keyframes.interpolate](zigodot-2026-08/case-01-keyframes-interpolate/STORY.md) | un-panic the variant interpolation | **qwen3** — addressed to gemma, silently served by the coder tier (the pre-M411 routing override, proven in the run's journal); hollow gate, repaired | day 1: **"PASS" by editing the gate tests** → the TAINTED verdict exists because of this; day 2: solved cleanly as a side effect of case 2 |
| [02 — ping-pong loop mode](zigodot-2026-08/case-02-pingpong-loop/STORY.md) | implement the `.loop_pingpong` wrap | gemma (excellent prose; both edit runs spliced into neighbors) | clean PASS, 973k tokens, 0 hints, 0 test edits |
| [03 — vector interpolation](zigodot-2026-08/case-03-vector-interpolation/STORY.md) | close case 1's gate-vs-spec gap | qwen3 (good content; overwrote an existing spec, hallucinated a `.lerp` API) | PASS, 1,356k tokens, 0 hints — and it **made the hallucinated API real** (accepted on review) |
| [04 — interpolation-mode dispatch](zigodot-2026-08/case-04-interpolate-mode-dispatch/STORY.md) | dispatch on the Interpolation enum | **tutor** (gemma's third run hung 22 min with a 0-byte journal; writing the gate then found a struct that had never compiled) | PASS, **520k tokens**, 0 hints, one file — and one design axis **better than the reference** |

The day-by-day defect write-up (five jichi findings, four fixed the same day)
lives with the zigodot repository:
`docs/analysis/2026-08-12-driving-jichi-on-zigodot.md`. Day 2's campaign added
two more jichi findings, both in `DEFERRED.md`: a hang **before the first tool
boundary** is outside every envelope budget (22 minutes, 0-byte journal, ended
by hand), and the envelope cannot tell a concurrent actor's edits from its own
run's — so `revertOutOfScope` and a mid-run merge by someone else are one
working tree away from undoing each other. Operating rule until then: **one
envelope per working tree at a time.**

## The lessons, and where they are taught

Each lesson is taught where its audience reads — this table is the index, not
the copy:

| Lesson | Where |
|---|---|
| A gate the worker can edit is a suggestion; the fence made the same model honest **and** 10× cheaper | [`ANECDOTES.md` #51](../ANECDOTES.md) |
| A tolerant reader above a partial parser turns author intent into silent data loss (64 of 80 hint ladders) | [`ANECDOTES.md` #52](../ANECDOTES.md) |
| The inherited verifier can contradict a test-first brief; the inverted gate is the honest verifier | [`GATE_INTEGRITY.md` §10a](../GATE_INTEGRITY.md) |
| A broad verify couples assignments; a name filter interacts with lazy analysis | [`GATE_INTEGRITY.md` §10b](../GATE_INTEGRITY.md) |
| The gate is the floor, not the spec | [`GATE_INTEGRITY.md` §10c](../GATE_INTEGRITY.md) |
| "Coding model" is not one skill: prose vs surgical edits, measured | [`CHOOSING_A_MODEL.md`](../CHOOSING_A_MODEL.md) (the scoreboard) |
| Prove the gate before a learner spends a token | [`ASSIGNMENTS.md`](../ASSIGNMENTS.md) (`grade --expect-fail`) |

## Recommendations and design decisions

The campaign's residue for jichi's **assignment** and **learn** features. Items
marked *decided* carry a `DECISIONS.md` row; items marked *recommended* are
open, some with a `DEFERRED.md` row.

### The assignment feature

1. **Decided — gates are the tutor's job; models author prose.** Both models
   damaged code mechanically (splices, overwritten files, a gate a correct
   implementation would fail); both authored useful prose. The working protocol:
   the model writes the spec, the tutor writes the gate tests, and
   `grade --expect-fail` notarises the red state. Rejected alternative: letting
   the authoring model write its own gate — measured twice, it produces either
   a hollow gate or an unsatisfiable one.
2. **Decided — spec-specific verifies, with the two sharp edges stated.** A
   suite-wide verify couples assignments (a learner set one task had to solve
   two). The fix is a test-name filter — and it took its own measurement to get
   right: a leaf-only filter excluded the *gate block that imports the test
   file* (lazy analysis → 0 tests → green), so the filter must name both, and a
   nothing-matching filter is exactly the hollow shape `--expect-fail` catches.
3. **Done (M415) — `attempt` emits a machine row.** `attempt --output json`
   prints one gradebook object to stdout (verdict, passed, test_edits,
   hints_used, tokens, `files_changed[]`, kept-worktree path) while the human
   verdict stays on stderr. The files-changed list earned its place in case 3:
   the junior's out-of-task edit to a core type was invisible to its own
   filtered verify and surfaced only in the kept worktree.
4. **Done (M415) — `attempt --record`.** The attempt appends a progress line
   *with the hints-used count* (the field existed; `grade` is stateless and
   passes −1) and with the **verdict's** truth: a TAINTED attempt records
   `passed:false`, because green earned by moving the gate is not a pass in the
   gradebook either.
5. **Done (M416) — the "file discipline" lines in the `/assign` scaffold** (and
   in zigodot's live copy): create a NEW file, never modify an existing spec,
   close the frontmatter fence, quote colon-bearing hints, and name a verify
   that FAILS on the untouched tree because the tutor will prove it with
   `--expect-fail`. Each line exists because a capable model violated it;
   the scaffold says so, and the enforcement stays the edit scope.

### The learn feature

6. **Withdrawn — already built (M330), found on checking.** The
   `learn_on_stop` journal event *does* carry the mentor's tokens, and `runs`
   *does* surface them in both the notes column and the JSON. This
   recommendation was written from the run's stderr without reading
   `jc_runsview` — the register's own rule ("check the checkable part before
   parking") applied one day late. Kept rather than deleted, because a
   withdrawn recommendation that leaves no trace gets re-proposed.
7. **Done (M417) — the moved goalpost reaches the learn loop.** The M88 warning
   is now mirrored to telemetry (`test_edit`, with tool + path), `telemetry`
   renders a loud `Goalposts:` line, and `learn analyze` raises an insight at
   threshold **one** — every other rule needs repetition; a single gate edit is
   the difference between a grade and a gamed grade. The measured case (ten
   warnings, verdict PASS, evidence deleted) can now end as a drafted lesson
   instead of a warning nobody mines.
8. **Observed, no action — learn-on-stop correctly skipped interrupted runs**
   (`budget_exhausted` runs drafted no lessons, with the reason printed). That
   guard is the right shape; noted so nobody "fixes" it.

## Using this for a presentation

The one-slide version of the campaign is the scoreboard in
[`CHOOSING_A_MODEL.md`](../CHOOSING_A_MODEL.md); the one-story version is
[`ANECDOTES.md` #51](../ANECDOTES.md) (the moved goalpost — it has a villain,
a fence, and a 10× number). The per-case directories hold the diffs to show on
screen — and the two-diff comparison audiences ask for
(`reference-solution.diff` beside `junior-solution.diff`) exists for **cases 3
and 4**, not 1–2. Case 4's pair is the best of them: the same `switch`, written
two ways, where the junior's exhaustive form beats the tutor's `else`.
`authoring-run.journal.jsonl` (cases 2 and 3) is what "the envelope journals
every run" looks like in practice.

## Honest limits

- **n=1 codebase, n=2 models, 4 tasks, 11 runs.** These are case studies, not
  benchmarks; they generalise the way war stories do, not the way measurements
  do. The bench (`tests/bench/`) is the measurement instrument.
- The reference solutions were written by the supervising agent (Claude), not
  by a human tutor — stated so nobody mistakes them for independent expert
  baselines.
- Token counts are per-run envelope totals on one gateway with prefix caching
  in an unknown state per run; compare within the campaign, not across
  providers.
