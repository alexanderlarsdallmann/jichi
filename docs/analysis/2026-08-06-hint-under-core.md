# Does `core` cost the hint ladder? Measured: the model never asks (M319)

**Date:** 2026-08-06 · **Model:** `jlu/gemma-4-31b-it` (HRZ, 32k) · **Pre-registration:**
[`proposals/2026-08-hint-under-core.md`](../proposals/2026-08-hint-under-core.md) ·
**Verdict: H2. The capability `core` "costs" is one this model never uses.**

---

## 0 — A defect found while designing the measurement, fixed before running it

The **solving stance** (M173b) told the learner-model to use `hint`, `ask_for_help` and
`spawn_subagent`. Under `--tool-profile core` **none of the three is advertised**. So the
prompt instructed the model to call tools it did not have — the M285 class of defect (a
declared name that resolves to nothing), one layer up from a tool fence.

Fixed first, deliberately: measuring before the fix would conflate *"failed because the ladder
was missing"* with *"burned calls hunting for a tool the prompt promised"*, and those want
different fixes. The stance now names only the tools actually advertised, resolved through the
same `jc_config_tool_profile_core` call the loop and every report use, and under `core` it says
so outright (`no hint or help tools are available in this run`) rather than leaving a heading
with nothing under it.

**My first evidence for this defect was wrong**, and that is worth recording. I ran
`sysmsg --tool-profile core | grep -c hint`, got 3, and called it confirmed. All three matches
were unrelated — two lines of this repository's `CLAUDE.md` and the phrase "research hints" in
the *authoring* stance. The solving stance is emitted only while an assignment is **active**,
which the `sysmsg` subcommand never is, so that grep could not have seen it either way. The
defect was real; my proof of it was not. Verified properly by a unit test over
`jc_sysmsg_build` with an assignment set — and the revert turns **six** checks red.

## 1 — The measurement: 12 runs, 12 passes, zero hint calls

Two tasks chosen because their ladders are load-bearing: `05-the-ambiguous-edit` (2 pt, whose
third hint contains the answer) and `14-the-hollow-gate` (4 pt, hard). `assignments: true` in
both arms; only the profile differs.

| Task | profile | input tokens (3 runs) | hint calls | result |
|---|---|---|---|---|
| `05-the-ambiguous-edit` | **full** | 39k / 45k / 39k | 0 / 0 / 0 | 3× PASS |
| | **core** | 10k / 10k / 10k | — (fenced) | 3× PASS |
| `14-the-hollow-gate` | **full** | 117k / 174k / 88k | 0 / 0 / 0 | 3× PASS |
| | **core** | 38k / 38k / 63k | — (fenced) | 3× PASS |

The `full` arm genuinely advertised the support tools (`tools_tok` 4664 vs core's 970), so the
arm is valid: `hint` was there and was never called — not once, on either task, including the
4-pointer.

**Instrument validated before publishing the null.** A mock model was scripted to *choose*
`hint`, and both signals reported it: the attempt line said `1 hint used` and telemetry logged
`"name":"hint"` once. The zeros above are the model's behaviour, not a broken counter. (Two
independent zeros agreeing is not proof: they could share a cause. Forcing a one is.)

## 2 — And `core` is much cheaper, on the harder task too

- `05`: **39–45k → 10k** (−75%), and *identical* across all three core runs.
- `14`: **88–174k → 38–63k** (−60%), with the spread collapsing too.

The same pattern M310 found: fewer tools, less wandering, and far more repeatable — which
matters more than the mean for a graded run at a fixed `--budget-tokens`.

## 3 — What this does and does not license

**Measured away:** the objection that blocked two other deferred items. M310 declined to make
`core` the default for `attempt` because it "costs the hint ladder — the machinery `attempt`
exists to exercise". On this model, `attempt` **does not exercise it**.

**Not changed here, as pre-registered.** The default stays `full`, because the question is
about a *learner's* experience and not only about pass rate, and because:

- **The ladder's primary path is a human.** `/hint` in the TUI is driven by the person working
  the task, and `tests/smoke/learner_flow.sh` shows that path working. This measured the
  *model* reaching for it, which is a different and lesser claim.
- **One model, two tasks.** A model that follows instructions more literally might well climb
  the ladder when told the support tools exist.
- **A default that a beginner cannot see is the wrong place to save tokens** if it changes what
  the curriculum hands them.

**What would license the change:** the same null on a second model, plus a decision that
`attempt`'s job is to measure the *artifact* rather than to rehearse the hint ladder. Both are
statements someone should make deliberately, not consequences of a token measurement.

**What this says about the feature**, which is the more interesting half: a graded-hint ladder
that no model reaches for is a *human-driven* mechanism. That is a legitimate design — it is
how a tutor uses it — but it should be **stated** rather than assumed to be exercised by
`attempt`. `docs/ASSIGNMENTS.md` now says so.
