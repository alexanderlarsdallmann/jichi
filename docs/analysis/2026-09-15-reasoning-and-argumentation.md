# Reasoning, decision discipline, and argumentation theory in jichi — what is already practised, what theory names it, and what it would buy

*Analysis, 2026-09-15, at the operator's request ("analyze the jichi codebase
regarding how reasoning, and adhering to decision making, and argumentation
theory can be used to support agentic software development, as well as users,
and self-learners"). Method: three parallel read-only surveys — the agent-side
machinery (`jc_sysmsg.c`, modes, `ask_user`, constraints, the learning loop,
verdicts, the envelope), the learner-side scaffolds (records practice, design
tutorials, process track, hint ladder, curriculum modules, dialogues), and the
registers read as data (`DECISIONS.md`, `DEFERRED.md`, `ANECDOTES.md`) — every
load-bearing claim anchored to a file. The mapping onto named theory is mine
and is marked as such; the corpus itself uses none of those names.*

**A bias, declared.** A language model analysing a corpus written almost
entirely by language models, about how language models should reason. Read
the anchors and trust the sentences exactly as far as those carry them
(`docs/analysis/2026-08-27-the-language-of-lessons.md` §0 says the same about
itself, and it was right to).

## 0. The finding in one paragraph

jichi already practises a coherent argumentation discipline — in its own
vernacular, without a single occurrence of *Toulmin*, *IBIS*, *critical
question*, *defeasible* or *Socratic* in the learner-facing docs (§1, the
count table). Its dominant move is one sentence long: **make the claim
checkable, then check the checker**, and it appears at four layers with the
same shape — the craft prompt asks for behaviour an observer could check; the
deferral register asks that a reason's factual half be checked; the gate
doctrine asks who checks the gate; the test doctrine asks whether the suite
could notice. Its registers are, in effect, a design-rationale method (the
IBIS/ADR shape: decision + rejected alternatives), a defeasibility register
(`DEFERRED.md`'s "revisit when" triggers), and a falsification norm (born
red, TAINTED, refusal ≠ grade). What it lacks is precise and small: **no
comparative reasoning** anywhere (its decision criterion is binary and
generative — "name one rejected alternative" — never "criteria, then
options"); **no modelled warrant layer** ("why it lost" is prose, so the
registers cannot be asked *what kind* of reason a row rests on); **counter-
argument as a practised move** (one rebuttal gate, one devil's-advocate
prompt, and the adversarial second seat recommended at M602 never shipped);
**no plan artifact** (plan mode is a permission fence plus a prose request);
and **three places where doctrine and code have parted** that this survey
found on the way (§5). The theory's contribution is not "reason better"
prose — the project's own A/B says prose moves nothing a grader can see — but
**structured artifacts with a mechanical floor**, the tier the project trusts.

## 1. What jichi calls it, and what the theory calls it

The house vocabulary is consistent enough to be tabulated. Column three is my
mapping; column four is where the term is load-bearing in the tree.

