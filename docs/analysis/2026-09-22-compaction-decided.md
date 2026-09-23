# The compaction row, decided: none of the three, and the premise was wrong

*2026-09-22, M710. A measurement and a decision. `DEFERRED.md` has carried
"decide what jichi should DO when mid-turn compaction cannot reach its target"
since **2026-08-06** — 47 days and 389 milestones — because the row
would not be settled by an opinion. The workload it was owed exists.
Reproduce with `python3 tests/measure/compaction_pressure.py`.*

---

## The answer first

**None of (a), (b) or (c). The behaviour does not change.** Not because the
options are equally bad, but because **the harm they exist to prevent has never
once occurred**:

> **0 context-overflow rejections in 49,600 events across 243 sessions** — and
> **0** in the 34,216-event corpus that opened this row, which said so itself:
> *"no HTTP 400 was ever returned … no turn died of it."* Every request that went
> out after a failed compaction was **served**. The closest any of them came to
> the declared window was **98.0%** of it; the median was **86.2%**.

The row's premise — stated in its own words as *"the request still goes out over
the configured `contextLimit`"* — is false on this corpus. `target` is **60%** of
the window and the trigger is **80%**; missing a 60% comfort mark is not an
overflow, and the 20 points between the trigger and the wall absorbed all 243
short-falls.

**What the data asks for instead is a different change**, and it is recorded as
its own row rather than smuggled in here: the pass **exhausts**, and jichi keeps
running it.

## The population, and the two filters M700 did not apply

[`2026-09-22-mid-turn-compaction-measured.md`](2026-09-22-mid-turn-compaction-measured.md)
(M700) reported 313 passes with an 84% / 14% / 1.6% split. That page's
population is wrong in a way its own source tree had already warned about.

| | passes | why |
|---|--:|---|
| M700's population (`target > 0`) | 313 | |
| — never **pressed** | −64 | `jc_compact.c:1296` sets `target` unconditionally: *"a property of the LIMIT, not of this pass"*. On the unpressed early return `local.before` and `local.after` are assigned **from one `effective` value**, so `before == after` is true *by construction* — all 64 landed in the "changed nothing" bucket as a tautology. |
| — pressing a limit proven **under-declared** | −1 | The M459 case: a 203,264-token request **served** against the 150,000 that pass was aiming at. |
| **the core** | **248** | |

`jc_agent.c`'s emit site had made this exact argument one field earlier:
*"`short` is meaningful ONLY when pressed: an unpressured pass never had a
target to fall short of … **19 of 19 such events in the measured workload were
false**."* M326x added `pressed` so a reader could not make this mistake; M700
filtered on `target` anyway. (M700 also says "17 distinct sessions" — that is the
count over 332; the 313 come from **15**.)

The corrected split, on the 248:

| outcome | passes | share |
|---|--:|--:|
| **elided nothing** | 200 | **80.6%** |
| reduced, still over target | 43 | 17.3% |
| **reached target** | 5 | 2.0% |

`unrelieved` (still above the 80% high-water, so it re-fires next round): **228,
91.9%**. `after <= before`: **248 of 248**.

## Is the window correctly declared? Yes — for 248 of 249

The row's blocking condition was *"a workload that presses a **correctly
declared** window is still owed"*, because M459's corpus turned out to be seven
compactions toward a target that was never needed.

The test needs no new run. `in_tok` is written **only** inside
`st == JC_OK && http_status < 400`, so its presence is the proof the server
served the request, and `in_tok + cache_read_in + cache_write_in` is exactly how
`jc_agent.c` computes `app->last_prompt_tokens`. A served request larger than the
declared limit proves the limit understates the model.

**One pressed pass of 249 is contaminated**, and for precisely the M459 reason.
The other 248 press a window nothing proves too small.

The test is **per pass, against the limit that pass was aiming at** — not a
blanket taint on the session — and that session is why it has to be. It carries
both a 150,000 and a 256,000 declaration: the operator raised the limit
mid-session, which is exactly the remedy M459's notice asks for. Its 203,264-token
served request refutes the first and not the second, so **one of its three
pressed passes is dropped and two are kept**. A session-level rule would have
thrown away two honest passes as the price of catching one bad one.

