# jichi agent orchestration & collaboration: the one thing it definitely lacks — a recorded run of the multi-machine layer

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified. All claims cite a source; inferences are flagged.
> **Companions:** the other four `JICHI-*-GAP-ANALYSIS.md` files.

---

## 1. TL;DR

Of the five analyses in this set, agent orchestration & collaboration is the
one where the "never observed" pattern is **already filled**. jichi has a
recorded, multi-run, live-model **team drive**: the zigodot weekend
(2026-09-25 → 09-28, **248 runs, 62.8 h**) in which "a jichi lead, the
workers and reviewers it started, and a supervising agent (Claude) that
checked on it every five minutes" built Godot's 2D pong in a private repo,
on free local models, in a single container — with the trace matching Godot's
frame-for-frame (10,000-frame no-input trace byte-exact; input-driven
600/600 within 1e-3). There are companion drives too (the corpus drive, 48
runs; the overnight drive, 246). This is the only surface in the set with a
**real, recorded, multi-agent, multi-run, live-model** event.

So the gap is not "orchestration has never been observed." It is that the
orchestration has only ever been observed **on one machine**. The
multi-machine layer — the SSH fan-out in `SUPERVISOR_OF_MANY.md` Layer 2,
the pattern the guide exists for ("a change that must land on several
machines") — **has no recorded run anywhere in the tree.** Every documented
drive is single-container: `--auto --no-route --no-session` headless one-shots
spawned in one checkout. The SSH recipe is a template (`for host in build-a
build-b gpu-1; do ssh "$host" 'jichi -p …' & done`), not an event.

Two sharper, secondary threads reinforce the main one:

* **The supervising agent was Claude, not jichi.** The weekend drive's
  coordinator was "a supervising agent (Claude) that checked on it every
  five minutes." Jichi's own Layer 3 ("a jichi agent as the supervisor") —
  jichi *itself* fanning out and reviewing workers — is documented but not
  the pattern that produced the one recorded multi-run event. The strongest
  recorded collaboration is *Claude watching jichi*, not *jichi watching
  jichi*. (Flagged inference: the overnight drive's table shows a jichi
  *lead* spawning `spawn_parallel`/`spawn_subagent` workers, so Layer 3 is
  exercised *within* a single machine; the unrecorded half is the
  *cross-machine* supervisor.)
* **The goal met "for the team" still awaits the operator.** W8 — the top
  rung, "playable in an X11 window" — is marked met, but "W8's own gate still
  needs the operator to play the game once, as the plan says." The one
  recorded team's terminal verification is, in the same shape as the other
  analyses, the *person* step that has not happened.

## 2. What orchestration & collaboration *does* have (for precision)

- **A three-layer model** (`SUPERVISOR_OF_MANY.md`): Layer 1 local fan-out
  (`spawn_parallel`, worktree-isolated, budget-sliced, first-wins merge);
  Layer 2 multi-process over SSH (`jichi -p … --output json` per box, daemon
  warmth, per-worker autonomy envelope); Layer 3 a jichi agent as supervisor
  (dispatch via shell or a `delegate_to_worker` user tool, review + merge,
  `ask_user` on conflict).
- **The primitives** (`SUBAGENTS.md` 310 lines, `PARALLEL.md` 434 lines,
  `AUTONOMY.md` 1105, `AUTONOMOUS_LOOPS.md` 750, `CONTROL.md` 246,
  `DRIVING_A_TEAM.md` 718, `TUTORIAL_ORCHESTRATION.md` 173): spawn/keep/
  merge, per-child watchdog, `maxParallelAgents`, a live board, `jichi
  control` for driving a lead from outside, `--journal` telemetry per run.
- **A recorded multi-run team event** (`analysis/2026-09-28-the-weekend-drive-
  complete.md` + the day-one and "what a team must be told" companions, the
  corpus drive, the overnight drive): 248 runs, 62.8 h, a supervising agent
  logging 122 entries, "every total checked by a second route where one
  exists" (37,509 `model_call` rows by two routes), the supervisor's own
  mistakes recorded in §5. This is orchestration *evidence*, not speculation.
- **Single-machine Layer-1/3 coverage in that event**: the lead planned the
  split and merged; workers ran in worktrees; `spawn_parallel` and
  `spawn_subagent` are exercised in the recorded drives (the overnight drive's
  arm table counts "spawn calls … 13, 2 of them `spawn_parallel`").

Every *mechanism* of single-machine orchestration exists, is tested (the pure
`jc_parallel_parse_msg` classifier is unit-tested), and has a live multi-run
record. What does not exist is a live record of the **cross-machine** layer.

## 3. The gap, stated

> **jichi's multi-machine orchestration — the SSH fan-out (`SUPERVISOR_OF_MANY.md`
> Layer 2) for "a change that must land on several machines" — has no recorded
> run. Every documented team drive (zigodot weekend, corpus, overnight) is
> single-container; the SSH recipe is a template, not an event. The one
> surface the set of analyses has been tracking — a live, multi-agent,
> cross-boundary collaboration — is proven within a machine and unproven
> across machines.**

