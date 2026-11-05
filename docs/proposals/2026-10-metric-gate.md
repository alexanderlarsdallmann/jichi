# A metric gate beside the test-edit hold: when a moved assertion may merge

*Design and decision record. **Built as M875 (2026-10-02).** Written on the workstation for the
operator's decision of that day: "Go with 1., and build (b) the metric gate, as well, so either option
is configurable. Write honest, and comprehensive design decisions, and recommendations." The code is
`src/tools/jc_tool_parallel.c` (`metric_gate`, `metric_measure`, `metric_base_measure`) and the pure
parts in `src/tools/jc_parallel.c` (`jc_parallel_metric_parse`, `jc_parallel_metric_worse`,
`jc_parallel_path_is_pin`, `jc_parallel_metric_fmt`).*

## 1. The question

A `spawn_parallel` write child works in its own worktree. Before its changes merge into the lead's
tree, jichi runs the configured verify in that worktree (M144, `parallelVerify`): a red verify keeps
the work instead of merging it. Since **M865** one more thing keeps it: a hunk in a test file the
child *modified* whose removed and added lines both carry an assertion, the old text not kept. A
child can turn its verify green by moving an expected value instead of fixing the code, and a green
verify reached that way is not evidence.

The case that decided M865 was the demos drive's card 215: its worker got "expected 47, found 43"
from the drive's scoreboard pin, and setting the pin to 43 would have turned its verify green and
merged a regression from 47 demos to 43. Only the lead's own census stood in the way.

The cost came with the next measurement. **N46** (threadwork, 2026-10-02): every demo the drive has
flipped since Thursday reached its main branch by bumping that same pin --
`try std.testing.expectEqual(N, l1_in_scope_from_test);` in `src/demos/scoreboard_test.zig` --
seven of seven. M865's own matcher, run on the seven real diffs, holds all seven; it holds neither
control (the same diff under a path without "test", and a pure addition). So with M865 in effect,
the drive's every honest flip comes back in the Kept list as `parallel-test-edit`.

What "take it on purpose" means today is several steps: `jichi attempts show <ref>` reads the kept
state, `jichi recover <ref> --into <dir>` materialises it into a separate worktree (never the live
tree), and the lead applies the change itself. A `"from":"<ref>"` resume is no way around it: the
resumed child's diff still moves the assertion, so it is held again.

Threadwork's proposal (b), recorded with M865 as "not part of the decision", is the alternative: a
gate that **compares a declared metric with the base's run of the same command**, so that the merge
does not rest on a number the child can type.

## 2. What the operator decided

1. **The hold stays the default.** Nothing changes for a configuration that does not ask.
2. **(b) is built beside it, selectable in the configuration.** An operator who declares a metric
   and the pin files it governs can let a moved pin merge when the measurement backs it.

## 3. The design

### 3.1 Configuration

```json
"parallelTestEdits": "metric",
"parallelMetric": {
  "command": "zig build census",
  "label": "L1 in scope:",
  "better": "higher",
  "pins": ["src/demos/scoreboard_test.zig"],
  "timeoutSecs": 1200
}
```

- `parallelTestEdits`: `"hold"` (the default, M865) or `"metric"`.
- `parallelMetric.command`: a shell command, run in a worktree, that prints the metric. Required in
  `"metric"` mode.
- `parallelMetric.label`: the metric is the first number after the first occurrence of this text in
  the command's output. Without a label, it is the last number the command prints.
- `parallelMetric.better`: `"higher"` (the default) or `"lower"`.
- `parallelMetric.pins`: the files whose moved assertions the metric may decide, as paths relative
  to the project or globs as `editScope` reads them (`*` within one path segment, `**` across
  them). Required in `"metric"` mode.
- `parallelMetric.timeoutSecs`: the limit for one run of the command; the verify's limit when unset.

A `"metric"` mode without a command or without pins is not a metric gate: jichi warns once, when it
loads the configuration, and holds as in `"hold"` mode. `-q` hides warnings, so `jichi doctor` is
where to look for it under a quiet harness.