| jichi's phrase | The move | The theory that names it | Where |
|---|---|---|---|
| "If there was no rejected alternative, it was not a decision — it was just work" | a decision must carry the option it beat, and why | **Design rationale** (Rittel & Kunz's IBIS: issue → positions → arguments; Nygard's ADR: context / decision / alternatives / consequences) | `DECISIONS.md:22-25`, `PROJECT_RECORDS.md:206-218`, `ARCHITECTURE_TUTORIAL.md:75-80`, `10-design-before-code/test.sh` check 3 |
| "If new information could change the answer, it is deferred; if the answer follows from the design, it was decided" + **Revisit when** | a conclusion held provisionally, with its defeater named | **Defeasible reasoning** (Pollock): a deferral is a defeasibly-held conclusion with an explicit defeater condition; 19 of 48 open rows name one | `DEFERRED.md:47-52`, `:126`, `:214`, `:329` |
| "A reason may contain judgement, evidence, or an unchecked factual claim. The first two belong here. The third does not — check it first" (M326b) | classify what a reason is made of before admitting it | **Toulmin's layout**: grounds vs. backing; an argument whose grounds are unverified has no backing and is inadmissible as a *reason*, however sound its warrant | `DEFERRED.md:19-40` |
| `## Claim` / `## Evidence` / `## Verdict` | the only structured argument schema in the tree, graded by `grep` for headings | **Toulmin**, two of six slots (claim, grounds); missing warrant, qualifier, rebuttal | `15-the-confident-misdiagnosis.md:30-38`, its `test.sh:12-18` |
| "when X changes, this costs Y, for Z"; `why it matters:` | a finding must argue its consequence | **Walton's argument from consequences**, with its critical questions ("how likely, how costly, for whom") folded into "for Z" | `INSTRUCTOR.md:229-231`, `11-name-whats-wrong/test.sh` check 3, `curriculum/07:34-38` |
| "Fluency is not evidence"; "the answer is the *last* artifact you should trust, because it is the only one nothing checked" | confidence is not a warrant; the claimant carries the burden | **Burden of proof** on the proponent; the fallacy of appeal to confidence (ethos standing in for logos) | `15-…md:28`, `tsuiseki-04:208-212`, `fukabori-11:16-21` |
| "a classifier's else-branch must not be a finding — make the fallback say *unknown*" | absence of a match is not evidence of the other class | **Negation-as-failure is not evidence**; the closed-world assumption made explicit and refused | `CLAUDE.md:251-254` (see §5 — not implemented in `jc_toolprobe`) |
| "the floor a script can check; the quality is your judgment"; `WHAT IS AND IS NOT CHECKED (the M305 rule)` | state the scope of a claim beside the claim | **Toulmin's qualifier**, written as a header: verifiable premises separated from evaluative ones; 11 drivers carry it verbatim | every process-track grader; `fence_write_tools.sh` header; `DECISIONS.md:226` |
| born red / VACUOUS / TOOTHLESS | a test's epistemic standing: falsified once, or never | **Popperian falsifiability**, applied to the instrument | `APPROACH.md:74-84`, `TEST_INTEGRITY.md:685-696`, `tests/teeth.sh` |
| PASS / FAIL / **TAINTED** / refusal (exit 77 → exit 2) | verdicts that refuse to conflate categories | **Undercutting vs. rebutting defeaters** (Pollock): TAINTED is green whose *warrant* was undermined (the gate was moved), not green that was rebutted; a refusal is "no verdict", not a verdict | `jc_assign.c:260-277`, `jc_gradecore.h:33-48`, `GATE_INTEGRITY.md` §5.1 REFUSE-THE-GREEN |
| "the model proposes, the system and the human dispose"; propose-only | separation of proposal and disposal | **Dialogue-game roles** (Walton & Krabbe): proponent (model), opponent/judge (gate, human); the learning loop and self-improvement are *inquiry* dialogues whose conclusions only the judge may adopt | `fukabori-11:138-145`, `LEARNING.md:355-360`, `DECISIONS.md:44` ("a model writing a fence for itself") |
| tutor stance; hint ladder nudge → approach → worked step; "recorded, never penalised"; predict before reveal | teach by questions; make inquiry moves free; elicit a commitment before showing | **Socratic elenchus / maieutics**; removing the cost of asking removes the incentive to bluff | `jc_sysmsg.c:894-916`, `jc_tool_hint.c:45-81`, `code-reading` skill (`jc_scaffold.c`), `CODE_REVIEW.md:64-68` |
| symptom → dead ends → root cause → lesson; "record the surprise before you explain it"; the pre-mortem | keep the refuted hypotheses with the answer | **Abduction** (Peirce) with the discarded hypotheses retained; prospective hindsight | `ANECDOTES.md:3-42`, `curriculum/04:13-24`, `JOURNEY.md:74-78` |
| "## What would falsify the whole thing"; "The decision, stated falsifiably" | name the observation that would overturn the claim | explicit **falsifiers** for design claims, not only tests | `fukabori-01:5,13,96`, `plans/2026-08-basicfantasy-orchestration.md:410` |
| `docs/dialogues/` — "conversations that decided something", ending "the maintainer answered: write it" | a recorded deliberation terminating in an adopted claim plus an artifact | **Deliberation dialogue** (Walton & Krabbe), with the judge's ruling written down | `docs/README.md:259`, `dialogues/2026-07-14-the-one-feature.md:69-70` |
| `## Objections` — "/check feedback either applied or explicitly rebutted in the doc" | a review must be answered, not ignored | **Rebuttal** as an obligation; the corpus's single use of the word | `curriculum/06:45-50` |