Two strength levels:

* **(Strong) No recorded cross-machine drive.** Grep of `docs/` for evidence
  of a multi-machine jichi team run returns only the recipe itself
  (`SUPERVISOR_OF_MANY.md` Layer 2). The recorded drives are explicit: the
  corpus drive's invocation is "`--auto --no-route --no-session --quiet
  --lease fail`, with an edit scope — every run a headless one-shot" (single
  checkout); the overnight drive "spawned a new child" in one zigodot
  checkout; the weekend drive ran "unattended in a container." No drive
  names a second host.
* **(Weak, but real) The flagship use case is the unrecorded one.**
  `SUPERVISOR_OF_MANY.md` opens with the multi-machine jobs as *the* reason
  for the guide ("a change that must land on several machines"). The layer
  the guide exists to justify is the layer with zero events. Single-machine
  fan-out — which a shell script or CI could do without jichi — is proven;
  multi-machine fan-out — which is what needs a *tool* — is not.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| Single-machine `spawn_parallel` / worktree merge | **Recorded and multi-run** (zigodot, overnight, corpus drives; `PARALLEL.md`'s M808/M815/M865/M890 entries are each tied to an observed event). Not a hole. |
| Pure orchestration logic (parse/classifier/merge) | **Unit-tested** (`PARALLEL.md:76`: "The pure `jc_parallel_parse_msg` classifier … is unit-tested"). Not a hole. |
| jichi-as-supervisor (Layer 3), within a machine | **Observed** (the weekend drive's jichi lead planned + merged; the overnight drive's lead spawned workers). Partially proven; the *cross-machine* half is not. |
| External supervision of a jichi team | **Recorded** (Claude checked every five minutes, 122-entry log). A real collaboration event — but the supervisor is not jichi, and it is single-machine. |
| **Multi-machine orchestration (Layer 2, SSH fan-out + merge)** | **No recorded run.** The recipe is a `for host in …; ssh …; done` template; the "several machines" jobs the guide names as its reason are the unobserved ones. |

The distinctive form, the seventh instance — and the *inversion of the
inversion*: the other four gaps were "never happened once, on any machine."
The editor gap was "happened, but offline by design." The platform gap was
"happened, and the happening is a photograph." **This one is "happened,
magnificently, on one machine — and the layer that needs a machine *count
greater than one* is the template."** The set's shared boundary — the check
reaches as far as a machine can run — here splits into two: within a
container, jichi has *over*run the boundary (248 live runs, frame-exact
trace); across containers, it has not crossed it at all.

## 5. Evidence

### 5.1 The one recorded team is single-container

`analysis/2026-09-28-the-weekend-drive-complete.md:5`: "The team: a jichi
lead, the workers and reviewers it started, and a supervising agent (Claude)
that checked on it every five minutes." `:284` (scope line): "One project,
one operator, one supervising agent, free local models, and one pinned
build." The container appears throughout (zombie reaping, key isolation in
the container, the M811/M812 same-pid-collision-in-containers fix). No
second host is named in the drive.

The companion drives agree: `2026-09-24-the-corpus-drive.md:57` —
"`--auto --no-route --no-session --quiet --lease fail`, with an edit scope —
**every run a headless one-shot**"; `2026-09-25-the-overnight-drive.md:80` —
"zigodot checkout into `/tmp/jichi_work` and spawned a new child." Single
machine, three ways.

### 5.2 The multi-machine layer is a template

`SUPERVISOR_OF_MANY.md` Layer 2 is a recipe:

```sh
# supervisor.sh — fan one task out to N hosts, collect JSON results
for host in build-a build-b gpu-1; do
  ssh "$host" 'cd ~/project && jichi -p "run the test suite and report \
    failures" --auto --output json --budget-tokens 1M' > "result.$host.json" &
