# Three headless runs, two projects, and the cap that bound all three

**Date:** 2026-08-20 · **Milestone:** M504 · **Models:** `jlu/qwen3-coder-next`
only (local, free) · **Outcome:** two real defects found and committed in the
target projects, three lessons about driving, and one design question for jichi.

---

## 0. What was run

| # | Project | Task | Gate (explicit, `--verify-kind goal`) | Ended |
| --- | --- | --- | --- | --- |
| 1 | zigodot | add a compile-only build step for the agent server, then make it pass | `zig build agent-check` | `budget_exhausted` at 70/70 tool calls |
| 2 | zigodot | fix the one remaining compile error | `zig build agent-check` | `budget_exhausted` at 118/120 |
| 3 | chrtext | wire three dark test files into the gate | `sh scripts/gate-coverage.sh 18 && zig build test` | `budget_exhausted` at 76/90 executed |

Every run had an edit scope, a deadline, a tool-call cap, a run timeout and a
journal. All three used the M503 features: an **explicitly chosen** verifier
(`verify_source: flag` in each journal, never an inherited `testCommand`), and a
declared `--verify-kind goal` — correct here, since each gate was red before the
work by construction.

## 1. What the runs actually delivered

**zigodot: a compile-only gate, and the darkness it exposed.** Run 1 wrote the
`agent-check` step — `addExecutable` plus a step depending on the *compile*
artifact rather than `addRunArtifact`, so the module type-checks without binding
a port. That was the durable half. The module had been compiled by **nothing**:
`zig build test` never reached it, and `root.zig`'s `test "gate: agent"` only
imports the module type, which in Zig does not analyse its functions. Measured by
planting a type error *inside `init()`* — zero errors reported.

**chrtext: 21 test files the gate never ran.** Not a thin gate — it wires 71
`addTest` targets — but 21 tracked `*_test.zig` files were outside all of them.
Proven by planting a failing test in one (`exit 0`) and in a wired one (`exit 2`).
A floor script now fails when that set grows, and one file was wired with the
planted-failure proof.

## 2. The cap was the binding constraint in all three runs

| | tool calls | tokens | deadline used |
| --- | --- | --- | --- |
| run 1 | **70 / 70** | 3.06M | ~14 of 18 min |
| run 2 | **118 / 120** | 7.19M | ~16 of 20 min |
| run 3 | **76 / 90** executed | 6.57M | ~13 of 15 min |

Not once did the deadline or a token budget stop a run: **the tool-call cap did,
every time.** The reason is visible in the journals — an API migration spends most
of its calls *reading*: the standard library, the build file, the failing output,
again. That is the right instinct and it is expensive in calls, not in tokens
(all three ran on a free local model).

**The lever to reach for is therefore `--max-tool-calls`, not `--budget-tokens`,
on migration-shaped work.** The budget notice fired in every run, so the agent was
told; the cap was simply set for a different shape of task.

## 3. A brief that names a symptom invites a fix at the symptom

The sharpest lesson, and it cost a run. Run 1 fixed `main.zig` (the caller) so the
next error moved *into* `server.zig`. Run 2's brief quoted that new error —
`server.zig:95: expected 'mem.Allocator', found '*const mem.Allocator'` — and run
2 dutifully changed `server.zig:95`, **reverting the field to the shape whose
caller run 1 had just corrected.** Two narrow scopes, run in sequence, moved one
defect back and forth.

The brief should have named the **invariant**, not the line: *"the struct owns a
`*const Allocator`; callers pass `&`"*. A symptom is a coordinate; an invariant is
a rule. Only the second survives the fix moving.

This is the same failure mode as a bug report that says "line 40 crashes" instead
of "the list may be empty here".

## 4. The out-of-scope guard fired on the agent's own memory

Run 2's journal:

    "event":"out_of_scope","paths":[".jichi/memory.md"]

The run's scope was `src/agent/**`; the agent wrote its own memory file, which is
outside it. Detection-only here (`revertOutOfScope` off), so it was reported and
kept — the right outcome. But it raises a question worth deciding rather than
rediscovering: **should `.jichi/`'s own state be implicitly in scope?** Arguments
both ways, recorded in `DEFERRED.md`:

* *For:* it is jichi's own bookkeeping, not the user's code; reporting it as a
  violation trains an operator to ignore the notice — and with
  `revertOutOfScope: true` the M501 rule would revert it, since a run that used
  the shell is attributable.
* *Against:* an implicit exemption is a hole in a fence, and `.jichi/memory.md`
  is a file the *model* controls; a run that was told to touch only `src/**` and
  wrote elsewhere is exactly what the guard is for. An operator who wants it in
  scope can say `--edit-scope '.jichi/**'`.

## 5. A third occurrence for the budget-stop reporting row

All three runs ended `budget_exhausted` with **no `verify` event in the journal**
for runs 1 and 3 — the open DEFERRED row *"a budget-stopped run never runs its
verifier, so a run that satisfied its own gate is reported as a failure with no
gate verdict"*, now seen a third time. Run 1 is the interesting case: it did the
valuable half of its task, and the record shows only a budget stop.

Run 2 *did* journal one `verify` (exit 1) because it reached the gate before the
cap. So the row is not "verify never runs at a budget stop" — it is "the verifier
is not consulted **at** the stop", which is the shape the candidate fix already
describes: run it once for the record, never to change the outcome.

## 6. Where the local model stopped, honestly

Neither zigodot run reached a green verify, and the remaining six API mismatches
were fixed by hand (each small, each hidden behind the previous one). The seventh
— `std.json`'s object type is now an array hash map that the code still indexes —
is a multi-site migration and is left, red and documented, in that project's
`docs/analysis/2026-08-20-the-dark-agent-module.md`.

That is not a complaint about the model. It is the measurement: a **7B-class local
model on an unfamiliar standard-library migration** converges slowly and burns
tool calls reading. On the mechanical task (run 3: copy an existing build-file
shape) it produced correct code on the first attempt for one file and a plausible
but module-colliding attempt for another — which the gate caught, which is the
system working.

## 7. Lessons

1. **Name the invariant, not the line.** A brief quoting an error message invites
   an edit at that coordinate, including one that undoes the previous fix.
2. **For migration work, raise `--max-tool-calls` first.** Reading is the cost;
   tokens on a local model are not.
3. **A compile-only build step is worth more than a fixed compile.** The step
   outlives the fix and turns an invisible breakage into a red one.
4. **Prove darkness by planting, not by reading the build file.** Both projects'
   dark modules looked covered — one had a `test` block that imports the module,
   the other had 71 wired targets.
5. **A coverage measure whose false negatives depend on code layout is worse than
   a blunt one.** The first `gate-coverage.sh` missed two freshly wired files
   because they named their module first.
