# Does a `refute` stage find what a "be critical" prompt does not? (M634)

> **Run on 2026-09-17 (M637).** Result and reading: [`analysis/2026-09-17-refute-ab.md`](../analysis/2026-09-17-refute-ab.md) — refute 12 of 12, control 1 of 12; the frame did work the words did not. What below says "has not been run" was true when written and is kept as written.

*Pre-registration, written before any run, in the shape of
[`2026-08-craft-ab.md`](2026-08-craft-ab.md): the hypothesis stated so it can
fail, the model, the planted claims and the metrics fixed here, and what will
not be concluded. The stage exists (M634); the experiment has **not** been run.
When it is, the result page will link back here and report against these
numbers -- as run, or as not run, never as expected.*

---

## The question

M602's R2 recommended "ask the second seat to adversarially refute the first".
The craft A/B is the precedent for what happens to prose-level instructions:
"be critical" in an author's prompt is tier-3 guidance, and the craft section
moved nothing a grader could see. The `refute` stage is a *frame the author
cannot weaken* -- three forced headings, "nothing found" permitted, read-only
-- so the question is whether the **frame** does work the **prompt** does not.

## Hypothesis (falsifiable)

On **N = 12** recorded first-seat reports, each carrying **one planted false
claim** chosen before any run, the `refute` stage names the planted claim under
`## Rebutting` or `## Undercutting` in **at least 6 of 12**; the control -- a
second `synthesize` stage whose prompt says "Review the following critically
and list anything wrong" -- names it in **fewer**.

If the stage names 6+ and the control names as many, the frame adds nothing
over the words. If both name fewer than 6, the second seat -- of this model, on
these reports -- is not a move that finds planted errors, and R2 is a
recommendation that did not survive contact.

## Fixed in advance

- **Model:** `jlu/qwen3-coder-next` on the HRZ gateway (free; `priced_model_lint`
  holds the harness to it). One model: the finding will be about *this* model.
- **The twelve first-seat reports** are the `map` stage's review of one file each
  of jichi's own `src/util/*.c`, produced by the same model **once**, recorded to
  `tests/bench/refute_ab/reports/`, and then edited by hand to plant exactly one
  false claim per report (a wrong line number, an inverted condition, a function
  said to exist that does not). The planted claims are listed in
  `tests/bench/refute_ab/planted.tsv` *before* the runs.
- **A hit** = the stage's output mentions the planted claim's subject (its
  function name or line) under Rebutting or Undercutting. Read by a person, not a
  grep -- and the person reads the twelve control outputs first, blind to the
  condition, to keep the reading honest.
- **Runs:** 1 per report per condition (24 runs). n is small; the result is a
  direction, never a magnitude, and every number gets its n beside it.
- **Also recorded:** false positives -- claims the refuter attacks that are true
  -- per condition. A stage that names every planted claim by attacking
  everything has found nothing.

## How this could mislead

- **Two seats, one base model** share blind spots (M602's caveat, already on
  record). A null result here does not say a *different* second seat would fail.
- **The planted claims are mine**, and I wrote the frame. The list is fixed before
  the runs so the framing cannot move afterwards; the blind reading of the
  control first is the check on my own scoring.
- **The reports are about jichi**, which the model has been reading for months in
  this project's own dogfooding -- familiarity may inflate both conditions.

## What will not be concluded

- Not "the second seat works" in general -- one model, twelve planted errors of
  three kinds, one codebase.
- Not a default: `refute` is a stage an author adds to a spec; nothing here would
  make it automatic.
- Not anything about *unplanted* errors: this measures recall on known errors,
  which is the only thing a small pre-registered run can measure.

---

## Cross-model run — pre-registered 2026-09-17 (M640); run the same day (M641)

> **Run.** Results and reading: [`analysis/2026-09-17-refute-cross-model.md`](../analysis/2026-09-17-refute-cross-model.md) — every refuter cleared both bars; the false attacks cluster on the one real defect in the corpus. What below says "not yet run" was true when written and is kept.

> Written before any run with a second refuter. What is below is fixed; the
> results page will say what moved.

### The question

The A/B above measured one model reading its own prose: `jlu/qwen3-coder-next`
wrote the twelve reports and `jlu/qwen3-coder-next` refuted them. Two seats, one
base model, shared blind spots — the caveat M602 raised and this page carried. So
12 of 12 is a fact about that model. Two questions follow, and one measurement
the first run left undone answers both:

