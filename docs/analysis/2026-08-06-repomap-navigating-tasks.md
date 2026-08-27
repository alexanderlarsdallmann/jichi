# Does the repo map earn its place on repository-reading tasks? (M326)

**Date:** 2026-08-06 · **Model:** `jlu/gemma-4-31b-it` (HRZ, 32k) · **Design:** three tasks ×
`repoMap` on/off × 3 runs, everything else held fixed (`--tool-profile core`,
`--budget-tokens 400k`) · *Written before the runs finished, so the reading is pre-registered.*

---

## The claim under test, and why it was suspect

`DEFERRED.md` kept "make `repoMap: false` the default for `attempt`" open with this reason:

> Unlike the rules file, the map is *sometimes* the thing being paid for: dropping it cost
> `06-make-the-test-pass` three extra model calls, and **tasks 20–22 are explicitly about reading
> this repository**.

Two claims. The first is measured (M312). **The second is an assumption**, and it is the same
shape as the one M319/M320 dissolved: *"core costs the hint ladder — the machinery `attempt`
exists to exercise"* also sounded obviously true, and 24 runs found the ladder was never touched.

## Finding 0 — the premise is false, and reading the tasks was enough

Checked before any model ran:

- **`20-the-wrong-lifetime`** operates on `arena.c` / `arena.h` / `notekeeper.c` — its own
  fixture directory.
- **`21-the-invisible-growth`**: `buf.c` / `buf.h` / `spooler.c`. Its own fixture.
- **`22-slope-lies-keep-the-peak`**: `track.c` / `track.h` / `digest.h` / `candidates`. Its own
  fixture.

None of the three mentions `src/`, `include/`, or any `jc_*.c` file. Task 20 *cites* a jichi
analysis document as background prose — that is where "about reading this repository" came from —
but the work is entirely inside a self-contained fixture. **The tasks are about the arena bug
class this project has lived through; they are not about navigating this codebase.**

So the stated blocker does not exist. A repo map of jichi's 85,000 lines is as irrelevant to
these three tasks as it was to `00-hello`.

That is worth separating from what follows: **Finding 0 settles the register's reason. The runs
below ask a different, still-open question** — whether the map helps or hurts on the hardest
tasks in the curriculum, where its ~3,100 tokens per call compete with a model that is already
struggling.

## Pre-registered readings

- **If pass rates tie and the no-map arm is cheaper** — the map earns nothing here, and with
  Finding 0 the objection is gone. That would license defaulting `repoMap: false` for `attempt`,
  though the *decision* remains the operator's.
- **If the no-map arm takes more model calls** (M312's mechanism on `06`), that is the honest
  cost of the change and gets reported as such even if it still passes.
- **If both arms mostly FAIL at the budget** — these are 3–4 point tasks and the same model
  burned 250k and failed on `14-the-hollow-gate` under a second model. Then the A/B **cannot
  answer** the question, and I will say that rather than read a difference out of noise. n=3.
- **If the map arm is clearly better**, the default stays and the register gets a *measured*
  reason in place of a false one — which is a better outcome than the change.

## Results

18 runs. `sys_tok` per call: **3,849 with the map, 725 without** — the map is ~3,100 tokens on
every call, confirming M312's figure on a different set of tasks.

| Task | arm | verdicts | model calls | input tokens |
|---|---|---|---|---|
| `20-the-wrong-lifetime` (3 pt) | map | F/F/P **(1/3)** | 6 / 6 / 10 | 51k / 51k / 99k |
| | no map | P/P/F **(2/3)** | 10 / 15 / 6 | 43k / 59k / 21k |
| `21-the-invisible-growth` (3 pt) | map | P/P/F **(2/3)** | 12 / 10 / 9 | 111k / 92k / 79k |
| | no map | P/F/F **(1/3)** | 9 / 7 / 9 | 37k / 24k / 35k |
| `22-slope-lies-keep-the-peak` (4 pt) | map | F/P/F **(1/3)** | 26 / 21 / 31 | 408k / 250k / 413k |
| | no map | F/F/F **(0/3)** | 39 / 38 / 43 | 401k / 409k / 409k |
| **total** | **map 4/9** | | | |
| | **no map 3/9** | | | |

Medians:

| Task | input, map → no map | model calls, map → no map | budget-bound runs |
|---|---|---|---|
| 20 | 51k → 43k (**−15%**) | 6 → 10 (**+67%**) | 0 / 0 |
| 21 | 92k → 35k (**−62%**) | 10 → 9 (−10%) | 0 / 0 |
| 22 | 408k → 409k (±0%) | 26 → 39 (**+50%**) | 2 of 3 / 3 of 3 |

### Reading it, against the pre-registration

**Pass rate: no difference detectable.** 4/9 against 3/9, with every cell between 0 and 2 of 3.
At this n that is noise, and the pre-registered response applies: *do not read a difference out of
it.*

**Cost: the map is expensive and mostly unearned.** On the two tasks the model can actually
finish, dropping it saves 15% and **62%** of the input tokens. That is the M312 result reproduced
on harder, different tasks.

**But the mechanism M312 found is here too, and it is the important half.** Without the map the
model needs **more model calls** — median +67% on task 20 and **+50% on task 22** — because it
navigates with `list_files`/`search_code` instead of reading a map. On task 22 that exactly
cancels the saving: **408k with the map, 409k without.** Cheaper calls, more of them, same bill.

**Task 22 cannot answer anything, and 5 of its 6 runs say why:** they exhausted the 400k budget.
That task is beyond this model in both arms. Its 1/3-vs-0/3 split is not evidence.

**Except that it is the second time the leaner arm went 0/3 on the hardest task.** M320 found
exactly this shape with `--tool-profile core` (1/3 with the fuller prompt, 0/3 with the leaner
one, both budget-bound, more calls in the lean arm). Two independent measurements, two different
levers, the same direction: **on a task at the edge of a model's ability, a leaner prompt buys
more flailing rather than more progress.** Neither result alone is significant; the pair is worth
believing more than either.

## Conclusion

**The register's reason was false and is now replaced by a measured one.**

- **Finding 0 settles the stated blocker:** these tasks are self-contained fixtures, not
  repository navigation. That claim should never have survived a reading of the tasks.
- **The default stays `repoMap: true` for `attempt`** — not because the map helps here (it does
  not, on pass rate), but because a *default* has to hold for the learner who is struggling, and
  the only signal about struggling learners points the other way. Same reasoning as M320, and
  now with a second measurement behind it.
- **The recommendation is strengthened, not weakened:** for a small self-contained task on a
  budget, `repoMap: false` is a 15–62% saving with no measured cost to the outcome. That is most
  of the curriculum.
- **What would change the default:** a measurement on tasks the model comfortably passes, showing
  no call-count penalty. The +67% and +50% call inflation is the thing to disprove, and it is now
  the recorded reason rather than a guess about what the tasks are about.

