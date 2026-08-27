# Does `core` actually cost the hint ladder? (M319)

*Design note written before running anything, per the M299 craft rule.*

---

## The deferred question

M310 refused to make `--tool-profile core` the default for `attempt` because core drops the
assignment tools `hint` and `ask_for_help` — "the machinery `attempt` exists to exercise".
[`DEFERRED.md`](../DEFERRED.md) then recorded the gap in that reasoning:

> **Measuring a `core` attempt that actually needs a hint.** Both measured tasks passed with 0
> hints, so the capability loss has no measured behavioural consequence yet.

Two other deferred items — making `core` and `repoMap: false` the defaults for `attempt` —
are waiting on this one, which is why it is next.

## A defect found while designing it, and why it must be fixed first

`jc_sysmsg_build`'s **solving stance** (M173b) tells the learner-model:

> - `hint`: reveal the next graded hint … - `ask_for_help`: ask a focused clarifying question
> … - `spawn_subagent`: delegate a well-scoped sub-part …

Under `--tool-profile core` **none of those three is advertised**. Confirmed: `sysmsg
--tool-profile core` still mentions `hint` three times. So the prompt instructs the model to
use tools it does not have — the M285 class of defect (a declared name that resolves to
nothing), one layer up.

**This has to be fixed before the measurement, not after.** Measuring first would conflate two
different causes: a run that fails under core could be failing because the hint ladder is
missing, or because the prompt sent it looking for a tool that isn't there and it burned calls
discovering that. Those want different fixes, and an A/B cannot separate them.

So: the stance renders only the bullets whose tools are actually advertised, resolved through
the same `jc_config_tool_profile_core` call the loop and every report use. When none of the
three is available the support paragraph is omitted entirely rather than left as a heading with
nothing under it.

## The measurement

**Tasks chosen because their hints are load-bearing.** `05-the-ambiguous-edit` (2 pt) — its
third hint literally contains the answer, so a model that climbs the ladder gets the solution —
and `14-the-hollow-gate` (4 pt), which is hard.

**Conditions:** `full` (hint available) vs `core` (hint fenced), `assignments: true` in both,
everything else fixed. Three runs per cell.

**Metrics:** PASS/FAIL, hints used (the attempt line reports it), whether `hint` appears as a
`tool_call` in telemetry at all, and tokens.

### Pre-registered readings

- **H1 — the model uses hints and they change outcomes.** The capability cost is real; M310's
  and M312's refusal to default `core` stands, with evidence instead of an assumption.
- **H2 — the model never calls `hint`, even when stuck.** Then core's "capability cost" is
  theoretical on this model. That would be a finding about the *hint ladder feature*, not just
  about the profile: a graded-learning mechanism no model reaches for is a mechanism that only
  works when a human drives it (`/hint` in the TUI), which is a legitimate design — but it
  should be *stated* rather than assumed to be exercised by `attempt`.
- **H3 — core passes anyway.** Possible, and the least informative: it would mean the tasks are
  not hard enough to need the ladder, and the honest report is "still unmeasured, and here is
  what I tried".

I commit to H2 and H3 being reported as findings rather than as failed attempts.

## What will not be concluded

- **Not** a change to `attempt`'s default in this milestone, whichever way it goes: the
  defaults question involves the learner's experience, not only pass rate, and one model's
  behaviour on two tasks is thin ground for changing what a curriculum hands a beginner.
- **Nothing about `/hint` driven by a human**, which is the ladder's other and arguably primary
  path. This measures the *model* reaching for it.
