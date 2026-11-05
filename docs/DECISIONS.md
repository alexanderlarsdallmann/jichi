# Decision register

> **What this is.** A findable index of decisions that shaped jichi — each with the
> alternatives that were **rejected** and why. The detailed reasoning lives in the
> milestone entries of [`ROADMAP.md`](ROADMAP.md) and in
> [`proposals/`](proposals/); this page exists because those are over 400 entries and
> over 35 proposals deep, and "later review" needs an index rather than a search.
> (Both counts are stated as bounds on purpose — they only grow, and a stale exact
> figure is the rot this register exists to prevent. M391's rule.)
>
> **Companion page:** [`DEFERRED.md`](DEFERRED.md) records what was consciously *not*
> done, with the reason — the other half of a decision.
>
> **Coverage is honest, not complete.** It starts at **M293** (2026-08-05), when it
> was created. Earlier decisions are recorded in their own milestone entries and are
> *not* back-filled here — inventing a tidy history for decisions I did not witness
> would defeat the purpose. Add a row when a decision is one a future reader could
> reasonably disagree with.

## How to add a row

A decision earns a row when someone could sensibly have chosen otherwise. Record
**what was decided**, **what was rejected**, and **where the reasoning lives**. If
there is no rejected alternative, it was not a decision — it was just work.

**Superseding a row (M628).** Never delete a decision. When a later milestone
reverses one, keep the row and prefix its decision cell with
`**Superseded at M<n>** —`, naming the milestone that reversed it (and the row that
replaced it, if there is one). The wrong turn is half the register's value
([PROJECT_RECORDS.md](PROJECT_RECORDS.md) §3.1 says the same to the learner);
`tests/smoke/decisions_superseded_lint.sh` holds every marker to its form and to a
ROADMAP entry. No row carried the marker when the convention was written — the
register is not back-filled, and nothing had been reversed since it started.

---

## Behaviour and defaults

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **The dirty-tree dispatch guard's product default is PROVISIONAL `warn` (the drive runs `refuse`); a failed probe reads `unknown`, never clean** (M1006) | [M1006](#dec-m1006) |
| **The 30 s `timeouts.stall` default stays; driven turns run it off; the final value is deferred to cut data the censored telemetry cannot yet provide** (M959) | [M959](#dec-m959) |
| **A disputed grading call in a refuter measurement is settled by a test where one can reach the code, and labelled "read only" where none can** (M642) | [M642](#dec-m642) |
| **A workflow's claim goes in the spec's `input`, and a refuter that declines an empty claim is behaving correctly** (M641) | [M641](#dec-m641) |
| **A write into a not-yet-existing directory inside the workspace passes the path fence, and the fence's own refusals are counted apart from errors in the reach footer** (M638) | [M638](#dec-m638) |
| **Predictions are a learner-typed slash command into a separate, never-scored file (`.jichi/predictions.jsonl`), and the argumentation vocabulary is one short page placed LAST** (M635) | [M635](#dec-m635) |
| **Counter-argument is a workflow stage: `refute` runs a read-only second seat under a frame the spec author cannot weaken (three forced headings, \"nothing found\" permitted), keeps the claim beside the refutation, and ships with a pre-registration instead of a claim of benefit** (M634) | [M634](#dec-m634) |
| **The steelman is a graded shape: when a design carries `## Objections`, every entry needs an `Objection:` line before its `Reply:`; the section stays optional. The critical questions of a consequence argument are taught as prose in module 07 and CODE_REVIEW.md** (M633) | [M633](#dec-m633) |
| **Every drafted lesson carries a warrant tag — `[warrant: measured\|judgement\|unchecked]` — counted by `learn apply` (untagged included) and reviewed first by `learn analyze`; a label, never a gate** (M632) | [M632](#dec-m632) |
| **The plan is a file: `.jichi/PLAN.md`, written in plan mode by one tool into one path, five markdown sections a script can count, and reconciled at run end as REPORTED drift** (M631) | [M631](#dec-m631) |
| **Every headless answer is footed by what the run's record checked and did not — two lines on stderr and a `reach` member of the `done` object — derived from counters, never from the model** (M630) | [M630](#dec-m630) |
| **A decision is graded as three lines — `Chose:`, `Rejected: … — why it lost`, `Because:` the criterion traced to a requirement — in a new process-track task, `75-process-decisions`** (M629) | [M629](#dec-m629) |
| **Two retraction norms, one per reader** (M628) | [M628](#dec-m628) |
| **The reading-for-review discipline is a graded reading of jichi's OWN source, checked on two tiers: a structural floor the two-sided proof rests on, and an anchor-resolution gate that runs only where the tree is reachable** (M627) | [M627](#dec-m627) |
| **`assignments` groups by a `stage:` frontmatter key with per-stage point totals; the stage vocabulary is the curriculum's own (shu/ha/ri + track slugs); grouping activates only when a spec carries the key** (M626) | [M626](#dec-m626) |
| **The verify exit-code contract is exit 77 = cannot-run, read as a refusal on every grading surface, and a refusal records nothing** (M625) | [M625, M624](#dec-m625) |
| **`prune` sweeps stale sandbox worktrees: only names jichi mints, and never a live pid** (M616) | [M616](#dec-m616) |
| **`prune` scopes the codebase-index cache too, by the same selectors, protecting the current workspace's index** (M612) | [M612](#dec-m612) |
| **`prune` scopes dreams as well as sessions, by the same selectors** (M611) | [M611](#dec-m611) |
| **The wrong-directory signal is a NOTE beside a FAIL, never a refusal; the tutor fence covers the hint ladder, not the write tools** (M617) | [M617](#dec-m617) |
| **`attempt` refuses a broken harness (exit 2, nothing recorded), and a wholesale test overwrite is TAINTED by the same M88 predicate edit_file uses** (M615) | [M88, M615](#dec-m88) |
| **The TUI grades through the one mechanic, and a refusal records nothing** (M614) | [M614, M529, M502](#dec-m614) |
| **One licence for the whole tree, documentation included** (M620) | [M620](#dec-m620) |
| **The licence is Apache-2.0; the copyright holder is Justus-Liebig-Universität Gießen; the author line stays Alexander-Lars Dallmann** (M619) | [M619](#dec-m619) |
| **The task list belongs to the session** (M606) | [M606](#dec-m606) |
| **A `## Checks` lesson is committed as an AUTHORED constraint by `learn apply`, and only through the existing scanner** (M602) | [M602](#dec-m602) |
| **Corrections retract learned conventions, inside the `## Learned conventions` section only** (M601) | [M601](#dec-m601) |
| **A memory note keeps its `[evidence: …]` and may carry `[pins: …]`; the unpinned share is reported, not judged** (M600) | [M600](#dec-m600) |
| **Telemetry `metrics` ON by default, one log per workspace, readers prefer it** (M599) | [M599](#dec-m599) |
| **Lessons are stored in the self-learner's language; English-canonical is an option spelled `language: English` on `learn.md`** (M597) | [M597](#dec-m597) |
| **A persona REPLACES the generic delegate paragraph, and the enforced sections follow it** (M596) | [M596](#dec-m596) |
| **`tu_arg_bool` IS `jc_json_get_bool_lenient`** (M530) | [M530](#dec-m530) |
| **The assignment verbs take a NAME, not a path** (M529) | [M529](#dec-m529) |
| **Verify the socket's mode as an EFFECT rather than implement a peer-credential check** (M528) | [M528](#dec-m528) |
| **A clean-sheet protocol specification, with no implementation** (M525) | [M525](#dec-m525) |
| **The git index is the public snapshot's manifest** (M484) | [M484](#dec-m484) |
| **`attempt` does not load the project's rules file** (M309) | [M309](#dec-m309) |
| **The evidence for advising on tool use is *distinct sessions*, not turns** (M316) | [M316](#dec-m316) |
| **The unused-tools warning advises the lever, not the surgery** (M316) | [M316](#dec-m316-2) |
| **"No telemetry data" is a stated absence, never a column of zeroes** (M314) | [M314](#dec-m314) |
| **The paid-for-vs-called report describes and never advises** (M314) | [M314](#dec-m314-2) |
| **No per-use price column** (M314) | [M314](#dec-m314-3) |
| **No `--since` window on `context tools`** (M314) | [M314](#dec-m314-4) |
| **The reports do not predict what compaction would elide next** (M315) | [M315](#dec-m315) |
| **A deferred item's reason gets its factual claims checked before it is parked** (M326b) | [M326b](#dec-m326b) |
| **A reason resting on someone's statement says whose, and how long ago** (M326b) | [M326b](#dec-m326b-2) |
| **A deferral is not a rejection** (M317) | [M317](#dec-m317) |
| **`/context` takes sub-views in the TUI too** (M317) | [M317](#dec-m317-2) |
| **`context history` reads a saved session** (M315) | [M315](#dec-m315-2) |
| **Tool results are attributed by searching *backwards** (M315) | [M315](#dec-m315-3) |
| **A line stays a line when the detail is a list whose length grows with the workload** (M315) | [M315, M312, M313](#dec-m315-4) |
| **Per-tool sizes are a separate view (`context tools`), not more lines in `/context`** (M313) | [M313, M312](#dec-m313) |
| **The per-tool view states the same total as `/context`** (M313) | [M313](#dec-m313-2) |
| **The prompt builder reports its own section sizes** (M312) | [M312](#dec-m312) |
| **Sections are listed non-zero-only, largest first** (M312) | [M312](#dec-m312-2) |
| **`repoMap: false` is recommended for graded attempts, not defaulted** (M312) | [M312](#dec-m312-3) |
| **`context` and `sysmsg` load their assets through one shared helper** (M311) | [M311](#dec-m311) |
| **`/context` sizes what is actually sent** (M310) | [M310](#dec-m310) |
| **`toolProfile: core` is recommended for graded attempts, not defaulted** (M310) | [M310](#dec-m310-2) |
| **Compaction runs *after* routing** (M298) | [M298](#dec-m298) |
| **De-escalation needs hysteresis, not a threshold** (M298) | [M298](#dec-m298-2) |
| **Only a *room*-caused escalation may be undone** (M298) | [M298](#dec-m298-3) |
| **Mid-run posture changes only ever narrow** (M304) | [M304](#dec-m304) |
| **The narrowness order is an explicit table, not the enum's values** (M304) | [M304](#dec-m304-2) |
| **Todo state is a word, not a checkbox** (M299) | [M299](#dec-m299) |
| **The solving stance names only the support tools actually advertised** (M319) | [M319](#dec-m319) |
| **`attempt`'s default profile stays `full`** (M320) | [M320, M310](#dec-m320) |
| **`attempt`'s default profile stays `full`** (M319) | [M319](#dec-m319-2) |
| **`craft` defaults off under `--lite`, and is unchanged elsewhere** (M318) | [M318](#dec-m318) |
| **A/B metrics and the probe were fixed before the runs** (M318) | [M318](#dec-m318-2) |
| **The craft prompt applies even under a persona override** (M299) | [M299](#dec-m299-2) |
| **`--voice` speaks approval prompts and errors, not just answers** (M303) | [M303](#dec-m303) |
| **The spoken utterance is capped** (M303) | [M303](#dec-m303-2) |
| **A `style:` names an existing output style; it is not prose** (M302) | [M302](#dec-m302) |
| **A transport failure carries its diagnosis on the event, not only in stderr** (M321) | [M321](#dec-m321) |
| **Three "broken alias" findings were tested against the build before being believed** (M321) | [M321](#dec-m321-2) |
| **The iteration cap returns success and keeps the work** (M322) | [M322](#dec-m322) |
| **`turn_capped` is set only at depth 0** (M322) | [M322](#dec-m322-2) |
| **The daemon socket's mode is set via the umask around `bind()`** (M322) | [M322](#dec-m322-3) |
| **A mid-turn `compact` event is emitted whenever the trigger fired** (M323) | [M323](#dec-m323) |
| **The short-fall warning is throttled by the agent loop, not by the compaction pass** (M323) | [M323](#dec-m323-2) |
| **M323 shipped the observability and left the behaviour alone.** | [M323](#dec-m323-3) |
| **`list_files` gained a `pattern` so `glob` could become a real alias** (M324) | [M324](#dec-m324) |
| **The pattern reuses `jc_glob_match`** (M324) | [M324](#dec-m324-2) |
| **`list_files` is now path-fenced** (M324) | [M324](#dec-m324-3) |
| **A killed parallel child names the limit and the knob** (M325) | [M325](#dec-m325) |
| **The config-key lint no longer counts the history as documentation** (M325) | [M325](#dec-m325-2) |
| **One registrar for the conditional built-ins** (M325b) | [M325b](#dec-m325b) |
| **MCP tools stay out of the report, and the report says so** (M325b) | [M325b](#dec-m325b-2) |
| **`repoMap` stays on by default for `attempt`** (M326) | [M326](#dec-m326) |
| **A kill note names the limit that fired, as a number, never a config key** (M342) | [M342](#dec-m342) |
| **The verifier's kind is a checked declaration, not a split flag and not an inference** (M343) | [M343](#dec-m343) |
| **Declaring a kind arms the baseline probe** (M343) | [M343](#dec-m343-2) |
| **A bad kind on the CLI is a hard usage error; a bad `verifyKind` in config warns and is ignored** (M343) | [M343](#dec-m343-3) |
| **`--strict-green` stays opt-in, on a measurement that reversed the earlier one** (M662) | [M662, M343, M459, M657](#dec-m662) |
| **`search_code` speaks extended regular expressions (`grep -rnE`)** (M714) | [M714](#dec-m714) |
| **The journal records the SIZE of a turn's answer, not whether it answered** (M715) | [M715](#dec-m715) |
| **A read's range is reported by the tool that executed it, and recorded beside the argument summary rather than in it** (M715) | [M715](#dec-m715-2) |
| **The rules walk's root is the nearest directory holding a `.git` of either kind; a submodule is its own** (M884) | [M884](#dec-m884) |

<a id="dec-m642"></a>
#### M642 — A disputed grading call in a refuter measurement is settled…
Two of three became product fixes.
**Rejected:** **Settle it by reading harder** — four models and the grader had read; the disagreement survived reading, and the injector settled it with a segfault in under an hour. **Leave the calls as the grader's judgement** — the page then rests on one person's reading of the very claims the refuters could not agree on. **Add an API to reach the `jc_rss.c` guard so it too can be tested** — an interface nobody needs for a bound no caller can exceed; the page says "tested by reading only" instead. **Test the diff fallback without the injector** — no public path forces the allocation to fail; the fault tier gaining the unit suite is the smaller change and revives a test dead since M198.
**Where:** [ROADMAP M642](ROADMAP.md), ANECDOTES #80

<a id="dec-m641"></a>
#### M641 — A workflow's claim goes in the spec's input, and a…
The refute A/B had fed each report as the stage's *prompt*; Qwen models read past the `(empty)` claim slot and refuted the prompt, Gemma and gpt-oss declined. The harness now seeds `input`; the frame is unchanged.
**Rejected:** **Make the refute stage treat its prompt as the claim when the context is empty** — a fallback that guesses what the author meant is how the harness defect stayed invisible for one model and one A/B; the stage should read one slot and the spec should fill it. **Tell the model in the frame to "use the prompt if the claim is empty"** — weakening the frame the author cannot weaken, from inside. **Score Gemma and gpt-oss at 0 of 12** — those runs measured the harness, not the models; they are kept as evidence and excluded from the table. **A cross-model workspace that is this checkout** — it holds `planted.tsv`, the reports and a prior grading; the 27B cited one. Runs use a clean `git archive` of the report commit.
**Where:** [ROADMAP M641](ROADMAP.md), [proposals/2026-09-refute-ab.md](proposals/2026-09-refute-ab.md)

<a id="dec-m638"></a>
#### M638 — A write into a not-yet-existing directory inside the workspace passes…
**Rejected:** **Keep failing closed on a missing parent and teach the model to `mkdir` first** — `write_file` already does `mkdir -p`, so the fence was refusing what the tool would do, with a false reason ("outside workspace"); a fence that lies is routed around through the shell, which the fence cannot see at all (fifty appends, 2026-09-17). **Re-append any missing tail** — a `.`/`..` in a path that does not exist yet is a name to the resolver and a step to the kernel; such a tail still fails closed. **Subtract refusals from the error count** — a refusal IS an error result the model was handed, and the count is what the model saw; the footer prints both, and the zero. **Count refusals by matching the denial strings in the sink** — a counter at each fence, not a grep over prose that the next wording change silently misses.
**Where:** [ROADMAP M638](ROADMAP.md)

<a id="dec-m635"></a>
#### M635 — Predictions are a learner-typed slash command into a separate, never-scored…
**Rejected:** **A `record_prediction` tool for the model** — the hint ladder's measurement (M319: two models, 24 runs, zero calls) says learner-side mechanisms are learner-driven; a slash command is what a person types. **Scoring the hit rate into progress** — every reader of `progress.jsonl` treats a line as an attempt with a verdict, and \"a self-learner is never punished for learning\" has to be true by construction. **Teaching the theory first** — a learner who can name a warrant and cannot write one has a vocabulary; the curriculum grades artifacts, so the names come after the moves. **A resolve that creates its own prediction** — a resolution with nothing open is refused; the record must not be able to lie by ordering.
**Where:** [ROADMAP M635](ROADMAP.md)

<a id="dec-m634"></a>
#### M634 — Counter-argument is a workflow stage: refute runs a read-only second…
**Rejected:** **A free prompt in a `synthesize` stage** — \"be critical\" in an author's prompt is tier-3 guidance, the tier the craft A/B measured as moving nothing; the frame is the point. **Letting the refuter edit** — it is a second reader, not a second author; a read-only subagent refuses a write at the gate. **Replacing the claim with the refutation** — a human rules by reading both; a stage that returns only the attack has decided for them. **Claiming the stage finds errors** — nothing has been measured; `proposals/2026-09-refute-ab.md` fixes the hypothesis, model, planted claims and metric before any run.
**Where:** [ROADMAP M634](ROADMAP.md)

<a id="dec-m633"></a>
#### M633 — The steelman is a graded shape: when a design carries…
**Rejected:** **Requiring `## Objections`** — the gate says rebut *or apply*; a design that applied every point has nothing to rebut and must not be failed for it. **Grading the objection's strength** — a script sees two labels, never whether the point is stated in its strongest form; that is the instructor's reading-aloud and `/check`'s job (D8). **A new task** for the steelman — one conditional check on the task that already asks for the section teaches the shape where the habit forms, and does not move the curriculum's counts. **Grading the four critical questions** — asking a script to find "how likely" in a review is asking for the words, not the reasoning; prose is the tier where a checklist works.
**Where:** [ROADMAP M633](ROADMAP.md)

<a id="dec-m632"></a>
#### M632 — Every drafted lesson carries a warrant tag — [warrant: measured\|judgement\|unchecked]…
**Rejected:** **Refusing unchecked notes** — turns a label into a gate and loses the gotchas; the point is to see them, not to filter them. **Letting the model judge a note's truth** — D3's rejection stands; this is *provenance the drafter can state honestly* ("I inferred this from one run"), not a verdict. **Hiding untagged notes in the counts** — an absent label is information (the mentor did not say how it knows) and is the case most worth a second look. **A new trailer syntax** (`{measured}`, a column) — the `[key: value]` bracket the `evidence` and `pins` trailers already use is the one machine format the parser and the reader both know.
**Where:** [ROADMAP M632](ROADMAP.md)

<a id="dec-m631"></a>
#### M631 — The plan is a file: .jichi/PLAN.md, written in plan mode…
**Rejected:** **A JSON plan schema** — the learner's registers are markdown with a grep-countable rule, and the agent's plan should be the same kind of thing so one person can read both. **Enforcing `## Touches` as a fence** — a plan is a prediction; `--edit-scope` is the fence, and fencing the prediction would make every honest discovery mid-task a violation. **Marking `write_plan` read-only to pass plan mode** — it writes; `plan_allowed` says the true thing (one declared write) and is honoured where read-only was consulted. **Writing the plan through the `jc_app_write_file` chokepoint** — then the plan would count as a file the run wrote and reconcile against itself. **A `jichi plan` subcommand to check the file** — the tool refuses an incomplete plan and the footer reports drift; a third surface would be a third copy of the shape.
**Where:** [ROADMAP M631](ROADMAP.md)

<a id="dec-m630"></a>
#### M630 — Every headless answer is footed by what the run's record…
**Rejected:** **Having the model write the footer** — one more sentence generated from the same context as the answer, "exactly as reliable as the run and not one bit more" (tsuiseki-04). **Printing only when something is wrong** — silence is indistinguishable from a pass (M316); a zero is a claim and is printed, an absence is stated. **Putting it on stdout** — stdout is the raw answer for scripts (M73); the footer is a diagnostic and rides with the others. **A verdict word ("trustworthy")** — the footer states what was checked; it never says the answer is true.
**Where:** [ROADMAP M630](ROADMAP.md)

<a id="dec-m629"></a>
#### M629 — A decision is graded as three lines — Chose:, Rejected:…
**Rejected:** **Upgrading task 69 in place** — it would change a shipped grader under learners mid-course, and traceability and rationale are two lessons. **Grading the criterion's soundness** — a script can see that a `Because:` clause exists and names an `R<n>`, never whether it is the criterion that *should* decide or whether the alternative is a straw man; M6's instructor notes already name that failure mode and the peer-ranking exercise is its instrument. **A trade-off table** (options × criteria with weights) — the comparative form the curriculum lacks, but a table with invented weights is a worse first lesson than one criterion named honestly; the reference solution shows one row of the table in prose.
**Where:** [ROADMAP M629](ROADMAP.md)

<a id="dec-m628"></a>
#### M628 — Two retraction norms, one per reader
a retracted memory note or learned convention is *deleted and counted*; a retracted claim in an analysis page or anecdote *keeps its wrong text under a correction banner*. And the empty-reply verdict of the tool-calling probe is `unknown` (a fourth, negative enum value), never `none`.
**Rejected:** **One retraction norm for both** — a wrong instruction kept "for the record" is a wrong instruction delivered to the model every turn, while a wrong sentence deleted from a record teaches nothing about how it happened; the readers differ, so the norms do. **A `Status` column on this register** — 302 rows would move for a handful of markers, and a greppable in-cell marker is held by a lint just as well. **Narrowing CLAUDE.md's "make the fallback say unknown" rule** because the M519 cause was fixed — the rule is right; the value it asks for had simply never been built. **Making UNKNOWN a doctor WARN** — an empty reply under `native` means the loop cannot run, so it stays a FAIL; the word changed, the consequence did not.
**Where:** [ROADMAP M628](ROADMAP.md)

<a id="dec-m627"></a>
#### M627 — The reading-for-review discipline is a graded reading of jichi's OWN…
**Rejected:** **A `jichi trace` subcommand that generates the flow view** — nothing here can: the repo map is a symbol-name scan with no call graph, the LSP gives references but no call hierarchy, there is no C AST; and generating the trace would remove the skill being trained. **A self-contained specimen as the subject** (task 24's shape) — stable, but toy; the value of the reading track is a real 90k-line program, and the guides already accept coupling to live symbols via `reading_refs_lint`. **Refusing (exit 77) when `src/` is absent** — the e2e proof copies only `docs/assignments`, so a grader that refused there could never be proven two-sided; skipping ONE gate and saying so is M625's principle at the right granularity. **Grading the reading's correctness** — a script can check that a control-flow order is anchored, not that it is right; the floor is completeness, anchoring and a checked record, the rest is comparison against the reference.
**Where:** [ROADMAP M627](ROADMAP.md)

<a id="dec-m626"></a>
#### M626 — assignments groups by a stage: frontmatter key with per-stage point…
**Rejected:** **Printing gate thresholds ("gate met: 14/17")** — a gate is more than points (a debugging record, task 09 required, all-four-floors), so a verdict computed from points alone would lie about Sets B and C; totals make the learner's own INDEX.md comparison trivial instead. **The binary parsing INDEX.md's tables** — the original deferral's objection stands; the lint parses the document and pins it to the frontmatter, the binary reads only the key. **A hardcoded stage-order table** — group order is first-seen under the existing name sort, which the task numbering already encodes. **Set letters (A/B/C) as the vocabulary** — the letters don't extend to the tracks; shu/ha/ri is the language CURRICULUM.md already teaches in.
**Where:** [ROADMAP M626](ROADMAP.md)

<a id="dec-m625"></a>
#### M625 — The verify exit-code contract is exit 77 = cannot-run, read…
77 is automake's SKIP; the guards say "CANNOT RUN" and probe usability, not existence (M624).
**Rejected:** **Exit 2 as the sentinel** (the deferral row's own example) — 12 shipped specs use `grep`/`[` bare as the verify, and both exit 2 on ordinary operational errors: a missing learner file must grade FAIL, not refuse. **Reclassifying 126/127 (the shell's own cannot-run codes)** — a verify whose program is the learner's deliverable would make 127 ambiguous; deferred, not decided. **Recording refusals in progress.jsonl with a marker** — cannot-run is a property of the machine today, not the learner's history; a recorded refusal rots the day the toolchain is installed (and M614 already rejected teaching every reader a not-a-grade row).
**Where:** [ROADMAP M625](ROADMAP.md)

<a id="dec-m616"></a>
#### M616 — prune sweeps stale sandbox worktrees: only names jichi mints, and…
**Rejected:** **A separate flag for `--keep-worktree` trees** — kept means kept for review, not forever; selectors already say how long, and a live run is protected by pid, not flag archaeology. **`git worktree remove` per tree** — needs the owning shadow repo; M272's delete-then-prune procedure covers the orphan case version-independently. **Sweeping every name in the directory** — the parallel pool's trees are not prune's to name.
**Where:** [ROADMAP M616](ROADMAP.md)

<a id="dec-m612"></a>
#### M612 — prune scopes the codebase-index cache too, by the same selectors,…
**Rejected:** **A build-time LRU size cap** — a third bespoke retention policy; `prune` is now the single home for all `~/.jichi.d` accumulation. **Deleting the current index too** — harmless (rebuildable) but pure waste right after a manual prune. **An opt-in `--index` flag** — same as dreams: one cleanup, one mechanism.
**Where:** [ROADMAP M612](ROADMAP.md)

<a id="dec-m611"></a>
#### M611 — prune scopes dreams as well as sessions, by the same…
**Rejected:** **A separate `--dreams` flag** — dreams and sessions are the same kind of `~/.jichi.d` accumulation a cleanup command should trim together; two flags is friction. **A daemon-side write cap** — S3's dedup already stops the daemon's duplicate dreams at the source, and prune covers manual dreams too. **Leaving dreams unbounded** — the daemon's idle-dream makes that a real growth path.
**Where:** [ROADMAP M611](ROADMAP.md)

<a id="dec-m617"></a>
#### M617 — The wrong-directory signal is a NOTE beside a FAIL, never…
**Rejected:** **Refusing the grade on the heuristic** — M502's own reasoning caps argument-shaped signals at advice. **Hardcoding `docs/assignments/` as the wrong-dir signal** — the scanner is layout-free. **Read-only tutor mode** — breaks legitimate mid-session asks; the ladder was the concrete attributable damage, and decline-to-write is now documented as a stance; revisit on a measured incident.
**Where:** [ROADMAP M617](ROADMAP.md)

<a id="dec-m88"></a>
#### M88 — attempt refuses a broken harness (exit 2, nothing recorded), and…
**Rejected:** **Counting every write to a test path as a test edit** — a learner legitimately writes NEW tests; the predicate (assertions both sides, old text not preserved) is what separates moving a goalpost from adding one. **Predicating shell writes** — no old/new pair exists at that chokepoint; `shell_ran` + the out-of-scope guard remain its detectors (ANECDOTES #12 residual). **Exit 1 for the refusal** — a supervisor must be able to tell a broken harness from failed work.
**Where:** [ROADMAP M615](ROADMAP.md)

<a id="dec-m614"></a>
#### M614 — The TUI grades through the one mechanic, and a refusal…
`jc_grade_core` moved to its own TU so `/grade` is the fourth caller of M529's mechanic, and `JC_GRADE_CANNOT_RUN` answers in M502's words with no progress row.
**Rejected:** **Porting the guard into the TUI's own copy** — a fifth guard is the M529 defect with better intentions. **Recording refusals with a marker field** — every progress.jsonl reader counts a line as an attempt with a verdict; a not-a-grade line would need every reader taught forever. **Making the TUI record opt-in** — the defect was recording refusals, not recording.
**Where:** [ROADMAP M614](ROADMAP.md)

<a id="dec-m620"></a>
#### M620 — One licence for the whole tree, documentation included
(M620, at the operator's question "CC BY-SA 4.0 for the docs?"). The docs are executable and flow INTO code by design — 158 tracked code files under `docs/` (assignment fixtures/graders), 80 specs whose `verify:` runs, fixtures copied into learner worktrees and modified into solutions, reading chapters quoting source verbatim — so a ShareAlike docs licence would encumber exactly the copying the curriculum teaches; and `license_lint`'s own design premise is ONE identifier ("a tree that names two licences cannot be packaged").
**Rejected:** **CC BY-SA 4.0 for `docs/`** — BY-SA is incompatible with Apache-2.0 for code, and CC advises against CC for software; a learner's solution derived from a BY-SA fixture would be copyleft-encumbered. **A dual CC BY 4.0 grant on pure prose** — harmless but unearned: no concrete reuse request exists, a permission granted early is hard to withdraw, and Apache-2.0 §4 already carries attribution; revisit if a real request arrives.
**Where:** [ROADMAP M620](ROADMAP.md)

<a id="dec-m619"></a>
#### M619 — The licence is Apache-2.0; the copyright holder is Justus-Liebig-Universität Gießen;…
(M619, the institutional answer of 2026-08-27; "Apache 2.0" normalised to the SPDX spelling `Apache-2.0`). `set-license.sh` learned deliberate switches, since the review may still choose MIT.
**Rejected:** **Filling the Apache appendix boilerplate into 486 headers** — the three-line SPDX form is the standard short equivalent. **Keeping `set-license.sh` one-way** — a switch that means hand-editing ~500 headers is the M497 defect recurring. **Naming the author as holder in NOTICE** — § 69b splits the roles; each line now says one true thing.
**Where:** [ROADMAP M619](ROADMAP.md)

<a id="dec-m606"></a>
#### M606 — The task list belongs to the session
`struct jc_session` owns it, `app->todos` points at the live session's list, the codec saves and restores it, and a store written by a NEWER jichi is loaded with one warning.
**Rejected:** **Rebuilding the list from the history on resume** (the last `todowrite` call's arguments) — mid-turn elision and compaction rewrite exactly those arguments, so the rebuilt list would be whatever the summarizer kept; a plan is state, not a message. **Refusing a newer store version** the way `jc_calib` does — a calibration file is regenerable, a conversation is not. **Leaving `/clear` alone** — a cleared conversation with yesterday's plan attached is the same split.
**Where:** [ROADMAP M606](ROADMAP.md)

<a id="dec-m602"></a>
#### M602 — A ## Checks lesson is committed as an AUTHORED constraint…
(M602, the operator's yes to Q3).
**Rejected:** **Auto-adopting a proposed constraint at learn-on-stop** — that is a model writing a fence for itself; propose-only is the invariant and M423 is why. **A free-form constraint language** — the scanner's vocabulary (six command keys, `do not use the tool X`, `read-only`) is what the gate can enforce; a phrase it cannot read is counted and named rather than approximated, because a fence that "roughly" matches is the M167d/M168c misparse class. **Writing hooks from lessons** — a hook is a shell command in `config.json` behind `hooksEnabled`; letting the loop write one is a larger trust decision, deferred with the reason.
**Where:** ROADMAP M602, [LEARNING.md](LEARNING.md), [CONSTRAINTS.md](CONSTRAINTS.md)

<a id="dec-m601"></a>
#### M601 — Corrections retract learned conventions, inside the ## Learned conventions section…
**Rejected:** **Retracting anywhere in the rules file** — the hand-written rules above the heading were never the loop's to edit; a substring match there would let a model's directive delete an operator's rule (M533 is what one careless write did to this repository's own rules file). **Leaving rules append-only** — the one store in the loop that could only grow; 守破離 asks the student to leave the form, and a store that cannot forget is dogma.
**Where:** ROADMAP M601, [LEARNING.md](LEARNING.md)

<a id="dec-m600"></a>
#### M600 — A memory note keeps its [evidence: …] and may carry…
**Rejected:** **Stripping evidence on commit** (the M70 behaviour) — short notes, but a note whose origin is lost cannot be checked or retracted later, and the mentor's corrections pass had nothing to compare against. **A model judging whether a note is still true** — the author-reviews-author loop ANECDOTES #75 measured; it reports confidence, not truth. What ships is the mechanical half only: does the cited path resolve, is there a pin, how full is the budget. **Refusing an unpinned note** — most lessons are judgement (`analysis/2026-08-27-the-language-of-lessons.md` §6); the aim is visibility, as with `doc_claims_lint`, not a gate. **Refusing the whole apply on a prose correction** — punishes the good sections for one bad bullet; the bullet is counted and the syntax named instead.
**Where:** ROADMAP M600, [LEARNING.md](LEARNING.md), [MEMORY.md](MEMORY.md)

<a id="dec-m599"></a>
#### M599 — Telemetry metrics ON by default, one log per workspace, readers…
(M599, the operator's decision: "otherwise a learner forgets").
**Rejected:** **Opt-in, as since M21** — chosen for privacy, and the reason still holds for `full`, which stays opt-in; `metrics` carries no content, and the measured cost of opt-in was a learning loop with nothing to read on the project that had the most to teach it. **`full` by default** — no: it records prompts and code. **One `<run-id>.jsonl` per run** (the old default path) — `learn analyze` reads one log, so that is a one-run memory; per-workspace appended files are the shape the three human-named logs on the operator's machine already had. **Keying the file by hash alone** — unreadable in a directory listing; the basename is for the human, the key for two projects with one name.
**Where:** ROADMAP M599, [TELEMETRY.md](TELEMETRY.md)

<a id="dec-m597"></a>
#### M597 — Lessons are stored in the self-learner's language; English-canonical is an…
(M597, the operator's decision). The mentor -- a command subtask -- receives the session's `language` directive; a command's `language:` frontmatter replaces it for that run.
**Rejected:** **A config key (`lessonsLanguage`)** -- a second global knob for one command's behaviour, when the choice belongs beside the mentor it governs and every other command may want the same lever. **Keeping "top-level only" and fixing the doc** -- cheaper, and it leaves a German learner with English lessons and no way to change it short of editing `mentor.md`. **Translating headings too** -- the five headings are the machine format `learn apply` parses; the parser keys on English words, so they stay English and the mentor is told why.
**Where:** ROADMAP M597, [LANGUAGE.md](LANGUAGE.md), [LEARNING.md](LEARNING.md)

<a id="dec-m596"></a>
#### M596 — A persona REPLACES the generic delegate paragraph, and the enforced…
`jc_sysmsg_build_sub_as(app, persona)` is the one sub-prompt body; a command's `agent:` on a `subtask: true` run and a profiled `spawn_subagent`/`spawn_parallel`/`ask_for_help` delegate all build through it.
**Rejected:** **Prepending the persona beside the generic "focused sub-agent" paragraph** — two "you are" sentences, one framing a main agent that a command subtask does not have. **Keeping the delegation clause under a persona** — a profile may fence `spawn_subagent` away, and a clause naming a tool the array does not offer is M431 mirrored; the tool array is the truth. **Handing the subtask the full top-level prompt** — defeats the isolation that is the reason to delegate. **Leaving the profiled-delegate sites on the bare profile text** — they were fenced by rules they had never seen (the M434 shape); a shared function is what makes the fix travel (ANECDOTES #70).
**Where:** ROADMAP M596, [COMMANDS.md](COMMANDS.md), [SUBAGENTS.md](SUBAGENTS.md)

<a id="dec-m530"></a>
#### M530 — tu_arg_bool IS jc_json_get_bool_lenient
one reader for a boolean a model wrote, and the TUI's preview reads arguments through it too.
**Rejected:** **Leaving them separate.** They already disagreed: `tu_arg_bool` took the strings `"true"`/`"1"` (M168) but not a JSON number, and the TUI's preview took neither — so `{"replace_all": "true"}` previewed ONE replacement and performed ALL of them, and `{"readonly": 1}` on `spawn_subagent` gave a WRITABLE child to a model that asked for read-only. **Fixing only the fence** — the same defect sits behind eight other boolean tool arguments, and a fix that does not close the class invites the next one. **Making the TUI call `tu_arg_bool` directly instead** — equivalent agreement, but it leaves two functions where one will drift again, and it would not have closed the number gap for the tools themselves.
**Where:** [CLAUDE.md](../CLAUDE.md), ROADMAP M530

<a id="dec-m529"></a>
#### M529 — The assignment verbs take a NAME, not a path
The wire says `00-hello.md` and the server resolves it inside `<workspace>/docs/assignments/`; anything expressing a location is refused before anything is opened.
**Rejected:** **A path**, which is the obvious API and is what the CLI takes — but grading **runs the spec's own `verify` command**, so a caller who can name a location can name a file they wrote, and the verb becomes a convenient arbitrary-execution path. Proven, not asserted: with the rule removed, `../outside.md` grades a spec one directory up and reports `passed: true, points: 99`. **Sanitising a path** (reject `..`, then resolve) — the same rule expressed where it is easy to get wrong, and the first version of the test for it was itself vacuous because the escape target sat one level from where `..` actually points. **Confining by realpath comparison** — more code, one more platform-dependent call, and it answers a question the caller should not have been able to ask.
**Where:** [DAEMON.md](DAEMON.md), [proposal](proposals/2026-08-jichi-protocol.md) §5.2

<a id="dec-m528"></a>
#### M528 — Verify the socket's mode as an EFFECT rather than implement…
The daemon reads its socket's mode back after `bind()` and refuses to serve unless it is owner-only, and `hello.ok` reports `peercred: false`.
**Rejected:** **`SO_PEERCRED` / `struct ucred`** — measured hidden under `_POSIX_C_SOURCE=200112L` on glibc, so using it needs `_GNU_SOURCE`, which this project forbids by rule. **Hardcoding the `SO_PEERCRED` constant** — it differs by architecture on Linux (mips and sparc disagree with x86), the exact portability landmine the project refuses. **`getpeereid`** — BSD-only, hidden here too. **Claiming the check anyway** — a `peercred` field that said `true` without a check would be worse than no field. And the mode is the real control: on a POSIX system a 0600 socket can only be connected by its owner or root, so a peer check is defence in depth on top of it, not the primary gate. What was missing was not the check but the *verification* that the mode is what two calls asked for — the code's own comment named a case it could not rule out ("platforms that ignore the umask for sockets").
**Where:** [DAEMON.md](DAEMON.md), [proposal](proposals/2026-08-jichi-protocol.md)

<a id="dec-m525"></a>
#### M525 — A clean-sheet protocol specification, with no implementation
The five verb groups, the handshake, the transports and the conformance levels are specified; nothing in `src/` changed, so nothing new entered the stability contract. The design was then checked against [`EMBEDDING.md`](EMBEDDING.md) §4 rather than around it: existing surfaces become a conforming subset, and the one real migration cost (today's daemon accepts a bare `prompt` with no handshake) is answered by accepting the legacy shape forever on the unix socket.
**Rejected:** **Extending ACP** — its change process is another project's and its subject is an editor driving an agent, so jichi's teaching verbs would wait on someone else's review. **Specifying what already exists** and stopping — leaves the authentication gap unaddressed and never makes "supports the protocol" checkable. **Documentation only** — leaves jichi↔jichi impossible. **Implementing it now** — every verb shipped becomes a stability promise, and the open question of whether `peer` is needed at all is answered by the distribution analysis, not by this document.
**Where:** [proposal](proposals/2026-08-jichi-protocol.md)

<a id="dec-m484"></a>
#### M484 — The git index is the public snapshot's manifest
`scripts/make-snapshot.sh` extracts `git archive` and nothing else, so a file ships if and only if it is tracked.
**Rejected:** A written table of paths to delete after copying, which is what [the plan](plans/2026-08-public-snapshot.md) §4 specified. It is a **second source of truth about the tree**, and it had already gone stale in both directions before anyone executed it: §4 said no generated artifact ships while three tracked binaries would have, and §2.4 described five untracked agent profiles and four commands where there are two tracked profiles and no commands. Also rejected: `cp -a`/`rsync` of the working tree, which would carry `local/` (a live API key), the built binaries, and a root-owned directory that breaks a non-sudo copy. The index rule makes all three impossible structurally rather than by remembering.
**Where:** [plans/2026-08-public-snapshot.md](plans/2026-08-public-snapshot.md) §4

<a id="dec-m309"></a>
#### M309 — attempt does not load the project's rules file
Measured: 70% of every call's prompt was the host `CLAUDE.md`; the 1-point task cost the same as the 3-point one, so a learner on a modest budget saw FAIL and blamed themselves.
**Rejected:** Documenting it only (the trap stays, aimed at those who cannot see it); trimming the rules harder (an arbitrarily truncated guide is worse than none); a config key (the *default* was the problem); skipping rules for every `--auto` run (too broad — a real autonomous run should honour a project's conventions). `--with-rules` is the escape hatch.
**Where:** [proposal](proposals/2026-08-attempt-rules-and-plain-assignments.md)

<a id="dec-m316"></a>
#### M316 — The evidence for advising on tool use is distinct sessions,…
≥3 sessions and ≥20 tool calls before doctor will conclude anything.
**Rejected:** Turns or calls alone — one long session is still one task, so only a plural claim about *different* tasks is informative. Below the bar the check prints the shortfall rather than going silent, since silence is indistinguishable from a pass.
**Where:** [proposal](proposals/2026-08-unused-tools-doctor.md)

<a id="dec-m316-2"></a>
#### M316 — The unused-tools warning advises the lever, not the surgery
it names `toolProfile: core`, never individual tools.
**Rejected:** Naming the unused tools: with 11 of 16 unused the line becomes a list nobody acts on, and it invites removing one that is rare and right. `context tools` names them; the warning points there.
**Where:** [DOCTOR.md](DOCTOR.md)

<a id="dec-m314"></a>
#### M314 — "No telemetry data" is a stated absence, never a column…
including a log with no events for this workspace.
**Rejected:** Printing zeroes: telemetry is **off by default**, so that would tell most users they use none of their tools, and a foreign project's log is not this project's evidence.
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m314-2"></a>
#### M314 — The paid-for-vs-called report describes and never advises
and prints its evidence base beside its conclusion.
**Rejected:** Recommending removals: rare is not useless, and a tool the model was never *able* to call (permissions, an unusable schema — cf. M285) looks identical here to an unwanted one. Advice is `doctor`'s job. The first real run said *1 turn, 5 calls, 13 unused* — exactly what a reader must be able to discount.
**Where:** [proposal](proposals/2026-08-tool-cost-vs-use.md)

<a id="dec-m314-3"></a>
#### M314 — No per-use price column
**Rejected:** Dividing definition cost by call count: a definition is billed **per request**, not per use, so the quotient would look precise and mean nothing.
**Where:** same

<a id="dec-m314-4"></a>
#### M314 — No --since window on context tools
**Rejected:** Adding one: `telemetry --since` exists, and a second definition of "recent" is worse than a stale turn count the footer makes visible.
**Where:** ROADMAP M314

<a id="dec-m315"></a>
#### M315 — The reports do not predict what compaction would elide next
**Rejected:** Showing it: the trigger is a live-state decision (M76), so a static prediction would be wrong as soon as the next turn changed the estimate.
**Where:** [proposal](proposals/2026-08-history-breakdown.md)

<a id="dec-m326b"></a>
#### M326b — A deferred item's reason gets its factual claims checked before…
**Rejected:** Parking on a plausible claim: three entries in a row (M319, M320, M326) rested on an assumption a minute's reading would have settled, and each misdirected the next reader for several milestones. Judgement and evidence belong in a reason; an unchecked fact does not.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m326b-2"></a>
#### M326b — A reason resting on someone's statement says whose, and how…
**Rejected:** Recording it as if it were established: the licence and logo rows cannot be verified from inside the repo, and an unattributed "waiting on X" is how "we asked once" becomes "we asked months ago and never followed up".
**Where:** same

<a id="dec-m317"></a>
#### M317 — A deferral is not a rejection
`DEFERRED.md` holds only work new information could change; decided-against alternatives live here.
**Rejected:** Keeping both on one list — it makes the pending items unreadable and sends a future reader to re-open questions that were answered.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m317-2"></a>
#### M317 — /context takes sub-views in the TUI too
`tools` and `history`.
**Rejected:** Leaving them CLI-only: the TUI holds the **live** history (no save lag, works before the first save) and the **full live registry** (18 tools vs the subcommand's 16), so it is the better home, not merely a convenience. An unknown view is refused rather than falling back to the budget report, so a typo cannot look like it worked.
**Where:** ROADMAP M317

<a id="dec-m315-2"></a>
#### M315 — context history reads a saved session
not the (absent) live one.
**Rejected:** Requiring a live conversation: the subcommand has none, and "what filled my window on that run?" is asked *after* the run, often about an unattended one — the session file is the artifact that survives it.
**Where:** [proposal](proposals/2026-08-history-breakdown.md)

<a id="dec-m315-3"></a>
#### M315 — Tool results are attributed by searching backwards
* from the result (M315).
**Rejected:** A forward scan from the start: a `tool_call_id` is unique only within one provider response, so a backend that numbers per response (and the smoke mock) reuses `c1` and the whole session is charged to the first such call — which is exactly what the driver reported before the fix.
**Where:** ROADMAP M315

<a id="dec-m315-4"></a>
#### M315 — A line stays a line when the detail is a…
the rule reconciling M312 (expanded), M313 (not), M315 (separate view).
**Rejected:** Treating the three inconsistently without a stated rule; the sixteen prompt sections are fixed, tools and messages are not.
**Where:** ROADMAP M315

<a id="dec-m313"></a>
#### M313 — Per-tool sizes are a separate view (context tools), not more…
the opposite of M312's decision, on purpose.
**Rejected:** Expanding the tools line as M312 expanded the prompt line: the measured shape differs. Six lines explained a 15,196-token block with a 10,441-token top item; sixteen would explain ~2,900 with a ~380 top item (top 5 = 49%, largest 12%) and push the lines that matter off the screen. Also rejected: a global tools flag (a flag that affects one subcommand is a worse contract than a sub-verb), and showing only the top N (the truncation problem is what pushed it out of `/context`).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m313-2"></a>
#### M313 — The per-tool view states the same total as /context
naming JSON array framing as the difference.
**Rejected:** Letting the two reports differ by 5 tokens, or giving framing its own row (noise at 5 tokens). Two reports on one thing showing two totals is the drift M310–M312 each fixed.
**Where:** [context_tools.sh](../tests/smoke/context_tools.sh)

<a id="dec-m312"></a>
#### M312 — The prompt builder reports its own section sizes
rather than a reporter re-measuring each piece.
**Rejected:** A second pass over the parts — the drift that left "the base persona + section headers" as an unnamed remainder, and that explained *nothing* on a graded attempt where every named part is zero. Two re-rendering helpers were deleted.
**Where:** [proposal](proposals/2026-08-sysmsg-breakdown.md)

<a id="dec-m312-2"></a>
#### M312 — Sections are listed non-zero-only, largest first
**Rejected:** Prompt order (reads like a table of contents; the reader has a full window, not curiosity); printing all sixteen (fourteen zeroes bury the two lines that matter); keeping the one-line form (it cannot hold sixteen parts without reintroducing a remainder).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m312-3"></a>
#### M312 — repoMap: false is recommended for graded attempts, not defaulted
though it takes `00-hello` from 29k to 9.4k.
**Rejected:** Defaulting it as M309 did for rules: the rules file was never what an exercise measures, but the map sometimes *is* — dropping it cost `06` three extra model calls, and tasks 20–22 are explicitly about reading this repository.
**Where:** [analysis](analysis/2026-08-06-sysmsg-breakdown.md)

<a id="dec-m311"></a>
#### M311 — context and sysmsg load their assets through one shared helper
so the report and the prompt cannot describe different prompts.
**Rejected:** Two copies of the load sequence (how they diverged: `context` printed `rules ~0` on every project, reporting 11% of the window where the truth was 57%). Moving asset loading ahead of subcommand dispatch — the shape M310 assumed the fix had, which would have changed startup for every subcommand; `run_sysmsg` already showed the local fix.
**Where:** ROADMAP M311

<a id="dec-m310"></a>
#### M310 — /context sizes what is actually sent
it applies the resolved tool-profile fence, and names the profile when the lean set is in play.
**Rejected:** Reporting both counts ("16 tools, 7 advertised") — unsent definitions cost nothing, and giving them a number invites budgeting for them; what `core` withholds is `doctor`'s job. Fixing only the TUI surface (one function, on purpose). Documenting the discrepancy instead (a documented trap is still a trap — M309's own reasoning).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m310-2"></a>
#### M310 — toolProfile: core is recommended for graded attempts, not defaulted
though measured at ~55% cheaper *and* more repeatable.
**Rejected:** Making it the default: it costs `hint`/`ask_for_help`, the machinery `attempt` exists to exercise. M309's rules change was free; a change that costs a capability is the operator's call, not one discovered by having a feature stop working.
**Where:** [analysis](analysis/2026-08-06-tool-profile-cost.md)

<a id="dec-m298"></a>
#### M298 — Compaction runs after routing
so it measures the model that will actually run.
**Rejected:** Teaching compaction about routing (the summarizer uses its own model; only the *trigger*'s window was wrong).
**Where:** ROADMAP M298

<a id="dec-m298-2"></a>
#### M298 — De-escalation needs hysteresis, not a threshold
down at 55% against up at 75%.
**Rejected:** A bare threshold — it oscillates escalate→compact→de-escalate, and each switch rebuilds the provider and drops the cached prefix.
**Where:** ROADMAP M298

<a id="dec-m298-3"></a>
#### M298 — Only a room-caused escalation may be undone
**Rejected:** Undoing any escalation — a verify-fail said the fast model was not *capable* enough, which freeing context does not change.
**Where:** ROADMAP M298

<a id="dec-m304"></a>
#### M304 — Mid-run posture changes only ever narrow
auto → chat → plan.
**Rejected:** Allowing widening — a socket that can loosen a running agent is a privilege-escalation surface wearing a convenience hat.
**Where:** [CONTROL.md](CONTROL.md)

<a id="dec-m304-2"></a>
#### M304 — The narrowness order is an explicit table, not the enum's…
**Rejected:** Comparing `enum jc_agent_mode` directly — it is declared `CHAT, PLAN, AUTO`, so `to > from` would call `auto → chat` a *widening* and invert the safety property in code that reads as correct.
**Where:** ROADMAP M304

<a id="dec-m299"></a>
#### M299 — Todo state is a word, not a checkbox
an in-content marker wins over the `status` field.
**Rejected:** Strict rejection of checkboxes (the habit outlasts the change); honouring both (renders `pending: [x] foo`, the exact drift being fixed). `incomplete` → *pending*, since reading it as in-progress would claim work had **started**.
**Where:** ROADMAP M299

<a id="dec-m319"></a>
#### M319 — The solving stance names only the support tools actually advertised
and states their absence under `core`.
**Rejected:** Listing all three unconditionally — under `core` none is sent, so the prompt was telling the model to call tools it did not have (M285's defect one layer up). Staying silent instead of saying so: a learner-model told nothing may keep hunting for a hint tool.
**Where:** ROADMAP M319

<a id="dec-m320"></a>
#### M320 — attempt's default profile stays full
(M320, settled). Across **two models and 24 runs the `hint` tool was never called once** — including six runs that FAILED with it advertised — so the capability cost M310 cited was imaginary. The flip is still refused, on a **better** objection the same runs produced: on `14-the-hollow-gate` the second model passed 1/3 under `full` and 0/3 under `core`, taking 33–37 model calls instead of 23–25. `core` is a win on tasks a model can do and a possible loss on tasks it cannot, and a curriculum default must hold for the learner who is struggling.
**Rejected:** Flipping it on the token saving (−55% to −75% on easy tasks); flipping it because the hint objection dissolved (a dissolved objection is not an argument for the change). Claiming `core` is *worse* — 1/3 vs 0/3 on budget-bound runs is noise-adjacent, and the analysis says so.
**Where:** [analysis](analysis/2026-08-06-hint-second-model.md)

<a id="dec-m319-2"></a>
#### M319 — attempt's default profile stays full
even though `core` is 60–75% cheaper, passes identically, and the hint ladder it "costs" was never called in 12 runs.
**Rejected:** Flipping the default on that evidence: the ladder's primary path is a *human* (`/hint`), this is one model on two tasks, and a default a beginner cannot see is the wrong place to save tokens if it changes what the curriculum hands them. Pre-registered as out of scope before the runs.
**Where:** [analysis](analysis/2026-08-06-hint-under-core.md)

<a id="dec-m318"></a>
#### M318 — craft defaults off under --lite, and is unchanged elsewhere
**Rejected:** Removing it outright (18 runs of one 31B model cannot license that: its claimed value is larger models and vaguer tasks, untested here); leaving it on everywhere (it is 329–386 tokens on *every* call, and on this model bought a measured nothing — 18/18 pass either way, no design note or named alternative on an under-specified probe in either condition).
**Where:** [analysis](analysis/2026-08-06-craft-ab.md)

<a id="dec-m318-2"></a>
#### M318 — A/B metrics and the probe were fixed before the runs
by the author of the thing under test.
**Rejected:** Choosing the framing after seeing the numbers — the pre-registration is what makes a negative result checkable rather than a matter of my good faith.
**Where:** [proposal](proposals/2026-08-craft-ab.md)

<a id="dec-m299-2"></a>
#### M299 — The craft prompt applies even under a persona override
**Rejected:** Treating it as part of the persona — *how* to work is not *who* is working; a blunt reviewer still analyses first.
**Where:** ROADMAP M299

<a id="dec-m303"></a>
#### M303 — --voice speaks approval prompts and errors, not just answers
**Rejected:** Reading only the reply — if the screen is not being read, anything jichi *waits on* must be audible or the session stops with no explanation.
**Where:** [VOICE.md](VOICE.md)

<a id="dec-m303-2"></a>
#### M303 — The spoken utterance is capped
**Rejected:** Reading long replies in full — there is **no barge-in**, so an unbounded monologue cannot be escaped. The cap is a safety property, not a nicety.
**Where:** [VOICE.md](VOICE.md)

<a id="dec-m302"></a>
#### M302 — A style: names an existing output style; it is not…
**Rejected:** A second persona mechanism — one place for personality, so improving "blunt" improves every specialist using it. A skill's style beats a profile's (named in *this* call); styles are **not** intersected like `tools`, since two tones have no intersection.
**Where:** [OUTPUT_STYLES.md](OUTPUT_STYLES.md)

<a id="dec-m321"></a>
#### M321 — A transport failure carries its diagnosis on the event, not…
and the message names the knob (`timeouts.connect` vs `timeouts.stall`).
**Rejected:** Logging `curl_easy_strerror` alone — for both timeouts it says "Timeout was reached", so it cannot tell the operator which limit to raise; and stderr is gone by the time anyone reads the log. A global "last error" accessor — the out-param on the request struct matches `resp_headers` and avoids process-wide state that forking would complicate.
**Where:** [analysis](analysis/2026-08-06-large-workload-telemetry.md)

<a id="dec-m321-2"></a>
#### M321 — Three "broken alias" findings were tested against the build before…
**Rejected:** Filing them from the log: it reports only `jichi 0.9.0`, and three of the four had already been fixed (M290's lesson — a log outlives the code it describes).
**Where:** ROADMAP M321

<a id="dec-m322"></a>
#### M322 — The iteration cap returns success and keeps the work
reporting `stop_reason: "max_iters"` with exit code 0.
**Rejected:** Returning an error: the cap is a circuit breaker against a loop, not a verdict on the work — every edit is real and the history resumes. But leaving it as `"done"` was equally wrong: a supervisor could not tell a half-finished task from a finished one.
**Where:** [AGENT_MODES.md](AGENT_MODES.md)

<a id="dec-m322-2"></a>
#### M322 — turn_capped is set only at depth 0
rather than reusing `last_run_capped`.
**Rejected:** Reusing it: it describes whichever run ended *last* and is cleared only on entry, so a capped subagent made a clean top-level turn report `max_iters` — confirmed by reverting the guard.
**Where:** ROADMAP M322

<a id="dec-m322-3"></a>
#### M322 — The daemon socket's mode is set via the umask around…
not by a later `chmod`.
**Rejected:** A chmod after bind: the socket exists at the looser mode in between, and a connect can already succeed. Leaving it to the umask at all — that was the bug (0775 measured), on the socket that accepts arbitrary prompts.
**Where:** [DAEMON.md](DAEMON.md)

<a id="dec-m323"></a>
#### M323 — A mid-turn compact event is emitted whenever the trigger fired
elisions or not (M323), carrying before/after/target/limit and a `short` flag.
**Rejected:** Emitting only when something was elided — that gated out the case a reader most needs: the pass ran, found nothing large to elide, and the request went out over the configured limit.
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m323-2"></a>
#### M323 — The short-fall warning is throttled by the agent loop, not…
**Rejected:** Warning from inside `jc_compact_midturn`: it runs after every round of tool results, so one condition produced 1,038 warnings in the measured workload. Only the caller knows where a turn begins.
**Where:** ROADMAP M323

<a id="dec-m323-3"></a>
#### M323 — M323 shipped the observability and left the behaviour alone.
**Rejected:** Also "fixing" it: dropping whole message pairs, summarizing mid-turn, or refusing the call are each lossy in a different way, and choosing needs a workload to measure against rather than a preference. Shipping one as though the visibility settled it would be the dishonest half of a good idea.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m324"></a>
#### M324 — list_files gained a pattern so glob could become a real…
rather than keeping glob hint-only.
**Rejected:** Arguing the schema objection (M219 was right that a mismatched alias teaches nothing — but the fix was to remove the mismatch); a `glob` tool of its own (a second file-listing tool with overlapping semantics); leaving it hint-only (46 invented calls and 7,761 shell calls say the hint was not working). `find`/`ls`/`dir` stay hints: their arguments are flags, not patterns.
**Where:** ROADMAP M324

<a id="dec-m324-2"></a>
#### M324 — The pattern reuses jc_glob_match
the edit-scope fence's matcher.
**Rejected:** A second glob implementation: `editScope` and `list_files` would then mean different things by `**`, and a user cannot be expected to hold two dialects.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m324-3"></a>
#### M324 — list_files is now path-fenced
a behaviour change on an existing tool.
**Rejected:** Leaving it unfenced: it returned only names, which was a mild leak at one level and a filesystem enumeration once it could recurse. The feature and the fence shipped together on purpose.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m325"></a>
#### M325 — A killed parallel child names the limit and the knob
a failed `fork()` carries its `errno`.
**Rejected:** The bare `sub-agent timed out` / `fork failed`: the first sends an operator hunting a hung child rather than a 300 s default that was killing half their children, and the second cannot separate EAGAIN (lower `maxParallelAgents`) from ENOMEM (run fewer children).
**Where:** [PARALLEL.md](PARALLEL.md)

<a id="dec-m325-2"></a>
#### M325 — The config-key lint no longer counts the history as documentation
roadmap, anecdotes, decisions, deferred, analysis/ and proposals/ are excluded from the key search.
**Rejected:** Treating any mention in `docs/` as coverage — that is how `parallelTaskTimeout`, `memBudgetMb`, `runTimeout` and `wisdom` stayed documented only in a 12,000-line changelog while a lint asserted otherwise. Not excluded: `docs/reading/`, which is thin but genuinely user-facing; the fix there is to write the reference entry, not to tighten the lint until it agrees.
**Where:** [config_keys_lint.sh](../tests/smoke/config_keys_lint.sh)

<a id="dec-m325b"></a>
#### M325b — One registrar for the conditional built-ins
shared by `main()`, `context` and `doctor`.
**Rejected:** Letting the reporting surfaces build their own subset: they listed 16 tools where a live turn sent 26, so the report under-stated the cost it exists to measure — and every conditional tool added since M41 had widened that gap without anyone noticing.
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m325b-2"></a>
#### M325b — MCP tools stay out of the report, and the report…
**Rejected:** Connecting to every configured server to list them (a read-only report must not); or omitting them silently, which would let the count look complete.
**Where:** same

<a id="dec-m326"></a>
#### M326 — repoMap stays on by default for attempt
on a measured reason replacing a false one.
**Rejected:** Flipping it on the token saving (15–62%): without the map the model makes **+67%/+50% more model calls**, which on the hardest task cancelled the saving exactly (408k → 409k), and the leaner arm went 0/3 there — the second time after M320 that a leaner prompt goes 0/3 on the hardest task. Also rejected: keeping the old reason (*"tasks 20–22 are about reading this repository"* — they are self-contained fixtures, checkable by reading them).
**Where:** [analysis](analysis/2026-08-06-repomap-navigating-tasks.md)

<a id="dec-m342"></a>
#### M342 — A kill note names the limit that fired, as a…
the watched runner's three kill causes (byte cap, memory watchdog, abort) each get their own note.
**Rejected:** One shared label — what shipped until M342, and it read "exceeded the memory budget" for a byte-cap kill, against which a driven model escalated the same `git log` 32× in one turn (the remedies for the two causes point in opposite directions). Naming the knob instead of the number: the runner serves three callers whose limits are a compile-time spill ceiling, a config cap, and a hardcoded 65536, so any key it named would sometimes be wrong — the caller's own truncation/spill note carries the advice. Changing exit 137: the SIGKILL is a fact.
**Where:** [analysis](analysis/2026-08-09-three-seams.md)

<a id="dec-m343"></a>
#### M343 — The verifier's kind is a checked declaration, not a split…
one optional value on the existing verifier, compared against the tree at run start.
**Rejected:** Finding 10 rejected both alternatives and this is neither: *splitting the flag* fails silently on the wrong choice — here a wrong choice is caught by the probe (a green "goal" is called out as forcing nothing; a red "invariant" warns) — and *inference* reclassifies a genuinely broken tree as a goal, which a declaration cannot do. Also rejected: changing any outcome on the verdict (advisory, like M83/M96/M331), and inferring the kind when undeclared (UNSET keeps pre-M343 behaviour byte-for-byte).
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m343-2"></a>
#### M343 — Declaring a kind arms the baseline probe
no separate `--verify-baseline` needed.
**Rejected:** An annotation-only key that merely relabels warnings: a declaration nobody checks is a comment, and an unchecked "goal" would *silence* the baseline alarm while verifying nothing — strictly worse than today. The probe costs one verifier run at start, which the operator opted into by declaring.
**Where:** ROADMAP M343

<a id="dec-m343-3"></a>
#### M343 — A bad kind on the CLI is a hard usage…
**Rejected:** Uniform leniency — a silently dropped CLI declaration is the exact trap the flag exists to remove; uniform strictness — hard-failing every session on one typo'd key in a shared config punishes the wrong person. The config's `mode` key silently ignores bad values; this one at least warns.
**Where:** ROADMAP M343

<a id="dec-m662"></a>
#### M662 — --strict-green stays opt-in, on a measurement that reversed the earlier…
M343 measured 0 downgrades in 21 scoped green runs and M459 added 0 of 2, and on that evidence M657 recommended enabling it under `--unattended`. Re-measured on the corpus as it now stands — 35 scoped runs ending `ok` — it would downgrade **16 of 35, 46%**. Of the 1,158 flagged paths, **85% are the work's own output**: downloaded PDFs, `*.o`/`*.so`/`*.beam`, `test_0.tmp`, jichi's own `.jichi/` state, and ignore files the run wrote.
**Rejected:** **Flipping the default** on the 0/21: that corpus was this project's own tidy gates, and the rate is not a property of the rule but of what the run builds. **Escalating it in `doctor --unattended`**: doubly wrong — `--unattended` is a doctor flag, so the escalation would have made doctor *demand* a setting that fails 46% of real runs. **Doing nothing**: rejected too; the rule is too blunt, and the sharp line the data points at is **tracked vs untracked** — a gate file is in version control, a downloaded PDF is not. The journal does not record that yet, which is the next step rather than this one.
**Where:** ROADMAP M662, [DEFERRED.md](DEFERRED.md) §gate integrity

<a id="dec-m714"></a>
#### M714 — search_code speaks extended regular expressions (grep -rnE)
Models write `a\|b`, `+` and `( )`: 230 of 888 patterns in the bench's telemetry used a bare `\|` against 5 using GNU-basic `\\|`, and in grep's basic mode 11 of the corpus pilot's 45 searches were false `(no matches)` -- re-run against the same tree -- and 1 a spurious error. An invalid pattern is now reported as one, with grep's own message.
**Rejected:** Keeping basic mode and saying so in the schema -- guidance against a habit measured at 46 to 1. Retrying in the other dialect when a search comes back empty -- two semantics behind one call, and a pattern valid in both can match differently. A `dialect` argument -- a choice the model would have to get right on every call, to work around a default that is simply wrong for it. Fixed strings by default -- throws away the regex models do use.
**Where:** [pilot](analysis/2026-09-23-the-corpus-pilot.md) §3

<a id="dec-m715"></a>
#### M715 — The journal records the SIZE of a turn's answer, not…
`end.answer_bytes` beside `start.one_shot`, the two facts DEFERRED item 7 lacked; plan D6 had named the field `answered`, a boolean.
**Rejected:** **A boolean `answered`** -- a judgement the journal cannot make: a capped run's last words ("Let me try with explicit tabs:") are text without being an answer, so `true` would count them and the recording would settle the question item 7 asks; and `answered` already means, in the same journal, that a human answered the model's `ask` (M359), so `select(.answered)` over a journal would mix the two. **The answer's text in the journal** -- the journal records no content by design; the text is on the run's stdout, or in `full`-tier telemetry. **A classifier of "real" answers** -- guessing at prose is how a typo becomes a policy (M168); the measurement prints sizes in buckets and leaves the reading to whoever decides.
**Where:** [plan](plans/2026-09-after-m712.md) §7; `tests/smoke/turn_answer.sh`

<a id="dec-m715-2"></a>
#### M715 — A read's range is reported by the tool that executed…
`read_file` sets `has_range`, `range_offset` and `range_limit` in its result, as a command tool sets `exit_status`, and the telemetry `tool_call` event copies the two numbers.
**Rejected:** **Append the range to the summary**, which plan D6 proposed -- the summary is also what the TUI and headless stderr print, what ACP titles show and what a screen reader speaks (M571, M566), so a telemetry need would change what an operator hears. **Parse `offset`/`limit` at the telemetry site** -- `jc_tool_execute` repairs (M148) and unwraps (M172) the arguments before the tool runs, so a second reader records what was sent rather than what ran, and misses a wrapped call's range entirely (`read_range.sh` check 3); it would also be the third copy of the tool's defaults, after `jc_tool_read.c` and `jc_compact.c` -- the drift that cost M286. **Select the tools by name** -- the result carries a range only when the tool reports one, so there is no list to keep.
**Where:** [plan](plans/2026-09-after-m712.md) §7; `tests/smoke/read_range.sh`

<a id="dec-m884"></a>
#### M884 — The rules walk's root is the nearest directory holding a `.git` of either kind…
**Decision** (2026-10-03, carrying out what `docs/RULES.md` already said). The walk stops at a `.git`
directory or file: a checkout's, a linked worktree's or a submodule's root, the directory
`git rev-parse --show-toplevel` names. Rejected: **ask git for the top level** -- the same answer, for a
process on every run, and none where git is not installed; **follow the worktree's `gitdir:` to the main
checkout** -- a worktree carries its own checked-out rules files, its branch's, and the main checkout may
be on another branch; **walk on to the filesystem root** -- every parent directory's rules would reach
every project below it, and rules for all projects have their own file, `~/.config/jichi/AGENTS.md`;
**keep a submodule inside its superproject** -- the old walk did, by the same accident, and nothing
documented it; a submodule is a repository of its own.
**What it costs:** a submodule that relied on its superproject's rules loses them until they are listed
under `instructions` (in a project's config, a trusted one), which `docs/RULES.md` and the changelog say.
**Where:** ROADMAP M884, `src/chat/jc_rules.c` (`jc_rules_load`), [RULES.md](RULES.md),
`tests/test_rules.c`, `tests/smoke/rules_walk_worktree.sh`

<a id="dec-m959"></a>
#### M959 — The 30 s `timeouts.stall` default stays, driven turns run it off…
**Chose:** **Keep `timeouts.stall` at its 30 s default, and correct the claim around it: stall is a CAP on silence, not a fence on a dead stream** (2026-10-15, M958 measured the two cut distributions that were always going to show this: 4 of 4 first attempts cut on the boards, a replay of the same turn at `stall:0` answering in 64 s). The rig's driven config sets `"stall":0` (the measurement protocol), the false "kills a frozen stream without capping a slow-but-progressing generation" comment at the `stall_timeout_secs` field is corrected, and the FINAL DEFAULT is deferred to a `stall_ms` cut distribution (the M958 instrument: a cut attempt that arrived zero bytes records the time-to-cut, so the distribution is no longer censored at the cap). **Supersede this row** when that distribution has been collected on slow local models across prompt sizes: then decide between (i) a measured default from it and (ii) `stall:0` by default under the `deadStream` fence — whichever the data supports, with the other rejected here in that row.
**Rejected:** **A measured default now** — the workstation telemetry carries ZERO `stall_ms` cut events (91 files, counted 2026-10-17): every cut that has ever been measured (4/4 first attempts, VisionFive 2 2026-10-14; 4/4, LicheeRV Nano 2026-10-15) predates the instrument by a day, and a number chosen from the `ttft_ms` of successful calls would be justified by a distribution censored at the very value it would set. **`stall:0` by default now** — the 11,677 s dead-stream run (M810) is the measured cost of no cap; `deadStream` bounds a dead stream, not a healthy slow prefill, so 0-by-default with no data on how slow slow gets is choosing between two unmeasured risks. **Rewording the claim without the protocol** — a comment that says "a cap" while every driven row still runs under it is the M519 shape (a fence silently off).
**What it costs:** a local prefill slower than 30 s still cuts in any config that keeps the default; every reader of a `model stalled` result on a local model has to read the cap out of the message. That is the honest state of an unmeasured default, and the supersession path above names the data that closes it.
**Where:** [internal handoff 2026-10-15](internal/2026-10-15-stall-default-handoff.md), M958 (`stall_ms` instrument, `tests/smoke/stall_silent_answer.sh`), `include/jc_http.h`, [MODELS.md](MODELS.md)

## Safety

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **Typed input is kept across TUI runs; a line jichi can tell holds a secret is not written at all** (M763) | [M763](#dec-m763) |
| **The secret registry is armed once, right after the config loads; jichi's own two key names are on the built-in scrub list** (M608) | [M608](#dec-m608) |
| **A dangling symlink leaf is resolved to its target (40-hop bound, a cycle failing closed), so every fence caller gets one answer** (M607) | [M607](#dec-m607) |
| **The wizard's closing advice is driven by what actually happened to the key, via an explicit state rather than a `getenv` probe** (M326f) | [M326f](#dec-m326f) |
| **A key-shaped `apiKeyEnv` is a doctor FAIL, not a WARN** (M326e) | [M326e](#dec-m326e-fail) |
| **A diagnostic about a suspected key never repeats the value** (M326e) | [M326e](#dec-m326e-diag) |
| **The wizard's key prompt hides what you type; the variable-name prompt does not** (M326e) | [M326e](#dec-m326e-echo) |
| **External content is fenced as data, and the fence is called a mitigation, not a solution** (M300) | [M300](#dec-m300-fence) |
| **Only *model-chosen* URLs are fenced** (M300) | [M300](#dec-m300-urls) |
| **The untrusted-content rule is unconditional, unlike the craft section** (M300) | [M300](#dec-m300-uncond) |
| **`kineticCommandsAllow` is checked *before* `deny`, so a safe-state `stop_all` survives a blanket refusal** (M163a) | [M163a](#dec-m163a) |
| **The physical E-stop is not substitutable by software** (M306) | [M306](#dec-m306) |
| **An out-of-`int`-range JSON number is clamped to `INT_MAX`/`INT_MIN`, not rejected** (M469) | [M469](#dec-m469) |
| **The Guix machine definition refuses when `JICHI_BENCH_PUBKEY` is unset** (M466) | [M466](#dec-m466) |
| **jichi makes itself non-dumpable by default; a core dump is the opt-in** (M770) | [M770](#dec-m770) |
| **A policy boolean is read as its writer meant -- the widening fields too** (M843) | [M843](#dec-m843) |
| **A saved session's mode may narrow a run's, never widen it -- in every front-end** (M845) | [M845](#dec-m845) |
| **One predicate decides a tool runs a shell; every shell gate uses it, run_tests included** (M846) | [M846](#dec-m846) |
| **A formatCommand `{}` is refused inside quotes, rather than silently rewritten** (M847) | [M847](#dec-m847) |
| **A subtask command runs under the user's posture; a delegate that asks nobody is path-fenced; the mentor is read-only** (M848) | [M848](#dec-m848) |
| **A delegate's shell call follows the user's posture; the privileged allowlist is never a delegate's** (M849) | [M849](#dec-m849) |
| **A project config applies only in the safe direction until the user trusts its content** (M851) | [M851](#dec-m851) |
| **A write child's temp dir lives in its worktree's git admin dir; a fence that cannot be applied refuses the task** (M852) | [M852](#dec-m852) |
| **A delegate's call to an unregistered `finish`-like tool with a `message` ends its turn; nothing else does** (M853) | [M853](#dec-m853) |
| **A write child that did not end on its own answers with a note and merges only after a green verify** (M854) | [M854](#dec-m854) |
| **A parallel batch keeps and empties the shared stash stack before and after; children are told to commit** (M855) | [M855](#dec-m855) |
| **A resume replays the predecessor's range from its recorded base, not the kept commit alone** (M856) | [M856](#dec-m856) |
| **A fenced write child's own tool-output store is created before the fence and granted, alone** (M857) | [M857](#dec-m857) |
| **A reply is cut when its completion_tokens reach max_tokens; a cut final reply is M854's fourth ending** (M858) | [M858](#dec-m858) |
| **A cut reply is continued once -- in a delegate by default, at a headless top level when configured** (M859) | [M859](#dec-m859) |
| **A parallel batch's id is `<base>-b<k>`; only a batch that holds its base's lock removes it** (M860) | [M860](#dec-m860) |
| **A parallel task's `max_tool_calls` binds, is announced once at four-fifths, and ends the task as M854's endings do** (M861) | [M861](#dec-m861) |
| **A red worktree verify shows the lines that look like failures first, then the tail, and keeps the whole output** (M862) | [M862](#dec-m862) |
| **A write child's discarding git command runs only after jichi has kept its worktree, untouched** (M863) | [M863](#dec-m863) |
| **A parallel child's log lines begin with the UTC time and its task_run; every line is one write** (M864) | [M864](#dec-m864) |
| **A write child that changed an existing test assertion is kept, not merged, even after a green verify** (M865) | [M865](#dec-m865) |
| **A child's refused-call loop ends it at nine, copied placeholders at three, and every loop stop is named as a loop** (M869) | [M869](#dec-m869) |
| **A child whose last 24 calls bring nothing new is stopped: a novelty window beside M829's counts** (M870) | [M870](#dec-m870) |
| **The C++ build is gated in `make ci`: built, linked and tested with g++** (M871) | [M871](#dec-m871) |
| **A cue the request quotes or asks about infers nothing; an inferred rule carries its sentence** (M872) | [M872](#dec-m872) |
| **An inferred rule is matched against what a command runs; an authored rule against its words** (M873) | [M873](#dec-m873) |
| **`constraintsAutoAdopt` turns inference off; a project config may only leave it on** (M874) | [M874](#dec-m874) |
| **A metric gate beside the test-edit hold: a moved pin merges when the base's measurement backs it** (M875) | [M875](#dec-m875) |
| **A delegate's report names its run's own model failure first; its last failing call is an earlier event** (M876) | [M876](#dec-m876) |
| **A TLS handshake in progress is still connecting; `connect` also fires on a slow handshake** (M877) | [M877](#dec-m877) |
| **A read-only phrase is an order only when its clause speaks to the reader; where it cannot tell, a statement** (M878) | [M878](#dec-m878) |
| **A floor counts the pages that ship, so a checkout and a shipped tree read the same** (M879) | [M879](#dec-m879) |
| **Whether TCP was up is asked of the transfer's sockets as libcurl closes them** (M880) | [M880](#dec-m880) |
| **A test that drives the wizard stays off the network and waits for its prompts; the rig reads doctor's summary** (M881) | [M881](#dec-m881) |
| **Data handed to libcurl outlives every connection; the sanitizer turn runs over a kept connection** (M882) | [M882](#dec-m882) |
| **A SIGTERM from outside is named as one; a call the run's stop killed met no limit** (M885) | [M885](#dec-m885) |
| **A child's failing run is reset only by a shell command that changed the worktree's content** (M886) | [M886](#dec-m886) |
| **A write child's file tools reach what its shell fence grants, and no further** (M887) | [M887](#dec-m887) |
| **Every new directory a write child adds is named; one over 1 MB or 100 files is held, the rest merges** (M888) | [M888](#dec-m888) |
| **The event log repairs any line a producer cut mid-character; the byte-counted cuts left in src/ are held by count** (M889) | [M889](#dec-m889) |
| **`"merge": false` keeps a write task's work at any ending; the merge stays file by file** (M890) | [M890](#dec-m890) |
| **Each task's section names its model, and says when it was the lead's own** (M891) | [M891](#dec-m891) |
| **The end-of-run check names its sender and a way out; an answer that changed nothing ends the run on its verdict** (M892) | [M892](#dec-m892) |
| **A failure on a line that failed before the run's first change predates it: said so, no retry spent, once** (M893) | [M893](#dec-m893) |
| **The CI example names its key's variable; a driver runs the recipe as printed; the class waits for a design** (M894) | [M894](#dec-m894) |
| **A silent success says it succeeded; its third per normalised command in a turn says what that means** (M895) | [M895](#dec-m895) |
| **A shared fence grant is documented as shared state; the removal advisory waits on a measurement** (M896) | [M896](#dec-m896) |
| **The first-token time is the first event the provider calls output, asked through the vtable** (M897) | [M897](#dec-m897) |
| **An attempt journals into the process envelope's journal when there is one, else its own** (M898) | [M898](#dec-m898) |
| **The board tool returns the change and the counts; `list` counts the done cards unless asked** (M932) | [M932a](#dec-m932a) |
| **`jichi board --output json` is read-only, and an unreadable board is an error, not an empty one** (M932) | [M932b](#dec-m932b) |
| **Every headless run a living page shows pins its stdin, not only the ones that can hang** (M944) | [M944](#dec-m944) |
| **An error inside a 200 stream fails the call like an HTTP 4xx: no answer, no retry** (M909) | [M909](#dec-m909) |

<a id="dec-m763"></a>
#### M763 — input history keeps no secret
Such a line is one with a registered secret (a resolved API key) or a token in a common credential shape; it stays in the session's memory. The file is 0600, bounded to 500 entries, and `inputHistory: false` turns it off.
**Rejected:** *Writing every line*, as a shell does — the one thing HARDENING §7.5 deferred the feature for. *Writing the line with the secret replaced by `***`* — recalling it gives back a line that no longer does what it did, and the part kept (a command naming the key) is often the half that tells where the key belongs. *Not keeping history at all* — the convenience is real, and the shell's rule (*do not type a secret*) applies here too; jichi adds what it can see on top. *An opt-in switch* — a feature off by default is not found, and the rule above is what makes it safe to have on.
**Where:** [HARDENING.md](HARDENING.md) §7.5, [ROADMAP M763](ROADMAP.md).

<a id="dec-m608"></a>
#### M608 — secret registry armed once
**Rejected:** *Registering inside each subcommand* — four sites today and the next one forgotten; arming belongs where the config becomes final (the M444 lesson). *Scrubbing by pattern* (`*_API_KEY`) — drops a user's unrelated variables from every shell call, when the names jichi writes are known exactly. *A lint universe that includes `docs/`* — measured: 11 distinct names there, 7 illustrative.
**Where:** [ROADMAP M608](ROADMAP.md).

<a id="dec-m607"></a>
#### M607 — symlink leaf resolved to its target
So every fence caller (write, read, `jc_path_in_root`, reference roots) gets one answer.
**Rejected:** *`O_NOFOLLOW` at the open* — covers writes only and needs `open(2)` in place of `fopen`; a read through the same link would still be judged inside. *`lstat` in `jc_app_path_denied_ex` alone* — the fuzz target and other callers use the resolver. *Refusing every symlink leaf* — breaks a project's legitimate link to a build output not written yet (`dangle_in` in `test_symlink_escape`).
**Where:** [ROADMAP M607](ROADMAP.md).

<a id="dec-m326f"></a>
#### M326f — closing advice driven by what happened to the key
Via an explicit `jc_setup_key_state` rather than a `getenv` probe.
**Rejected:** *Re-probing the environment at the end* — it reports the truth about the *current shell* and the wrong thing about the next run, since the start script sources `~/.jichi.env`. That is precisely how a user who had just handed over their key got told three times to supply it.
**Where:** [ROADMAP M326f](ROADMAP.md).

<a id="dec-m326e-fail"></a>
#### M326e — key-shaped apiKeyEnv is a FAIL
No POSIX shell can export `sk-live-x=...`, so `getenv` can never resolve it: the model has no key and every call 401s.
**Rejected:** *WARN* (the M285 shape for a degraded asset) — rejected because this is unresolvable, not degraded, and the pre-existing "no API key … set apiKey/apiKeyEnv" WARN already fired while advising the very thing the user believed they had done.
**Where:** [ROADMAP M326e](ROADMAP.md).

<a id="dec-m326e-diag"></a>
#### M326e — a key diagnostic never repeats the value
The refusal and the doctor finding name the model and the fix, not the string.
**Rejected:** *Quoting the offending value*, as most "invalid input" messages do — if the diagnosis is right, the value is the user's key, and a doctor report is the most likely thing to be pasted into an issue. Two smoke checks pin the absence.
**Where:** [ROADMAP M326e](ROADMAP.md).

<a id="dec-m326e-echo"></a>
#### M326e — key prompt hides input, name prompt does not
**Rejected:** *Hiding both* — the name prompt is asking for a NAME, and masking it would reinforce the mental model that produced the bug. *Adding a no-echo flag to `jc_term_readline`*, whose editor renders keystrokes itself — termios `ECHO` off around one `fgets` is the smaller change.
**Where:** [ROADMAP M326e](ROADMAP.md).

<a id="dec-m300-fence"></a>
#### M300 — external content fenced as a mitigation
**Rejected:** *Claiming it solves prompt injection* — it does not, and saying so would be the dangerous part. The real defences are those that do not need the model's cooperation.
**Where:** [HARDENING.md](HARDENING.md) §6b.

<a id="dec-m300-urls"></a>
#### M300 — only model-chosen URLs are fenced
`fetch_url`, `@rss:`, `web_search`, MCP *resources*.
**Rejected:** *Fencing workspace files* (would drown the prompt) and *MCP tool results* (a hand-configured server is semi-trusted, like the user's own repo).
**Where:** [jc_untrusted.h](../include/jc_untrusted.h).

<a id="dec-m300-uncond"></a>
#### M300 — the untrusted-content rule is unconditional
Unlike the craft section.
**Rejected:** *Gating it with prose guidance* — a user turning off style advice must not silently turn off a safety warning.
**Where:** [ROADMAP M300](ROADMAP.md).

<a id="dec-m163a"></a>
#### M163a — kineticCommandsAllow is checked before deny
Re-verified M306, so a safe-state `stop_all` survives a blanket refusal.
**Rejected:** *Checking deny first* — it would disarm the E-stop exactly when it is most needed.
**Where:** [ROBOTICS_BRINGLIST.md](ROBOTICS_BRINGLIST.md).

<a id="dec-m306"></a>
#### M306 — the physical E-stop is not substitutable by software
**Rejected:** *Trusting `stop_all` alone* — it needs jichi alive, a model reachable, and a turn in flight. Keep both; trust only one.
**Where:** [ROBOTICS_BRINGLIST.md](ROBOTICS_BRINGLIST.md).

<a id="dec-m469"></a>
#### M469 — out-of-range JSON number is clamped, not rejected
**Rejected:** *Leaving `node->valueint = (int)val`* — undefined behaviour, and jichi parses JSON it did not write (model responses, fetched pages, MCP results) where `{"x":1e300}` is valid and enough to reach it. *Rejecting the document as invalid* — it **is** valid JSON, and refusing a whole model response over one large number turns a latent UB into a live outage. *Leaving `valueint` at 0* — silently claims the number was zero; a saturated value is at least ordered correctly, and `valuedouble` keeps the exact value either way. *Adding a range error callers must check* — 40-odd sites read `valueint` directly, and a new error nobody checks is worse than a defined value.
**Where:** [ROADMAP M469](ROADMAP.md).

<a id="dec-m466"></a>
#### M466 — the Guix machine definition refuses an unset pubkey
**Rejected:** *Defaulting to `$HOME/.ssh/id_ed25519_jichi_bench.pub`* — the wrong guess there is an **authorized credential** in a built image, worse than a build that stops. *Keeping the hardcoded `/home/u/...` path* — it made the one platform whose row cannot rebuild itself unrebuildable on every host but the one that wrote the file. *A `--pubkey` flag* — this is a Guile machine definition consumed by `guix system`, not a script with an argument parser.
**Where:** [ROADMAP M466](ROADMAP.md).

<a id="dec-m770"></a>
#### M770 — jichi is non-dumpable by default
A same-user child read the key back out of jichi's own `/proc/<pid>/environ`, which the M130 scrub never touched. `prctl(PR_SET_DUMPABLE, 0)` at the top of `main()` makes that file and `fd/` refuse every other process of the user (measured), and `mem` and ptrace with them; the kernel resets the flag at exec, so the children are ordinary. `JICHI_KEEP_DUMPABLE=1` gives the core dump and the debugger back, and doctor warns while it is set.
**Rejected:** *Overwriting the key in jichi's original environment block* after reading it — hides `environ`, but not the copy jichi keeps to use, which `/proc/<pid>/mem` exposes wherever Yama `ptrace_scope` is 0, nor `fd/`; one flag covers all three. *Opt-in hardening* — the exposure was measured and the cost is a debugging convenience, so the default protects and the convenience asks. *A doctor check alone* — it reports a hole without closing it.
**Where:** [ROADMAP M770](ROADMAP.md), [HARDENING.md](HARDENING.md) §2.

<a id="dec-m843"></a>
#### M843 — A policy boolean is read as its writer meant, the widening fields too…
`parse_policy_field` serves two fences (`permissions.deny`, an MCP server's `deny`) and two widening fields (`permissions.allow`, `autoApprove`). M843 reads all four leniently, so `"allow": 1` now means allow-all, where it used to mean nothing.
**Rejected:** **Lenient for the fences only, strict for the widening fields** -- a strict read fails safe there, but it is still a silent misread of an explicit setting, CLAUDE.md's rule is who wrote the JSON and not which way the key points, and two dialects in one reader would be a trap for the next key. **A warning on a non-boolean true in a widening field** -- no reader warns on spelling anywhere else, and the widening keys' real risk, a project's own config file setting them, is the trust step's to bound (decided 2026-10-01, the last fix of the same batch).
**Where:** ROADMAP M843, `src/config/jc_config.c`, `tests/test_config.c`

<a id="dec-m845"></a>
#### M845 — A saved mode may narrow the run, never widen it, in every front-end…
ACP's load applied a session's saved mode unconditionally, and the TUI's and the headless resume applied it whenever no `--plan` or `--auto` was given. M845 holds all four places to one rule, the control channel's: a saved mode may narrow the process's mode, never widen it, and a pinned mode is kept against a stricter one -- with a note whenever a saved mode is kept out. "Resume restores the saved mode", documented until now, holds for the same or a stricter mode only.
**Rejected:** **ACP only** (the defect as found) -- the TUI and the headless resume would keep widening chat to auto, silently in a headless run. **Restoring the saved mode with a warning** -- a warning printed after the mode changed does not undo the writes that follow. **A switch to keep the old behaviour** -- a switch that removes the approval gate on resume is the hole itself, and `--auto` already says it explicitly.
**Where:** ROADMAP M845, `src/chat/jc_perm.c`, `src/chat/jc_app.c`, `tests/smoke/saved_mode_never_widens.sh`

<a id="dec-m846"></a>
#### M846 — One predicate for "this tool runs a shell", used at every gate…
Five of the six gate sites that select a shell command named `run_terminal_command` directly, and
`run_tests` -- which runs its `command` through the shell -- slipped `--strict-scope`, the
privileged gate and the kinetic gate. M846 makes `jc_tool_name_runs_shell` the single source of
truth and routes all six through it, so `run_tests` is fenced like `run_terminal_command`
everywhere.
**Rejected:** **Fixing only `--strict-scope`** (the finding as filed) -- the privileged, kinetic
and loop-key sites carried the same name list and the same latent gap; a predicate applied at one
site and not the others is the drift that caused this. **A `tools:` capability flag on the tool
struct** -- cleaner in theory, but it would touch every tool definition for a two-name set, and the
gates ask about a name they already hold. **Treating a user-defined or MCP tool as a shell** -- its
command line is fixed by config, so no free-form shell string the user wrote passes through it; it
is deliberately out.
**Where:** ROADMAP M846, `src/tools/jc_tool.c` (`jc_tool_name_runs_shell`), `src/chat/jc_agent.c`,
`tests/smoke/shell_tools_fenced_alike.sh`

<a id="dec-m847"></a>
#### M847 — A formatCommand placeholder inside quotes is refused, not rewritten…
The model-supplied path is substituted into `formatCommand` already single-quoted, which is safe
only outside the author's own quotes. A `{}` inside quotes let the path break out. M847 refuses
that shape.
**Rejected:** **Rewriting the path to be safe inside the author's quoting context** -- it would keep
every existing config working, but it means re-implementing shell quote-nesting (single, double,
backslash, the lot) in the builder, and a mistake there reintroduces the hole silently; a refusal is
simple and provable. **Leaving it and only fixing the packs** -- the next person to copy the old
`FORMATTING.md` shape would hit it again. **Switching `formatCommand` to an argv (no shell)** --
real formatter invocations (`Rscript -e '...'`) need the shell, which is why it is a string like
`testCommand` and `verify`; changing that is a larger contract break for a narrower gain.
**The cost, stated:** a hand-written `formatCommand` with `{}` inside quotes now errors until it is
moved -- a breaking change, chosen over a silent vulnerability.
**Where:** ROADMAP M847, `src/util/jc_fmtcmd.c`, `tests/test_fmtcmd.c`, `tests/smoke/format_command.sh`

<a id="dec-m848"></a>
#### M848 — A subtask command is the user's turn, so it asks like one…
Decided by the operator on 2026-09-30 (the scaffolding review's S2): a `subtask: true` command runs
under the user's own posture (1c) -- in chat it asks, headless without `--auto` a change is refused,
under `--auto` it runs -- and every delegate that runs WITHOUT asking (spawn_subagent, a parallel
child, ask_for_help's helper, a workflow stage) has the path fence on even in chat mode (1e); the
scaffolded mentor is read-only, its answer saved as the draft (1a). Before M848 a subtask ran with
the delegate's AUTO posture and no allow list, so `/learn` ran the shell and wrote files with no
prompt in a non-auto run (measured with a real model; ROADMAP M848).
**Rejected:** **1a alone** (the read-only mentor) -- `init` never overwrites a pack file (measured
2026-09-30), so it reaches only new projects, and a user's own subtask command stays fully powered
and unprompted. **1b alone** (apply the profile's `tools:`, keep the AUTO posture) -- the mentor's
own list still includes `write_file`, unprompted, and a command with no `agent:` has no list at all.
**1d** (read-only by default, a new `write: true` key) -- a breaking change for every user command
that writes, for a problem 1c solves without one.
**One more choice, made in the code:** two fences that share no tool (a profile and a
`restrict-tools` skill) used to yield an empty list, which read as no fence. The helper now yields a
fence that admits nothing, and `spawn_subagent` **refuses** the pair, naming both, rather than run a
child with no tools -- a toolless child cannot do the task either, and the parent model can act on
an error it reads.
**What it costs, stated:** a `/learn` while a draft exists writes `lessons.draft.md.answer` beside it
(M423) instead of asking to overwrite; existing projects' writable mentors now ask once per write in
chat, and headless without `--auto` their writes are refused (the answer is still saved). Whether a
delegate the *model* starts should also follow the user's posture is left open (DEFERRED def-286).
**Where:** ROADMAP M848, `src/chat/jc_agent.c` (`run_subagent_posture`), `src/chat/jc_app.c`
(`jc_app_path_fence_on`), `src/tools/jc_tool.c` (`jc_tool_allow_intersect`),
`tests/smoke/subtask_follows_posture.sh`, `tests/smoke/attempt_profile_fence.sh`

<a id="dec-m849"></a>
#### M849 — A delegate asks before the shell; the privileged allowlist is never a delegate's…
Decided by the operator on 2026-10-01 (the scaffolding review's S4), the recommended option in each
of two questions. **1.** A delegate's SHELL call -- `run_terminal_command`, and `run_tests` with a
command -- follows the user's own posture: asked in chat, refused where the delegate cannot ask,
run under `--auto`. Its other tools keep the AUTO posture. **2.** `privilegedCommandsAllow` holds
for the user's own turn (the main turn, a subtask command) and a top-level `--auto` run, never for
a delegate; a delegate's privileged command is refused and audited `delegate_refused`.
**Why the shell, and only the shell:** it is the one tool whose reach no other fence bounds -- the
path fence, the edit scope and the tool list all stop at its argv -- and since M848 a delegate's
file tools are held inside the workspace by the path fence. Asking for every delegate tool would
answer DEFERRED def-286 wholesale, which the operator left open.
**Rejected:** for 1, **propose-only profiles** (both admin profiles read-only, proposing commands for
the main turn) -- safe, but the `sysadmin` profile exists to inspect a live system, inspection needs
the shell too (`systemctl status`, `journalctl`), and jichi cannot tell an inspecting command from a
mutating one; and **documenting it alone** -- the exposure would stay. For 2, **a docs warning
alone** and **`doctor` flagging broad entries** -- both leave an unattended delegate able to run
whatever a prefix admits.
**Keyed on the delegate's posture, not on `agent_depth`.** The option as offered said "agent_depth >
0"; a subtask command also runs at depth 1 and is the user's own turn since M848, so the code asks
"is this a delegate", which is what the decision meant. The difference is observable: keying on
depth strips a subtask command's allowlist (the driver's check 7).
**Implications, for whoever revisits this:**
- *The TUI* asks: it is the one front-end that forwards its callbacks into a delegate. An
  `always` grant given earlier for `run_terminal_command` covers a delegate's calls too (the grant
  is per tool name, as in the main turn). Under `-q` it forwards nothing, so the call is refused.
- *Headless without `--auto`, an editor (ACP), a `spawn_parallel` child* cannot ask, so a
  delegate's shell call is refused there, with a reason that names the delegate. In an editor it is
  refused rather than routed to the editor's permission prompt, which would receive a request for a
  tool call it was never shown. Parallel shell work runs under `--auto`.
- *Under `--auto`* nothing changes: a delegate's shell runs unprompted, as the main turn's does.
- *`privilegedCommands: allow`*, the explicit allow-all, still admits a delegate's privileged
  command: that setting means what it says, and `doctor` already warns about it.
- *The kinetic gate's allowlist* is not part of this: it exists so a safe-state `stop_all` stays
  callable when everything else refuses.
- *An allowlist entry is a word prefix*: `sudo systemctl` admits every `sudo systemctl ...`. The
  docs and both admin packs now say so; the match itself is unchanged (chaining still
  disqualifies it).
- *A profile's `readonly: true` is still not a floor* in the spawn tools (an explicit
  `readonly: false` overrides it, SUBAGENTS.md); this fix does not depend on it, because the shell
  now asks whatever the flag says.
**What it costs:** in chat, a delegate that works through the shell now stops for an approval per
command (or an `always`); a headless or editor workflow that relied on a delegate's shell without
`--auto` now has those calls refused; a delegate that ran an allowlisted `sudo` now has it refused.
**Revisit when** real use says so: prompt fatigue from delegates in chat (a per-delegate grant would
be the next step); editor users who need a delegate to run commands; parallel shell work wanted in
chat; a team whose delegate relied on an allowlisted privileged command.
**Where:** ROADMAP M849, `src/chat/jc_agent.c` (the ASK branch, the privileged gate),
`src/util/jc_auditview.c`, `tests/smoke/delegate_shell_follows_posture.sh`,
`tests/test_auditview.c`

<a id="dec-m851"></a>
#### M851 — A project config applies only in the safe direction until its content is trusted…
Decided by the operator on 2026-10-01 (the branch review's CFG-1), as recommended: **(1)** an
untrusted project config (`local/config.json`, else `.jichi/config.json`, merged by the implicit
discovery) honours only the safe direction -- a narrower posture, a tighter bound, the harmless
keys; everything that widens a permission, starts a program, redirects a request or a
credential, or reads outside the project waits for trust, and a lint holds the classification so
a new key cannot slip through unclassified; **(2)** the TUI asks once, listing exactly what the
file would do; `jichi trust` and `jichi untrust` cover everything else; headless and ACP never
ask -- they run untrusted and say so; **(3)** trust is bound to the file's content, so a
`git pull` that adds an MCP server does not inherit it; **(4)** the scope is the config files --
rules files, commands, agents and skills are a later question (DEFERRED def-298); **(5)** never
silent: one line names the ignored keys, `doctor` lists them, the store lives in `~/.jichi.d/`,
and `--config` / `$JC_CONFIG` are trusted by definition.
**How comparable tools do it** (their own docs, read 2026-10-01): Gemini CLI's trusted folders
are "disabled by default", and its untrusted "safe mode" ignores workspace settings, MCP servers
and custom commands; Codex "loads project-scoped config files only when the project is
trusted". Both trust a FOLDER; this binds trust to a file's CONTENT, which is stricter.
**Rejected:** **per-folder trust** -- a later commit to a trusted folder would inherit it, which
is the case a clone-then-pull produces; **asking in headless and ACP** -- nobody to answer, and a
question on a pipe hangs or is answered by whatever comes next; **trusting `local/config.json`
because it is conventionally git-ignored** -- a repository can commit one, and jichi prefers it;
**filtering only the redirect and the approval keys** (the finding's minimum) -- it leaves
program starts and outside reads open.
**Choices this milestone made inside the rule, each open to review:**
- *The classification* (in `src/config/jc_config_trust.c`, one line of reason per key): 43
  SAFE, 27 NARROW, 30 GATED. Where in doubt a key is GATED -- over-classifying costs the user a
  listed key and one `jichi trust`, under-classifying costs a hole. The judgment calls, each
  GATED or NARROW conservatively: `systemPrompt` (prompt text for every turn and subagent, though
  a rules file can say as much -- def-298), `timeouts` (it includes the dead-stream fence),
  `routing` and `retrieval` (they can route a request to another model), `aliases` (they can
  run a command), `logging` (its `path` writes anywhere), `daemonWorkers`/`daemonWorkerTimeout`
  and `parallelTaskTimeout` (process counts and watchdogs), and the in-project reads
  (`instructions`, `design`, `docs`, `referenceRoots` are GATED even when a path stays inside
  the project -- a containment check per path is a refinement, not this milestone);
  `assignments`, `board`, `configEditable` (it lets the MODEL rewrite the config, so `config set`
  on an untrusted file needs `--config-editable` or trust), `control`, `hooksEnabled`,
  `learnOnStop`, `parallelVerify`, `typeAhead`, `inputHistory` and `voice` apply only as OFF.
- *NARROW keys are applied after the parse*, against the parsed values, because "tighter" depends
  on machine-aware defaults; a key absent from the table fails closed (GATED).
- *SHA-256*, written from FIPS 180-4, because jichi had none and the binding needs a hash a
  hostile commit cannot collide.
- *`jichi setup` trusts a config it CREATES, not one it MERGES into* -- the merged file's keys may
  have arrived with a clone.
- *The user's own `config set` keeps a trusted file trusted* -- binding trust to content is
  against other people's edits, not the user's; on an untrusted file it vouches for nothing.
- *`jichi trust` with nothing to trust answers on stdout and exits 0* -- the state it exists to
  reach (no untrusted project config) already holds.
- *The no-model error names `jichi trust`* when the withheld file was the one naming a model.
**What it costs, stated:** a bare `jichi` run in a project whose config carries a GATED key
changes until the file is trusted -- the TUI asks once, headless and ACP print one line, and
where the project file was the only one naming a model the run has no model until trusted (the
error says so). Editing a trusted file outside jichi makes it untrusted again. `jichi setup`'s
start scripts pass `--config` and are unaffected.
**Revisit when:** re-trusting after one's own edits becomes friction (a narrower binding, or a
trusted-author signal, would be next); a SAFE/NARROW/GATED call turns out wrong in use (the
table is one line per key); the in-project reads deserve a per-path containment check; or the
project assets question (def-298) is taken up.
**Where:** ROADMAP M851, `src/config/jc_config_trust.c`, `src/config/jc_config.c`
(`project_trust_gate`), `src/util/jc_sha256.c`, `src/main.c` (`trust_prompt`, `run_trust`),
`src/config/jc_configedit.c`, `tests/smoke/project_config_trust.sh`,
`tests/smoke/config_trust_lint.sh`, `tests/test_config_trust.c`, `tests/test_sha256.c`

<a id="dec-m852"></a>
#### M852 — The shell fence: a private temp dir, a refusal when it cannot apply, a notice when an entry grants nothing…
Three choices, the last two the operator's (2026-10-01, asked twice -- the first asking carried a wrong
mechanism, corrected before the second).
**1. Where a write child's `$TMPDIR` lives: its worktree's git admin dir** (`<shadow repo>/worktrees/<id>/tmp`).
Rejected: **granting `/tmp`** -- M831's own reason stands: world-writable, and a project can live there;
**a temp dir inside the worktree** -- `git add -A` would commit it and the merge would bring temp files
into the live tree (the perturbation shows it); **a sibling of the worktree** -- a new allow-set entry,
and a recursive remover jichi does not have, which must never follow a symlink a child planted; git's
own `worktree remove` already deletes the admin dir that way.
**2. Case A, Landlock present but the fence cannot be applied: the write task is refused** (M831 ran it
unfenced). The M831 proposal's §3.3 had planned this ("a fence that half applied is not run"); M831's
fallback was recorded as behaviour ("unfenced but never silently") without the rejected alternative.
Rejected: **keep running unfenced** -- work never stops, but a fence that is supposed to be on can be off
with only a log line saying so. A kernel without Landlock still runs unfenced with a WARN, or no write
task could run there; `shellFence: false` is the explicit opt-out.
**3. Case B, an entry that is not absolute or does not exist: skip it, and say so once at the top of the
batch result.** The fence holds either way -- `add_beneath` grants nothing for a path that will not open
(measured) -- so the gap was silence, not safety. Rejected: **refuse the write tasks** -- it stops all
write work over one cache directory the fence already keeps closed.
**What it costs:** a write child's temp files now go to the shadow repo's directory for the batch's
lifetime (the disk guard counts `~/.jichi.d`); a setup that listed a file in `shellFenceAllow` now has its
write tasks refused, where it ran them unfenced before.
**Where:** ROADMAP M852, `src/tools/jc_tool_parallel.c` (`worktree_tmpdir`, `apply_shell_fence`, the
batch's `fence_note`), `src/chat/jc_agent.c` (the hint), `src/util/jc_landlock.c`,
`tests/smoke/shell_fence_temp_and_setup.sh`, `tests/test_landlock.c`

<a id="dec-m853"></a>
#### M853 — A delegate's `finish` call ends its turn; the top level and a call with no message get an error…
**Decision.** In a delegate (depth >= 1), a call to an unregistered tool named `finish`, `done`, `complete`,
`submit`, `task_complete` or `final_answer` that carries a non-blank `message` string ends the turn, and the
message is the answer. Anywhere else such a call gets an error that says how a turn ends.
Rejected: **a better error only, everywhere** -- the drive's workers were refused five times in a row and
looped, the answer is already in the call, and whether a better wording would stop the loop is not
measured; **the whole arguments as the answer when there is no `message`** (the handoff's fallback) --
`complete {"id": 3}` would end a turn with `{"id": 3}` as its answer, while the error costs one model
call and loses nothing; **ending the turn at the top level too** -- a top-level answer passes the
self-review and the envelope's verify gate, and an ending by tool call would go around both;
**registering a real `finish` tool** -- every request would carry one more schema, and the way every
provider ends a turn is a reply with no tool call, while the alias costs nothing until a model calls it;
**more names** (`attempt_completion`, say) -- unmeasured, and each name added is one more that can end a
turn by mistake.
**What it costs:** a delegate that calls one of the six names with a `message` by mistake ends its turn
early, with that message as its answer; a write child's work is then verified (with `parallelVerify`)
and merged, as after any answer. Only `finish` is measured; the other five names are the handoff's.
**Where:** ROADMAP M853, `src/chat/jc_agent.c` (the intercept, ahead of the fences, and the end of the
round), `src/tools/jc_tool.c` (`jc_tool_is_finish_alias`, `jc_tool_finish_alias_message`, the error),
`tests/smoke/delegate_finish_alias.sh`, `tests/test_tool.c`

<a id="dec-m854"></a>
#### M854 — A write child that did not end on its own answers with a note; it merges only after a green verify…
**Decision** (the operator's go of 2026-10-01). A write child stopped from outside (SIGTERM), ended by a
context-window 400, or capped (`maxSubagentIters`) answers with its last text and one line saying how it
ended; its work merges only after a green worktree verify, and with no verify it is kept as
`parallel-unverified` and named. The PARENT decides, from facts the child reports (`last`, `term`,
`overflow`, its stop name).
Rejected: **merge such a child like any answered one** -- its work stopped mid-flight by definition, and
a merge without a verify would put half-done work into the live tree; **keep it unmerged as before** --
N33's worker had committed finished work, and the lead recovered it by hand 43 minutes later;
**let the child decide** -- a child cannot tell a supervisor's SIGTERM from the batch's own, and only
the parent knows whether the batch was aborted; **any HTTP 400 as an overflow** -- a malformed
request is a 400 too (M166), so the error body must say context; **a new command to apply a kept
state** -- `jichi attempts show` and `jichi recover` exist, and the Kept list names them;
**verify such a child even with `parallelVerify` off** -- the verifier's cost was opted out of, and a
project whose tests cannot run in a worktree would see every such child quarantined.
**What it costs:** a capped child that had written text merged unverified before; with no verify it is
now kept, a changed behaviour on master's default. A timed-out child is not covered (def-300).
**Where:** ROADMAP M854, `src/tools/jc_tool_parallel.c` (the classification, the merge gate, the end
record), `src/tools/jc_parallel.c` (`jc_parallel_ending`, the done line's fields), `src/net/jc_http.c`
(`jc_http_body_ctx_overflow`), `src/chat/jc_agent.c`, `tests/smoke/parallel_abnormal_endings.sh`

<a id="dec-m855"></a>
#### M855 — A parallel batch keeps and empties the shared stash stack; a child that stashes is told to commit…
**Decision.** Linked worktrees share one `refs/stash` (measured, git 2.43), so before a batch with write
tasks starts and after it ends, every entry on the shadow repo's stack is pinned as a kept ref and the
stack is emptied; the batch result names the entries; a write child that runs `git stash` is told the
stash is shared and not kept. With preservation off the stack is left alone.
Rejected: **name the entries in each child's end record** (the go's wording) -- an entry cannot be traced
to one child, so the batch result names them; **refuse `git stash` in a write child** -- models use it
to test the base (run 0070), and the keep plus the hint cover the loss; **a repository per child** (a
clone with shared objects) to give each its own stash -- a redesign of M803's preservation and of the
merge, not this milestone (def-301); **empty the stack without pinning** -- deletes the only reference
to a stashed change; **empty it with preservation off too** -- the same loss, by the operator's choice
not to keep anything.
**What it costs:** a later batch's child can no longer `git stash pop` an earlier batch's entry (the
point); two children of one batch still can pop each other's entries (def-301).
**Where:** ROADMAP M855, `src/snapshot/jc_snapshot.c` (`jc_snapshot_stash_keep`),
`src/tools/jc_tool_parallel.c` (the two calls, the result's block), `src/chat/jc_agent.c` (the hint),
`src/tools/jc_tool.c` (`jc_cmd_runs_git_stash`), `tests/smoke/parallel_stash_kept.sh`

<a id="dec-m856"></a>
#### M856 — A resume replays the predecessor's range from the base its end record names…
**Decision.** A `from:` resume cherry-picks `<base>..<ref>` with `--no-commit`, the base read from the
kept state's end record and checked to be an ancestor; `attempts show` diffs against the same base.
Rejected: **one combined diff, `git diff base ref | git apply --3way`** -- replaying the commits keeps
each one's own 3-way merge and the existing `cherry-pick --abort` path; **the base as the merge-base of
the kept commit and the current base** -- the end record already names the exact base, and a merge-base
computed against a later checkpoint is a guess; **squash a child's commits into the kept commit**
(parent = the base) when preserving -- the resume and `show` would work unchanged, but the child's own
commits would become unreachable and its history, part of its record, would be lost.
**What it costs:** a state kept before M808, with no `base:` line, is still resumed with the old single
pick; a merge commit in the range stops the pick, which then reports a conflict.
**Where:** ROADMAP M856, `src/snapshot/jc_snapshot.c` (`kept_base`, `jc_snapshot_resume_apply`,
`jc_snapshot_discarded_show`, `jc_snapshot_kept_changes`), `src/tools/jc_tool_parallel.c`
(`build_resume_note`), `tests/smoke/from_resume_commits.sh`

<a id="dec-m857"></a>
#### M857 — A fenced write child's tool-output store is created before the fence and granted, alone…
**Decision.** `apply_shell_fence` creates `$HOME/.jichi.d/tool-output/<pid>` and grants that directory
only; the store keeps its place.
Rejected: **move a fenced child's store into its worktree's admin dir**, beside M852's `$TMPDIR` (no
new grant) -- the path the model is given and the path fence's read exemption (M339) would both have to
follow it, and the tickets would vanish with the worktree; **grant `tool-output/`** -- every other
process's store would open to this child's shell; **fence only the commands the child starts**, not
the child -- a process applies Landlock to itself, and M832's `posix_spawn` backend leaves no point
between fork and exec to do it; it would redesign M831, not fix N41.
**What it costs:** the child's shell can write its own store too; it is jichi's per-process scratch, and
nothing reads it as instructions.
**Where:** ROADMAP M857, `src/tools/jc_tool_parallel.c` (`apply_shell_fence`),
`tests/smoke/shell_fence_tool_output.sh`

<a id="dec-m858"></a>
#### M858 — A reply is cut when its completion_tokens reach max_tokens; a cut final reply is M854's fourth ending…
**Decision.** The OpenAI provider marks a reply cut when `finish_reason` is `length` OR `completion_tokens`
reached the `max_tokens` the request sent; `model_call` records it; a write child whose turn ended on a
cut reply answers with a note naming the cut, and merges only after a green worktree verify.
Rejected: **trust `finish_reason` alone** (M334) -- measured, the institution's gateway reports a cut
tool call as `tool_calls`; **raise the default `maxTokens`** -- the measured cuts were runaway replies
(16 KB arguments, 132 s and 321 s endings), and a higher ceiling lets the same runaway run longer;
**mark a reply cut only above `max_tokens`** -- the measured cut is exactly equal to it; **treat a cut
reply as a failure** (no answer) -- its text is the worker's last account of its state, and the note
keeps it while saying it is not an answer.
**What it costs:** a reply that ends naturally at exactly `max_tokens` is called cut -- improbable, and
the cost is a note and a verify-gated merge; a write child that ends on a cut reply is kept, not merged,
when no verifier runs (changed behaviour). The one continuation (N42's (d)) is the next milestone.
**Where:** ROADMAP M858, `src/provider/jc_provider_openai.c` (`oa_mark_cut_by_usage`),
`src/chat/jc_agent.c` (`model_call`'s `truncated`), `src/tools/jc_parallel.c` (`jc_parallel_ending`),
`src/tools/jc_tool_parallel.c`, `src/util/jc_delegreport.c`, `tests/smoke/cut_reply_ending.sh`

<a id="dec-m859"></a>
#### M859 — A cut reply is continued once: in a delegate by default, at a headless top level when configured…
**Decision** (the operator, 2026-10-02: "Go with 1, but consider an option for configuration that allows
continue in headless mode, too"). A reply cut at the output ceiling with no tool call gets ONE "continue,
and keep each reply short" turn: in a delegate unless `continueCutReply` is `"off"`, at a headless top
level only with `"headless"`. A second cut ends the turn.
Rejected: **never continue** -- run 0090's worker would still stop mid-sentence after 331 calls, with
nobody to say "continue"; **continue everywhere** -- in chat the person can say it, and an automatic
extra call there is one they did not ask for; **continue until the reply is whole** -- a runaway reply
(N42: 16 KB arguments, 132 s and 321 s endings) would run again and again; **raise `maxTokens`** -- the
same runaway, longer; **a SAFE trust class** -- the key can add a model call per turn, so an untrusted
project config must not set it (GATED).
**What it costs:** one more model call per cut turn, which is the point; a model that continues by
repeating itself is cut again and ends as M858's ending.
**Where:** ROADMAP M859, `src/chat/jc_agent.c` (`cut_continue_allowed`, the continuation),
`src/config/jc_config.c` (`continueCutReply`), `src/config/jc_config_trust.c`, `src/main.c`
(`headless_run`), `tests/smoke/cut_reply_continue.sh`

<a id="dec-m860"></a>
#### M860 — A parallel batch's id is <base>-b<k>; only a batch that holds its base's lock removes it…
**Decision** (the branch review's CTL-5 fix, with N26). A batch's id is `<base>-b<k>` (the session id, or
inside a child that child's own id, `<batch>-t<task>`, which the child keeps itself), its children's `seq`
bands are numbered on across the process's batches, and a batch removes its base's lock and manifest only
when it held the lock.
Rejected: **refuse a nested batch** -- `spawn_parallel` is not advertised to delegates, and forbidding it
is a policy decision the finding did not make; with distinct ids a nested batch harms nothing it does
not own; **a fresh UUID per batch** -- the batch would no longer read as part of its session;
**bands of `batch * N + task`** -- a 32-bit `long` overflows within about 130 batches; **keep the
session id and only fix the teardown** -- `task_run` and `seq` would still collide (N26), and the
nested batch would still overwrite the lead's manifest; **read the child's id back from its telemetry
`task_run`** -- the first gate's build did, and with telemetry off (`--log -`) the nested base fell back
to the envelope's run id, the lead's own, so the nested batch took the id of the lead's next batch
(found before landing; check 6).
**What it costs:** `task_run` and the transcripts' directory change format; older logs are split by
where a child's `seq` restarts. A nested batch's bands can overlap a later top-level batch's, since a
forked child counts in its own copy; its `task_run` still tells them apart.
**Where:** ROADMAP M860, `src/tools/jc_tool_parallel.c` (the id, the bands, the teardown),
`src/util/jc_eventlog.c` (`jc_eventlog_set_task_run`), `tests/smoke/parallel_batch_ids.sh`

<a id="dec-m861"></a>
#### M861 — A parallel task's max_tool_calls binds, is announced once at four-fifths, and ends the task…
**Decision** (the demos drive's N28; the operator's order of 2026-10-02). A `spawn_parallel` task takes
`max_tool_calls`, read leniently (a number or a string of digits; any other value refuses the task). It
binds in the child whether or not the run has an envelope, bounded by the task's share of the run's cap.
The child is told once, at four-fifths of the budget it has, and at the budget it ends, keeping its work.
That is one of M854's endings: it merges only after a green worktree verify. So is a stop on the run's
share, which M854 had left with the normal stops, and its report no longer says the run's budget is spent
when the run has budget left.
Rejected: **the budget in the task text** -- the drive's workers went to 189 and 302 calls under it; a
worker cannot see its count; **a strict read** (a JSON integer only) -- a model sends `"150"` as often as
`150`, and refusing that would cost a task for a quoting habit; **ignore an unreadable value** -- the lead
would believe the budget binds (N17: a strictly read `write` silently ran tasks read-only); **ride on the
envelope** (`--max-tool-calls`) -- a run without one would have no per-task budget, and the slice is the
run's share, not the task's; **a note per round, or a countdown** -- M347's lesson (a nag a model learns
to skip); **a note for a task bounded only by the run's share** -- M347 rejected the run's notice in
delegate histories, and that reason stands; this note exists because the lead set this task's budget;
**merge a budget stop as before** -- its text is not an answer the child chose to give, the defect M854
removed for the iteration cap; **count only successful calls** -- the envelope counts attempts, and a
child repeating a refused call is the case a budget is for.
**What it costs:** a long task needs a key large enough for it; a run that relied on a
budget-stopped child merging unverified now needs `parallelVerify` and a verifier, or recovers the kept
ref.
**Where:** ROADMAP M861, `src/tools/jc_tool_parallel.c` (the key, the child's budget, the note's
parenthetical), `src/tools/jc_parallel.c` (`jc_parallel_parse_max_calls`, `jc_parallel_budget_note`,
`jc_parallel_ending`), `src/chat/jc_agent.c` (`task_budget_should_stop`, `task_budget_note`),
`src/util/jc_delegreport.c` (`task_budget`), `tests/smoke/parallel_task_budget.sh`

<a id="dec-m862"></a>
#### M862 — A red worktree verify shows the lines that look like failures first, then the tail…
**Decision** (the demos drive's N43, its (a) and (c); the operator's order of 2026-10-02). The excerpt of a
red worktree verify lists, before the 500-byte tail, the lines from before the tail that carry a failure
marker -- at most 12, each cut at 200 bytes, with a count of the rest -- the tail starts at a line's start
when one is within 200 bytes, and the whole output is kept as a claim ticket in the lead's store, named in
the result.
Rejected: **a longer tail** -- zig's failures come before its summary, and a long summary pushes them out
of any tail; **a head and a tail** (N43 (b), `jc_toolout_split`) -- a build's head is its compile lines,
and the failures can sit anywhere in between; **structured fields per test** (N43 (d)) -- a parser per
runner; **`jc_testparse`'s records** -- they are parsed failures, not the runner's lines, and its generic
markers miss zig's "expected 47, found 40"; **the whole output in the worktree** -- the merge stages
everything there (`git add -A`), so it would merge into the tree; **every marked line, unbounded** --
a noisy log would flood the result the excerpt exists to keep short.
**What it costs:** two marker lists (this one and `jc_testparse`'s); a line with a marker that is not a
failure ("test_fail_fast ... ok") can take one of the 12 places; the ticket lives as long as the lead's
process.
**Where:** ROADMAP M862, `src/tools/jc_parallel.c` (`jc_parallel_verify_excerpt`),
`src/tools/jc_tool_parallel.c` (the gate's excerpt and the ticket), `tests/smoke/parallel_verify_excerpt.sh`

<a id="dec-m863"></a>
#### M863 — A write child's discarding git command runs only after jichi has kept its worktree…
**Decision** (the demos drive's N44, its (a); the operator's order of 2026-10-02). Before a write child's
shell runs `checkout`, `switch`, `restore`, `reset`, `clean` or `rebase` (harmless forms excepted), jichi
pins the worktree's files, tracked and untracked, on its HEAD as a ref under
`refs/jichi/discarded/pre-discard/`, without touching HEAD, the index or the files, and names the ref in
the tool result and in the task's part of the batch result.
Rejected: **refuse the command** (N44 (b)) -- the operator chose keeping, and a refusal costs a worker
the compare it was after; **ask in the task text** -- the drive's texts forbade it, and four workers did it
anyway; **commit in the worktree first** (the batch's own keep, `jc_snapshot_preserve`) -- the command
would then act on the committed state, so `git checkout -- f` would discard nothing; **`git stash
create`** -- it leaves out untracked files, which `clean` deletes; **pin on every shell call** -- noise
and cost for the calls that discard nothing; **parse the command** -- words decide only whether to keep
first, and a false positive costs one ref.
**What it costs:** a ref and a note for each kept state; a command that merely mentions the words ("echo
git checkout") counts; a pin can fail, and then the command runs with only a note.
**Where:** ROADMAP M863, `src/snapshot/jc_snapshot.c` (`jc_snapshot_worktree_pin`), `src/tools/jc_tool.c`
(`jc_cmd_git_discards`), `src/tools/jc_tool_parallel.c` (`jc_parallel_pre_discard`, the done line, the
batch result), `src/chat/jc_agent.c` (before the call, the note after), `tests/smoke/parallel_pre_discard.sh`

<a id="dec-m864"></a>
#### M864 — A parallel child's log lines begin with the UTC time and its task_run; every line is one write…
**Decision** (the demos drive's §11 (b); the operator's order of 2026-10-02). In a spawn_parallel child,
each `jc_logf` line begins with the UTC time and the child's id, its `task_run`; every log line, in any
process, is formatted first and written with one call.
Rejected: **a new `agent` UUID** (§11 (a)) -- `task_run` is unique, already on every telemetry row, and
names its parent as a prefix; a second id would name the same agent twice; **a per-agent log file** --
the supervisor reads one stderr, and a file per child is another place to look; **a time on the lead's
lines too** -- every run's stderr would change for every user and every driver that anchors on
"[jichi"; **the id after the tag** ("[jichi warn] [id] ...") -- drivers and readers match the tag and
the text together; **a fixed buffer for every line** -- a debug dump of a request body would be cut.
**What it costs:** a child's lines are longer; a delegate inside a child shares the child's id.
**Where:** ROADMAP M864, `src/util/jc_log.c` (`jc_log_set_prefix`, the one-write line),
`src/tools/jc_tool_parallel.c` (`run_child`), `tests/smoke/parallel_child_stderr.sh`

<a id="dec-m865"></a>
#### M865 — A write child that changed an existing test assertion is kept, not merged, even after a green verify…
**Decision** (the operator, 2026-10-02: "Hold the merge"; the demos drive's §9). Before a write child's
changes merge, the parent diffs each test-looking file the task modified against the base; a hunk that
changes an existing assertion (M88's predicate, per hunk) keeps the work as `parallel-test-edit` instead
of merging it, and the note names the files.
Rejected: **a note only, merging as today** -- the moved pin would be in the tree before anyone read the
note; **leave it to the lead's census** -- the drive's lead did census card 215, but a lead that does
not would merge 47 -> 43; **refuse the edit in the child** -- fixing a genuinely wrong test is
legitimate, and the task may allow it; **the tool-level test_edit event as the signal** -- it fires only
for jichi's own edit tools with an envelope armed, and misses a shell rewrite; **compare a declared
metric with the base's run** (the proposal's (b)) -- a contract a project has to declare, beyond this
decision.
**What it costs:** friction on honest pin updates (the lead takes them from the Kept list); a test file
whose assertion lines look like other code may be held for a non-assertion change, and a move without
a marker on both sides is missed (M88's heuristic).
**Where:** ROADMAP M865, `src/tools/jc_parallel.c` (`jc_parallel_diff_edits_assertion`),
`src/tools/jc_tool_parallel.c` (`assertion_hold`, the merge), `src/snapshot/jc_snapshot.c`
(`jc_snapshot_worktree_diff`), `tests/smoke/parallel_test_edit_hold.sh`

<a id="dec-m869"></a>
#### M869 — A child's refused-call loop ends it at nine, copied placeholders at three, and every loop stop is named as a loop…
**Decision** (the demos drive's N27; DEFERRED def-305). In a child, the same refused call (the exact key) at
M829's threshold ends it as M829 ends a success loop; three copied placeholders in a row end it sooner; the
done line says which loop, and the ending and the delegate report name it.
Rejected: **warn only, as M432 did** -- run 0015's worker was warned through 742 refusals; **a lower
threshold for every refused call** -- a refusal a model repairs by rephrasing is a slip, and M829's nine
was fitted on drives; **nine for placeholders too** -- a placeholder cannot be repaired at all, and nine
of them cost a minute of a lead's batch; **stop the top level** -- it has more turns and an operator, as
M829 decided; **a new stop name on the wire** (`looped`) -- the parser maps an unknown name to `done`, so
an older parent would read a loop as an answer; `max_iters` with a `looped` field degrades to the old
naming instead; **change the placeholder text** -- M289 made it directive, and a copied command start
would run a truncated command.
**What it costs:** a child that retries the same failing command nine times with no change in between
is ended; its work is kept, and its end record says why. **Amended before landing:** the first build
counted across edits (M432's per-turn counts), which would have ended a child fixing and re-testing;
the stop's own count starts again at an edit or a successful shell command.
**Where:** ROADMAP M869, `src/chat/jc_agent.c` (the two stops, `loop_stop`), `src/tools/jc_parallel.c`
(`jc_parallel_loop_ending`), `src/util/jc_delegreport.c`, `tests/smoke/parallel_loop_stops.sh`

<a id="dec-m870"></a>
#### M870 — A child whose last 24 calls bring nothing new is stopped: a novelty window beside M829's counts…
**Decision** (the demos drive's N21). A ring of the last 24 call triples; 24 stale calls in a row (each
already among the 24 before it) end a child as a loop stop; an edit clears the window.
Rejected: **drop M829's shell rule** -- it is right for one command (a shell command may change what the
next call sees), and dropping it would stop children whose commands do change things; **count distinct
commands** -- run 0012's rotation had 4 commands and 8 command-output pairs in its last 60 calls, and a
count does not see order; **a smaller window** -- a test, a log, a status and a diff are a normal
four-step check, and the window must hold several rounds of it before calling it a loop; **the top
level** -- it has more turns and an operator, as M829 decided.
**What it costs:** a child that legitimately re-runs a fixed set of checks 24 times in a row with
identical results is ended -- which is the case the window exists for.
**Where:** ROADMAP M870, `src/util/jc_toolloop.c` (`jc_noprogress_note`, `jc_noprogress_reset`),
`src/chat/jc_agent.c`, `tests/smoke/parallel_loop_stops.sh`

<a id="dec-m871"></a>
#### M871 — The C++ build is gated in make ci: built, linked and tested with g++…
**Decision** (the operator, 2026-10-02; DEFERRED def-307). `make ci` builds and links jichi and the unit
suite with `CC=g++`, runs the suite, and runs `cpp-check`. It supersedes M188's "not in `make ci`"
(docs/CPP_BUILD.md, the Makefile's `cpp-check` comment).
Rejected: **keep it out, as M188 and M636 decided** -- it rotted twice in ten days, each time because
nothing ran it; **`cpp-check` alone** -- it compiles but never links, and M690's break was a linkage
mismatch; **a smoke lint instead** -- cheaper per milestone, but the gate is where a batch is held
before master moves; **clang++ as well** -- this machine's clang++ links only with `--gcc-install-dir`,
a machine fact the gate should not depend on, and g++ and clang++ broke together both times; **WERROR**
-- the `-Wwrite-strings` residue is real C, left on purpose.
**What it costs:** about 40 s per `make ci`.
**Where:** ROADMAP M871, `Makefile` (`ci`), `docs/CPP_BUILD.md`, `tests/smoke/cppcheck_lint.sh`

<a id="dec-m872"></a>
#### M872 — A cue the request quotes or asks about infers nothing; an inferred rule carries its sentence…
**Decision** (the demos drive's N45 (a) and (b), as the reply's §12 recorded them). A negation cue inside a
quoted span -- a fenced block, an inline code span, a closed pair of double, single or typographic quotes
within a paragraph -- or in a sentence that ends in `?` is mentioned, not said, and infers nothing. Each
inferred rule keeps the sentence it was read from (`source`, at most 200 bytes), and every place that
names the rule quotes it.
Rejected: **blank quoted text out of the scan** -- a target quoted after an unquoted cue is how a prompt
names a tool ("never invoke `make`", M743), and blanking it loses the rule; **double quotes only, the
form the drive met** -- single, typographic and code quotes quote the same way, and on the corpus a
backticked error message (`tool disabled in read-only mode`) inferred read-only; **an unclosed quote
quotes to the end** -- an inch mark or a stray quote would silence every order after it; **the line
instead of the sentence** -- M433's line number stays in `brief-check`, but a line holds several
sentences and a sentence several lines, and the notice must show the words that were read; **one
bullet per sentence in the system prompt** -- one rule per bullet, each with its sentence, is what the
model weighs, and a repeated sentence costs a few tokens.
**What it costs:** a rule the request quotes because it means it, and an order put as a polite question,
infer nothing; the request still says both. A rule that loses its sentence's object ("Don't commit the
build output.") still does: the quote shows the object, it does not narrow the rule.
**Where:** ROADMAP M872, `src/chat/jc_constraint.c` (`quote_mask`, `mentioned_not_said`,
`source_sentence`, `jc_constraint_join_sourced`), `src/chat/jc_agent.c`, `tests/smoke/constraint_quoted.sh`

<a id="dec-m873"></a>
#### M873 — An inferred rule is matched against what a command runs; an authored rule against its words…
**Decision** (the demos drive's N40). For a rule jichi inferred, a shell command goes against it when a
segment RUNS one of the rule's words: its command word after assignments and wrappers, the action of a
program that takes one (the first word after its options, and the word after a `run`, `build` or
`exec`), or a target runner's target. A rule the operator wrote keeps `jc_constraint_cmd_hits`.
Rejected: **narrow the authored match too** -- a fence that errs toward refusing must not shrink to
what a parser recognizes (`ssh host 'pip install x'`, `pip3.11 install`, an installer no table knows);
**narrow `install` only, the key the drive met** -- `cat build.zig` went against "do not run build
commands" and `cat tests/x_test.zig` against "do not run tests" by the same mechanism; **a list of
reading commands to exempt (grep, cat, sed ...)** -- a reader not on it would still be accused, and for
a guess the safe direction is silence; **the command word only** -- the commonest forms, `pip install`
and `git commit`, are the action of a program; **strip quotes as a shell does** -- a quoted word in
action position is rare, and reading quoted text as data keeps one rule with M872.
**What it costs:** an advisory misses what the tables do not know -- `npm i`, `yarn add`,
`cmake --install`, a quoted command word -- and the rule is still in the system prompt.
**Where:** ROADMAP M873, `src/chat/jc_constraint.c` (`jc_constraint_cmd_runs`, `runs_line`,
`runs_segment`, `jc_constraint_blocks_ex`), `tests/smoke/constraint_runs.sh`

<a id="dec-m874"></a>
#### M874 — constraintsAutoAdopt turns inference off; a project config may only leave it on…
**Decision** (the operator, 2026-10-02: "Add it, narrow-only"; the demos drive's N45 (c)). A config key,
default on, that turns off reading rules out of an `--auto` request. NARROW with on as the strict side:
`--config`, `$JC_CONFIG` and the user's own config apply it; an untrusted project config may leave it on
but not turn it off. It governs inference only: authored rules load and bind either way.
Rejected: **SAFE, any config may set it** -- a cloned repository could switch off advice read from the
operator's own words; **no key, (a) and (b) only** -- a harness that assembles its prompt from several
documents cannot know every phrase those documents will carry; **a CLI flag** -- the drive passes
`--config`, and a one-off run has `--config-json`; **turning `constraints_on` off too** -- that would
silence the rules the operator wrote, which is not what was asked.
**What it costs:** with it off, a real order in the request ("never push") is not restated to the model
or noted on a call against it; the request still says it, and a binding rule belongs in
`.jichi/constraints.md`.
**Where:** ROADMAP M874, `src/config/jc_config.c`, `src/config/jc_config_trust.c`, `src/main.c`,
`tests/smoke/constraints_auto_adopt.sh`

<a id="dec-m875"></a>
#### M875 — A metric gate beside the test-edit hold: a moved pin merges when the base's measurement backs it…
**Decision** (the operator, 2026-10-02: "Go with 1., and build (b) the metric gate, as well, so either
option is configurable"; the demos drive's §9 (b) and N46). `parallelTestEdits`: `"hold"` (the
default, M865) or `"metric"`. In `"metric"` mode a child whose moved assertions are all in the declared
`parallelMetric.pins` merges when `parallelMetric.command`, measured in its worktree, is not worse than
the base's run, measured once per batch in a fresh worktree of the base; otherwise it is kept. An
unknown measurement holds; an assertion outside the pins is held. Both keys GATED.
Rejected (the design's D2-D10, each with its reason): **compare the old pin's number with the new one**
-- free, but it trusts that the base's verify was green and reads numbers out of language-specific
assertion text; **any moved assertion may merge when the metric holds** -- the metric says nothing about
a test it does not measure; **fall back to the verify on an unknown measurement** -- that is the hole;
**measure the live tree** -- before the batch costs every batch, at merge time it is no longer the base;
**a task field declared by the lead** -- it would have merged card 215's 47-to-43; **measure every
child** -- a different feature (DEFERRED def-310); **a regular expression for the number** -- a new
portability surface for one field.
**What it costs:** a base run per batch that needs one, from a cold worktree, and a run per pin-moving
child. And it closes "type the number", not "change what is measured": the metric runs the
worktree's own code (def-311).
**Where:** ROADMAP M875, [proposals/2026-10-metric-gate.md](proposals/2026-10-metric-gate.md),
`src/tools/jc_tool_parallel.c` (`metric_gate`), `src/tools/jc_parallel.c` (the pure parts),
`tests/smoke/parallel_metric_gate.sh`

<a id="dec-m876"></a>
#### M876 — A delegate's report names its run's own model failure first; its last failing call is an earlier event…
**Decision** (the operator, 2026-10-02: "Now, in batch 6"; the demos drive's N47 (a)-(c)). For a stop
of `error` whose model calls failed, the report opens with that failure (the model call's diagnosis,
its attempts, the first and last times), names the last failing tool call as an earlier event with no
remedy, and says "kept hitting" only when that call's class failed more than once.
Rejected: **drop the last failing call when the model failed** -- it is still true and still useful
(run 0094's worker DID waste time on `timeout N` runs; the lead's remedy for that was right); **infer
the model failure in the parent from telemetry** -- the child holds it, and its done line is the channel
the parent already reads; **keep "kept hitting" and add the count** -- the phrase asserts repetition, so
a single hit must not carry it; **name the model failure for every stop** -- a run that recovered or
ended for another reason did not end on it.
**What it costs:** four done-line fields and a per-class count per run; the error text is capped at 200
bytes.
**Where:** ROADMAP M876, `src/chat/jc_agent.c` (`stream_once`), `src/util/jc_delegreport.c`,
`src/tools/jc_tool_parallel.c`, `src/tools/jc_tool_subagent.c`, `tests/smoke/delegate_model_failure.sh`

<a id="dec-m877"></a>
#### M877 — A TLS handshake in progress is still connecting; connect also fires on a slow handshake…
**Decision** (the operator, 2026-10-02: "Now, in batch 6"; the demos drive's N47 (d), (e)). A failed
`https` transfer whose TCP connected and whose TLS handshake did not finish is described and classified
as not connected: a connect failure, retried as one, never a stall. The doctrine is corrected:
`connect` fires when nothing listens or a handshake is slow, and is raised for a shared gateway.
Rejected: **only reword the message** -- the classification decided the retry, the stall warning, M23a's
escalation and the footer, and every one of them blamed the model; **a separate `timeouts.tls`** --
libcurl has no separate handshake bound, and two knobs for one connect phase invite a wrong setting;
**reuse the handle after a handshake failure** -- the socket is mid-TLS-record, which M227 drops;
**rewrite the M-L2 ROADMAP history** -- a record is corrected forward, in CLAUDE.md and this entry.
**What it costs:** none at run time (two getinfo calls on a failure); a message a driver may pin
changes for this case.
**Where:** ROADMAP M877, `src/net/jc_http.c` (`jc_http_in_handshake`, `jc_http_describe_failure`),
CLAUDE.md, `docs/TESTING_RUNBOOK.md`, `tests/smoke/tls_handshake_timeout.sh`

<a id="dec-m878"></a>
#### M878 — A read-only phrase is an order only when its clause speaks to the reader…
**Decision** (the operator, 2026-10-02: "Now, as M878 in batch 6"; the demos drive's N45, continued).
Where a cue has made "read-only" an instruction, the clause holding it decides. An order starts with a
verb whose doer is in the mode, the reader, the reader's own scope, a preposition or the phrase; a verb
that puts its object in the mode needs that object to be the reader's own; "make sure" and its kind
pass the decision to the clause they take; an aside is judged by the clause it interrupts; a role noun
after the phrase makes it an adjective. Any other start is a statement, and so is a clause that cannot
be told.
Rejected: **"read-only" never infers** -- it is how a read-only task is most often asked ("analyze it
in read-only mode", the session corpus's one); **a list of third-party subjects** -- a statement's
subject is any noun, while the reader's forms are few, so the closed list is the reader's; **the
sentence's subject decides** -- "When the child runs, keep it read-only" starts with the child and is
an order; **leave it to `constraintsAutoAdopt: false`** (M874) -- that turns off every inferred rule,
for a harness, and this is the default reading; **ask a model** -- `constraints scan` and
`brief-check` predict offline what a run adopts, which a model's reading would not.
**What it costs:** an order whose clause starts with a word the lists do not know reads as a statement
("Rules -- read-only, no commits."); the shapes still read as orders are DEFERRED def-312. A missed
order costs a restatement, and an inferred rule only advises (M734).
**Where:** ROADMAP M878, `src/chat/jc_constraint.c` (`read_only_said_of_other`, `ro_clause_start`,
`ro_judge`, `ro_object_is_reader`), `docs/CONSTRAINTS.md`, `tests/smoke/constraint_quoted.sh`

<a id="dec-m879"></a>
#### M879 — A floor counts the pages that ship, so a checkout and a shipped tree read the same…
**Decision** (found by the Pi 400's dress rehearsal, 2026-10-02). `docs_placeholder_lint` counts the
Markdown pages outside `docs/internal/`, the one export-ignored directory, and its floor is that count.
Rejected: **lower the floor and keep counting everything** -- the checkout would hold 71 more pages than
any shipped tree, so a floor set from it again (M872's mistake) would pass `make ci` and fail every board;
**ask git which paths are export-ignored** -- a shipped tree is not a repository, and an on-target tree
often has no git (M451); **skip the check outside a checkout** -- a check that silently skips on the
machines that matter is worse than a narrower one that always runs (M451).
**What it costs:** a second export-ignored directory would have to be added to the prune; the lint names
the one there is.
**Where:** ROADMAP M879, `tests/smoke/docs_placeholder_lint.sh`

<a id="dec-m880"></a>
#### M880 — Whether TCP was up is asked of the transfer's sockets as libcurl closes them…
**Decision** (found by the Pi 400's dress rehearsal, 2026-10-02). jichi records the sockets a transfer
opens (CURLOPT_SOCKOPTFUNCTION) and asks each, as libcurl closes it (CURLOPT_CLOSESOCKETFUNCTION, 7.21.7),
whether it had a peer. Once the watch saw the transfer's sockets close, its answer decides; otherwise the
connect time does. Rejected, each measured on 8.14.1 and 8.18.0: **sample CURLINFO_CONNECT_TIME during
the transfer** -- 0 throughout; **CURLINFO_LOCAL_PORT or CURLINFO_PRIMARY_IP** -- unset after the
failure; **the TLS bytes the debug callback sees (CURLINFO_SSL_DATA_OUT)** -- seen on all three, but
only with CURLOPT_VERBOSE, which formats every event of every transfer and routes every byte through a
callback; **curl's error text** -- "Connection timed out after N milliseconds" names neither TCP nor
TLS. And **the watch as a fallback only** -- this workstation's 8.5.0 would then never exercise it, and a
broken watch would pass every gate here and fail only on a newer libcurl.
**What it costs:** one `getpeername` per socket close. A libcurl before 7.21.7 keeps M877's reading,
and so does a failure on a reused connection, whose socket this transfer did not open.
**Where:** ROADMAP M880, `src/net/jc_http.c` (`jc_sockwatch_*`, `closesocket_watch`, `sockwatch_detach`),
`include/jc_http.h`, `tests/smoke/tls_handshake_timeout.sh`, `tests/smoke/portability_lint.sh`

<a id="dec-m881"></a>
#### M881 — A test that drives the wizard stays off the network and waits for its prompts; the rig reads doctor's summary…
**Decision** (found by the Pi 400's dress rehearsal, 2026-10-02). `setup_keyfile` waits for each prompt
before answering it and runs with its proxy at a closed local port; `tier-b-device.sh` judges `doctor`
by its summary line and keeps its output. Rejected: **longer fixed pauses** -- a pause only moves the
race, and an offline board waits out the whole listing timeout; **a switch in jichi that skips the
wizard's listing under test** -- the product would carry a test-only path, while the proxy is the
environment every libcurl program already honours; **a model config for the rig's step 4** -- the step
is the offline surfaces with no network and no key, and a configured model would make doctor probe a
server; **accept doctor's exit code 1** -- a doctor that crashed also exits non-zero, and the summary line
is the positive marker that it ran to the end.
**What it costs:** nothing this test held before: without a key the providers answered 401, so it never
reached a successful listing. No test does -- DEFERRED def-314.
**Where:** ROADMAP M881, `tests/smoke/setup_keyfile.sh`, `scripts/tier-b-device.sh` (step 4)

<a id="dec-m882"></a>
#### M882 — Data handed to libcurl outlives every connection; the sanitizer turn runs over a kept connection…
**Decision** (the operator, 2026-10-02: "Fix forward tonight"). The socket watch has static storage,
reset per transfer, and the mock can keep a connection open, under which `make ci`'s AddressSanitizer
turn runs with `detect_stack_use_after_return`. Rejected: **withdraw M880** -- the operator chose to fix
forward; **detach the data after the transfer** -- libcurl copies it into each connection, which is what
crashed; **a watch allocated per handle** -- right for concurrent handles, but jichi's HTTP is one reuse
handle per process, and a heap watch would need freeing after the last connection closes;
**`Connection: close` on every request** -- it would hide the class instead of testing it, and give up
the reuse that M227 measured; **a keep-alive test in the normal smoke tier only** -- a write into a
returned frame is undefined behaviour -- here the same turn crashed under `timeout` and ran clean
without it -- so only the sanitizer's verdict is a gate.
**What it costs:** one static watch per process. A second thread doing HTTP would need a watch per handle,
which the comment on `g_sockwatch` says.
**Where:** ROADMAP M882, `src/net/jc_http.c` (`g_sockwatch`), `tests/tools/mockmodel.c` (`--keep-alive`),
`tests/tools/mm_core.c`, `tests/smoke/leak_turn.sh`, `tests/smoke/_smoke.sh` (`mm_start`)

<a id="dec-m885"></a>
#### M885 — A SIGTERM from outside is named as one; a call the run's stop killed met no limit…
**Decision** (2026-10-03; the demos drive's N47, continued, as threadwork proposed it). A delegate stopped
by a SIGTERM its batch did not send is reported as stopped from outside the run, and the parent is told
its own run goes on. A call that the run's stop killed gets no limit advice. Rejected: **a new stop name
on the wire** (`terminated`) -- `stop=aborted` is read by the journal, the drive's tools and older
parents, so the difference travels as a field and is said in the advice; **infer the stop's kill from the
call's message** (`[stopped: ...]`) -- the report keeps 200 bytes of it, and the marker comes at its end;
**name a SIGTERM in a batch the lead aborted** -- the lead stops its children itself then, closing their
pipes first, and an operator's interrupt of the lead reaches them the same way; **make the popen path stop
its command in the same milestone** -- every command's process group and abort handling would change, so
it is def-315.
**What it costs:** two fields on the report, two on the app, one on the done line.
**Where:** ROADMAP M885, `src/util/jc_delegreport.c`, `src/chat/jc_app.c`, `src/chat/jc_agent.c`,
`src/tools/jc_tool_parallel.c`, `tests/smoke/parallel_term_report.sh`, DEFERRED def-315

<a id="dec-m886"></a>
#### M886 — A child's failing run is reset only by a shell command that changed the worktree's content…
**Decision** (the operator, 2026-10-03: "By the worktree's content"; the demos drive's N50). While a
child's failing run is counted, the worktree's content is fingerprinted before and after each shell
call, and only a change resets the count. Rejected: **judge a command by its words** -- a list of
read-only commands is cheaper, but a pipe or a script hides a write from a list; **`git status`
before and after**, threadwork's proposal -- a second edit to a file already modified leaves its
status line unchanged, so a real fix made through the shell would not reset the count; **write a
tree to the object store** (M863's temporary index) -- it writes blobs for every changed and untracked
file on every shell call of a failing run, where hashing them reads and writes nothing; **reset on
"cannot tell" never** -- with no git the count would stop a child that was editing through the shell.
**What it costs:** a `git ls-files` and a read of each changed file per shell call, only during a
child's failing run; files over 8 MB count by size and mtime.
**Where:** ROADMAP M886, `src/snapshot/jc_snapshot.c` (`jc_snapshot_worktree_fingerprint`),
`src/chat/jc_agent.c`, `tests/smoke/fail_loop_reads.sh`

<a id="dec-m887"></a>
#### M887 — A write child's file tools reach what its shell fence grants, and no further…
**Decision** (the operator, 2026-10-03: "Yes, in write children"; the demos drive's N49 (a)). In a
parallel write child the file tools also reach its own temp dir and, with the shell fence on, the
`shellFenceAllow` directories. Rejected: **keep the fences separate** and name the shell in the refusal
-- the probes did reach for the shell, and their relative-path scratch was merged into the tree; **grant
the file tools all the shell may write**, the shadow repo and `/dev/null` with it -- the shadow repo is
jichi's own store, not a place for a task's files; **read the inherited `TMPDIR`** -- a child whose
private dir could not be made keeps the parent's, possibly `/tmp`, which the fence does not grant;
**grant it at the top level too** -- there is no shell fence there to follow.
**What it costs:** a granted directory is now written by two routes, the file tools and the shell. Only
the shell's route is held by Landlock; the file tools are held by this check.
**Where:** ROADMAP M887, `src/chat/jc_app.c` (`jc_app_path_denied_ex`), `src/tools/jc_tool_parallel.c`,
`tests/test_app.c`, `tests/smoke/fence_file_tools_grants.sh`

<a id="dec-m888"></a>
#### M888 — Every new directory a write child adds is named; one over 1 MB or 100 files is held, the rest merges…
**Decision** (the operator, 2026-10-03: "Name all, hold large ones"; the demos drive's N49 (b)). The
batch result names each new directory a write task adds, with its file count and size; one over 1 MB or
100 files is held out of the merge and the task kept as `parallel-new-dir`; the task's other files
merge. Rejected: **name them and merge as today** -- the merge runs before the lead reads the batch
result, so run 0102's 15 MB was in the live tree when its name first reached the lead; **hold every new
directory** -- a task that adds a source directory would need
a recover step each time; **hold the whole task, as M865 does** -- the scratch is not the task's work, and
its real change would wait on a directory nobody wanted; **compare against a per-task file list** --
that is N35's `files`, which needs the lead to declare it, and this holds without one.
**What it costs:** a large directory a task meant to add is held too, and is taken from the Kept list on
purpose. A directory an earlier task in the batch created is not new to a later one.
**Where:** ROADMAP M888, `src/tools/jc_tool_parallel.c` (`newdirs_scan`, the merge),
`tests/smoke/parallel_new_dirs.sh`

<a id="dec-m889"></a>
#### M889 — The event log repairs any line a producer cut mid-character; the byte-counted cuts left in src/ are held by count…
**Decision** (2026-10-03; the demos drive's N51, as threadwork proposed it). The title and the answer cap
cut at a character; `jc_eventlog_end` repairs a line that is not valid UTF-8; a lint holds the 27
`%.<n>s` conversions left in `src/` per file. Rejected: **fix all 27 now** -- most cut ASCII (hashes,
refs, ids), and the sink now repairs what any of them could break in a telemetry line; **drop an
invalid line** instead of repairing it -- the event is real, and a lost event is worse than a U+FFFD;
**repair only in `jc_eventlog_add_text`** -- a string added with `cJSON_AddStringToObject` bypasses it,
and the line is where every producer meets.
**What it costs:** one validity scan of each event line, which allocates only when a repair is needed.
**Where:** ROADMAP M889, `src/util/jc_eventlog.c` (`jc_eventlog_end`), `src/tools/jc_tool_parallel.c`,
`tests/smoke/parallel_title_utf8.sh`, `tests/smoke/utf8_cut_lint.sh`

<a id="dec-m890"></a>
#### M890 — `"merge": false` keeps a write task's work at any ending; the merge stays file by file…
**Decision** (the operator, 2026-10-03: "Build `"merge": false`" and "Keep file by file"; the demos
drive's N48, §15 and §20). A write task with `"merge": false` is kept and named, never merged, at any
ending and after any verify; the merge of the other tasks stays file by file, first writer kept.
Rejected: **not now** -- a probe's code reached the tree four times in one night, each taken out by
hand; **infer it from the
agent profile** ("changes no repository file") -- prose is not a fence; **merge a task whole or not at
all** -- it would also hold every task that adds to a file an earlier task changed, such as a test
registry, where today the dropped file is named and re-applied.
**What it costs:** a lead must mark its probes. A probe left unmarked merges as before.
**Where:** ROADMAP M890, `src/tools/jc_tool_parallel.c` (the schema, the parse, the merge),
`tests/smoke/parallel_no_merge.sh`

<a id="dec-m891"></a>
#### M891 — Each task's section names its model, and says when it was the lead's own…
**Decision** (the operator, 2026-10-03: "Name each task's model"; the demos drive's §15). Each task's
section in the batch result names the model it ran on, and adds a note when that was the lead's own
because neither the task nor its profile named one. Rejected: **leave it to telemetry** -- the lead
reads the batch result, not the telemetry, and the drive's probes ran on the slow model unseen;
**require a model per task** -- omitting it is a legitimate choice, and the note makes it visible.
**What it costs:** one line per task.
**Where:** ROADMAP M891, `src/tools/jc_tool_parallel.c` (the batch result), `tests/smoke/parallel_task_model.sh`

<a id="dec-m892"></a>
#### M892 — The end-of-run check names its sender and a way out; an answer that changed nothing ends the run on its verdict…
**Decision** (the operator, 2026-10-03: "(a) and (b)"; the demos drive's N52 (a)). The fix-forward
message says it is jichi's end-of-run check, not the operator, and offers a way out: say the failure is
not yours and make no further edits. An answer after which the worktree's content is unchanged (by
calls where git cannot tell) ends the run on the check's verdict, without re-running the check.
Rejected: **reword only** -- the way out would be words: an answer with no edits re-ran the same check
on the same tree and spent a retry, four times in a row; **re-run the check anyway** -- an unchanged tree
fails the same way, or passes by a flake, and neither is the model's work; **a new outcome for a
declined check** -- the verdict is the check's, and a new outcome would ask every consumer of the
journal and the exit code to learn a name for "verify failed".
**What it costs:** a fingerprint of the worktree at each fix-forward, where git can take one. A model
that answers "fixed" without changing anything now ends the run at once, where it used to burn the
retries first.
**Where:** ROADMAP M892, `src/chat/jc_agent.c` (the end-of-run gate, the fix-forward message),
`tests/smoke/verify_way_out.sh`

<a id="dec-m893"></a>
#### M893 — A failure on a line that failed before the run's first change predates it: said so, no retry spent, once…
**Decision** (the operator, 2026-10-03: "(a) and (b)"; the demos drive's N52 (b)). jichi keeps the first
failing line the check printed before the run's first change -- in the run-start verify, or in a batch's
worktree verify of the same command on the starting tree -- and when the end-of-run check fails on that
very line, says the failure predates the run's changes and spends no retry on that round, once per run.
Rejected: **match the message without its location** (M89's signature) -- the same error in another file
is another failure, and the claim is about this one; **accept any earlier failure** -- a batch on a base
the lead had already changed proves nothing about the starting tree; **skip the round altogether** --
the model may still find a cause in the tree, and M892's way out already ends the run when it does not;
**free rounds without a bound** -- a run could live on them.
**What it costs:** the baseline's output is kept while it runs; one fingerprint per run's first turn, and
one per batch. A failure that prints no `error:` line is never said to predate anything.
**Where:** ROADMAP M893, `src/chat/jc_envelope.c` (`jc_env_fail_line`), `src/chat/jc_agent.c`,
`src/tools/jc_tool_parallel.c`, `src/snapshot/jc_snapshot.c`, `tests/smoke/verify_predates.sh`

<a id="dec-m894"></a>
#### M894 — The CI example names its key's variable; a driver runs the recipe as printed; the class waits for a design…
**Decision** (the operator, 2026-10-03: "include the credential fix"). `DEPLOYMENT.md`'s CI example names
the CI variable in its config and copies nothing; `HARDENING.md` says what the scrub does not cover; a
smoke driver runs the recipe read from the page. Rejected: **add CI names to the built-in scrub list
now** -- the class needs a design (the plan's §4.2: a register, built-in CI names, a `doctor` warning,
and a verify command that may legitimately need a token); **a warning beside the old recipe** -- a
recipe is copied, not read; **retype the recipe in the test** -- a test of a copy holds the copy.
**What it costs:** one driver that reads a page and runs `make`.
**Where:** ROADMAP M894, `docs/DEPLOYMENT.md` §4c, `docs/HARDENING.md` §2,
`tests/smoke/ci_recipe_secret.sh`

<a id="dec-m895"></a>
#### M895 — A silent success says it succeeded; its third per normalised command in a turn says what that means…
**Decision** (the operator, 2026-10-04: "(a) and (b)"; the demos drive's N54). A command that exits 0 and
prints nothing reads "(no output: the command succeeded and printed nothing)"; silent successes are counted
per turn by M806's normalised command, and from the third the result says what the silence means.
Rejected: **reword only** -- the drive's workers re-ran the suite after a profile line had told them the
same thing; **stop the run** at a count -- a passing suite re-run is waste, not harm, and an advisory is
the measured first step (M733's success twin notes and never stops either); **count by the exact
arguments** -- the drive spelled one command six ways; **count every silent command together** -- two
different checks that both pass silently are not a loop.
**What it costs:** a 16-key table per turn, and one more line in a result now and then. A command run with
different arguments counts apart.
**Where:** ROADMAP M895, `include/jc_tool.h` (`JC_RUN_SILENT_OK`), `src/tools/jc_tool_run.c`,
`src/chat/jc_agent.c`, `tests/smoke/silent_success.sh`

<a id="dec-m896"></a>
#### M896 — A shared fence grant is documented as shared state; the removal advisory waits on a measurement…
**Decision** (the operator, 2026-10-04: "(a) now, (b) later"; the demos drive's N53). The configuration
guide says a `shellFenceAllow` directory is shared by every write child, removal included, and shows how
to give each child its own copy inside its `$TMPDIR`. Rejected for now: **the advisory at once** -- how
often it would fire, and on what, is unmeasured (def-316); **a grant without removal rights** -- Landlock
can withhold them, but a cache deletes and renames its own files, so the grant could break the tool it
exists for, unmeasured; **per-child copies made by jichi** -- which variable a tool reads is the tool's
business and the drive's to set.
**What it costs:** nothing in the code; a per-child cache, where a drive chooses one, starts cold.
**Where:** ROADMAP M896, `docs/CONFIG_TUTORIAL.md` (the shell fence), DEFERRED def-316

<a id="dec-m897"></a>
#### M897 — The first-token time is the first event the provider calls output, asked through the vtable…
**Decision** (plan 2026-10-03, step 1.1). `ttft_ms` runs from sending the request to the first SSE
event after which the provider reports answer text, reasoning or a tool call; the agent asks through a
new vtable entry, `stream_started`. Rejected: **the first byte** -- LM Studio sends a role-only delta
with empty content at once, so the first byte says the server is up, not that the model answered;
**the first text delta through the sink** -- the sink sees answer text only, so a reply that is a tool
call, or a reasoning model's long think, would read as no first token or a late one; **reading the
event in the agent** -- that is a dialect in the loop, which the provider abstraction exists to keep
out. Reasoning counts because something is arriving; a reader who wants the first answer token
cannot get it from this field.
**What it costs:** one call through the vtable per SSE event, and a field on each model call.
**Where:** ROADMAP M897, `include/jc_provider.h` (`stream_started`), `src/provider/jc_provider.c`,
`src/chat/jc_agent.c`, `src/util/jc_telemetry.c`, `tests/smoke/first_token_time.sh`

<a id="dec-m898"></a>
#### M898 — An attempt journals into the process envelope's journal when there is one, else its own…
**Decision** (plan 2026-10-03, step 6.0). Every attempt keeps a run journal: the process envelope's,
with its run id, when an envelope flag armed one; otherwise `~/.jichi.d/runs/<id>.jsonl`. An `attempt`
event carries the verdict, and `jichi runs` shows it as a note. Rejected: **a journal only when a flag
asks** -- that is what left a passed attempt unlisted, and a curriculum run is exactly the unattended
batch `runs` exists for; **a second journal beside the process one** -- the measured stub (`open` only,
outcome `?`) would stay beside the real row, two rows for one attempt; **a new outcome value for the
verdict** -- `outcome` is the turn's envelope outcome, which a supervisor already gates on, and TAINTED
can coexist with outcome `ok`; the verdict goes in its own field.
**What it costs:** one file under `~/.jichi.d/runs` per attempt, as for any bounded run.
**Where:** ROADMAP M898, `src/main.c` (`run_assignment_attempt`, `attempt_journal`),
`src/util/jc_runsview.c`, `tests/smoke/attempt_record.sh`

<a id="dec-m932a"></a>
#### M932a — The board tool returns the change and the counts; `list` counts the done cards unless asked…
**Decision** (the demos drive's N37, as threadwork proposed it; plan 2026-10-03, step 2.1). A change
returns what happened, the changed card and one line of counts; `list` shows todo and doing in full
and done as a count, and `"all": true` shows everything. Rejected: **paginate `list`** -- a model
must then know to ask for page two, and the cards it acts on are the open ones; **drop done cards from
the file** -- the team did that by hand into `board_done.json`, and the board is the user's record, not
the model's working set; **compact `/board` and `jichi board` too** -- a person reads them once, not
on every turn.
**What it costs:** a model that wants a done card's text makes a second call with `all`.
**Where:** ROADMAP M932, `src/tools/jc_tool_board.c`, `src/chat/jc_board.c`,
`tests/smoke/board_compact.sh`

<a id="dec-m932b"></a>
#### M932b — `jichi board --output json` is read-only, and an unreadable board is an error, not an empty one…
**Decision** (plan 2026-10-03, step 2.2). The JSON view prints the board and writes nothing; edits stay
on the existing commands. A board file that cannot be read exits 1. Rejected: **a control-socket verb
now** -- no supervisor has needed to change a running session's board, and the file under its lock
(M761) already serves every reader; **JSON for the edits too** -- their one-line text answers are
already exact, and a second format doubles the contract for no reader; **an empty board with a
warning, as the text view does** -- a script reads stdout, not the warning, and `{"cards":[]}` from a
damaged file is the silent-empty failure M533 fixed for writers.
**What it costs:** a script must handle exit 1 as "the board is damaged", distinct from no board.
**Where:** ROADMAP M932, `src/main.c` (`run_board`), `src/chat/jc_board.c` (`jc_board_to_json`),
`tests/smoke/board_json.sh`

<a id="dec-m944"></a>
#### M944 — Every headless run a living page shows pins its stdin, not only the ones that can hang…
**Decision** (the operator, 2026-10-04, asked with the measurement: 152 of 179 runs unpinned, most typed
at a terminal where nothing blocks). Every `jichi -p` example on a living page carries `< /dev/null` or
another source for stdin, and `docs_stdin_lint` holds it. Rejected: **only where it can hang** (ssh,
`$( )`, `&`, CI recipes; ~30 edits) -- shorter examples, but a learner who pastes a terminal example into
a script inherits the hang, and the rule would be harder to state and to lint exactly; **teach it once in
prose** -- CLAUDE.md already did, and the pages drifted to 152 bare runs under it.
**What it costs:** twelve characters per example, and one more thing a reader sees before the prompt
does anything.
**Where:** ROADMAP M944, `tests/smoke/docs_stdin_lint.sh`

<a id="dec-m909"></a>
#### M909 — An error inside a 200 stream fails the call like an HTTP 4xx: no answer, no retry…
**Decision** (found by curriculum Stage 1, 2026-10-04). A provider keeps a stream's error message
(`stream_error` on the vtable) and the agent loop fails the call as `JC_ERR_PROVIDER`. Rejected: **keep
the text and flag it** -- the message would still be the "answer" every consumer reads, the M521 shape of
"an empty answer with a success verdict"; **retry it as transient** -- the observed cause, a request over
the loaded context, fails identically on retry, and the unstreamed twin is a 400, which jichi does not
retry; **teach the loop the `error` event** -- the loop never reads a dialect; the vtable is where one
lives.
**What it costs:** a server that sends a recoverable error inside a stream (Anthropic's `overloaded_error`)
is not retried, while the same error as HTTP 529 is (`is_transient` retries every 5xx) -- DEFERRED def-319.
**Where:** ROADMAP M909, `include/jc_provider.h`, `src/provider/jc_provider.c`,
`src/provider/jc_provider_openai.c`, `src/provider/jc_provider_anthropic.c`, `src/chat/jc_agent.c`,
`tests/smoke/stream_error.sh`

## Interfaces and contracts

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **jichi is a program, not a library** (M301) | [M301](#dec-m301) |
| **No new API was written for embedding** (M301) | [M301](#dec-m301-2) |
| **Completions are linted exactly; the man page only to the `describe` contract set** (M326e) | [M326e](#dec-m326e) |
| **Capability probe for "will it work", OS name for "what should I suggest"** (M326q) | [M326q](#dec-m326q) |
| **OS detection supplies a DEFAULT for an opt-in prompt, it does not write `sound` itself** (M326q) | [M326q](#dec-m326q-2) |
| **A new fault-injection site rather than an untested branch** (M326q) | [M326q](#dec-m326q-3) |
| **A small MACHINE and a small MODEL are different constraints** (M326p) | [M326p](#dec-m326p) |
| **The machine probe asks before it WRITES, not before it reads** (M326p) | [M326p](#dec-m326p-2) |
| **The wizard teaches the config while writing it** (M326n) | [M326n](#dec-m326n) |
| **The 'sets:' line is DERIVED from the feature bitmask** (M326n) | [M326n](#dec-m326n-2) |
| **`--accessible` was made to reach `setup`** (M326n) | [M326n](#dec-m326n-3) |
| **`tester` and `reviewer` are journeys; `learner`/`instructor` are a fourth axis** (M326m) | [M326m](#dec-m326m) |
| **The stance defaults to `learner` interactively and to none on the flag path** (M326m) | [M326m](#dec-m326m-2) |
| **Axis order of application is role → journey → machine → stance** (M326m) | [M326m](#dec-m326m-3) |
| **`small-local` is a third axis, not a role** (M326k) | [M326k](#dec-m326k) |
| **The wizard states the optional-answer rules once, then reports what was skipped** (M326k) | [M326k](#dec-m326k-2) |
| **Each menu walks the table counting its own axis** (M326k) | [M326k](#dec-m326k-3) |
| **Roles and journeys are two questions that compose** (M326j) | [M326j](#dec-m326j) |
| **Journeys stay addressable as `--preset`** (M326j) | [M326j](#dec-m326j-2) |
| **Both scaffold packs are written, role first** (M326j) | [M326j](#dec-m326j-3) |
| **The composition property is tested, not the 60 pairs** (M326j) | [M326j](#dec-m326j-4) |
| **"Journey" became a field on the preset, not a name comparison** (M326i) | [M326i](#dec-m326i) |
| **One numbering across both halves of the first screen** (M326i) | [M326i](#dec-m326i-2) |
| **`DECISIONS.md` is excluded from the flags lint** (M326i) | [M326i](#dec-m326i-3) |
| **The headless default was fixed rather than given an opt-out flag** (M326h) | [M326h](#dec-m326h) |
| **The two surfaces share the renderer, not the decoration** (M326h) | [M326h](#dec-m326h-2) |
| **The raw tool arguments moved to `-v`, not to nothing** (M326h) | [M326h](#dec-m326h-3) |
| **The machine surface stays complete; only the human surface is bounded** (M326h) | [M326h](#dec-m326h-4) |
| **An advertised verb found unreachable is revived, not retired** (M326c) | [M326c](#dec-m326c) |
| **The prompt keeps the tier, not the model id** (M296) | [M296](#dec-m296) |
| **The Guix `parallel_abort` row is closed as "does not reproduce at HEAD", not "was never real"** (M468) | [M468](#dec-m468) |
| **A flag four platforms accept is not a portable flag; ask the tool** (M658) | [M658](#dec-m658) |
| **Naming a profile: voice and read-only in the main turn; its tools a fence in a run of its own** (2026-09-30) | [2026-09-30](#dec-2026-09-30-profile) |
| **jichi reads the shared rules (AGENTS.md, else CLAUDE.md), then its own JICHI.md** (2026-10-01) | [2026-10-01](#dec-2026-10-01-rules-names) |
| **`init` never hides a project's own rules file; a pack's rules then go to JICHI.md** (2026-10-01) | [2026-10-01](#dec-2026-10-01-init-rules) |
| **`/write-docs` and `/proofread`: one text in every pack, routing to the docs profiles when they exist** (2026-10-01) | [2026-10-01](#dec-2026-10-01-docs-routing) |
| **LSP: report a server's refusal, ask for pull diagnostics, fall back; then TypeScript 7's server first** (2026-10-01) | [2026-10-01](#dec-2026-10-01-lsp-ts) |
| **Godot: the `--check-only` gate by default; the headless editor and a bridge as a measured opt-in** (2026-10-01) | [2026-10-01](#dec-2026-10-01-godot) |
| **The desktop application is postponed; its design and checklist stay proposals** (2026-10-01) | [2026-10-01](#dec-2026-10-01-desktop) |

<a id="dec-m301"></a>
#### M301 — jichi is a program, not a library
embedding means driving the process. No `libjichi`, no stable ABI.
**Rejected:** Exposing C symbols — the process boundary *is* the contract.
**Where:** [EMBEDDING.md](EMBEDDING.md) §4

<a id="dec-m301-2"></a>
#### M301 — No new API was written for embedding
a stability *statement* was.
**Rejected:** Designing an embedding API — measuring first showed jichi was already drivable; what was missing was which parts are a promise.
**Where:** [EMBEDDING.md](EMBEDDING.md)

<a id="dec-m326e"></a>
#### M326e — Completions are linted exactly; the man page only to the…
**Rejected:** Linting the man page against all 51 subcommands — subcommands_lint records a standing decision that `jichi.1` is deliberately terse, and mechanising "document everything" would overturn an editorial choice by lint. Also rejected: leaving the man page unlinted — what jichi tells integrators to rely on must be in the manual.
**Where:** [ROADMAP M326e](ROADMAP.md)

<a id="dec-m326q"></a>
#### M326q — Capability probe for "will it work", OS name for "what…
**Rejected:** A single OS-name check for both — `/proc` can be absent on Linux (a container or chroot) and the name would lie about whether the RSS watchdog runs; conversely the name is exactly what you need to guess `afplay` over `aplay`. Two questions, two kinds of evidence.
**Where:** [ROADMAP M326q](ROADMAP.md)

<a id="dec-m326q-2"></a>
#### M326q — OS detection supplies a DEFAULT for an opt-in prompt, it…
**Rejected:** Seeding `sound` into every generated config, as the chosen option's sketch implied — a `sound` key *registers the `play_audio`/`record_audio` tools*, so it would advertise two mutating tools to projects that never asked for audio. The detection still does its job: it picks the value, the user picks whether to have it.
**Where:** same

<a id="dec-m326q-3"></a>
#### M326q — A new fault-injection site rather than an untested branch
`JC_FAULT_PROCFS`.
**Rejected:** Shipping the `memBudgetMb` warning unexercised — it guards a *safety* key silently doing nothing, and the only other ways to reach it are a non-Linux box or privileges to unmount `/proc` (unprivileged `unshare` was tried and refused).
**Where:** [faults.sh](../tests/smoke/faults.sh)

<a id="dec-m326p"></a>
#### M326p — A small MACHINE and a small MODEL are different constraints
and needed a new feature flag (`JC_SF_LEANHOST`) to keep apart.
**Rejected:** Reusing `JC_SF_LOWRES` for both — it couples `lowResource` to a 6000-token `contextLimit`, which is right when the model cannot use a bigger window and wrong when a hosted 200k model happens to run on a small box.
**Where:** [ROADMAP M326p](ROADMAP.md)

<a id="dec-m326p-2"></a>
#### M326p — The machine probe asks before it WRITES, not before it…
**Rejected:** Asking before reading (a question everyone answers to get local facts that never leave the machine); and keeping the silent read (what deserved consent is the `maxParallelAgents: 8` that ends up in a committed config, disclosing the machine's size). **Operator's decision.**
**Where:** same

<a id="dec-m326n"></a>
#### M326n — The wizard teaches the config while writing it
every option shows the keys it writes and why, and the run ends by printing the file and the four-line minimum.
**Rejected:** A terse menu optimised for keystrokes — it hides the artifact the user must eventually edit by hand, which is where the real work happens. **Operator's decision**, and the reason given is worth keeping: the wizard is a helper, and learning to configure jichi in a text editor is the skill.
**Where:** [ROADMAP M326n](ROADMAP.md)

<a id="dec-m326n-2"></a>
#### M326n — The 'sets:' line is DERIVED from the feature bitmask
never written beside it.
**Rejected:** Hand-writing what each preset does — a second list of the same facts is a list that rots, and this project has paid for that twice (M262's tool table, M285's dead fences). The `why:` line *is* hand-written, because it is judgement rather than fact.
**Where:** same

<a id="dec-m326n-3"></a>
#### M326n — --accessible was made to reach setup
by applying the flag before subcommand dispatch.
**Rejected:** Leaving it: the wizard is the first thing a new user meets, and the one accessibility switch jichi has was inert there — it was set at main.c:10301 while setup dispatched at 9967, and the wizard never consulted it anyway.
**Where:** [ACCESSIBILITY.md](ACCESSIBILITY.md)

<a id="dec-m326m"></a>
#### M326m — tester and reviewer are journeys; learner/instructor are a fourth axis
**Rejected:** Enumerating more professions — measured, six of the seven remaining entries differ from each other *only* in which scaffold pack they select, so a new profession is a new row restating an existing pack, and the queued domain proposal would have made the first question a 20-item pack-picker. Also rejected: the full generic re-cut (domain / journey / system / stance with detection), as more change than the confusion warranted. **Operator's decision.**
**Where:** [ROADMAP M326m](ROADMAP.md)

<a id="dec-m326m-2"></a>
#### M326m — The stance defaults to learner interactively and to none on…
**Rejected:** One default for both — a scripted `setup --preset developer` silently gaining assignment scaffolding is a surprise, not a kindness, and would have changed what CI and `tests/smoke/setup.sh` produce. Also rejected: defaulting to professional, and asking with no default; *self-learners are the first learners*, and they are least able to configure their way out of a bad default. **Operator's decision.**
**Where:** same

<a id="dec-m326m-3"></a>
#### M326m — Axis order of application is role → journey → machine…
**Rejected:** Any other order: `jc_setup_apply_preset` fills only what is unset, so first-writer wins — and `mode` is claimed by both journey and stance. A review journey is plan mode whoever is doing it, so the journey must precede the stance. The one genuine overlap between the four axes, stated rather than left to be discovered.
**Where:** same

<a id="dec-m326k"></a>
#### M326k — small-local is a third axis, not a role
the wizard asks who you are, what you are walking into, and what you are running on.
**Rejected:** Leaving it among the roles — it answered a different question from every entry around it (your hardware, not your work), and made "a learner on a 7B model" unsayable. Also rejected: grouping the role menu visually without changing behaviour, which would have organised the category error rather than removing it. **Operator's decision.**
**Where:** [ROADMAP M326k](ROADMAP.md)

<a id="dec-m326k-2"></a>
#### M326k — The wizard states the optional-answer rules once, then reports what…
**Rejected:** Repeating "(blank = none; config key …)" on all ~15 optional prompts — a reader stops seeing it by the fourth. Also rejected: a single pointer line and nothing else, which leaves a user who pressed Enter through the block with no record of what they declined. **Operator's decision.**
**Where:** same

<a id="dec-m326k-3"></a>
#### M326k — Each menu walks the table counting its own axis
never `index = choice - 1`.
**Rejected:** Assuming menu position equals array index — true only while the non-roles sat in a block at the end, and `small-local` moved out from the MIDDLE. The unit test now asserts the axes are *interleaved*, so a future reordering cannot make the shortcut look safe again.
**Where:** same

<a id="dec-m326j"></a>
#### M326j — Roles and journeys are two questions that compose
the journey layers onto the role, as `--profile` already does.
**Rejected:** Keeping them one exclusive choice and only fixing the wording — the source had called them *orthogonal* since M183 while applying exactly one preset, and `tester` / `small-project` differ only in their start script, so a user was choosing between near-duplicates. *(M326k correction: this row first said "literally the same configuration"; a full four-axis comparison found no exact duplicates in the table.)* **Operator's decision, taken after the ambiguity was put to them**; M326i had improved how that list looked without touching why it confused.
**Where:** [proposal](proposals/2026-08-role-and-journey.md)

<a id="dec-m326j-2"></a>
#### M326j — Journeys stay addressable as --preset
**Rejected:** Moving them to their own table — tidier, and it breaks `--preset rewrite`, which is documented in `SDLC.md` and `WORKFLOWS.md` and exercised by `tests/smoke/setup.sh`. Checked before designing.
**Where:** same

<a id="dec-m326j-3"></a>
#### M326j — Both scaffold packs are written, role first
**Rejected:** Writing only one — that reintroduces the either/or the change exists to remove. `scaffold_write_pack` already skips existing files, so a second pack composes by construction.
**Where:** same

<a id="dec-m326j-4"></a>
#### M326j — The composition property is tested, not the 60 pairs
role-then-journey may only add.
**Rejected:** Enumerating 12 × 5 combinations — unmaintainable, and it would assert what the table happens to contain rather than what makes any pair safe.
**Where:** same

<a id="dec-m326i"></a>
#### M326i — "Journey" became a field on the preset, not a name…
**Rejected:** Keeping `strcmp(p->name, "small-project")` in `main.c` — it made the preset table's ORDER load-bearing with nothing saying so, so a role inserted after that point would have been silently relabelled a journey. Found while writing the deferral row, by checking the claim rather than asserting it.
**Where:** [ROADMAP M326i](ROADMAP.md)

<a id="dec-m326i-2"></a>
#### M326i — One numbering across both halves of the first screen
with the halves framed as alternatives (`...OR`).
**Rejected:** Per-group numbering — the user picks exactly one preset, so two lists both starting at 1 make "1" ambiguous. Also rejected: splitting into two questions (pick a kind, then pick within it) — it costs a screen and needs a "neither" path, which is more than the confusion warranted; the complaint was that the halves were indistinguishable, not that the list was long.
**Where:** same

<a id="dec-m326i-3"></a>
#### M326i — DECISIONS.md is excluded from the flags lint
**Rejected:** Rewording each rejected flag to drop its dashes, which is what M326h did — a register whose purpose is recording REJECTED alternatives will name flags that do not exist, so the collision is structural rather than incidental, and `proposals/` was already excluded for the same reason. The cost is stated rather than hidden: a genuine flag typo in that file is now unlinted.
**Where:** [docs_flags.sh](../tests/smoke/docs_flags.sh)

<a id="dec-m326h"></a>
#### M326h — The headless default was fixed rather than given an opt-out…
the tool line now renders through `jc_tool_arg_summary`, the same summariser the TUI uses.
**Rejected:** Adding `--progress` / `--tool-lines` — it would leave an unbounded `fprintf(stderr, "[tool] %s %s", name, args)` in place as the default and add a flag to escape a defect. A `write_file` of a 200 KB file wrote 200 KB to stderr; that is a bug, not a preference. Permitted because `EMBEDDING.md` lists stderr text under *not an interface*, and no test asserted on it.
**Where:** [proposal](proposals/2026-08-headless-progress.md)

<a id="dec-m326h-2"></a>
#### M326h — The two surfaces share the renderer, not the decoration
**Rejected:** Making headless byte-identical to the TUI (`▸`, colour, depth indentation) — those exist for an interactive terminal; a log file wants a stable greppable prefix and no ANSI. What must not differ is the *content*, which one shared function guarantees.
**Where:** same

<a id="dec-m326h-3"></a>
#### M326h — The raw tool arguments moved to -v, not to nothing
**Rejected:** Deleting them — a malformed tool call is diagnosed by seeing what the model actually sent (the M148 repair path exists because models emit broken argument JSON). Also rejected: a dedicated `--tool-args`; `-v` is already *enable debug logging*.
**Where:** same

<a id="dec-m326h-4"></a>
#### M326h — The machine surface stays complete; only the human surface is…
jsonl `tool_call` keeps the full raw `args`.
**Rejected:** Bounding jsonl too, for symmetry — an agent parsing events needs the real arguments, and unlike stderr the jsonl schema *is* a stable documented interface. The asymmetry is now deliberate and stated; before, it ran the other way by accident.
**Where:** same

<a id="dec-m326c"></a>
#### M326c — An advertised verb found unreachable is revived, not retired
`config path`/`validate` were promised by `--help`, `CONVERT.md` and `describe` (a stability surface) while shadowed by a second dispatch, so they were folded into the live handler.
**Rejected:** Editing `--help` down to the shipped `show\|set\|telemetry` — the smaller diff, and it would have withdrawn a documented answer to "which config file is in effect", which the global+project merge makes genuinely unanswerable by inspection. Also rejected: adopting the dead copy's `show` (a raw-file dump) over the live summary — a doc fix must not silently change a shipped output.
**Where:** [ROADMAP M326c](ROADMAP.md)

<a id="dec-m296"></a>
#### M296 — The prompt keeps the tier, not the model id
**Rejected:** Showing the full id — the prompt is drawn *before* a turn while routing switches models *during* one, so it would make a stale value look precise. The reply header is per model call, so it is the honest place.
**Where:** ROADMAP M296

<a id="dec-m468"></a>
#### M468 — The Guix parallel_abort row is closed as "does not reproduce…
**Rejected:** Declaring it fixed: the measurement is not a byte-identical re-run — M458's row was a Guix System built from the recorded config, this is `guix shell` inside the published desktop image with 6 vCPUs and 8 GB — and a pass in a different environment is weaker than a pass in the same one. Leaving it open: three fixes landed in its exact code path after it was filed and it now passes, so "open" would misdescribe the evidence too. Naming the responsible commit as certain: `448616d` (children inheriting an ignored SIGPIPE) is the best *structural* fit for a parent that never leaves `waitpid`, and nothing bisected it.
**Where:** [ROADMAP.md](ROADMAP.md) M468

<a id="dec-m658"></a>
#### M658 — A flag four platforms accept is not a portable flag;…
`search_code` ran `grep -rnI`, above a comment recording that the flags were *"checked against each usage string, not assumed"* for GNU, FreeBSD, NetBSD and OpenBSD. illumos's grep accepts `-r` and rejects `-I`, so every search on that platform exited 2 and the tool was dead. The fix is a one-shot capability probe (`grep -I -e x /dev/null`) selecting the prefix, with the cost of the fallback — binary files are no longer skipped — stated at the probe rather than hidden.
**Rejected:** **Dropping `-I` everywhere**: it would stop binary files being skipped on every platform to accommodate one. **Keying on `uname`**: same objection as the row above. **Leaving it**: the M461 `--color` defect on OpenBSD was this exact shape one platform earlier, and the lesson there was that the tool *lied* rather than failed — a search returning "(no matches)" reads to a model as "the code does not contain this". The fallback branch cannot occur on any machine here, so it is exercised by a shim grep that rejects `-I`: a branch nobody has watched work is a branch that does not work.
**Where:** ROADMAP M658, `tests/smoke/search_grep_dash_i.sh`

<a id="dec-2026-09-30-profile"></a>
#### 2026-09-30 — Naming a profile: what it applies, and where…
**Decided by the operator on 2026-09-30; built at M848 (2026-10-01)** with the fix for how a delegated command is fenced, which changes the same function ([DEFERRED def-287](DEFERRED.md#def-287), closed). One rule for what naming a profile (`.jichi/agents/<name>.md`) applies. **In the main turn** -- a command's `agent:` key -- the profile sets the persona and the style and can make the turn read-only; its `tools:` and `model:` do not apply, so the turn keeps its tool set and the prompt cache keeps its stable prefix. **In a run of its own** -- a `subtask: true` command, a subagent, `attempt --agent` -- its `tools:` list is also a fence, and it can only narrow what the run would otherwise have. **`attempt` and subagents also take its `model:`.** A command, subtask or not, keeps choosing its model with its own `model:` key, as today.
**Before M848 (from the code, 2026-09-30):** only a subagent applied a profile's `tools:` ([SUBAGENTS.md](SUBAGENTS.md)); a command's `agent:` and `attempt --agent` apply the persona, the style and the read-only flag and nothing else (`jc_app_command_agent_apply`, whose comment calls the rest "a subagent-only concern"). So the assignments pack's four learner tiers differ by persona alone under `attempt`: `learner-senior`, written to have no help tools, is offered `hint` and `ask_for_help` whenever `assignments` is on, and a tier's `model:` does nothing there. `/release-check`'s six-tool list is ignored for the same reason. The tiered-learner experiment of ANECDOTES #12 was designed around a `learner-senior` with no help tools.
**Rejected:** **Applying `tools:` and `model:` everywhere, main turn included** -- in a main-turn command the offered tools would change for one turn, breaking the cached prefix going in and again coming back (re-billed where caching is on), and the history would name tools the turn no longer offers, so a model that calls one costs a refused round trip. **`tools:` as a narrowing in main-turn commands too** -- the same cache cost in every main-turn command that names a profile. **Applying them in `attempt` only** -- it leaves a subtask command's `tools:` without effect, and once a subtask follows the user's own approval mode, in auto mode the list is what keeps a writing profile -- the mentor, in every project initialized before that fix -- away from the shell. **Rewording the pages and changing no code** -- the tiers would stay persona-only, giving up the premise of the assignments pack.
**What it costs, stated:** a user's own subtask command whose profile lists fewer tools than the command uses will have those tools refused -- so the refusal must name the profile's list, and the CHANGELOG must say so. The change reaches existing projects, whose profiles already carry `tools:` and `model:`; the reworded pack assets (the scaffolded sentence that profiles "pin a tier with 'model: fast'", the learner personas, `/release-check`) reach only projects initialized after it, because `init` never overwrites a file (measured 2026-09-30). **What it does not fix:** the rule narrows and never widens, so with `assignments` off the tiers whose persona leans on `hint` and `ask_for_help` are still told to call tools that do not exist.
**Revisit when real use says so** -- the operator's words: "User experience will show if a change is required." The signals to watch: refusals that name a profile's list turning up in real sessions; users asking why a main-turn command ignores its profile's `tools:` or `model:`; `attempt` tiers that stop being comparable.
**Where:** [DEFERRED def-287](DEFERRED.md#def-287), [ASSIGNMENTS.md](ASSIGNMENTS.md), [COMMANDS.md](COMMANDS.md) (the `agent` key, which describes today), the scaffolding packs review (`docs/analysis/2026-09-29-scaffolding-packs-review.md`, A2)

<a id="dec-2026-10-01-rules-names"></a>
#### 2026-10-01 — The rules files: the shared one, and jichi's own…
**Decided by the operator on 2026-10-01, not built yet** ([DEFERRED def-289](DEFERRED.md#def-289)). The operator's reason: "CLAUDE.md is vendor-specific, and jichi does not belong to that vendor"; `AGENTS.md`, the cross-tool name, is fine. The rule: in each directory of the walk, and in `~/.config/jichi/`, jichi reads the **shared rules** -- the first file found from a configurable list, by default `AGENTS.md` then `CLAUDE.md`, which is today's behaviour and keeps every existing project working -- and **then its own file, `JICHI.md`, in addition**. So a project can be vendor-free (`AGENTS.md` and `JICHI.md`, with `CLAUDE.md` taken off the list), and jichi-only notes never displace the shared rules. **Nothing is hidden silently:** `jichi rules` and `doctor` name a rules file that is present and not read, and say why.
**Today (from the code):** globally only `~/.config/jichi/AGENTS.md`; in each directory `AGENTS.md`, else `CLAUDE.md`, the first found winning; then the config's `instructions`, which add. jichi has no file of its own, and a file it does not read says nothing -- which is how `init` came to hide a project's `CLAUDE.md` (the next row).
**How comparable tools do it (their own docs, read 2026-10-01):** Claude Code reads `CLAUDE.md`, and since 2.1.277 "in a project with no CLAUDE.md, Claude Code reads AGENTS.md instead"; opencode reads `AGENTS.md`, with `CLAUDE.md` "used if no AGENTS.md exists"; Gemini CLI reads `GEMINI.md`, its `context.fileName` takes a name or a list, and it "concatenates the contents of all found files". jichi's order is opencode's.
**Rejected:** **`JICHI.md` first in the same chain** -- a user who adds one for a few jichi-only notes would silently lose the shared rules: the next row's trap, reintroduced. **A configurable first-found list alone** -- the choice of names, but still no name of jichi's own, and still silent hiding. **Reading every listed file (Gemini's model)** -- many projects keep a `CLAUDE.md` that is `@AGENTS.md` plus notes for Claude; jichi has no include syntax, so it would read that line literally along with another tool's notes, and near-copies would count twice against the 32 KB cap. **`.jichi/RULES.md` as the name** -- a clean project root, but less discoverable than a file beside `AGENTS.md`, and this repository's `.gitignore` ignores every `.jichi` path, so its own copy would need force-adding.
**What it costs:** a `JICHI.md` adds to the 32 KB rules block, and only when someone writes one. And two ideas to explain where there was one -- in one sentence: jichi reads the shared rules, then its own.
**Revisit when** real use says so: users who want every listed file read, users who want a different own-name, or a `CLAUDE.md` fallback that surprises someone.
**Where:** [RULES.md](RULES.md), [DEFERRED def-289](DEFERRED.md#def-289), `src/chat/jc_rules.c`

<a id="dec-2026-10-01-init-rules"></a>
#### 2026-10-01 — init never hides a project's own rules file…
**Decided by the operator on 2026-10-01, not built yet** ([DEFERRED def-288](DEFERRED.md#def-288)), together with the row above, which settles where a pack's rules go. **Measured on 2026-09-30** with the real binary, offline: in a project whose only rules file is a `CLAUDE.md`, `jichi init` exits 0, writes an `AGENTS.md` and prints nothing about `CLAUDE.md` -- and afterwards `jichi rules` no longer shows the user's rule, because the loader reads `CLAUDE.md` only when there is no `AGENTS.md`. The stub it writes says "jichi reads this file (and CLAUDE.md)", which is false. The decided behaviour: when the project already has its own `AGENTS.md` or `CLAUDE.md`, `init` writes no `AGENTS.md`; it writes the pack's rules to `JICHI.md`, which jichi reads in addition, and says so in one plain message naming both files. If a `JICHI.md` exists as well, the pack's rules go to `.jichi/rules/<pack>.md`, and the message says they are not active until one line is added to `.jichi/config.json`. `init` never edits a config file. The stub stops claiming that `CLAUDE.md` is read beside it.
**Rejected:** **Writing `AGENTS.md` and warning** -- the user's rules stay lost until they act on one line among a list of written files, which a beginner may not see. **Adding `"instructions": ["CLAUDE.md"]` automatically** -- `init` would have to merge into a config it did not write without harming the user's keys, and both files would share the 32 KB cap. **Reading both files in the loader** -- see the row above. **Refusing unless `--force`** -- the `setup` presets call the same writer, and scripted inits would break. **The pack's rules in `.jichi/rules/<pack>.md` in every case** -- the first form of this decision, inactive until the user adds a line; `JICHI.md` makes them active without `init` touching a config.
**Open details, for the milestone to settle:** a later `init` finds the `AGENTS.md` an earlier `init` wrote and cannot tell it from the user's own, so running a pack a second time must not write its rules into `JICHI.md` again (compare the content, or keep a record of what `init` wrote). And when packs are composed in one call, today the first pack to claim `AGENTS.md` wins and the others skip it; whether a later pack's rules should go to `JICHI.md` instead is open.
**What it costs:** the pack's rules become active beside the user's own, so the two can disagree; the message names both files so the user can reconcile them.
**Revisit when** real use says so: users who expected the pack to replace their rules, or to leave them alone entirely.
**Where:** [SCAFFOLDING.md](SCAFFOLDING.md), [DEFERRED def-288](DEFERRED.md#def-288), `src/scaffold/jc_scaffold.c`

<a id="dec-2026-10-01-docs-routing"></a>
#### 2026-10-01 — /write-docs and /proofread: one text in every pack…
**Decided by the operator on 2026-10-01, not built yet** ([DEFERRED def-290](DEFERRED.md#def-290)). **Measured the same night** with the real binary, offline: `jichi init c-cli docs` leaves the generic `/write-docs` and `/proofread`, which do not route, and installs the docs pack's six audience profiles with nothing to call them; `jichi init docs c-cli` keeps the routing. `init` reports the lost version as `= .jichi/commands/write-docs.md (exists; --force to overwrite)`, the same line as a harmless skip. The cause: in one call the first pack wins every shared file, and these two commands come in two versions -- the docs pack's hands the work to `docs-writer-<audience>` and `docs-proofreader-<audience>`, the generic one, which nineteen other packs ship (the scaffolding review's diff of all 33), writes it itself -- while `SCAFFOLDING.md` recommended `<language> docs` and called shared files byte-identical. The decided fix: **one shared text for the two commands in every pack** -- if the first word names an audience and the matching profile exists, hand the work to it; otherwise write it yourself -- so the order stops mattering; a unit test that every shared path is byte-identical across packs, except the files meant to differ (`AGENTS.md`, `config.example.json`); and **`init` saying when a file it skips differs from the pack's version**, with the same comparison the `JICHI.md` duplicate guard needs (the row above).
**Rejected:** **Distinct names for the docs pack's commands** -- the order would not matter, but a combined project would show two near-identical commands, which confuses learners. **Documenting "docs first" alone** -- the wrong order would still lose the routing, only visibly. **Letting a pack's richer version replace a generic one written earlier in the same call** -- code to track what the call wrote, to solve what an asset change solves.
**What it costs:** a project without the audience profiles pays at most one failed hand-over before the command writes the text itself. Projects already set up in the losing order are not repaired, because `init` never overwrites; `init --force docs` would restore the two commands but also overwrite the docs pack's other files, `AGENTS.md` included, so the honest repair is to copy the two files from a scratch `init docs`.
**What it does not fix:** a combined `--dry-run` still counts only what is on disk, not what an earlier pack in the same call will write (the scaffolding review's E4).
**Revisit when** real use says so: a failed hand-over that confuses a model or a user, or a pack that needs its own version of a shared command after all.
**Where:** [SCAFFOLDING.md](SCAFFOLDING.md), [DEFERRED def-290](DEFERRED.md#def-290), `src/scaffold/jc_scaffold.c`, `tests/test_scaffold.c`

<a id="dec-2026-10-01-lsp-ts"></a>
#### 2026-10-01 — The web-ts pack and TypeScript 7: fix the client first…
**Decided by the operator on 2026-10-01, not built yet** ([DEFERRED def-291](DEFERRED.md#def-291), [def-292](DEFERRED.md#def-292), [def-293](DEFERRED.md#def-293)). **Measured that night** on real servers installed from npm into scratch projects, through `jichi lsp`: TypeScript 6 with the pack's `typescript-language-server` reports the type error in 1.1 s; with TypeScript 7 that server refuses to start ("The TypeScript of the workspace (TypeScript 7.0.2 …) provides no tsserver.js … Exiting.") and jichi shows only "no diagnostics received", after 16 s; TypeScript 7's own server, `tsc --lsp --stdio` (typescript-go 7.0.2), pushes nothing and answers a pull request (`textDocument/diagnostic`, LSP 3.17) with the exact error -- a request jichi's push-only client never sends, so again "no diagnostics received"; on TypeScript 6, `tsc --lsp` fails in 130 ms. npm's `typescript` `latest` was 7.0.2 that night, so a plain `npm install typescript` gets the version the pack's server refuses. The plan, one LSP milestone, in this order: **jichi reports a server's error reply** in the server's own words instead of treating it as success (`lsp_request` never looks at `"error"`); **jichi asks for diagnostics** when a server advertises pull diagnostics; **jichi falls back to the next server** for the extension when one fails or refuses to start, and `jichi lsp` and `doctor` say which one ran; then the pack ships both entries, `tsc --lsp --stdio` first and `typescript-language-server` second, once the probe has run on real TypeScript 6 and 7 projects.
**Rejected:** **Switching the pack to `tsc --lsp` now** -- measured: no diagnostics in jichi today. **Shipping the TypeScript 7 entry inert under a key the loader ignores** (the scaffolding review's suggestion, made before anything was run) -- it would hold a server jichi cannot use yet, and it relies on an unknown top-level key passing validation without a word. **Choosing the server at `init` from `package.json`** -- decided once, and stale when the project upgrades; the fallback decides at run time. **Documenting "TypeScript 6 only" and changing nothing** -- every new project, which gets TypeScript 7 by default, would keep missing diagnostics silently.
**What it costs:** a small client milestone rather than the asset tweak the review expected, placed after the security batch. Until it lands, a web-ts project on TypeScript 7 gets no diagnostics from the pack's server; `LSP.md` says so, and how to pin TypeScript 6. **What it buys beyond TypeScript:** every server's refusal becomes visible, pull-only servers work, and the fallback serves the Elixir and Erlang cases the review raised.
**Revisit when** real use says so, or when typescript-language-server supports TypeScript 7.
**Where:** [LSP.md](LSP.md), `src/lsp/jc_lsp.c`, `src/scaffold/jc_scaffold.c`

<a id="dec-2026-10-01-godot"></a>
#### 2026-10-01 — Godot's language server: a measured opt-in, not a default…
**Decided by the operator on 2026-10-01, after a hand test the operator agreed to** ([DEFERRED def-294](DEFERRED.md#def-294), [def-295](DEFERRED.md#def-295)). Godot's GDScript language server runs inside an editor and speaks TCP; jichi's client speaks stdio. **Measured** with Godot 4.7.2 on this project's workstation, in an isolated scratch project: a headless editor (`--headless --editor --path . --lsp-port 6005`) accepted connections after 2-2.5 s and pushed diagnostics; `jichi lsp` through `socat - TCP:127.0.0.1:6005` printed the script's two type errors in 0.6 s, through OpenBSD `nc` in 1.1 s; with no editor running, the server was `unavailable` within 11 ms; Ctrl-C stopped the editor at once; and `--headless --check-only --script` exited 1 in 213 ms with the same errors and no editor. The editor peaked at about 750 MB of resident memory. The decision: **the `--check-only` gate and GDScript 4 guidance** (signals, `CharacterBody2D`, `super()`) in the godot pack by default; **the headless editor and a stdio bridge as an opt-in recipe**, published as run ([GAME_DEVELOPMENT.md](GAME_DEVELOPMENT.md)) and offered by the pack as an example; **a TCP transport in jichi's own client later**.
**Rejected:** **No language server at all** (the scaffolding review's suggestion before the test) -- the test showed the bridge works, so withholding a measured recipe helps no one. **The bridge as a default** -- it needs a running editor, about 750 MB, and `socat` or `nc`; a default must work everywhere the pack does, and the `--check-only` gate does. **TCP in jichi's client now** -- C code, a config key and a clean refusal in builds without sockets, for a need that real use of the recipe should show first.
**What it costs:** a second terminal and the editor's memory, for those who opt in; and the recipe depends on tools jichi does not ship.
**Revisit when** real use says so: users of the recipe on machines without `socat` or `nc`, which is the case for the TCP transport.
**Where:** [GAME_DEVELOPMENT.md](GAME_DEVELOPMENT.md), [LSP.md](LSP.md), [DEFERRED def-294](DEFERRED.md#def-294), `src/scaffold/jc_scaffold.c`

<a id="dec-2026-10-01-desktop"></a>
#### 2026-10-01 — The desktop application is postponed…
**Postponed by the operator on 2026-10-01** ("No. Postpone the desktop app."). The design ([proposals/2026-09-desktop-gui.md](proposals/2026-09-desktop-gui.md)) and its checklist stay proposals: a separate program, in its own repository, that would start `jichi --acp` as a child process and offer chat with approvals, setup, projects and sessions, and the documentation and source beside the chat, for people who do not live in a terminal. The operator gave no reason beyond the decision, and none is invented here.
**Not taken:** **building it now**, through the whole checklist on three platforms; **building it in stages to an evidence gate** -- the review's recommendation: jichi's ACP contract first (D2, D3, D4, D6, D7 and the JSON gaps N1 and N2 of the design's §9), then the checklist's Phase 0-1, a Linux-only prototype and the design's own five-person test (§10); and **deciding never to build it**.
**What stays recorded:** the design's §9 lists what jichi would have to change for a graphical or editor client, and this decision schedules none of it. D5 was decided separately and is first in the fix order ([DEFERRED def-285](DEFERRED.md#def-285)); D1's documentation is corrected. §10 keeps the stop rules, and the checklist's Phase 1 the decisions a builder must record first.
**Revisit when** the operator takes it up again.
**Where:** [the design](proposals/2026-09-desktop-gui.md), [the checklist](proposals/2026-09-desktop-gui-checklist.md), [DEFERRED def-297](DEFERRED.md#def-297)

## Documentation and teaching

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **Plain language is a separate page, never a simplification** (M305) | [M305](#dec-m305) |
| **Plain-register assignments are graded as strictly as any other** (M309) | [M309](#dec-m309-3) |
| **New plain tasks, not rewrites of existing ones** (M309) | [M309](#dec-m309-2) |
| **The plain tier gets German editions ahead of the translation trigger** (M309b) | [M309b](#dec-m309b) |
| **A translated assignment's graded frontmatter is copied byte-for-byte** (M309b) | [M309b](#dec-m309b-2) |
| **Slide figures that grow are lower bounds** (M307) | [M307](#dec-m307) |
| **Prefer a lint to an audit** (M259) | [M259](#dec-m259) |
| **A lint must be quiet on the current tree by construction** (M295/M305) | [M295, M305](#dec-m295) |
| **Narrow the lint's scope explicitly** (M305) | [M305](#dec-m305-2) |
| **The built-in tool-output caps are NOT lowered** (M326z) | [M326z](#dec-m326z) |
| **jichi does not auto-bound a whole-file read** (M326z) | [M326z](#dec-m326z-2) |
| **The caps are not tightened automatically on a measured 0% cache hit-rate** (M326z) | [M326z](#dec-m326z-3) |
| **The cost page reports ONE workload and says so** (M326z) | [M326z](#dec-m326z-4) |
| **The mid-turn 80/60 constants stay as they are** (M326y) | [M326y](#dec-m326y) |
| **`unrelieved` is defined by EFFECT (still above the high-water), not by elision count** (M326y) | [M326y](#dec-m326y-2) |
| **`unrelieved` is NOT inferred on pre-M326y logs** (M326y) | [M326y](#dec-m326y-3) |
| **`short` implies `pressed`** (M326x) | [M326x](#dec-m326x) |
| **The telemetry reader understands both the old and new `compact` shapes** (M326x) | [M326x](#dec-m326x-2) |
| **Fix the instrument before tuning what it measures** (M326x) | [M326x](#dec-m326x-3) |
| **A cacheless backend is a WARN, never a `--unattended` failure** (M326w) | [M326w](#dec-m326w) |
| **The cache warning states the fixed-prefix size** (M326w) | [M326w](#dec-m326w-2) |
| **`doctor` and `telemetry --cache-audit` share one summing loop** (M326w) | [M326w](#dec-m326w-3) |
| **A pooled connection survives a failure that happened BEFORE it existed** (M326v) | [M326v](#dec-m326v) |
| **`timeouts.connect` defaults to 30 s, not 10 s** (M326v) | [M326v](#dec-m326v-2) |
| **A failed model call records `req_bytes`, not an estimated token count** (M326v) | [M326v](#dec-m326v-3) |
| **The migration hint speaks when BOTH paths exist, and refuses to suggest `mv`** (M326v) | [M326v](#dec-m326v-4) |
| **Only hook FAILURES are telemetered** (M326v) | [M326v](#dec-m326v-5) |
| **`JC_NO_CLOCK_GETTIME` is a NEGATIVE gate** (M326u) | [M326u](#dec-m326u) |
| **The documented libcurl floor is decoded from the `LIBCURL_VERSION_NUM` guards, not written by hand** (M326u) | [M326u](#dec-m326u-2) |
| **The suite sizes in the release banner are lower bounds, not exact counts** (M326t) | [M326t](#dec-m326t) |
| **The banner and `CURRICULUM.md` are checked against each other, not only against ground truth** (M326t) | [M326t](#dec-m326t-2) |
| **A lint's scrape is narrowed rather than the prose reworded, when the prose is the record of the failure** (M326t) | [M326t](#dec-m326t-3) |
| **Markdown is the taught default for a project's records** (M326s) | [M326s](#dec-m326s) |
| **The records pages are ungraded reading** (M326s) | [M326s](#dec-m326s-2) |
| **A tutorial's examples are executed, and an unclassified example block fails the build** (M326s) | [M326s](#dec-m326s-3) |
| **The measuring instrument holds no expected values** (M326s) | [M326s](#dec-m326s-4) |
| **The deferred register's structure is a lint; its reasons stay a human's job** (M657) | [M657, M463, M492](#dec-m657) |
| **The Japanese pages' register is formal, and that is a stated decision rather than a habit** (M657) | [M657](#dec-m657-2) |
| **A load-bearing word must be defined *and* routed to** (M657) | [M657, M499](#dec-m657-3) |
| **A reading chapter carries a diagram and code when it explains a MECHANISM; an argument, a pointer or a replayed trace carries neither — and there is no lint.** (M?) | [M?](#dec-m?-2) |
| **A change that shipped without a changelog line gets one in the release section it shipped in, marked as recorded late** (M713) | [M713](#dec-m713) |
| **Every diagram in words and as text; Mermaid only where it helps; no Graphviz** (2026-09-29) | [2026-09-29](#dec-2026-09-29-diagrams) |
| **`CLAUDE.md` opens with one line per rule, keyed to its moment; the reasons stay below** (M883) | [M883](#dec-m883) |

<a id="dec-m305"></a>
#### M305 — Plain language is a separate page, never a simplification
German (*Einfache Sprache*) is the original; English is its sibling.
**Rejected:** Simplifying the existing pages — their density is a feature for their readers, and flattening would serve neither group.
**Where:** [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md)

<a id="dec-m309-3"></a>
#### M309 — Plain-register assignments are graded as strictly as any other
**Rejected:** Softer marking for this audience — an easier grade would be a kindness that teaches nothing.
**Where:** [proposal](proposals/2026-08-attempt-rules-and-plain-assignments.md)

<a id="dec-m309-2"></a>
#### M309 — New plain tasks, not rewrites of existing ones
**Rejected:** Rewriting or forking each task into two variants — the variants would drift.
**Where:** same

<a id="dec-m309b"></a>
#### M309b — The plain tier gets German editions ahead of the translation…
a narrow exception, not a policy change.
**Rejected:** Waiting for the general trigger: *Einfache Sprache* is a defined **German** register whose German page is the *original* (M305), so withholding it from that audience inverts the policy's own purpose. Machine-translating the rest — nothing about the argument generalises.
**Where:** [proposal](proposals/2026-08-attempt-rules-and-plain-assignments.md) §Part 3

<a id="dec-m309b-2"></a>
#### M309b — A translated assignment's graded frontmatter is copied byte-for-byte
`verify`, `points`, `difficulty`, `phase`, `audience`, held by a lint.
**Rejected:** Localising `verify` with the rest of the page — it is a *shell command*, and translating it breaks grading silently, for the readers least able to tell a broken grader from their own mistake. Per-language fixtures — two sets for one exercise is a sync job nobody does, and the divergence surfaces as a mystery FAIL, not a diff.
**Where:** [assignment_i18n_lint.sh](../tests/smoke/assignment_i18n_lint.sh)

<a id="dec-m307"></a>
#### M307 — Slide figures that grow are lower bounds
"over 10,000", not "10,350".
**Rejected:** Exact live figures — a slide needing a re-edit on every commit is a slide that gets shown while wrong. Stamped measurements stay exact.
**Where:** ROADMAP M307

<a id="dec-m259"></a>
#### M259 — Prefer a lint to an audit
six namespaces now checked mechanically (flags, tool names, slash-command registry, `/command` mentions, subcommands, config keys + orphan pages).
**Rejected:** Re-reading the corpus — "the audit found what it knew to look for".
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md)

<a id="dec-m295"></a>
#### M295 — A lint must be quiet on the current tree by…
Prose collisions were **reworded**, not excepted.
**Rejected:** An exception list — every entry is a place the invariant no longer holds, recorded as though it did.
**Where:** ROADMAP M295

<a id="dec-m305-2"></a>
#### M305 — Narrow the lint's scope explicitly
config-key coverage checks top-level keys only.
**Rejected:** Half-covering a namespace while claiming the namespace.
**Where:** [config_keys_lint.sh](../tests/smoke/config_keys_lint.sh)

<a id="dec-m326z"></a>
#### M326z — The built-in tool-output caps are NOT lowered
They are a safety bound; a token budget is the operator's call.
**Rejected:** Shipping `--lite`'s numbers as the default — it would silently truncate reads for everyone, including users on a caching backend where the cost does not exist. A bound that tries to be both a crash guard and a budget serves neither.
**Where:** [TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) §7

<a id="dec-m326z-2"></a>
#### M326z — jichi does not auto-bound a whole-file read
**Rejected:** Refusing large reads, or injecting `limit` when the model omits it: the model asked for the file, and a silently partial answer to an explicit request produces a wrong conclusion two turns later. The cap already truncates *with a notice* — the honest form of the same idea.
**Where:** same

<a id="dec-m326z-3"></a>
#### M326z — The caps are not tightened automatically on a measured 0%…
even though jichi now knows it.
**Rejected:** Adapting them at runtime: tool behaviour would change mid-session based on a statistic — a read returning 200 KB yesterday and 32 KB today, for reasons invisible in the config. `doctor` advises instead.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m326z-4"></a>
#### M326z — The cost page reports ONE workload and says so
**Rejected:** Presenting the figures as general: the concentration and the compaction decay are structural and should reproduce, but a 72% re-read rate is a fact about one agent on one codebase.
**Where:** [TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md)

<a id="dec-m326y"></a>
#### M326y — The mid-turn 80/60 constants stay as they are
**Rejected:** Lowering the high-water: measured reclaim decays to ~0 by the third pass in a turn, so firing earlier buys one round while making the useless passes *more* frequent. Lowering the target: it asks a pass to try harder against material that no longer exists. Also rejected: lowering `ELIDE_MIN_BYTES` — 86% of tool-output bytes are already above it, and 200 would reach 98% for 2.2× the churn.
**Where:** ROADMAP M326y

<a id="dec-m326y-2"></a>
#### M326y — unrelieved is defined by EFFECT (still above the high-water), not…
**Rejected:** Counting elisions — tried first, and it did not fire on real data: the repeating passes do elide something, usually one small item, and reclaim nothing. Counting what was taken cannot express achieving nothing.
**Where:** [jc_compact.h](../include/jc_compact.h)

<a id="dec-m326y-3"></a>
#### M326y — unrelieved is NOT inferred on pre-M326y logs
unlike `pressed`, which is.
**Rejected:** Guessing it from elision counts: it is a fact about the estimate *after* the pass, and a log without `before`/`after` does not contain it. `pressed` is inferable because the lossy trims provably run only past the high-water; this is not.
**Where:** [jc_telemetry.c](../src/util/jc_telemetry.c)

<a id="dec-m326x"></a>
#### M326x — short implies pressed
a compaction that never crossed the high-water had no target to miss.
**Rejected:** Reporting `!reached` alone — it made every routine dedup look like an emergency and drove a summary line that was false in 19 of 19 cases.
**Where:** ROADMAP M326x

<a id="dec-m326x-2"></a>
#### M326x — The telemetry reader understands both the old and new compact…
trust `pressed` when present, else infer pressure from `age`+`args` > 0, scoped to mid-turn.
**Rejected:** Counting raw `short` — the operator has 40 MB of pre-fix logs whose false positives would persist. Also rejected: an unscoped inference, which counts a between-turn compaction (it also carries `age`) as pressure — caught by its own unit test, which is why the fixture keeps one.
**Where:** [jc_telemetry.c](../src/util/jc_telemetry.c)

<a id="dec-m326x-3"></a>
#### M326x — Fix the instrument before tuning what it measures
The 80/60 mid-turn constants are deliberately untouched.
**Rejected:** Tuning now: the thrash is real (97 pressured passes in one turn), but choosing better behaviour under pressure needs a metric that can distinguish a pressured pass from routine housekeeping — which it could not.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md) fm. 1

<a id="dec-m326w"></a>
#### M326w — A cacheless backend is a WARN, never a --unattended failure
**Rejected:** Escalating it: a supervisor gating on the exit code would refuse to run against a perfectly working endpoint. `--unattended` exists for posture; token efficiency is not posture. Follows M316.
**Where:** ROADMAP M326w

<a id="dec-m326w-2"></a>
#### M326w — The cache warning states the fixed-prefix size
via the pure `jc_cacheaudit_prefix`.
**Rejected:** A bare "not caching" line: true, and unactionable — the reader cannot tell whether it costs 1% or 14% of their spend.
**Where:** [jc_cacheaudit.h](../include/jc_cacheaudit.h)

<a id="dec-m326w-3"></a>
#### M326w — doctor and telemetry --cache-audit share one summing loop
**Rejected:** A second loop in `main.c` — that is precisely how two reports on one thing come to show two totals, which M310–M313 each had to fix. The smoke driver compares the two surfaces to each other rather than to a constant.
**Where:** [doctor_cache.sh](../tests/smoke/doctor_cache.sh)

<a id="dec-m326v"></a>
#### M326v — A pooled connection survives a failure that happened BEFORE it…
**Rejected:** `rc == CURLE_OK` as the reuse test — simple, and it discarded the DNS/TLS caches on exactly the failures whose retry needed them, forcing a cold handshake to repeat the one that had just timed out. Also rejected: keeping the handle on every failure — a mid-stream error may leave the socket unusable, which the original comment was right about.
**Where:** [jc_http.h](../include/jc_http.h)

<a id="dec-m326v-2"></a>
#### M326v — timeouts.connect defaults to 30 s, not 10 s
**Rejected:** Keeping 10 s: measured, a shared endpoint exceeded it on 15–34% of calls for five days. The cost is stated rather than hidden — an unreachable endpoint now takes 30 s to report, and M321's message already names the knob.
**Where:** ROADMAP M326v

<a id="dec-m326v-3"></a>
#### M326v — A failed model call records req_bytes, not an estimated token…
**Rejected:** Estimating tokens: bytes are the one figure known exactly at the moment of sending, and the estimate is the thing calibration exists to correct. `in_tok` stays 0 on failure — they are different facts, one measured on the way out and one only available on the way back.
**Where:** [telemetry_failure.sh](../tests/smoke/telemetry_failure.sh)

<a id="dec-m326v-4"></a>
#### M326v — The migration hint speaks when BOTH paths exist, and refuses…
**Rejected:** Firing only on `old && !new` — that is silent from the first run onward, because jichi creates the new path itself, and the silence ends with the old tree buried one level deeper. Also rejected: migrating automatically (M170's standing decision — detect and instruct), and advising `mv -T`, which is GNU-only.
**Where:** [MIGRATION.md](MIGRATION.md)

<a id="dec-m326v-5"></a>
#### M326v — Only hook FAILURES are telemetered
**Rejected:** An event per hook fire: `PreToolUse` runs on every tool call, so a healthy hook would drown the log in lines nobody reads. A timeout is the cost nobody can otherwise see.
**Where:** [HOOKS.md](HOOKS.md)

<a id="dec-m326u"></a>
#### M326u — JC_NO_CLOCK_GETTIME is a NEGATIVE gate
unlike the positive `JC_HAVE_VSNPRINTF`/`JC_HAVE_MALLOC_TRIM` beside it.
**Rejected:** A positive `JC_HAVE_CLOCK_GETTIME`: consistent, and it would make anyone compiling by hand — without this Makefile — silently drop every latency measurement to one-second resolution. Absence of the flag must preserve today's behaviour; the flag is set only when the probe has *proved* the symbol unreachable.
**Where:** Makefile, `src/platform/jc_platform_posix.c`

<a id="dec-m326u-2"></a>
#### M326u — The documented libcurl floor is decoded from the LIBCURL_VERSION_NUM guards,…
**Rejected:** Trusting a dotted version in prose: the guards are the ground truth, and a doc naming a *different* version is worse than one naming none.
**Where:** [portability_lint.sh](../tests/smoke/portability_lint.sh)

<a id="dec-m326t"></a>
#### M326t — The suite sizes in the release banner are lower bounds,…
"over 10,000 unit checks", with the dated measurement beside it.
**Rejected:** Exact figures — they are the ones that rotted, because an exact number must be revised to stay true and a bound does not. Also rejected: linting the unit count, which would mean running `./run_tests` (6.5 s) from a docs lint, on a binary the smoke tier does not guarantee is built. Extends M307's slide rule to the ROADMAP.
**Where:** ROADMAP M326t

<a id="dec-m326t-2"></a>
#### M326t — The banner and CURRICULUM.md are checked against each other, not…
**Rejected:** Checking each page alone: that is precisely the state that persisted for 30 milestones — 74/46 in one file, 77/55 in the other, each looking maintained. A cross-check is what sees the disagreement.
**Where:** [docs_counts_lint.sh](../tests/smoke/docs_counts_lint.sh)

<a id="dec-m326t-3"></a>
#### M326t — A lint's scrape is narrowed rather than the prose reworded,…
The banner narrates the old wrong figures on purpose.
**Rejected:** Rewording the sentence to dodge the scrape — that deletes the account of what went wrong in order to make a test easier, which inverts M295's rule (reword a prose *collision*; do not edit away *evidence*).
**Where:** ROADMAP M326t

<a id="dec-m326s"></a>
#### M326s — Markdown is the taught default for a project's records
because Emacs costs ~119 MB against nano's 836 KB, not because org is unavailable (it has shipped *inside* Emacs since 2006).
**Rejected:** Teaching org first — a project arguing it compiles on old and small machines cannot require the largest editor on the shelf. Interleaving both formats in one page — the practice would read as a comparison rather than a practice. Shipping `.org` scaffolds — that drives users to one editor.
**Where:** ROADMAP M326s

<a id="dec-m326s-2"></a>
#### M326s — The records pages are ungraded reading
beside `READING_OPEN_SOURCE.md` rather than as a graded task.
**Rejected:** A `74-your-own-registers` assignment — designed, then dropped: its own checker admitted it could not tell whether a decision was real, the dates true, or `Where:` a real path. It graded *shape*, while a register is a habit sustained over months, and it carried the most coupling in the plan (two count bumps, a grader entry, an INDEX row). Putting them in `docs/reading/` — that genre is ANNAI/FUKABORI, guided tour and deep dive **through jichi's own source**.
**Where:** same

<a id="dec-m326s-3"></a>
#### M326s — A tutorial's examples are executed, and an unclassified example block…
`fragment` marks a block the author looked at and chose not to run.
**Rejected:** Trusting the prose — a tutorial whose commands were never run rots silently, and the reader concludes the practice is broken rather than the page. A silent opt-out — then "unchecked" and "checked" are indistinguishable.
**Where:** [project_records_lint.sh](../tests/smoke/project_records_lint.sh)

<a id="dec-m326s-4"></a>
#### M326s — The measuring instrument holds no expected values
`org-checks.el` prints `KEY=VALUE`; the driver scrapes the expectations from the page's **visible prose**.
**Rejected:** Expected values in the Elisp, or in a marker comment — either lets the sentence a reader actually reads drift while the test stays green, which is the whole failure the driver exists to prevent.
**Where:** [org_mode_lint.sh](../tests/smoke/org_mode_lint.sh)

<a id="dec-m657"></a>
#### M657 — The deferred register's structure is a lint; its reasons stay…
Three hand-audits of the same invariant — M463 (open headings whose rows had all closed), M492 (four closed rows under an open heading, closure appended behind a present-tense reason), M657 (six struck rows, one empty heading, and illumos missing from the page entirely) — is past this project's threshold for preferring a lint to an audit. `deferred_register_lint.sh` checks four structural invariants and, deliberately, **no reason**.
**Rejected:** **A staleness lint over the reasons**: a reason may hold judgement, evidence or an unchecked factual claim, and only the third is mechanically decidable — one claim at a time, by going and looking (the M326b rule). A lint that tried would encode today's answers and rot exactly like the rows it policed. Also rejected: **checking only this page**. Check 4 enumerates the never-compiled platform set from `PLATFORMS.md`, which owns platform verdicts, because the failure that cost most was a row that existed in three other documents and not in this one.
**Where:** ROADMAP M657, [DEFERRED.md](DEFERRED.md)

<a id="dec-m657-2"></a>
#### M657 — The Japanese pages' register is formal, and that is a…
The first native Japanese-speaking reviewer this project has had reported the pages read *"formal, and not casual"*; the register was never argued for, it is what a translator reaches for. It is now written down in [i18n/README.md](i18n/README.md) §Policy item 4, with the argument on both sides and the per-genre question — reference formal, learner-facing closer to the English's directness — left open **for her**.
**Rejected:** **Changing the register on a first impression**: the review is explicitly unfinished, nobody here can judge Japanese register, and a local model least of all (M651 measured five of them failing this page set in *meaning*, before style was in question). Also rejected: **leaving it unstated** — an unexamined habit gives a reviewer nothing to disagree with, which is how it survived four pages and five decks.
**Where:** ROADMAP M657, [i18n/README.md](i18n/README.md)

<a id="dec-m657-3"></a>
#### M657 — A load-bearing word must be defined and routed to
`self_learner_lint` check 4 had held VOCABULARY.md to **defining** 24 required words since M499, and it was green throughout — while the page was linked from **0 of 10 tutorials** and 0 of 14 curriculum module pages. Defining a word and putting a reader in front of the definition are different guarantees; only the first was checked. `docs_locators_lint` check 8 now pins the second.
**Rejected:** **Trusting the definition check**: it measures the page, not the path to it — "a policy nobody is routed to is a policy nobody applies" (M510) applies to a glossary exactly as it does to a rule. Also rejected: **one check over tutorials and curriculum modules together** — two populations with two navigation conventions under one check is how a green stops meaning either. The module pages are measured and named, not folded in.
**Where:** ROADMAP M657, [VOCABULARY.md](VOCABULARY.md)

<a id="dec-m?-2"></a>
#### M? — A reading chapter carries a diagram and code when it…
Measured 2026-09-21 across all 27 chapters: Annai 9 of 11 with a diagram, Fukabori 10 of 12, **Tsuiseki 0 of 4**. The four missing in Annai and Fukabori are *the same four* that sit at one code block — Annai's `10-where-to-go-next` and `a-no-bench`, Fukabori's `01-why-c89` and `11-ai-supported-coding-examined` — and every one of them argues or points rather than explaining a mechanism. Tsuiseki is a whole guide whose form is a byte-for-byte replayed run: the trace *is* the artifact. So the exception set is structured by kind, not a list of stragglers.
**Rejected:** **A lint requiring a diagram per chapter, with an allowlist.** It would encode 8 exceptions across 27 files and check 19 files for a property they already have — the "checks almost nothing and reports success" shape this project's lints exist to avoid, and the same call made when a quote lint over `docs/` was dropped after counting 1 verbatim quote in 110 blocks. **Also rejected: adding a diagram or a code block to the four**, because a diagram restating prose is decoration and `UML_TUTORIAL.md`'s own rule is *one diagram, one question*. **Revisit if** the exception set grows *by kind* — a mechanism chapter without a diagram would be drift; another argument chapter would not.
**Where:** `docs/reading/`, DEFERRED's two closed rows

<a id="dec-m713"></a>
#### M713 — A change that shipped without a changelog line gets one…
`1d31473d` — the constraint scanner reading "do not compile" in a description as an order — was published in 0.10.0 with no entry; the 0.10.0 section now carries it, with a sentence saying when and why it was added.
**Rejected:** Listing it under `[Unreleased]` — it would read as not yet released, the reverse of the truth. Leaving the released section as it is — a changelog that omits a shipped behaviour change misleads exactly the reader deciding whether to upgrade, and `changelog_coverage_lint` deliberately cannot catch a single omission.
**Where:** [analysis](analysis/2026-09-23-what-to-build-next.md) §5.1

<a id="dec-2026-09-29-diagrams"></a>
#### 2026-09-29 — Every diagram in words and as text; Mermaid only where it helps…
**Decided by the operator on 2026-09-29, "for a presentation that works on every platform"** (recorded here on 2026-10-01; the rule itself is [DOC_REVIEW.md](DOC_REVIEW.md) §7). Every diagram comes in up to three forms: an **"In words" paragraph** that says everything the diagram shows, for a screen reader and for anyone who cannot read the picture; a **small text diagram** in a ```` ```text ```` block, which looks the same in a git preview, a terminal, `less`, a PDF made with pandoc and on a phone; and a **Mermaid block only where it is meaningful and readable**. **No Graphviz.** Mermaid is for the git preview only, so a page must be complete without it. Existing pages are brought to the rule as they are revised, not in one sweep.
**Rejected:** the operator considered **Mermaid alone**, **Graphviz** and **pre-rendered images**, and took none of them (the workbench handoff of 2026-09-29); §7 gives the purpose they were weighed against, quoted above. **Sweeping every existing diagram at once** -- the operator's own "not in one sweep".
**Measured on 2026-10-01:** 162 Mermaid blocks on 91 pages outside the translations, and no Graphviz; 155 of the Mermaid blocks have no text diagram within about 4 KB -- a rough proxy, which does not count "In words" paragraphs. Nearly every diagram predates the rule.
**To hold it** (decided 2026-10-01): a smoke lint whose count can only fall ([DEFERRED def-296](DEFERRED.md#def-296)) -- Mermaid diagrams without their text diagram may not increase, and Graphviz stays at zero -- so a new page cannot add a Mermaid-only diagram, and the old ones become a visible, shrinking number.
**Where:** [DOC_REVIEW.md](DOC_REVIEW.md) §7, [WORKBENCH.md](WORKBENCH.md) (the first page written this way), [DEFERRED def-296](DEFERRED.md#def-296)

<a id="dec-m883"></a>
#### M883 — `CLAUDE.md` opens with one line per rule, keyed to its moment…
**Decision** (the operator, 2026-10-02: "Both layers"). `CLAUDE.md` opens with `## Before you act`:
twelve rules of one to three lines, each keyed to the action at which it applies and naming the section
that says why. Five incident stories told in full elsewhere are cut to their rule and a pointer, and
`rules_core_lint` keeps the block first, short and pointing at what exists. The other layer is outside
this tree: a short file where the sessions start, loaded at every launch, which imports this one.
Rejected: **start the sessions in the repository** -- this file would load at launch and survive
compaction, but a session that spans repositories would still not see it, and the agent's memory is kept
per launch directory; **the workspace file only** -- jichi reads this file itself, for its own models
working here, and a rule buried as one bullet of 35 stays buried wherever the file is loaded; **move
every reason out** -- a rule kept without its reason is the one that gets argued away, and the budget
did not ask for it: the block fit after five cuts; **keep the rules keyed to labels** ("for measurement
runs") -- the incident was a run I had not given that label.
**What it costs:** a rule now lives in two places, its line and its section, which can drift apart.
Check 6 catches a pointer to a section that is gone, not a section that stopped saying why. The block
is 1,960 bytes; the cuts return 2,410, so the file and every system prompt jichi assembles here are
450 bytes shorter.
**Where:** ROADMAP M883, [CLAUDE.md](../CLAUDE.md), `tests/smoke/rules_core_lint.sh`,
[TESTING_RUNBOOK.md](TESTING_RUNBOOK.md) §3c, [ANECDOTES #120](ANECDOTES.md)

## Testing

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **A test that will not fail is deleted, not tuned** (M298) | [M298, M300, M304, M293](#dec-m298-4) |
| **State an invariant at the strength it actually has** (M312) | [M312](#dec-m312-4) |
| **An absence assertion is paired with a presence one** (M310) | [M310](#dec-m310-3) |
| **Transcript assertions must be anchored** (M296) | [M296](#dec-m296-2) |
| **Say when a red-before-green run is incomplete** (M305) | [M305](#dec-m305-3) |
| **A dynamic plan, not `t_skip`, when only *part* of a driver needs a missing tool** (M326s) | [M326s](#dec-m326s-5) |
| **A probe that mutates the buffer runs on a scratch copy** (M326s) | [M326s](#dec-m326s-2-2) |
| **Predict an inferred constraint, do not add a new inference** (M327) | [M327](#dec-m327) |
| **Exit 1 means "something would be adopted", not "error"** (M327) | [M327](#dec-m327-2) |
| **Document the invariant-vs-goal verifier split rather than fix it** (M?) | [M?](#dec-m?) |
| **Gate learn-on-stop on the envelope OUTCOME, not the loop's return** (M328) | [M328](#dec-m328) |
| **Guard at the metering chokepoint, not the call site** (M329) | [M329](#dec-m329) |
| **Warn on the FIRST post-outcome call, and surface it in `jichi runs`** (M329) | [M329](#dec-m329-2) |
| **Attribute the mentor's cost, do not defer the outcome** (M330) | [M330](#dec-m330) |
| **Prove a gate is satisfiable before spending a run on it** (M330) | [M330](#dec-m330-2) |
| **A gate check that claims to force a deliverable must be shown to fail without it.** (M330) | [M330](#dec-m330-3) |
| **Measure a workload with no `--budget-tokens` before budgeting it** (M?) | [M?](#dec-m?-4) |
| **Treat a green verifier as a claim about the assertions, not the code.** (M?) | [M?](#dec-m?-5) |
| **A deferral must name what it is deferring between.** (M?) | [M?](#dec-m?-6) |
| **Consistency across your own notes is not corroboration.** (M?) | [M?](#dec-m?-7) |
| **The repeat-failure measure reports the exact and the paraphrased form side by side, never merged** (M342) | [M342](#dec-m342-2) |
| **An experiment runner classes a run as errored by `stop_reason`, never by the presence of an `error` object** (M344) | [M344](#dec-m344) |
| **The did-you-mean closeness rule is one shared predicate** (M345) | [M345](#dec-m345) |
| **The working line's elapsed display stays byte-identical below one minute** (M346) | [M346](#dec-m346) |
| **The budget notice fires once per run, top-level only, with no off switch** (M347) | [M347](#dec-m347) |
| **Only the LOSSY elision writes claim tickets** (M348) | [M348](#dec-m348) |
| **Elision idempotence keys on the marker signature, not the size floor** (M348) | [M348](#dec-m348-2) |
| **Tickets are turn-lived** (M348) | [M348, M339](#dec-m348-3) |
| **The undo notice is one user-role note, injected only when a file actually reverted, and saved immediately** (M349) | [M349](#dec-m349) |
| **`/rewind` gets no note** (M349) | [M349](#dec-m349-2) |
| **In a count-and-continue test harness, a null guard is combined into every check that dereferences** (M349) | [M349](#dec-m349-3) |
| **Resume drift is judged against the session file's own mtime, strictly greater or gone** (M350) | [M350](#dec-m350) |
| **Believed paths are the four file tools' `path` arguments from SUCCESSFUL calls only** (M350) | [M350](#dec-m350-2) |
| **The drift note saves immediately, and idempotence is tested as a count of one, never an absence** (M350) | [M350](#dec-m350-3) |
| **The hollow-green notice injects at the periodic verify only; the completion site stays journal-only** (M351) | [M351](#dec-m351) |
| **The prompt's date line is bare ISO `%Y-%m-%d`, local time, in `append_env`, with no off-switch** (M352) | [M352](#dec-m352) |
| **The repair note is per call, not once per turn, and rides error results too** (M353) | [M353](#dec-m353) |
| **Each elision flavour carries its own reason, and the idempotence guard matches their shared prefix** (M354) | [M354](#dec-m354) |
| **The drive lead does one-line changes itself and dispatches only larger work: the bottleneck is verification, not typing** (2026-10-20) | [2026-10-20](#dec-2026-10-20-d1) |
| **The 30-minute commit rule gets a mechanical backstop: the harness copies uncommitted files into the run records outside git** (2026-10-20) | [2026-10-20](#dec-2026-10-20-d2) |
| **`make ci` gains one optimised, fortified, `-Werror` build stage so a warning class fails the gate, with a natural red-first** (2026-10-20) | [2026-10-20](#dec-2026-10-20-d7) |
| **The flight plan names armed budgets only, in the envelope's own units, top-level only** (M355) | [M355](#dec-m355) |
| **The self-gate for these passes is `make WERROR=1` with colon-anchored patterns** (M355) | [M355](#dec-m355-2) |
| **The spawn tool descriptions name trigger shapes AND counter-cases, at ~+70 tokens per full-profile call** (M356) | [M356](#dec-m356) |
| **`/investigate` ships in the DEFAULT pack, not a new orchestration pack** (M356) | [M356](#dec-m356-2) |
| **The orchestration tutorial is ungraded, and content checks on a conditional file need a presence flag** (M356) | [M356](#dec-m356-3) |
| **Telemetry is armed by seven presets (tester, learner, instructor, the verify journeys), metrics tier, and `beginner` still clears it** (M357) | [M357](#dec-m357) |
| **The example configs price every model entry with 0, and setup's exit summary names the pricing keys only when telemetry is armed** (M357) | [M357](#dec-m357-2) |
| **The tester's start script arms the envelope live (`--budget-tokens 400k --verify-every 8`), not commented out** (M357) | [M357](#dec-m357-3) |
| **The context-gauge prompt line states the CONFIGURED window only — absent when only the built-in default would apply** (M358) | [M358](#dec-m358) |
| **The [context] pressure note fires once per agent loop, at ANY depth, from the pressed pass's own numbers** (M358) | [M358](#dec-m358-2) |
| **The gauge driver anchors on captured request bodies, exactly-once in the LAST request** (M358) | [M358](#dec-m358-3) |
| **`ask_user` journals every outcome, but `runs` counts and renders only the UNANSWERED ones** (M359) | [M359](#dec-m359) |
| **The question is journaled bounded (240 bytes, UTF-8 boundary); the ANSWER text never is** (M359) | [M359](#dec-m359-2) |
| **A fence refusal lists the FENCE's tools (bounded at 10), never the registry's, and repeats the refused name with "will not become available"** (M360) | [M360](#dec-m360) |
| **No finite-state-machine framework: transitions stay pure decision tables with exhaustive tests, adopted where a transition bug has bitten** (M360) | [M360](#dec-m360-2) |
| **The exhaustion latch re-arms at an exact LENGTH computed from window geometry, is bounded to keep+1 appends, and NULL disables it** (M361) | [M361](#dec-m361) |
| **The deferred-register sweep records its checks IN the rows, closes only what its own triggers permit** (M361) | [M361](#dec-m361-2) |
| **The fast input echo is gated on accessible mode, ASCII-only, end-of-line-only, and bails one column before every boundary** (M362) | [M362](#dec-m362) |
| **The screen-reader half ships as a written protocol with a record-the-result rule, not as a claim** (M362) | [M362](#dec-m362-2) |
| **Pasted C0 controls (except newline and tab) and DEL are STRIPPED at the splice chokepoint; tabs are kept and made geometrically true** (M363) | [M363](#dec-m363) |
| **Multi-line submissions are excluded from line history at the hist_add chokepoint** (M363) | [M363](#dec-m363-2) |
| **The paste-cap overflow drains to the terminator instead of returning** (M363) | [M363](#dec-m363-3) |
| **The history validator runs at the loop chokepoint, not per mutator, and is detection-only** (M364) | [M364](#dec-m364) |
| **The validator's NON-checks are explicit, each verified in the code before being left out** (M364) | [M364](#dec-m364-2) |
| **The fit budget is held by a DEADBAND, not quantized; the rejected first design is stated** (M365) | [M365](#dec-m365) |
| **The prefix sentinel accuses only on 3 consecutive changed turns, fires once, and hashes the system prompt only** (M365) | [M365](#dec-m365-2) |
| **The event registries live in the docs tables and the lint checks BOTH directions with extraction floors** (M366) | [M366](#dec-m366) |
| **The field lists in the new rows were read from each emit site, never recalled** (M366) | [M366](#dec-m366-2) |
| **Session field fidelity is pinned at two levels: a value round-trip test per codec site, and a struct-scrape lint that fails on unknown fields** (M367) | [M367](#dec-m367) |
| **A unit test that SAVES owns its store and deletes what it wrote** (M367) | [M367](#dec-m367-2) |
| **Kill deadlines run on the monotonic clock, everywhere** (M368) | [M368](#dec-m368) |
| **Full-tier verification is by EXIT CODE; grep pipes over runner output are not a gate** (M368) | [M368](#dec-m368-2) |
| **The tag registry documents structural forms as a column, and the lint enforces one-render-file uniqueness for them** (M369) | [M369](#dec-m369) |
| **Operator log tags are registered but explicitly unstable** (M369) | [M369](#dec-m369-2) |
| **Flag coverage (docs_flags check 4) searches ALL of docs/ leniently, while the lie-checks keep their strict exclusions** (M370) | [M370](#dec-m370) |
| **Nested-key coverage probes are anchored (backticked or quoted tokens), and the check lives inside config_keys_lint as check 6** (M371) | [M371](#dec-m371) |
| **The keybinding lint maps bytes to chord names inside itself, and escape-sequence keys are out of scope v1** (M372) | [M372](#dec-m372) |
| **`search_code` is now path-fenced** (M383) | [M383, M324](#dec-m383) |
| **Output styles became the fourth validated asset kind, and the key tables got a lint** (M389) | [M389](#dec-m389) |
| **The unit tier got the orphan check its sibling smoke tier already had, and the per-file count baseline was rejected** (M390) | [M390](#dec-m390) |
| **Slide figures are bounds or dated stamps, never live exact counts** (M391) | [M391](#dec-m391) |
| **A capped `read_file` reports the FILE's line count, computed from bytes it already holds** (M594) | [M594](#dec-m594) |
| **`make install` verifies the build stamp by reading the generated header, not by asking git** (M593) | [M593, M586](#dec-m593) |
| **An aliased tool call is grouped by the tool that RAN, in the reader — not stamped canonical in the log** (M591) | [M591, M532](#dec-m591) |
| **No lint guards the "cJSON is original code" claim, and the population is the reason** (M587) | [M587, M497, M498, M171, M552, M580, M582](#dec-m587) |
| **The localized decks ARE linted after all — by a check that reads digits, not prose** (M582) | [M582, M391, M163](#dec-m582) |
| **The defaults lint checks the `--lite` branch too, and a lite-dependent default must disclose that it is one** (M393) | [M393](#dec-m393) |
| **Documented commands are linted by SHAPE, in fenced blocks only** (M394) | [M394](#dec-m394) |
| **One page owns every platform verdict, and "never compiled" is the phrase** (M400) | [M400](#dec-m400) |
| **A tree-wide `long long` lint, not a Darwin-branch one** (M400) | [M400](#dec-m400-2) |
| **`doctor` warns on an unverified host; it does not fail** (M400) | [M400](#dec-m400-3) |
| **A whole-line quoted sequence item is a scalar; the corpus was not reworded** (M409) | [M409](#dec-m409) |
| **TAINTED is a third verdict, not a FAIL, and only `attempt` hardens it** (M410) | [M410](#dec-m410) |
| **`--model` pins the run; the TUI's `/model` stays live-within-routing** (M411) | [M411](#dec-m411) |
| **The gate-can-fail proof is a command, not an authoring-prompt demand** (M412) | [M412](#dec-m412) |
| **Campaign protocol: models author assignment prose; the tutor writes the gate; `--expect-fail` notarises it** (M414) | [M414](#dec-m414) |
| **A TAINTED attempt records `passed:false`, and the goalpost insight fires at threshold one** (M415/M417) | [M415, M417](#dec-m415) |
| **The `output:` fallback writes *beside* an existing artifact, never over it** (M423) | [M423](#dec-m423) |
| **A narrow `env_budget_applies()` re-arms the budget at any depth, rather than loosening `env_active()`** (M431) | [M431](#dec-m431) |
| **A delegate over budget stops with `JC_OK` and its work intact; only the top level settles the run** (M431) | [M431](#dec-m431-2) |
| **The edit-scope globs are rendered by one shared function used by both the prompt and the refusal** (M431) | [M431](#dec-m431-3) |
| **`spawn_parallel`'s `is_error` stays "all children failed"; the doc is corrected instead** (M431) | [M431](#dec-m431-4) |
| **`describe`'s contract is checked by a lint over the emitters, not by review** (M431) | [M431](#dec-m431-5) |
| **The changelog lint bounds systemic drift and demands no per-milestone entry** (M431b) | [M431b](#dec-m431b) |
| **The M331 reframing lives in one shared renderer, and precedes the evidence** (M431b) | [M331, M431b](#dec-m331) |
| **The run id ships as a `run_start` EVENT plus `done.run`, not as a journal path** (M431c) | [M431c](#dec-m431c) |
| **The workspace lease warns by default, and is a FILE rather than a lock** (M431e) | [M431e](#dec-m431e) |
| **A hollow completion green buys one extra round, before banking and with a re-verify** (M431d) | [M431d](#dec-m431d) |
| **The budget panel ships OFF, as a flag to be measured against M347 rather than a default that overrules it** (M431f) | [M347, M431f](#dec-m347-2) |
| **The loop detector uses TWO keys with separate thresholds, classifies in the loop, and tells once per loop** (M432) | [M432](#dec-m432) |
| **`brief-check` attributes a constraint to its line by RE-SCANNING lines, and returns 0 rather than guessing** (M433) | [M433](#dec-m433) |
| **A subagent's prompt states what is ENFORCED on it, and nothing more** (M434) | [M434](#dec-m434) |
| **The goalpost note tells the model, and does not refuse the edit** (M435) | [M435](#dec-m435) |
| **A depth-gated tool is declared once on `struct jc_tool`, not checked in each tool body** (M436) | [M436](#dec-m436) |
| **The delegation report is a bounded summary the parent can act on, not the delegate's transcript** (M437) | [M437](#dec-m437) |
| **A poll-only control service point between tool calls, not the full boundary** (M438) | [M438](#dec-m438) |
| **The cost-model section is gated on the CONFIGURED cache setting and cannot see run state** (M440) | [M440](#dec-m440) |
| **`degraded` counts decisions taken in the operator's ABSENCE, and an `--auto` auto-approval is not one** (M443) | [M443](#dec-m443) |
| **The envelope's ARMING is split from its JOURNAL so an introspection command can arm one** (M444) | [M444](#dec-m444) |
| **The milestone-currency lint's ground truth is the highest `M###` cited by the OTHER top-level docs pages, not git** (M463) | [M463](#dec-m463) |
| **A variable for a `with_deadline` child is passed as `with_deadline N env NAME=value cmd`, and the prefix form is banned outright** (M465) | [M465](#dec-m465) |
| **The GNU word-boundary ban is narrowed to lines that also invoke grep/sed/awk** (M466) | [M466](#dec-m466-5) |
| **`JC_SMOKE_KEEP_GOING` defaults OFF; the tier stays fail-fast** (M466) | [M466](#dec-m466-2) |
| **A named driver that does not resolve is exit 2, never a silent skip** (M466) | [M466](#dec-m466-3) |
| **A rig ships `git archive HEAD` by default and `--dirty` ships the working tree, stamped NOT a commit** (M466) | [M466](#dec-m466-4) |
| **`ptydrive` tolerates an ambiguous zero-length read ONLY before the first byte, and only a bounded number of times** (M467) | [M467](#dec-m467) |
| **A backgrounded subshell in a driver must `exec`, enforced by a lint rather than judged per site** (M467) | [M467](#dec-m467-2) |
| **`JC_CHECK_NEAR` replaces exact float equality only at the NON-REPRESENTABLE sites** (M469) | [M469](#dec-m469-5) |
| **`tier-v-arch.sh` compile-probes every triple instead of trusting `zig targets`** (M469) | [M469](#dec-m469-2) |
| **The sweep links `HAVE_CURL=` and every row says so** (M469) | [M469](#dec-m469-3) |
| **MIPS's 73 failures are classified as the emulator's limitation, not a jichi defect** (M469) | [M469](#dec-m469-4) |
| **A terminal, not an integration** (M478) | [M478](#dec-m478) |
| **The JupyterHub rigs are an operator tier, outside `make ci`** (M478) | [M478](#dec-m478-2) |
| **Two wheelhouses, host and guest** (M478) | [M478](#dec-m478-3) |
| **`--negative-control` is a first-class mode of the verification rig** (M478) | [M478](#dec-m478-4) |
| **Accessible mode gets its OWN prompt string, and the sighted one is untouched** (M551) | [M551](#dec-m551) |
| **The spoken key list uses "y as in yes", and only in English** (M552) | [M552](#dec-m552) |
| **Line-buffered output stays accessible-only** (M581) | [M581, M549](#dec-m581) |
| **Prose chrome stays accessible-only; it does not become the default** (M561) | [M561](#dec-m561) |
| **`make ci` green must mean the same thing on every clean checkout: the gate is identity-independent** (M621) | [M621](#dec-m621) |
| **The newest-log pick is deterministic: ties on the whole-second mtime fall to the lexicographically greater name** (M622) | [M622](#dec-m622) |
| **Headless stdin stays a feature; the gate pins it instead** (M623) | [M623](#dec-m623) |
| **Toolchain gates in the grader tier probe usability, never existence** (M624) | [M624](#dec-m624) |
| **A staleness gate names its files; the tree-wide sweep was measured and dropped** (M645) | [M645](#dec-m645) |
| **A staleness check on a measured figure tests its UNIVERSE, not its drift** (M646) | [M646, M620](#dec-m646) |
| **A platform is re-run on a trigger, never on a schedule; and staleness is measured as coverage debt** (M647) | [M647](#dec-m647) |
| **A portability claim that reduces to a pure core is tested on the box you have** (M647) | [M647](#dec-m647-2) |
| **AHP is watched, not adopted; the bridge stays out of the tree** (M648) | [M648](#dec-m648) |
| **libcurl stays; jichi does not write an HTTP client** (M648) | [M648](#dec-m648-2) |
| **The population for a text tool-calling mode was measured before designing it** (M648) | [M648](#dec-m648-3) |
| **A named work gets a full citation; a named concept does not** (M650) | [M650](#dec-m650) |
| **A local model may draft a translation for a reviewer; it may not replace one** (M651) | [M651](#dec-m651) |
| **A cap that says nothing is a cap nobody can test** (M657) | [M657, M609, M438](#dec-m657-4) |
| **"Blocked" is a factual claim, so it gets checked like one** (M657) | [M657](#dec-m657-2-2) |
| **A platform's build requirements are probed, never keyed on `uname`** (M658) | [M658](#dec-m658-2) |
| **The course scaffold writes no config** (M675) | [M675](#dec-m675) |
| **An absent journal key means "nobody asked"; an empty one means "none"** (M684) | [M684, M662](#dec-m684) |
| **A capped turn is reported in the reach footer, not on stdout, and not by the exit code** (M687) | [M687, M73, M322](#dec-m687) |
| **One classifier for why a run stopped, guarded by `-Wswitch` rather than by review** (M688) | [M688, M687, M322](#dec-m688) |
| **jichi chooses no provider and no model — reversing M505** (M709) | [M505, M709](#dec-m505) |
| **A vendor's key goes to that vendor's own endpoint, and nowhere else unless the config names it — "strict", finishing M709** (M718) | [M709, M718, M505](#dec-m709) |
| **A missing dialect is refused where a model is CALLED, not where the program starts** (M718) | [M718](#dec-m718) |
| **`config validate` fails what jichi refuses to run, with doctor's own sentence, and nothing else** (M719) | [M719, M709, M718, M431](#dec-m719) |
| **A measurement's population is classified before it is counted, by what the model did, not where it ran** (M720) | [M720](#dec-m720) |
| **A shell's stderr is redirected once, for the whole shell (`exec 2>&1` first), and a command that does not fit the line is refused** (M721) | [M721, M258](#dec-m721) |
| **A warning flag a supported compiler may lack is probed, never mandatory, and the mandatory list is held to a measured baseline** (M722) | [M722, M472](#dec-m722) |
| **A header a libc may hide is probed, and the source includes the header POSIX names as a type's home** (M723) | [M723, M657c, M141](#dec-m723) |
| **The registry of key names to scrub grows, the popen prefix is sized exactly, and a command whose prefix cannot be built is refused** (M724) | [M724](#dec-m724) |
| **The JSON parser sizes each string by its own span, and the embeddings fast path planned as D3 was built, measured and not kept** (M725) | [M725](#dec-m725) |
| **A MiNT build defines its own 512 KB stack, in the platform layer** (M726) | [M726](#dec-m726) |
| **The path resolver follows links in a loop, and the gate runs the curl-free unit suite under MiNT's 512 KB stack** (M727) | [M727](#dec-m727) |
| **The unit suite removes fixtures through one helper that refuses anything not strictly below `$TMPDIR`, and refuses a `TMPDIR` longer than it was measured under** (M728) | [M728](#dec-m728) |
| **The gate runs the unit suite on a `TMPDIR` it cannot write and requires it to finish, and the null checks were converted where a machine could do it safely** (M729) | [M729](#dec-m729) |
| **D1's note resets on an edit, on a shell command for every call but itself, and never on compaction** (M733) | [M733, M432, M687](#dec-m733) |
| **An inferred constraint advises and never refuses; a negation reaches to the end of its sentence** (M734) | [M734, M110](#dec-m734) |
| **A kind of file is a scope, not a blanket object: "do not modify the code" infers no read-only rule** (M743) | [M743, M167, M168, M207, M734](#dec-m743) |

<a id="dec-m298-4"></a>
#### M298 — A test that will not fail is deleted, not tuned
four times this program (M298, M300, M304, and a fixture in M293).
**Rejected:** Shipping green checks that could not discriminate; each obstacle is recorded in the driver instead.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md) fm. 9

<a id="dec-m312-4"></a>
#### M312 — State an invariant at the strength it actually has
`sum(parts) == total` catches an append after the *last* mark only — a section inserted between two marks balances anyway. The zero-slot assertions close most of the gap; the rest is written down.
**Rejected:** Publishing the headline claim, which is what the design note said until the break was attempted and stayed green.
**Where:** [jc_sysmsg.h](../include/jc_sysmsg.h)

<a id="dec-m310-3"></a>
#### M310 — An absence assertion is paired with a presence one
and requires its capture to exist first.
**Rejected:** A bare "the fence dropped it" check — it passes when the capture is missing *or* the feature was never enabled. Exactly how M310's own driver sailed green over an 18-tool request that never contained the tool.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md) fm. 9

<a id="dec-m296-2"></a>
#### M296 — Transcript assertions must be anchored
on something unique to the surface under test.
**Rejected:** Bare greps — a PTY log holds every command's output, so an unanchored match tests the log, not the code.
**Where:** same

<a id="dec-m305-3"></a>
#### M305 — Say when a red-before-green run is incomplete
a key documented in two files needs both broken to prove the check.
**Rejected:** Claiming teeth from a partial break.
**Where:** ROADMAP M305

<a id="dec-m326s-5"></a>
#### M326s — A dynamic plan, not t_skip, when only part of a…
`org_mode_lint.sh` emits `1..12` with Emacs and `1..4` without.
**Rejected:** `t_skip` — it is all-or-nothing and would discard the four structural checks that need nothing but the page. A separate driver per tier — two files that must be kept in step, for one page.
**Where:** [org_mode_lint.sh](../tests/smoke/org_mode_lint.sh)

<a id="dec-m326s-2-2"></a>
#### M326s — A probe that mutates the buffer runs on a scratch…
The org cycle probe called `org-todo` seven times in place, and the state query after it counted the residue as a finding.
**Rejected:** Ordering the probes so the mutation comes last — an ordering constraint nobody can see is a trap for the next probe added.
**Where:** ROADMAP M326s

<a id="dec-m327"></a>
#### M327 — Predict an inferred constraint, do not add a new inference
`constraints scan` calls the same pure `jc_constraint_scan` the agent calls.
**Rejected:** A separate lint over brief prose. Written first, in Python, and it over-flagged three phrasings in a brief whose real run adopted nothing — a predictor that disagrees with what it predicts is a green light meaning nothing, so it was deleted rather than tuned.
**Where:** [CONSTRAINTS.md](CONSTRAINTS.md)

<a id="dec-m327-2"></a>
#### M327 — Exit 1 means "something would be adopted", not "error"
the polarity a supervisor gates a brief on, following `doctor`, where the exit code reports findings.
**Rejected:** Always exiting 0 and making the caller grep the output; or a `--strict` flag, which puts the default on the wrong side for the unattended case the feature exists for.
**Where:** same

<a id="dec-m?"></a>
#### M? — Document the invariant-vs-goal verifier split rather than fix it
(finding 10): with a goal gate, `--verify-baseline` warns on every correct run and `--verify-every` can never bank a checkpoint.
**Rejected:** Splitting `--verify` into two flags — doubles a surface every driving script uses and fails silently on the wrong choice. Inferring the kind from a red first run — reclassifies a genuinely broken tree as a goal, the exact case the baseline check exists to catch.
**Where:** [findings §10](analysis/2026-08-07-driving-zigodot-harness-findings.md)

<a id="dec-m328"></a>
#### M328 — Gate learn-on-stop on the envelope OUTCOME, not the loop's return
and say so when it skips.
**Rejected:** Leaving `st == JC_OK`, which is every terminal state -- the mentor then spends tokens outside a closed journal (measured: 625k past a 1m budget). Also rejected: skipping silently, which is indistinguishable from a broken feature.
**Where:** [ROADMAP M328](ROADMAP.md)

<a id="dec-m329"></a>
#### M329 — Guard at the metering chokepoint, not the call site
`jc_env_record_tokens` sees every model call at every depth, so it catches paths nobody has thought of.
**Rejected:** Checking in `stream_once`, which would have caught only the learn-on-stop path that motivated it.
**Where:** [ROADMAP M329](ROADMAP.md)

<a id="dec-m329-2"></a>
#### M329 — Warn on the FIRST post-outcome call, and surface it in…
as `post_outcome(totals_short)`.
**Rejected:** Warning at teardown (the process may not reach it) or once per call (noise). Warning on stderr only -- during an unattended run that is a warning nobody reads, which is how M328 stayed invisible.
**Where:** same

<a id="dec-m330"></a>
#### M330 — Attribute the mentor's cost, do not defer the outcome
a `learn_on_stop` journal event carries the token/tool-call deltas.
**Rejected:** Setting the envelope's outcome after learn-on-stop runs — the outcome answers whether the WORK succeeded, and M328's gate reads it to decide whether to run the mentor at all, so deferring it is circular.
**Where:** [ROADMAP M330](ROADMAP.md)

<a id="dec-m330-2"></a>
#### M330 — Prove a gate is satisfiable before spending a run on…
hand-complete or stub the task and watch it go green.
**Rejected:** Trusting a gate because its checks look strict. Two runs in one session spent ~3M tokens against gates that a correct change could not satisfy (a scope that forbade the required file; a grep for a string the cited convention would never produce).
**Where:** [ANECDOTES #38](ANECDOTES.md)

<a id="dec-m330-3"></a>
#### M330 — A gate check that claims to force a deliverable must…
**Rejected:** Assuming it does. M330's docs check ran a CONSISTENCY lint (banner vs newest entry), which holds trivially when nothing is added — a requirement that was never imposed.
**Where:** same

<a id="dec-m?-4"></a>
#### M? — Measure a workload with no --budget-tokens before budgeting it
and size from calls × what-the-task-reads.
**Rejected:** Estimating from the change's size (a 15-line change measured 2,009,021 tokens over 56 calls), or copying a previous run's consumption — circular when that run was itself truncated by a guess. ~3M tokens went that way in one session.
**Where:** [AUTONOMY.md](AUTONOMY.md) § Budgets, [ANECDOTES #39](ANECDOTES.md)

<a id="dec-m?-5"></a>
#### M? — Treat a green verifier as a claim about the assertions,…
**Rejected:** Trusting `outcome: ok`. A run finished green having shipped an out-of-bounds slice, because the test asserted only `len > 0` and the failure path never ran.
**Where:** [ANECDOTES #39](ANECDOTES.md)

<a id="dec-m?-6"></a>
#### M? — A deferral must name what it is deferring between.
"We must decide X", with no options listed, is a research task wearing a decision's clothes — read instead of deferring.
**Rejected:** Deferring on the shape of the question. One such phrase was restated five times across code comments, an analysis doc and a milestone entry, justified deleting two fields, and dissolved on one `read_file`: the runtime already supplied what it claimed had to be chosen.
**Where:** [ANECDOTES #40](ANECDOTES.md)

<a id="dec-m?-7"></a>
#### M? — Consistency across your own notes is not corroboration.
**Rejected:** Treating five files that agree as five confirmations, when it is one claim copied forward.
**Where:** same

<a id="dec-m342-2"></a>
#### M342 — The repeat-failure measure reports the exact and the paraphrased form…
same (tool, target) twice, and same (tool, exit class) ≥3× over ≥2 targets.
**Rejected:** Widening the exact key until it caught the observed loop — the paraphrased form is weaker by construction (three different greps exiting 1 is ordinary work), so folding the two would launder a signal that needs its examples read into a count that gets quoted. Also rejected: leaving the exact key alone — it scored 0% over logs holding one loop 96 times, which is TEST_INTEGRITY failure mode 1 in a measurement script.
**Where:** [repeat_failures.py](../tests/measure/repeat_failures.py)

<a id="dec-m344"></a>
#### M344 — An experiment runner classes a run as errored by stop_reason,…
`verify_failed` carries one too, and in the gate-sentence experiment it is the *expected* terminal state.
**Rejected:** Keying on the object — it declared an empty sample over ten completed runs (failure mode 1, third instance this program). Also decided: a shell call that *runs* the gate is diagnosis and is counted apart from one that *edits* it — a single "mentions the path" counter had conflated them.
**Where:** [declare_gate_repeat.py](../tests/measure/declare_gate_repeat.py)

<a id="dec-m345"></a>
#### M345 — The did-you-mean closeness rule is one shared predicate
(M345, `jc_str_close_enough`): the model's tool-name suggester and the human's slash-command one consume the same clamp, and each surface suggests only from its own universe — the TUI from the completion table + custom commands + MCP prompts, headless from custom commands + MCP prompts alone.
**Rejected:** A second inline copy of the threshold (the drift M286/M313 class); suggesting TUI built-ins in headless (a false promise — `-p` cannot run them); including the easter eggs in the candidate pool (undiscoverable is the point, and a suggester that hints at `/tea` un-hides it); fuzzy-matching CLI *subcommands* too (a bare unknown positional is deliberately a prompt, not an error — nothing to hang a suggestion on without changing that contract).
**Where:** ROADMAP M345

<a id="dec-m346"></a>
#### M346 — The working line's elapsed display stays byte-identical below one minute
(M346, `jc_fmt_elapsed`): `%.1fs` exactly, then `2m 07s` / `1h 04m`, zero-padded, floored.
**Rejected:** Humanizing the sub-minute form too — the ticking decimal is the liveness signal on a line whose whole job is proving the process is alive, and exactness there means no PTY fixture can drift. Rounding instead of flooring — a stopwatch shows `1m 29s` until 90 arrives, and rounding up would claim time that has not passed. Unpadded seconds — `2m 7s`→`2m 17s` changes width mid-tick and the line shimmers. Reusing `jc_reltime` — "2h ago" is a *coarse past* for tables; a live counter needs both units.
**Where:** ROADMAP M346

<a id="dec-m347"></a>
#### M347 — The budget notice fires once per run, top-level only, with…
one `[envelope]` user message at the first four-fifths crossing of any armed budget, at the steering boundary.
**Rejected:** A reminder per round or a countdown — M323's lesson (1,038 warnings from one unthrottled condition), and a nag a model learns to skip is worse than one clear ask. Injecting into subagent histories — their iteration budget is tapered by M62 and their history is their own; the run-level ask belongs in the main conversation. A config knob to disable it — the line only informs the model of caps the operator armed (M300's precedent: turning off prose guidance must not turn off a safety courtesy); if evidence ever shows a model performing worse when told, that is a measurement to run, not a flag to pre-ship. Changing behaviour at the cap (auto-summarize, refuse calls) — the notice is advice, and M80 already decides what happens at exhaustion.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m348"></a>
#### M348 — Only the LOSSY elision writes claim tickets
the superseded-read dedup stays ticketless.
**Rejected:** Ticketing both — a superseded read is zero-loss by construction (the newest read of the same path/range is still in context), so its ticket would duplicate the store for content nobody lost. Also decided: the ticket holds the FULL original (M339's exact promise, one convention), not just the elided middle — "the complete result is at PATH" survives the model reasoning about offsets; and the trim pass takes the writer as a callback so it stays pure and the unit tests stub it.
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m348-2"></a>
#### M348 — Elision idempotence keys on the marker signature, not the size…
**Rejected:** Keeping the 800-byte floor as the guard — it held only while the marker was ~70 bytes; a ticketed marker carries a path, and a deep enough `$HOME` pushes the elided message back over the floor, whereupon a second pass would elide the marker itself and preserve a ticket pointing at a ticket. A knife-edge that a *config-independent* constant cannot defuse is replaced by an explicit `strstr`, unit-tested with a 300-byte fake path.
**Where:** [jc_compact.c](../src/chat/jc_compact.c)

<a id="dec-m348-3"></a>
#### M348 — Tickets are turn-lived
(M348, inherited from M339's store): `jc_toolout_cleanup` removes the per-run directory at teardown, and the driver asserts the cleanup rather than working around it.
**Rejected:** Persisting tickets across runs — the store would grow without bound, and a stale ticket naming content from a previous run is worse than none. The driver's first version asserted post-exit existence and failed; the honest fix was observing mid-run (a PostToolUse hook snapshot), which is the view the model actually has.
**Where:** [elide_ticket.sh](../tests/smoke/elide_ticket.sh)

<a id="dec-m349"></a>
#### M349 — The undo notice is one user-role note, injected only when…
**Rejected:** Injecting on every `/undo` — an undo that changed nothing made nothing stale, and a note about it is noise. Rewriting or pruning the stale tool results instead — mutating past messages breaks the conversation's honesty (and the cached prefix); a dated correction beside the record is how this project fixes its own registers, applied to the model's. Deferring the save to the next turn — the user may `/exit` first, and a resume would then revive the stale beliefs without the correction.
**Where:** [SNAPSHOTS.md](SNAPSHOTS.md)

<a id="dec-m349-2"></a>
#### M349 — /rewind gets no note
**Rejected:** Noting it like `/undo` — rewind truncates the conversation back past the work, so the beliefs leave with the edits; a note would describe files the conversation no longer claims anything about. The asymmetry is the M34c split itself: `/undo` keeps the half that goes stale.
**Where:** [REWIND.md](REWIND.md)

<a id="dec-m349-3"></a>
#### M349 — In a count-and-continue test harness, a null guard is combined…
`JC_CHECK(p != NULL && strstr(p, ...))`.
**Rejected:** A separate `JC_CHECK(p != NULL)` line — it counts the failure and CONTINUES, so the next check dereferences NULL and the teeth run segfaults instead of counting (observed on this milestone's own teeth). The M343 tests had the right idiom already; now it is a stated rule rather than a habit.
**Where:** ROADMAP M349

<a id="dec-m350"></a>
#### M350 — Resume drift is judged against the session file's own mtime,…
**Rejected:** A stored per-file mtime map in the session (new format surface for a fact the filesystem already holds); comparing against the last checkpoint (snapshots can be off, and the shadow repo's head moves per turn, not per save); greater-or-equal (the agent's own last-turn edits land in the same second as the save that follows them, and a tie must not cry drift). A vanished file counts as drift — maximal staleness, not a special case.
**Where:** [SCRIPTING.md](SCRIPTING.md)

<a id="dec-m350-2"></a>
#### M350 — Believed paths are the four file tools' path arguments from…
matched to their calls backwards from each result.
**Rejected:** Including errored calls (a failed write left no belief worth checking — and a path that never existed would count as "gone" forever); attributing `search_code`/`list_files` results to files (their arguments do not name what they matched — the limit is stated in the docs rather than half-covered); a forward id scan (a tool_call_id is unique only within one provider response — M315's rule, reapplied).
**Where:** [jc_session.c](../src/session/jc_session.c)

<a id="dec-m350-3"></a>
#### M350 — The drift note saves immediately, and idempotence is tested as…
**Rejected:** Deferring the save (a second resume would re-detect and duplicate the note); asserting run 3 "contains no [resume]" — run 2's note legitimately LIVES in the resumed history, so the absence form would be red on correct behaviour and the M310 pairing inverts: presence-exactly-once is the honest assertion. ACP `session/load` stays untouched — the editor replays the transcript under its own replay contract, and injecting into it would surprise a surface jichi does not render.
**Where:** [resume_drift.sh](../tests/smoke/resume_drift.sh)

<a id="dec-m351"></a>
#### M351 — The hollow-green notice injects at the periodic verify only; the…
**Rejected:** Injecting at completion too — a completion green ends the run, so the note would be written to nobody and cost a history mutation on the way out; the journal already carries `sanity` for the post-mortem. Making the note change the outcome — M86 has been advisory since it shipped, and a false hollow-detection (a gate that legitimately prints no counts is already exempt) must not fail a good run. Telling the model on every hollow green — the M347/M323 once-per-run throttle; a bell that rings each round is a bell that stops meaning anything.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m352"></a>
#### M352 — The prompt's date line is bare ISO %Y-%m-%d, local time,…
**Rejected:** A weekday or month name — main.c deliberately sets LC_TIME from the user's locale (`timeFormat`), so `%A`/`%B` would make the canonical prompt vary by locale and every English-asserting driver flake abroad; the numeric form cannot. UTC — register dates are local dates, and a note dated "yesterday" at 00:30 local would be wrong in the sense that matters here. A second call site per builder — `append_env` is already the one function both prompts pass through, and the M312 accounting charges the line to ENV with no new mark. A config switch — a fact is not guidance (the M300/M347 line); the daily cache bust it costs is one per day against a prefix that is byte-stable within the day, which is the stability M31 needs.
**Where:** ROADMAP M352

<a id="dec-m353"></a>
#### M353 — The repair note is per call, not once per turn,…
**Rejected:** The M323/M347 once-per-run throttle — that governs run-level *warnings*; this is a fact about one result, like the exit status, and thirty repaired calls are thirty chances the conservative fix guessed wrong, each worth knowing at its call. Suppressing it on `is_error` results — the tool still ran on repaired input, and the error may itself be the repair's fault. Rewriting the history's broken argument string to the repaired form — the past stays as sent (the M349 rule); the note beside the result is the honest correction, and the Anthropic serializer's `_unparsed_arguments` degradation of the original is observed and left.
**Where:** [TELEMETRY.md](TELEMETRY.md)

<a id="dec-m354"></a>
#### M354 — Each elision flavour carries its own reason, and the idempotence…
pressure says "to fit the context window", the dedup says "superseded — a newer read appears LATER in this conversation".
**Rejected:** One marker for both — what shipped until M354, and for the eager dedup (budget 0, no pressure) it was a wrong-cause message, which M342 established is a loop amplifier: "cut for space" invites re-reading the file, while "the newer copy is below" ends the loop. Guarding idempotence on the full pressure text — a superseded-elided message could then be re-elided by the pressure pass (the M348 knife-edge, second edition); the guard now matches "bytes of tool output elided", common to every flavour. A claim ticket for the dedup — rejected already in M348: the pointer would duplicate the store, and "look below" IS the ticket.
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m355"></a>
#### M355 — The flight plan names armed budgets only, in the envelope's…
**Rejected:** Naming every budget with "unset" rows — the M347 armed-only rule: a limit that does not exist is not a fact about this run. Humanizing the deadline ("30m") — the envelope holds seconds and the model does arithmetic; a derived rendering can drift from the enforced value. Including subagent prompts — their budget is the M62 iteration taper, not the run envelope, and the parent paces the run (the M347 boundary, reused). Repeating the plan per turn as a user message — the system prompt is the cache-stable home for run constants, and the ENV slot already accounts for it under M312.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m355-2"></a>
#### M355 — The self-gate for these passes is make WERROR=1 with colon-anchored…
**Rejected:** Counting `grep -icE 'warning|error'` over a default build — it matches the literal `-Werror` in an echoed compile line (noise as signal) and printed ZERO while M352 shipped two implicit-declaration warnings (signal as nothing, across cached rebuilds). The zero-warnings invariant deserves the compiler as its enforcer, not a grep.
**Where:** ROADMAP M355

<a id="dec-m356"></a>
#### M356 — The spawn tool descriptions name trigger shapes AND counter-cases, at…
**Rejected:** Leaving the terse descriptions and steering with AGENTS.md alone — prose in the rules is advice read alongside twenty other lines, while the description travels WITH the capability into every request, and the record shows the description is what moved: Opus fanned out unprompted in the craft A/B on descriptions alone, and the hint tool went 0-for-24-runs when advertised without a trigger shape (M319/M320). A system-prompt orchestration section — charges every turn including turns with no delegation to make; the description charges the toolset that already includes the tools. The cost is falsifiable, not assumed: M314's paid-for-vs-called join measures the spawn call rate before and after.
**Where:** [SUBAGENTS.md](SUBAGENTS.md)

<a id="dec-m356-2"></a>
#### M356 — /investigate ships in the DEFAULT pack, not a new orchestration…
**Rejected:** A dedicated pack — a demonstration teaches only where it is installed, and the default pack is what a fresh `init` gets; the pack count stays 31 and every archetype pack inherits it by shipping the default files. Making it a skill — a command is human-triggered (the learner runs it and WATCHES the fan-out); a skill waits for the model to find it, which is the discoverability problem this milestone exists to fix. The counter-case line ("only ONE angle: skip the fan-out") is load-bearing and unit-pinned — a worked example without it teaches over-delegation.
**Where:** [SCAFFOLDING.md](SCAFFOLDING.md)

<a id="dec-m356-3"></a>
#### M356 — The orchestration tutorial is ungraded, and content checks on a…
**Rejected:** Grading the exercises — the M326s reasoning holds: a checker can grade the shape of a delegation, never the judgment of when; exercise 4 even instructs measuring one's own AGENTS.md line like the advice it is. On the teeth: asserting content inside `if (file matches)` blocks — removing the pack entry made the checks VANISH and the suite went green at a lower count (watched happening on this test's own first draft); the `saw_investigate` flag asserted after the loops is the M310 artifact-must-exist rule for tests whose subject can disappear.
**Where:** [TUTORIAL_ORCHESTRATION.md](TUTORIAL_ORCHESTRATION.md)

<a id="dec-m357"></a>
#### M357 — Telemetry is armed by seven presets (tester, learner, instructor, the…
**Rejected:** Arming ALL presets — developer/generic stay quiet by choice: telemetry earns its cost where the loop consumes it (fix-forward runs, the learner's record, verify-gated journeys), and the absences are unit-pinned so they read as decisions, not omissions. Asking harder interactively — the ask already exists and defaults to no; the preset is where a role's needs are encoded (the M48 design), and the accept-defaults shortcut must keep meaning "the preset knows". Arming `full` — that tier records prompt/response content; a default must never widen what is recorded, only whether counters are kept.
**Where:** [TELEMETRY.md](TELEMETRY.md)

<a id="dec-m357-2"></a>
#### M357 — The example configs price every model entry with 0, and…
**Rejected:** Asking for prices interactively — a number the user does not know at setup time produces a guessed lie in a config; a named key with an honest 0 produces an edit site. Real example prices — the examples point at local endpoints where 0 is true, and a stale vendor price teaches the wrong number with confidence. Showing the pricing row unconditionally — the summary's contract is relevance ("what you declined"); pricing bites exactly when cost rows are being collected, so it is gated on log_level, and the row text says WHY ("else costs read $0").
**Where:** [MODELS.md](MODELS.md)

<a id="dec-m357-3"></a>
#### M357 — The tester's start script arms the envelope live (--budget-tokens 400k…
**Rejected:** A commented example — the M319 lesson again: advertised machinery nobody meets; a commented flag is a flag with a zero call rate. No budget (status quo) — every measurement feature of M347/M355 plus the default run journal stays dark, and an unattended fix-forward run with NO ceiling is the case the envelope was built for. Also `--journal <path>`: rejected as redundant — the journal defaults on for any bounded run, outside the workspace (ANECDOTES #1). Script pins anchor the EXEC row's leading spaces, because the first teeth run showed the comment block satisfying a bare strstr.
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m358"></a>
#### M358 — The context-gauge prompt line states the CONFIGURED window only —…
**Rejected:** Stating `jc_compact_context_limit` unconditionally — that returns JC_COMPACT_DEFAULT_LIMIT (32000) when nothing is set, and the prompt would assert a guess as a fact about the model (the M355 armed-only rule; a wrong window number teaches the model to misjudge exactly the thing the gauge exists to judge). Deflating by the M77 calibration ratio — the configured value is the fact; a derived rendering can drift from what the trigger enforces. Skipping subagents — they run the same model's window, and append_env already serves both builders (the M352 date's route, reused).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m358-2"></a>
#### M358 — The [context] pressure note fires once per agent loop, at…
**Rejected:** Top-level-only like M347 — a budget is the parent's affair (M62 tapers a child's), but a window is each (sub)agent's own; the note goes into whichever history is under pressure. Once per session — after a between-turn compaction, renewed pressure is news, and old notes get folded into the summary rather than stacking. Recomputing the percentage at injection time — the note must not be able to disagree with the trigger that caused it, so it renders mrep.before/limit/reached verbatim. `reached` picks the message because "keep reading carefully" is wrong advice when eliding cannot cope — that case needs "write durable results now" (M342: the right-cause message ends the loop, the wrong-cause one amplifies it).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m358-3"></a>
#### M358 — The gauge driver anchors on captured request bodies, exactly-once in…
**Rejected:** Asserting absence in later requests — the note legitimately persists in history (the M350 count-not-absence rule). Trusting the telemetry event alone — noticed:true proves the intent, the dump proves the wire; the driver pins both plus the pre-pressure absence and the no-configured-limit absence pair (M310). Same-file reads for the pressure recipe — the eager dedup would relieve exactly the pressure the driver needs, so the subjects are eight DISTINCT files.
**Where:** [context_gauge.sh](../tests/smoke/context_gauge.sh)

<a id="dec-m359"></a>
#### M359 — ask_user journals every outcome, but runs counts and renders only…
**Rejected:** Counting all asks — an answered question already shaped the run through the history, and `unanswered=N` is the actionable review signal ("the model guessed at a decision it judged blocking"); diluting it with answered counts turns a triage flag into trivia. Not journaling the answered path at all — a reviewer diffing an attended vs unattended run of the same task wants both facts, and the flag costs one field. A telemetry event instead of the journal — the journal is per-run and exists exactly when a run is bounded, which is exactly when nobody was watching; `runs` is where its reviewer already looks (the M161 route, reused).
**Where:** [ASK.md](ASK.md)

<a id="dec-m359-2"></a>
#### M359 — The question is journaled bounded (240 bytes, UTF-8 boundary); the…
**Rejected:** Journaling the full question — journal rows are triage rows, not transcripts, and an unbounded model-authored string in every row is how a review surface becomes a second transcript. Journaling the answer — it reaches the history (and the session export) anyway, it is human-typed free text with no bound of its own, and the journal must stay lean enough to grep; `answered:true` carries the fact that matters. The tool's model-facing no-op text is deliberately unchanged — it was already honest, and M359's driver pins it through the wire for the first time rather than rewording it.
**Where:** [jc_tool_ask.c](../src/tools/jc_tool_ask.c)

<a id="dec-m360"></a>
#### M360 — A fence refusal lists the FENCE's tools (bounded at 10),…
**Rejected:** Listing the registry — the registry holds tools the fence exists to hide; naming them re-advertises what was fenced off. An unbounded list — a subagent fence is 3-8 names but a pathological one is not, and a refusal is a tool result the model re-reads every turn. Leaving the old text — "Tool not permitted by this agent's allowed-tools." states a cause with no way forward, the M342 class that amplifies retry loops, and under the core tool profile the TOP-LEVEL agent meets it; the cap is 10 so the whole core profile (8) always shows. The deny/read-only refusals got way-forward CLAUSES but no list: the deny verdict is per-tool (no allow-list exists at that site), and inventing one there would be a second source of truth.
**Where:** [AGENT_MODES.md](AGENT_MODES.md)

<a id="dec-m360-2"></a>
#### M360 — No finite-state-machine framework: transitions stay pure decision tables with exhaustive…
(M360 determination).
**Rejected:** A generic FSM engine for the agent loop — it would freeze stream→tools→verify→compact behind a table exactly where the loop's flexibility (hooks, notices, gates slotting between states) is the point, and C89 offers no dispatch sugar that pays for the indirection. Doing nothing — the precedents ARE FSMs and earn their keep: jc_perm_mode_rank exists because an enum's numeric order INVERTED a safety property (M304), verify_kind (M343) and budget_rollback_decision (M80) are transition functions with exhaustive tests. The rule is the idiom, applied on evidence of a transition bug, recorded here each time.
**Where:** [analysis/2026-08-10-guidance-and-crown.md](analysis/2026-08-10-guidance-and-crown.md)

<a id="dec-m361"></a>
#### M361 — The exhaustion latch re-arms at an exact LENGTH computed from…
**Rejected:** Re-arming on a "new large result" event — the deferral's own warned trap: the keep-recent window slides, so a result protected when it landed becomes eligible later with NO new event; an event-keyed re-arm skips it forever. An unconditional every-N-rounds skip counter — arbitrary where the geometry gives an exact answer, and wrong in both directions. Latching inside jc_app — the latch is per-(sub)turn state and the loop already owns exactly that scope (the M358 ctx_noticed route); a NULL latch keeps every direct caller's behavior byte-identical. Skipping the estimate too — rejected: `pressed` and the telemetry event must keep telling the truth about pressure while the work is skipped, or the latch would trade CPU for observability (`latched:true` marks the skips instead).
**Where:** [COMPACTION.md](COMPACTION.md)

<a id="dec-m361-2"></a>
#### M361 — The deferred-register sweep records its checks IN the rows, closes…
**Rejected:** Implementing the tempting rows regardless — the compaction design rows' triggers demand a pressured corpus and the sweep MEASURED that none exists locally (364 events, zero compact); building on no data is the M326b shape inverted. Deleting the undo-sliver row silently — it asked to be kept "one register cycle for visibility", so it closes BY that clause, recorded in the Closed table. Leaving the checks in the chat — a register whose rows carry their own check dates is the only register the next sweep can trust.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m362"></a>
#### M362 — The fast input echo is gated on accessible mode, ASCII-only,…
**Rejected:** Making it universal — it would benefit everyone (SSH latency included), but the wrap-aware redraw is the TUI's diciest machinery and the accessible gate bounds the blast radius to the mode that measurably needs it; universal is the natural follow-up once the VT-emulator suite covers the fast path. Fast-pathing UTF-8 — multi-byte input arrives byte-wise through the key loop, and mid-sequence column math is exactly the class of bug the full redraw exists to avoid; render() places completed sequences correctly. Handling the phantom last column inline — landing exactly on a column boundary needs render()'s CR/LF resolution, so the predicate refuses at `(prompt+line) % cols == 0` rather than reimplementing it.
**Where:** [ACCESSIBILITY.md](ACCESSIBILITY.md)

<a id="dec-m362-2"></a>
#### M362 — The screen-reader half ships as a written protocol with a…
**Rejected:** Claiming "works with screen readers" from byte-level pins alone — the pins prove what jichi emits, not what Orca or fenrir make of it, and this box has no screen reader and no verifiable audio; asserting the untested half would be the docs-claims failure TEST_INTEGRITY exists to prevent. Skipping the write-up until a reader is available — the user asked for exactly this artifact ("write down what is necessary"), and a protocol keyed to the driver's own checks means whoever runs it measures the same things the machine does. The ghost suggestion's visual-only nature is stated as a known gap in the protocol itself, not hidden.
**Where:** [ACCESSIBILITY.md](ACCESSIBILITY.md)

<a id="dec-m363"></a>
#### M363 — Pasted C0 controls (except newline and tab) and DEL are…
**Rejected:** Keeping controls verbatim — the editor re-emits the buffer per redraw, so a pasted ESC replays escape sequences into the terminal on every keystroke (output-side paste injection); fidelity for bytes whose only effect is terminal damage is not fidelity. Stripping tabs too — they are real content (Makefiles), and the honest fix was making the column math true (`jc_term_str_cols_from`, absolute-column tab stops) rather than mutating content to fit wrong math. Visualizing controls as `^X` — display-only mutation with a byte/column bookkeeping split the one-row editor does not carry; stripping is one rule, stated in the docs. Bytewise C1 stripping — those are UTF-8 continuation bytes; touching them corrupts multibyte text.
**Where:** [TUI_RENDER.md](TUI_RENDER.md)

<a id="dec-m363-2"></a>
#### M363 — Multi-line submissions are excluded from line history at the hist_add…
**Rejected:** Storing them and rendering newlines on recall — that is a multi-row editor, and pretending with one row stair-steps the screen (raw mode, OPOST off: bare LF, no CR). Space-joining or backslash-folding the entry — both mutate what a resubmit would send, and a history that lies about what it will submit is worse than one entry short. Storing per-row entries — arrow-up would replay a pasted block one fragment at a time, which nobody asked for. The guard lives in hist_add so both callers (submit path and jc_term_history_add) meet the same rule. Revisit if the editor ever learns rows.
**Where:** [jc_term.c](../src/tui/jc_term.c)

<a id="dec-m363-3"></a>
#### M363 — The paste-cap overflow drains to the terminator instead of returning
**Rejected:** Returning at the cap — the remainder, terminator included, stayed in the input queue and replayed as KEYSTROKES: pasted ESC ran as key escapes, a pasted "q" answered the next prompt; the exact injection bracketed paste exists to prevent, reintroduced at the overflow edge. Raising the cap — any finite cap has the same edge; the drain fixes the edge itself. Erroring the whole paste — 1MB of kept content is more honest than none, and the straddle-aware window trims the terminator bytes that landed inside the kept tail.
**Where:** [jc_term.c](../src/tui/jc_term.c)

<a id="dec-m364"></a>
#### M364 — The history validator runs at the loop chokepoint, not per…
**Rejected:** A check inside each of the 69 mutation sites — that repeats the exact per-site hand-care whose fragility motivated the checker, and the chokepoint (the settled history before each request) sees every mutator's work by construction. Refusing to build the request on violations — a degraded run becomes a dead one, and the serializers' placeholder-skip already tolerates the commonest shapes; the operator gets one loud WARN + events instead. A debug-build-only assert — corruption in production is exactly when detection matters, and the walk is linear metadata (no JSON parsing), cheap enough to always run.
**Where:** [CLAUDE.md](../CLAUDE.md)

<a id="dec-m364-2"></a>
#### M364 — The validator's NON-checks are explicit, each verified in the code…
**Rejected:** Flagging interior placeholder assistants — an empty model answer legally persists in history (M167 warns separately; both serializers skip them), so flagging teaches people to ignore the validator. Flagging consecutive user messages — coalesced tool results followed by an injected note is jichi's normal shape, shipped clean against the Messages API since M159. Exempting truncated assistants from the answered-exactly-once rule — unnecessary, because M334 still executes cut-off calls and error results land; the exemption would have weakened the rule on a guess the code disproves. A validator is a statement of the contract: what it deliberately allows is half the statement.
**Where:** [jc_message.c](../src/chat/jc_message.c)

<a id="dec-m365"></a>
#### M365 — The fit budget is held by a DEADBAND, not quantized;…
**Rejected:** Stateless quantization (1024-token steps, round down) — implemented first and disproven by its own arithmetic before shipping: a deflated budget sitting 39 tokens above a step boundary still flaps on 0.8% ratio jitter; quantization relocates edge-flapping, hysteresis removes it. Freezing the ratio at session start — the early session is exactly when calibration learns most, and freezing at 1.0 risks overflow on the first turns. A finer band — the band (limit/8 capped at 1024, floor 64) bounds the worst-case over-fit during a held drift-down, and that bound is stated in the code rather than implied.
**Where:** [PROMPT_CACHING.md](PROMPT_CACHING.md)

<a id="dec-m365-2"></a>
#### M365 — The prefix sentinel accuses only on 3 consecutive changed turns,…
**Rejected:** Warning on any change — a memory write, a mode switch, and midnight each legitimately bust the prefix once; a sentinel that cries at normal life teaches people to ignore it (the M364 non-check rule, applied to a detector). Auto-exculpating each legitimate cause with per-cause tracking — that rebuilds the M364 problem (state threaded through every writer) to suppress a warning that a streak requirement suppresses for free. Hashing the serialized tools too — the registry order is fixed at startup and tool schemas are static per session; noted as the follow-up if a dynamic schema ever varies. Comparing wire-level cached_tokens instead — that conflates server-side eviction and TTL with jichi's own churn; the hash isolates what jichi controls.
**Where:** [jc_prefix.c](../src/util/jc_prefix.c)

<a id="dec-m366"></a>
#### M366 — The event registries live in the docs tables and the…
**Rejected:** A C-side registry (an enum/table of event names) — it would make the emit sites honest but not the documentation, and the reader-facing tables are where a human learns what a log means; the docs ARE the registry, so the lint pins them. One-sided checking (emitted ⊆ documented only) — a ghost row misleads exactly like a missing one, and the two-sided form is what let a missed emit SHAPE surface (its documented row reads as never-emitted), so the lint polices its own scrape. Runtime enforcement (refuse unknown events at jc_eventlog_begin) — readers tolerate unknown events BY DESIGN (the M301 forward-compat contract for consumers); the registry problem is at write-and-document time, which is build time, which is a lint.
**Where:** [TELEMETRY.md](TELEMETRY.md)

<a id="dec-m366-2"></a>
#### M366 — The field lists in the new rows were read from…
**Rejected:** Writing the table from memory — this milestone's own survey missed `parallel_verify` because its emit is split across lines, and the lint caught the author's table on its first run; a registry built by recall would have shipped that hole silently. Documenting fields exhaustively per event in the lint too — field extraction across cJSON Add* spans is a heavier scrape with real false-positive surface; names first (the part that rots fastest), fields as table prose, revisit if a field-drift defect ever ships.
**Where:** [OBSERVABILITY.md](OBSERVABILITY.md)

<a id="dec-m367"></a>
#### M367 — Session field fidelity is pinned at two levels: a value…
**Rejected:** The lint alone — a mention-count cannot see a missing COPY: the fork hole existed while "truncated" had mentions elsewhere in the same file, so only a value-level assertion per site (save→load, and fork) proves the field travels. The test alone — it covers today's fields; the lint's unknown-field failure is what makes the NEXT field impossible to forget (teach the codec and the map, or the build is red). A sizeof-tripwire on the struct — platform-dependent padding makes it flaky where the name scrape is exact. Auto-carrying unknown fields generically — the codec is deliberately explicit (images are ephemeral BY DESIGN, marked in the codec's own comment), and an implicit carry would silently persist whatever a future field means, which is the failure class inverted.
**Where:** [session_fields_lint.sh](../tests/smoke/session_fields_lint.sh)

<a id="dec-m367-2"></a>
#### M367 — A unit test that SAVES owns its store and deletes…
**Rejected:** Sharing the suite's /tmp home — the first draft did, and its accumulated same-second files flipped test_session's recent-load mtime tie-break on the SECOND consecutive ./run_tests: green under make test, red standalone immediately after, an every-other-run flake. Fixing load_recent's tie-break instead — out of this milestone's scope and unnecessary once no test pollutes the shared store; noted as latent (ties resolve by directory order at 1-second mtime resolution). Rerun-stability is now part of the gate: the suite was run three times consecutively before commit.
**Where:** [test_session.c](../tests/test_session.c)

<a id="dec-m368"></a>
#### M368 — Kill deadlines run on the monotonic clock, everywhere
the envelope verify runner and jc_proc_capture moved from time(NULL) to jc_now_millis.
**Rejected:** Keeping wall time with wider margins — a backward clock step (VM guest time-sync, NTP) makes a wall deadline UNREACHABLE, not merely late, so no margin fixes it; the child runs to completion and the runner reports success where it should report TIMEOUT. Fixing only the site the flake exposed — jc_proc_capture's deadline bounds every run_terminal_command, user tool, and hook; leaving it was the same defect with a bigger blast radius. The control channel's 2s socket-read window keeps wall time deliberately: a step there lengthens a read, kills nothing.
**Where:** ROADMAP M368

<a id="dec-m368-2"></a>
#### M368 — Full-tier verification is by EXIT CODE; grep pipes over runner…
**Rejected:** The habit as it was — `make smoke \| grep -c '^not ok'` discards make's status and the anchored pattern misses the runner's prefixed replay lines; it read green over budget_notice for thirteen milestones while the M355 flight-plan sentence sat inside the driver's needle. Loosening the grep instead — any output-scrape gate rots with the output format; the exit code is the runner's contract. The needle fix follows M295: pin the notice's structural colon rather than reword the prompt sentence, because the flight plan NAMING the notice's tag is deliberate pedagogy.
**Where:** [BUILD.md](BUILD.md)

<a id="dec-m369"></a>
#### M369 — The tag registry documents structural forms as a column, and…
**Rejected:** A registry of bare tags only — M368's collision was never about the tag ([envelope] may legitimately appear in prose that teaches it); it was about the STRUCTURAL FORM (the colon variant a driver pins), so the form is what gets a column and a uniqueness check. Rewording the two prose brackets instead of listing them — `[json]` is real CLI optional-argument syntax and rewording it damages the usage convention; the M295 rule takes a two-row exception list WITH stated reasons over a lie of omission. Including src/scaffold — pack content is authored prose that defines its own conventions (the mentor's `[evidence:` request); linting it as runtime emission would force rows for teaching text.
**Where:** [NOTICES.md](NOTICES.md)

<a id="dec-m369-2"></a>
#### M369 — Operator log tags are registered but explicitly unstable
**Rejected:** Leaving them out — the lint's union coverage is what makes the extraction self-policing (an unregistered emission fails whether it is model-facing or not), and the table costs one line each. Promising their wording — stderr is not an interface (EMBEDDING.md); the page says so per group, so registering them cannot be read as stabilizing them.
**Where:** [NOTICES.md](NOTICES.md)

<a id="dec-m370"></a>
#### M370 — Flag coverage (docs_flags check 4) searches ALL of docs/ leniently,…
**Rejected:** One scope for both directions — the two failure modes are opposites: a doc MENTIONING a nonexistent flag misleads a reader wherever it appears except in registers whose purpose is recording rejected alternatives (the M326i reasoning), while a flag documented NOWHERE is a failure no matter which page would have carried it, so the coverage search accepts any mention including registers. Linting describe's key_flags for exhaustiveness — rejected as misreading the M301 contract: key_flags is CURATED ("core headless flags", ten of 139), and an exhaustiveness check would turn deliberate curation into permanent red. The parsed set comes from the strcmp() dispatch, not string literals, so help-text mentions cannot inflate it.
**Where:** [docs_flags.sh](../tests/smoke/docs_flags.sh)

<a id="dec-m371"></a>
#### M371 — Nested-key coverage probes are anchored (backticked or quoted tokens), and…
**Rejected:** Bare-word probes — `value`, `name`, `model` appear in prose on every page, so an unanchored search would mark any key "covered" the moment English mentions the word; anchoring on the token forms documentation actually uses keeps the check honest. A separate lint file — the vocabulary is the same lint's subject, and its own scope note named this exclusion; closing it where the promise was made keeps one lint per vocabulary (the M366 convention). Documenting `privilegedAudit` as discouraged rather than hiding it — a parsed knob that exists undocumented is worse than one documented with its warning; the page now says what doctor does about it (warn, FAIL unattended).
**Where:** [config_keys_lint.sh](../tests/smoke/config_keys_lint.sh)

<a id="dec-m372"></a>
#### M372 — The keybinding lint maps bytes to chord names inside itself,…
**Rejected:** Deriving names from source comments — the comments are prose (`/* Ctrl-E: end */`) and a lint keyed to comment wording rots with rewording; the map in the lint is the registry the tripwire needs (an unmapped dispatched byte fails until named), the session_fields pattern applied to keys. Including arrow/Alt escape sequences — their extraction is a state-machine parse of the ESC handler, not a grep; a half-right parser would false-fail on every editor change, and those keys are discoverable conventions. Stated in the lint header rather than implied. Semantics in the new table were read from each handler before being claimed (Ctrl-D and Ctrl-C are conditional, and the table says so) — a key reference that guesses is worse than none.
**Where:** [keys_lint.sh](../tests/smoke/keys_lint.sh)

<a id="dec-m383"></a>
#### M383 — search_code is now path-fenced
the same behaviour change M324 made to `list_files`.
**Rejected:** Leaving it unfenced: it runs `grep -rn` from a model-supplied `path` and returns file **contents**, so an unfenced read escaped the workspace in any mode including plan, unprompted and unjournaled — strictly worse than list_files' name-only leak, which M324 already judged worth fencing. A shell-level screen instead — the fence belongs on the tool that has a `path` argument to check; the shell workaround is item-8 territory (a different, heuristic layer).
**Where:** [AUTONOMY.md](AUTONOMY.md)

<a id="dec-m389"></a>
#### M389 — Output styles became the fourth validated asset kind, and the…
**Rejected:** Leaving output styles unvalidated: they were the one frontmatter-bearing `.jichi/` asset doctor skipped — the very kind M302 pointed AT when agents and skills gained `style:` — so a dead `name:` (the filename wins), a typo'd `descrption:` and an unterminated `---` (which swallows the body, leaving a style with no content) all passed silently. Trusting the tables' "keep in sync with the loaders" comment: that same promise, on the BUILTIN_CMDS list six lines below, had already drifted to 25 of 53 and was made mechanical at M262 — a hand-maintained invariant is a promise nobody can keep, and M302 proved this one breakable too. Deriving the lint's parser set automatically (a heuristic over files that call `jc_md_parse`): it would sweep in `jc_assign.c`, whose assignment specs are graded elsewhere; an explicit map with a stated scope limit (the M372 keys_lint shape) is honest about what it cannot see.
**Where:** [asset_keys_lint.sh](../tests/smoke/asset_keys_lint.sh)

<a id="dec-m390"></a>
#### M390 — The unit tier got the orphan check its sibling smoke…
**Rejected:** A per-file check-count baseline (TEST_INTEGRITY rec 3's literal wording): a count is only information until something compares it to a committed number, and that number must be edited on every test addition — the rotting-numbers problem M259 fixed and M307 answered with bounds, so it would trade a silent gap for a maintenance burden that decays into the same silence. Relying on the compiler: a non-static `void test_*(void)` nobody calls is legal C, and a declaration with no definition links fine — both are invisible below the lint. Including `static void test_*` helpers as suite entries (11 exist in test_acp.c alone): they are file-internal by construction, so the check anchors on column-0 signatures and says so; a non-static helper called from another test is flagged, and the message names both remedies.
**Where:** [unit_orphans_lint.sh](../tests/smoke/unit_orphans_lint.sh)

<a id="dec-m391"></a>
#### M391 — Slide figures are bounds or dated stamps, never live exact…
**Rejected:** Refreshing the exact numbers again: the decks had `107 smoke drivers` (now 164), `74 graded tasks` (77), `~85,000 lines` (92,213) and `~770 KB with SIZE=1` (measured 1,155 KB — 50% optimistic, on the very slide whose thesis is "not 'should run' — *measured*"). Every one had been true when written; slides are refreshed rarely, so an exact count there is a promise to revisit that nobody keeps — the M259 rot, in a surface no lint scans. So: monotonic figures become **bounds** ("over 160 drivers", which stays true as it grows), and platform/footprint measurements become **dated stamps** with the milestone or date attached (the README's own convention for stamped data). A smallness claim takes an UPPER bound ("under 2 MB"), which is the durable form of the thing the slide actually asserts. Rejected also: linting the decks — the figures are prose in four languages, and a lint that must parse "über 160" is a translation problem wearing a lint's clothes.
**Where:** [presentations/](presentations/README.md)

<a id="dec-m594"></a>
#### M594 — A capped read_file reports the FILE's line count, computed from…
The old message stated the capped buffer's count as the file's, turning a window limit into a denial: a model asked for line 11,715 of a 12,509-line file was told the file has 4,027 lines and stopped looking. **Rejected: leaving it and documenting the cap** — the sentence is read by a model mid-task, not by a person reading docs, and the tool printed the contradiction itself (`file has 4027 lines` immediately followed by `... [output truncated]`). **Rejected: a second pass over the file on disk to count lines** — unnecessary, because `read_file` already reads the whole file and caps only the output, so the true count is one pass over memory that is being held anyway; a re-read would add I/O to fix a message. **Rejected: reporting a count for PDFs too** — there the extractor is bounded at `cap`, so no true total exists at that point, and inventing one would be the same defect in a new place; that path says only what was read. The counting helper is shared with `jc_format_numbered` and unit-tested against it on eight inputs both ways, because the value of the number is that the two agree.
**Where:** [read_truncated_total.sh](../tests/smoke/read_truncated_total.sh)

<a id="dec-m593"></a>
#### M593 — make install verifies the build stamp by reading the generated…
The target refuses a binary stamped `-dirty`, because the documented sequence (build → gate → commit → `sudo make install`) succeeds at every step and puts a revision on the PATH that cannot be checked out. **Rejected: `git rev-parse` / `git status` inside the recipe.** `install` runs under `sudo`, and git as root in a user-owned repository can refuse with *"detected dubious ownership"* — on such a machine the check would silently skip in the one condition it exists for, which is this project's most-repeated defect class (a check that cannot fail). The generated stamp is a plain file, already compiled into the binary, and reading it needs no git, no network and no ownership rules; the cost is that a binary built from a clean tree at an *older* commit is not detected, which is stated here rather than papered over. **Rejected also: making `install` build again** — that is M586, which cost this tree 353 root-owned files. **Rejected also: refusing without an override** — testing an uncommitted change on the real PATH binary is a legitimate thing to want, so `ALLOW_DIRTY=1` exists and the refusal names it.
**Where:** [install_no_build.sh](../tests/smoke/install_no_build.sh)

<a id="dec-m591"></a>
#### M591 — An aliased tool call is grouped by the tool that…
One tool printed as two rows (`todo_write calls=1`, `todowrite calls=2`), so its ok-rate, latency and output bytes were each computed on a third of its calls. **Rejected: adding a resolved-name field to the event.** M532 settled this boundary after finding that `permissions.deny: ["write_file"]` failed to stop a `create_file` call — *"a gate must decide on what will run; a message must say what was asked"* — and named telemetry explicitly among the surfaces that keep the raw name. Stamping the canonical name there would contradict a standing decision to fix a reader bug. It would also only help **future** logs, where a read-time fix repairs the 47 MB already on this machine with no migration and no schema version. **Rejected also: dropping the requested spelling** once the row is grouped — it is the actionable half, since a model reaching for a name that is not the tool's own is reporting what the tool list taught it, and the remedy (teach the name, rename the tool, promote the alias) needs to know which name. It gets its own section, printed only when an alias was resolved, with a control check asserting its **absence** on a clean log so it cannot decay into a line that always prints.
**Where:** [telem_alias_rows.sh](../tests/smoke/telem_alias_rows.sh)

<a id="dec-m587"></a>
#### M587 — No lint guards the "cJSON is original code" claim, and…
The claim is load-bearing: [`LICENSING.md`](LICENSING.md) argues the licence choice is **unconstrained** because *"No vendored third-party source exists"*, so a page saying otherwise contradicts the project's legal position. M497's sweep removed *"three comments"* that called cJSON vendored, with no gate — and M587 found **six more**: one live test-file comment, two untranslated deck lines, and the timeline row in **four languages**, where the translations were *faithful to an English page that carried the same claim until M498*. A lint looks obvious. **Measured first (the population rule): six genuinely-wrong sites against eight legitimate ones** — `LICENSING.md`'s own history sentence, `ROADMAP`'s account of the pre-M171 `third_party/cjson/` layout, `ZIG_REWRITE_ANALYSIS`'s *negation* ("no vendored cJSON"), two dated analysis notes, `src/json/cJSON.c`'s PROVENANCE header, and M587's own declaration comments. Every one of those must keep the words. A lint would therefore ship with an exemption list longer than the defect it catches, and this project's own finding is that a lint people must edit is a lint they will edit (M552). **Rejected also: editing ROADMAP's three historical entries** — house practice is that a retraction which edits the mistake out of existence teaches nothing (M580), so milestone entries keep their wording. What replaces the lint is narrower and already exists: `i18n_tracks_lint` (M582) makes a translation's staleness visible, which is the mechanism that let this claim survive four days past its English correction and then live on in four languages.
**Where:** [LICENSING.md](LICENSING.md)

<a id="dec-m582"></a>
#### M582 — The localized decks ARE linted after all — by a…
This qualifies M391's rejection above rather than overturning it. M391 declined a lint because *"the figures are prose in four languages, and a lint that must parse 'über 160' is a translation problem wearing a lint's clothes"* — true of the lint it imagined, which would verify that a translated **bound** is correct. `tests/smoke/i18n_tracks_lint.sh` verifies something weaker and language-blind: that a translation introduces **no figure of three digits or more that its English counterpart lacks**, after normalizing thousands separators. It parses no words, evaluates no bound and knows no language. **Measured before it was built** (the population rule): across 28 decks it flagged exactly four figures — `700`, `7170`, `25100`, `163` — identical in all four languages, with **zero false positives**; the one- and two-digit noise (deck filenames `00`–`05`) is excluded by the digit floor, and a legitimate `M163` reference passes because the English deck contains it too. What M391 wanted to prevent is exactly what this catches: for months the four decks promised a **~700 KB** binary (measured 1.2 MB size-optimized, 1.7 MB as built) and **7,170** checks (12,960). Rejected: a git-history check (`git log <tracked>..HEAD`) — refused for the reason `changelog_coverage_lint.sh` already records, that the published snapshot ships a **fresh history**, so a history-dependent gate behaves differently in the tree people actually acquire. Every check is content-only and gives the same verdict in both trees.
**Where:** [i18n/README.md](i18n/README.md)

<a id="dec-m393"></a>
#### M393 — The defaults lint checks the --lite branch too, and a…
**Rejected:** Keeping M261's stated exclusion ("`lite ? A : B` takes B, the non-lite default, which is what the comment describes"): that exclusion is where the M392 review's two most dangerous findings lived — `snapshots` documented "default true" when it is 0 under a profile that turns *itself* on below ~1 GB (no checkpoints, no `/undo`, silently), and `references` the same. A header comment stating one number for a two-valued default is the source every user-facing page is written from, so the fix belongs there rather than in each page. Checking only the FIRST declaration of a field name — two structs share `context_limit`, and only the top-level one is lite-dependent; the first-match rule sent the lint at the per-model field, and "fixing" that one would have written a *false* claim onto a field with no lite branch, so the check now accepts disclosure on any declaration of the name and states the ambiguity. Also rejected: linting the user-facing prose for the same claim — the phrasing varies legitimately per page (CONFIG_TUTORIAL's `craft` line is the model form), and a prose lint over four languages of docs is the thing M391 already refused.
**Where:** [config_defaults_lint.sh](../tests/smoke/config_defaults_lint.sh)

<a id="dec-m394"></a>
#### M394 — Documented commands are linted by SHAPE, in fenced blocks only
**Rejected:** Linting whole invocations by running them: most need a live model, a key and a workspace, so the tier that must stay offline and python-free cannot execute them — and the class that actually bit us needs no execution, because the argument parser refuses these forms deterministically. Scanning prose as well as fences: two pages quote the refused form *deliberately* to teach it (CHOOSING_A_MODEL's scar, SCRIPTING's contract note), so the fence boundary is a principled discriminator — what a reader COPIES — rather than an exception list, which the M295 rule prefers. Matching any line containing "jichi": that flagged `tmux capture-pane -p -t jichi:0.1`, a tmux command whose *session* is named jichi, so the matcher now requires command position and tests only the substring from there. Treating `jichi -p -` as suspicious: it is legal and eight pages use it (the prompt is stdin), and check 4 proves the matcher makes that distinction rather than merely passing today. Checking `--prompt-b64 <b64> <subcommand>`: refused at a third layer M376 recorded as deferred, and no fenced block uses it — stated, not silently omitted.
**Where:** [doc_commands_lint.sh](../tests/smoke/doc_commands_lint.sh)

<a id="dec-m400"></a>
#### M400 — One page owns every platform verdict, and "never compiled" is…
**Rejected:** Leaving the verdict distributed: five pages stated it, and they had drifted into three different confidence levels for the *same* untested platform — `PREPARE_AND_BUILD` said macOS "works" and was "well-supported", `BUILD` said "should build, unverified in CI", `INSTALL` said "untested". A newcomer believed whichever they opened first, and each page was individually defensible. Also rejected: the softer wording "untested" or "unverified in CI" everywhere — both are true of a platform someone compiled once and true of one no compiler has ever seen, and only the second is jichi's actual state. "Never compiled" cannot be misread as "we ran it and did not measure". Also rejected: deleting the macOS/WSL instructions until someone verifies them — a reader with a Mac is better served by careful untried steps plus a warning than by nothing.
**Where:** [PLATFORMS.md](PLATFORMS.md)

<a id="dec-m400-2"></a>
#### M400 — A tree-wide long long lint, not a Darwin-branch one
**Rejected:** Linting only inside `#if defined(__APPLE__)`: the C89 rule is tree-wide, and the *class* of defect is "code behind a platform guard we cannot compile gets no compiler feedback" — a branch-scoped lint would pass the day someone adds a `__FreeBSD__` guard. Also rejected: an exception list for the comment that explains the rule (the first cut flagged its own explanation, the `asset_keys_lint` blindness again); instead the extraction drops lines whose first non-blank character is `*` or `/`, narrowing on a fact about C rather than a guess about English — the reason for a rule must stay writable next to the code. Also rejected: trusting `WERROR=1` to catch it, which is what failed for months — a warning flag only sees translation units this machine compiles.
**Where:** [PLATFORMS.md](PLATFORMS.md#the-finding-that-made-this-page-m400)

<a id="dec-m400-3"></a>
#### M400 — doctor warns on an unverified host; it does not fail
**Rejected:** A FAIL: unverified is not broken, and it would gate `doctor --unattended` (the loop-supervisor exit-code check) on a platform question no run can answer. Silence: the setup wizard already warned a non-Linux user and `doctor` — the command every page says to run first, and where a support conversation starts — did not, so a macOS user learned it only by running `setup` interactively and reading a paragraph. Also rejected: a line on every host; the verified case must cost nothing, so Linux prints nothing at all.
**Where:** [DOCTOR.md](DOCTOR.md)

<a id="dec-m409"></a>
#### M409 — A whole-line quoted sequence item is a scalar; the corpus…
**Rejected:** Rewording 64 files' hints to avoid colons: bends teaching prose to a parser limitation, and the next author reintroduces the form the same day. A lint-only fix (flag colon hints, keep the parser): leaves every *user's* project silently truncated and enshrines the bug as a convention. Full YAML quoting rules: out of scope for a deliberate subset — the fix reads exactly one form, a value that is one quoted string, and a quoted *key* still parses as a mapping.
**Where:** [jc_yaml.c](../src/convert/jc_yaml.c)

<a id="dec-m410"></a>
#### M410 — TAINTED is a third verdict, not a FAIL, and only…
**Rejected:** Reporting FAIL: wrong claim — the verify *did* pass, and what is withheld is trust, not the exit status of a command; a supervisor deserves the distinction. Keeping PASS with a warning line: the measured failure mode — the warning fired ten times and was ignored, because a verdict outranks its footnotes. Blocking test edits in the envelope generally: legitimate work edits tests (the test-first assignments *require* it); only `attempt`, which grades against a fixed gate, may treat a moved assertion as disqualifying.
**Where:** [ASSIGNMENTS.md](ASSIGNMENTS.md)

<a id="dec-m411"></a>
#### M411 — --model pins the run; the TUI's /model stays live-within-routing
**Rejected:** Routing winning with `status` telling the truth: fixes the lie, keeps the surprise — the user's explicit choice still loses to a policy, which no other flag in jichi does. Pinning in the TUI too: an interactive `/model` user *sees* the `[route]` banner and has `/route off` one keystroke away, so the headless asymmetry that made this a defect does not exist there — and changing a live session's semantics for a headless bug widens the blast radius for no measured gain. A `--model`-implies-`--no-route` hard rule with no escape: `--route-fast X --route-strong Y --model X` is a coherent request (start on X, route normally) and refusing it helps nobody.
**Where:** [ROUTING.md](ROUTING.md)

<a id="dec-m412"></a>
#### M412 — The gate-can-fail proof is a command, not an authoring-prompt demand
**Rejected:** Teaching `/assign`'s scaffold to demand red-first evidence from the model: binds only the model that reads the prompt, and the measured failure was precisely a model not honouring its brief ("a real verify command"). A `grade` mode that inverts silently instead of refusing `--record`: recording an inverted verdict would poison `progress.jsonl`'s meaning, and the M294 rule is refuse-don't-ignore. Naming the green case "FAIL": it *is* a failure of the gate, but the M86 word for a green gate that proves nothing is **HOLLOW**, and reusing the vocabulary keeps one concept one name.
**Where:** [ASSIGNMENTS.md](ASSIGNMENTS.md)

<a id="dec-m414"></a>
#### M414 — Campaign protocol: models author assignment prose; the tutor writes the…
**Rejected:** Letting the authoring model write its own gate — measured twice in one campaign: gemma shipped a verify that was already green (PASS at 100% with the target function still panicking) and, retried, a gate whose exact f32 `==` a *correct* implementation would fail; qwen's authoring run needed its gate hand-repaired too. Fully hand-authored assignments — discards the thing the models are measurably good at (gemma's ping-pong prose is the best teaching text in the campaign). One model authoring, another gating — case 3 showed author and solver *sharing* a model share its blind spots (the junior built the API the author hallucinated), so a cross-model gate would still not be a *proof*; only the red run is.
**Where:** [case-studies/README.md](case-studies/README.md)

<a id="dec-m415"></a>
#### M415 — A TAINTED attempt records passed:false, and the goalpost insight fires…
**Rejected:** Recording TAINTED with `passed:true, tainted:true`: the gradebook's one boolean would then say the opposite of the verdict, and every consumer must know the second field to avoid crediting a gamed grade — the default reading must be the safe one. Aligning the insight with the other rules' repetition thresholds (2–3 occurrences): those guard against noise in *frequency* signals; a single moved assertion is not noise, it is the difference between a grade and a gamed grade, and the measured case would have needed ten.
**Where:** [case-studies/README.md](case-studies/README.md)

<a id="dec-m423"></a>
#### M423 — The output: fallback writes beside an existing artifact, never over…
**Rejected:** Overwriting unconditionally (the shipped behaviour): the fallback exists so a narrated result is not lost, and it destroyed an 85-line reviewed lessons draft with four lines of mid-thought narration from an unrelated run — a mechanism built to prevent loss cannot be the thing that causes it. Refusing to write at all when the target exists: trades one loss for another, and the narrated answer is often the only copy. Parsing the answer and overwriting only when it "looks like" the artifact: `jc_learn_parse_draft` deliberately tolerates prose, so it would have accepted the narration — and a per-artifact validator does not generalise to any other `output:` command. Appending to the existing file: silently merges two documents with different authors, and the draft's section structure is what `learn apply` reads. The sibling `<path>.answer` keeps both copies, needs no parser, works for every `output:` command, and puts the choice where it belongs — with the human, who is told on stderr.
**Where:** [LEARNING.md](LEARNING.md)

<a id="dec-m431"></a>
#### M431 — A narrow env_budget_applies() re-arms the budget at any depth, rather…
**Rejected:** Loosening `env_active` itself: the journal, verify gate, baseline, self-review and periodic verify must stay top-level — a delegate that ran the verifier could roll the tree back mid-parent-turn — and **M133 already declined that same loosening** for the edit-scope fence, adding `env_scope_fence` beside it instead. Setting `agent_depth = 0` inside the forked child: it would switch on several other depth-0 behaviours that are correctly off there (control channel, type-ahead, todos, snapshots, the 80% notice). Leaving enforcement at depth 0 and documenting the gap: `PARALLEL.md` promised the bound, and a supervisor sizes a fan-out on it.
**Where:** [proposals/2026-08-model-facing-orchestration.md](proposals/2026-08-model-facing-orchestration.md)

<a id="dec-m431-2"></a>
#### M431 — A delegate over budget stops with JC_OK and its work…
**Rejected:** Returning an error: it invites callers to discard or roll back work that is fine — the iteration cap's reasoning, which this reuses deliberately. Letting the delegate call `env_stop_for_budget` itself: that journals, may run the verifier and may roll the tree back, none of which is a delegate's to do (and a forked child appending to its siblings' journal would interleave). Stopping silently: a budget-stopped delegate reporting as success would be a *new* silent false success, which is the defect class the milestone exists to remove — so the note ships with the bound.
**Where:** [ROADMAP.md](ROADMAP.md) M431

<a id="dec-m431-3"></a>
#### M431 — The edit-scope globs are rendered by one shared function used…
**Rejected:** A second wording in the prompt: M296's lesson is that two surfaces must not describe one result two ways, and here the *fact* is identical — a model told one list up front and shown a different one on refusal learns to trust neither. Keeping discovery-by-refusal (the shipped behaviour): M333 put the list in the refusal after a run guessed 177 times; stating it before the tokens are spent is the same fact, delivered earlier.
**Where:** [ROADMAP.md](ROADMAP.md) M431

<a id="dec-m431-4"></a>
#### M431 — spawn_parallel's is_error stays "all children failed"; the doc is corrected…
**Rejected:** Matching `PARALLEL.md`'s "any child failed", which is better *telemetry* semantics: that same bit is what the **model** reads as `is_error`, so flagging a call in which three of four children succeeded would tell the model its fan-out failed, and which child died is in the result text either way. Serving both consumers properly needs a separate partial-failure field — a bigger change than correcting a sentence, and not one this milestone measured a need for.
**Where:** [PARALLEL.md](PARALLEL.md)

<a id="dec-m431-5"></a>
#### M431 — describe's contract is checked by a lint over the emitters,…
**Rejected:** Trusting review: four drifts had accumulated in a surface `main.c` itself calls "the contract" and `EMBEDDING.md` tells consumers to diff in CI, and `describe.sh` had been checking presence and identifier *shape* for milestones without noticing any of them. Generating `describe` from the emitters at build time: it is a hand-written contract on purpose (the prose notes carry meaning no extraction could produce), so the fix is a checker, not a generator — "prefer a lint to an audit" applied to a promise rather than to code.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md)

<a id="dec-m431b"></a>
#### M431b — The changelog lint bounds systemic drift and demands no per-milestone…
**Rejected:** Demanding an entry per milestone — the DEFERRED row's own rejected design, because "a check demanding one per milestone would be satisfied by a line of noise" and a file of dutiful non-entries is worse than an honest visible gap. Asking git whether the newest milestone's commit touched `src/` and requiring an entry only then — precise, and it WOULD have caught M431's own omission, but the public snapshot ships a fresh history, so a history-dependent gate behaves differently in the tree people acquire. Overruling the row instead of meeting its condition — the row named "drifts again despite the note" as its revisit trigger, and M402's note plus M431's silent omission met it, so this is the condition being honoured rather than a preference winning. The consequence is stated in the driver: a drift of one is invisible to it.
**Where:** [DEFERRED.md](DEFERRED.md)

<a id="dec-m331"></a>
#### M331 — The M331 reframing lives in one shared renderer, and precedes…
**Rejected:** A second copy of the paragraph at the periodic site — two surfaces describing one result two ways is what M296 forbids, and this paragraph is the whole return on the check. Appending it after the parsed failures and output tail — the completion path's own comment states it "has to arrive before the evidence it reframes", since a model handed an exit code plus zero failures starts editing code; ordering is the feature, and the driver asserts it. Leaving the periodic site journal-only (the shipped behaviour) — `--verify-every` exists to catch a problem earlier, so it is the site that most needs the redirection.
**Where:** [ROADMAP.md](ROADMAP.md) M431b

<a id="dec-m431c"></a>
#### M431c — The run id ships as a run_start EVENT plus done.run,…
**Rejected:** Emitting the journal **path** instead: for the default location it is derivable from the id, and for a custom `--journal` the supervisor supplied it — so the path is never the only way to know, while the id is additionally the telemetry join key. Only adding `done.run`: it arrives at the END, which is too late for the thing the id is for — tailing a live run's journal. Putting `run` on `message_start`: that event fires per assistant message and is about the message, not the run. Emitting `run_start` unconditionally: without an envelope there is no run id and no journal, so an event carrying nothing would make a supervisor wait for a signal that cannot come.
**Where:** [SCRIPTING.md](SCRIPTING.md)

<a id="dec-m431e"></a>
#### M431e — The workspace lease warns by default, and is a FILE…
**Rejected:** Refuse-by-default: two read-only runs over one tree are routine — it is how an operator inspects a running job — so breaking them to prevent a write–write hazard is a cure worse than the disease. `flock`: it dies with the process, which sounds like a feature until a crashed run leaves no evidence it ever held the tree, and "who was in here?" is exactly the forensic question a supervisor asks; a file plus a pid liveness check answers it, a released lock cannot. Blocking on a stale lease (a holder whose pid is gone) in `fail` mode: that is the classic lockfile failure, and an operator cannot tell stale from live by looking. `--lease wait`: deferred, because it needs a timeout policy of its own and a supervisor can express waiting today as `--lease fail` plus a retry, which is strictly more flexible. **Stated limit:** a lease does not fix the one-actor assumption, it makes violating it loud.
**Where:** [AUTONOMY.md](AUTONOMY.md) §3b

<a id="dec-m431d"></a>
#### M431d — A hollow completion green buys one extra round, before banking…
**Rejected:** Leaving the completion site silent (the shipped behaviour): without `--verify-every` a hollow green was invisible to the model in every run. Making M86 change the outcome: it is deliberately advisory, and a detection heuristic that can fail a run is a worse trade than one that can only warn. Defaulting `--verify-every`: it pays a full verifier run every N calls to cover a rare case. Banking the green checkpoint *before* the extra round: the model may edit files in it, so the checkpoint could describe a tree that no longer exists. Skipping the re-verify on that round: it would let a round in which the model edited go entirely ungated.
**Where:** [ROADMAP.md](ROADMAP.md) M431d

<a id="dec-m347-2"></a>
#### M347 — The budget panel ships OFF, as a flag to be…
**Rejected:** Shipping it on: M347's register rejected "a reminder per round or a countdown" on the M323 evidence (1,038 warnings from one unthrottled condition) and the principle that a nag a model learns to skip is worse than one clear ask — and that objection is a judgement about model behaviour, so overruling it without a measurement would be preference beating evidence. Per-round delivery: the cadence is every Nth tool call PLUS each quintile crossing, which is what answers M347 mechanically. A system-prompt section: it would invalidate the M31 cached prefix on every call, the most expensive change available on a caching backend, and be stale by construction. A `budget` tool the model calls: at ~34k input tokens per call, asking what you have spent costs more than most answers are worth, and a worried model asks repeatedly. Emitting at every depth: M347's reasoning is that the PARENT paces the run — but the rate's denominator does count every depth's calls, so a delegating run's reading does not read low.
**Where:** [AUTONOMY.md](AUTONOMY.md) §3a

<a id="dec-m432"></a>
#### M432 — The loop detector uses TWO keys with separate thresholds, classifies…
**Rejected:** One key: an exact `(tool, args)` key scored **zero** over logs holding a real loop 96 times, because that loop varied one constant (`git log -N`, 300 → 20,000,000); a class-only key would miss byte-identical repetition at a tighter threshold than 4 is safe for. A threshold of 2: the 2× tail is thick (78 turns on chrtext) and usually a legitimate retry. Classifying offline: at the `metrics` tier a `tool_call` event carries only `ok:false` and no reason, so a reader can see a loop's shape and never its cause — the classifier has to be where the result string is. Generic advice: a wrong cause is a loop *amplifier* (M342), so advice is per-cause and `other` says less rather than guessing. Telling per key: firing the exact key at 3 let the class key fire at 4 for the same loop — one problem, two notes, which is M323's nag by a side door; the exact key now suppresses the class slot, while the same tool failing for a *different* cause still fires. Telling only the model: M422's lesson — a detector whose finding never reaches `runs` is one nobody acts on.
**Where:** [AUTONOMY.md](AUTONOMY.md) §3c

<a id="dec-m433"></a>
#### M433 — brief-check attributes a constraint to its line by RE-SCANNING lines,…
**Rejected:** Extending `jc_constraint_scan` to carry a source span: a signature change to the *enforcement* path for the sake of a report, when re-scanning smaller units gets the same answer with no risk to what actually gates a run. Naming a best-guess line when no single line reproduces the finding: a scan over the whole message can match across a line break, and a wrong line has the author reword text that was never responsible — worse than admitting none. Listing only the *surprising* constraints: an author who sees a filtered list trusts it less, so the intended ones are shown too. Exiting non-zero on any finding: an inferred constraint may well be intended, so only the unambiguous defect (a declared `goal` gate that already passes) is worth a wrapper's gate.
**Where:** [AUTONOMY.md](AUTONOMY.md) §3z

<a id="dec-m434"></a>
#### M434 — A subagent's prompt states what is ENFORCED on it, and…
**Rejected:** Giving the subagent the main prompt: the repo map and skills catalogue are *context* a delegate does not need — the parent digested one, a skill is handed over explicitly — and copying them defeats the context isolation that is the entire reason to delegate; project rules and memory fall the same side of that line. Leaving the prompt as it was: three gates hold at any depth while it named none, and a delegate cannot learn them from a refusal the way a human operator can, because its task text is written by the parent. Making the untrusted-content rule conditional: M300's reason applies unchanged — turning off prose guidance must not turn off a safety convention. A lint over the prompt's *wording*: that would be guessing about English, so the lint pins the wiring and DERIVES each requirement from the gate's own depth condition, meaning a future depth guard relaxes it honestly instead of leaving a stale demand.
**Where:** [ROADMAP.md](ROADMAP.md) M434

<a id="dec-m435"></a>
#### M435 — The goalpost note tells the model, and does not refuse…
**Rejected:** Refusing a test-assertion edit outright: it breaks the legitimate case — a test can encode a mistaken expectation, and a tool that forbids correcting one forces the model to stall or misreport — and it is *routable*, since `edit_file` becomes `sed -i` under `run_terminal_command`, which M88 does not inspect and the M83 guard sees only at turn end. Leaving the four operator-facing destinations as the whole story: all four are read after the fact, and the measured run adjusted expectations ten times while the WARN fired to nobody who could act. Throttling to once per turn like M432's loop detector: each assertion edit is a distinct decision rather than a repetition of one, so suppressing the second hides new information instead of a nag. Adding the note to both tools separately: six duplicated side effects across two tools is the drift M296 forbids, so the two M88 blocks were factored into one reporter first.
**Where:** [ROADMAP.md](ROADMAP.md) M435

<a id="dec-m436"></a>
#### M436 — A depth-gated tool is declared once on struct jc_tool, not…
**Rejected:** Removing the depth gate so the advertisement becomes honest: the gate is right — the todo list is shared with the human and outlives the subtask, so a delegate must not overwrite it. Leaving the runtime checks and only filtering the advertisement: the fact would then be stated in four places (three tool bodies plus the builder), and a disagreement between them is exactly what produced the bug. A name table plus a two-directional lint (M285's idiom in this same file): once the in-tool gates are deleted the table *is* the only source, so the lint would check the table against itself — the "lint checking nothing" failure TEST_INTEGRITY.md names. Relying on C89's implicit zeroing of a trailing member to avoid touching 40 definitions: `-Wextra` includes `-Wmissing-field-initializers`, and naming the field at every definition is better anyway, since a reader of any tool definition can then see whether it is main-agent-only.
**Where:** [ROADMAP.md](ROADMAP.md) M436

<a id="dec-m437"></a>
#### M437 — The delegation report is a bounded summary the parent can…
**Rejected:** Forwarding the transcript: that defeats the purpose of delegating — the parent delegated precisely to avoid holding that context, and a report is useful in proportion to how little of it there is. Reporting on every delegation: a clean delegation already says it finished by producing an answer, so a block saying so each time is the per-round nag M347 rejected on measured evidence; the renderer emits nothing in that case. Reporting `tool_calls: 0` when the envelope is absent: 0 asserts the delegate made no calls, which is a different and usually false claim, so absent is reported as absent. Sending the stop reason as an integer over the fork-pool pipe: a name survives an enum renumbering and an integer silently does not. Zeroing the per-run slots around a nested call instead of saving and restoring them: this run may have had its own earlier failure, and zeroing would erase it.
**Where:** [ROADMAP.md](ROADMAP.md) M437

<a id="dec-m438"></a>
#### M438 — A poll-only control service point between tool calls, not the…
**Rejected:** Moving `jc_control_boundary` itself inside the per-call loop: it folds queued steering into history as a user-role message, and a user message between two tool results of one round makes the request malformed — the contract `jc_history_check` (M364) exists to catch. Serving the socket during a model call: that means polling from libcurl's progress callback, where a `pause` blocks the HTTP read and can time the request out; `--heartbeat` covers that window instead. A second implementation of the serve loop: the poll is `jc_control_boundary` with `hist = NULL`, which is already how the boundary expresses "do not fold", so a future change to the pause semantics cannot apply to one caller and not the other. Putting the run's budgets on the new `open` record: they are already on `start`, where a reader looks; `open` answers one question, "the run exists".
**Where:** [ROADMAP.md](ROADMAP.md) M438

<a id="dec-m440"></a>
#### M440 — The cost-model section is gated on the CONFIGURED cache setting…
**Rejected:** Unconditional frugality prose: the correct read policy is opposite on the two backend classes, so it would be wrong on one of them and permanently billed into a cached prefix where it buys little. Gating on the OBSERVED hit-rate, which is the more accurate signal: it is a running statistic, and a running statistic in the system prompt changes the cached prefix every turn and destroys the caching it describes (M31) — so the section's signature takes five integers and no `jc_app`, making the hazard structurally impossible rather than merely discouraged. The gap this leaves (a backend that accepts a caching request and returns no cached tokens, a measured case) is handled by the explicit flag plus `doctor`/`telemetry --cache-audit`, not by making the prompt smarter. Telling the model to refuse or narrow a large read: that re-opens TOOL_OUTPUT_COST §7's rejection of auto-bounding through the back door, so the section informs and never narrows.
**Where:** [ROADMAP.md](ROADMAP.md) M440

<a id="dec-m443"></a>
#### M443 — degraded counts decisions taken in the operator's ABSENCE, and an…
**Rejected:** Counting every auto-approved tool call: `--auto` *is* the operator's instruction to approve, so counting it would set the flag on every unattended run and make it worthless — a field that is always present is a field nobody reads. A plain boolean: it cannot distinguish a run that asked one question from one that asked twelve, and repetition is what tells an operator the brief was unclear. Always emitting the object with zeros: an always-present `degraded:false` trains a reader to skip the field on the one run where it matters, which is the M420 argument that gave `goalposts=N` its non-zero gate — so presence IS the flag. Folding it into `stop_reason`: the flag reports, it does not judge; whether unanswered questions make a result untrustworthy is the supervisor's call.
**Where:** [ROADMAP.md](ROADMAP.md) M443

<a id="dec-m444"></a>
#### M444 — The envelope's ARMING is split from its JOURNAL so an…
**Rejected:** Moving the `sysmsg`/`context` dispatch to after main()'s arming block — the fix the register row assumed: MCP servers are connected in between, so a read-only prompt dump would have started spawning subprocesses, which is worse than the gap it closes. Letting the introspection path arm an envelope the normal way: that opens a journal, so `jichi sysmsg` would litter `~/.jichi.d/runs` with a record per invocation. Having `envelope_arm` also set `app->env` and promote the mode: those belong to a run, and putting them in the helper is exactly how a refactor like this turns a read-only command into a side-effecting one — so the two callers decide separately.
**Where:** [ROADMAP.md](ROADMAP.md) M444

<a id="dec-m463"></a>
#### M463 — The milestone-currency lint's ground truth is the highest M### cited…
**Rejected:** A commit-count-since-`ROADMAP.md`-was-last-touched check, which is the obvious basis and reads more directly: the shipped tree is **not a repository** (M451 unpacked a `git archive` and found four checks silently missing), and smoke runs on-target where `git` may be absent — so the check would pass vacuously exactly where it is most needed. Comparing the ROADMAP against itself, which is what all three pre-existing currency checks did: that is the defect, not the design — when the file freezes, every check that trusts it freezes green with it. Including `docs/ROADMAP.md` in the scan: it is the file under test, and an entry saying "written retrospectively (M463)" cites a milestone whose heading is correctly not there yet.
**Where:** [ROADMAP.md](ROADMAP.md) M463

<a id="dec-m465"></a>
#### M465 — A variable for a with_deadline child is passed as with_deadline…
**Rejected:** `export` before the call: these fixtures are deliberately scoped, and an export leaks into the rest of the driver. A subshell with `export`: correct, but ceremony at 210 call sites where `env` reads as intent. Judging each of the 210 sites for whether the child actually needs the variable: `with_deadline` always runs a child, so the rule can be **total** — and a total rule needs no exception list, which is the property that makes a lint trustworthy. The check's own two calls were rewritten rather than exempted, because a lint that exempts itself is a lint nobody believes.
**Where:** [ROADMAP.md](ROADMAP.md) M465

<a id="dec-m466-5"></a>
#### M466 — The GNU word-boundary ban is narrowed to lines that also…
**Rejected:** Banning `\b` in shell sources outright: in a `printf` format `\b` is a **backspace** and entirely legitimate — a different construct that happens to share two characters. Fixing only the two sites found: the tier already banned `grep -P` and GNU `\|` for exactly these reasons and was *one row short of the same family*, which is how the construct got typed by the author of the neighbouring rows. Trusting an audit instead of a lint: an audit finds what it knew to look for, and this family (`\b`, `\<`, `\>`, `\w`) is one thought rather than four.
**Where:** [ROADMAP.md](ROADMAP.md) M466

<a id="dec-m466-2"></a>
#### M466 — JC_SMOKE_KEEP_GOING defaults OFF; the tier stays fail-fast
**Rejected:** Making keep-going the default: locally the fix loop is seconds long and the first failure is the one you want, so fail-fast is right there and `make ci` should not change behaviour. Leaving the tier fail-fast everywhere: on a remote row that costs a ten-minute unattended install it reports exactly **one** failing driver, so an N-defect platform costs N boots — and it *masks*, since OpenBSD's real stop had never been reached in three rows behind an unrelated lint that ran earlier in the list. A per-driver `|| true`: that loses the exit code, and the tier must still fail.
**Where:** [ROADMAP.md](ROADMAP.md) M466

<a id="dec-m466-3"></a>
#### M466 — A named driver that does not resolve is exit 2,…
**Rejected:** Skipping unknown names and running the rest: a typo would then report `smoke: OK` over **zero** drivers, which is the "lint checking nothing" failure `TEST_INTEGRITY.md` exists to name. Failing with exit 1: that is the code for *something under test failed*, and a mistyped argument is a usage error — the rigs' contract already distinguishes them.
**Where:** [ROADMAP.md](ROADMAP.md) M466

<a id="dec-m466-4"></a>
#### M466 — A rig ships git archive HEAD by default and --dirty…
**Rejected:** HEAD only, as every rig did: the loop *find a portability defect on the target → fix it → verify the fix there* is then impossible, and the only way to test a fix is to commit it untested — which is how M466's own `\b` fix would have shipped. Working tree by default: a published row must name a commit or it cannot be reproduced. `git stash` + archive: it mutates the developer's tree, and a rig that can dirty the tree it tests has already been three separate mistakes in this campaign. Shipping quietly either way: a dirty row that looks reproducible is worse than no row, so the provenance line is written **before** the build and says *NOT a commit*.
**Where:** [ROADMAP.md](ROADMAP.md) M466

<a id="dec-m467"></a>
#### M467 — ptydrive tolerates an ambiguous zero-length read ONLY before the first…
**Rejected:** Treating `read()==0` as EOF, as it did: before the child opens the pty slave, OpenBSD reports the master **readable** and returns 0 with errno 0 — identical to a closed slave — so every driver whose script opens with `expect` raced the child's `open()` and stopped reading. 21 drivers, `0 of 0 bytes`. Tolerating it unbounded: a child that exits having written nothing is a *genuine* EOF that Linux reports through the same path, so it would fail by timeout instead of at once — measured at 30 s against ~2 s, trading one platform's bug for every platform's diagnostics. Keeping the parent's slave fd open (the textbook fix): the master then never reports EOF at all, and three `expect` paths use exactly that to detect a dead child. Returning to the caller without a nap: OpenBSD keeps calling the master readable, so it spins hot until the deadline.
**Where:** [ROADMAP.md](ROADMAP.md) M467

<a id="dec-m467-2"></a>
#### M467 — A backgrounded subshell in a driver must exec, enforced by…
**Rejected:** Leaving the sites alone: on OpenBSD ksh `$!` names **the subshell**, not the command (dash and bash implicit-exec; measured on all three), so every `kill`, `kill -INT` and `wait` hit a shell — `parallel_abort` SIGINT-ed a shell and then reported *"abort/reaping deadlocked"* about an agent that was never asked to abort. Fixing only the two drivers that failed: `signals.sh` had used `exec` since it was written, because its whole subject is signal delivery, and the idiom was never generalised — the same one-row-short shape as the `\b` family, twice in one day. Scoping the lint to synchronous subshells too: those neither signal nor wait, and a dozen of them are correct as they are.
**Where:** [ROADMAP.md](ROADMAP.md) M467

<a id="dec-m469-5"></a>
#### M469 — JC_CHECK_NEAR replaces exact float equality only at the NON-REPRESENTABLE sites
**Rejected:** Converting all 80 exact float comparisons in the suite: `== 3.0`, `== 0.5` and `== 0.75` are exact in both IEEE-754 double and m68k's 80-bit extended format, so changing them trades a precise assertion for a fuzzy one and buys nothing. Leaving them all exact: five checks failed on m68k while the **values were right** — `strtod("0.2")` prints `0.20000000000000001` there, byte-for-byte what x86-64 prints — because the comparison happens at extended precision where the literal carries more bits than the double. C89 permits it (`FLT_EVAL_METHOD` 2) and i386/x87 does the same, so the suite had a latent inability to validate on a whole class of targets. A relative epsilon: every value compared this way is a price, a temperature or a unit-scale score within a couple of orders of magnitude of 1, and an absolute 1e-9 is the one a reader can check by eye. `fabs`: it would add `<math.h>` to fifteen test files for a subtraction.
**Where:** [ROADMAP.md](ROADMAP.md) M469

<a id="dec-m469-2"></a>
#### M469 — tier-v-arch.sh compile-probes every triple instead of trusting zig targets
**Rejected:** Reading the candidate matrix off `zig targets`' libc table, which is the obvious source: that table and the LLVM **backend** list are different sets — zig advertises `m68k-linux-musl` and cannot generate code for it, so the matrix would claim 22 architectures and silently test 21. Hardcoding a known-good list: it rots the first time zig or the kernel's handler set changes, and would then test fewer arches than it claims without saying so. Dropping the unbuildable triples in silence: *"zig says it can and then cannot"* is a finding about the toolchain, so `--list` names m68k as **ships musl, cannot compile**.
**Where:** [ROADMAP.md](ROADMAP.md) M469

<a id="dec-m469-3"></a>
#### M469 — The sweep links HAVE_CURL= and every row says so
**Rejected:** Building a minimal libcurl per architecture so the rows exercise a model call: `zig cc` bundles libcs, not dependency trees, and 21 cross-builds of curl is a different project — `scripts/minimal-curl.sh` exists for the one case where that footprint is the measurement. Reporting the rows without the caveat: they exercise the core, arenas, JSON and pure helpers but **no model call**, so a green row here is weaker evidence than a green FreeBSD row, and a reader comparing the two counts deserves to be told rather than left to infer.
**Where:** [ROADMAP.md](ROADMAP.md) M469

<a id="dec-m469-4"></a>
#### M469 — MIPS's 73 failures are classified as the emulator's limitation, not…
**Rejected:** Filing them as a jichi failure, which is what the rig's own output said (*"unit suite RAN and reported failures"*): `pipe()` itself fails under `qemu-mips` with zig's musl — probed directly, and it works on powerpc — and all 73 are downstream, the seven failing files being exactly those that spawn a subprocess or read `/proc`. MIPS is the one Linux architecture whose `pipe` syscall returns both descriptors in registers. Dropping the MIPS rows quietly: the *number* is real and someone should be able to reproduce the reasoning. Assuming emulation for armeb on the same grounds without probing: that one needed its own discriminator (the **literal** `0.2` misprints, so jichi cannot be the cause) — and the armeb dig is what produced the real cJSON bug, so the habit of probing before dismissing paid for itself in the same sweep.
**Where:** [ROADMAP.md](ROADMAP.md) M469

<a id="dec-m478"></a>
#### M478 — A terminal, not an integration
JupyterHub support is *documentation plus a measurement*, not code: install jichi in the image, users type `jichi`.
**Rejected:** A kernel wrapper, an IPython magic, or a hub-level jichi service. All three re-create the multi-user problem **inside** jichi: a shared daemon holds one process's `$HOME`, so the per-user isolation a hub gives for free — keys, audit log, checkpoints, leases — would have to be re-invented, and jichi has no notion of a caller identity to key any of it on. The demand was also hypothetical: nobody had reported that a terminal was insufficient.
**Where:** [JUPYTERHUB.md](JUPYTERHUB.md) §3, ROADMAP M478

<a id="dec-m478-2"></a>
#### M478 — The JupyterHub rigs are an operator tier, outside make ci
`scripts/jhub-*.sh` are run by a person, like `scripts/tier-v-terminals.sh`.
**Rejected:** Wiring them into the gate. They need a Python virtualenv with JupyterLab in it and, for Tier J2, qemu and `/dev/kvm` — none of which the gate may assume, on a project whose whole premise is that the product needs a C compiler and libcurl and nothing else. A gate that cannot run on a Raspberry Pi is not this project's gate.
**Where:** ROADMAP M478

<a id="dec-m478-3"></a>
#### M478 — Two wheelhouses, host and guest
The prep downloads a cp314 set for this bench and a cp311/manylinux set for the Debian guest.
**Rejected:** One wheelhouse. `pip download` bakes in the interpreter **and** the platform: the host set has 14 cp314 wheels and the guest set has none. The decision was later vindicated twice — the guest set installed JupyterHub offline in the VM, and it also exposed that `--python-version` silently omits marker-gated dependencies, which only an install on the target can reveal.
**Where:** ROADMAP M478

<a id="dec-m478-4"></a>
#### M478 — --negative-control is a first-class mode of the verification rig
not a one-off check performed once by hand.
**Rejected:** Trusting a green run. Four versions of the J1 probe were green **and wrong** — a marker that does not exist under `LC_ALL=C`, two markers satisfied by the terminal's echo of the typed command, and a whole-transcript search satisfied by an earlier occurrence. The control is what caught them, and it caught a fifth defect on its first run. The cost is a second run of the suite; the alternative is a number nobody should quote.
**Where:** [TEST_INTEGRITY.md](TEST_INTEGRITY.md), ROADMAP M478

<a id="dec-m551"></a>
#### M551 — Accessible mode gets its OWN prompt string, and the sighted…
A second catalog entry, `JC_MSG_ALLOW_PROMPT_ACC`, branched on `--accessible`.
**Rejected:** **Changing `[y]es  [n]o  [a]lways` for everyone.** The bracket form is not a mistake — it is a good *visual* affordance, showing the accepted key inside the word it names, and it is what every existing user's muscle memory and every screenshot in the docs shows. Replacing it would fix a listener by degrading a reader, and there is no evidence anyone wanted that. **Localising by re-wording the four translations properly**: no screen-reader test has been run in de/es/ja/zh, so the translated entries are the reviewed strings with the brackets removed and nothing else changed — a change defensible without a speech test, where a new sentence would not be. **Inserting the new id mid-enum beside its sibling**: the five language tables are positionally initialized, so a mid-list insert landing in the wrong slot in one table swaps two strings and no test can see it; appended instead, where the slot cannot be confused.
**Where:** [analysis/2026-08-22-screen-reader-audit.md](analysis/2026-08-22-screen-reader-audit.md), ROADMAP M551

<a id="dec-m552"></a>
#### M552 — The spoken key list uses "y as in yes", and…
The key is followed by a word beginning with that letter, so mishearing the letter costs nothing.
**Rejected:** **"y for yes"**, M551's wording — measured by ear and rejected: `a` and `e` are weak spoken alone, and *"a for always"* is ambiguous with the indefinite article, so the phrase hid the key it existed to expose. **Word-first ("always is a")**: gives context before the letter but leaves nothing if the letter is missed, which is the failure being fixed. **The NATO alphabet ("a for alpha")**: unambiguous and wrong here — the mnemonic must be the *action*, and `alpha` names no action. **Translating the cue**: `a wie immer` is false, since the keys are never localised and are therefore the English words' initials; there is no correct in-language cue and no screen-reader test in de/es/ja/zh to design one against, so those four keep the bracket-free form and the reason sits in a comment beside them. **Asserting the sentence in the test**: it pins `" a as in a"` instead — the relation is the fix, the prose is free to change.
**Where:** [analysis/2026-08-22-screen-reader-audit.md](analysis/2026-08-22-screen-reader-audit.md), ROADMAP M552

<a id="dec-m581"></a>
#### M581 — Line-buffered output stays accessible-only
closing the one property stage A3 left open. `--accessible` holds model output until a newline, so a reader announces whole lines instead of 3.5-character fragments (M549). A3 asked whether it could simply become the default — "needs a measurement: is the delay perceptible to a sighted user? Answerable in milliseconds, not yet answered."
**Rejected:** **Making it the default for everyone**, which would have deleted a branch. Rejected on the measurement: on a real streamed reply a character waits a **median of 340 ms** before its line completes, **p90 631 ms, max 810 ms**, with **85% of deltas over the 100 ms perceptibility floor**. The cost is proportional to line length and therefore *invisible in the easy case*: the same measurement over short-line output (a 25-item list) gives a median of **12.5 ms** and nothing above 31 ms. Prose is the dominant shape of a chat reply, so the honest figure is the 340 ms one. **The implementation was already correct** — `jc_tui.c` gates the accumulator on `!c->accessible` — so this decision confirms the existing branch rather than changing it, and replaces a guess with a number.
**Where:** ROADMAP M581, M561

<a id="dec-m561"></a>
#### M561 — Prose chrome stays accessible-only; it does not become the default
The compact renderings — `[tokens in=20 out=5]`, `> read_file`, `session total: 40 in / 10 out` — are kept for sighted users.
**Rejected:** **Flipping prose on for everyone**, which stage A3 was written to consider and which §1 of the design argued for. Rejected on a distinction §1 had missed: information carried by **colour vanishes** without colour, so restoring it is a defect fix (that is M560, and it shipped); information carried by **punctuation does not vanish** — `[tokens in=20 out=5]` survives copy-paste, `NO_COLOR` and a pipe intact — so converting it to prose is a *preference*. The §1 benefits then mostly evaporate: "prose survives copy-paste" is true of colour and false here; "a model parses prose better" is what `--output json` is for; braille users run `--accessible` and already have it. The costs are measured rather than supposed: **50 columns against 25** (M554), **five of the six chrome lines fire per event** rather than once per session (`cb_usage`, `cb_message_begin`, `cb_tool_start`, `cb_tool_result` — 10–50 times a session), German adds up to **+22 columns** on top, and `grep 'tokens in='` is an anchor that four of our own test assertions had to be rewritten around. **Also rejected: flipping only the once-per-session lines.** The session total is the single one that fires once, and one line is not worth a third rendering to maintain.
**Where:** [analysis/2026-08-23-chrome-width.md](analysis/2026-08-23-chrome-width.md), ROADMAP M561

<a id="dec-m621"></a>
#### M621 — make ci green must mean the same thing on every…
After the first hosted run failed in snapshot_lint's --commit rehearsal ("refusing --commit with no author identity"), the mend went into the producer and the lint -- an explicit GIT_AUTHOR_NAME/EMAIL fallback for the rehearsal, resolution-authoritative committing, and a clone-based check that the refusal still fires when every identity source is dark.
**Rejected:** **Configuring an identity in the workflow** (`git config user.name ci` on the runner): one line, green badge, and it redefines the gate as "passes where somebody configured git", hiding the defect from every future bare environment -- a contributor's fresh clone included. **Weakening the refusal to a warning**: the refusal is the feature (M619); the first public commit's attribution is a decision, not a default. **Auto-detection as the fallback** (git's ambient user@host): exactly what M619 exists to exclude.
**Where:** ROADMAP M621, ANECDOTES #76

<a id="dec-m622"></a>
#### M622 — The newest-log pick is deterministic: ties on the whole-second mtime…
in `jc_app_newest_beats` -- used by the one place that picks (`app_newest_jsonl`), pure, unit-tested with the feed order controlled in both directions.
**Rejected:** **Leaving the tie to readdir order**: per-machine and invisible locally -- ext4 hash order made the dev box always pick the control log while the hosted runner picked the stale fixture, so three full local gates could not see it. **Sub-second mtimes** (`st_mtim.tv_nsec`): shrinks the window instead of closing it, adds a porting surface (the field's spelling varies by platform), and same-tick writes still tie. **Fixing only the driver** (removing the stale fixture -- done as hygiene too): leaves every future two-file directory to rediscover the coin.
**Where:** ROADMAP M622, ANECDOTES #77

<a id="dec-m623"></a>
#### M623 — Headless stdin stays a feature; the gate pins it instead
jichi keeps reading a non-TTY stdin as piped context on `-p` runs; smoke_lint 18/19 refuse any driver run that does not pin stdin, and both deadline wrappers escalate with `timeout -k 5`.
**Rejected:** **A jichi-side read timeout on piped context**: truncates slow legitimate pipes (a generator feeding context) and turns a correctness feature into a race. **Polling stdin for readiness first**: the same race, earlier. **`--no-stdin` as the headless default**: a silent behavioural break for every pipe user, to fix a harness hygiene problem. **A permanent hang-regression check**: re-proves per run, in deadline-seconds, what the -k probe and the lint hold structurally; the two-sided demonstration is recorded in the M623 entry instead.
**Where:** ROADMAP M623, ANECDOTES #78

<a id="dec-m624"></a>
#### M624 — Toolchain gates in the grader tier probe usability, never existence
HAVE_RUST/HAVE_ZIG moved onto usable() beside the five language courses; a shim that cannot answer `--version`/`version` under this tier's private $HOME is absent for grading purposes, and the skip says so loudly.
**Rejected:** **Exporting RUSTUP_HOME (or the real HOME) into the workflow**: makes the badge green by preparing the machine -- the M621 rejection again; `make ci` must tell the truth on checkouts nobody prepared, contributors' included. **which() plus a better error message**: still runs four tasks to fail four times; a course either runs against a working toolchain or is honestly absent. **Installing a toolchain in CI**: minutes and a network dependency on every push, for coverage that belongs on machines that HAVE the toolchain -- and it would have buried the shim lesson this row exists to keep.
**Where:** ROADMAP M624, ANECDOTES #79

<a id="dec-m645"></a>
#### M645 — A staleness gate names its files; the tree-wide sweep was…
`docs_counts_lint` checks 14-16 hold three named pages -- CONTRIBUTING.md and VOCABULARY.md on the smoke-tier driver count, README.md on its "Green end to end at **MNNN**" stamp, BENCH_LOCAL_GPU.md on the corpus size -- after the front page sat 158 milestones stale under a sentence every currency check in the tree was pointed away from.
**Rejected:** **A tree-wide rule** ("an exact driver count must carry an M-stamp or a date"): written, then measured before shipping, per the house rule -- **~190 lines state a driver count and ~185 are correct history** (ROADMAP, CHANGELOG, ANECDOTES, `plans/`, and PLATFORMS' deliberately-stamped per-row data). A gate that fires 185 times to catch 5 trains people to silence it, and the same measurement already killed the docs-wide quote lint (1 verbatim quote in 110 blocks). **Comparing the README stamp's CHECK COUNTS to today's tree**: rejected, and this is the subtler half -- a stamp records one past run, so forcing its numbers to equal the present would make every milestone rewrite it into something no run produced. Check 15 bounds the stamp's **age** (40 milestones) and leaves its numbers alone; re-stating them requires re-running the gate, which is what M645 did. **Extending check 13's 25-driver tolerance to these pages**: a tolerance is right for a page that REPORTS a measurement (PROJECT_TIMELINE) and wrong for a sentence claiming what the tier IS, so 14 is exact.
**Where:** ROADMAP M645

<a id="dec-m646"></a>
#### M646 — A staleness check on a measured figure tests its UNIVERSE,…
after M620's retrospective overstated English documentation by ~8,000 lines by counting `docs/i18n/` into a line figure whose companion page count excluded it -- then adding the translations again. `docs_counts_lint` check 18 asks which of the two candidate universes the published number is nearer, and check 17 pins the fuzz-target count to the table.
**Rejected:** **A percentage tolerance on the line figure** (the shape of checks 12-13): measured and rejected -- documentation grew **5.3% in 26 milestones**, so a tolerance tight enough to catch an 8,000-line (6%) universe error fires on ordinary growth every few weeks, and a gate that cries wolf is a gate somebody silences. A universe test cannot be tripped by growth at all. **Re-measuring the figure inside the lint and requiring equality**: it is a `~` figure in prose that a human rounds; equality would make every doc commit a failing build. **Recounting only when check 12's 40-milestone threshold fires**: this recount ran at 26 because the operator asked, and that is the only reason both defects surfaced 16 milestones early -- a threshold promises nothing rots faster than the threshold, and a figure copied instead of counted rots the moment it is written. **Raising the ~35-day no-lines effort allowance to match P13-P18**: rejected as a guess; it is left low and labelled low, because an invented number inside an estimate that asks to be argued with is worse than a stated understatement.
**Where:** ROADMAP M646

<a id="dec-m647"></a>
#### M647 — A platform is re-run on a trigger, never on a…
`docs/PLATFORM_RETEST.md` names seven change classes that invalidate a row, and defines debt as the smoke drivers in the tree minus the drivers the row ran, with thresholds at 25 and 100 drivers.
**Rejected:** **A calendar cadence** ("re-verify every N weeks"): rejected because a row is a claim about a commit and is exactly as true a year later; a calendar re-run tests the calendar. **A simultaneous sweep of all 18 rows before each release**: days of VM and hardware time for one undifferentiated result, and — being expensive — deferred until stale anyway; the per-trigger re-run costs one to three rows at rung 2 or 3. **Measuring staleness in milestones**: milestones vary enormously in how much test surface they add, and the thing that actually went unexercised is a driver. **A lint that detects the triggers**: it would have to classify every diff, and a classifier wrong in the quiet direction is worse than a rule a human has read — so the lint keeps rows *datable* (the precondition) and the page states the hole rather than hiding it. **Deleting rows once their debt passes 100**: deleting measurements is how a project forgets what it learned; the row stays and the claim resting on it is what needs renewing.
**Where:** ROADMAP M647

<a id="dec-m647-2"></a>
#### M647 — A portability claim that reduces to a pure core is…
so only what genuinely needs the kernel waits for the kernel. The illumos binary-`pstatus_t` prediction became `tests/test_meminfo.c:test_binary_status` on Linux.
**Rejected:** **Waiting for an illumos VM**: the claim had stood unverified since M469 while the box remained hypothetical, and the parsing half never needed one. **Promoting the illumos row on the strength of it**: refused — what is tested is a pure function over bytes; that `fopen` succeeds there, that the `pstatus_t` has this shape, and that the `fread` path matches all still need the platform, so the row stays NEVER COMPILED and both pages say so. **Asserting only that the parser finds nothing**: that check passes for a parser that finds nothing anywhere, so the control (the same bytes without the embedded NUL, which *are* found) is what stops the pair being vacuous. **Adding an illumos conditional in advance**: M400's Darwin branch is what inventing conditionals for a platform nobody has produces.
**Where:** ROADMAP M647

<a id="dec-m648"></a>
#### M648 — AHP is watched, not adopted; the bridge stays out of…
jichi remains an ACP **agent server** and does not implement Microsoft's Agent Host Protocol.
**Rejected:** **A C89 AHP server in jichi**: AHP's server is a sessions *host* whose clients are viewers, and jichi is the agent that sits behind such a host — adopting it inverts what jichi is. Sized rather than asserted: jichi's whole ACP implementation is **1,815 lines for 9 methods**, AHP has **96 action types and 392 command definitions**, there is no WebSocket code in the tree, and AHP's correctness model (immutable state, pure reducers, write-ahead reconciliation) is one C89 gives nothing toward — "subtly wrong" there means two clients disagreeing about what the agent did. **An AHP client**: jichi would become a viewer of someone else's sessions. **Adopting at 0.9.0**: this project's own CHANGELOG argues 1.0 is a claim about interface stability that should follow contact with someone else's machine; the same standard applied to someone else's protocol says wait. **An in-tree bridge**: kept out deliberately — jichi already exposes `--output jsonl` and an AF_UNIX newline-framed control channel, so a bridge is a weekend against a published SDK, while the same capability inside jichi is a subsystem larger than its TUI.
**Where:** ROADMAP M648, [analysis/2026-09-17-agent-host-protocol.md](analysis/2026-09-17-agent-host-protocol.md)

<a id="dec-m648-2"></a>
#### M648 — libcurl stays; jichi does not write an HTTP client
The surface is 11 functions and 37 `CURLOPT_` options in one 953-line translation unit, and it stays there.
**Rejected:** **An in-house HTTP/1.1 client**: rejected on a measurement that predates the question — minimal libcurl + static musl + mbedTLS is **804 KB RSS with a verified model call**, against ~500 KB curl-free that cannot make a call at all, so the entire prize is ~300 KB. And jichi speaks HTTPS, so a replacement **substitutes a TLS library for curl** rather than removing a dependency, while giving up the SSRF guard, `FD_CLOEXEC`, the protocol allow-list, redirects-off-by-default, the TLS floor, and portability back to libcurl 7.19.4 (2009) across five libcs and four kernels. Estimated 2,500–4,000 lines of security-relevant C89 to replace 953 lines that delegate. **Vendoring libcurl**: jichi vendors no third-party source. **Leaving footprint unaddressed**: it is addressed — `scripts/minimal-curl.sh` is the measured middle path and is deliberately not part of `make`, because no build of jichi may require compiling a dependency. **Honest gap:** the split of the 804 KB between curl and mbedTLS+musl is unmeasured, and is the first measurement if this is reopened.
**Where:** ROADMAP M648, [analysis/2026-09-17-libcurl-surface.md](analysis/2026-09-17-libcurl-surface.md)

<a id="dec-m648-3"></a>
#### M648 — The population for a text tool-calling mode was measured before…
nine local models, three probes each, one resident at a time — **5 native, 4 text, 0 none**.
**Rejected:** **Designing `toolCalling: "text"` on the assumption that small models need it**: the measurement refutes the premise's usual form in *both* directions — the smallest model in the set is native 3/3, and a **14B coding model** is text 3/3 — so parameter count is not the predictor and any doc saying so is wrong. **Probing a warm shared server**: the first sweep did, and six of ten models returned no verdict because the endpoint answered `HTTP 400 "Failed to load model"`; scoring those as "no tool calling" would have been exactly backwards for four models that are native. An infrastructure failure must never be read as a capability verdict, which is what `JC_TOOLPROBE_UNKNOWN` exists for. **One probe per model**: three were run precisely to find out whether one is enough; 8 of 9 were stable 3-for-3, which is what makes `doctor --live`'s single probe defensible. **Loosening `jc_toolcall_scan` instead of building the mode**: the scanner is *recovery* and is tuned so a false positive is nearly impossible, because inventing a call the model never made is worse than missing one; a declared mode inverts that to recall and lets jichi choose an unambiguous syntax.
**Where:** ROADMAP M648, [analysis/2026-09-17-local-tool-calling-sweep.md](analysis/2026-09-17-local-tool-calling-sweep.md)

<a id="dec-m650"></a>
#### M650 — A named work gets a full citation; a named concept…
after six design tutorials listed fourteen works under *"search these; prefer primary sources"* with no publisher, year, ISBN, DOI or URL. All fourteen became verified `BIBLIOGRAPHY.md` entries; the concepts in the same lists (bounded context, coupling and cohesion, equivalence partitioning, flaky tests) stayed concepts.
**Rejected:** **Leaving the lists as they were**: "search these" is defensible for an idea and indefensible for a book — the reader cannot tell which edition, and this project's own register is that a claim carries its evidence. **Citing from memory**: refused; four ISBN-13s were resolved against Open Library and two papers cited by DOI *because* their publishers answer an automated request with 403, which is the case the page's marker table already covered. Searched-for open copies that returned 404 are not claimed. **Inventing an open mirror for the paywalled pair**: a citation nobody probed is the defect wearing a URL. **A lint that detects an uncited work in prose**: measured and rejected — it needs a classifier over prose, and M645's measurement against this tree says such gates fire overwhelmingly on correct history; check 8 holds the *route* instead, and its header states that limit rather than implying coverage. **Including `CHOOSING_A_MODEL.md` in that check**: its outward list names only concepts, so it has nothing to cite and telling a reader to search a concept is correct.
**Where:** ROADMAP M650

<a id="dec-m651"></a>
#### M651 — A local model may draft a translation for a reviewer;…
measured rather than assumed. Five local models translated a passage whose reviewed Japanese is already in this tree; `llm-jp-4-8b-thinking` alone got both pre-registered criteria right and is fit to draft.
**Rejected:** **Bulk-translating the documentation with a local model**: two of five produced meaning-level errors on 1,084 characters — "under supervision" as 下位で ("in a lower position") and as 監視下で (surveillance) — and one **fabricated a different English document with invented week counts** and repeated it six times. Across 466 pages that is not a translation project but a corpus of plausible claims nobody can check, against a page set whose value is that its claims are checkable. **Asserting the policy instead of testing it**: `presentations/README.md` already said a confident wrong translation is worse than an absent one; this replaces the assertion with evidence, and the evidence is stronger than the assertion was. **Judging the two reasoning models on the first run**: they returned empty content at `max_tokens` 1,200 and the obvious reading would have been wrong for one — re-run at 8,000, the 27B translated. Caps stay off for measurement runs precisely because a cap that fires manufactures a plausible different answer. **Leaving the ja numeral debt until a Japanese writer is available**: rejected — the substitutable half needs no Japanese judgement at all, and separating it from the prose half took the marker from 25 to 0 while stating exactly what remains owed.
**Where:** ROADMAP M651, [analysis/2026-09-17-local-japanese-translation.md](analysis/2026-09-17-local-japanese-translation.md)

<a id="dec-m657-4"></a>
#### M657 — A cap that says nothing is a cap nobody can…
which re-scopes the three M609 peer-transport rows. Their stated blocker was "a memory-pressure harness the smoke tier does not have" — but RSS was never the property: each fix makes the bound observable as an error, a closed client, or a bounded exit, so a mock streaming past the ceiling produces a behavioural difference, exactly as M438's liveness test turned out to be about latency rather than about an answer.
**Rejected:** **Building the memory-pressure tier**: a new test tier for three one-line fixes, whose red would have been a byte count nobody could read in a failure report. The one case that genuinely has no observable — ACP, which has no deadline — is answered by making the cap *announce itself* rather than by measuring it, which also settles the `tool-output cost` row's standing revisit condition ("a way to make the adaptation visible at the point it happens").
**Where:** ROADMAP M657, [DEFERRED.md](DEFERRED.md) §peer transport

<a id="dec-m657-2-2"></a>
#### M657 — "Blocked" is a factual claim, so it gets checked like…
illumos had been filed as a never-compiled platform for four months; checking what the word meant found `/dev/kvm`, group membership, `qemu-system-x86_64` and 137 GB free **on this machine**, so the row costs a session rather than a resource. The three never-compiled/stale platform rows are now distinguished by what actually stands in the way: **access** (macOS — hardware nobody here has), **a session** (illumos), **a power switch** (the ARM bench rows, all three unreachable on 2026-09-18).
**Rejected:** **Carrying one undifferentiated "platform work" backlog**, which is how illumos sat behind macOS for months although only one of them was ever actually blocked. Also rejected: **promoting illumos on the strength of the M647 `pstatus_t` measurement** — that measured a pure function over bytes and says nothing about a kernel, and the row stays NEVER COMPILED until a box runs it.
**Where:** ROADMAP M657, [PLATFORM_RETEST.md](PLATFORM_RETEST.md) §6

<a id="dec-m658-2"></a>
#### M658 — A platform's build requirements are probed, never keyed on uname
The first illumos row needed two things no other platform in the matrix needs: `-D__EXTENSIONS__`, because Solaris hides every non-POSIX interface when `_POSIX_C_SOURCE` is defined and `struct winsize` / `TIOCGWINSZ` go with it; and `-lsocket -lnsl`, because `accept`, `listen` and `bind` are not in its libc. Both are Makefile probes in the existing `HAVE_CLOCK` idiom — compile the four-line question, and only if it fails, ask again with the extra thing. On Linux and the BSDs the first form succeeds and nothing is added.
**Rejected:** **`ifeq ($(shell uname -s),SunOS)`**: a platform name is a proxy for the question, and the proxy is what rots — the Darwin branch M400 found had been uncompilable for milestones because nothing ever asked it anything. Also rejected: **defining `__EXTENSIONS__` unconditionally**, which is harmless in practice and states a Solaris fact about every platform, and **a source-level `#if defined(__sun)`**, which is the BSD-conditional the M460/M461 rows measured their way out of: ten defects, and not one of them wanted a conditional.
**Where:** ROADMAP M658, [PLATFORMS.md](PLATFORMS.md)

<a id="dec-m675"></a>
#### M675 — The course scaffold writes no config
`jichi init course` writes a route, a coaching skill and a command, and deliberately stops there. The plan it implements ([`plans/2026-09-language-course.md`](plans/2026-09-language-course.md) §4.2) said it should write "a config (chat model, an `embed`-role model, the `docs` sources)", and every one of those three is a guess: the docs path is the snapshot the learner just fetched and only they know where it landed; the embed model is whatever their machine actually has; and the chat model likewise. A scaffolded config full of placeholders is **worse than no config**, because `doctor` then reports a setup that FAILED rather than one not yet done — and a beginner cannot tell those apart. `COURSE.md` shows the four lines to add instead, next to the command that prints whether they worked.
**Rejected:** **Writing a config with placeholder values**: the failure above. **Writing a config with the values detected from the environment**: `setup` is the wizard that does this, it asks before writing, and duplicating a quarter of it inside `init` would drift. **Prompting during `init`**: `init` is non-interactive by contract (it runs under `--dry-run`, in scripts, and in the smoke tier), and the one flag that would make it interactive is a different feature.
**Where:** `tests/smoke/init.sh` check 18, which watches **all three** paths a project config can occupy — the first draft watched only `.jichi/config.json` and a perturbation adding a root `config.json` walked past it

<a id="dec-m684"></a>
#### M684 — An absent journal key means "nobody asked"; an empty one…
The `out_of_scope` record's new `tracked` array names the subset of flagged paths that git knows about. Three alternatives were rejected. A **parallel array of booleans** indexed against `paths`: rejected because it desynchronises silently and a reader cannot distinguish a short array from a false one. **Rewriting `paths` into objects** carrying their own trackedness: rejected because it breaks every existing reader, including `tests/measure/strict_green_fp.py`, which is the one reader the field exists to serve. **Emitting an empty array when git cannot answer**: rejected because it is the conflation that reversed M662's own recommendation — a rule reading "no tracked paths" out of a workspace with no version control would downgrade every run there. So the distinction lives in the wire format, where a reader cannot fail to notice it, rather than in a convention they have to know. A future reader could reasonably prefer the object form for its self-description; the cost is a flag day for the measurement corpus, and the corpus is the whole point.

<a id="dec-m687"></a>
#### M687 — A capped turn is reported in the reach footer, not…
A headless turn that stops at the tool-call cap produced an empty answer, exit 0, and an envelope saying `verified ok`. Three alternatives were rejected. **Printing a notice on stdout**: rejected because M73 makes stdout the raw answer so a caller can pipe it, and prose there breaks every such script — the defect would be traded for a worse one. **Flipping the exit code to non-zero**: rejected *here*, not on the merits. M322 chose 0 deliberately (the cap is a circuit breaker; the history is intact and another prompt resumes), and the counter-argument — that a `--no-session` one-shot has no next prompt, so the work really is lost — is real but is a change to a stable-tier contract. This project has already withdrawn one exit-code flip for being proposed ahead of its measurement, so the argument is recorded in `DEFERRED.md` and left for the operator. **Raising the default cap**: rejected as treating the symptom; the run that prompted this had the answer by call sixteen of two hundred and then looped, so a larger cap buys more looping. A future reader could reasonably prefer the exit-code flip, and the row in DEFERRED is where that case should be made — with a corpus, not an argument.

<a id="dec-m688"></a>
#### M688 — One classifier for why a run stopped, guarded by -Wswitch…
Eight stop reasons times five reporting surfaces is forty cells, and two were found empty in two days. Three alternatives were rejected. **Leaving it to review**: rejected because M687's empty cell survived fifteen milestones *after* M322 documented exactly what was wrong with it — the register of a defect is not a guard against it. **A runtime assertion that every surface agrees**: rejected because it fires on the developer who already made the mistake, at a moment when the run is already going wrong; `-Wswitch` fires on the one adding the reason, before it can ship. **A `default:` label that renders unknown reasons generically**: rejected outright, and it is the tempting one — it makes the code compile and turns every future omission back into a silent cell, which is the entire defect. The cost of the choice is that adding a stop reason now breaks the build in several files at once; that is the intended behaviour, not a side effect. A future reader could reasonably want a lookup table instead of switches, and would then have to find another way to make omission fail loudly.

<a id="dec-m505"></a>
#### M505 — jichi chooses no provider and no model — reversing M505
A fresh install resolved to `api.anthropic.com` and `claude-opus-4-8`, because `default_model()` returned that id for any provider that was not `openai` and the provider itself fell back to `anthropic`. M505 found exactly this, wrote it up, made `doctor` warn about it, and deliberately kept the default — *"a reporting defect, not a resolution one"*. The operator then installed jichi on a second machine and was told, correctly, that it was configured to spend money on a model nobody had chosen, and decided the other way. **Rejected alternatives:** keep the default and warn harder (M505's position — a warning does not stop the request, and the id was a priced frontier model); default to a free local endpoint instead (still a choice made for the user, and it fails on a machine with no server); default to the cheapest id of a named vendor (jichi cannot know what an id costs *you*, and the list goes stale). **What replaces it:** nothing is substituted, `doctor` fails with `no model is configured`, and `jichi setup` asks the endpoint which models it serves via `/v1/models` rather than suggesting one. `priced_model_lint` checks 5 and 6 now cover `src/` — its exclusion comment had read *"NOT src/ — jichi's own code names no model id"*, which was false when it was written.
**Rejected:** M709
**Where:** [`CONFIG_TUTORIAL.md` §0a](CONFIG_TUTORIAL.md#0a-if-this-is-your-first-config--start-here)

<a id="dec-m709"></a>
#### M709 — A vendor's key goes to that vendor's own endpoint, and…
An entry that named no `apiKey`/`apiKeyEnv` took `OPENAI_API_KEY` for provider `openai` and `ANTHROPIC_API_KEY` for **everything else** — an unset or unrecognised provider included — and sent it wherever the entry pointed; `tests/smoke/no_house_key.sh` observed `x-api-key: <ANTHROPIC_API_KEY>` go to a mock for an entry with no provider. The operator decided (2026-09-23): the conventional variable is read only when the entry names that vendor **and** the endpoint is the vendor's own (`https://`, the exact host, no userinfo, port 443 or none). **Rejected alternatives:** the OpenAI-SDK convention of sending `OPENAI_API_KEY` to any base URL (a refused request costs one config line and is reversible; a key sent to the wrong host is not — and `"provider": "openai"` names a wire dialect, which is exactly why it cannot identify a vendor); keeping the fallback and warning in `doctor` (M505's position on the default, and M709's reason for reversing it: a warning does not stop the request); matching the vendor's host by substring or by `jc_url_host` alone (`api.openai.com@evil.example` and `api.openai.com.evil.example` are the rows that parser gets wrong, and both are unit-tested). **The converter had the same defect one layer down:** `jichi-convert` wrote `"apiKeyEnv": "OPENAI_API_KEY"` for every keyless model and dropped the name inside `${{ secrets.X }}`, and a NAMED variable is honoured anywhere — so it now writes no key line for a keyless model and the variable a template names, and `doctor` warns about a vendor's variable named for another host, which is what configs converted earlier carry.
**Rejected:** M718
**Where:** [`CONFIG_TUTORIAL.md` §1.5](CONFIG_TUTORIAL.md#15-what-jichi-does-with-the-key-the-resolution-order)

<a id="dec-m718"></a>
#### M718 — A missing dialect is refused where a model is CALLED,…
`jc_provider_create` no longer guesses (it defaulted to Anthropic's dialect unless the model id contained `gpt`), so an entry naming no provider has no provider. **Rejected alternatives:** refusing at program start (the first draft: it took down the TUI on four smoke drivers, because the assignments listing, `/model` and every local command need no model, and the empty config the tier runs on names none); an inert placeholder provider (a dialect chosen for nobody is the house default again, one level of indirection away); NULL checks at the ~30 sites that read `app->provider` (one guard at the top of `jc_agent_run_turn` covers every front-end — TUI, headless, daemon, ACP — because every turn passes through it). **Kept at startup:** a run that exists only to call a model (`-p`, `serve`, `daemon` — no terminal) is still refused before anything is sent, exit 2, when the entry names a model but no dialect. **Found on the way:** `jc_app_switch_model` made the new entry active before creating its provider and returned with it active on failure, so the old provider would have read the new entry's endpoint and key through its pointer — latent while creation failed only on OOM.
**Rejected:** M718
**Where:** [`CONFIG_TUTORIAL.md` §1.5](CONFIG_TUTORIAL.md#15-what-jichi-does-with-the-key-the-resolution-order)

<a id="dec-m719"></a>
#### M719 — config validate fails what jichi refuses to run, with doctor's…
`{"models":[{"name":"a"}]}` printed `OK … exit 0` while `doctor` failed it — the DEFERRED row's premise (a priced default) had been false since M709. The operator decided "fail" (2026-09-23). It fails for no model id and — since M718 — for a named model with no wire dialect: the two things the program refuses. **Rejected alternatives:** a pure parse check (its OK becomes a statement about a config the program will not run); surfacing doctor's warnings too — no key, no pricing (two renderers of one judgement drift apart, the M431 argument the row always made). The sentences are shared functions (`jc_config_no_model_advice`, `jc_config_provider_problem`), and `tests/smoke/validate_runs.sh` compares validate's words against doctor's actual output, so the two cannot drift silently.
**Rejected:** M719
**Where:** [`DEFERRED.md`](DEFERRED.md) (row closed)

<a id="dec-m720"></a>
#### M720 — A measurement's population is classified before it is counted, by…
The workstation's `~/.jichi.d` was 40 % agent probes against `mockmodel` that had used the real `HOME`. A session is synthetic when a model it called is named `mock` or every answered call took under 50 ms — a threshold measured, not chosen (mocks ≤ 1.25 ms, the fastest real session 244.9 ms); a journal is classified by its own `start.model` or joined to telemetry on `run`, and one with neither is kept as *unverified*. **Rejected alternatives:** classifying by workspace path (real drives also run in scratch clones — the zigodot drive did); deleting the probes from the store (it destroys the evidence of the mistake along with the mistake, and cannot reach the other machine's store); relying on the new runbook rule alone (a habit, and the existing contamination stays). Every script prints what it dropped and why, and `--include-synthetic` restores it, because a filter nobody can see is a second source of truth.
**Rejected:** M720
**Where:** [`SESSION_RUNBOOK.md`](SESSION_RUNBOOK.md) §3

<a id="dec-m721"></a>
#### M721 — A shell's stderr is redirected once, for the whole shell…
The popen path ran `<command> 2>&1`, which binds to the last simple command only. **Rejected alternatives:** a brace group, `{ <command>\n} 2>&1` (it parses the command — an unbalanced brace or a heredoc changes what it means; `exec` with only a redirection needs no parsing, POSIX); always taking the watched fork path, which dup2()s fd 2 (the right shape, but it replaces M258's tick loop for every command, a larger change than the defect); heap-allocating the line so any length runs (it trades a uniform, explained refusal for a platform-dependent `E2BIG` from `sh -c`, and `write_file` is the right tool for long content — revisit if a legitimate use meets the limit).
**Rejected:** M721
**Where:** [pilot §6](analysis/2026-09-23-the-corpus-pilot.md#6-under---auto-a-commands-stderr-is-copied-onto-jichis-own)

<a id="dec-m722"></a>
#### M722 — A warning flag a supported compiler may lack is probed,…
`-Walloca` joined the mandatory list at M472, and every gcc before 7 rejects it as an error, so the tree could not compile on CentOS 7, on Debian 9, or on the CentOS 6 / Debian 7 floor `INSTALL.md` promises. **Rejected alternatives:** raising the documented floor to gcc 7 (it retires two recorded rows and the MiNT toolchain to keep a tripwire that only fires on code C89 already forbids); deleting `-Walloca` (gcc 7 and later and clang would lose the tripwire M472 wanted, which is why check 27 exists); a wrapper compiler in the gate that rejects this one flag (it catches this flag and no other, while a baseline catches the next); a baseline recalled from GCC release notes (the nine flags are the ones gcc 4.6.4 actually compiled 299 files with).
**Rejected:** M722
**Where:** [plan §8](plans/2026-09-freemint-aranym.md#8-step-0-run-on-2026-09-24--the-outcome-against-the-predictions)

<a id="dec-m723"></a>
#### M723 — A header a libc may hide is probed, and the…
MiNTLib declares `lstat`, `readlink` and `symlink` only at an X/Open level, and `pid_t` in `<unistd.h>` likewise; its `<sys/select.h>` does not define `struct timeval`. **Rejected alternatives:** defining `_XOPEN_SOURCE=600` for every platform (it changes the declared feature set everywhere, where the probe changes nothing on glibc, musl, the BSDs or illumos); keying the flag on `uname` or on `__MINT__` (the probe asks the compiler's question, as M657c's does); a `jc_compat.h` that every file includes (one more layer to keep right, for four facts that each have a standard header); dropping `mmap` everywhere to avoid the guard (it gives up M141's evictable, shared mapping on every platform that has it).
**Rejected:** M723
**Where:** [plan §8](plans/2026-09-freemint-aranym.md#8-step-0-run-on-2026-09-24--the-outcome-against-the-predictions)

<a id="dec-m724"></a>
#### M724 — The registry of key names to scrub grows, the popen…
The prefix lived in a 1 KB buffer and was dropped whole when it did not fit; the registry stopped at 32 names. **Rejected alternatives:** a bigger fixed buffer (it moves the cliff and keeps the silent drop past it); refusing to start when a config names too many keys (it turns a scrubbing limit into a config limit nobody asked for); `env -u NAME` in place of the shell prefix (`-u` is not POSIX); `env -i` with an allowlist (children legitimately need `PATH`, `HOME` and the locale, the reason HARDENING.md scrubs by name); keeping 0 for "does not fit" and teaching the caller to measure first (a return value that means two things is how the leak happened).
**Rejected:** M724
**Where:** [HARDENING.md](HARDENING.md)

<a id="dec-m725"></a>
#### M725 — The JSON parser sizes each string by its own span,…
`parse_string` reserved the rest of the input for every string and kept it. **Rejected alternatives:** the fast path itself, reading floats straight from the reply (zero bytes off the peak once the sizing was fixed, for ~240 lines, a cJSON extension and a fuzz property); `realloc` down to size after decoding (still reserves the rest of the input once per string, and pays a copy); leaving it because resident memory on Linux barely moved (the requested bytes are real where nothing pages them lazily: FreeMiNT, a strict commit limit, a `ulimit -v`).
**Rejected:** M725
**Where:** [the massif page](analysis/2026-09-22-index-embedding-allocation-massif.md)

<a id="dec-m726"></a>
#### M726 — A MiNT build defines its own 512 KB stack, in…
FreeMiNT's stack is fixed at `_stksize` bytes, 64 KB by MiNTLib's default, and the unit suite overran it. **Rejected alternatives:** the post-link `stack` tool as a Makefile step (a MiNT-only build step whose name follows the toolchain prefix); raising `TPA_INITIALMEM` in the guest (a system setting, not the program's); moving only `test_vision.c`'s 64 KB buffer to the heap (it fixes the test that tripped, not a program whose stack use nobody has measured); a stack of 1 MB or more (a 4 MB stack did not start: MiNT's default initial allocation is 1,024 KB).
**Rejected:** M726
**Where:** [plan §9](plans/2026-09-freemint-aranym.md#9-steps-1-and-2-run-by-hand-on-2026-09-24--and-what-they-found)

<a id="dec-m727"></a>
#### M727 — The path resolver follows links in a loop, and the…
The resolver recursed once per symlink hop at ~16.5 KB a call, so a cycle took ~700 KB of stack before its 40-hop bound refused it, and MiNT's stack is a fixed 512 KB. **Rejected alternatives:** raising `_stksize` (the depth is set by the bound, so this moves the cliff, and every byte comes out of MiNT's 1,024 KB initial allocation); lowering the hop bound (it changes which legitimate chains resolve, to fit a stack); moving the buffers to the heap (an allocation failure in every path check, for a frame the loop needs only once); `-Wstack-usage` (it bounds one frame, and one frame was never the problem: the depth was); running every gate stage under the limit (ASan's frames are several times larger, so the limit would measure the sanitizer); a stage of its own (the curl-free stage already builds exactly what FreeMiNT runs, so the limit costs no time there).
**Rejected:** M727
**Where:** [plan §10](plans/2026-09-freemint-aranym.md#10-the-rig-run-on-2026-09-24--and-the-recursion-its-fresh-disk-found)

<a id="dec-m728"></a>
#### M728 — The unit suite removes fixtures through one helper that refuses…
Twenty-five `rm -rf %s` lines in fixed buffers removed the whole `TMPDIR` when its path was 127 characters long. **Rejected alternatives:** larger buffers at each site (the path's length is set by an environment variable nobody controls, so this moves the cliff); quoting the argument (it fixes a space, not a cut); a fresh `mkdtemp` root per run (it still starts with the same long `TMPDIR`); the length guard alone (a buffer under the limit is safe only if every suffix was sized for it, which is the assumption that failed); `nftw` for the walk (XSI, where the tree already walks with `jc_list_dir`); keeping `tests/` outside `sprintf_lint` (the exclusion rested on "known-safe buffers", a claim that depended on `TMPDIR`).
**Rejected:** M728
**Where:** [CONTRIBUTING.md](../CONTRIBUTING.md)

<a id="dec-m729"></a>
#### M729 — The gate runs the unit suite on a TMPDIR it…
On such a `TMPDIR` the suite segfaulted at a `JC_CHECK(E != NULL)` whose next line used `E`, and 172 lines have that shape. **Rejected alternatives:** converting all 172 by hand now (120 of them are in shapes a transform cannot rewrite safely, and the gate line already holds the environment where they bite); a lint over the shape first (it would fail on 120 sites the milestone does not change); `/nonexistent` as the `TMPDIR` (root can create it, and CI often runs as root, where a path below a regular file fails with `ENOTDIR` for everyone); making `jc_sb` data never NULL (a product change, made to spare a test harness).
**Rejected:** M729
**Where:** [CONTRIBUTING.md](../CONTRIBUTING.md)

<a id="dec-m733"></a>
#### M733 — D1's note resets on an edit, on a shell command…
The success twin of M432 counts (tool, arguments, result) and tells the third repeat. **Rejected alternatives:** a shell command resetting nothing (the fit's offline rule -- an edit made through `sed` then looks like no change, and the re-run test after it like a loop); every shell command resetting everything (blind to the loop the corpora measured most, the same command with the same output -- M687's 13, the pilot's 51); a per-command tree fingerprint (a walk of the tree per shell command, in every run, to sharpen an advisory note); resetting at every mid-turn compaction, as the plan sketched (replayed on the drive, it erased bonsai 06's loop: under pressure a pass elides nearly every round, and it elides old results, not the repeats).
**Where:** [ROADMAP M733](ROADMAP.md), [plan §2](plans/2026-09-after-m712.md#2-d1--a-no-progress-detector-for-successful-repeats)

<a id="dec-m734"></a>
#### M734 — An inferred constraint advises and never refuses; a negation reaches…
(M734, plan D2 option (c), chosen by the operator 2026-09-24). Over 289 stored prompts the scanner inferred nine rules and six were wrong, each forbidding the gate its task named, and all six crossed a sentence. **Rejected alternatives:** (a) the sentence scope alone with inferred rules still binding (it fixes the measured mechanism, but the sample was four prompts on one machine, and the next misparse class would again refuse a run's own gate); (b) advisory alone (it leaves the scanner telling the model six wrong rules for every three right ones); turning auto-adopt off in `--auto` (loses the instruction that scrolls out of a long unattended run -- M110's reason for existing); asking for confirmation (`--auto` has no one to ask); another per-phrase guard (three incidents, three guards, and none made the next misparse visible).
**Where:** [ROADMAP M734](ROADMAP.md), [CONSTRAINTS.md](CONSTRAINTS.md), [plan §3](plans/2026-09-after-m712.md#3-d2--inferred-constraints-that-explain-themselves)

<a id="dec-m743"></a>
#### M743 — A kind of file is a scope, not a blanket…
Measured over threadwork's 656 stored messages, *"Do NOT edit any source; your only file output is the design doc named below."* inferred read-only for a run whose deliverable was a file, and *"Do not touch src/gdscript/codegen.zig."* would have the moment *touch* became a verb. *code*, *source*, *sources* and *src* left the blanket list; *file(s)*, *anything*, *everything*, *repo*, *workspace* and *tree* stayed. **Rejected alternatives:** keeping the kinds blanket and excepting briefs that name an output file (a guard for one phrasing, and the M167/M168/M207 lesson is that the next phrasing arrives); keeping them and relying on M734's advisory (it tells the model a rule the operator never gave, and a model may obey it); dropping the read-only inference altogether (it is right in 11 of 13 messages at HEAD and misses the 16 drive tasks that need it). **The cost, stated:** a brief that means *"do not modify the code"* as read-only gets no backstop, only its own sentence.
**Where:** [ROADMAP M743](ROADMAP.md), [analysis](analysis/2026-09-25-the-scanner-read-again.md), [CONSTRAINTS.md](CONSTRAINTS.md)

## Drive process — the operator's review of 2026-10-20 (supervisor note 00:22)

The operator reviewed the hardening drive's state and process and accepted the supervisor's
eight recommendations ("Go with your recommendations for all eight"). Five of the eight restate a
decision this register already holds (the running-time spool bound stays given up is [M984](#dec-m984);
master moves per batch is the drive's standing landing rule in PLAN.md §2; the documentation branch,
the 20-minute check-ins and the pinned-build-follows-master are standing process), so they are named
there, not re-decided. The three that carry a rejected alternative of their own are recorded below.

<a id="dec-2026-10-20-d1"></a>
#### 2026-10-20 (d1) — the drive lead does one-line changes itself and dispatches only larger work
The operator confirmed the lead's practice: a change that fits one line (a record, a count, a trailer) is the lead's own work, and dispatching a worker for it spends verification for nothing — the bottleneck is proving the teeth (the red-first check and the supervisor's re-check), not typing. **Rejected alternatives:** dispatching every change for consistency (a worker's commit still needs the lead's and the supervisor's re-run, so the two checks are paid either way while the typing adds a round-trip and a review surface); letting the lead defer one-liners to a later run (a record left uncommitted is the thing the 30-minute rule exists to prevent). The lead keeps dispatching work larger than one line, and every worker result still passes the same lead re-run and the same supervisor re-check the lead's own edits pass.
**Where:** the supervisor's 2026-10-20 00:22 note (item 1), `/records/runs/0035/notes/2026-10-20-0022-supervisor-operator-decisions-review.md`; the drive's LOG (the one-liners committed directly by the lead, e.g. the M996 records).

<a id="dec-2026-10-20-d2"></a>
#### 2026-10-20 (d2) — the 30-minute commit rule gets a mechanical backstop, history is never rewritten
The harness now copies the tree's uncommitted files into the run records every 30 minutes, outside git, so a run that dies does not lose work and the supervisor can see what a lead had in flight; the rule itself is unchanged (commit verified work before each dispatch, uncommitted at most 30 minutes). **Rejected alternative:** amending or rebasing a committed change to add the attribution trailers the `trailer_block_lint` checks — run 0034 did exactly this (rebase + `reset --soft` to add trailers to two commits) and believed them unpushed, but the harness pushes every 20 minutes DURING a run, so the rewritten history diverged from GitLab and the mirror refused the push. The decision the backstop enforces: fix a mistake with a new commit, never by replacing a pushed one.
**Where:** the supervisor's 2026-10-20 00:22 note (item 2) and 00:58 note (`/records/runs/0035/notes/2026-10-20-0058-supervisor-never-rewrite-pushed-history.md`); the drive's LOG (run 0034's branch repair and the never-rewrite rule).

<a id="dec-2026-10-20-d7"></a>
#### 2026-10-20 (d7) — `make ci` gains one optimised, fortified, `-Werror` build stage
`make ci` now carries a second unit-suite build at `OPT=-O2` with fortification and `-Werror` (landed in the M996 gate work), so a warning that the default no-`-O` build cannot see — the unchecked `ftruncate` `[-Wunused-result]` behind the L-27 drain finding — fails the gate instead of shipping. **Rejected alternatives:** fixing each such warning ad hoc as found (the default build has no `-O`, so it never produces the warning, and the next one of the class would again ship until somebody ran an optimised build); a separate always-on fortified *default* (it would change every machine's ordinary build and its timings, for a stage the gate can run once). The stage's red-first is natural and was taken before the fix: the new stage is RED on the unfixed tree (it aborts at `jc_bg.c:377`) and GREEN after (15746 checks, 0 failures, zero warnings).
**Where:** the supervisor's 2026-10-20 00:22 note (item 7); [ROADMAP M996](ROADMAP.md); the `Makefile` `ci` stage; the drive's FINDINGS L-27.

<a id="dec-m1002"></a>
#### M1002 — `jc_state_file_check` keeps its "under-rootness guaranteed by construction" contract; no per-call containment check
Review 5 (supervisor 2026-10-20 14:05, item 2) asked either to implement D-15's "require the path under `app->root`" in `jc_state_file_check` or to record the argument for why not. The argument, measured: the helper's `root` parameter is inconsistent ACROSS call sites by design — `jc_learn.c`/`jc_memory.c` pass `app->root` (canonical, `main.c:17669`), while `jc_board.c`, `jc_progress.c` and the plan tool pass the caller's workspace string directly — and the under-rootness every call site relies on is a PROPERTY OF THE PATH ARGUMENT (built as `<base>/.jichi/<fixed leaf>` from the process's own cwd), not of `root`. `getcwd` (POSIX) returns an absolute canonical path, so `app->cwd` and `app->root` agree in practice (probed 2026-10-20 on this tree: both `/tmp`-shaped, `jc_path_in_root(root, cwd + "/.jichi/x")` is 1); a containment test against one of the two redundant canonical bases would pass vacuously, and against a base the path was not built from (e.g. `app->root` at the board/progress sites) would need every call site to switch to passing the fence root — a 25-caller signature sweep to re-check what the path construction already guarantees. The per-call teeth remain the two lstat-based refusals the helper already does: directory part is a symlink → DENIED, leaf is a symlink/non-regular → INVALID (the D-15/M997 class, where the path's NAME, not its containment, is the threat). **Rejected alternatives:** (a) `jc_path_in_root` inside the helper — rejected: either redundant (path built from the same canonical cwd) or breaks call sites that pass a non-fence root; the redundancy was measured, not assumed. (b) a per-call `realpath` fence on the helper regardless of `root` — rejected: changes the contract for all 25 call sites and re-resolves a path the caller just constructed. **Where:** `src/util/jc_path.c` (`jc_state_file_check`), the D-15 guards in `src/chat/jc_board.c`, `src/util/jc_progress.c`, `src/util/jc_learn.c`, `src/chat/jc_memory.c`, `src/main.c:17669`; the drive's LOG run 0040.

<a id="dec-m1006"></a>
#### M1006 — the dirty-tree dispatch guard's product default is PROVISIONAL `warn`
The operator chose the guard on 2026-10-20 17:47 (it was LOST to the register before being built -- the incident that produced the 2026-10-23 rule "a tool or guard that would help the drive gets a design note when it is found, and is built when we need it") and ordered it built 2026-10-23 09:19, with the drive running `refuse` (09:20). **Chose:** a WRITE dispatch (`spawn_parallel` write tasks / `spawn_subagent` write tools) checks the live workspace for uncommitted changes before any fork; `strictness.dirtyDispatch` takes `off` / `warn` / `refuse`, product default **`warn`**, PROVISIONAL. **Rejected:** `refuse` as the product default -- it hard-stops a healthy lead with a legitimate in-progress file in every environment, and the provisional default exists so the drive (a disciplined, supervised context) measures `refuse` while the product measures `warn`; `off` as default -- invisible state is the defect the guard exists to close; a flat boolean key -- cannot express the three modes or the escape; stashing the dirty tree before dispatch -- a stash is itself a write and strands work when the batch dies (the M855 one-stash-stack incident is the class). **Because:** the base of a write dispatch is not a committed tree (the parallel base includes uncommitted work: `jc_snapshot_take` runs `git add -A` first, `src/snapshot/jc_snapshot.c:643`), unverified work gets built on (the commit-then-dispatch rule was broken four times in three days, the drive's own log), and no record otherwise survives to say the tree was dirty. The provisional default is DECIDED BY MEASUREMENT, named so a future run can close it: the `dirty_dispatch` telemetry summary (checked / refused / unknown) across drive runs once the drive is in `refuse` -- if `warn`-visible dirty dispatches that then caused a base problem are rare, `warn` stays; if they are the norm, `refuse` becomes the default. Two refinements the supervisor measured into the build (M1006-a): a typo value falls through to the default but LOUDLY (a config-load notice naming the key, the value and the mode in effect; `doctor` prints the effective mode), because a typo in the drive's own `refuse` would otherwise run it half-open unobserved; and a probe that FAILS in a git workspace reads `unknown` (proceeds with a note under `refuse`; the telemetry field says `unknown`, never `false` -- the classifier's else-branch says unknown, M519/M628), because an infra failure must not block work and must not masquerade as a clean tree. **Where:** `docs/plans/2026-10-23-m1006-dirty-dispatch-guard.md` (the design note), `src/util/jc_dispatch.{c,h}`, the `strictness` parse in `src/config/jc_config.c`, the check sites in `src/tools/jc_tool_parallel.c` / `src/tools/jc_tool_subagent.c`, the reader in `src/util/jc_telemetry.c`, `tests/smoke/dirty_dispatch.sh`; the drive's LOG-2 run 0074.

## Platforms and portability

Scan the one-liners; each links to its full reasoning — the rejected alternatives — below.

| Decision | Detail |
|---|---|
| **A build without sockets is decided by the header, and it refuses instead of degrading** (M835) | [M835](#dec-m835) |
| **A socket-less build's contract marks the daemon unavailable instead of dropping it** (M836) | [M836](#dec-m836) |
| **Every file is opened binary, with the rule in a lint, not a global mode switch** (M837) | [M837](#dec-m837) |
| **Without long file names jichi warns and goes on; it asks the C library at run time** (M838) | [M838](#dec-m838) |
| **An unusable time zone is repaired for the process, before the first timestamp** (M839) | [M839](#dec-m839) |
| **An explicit `--readonly` is pinned: no mode change may clear it, and a read-only run is not promoted** (M840) | [M840](#dec-m840) |
| **A background command's output is spooled to a file, not a pipe: a chatty child never blocks on a reader** (M940) | [M940](#dec-m940) |
| **Topology B is the operators one-line config switch, off by default (M952)** | [M952](#dec-m952) |
| **A chatbot runs in chat mode with `--readonly`, one process per message, never plan mode** (M841) | [M841](#dec-m841) |

<a id="dec-m835"></a>
#### M835 — A build without sockets is decided by the header, and it refuses…
DOS has no socket library unless Watt-32 is linked, and never Unix-domain sockets. The Makefile sets `JC_NO_SOCKETS` only when every socket-library probe fails **and** `<sys/socket.h>` does not compile, and such a build refuses the daemon, `--connect` and `jichi control` with exit 2 and a message. **Rejected alternatives:** keying on `__DJGPP__` (it asks who made the compiler, not whether the header exists, and a DJGPP with Watt-32 has sockets); setting the flag when the *link* probe fails (a system whose socket library the probes do not know would then build a jichi without its daemon, silently, where today the link fails and names the symbols); stubbing the socket calls to fail at run time (every call site would grow an error path that no gate exercises); guarding the SSRF classifier away together with the sockets (M190's lesson: that removed IP-literal checks from every build that lacked the guard's condition, so a pure IPv4 parser, held equal to `inet_pton`, replaces it instead); and compiling the connect-time SSRF guard out when libcurl is present without socket headers (that pair is an `#error`: libcurl cannot exist without sockets, so the headers are somewhere the probe did not look).
**Where:** [ROADMAP M835](ROADMAP.md), [DEFERRED def-267](DEFERRED.md#def-267)

<a id="dec-m836"></a>
#### M836 — A socket-less build's contract marks the daemon unavailable…
`jichi describe --output json` is declared Stable in [EMBEDDING.md](EMBEDDING.md), and a driving program plans from it, so on a build that refuses the daemon the contract must say so. It now carries `"available": false` and an `unavailable` reason on the `daemon` and `control` objects, emitted only where they are false. **Rejected alternatives:** removing `daemon` and `control` from such a build's contract (a consumer diffing contracts across builds would see a disappearance and no reason, and the protocols themselves have not changed); a new top-level capabilities object (a Stable field that every build would then carry for one row's sake); fixing only the text form (the JSON is what programs read). Because the fields appear only where false, a missing `available` keeps the meaning it had before M836: available.
**Where:** [ROADMAP M836](ROADMAP.md), [EMBEDDING.md](EMBEDDING.md)

<a id="dec-m837"></a>
#### M837 — Every file is opened binary, with the rule in a lint…
DJGPP, like Windows' C libraries, opens files in text mode by default, so `"\n"` is written as `"\r\n"` and a read stops at Ctrl-Z. Every stream mode now carries `"b"` and every `open()` of a file carries `JC_O_BINARY`, and `portability_lint` checks 37 and 38 fail the build otherwise. **Rejected alternatives:** setting DJGPP's `_fmode = O_BINARY` at start-up (one line, but DJGPP-only and invisible at the call site: a reader of `fopen(p, "a")` could not tell that it means binary, Windows' C libraries keep their own `_fmode`, and the unit tests' fixture writers would still need their own fix); fixing `jc_inhist` alone (that was the symptom: 26 other text-mode opens and 4 raw `open()` calls would stay latent, among them the atomic writer that every session save goes through); and text mode for the human-readable logs (jichi reads its own logs back, and a file written in one mode and read in the other is exactly the defect). The console stays in text mode, because a DOS terminal needs its carriage returns.
**Where:** [ROADMAP M837](ROADMAP.md), [DEFERRED def-270](DEFERRED.md#def-270), `include/jc_fcntl.h`

<a id="dec-m838"></a>
#### M838 — Without long file names jichi warns and goes on…
Plain DOS keeps 8.3 names until an LFN driver is loaded, and jichi's state needs long ones. `jc_long_names_ok()` asks DJGPP's `_use_lfn()` at run time; `doctor` reports it as ok or FAIL, and a command that writes state warns once. **Rejected alternatives:** refusing to run on 8.3 names (it would also block `--version`, `describe` and `doctor`, the very commands that explain the problem); deciding it at build time or keying on `__DJGPP__` (DOSLFN is loaded or not on the day, on the machine; only a run-time question is true); a translation layer that maps every state file to an 8.3-safe name (every path jichi builds would pass through it, and `.jichi.d` would still need a new name everywhere it is documented, for a configuration FreeDOS itself fixes with one line); and a warning that `--quiet` cannot silence (a script that asked for silence gets it; `doctor` still says so).
**Where:** [ROADMAP M838](ROADMAP.md), [DEFERRED def-274](DEFERRED.md#def-274)

<a id="dec-m839"></a>
#### M839 — An unusable time zone is repaired for the process, before…
DJGPP returns garbage local time whenever `TZ` is unset, empty or has no offset, and shifts `time()` itself by the same garbage. `jc_tz_repair()` runs first in `main()`: where the local time is not a plausible offset from UTC, it sets `TZ=UTC0` and re-reads it, warns once, and `doctor` explains. **Rejected alternatives:** showing UTC through `gmtime()` whenever the local time is implausible (built first and measured wrong: because `time()` is shifted too, it printed 2026-10-23 on 2026-09-29, although the fallback stays for timestamps that come from elsewhere); fixing only the test's `TZ` string (the product would go on writing shifted timestamps on a default FreeDOS); setting `TZ` in the rig (it would hide the defect that users meet); refusing to run until `TZ` is set (it would block the commands that explain the problem, on a setting FreeDOS leaves empty by default); and keying the repair on `__DJGPP__` (the check is by effect, so any C library with the same fault is repaired and a healthy one never is). `UTC0` is chosen because it makes `time()` agree with the system clock; the remaining error is the user's real zone offset, which is bounded, instead of weeks.
**Where:** [ROADMAP M839](ROADMAP.md), [DEFERRED def-273](DEFERRED.md#def-273), `include/jc_platform.h`

<a id="dec-m840"></a>
#### M840 — An explicit --readonly is pinned: no mode change may clear it…
A bounded headless run is promoted to auto mode, and a resumed session re-applies its saved mode; both went through `jc_app_set_mode`, which cleared `readonly` for chat and auto, so an explicit `--readonly` could be lost and the run could write. Now `--readonly` sets `readonly_pinned`, and `jc_app_set_mode` never clears `readonly` while it is set; and a `--readonly` run is not promoted at all. **Rejected alternatives:** only skipping the promotion (resuming a session saved in auto mode still cleared it, which `readonly_pinned.sh` check 2 measured: the write landed); re-applying `readonly` after the promotion only (the same resume path again, and the "auto-approving tools" notice would still claim something the run does not do); making `--readonly` pin the *mode* (`mode_pinned`), which would also stop a resume from applying the saved mode and change behaviour that has nothing to do with writing; and refusing `--readonly` together with the bounds (a bounded read-only run is exactly what a component such as a chatbot needs). The fix sits in the one function every mode change passes through, so a third path would inherit it.
**Where:** [ROADMAP M840](ROADMAP.md), `tests/smoke/readonly_pinned.sh`, `src/chat/jc_app.c`

<a id="dec-m940"></a>
#### M940 — A background command's output is spooled to a file, not a pipe
A detached child's output went to a pipe jichi drained only at tool-call boundaries; a child that filled the 64 KiB pipe while the model sat in a long call blocked in `write(2)` (a 13-hour `make ci`). Now the child writes to `~/.jichi.d/bgspool/bg-N.out` (0700/0600, outside the workspace so the snapshot/rollback blast radius cannot reach it); `drain` re-opens the file at its saved offset, so the incremental-read (M459) and 256 KiB head-keep (M26) contracts are unchanged. The spool is unlinked at reap and swept at boot (a stale file has no writer; the sweep checks `st_nlink`). **Rejected alternatives:** a draining reader in the wait loops (the child still blocks between polls, and the fix would ride on the model's turn running that loop at the right moment); a bigger pipe (the default is the kernel's, and a bigger buffer only delays the stall); and keeping the pipe and merely non-blocking the write (a `write` that would block cannot be made non-blocking, only failed, which is data loss). The file is the only mechanism with no reader at all.
**Where:** [ROADMAP M940](ROADMAP.md), `tests/smoke/bg_output_stall.sh`, `src/chat/jc_bg.c`

<a id="dec-m841"></a>
#### M841 — A chatbot runs in chat mode with --readonly, one process per message…
The chatbot guide's configuration: chat mode with `--readonly` and `"toolProfile": "core"` (the model is offered read, list and search only), `pathFence` on, a deny list behind them for the read-only tools a stranger should not reach, a persona through the folder's own `/ask` command, and one `jichi -p` process per message. **Rejected alternatives:** plan mode, the read-only mode the operator named first (measured: its instructions steer the model toward `write_plan`, which a chatbot must deny, and it was slower, at a median of 7.1 s against 5.0 s, with no measurable gain); the daemon as the default shape (one process then serves every visitor with the posture of its start-up flags, which suits a measured latency problem rather than a first deployment); trusting session ids sent by the browser (a visitor could name someone else's session and read that conversation, so the server keeps them); passing the visitor's text to `-p` (a message beginning with `--` is a flag, so `--prompt-b64`); and relying on `--readonly` alone (read-only tools such as `fetch_url` and the git tools reach the web and a repository's authors).
**Where:** [ROADMAP M841](ROADMAP.md), [CHATBOT.md](CHATBOT.md), `tests/smoke/chatbot_example.sh`

<a id="dec-m951"></a>
#### M951 — The "no built-in loop" rule is overturned for in-process legs of one task
The operator decided (2026-10-13, answering HANDOFF section 4 item 1 of the context-relay review): AUTONOMOUS_LOOPS.md's "jichi ships no loop, scheduler, or persistent task queue" is overturned for **one shape** — a loop over the legs of *one* task, the context relay's in-process topology. The three reasons the rule gave (scheduling, persistence, isolation) are all arguments about *many* tasks; a relay continues one task inside one envelope with the same fences re-applied at every leg start, and its subagent form cannot be built anywhere else. The bounds are the decision's other half: at most a ceiling of legs per process; two consecutive legs with no measured progress end the relay as stalled (not a resume loop with extra steps, SCRIPTING.md's warning); the envelope is the relay's, summed over legs, never re-armed per leg; the process's memory growth falls back to the supervised topology instead of another in-place leg; and the relay stays off unless asked, and an untrusted project may turn it off, never on. **Rejected alternatives:** keeping the rule for this shape (the subagent case is then unbuildable, and the measured failure the relay exists for — a long headless turn ending on the gateway's hard limit with nothing written down — stays unfixed for runs with no supervisor); a built-in queue or top-level loop (duplicates systemd/cron/flock and tempts many tasks inside one long-lived process — the rule's original argument, and the daemon's clause in the loops page's section 9 stands for that case); the re-exec topology (it would rebuild every warm structure and move the lease and control socket across an exec, for isolation the supervised topology already gives); and making in-process legs the *default* relay topology (it changes the blast-radius profile for runs where a supervisor already provides per-leg process isolation for free — the default stays the single-run shape, and the supervised topology stays the choice wherever a supervisor exists). **Supersedes, for this shape:** the "no built-in loop" statement in [AUTONOMOUS_LOOPS.md section 1](AUTONOMOUS_LOOPS.md), which now reads with this row and its section-9 clause for the many-task case.
**Where:** [RELAY.md](RELAY.md), the context-relay design proposal in `docs/proposals/` (section 6.8 "What the in-process topology overturns"), the internal design page `docs/internal/2026-10-13-relay-loop-design.md` (the not-yet-built topology, the interface and the ordering), `docs/internal/2026-10-13-next-steps-recommendation.md` (the follow-through that recorded the answer)

<a id="dec-m952"></a>
#### M952 — Topology B is the operator's one-line config switch, off by default
M951's decision overturned "no built-in loop" for in-process legs but named their shape unspecifiable without a supervisor routing. The landing shape (2026-10-14) is narrower and operator-owned: a top-level headless relay run with `relayInProcess` (a config key, off by default) continues its next legs in the same process up to `relayMaxLegs` (default 5, guard 32); every in-process handover still writes the baton, so a crashed relay is resumable and the supervisor topology stays available. **Rejected:** making in-process legs the default for any relay-on run — the three existing topology-A drivers (relay_handoff_in, relay_account, relay_leg_budget) encode the handover-and-exit contract, and a default-flip broke all three in red before the switch existed; a command-line flag — the loop's audience is an unattended run, which is configured, not typed. **Where:** [RELAY.md](RELAY.md) (the status table), the M952 entry in [ROADMAP.md](ROADMAP.md), `docs/internal/2026-10-14-topology-b-design.md` (the slice's invariants and the B-2/B-4 follow-ups), [M951](#dec-m951) (the decision this lands).

<a id="dec-m984"></a>
#### M984 — the background spool's on-disk cap binds at reap only (the running-time bound is given up)
The operator decided (2026-10-19 08:00, after the supervisor's 07:10 note withdrew BATCH 1 READY over the `make ci` failure on the def-324 spool race): cap-at-reap is accepted for L-27. While a background command runs, its spool file is **unbounded** — the pre-M984 cap rewrote the file with `O_TRUNC` on every mid-run drain that saw it oversized, destroying the child's output in the read->truncate window (the gate's exact red) — and the cap binds only at reap, where the child has stopped and the rewrite is race-free. The in-memory head + tail (the model's view) is unchanged. **Rejected alternatives:** a single-writer relay owning the file so the parent never truncates a live child's (a new C89 synchronisation surface for a window that only matters between drains — that is the deferred L-22's problem, and a periodic drain in the wait loop is the M940 design def-324 exists to remove); Linux punch-hole (`FALLOC_FL_PUNCH_HOLE`) to drop bytes without rewriting (non-portable — OpenBSD/illumos lack the syscall, and its required fallback was the same race; its fallback is now cap-at-reap, which is race-free); keeping the running cap and making the rewrite atomic (no POSIX atomic truncate-in-place exists; a temp-file + rename breaks the child's open `O_APPEND` fd, which would then keep writing the orphaned inode — the child never sees the rename). **The decision's other half:** the running-time disk bound is deferred (def-324: RLIMIT_FSIZE in the forked child before exec, or a periodic drain, a loss-free redesign) — recorded in DEFERRED.md, the `jc_bg.c`/`jc_bg.h` comments, and the drive's FINDINGS L-27. **Supersedes:** the def-324 running-time effect of M962 (the on-disk cap binding mid-run) and the `test_bg` M962 block's `cap_verified` assertion, rewritten per this decision. **Where:** [ROADMAP M984](ROADMAP.md), [DEFERRED def-324](DEFERRED.md), the `jc_bg.h` `JC_BG_SPOOL_MAX` comment, the drive's FINDINGS L-27 and the M984 design note (LOG run 0011); the L-27 entry in `docs/internal/drive/2026-10-17-hardening/FINDINGS.md`.

<a id="dec-m1097"></a>
#### Batch 10 split (the operator, 2026-10-30 19:29, SV:1929) — READY what's landed; the rest runs on as batch 10b
The operator decided: the hardening drive's batch 10 (PLAN §3, "The rest, by severity") is **split**. **Batch 10** — the findings already landed, M1089–M1096 with their -b/-c follow-ups and M1094 (G3) — gets a `BATCH 10 READY at <sha>` line now, gated exactly as any ready batch (fast gate and full smoke green on the exact head, records consistent, doc lints green). **Batch 10b** is the remainder of the batch-10 table: D-12, D-16, D-17, D-18, D-23, D-24, D-25, D-33, D-35, D-36, then the lows, the notes and the record corrections; it starts immediately after the READY line and is gated the same way. **D-09 (the djb2 workspace-key collision; its milestone number is claimed only in the drive's internal PLAN and LOG) was in flight at the split and is parked cleanly into batch 10b, not finished into the READY.** The finding is confirmed on head (two djb2-colliding workspaces share one shadow checkpoint repo, one lease and one telemetry file — a mock run in one workspace's directory makes the other list its checkpoint and offer to restore the first's file into it), and its red-first unit-test leg is kept at `refs/jichi/discarded/parallel-verify-red/1793386975-1` (commit `2f2b1b7c`) for the next run to resume; the smoke-driver leg is re-cut with the fix. **Why parked, not finished into the READY:** the operator's own words allow either ("finish it into this READY or park it cleanly, and say which"); the READY head must be green on both the fast gate and the full smoke, and D-09's red-first driver is red on the unfixed tree, so landing it means landing its fix too — a SHA-256-key + `jichi.worktree`-belt change across three stores — whose review and gate belong to a dedicated run. **Rejected alternatives:** finishing D-09 into the READY (it would push a medium security finding's fix into the same gate as the already-landed half, and the operator explicitly offered the park); leaving batch 10 not-READY because one of its findings is in flight (the operator overrode that: READY what's done). The split is recorded here and in PLAN §3; the READY line itself goes in the drive's LOG.
**Where:** this row, PLAN §3 (the batch-10 split note), the drive's LOG (the `BATCH 10 READY` line, run 0102), and the D-09 red-first leg kept at `refs/jichi/discarded/parallel-verify-red/1793386975-1`.

<a id="dec-m1106"></a>
#### M1106 — `setup --from-global` drops a literal `apiKey` when inheriting (the exact member, not a case-insensitive family), keeps `apiKeyEnv`, and warns by model name
The decision (the lead's design note, run 0110, on top of the finding's own recommendation): when inheriting a global config, remove only the EXACT case-sensitive member `apiKey` (from every object in `models` and from the top-level `search`), keep `apiKeyEnv`, and print one stderr warning naming each affected model.
**Rejected alternatives:** (1) filtering at the main.c writer -- wrong altitude: the unit test cannot see main.c, and the `_comment` promise belongs to the inherited tree itself. (2) refusing the whole run -- over-refusal: a global config with a literal key is legitimate, the user expects the other settings to carry over, and doctor already diagnoses a literal key in an existing config. (3) stripping case-insensitive variants (`APIKey`, ...) -- jichi reads the key from the exact `apiKey` member only (`jc_json_get_str`/`cJSON_GetObjectItem` is exact-case, jc_config.c:236/550/1575), so a foreign spelling is inert and would be deleted with nothing to lose but surprise.
**Where:** the M1106 ROADMAP entry, the finding's D-24 row in findings-index, the drive's LOG-3 (run 0110), and the red-first legs in tests/test_setup.c + the driver tests/smoke/setup_from_global_nokey.sh.

#### B-02 build (SV:0837, the operator 2026-11-01) -- both halves, later
The operator decided, in the supervisor's session: "Take both halves of B-02; tell the lead to build it, later." Both halves of M995's design note (ROADMAP "### M995 -- B-02, the M2 'last piece'"): **half 1** watches `<worktree>/.git/config` and `<worktree>/.git/hooks/**` as scoped-edit targets UNCONDITIONALLY, for the lead too, so an out-of-band write to a control path is reported in the next tool result; **half 2** narrows a write child's Landlock grant from "your worktree" to "your worktree minus the control paths", so a child cannot plant a clean/smudge filter, a hook, or the next program-naming setting. "Later" means NOT in the current wave -- finish what is in flight (batch 10b's D-series first). The supervisor's half-2 scope correction, before building: the denial targets config, hooks and info/attributes (the program-naming paths), NOT every write under .git/ -- a write child still needs `git add` / `git commit` / `git stash`, which write the index and objects under the MAIN repository's .git/ (a linked worktree's .git is a file pointing there); M995's test (4) "the child's normal edits are unaffected" is where that shows, and the build must add a leg that runs `git add` + `git commit` in the child's worktree under the new fence. **Rejected alternatives:** building it now (the operator's own "later"), and building only half 1 (the operator's "both halves").
**Where:** this row, the B-02 row in `docs/internal/2026-10-17-hardening-review/findings-index.md` ("decided 2026-11-01: both halves; build pending"), the board card, and the drive's LOG-3 (run 0111).

#### SV:0825 depth-0 stream retry (the operator, 2026-11-03 08:25) -- a cut mid-generation in the lead's own turn is retried (built after the 0.13.0 RC)
The operator answered the drive's TO RATE row (SV:1852.3): "yes, let's retry in such a case." A model stream cut mid-generation at depth 0 (the lead's own turn) is treated as **transient and retried**, as it already is at depth 1, instead of ending the turn and the run. It caused 2 of the drive's 15 run deaths (0124, 0127; telemetry pages 40ad64a4/e50b346f; the next event after the cut is `turn_end`). The agent's retry gate is `is_transient(st, http) && !emitted` (src/chat/jc_agent.c:743) where `emitted` is "a content or a tool call has already been streamed" (:681) -- the prime suspect that makes depth 0 unlike depth 1, but **measured before chosen** (the operator's precondition): which of the two conditions stopped 0124/0127 is not recorded, and the fix must not duplicate an already-streamed partial answer (reasoning only, text, or tool calls) into what the user or the journal saw. It is a new milestone, claimed only in the drive's internal LOG-3 and FINDINGS (its number is not in ROADMAP yet -- the 0.13.0 RC is M1122 -- so this public page does not print it), red-first, lands on top of the BATCH 10b-6d READY head (07822843) with the next batch -- after the release candidate, per the operator's SV:0833 order ("nothing new goes into that head"). **Rejected alternative:** ending the turn on such a cut (the current behaviour, and the TO RATE row's default) -- the operator chose retry: a dead model stream is a transient transport failure, not an operator intent, and the run's envelope (and the drive) should not die on it; a retry that ignores `!emitted` at depth 0 is not the shape either (it duplicates the streamed prefix), which is why the measurement precedes the design.
**Where:** this row, the SV:1852.3 row in the drive's FINDINGS (the decision recorded on it, with the milestone number), and LOG-3 (run 0136).

<a id="dec-m1129"></a>
#### M1129 — `make ci` gains two build stages (the optimised fortified release, the 32-bit build), and they run at the END of the gate
The operator decided (2026-11-03 10:57, SV:1057, superseding SV:1053's "design only, do not add"): the quality gate `make ci` must **build and run the unit suite** in two modes it never had: the RELEASE build (optimised, fortified, `-Werror`, both `jichi` and `jichi-harness` plus the suite) and the 32-bit build (`gcc -m32`, curl-free, `-Werror`, `jichi` plus the suite). The red-first is **the stage itself** -- the stages are landed first and shown red on the current tree, then the defects they expose are fixed and the stages shown green. Where gcc-multilib is absent the 32-bit stage prints a SKIP line naming why (never passes silently). The two stages run at the END of the gate, after `e2e`, each self-cleaning, followed by a final ordinary `clean && WERROR=1 CC=gcc all`.
**Why the stages, and why at the end:** SV:1053 measured two build defects that no existing stage could see, because no existing stage built in those modes: (A) the jichi-harness is not warning-clean in optimised builds (a redundant `realpath` declaration -- fortify's `bits/stdlib.h` declares `realpath` itself at `-O`, so the local decl that exists only for the `-O0` builds becomes a redeclaration -- and an ignored `write` return in the drain loop, where the `(void)` cast does not silence glibc's `warn_unused_result`); (B) the unit suite is not 32-bit clean (two test literals overflow a 32-bit `long`, one of them a `-Wlong-long` compile error even in a runtime ternary's unselected arm, the other a runtime failure because the clamp correctly returns LONG_MAX). A text lint cannot see a `-Werror` diagnostic; only the build can, which is why these are stages, not checks. The placement at the end is forced by the gate's own structure: the `smoke` and `e2e` stages take the tree's `jichi`/`jichi-harness` as up-to-date prerequisites, so a self-cleaning optimised or 32-bit rebuild anywhere before them would destroy the binary they test (on a multilib host, the smoke tier could not even relink its tools against the 32-bit residue) -- the qwen27b review that caught this rejected the mid-gate landing, and the gate now closes by restoring the ordinary build.
**Rejected alternatives:** (1) a separate opt-in target (`make ci-release`, `make ci-32`) -- an opt-in target means the harness is again never built optimised by the gate, exactly defect A's class; the operator's SV:1057 names `make ci` explicitly and supersedes SV:1053's "do not add". (2) the status quo, with a lint in place of a build stage -- a lint cannot see a warning or a `-Wlong-long` error; SV:1053's own defect A is that nothing builds the harness optimised at all. (3) suppressing the warnings (`-Wno-redundant-decls`, `-Wno-unused-result`, a pragma, `-Wno-long-long`) -- silences the symptom; the fixes remove the cause, and each mirrors an existing in-tree precedent: the `_XOPEN_SOURCE 600` guard is what `src/util/jc_path.c` already uses (glibc declares `realpath` in every build mode, so no local decl is ever needed or redundant), the write-return capture is what `src/tui/jc_term.c`'s `ed_write_n` already does, and the width-aware preprocessor test constants mirror the file's own existing `sizeof`-based dispatches with the 64-bit arms byte-identical to the original checks. (4) changing `jc_json_clamp_long`'s contract to make the 32-bit test constant hold -- the clamp is correct (it returns LONG_MAX on 32-bit); the test constant was the bug.
**Where:** the M1129 ROADMAP entry, the two stage comments in the Makefile's `ci:` target, the drive's LOG-3 (run 0142: the design, the red-first legs, the S-b placement correction, the gates), and the worker reports 2026-11-03-M1129-{S,S-b,A,B}.md.
