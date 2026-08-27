# Three seams — where guidance for users and agents is thinnest (2026-08-09)

**Date:** 2026-08-09 · **Question:** following this project's philosophy, which three seams
need mending so users *and* agents receive better guidance and support — for learning software
development, and for designing, developing, testing and documenting software? ·
**Method:** read out of the project's own registers, not invented — 縁（えん）: the surprises
were already recorded, in [`DEFERRED.md`](../DEFERRED.md), [`DECISIONS.md`](../DECISIONS.md),
[`TEST_INTEGRITY.md`](../TEST_INTEGRITY.md), [`GATE_INTEGRITY.md`](../GATE_INTEGRITY.md) and
[the driving findings](2026-08-07-driving-zigodot-harness-findings.md); this page joins them
into three joints and names the next evidenced step for each. ·
**Status:** seam 1's first stitch landed the same day (**M342**, §1a below); seams 2 and 3 are
named with their next step, which in both cases is a measurement that already has its harness.

A **seam** here means: two *shipped* subsystems meet, and the stitching between them is prose,
or missing, or unmeasured. The philosophy page sets the acceptance bar for any claim on this
page — a term "earns its place only while something in the code or the practice actually
differs because of it" — so each seam below ends with what would actually differ.

---

## Seam 1 — The missing middle timescale: nothing notices a pattern while it is still cheap

**The two edges.** jichi's recovery machinery is all *reactive to one failure* — the retry
ladder, `jc_jsonrepair` (M148), fuzzy edit matching (M38), route-on-stall, mid-turn compaction.
Its learning machinery is all *after the run* — `learn analyze`, the mentor, `dream`,
corrections ([LEARNING.md](../LEARNING.md)). Between them sits the timescale where the tokens
actually burn, and it is unwatched: **within a turn, nothing notices that the same call keeps
failing the same way**, and the help surfaces that exist go unasked — across two models and 24
graded runs the `hint` tool was **never called once**, six of those runs failing with it
advertised (M319/M320); one measured workload re-read files at 72% (2,056 reads over 584 paths,
one path 216×, M326z).

