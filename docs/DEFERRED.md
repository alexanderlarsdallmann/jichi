# Deferred register

> **What this is.** Things deliberately **not done**, with the reason and where the
> reasoning lives. A companion to [`DECISIONS.md`](DECISIONS.md): that page records what
> was chosen, this one records what was consciously left.
>
> **Why it exists.** "Recorded as the next slice" is only honest if the record is
> findable. Deferrals were accumulating inside individual ROADMAP entries and proposals,
> where a reader would have to already know the milestone to find them.
>
> **Coverage starts at M298** (2026-08-05), the beginning of the current program. Earlier
> deferrals live in their own milestone entries and are not back-filled — inventing a tidy
> list of decisions I did not witness would defeat the purpose. Items are removed when
> done, with the closing milestone noted in `DECISIONS.md` or the ROADMAP.

A deferral belongs here when someone could reasonably ask "why isn't this done?" and the
answer is a judgement rather than an oversight.

## Check the checkable part of a reason BEFORE parking the item (M326b)

**Three entries in a row were parked on an assumption that a minute's reading would have
settled:**

| Entry | The reason given | What it actually was |
|---|---|---|
| Measuring a `core` attempt that needs a hint (M319) | *"core costs the hint ladder — the machinery `attempt` exists to exercise"* | across two models and 24 runs the ladder was **never once called**, six of those runs failing with the tool advertised |
| Flipping `attempt` to `core` (M320) | the same hint-ladder cost | same; the flip was then refused for an entirely different, measured reason |
| `repoMap: false` for `attempt` (M326) | *"tasks 20–22 are explicitly about reading this repository"* | all three work on **self-contained fixtures**; none names a file in `src/` |

The shape is identical every time: the reason made a **factual claim about a fixture, a tool or a
number**, it sounded obviously true, and nobody checked it. Meanwhile the *judgement* half of each
reason was fine. So:

> **A reason may contain judgement, evidence, or an unchecked factual claim. The first two belong
> here. The third does not — check it first, and write down what the check found.**

Concretely, before adding a row: if the reason asserts what a task contains, what a tool does, how
many call sites something touches, or what is reachable from this machine — **go and look.** These
are minutes of work, and every one of them that went unchecked outlived several milestones and
misdirected the next person to read it.

The audit that produced this rule (M326b) is recorded in the rows below: each remaining reason's
checkable claims were verified, and two of them changed.

**A deferral is not a rejection** (M317). Its first revision conflated the two: three rows
were things decided *against* on the merits, which will never be done and do not belong on a
list of pending work — a register that mixes "not yet" with "not ever" makes both unreadable,
and the "not ever" rows are the ones a future reader would waste time on. Those moved to
[`DECISIONS.md`](DECISIONS.md), where a rejected alternative belongs. **The test: if new
information could change the answer, it is deferred; if the answer follows from the design,
it was decided.**

---

## What a walk of the whole register found (M657, 2026-09-18)

Every row on this page was read in one sitting, and the page's *structure* was
audited mechanically rather than by eye. What that produced is recorded here in
three parts, including the third, because a review that reports only what it
fixed is exactly the shape this page exists to distrust.

**The population.** 26 open sections, 58 open rows. That is what "the deferred
register" refers to when anyone cites it.

**What is now checked forever, for every row** —
[`tests/smoke/deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh),
five checks — a floor on its own extraction, then: no closed row under an open
heading, no open heading with no rows, every `path:line` anchor resolves, and the
never-compiled platform set matches `PLATFORMS.md`'s. It found, on its first run: **six struck-through rows under
three `## Open` headings**, **one `## Open` heading with no rows at all**, and
**illumos missing from the register entirely**. Three hand-audits of the same
invariant — M463, M492, this one — is past this project's threshold for
preferring a lint to an audit. The lint is deliberately structural: *it could not
have caught the worst row on the page*, which was well-formed and simply untrue.
What it catches is the class that kept the untrue row invisible.

**What was re-checked by hand, one claim at a time (the M326b rule).** Twelve
claims, and two of them were false:

| Claim as the row stated it | What the check found |
|---|---|
| *"no `LICENSE` or `COPYING` file exists"* (the licence row) | **False.** `LICENSE` and `NOTICE` committed 2026-08-27; the public repository shipped that day and was re-cut as `v0.9.1` on 2026-09-17. Closed, below |
| *"they will bring [the logo] before release"* | **Expired.** Two releases shipped without it; no logo file in either tree. Re-scoped rather than closed |
| *"146 hand-written `printf(name); test_name();` pairs"* | **Stale: 174.** True when written; the row's own argument is stronger for it |
| *illumos is blocked* | **Not blocked on access.** `/dev/kvm`, group `kvm`, `qemu-system-x86_64`, 24 threads with virtualisation, 137 GB free — all present on this machine today. It costs a session, not a resource |
| the ARM bench rows can be re-run | **Not today.** Pi Zero 2 W, Pi 400 and UNO Q all fail to answer; the hardware is powered down |
| *"a memory-pressure harness the smoke tier does not have"* (three peer-transport rows) | **Wrong about the test.** Each fix's effect is observable as an error, not as RSS. Re-scoped in place |
| the two unbounded read loops are where the rows say | Confirmed: `jc_sb_append_n` into an unbounded builder, at both anchors |
| *"MCP and LSP shutdown block in `waitpid(pid, …, 0)`"* | Confirmed, both sites |
| *"no `seccomp`/`bubblewrap`/`firejail`/`unshare`-as-sandbox exists in `src/`"* | Confirmed — the terms appear in `docs/` only, discussing the gap |
| *"`jc_mem_total_mb` has an `#if defined(__APPLE__)` branch"* (the macOS row) | Confirmed, `src/platform/jc_platform_posix.c` |
| *"jichi has zero notebook support"* | Confirmed: `grep -rc ipynb src/ include/` is still empty |
| the debt table's arithmetic | Confirmed against the tree as it stood at 298 drivers (299 once this milestone's own driver landed; the table is restated there) — and it exposed a gap of its own: **7 of 18 Verified rows cite a driver count**, so eleven rows have no computable debt at all. Now a row of its own |