1. **Does the frame travel?** Does "do not agree with it" send a *different* model
   to the source, or only the one measured?
2. **Does the frame find, or attack?** A refuter that names every plant by
   attacking everything has found nothing. The first run did not count its false
   attacks; the column reads "not assessed".

### Hypothesis (falsifiable)

For each refuter below, on the **same twelve reports** with the **same twelve
plants**, the `refute` stage names the planted claim in **at least 6 of 12**
(*the frame travels*), and its **false attacks are fewer than its hits** (*it
found rather than attacked*). A refuter that clears the first bar and not the
second names plants by attacking everything; one that clears neither is a model
the frame does not move.

The same count is made on the **existing twelve `qwen3-coder-next` refute
answers** (run `ab-1`), so the baseline gets the number its table lacks.

### Fixed in advance

- **Refuters:** `jlu/qwen3.8-27b` (same family as the baseline, a later
  generation, a thinking model, 977k-token window), `jlu/gemma-4-26b-it` (a
  different family), `jlu/gpt-oss-20b` (a third family; no published context).
  Optional, as a size floor: `qwen/qwen3.5-9b` on LM Studio over loopback. All
  free: the harness's `--model` refuses anything that is not `jlu/*` through the
  gateway or served from loopback, before a request is built.
- **Arm:** `refute` only (`--arms refute`). The control was measured once, at 1 of
  12, on the baseline model; it is not re-run, and **nothing here compares frame
  to words on the new models** — only the frame's recall and precision per model.
- **Reports and plants:** unchanged from `ab-1`; `planted.tsv` is not edited.
- **A hit:** as above — the answer names the planted claim's subject as false or
  unsupported under Rebutting or Undercutting. Read by a person.
- **An attack:** any claim in the report, other than the plant, that the answer
  calls false, wrong or unsupported. **A false attack:** an attack on a claim that
  is true of the source, decided by reading the source file at the cited place,
  never by reading the answer's tone. Recorded per answer as `attacks` and
  `attacks_true` in the form; the rate is their ratio, printed with both numbers.
- **Runs:** 1 per report per refuter, 12 per model, opaque ids, `--deadline 30m`
  as headroom (not a cap). Thinking models keep their reasoning tokens; the
  output cap is not lowered for them.
- **Order of grading:** the baseline's twelve answers first (they exist), then
  each new model's pack as it lands; the form is filled before `score` is run.

### Amendment before the runs completed (same day, M641)

The first cross-model run was started in this checkout and stopped after three
answers: the second answer cited `tests/bench/refute_ab/results/ab-1/grading/…/A.md`
— a prior refuter's graded answer — and the report file itself, both of which sit
in the tree the refuter reads, beside `planted.tsv`, which names every plant. The
refuter had the answer key on disk. Those three answers were discarded, the
harness gained `--workspace`, and every cross-model run below is made against a
**clean export** (`git archive` of `fce98655`, the source the reports were written
against, which contains no `tests/bench/refute_ab/`). The sealed record carries the
workspace and a `contaminated` flag, and `run` warns when the workspace holds the
plant list. **This also touches the baseline:** `planted.tsv` was on disk, uncommitted,
when `ab-1` ran; none of its twelve answers or stderr files mentions it, the reports
directory or a grading file, so there is no evidence the baseline read the key —
and no proof it did not, since the run did not log tool calls. Recorded as a limit
of the 12 of 12.

### How this could mislead

- **The grader planted the claims** and wrote the frame — the same caveat as
  above. A false attack is easier to judge than a blind arm: the source either
  has the property or it does not.
- **Verbosity moves the count.** A model that attacks more will have more false
  attacks by volume alone; that is why the denominator is recorded and the rate
  reported beside the count, and why the bar is "fewer false attacks than hits",
  not a percentage.
- **The reports are one model's prose.** A refuter of another family may find the
  baseline's habits easy or hard to read for reasons that are not about the frame.
- **The gateway's context claims** (977k for the 27B) are its metadata, not a
  measurement; the files here are under 510 lines and fit every window.

### What will not be concluded

- Nothing about a **different first seat**: the reports stay the baseline's.
- Nothing about a control on the new models: the frame-versus-words question was
  asked once and answered for one model.
- Not a default. `refute` stays a stage an author adds.