### 3.2 What happens to a write child, in `"metric"` mode

After its worktree verify is green and its changes are listed:

1. M865's matcher lists the files where it moved an existing assertion.
2. **None:** it merges, as always.
3. **Any of them outside `pins`:** it is held, as in `"hold"` mode (`parallel-test-edit`). The
   metric says nothing about an assertion it does not measure.
4. **All of them in `pins`:** jichi measures.
   - **The base,** once per batch and only when a child needs it: in a fresh worktree of the
     batch's base commit, removed afterwards.
   - **The child,** in its own worktree.
   - **Either measurement unknown** -- the command failed, timed out, or printed no number -- it is
     held, and the note says which and why.
   - **Worse than the base:** it is held as `parallel-metric`, and the note gives both numbers.
   - **Not worse** (equal counts): it merges, and the note says it moved a pin and what the metric
     read: "[merged: this task changed an existing test assertion in a pin file
     (src/demos/scoreboard_test.zig), and the metric did not fall: base 51, this task 52 (higher is
     better)]".
5. Each measurement is a `metric_gate` telemetry event and journal entry: `task`, `verdict`
   (`merged`, `held` or `unknown`), and `base` and `value` where they were measured.
6. The child's note names a held file outside the pins as such: "changed an existing test assertion
   outside the metric's pin files (...)".

## 4. Decisions, and what was rejected

Each decision here is one a reader could reasonably make otherwise.

**D1. The hold stays the default** (the operator). A moved assertion proves nothing by itself, and a
configuration that has not declared what would prove it keeps the conservative answer.

**D2. Measure the base; do not trust the old pin.** *Rejected: compare the old pin's number with the
new one.* That costs nothing -- with a green verify the new pin equals the worktree's measurement,
and the old one should equal the base's -- and it would have caught card 215 (47 to 43). But "should"
is the problem: it holds only when the base's own verify was green, which jichi does not check, and
it reads numbers out of assertion text, which differs per language and test framework. The base's
run is the independent evidence the hold was missing. Its price is one run per batch (§5).

**D3. The metric decides only the declared pin files.** *Rejected: any moved assertion merges when the
metric did not fall.* A child that breaks a function and rewrites that function's test to match
leaves the census untouched. The metric speaks for what it measures, and an operator names those
files.

**D4. An unknown measurement holds.** *Rejected: fall back to the verify.* The verify is what the
moved pin made green; returning to it is returning to the hole.

**D5. The base is measured lazily, in a fresh worktree at the batch base.** *Rejected: measure the
live tree before the batch starts* -- it costs every batch, including those whose children move no
pin, and the drive's census is not cheap. *Rejected: measure the live tree at merge time* -- the
merge loop merges children one by one, so after the first merge the live tree is no longer the
base.

**D6. Equal merges.** With a green verify, a moved pin equals the worktree's measurement; if the
metric is the same as the base's, the moved pin cannot be hiding a fall in it. A stale pin corrected
to the measured value is the common honest case.

**D7. The operator's configuration declares the gate, not the lead or the task.** *Rejected: a task
field naming the files whose assertions the child may move* -- the simplest option, and the one that
would have merged card 215's 47-to-43: the lead would have declared the pin file for a task meant to
flip a demo, and the child's downward move would have been as declared. A model must not be the one
to relax its own gate.

**D8. Both keys are GATED in a project configuration.** `parallelMetric.command` runs a program, and
`"metric"` widens what merges. An untrusted project file sets neither; `--config` and the user's own
configuration do (M851).

**D9. Only a child that moved a pin is measured.** *Rejected: measure every write child -- a general
"the metric must not fall" gate.* It is a different feature with a different cost (a run per child),
and nobody asked for it. It is recorded as a possible extension (§6).

