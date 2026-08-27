# Driving jichi at a real project: what 28 runs measured

*Every number here came from a journal or a command. This is the record of one engagement —
jichi `--auto` driving **zigodot**, a from-scratch Zig reimplementation of the Godot
engine (a private repository, so there is no link to follow; what matters here is that
it is ~19k lines of a language jichi's author was not fluent in) — written for whoever drives it next,
human or agent. Where a finding rests on two data points it says so. Where an earlier
version of this page was wrong, the correction is left in.*

The single most expensive lesson, stated first: **do not cap a run from an estimate. Measure
one uncapped run, then decide.** Estimates in this engagement were wrong by 2×–2.5× in both
directions, and the failure they caused was not "slightly truncated".

---

## 1. The measured record

28 runs against one project, one model (`jlu/qwen3-coder-next` via an HRZ endpoint), **no
prompt caching**, so every figure is dominated by re-sent conversation history.

| run | task shape | outcome | tokens | calls | token cap | call cap |
| --- | --- | --- | --- | --- | --- | --- |
| x11assert | 2 assertions, small file | **ok** | 223,346 | 10 | 700k | 28 |
| run2 | one design document | **ok** | 400,817 | 27 | 1.2M | 45 |
| run10b | one plan document | **ok** | 436,491 | 12 | 2M | 55 |
| run5 | one design document | **ok** | 455,919 | 19 | 1.6M | 55 |
| undo-b | delegate undo to a view | **ok** | 538,524 | 18 | *none* | 35 |
| run12 | `/solve` command | **ok** | 563,504 | 21 | 1.5M | 40 |
| run3 | one plan document | **ok** | 653,225 | 17 | 1.2M | 45 |
| run4 (2nd) | design document, retry | **ok** | 967,525 | 32 | 1.6M | 60 |
| undo-a2 | undo/redo in one type | **ok** | 1,102,913 | 28 | *none* | 45 |
| run9 | analyse a subsystem | **ok** | 1,497,102 | 31 | 2M | 60 |
| platpaths | 2 syscall ports + tests | **ok** | 2,009,021 | 56 | *none* | *none* |
| envmain | env injection + 2 mains | **ok** | 3,157,985 | 53 | *none* | *none* |
| run10 | port a plan | verify_failed | 484,633 | 13 | 2M | 55 |
| run1-gate | fix a red gate | budget | 509,216 | 18 | **500k** | 30 |
| gatelint | one Python lint change | budget | 928,692 | 26 | **900k** | 35 |
| gate-string | gate a subsystem (29 tests) | budget | 1,017,880 | 27 | **1M** | 55 |
| run6 | parse a literal | budget | 1,103,037 | 29 | 2M | 55 |
| run1b | fix 6 compile errors | budget | 1,190,923 | **40** | 1.5M (79% used) | **40/40** |
| m330 | jichi C change + tests | budget | 1,427,680 | 45 | **1.5M** | 45 |
| gate-se | gate a subsystem | budget | 1,516,474 | 41 | **1.5M** | 45 |
| m330b | the same, retry | budget | 1,527,176 | 30 | **1.5M** | 50 |
| run1c | the same, 3rd attempt | budget | 2,037,377 | 58 | **2M** | 60 |
| run8 | a GDScript prefix | budget | 2,044,380 | 48 | **2M** | 55 |
| run6b | parse a node path | budget | 2,047,125 | 54 | **2M** | 55 |
| run14 | gate a subsystem | budget | 2,052,369 | 48 | **2M** | 60 |
| run7 | multiline brackets | budget | 2,052,506 | 47 | **2M** | 55 |
| undoredo1 | implement a design | budget | 4,467,510 | 50 | *none* | **50/50 calls** |

**11 ok, 16 budget-exhausted, 1 verify-failed.** A ~41% completion rate, and the dominant
failure by an enormous margin is the budget — not the model, not the brief.

## 2. The finding that should change how you set a cap

Take only the runs given a token cap, and express what they spent as a fraction of it:

| runs that **finished** | 22% · 28% · 32% · 33% · 38% · 54% · 60% · 75% |
| --- | --- |
| runs that **died at the cap** | 95% · 101% · 102% · 102% · 102% · 102% · 103% · 103% · 103% |

**The band from 76% to 100% is empty.** Twenty runs, not one of them finished having used
between three-quarters and all of its budget.

That is not a coincidence, and it is the reason estimating a cap does not work. Cost on a
backend without prompt caching is **superlinear in call count** — each call re-sends
everything before it — so a run that needs more than you gave it does not need *slightly*
more. It is still converging, and each further call costs more than the last. There is no
"just barely made it".

So a tight cap does not buy you a safety margin. It buys you a coin flip:

- If your estimate was generous, the run finishes with **at least 25% headroom** and the cap
  never mattered.
- If it was not, the run dies **at the ceiling**, having spent everything and delivered
  nothing you can gate.

**A cap is a circuit breaker, not a budget.** Set it far above any plausible need, or omit it
and bound the run with a deadline instead. Sizing it "about right" is the one choice with no
upside.

Corroboration from the other direction: of the five runs given **no** token cap, four
finished. The fifth died on a *tool-call* cap — see §3.

## 3. There are three axes, and any one of them binds first

`--budget-tokens`, `--max-tool-calls`, `--deadline`. Capping one and leaving another open
just moves the failure:

- **run1b** died on calls (40 of 40) with tokens at 79% of their cap.
- **undoredo1** died on calls (50 of 50) with tokens *uncapped* — 4.47M spent, work kept,
  and the verify sequence one failing test from green (`exit 1` → `failed: 3` →
  `failed: 1`). The operator had set that call cap on his own initiative after agreeing the
  token budget would be open. It is the same mistake as a tight token cap, wearing a
  different hat.

If you cap nothing else, cap the **deadline**. It is the axis whose exhaustion tells you
least about the work and most about the wall clock, so it makes the honest circuit breaker.

## 4. What a run actually costs

**Do not estimate from lines of code.** A ~15-line change (two syscall ports, three
`errdefer` fixes, two test un-skips) cost **2,009,021 tokens over 56 calls**. The estimate
beforehand was 700k–1.1M over 15–25 calls: wrong by ~2× on tokens and ~2.5× on calls. Almost
all cost is *input* — files read, and then re-sent on every subsequent call.

**Do not estimate from call count either.** That was this page's first correction:

| | platpaths | envmain |
| --- | --- | --- |
| tokens | 2,009,021 | **3,157,985** |
| calls | 56 | **53** |
| tokens/call | 35.9k | **59.6k** |

More tokens with *fewer* calls. Per-call cost is a property of **what each call must carry**,
not of the project or the call count.

**Tokens per call rises monotonically through a run**, because history is re-billed:

| run | first sample | final |
| --- | --- | --- |
| platpaths | 24.8k | 35.9k |
| envmain | 37.9k | 59.6k |
| undoredo1 | — | **99.7k** |

A figure sampled early under-predicts the whole. In `platpaths` the last call cost roughly
2.4× its twentieth.

**Observed range, same model and backend:** 22.3k/call (two assertions in a small file) to
99.7k/call (implementing a design document). A 4.5× spread on one project.

### `--design` is a strong lever with a per-call price

`--design <file>` (see [DESIGN_INPUT.md](DESIGN_INPUT.md)) supplies a design document to the
run. Its cost is **document size × call count**, because it rides in the prompt on every
call. A 15.5 KB document (~4k tokens) across 50 calls contributed to a per-call input of
**99,670** — 2.5× the peak of any other run in this engagement.

It pays for itself when the document prevents more re-reading than it costs. Prefer a
document that is *decisive and short* over one that is complete: the decisions and their
rejected alternatives earn their tokens, a restated problem statement does not.

## 5. Splitting work is cheaper than one long run

`gate-string` spent 1,017,880 tokens over 27 calls and died at a 1M cap **while converging**
(`exit 1, no counts` → `failed: 9` → `failed: 6`). The same task shape, deliberately split
in two:

| | unsplit | split, part A | split, part B |
| --- | --- | --- | --- |
| outcome | budget_exhausted | **ok** | **ok** |
| tokens | 1,017,880 | 1,102,913 | **538,524** |
| calls | 27 | 28 | 18 |
| delivered | nothing | the mechanism | the consumer |

Part A cost about what the unsplit attempt cost and *finished*. Part B cost half of part A —
not because it was briefed better, but because part A had already settled the hard questions
(which mechanism, what one step covers, where the cursor sits), and a brief can hand those
forward as **facts** rather than as questions.

Since cost is superlinear in calls, two runs of 25 calls cost far less than one of 50, *and*
each gets a fresh history to think in.

## 6. Gates that cannot lie

A gate is the only thing standing between a driven run and plausible-looking wrong work.
Five rules, each learned by a gate failing to do its job.

**6.1 Prove it two-sided before spending a run on it.** Implement the task by hand, watch the
gate go green, revert, watch it go red. Two runs in this engagement — ~3M tokens — died
against gates a correct change could not have satisfied: one whose edit scope forbade the
file its gate required, one that grepped output for a string the naming convention it had
itself mandated would never produce. Both looked like model failures and were briefing
failures.

Proving satisfiability is also the cheapest design review there is. Hand-implementing one
increment revealed that the type it was supposed to build on **did not compile and its core
method was an empty stub** — so the increment as first scoped was impossible, and no amount
of model capability would have found that acceptable.

**6.2 Make green mean *progress*, not perfection.** A gate that can only pass at 100%
completion cannot bank a checkpoint, so `--verify-every` never fires green and
budget exhaustion keeps only unverified work. For any multi-item task, assert that the counts
**moved** — pass rose *and* skip fell — and reserve all-or-nothing gates for single-item work.

Checking both directions catches more than it was designed for: one run *replaced* an
existing gate block with its own instead of adding one, silently un-gating **30 tests**. The
only reason it was caught is that the gate asserted `pass > 407` and the suite reported 376.

**6.3 `--verify-baseline` is incompatible with a monotonic gate.** A gate demanding progress
can never pass at baseline, so the flag reports "the starting tree is not known-good" about a
provably green tree. Use it only for gates that assert "nothing is broken".

**6.4 A gate that greps for a token can be satisfied by that token in a name.** A gate
required the string `checkAllAllocationFailures` to appear in the file. A run wrote a test
whose **title** contained that word while its body used the ordinary testing allocator and
proved nothing — so the ownership defect the check existed to prevent shipped anyway
(3 allocations, 0 frees, once a real harness call replaced the decorative one). Grep for the
**call**: `std\.testing\.checkAllAllocationFailures\(`.

**6.5 The strongest check is one the run cannot write.** Have the gate construct **its own**
consumer of the new API — its own fixture, its own assertions — run it, and clean up. A
passing test the run wrote cannot satisfy such a check; only a working implementation can.
This matters most when the thing being replaced "compiled" in the sense that nothing looked
at it, which is exactly how a stub survives for months.

## 7. Briefs

**7.1 Never phrase a boundary as a prohibition.** In AUTO mode jichi infers constraints from
the request and mechanically refuses matching tool calls for the whole run
([AUTONOMY.md](AUTONOMY.md), M110). Recorded misfires: *"Do not change the test file"* banned
the entire suite; *"Oracle files (read-only…)"* — a mere description — cost a 1.56M-token
run; and **"Do not build on that type"** was inferred as *"do not run build commands, do not
use `run_tests`, do not run tests"*, which had to be aborted 45 seconds in.

That last one was written by the same person who had written the rule forbidding it, one turn
earlier. So the check is now mechanical, not a matter of care:

```sh
grep -niE '\b(do not|don.t|never|avoid|must not|cannot)\b' brief.md
```

Rewrite every hit positively before driving. Let `--edit-scope`, `pathFence` and
`referenceRoots` carry the boundary — they are enforced identically and cannot misparse.
Read the `[constraint]` WARN line in the run's first output lines every time.

**7.2 What a brief that worked contains.** The cheapest successful run in this engagement
(223,346 tokens, 10 calls) had this shape, and so did every other one:

1. One sentence naming the deliverable — not a theme.
2. **State you are starting from**, with the actual current numbers, so the model need not
   discover them and the gate's arithmetic is checkable.
3. The constraint that decides the approach, once, with its reason.
4. A pointer to a **working local example**, by file and line.
5. A numbered deliverable list carrying the exact counts the gate will assert.
6. The gate, named by path, plus one line on why its assertions cannot be gamed.
7. Working notes: what to write, what to read whole, what to leave alone.

Add an **escape hatch** wherever an honest answer might be "this cannot be done as asked".
The 10-call run had one — *"if you conclude this test cannot assert anything meaningful, say
so and skip it with the reason; that is an acceptable outcome"* — and the runs that died at
their caps had no permitted outcome short of total success.

**7.3 Length is not the cost.** A brief that died at its cap was 119 lines against a
successful one's 69 — about 800 extra tokens per call, ~2% of that run's spend. The extra
prose bought an explanation that collapsed nine errors into one import change. **Do not
shorten briefs; shrink deliverables.**

**7.4 Keep briefs in version control.** For an entire engagement they lived outside the
repository, and nothing checked them. The consequence was measurable: **17 of 20 briefs
silently omitted the project's own stated first step** (analyse the reference source) for two
days, in a repository that lints ROADMAP entries, flag documentation, slash commands and
config keys. The one artefact determining what the agent attempts had no check at all.

## 8. Steering a live run

The control channel ([CONTROL.md](CONTROL.md)) is served at tool-call boundaries and cannot
widen permissions. In this engagement it earned its place once, decisively.

A run stalled: three consecutive **byte-identical** 12,586-byte writes of the same file, each
returning the same 7 trivial diagnostics (unused parameter, unused constant, undeclared
identifier). One `inject` naming the exact line numbers and the one-line fix for each broke
the loop immediately — afterwards it moved from compile errors to `failed: 3` to `failed: 1`.

**The stall signature worth watching for** in `--output jsonl`: repeated `write_file` results
with an identical byte count. Watch for it, and inject specifics rather than encouragement.
`status` gives you `tokens_used`, `tool_calls` and `elapsed` to decide with.

## 9. Reviewing the output: green is not correct

Every increment in this engagement passed its gate. Five carried defects anyway:

| defect | how it was found |
| --- | --- |
| out-of-bounds slice — a raw syscall's negative errno read as a length | probe, after noticing the test asserted only `len > 0` |
| a path containing a NUL byte — `getcwd`'s length includes the terminator | probe |
| an empty `LC_*` treated as set, where POSIX says unset | reading, prompted by an inconsistency *inside one change* |
| a callback fired after a no-op, plus a phantom undo step | probe |
| an ownership leak under allocation failure | `std.testing.checkAllAllocationFailures` |

The pattern: **every one lived at the boundary where a foreign convention meets your code** —
a syscall's error encoding, a length that counts its terminator, an environment variable whose
empty value means unset, an allocator that can fail. Put the reviewing effort there.

And the uncomfortable corollary: **prose in a brief is advice; an assertion is not.** One
brief named the exact hazard *and* pointed at a correct implementation ten lines above the
defect. The defect shipped anyway. What caught it was a test written afterwards. See
[ANECDOTES.md](ANECDOTES.md) #41.

Two more review habits earned the hard way:

- **Read the reference to learn the convention, not to copy the behaviour.** For one defect
  the upstream C++ had the right idiom and reading fifteen lines would have prevented it. For
  the very next one the upstream was **narrower than the spec** — copying it would have been
  parity-correct and wrong.
- **Do not diagnose from a tree the agent is writing to.** A transient mid-run state was
  nearly recorded as a third gate defect that did not exist.

## 10. Checklist

For a human, and for an agent driving jichi:

**Before the run**
- [ ] Say plainly what the run will build or test.
- [ ] Ask the operator for the budget rather than choosing one. If there is no measurement for
      this task shape, run **uncapped with a deadline** and measure.
- [ ] Prove the gate two-sided: green by hand, red at HEAD.
- [ ] Make green mean progress (both counts moved), not perfection.
- [ ] `grep -niE '\b(do not|don.t|never|avoid|must not|cannot)\b'` the brief; rewrite every hit.
- [ ] Check every gate assertion matches the contract the brief points at.
- [ ] Give the brief an escape hatch for an honest "this cannot be done as asked".
- [ ] Drop `--verify-baseline` if the gate is monotonic.

**During**
- [ ] Read the `[constraint]` WARN line in the first output lines.
- [ ] Watch for repeated `write_file` results with identical byte counts; `inject` specifics.

**After**
- [ ] Re-run the gate yourself, independently.
- [ ] Read the diff. Look hardest where a foreign convention meets your code.
- [ ] Check the test **count** did not fall, and that no gate entry was replaced.
- [ ] Record what it actually cost, next to the brief, in version control.

## 10b. The gap this engagement most wants closed

Every finding above is downstream of one number: the verifier's exit code. jichi holds a
second, independent signal about the same event — `jc_testparse`'s structured report — and
compares them only for **green** verifies (M86). When a red verdict disagrees with its own
evidence, nothing says so, and a driven run spends its whole budget repairing a phantom.

Written up as [proposals/2026-08-verify-consistency.md](proposals/2026-08-verify-consistency.md),
along with the four alternatives considered and why each lost — including the budget advisor
this page's §2 might seem to argue for, which lost because the correct advice is "omit the
cap and supervise", and that is a sentence rather than a feature.

## 11. What this page cannot tell you

One project, one model, one backend, **no prompt caching**, 28 runs, one operator. The
per-call figures are the least transferable thing here; the *shape* of the findings — cost
superlinear in calls, the empty 76–100% band, boundaries being where defects live — is more
likely to hold. Treat the numbers as a reason to measure your own workload, not a substitute
for doing it.

Two claims on this page were overturned by later measurement and are kept visible as
warnings: "budget from call count" (§4) and an attempt to blame a class of failed runs on a
harness bug that the journals then refused to support ([ANECDOTES.md](ANECDOTES.md) #42).

Full per-run detail, every brief verbatim, and each gate script:
`zigodot/docs/briefs/` and `zigodot/docs/analysis/2026-08-08-driven-run-cost.md`.
See also [AUTONOMY.md](AUTONOMY.md) for the envelope's flags,
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) for how this project's own suites have failed while
green, and [ANECDOTES.md](ANECDOTES.md) #38–#43 for the individual war stories.