done
wait
```

`build-a build-b gpu-1` are placeholders. Grep of the whole tree for a
recorded multi-machine jichi run ("many machines," "multi-machine," "fleet
of jichi," "jichi on every box") returns **only this recipe's own page**.
The guide's opening names the multi-machine jobs as its reason ("a change
that must land on several machines") — the reason is unmet in the record.

### 5.3 The supervising agent was Claude, not jichi

`weekend-drive-complete.md:5` (quoted in §5.1): "a supervising agent
(**Claude**)." `DRIVING_A_TEAM.md:6` lists the third reader as "another
agent that drives a jichi lead from outside, as a supervisor" — i.e., the
documented pattern's *supervisor* is allowed to be an external agent, and
the one recorded run took that path. Layer 3 (jichi *as* the supervisor,
cross-machine) is "composition of existing pieces" with no event.

### 5.4 The terminal gate is the person, again

`weekend-drive-complete.md:29-30`: "The goal was met, **for the team** …
W8's own gate still needs **the operator to play the game once**, as the
plan says." `:63`: "the pong ladder W0–W8 | all met 'for the team'; **W8
awaits the operator's own check**." The trace fidelity that would substitute
("the 10,000-frame no-input trace matches Godot's on every frame") is the
*machine* check; the plan's own *human* check is the one open item. This is
the collaboration surface's instance of the set's shared shape: the last
gate is a person.

**Flagged inference:** "no recorded cross-machine drive" proves the *tree*
contains none. The operator could have fanned jichi out over SSH in a
private fleet without writing it up — the zigodot record proves the drive
was in a *private* repo and the analysis is the only thing that surfaced it.
The register's culture (each drive gets an `analysis/` page) makes an
unrecorded fleet drive unlikely, but the bound is the same as every other
analysis here.

## 6. Implications

1. **The guide's load-bearing claim is its least-tested claim.**
   `SUPERVISOR_OF_MANY.md` is organized around scale-out; the recorded
   evidence supports scale-*up* (many runs, one box). A reader who takes the
   guide on its opening sentence and deploys the SSH layer is relying on a
   template, not on the 248-run event — which exercised the *other* two
   layers.
2. **The multi-machine merge is where first-wins meets real divergence.**
   Layer 1's merge is "file-level, first-wins (conflicts reported, never
   auto-merged)" — sound within a worktree set that shares a base. Across
   machines, workers may run against *different trees* (the guide's own
   "partitioned by machine/repo" case), so first-wins is a policy the
   record has never stressed. The weekend drive's conflicts were
   same-repo, same-build, and caught by the lead's reviews — a different
   failure class than divergent-fleet merges.
3. **jichi supervising jichi, across machines, is the unclaimed pattern.**
   The strongest recorded supervision is Claude's. A jichi supervisor that
   dispatches `ssh host 'jichi -p …'` workers and merges their JSON —
   Layer 2 + Layer 3 together — is the composition the guide sketches and
   no event exercises. That is the collaboration capability with the most
   documented *intent* and the least *record*.
4. **The set's boundary now has a shape.** Across the five analyses the
   check reaches "as far as a machine can run." Here the machine ran, and
   ran *well*, and the boundary is the *edge of that machine*. The
   collaboration gap is not a missing event inside the box (that is the
   other four); it is the missing event *between* boxes.
5. **Meta, the seventh, and the set's turning point.** Debugger, UX,
   curriculum, presentations: never observed. Editor: offline by design.
   Platform: observed, and the observation is a photograph. **Orchestration:
   observed, repeatedly, live, multi-agent — within a machine, never across.**
   This is the first analysis where "definitely lacking" is not an absence
   but a *boundary location*, and where the project's own record is strong
   enough to locate the boundary precisely rather than assert it.

## 7. Recommendation

1. **Run the SSH layer once, on the fleet the operator already has.** The
   recipe is complete; the missing input is `build-a build-b` being *real*
   hosts and a run producing an `analysis/` page with the same "checked by a
   second route" discipline the weekend drive used. One cross-machine drive —
   even a small partition, one repo per host, the `--output json` merge —
   turns the guide's opening sentence into a datum.
2. **Make the jichi the supervisor of it (Layer 3 over Layer 2).** The one
   recorded supervisor was Claude. A jichi lead that dispatches the SSH
   workers and merges their reports is the unclaimed pattern (§6.3); running
   it is what would record it. Keep the external supervisor (a human or
   Claude) on top, as the weekend drive did — the record shows that layer
   earned its place.
3. **Record the divergent-fleet merge policy, since that is what first-wins
   has never faced.** §6.2 is the real risk of the cross-machine layer.
   Before (or as part of) the first run, write down what happens when two
   hosts' workers touch the same file from different bases — the record
   currently has no such case.
4. **Close W8 the way the plan says: the operator plays the pong once.** It
   is the single open human gate on the one recorded team event, and it is
   the collaboration surface's instance of the set's shared "last gate is a
   person." It is cheap (a 2D pong in an X11 window) and it makes the
   strongest recorded event *complete* rather than "met for the team."

Rejected, in the register's spirit:

* *Simulate the multi-machine layer in one container* — the whole point of
  Layer 2 is that the compute lives elsewhere (daemon warmth, per-box
  envelope, cross-host merge). A container-internal "fake fleet" proves the
  JSON plumbing (already proven by the single-machine drives) and not the
  cross-boundary behavior, so it would be the M326b shape: a check that
  passes because its universe shrank to one box.
* *Declare the SSH layer "partly verified"* — it has *no* recorded run, so
  the honest status is "documented, unrun," the same word the platform
  register uses for "never compiled." "Partly" would imply some event
  happened; none did.
* *Add a cross-machine test to `make ci`* — the offline-by-design lesson of
  the editor and platform analyses: a live, multi-host, model-calling gate
  does not belong in PR CI. It belongs in a milestone drive with its own
  `analysis/` page, which is how every other orchestration event in this
  tree was recorded.

## 8. Limitations and honest counterarguments

1. **This is the strongest-verified surface in the set, and the gap is a
   boundary location, not an absence.** A reader expecting "jichi can't do
   orchestration" will find the opposite: 248 live runs, a frame-exact
   trace, a logged external supervisor. The claim is narrower: the
   *cross-machine* layer specifically is unrecorded. If the operator
   considers single-machine fan-out the whole of "orchestration" and SSH
   scale-out an out-of-scope addendum, §3 weakens to: *the documented
   multi-machine capability has never been exercised, but the core (single-
   machine multi-agent) is well-recorded.*
2. **Absence-based on the fleet.** "No recorded cross-machine drive" rests
   on a negative grep plus the drives' explicit single-container scope. A
   private, unwritten fleet run would be invisible — and the zigodot drive
   was itself in a *private* repo, surfaced only because it was analyzed.
   The register culture is the discount, not a proof.
3. **"The supervisor was Claude" is one event's fact, not the pattern's
   rule.** Layer 3 (jichi as supervisor) *is* documented and *is* exercised
   within a machine (the lead planned and merged the weekend drive; the
   overnight lead spawned workers). I claim only that the *recorded external
   supervision* was Claude's, and that the *cross-machine* jichi-supervisor
   has no event. I do not claim jichi-as-supervisor is untested in general.
4. **I did not run any orchestration.** A team drive is a network/model
   event in a private repo; per the register's rule it is a datum, not a
   read. Everything here is read from `SUPERVISOR_OF_MANY.md`, the three
   `analysis/` drive pages, `PARALLEL.md`, and the grep for a second host.
5. **Tree vintage.** The drives are 2026-08-20 → 09-28 (M504 → the post-M815
   weekend, pinned build M770/b5280d5d for the weekend). If a fleet drive
   landed after the last `analysis/` page and before `1b71247d`, the grep
   would not show it — same bound as §8.2, and the same reason the
   recommendation is "run it and record it," not "it has never been done."

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the one recorded team, and its single-container scope
sed -n '1,10p' docs/analysis/2026-09-28-the-weekend-drive-complete.md     # "248 runs, 62.8 h … supervising agent (Claude)"
grep -n "one project, one operator" docs/analysis/2026-09-28-the-weekend-drive-complete.md   # :284 scope line
grep -n "container" docs/analysis/2026-09-28-the-weekend-drive-complete.md | head            # single-container evidence

# the companion drives are single-machine too
sed -n '57,57p' docs/analysis/2026-09-24-the-corpus-drive.md              # "every run a headless one-shot"
sed -n '80,80p' docs/analysis/2026-09-25-the-overnight-drive.md           # "spawned a new child" in one checkout

# the multi-machine layer is a template (placeholder hosts)
sed -n '/## Layer 2/,/## Layer 3/p' docs/SUPERVISOR_OF_MANY.md            # "for host in build-a build-b gpu-1"

# the only "many machines" hits in the tree are the recipe itself
grep -rln "many machines\|multi-machine\|fleet of jichi\|jichi on every box" docs/ --include=*.md

# jichi-as-supervisor vs the recorded external supervisor
sed -n '/## Layer 3/,/## Safety/p' docs/SUPERVISOR_OF_MANY.md
grep -n "supervising agent (Claude)\|the lead\|spawn" docs/analysis/2026-09-28-the-weekend-drive-complete.md | head

# the terminal gate is the person
sed -n '29,30p' docs/analysis/2026-09-28-the-weekend-drive-complete.md    # "W8's own gate still needs the operator to play the game once"
sed -n '63,63p' docs/analysis/2026-09-28-the-weekend-drive-complete.md    # "W8 awaits the operator's own check"

# the scale of the single-machine primitives (the proven half)
wc -l docs/AUTONOMY.md docs/AUTONOMOUS_LOOPS.md docs/DRIVING_A_TEAM.md docs/PARALLEL.md docs/SUBAGENTS.md docs/CONTROL.md
grep -n "jc_parallel_parse_msg" docs/PARALLEL.md                          # "is unit-tested"
```

**Verdict line for the register this could seed:** *jichi's agent
orchestration & collaboration has a recorded multi-run, live-model, multi-
agent team event (the zigodot weekend: 248 runs, 62.8 h, "a jichi lead, the
workers and reviewers it started, and a supervising agent (Claude)," trace
byte-exact to Godot) — the only surface in this set where the "never
observed" pattern is filled. The gap is a boundary, not an absence: every
recorded drive is single-container, so the multi-machine layer —
`SUPERVISOR_OF_MANY.md` Layer 2, the SSH fan-out the guide exists to justify
("a change that must land on several machines") — has no recorded run; its
recipe's hosts are placeholders (`build-a build-b gpu-1`). The recorded
supervisor was Claude, not jichi; Layer 3 (jichi supervising workers) is
exercised within a machine, not across one. And the one recorded team's
terminal gate is the set's shared "last gate is a person": W8 "awaits the
operator's own check" — the operator playing the pong once. Revisit when: a
cross-machine drive (Layer 2, ideally with jichi as the Layer-3 supervisor)
is recorded as an `analysis/` page with the weekend drive's
"checked by a second route" discipline, and W8's X11 pong is played by the
operator.*
