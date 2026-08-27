# The hint null on a second model — and the case against flipping `attempt`'s default (M320)

**Date:** 2026-08-06 · **Model:** `jlu/qwen3-coder-next` (HRZ, 32k), promoted to the `chat`
role · **Design:** identical to [M319](2026-08-06-hint-under-core.md) — same two tasks, same
two profiles, three runs each · **Verdict: the hint null is confirmed *harder*, and the case
for flipping the default got weaker, not stronger.**

---

## 0 — Subject validated first

`doctor --live` on the second model: **native tool calling confirmed** (probe prefix 316 real
prompt tokens). A model that could not tool-call would have made the A/B meaningless, so this
was checked before any run rather than inferred from a passing task.

## 1 — Results

| Task | profile | input tokens | model calls | tool calls | **hint calls** | result |
|---|---|---|---|---|---|---|
| `05-the-ambiguous-edit` | full | 37k / 45k / 37k | 5 / 6 / 5 | 4 / 5 / 4 | **0 / 0 / 0** | 3× PASS |
| | core | 18k / 15k / 18k | 6 / 5 / 6 | 5 / 4 / 5 | — (fenced) | 3× PASS |
| `14-the-hollow-gate` (4 pt) | full | 252k / 250k / 253k | 25 / 23 / 25 | 24 / 26 / 24 | **0 / 0 / 0** | FAIL, FAIL, **PASS** |
| | core | 245k / 250k / 255k | 33 / 37 / 35 | 36 / 40 / 34 | — (fenced) | FAIL, FAIL, FAIL |

Every `14` run **exhausted the 250k token budget** ("bound reached"): these are budget stops,
not wrong answers.

## 2 — The hint null is now much stronger

On the first model (M319) every run passed, so *"never called `hint`"* had a boring
explanation: it never needed to.

**This model needed help, had the tool, and did not use it.** It burned a quarter of a million
tokens across 23–25 model calls on `14-the-hollow-gate`, failed, and never once called the
`hint` tool — which was advertised (the full arm's `tools_tok` proves it), which the system
prompt named explicitly, and whose ladder for that task exists precisely for being stuck.

That is the observation M319 wanted and could not get. **Two models, 24 runs, 0 hint calls,
including six runs that failed with the tool in hand.** The hint ladder is a human-driven
mechanism; `attempt` does not exercise it. Both the deferred item's requirement ("the same null
on a second model") and its interpretation are satisfied.

## 3 — But the case for flipping the default got *weaker*

The deferred item was: *flip `attempt` to `core` if the null holds on a second model and if the
decision is made deliberately.* The null holds. The rest does not.

**`core` did not rescue the hard task, and may have hurt it.** `full` passed 1 of 3;
`core` passed 0 of 3. With n=3 and every run budget-bound that comparison is underpowered and I
will not claim `core` is worse — but it is plainly **not** the free win it was on the easy
tasks, and the direction is against the change rather than for it.

**The mechanism is visible in the call counts.** On `14`, `core` took **33–37 model calls and
34–40 tool calls** against `full`'s 23–25 and 24–26 — *more* work for the same budget. Cheaper
calls bought more of them, and on a task this model cannot finish, more cheap flailing is not
better than less expensive flailing. The M310/M319 finding ("fewer tools, less wandering") holds
where the model is *capable*; on a task beyond it, the lean profile just wanders further.

**And the cheap-task saving is smaller here:** `05` fell 37–45k → 15–19k (−55%), against the
first model's −75%. The benefit is model-dependent; the risk is too.

## 4 — Recommendation

**Do not flip `attempt`'s default to `core`.** Keep it `full`, and keep `core` as the documented
recommendation for a learner on a tight budget (which is where it was measured to help).

The reasoning has changed shape, and that is worth being explicit about. M310 and M312 declined
the flip because of a **capability cost** — and that cost turned out to be **imaginary** on both
models tested. But the measurement that dissolved the old objection produced a better one:
**`core` is a win on tasks the model can do and a possible loss on tasks it cannot**, and a
curriculum default has to hold for the learner who is struggling, not the one who is fine.

This closes the item as **decided**, not deferred: no further measurement changes the answer,
because the answer now rests on what a default owes a struggling beginner. Per M317's rule, it
moves to [`DECISIONS.md`](../DECISIONS.md).

## 5 — What this does not say

- **Not that `core` is worse.** 1/3 vs 0/3 on budget-bound runs is noise-adjacent; the claim is
  only that it is not the free win the easy tasks suggested.
- **Nothing about the two models' relative quality.** `14` may simply be beyond this one; the
  first model passed it at 88–174k. That is a different question and not one this A/B asked.
- **Nothing about `/hint` driven by a human**, still the ladder's real path, still exercised by
  `learner_flow.sh`.