**Calibration** (a different agent, so a floor for plausibility, not jichi's rate): on 294
Continue sessions of the operator's real work, 25 of 50 error-turns repeated an identical
failing call, tail to 10× (2026-08-07, recorded in
[`repeat_failures.py`](../../tests/measure/repeat_failures.py)).

**Why the philosophy calls it the seam.** 守破離（しゅはり） set the standard for the learning
loop itself: *"the loop was not finished until it could take a lesson back"* (M78). By the same
standard, guidance is not finished until it can arrive while it still changes the run. And
侘寂（わびさび） — the tool admits truncation, caps, partial answers; an agent inside a redo
loop currently admits nothing, because it cannot see the loop.

**What mending looks like.** Measure on jichi's own telemetry (the register's bar: a week of
ordinary use, plus a second project's log); if confirmed, a pure in-turn pattern check at the
tool-call boundary chokepoint (where `jc_bg_poll`/`jc_control_boundary` already sit — M329's
lesson: guard at the chokepoint), advisory-first in the M83 shipping order, that says so to the
model once per turn (the M147 nudge shape) and **names only help actually advertised in this
run's toolset** (the M319 rule): `hint` under `attempt`, `ask_user` interactively, stop-and-
report under `--auto`. Plus a journal event and a `runs` column, so the operator sees the loop
without grepping jsonl.

### §1a — The first stitch, same day (M342)

The interim measurement ran on 2026-08-09 — **two days in, below the register row's own bar,
and experiment-dominated (three of the 19 logs are one scripted scenario, re-run), so it
settles nothing about rates**. It still paid for itself twice.

| what the instrument said | what the logs held |
|---|---|
| 208 calls, 103 errored (49.5%), 7 error-turns, **0 turns repeating an identical failing call** | **one loop, 96 times**: each of three driven runs re-issued the same over-cap `git log --oneline --all -N` **32× in one turn**, escalating `-N` from 300 to 20,000,000 with `head`/`tail` slices varied |

The exact `(tool, target)` key is blind to a loop that varies a constant —
[`TEST_INTEGRITY.md`](../TEST_INTEGRITY.md) failure mode 1, consumed once more: the instrument
reported the absence of exactly the thing it was built to find, and the null was believed until
the raw events were read. The script now reports a **paraphrased** measure beside the exact one
(same tool + exit class, ≥3 failures, ≥2 distinct targets): **3 of 7 error-turns (43%)** on the
same logs. The two measures are deliberately never merged (see the M342 row in
[`DECISIONS.md`](../DECISIONS.md)) — a shared exit code is not proof of a loop, so the weak
form's examples must be read, not counted.

**Reading the loop found the cause, and it was jichi's.** Every kill in that loop told the
model `[stopped: exceeded the memory budget]`. The actual kill was the **output byte cap** —
that config's own `runMaxBytes: 8192`, checked in the file rather than inferred; exit 137 at
~8.2 KB in 12 ms, while `memBudgetMb: 2500` had nothing to do with it. `run_command_watched`
shared one `killed` flag across three causes (byte cap, memory watchdog, abort) and gave them
all the memory-budget label. Told "memory" 32 times, the model escalated how much history it
asked for instead of narrowing its output — a reasonable response to a false diagnosis.

**Fixed as M342**: each kill cause now names itself; the cap note carries the byte figure (a
number, never a config key — the runner's three callers resolve three different limits); exit
137 stays, because the SIGKILL is a fact. Proven red-first:
[`tests/smoke/run_kill_note.sh`](../../tests/smoke/run_kill_note.sh) failed 2 of 4 checks
against the unfixed binary, anchored on the kill note's own shape because the tool layer's
separate truncation note also says "capture limit" (the M293/M296 anchoring lesson).

**The design input this buys** (recorded on the register row): the detector's key must be
**(tool, failure class), not (tool, args)** — and whatever it tells the model must name a true
cause, because *a wrong-cause note is a loop amplifier*. The detector itself stays deferred on
its stated bar.

---

## Seam 2 — The unexamined gate: the curriculum proves every grader two-sided; the envelope trusts every working gate as written

**The two edges.** The teaching band and the working band. In the curriculum, a grader must
provably reject the untouched fixture *and* accept a reference solution, through the runner's
own parser — 77 graded tasks, 55 trap cases, enforced on every change
([CURRICULUM.md](../CURRICULUM.md)). In real bounded runs, `--verify` — the single command every
envelope guarantee reduces to — is taken on faith, and the record shows the price:

- **Two unsatisfiable gates burned ~3M tokens in one session** (findings §14: 929k + 1.53M; one
  run's work was complete and green when the budget died — only the gate disagreed).
- **A forcing check that forced nothing**: M330's docs gate ran a consistency lint that holds
  trivially when no entry is added — a requirement never imposed, discovered only afterwards.
- **The two kinds of verifier are conflated** (findings §10, documented and deliberately not
  fixed): with a *goal* gate (red before the work, by construction), `--verify-baseline` warns
  on every correct run, `--verify-every` can never bank a checkpoint, and a budget exit near the
  end looks identical to one at the start.
- **The gate is a file** ([GATE_INTEGRITY.md](../GATE_INTEGRITY.md)): a run edited its verifier
  to `exit 0` and finished `ok` (ANECDOTES #45). M332 shipped `--strict-green` — opt-in,
  default off, and the default flip is *explicitly waiting on a false-positive rate nobody has
  measured yet*.
- The standing rules that prevent all of this — *prove the gate satisfiable by hand-completing
  or stubbing the task; show a forcing check failing without the deliverable* — live in
  ANECDOTES #38/#39 and in memory. **They are audits.** The project's own maxim says prefer a
  lint; `TEST_INTEGRITY.md`'s recommendation #1 (a teeth-check helper) is still marked *not yet
  implemented*.

**Why the philosophy calls it the seam.** *"A test never observed failing has never been
observed working"* — applied to gates: **a gate never observed red against the missing
deliverable has never been observed forcing it.** The curriculum institutionalized exactly this
(two-sided graders); the envelope never inherited it.

**What mending looks like** — three evidenced steps, each threading a recorded rejection rather
than arguing with one. (1) Measure `--strict-green`'s false-positive rate on real runs — the
"separate, evidenced decision" M332 named, still ungathered. (2) Let the operator *declare* the
gate's kind — invariant or goal — and have the baseline probe **check the declaration** instead
of inferring anything: a declared invariant that is red at start makes the baseline warning
informative again; a declared goal that is *green* at start is the M330 failure caught before a
token is spent ("your gate passes without the work"). Findings §10 rejected silent inference
and a silent flag split; a checked declaration is neither. (3) Port the graders' two-sidedness
as an optional rehearsal: run the verifier against a stub or hand-completed fixture and confirm
green, then red without it — the generalized teeth-check helper.

**What would differ:** a driving agent stops being able to spend a million tokens against a
gate that cannot go green, and an operator's `verify` gets the same honesty bar as a
curriculum grader — for the human writing it and the agent bounded by it alike.

---

## Seam 3 — Guidance exempt from its own evidence bar: the prose that teaches design is the last unmeasured subsystem

**The two edges.** The system-prompt guidance layer and the project's measure-first discipline.
The craft section (M299) is jichi's design-and-decision pedagogy for agents — understand first,
write the design *and the decisions including rejected alternatives*, prove a test can fail —
the philosophy made executable, at **329–386 tokens on every call**. On the one model class
where it has been measured ([the 31B A/B](2026-08-06-craft-ab.md), pre-registered), it bought
**a measured nothing**: 18/18 passes both ways, no unprompted design note, no named alternative
in either condition. The honest conclusion shipped (off under `--lite`, unchanged elsewhere)
*because* the claimed value — larger models, vaguer tasks — was not tested there. It still is
not: the frontier A/B harness is **built and pre-registered**
([`proposals/2026-08-craft-ab-frontier.md`](../proposals/2026-08-craft-ab-frontier.md), M326g —
three unstated-deliverable tasks, blind pairwise grading) and waits on the two things code
cannot supply: the key the operator promised on 2026-08-06, and **a grader who is not the
author of the section under test**. The same seam holds DECLARE-THE-GATE: its one encouraging
result is n=1 per arm, and [GATE_INTEGRITY.md](../GATE_INTEGRITY.md) §8 says plainly the honest
experiment has not been done.

**Why the philosophy calls it the seam.** The bar is the philosophy page's own: *"each term
earns its place only while something in the code or the practice actually differs because of it
— and where one does not, it should be cut rather than admired."* The craft prompt currently
rides on faith exactly where it claims its value lives. What does not fit this project is the
current state — admired, unmeasured.

**What mending looks like.** Run the pre-registered experiment; repeat DECLARE-THE-GATE past
n=1; publish nulls as nulls (as M318–M320 already did); keep, scope, or cut on the result.
Either outcome mends the seam: guidance proven to change conduct on the models it claims to
serve, or ~350 tokens returned to every user on every call. This is the *designing and
documenting* seam — what the A/B grades is precisely whether design notes and named rejected
alternatives appear in the work.

---

## What was deliberately not picked

Named in the register's own spirit, with the reason:

- **Mid-turn compaction's short-fall behaviour** (what to DO when the pass cannot reach its
  target) — real, but waiting on its stated trigger: an `unrelieved` share measured on a
  post-M326y workload. An internal design choice more than a guidance gap.
- **Work preservation** — was a seam a week ago (a rollback destroyed 711,628 tokens of work,
  ANECDOTES #48); M336–M339 stitched it.
- **Grading the learner's record habit** — considered and honestly settled at M326s: a checker
  can grade the shape of a register, not the habit. Its revisit condition is stated on the
  register.

## Coverage against the question

| Clause | Seam 1 | Seam 2 | Seam 3 |
|---|---|---|---|
| learning software development | the hint ladder finally exercised; `learn analyze` gains a within-turn evidence class | the grader discipline follows the learner from `attempt` into real work | the tutor/craft stance proven or cut |
| designing | — | write the gate before the code, as a checked declaration | the design-first prompt measured where it claims value |
| developing | loops named while they are still cheap | — | — |
| testing | — | gates observed red before being trusted | — |
| documenting | — | forcing checks for document deliverables that actually force | design notes and decision records as graded evidence |
| users | see the loop and its cost | their `verify` gets the graders' honesty bar | stop paying for unproven prose, or keep it proven |
| agents | told the true cause, and where help is | told the gate is a contract, and which kind | guidance that measurably changes conduct |

The mechanism is shared on purpose — *"an interface a human can read is an interface a human
can audit"* ([AGENT_COLLABORATION.md](../AGENT_COLLABORATION.md)).
