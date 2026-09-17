# Plan: test-running the whole curriculum, and what that phrase has to mean

*2026-09-17 (M652). The operator asked for a plan to test-run all assignments
and the complete curriculum. The first finding is that **one reading of that
request is already done and green**, and saying so is more useful than a plan
that quietly re-proposes it. The second is that the other reading is expensive,
and this page prices it before recommending it.*

---

## 1. What already runs, measured today

`tests/e2e/curriculum_graders.py` — 4,132 lines — drives **every** assignment
spec through `jichi grade` itself, never through a re-implementation of the spec
parser (the M20 lesson: a harness that re-implements the thing it tests proves
the harness).

Re-run for this plan, on this bench:

| | |
|---|---|
| Checks | **224** |
| Wall clock | **96 s** |
| Result | **0 failures** |
| Skips | **1**, loud: the Rust systems course 63–66, because `rustc` is not usable under this tier's private `$HOME` |

For every spec it asserts **both sides**: the pristine fixture must **fail**
(exit 1) and a reference solution must **pass** (exit 0). Compound graders get a
third assertion — a half-solution (a fix without the demanded test, a fix without
the debugging record) must **still fail** — because a grader that accepts a half
answer is hollow. It is part of `make ci`.

**So "test-run all assignments" in the sense of *does every grader work, on both
sides* is done, is gated, and takes a minute and a half.** The corpus is 84
graded tasks, 246 points, 71 trap cases; 62 of the 84 need nothing but POSIX
`sh`, and 22 gate on a toolchain (6 Zig, 4 each Racket, Guile, Elixir, Clojure,
plus the Rust set that skipped).

---

## 2. What does NOT run, and it is the interesting half

**No model has ever attempted the whole curriculum.** The grader tier proves a
grader can tell a right answer from a wrong one. It cannot tell you:

1. **Whether a task is solvable from its own spec.** The reference solution and
   the spec were written by the same author in the same sitting. A grader that
   is green on that pair is silent about whether a competent solver *who only
   has the spec* can reach it. This is the single most valuable thing a
   rehearsal would find, and nothing in the tree currently looks for it.
2. **Which tasks are under- or over-specified.** A task that every solver
   passes on the first try teaches nothing; a task that no solver passes may be
   ambiguous rather than hard.
3. **What a task costs.** A learner deciding whether to start task 58 deserves
   better than silence.
4. **Whether the hint ladder is reachable in an agent run** — and here there is
   already a measurement that says no. M319 and M320: across **24 `jichi
   attempt` runs on two tasks whose ladders are load-bearing, including one
   whose third hint contains the answer, the model called `hint` zero times** —
   including six runs that *failed* with the tool in hand. `ASSIGNMENTS.md`
   already draws the right conclusion (the ladder's real path is `/hint` in the
   TUI, driven by a person). A rehearsal should **not** be sold as testing the
   ladder; it would re-measure a question already answered.

---

## 3. The cost, before the design

The one honest data point in this tree: **M320 measured `jlu/qwen3-coder-next`
burning roughly a quarter of a million tokens over 23–25 model calls on a single
**4-point** task — and failing it.**

That is one model on one hard task, so it is an anchor and not a rate. Taking it
at face value across 246 points gives an order of **10–15 M tokens** for one
full pass; assuming most 1-point tasks are far cheaper gives a band whose bottom
is perhaps a fifth of that. **The honest statement is that a full pass is a
multi-million-token run whose spread is unmeasured**, which is exactly why §4
starts with a sample rather than a sweep.

Wall clock is the friendlier number: local models on this bench, one task at a
time, with 22 tasks gated on toolchains that must be installed first.

---

## 4. The plan, cheapest rung first

**Stage 0 — do nothing more, and be able to say why.** If the question is *"do
the graders work?"*, §1 answers it in 96 seconds and is already in `make ci`.
Stages 1–3 answer a different question. Do not run them to feel thorough.

**Stage 1 — a 10-task sample, to get a rate.** Pick ten tasks spanning the
point range (three 1-point, four 2–3-point, three 4-point) and all four
shu-ha-ri stages, all from the 62 that need only `sh` so no toolchain is in the
way. Run each under `jichi attempt` with a **local** model, fences on and
**caps off** — this is a measurement, and a `--deadline` that fires would
manufacture a plausible wrong answer, as it did twice in the M651 translation
sweep. Record per task: pass/fail, model calls, tokens, wall clock.
**Deliverable: a tokens-per-point rate with a spread**, which turns §3's
estimate into a number. **Decision gate: if the ten-task sample costs more than
budgeted, stop here and publish the rate.** That is a useful result on its own.

**Stage 2 — the `sh`-only corpus, 62 tasks.** Only if Stage 1's rate makes it
affordable. One model, one pass, no retries beyond `attempt`'s own. The
deliverable is a **per-task table**: passed / failed / cannot-run, with tokens.
The finding to look for is *not* the pass rate — it is the **tasks that fail for
a reason that is not difficulty**, because those are spec defects, and each one
found repays the run.

**Stage 3 — a second model, on the failures only.** Two models disagreeing on a
task is much stronger evidence about the *task* than one model failing it. This
is the same design as the M640–M641 cross-model refute run, and it is cheap
because it touches only Stage 2's failures.

**What must be true before any stage runs:**

- **Local models only** (`jlu/*` on the HRZ gateway, or LM Studio). A curriculum
  rehearsal is exactly the kind of long unattended run that turns into a bill.
- **A throwaway workspace and a throwaway `HOME` per task**, as
  `curriculum_graders.py` already does — and for the Zig tasks a **private build
  cache**, because M511 measured a shared `~/.cache/zig` making an identical
  source fail one run and pass the next.
- **Toolchain probes answer *usable*, not *present*** (M624): a `rustup` shim
  that cannot answer `--version` under the tier's private `$HOME` is **absent
  for grading purposes**, and the skip must say so loudly rather than pass.
- **`verify` exit 77 means cannot-run**, and must be recorded as a third outcome
  and never folded into "failed" (M625).

---

## 5. What this plan deliberately does not propose

- **Adding the rehearsal to `make ci`.** It needs a model, a network and
  minutes-to-hours; `make ci` must stay runnable on a checkout nobody prepared,
  which is the M621/M624 rule. This is a **measurement**, and measurements live
  beside `tests/bench/` and `tests/measure/`.
- **Grading the model.** The subject under test is **the curriculum**, not the
  agent. A run that reports "the model scored 61%" has measured the wrong noun.
- **Testing the hint ladder** — already answered, twice, in the negative (§2.4).
- **A pass-rate target.** There is no number that would be good news. A 100%
  pass rate would mean the tasks are too easy; 0% would mean they are broken.
  The output is a **list of tasks to look at**, not a score.
