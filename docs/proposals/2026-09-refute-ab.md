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
