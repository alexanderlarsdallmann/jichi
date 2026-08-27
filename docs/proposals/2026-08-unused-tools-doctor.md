# A `doctor` check for tools you pay for and never call (M316)

*Design note written before implementation, per the M299 craft rule. Short feature, one
real judgement: what evidence entitles a tool to give advice.*

---

## Why this was deferred, and what changed

[`DEFERRED.md`](../DEFERRED.md) recorded it with its own objection:

> Advice needs a threshold ("unused across N sessions"), and a threshold needs more than
> one session's evidence — which is precisely what M314's own footer says it does not have.

That objection stands against the *shape* M314 has, not against the check. M314 reports one
log's worth of calls and explicitly refuses to advise. `doctor`'s job **is** advice. So the
check is possible exactly when it can clear a bar M314 does not try to clear.

Two things make it clearable:

1. **Count distinct sessions, not turns.** The summary already carries a per-session vector
   (M82). "Never called in any of your last seven sessions" is a different class of evidence
   from "never called in this one turn" — and it is the axis that actually matters, because
   a single session reflects one task.
2. **Advise the lever, not the surgery.** The check does not say *remove `spawn_parallel`*.
   It says: you are advertising N tools that N sessions never used, costing ~X tokens on
   every model call, and `toolProfile: core` is the knob. `core`'s contents are a fixed,
   considered set, and `doctor` **already** warns about what `core` withholds — so pointing
   at it is safe advice, where per-tool surgery would not be.

## Decision

A `doctor` check, **WARN at most, never FAIL**, gated on an evidence threshold:

```
JC_TOOLUSE_MIN_SESSIONS 3      /* distinct sessions in the log       */
JC_TOOLUSE_MIN_CALLS    20     /* recorded tool calls                */
```

- **Below the threshold** (or no log at all): an **OK** line stating the check exists and
  why it is quiet — "not enough telemetry to judge tool use (N sessions, M calls)". Silence
  would be indistinguishable from a pass, which is the M314 stated-absence principle applied
  to a checklist.
- **Above it, with unused tools**: a **WARN** naming the count, their combined cost per call,
  and `toolProfile: core` as the lever.
- **Above it, with none unused**: an **OK** line — a real pass, distinguishable from the
  quiet one.

Never a FAIL: nothing is broken. This follows M285's fence lint (WARN, a degraded profile)
rather than M284's selector lint (FAIL, an unresolvable name).

### Why those numbers

They are defensible rather than derived, and worth stating as such. Three sessions is the
smallest number for which "every one of them" is a plural claim about *different tasks*;
twenty tool calls is roughly the point at which a run has done enough work that a
never-chosen tool is informative. Both are round, both are conservative in the direction
that matters — a check that stays quiet too long is a nuisance, one that advises too early
is a liar. A user who wants the raw numbers under the threshold has `context tools`.

## One definition of "unused"

M314's report computes the unused count and cost inside its render loop. Doctor needs the
same two numbers. Computing them twice is precisely the drift M311, M312 and M313 each had
to undo — so this milestone **factors the join into `jc_context_tool_use()`** and has the
report's footer use it too. The rows stay in the render loop (they are display); the claim
comes from one place.

## Alternatives rejected

- **Reuse M314's report as the check** (run it, print it under `doctor`). Rejected: a
  checklist item must be a verdict, and the report is deliberately not one.
- **Threshold on turns or tool calls only.** Rejected: one long session is still one task.
  Sessions are the axis that makes the claim plural.
- **Name the unused tools in the warning.** Rejected: with 9 of 16 unused the line becomes a
  list nobody acts on (M285's lesson about bounded samples), and naming individual tools
  invites the per-tool surgery this check deliberately does not recommend. `context tools`
  names them, and the warning points there.
- **FAIL under `--unattended`.** Rejected: `doctor --unattended` escalates *posture* WARNs
  so a loop supervisor can gate on them. Token efficiency is not a posture problem, and
  failing a supervised run over it would be absurd.
- **A config knob for the thresholds.** Rejected as premature: nobody has asked, and a knob
  on a heuristic invites tuning it until the warning goes away.

## Not in scope

- **Cross-workspace evidence.** The join stays workspace-filtered (M314's reasoning: "never
  called *here*" is the per-project claim).
- **Per-tool advice of any kind.** Stated above; the report is where names live.
- **Anything about tools the model *could not* call.** Still indistinguishable here from
  unwanted ones — M314 says so, and this check inherits the caveat rather than pretending to
  have solved it.