**D10. The number is found by a label or taken last.** *Rejected: a regular expression* -- jichi
depends on libcurl and nothing else, and POSIX `regcomp` would be a new portability surface on its
DOS, Haiku and Windows rows for one config field. *Rejected: JSON output* -- it would impose a format
on the project's own tool. A label covers a line such as `L1 in scope: 52`; a command whose last
output is the number covers the rest.

## 5. What it does not do -- the honest limits

- **The metric runs the worktree's own code.** A child that changes how the number is computed
  changes the number. In the demos drive the census runs the very VM the tasks change, so a child
  that special-cased a demo into passing would raise the census, as it would raise the pin. The gate
  closes "type the number"; it does not close "change what is measured", and nothing short of a
  measurement the child cannot reach would. The lead's census has the same property.
- **It costs time:** one base run per batch that needs it, from a fresh worktree with no build
  products, and one run per child that moved a pin. For the drive that is a census build and run.
- **A metric that varies by itself** -- a timing, a flaky count -- holds or merges by noise. It
  should be a deterministic count.
- **Two children that flip the same pin** both measure against the batch base. The second one's
  edit to the same line conflicts with the first's, and conflicts are kept, as today
  (`parallel-conflict`).
- **Taking a held state is still several steps.** The gate makes fewer states held; it does not make
  taking one easier.

## 6. Recommendations

1. **For the demos drive:** keep the hold until a pin that contains M875 is in effect. Then use
   `"metric"` with the L1 census as the command and `src/demos/scoreboard_test.zig` as the only pin.
   Keep the census harness out of every task's edit scope, and add one line to the lead prompt: a
   flip whose census fell comes back as `parallel-metric` and is not taken without a reason.
2. **For an operator in general:** use `"metric"` only with a deterministic metric the tasks cannot
   redefine, and pins that hold nothing but that metric's assertions. Otherwise keep `"hold"`.
3. **Watch it:** `jichi telemetry` counts `metric_gate` by verdict. A run of `unknown` verdicts means
   the command does not print what the label expects.
4. **Follow-ups, recorded in DEFERRED rather than built:**
   - a one-step way to take a kept state into the live tree (`jichi attempts apply <ref>`), def-309;
   - a general metric gate for every write child (D9), if a project asks for one, def-310;
   - measuring the worktree's build with the base's harness, which closes the first limit for a
     project whose harness is separate from what the tasks change, def-311.

## 7. How it is tested

`tests/smoke/parallel_metric_gate.sh` runs a batch of four write children with `parallelVerify` on, a
verifier that is always green, the metric `echo score: $(cat SCORE)` read after the label `score:`,
and the pins as the glob `src/score*_test.zig`:
1. a child that moves its pin and raises the score merges, and the note gives both numbers;
2. a child that moves its pin and lowers the score is kept as `parallel-metric`;
3. a child that moves an assertion outside the pins is held, as in `"hold"` mode;
4. a child whose score is no number is held, and the note says why;
5. telemetry records the three measured verdicts, and `jichi telemetry` counts them;
6. CONTROL: `"metric"` with no pins declared holds, and the run says so.

Each check was shown red under the perturbation that removes what it holds: the gate never
consulted (checks 1, 2, 4, 5); a fall merged (2, 5); a held file outside the pins not counted (3, 5);
an unknown measurement merged (4, 5); no telemetry (5); the count missing from `jichi telemetry`
(5); no warning (6); a base taken as 0 instead of measured (1, 2, 5). The pure parts (`tests/test_parallel.c`, 29 checks) were red under each of seven
perturbations: the label ignored, a number inside a word counted, no negatives, no fractions, the
direction ignored, pins matched without globs, and a huge value cut instead of written with an
exponent.

## 8. When to revisit

- **Unknown verdicts dominate** on a drive: the command or the label does not fit the project, or the
  base worktree cannot build from scratch. Fix the declaration before reading any verdict.
- **A merged flip later proves to be a regression the metric missed:** the first limit of §5 has
  happened, and measuring with the base's harness (§6, follow-ups) stops being optional.
- **The base run's cost dominates a batch:** cache the base's number by base commit, across batches.