**This test is one-sided and the page says so**: it can prove a window too small,
never prove one exactly right. A session whose requests never passed the declared
limit gives no evidence either way — and that is a real limit, because compaction
exists to stop them passing it.

## Neither is the estimate wrong

Before blaming behaviour it is worth checking the reading. `before`/`after`/
`target` are `jc_compact_effective_est` — calibrated(history + non-history) — so
they are full-request figures in the same units the server reports.

**|served − `after`| median 251 tokens; 226 of 234 within 5%.** On requests
with a median size of **122,297** tokens (range 18,148–162,000) that is M77's
calibration working. jichi's belief that it sits at
86% of the window is accurate. The trigger fires on a true reading and the
short-fall is real; it is the *consequence* that was assumed.

## What the three options would have cost

| | cost on this corpus |
|---|---|
| **(a) drop old messages** | Median gap left to target **31,311 tokens**; in the worst turn, **43,054** — which is **2.1× everything its 120 `run_tests` results produced** (~20,280 tokens). So it cannot be satisfied by dropping tool output at all: it has to reach past the results into the conversation. Cheapest to build (`jc_history_drop_front` exists), and it drops the evidence the agent is reasoning from. |
| **(b) summarize mid-turn** | A model call added to **243 of 248** passes, **228** of which re-trigger immediately and would pay again. On the worst turn that is **128 extra model calls in a single turn** that already made 140. |
| **(c) refuse the call** | Would have failed **243 requests the server served**, at a median 86% of the window. New code, and it converts a degraded run into a failed one — for requests that were never in danger. |

## The exhaustion curve, which is the real finding

No aggregate that pools a turn's passes can show this. Ordinal within the turn:

| position | n | elided > 0 | tokens reclaimed | per pass |
|---|--:|--:|--:|--:|
| **1st pressed pass of the turn** | 12 | 66% | 233,585 | **19,465** |
| 2nd | 6 | 50% | 91,841 | 15,306 |
| 3rd–10th | 36 | 27% | 216,252 | 6,007 |
| **11th and later** | **194** | 13% | 31,363 | **161** |

**The first pass of a turn is 4% of the passes and gets 40% of the reclaim. The
11th-and-later passes are 78% of them and reclaim 161 tokens each** — 0.8% of
what the first one gets.

The mechanism is not subtle: within a turn, each result can be harvested once.
The first pressed pass takes everything above `ELIDE_MIN_BYTES` that is outside
the `MIDTURN_KEEP_RECENT` window of 6; every later pass sees only what the last
round appended, which is protected. **M361's exhaustion latch was built for
exactly this and fired on 34 of 248 (13.7%)** — it re-arms whenever the history
grows, and a looping turn grows it every round.

*Only 12 turns, so the three early bands are thin. The 194-pass tail is the
well-powered one, and it is the one the conclusion rests on.*

## The worst turn, named

Session `0bf75213`, **turn 22**: 408 events — 140 model calls, 139 tool calls,
**128 pressed compaction passes**, no `turn_end`; the session log simply stops.
Turns 19, 20 and 21 had ended `verify_failed`, `verify_failed`, `aborted`.

- **120 of the 139 tool calls are `run_tests`.**
- **All 120 results are 676 bytes. Every one. `ELIDE_MIN_BYTES` is 800**, so not
  one of them was ever eligible for elision — 81,120 bytes of context that
  compaction is structurally unable to touch.
- **120 of the 140 model calls produced exactly 13 output tokens** — a tool call
  and nothing else.
- Context climbed 123,109 → 147,118 against a 150,000 window, ~190 tokens a
  round, and **124 of the 128 passes elided nothing at all**.

This is a **livelock**, not an overflow. The agent ran the same failing test 120
times and the compaction passes are the thermometer, correctly reporting a fever.
All three options treat the thermometer.

*Generalising the 676-byte detail would be the error this page exists to avoid:
across every session that pressed, sub-800-byte results are **65% of the items
but only 10% of the volume**. "Nothing is eligible" is true of that turn, not of
the corpus.*

## And it settles the second compaction row too

`DEFERRED.md`'s other compaction row (M588) deferred a mid-turn mechanism with
the revisit condition *"a workload presses at a window that is **already
generously sized** — this corpus is not that."*

