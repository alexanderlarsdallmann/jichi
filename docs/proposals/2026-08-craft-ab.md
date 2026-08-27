# Does the craft section earn its ~419 tokens? (M318)

*Design note written before running anything, per the M299 craft rule — which is also the
thing under test, so the note is written by the subject of the experiment. Stated here so a
reader can weigh it.*

---

## The question, and why it is hard

M312's breakdown made the craft section (M299, config `craft`, default on) measurable: on a
graded attempt with rules skipped it is **~329 of ~626 system-prompt tokens — the largest
part**, and on this repository interactively ~419. It is paid on **every model call**.

`DEFERRED.md` recorded the question as an A/B: does turning it off change outcomes?

**The hard part is that my graders cannot see what it claims to improve.** The section asks
for: understand before changing, ask when genuinely ambiguous, *write the design and the
rejected alternatives*, prove a test can fail, say plainly what is unverified. Every graded
curriculum task **states its deliverable explicitly** — even `10-design-before-code`, whose
spec lists the five section headings its grader then greps for. So a task like that measures
instruction-following, not whether a habit was instilled.

Two consequences I commit to in advance:

1. **A null result on pass rate does not vindicate or condemn the section.** It says the
   instrument cannot see it. I will report it that way, not as "craft does nothing".
2. **Therefore the measurement needs a probe where the deliverable is *not* stated**, because
   that is the only situation in which the section can do work no spec already does.

## What will be measured

**A — Cost. Exact, deterministic, no model needed.** The per-call and per-run token cost of
the section, from `context` and from telemetry `sys_tok`. This is the denominator of "earns
its place" and is not in doubt.

**B — Outcome on graded tasks. Weak but real.** `00-hello` (1 pt), `06-make-the-test-pass`
(3 pt), `10-design-before-code` (3 pt), three runs each per condition, everything else held
fixed at the cheap settings (M310's `--tool-profile core`, M312's `repoMap: false`) so the
**only** variable is `craft`. Metrics: PASS/FAIL, input tokens, model calls.

A specific outcome worth watching for: **craft may make a small model *worse*.** 419 extra
tokens of process instruction on a 31B model competing with the actual task is a real risk,
and if it shows up it is the most useful thing this milestone could find.

**C — An under-specified probe. The fair test of the benefit side.** One prompt that names a
goal and *no* deliverable, run in both conditions:

> "The note-taking script in this folder appends lines but never rotates the file. Deal with
> it."

Nothing there asks for a design note, alternatives, or a test. If the section works, the
difference should be visible without a grader asking for it. Recorded mechanically:

- did any explanatory/design file get written, unprompted?
- does the final answer name an alternative it rejected?
- did it edit before reading?

All three are crude proxies and will be labelled as such. The probe is **not** graded
pass/fail, because there is no correct answer to a prompt like that — which is the point.

## How this could mislead, and what I will do about it

- **n is tiny and the model is nondeterministic.** M310 measured `full` taking 6, 7 and 6
  calls on the same task. Three runs can show a direction, never a magnitude. Every number
  gets its n printed beside it.
- **One model, one size.** `jlu/gemma-4-31b-it`, 32k. A frontier model may absorb the extra
  instructions for free and a 7B may drown in them. The finding is about *this* model, said
  in the title of the results table.
- **I wrote the section being tested.** I will report a negative result if I get one, and the
  pre-registration above is what makes that checkable: the metrics and the probe are fixed
  *before* the runs, so I cannot pick the framing afterwards.
- **`craft: false` is not "no guidance".** The base persona still says to prefer reading
  before editing and to make minimal targeted edits. The A/B measures the *M299 addendum*,
  not the difference between guidance and none.

## What will not be concluded

- **Not** "delete the section" or "keep it forever" — the outcome is a recommendation about a
  **default** for a **model class**, plus a documented number so an operator on a small
  window can decide for themselves.
- **Not** anything about frontier models, which are not being tested.
- **Not** anything about the honesty half of the section ("say plainly what is unverified").
  No mechanical probe I have can measure a model's candour, and I would rather say so than
  invent a metric for it.