**What the table shows.** Every row but one is a *disciplined practice with a
mechanical floor*; the theory adds names, not moves. The exception is the
warrant: in every register the *reason* is free prose. That is the seam §2–§4
return to.

## 2. Agentic development — where the agent's reasoning is shaped, and what theory would add

### 2.1 What exists, by tier

The project ranks its own instruments (`language-of-lessons` §0): **1** a
lint or gate that fires whether anyone remembers; **2** a ritual with a
mechanical output; **3** a rule delivered every turn; **4** a narrative
record; **5** an intention. Sorted that way, the agent-side reasoning
machinery reads:

- **Tier 1 — fences and verdicts.** Plan mode is a read-only *fence*
  (`jc_perm.c:31`: a mutating tool in plan mode is denied before any
  allow-list), not a plan protocol. The constraint scanner is a small
  *grammar of prohibitions* (`jc_constraint.c:612-615`, eight negation cues;
  `acts_on()` distinguishes verb from noun after M167 turned "do not change
  the test file" into a ban on running tests) whose inferences are
  **session-scoped by provenance** — a guess may not outlive the turn that
  made it (`CONSTRAINTS.md` §Provenance). The verdict taxonomy refuses
  category conflation (§1, TAINTED row). Gate integrity asks who checks the
  gate (`GATE_INTEGRITY.md:20`: "a gate that the thing it grades can edit is
  not a gate").
- **Tier 2 — rituals with output.** `brief-check` predicts, before a run is
  spent, every constraint a brief would infer *and the line that produced
  it*, plus the gate's baseline colour — "a `goal` gate that already passes
  forces nothing" (`AUTONOMY.md` §3z). The self-review pass (M39) hands the
  model its own diff once per turn. `ask_user` is **recorded, not gated**:
  every call is journaled `answered:true|false` (M359) so a reviewer learns
  "the model judged N decisions blocking and guessed".
- **Tier 3 — prose.** The craft prompt (`jc_sysmsg.c:214-241`) asks for
  exactly the argumentation moves: measure rather than assume, ask only when
  two readings differ materially, design with rejected alternatives, prove a
  test can fail, say what was not done. **Measured (the craft A/B,
  `analysis/2026-08-06-craft-ab.md`): on a 31B local model, nothing the
  section asks for appeared in either arm** — 0/3 design notes, 0/3 rejected
  alternatives. The project's honest conclusion: "the instrument cannot see
  it."

### 2.2 The pattern, and what it implies for theory

Every reasoning intervention aimed at the *agent* is tier 3 — a sentence in a
prompt — while every one the project trusts is tier 1. Argumentation theory's
useful contribution is therefore not better exhortation. It is to give the
agent's reasoning **a shape the machine can check for completeness**, the way
the graders check a learner's `VERDICT.md` for its three headings without
judging the argument. That is the same floor-vs-judgment line the curriculum
draws, drawn for the agent.

### 2.3 Candidates, each with what it buys and its honest limit

**A. A plan artifact with an argument shape** (the largest gain). Plan mode
prints a plan into the conversation; nothing records it, nothing compares it
to what was then done, and compaction can drop it. A `PLAN.md` the model
writes in plan mode with four headed sections — *Claim* (what will change and
why), *Rejected* (≥1 alternative, with reason), *Falsifier* (what result
would show the plan wrong), *Not-goals* — is checkable for shape the way task
10 is (count headings and bullets), survives compaction as a file, and gives
the envelope something to **reconcile**: files touched vs. files the plan
named, exactly as `revertOutOfScope` reconciles writes against a scope.
`DESIGN_INPUT.md` is already the *input* twin ("authoritative for planning,
not a licence to ignore the code"); this is its output. *Limit:* shape, not
quality — a fluent plan with a hollow falsifier passes the floor. *Rejected
here:* a JSON plan schema — the learner's registers are markdown with a
grep-countable rule, and the agent's should be the same kind of thing.

**B. `ask_user` with its critical question attached.** The rule "ask only
what the code cannot answer" is taught in three places and enforced nowhere.
Walton's form of a good question names the *two readings* and *what differs
materially*. If the tool required both fields (`readings: [..]`, `differs:`)
and journaled them, the M359 record would carry not just that the model asked
but *why* — and a question with one reading, or a "differs" a `grep` would
settle, becomes countable in `jichi runs`. *Limit:* the fields are prose; a
model can fill them fluently. But the smell becomes visible, which is the
tier-2 gain.

**C. The adversarial second seat (M602's R2, unshipped).** The
`language-of-lessons` analysis recommended "ask the second seat to
adversarially refute the first" and noted the workflow `verify` stage could
host it; today that stage runs a shell command
(`jc_workflow.c:19`), and the only adversarial instruction in the tree is
one parenthesis to the *learner* ("argue for the design I rejected",
`curriculum/06:27`). A read-only `refute` stage handed the first seat's claim
and evidence, asked for **rebutting** defeaters (the claim is false) and
**undercutting** ones (the evidence does not support it), output as headed
objections — would make counter-argument a practised move in the loop.
*Limit, already recorded:* two models sharing a base share blind spots. And
the craft A/B is the precedent: **measure before believing** — run it as a
pre-registered A/B on `jlu/qwen3-coder-next`, the same shape M545 asked for.

**D. Warrant typing in the learning loop.** M326b's trichotomy — judgement /
measured / unchecked factual claim — exists for deferrals and nowhere else.
If the `/learn` mentor had to tag each drafted lesson with one of the three
(`[warrant: measured]`, `[warrant: judgement]`, `[warrant: unchecked]`) and
`learn apply` reported the counts, an unchecked claim could not enter memory
unflagged, and `## Checks` — which only makes sense for a lesson that is
*measured* — would have its precondition visible. This is **not** D3's
rejected "model judges whether a note is true"; it is provenance-labelling,
which the mentor can do honestly ("I inferred this from one run"). *Limit:*
a label is a hint, not a schema (D8's own point); the count is the floor.

**E. The final sentence, footed by the record.** `tsuiseki-04` proves the
run's summary is a summary of what was *tried*, and the answer "is exactly as
reliable as the run and not one bit more". The machine already knows what the
answer cannot: `is_error` counts, `test_edits`, whether verify ran, whether
writes left the scope. A one-line footer attached to a headless result by the
envelope — *checked: verify green · 0 tool errors · 0 test edits; not
checked: no verifier armed* — is `STATE-THE-REACH` applied to the answer
itself, and it is the M305 header for a run. *Limit:* it states what was
checked, never whether the answer is true.

## 3. Users — operators driving jichi

Most of what a user needs is present and unusually well-argued.
`TOOL_DECISIONS.md` walks the nine steps between a model's tool call and its
execution with a who-decides column ("An ALLOW verdict does not mean 'it
runs'"; "Your 'yes' is not the last word either"); `brief-check` is decision
hygiene before spend; the caps-vs-fences distinction (present as structure,
not as the phrase) tells a user which bounds *stop* and which *forbid*
(`AUTONOMY.md:11-18`: a run with no verifier is "bounded in cost and
unbounded in consequence"). The `grading-rubric` skill is the one place in
the tree that teaches **criteria** ("each objectively checkable… tie each
criterion to a specific requirement… mark must-pass vs nice-to-have").

The gap for users is the same as §2.3 E: the run's *record* is honest
(journal, `runs`, `audit`), but the run's *answer* is not footed by it. A
user reads the sentence first and the journal never. E closes that at no
model cost.

## 4. Self-learners

### 4.1 What exists

Ten disciplined moves, most with a floor a grader checks (the learner-side
survey, §"Synthesis"): the binary decision criterion (task 10 counts ≥2
rejected alternatives); claim / evidence / verdict (15); argued-by-consequence
(11, with the board template "when X changes, this costs Y, for Z"); the
rebuttal gate (`## Objections`, module 06); predict-before-reveal and the
fenced hint ladder (the `code-reading` skill; the tool refuses to spend a
human learner's rungs, `jc_tool_hint.c:45-58` — a prompt binds only the model
that reads it); calibration (73: estimate vs. actual, "learn how wrong you
are"); belief retraction (module 10: "a teaching you cannot take back is
dogma"); dead ends recorded with their refutation (04); anchored claims (74,
`CODE_REVIEW.md`: "an anchor is a checkable claim; a sentence without one is
an opinion about code"); and the floor-vs-judgment frame stated in every
grader's own header. The peer layer is real but instructor-side: M6's
"read three submissions aloud and let the room rank them", M10's mandatory
exchange, M11's live defence ("explain a decision, change something on
request, show the check that would catch its removal").

### 4.2 The gaps, stated plainly

- **No comparative reasoning is taught anywhere.** `grep -iE 'trade-?off|
  criteri|weigh|decision matrix'` over the five design tutorials returns four
  hits, all incidental or in external-reading lists. The discipline is
  generative (name what you rejected) and never evaluative (say what
  criterion decided). `ARCHITECTURE_TUTORIAL.md:163-165` defers "architecture
  as trade-offs" to a book.
- **The process track (67–73) grades no decision and no argument.**
  69-process-design grades *traceability only* — every requirement id must
  appear in `DESIGN.md`; it never asks why this design and not another. 71
  grades the presence of the word "decided", not a reason. The rejected-
  alternative requirement lives one track over, in task 10, as a bullet
  count.
- **Counter-argument is nearly absent for learners.** One `rebut` (module
  06), one adversarial prompt, and the one fallacy the corpus names — "straw
  men" — is named to the *instructor* (`INSTRUCTOR.md:221`), never to the
  learner who will commit it.
- **The theory is never named**, so the discipline does not transfer under
  its own name: a learner who has written forty `Rejected:` lines has been
  keeping ADRs and does not know the word.

### 4.3 Candidates

**G. Grade a decision in the process track.** Upgrade 69 (or add a task):
`DESIGN.md` carries a `## Decisions` section where each decision names ≥1
rejected alternative **and the criterion that decided it** — "criteria before
options" (the QOC method: Questions, Options, Criteria). The grader counts
decisions, alternatives per decision, and a criterion clause per decision;
the floor, not the quality — the process track's own contract. This is the
smallest change that introduces *comparative* reasoning, and it closes the
"process track grades no argument" gap with a 15-line `sh` script in the
existing style. The reference solution models a trade-off table once.

**H. Critical questions as the learner's own checklist.** Module 07's board
template gives the *form* of a consequence argument; Walton gives its
*critical questions* — how likely is the consequence, how costly, for whom,
and what would the alternative have cost? Stated in `curriculum/07` and
`CODE_REVIEW.md`'s Review row as four questions the learner asks of their
own finding before a reviewer does. *Limit:* prose; but it is prose for a
human, which is the tier where prose works.

**I. Steelman before rebuttal.** Module 06's `## Objections` gate requires a
reply; it does not require the objection be stated fairly first. Two-line
form — `Objection (strongest form): …` / `Reply: …` — is grep-checkable and
teaches the learner the anti-straw-man move the instructor guide currently
reserves for the instructor.

**J. Name the theory once, lightly, after the fact.** A short page mapping
the house vernacular to its names (this document's §1 table, made
learner-facing) — with the honest ordering: the practice came first and
works; the names are for *leaving the form* (Ri), so the learner can
recognise the same discipline in a codebase that calls it ADRs. Not before G
and I exist, or it describes aspiration.

**K. Record predictions, not only estimates.** Task 73 is the corpus's one
calibration exercise (estimate vs. actual). The `code-reading` skill already
asks the learner to *predict* before it reveals. If those predictions landed
in their own sink (`.jichi/predictions.jsonl`, separate for the same reason
`hints.jsonl` is) the learner could read their own hit rate — calibration of
belief, not just of effort. Propose-only, learner-owned, never penalised.

## 5. Three places doctrine and code have parted — found on the way

Each is checkable now and cheap to settle; together they are the kind of
finding `DOC_REVIEW.md` §4 says only a reader can make.

1. **"The reasoning is streamed so you can audit it, never hidden"**
   (`docs/reading/fukabori-11-ai-supported-coding-examined.md:27`) — **false
   of the code.** The OpenAI provider reads `reasoning_content` and sets a
   flag (`jc_provider_openai.c:400-419`, `prov_internal.h:44`) and *never
   emits it*; the comment says "Not the answer -- we don't emit it"; the
   Anthropic provider has no `thinking` handling at all. The flag exists to
   diagnose an empty turn (`reasoning_budget_hint.sh`, M521), which is good
   evidential discipline — but the page claims the opposite, in the chapter
   about what a reasoning trace warrants. Fix: correct the sentence; decide
   separately whether surfacing reasoning is wanted (it is a warrant-shaped
   artifact the user cannot currently see).
2. **"Make the fallback say *unknown*"** (`CLAUDE.md:253`) — **not
   implemented where it is cited.** `jc_toolprobe_verdict` has no UNKNOWN
   member (`jc_toolprobe.h:45-49`; `default:` returns `"none"`) and
   `scripts/probe-models.sh:162-164`'s else-branch says `none`, not
   `unknown`. The M519 defect was fixed at its cause (the newline blindness),
   which is right; but the rule the fix was generalised into is not in force.
   Either add the value or narrow the rule's claim.
3. **Retraction keeps the wrong version in prose and deletes it in the
   machine.** `ANECDOTES.md:4843-4845` (#75, lesson 5): "leave the wrong
   version in place… A retraction that edits the mistake out of existence
   teaches nothing." `jc_memory_apply_correction` (`jc_memory.c:220-266`) and
   `jc_learn_rules_correct` (`jc_learn.c:604-662`) drop the line and keep only
   a count in the apply summary. Neither is wrong on its own — a memory note
   is not an analysis page — but the project has two retraction norms and
   has not said which applies where. `PROJECT_RECORDS.md:274` tells the
   *learner* "never delete a decision; supersede it", while jichi's own
   `DECISIONS.md` has no status or superseded column (302 rows, zero marked
   reversed; supersession is handled by `DEFERRED.md` closures and ROADMAP
   "Corrected at" entries). An ADR-style `status:` (accepted / superseded by
   M…) is the small fix that makes the register defeasible in the way its
   deferral twin already is.

## 6. Recommendation, ranked smallest-first

| # | Candidate | Kind | Why this order |
|---|---|---|---|
| 1 | §5 — settle the three divergences | doc fix + one decision (retraction norm; ADR status column) | Cheap, found by this analysis, and the project's own rule is that a register carrying a false reason "is worse than one missing the row" |
| 2 | **G** — grade a decision (criterion + rejected alternative) in the process track | artifact with a floor | Smallest change that closes the largest learner gap (no comparative reasoning; process track grades no argument); pure `sh`, born-red-able |
| 3 | **E** — foot the headless answer with what the record checked | machine-derived, no model | Closes the user gap at zero model cost; `STATE-THE-REACH` for the answer |
| 4 | **A** — the plan artifact, reconciled against execution | artifact with a floor | Largest agentic gain; needs its own plan; `DESIGN_INPUT` is its twin |
| 5 | **D** — warrant typing in `/learn` drafts | provenance label + count | Extends M326b to the loop; precondition for `## Checks` made visible |
| 6 | **I**, **H** — steelman form; critical questions | learner-facing floor / prose | Teach the counter-argument move to the person who will commit the fallacy |
| 7 | **C** — the adversarial second seat | model-driven; **measure first** | R2 is a recommendation, not a result; the craft A/B is the precedent for how it could disappoint |
| 8 | **J**, **K** — name the theory; record predictions | after the practice exists | Names describe practice, not aspiration; predictions need the skill in use |

**What would falsify this analysis.** The craft A/B showed prose-level
interventions moved nothing a grader could see; H and J are prose and inherit
that risk in full. If G's decision-grading produces only *criteria written to
satisfy the grep* — the "straw men" failure mode M6's instructor notes
already predict — the floor was the wrong instrument and the judgment layer
(peer ranking, live defence) is the only one that works for argument quality.
And nothing here is measured: no learner has produced a `DECISIONS.md` under
observation, and no A/B has tested whether a second adversarial seat catches
what the first missed. The three §5 divergences are the only claims above
that are simply true or false today.