| declared window | pressed | unrelieved | sessions | fixed overhead | % of window |
|---:|--:|--:|--:|--:|--:|
| 65,536 | 107 | 103 | 4 | 16,259 | **24.8%** |
| 150,000 | 138 | 125 | 1 | 13,525 | 9.0% |
| 196,608 | 1 | **0** | 1 | 16,639 | 8.5% |
| 256,000 | 2 | **0** | 1 | 13,525 | 5.3% |

Two independent corroborations and one new fact:

1. **M588 measured the fixed overhead at 65,536 as 16,268 tokens, 25% of the
   window. This corpus, gathered separately and read a month later, says
   16,259 — 24.8%.** Nine tokens apart. That number is a property of the program.
2. **M588's 196,608 control reproduces**: 1 pressed pass, 0 unrelieved.
3. **A generous window did press** — 138 times at 150,000, where the fixed
   overhead is 9%, not 25%. So the revisit condition is met, and the answer is
   *yes, but not for M588's reason*: **at 65k it is mis-provisioning, at 150k it
   is a livelock.** Same symptom, different disease, and the remedy M588 was
   holding the door open for (mid-turn summarization) is wrong for both.

**Caveat, stated rather than buried: the 150,000 band is one session, and 128 of
its 138 pressed passes are one turn.** It is an existence proof that a generous
window can be pressed. It is not a rate.

## The other corpus says the same thing, and said it first

The obvious objection is that this is one machine. It is — but the row was opened
*by* a second corpus, and that one was checked rather than assumed.
[`2026-08-06-large-workload-telemetry.md`](2026-08-06-large-workload-telemetry.md),
a private third-party workload of **34,216 events** on different hardware:

- the mid-turn pass ran **1,038 times**;
- **426 calls (3.1%) exceeded the configured `contextLimit` of 128,000**;
- **148 calls (1.1%) exceeded the model's declared `contextLength` of 150,000 —
  the largest by 1.36×**;
- and, in that page's own words: ***"No HTTP 400 was ever returned, so the
  declared window was conservative and no turn died of it."***

**Across both corpora — roughly 84,000 events, two machines, four workloads —
the number of requests that failed for context is zero.** The page that opened
this row had already measured the thing that decides it, one sentence after the
figure everyone quotes.

Two differences are worth keeping rather than smoothing over:

1. **That corpus did exceed the operator's stated budget; this one did not** (0
   of 234 against the pass's own `limit`). Exceeding `contextLimit` is a
   budget-honesty question, and M323 answered it with observability — the event,
   the warning, the summary line. It is not the question this row asks, which is
   what to do to avoid a request *failing*.
2. **148 calls served at up to 1.36× a declared 150,000 window is, by the M459
   rule this row already adopted, proof that that window was under-declared
   too.** The server counted 204,000 input tokens for a request it answered. So
   the one corpus with over-budget calls is also a corpus whose limit understated
   the model — which is the exact population this row asks to exclude.

## A bonus the same grep paid for

M700 closed with *"one thing left unexplained"* — 19 events carrying
`target == 0`, and *"guessing would be the error this page exists to avoid"*.
Not guessing was right; **not looking** was the miss. All nineteen carry no
`pressed` field, `limit > 0`, and `short: true`, and they stop the day the
positive ones start. That is the M326x defect exactly, and `jc_agent.c`'s comment
for the fix names the same count: ***"19 of 19 such events in the measured
workload were false."***

The register has a rule for this — *check the checkable part of a reason BEFORE
parking the item* (M326b), the first heading in `DEFERRED.md`. It turns out to
apply to an analysis page's open question just as much as to a deferred row.

## What is NOT established

- **That no workload can overflow.** This corpus never did. A history growing
  faster than 20% of a window per turn would, and nothing here bounds that.
- **That (a), (b) or (c) is wrong in general.** They are wrong *here*, against
  this corpus, priced. A workload whose short-falls end in real rejections would
  reopen the row, and the instrument to detect that now exists.
- **A rate for the livelock.** One turn. What is established is that the state
  occurs and that compaction is not what is failing in it.
- **One machine, one operator, three workloads** — as M700 said. The zero-rejection
  finding is the one part that now has a second machine behind it; the
  *proportions* (81/17/2, the exhaustion curve, the per-window table) are this
  corpus's alone.
