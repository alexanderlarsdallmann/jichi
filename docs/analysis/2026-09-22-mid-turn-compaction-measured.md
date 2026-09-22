# Mid-turn compaction, measured: 5 passes in 313 reached target

*2026-09-22. A measurement, not a change — nothing in `src/` or `tests/` moves on
this page. It was run to test one line of
[`proposals/2026-09-sustained-task.md`](../proposals/2026-09-sustained-task.md)'s
own discard criteria, and it answers a different and older question on the way.*

## What was measured, and why this corpus

`DEFERRED.md` carries an open row: **"Decide what jichi should DO when mid-turn
compaction cannot reach its target."** Its reason for staying open is that the
decision needs a corpus rather than an opinion.

There is one. This machine's telemetry holds **1,421 `compact` events**; **332**
of them are mid-turn passes emitted after M323, which is the milestone that added
`before`, `after` and `target` to the event *"in calibrated real-token terms —
the same units the trigger compares, so a reader can check the decision"*
(`src/chat/jc_agent.c`). That comment is what makes this measurable at all.

**313** of those 332 carry a positive target and are the population below. They
come from **three workloads** (`chrtext`, `chrtext-full`, `zigodot`) across **17
distinct sessions**; six sessions contribute ten events or more and the pattern
below repeats in every one of them, so this is not one pathological run.

## The result

| outcome of a mid-turn compaction pass | events | share |
|---|---:|---:|
| **fired and changed nothing** (`before == after`) | 264 | **84%** |
| reduced the context but **still over target** | 44 | 14% |
| **reached target** | **5** | **1.6%** |

Of the 264 that changed nothing, **63** elided exactly one duplicate and **50**
were already *under* target when they fired.

When a pass did reduce and still missed, it was still over by a median of
**16,380** tokens after cutting a median of **1,288**.

**The one invariant that held everywhere: `after <= before`, in 332 of 332.**
Compaction never grew the context. That is worth stating precisely because it is
the only thing in this data that can be asserted rather than recorded.

## The source predicted this, in writing

`jc_agent.c`, at the emit site, on why the event fires even when nothing is
elided:

> *"a workload whose history is many small tool results gives this pass nothing
> to elide, so it fires every round and the request still goes out over the
> configured limit."*

That is exactly what 264 + 44 = **308 of 313 passes (98%)** did. The comment
called the shape; this page supplies the number.

## What this does to the Sustained design, which is why it was run

`proposals/2026-09-sustained-task.md` §3b proposed that the compaction rung (S2)
**assert** arithmetic: `before > target`, `after <= target`, `elided > 0`. The
page argued this was its best feature — *"the assertion does not have to be 'it
did not crash'."*

**Those assertions fail on 98% of real passes.** As a pass/fail gate S2 would be
red on every row, and a gate that is red everywhere is turned off. The design is
corrected on its own page: **S2 records the four numbers and classifies the
outcome; the only thing it asserts is `after <= before`, plus that the event
fired at all.**

This is the discard criterion working as intended, and it did not discard the
rung — it discarded the *assertion*. A rung that reports "on this platform,
N passes, k reached target" is still worth having; one that fails everywhere is
not.

## What this does NOT say

- **It is one machine and one operator**, three workloads. The *shape* is
  consistent across 17 sessions; the *proportions* are this corpus's.
- It measures the **mid-turn elision pass only**. Between-turn summarisation is
  a different mechanism with 51 events here and is not analysed.
- It says nothing about whether the current behaviour is **wrong**. A pass that
  elides nothing and lets the request go out over the limit may be the right
  thing when there is genuinely nothing to elide. What the register row asks is
  what jichi should *do* about it, and this page supplies the frequency, not the
  answer.
- The corpus is entirely **pre-existing**. No run was made to produce it, so
  nothing here is tuned to make a point.

## One thing left unexplained

**19 events carry `target == 0`**, with `before == after == 77,904` in the
sample. A target of zero is not a target, and both assertions are vacuous
against it — `before > 0` is trivially true and `after <= 0` is impossible. They
are excluded from the 313 above rather than silently folded in. Where a zero
target comes from is not established here, and guessing would be the error this
page exists to avoid.

## A correction made while measuring

The first pass at this reported *"no event carries `before`/`after`/`target`"*.
That was wrong, and wrong in an instructive way: it came from reading a
`most_common(14)` key listing as if it were the complete key set. `before`
appears in 332 events and ranks below the cut. The aggregate was believed for
about a minute; the raw rows corrected it. **The decomposition above exists
because the second look was at individual events rather than at a summary** —
the same reason the 84%/14%/1.6% split replaced an earlier, blunter "82% pressed"
that had conflated no-ops with genuine misses.