**What was NOT re-checked, and is therefore as old as its last sweep.** The
factual claims inside the other ~46 rows. Several carry measurements from
2026-08-10 or 2026-08-17 sweeps and say so; several name a revisit *condition*
whose truth nobody has tested since it was written — and the M449 closure is the
warning here, where the unblocking condition ("if a buildroot toolchain becomes
available") had been met for some time and nobody had looked. A walk that claimed
to have verified fifty-eight rows in one sitting would be the overclaim this
register was built to prevent, so: twelve were checked, and this sentence is the
other forty-six.

### The second pass, over those forty-six (M658, same night)

The operator asked for the rest to be checked "as far as possible", so they were.
**Eleven more rows had a mechanically checkable claim; nine of the eleven were
wrong, and one of the nine was wrong in the other direction — the work had been
done and the row never noticed.**

| Row | What it claimed | What the check found |
|---|---|---|
| A seeded fuzz-lite harness for the pure cores | no such harness exists | **It exists.** `tests/fuzz/` — a deterministic seeded generator (xorshift32), a committed corpus, a libFuzzer front end, **19 targets**, and a lint already holding the documented count. Three of the four *named* cores still lack a target; that is the remaining work, and it is three targets, not a harness |
| Fukabori is thin on code, Annai is not | Fukabori's twelve chapters carry **one** block each | **1–4, mean 2.1**, with exactly two still at one — and those two are the two the neighbouring row argues should be. The inversion is gone |
| A diagram lint would encode a two-item allowlist | three chapters have no mermaid | **Exactly two**, chapters 1 and 11. Reason confirmed, not eroded |
| The system-prompt writer object | 38 appends in `build_parts`, 57 in the file | **38 and 81.** The number the cost rests on has not moved in 300 milestones; the decorative one rotted |
| The per-spec "stuck alone" line | 7 of 77 specs | **8 of 87** |
| The localized decks | one to two slides short | **one, two AND three** — the declared gap widened and the prose describing it did not |
| `PROJECT_TIMELINE` figures behind | ja 4; de·es·zh 22 | **ja 0** (M651 finished it) **and 21** for the rest. The Japanese half of that row is done |
| `telemetry` reads one log of many | 1 of 35 | **1 of 6** — M599's narrowing, visible in the count at last |
| A records assignment, `74-your-own-registers` | a free slot | **74 is taken** by `74-read-the-turn` (M627) |
| `CURRICULUM.md` states no scope boundary | zero matches, three phrasings | **Still zero, all three.** Corpus 505 → 524 files |
| Modern C / modern C++ appear zero times | `uint32_t` 0, `_Static_assert` 0, `std::span` 0 | **Holds — but only just.** A bare grep now returns 2 each, and **every hit is this register's own row or the analysis table reporting the zero.** The claim about the *teaching corpus* is intact; the measurement had started counting itself |

That last one is the reason this pass was worth running twice. A documentation
count must exclude the pages that report the count, or it re-counts itself — the
same species as the four withdrawn recommendations at M406/M407, where an ad-hoc
grep matched "the comment quoting its own subject". It cost one nearly-recorded
false finding, caught by looking at *where* the matches were instead of how many.

**Still not re-checked, and now a much smaller set:** the rows whose claims are
not mechanically decidable from this machine — a workload's measured behaviour, a
third-party corpus, an operator's statement, a judgement about what a learner
needs. Those are named in their rows and stay as old as their last sweep.

### What I recommend doing next, in order

*Rewritten 2026-09-19. The previous list had **four of its five items done** —
illumos compiled (M658), the Pi Zero re-run (M664), the three peer-transport caps
(M659/M661b) and `--strict-green` measured and its recommendation withdrawn — and
a recommendation list that outlives its recommendations is worse than none, because
it is read as current. Ordered by **value per hour**, with what stands in the way
named.*

1. **Build the language course, Python first.** Designed and its feasibility
   measured ([`plans/2026-09-language-course.md`](plans/2026-09-language-course.md)):
   the corpus snapshot works, retrieval answered a real question in **4 s** with a
   resolving anchor, and a grounded agentic turn quoted the tutorial in **27 s**.
   Nothing is blocked. It is the largest piece of user-facing value currently
   sitting at zero, and Racket follows the same shape once Python proves it.
2. **`doctor` should say that `maxParallelAgents` is 1 on this machine.** One
   warning, and it closes a real user-facing degradation: on FreeBSD and NetBSD a
   person gets one parallel agent on an eight-core box and is told nothing. The
   diagnosis is finished and the decision not to add a `sysctl` path still stands;
   what is missing is only the sentence that tells the user.
3. **The UI/UX tutorial and its bibliography**, treating documents as interfaces —
   *you are here, this is what you can do here*. It has a natural anchor in this
   tree already (the `describe`/`--help`/`doctor` surfaces are that idea applied to
   a CLI) and it makes the curriculum's writing work assessable rather than
   advisory.
4. **The six deferred languages in `BIBLIOGRAPHY.md`**, Racket and Python first,
   because the course points at them for "the important literature". One language
   per sitting; the discipline is already written down.
5. **Diagnose `lite_context_cap` on the Pi Zero and `preprompt_discard` on
   OpenBSD.** The last two red drivers on otherwise complete rows. The second has
   a refuted hypothesis recorded, which is the cheap half of the work already done.
6. **Drive the rows that have never called a model.** Termux, proot, Guix and the
   tiny rows have rigs and no live step; the pattern is now proven in three rigs
   and is a copy, not a design. **Decide the minimum driven task first** so the
   rows are comparable — that decision is itself item zero and costs minutes.

**And what I recommend *not* doing yet**, so the list is a judgement rather than a
wish: the **14 `qemu-user` architecture rows** cannot be driven at all — they link
`HAVE_CURL=`, so there is no HTTP in the binary, and driving them is a different
piece of work than a longer run; **macOS**, still blocked on hardware, where no
amount of willingness moves it; the **frontier craft A/B**, superseded by the
standing local-and-free-models rule and kept only because ~3.73M input tokens of it
were spent and lost; and a **blind sweep of the remaining bare-`make` sites**,
which is how a portability fix becomes a portability bug.

---

## Open — instrumentation and cost

| Deferred | Why | Where |
|---|---|---|
| **A workflow stage's model call is invisible to telemetry.** Measured 2026-09-17: a plain `-p` call adds one `model_call` event to the telemetry stream; the same call made from a workflow `synthesize` stage adds **nothing**, because that path goes through `jc_oneshot_ex` while the sink is wired into the agent loop. A multi-agent workflow's cost, latency and model attribution are therefore unmeasured, and `jichi telemetry` under-reported a strong model as one call when several were made. | The fix is not one line: `jc_oneshot_ex` has no app handle, so either the sink is threaded into it or the runner emits the event around it -- and the second risks double-counting the stages that DO go through the loop (`map`, `refute`). M643 fixed the model SELECTION and deliberately left the measurement gap open rather than guess at the shape. **Revisit when** a workflow is run for a measurement rather than for an answer; that is the first time the missing numbers are actually needed. | [ROADMAP M643](ROADMAP.md) |
| **The same craft A/B on a frontier model**, with a task whose deliverable is unstated and whose output a human grades. | M318 measured one 31B model and shipped the conclusion its evidence licenses (off under `--lite` only). The frontier case is where the section's claimed value lives. **Checked (M326b): no frontier model is reachable from this machine** — all five configured endpoints are HRZ-hosted (`jlu/gemma-4-31b-it`, `jlu/qwen3-coder-next`, plus embed/rerank), so this was genuinely resource-blocked and not merely unattempted.<br>**Operator's statement (2026-08-06): they will supply an API key with frontier-model access for this test.** So the blocker moves from *"no such model here"* to *waiting on the key* — and the entry is kept rather than closed, because the key is only **one of three** things the experiment needs. The other two do not arrive with it: a task whose deliverable is genuinely **unstated** (every graded curriculum task names its deliverable, which is exactly why M318's pass-rate result was uninformative), and **a grader who is not the author of the section under test** — blind pairs for the operator to grade is the clean form. **Harness and tasks built 2026-08-07** (M326g): `tests/bench/craft_ab/`, three unstated-deliverable tasks, blind pairwise grading, pre-registered in [proposals/2026-08-craft-ab-frontier.md](proposals/2026-08-craft-ab-frontier.md). What remains is the operator running and **grading** it.<br>**Run attempted 2026-08-10 (`session-01`): 18/18 errored on the key budget.** Two runs answered (~96k input) and consumed the key's remainder; sixteen then failed in under a second each with the gateway's own `429 budget_exceeded`, which named the key, the spend and the cap (machine-verifiable, and far more actionable than a bare 429). The 2026-08-07 pilot ran under a different key (`JC_DEV_KEY`, not on this machine). So the blocker is now **the key budget, not the harness**: reachability had been checked, the per-key budget had not — the M326b shape, again, in our own register. Unblock is either a budget raise/reset on that key (the operator report should carry the quota finding) or the pilot's dev key; then the session is one command (a fresh three-pair run under a new label, then the blinding step, then the operator grades). The over-budget key 429s **every** model including `jlu/*`, so ordinary jichi work on this key is blocked with it.<br>**Run completed 2026-08-10 (`session-02`, on the dev key the operator supplied): 18/18 runs `done`, zero truncations** — ~3.73M input / ~54k output on `anthropic/claude-opus-4-5`, ~20 minutes, preflight proving the arms differ (+1316 bytes in ON). The blinded pack was built, and **that pack is gone (checked 2026-08-22, M545).** `results/` is in `.gitignore`, nothing committed it, and the directory exists on no machine here — so ~3.73M input tokens of frontier data produced **no result**, because the one step a machine cannot do was also the slowest and the artifact did not outlive the wait. A writer produced something its reader could never read, and this row went on naming the path as "exactly one thing remaining" for twelve days. The spend is unrecoverable; the frontier question is **open again and now costs money to reopen**. M545 makes `blind` print that the pack is the only copy, with the one command that preserves it (archiving `grading/` alone keeps the blind, since the arm mapping lives in `.sealed/`).<br>**Superseded in practice (M545):** the operator's standing rule is local and free models only, so the frontier arm is not re-run. A **fresh pre-registration on `jlu/qwen3-coder-next`** asks the question that is actually actionable — the craft section ships **on** by default for every non-`--lite` model, that model is one, and nothing has tested it there. M318 measured a 31B (`jlu/gemma-4-31b-it`, no benefit) and this proposal registered a frontier class; the new run is neither, and says so. | [analysis](analysis/2026-08-06-craft-ab.md), [§7](analysis/2026-08-09-hrz-gateway-findings.md) |

## Open — the graded-attempt cost chain

The measured chain for one 1-point task is **128k → 66k → 29k → 9.4k tokens** (M309, M310,
M312). One lever is still a recommendation rather than a default.

> **Removed from this list (M325b):** *"make `--tool-profile core` the default for `attempt`"*
> carried the reason *"it costs the hint ladder"* — which **M319 and M320 measured away** (two
> models, 24 runs, zero `hint` calls, six of them failures with the tool advertised). M320 then
> refused the flip on a *better* reason and moved it to [`DECISIONS.md`](DECISIONS.md), but this
> row survived with the refuted argument still attached. A register carrying a reason known to be
> false is worse than one missing the row: it invites a future reader to re-open a settled
> question with a dead argument.

| Deferred | Why | Where |
|---|---|---|
| **Making `repoMap: false` the default for `attempt`.** *(reason replaced, M326 — the old one was false)* | The stated blocker was *"tasks 20–22 are explicitly about reading this repository"*. **They are not** — all three are self-contained fixtures, checkable by reading them. The measured reason: 18 runs found no pass-rate difference (4/9 vs 3/9) and a 15–62% token saving, but **+67% and +50% more model calls** without the map, which on the hardest task cancels the saving exactly (408k → 409k). And the leaner arm went 0/3 there — the second time after M320 that a leaner prompt goes 0/3 on the hardest task. To change the default, disprove the call inflation on tasks the model comfortably passes. | [analysis](analysis/2026-08-06-repomap-navigating-tasks.md) |

## Open — from the M321 large-workload measurement

A 34,216-event log from a private third-party workload
([analysis](analysis/2026-08-06-large-workload-telemetry.md)). Its four findings shipped as
M321 (transport diagnosis), M323 (compaction short-fall, observability only), M324 (the `glob`
gap) and M325 (`spawn_parallel`). **One item is left, and it is a design question rather than a
measurement** — which is why it outlived the others.

| Deferred | Why | Where |
|---|---|---|
| **Decide what jichi should DO when mid-turn compaction cannot reach its target.** M323 made the short-fall visible (event fields, a once-per-turn warning, a summary line); the behaviour is unchanged — the request still goes out over the configured `contextLimit`. | Three options, all lossy in different ways, and **the costs are not equal — checked (M326b), where the entry had implied they were**:<br>**(a) drop old messages** — `jc_history_drop_front` exists, and `jc_compact_find_cut` already knows how to snap a cut to a user-message boundary so a `tool_call`/`tool_result` pair is never split. *Cheapest: the machinery is there.* Loses work the agent may still need.<br>**(b) summarize mid-turn** — `summarize_call` is internal to `jc_compact.c`, so no new plumbing, but it means a model call inside a turn that is already over budget.<br>**(c) refuse the call** — **new code**: `jc_text_is_context_overflow` only recognises a *server's* rejection after the fact; there is no pre-send refusal path. Turns a degraded run into a failed one.<br>Picking one is a judgement about what is acceptable to lose, and wants a workload to measure against. **Checked (2026-08-10 sweep): the local telemetry cannot supply that workload -- 364 events since 08-07, zero compact events; the M321 third-party log remains the only pressured corpus.** **A local pressured corpus now exists (M459): 7 mid-turn compactions, 7/7 `unrelieved`.** It did not settle (a)/(b)/(c), and it changed the question. The run's short-fall was caused by an **under-declared window**, not by a genuinely full one: `contextLength` said 32000 while the server's real `max_model_len` was **256000** and it had been accepting ~160k-token requests throughout. So jichi compacted seven times toward a target it never needed, and M323's warning advised shrinking tool output when the fix was one config number. **Before deciding what to DO in this state, jichi now checks whether the state is real** — `last_prompt_tokens` is the server's own count for a request it ACCEPTED, so a served request larger than the declared limit proves the limit understates the model, and the operator is told that instead (`tests/smoke/context_underdeclared.sh`). That removes the commonest false instance of this row's condition from the population it has to decide about; a workload that presses a CORRECTLY declared window is still owed. | [COMPACTION.md](COMPACTION.md) |

## Open — gate integrity

| Deferred | Why | Where |
|---|---|---|
| **Flipping `--strict-green` on by default.** | M332 shipped it opt-in because the flip changes a currently-zero exit code (a stable interface) and the false-positive rate was unmeasured. **Measured (M343, retroactively from 138 existing journals): 0 downgrades in 21 scoped green runs** — the incidental lock-file FP never occurred; the two flagged runs ended non-ok, which strict-green ignores. Still deferred because the evidence is one project and one operator's gates, and the change is to a stable-tier contract — **the operator's call now, with a number instead of a fear**. Re-run `tests/measure/strict_green_fp.py` as corpora grow; a second project's corpus at 0 FPs is the natural strengthening. **Re-run (2026-08-10 sweep): 94 local journals, 16 completed runs, 0 with an edit scope -- nothing strict-green could downgrade either way; the M343 0/21 stands as the only number.** **Re-run (M459, a genuinely second and third project): jichi driven headless against **chrtext** and **zigodot**, each fenced to one named file — **0/2 downgrades**. Small, and said plainly: two runs, each a single-file documentation edit, so this strengthens M343's 0/21 without transforming it. Combined 0/23. **The more useful half of that re-run was a defect in the measurement itself:** the journal recorded only the *count* of edit-scope globs, so `--edit-scope AGENTS.md` and `--edit-scope '**'` were indistinguishable — and seven concurrent fleet runs, all `'**'`, would have contributed seven free zeroes to a rate that cannot be falsified. The journal now records `edit_scope_globs` and the script excludes vacuously-scoped runs from the denominator (older journals are counted as before rather than guessed at). A rate computed over fences that fence nothing is the shape of evidence this row was right to distrust.<br>**MEASURED AGAIN AT M662, AND THE ANSWER REVERSED.** The M657 recommendation below is **withdrawn**, and it is left in place because a withdrawn recommendation that leaves no trace gets re-proposed (M406/M407's rule). On the corpus as it stands — 91 journals, 63 completed runs, 42 with an edit scope, **35 ending `ok`** — strict-green would downgrade **16 of 35: a 46% rate**, against M343's 0/21 and M459's 0/2. Flipping the default would have failed nearly half of the operator's successful driven runs.<br>**And the classification says why.** Of 1,158 flagged paths (663 distinct): **35% downloaded or generated binaries** (an arxiv run's `.papers/cs_AI/*.pdf`), **30% build artifacts and temporaries** (`*.o`, `*.so`, `*.beam`, `test_0.tmp`), 10% jichi's own `.jichi/` state, 5% ignore files the run itself wrote — **85% is the work's own output**, and only 15% is source at all. M332's own words name the plausible false positive as *"an incidental shell-written file"* and the genuine one as *"the gate edited through the shell"*; **strict-green cannot tell them apart**, and that is the finding rather than the rate.<br>**Why 0/21 looked safe:** that corpus was this project's own tidy gates, and M459's two additions were single-file documentation edits. The moment jichi drives real work on another project, its build writes files.<br>**So: not the default flip, and not a `doctor --unattended` escalation either** — the second was additionally wrong on its own terms, since `--unattended` is a *doctor* flag and the escalation would have meant doctor DEMANDING a setting that fails 46% of real runs. **What the data asks for instead** is a rule that discriminates, and the sharp line is **tracked vs untracked**: a gate file is in version control, a downloaded PDF is not. The journal does not record trackedness, so that is the next step and the measurement's own output now says so. `tests/measure/strict_green_fp.py` classifies the paths as of M662, because telling a reader to "read the flagged paths" meant reading 663 of them.<br><br>*Withdrawn M657 recommendation, kept for the record:* **Recommendation (M657), because this row has said "the operator's call now" since M343 and a question nobody is asked has not been deferred, it has been dropped:** do **not** flip the global default. The objection stands — it changes a currently-zero exit code on a stable-tier contract — and 0/23 is a small denominator that would get cited as though it were large. Flip it **under `--unattended` only**, joining M158b's escalation set, which already holds `privilegedAudit: false` and, since M503, the private-files probe, for exactly this reason: an unattended run has nobody to read a warning, and its exit code is the whole interface. Interactive behaviour is untouched, so the contract a human depends on does not move; the runs where a silent hollow green actually costs something get the stricter rule. The measurement stays what it is (0/23, three projects, two of them single-file edits) and the change no longer needs it to be more. That is a milestone with its own teeth, and it is the operator's to take — but with a proposal in front of it rather than an open question. | [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §8b |
| **A gate rehearsal that proves a goal gate satisfiable** — run the verifier against a stub or hand-completed fixture and confirm green, then red without it (the curriculum's two-sided grader bar, ported to working gates; TEST_INTEGRITY recommendation #1 is its unit-suite sibling). | M343's declaration checks the red side for free (a declared goal must be red at start) but cannot prove the green side: that needs a *reference completion*, which only the operator can supply. The manual discipline is a standing rule (ANECDOTES #38: prove the gate green by hand first). **Revisit when** a run has a natural artifact to rehearse against — e.g. `attempt`'s reference solutions, or an operator-supplied stub patch. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) |

## Open — invariants known to be incomplete

| Deferred | Why | Where |
|---|---|---|
| **A writer object for the system prompt**, where every append names its section, so a section *cannot* be added anonymously. | Today `sum(parts) == total` catches a section appended after the last mark, and the zero-slot assertions catch most of the rest; an insertion between two *active* slots is credited to a neighbour. **Counted (M326b): the "~40 call sites" is accurate — 38 `jc_sb_append` calls inside `jc_sysmsg_build_parts` (57 in the file, counting the helpers).** **Re-counted 2026-09-18 (M658), and the halves aged differently:** inside `jc_sysmsg_build_parts` it is still **exactly 38**, so the number this row's cost estimate rests on has not moved in over 300 milestones; the file-wide figure is now **81**, and `mark()` is **18** rather than 16. The decision-relevant count held and the decorative one rotted, which is worth knowing about counts in general.** But the count overstates the *labelling* work: only 16 are section boundaries, which is exactly what the existing `mark()` calls already are.** So the real cost is 38 mechanical call-site changes to route through the writer, not 38 decisions — weaker than the entry claimed, and still not obviously worth converting a misattribution in a diagnostic report into a compile error. | [jc_sysmsg.h](../include/jc_sysmsg.h) |

## Open — what a gateway charges that jichi cannot see (M663)

| Deferred | Why | Where |
|---|---|---|
| **`doctor` cannot see per-character or per-image pricing**, so a model that bills either reads as free. | Found 2026-09-18 while inventorying the HRZ gateway for speech and image work. `jlu/tts-1-hd` declares `input_cost_per_token: 0.0` **and** `input_cost_per_character: 3e-05`; `doctor`'s check is on *missing* `inputCostPer1M`/`outputCostPer1M`, and `CLAUDE.md`'s own free-namespace rule reads the per-**token** fields. So a per-character biller satisfies every signal this project uses for "free". Pointing the other way, all 81 `openai/*` image models publish `input_cost_per_token: 0` because image models bill **per image** — a zero that means nothing, and an easy way to talk oneself into a priced run. **Not fixed here because the shape of the fix is a judgement:** jichi could read the extra LiteLLM fields (which are a gateway's schema, not the OpenAI API's), or `doctor` could say *"this model publishes a cost field jichi does not price"* without pretending to total it. The second is honest and cheap; the first invites jichi to track someone else's billing schema. **Revisit when** the HRZ admins say what `jlu/tts-1-hd` actually costs — the operator is asking — because a real charge makes this urgent and a LiteLLM default makes it a documentation note. | [analysis/2026-09-18-local-media-and-the-illumos-live-turn.md](analysis/2026-09-18-local-media-and-the-illumos-live-turn.md) §1 |

## Open — tool-output cost

| Deferred | Why | Where |
|---|---|---|
| **Tighten the per-tool output caps automatically when a 0% cache hit-rate is measured.** | jichi knows the hit-rate after a few calls (M326w) and the `--lite` caps already exist, so the pieces are there. **Not done because it changes tool behaviour mid-session based on a statistic** — a `read_file` returning 200 KB yesterday and 32 KB today, for reasons invisible in the config and hard to debug from the outside. `doctor` advises instead. **Revisit when** there is a way to make the adaptation *visible* at the point it happens (the truncation notice naming the reason, not just the byte count). | [TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) §7 |
| **A `doctor` check for a high re-read ratio.** *(reviewed M503 and deliberately NOT built: the row's own argument still holds -- doctor's advice names a lever the reader can pull, and the lever here is a prompt or a skill doctor cannot check was applied. It needs a second measurement, not a feature.)* | Measured at **72%** in one workload — 2,056 `read_file` calls over 584 distinct paths, one path read 216 times — which is a loop: compaction elides the read, the model re-reads, compaction elides it again. Computable from telemetry exactly as the M316 unused-tools check is. **Not done because it is advice about the AGENT's behaviour, not the operator's configuration**, and doctor's other advice all names a lever the reader can pull. The lever here is a prompt or a skill, which doctor cannot check was applied. **Revisit when** a second workload confirms the ratio is high generally rather than specific to one agent on one codebase — **and measure that workload post-M348**, which attacked the loop mechanically: the elision marker is now a claim ticket naming a preservation-store path, so the re-read the loop consists of has a cheap targeted substitute. If the ratio collapses, this row closes without doctor ever advising. **Measured (M459, post-M348, first pressured corpus): 0% — 14 `read_file` calls over 14 distinct paths, no path read twice**, on an unbudgeted read-heavy run over 16k lines of a real codebase that compacted seven times while doing it. Directionally this is what M348's claim ticket was for: the run read every module once, under the very pressure that is supposed to cause the loop, and never went back. **It is NOT evidence and this row stays open**: 14 calls against a 2,056-call reference cannot confirm or refute a 72% figure, and quoting "0%" as a refutation would be the overclaim this row was written to avoid. The measurement is now a committed script, `tests/measure/reread_ratio.py`, which carries a 50-call floor and prints NOT EVIDENCE below it — so the next person to run it cannot accidentally close this row with a handful of doc edits. | ROADMAP M326z |

## Open — compaction

| Deferred | Why | Where |
|---|---|---|
| **A mid-turn mechanism for turns eliding cannot save.** **Measured at M588 — the pressured corpus finally exists, and it argues against building the mechanism first.** | The row's revisit condition was *"a workload's `unrelieved` share measured on a post-M326y log"*, and the 2026-08-10 sweep found **zero** `compact` events to measure. An overnight autonomous run on a 9B model at a **65,536** window produced one: **29 mid-turn passes, 100% pressured, 27 of 29 (93%) UNRELIEVED, 28 SHORT**, and reclaim that was **100% lossy** (`dup=0`) because the zero-loss dedup found nothing to dedup. So turns eliding cannot save are real, and on that configuration they are almost all of them.<br>**But the control run says the mechanism is not the first lever.** The same jichi, the same envelope shape and near-identical per-call input (~43k vs ~48k) against a **196,608** window: **2 compactions, both zero-loss, zero pressured.** The arithmetic is the whole story — system 11,536 + tool definitions 4,732 = **16,268 fixed tokens before any history**, which is 25% of a 65k window and 8% of a 196k one. Elision can only touch history, keep-recent protects the newest of it, so the pass ends above the line and re-triggers. **Mid-turn summarization would be treating a symptom of mis-provisioning.** The cheaper levers, in order: size the window to the work, then cut tool output (`readMaxBytes`), then prune tool definitions. **Revisit the mechanism when** a workload presses at a window that is already generously sized — this corpus is not that. | [proposals/2026-08-observability-seams.md](proposals/2026-08-observability-seams.md) |

## Open — lessons that become checks (M602)

| Deferred | Why | Where |
|---|---|---|
| **`hook:` bullets under `## Checks` — a lesson committed as a `PreToolUse` hook.** | A hook is a shell command in `config.json` behind `hooksEnabled: true`, top-level only, that can block a tool with exit 2 (`HOOKS.md`). It is the stronger bridge from a lesson to a refusal — arbitrary predicates, not the constraint scanner's eight phrasings — and exactly for that reason letting the learning loop write one is a larger trust decision than committing a constraint: the loop would be authoring code that runs on every tool call. M602 counts such bullets as *unsupported* and says so in the apply summary. **Revisit when** a real draft proposes a check the constraint vocabulary cannot express and a human would have written the hook by hand anyway — then the design is a `## Checks` kind that writes a script under `.jichi/hooks/` and prints the `config.json` lines for the human to paste, never enabling it itself. | ROADMAP M602, [analysis/2026-08-27-the-language-of-lessons.md](analysis/2026-08-27-the-language-of-lessons.md) D4 |

## Closed at 2026-09-19 — parallel_abort on the BSDs

| Was deferred | Closed by |
|---|---|
| ~~**Diagnose `parallel_abort` on the BSDs.**~~ **DIAGNOSED AND CLOSED 2026-09-19 — and it was never an abort defect.** Instrumenting the real driver on the FreeBSD guest showed **two** jichi processes where Linux has three: the second child was never forked, so the mock was innocent and so was the abort path. `jc_parallel_eff_max` caps workers at `jc_cpu_count()` when no config max is set, and on FreeBSD/NetBSD that is **1** — not because the guest has one CPU (`sysctl hw.ncpu` says 2) but because `_SC_NPROCESSORS_ONLN` is hidden behind `__BSD_VISIBLE` while this tree compiles `-D_POSIX_C_SOURCE=200112L`. Measured on the guest: the same source prints `2` under default flags and `NOT DECLARED` under the tree's. **`src/platform/jc_platform_posix.c` had predicted this consequence in full, in a comment, since M459** — *"spawn_parallel runs one child"* — and nobody had connected the prediction to the red driver. The driver now pins `maxParallelAgents: 2` in its fixture, so it measures abort and reaping, which is its subject, rather than `jc_cpu_count()` by accident. Green on FreeBSD. |

## Open — the one OpenBSD stop that is not a text tool (2026-09-19)

Eight of the nine drivers failing on OpenBSD were GNU-isms in the tier's own
patterns and are fixed. This one is not, and it is recorded with what was
measured and what was **refuted**, rather than with a guess.

| Deferred | Why | Where |
|---|---|---|
| **`preprompt_discard` check 1 on OpenBSD: the discard is not ANNOUNCED.** | **The safety property holds** — check 3 passes, so type-ahead entered before the first prompt is still discarded and never becomes a prompt. What is missing is the notice. The mechanism is `enter_raw`, which calls `input_pending()` — a zero-timeout `select()` on the tty **before** raw mode is set — and announces only if it sees a byte; `TCSAFLUSH` then discards whatever is there regardless. So on OpenBSD `select()` reported nothing readable at that instant while there evidently *was* input to flush. **Refuted by measurement, not assumed:** the obvious explanation was canonical-mode line buffering, i.e. that `\r` does not terminate a line without `ICRNL` — sending `\n` instead fails identically. What remains is a genuine timing or pty-semantics difference that has not been isolated, and the announcement is best-effort by construction: it can only report input that had already arrived when the probe ran. | `src/tui/jc_term.c:134`, `tests/smoke/preprompt_discard.sh` |

## Open — one CPU on the BSDs, and what users get because of it (2026-09-19)

| Deferred | Why | Where |
|---|---|---|
| **`maxParallelAgents` silently defaults to 1 on FreeBSD and NetBSD.** | Not a bug and not harmless: `_SC_NPROCESSORS_ONLN` is a widely-copied extension the BSDs hide behind `__BSD_VISIBLE`, so under this tree's `-D_POSIX_C_SOURCE=200112L` the identifier is undeclared and `jc_cpu_count()` returns 1. M459 chose that deliberately over widening the feature-test macros everywhere or adding the tree's first `#ifdef __FreeBSD__`, and **that decision still looks right**. What is missing is the user-facing half: a person running jichi on FreeBSD gets **one** parallel agent on an eight-core machine and is told nothing. **OpenBSD is unaffected** — it declares the symbol under POSIX flags, which is why its `parallel_abort` passed while the other two BSDs' failed. Candidate fix, cheap and honest: `doctor` warns when the count is 1 and names `maxParallelAgents` as the override. Rejected for now: a sysctl path, for M459's reasons. | `src/platform/jc_platform_posix.c:585` |

## Open — the four programmes raised 2026-09-18

Raised together by the operator. One is designed
([`plans/2026-09-language-course.md`](plans/2026-09-language-course.md), with its
feasibility measured before it was written); the other three are recorded here so
they are a register entry rather than a memory.

| Deferred | Why | Where |
|---|---|---|
| **Learning a language with jichi, from its official tutorial.** | **Designed, not built.** Feasibility measured first: the Python 3.14 text archive is 537 files / 16 MB, the tutorial alone **17 files**; `jichi docs search` answered a list-comprehension question in **4 s** with a resolving `file:line` anchor, and a grounded agentic turn quoted `controlflow.txt` in **27 s**. Decisions fixed: local snapshot + manifest, deterministic committed graders (the model personalises framing only), ships inside jichi, and the learner-facing tutorial is written against **today's** jichi so every published command is one that runs. | [`plans/2026-09-language-course.md`](plans/2026-09-language-course.md) |
| **Complete the bibliography for the six deferred languages** — Racket, Guile, Elixir, Haskell, Clojure, Python. | One language per sitting, official sources first, every entry checked with the date it was checked, `bibliography_lint`'s floor raised as each lands. **Racket and Python first**, because they are what the language-course design points at for "important literature and papers" — the two pieces of work feed each other. | `docs/BIBLIOGRAPHY.md` §"What is deliberately absent" |
| **A tutorial for grounded discourse with a model** — citations, cross-references, argumentation theory; on documentation, literature and source. | Should be built on [`plans/2026-09-argumentation-program.md`](plans/2026-09-argumentation-program.md), whose central move fits exactly: take reasoning **down a tier**, from prose in a prompt to an **artifact a script can check**. So the tutorial should produce an artifact — claim, evidence that resolves, warrant, counter-argument, revision — rather than give advice about good discussion, which makes it gradeable. Topics named: how an agent supports a self-learner in software development, language learning, writing, reading texts/papers/source, comprehension, analysis, editing, and giving feedback to other writers. | undesigned |
| **Music creation, robotics and game development for self-learners and advanced users.** | **Game development first** (operator's choice): the zigodot precedent exists and the driving surface is solved. Each domain needs **two** surfaces and the second decides whether this is engineering or a demo: a *driving* surface (how jichi acts — the Godot protocol, a board over ssh, a score format) and a *verification* surface (what "it worked" means — a scene loads, a program runs in simulation, a score renders to audio a test can measure). Robotics has hardware here (UNO Q, `hardware bench machines`); **music has neither surface today**, and its verification surface is the open problem. | undesigned |

## Open — teaching and documentation

| Deferred | Why | Where |
|---|---|---|
| **An `init` option that scaffolds the records tree as `.org` instead of markdown.** | The format itself is a one-line change in the scaffold tables; the *cost* is that shipping `.org` assets pushes users toward one editor, which is exactly what M326s decided against. **Revisit when** two users ask for it — an observable trigger, per this page's own rule, and cheap to honour once someone has. (Deliberately described without inventing a flag spelling: `docs_flags.sh` scans this page — unlike `DECISIONS.md`, which is excluded — so naming a switch here would document one that does not exist. The first draft did, twice: once in the entry and once in the note explaining why not to.) | ROADMAP M326s |
| **A graded assignment for the records practice.** | Designed as `74-your-own-registers` and dropped at M326s — **note (M658) that the number is now taken: task 74 is `74-read-the-turn` (M627), so acting on this row means choosing a free number, not resurrecting that name** — the checker could only grade the *shape* of a register — it cannot know whether a decision was real, whether the dates are true, or whether `Where:` points anywhere. **Revisit when** there is a way to grade the habit rather than the headings; a fixture check is not one, and adding it would cost two count bumps, a grader entry and an INDEX row for a check nobody should trust. | [DECISIONS.md](DECISIONS.md) |
| **A seeded fuzz-lite harness for the pure cores** (`jc_patch`, `jc_utf8`, `jc_jsonrepair`, `jc_glob_match`): a tiny C89 LCG with fixed committed seeds, so "random" inputs are byte-reproducible and every failure becomes a permanent regression case. | **Substantially overtaken, M658.** The harness the row asks for **exists**: `tests/fuzz/` holds a deterministic seeded generator (`jc_fuzz_main.c`, xorshift32, "no dependencies: a seeded PRNG + a mutation loop over each target's seed"), a committed corpus, a libFuzzer front end, and **19 targets** — and `docs_counts_lint` check 17 already holds the documented count to `JC_FUZZ_TARGETS`. So "a tiny C89 LCG with fixed committed seeds" is shipped. What is *not* covered is three of the four pure cores this row names: **`glob` has a target; `jc_patch`, `jc_utf8` and `jc_jsonrepair` do not.** That is the remaining work, and it is three targets rather than a harness. Original reason, kept because it is why the harness came later and by another route: The 2026-08-10 procedural-generation determination: the pure cores' current failure findings come from real workloads — the honest source — and a fuzz harness deserves its own milestone with its own teeth (a generator that has never found a planted bug has never been seen working). **Revisit when** a pure-core defect ships that seeded input generation would plausibly have caught. | [analysis/2026-08-10-guidance-and-crown.md](analysis/2026-08-10-guidance-and-crown.md) |

## Open — JupyterHub and notebooks (M478)

| Deferred | Why | Where |
|---|---|---|
| **`read_file` renders an `.ipynb` to cells**, the way M42's PDF path renders a PDF (detect the extension, transform to text, never mark it read-before-edit because it is not editable that way). | jichi has **zero** notebook support (`grep -rc ipynb src/ include/` is empty), and M478 measured what that costs: a notebook with one figure is **~65,631 tokens** — over the 256 KB read cap, so truncated as well — against **~255** for the same code as a jupytext-paired `.py`. **257×.** The fix is modest and in character. **Not started because the use case is undecided:** the request that prompted M478 was a loose "can this be used with JupyterHub?", and the answer to "do your learners live in notebooks?" is not yet known. **Revisit when** someone states that notebooks *are* the workflow — the 257× figure is the argument, and [JUPYTERHUB.md](JUPYTERHUB.md) §14 is the decision aid. Full cell-**editing** is recommended against separately: a new corruption class in exchange for something `jupytext` already delivers. | [JUPYTERHUB.md](JUPYTERHUB.md) §4 |
| **The browser half of the terminal contract** — whether xterm.js or the browser wins for the keys jichi binds (Ctrl-R against page-reload, Ctrl-G against Firefox's find-next), plus browser paste and browser resize. | M478's rig speaks terminado's **websocket**, so it tests jupyter-server completely and xterm.js not at all — a websocket cannot press a key in a browser. This is **not** a deferral for cost reasons: it is ten minutes with a browser, and the nine-item checklist already ships with `scripts/jhub-verify.sh`. It is deferred because it needs a **human at a display**, which no rig here can be. **Revisit when** anyone runs the course, or sooner — it is the cheapest open item on this page. | [JUPYTERHUB.md](JUPYTERHUB.md) §13 |
| **A German *Einfache Sprache* counterpart** for the JupyterHub learner section. | [`PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) has a German **original** ([i18n/de/EINFACHE_SPRACHE.md](i18n/de/EINFACHE_SPRACHE.md)) — *Einfache Sprache* is a defined register with its own rules, and the English page is its sibling, not its source. The new section was written in English first, which inverts that relationship for this one section. The phased-i18n policy makes English canonical, so this is a known cost rather than a defect. **Revisit when** a German-speaking cohort actually uses a hub — and note it needs a **writer of the register**, not a translation pass. | [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md) |
| **Shape D: the web bridge behind `jupyter-server-proxy`.** | `examples/web-bridge/bridge.py` already uses "the Jupyter model" (a boot token) and binds loopback; a hub could put its own authentication in front of it at `/user/<name>/proxy/<port>/`, supplying the one thing the bridge deliberately lacks — real accounts. Attractive, and **entirely unrun**. Two specific unknowns: whether Server-Sent Events survive the proxy without buffering, and whether the bridge's own token still earns its place once the hub authenticates. **Revisit when** someone wants a browser UI rather than a terminal; until then the terminal is the recommendation and costs nothing. | [JUPYTERHUB.md](JUPYTERHUB.md) §3 |

## Closed at M659 — two of the three peer-transport buffers

**M657 refuted the blocker; M659 did the work.** The rows had said for fifty
milestones that a born-red test needed "a memory-pressure harness the smoke tier
does not have". RSS was never the property: each fix makes the bound
**observable**, and what the driver reads is the refusal, not a byte count. One
convention came out of it and is now a decision — *a cap that says nothing is a
cap nobody can test, and nobody can debug either* — carried by
[`include/jc_peercap.h`](../include/jc_peercap.h), which holds the single number
both readers use and the argument for saying it out loud.

| Was deferred | Closed by |
|---|---|
| ~~**The MCP stdio `rbuf` line buffer is bounded only by a 120 s deadline.**~~ **DONE, M659.** | Capped at `JC_PEER_LINE_MAX` (8 MiB, `include/jc_peercap.h`), and the refusal **names the cap in bytes**. `tests/smoke/peer_line_cap.sh`, 4 checks, proven red: without the cap the reader blocks to its deadline (75 s against 13 s) and reports *"server closed the connection"* — the message that is indistinguishable from a network stall, which is what the naming replaces. | `src/mcp/jc_mcp_stdio.c` |
| ~~**The ACP `inbuf` line buffer has no byte cap and no deadline.**~~ **DONE, M659 — with its red still owed, and that is stated rather than glossed.** | Same cap, same message; the buffer is **discarded** rather than flushed, because the eof path hands a final unterminated line to the caller and flushing it would pass on the very megabytes the cap refuses. **What is not done is a driver.** This path has no deadline, so the MCP fixture's argument — latency — does not transfer, and the only observable is the message. That is precisely why the cap speaks; writing the driver that reads it is the remaining work and it is small. | `src/acp/jc_acp.c` |

## Closed at M661b — peer transport shutdown, and the M609 family complete

The third and last row of the family M609 opened. All three are now done, and the
pattern across them is one sentence: **every one was blocked on a claim about its
test, and every claim was wrong.** Two were "this needs a memory-pressure
harness" (M659); this one was "deterministic but fiddly in POSIX sh", which was
fair about the difficulty and wrong about the obstacle.

| Was deferred | Closed by |
|---|---|
| **MCP and LSP shutdown block in `waitpid(pid, …, 0)` after `SIGTERM`.** | **M661b.** All three sites now use `jc_worker_reap_grace(pid, JC_WORKER_TERM_GRACE_MS)` — the helper the parallel pool and the daemon already used, which the row itself named as the drop-in and was right about. `tests/smoke/peer_reap_grace.sh`, 3 checks, proven red: **45 s against 0 s** on the same fixture. **The fixture took three attempts and the first two measured nothing**, which is the part worth keeping: a hand-rolled mock deadlocked the *request* path (jichi in `select()`, the mock in `read()`) while the driver reported 45 s as though it had measured shutdown, and a second version was defeated by a lingering `sleep` holding jichi's pipe open so the wait was for EOF rather than for the child. The working mock is **derived from the tier's own `smoke_write_mock_mcp`** with exactly two changes — `trap '' TERM` and `exec >&- 2>&-; sleep` — because the protocol half was already proven by three other drivers. The LSP site takes the identical one-line change and is **not** covered by a driver; that is stated in the driver's header rather than implied. |


## Open — fence hardening and isolation

| Deferred | Why | Where |
|---|---|---|
| **A shell/interpreter sandbox** — real OS-level containment of what `run_terminal_command` (and any program it launches) can read and write. | This is the project's oldest safety deferral, and until now it was **invisible from the register built to make deferrals findable** — it lived only in ANECDOTES #12 (*"a fuller shell sandbox is deferred"*) and `proposals/2026-07-privileged-commands.md` §Deferred, both predating this page's M298 coverage window. Recorded here so it is findable. **Checked (M326b):** no `seccomp`/`bubblewrap`/`firejail`/`unshare`-as-sandbox exists anywhere in `src/` or `docs/`; the file-tool fence (incl. `search_code`, M383) does not cover the shell, and M83 detection is writes-only / in-git-tree-only / post-hoc (GATE_INTEGRITY.md §9.1). **The honest position is that this is not a C feature:** a userspace heuristic cannot contain a determined program, so the real answer is deployment — run as a non-root user, in a container/VM (DEPLOYMENT.md §5, AUTONOMOUS_LOOPS.md's systemd unit). **Revisit as** a *documented `bwrap`/`unshare` launcher recipe* wrapping the whole process (not per-tool), and — separately and only behind an explicit opt-in — the heuristic path-screen of GATE_INTEGRITY.md §9.3-C, which is defense-in-depth with a stated miss list, never a wall. | [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §9 |
| **The path fence's check-then-open window** (M472, audit L3). `jc_path_in_root` resolves with `realpath()` and returns a verdict; the `open()` happens afterwards, so a path component swapped in between is a classic TOCTOU. | **Analysed and left, with the reasoning stated, because the threat model makes it near-worthless to fix.** The attacker who could win the race is a process running concurrently as the same user -- in practice the model's own background shell. But a model with shell access does not need to race the fence: it can write the file directly, which is [the shell-sandbox deferral](#) above and this project's oldest safety gap. The fence is the ONLY door in exactly one configuration, `--edit-scope --strict-scope`, which forbids `run_terminal_command` -- and there, by construction, there is no concurrent attacker to swap anything. So the window is reachable only where it does not matter, and closed where it would. **The cost of fixing it anyway is not small:** the file tools go through stdio (`fopen`), which has no `O_NOFOLLOW`, so it means converting the file I/O layer to `open()` + `fdopen()` plus an `fstat` re-check across five libcs and three non-Linux kernels -- a portability risk taken for a threat the same configuration already answers. **Revisit when** a sandbox lands (which changes the concurrency assumption), or when a file tool gains a caller that runs while an untrusted process shares the workspace -- a multi-tenant daemon would be the shape. | [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) L3 |
| **The popen path's descriptor total** (M472). Every child jichi forks and execs itself now inherits stdio and nothing else -- `jc_proc_child_close_fds()` closes the range, `jc_fd_cloexec()` marks the sinks and sockets at creation, and both are pinned by `posix_utils_lint.sh` checks 6/8 and `tests/smoke/child_fds.sh`. A command run WITHOUT a `timeout` goes through `jc_proc_popen`, where the fork happens inside libc, so no jichi code runs between fork and exec and the close-range backstop structurally cannot reach it. | **One pipe pair still arrives there, and it is libcurl's, not jichi's** -- created without `O_CLOEXEC` in curl's internals, where jichi has no hook (`CURLOPT_SOCKOPTFUNCTION` covers sockets, not pipes). Measured by strace: jichi's own pipes are all marked, libcurl's is not. The three descriptors with a demonstrated exploit -- the run journal, the telemetry sink, the provider socket -- are closed on BOTH paths, and this residual is not a sink, a socket or a secret; a child can hold it open or write bytes nobody reads. Recorded rather than papered over, because `child_fds.sh` deliberately asserts the total on the fork/exec path only, and a reader of that driver should know why. **Revisit as** routing model-issued commands through jichi's own fork/exec path unconditionally (the `timeout` argument already selects it), which would retire `jc_proc_popen` for tool execution and make the total hold everywhere -- a behaviour change with its own milestone, not a line in this one. | [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) §H2 |
| **Mid-run one-off fence exceptions** — an interactive "grant read of this path for the session?" prompt when a file tool hits the fence (D1), designed but unbuilt. | Designed in the item-7 proposal, not built this batch. **Reads only, interactive front-ends only** (a human answers); writes stay a pre-run decision (rollback cannot undo an out-of-tree write) and are the proposal's D2 deferral; the silence policy is settled (no timeout — structural, D4). The full implementation design — decisions with rejected alternatives, the safety invariant, the negative-test-first sequencing — is [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md). **Revisit when** a workload shows the occasional-unforeseen-external-read case is common enough to beat re-launching with `--reference-root`. | [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md) |

## Narrowed at M582 — localized presentation decks

| Deferred | Why | Where |
|---|---|---|
| **Bringing the four localized decks up to the English decks' CONTENT** (de · es · ja · zh). Four decks are one to two slides short: `00-super-features` (−1), `03-roadmap` (−2), `04-university` (−2), `05-school` (−1), in every language. **Re-read from the markers 2026-09-18 (M658): the gap has widened to one, two AND three** — sixteen `slides-behind` markers now read 8×1, 4×2, 4×3. The declaration mechanism did its job (the gap is visible and bounded); the prose describing it did not move with it, which is the one thing a declared-gap design cannot check for itself. | **The figures half is done (M582)** and is now gated. What is left is prose: the English decks gained sections, and translating them is writing in three languages this repository cannot review. Each of the sixteen files declares the gap with `<!-- slides-behind: N -->`, the count is checked, and the declaration re-fires when the English deck moves again — so the gap is bounded and visible rather than silent. **Revisit when** a reviewer for that language exists (see the row below). | [i18n/README.md](i18n/README.md) |
| **`PROJECT_TIMELINE.md`: figures behind the M579 recount** — **ja is down to 4** (M587); de · es · zh remain at **22**. **Re-read from the markers 2026-09-18 (M658): ja is at 0** — M651 carried the last numerals across — **and de · es · zh are at 21, not 22.** The Japanese half of this row is therefore *done*; what is left is three languages with no reviewer, which is the row below. | M587 brought across everything in the Japanese page that is a *pure numeral*: the 17-slice subsystem pie chart, the tool count inside its label, and five summary rows. The four that remain are each welded to prose that would become false — the test row's breakdown changed shape in English, and the proportion and commits-per-day tables are each followed by a paragraph that *interprets* them (English now argues "documentation now outweighs source"; the daily table was recounted, Jul 24 is 384 not 378, and extended by a month). Redrawing bars is arithmetic; the prose around them is not. **Revisit when** a Japanese writer can take the page as a unit. de/es/zh were left untouched deliberately — the same split applies and none has a reviewer. | [i18n/README.md](i18n/README.md) |
| **`docs/i18n/ja/PROJECT_TIMELINE.md` owes a corrected third-party row** (deleted at M587, not rewritten). | The row said the bundled cJSON is *"not authored by this project"*. That is **false**, and it contradicts [LICENSING.md](LICENSING.md), the README and the English page, all of which state `src/json/cJSON.{c,h}` is original code (M171) — **and LICENSING.md's argument that the licence choice is unconstrained rests on exactly that**. The translation was *faithful when made*: English carried the same claim until **M498 (2026-08-20)**, four days after this page's tracked commit, and the correction never propagated. That is the failure `tracks:` exists to expose and `i18n_tracks_lint` now gates. Deleting a false claim needs no Japanese; writing a true one does, so the row is **gone and owed**. **Revisit when** a Japanese reviewer can add the corrected sentence. | [i18n/README.md](i18n/README.md) |
| **Native review of the es · ja · zh decks and prose.** | M582 corrected **numbers only**, on the maintainer's explicit instruction, because a wrong number is wrong in every language while phrasing is not auditable without a reader. Two edits sit at that boundary and are named rather than hidden: the four decks' figures now read `10,000+` / `11,000+` where English says "over" (a `+` sign was chosen precisely so no "more than" word had to be invented in three languages), and `docs/i18n/zh/PHILOSOPHY.md`'s CJK numeral `四千五百多` became `一万多`. Both are numerals; neither has been read by a native speaker. Japanese has a route — `llm-jp` as a first-pass reviewer (M580), then the maintainer's friends. Spanish and Chinese have none.<br>**Underway since 2026-09-18 (M657): the Japanese review has a real native reader**, and her first impression is recorded rather than acted on, because it is a first impression and the review is not finished. Two findings so far. (1) **The register**: the pages read *formal, not casual* — a choice nobody here ever made deliberately, now stated as one in [i18n/README.md](i18n/README.md) §Policy item 4, with the argument on both sides and the per-genre question left to her rather than settled by us. (2) **The English idioms**: terms like *dogfooding* need the metaphor explained, not just the usage — nine are now glossed in [VOCABULARY.md](VOCABULARY.md), and the ten tutorials that pointed at that page **nowhere** now point at it. What is still owed to Japanese specifically: there is **no `i18n/ja/VOCABULARY.md`** at all, so a reader of the Japanese pages meets the English jargon with the glossary in the other language. That is a translation, and it waits on the same reviewer. | [i18n/README.md](i18n/README.md) |

**What the old row got wrong, kept because it is the same failure this section is about.** It listed the stale figures as ``107``, ``~770 KB``, ``~85,000`` and ``~700 KB``, "verified by grep". Re-measured at M582: **only `~700 KB` was still present**; the other three had been gone for milestones. A deferral row that cites grep evidence rots exactly like the documentation it describes, and nothing checked it either. The row also recorded *"deliberately NOT linted: the parity check would have to parse four languages' number prose"* — that premise was false and cheap to test, and the lint that does not parse prose is [`tests/smoke/i18n_tracks_lint.sh`](../tests/smoke/i18n_tracks_lint.sh); see the M582 row in [DECISIONS.md](DECISIONS.md).

## Closed at M499 — documentation owed to the self-learner

The four rows here were the milestone-sized items from
[analysis/2026-08-12-docs-review.md](analysis/2026-08-12-docs-review.md) (four
reviewers, 30 pages, one rubric). All four are done — and **one of them had
already been done and the row had rotted**, which is the M326b rule earning its
place again: check the checkable part of a reason before acting on it.

| Was deferred | Closed by |
|---|---|
| A "where your state lives" page, and the source-checkout prerequisite | [STATE.md](STATE.md) — all 15 `~/.jichi.d/` subtrees plus the file INSTALL's table omitted (`~/.jichi.env`, the one holding a secret), what is irreplaceable, and the exact `make install` manifest with the consequence stated: the course lives in the tree, not in the binary. |
| The tool-call decision chain, as one page | [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — the nine steps in the order the code runs them, read out of `jc_agent.c` rather than summarised, including the three that surprise readers: an ALLOW verdict does not mean it runs, your "yes" is not the last word (the scope fence and the hook come after it), and headless refuses an ASK rather than proceeding. |
| Five tutorial-shaped sections inside reference pages | Written, and pinned by `tests/smoke/self_learner_lint.sh` check 3 — a tutorial section inside a reference page is the least defended documentation there is, and deleting one was a two-line diff nobody would question. |
| The curriculum's lone-learner gaps | **Four of the five had already shipped** at M396/M406 and the row had not been updated: the skip rule is stated in `assignments/INDEX.md` ("the margin is exactly one 3-point task… leave it, go forward, come back later"), the four `cc`-needing specs carry prerequisite boxes, and CURRICULUM §Who it is for already routes both the process track and `PLAIN_LANGUAGE.md` and names the four INSTRUCTOR sections a lone learner should read. What was genuinely missing was the **cross-reference from the map**: a learner reading "gate: 14/17" saw a threshold, not a permission. One paragraph, now in CURRICULUM.md. |

Also closed with them: register item 15, *definitions of the load-bearing words
before use* — [VOCABULARY.md](VOCABULARY.md), **58 terms** (48 when that review closed; nine
English idioms were added at M657 and one had drifted uncounted), with the 24 the review and
the smoke tier force on a reader pinned by check 4. And the **name trap**: a reader
looking for "what does posture mean" opened `docs/GLOSSARY.md` and found a config
page (the `docs/DOCS.md` shape), so that page now carries a sign in front of it,
pinned by check 5.

**Still open from that review, and deliberately:** the items whose value is a
judgement rather than a structure — item 18's promised user-story tutorial, item 19
(`jichi assignments` as an orientation, which is the row below), and the prose-level
findings the register carries. A lint can hold a heading in place; it cannot make a
page good.

## Open — assignment and grading support

Found by the M398 workflow review of the assignment/tutoring documentation; the
workflows themselves shipped, these are the product gaps they had to document
around ([TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) § "What this feature
does not yet support well").

| Deferred | Why | Where |
|---|---|---|
| **A cohort view for teachers** — one command that reads many learners' `progress.jsonl`. | `jichi assignments` reads exactly one workspace, so thirty students are thirty benches with no aggregate; the documented answer is a shell loop collecting `--output json`. **Not done because it is a gradebook**, i.e. someone else's software: it needs identity, storage and a policy about grades, none of which belong in a coding agent (the M165 web-frontend reasoning applies — jichi provides the machine surface, a sidecar owns the aggregation). **Revisit as** a documented recipe or an `examples/` script rather than a subcommand. | [SCRIPTING.md](SCRIPTING.md) |
| **A per-spec "if you are stuck alone" line** — present in 7 of 77 specs (the process track only). **Re-counted 2026-09-18 (M658): 8 of 87.** Both numbers moved and the ratio barely did; the "70 small edits" the row prices is now 79. | The stuck path is complete but lives on the module pages, so it reaches a learner who navigates module-first and misses one who arrives from `jichi assignments` or the index. The escalation ladder is now documented once in TEACHING_ASSIGNMENTS (M398), which is the cheap half; putting one line in each spec is 70 small edits and wants a template pass rather than hand-editing. **Revisit with** the next assignment-authoring milestone, so the footer template changes once. | [analysis](analysis/2026-08-12-docs-review.md) |
| **Per-facet reading tasks** — one graded task each for abstraction→concrete, control flow, data flow and execution, beyond the single integrated `74-read-the-turn` (M627). | The integrated task exercises all five readings on one unit and ships the instrument (`CODE_REVIEW.md`) and the coaching skill; splitting it into a set costs four reference readings, four graders and four trap cases before anyone has run the first. **Revisit when** a learner has graded 74 and the record (their READING.md against the reference) shows which reading they get wrong most — that is the facet worth its own task, and the evidence for which one is not available yet. | ROADMAP M627 |

## Open — unit-suite integrity

| Deferred | Why | Where |
|---|---|---|
| **A `{name, fn}` table for the unit runner**, replacing `test_main.c`'s **174** hand-written `printf(name); test_name();` pairs *(recounted 2026-09-18: the row said 146, which was true when written and has been wrong for a while — the same rot as M583's "eight call sites" that turned out to be nine in five files, and it makes the row's own argument stronger rather than weaker)*. | It is the single enabler for three things at once: a per-function `jc_test_checks` delta (so "every test contributes ≥1 check" becomes checkable with no baseline), per-test selection (the prerequisite for the M201 re-ask below), and a cleaner ground truth than grepping call sites. **Not done because it touches the instrument** — failure mode 1 is "the instrument is broken" and M201's incident was exactly that, so it needs its own milestone with its own teeth: the total check count identical across the change (11,305 → 11,305) and a planted failure still reported with the right name and count. **Checked before parking (M326b):** `jc_test_checks` is a plain global incremented by the JC_CHECK macros, so the delta is trivial once the loop exists; and `test_main.c` takes no `argc`/`argv` at all, so selection genuinely cannot be bolted on without it. **Revisit when** either the ≥1-check assertion or the isolation sweep is wanted — neither is reachable without it. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 3/4 |
| **The M201 re-ask for the unit suite** — run each of the **174** test functions alone and compare with the in-suite result, both directions. | Blocked on the table above (no per-test selection exists). The expectation is honest: probably **zero** findings — `test_msg` already restores the process-wide language with a comment saying why, so the discipline exists — and a clean zero is a result worth recording (the M343 0/21 precedent), not a reason to skip the measurement. What it could find is order dependence via the `jc_msg` language global, registered redaction secrets, log level, locale, cwd, or a static cache. **Revisit with** the table, as its first customer. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 4 |

## Open — externally blocked

A reason can also rest on **someone's statement** rather than on evidence or judgement, and the
M326b rule applies there too — not by checking the statement (I cannot read anyone's email) but by
**saying whose it is**, so a future reader knows what kind of thing they are looking at. Two of the
three rows below are of that kind, and their elapsed time is stated because "we asked once" quietly
becomes "we asked months ago and never followed up".

| Deferred | Why | Where |
|---|---|---|
| **The logo (SVG + PNG).** | **Operator's statement:** they are drafting it. **Re-scoped M657, because the row's own condition expired:** it said they would bring it *"before release"*, and two releases have shipped without it — `v0.9.0` on 2026-08-27 and `v0.9.1` on 2026-09-17 — with no logo file in the private tree or the public one (checked 2026-09-18). So the logo gates nothing, and a row implying it does misdirects whoever reads this page looking for what stands between here and a release. By this page's own test (*"if new information could change the answer, it is deferred"*) it is barely a deferral: it is a want with no unblocking condition. Kept anyway, because the operator said they would draw one and a promise recorded is cheaper to honour than a promise remembered. | ROADMAP M307 |
| **Robotics Tier B (motion) and Tier C (the reflex layer).** | Deliberately human-gated: a person on the physical E-stop. **A judgement, not a schedule** — and the one row on this page that should never acquire an unblocking condition. | [ROBOTICS_BRINGLIST.md](ROBOTICS_BRINGLIST.md) |

## Closed at M657 — the licence, and the public repository

The single most consequential row on this page sat under **Open — externally
blocked** for three weeks after it had shipped. It still read *"Waiting on a JLU
rights answer"*, and it still carried, as its M326b evidence, *"no `LICENSE` or
`COPYING` file exists, so 'the first public commit needs it' holds."* Both halves
are false in the tree: `LICENSE` (Apache-2.0) and `NOTICE` have been committed
since **2026-08-27**, and the public repository has been published twice.

This is the M492 failure in its most expensive form. There, four rows had their
closure appended *behind* a reason still written in the present tense, and a
reader scanning the page met the stale assertion first. Here no closure was
appended at all — so a register built to answer *"why isn't this done?"* went on
answering *blocked on a rights question* about the one thing the project had
actually finished, with a tag on it, on two remotes. Nothing in the tree could
correct it, because nothing read this page. That is what
[`tests/smoke/deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh)
is for, and it is deliberately structural: it could not have caught *this* row,
which was well-formed and wrong. What it catches is the class that made the wrong
row invisible — closed rows under open headings, open headings with no rows, and
a platform that exists in every other register and not in this one.

| Was deferred | Closed by |
|---|---|
| **The licence.** | **M619 (2026-08-27): Apache-2.0**, copyright Justus-Liebig-Universität Gießen, author Alexander-Lars Dallmann, with `scripts/set-license.sh MIT` and the candidate text in the tree should a review switch it. The reasoning and the rights position are in [LICENSING.md](LICENSING.md). The decision is structural rather than remembered: `scripts/make-snapshot.sh` refuses `--commit` without a `LICENSE` file (M484), and `tests/smoke/snapshot_lint.sh` scans the tree that would be published (M485/M487). |
| **The public repository.** | **Published 2026-08-27** (`v0.9.0`) and re-cut **2026-09-17** (`v0.9.1`; public `573ccce` = private `a64cc4b3`), on GitLab and GitHub, both pushes green on the hosted runner. The development history stays private and the repository advances by curated states — the mechanism, the two deliberate omissions and the tag rule are in [plans/2026-08-public-snapshot.md](plans/2026-08-public-snapshot.md). |


## Closed at M503 — four signals that were silent, absent, or invisible

Reviewed as a batch with the operator, and the review itself is the lesson: of
five rows picked as "ready to build", **two turned out to argue against being
built** once read in full. Checking the reason before acting on it (the M326b
rule) is now three-for-three this month.

| Was deferred | Closed by |
|---|---|
| **Say something when a line typed before the first prompt is discarded** (M464) | `jc_term_readline` now probes for buffered input before the `TCSAFLUSH` that discards it, and says so: *"input typed before this prompt was discarded … please retype it."* The bytes are deliberately **not** recovered — the flush is what stops stray type-ahead from answering a `y/n` prompt nobody has read — so the honest fix is a notice with a way forward, not a smuggled line. The probe reuses M156's existing `input_pending` rather than adding a second readability check that could drift. `preprompt_discard.sh`, 3 checks (one asserting the stray text still never reached the model), proven red. |
| **Verify that a private file actually became private, instead of trusting `chmod`** | `doctor` now creates a probe under `~/.jichi.d`, tightens it, and **reads the mode back**: `✓ created 0664, tightened to 0600, read back`. On a filesystem that accepts `chmod` and ignores it — MSYS2's default `noacl` mount, some network mounts — it reports *"private files are NOT private on this filesystem"* and names what is exposed (the API key file, the daemon socket, the audit log). **The policy question the row left open is answered by precedent:** WARN interactively, **FAIL under `--unattended`**, joining M158b's explicit escalation set for the same reason `privilegedAudit: false` is in it. It also states what it did *not* exercise: under a narrow umask the probe is already private before the tightening runs, so the guarantee is verified while the mechanism is not. Proven with a new `JC_FAULT_CHMOD` site — the same justification as `JC_FAULT_PROCFS`, since there is no `noacl` mount on the bench and the branch guards a safety property. `faults.sh` checks 9-10. |
| **An `--auto` run silently inherits `testCommand`, and there is no opt-out** | **Half of it had rotted:** `--verify ""` already disarms the gate, inherited or not — measured, and it was simply undocumented. The half that was real is now built: the journal's `start` event carries **`verify_source`** (`flag` \| `config` \| empty), because `verify: make test` used to mean both "the operator chose this" and "the config supplied it", and those differ sharply when the gate then fails. A deliberate `--verify ""` records as `flag`, so a disarm is a decision rather than an absence. Documented in [AUTONOMY.md](AUTONOMY.md) §"Where the verifier comes from"; `verify_source.sh`, 4 checks, proven red. |
| **A `doctor` line for the BSDs, illumos, and any other POSIX host** | **Declined, on the row's own evidence.** It already said the M400 note fires on any non-Linux `uname`, that no BSD-specific code exists, and that "inventing conditionals for a platform nobody has is how the Darwin branch got into the state M400 found it in". Then M460/M461 measured it: FreeBSD and OpenBSD found **ten real defects between them and none of them wanted a BSD conditional** — every fix was a capability probe, a portable flag, or POSIX-correct signal discipline. A row whose reason has been confirmed by measurement is not deferred work; it is a decision. |
| **Identify the unit check that failed once during M407's gate** | **Closed as unidentifiable.** One occurrence, never reproduced, no artefact kept, and nothing left to examine. The honest action is to say so rather than carry a row nobody can act on — and to point at the mechanism that would catch the next one: *the M201 re-ask for the unit suite* (still open below), which labels a failure "in-suite only" or "also alone" instead of leaving it a mystery. |

## Open — the source-reading guides

From the M399 review of `docs/reading/` (25 files measured against their own
documented chapter skeleton; Annai measured complete and uniform, Fukabori
measured thin in one specific way).

| Deferred | Why | Where |
|---|---|---|
| **Bring the remaining Fukabori chapters to Annai's code density.** | **Largely closed by measurement, M658 — nobody noticed, because the row was never re-counted.** Today Annai's nine chapters carry 2–4 blocks (mean 3.1) and Fukabori's twelve carry **1–4, mean 2.1**, with exactly **two** still at one: chapter 1 ("why C89") and chapter 11 ("AI-supported coding examined"). Those are the same two the row below argues are *arguments rather than mechanisms*, where a block added to hit a quota would be decoration. So the inversion this row was filed for — the expert guide showing less code than the beginner guide — is gone, and what remains is two chapters that are deliberately at one. It should close with the next reading-guide pass rather than carry a premise that expired. Original measurement, kept because the row's own prediction is the point: Measured: Annai's nine numbered chapters carry **2–3 code/pseudocode blocks each**; Fukabori's twelve carry **one** (and chapter 3 carried none until M399 fixed it). The *expert* guide shows less code than the *beginner* guide, which is inverted — and FUKABORI.md's own conventions promise "the invariants first, then **the code that carries them**, then the failure that taught them". **Not done wholesale because the fix is per-chapter judgement, not a sweep:** each needs the *one* excerpt that carries its argument (chapter 3 wanted the wrong-arena line and its fix; chapter 7's would be the fork/select shape; chapter 8's the read-callback), and a block added to hit a quota would be decoration, which the house diagram rule already forbids. **Revisit** one chapter at a time, cheapest first, whenever that subsystem is being touched anyway. | [FUKABORI.md](reading/FUKABORI.md) |
| **A lint that each reading chapter carries a diagram or is a stated exception.** | Three Fukabori chapters have no mermaid (1 "why C89", 11 "AI-supported coding examined", 12 — the last now has one). **Re-counted 2026-09-18 (M658): exactly two, chapters 1 and 11, every other chapter at one.** The row's reason is confirmed rather than eroded — the exception list is still a two-item allowlist and a lint over it would check almost nothing. By the M503 precedent (*"a row whose reason has been confirmed by measurement is not deferred work; it is a decision"*) this is a candidate for `DECISIONS.md` at the next pass, and it is left here only because the same pass should close the row above with it. **Checked before parking:** 1 and 11 are *arguments*, not mechanisms, and a diagram restating prose is decoration — the UML tutorial's own rule is "one diagram, one question", so the honest state is an exception list, not a missing diagram. A lint would therefore encode a two-item allowlist and check almost nothing, which is the "checks zero things and reports success" shape this project's lints are written to avoid. **Revisit if** the exception list ever grows past a handful — that would mean the convention had actually drifted. | [reading_refs_lint.sh](../tests/smoke/reading_refs_lint.sh) |

## Open — platforms never DRIVEN (M665, 2026-09-18)

Raised by the operator: *"jichi must be tested on all platforms with actual
meaningful agentic tasks using language models. That is the point of using jichi,
and must be tested."* It is correct, and the matrix did not measure it.

**Every gate a platform row runs is offline** — build, unit suite, smoke tier and
the four surfaces (`--version`, `doctor`, `describe`, `context`). All of them can
be green on a kernel where jichi has never called a model. `docs/PLATFORMS.md`
now carries a fourth word, **Driven**, for a row that has run a live agentic task.

**The first count published with it — "three of nineteen" — was wrong**, and the
correction is the more useful finding. It counted what `PLATFORMS.md` records and
was written as though it counted what has been done. The **M459 fleet push**
(2026-08-15/16) had already driven a **Raspberry Pi, an Android tablet and a
proot guest** with a model over ssh, executing tools and writing files, with two
findings still in doctrine — recorded in `AUTONOMOUS_LOOPS.md`, `DISTRIBUTED.md`,
ANECDOTES #55 and ROADMAP M459, and **nowhere on the matrix**. The gap is that
the platform rows do not carry driven-ness, not that it never happened.

| Deferred | Why | Where |
|---|---|---|
| **Record driven-ness ON the platform rows, and drive the rows that have not been.** | Documented today: **WSL2** (chat, reasoning, tool use, embeddings), the **M459 fleet** — Pi, Android tablet, proot guest, a fenced `--auto` task that executed tools — **illumos** (a text turn only, M663 — no tool call), **FreeBSD** (text turn 4 s *and* a native `read_file` tool call, 7 s, M665). Only WSL2, illumos and FreeBSD say so on their rows. The rest — OpenBSD, NetBSD, the Pis, Termux/proot, Guix, musl-static, the 14 emulated architectures — have **never made a model call**. For the `qemu-user` architecture sweep it is not merely undone but currently impossible as built: those rows link `HAVE_CURL=`, so there is no HTTP at all; driving them needs a different rig, not a longer run. | `docs/PLATFORMS.md`, the **Driven** verdict |
| **Decide what the minimum driven task IS**, so rows are comparable. | FreeBSD's *read a file and report its contents* exercises request build, SSE framing, a native tool call, tool execution and the second turn — that is a defensible floor and it is what the FreeBSD row used. It is **not** yet written down as the standard, and rows measured against different tasks are not comparable, which is the same mistake `JC_SMOKE_TIMEOUT_MULT` was introduced to stop people making with build times. A candidate: one text turn, one tool-calling turn, one refused-by-a-fence turn. | undecided |
| **The reverse-tunnel arrangement is not in any rig.** | Both driven VM rows were set up by hand: `ssh -R 1234:127.0.0.1:1234` so LM Studio stays loopback-bound while the guest reaches it. It works, it is written down in two analysis pages, and it is in **no** `scripts/tier-v-*.sh` — so the next row does it from memory or not at all. | `scripts/` |

## Open — platforms never compiled

From M400, which asked the honest form of "we have not built this on macOS or WSL":
[`PLATFORMS.md`](PLATFORMS.md) now owns every platform verdict, and these two are
measurements this project does not have.
**Found missing at M657, and the reason it was findable nowhere is the point.** The illumos row below existed in three other documents and not in this one, so a reader following this page's own promise — *"things deliberately not done, with the reason"* — would have concluded the never-compiled set was one platform wide. [`deferred_register_lint.sh`](../tests/smoke/deferred_register_lint.sh) check 4 now enumerates that set from `PLATFORMS.md`, which owns platform verdicts, and fails the build when one of them has no row here. Enumerating the universe a second way, by a different route, is where this project's last four documentation gaps came from (M508, M510, M511).


| Deferred | Why | Where |
|---|---|---|
| **Compile jichi on macOS and run `make check-target`.** | **Never compiled — not once.** Not deferred out of doubt: there is no Mac on this project. **Checked before parking (the M326b rule):** the old claim "no Darwin-specific code" in both `BUILD.md` and `INSTALL.md` was **false** — `jc_mem_total_mb` has an `#if defined(__APPLE__)` `sysctl(HW_MEMSIZE)` branch, and it used `unsigned long long` + `ULL` constants, which is three diagnostics under this project's own mandatory `-std=c89 -pedantic -Wall -Wextra` and a failed build under `WERROR=1`. So the honest state was worse than "untested": jichi's only macOS code **could not have compiled**, and no build here would ever have said so. Fixed and pinned by a lint in the same milestone. **Revisit** the moment anyone has access to a Mac for an afternoon; one `uname -srm` + two check counts turns a "never compiled" row into a verified one. | [PLATFORMS.md](PLATFORMS.md) |


## Open — what the illumos row found and did not diagnose (M658)

The first illumos row behaved the way the platform doctrine says a
never-compiled row behaves — it detected defects. Three were fixed in the same
milestone. These two were measured and **not** diagnosed, and they are recorded
rather than guessed at.

| Deferred | Why | Where |
|---|---|---|
| **Diagnose the remaining failing smoke drivers on illumos.** *(238 of 300 at M658 → 249 of 301 once the rig made the shipped tree a git repo → 279 of 301 once the pty cluster was fixed, M661. The **pty cluster is closed**: 19 of 19. What is left is the text-tool cluster and a handful of others.)* *(62 at M658; ten of those were the rig not making the shipped tree a git repository, fixed at M660 — so ten "illumos failures" were never about illumos.)* | **~19 are pty-driven** (`tui_*`, `typeahead*`, `paste*`, `typed`, `tab`, `editor`, `approval_keys`, `view_key`, `slash_leading_space`, …) and point at `tests/tools/ptydrive` against illumos pty semantics — one cause would plausibly explain the whole cluster, which is why it is worth one sitting rather than nineteen. **Most of the rest are text-tool lints** (`bibliography_lint`, `man_page_lint`, `describe_names_lint`, `doc_claims_lint`, `i18n_tracks_lint`, `config_defaults_lint`, `superseded_marker`, …), and the **hypothesis** — untested, and named as one — is the cause the product already hit: illumos ships the legacy Solaris `grep`/`sed`/`awk`, and the tier's own text tooling may assume GNU behaviour. `posix_utils_lint` already polices part of that family and caught a GNU-only `\|` in this milestone's own driver, so if the hypothesis holds, the interesting question is why its universe missed these — but that question only exists once the cause is known. **It is not known.** Saying which cluster a driver is in is not saying why it fails, and this row deliberately stops where the measurement stopped. **Revisit** when the pty cluster is worth a sitting; the rig makes the row one command now (`scripts/tier-v-illumos.sh`). | [PLATFORMS.md](PLATFORMS.md), ROADMAP M658 |
| **Make the tier's `grep -o` extractions portable** — the dominant cause of the remaining illumos failures. | **Measured M661b: `grep -o` returns only the FIRST match per line on illumos, where GNU returns all** — 199 matches against 1, on `jc_config.c` flattened to one 66 KB line. A lint that flattens a file and extracts many items therefore collapses to one and announces *"the extraction broke, so this lint is vacuous"*: the floors are working, the extraction is not. **Counted before proposing a gate, and the count is why there is no gate yet:** 69 drivers use `grep -o` and **27 flatten first** (`tr '\n' ' '` plus `grep -o`). A lint would be born red on 27 and need a 27-entry allowlist, which is the "checks almost nothing and collects exceptions" shape this project refuses. So the work is a **sweep with a portable idiom** — split on a delimiter before matching, or `awk` — and the 27 is its floor. Distinct from check 17's hazard: that one is a pattern that can match *empty*, this one a pattern that matches *repeatedly*. **Revisit as** its own milestone; it is the largest single lever left on the illumos row. | `tests/smoke/*.sh`, [PLATFORMS.md](PLATFORMS.md) |
| **The smoke tier invokes `make` by name in several drivers, and on illumos that is Sun make.** | Found at M661 inside `install_no_build`, where four checks failed with messages about refusals and DESTDIR that had nothing to do with `install`: the driver shelled out to `make`, illumos **has** a `make`, and it is not GNU's. That driver now uses `${MAKE:-make}` — GNU make exports `MAKE` to every recipe shell, so a driver run by `make smoke` gets the same make that is running it. **Counted before deciding not to sweep:** across `tests/smoke/*.sh` the real invocations are `make install` (15), `make clean` (11), `make test` (9), `make ci` (7), `make info` (6), `make smoke` (4) — but most of the 57 files matching `make ` match it in *prose*, so a blind regex sweep would rewrite comments and grepped strings. **Revisit as** one pass with the count above as its floor, or when a second driver fails this way on a non-GNU-make platform. | `tests/smoke/install_no_build.sh` |

## Open — what the FreeBSD re-run found and did not diagnose (M665, 2026-09-18)

The row was re-run because its coverage debt had reached **102** — it last
recorded 201 drivers on 2026-08-17 against 303 in the tree. It came back at
**296 of 303** with six failing drivers. **Five were harness defects and are
fixed**; they are not listed here because they are closed. What follows is what
was measured and **not** explained.

Full account, including the four hand-built probes that were all broken:
[`analysis/2026-09-18-freebsd-and-the-probes-i-built.md`](analysis/2026-09-18-freebsd-and-the-probes-i-built.md).

| Deferred | Why | Where |
|---|---|---|
| **Finish the bare-`make` conversion across the tier.** | `smoke_make` now exists in `tests/smoke/_smoke.sh` (resolves `$MAKE`, else `gmake`, else `make`) and **two drivers are converted** — `cppcheck_lint` and `install_no_build`, the two that failed. The tier-wide count stands at **install 15, clean 11, test 9, ci 7, info 6, smoke 4**. The rest are unconverted deliberately: a blind sweep across sites whose behaviour on four kernels has not been measured is how a portability fix becomes a portability bug. **Note the shape of the bug this closed:** `install_no_build`'s own header said *"`${MAKE}`, not `make` (M661)"* while one line still ran bare `make` and three more used `"${MAKE:-make}"`, whose fallback is bare `make` exactly when a driver runs standalone — which is what the rig does to classify a failure. A rule stated in a header and applied at some sites is not a rule. | `tests/smoke/_smoke.sh`, `smoke_make` |
| **The README's platform count is pinned by nothing.** The sentence reads "Nineteen of them". | Left unchanged, because a replacement number would be a guess. `portability_lint` check 14 now pins the *never-compiled* half of that paragraph against `PLATFORMS.md`; the count itself is still prose nothing checks. | `README.md:207` |

## Closed at M661 — the install_no_build row, and the diagnosis that was wrong

| Was deferred | Closed by |
|---|---|
| ~~**`install_no_build` fails inside a full `check-target`.**~~ **DONE at M661 — and the M658 diagnosis recorded here was WRONG, which is worth more than the fix.** | M658 called this *"an in-suite order dependence"* and went on to accuse the harness's classifier of inheriting the suite's state. **Neither was true.** `make install` copies **both** binaries; `smoke: $(BIN) smoke-tools` builds only `jichi`; `check-target: test smoke` therefore never built `jichi-convert` — so the driver failed for a **missing file** on every device row. Reproduced on the host in one command by moving that file aside: the same two checks, the same two messages. The classifier was **right** to say "ALSO fails standalone → a real defect"; it simply could not say *which* defect, because the file was still missing on the retry. Fixed at both ends: `check-target` builds `all`, and the driver states the precondition and skips with the reason if it is ever unmet another way. **The lesson is the one this page is about:** the reason I filed was a plausible story that fitted the evidence, and checking it cost one `mv`. | `tests/smoke/install_no_build.sh`, `Makefile` |

## Closed at M658 — illumos, compiled

**Added to this register at M657 and measured the same night.** The row had
existed in `PLATFORMS.md`, `LOW_MEMORY.md` and `PLATFORM_RETEST.md` §6 for four
months as "the cheapest remaining row" and in this register not at all; checking
what *blocked* meant found KVM, QEMU and 137 GB free on this machine, so it cost
a session rather than a resource. It took about three hours, and it behaved the
way the platform doctrine says a never-compiled row behaves: **a defect
detector.**

| Was deferred | Closed by |
|---|---|
| **Compile jichi on illumos and run the gate.** | **M658, 2026-09-18: OmniOS CE r151058 under KVM** (cloud image + cloud-init, ssh in 30 s — no graphical console step, which is what made Guix expensive). gcc 14.3.0, GNU Make 4.4.1, `/bin/sh` = ksh93. `gmake WERROR=1 CC=gcc`: **zero diagnostics**; unit suite **13,273 checks / 0 failures**; smoke **238 of 300 drivers, 1,299 checks**. Recorded as **Partly verified**, not Verified — the honest word for a green build, a green unit suite and a smoke tier with 62 failing drivers. [PLATFORMS.md](PLATFORMS.md) carries the row, the two clusters the failures fall into, and what was *not* diagnosed. |
| **The M469 procfs prediction, reasoned from the source since 2026-08-17.** | **Measured on the box.** `/proc/self/stat` and `/proc/self/exe` are absent (`/proc/self/path/a.out` is the equivalent); `/proc/self/status` exists as a **1,136-byte binary `pstatus_t`** in which `VmRSS:` occurs **zero** times, so `jc_meminfo_parse` reports *no data* rather than *wrong data*. M647 had already reduced the parsing half to a pure function and pinned it (`tests/test_meminfo.c:test_binary_status`) precisely because it needed no box; the three things that did need one — that `fopen` succeeds, that the struct has this shape, that the `fread` path behaves — are now measured too. |

**What it cost the tree: three defects, none of which wanted a platform
conditional** — `-D__EXTENSIONS__` and `-lsocket -lnsl` as Makefile probes, and
`search_code`'s `grep -rnI`, which illumos's grep rejects outright. That last one
had killed the tool on that platform entirely, and it is the M461 `--color`
defect one platform later: a flag four greps accept is not a portable flag. All
three are in [DECISIONS.md](DECISIONS.md).


## Open — verified rows the tree has outgrown (M647)

[`PLATFORM_RETEST.md`](PLATFORM_RETEST.md) turned staleness into arithmetic: a
row's **coverage debt** is the smoke drivers in the tree today minus the drivers
that row actually ran, and past 100 the row is a *historical* datum rather than a
present-tense claim. The policy page computes the number; this is the page a
reader consults for "why isn't this done", so the two rows it produces belong
here as well.

| Deferred | Why | Where |
|---|---|---|
| **Re-run the Raspberry Pi Zero 2 W row at rung 3** (`make check-target`). | Its verdict is still M272's — **94 drivers**, debt **205** against today's tree, and the only full-gate row past the 100 threshold. README and PLATFORMS both make present-tense physical-ARM claims that rest partly on it, which is precisely what a debt over 100 says needs a new run first. **Ran 2026-09-18 (M658), and it did NOT retire this row — read the word size.** The operator powered the boards; the Pi Zero 2 W answers, and it is running **32-bit armhf**, which is the M454 row, not M272's **aarch64** one. The armhf row was re-run at rung 3 and is now current (297 of 299 drivers, 13,329 unit checks, debt 3, down from 106). The aarch64 row at debt 206 still stands, and retiring it needs the board re-flashed to 64-bit — a human with an SD card, not a session. The Pi 400 (aarch64) was re-run the same night and its unit suite is green, but its smoke tier stopped at one driver, so it does not stand in for this row either. | [PLATFORM_RETEST.md](PLATFORM_RETEST.md) §3 |
| **State a driver count in the rows that state none**, so their debt is a number at all. | Counted 2026-09-18: **18 Verified rows, 7 of which cite a driver count** — and the debt table works five of those seven. For the other eleven, including the **Pi 400's M451 full `check-target`**, debt is not a large number, it is *not a number*. `platform_retest_lint` check 2 asks that a row be **datable**, which is strictly weaker than **debt-computable**, and the gap is invisible from the table because the table can only show the rows that have the figure. The fresher aarch64 evidence is in that blind spot: M451 is four months newer than M272 and cannot be weighed against it. **Revisit with** the next re-run of any row — the count is free at the time and unrecoverable afterwards. | [PLATFORM_RETEST.md](PLATFORM_RETEST.md) §3 |


## Closed — done, and formerly left sitting under an *Open* heading

Four rows that were **done and still filed as open**. Three were reported at M475 and
fixed at M488; the fourth is M475's own WSL2 row, marked **DONE (M475)** and left for
thirteen milestones under a heading reading *platforms never compiled* — about a
platform `PLATFORMS.md` now records as **Verified, full gate**.

**Moved here rather than left annotated in place,
because that is what went wrong:** the closure was appended to the *end* of each row,
behind a reason still written in the present tense, so `--api-base` went on reading
*"there is **no** base-URL flag"* for four milestones after `main.c` began parsing one.
A reader scanning the register met the stale assertion and never reached the closure —
and all three sat under an **Open** heading while doing it. Found by the
`feature/hrz-model-info` session while landing (M492), which noted it is the same shape
as the `docs_flags.sh` `future` entry that same branch removed: a row saying *"move this
line out when the flag ships"* that outlived the shipping by a milestone.

This page says at its head that items are **removed** when done; ten `## Closed` sections
say the practice is gentler. Either is fine. A closed row under an *Open* heading is not.

| Deferred | Why | Where |
|---|---|---|
| **Stop `jc_eventlog_open` tightening a parent directory it does not own.** | **Found 2026-08-18 (M475); reported, not fixed.** `make_parent_dir()` calls `jc_make_private()` on the log path's parent **even when that directory already existed**; the guard catches `/` and nothing else. Run as root — every container, most CI — `--log /tmp/jichi.jsonl` turns `/tmp` into 0700 root-only for the whole machine (measured: 1777 before `run_tests`, 700 after; call site captured with an `LD_PRELOAD` shim on `chmod` and PIE offsets resolved against the symbol table). Non-root is inert **by accident**: the `chmod` fails with EPERM and the return is discarded. **Deferred because the fix is a design call, not a patch** — skip pre-existing directories, refuse when the owner differs, or tighten only what `jc_mkdir_p` actually created; each changes M132's privacy guarantee differently and M132 was deliberate. Note that `jc_platform_posix.c` already reasons about this exact hazard for the state root (*"jc_make_private applies a mode to whatever the path RESOLVES to"*) and guards it with an `lstat` owner/mode check — **the sibling two files away has none.** When a hazard earns a documented fix, grep for its family. | `src/util/jc_eventlog.c`  **CLOSED (M488).** Fixed as a FAMILY rather than a patch: `jc_mkdir_p_private()` creates the chain and applies 0700 **only to what it created**, and all four `jc_mkdir_p`+`jc_make_private` sites route through it — two of which (`--log`, `--control`) take the path from the operator, so the hazard was never confined to telemetry. `posix_utils_lint` check 7c bans the old pairing, proven two-sided. **The design question is answered and the rejected options recorded:** refusing on an owner mismatch turns a legitimate shared directory into an error, and chmod-ing only when the mode is wider than 0700 still re-permissions `/tmp`, just conditionally. M132 is unchanged for everything jichi owns — a directory it creates is still 0700, the file still 0600. Unit-tested both ways (`test_parent_dir_not_retightened`, `test_created_dir_is_private`).|
| **Stop a failed precondition in `test_session_roundtrip` from crashing the suite.** | **Found 2026-08-18 (M475); reported, not fixed.** Reachable once the row above has poisoned `/tmp`: a non-root `make test` reports `FAIL tests/test_session.c:759` (save failed), then four more FAILs that read a `struct jc_session` the failed `jc_session_load_by_id` never populated, then `free(): invalid pointer` / SIGSEGV — deterministic across three runs. `JC_CHECK` is non-fatal **by design**, so a failed *precondition* is followed by code dereferencing the object it was meant to produce. Worse, the abort discards block-buffered stdout, so the FAIL lines that name the cause vanish and the reader gets a bare `Aborted (core dumped)` — `stdbuf -o0` was needed to recover them. **Deferred because the fix is a design call:** leave the block when a precondition fails, or make precondition checks fatal suite-wide (which changes every test's semantics). | `tests/test_session.c`  **CLOSED (M488).** Reproduced deterministically **and without root** — pre-create that test's own sessions directory read-only, and it exits **139**. The fatal step turned out not to be the reads named above but `jc_session_free(&back)` on **uninitialised stack memory**: `jc_session_load_by_id` correctly leaves its out-param untouched on failure, and the product's own callers all bail before using or freeing it (checked, so no product change). Fixed by zeroing `back` and guarding the two preconditions with the existing `JC_REQUIRE`; the suite-wide semantics change was not needed. Now: exit **1**, both FAIL lines naming the cause, and the other 12,413 checks still run. `JC_REQUIRE`'s own comment records an audit that found 19 sites across 8 files (M452) — this was the twentieth, and that audit missed it.|
| **Add `--api-base` to `jichi setup`'s non-TTY flag form.** | **Found 2026-08-18 (M475).** `setup` correctly detects a non-TTY and prints the flag form — `--preset --provider --model --key-env --from-global --import` — but there is **no base-URL flag**, while the interactive wizard *does* prompt for `"apiBase URL"` (`src/main.c:4943`) and `model_obj()` already writes `apiBase` when given one (`jc_setup.c:333`). The machinery exists; only the CLI surface is missing. It bites precisely where it matters: **`small-local`** ("Small local model (7-14B)") and **`constrained`** exist for locally-hosted models, which by definition need a custom endpoint — so following the tool's own non-TTY guidance yields a config pointing at the provider's cloud. Workaround is the one `setup` already prints (`$EDITOR local/config.json`), and the generated scaffold is otherwise sound: verified against a local OpenAI-compatible server, `doctor` 19 ok / 0 problems and live inference green once the line is added. | `src/main.c`, `src/setup/jc_setup.c`  **CLOSED (M488).** The flag exists, is wired to `ans.api_base`, and is documented in `SETUP_WIZARD.md` with the local-server example — and, the half that actually mattered, **the not-a-TTY message now names it**, because that printed flag list was itself what produced a config pointing at the cloud. Three smoke checks, two-sided: the flag reaches the config, omitting it still leaves the provider default, and the guidance names it.|
| **Compile jichi under WSL2 and run `make check-target`.** | **DONE (M475, 2026-08-18).** The row was right about which parts mattered and wrong about how much. The compiler was a non-event — `make` clean on the first try, **12,418 unit checks / 0 failures** — and of the three subsystems it named, **only `/mnt/c` behaved differently**, exactly as warned (the same commit reads 0 modified on ext4 and **1,639 modified** through v9fs, from line-ending translation). The terminal surprised in the *other* direction: smoke ran **209 drivers / 1,104 checks at `JC_SMOKE_TIMEOUT_MULT=1`**, so WSL2's pty layer times like real hardware, not a constrained VM. `PREPARE_AND_BUILD.md`'s walkthrough has now been **executed end to end** — by a non-root user, against pristine HEAD, and it passes. What the row did not anticipate: running the *full gate* found **four pre-existing defects in `ci`-only configurations** (clang, sanitized, valgrind, no-emacs), none WSL-specific, all reproducing on bare-metal Ubuntu 24.04, all traceable to M472 having been measured against gcc/unsanitized/with-emacs — the third instance of the M447/M189 shape. Verdict and numbers: [PLATFORMS.md](PLATFORMS.md); session write-up: anecdote 62 in [ANECDOTES.md](ANECDOTES.md). | [PORTING_WINDOWS.md](PORTING_WINDOWS.md) |

## Closed at M501 — two ways an explicit intent lost to an inference

| Was deferred | Closed by |
|---|---|
| **An inferred constraint can override an explicit `--edit-scope`.** | Already fixed at **M459** for `write_file`/`edit_file` — and the row had rotted, which the M326b rule caught: checking it before acting found the fix in place with six checks green. What was still broken is that M459 tested **tool names**, so `apply_patch` — whose paths live in `edits[]`, and which is what a model reaches for when making several edits — was never exempt, and neither were `generate_audio`/`generate_image`/`record_audio` (a top-level `path` outside the name list). M501 replaces the name list with a property of the **arguments** (`jc_argpath_collect`): exempt when the call declares at least one path and **every** path it declares is in scope. All-or-nothing, because `apply_patch` is atomic and one out-of-scope path must refuse the whole call — and an overflowed collection (more paths than the buffer holds) counts as *not* exempt, so a truncated view can never widen a permission. `constraint_vs_scope.sh` checks 7–9, proven red against M459's behaviour. |
| **The envelope attributes every mid-run change to the run — a concurrent actor's edits get reverted under `revertOutOfScope`.** | M501 took the row's second option ("scope the revert to paths the run's own tools touched") and made it provable rather than heuristic. jichi can change a file in exactly two ways: the write chokepoint (fenced to the scope) and a shell command (not fenced). So the envelope now records both — the paths written through `jc_app_write_file`, and whether any shell tool ran — and reverts an out-of-scope change when it was written by this run **or** when a shell command ran (attributable). When neither is true the change **cannot** be the run's: it is left alone, named on stderr, and journalled as `not_ours`. `revert_provenance.sh`, 6 checks, with a floor under the "still reverted" half after the first version passed while the fixture's shell call never happened. **The residual is documented rather than closed:** a run that uses the shell still cannot tell its own out-of-scope writes from a colleague's, so the operating rule stays *one envelope per working tree at a time* — see [AUTONOMY.md](AUTONOMY.md) §"What `revertOutOfScope` will and will not undo". |

## Open — found by the 2026-08-20 dogfooding runs (M504)

| Deferred | Why | Where |
|---|---|---|
| **Decide whether `.jichi/` state is implicitly inside the edit scope.** | Measured: a run scoped `--edit-scope 'src/agent/**'` journalled `out_of_scope: [".jichi/memory.md"]` -- the agent writing its own memory file. Detection-only there, so it was reported and kept, which is the right outcome; the question is whether it should have been reported at all. **For an exemption:** it is jichi's own bookkeeping rather than the user's code, an operator trained to ignore the notice will ignore a real one, and under `revertOutOfScope: true` the M501 rule *would* revert it (a run that used the shell is attributable). **Against:** an implicit exemption is a hole in a fence, `.jichi/memory.md` is a file the MODEL controls, and an operator who wants it writable can say so with a second `--edit-scope`. **Not decided here** because it is a fence-semantics decision, not a defect -- and the current behaviour is the conservative one. **Revisit when** a run with `revertOutOfScope: true` loses memory it should have kept, or an operator reports the notice as noise. | [analysis/2026-08-20-three-runs-two-projects.md](analysis/2026-08-20-three-runs-two-projects.md) |

## Open — from the M505 self-review

| Deferred | Why | Where |
|---|---|---|
| **Whether `config validate` should surface posture warnings, or stay a pure parse check.** | Found at M505 while checking a documentation claim: `jichi --config bad.json config validate` prints `OK` for `{"models":[{"name":"a"}]}` -- a config naming no model id, whose active model then becomes a *priced* built-in default. `doctor` now warns about that (and FAILs under `--unattended`), so the fact is reachable; `config validate` still says OK. **Both readings are defensible:** validate is documented as a *parse* check and a reader may want exactly that, and duplicating doctor's judgement invites the two to drift (the M431 renderer argument). **Against:** an operator who runs `config validate` and reads `OK` reasonably believes the config is fit, which is what M486's "the front door told a lie its own gate could not see" was about. **Revisit when** someone reports the OK as misleading, or when a second posture warning wants the same home -- one is not a pattern. | [analysis/2026-08-20-reviewing-my-own-wave.md](analysis/2026-08-20-reviewing-my-own-wave.md) |
| **A four-persona documentation pass over the M499/M502 wave.** | M505 ran the *mechanical* half of `DOC_REVIEW.md` over the four new pages and five new sections -- every copy-pasteable command executed, every factual claim checked against the source -- and found one overstated claim plus the product defect above. What it could not do is the judgement half: the author and the reviewer were the same agent, which invalidates exactly the independence the four-reviewer method buys. **Revisit when** an independent pass can be run (a person, or agents given the read-only reviewer brief and no authoring history), before the public release. The mechanical floor is in place, so that pass starts from a corpus whose commands all run. | [DOC_REVIEW.md](DOC_REVIEW.md) |

## Closed at M506 — the budget stop that reported no verdict

| Was deferred | Closed by |
|---|---|
| **A budget-stopped run never runs its verifier, so a run that satisfied its own gate is reported as a failure with no gate verdict.** | The envelope ran the verifier at a budget exit only when the result could change the **rollback decision** (rollback armed *and* a green checkpoint banked) — correct for a decision, wrong for a report. A run whose gate was red at the start never banks a green, so the verifier was skipped entirely. Measured three times, twice in one afternoon of dogfooding, and one of those runs had done the valuable half of its task with its gate passing a minute later. Now the same verifier runs **for the record** in that case: journalled as `phase: "budget_exit_advisory"` with `advisory: true`, the exit code and the declared kind, and said on stderr when green (*"the run stopped on a budget, but its verifier PASSES on the tree as it stands"*). **Advisory by construction** — the code is journalled, logged, and dropped; the outcome stays `budget_exhausted`, because turning a green advisory verdict into a pass would make a stopped run indistinguishable from a completed one. `budget_stop_verdict.sh`, 4 checks, with check 3 pinning exactly that guarantee and check 1 a floor proving the fixture really stopped on a budget. Proven red. Cost stated in AUTONOMY.md: one extra verifier run per stopped run, bounded by `--verify-timeout`, avoidable with `--verify ""`. |

## Open — the data seams (M419)

Seven measured seams in what jichi records, designed but not built:
[`proposals/2026-08-observability-seams.md`](proposals/2026-08-observability-seams.md)
carries the numbers, the seven decisions and their rejected alternatives. The rows
here are only what a reader needs to *find* them; the order is the proposal's
cheapest-first order.

| Deferred | Why | Where |
|---|---|---|
| **`history.jsonl`: one append-only summary line per turn or run.** | Work without an envelope (interactive, plain `-p` — i.e. most work) leaves nothing a trend can be computed from, and the raw corpus is the thing retention must be free to delete. The shape is already proven three times here (`progress.jsonl`, the `improve` pass-rate history, `runs/`); this gives the whole tool the family member it lacks, so a trend is `tail`. **Deliberately conditional:** it earns its keep only if the trend is consulted — if nobody runs it in a month, delete it rather than polish it. **One measured need arrived at M425 (probe P7), and it is one field.** M86's hollow-gate check warns on `no_tests` (green with zero) or `fewer_tests` (fewer than an earlier green *within the run*). Measured on chrtext: its own `zig build test` gate reported **8 tests passed, exit 0** while the repository holds **1,525 `test " ` blocks and 99 `addTest` steps** -- roughly 0.5% of the suite, with four of those steps aborting on SIGABRT. Eight is neither zero nor a within-run regression, so nothing fired: the check sees the cliff and the slide, and cannot see a gate that was *always* tiny, because it has no idea what normal is for the project. Telling "8 tests" from "8 tests where there used to be 340" needs a **per-project historical test count** -- cross-run memory, which no current sink holds. So the row moves from *no demonstrated need* to **one measured need with a named consumer (`jc_env_verify_sanity`) and a single required field**; that is not the whole roll-up, and it is deliberately not recorded as a reversal. **Verdict, M421: not built, and this row now says why.** The M420 plan promised to *use* the join before deciding, and the use happened: three bounded runs across two projects, read as `runs --output json` indexed by `run` against telemetry grouped by the same key. The cross-sink question the roll-up was meant to answer — behaviour beside outcome, including the `in/call` quotient that diagnosed two budget deaths — came out of thirty lines of scripting over the existing sinks, with **no new file, schema, lint or version**. Two frictions were real and both were fixed in the reader instead: the sinks' differing vocabularies (now a table in [OBSERVABILITY.md](OBSERVABILITY.md)) and a phantom row when both logs shared a directory (M421). **What is still untested is the longitudinal half** — three runs from one afternoon are not a trend, and the "most work leaves nothing" argument stands on its own for un-enveloped runs. **Revisit** when a trend question is actually asked twice and the answer needs journals that retention has deleted; that, not the correlation question, is what would earn the file. | [S4 + D2](proposals/2026-08-observability-seams.md) |
| **Readers aggregate by default; retention as new `prune` scopes.** | `telemetry` reads **one** log (1 of 35 when this was written; **1 of 6 on this machine as of 2026-09-18**, because M599 made the default one appended file per workspace rather than one per run — the row's own narrowing, now visible in the count) while a throwaway measurement script globs the directory — the shipped reader is weaker than the scratch tool. And `prune` covers sessions only: `doctor` sizes the one store that *has* a policy (370 sessions, 21 MB) and none of the ones that do not. The audit log is decided **never** auto-pruned. **Narrowed at M599:** the default log is now one appended file per workspace and every reader opens *that* file first, so for the common case "one log" IS the project's whole history and the aggregation half is moot; what remains is retention, whose urgency M599 raised by turning `metrics` on by default. **Revisit when** a workspace's telemetry file passes ~100 MB, or when `doctor` grows a size check for the directory (not built: a metrics event is a few hundred bytes, and the operator asked for the memory, not for its pruning). | [S3/S6 + D3/D5](proposals/2026-08-observability-seams.md) |
| **A bounded failure *class* on `tool_call`, and the remaining unread events.** | At `metrics` an offline reader sees *that* a call failed and never *why* — which is what forced the loop detector's classifier into the loop. The exception is deliberate and narrow: a classifier output (`not_found`/`denied`/`bad_args`/`killed`/`nonzero_exit`/`other`), never the message, because `metrics` must stay content-free. | [S2/S7 + D6/D7](proposals/2026-08-observability-seams.md) |

**Closed at M584 — D6, the unread events.** Eight telemetry event types were emitted on
every run and displayed by no command. All nine unread types (the eight plus `privileged`)
now have a reader line in `jichi telemetry`, and `telemetry_events_lint.sh` check 10 fails
the build when a new event type arrives without one — the guarantee moves from "somebody
remembered" to "the gate says so". **What the measurement said, against the proposal's own
argument:** on the only real corpus available (42,652 events, one workload) just **two** of
the eight had ever fired — `hook` 15 times, `privileged` twice. The other six had never
occurred, because the features behind them are off by default (auto-context), need a
violation (`history_check`) or need hardware (`kinetic`). D6 argued from **emit sites**, not
from **occurrence**; so six of the new readers are unevidenced, and the honest claim is
structural, not empirical. **Still open from D6:** `history_check` is emitted to the journal
as well and `runs` does not read it there; and D7's bounded failure *class* on `tool_call`
is untouched.

**Closed at M583 — D4, the stamping path.** The row that stood here said *"the eight
direct `jc_eventlog_begin` call sites"* and named `kinetic` and `privileged` among them.
Re-measured before the work: **both already went through `telem()`**, and the real set was
**nine call sites in five files** emitting six event types — `prefix_churn`, `hook`,
`retrieve`, `test_edit`, `args_truncated` and four `args_repair` variants. Same class of rot
as M582's decks: a register row citing a count that nothing re-checks. The fix is structural
rather than per-site — one shared `jc_app_telem_begin()` in `src/chat/jc_app.c`, and
`telemetry_events_lint.sh` check 9 fails the build when an app-sourced event reaches
`jc_eventlog_begin` directly. It also made a **documented** claim true: `TELEMETRY.md` had
listed `depth`/`turn`/`run` as *"common fields on every event"* since M420.

## Closed — the changelog's drift is bounded (M431)

| Closed | What it does, and what it deliberately does not | Where |
|---|---|---|
| **`tests/smoke/changelog_coverage_lint.sh`** pins CHANGELOG.md's newest named milestone to within **10** of the ROADMAP's newest entry. | The row this replaces declined the lint for a real reason — *"a changelog entry is a judgement about what is user-visible, and a check demanding one per milestone would be satisfied by a line of noise"* — and named its own revisit condition: **"revisit if the file drifts again despite the note."** That condition was met by measurement, not preference: M402 added the coverage note after 75 milestones of silent drift, and **M431 then shipped with no entry anyway**. So the objection is honoured rather than overruled — the lint bounds *systemic* silence and demands **no** entry for any individual milestone, which means it would **not** have caught M431's own drift of one. That limitation is stated in the driver's header, because a lint that oversells its reach is worse than a loose one. Content is not checked and cannot be. Deliberately **not** git-dependent: a tighter rule could ask whether the newest milestone's commit touched `src/`, but the published snapshot ships a fresh history, so a history-dependent gate would behave differently in the tree people actually acquire. Teeth proven three ways (a simulated 34-milestone drift, a changelog leading the ROADMAP, and a broken extraction shape) — the second of which caught a defect in the lint itself: it reported a *negative* drift as "within the ceiling", a reassuring green over a broken state. | [CHANGELOG.md](../CHANGELOG.md) |
## Open — RAM tiers and libcs never measured

From M403, which asked the M400 question of [`LOW_MEMORY.md`](LOW_MEMORY.md): which
tiers are measured, and on what. Three grades of evidence are now tagged per tier
(real machine / cgroup ceiling / advice); these are the gaps that remain.

**Three of the four are now closed.** M430 closed the ≤64 MB row and the
minimal-libcurl row on threadwork, which is the access both were waiting for (see
Closed below); **M449 compiled uClibc** — a Bootlin toolchain, 11,593 checks, though
the unit suite only and not a `check-target`, and it says nothing about a ≤64 MB
uClibc box. What remains is the row below, and it is **narrower than it reads**: a
phone has run jichi since M461, so what is genuinely open is *Termux on a phone* —
an on-device toolchain, not the platform.

| Deferred | Why | Where |
|---|---|---|
| **Termux on a phone — an on-device toolchain.** | **Narrowed at M461, not closed.** The platform half is done: a **Motorola moto g(30)** (Android 12 / SDK 31, arm64-v8a) ran **11,571 unit checks / 0 failures** and all four offline surfaces, so "never run on a phone" no longer holds — see [PLATFORMS.md](PLATFORMS.md). What a phone still adds is **Termux's own libc/filesystem quirks and battery behaviour on a handset**: no on-device toolchain is installed on that phone, so the build-it-on-the-device claim stays **tablet-only** (M459, Lenovo TB336FU). A second, narrower gap sits beside it, and **M507 moved it**: the first smoke driver has now run on a phone (`smoke_lint`, 17/17, exit 0), but the **rest of the tier has not**, and the reason is cost rather than portability — 9m35s for that one driver against 7.8s on the bench, a fork penalty of roughly **27x** once the handset is held awake. **Revisit** opportunistically; the twenty-minute estimate was wrong for the smoke half. | [LOW_MEMORY.md](LOW_MEMORY.md#platform-notes) |

## Closed — found by driving jichi on zigodot (2026-08-12)

Four defects measured while using jichi to author and work an assignment in another
repository; **all four are closed** (M409 the hint-ladder truncation, M410 the
attempt verdict, M411 the `--model` override, M412 the unprovable gate — see Closed
below). The section was kept one register cycle as the worked example of a
dogfooding run converting into fixes; **retitled at M463**, because the cycle has
passed and an `## Open` heading with no rows is how this page ends up advertising
work that does not exist. The full write-up, with the fixtures and the token numbers,
is `zigodot/docs/analysis/2026-08-12-driving-jichi-on-zigodot.md`.

## Open — found by the second zigodot campaign (2026-08-13)

| Deferred | Why | Where |
|---|---|---|
| **A hang before the first tool boundary is outside every envelope budget.** *(re-scoped M503: M438 closed the reporting half -- the journal's `start` now lands before the first network touch and the control socket answers earlier, so a supervisor is no longer staring at a 0-byte file. What remains is the BOUNDING half: the deadline should cap the PROCESS, not just the loop.)* | Measured: an `--auto` run with `--deadline 12m`, `--journal`, and configured `timeouts` (connect 20 / stall 120 / request 900) hung for **22 minutes producing a 0-byte journal** — not even the `start` event — and 121 bytes of stderr (the M411 pin note). Every liveness mechanism jichi has assumes the loop is running: the deadline is enforced at tool boundaries, `--heartbeat` is jsonl-only and fires from the model-call progress callback, and the journal opens with the run. A pre-loop hang (this one was somewhere between arg processing and the first request — the pin note printed, nothing after) is invisible and unbounded. A supervisor's only tell is silence, indistinguishable from a long model call. **What to build:** journal `start` (or a stderr line) *before* the first network touch, and a wall-clock backstop that covers the pre-loop phase — the deadline the user asked for should bound the *process*, not just the loop. **Not diagnosed to root cause** (one occurrence, killed gracefully after announcement; a second concurrent jichi held the same workspace/HOME, so shadow-repo or session-store contention is the suspect). | [AUTONOMY.md](AUTONOMY.md) |

## Closed at M470 — the two improvements the architecture sweep asked for (M469)

| Deferred | Why | Where |
|---|---|---|
| **`tier-v-arch.sh` should snapshot the tree ONCE per sweep, not once per target.** | `jc_rig_ship_tar` runs inside the per-target loop, so a commit landing mid-sweep gives later rows a different tree than earlier ones and the table stops being comparable — silently, since every row still prints a check count. It is why M469's commit had to wait for a 21-target sweep to finish before anything could be committed, which is a real workflow cost on a rig meant to run unattended. **Fix:** ship once to `$DIR/src`, then copy per target. | `scripts/tier-v-arch.sh` |
| **`tier-v-arch.sh` should preflight `pipe()`/`select()`/fork+exec per target and say "the environment cannot run the subprocess tests".** | The six MIPS rows reported *"unit suite RAN and reported failures: 11,627 checks, 73 failures"*, which reads as an accusation against jichi. The cause is one level below it: `pipe()` fails under `qemu-mips` with zig's musl (MIPS is the one Linux architecture whose `pipe` syscall returns both descriptors in registers), and all 73 failures are downstream — the seven failing files are exactly those that spawn a subprocess or read `/proc`. A row that blames the program for its emulator's gap is worse than no row, and `tier-v-openbsd.sh` already has the pattern to copy (its procfs check). **Also worth recording in the same pass:** armeb cannot print a `double` (the *literal* `0.2` renders as `-2.35344e-185` while its stored bytes are correct big-endian IEEE-754), so that target is unusable for measurement through no fault of jichi's; and `x86_64-linux-muslx32` builds clean but cannot execute because this kernel lacks `CONFIG_X86_X32`. | `scripts/tier-v-arch.sh`, [PLATFORMS.md](PLATFORMS.md) |

> **Both closed at M470, and the second found more than it was filed for.** The rig
> snapshots once to `$DIR/src` and copies per target, so a mid-sweep commit can no longer
> make the table non-comparable. `scripts/envprobe.c` runs per target before the suite and
> the MIPS row now reads
> *"11,638 checks, 73 failures — env probe says [pipe=FAIL select=FAIL forkexec=ok
> procfs=present]: the EMULATOR cannot run the subprocess tests"*, classified `no-run`
> instead of accusing jichi. The probe independently confirmed the manual diagnosis
> (`pipe` and `select` fail, `fork`+`exec` does not) and it only ever **downgrades** an
> accusation — a passing suite reads the same whatever the probe said. It also reports
> `procfs=present|absent`, because *present but different* is the illumos hazard
> [`PLATFORMS.md`](PLATFORMS.md) now names.

## Closed by measurement — parallel_abort does NOT reproduce on Guix (M458; re-scoped M466, corrected M467, CLOSED M468)

> **The premise of this row changed on 2026-08-17.** It was filed as isolated to Guix
> System — the one platform whose row cannot rebuild itself — so the next step was
> believed to be building `scripts/tier-v-guix.sh`. The OpenBSD row, once
> `JC_SMOKE_KEEP_GOING=1` let it run to the end, reports **`parallel_abort`: "parent did
> not exit within 15s of SIGINT — abort/reaping deadlocked"** — word for word the Guix
> failure — and it fails standalone there too. **So it reproduces on a platform with an
> unattended rig** (`scripts/tier-v-openbsd.sh`, an 11 MB ISO and one command), and
> Guix is no longer on the critical path. Iterate with
> `sh tests/smoke/run.sh parallel_abort` in that guest. The three suspects named below
> — all landing after the Guix measurement and none re-measured — are now testable
> without Guix at all.
>
> This also means the row was never really about Guix: it is about a **non-glibc,
> non-Linux** reaping path, and the two platforms share that rather than sharing
> Guix's non-FHS layout. The M450 process-group explanation is now doubly incomplete.
>
> **CORRECTION (M467), and it retracts the paragraph above.** OpenBSD's identical
> failure turned out to be a defect in **jichi's own test harness**, not in the agent:
> `parallel_abort.sh` backgrounded a subshell (`( cd "$ws" && "$BIN" ... ) &`) and
> captured `$!`, and on OpenBSD ksh `$!` names **the subshell**, not the command —
> measured, against dash and bash, which both perform an implicit exec and name the
> command. So the driver SIGINT-ed a shell, jichi never received the signal, and the
> check reported *"abort/reaping deadlocked"* about an agent that was never asked to
> abort. With `exec` added it passes.
>
> **Therefore the "both platforms share a cause" claim written at M466 is withdrawn.**
> Guix's `/bin/sh` is **bash**, which does perform that implicit exec, so this
> explanation does **not** transfer. Guix's instance is **still open and still
> unexplained** — but it is now cheap to settle rather than blocked: the `exec` fix is
> shipped, so one re-measure (`sh tests/smoke/run.sh parallel_abort` in a Guix guest)
> distinguishes "the same harness bug after all" from "a real defect in the
> `spawn_parallel` reaping path". Until that runs, neither claim is earned — and the
> earlier framing was exactly the over-reach this register exists to prevent.
>
> **CLOSED at M468, by measuring it.** Guix System 1.5.0 (`guix describe` d58da8a,
> kernel 6.17.12-gnu, `/bin/sh` = bash 5.2.37, **no `cc` and no `c99`** — M458's lesson
> measured rather than recalled), built at HEAD `f025185` with `CC=gcc` inside
> `guix shell`: `-Werror` C89 build **clean**, **11,594 unit checks / 0 failures**, and
>
> ```
> --- smoke: parallel_abort
> ok 1 - SIGINT-ed parent exited (reaped both stalled children)
> ok 2 - exit was prompt (1s) -- the abort path, not the 120s watchdog
> ```
>
> plus `parallel_hang` 2/2, `signals` 4/4 and `stop_reason_capped` 5/5 (the last fails on
> OpenBSD, confirming *that* one as ksh-specific). The likely fix is one of the three
> commits this row already named — `448616d` is the best structural fit, since a child
> spinning on `EPIPE` instead of dying is exactly a parent that never leaves `waitpid`.
>
> **Caveat, so the closure is not stronger than the evidence:** this is not a
> byte-identical re-run of M458's row (that was a Guix System from the recorded config;
> this is `guix shell` in the published desktop image, 6 vCPUs / 8 GB). The defensible
> claim is *does not reproduce at HEAD*, not *was never real*. And **I predicted in
> writing that it would still fail** — see
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
>
> **The Guix image itself, same day:** the operator downloaded
> `guix-system-vm-image-1.5.0` and its source tarball. GRUB in that image turns out to
> read *and* write serial — so M450's "cannot be driven" is wrong at the bootloader
> level, and the boot entry (kernel, initrd, root UUID, args) is now extracted and
> recorded. What resists is GRUB's serial **input** reliability across four attempts;
> the decisive error was `unspecified search type`, i.e. GRUB parsing a bare `search`
> whose search-type argument was dropped in transit. Three ranked next moves — QEMU monitor
> `sendkey`, a direct `-kernel`/`-initrd` boot (the `tier-v-tiny.sh` pattern, needing
> the two files copied out via the existing Debian rig rather than root), or building
> the headless image from the now-parameterised `.scm` — are in
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
> **The one step that needs a human** is five minutes at that image's graphical console
> to enable sshd; everything after it is automatable.

| Deferred | Why | Where |
|---|---|---|
| **Diagnose why `tests/smoke/parallel_abort.sh` deadlocks on Guix System.** | After SIGINT the parent never exits; the driver reports *"abort/reaping deadlocked"*. **Two explanations were ruled out before filing:** it is **not a timeout artefact** (identical failure at `JC_SMOKE_TIMEOUT_MULT` 1 and 6, so a slower machine is not the cause), and it is **not the absent process groups** that explained the `guix shell -C` failure at M450 — `/proc/self/stat` shows a normal non-zero `pgrp` on the real system. The unit suite is green there (11,599 / 0), so it is isolated to the `spawn_parallel` abort/reaping path in `jc_parallel.c` / `jc_bg.c`, which uses `setpgid` + `kill(-pid, ...)`. **This also corrects M450**, whose process-group explanation was sufficient for the container and is now known to be incomplete. **Not diagnosed in the session that found it** because the row it belonged to was otherwise complete and a signal/reaping deadlock deserves its own sitting rather than the tail of a long one. **Revisit** with the Guix image still buildable from the recorded config — it reproduces in one command. **RE-SCOPED at M466: re-measure before debugging, and three named suspects.** The deadlock was measured at `8f1d4b6` (2026-08-15), and **34 commits later three fixes landed in its exact code path**, none of which has been re-measured on Guix: `448616d` — every child inherited jichi's *ignored* SIGPIPE, so a pipeline producer spins on `EPIPE` instead of dying when its consumer exits (the textbook shape of a parent stuck in `waitpid` on a child that will not die); `0d5b030` — a timed-out capture killed only its direct child and **orphaned the rest of the pipeline**; and `ac166d5` — the smoke harness reaped only the *last* mock a driver started, whose symptom on FreeBSD was a **2.19-second driver reported as a timeout 118 seconds before any deadline, with all five of its checks passing**. That last one is the closest match to the observed report, though its FreeBSD mechanism (a `timeout(1)` that waits for the whole process group) should not apply under Guix's GNU coreutils — which is exactly why it needs measuring rather than reasoning about. **Two blockers removed at M466:** the machine definition no longer hardcodes one host's ssh key path (it reads `JICHI_BENCH_PUBKEY` and refuses when unset), and the re-measure now costs **one boot** rather than one per defect — `JC_SMOKE_KEEP_GOING=1` reports the whole failure set, and `sh tests/smoke/run.sh parallel_abort` runs just this driver. **Still blocked on Guix itself:** `guix` is not installed on the threadwork bench and building a Guix System image needs a working daemon and `/gnu/store`, so this row belongs to whichever machine has Guix. `scripts/tier-v-guix.sh` is deliberately **not** written blind — an untested rig for a platform nobody can run here would be a fourth never-executed artifact, and the honest first step is a re-measure that needs Guix regardless. | ROADMAP M458/M466, [LOW_MEMORY.md](LOW_MEMORY.md) |

## Closed by measurement — the /tmp assumption and the crash class (M457)

Both rows below were filed at M453 with the reasons they were not done that day. They were
done at M457, in the order the plan's own *"what would make this plan wrong"* section
specified: the crash class first, because the audit found it wide (19 sites on a narrow
pattern, 43 on a broader one) and it is valuable on every platform rather than only on
`/tmp`-less ones.

| Closed | What it measured | Where |
|---|---|---|
| **Route the unit suite's fixture paths through `TMPDIR`.** | 158 literal paths converted, plus `jc_test_tmpdir()` for the six directory uses. The idiom was **copied from `_smoke.sh`'s `smoke_tmp`**, not invented. Verification is the **check count, not the colour**: 11,599 / 0 with `TMPDIR` unset, `/tmp`, short, and ~100 chars. That mattered — `test_git.c` built its repo through `system("mkdir -p /tmp/...")` while the C side used the helper, so with any other `TMPDIR` the fixture landed outside the repo and **ten checks silently did not run** while everything stayed green. Three other classes needed reading rather than regex: string concatenation, a non-literal format argument, and expectations whose matching input is literal JSON fixture data. | ROADMAP M457 |
| **Audit for a recorded null check followed by a dereference.** | Two harness verbs added, because the harness had no way to express "check, then branch": `JC_REQUIRE` (records and yields) and `JC_VEC_STR` (fetch-or-NULL). Same gap as M450's `t_skip_one`. **Shown to work on the machine that exposed it:** the Android 4.4 tablet went from *aborting at the 4th of 123 test files* to running to completion — 11,534 checks, 47 failures, zero crashes. The 47 are themselves a finding: they cluster on every test that forks or shells out, because **Android has no `/bin/sh`**, which musl's `system()` invokes. | ROADMAP M457, [TEST_INTEGRITY.md](TEST_INTEGRITY.md) |

## Closed by measurement — A5, line breaking for German compounds (M579)

**The item as scoped:** *"UAX #14 line breaking, so a German compound does not split
mid-word."* Raised at M554 when the chrome-width budget was first measured, and made to look
urgent by M568 shipping German chrome for real.

**Both halves of its premise turned out to be false, and both were cheap to check.**

**jichi does not wrap output.** `term_cols` (TIOCGWINSZ, then `$COLUMNS`, then 80) appears in
exactly one file — `src/tui/jc_term.c` — and only for cursor arithmetic while the user is
*typing*. No renderer takes a width; `jc_mdrender` has no width parameter at all. Output is
emitted as-is and **the terminal** wraps it, which `jc_tui.c`'s own comment says plainly: *"the
renderer emits them, the terminal wraps on them."* So there is no break-point decision in
jichi's code for UAX #14 to improve.

**And nothing is wide enough to wrap.** Measured by `tests/test_width.c`, widest non-exempt
chrome line per language:

| en | de | es | ja | zh |
|---|---|---|---|---|
| 75 | **76** | 59 | 64 | 52 |

against a budget of 78 — 80 columns minus a two-space indent. **At a standard terminal no chrome
line wraps in any language**, German included, and the budget exists precisely to keep that true:
a translation that would wrap fails the build the day it lands.

**What is genuinely left is a different question, and it needs a measurement I cannot make.**
Should jichi *pre-wrap* its chrome at word boundaries, so that a terminal narrower than ~78
columns breaks between words rather than mid-compound? That is a new capability rather than a
fix, and its value depends entirely on whether a hard mid-word wrap actually harms a listener —
which depends on the reader, the emulator, and how continuation lines are marked. **Nobody here
has heard it.** Building UAX #14 against an unmeasured harm, in a code path that does not exist
yet, would be the shape this project has already paid for twice this month.

**Reopen it when:** somebody reports that a wrapped chrome line reads badly at a narrow width.
Then the work is *pre-wrapping chrome*, and UAX #14 is one possible implementation of it.

## Closed by measurement — uClibc, on the toolchain it was waiting for (M449)

The row said it was blocked on a toolchain and named the unblocking condition: *"Revisit
if a buildroot/OpenWrt toolchain becomes available."* Bootlin publishes buildroot-built
toolchains, so the condition was already met and nobody had checked — the M326b failure
in its mildest form, since the reason was true when written and quietly expired.

| Closed | What it measured | Where |
|---|---|---|
| **Compile jichi against uClibc.** | Bootlin `x86-64--uclibc--stable-2025.08-1` (gcc 14.3.0, uClibc-ng), `HAVE_CURL=` + `-static`: **zero diagnostics** under the project's mandatory `-std=c89 -pedantic -Wall -Wextra -Werror`, **11,593 unit checks / 0 failures** run natively, all offline surfaces OK, `--version` peak RSS **384 KB** with 0 shared libraries. The row predicted the risk was "genuinely lower than the Darwin case" *because* the one libc-dependent feature is probe-gated rather than `#ifdef`-gated — and that prediction was **half right in an instructive way**. There was indeed no rotted `#ifdef` branch. But the probe itself was wrong: it omitted the build's warning flags, so it answered "yes" for a `malloc_trim` that uClibc-ng declares only under `__USE_GNU` while still exporting the symbol, and **every translation unit then failed under `-Werror`**. Probe-gating was the right design and was not, by itself, sufficient. | [PLATFORMS.md](PLATFORMS.md#libc-and-ram-tiers), ROADMAP M449 |

## Closed by measurement — two RAM rows, on the machine they were waiting for (M430)

Both rows above said, in the text, that they were blocked on access to one machine
rather than on a design question. That access arrived; here is what the sitting
produced. Kept rather than deleted because each row's *reason* contained a prediction,
and one of them was wrong in an instructive direction.

| Closed | What it measured | Where |
|---|---|---|
| **Run jichi on a machine with ≤64 MB of physical RAM.** | On a kernel + busybox initramfs guest: offline surfaces at **64 MB**, a **verified model turn at 80 MB**, jichi's own peak **1,160 / 1,396 KB**. The row predicted the tier "is not in doubt on jichi's side" and that was right — but it framed the open question as *the three things a real board pays*, and the actual binding constraint turned out to be **the kernel**: swapping Debian's generic kernel for Alpine's `virt` moved the floor 96 → 64 MB with jichi byte-identical. Also measured, and not predicted by anyone: a stock Debian 12 cloud image **cannot boot at 128 MB at all**, so the `~128 MB` tier was never gradeable with a distro. Still a VM, not a board. | [LOW_MEMORY.md](LOW_MEMORY.md), [analysis](analysis/2026-08-13-ram-tiers-whole-machine.md) |
| **Build a minimal single-TLS-backend libcurl and link it into a static musl jichi.** | **804 KB** `--version` RSS, 0 shared libraries, and it **makes a real model call** — against the system libcurl's 10,012 KB. M403's honest bound ("between 0.5 and 8.6 MB") collapses to the bottom of its own range. The row's estimate of the work was accurate ("an afternoon of libcurl `configure` flags"); what it did not anticipate is that the four blockers were all *toolchain* facts rather than flags — `zig` as a multi-call binary breaking `CMAKE_AR`, mbedTLS's `-Werror` against a newer clang, `zig cc` linking UBSan by default, and a mock deleted mid-test. Uses **mbedTLS**, not the OpenSSL the recipe names, so it is a sibling claim rather than the same number. | [LOW_MEMORY.md](LOW_MEMORY.md#build-time-footprint-reduction) |

## Closed by measurement — two recommendations withdrawn (M406/M407)

Kept because a withdrawn recommendation that leaves no trace gets re-proposed. Both
were raised by me after M405, both on **ad-hoc greps whose extraction was wrong** —
the same blindness the project's lints are written to avoid, met four times in one
day from my own probes.

| Recommendation | Why it was withdrawn |
|---|---|
| **"9 of 12 curriculum modules have no link forward" — add next-links.** | **All twelve already carry** `[◀ Prev] · [▲ Curriculum map] · [Next ▶]`. My probe grepped for `Next:` and `→` and missed the actual `[Next ▶]` form. Acting on it would have "fixed" a unanimous convention into duplication. Residue: the invariant is now pinned by `docs_locators_lint.sh` check 6, whose comment records this. |
| **"Nothing ever runs a documented command" — build an executable doc-command tier.** | **`subcommands_lint.sh` checks 7–11 (M326e) already run every advertised subcommand bare**, in a throwaway workspace, with exclusions named and check 11 guarding stale exclusions. I had read the *static* drivers (`doc_commands_lint`, `docs_flags`, `subcommands_lint` checks 1–6) and generalised from them. A separate driver was written and **deleted** rather than shipped: for six forms it would have been a duplicate wearing a new name. Residue: those six *flag-carrying* forms were genuinely uncovered — the strip between "the verb exists" and "the bare verb runs" — and are now executed by `doc_commands_lint.sh` check 5, which asserts they are not **rejected** (exit 2), the precise M375 signature. Its reach is stated in the driver: six forms, four of which legitimately exit 1 on an empty fixture HOME. |

**The transferable lesson**, recorded because it cost four probes in one session: *a
number produced by an ad-hoc grep is not a measurement.* Every one of these
extractions matched something real and missed the thing it was looking for — the
comment quoting its own subject, the wrong output marker, the wrong link form, the
wrong error signature. The rule that survives: before recommending work on the
strength of a count, either derive the count from ground truth the code owns, or
verify one instance by hand.

## Closed during this program

Kept briefly, because "we said we'd do this" is worth being able to check.

| Was deferred | Closed by |
|---|---|
| **A `--max-tool-calls` recommendation shaped by the WORK, not one number** | **M595** — and the shape hypothesis was WRONG. Six driven runs across two models and two task kinds: the documentation run ended on the *call* cap and the mechanical sweep on *tokens*, the opposite of the proposed advice. What fits every run is arithmetic rather than shape — with budget `B`, cap `C` and `t` tokens per call, tokens bind when `B / t < C`, and `t` is a property of the backend (21–35k per call cacheless, against 92% cache on one HRZ session). In [AUTONOMY.md](AUTONOMY.md) §1 |
| A Rust section in `BIBLIOGRAPHY.md` | **M636c** — 9 entries, 7 free, weighted toward ownership and the FFI seam, because Rust was the only language in the matrix with a graded course and zero literature. The intended cost was paid as designed: `bibliography_lint` check 5 went red until every count was redone, and the check itself was wrong on its first run (no terminator for the last section) |
| A file-I/O task in the C systems course | **M636g + M636h** — tasks 76 and 77 of a new sibling course, "C: files & structures": open a file that may not exist, survive a short read, refuse one over the cap; then replace one atomically via temp + `rename`, `0600` at creation, the temp removed on every failure. It was the one gap in the M636 review that was **not** defensible as a scope decision — 0 of 79 graded tasks against 26 files of `src/` that do it |
| `run` on telemetry + `ws` on the journal's `start` (the S1 join) | **M420** — two conditional fields, envelope struct untouched. Proven by performing the join rather than asserting presence: `telemetry_join.sh` compares the journal's `run` against telemetry's and requires **every** event to carry it (partial stamping would join some behaviour and silently drop the rest). Teeth: the perturbed binary reports `journal run='…' vs telemetry run=''`, the exact pre-M420 state |
| `verify_stuck` + `test_assertion_edit` in `runs` NOTES | **M420** — `stuck=N` and `goalposts=N`, plus the `ws` column the journal now supplies; printed only when non-zero, because an always-present `goalposts=0` trains a reader to skip the column the one time it matters. The plan called this "two counters" and it was three: a `ws` the reader could not read would have been written for nobody |
| Nothing checks that an authored assignment's gate can fail | **M412** — `grade --expect-fail`, the smaller option the row recommended: exit 0 iff the verify fails on the untouched tree, HOLLOW (exit 1) when it is already green, `--record` refused. A command composes with every authoring path (scaffold, hand-written, a model that ignored its brief); prompt text binds only the model that reads it |
| An explicit `--model` reported active, then overridden by config routing | **M411** — `--model` pins the run (routing disabled, one-line note, `-q`-respecting); `--route-fast`/`--route-strong` alongside it keeps routing. CLI-only on purpose: the TUI user sees the `[route]` banner and has `/route off`, which is exactly the visibility the headless `-q` user lacked |
| A colon in a `hints:` value silently deleting the whole ladder | **M409** — deferred for about six hours: the row recommended "the lint first, do not touch `jc_yaml` under time pressure", and the driver was indeed built first — born red, it measured **64 of 80** shipped ladders short, far past the sampled five. The parser fix then turned out to be eleven contained lines (`quoted_whole_line` before `find_colon`, quoted keys still mapping), every ladder recovered with no prose changed, and the M289 skip now counts what it drops so `hint` can say so |
| `attempt` reporting PASS over ten goalpost warnings, then deleting the evidence | **M410** — all three separable pieces from the row: the counter (`test_edits` on the envelope), the verdict (**TAINTED**, exit 1, via a pure unit-tested word function), and `--keep-worktree`. The "0 hints used" oddity resolved itself: the ladder was unreadable (M409), so the learner *had* no hints to use |
| **Tell the model when its own edit moved the goalpost** | **M435** — one sentence appended to `edit_file`'s and `apply_patch`'s own results, escalating with a count. The row's rejection held: not a refusal, because correcting a genuinely wrong test is fair work and a refusal is routable into `sed`. What the row did not anticipate was the precondition — the two tools each carried a near-identical five-part M88 block, so a sixth destination meant factoring them into `tu_report_test_edit` first. Two of my own bugs were caught by the new tests: `"the 2th test assertion"` from a hand-built ordinal (the same bug `jc_toolloop_render` fixed three milestones earlier), and a fixture that failed all six checks for a missing `read_file` rather than for the property under test |
| **Stop advertising tools a depth gate will refuse** | **M436** — the row's rejection held (the gate is right, the advertisement was the bug) and the fix went one step further than the row asked: the fact is now ONE field on `struct jc_tool`, read by the builder for the omission and by `jc_tool_execute` for the backstop, and the three hand-written `agent_depth > 0` checks in the tool bodies are deleted. Stating it three times and reading it in none of the places that build the advertisement is how the two halves could disagree at all. Cost: 40 definitions name the new field, because `-Wextra` does not let C89's implicit zeroing of a trailing member pass silently — and naming it everywhere is the better outcome. A name table plus a lint was rejected: with the gates gone the table would be its own ground truth |
| **One structured report shape for both delegation tools** | **M437** — the shape shipped with both named defects fixed: the grandchild poison (one save/restore of the per-run slots around every `jc_tool_execute`, restoring rather than zeroing so this run's own earlier failure survives) and the capped parallel child (its stop reason now travels the pipe as a NAME, so an enum renumbering cannot silently reinterpret it). Two things the row did not anticipate. A **policy block never reaches the loop's `is_error` branch**, so the denial case -- the single most valuable one -- bypassed the only recording site; `block_message` is the chokepoint for both block sites and now records there, classified as DENIED directly rather than inferred from wording. And `files_changed[]` shipped for a `spawn_parallel` write child ONLY: a worktree gives a per-delegate baseline, a nested in-process run has no equivalent, and manufacturing one would mean a shadow checkpoint per delegation appearing in the user's `/undo` stack. The row's rejection of forwarding the transcript held |
| **Arm liveness before the first tool boundary** | **M438** — all three parts: an `open` journal record at file-creation time (with the pid, so a supervisor can check the process rather than infer life from file growth), a full control boundary before the first request, and a poll-only one BETWEEN the tool calls of a round. The poll is deliberately not the full boundary: that folds steering into history, and a user message between two tool results of one round is malformed under M364's contract. Writing the test taught more than the fix did — the first cut asserted that `status` ANSWERED and passed with the fix reverted, because the client waits 300s; the property is latency, and measured over a 12s round it reads 1s with the fix and 7s without. One limit stands and is stated: the socket is still not served DURING a model call, since polling from libcurl's progress callback would let a `pause` block the HTTP read |
| **Cut the jsonl `tool_result.preview` on a UTF-8 boundary** | **M439** — fixed with the in-tree helper the row named, behind a pure `jc_agentjson_preview` in the module that owns the jsonl schema. The row understated the frequency: 512 is not a multiple of 3, so splitting a character was the NORMAL outcome for non-ASCII output long enough to truncate, not an edge case. It also missed a second site -- `copy_trunc` in `jc_cli.c`, backing `jc_tool_arg_summary`, which reaches stderr and the telemetry `args` field. Two of my own assertions were inverted before the driver was right: "the last byte is not a continuation byte" is FALSE for every valid string ending in a multi-byte character, and a length-modulo test ignored `read_file`'s 7-byte ASCII gutter |
| **Tell the model the price list when the numbers are measured** | **M440** — a `# Cost model` section with the effective caps and §6 items 5-8, gated tri-state on the cache verdict exactly as the row asked. Two things the row did not anticipate. The gate had to read the CONFIGURED cache setting, not the observed hit-rate: a running statistic in the system prompt changes the cached prefix every turn and destroys the caching it describes, so the section's signature takes five integers and no `jc_app` -- the hazard is structurally impossible, not merely discouraged, and the remaining gap (a backend that accepts a caching request and returns nothing) is what the explicit flag is for. And the four built-in caps had to move from four `#define`s in four tools into `include/jc_toolcaps.h` first, since a second reader of a number that must agree is the drift M296 forbids; `tool_caps_lint` failed loudly on the move and now reads one file instead of four |
| **Teach `mockmodel` to emit a `usage` block on the chat path** | **M441** — the tool path now sends usage in a separate final chunk with an empty `choices`, the shape a real provider uses. The row's fear of a blast radius was right in kind and small in size: ONE driver moved. `learn_on_stop_cost` ran on `--budget-tokens 1`, which only sufficed because a tool call cost nothing; it now ends budget_exhausted at 25 tokens, and the fix is a bigger budget plus a comment saying why it is not 1. The payoff was taken at once — `budget_panel.sh` gained the RATE check the row was written to unblock, proven two-sided against the old rig, which reads `0/400000 tokens (0%)` with no rate at all |
| **A tool-call id in the jsonl stream** | **M442** — the provider's own id on both `tool_call` and `tool_result`. "Small and additive" understated one thing: it is a shared callback signature, so all four front-ends had to change, and three of them deliberately IGNORE the id — the TUI (a human reads lines in order), ACP (it mints its own `toolCallId` an editor already pairs by, and swapping it would change bytes on a live wire), and the fork pool (a board and an aggregate, never a paired timeline). Each says so at the parameter. The driver pairs two calls to the SAME tool, since two different tools can be paired by name and would have passed with no id at all |
| **A `degraded` flag on the terminal object** | **M443** — an object rather than a boolean, counting `unanswered` / `approval_unavailable` / `privilege_refused`, and present only when one is non-zero so a supervisor tests for the key (the M420 non-zero-gate argument). The row listed `confirm_tool` NULL ⇒ allow among the degradations; that case is an `--auto` run, and it is deliberately NOT counted — auto-approval is the operator's instruction, not a decision taken in their absence, and counting it would make every `--auto` run degraded and the flag worthless. `stop_reason` is untouched: the flag reports, it does not judge |
| **`jichi sysmsg` omits every envelope-gated prompt section** | **M444** — and `context` with it, which the row did not name but which mattered more: its whole job is to size the prompt a run sends. The row said the fix was "a dispatch-order change with a blast radius worth measuring"; measuring it showed moving the dispatch was the WRONG fix, because MCP servers are connected in between and a read-only prompt dump would have begun spawning subprocesses. The arming was split from the journal instead: `envelope_arm` takes the path as a parameter, an introspection command passes NULL, and the driver requires the runs directory to stay empty. Measured effect on `context`: the environment slot goes from ~25 to ~136 tokens |
| **`tests/smoke/mincurl_recipe_lint.sh` is promised by a script and does not exist** | **M445** — it exists, and the script's claim is a fact again. Built with the caution the row asked for: only `--disable-*`/`--without-*` tokens, from BOUNDED regions (the doc's fenced block, the script's single `set --` list), floors of 15 on each side, and curl's own prefix, cross-host and TLS-backend options excluded because they legitimately differ between a page teaching one build and a script parameterised over three rungs. Proven three ways — a flag dropped from the script, one dropped from the page, and a reshaped invocation that trips the floor at 0 instead of comparing two empty sets |
| `docs/MCP.md`, the one entirely missing feature page | **M395** — written from the source rather than from memory, which caught three of my own wrong claims mid-draft: `headers` is an array of whole header lines (not an object), `type` is inferred from whether `url` is set, and `autoApprove`/`deny` accept `"*"`/`true` for every tool a server offers. The object-shaped `mcpServers` that Claude Code and Continue use now **warns** instead of configuring nothing in silence |
| STATE-THE-REACH in the system prompt + doctor | **M387** — a conditional prompt line when an edit scope is armed (deterrent framing; the unit test forbids it reading as an invitation) plus a doctor line, born red at both tiers. It was parked for one batch and closed two milestones later; the row outlived its deferral by a day and was found by reading this page back, which is the failure this page exists to prevent |
| Skip the lossy mid-turn pass once it is exhausted | **M361** — the exhaustion latch: a dry pressed pass records the exact length at which the sliding window next releases a candidate (oldest protected candidate + keep + 1; every latch expires within keep+1 appends, so a conservative detector can only delay one scan, never skip one forever — the exact re-arm the row demanded); on the driver fixture 12 pressed passes collapsed to 4 full scans |
| The CLI `undo` leaving saved sessions believing the pre-undo state | **M350** covered it from the resume side (the mtime drift note); the residual sliver (a clock moving backwards between undo and resume) is not worth a mechanism — kept one register cycle per the row's own note, closed by the 2026-08-10 sweep |
| The DECLARE-THE-GATE repetition (GATE_INTEGRITY §8's n=1 caveat) | **M344** — five per arm on the #45 setup: the tampering did not recur in either arm; the sentence halved the pre-endpoint poking; table in GATE_INTEGRITY §8c |
| The wizard's 17-option first screen, two questions under one list | **M326i** |
| The `context` subcommand reporting `rules ~0` | **M311** |
| The system prompt's unnamed remainder | **M312** |
| Per-tool definition sizes | **M313** |
| Telemetry joined to the tool registry | **M314** |
| The history as one number | **M315** |
| Flipping `attempt`'s default to `core` | **M320** — closed as *decided against*: the null held on a second model, but the same runs produced a better objection than the one they dissolved |
| `context tools` under-reporting the live toolset | **M325b** — one shared registrar for main/context/doctor; 16 → 26 on a git repo, with MCP named as the one exclusion |
| `spawn_parallel` at 4/10 | **M325** — the log *could* say why after all: 3 watchdog timeouts on a 300 s default against 300–462 s children, 3 forks that could not happen |
| The `glob` capability gap | **M324** — `list_files` gained a `pattern`, so the schema objection was fixed rather than argued with, and `glob` graduated from hint to alias |
| Measuring a `core` attempt that needs a hint | **M319** — 12 runs, 0 hint calls: the model never asks, so the "capability cost" was theoretical |
| Whether the craft section earns its tokens | **M318** — measured; a null on a 31B model, so it is now off under `--lite` and unchanged elsewhere |
| A TUI `/context tools` / `/context history` | **M317** — and better there than in the CLI: the TUI holds the *live* history, so no save lag |
| A `doctor` check for never-called tools | **M316** — the objection was answered by changing the evidence axis to *distinct sessions* and advising the lever (`toolProfile`) rather than individual tools |
| German editions of the plain-register assignments | **M309b** |

## Open — what the model asked for and M431 did not build (2026-08-13)

M431 implemented Tier 0 (five promises made true). The tiers below are designed, with
every rejected alternative, in
[`proposals/2026-08-model-facing-orchestration.md`](proposals/2026-08-model-facing-orchestration.md).
They are parked as a **scope decision**, not an oversight: the release is weeks away and
Tier 0 was the part that was making jichi's own documents untrue.

The thesis they serve is one sentence: **jichi shows the model its failures and shows the
human its false successes.** Every Tier 1/2 row below is one instance of that.

**Since closed, and removed from the table per this register's own rule** (an item is
removed when done, with the closing milestone in the ROADMAP): the M331 finding at the
periodic verify site and the changelog-coverage bound (**M431b**), the run id on the
machine surface (**M431c**), the hollow completion green (**M431d**), the workspace
lease (**M431e**), the ambient budget panel (**M431f**, shipped OFF so M347's decision
is measured rather than overruled) and the `--connect --output json` downgrade
(**M431g**). Two rows below were **missing entirely** until 2026-08-14 --
the refused-tool advertisement and the UTF-8 preview cut -- so they were findable nowhere
while being named in the proposal, which is the one failure a findability register cannot
afford.

| Deferred | Why | Where |
|---|---|---|
| **Charge a subagent's tool calls to the run** — *closed by M431 for the check and the counter*; what remains is the **journal**, which stays depth-0. | The enforcement half shipped (`env_budget_applies`). The journal deliberately did not: a forked parallel child appending to the same file would interleave with its siblings, and the parent already reconciles each child's piped count. So `runs`' `tool_calls` for a fan-out is the reconciled total while the journal's `tool_call` events remain top-level only — an asymmetry worth stating before someone joins on it. | ROADMAP M431 |
| **Daemon fleet-worker changes (`fresh`, exit codes, the `--output json` downgrade).** | **Promoted from a deferral to a planned EXPERIMENT with a discard gate**, at the maintainer's direction: [`plans/2026-08-daemon-fleet-worker.md`](plans/2026-08-daemon-fleet-worker.md) carries the honest implications, the design, and — written before any code — the criteria for discarding it. Built on `feat/daemon-fleet-worker`, field-tested on zigodot and chrtext over several sittings, then reviewed. The reason it is not simply built: history persisting across requests is **documented, correct behaviour for a warm interactive helper**, so the real question is whether the daemon should be a fleet worker at all rather than whether it has a bug. One of the three WAS an unambiguous bug (`--connect --output json` silently becoming text, on a Stable surface) and **landed on master at M431g**, ahead of the experiment and exempt from its discard gate; the remaining two (`fresh`, exit codes) are the experiment's. | [plans/2026-08-daemon-fleet-worker.md](plans/2026-08-daemon-fleet-worker.md) |
| **Taper `spawn_parallel` children's iterations, or state why they are not tapered.** | Looked like a defect and is at least an inconsistency: `spawn_subagent` applies `jc_subagent_iters_at_depth` (halving per level) while `run_child` passes `max_subagent_iters` raw, so a depth-1 subagent gets base/2 and a depth-1 parallel child gets base. **Checked before parking:** `SUBAGENTS.md` scopes the taper to *a deep synchronous chain*, and parallel children are a fan at one depth — what bounds a fan is the per-child slice, which M431 makes real. Tapering would silently halve every child's iterations with no measured failure behind it. So the honest remainder is a *documentation* decision (say which rule governs a fan) rather than a code change, and it should be made by whoever can say whether one rule for "a delegate at depth 1" is worth the behaviour change. | ROADMAP M431 |
| **Reclassify the shell's own cannot-run exits (126/127) as refusals on the grading surfaces.** M625 shipped the DECLARED contract (verify exit 77 = cannot-run); 127 (command not found) and 126 (found, not executable) are the same fact said by the shell, and today they still grade FAIL. | The ambiguity is real, not hypothetical in shape: a spec whose verify RUNS the learner's deliverable (`./bin/tool`) exits 127 precisely when the work is not done — there, FAIL is the correct grade. M502 ducks this by probing only slash-carrying programs before running; a post-run 126/127 has no way to tell the grader's missing tool from the learner's missing deliverable. Revisit if a real spec shape needs it; the shipped curriculum's graders are all `sh <script>`, covered by 77. (The M624/M615 seam this row replaced — toolchain guards exiting 1, graded as FAIL — closed at M625.) | ROADMAP M625, DECISIONS.md M625 |


## Closed — found while merging M430 and M431 (2026-08-13)

Two milestones were authored the same afternoon and both claimed the number M430; the
later one (this document's M431) was renumbered, because the other was already pushed
and renumbering unpublished work costs nothing. Reading both diffs against each other
is what surfaced its row — and it was the same defect class M431 is about, which is
why it was recorded rather than quietly built. **The row it produced is closed; the
heading is kept for the lesson, retitled at M463 because an `## Open` section with no
rows makes this page advertise work that does not exist.**

## Closed at M637 — the argumentation program (M628–M635), what it left

The program shipped six artifacts with a mechanical floor and said, in each ROADMAP
entry, what was measured and what is still a claim. These are the claims, as rows
with a revisit condition, so the next session starts from the register and not
from memory (recorded 2026-09-16, after M635's gate went green).
**All three closed on 2026-09-17 (M637–M638), and the heading did not follow them until M657** — three struck-through rows under an `## Open` heading, which is the exact state this page names as its one unacceptable one. The rows are kept because each carries what the measurement found, and two of them found something other than what they predicted.


| Deferred | Why | Where |
|---|---|---|
| ~~**Run the pre-registered refute A/B**~~ — **DONE, M637 (2026-09-17): refute 12 of 12, control 1 of 12; the frame did work the words did not.** Harness `tests/bench/refute_ab/` (first written by jichi itself, then corrected — see the companion note), twelve plants of the three pre-registered kinds listed before the runs, opaque ids, sealed mapping, a per-arm form. The blind was formal (the arm is legible from the answer's own headings) and the false-attack rate was not assessed; both are said on the results page, and the second is the next measurement. [analysis](analysis/2026-09-17-refute-ab.md) | The `refute` stage exists (M634) and its benefit is a hypothesis with a number on it, not a result. Plant and record the claims **before** any run. *Revisit when:* the planted list exists in `tests/bench/refute_ab/planted.tsv`; the result page then links the proposal and reports as run or as not run. | [proposals/2026-09-refute-ab.md](proposals/2026-09-refute-ab.md); [ROADMAP M634](ROADMAP.md) |
| ~~**Read the reach footer and a `PLAN.md` drift line "in anger"**~~ — **DONE, M637 (2026-09-17).** The real task was the A/B harness itself: plan mode wrote a five-section `PLAN.md`; `--auto` under an edit scope and a tool-call fence built the harness by fifty shell appends and stopped on the fence with `23 errors · plan: 3 of 5 predicted files touched · not checked: a shell command ran`. The footer changed the order of reading and exposed an under-specified verifier; it could not see that the harness was wrong. Two footer changes proposed, none made -- then both made, plus the fence fix under the cascade, in M638 the same day. [analysis](analysis/2026-09-17-reading-the-footer-in-anger.md) | Nothing in M630 or M631 has been read by a person on a task they cared about; both entries say so in those words. The measurement is one honest session note on what the footer changed, or did not. *Revisit when:* the note exists (a `docs/analysis/` page or an ANECDOTES entry), whichever way it came out. | [ROADMAP M630, M631](ROADMAP.md) |
| ~~**Link `ARGUMENT.md` from the Ri stage of `JOURNEY.md`**~~ — **DONE 2026-09-17:** the Ri list's last bullet now sends the reader to the names and says why they come last. (The fourth row this section opened with — a dedicated `workflow.sh` check for the read-only map fence — was done and removed from here by commit 4c0777db on 2026-09-16.) | The page is indexed and linked from VOCABULARY.md and CURRICULUM.md, but the one place that discusses leaving the form — where the names are meant to be met — does not point at it. Two sentences. *Revisit when:* the next docs pass touches JOURNEY.md. | [JOURNEY.md §Ri](JOURNEY.md); [ARGUMENT.md](ARGUMENT.md) |


## Open — the language-teaching coverage review (M636)

Counted, not guessed: `docs/analysis/2026-09-16-language-teaching-coverage.md`
measured what the documentation teaches per language across all 505 markdown
files. Three of the four rows below are consequences of the deliberate
"C is the parameter" choice rather than oversights, and the review says so. They
are recorded because a consequence somebody chose is still a thing the next
reader should meet as a decision rather than as a surprise.

| Deferred | Why | Where |
|---|---|---|
| **A "modern C" reading track**, in the reading-guide genre rather than as more graded tasks. | Modern C is currently taught as a **fence**: the constructs named in the docs are the ones CONTRIBUTING.md forbids (`<stdint.h>` 8 files, designated initializers 5, VLA 3), while the ones a modern-C programmer would simply use appear **zero** times (`uint32_t`, `_Static_assert`). A learner finishes able to write 1989's C and to name what jichi may not use. Spine: per construct, what jichi does instead and what that costs. *Revisit when:* a track exists, or the gap is argued closed — Gustedt's *Modern C* is now cited for exactly this and a book may be the right answer. | [C_STANDARDS.md](C_STANDARDS.md); [analysis](analysis/2026-09-16-language-teaching-coverage.md) §2.1 |
| **Extend the C++ course, or stop letting the reader read "modern" into it.** | Tasks 59–62 teach RAII, containers and exceptions — a fair curriculum, and **C++98/11 C++**. Across 505 files: `RAII` 15, `std::vector` 8, `C++20` 1, `unique_ptr` 1, `std::span` **0**. The curriculum's own words are accurate; the overstatement happens in the reader's head. Renaming to "C++ fundamentals" is two words and honest; a fifth task on move semantics and `unique_ptr` is better and is a milestone. *Revisit when:* either is done. | [CURRICULUM.md](CURRICULUM.md); [analysis](analysis/2026-09-16-language-teaching-coverage.md) §2.2 |
| **A "what this course does not teach" section in `CURRICULUM.md`** (the second half, "data structures" → "manual memory" in the 51–54 line, was done at M636g). **Narrowed at M636k:** the premise moved — a hash table (78), a linked list (79) and an ordered map / BST (80) are now taught, each built to a contract and measured, so the section would today name balancing, deletion in a tree, sorting algorithms and graphs, not "data structures". Whether to write it at all stays the operator's call about what this course is. | `CURRICULUM.md` states **no scope boundary at all** (zero matches for "does not teach", "out of scope", "not a course in" — **re-checked 2026-09-18 (M658): still zero, all three**; the corpus is 524 markdown files now, not the 505 the review counted), so a learner can finish four stages believing data structures were covered. They were not: taught are a growable array (52), a ring buffer (04), an arena (54), an RPN stack (50, 62) and the C++ containers (60–61); **never taught are linked list, hash table, tree, binary search or any sorting algorithm**. The boundary is *correct* — jichi has exactly one general container (`jc_vec`) and a course that reads its own source cannot teach what the source lacks — but the page's own words ("manual memory & **data structures**") let a reader infer more than one task delivers. Same species as the "modern" C++ finding. *Revisit when:* the section exists and the line says the smaller true thing. **Planned 2026-09-16**, and the plan resolves it better than by softening: splitting off a real **"C: files & structures"** course (tasks 76–80) makes the old course's title true by deleting one word rather than by editing a claim down. | [analysis](analysis/2026-09-16-language-teaching-coverage.md) §3b.2; [plan](plans/2026-09-files-and-structures.md) |
| **Literature for the other six tracks** — Racket, Guile, Elixir, Haskell, Clojure, Python. | Each has a reading or graded track and no bibliography section. Worth doing **only at the M636 standard**, or not at all: a thin section per language is worse than an honest gap, which is why the page names the absence instead of padding. Rust came off this list at M636c because it was the only one with a *graded course* and nothing to read; none of these six is in that position. *Revisit when:* someone wants a specific one, rather than all six as a sweep. | [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) |
