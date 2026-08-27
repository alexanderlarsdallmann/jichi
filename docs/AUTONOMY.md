# Autonomy envelope

Most coding agents can run unsupervised (`--auto`). Few can be *trusted* to. The
autonomy envelope wraps an unsupervised run in a **hard, verifiable safety
boundary**: you declare limits up front, the agent works inside them, and when
it finishes a configured verifier (build + tests) must pass. If the agent can't
make it pass within a retry budget — or it blows a budget — the workspace is
**automatically rolled back to the last known-good (green) checkpoint** and the
run ends with a non-zero status. Every decision is appended to a structured
**audit journal**.

> **Rollback is what a verifier buys you.** Read the paragraph above carefully:
> the rollback depends on a configured verifier and a green checkpoint. Without
> `--verify` (or config `verify`) **nothing can declare the tree broken, so
> nothing is ever rolled back** — budgets then only *stop* the run, and the work
> stays for you to inspect (which is deliberate, M80). Snapshots must be on for
> either behaviour. So `jichi --auto -p "…"` with no verifier is bounded in cost
> and unbounded in consequence; its only safety net is `/undo`.

```
jichi --auto --verify 'make && make test' \
  --budget-tokens 200k --deadline 20m --max-tool-calls 60 \
  --edit-scope 'src/**' --edit-scope 'tests/**' \
  -p "Fix the failing parser tests"
```

The envelope composes existing machinery — [snapshots](SNAPSHOTS.md),
[modes/permissions](AGENT_MODES.md), and the iteration cap — into one boundary.
It is off by default; passing any envelope flag (or setting `verify`/`editScope`
in config) turns it on.

> The bracketed notices the envelope sends the model (`[envelope]`, and the
> rest of the family) are registered in [NOTICES.md](NOTICES.md).

## What `revertOutOfScope` will and will not undo

`revertOutOfScope` (M142) restores files changed outside the edit scope at turn
end. Since **M501** it does that only for changes the run can be shown to have
made, because the two are not the same question and treating them as one nearly
cost an operator a merge.

jichi has exactly two ways to change a file:

1. **The write chokepoint** — every file tool (`write_file`, `edit_file`,
   `apply_patch`) and the ACP delegate pass through it, and the edit-scope fence
   already refuses a path outside the scope there.
2. **A shell command** — `sed -i`, a redirect, a build step. The fence does *not*
   cover this, which is why the sweep exists at all.

So the rule is:

| The out-of-scope change | What happens |
| --- | --- |
| a path the run wrote through the chokepoint | **reverted** to the run-start baseline |
| any path, when the run **has** run a shell command | **reverted** — attributable to the run, since the shell is the one writer the fence cannot see |
| a path the run never wrote, in a run that ran **no** shell command | **left alone**, reported as `not_ours` in the journal and named on stderr |

The third row is a proof, not a guess: with no chokepoint write and no shell call,
there is no mechanism by which the run could have made that change — so reverting
it could only destroy someone else's work.

**The residual, stated plainly.** A run that *does* use the shell cannot
distinguish its own out-of-scope writes from a colleague's concurrent edit, and it
still reverts both. jichi has no per-path provenance for a shell command. If you
edit a working tree while an autonomous run with `revertOutOfScope` is using the
shell in it, your edit can be reverted. The operating rule is therefore
**one envelope per working tree at a time** — which `--lease` (M431e) enforces
against another *jichi* run, but cannot enforce against a person with an editor.

---

## A run stopped by a budget still tells you whether its gate passes

Budget exhaustion is not a broken state (M80), so a stopped run **keeps its
work** rather than rolling back to an unverified baseline. What it used to not do
is say whether that work was any good.

The envelope runs the verifier at a budget exit **when the result could change
the rollback decision** — rollback armed *and* a green checkpoint banked. Correct
for a decision; wrong for a report. A run whose gate was red at the start never
banks a green, so the verifier was skipped and the journal ended:

    start → tool_call… → budget → end   (outcome: budget_exhausted)

with **no `verify` event at all**. Measured three times; in one case the run had
done the valuable half of its task and its gate passed a minute later, and the
operator found the finished work only by running `git status`.

Since **M506** the gate is evaluated for the record in that case, and the verdict
is **advisory by construction**:

| | |
| --- | --- |
| Journalled as | a `verify` event with `phase: "budget_exit_advisory"`, `advisory: true`, the exit code and the declared `kind` |
| Said on stderr | *"the run stopped on a budget, but its verifier PASSES on the tree as it stands"* when green |
| Effect on the outcome | **none.** The run remains `budget_exhausted` |

That last row is the point. Turning a green advisory verdict into a pass would
make a stopped run indistinguishable from a completed one, which is the opposite
of what this exists for. The two journal phases (`budget_exit` for the verdict
that decided a rollback, `budget_exit_advisory` for the one that merely informs)
let a reader tell them apart.

**The cost, stated:** one extra verifier run per stopped run, bounded by
`--verify-timeout`. If your gate is a slow suite and you would rather not pay it
at a stop, disarm the gate deliberately with `--verify ""`.

---

## Where the verifier comes from, and how to have none

With `--verify` the gate is what you typed. **Without it, an armed run inherits
the project's `verify` config key, or failing that its `testCommand`** — so an
`--auto` run in a repository that sets `testCommand` gets a gate nobody asked for.
That default is deliberate (a run with no gate proves nothing, which is M331's
"the gate must agree with itself"), and it has cost a real run dearly: a
test-first task whose brief said *the suite must end red* inherited the suite as
its gate, fix-forward then fed *"the verify failed, fix it"* back at the model,
and the run burned 1.56M tokens and delivered nothing.

Two things make that visible and avoidable:

| You want | Do this |
| --- | --- |
| a gate you chose | `--verify "<command>"` |
| **no gate at all** | `--verify ""` — an empty command disarms it, including an inherited one |
| to know which you got | read `verify_source` in the run journal's `start` event: `flag` (you chose it, including the empty disarm) or `config` (inherited) |

`verify_source` exists because `verify: make test` in a journal used to mean both
things, and they mean very different things when the gate then fails.

**When you want no gate**, say so with `--verify ""` rather than by hoping there
is no `testCommand`: the disarm is then a recorded decision (`verify_source:
flag`) instead of an absence a supervisor cannot distinguish from an oversight.

---

## Your first bounded run

Do this once before you trust `--auto` with anything. Five deliberate choices,
each of which you can check afterwards.

```sh
cd /path/to/your/project
git checkout -b jichi-scratch          # 1. a branch you do not mind losing
```

2. **Watch your verifier fail first.** Whatever you are about to hand jichi as
   the gate, run it by hand *and see it red* — break something on purpose if you
   must:

   ```sh
   make test        # or: zig build test, pytest, cargo test ...
   ```

   A verifier that cannot fail is not a gate, and a run that "passed" one is the
   hollow green this page's own §Rollback warns about. This is the single most
   valuable minute in the whole procedure.

3. **Give it a deadline, not an iteration count**, for the first run — wall-clock
   is the limit you can feel:

   ```sh
   jichi --auto --deadline 10m --verify "make test" --edit-scope 'src/**' -p "Fix the failing test in src/, and nothing else."
   ```

4. **One directory of scope.** `--edit-scope 'src/**'` means a write outside
   `src/` is refused even if you would have approved it — and the refusal names
   the path *and* the scope, so you can widen it deliberately next time.

5. **Read the journal, not the summary.**

   ```sh
   jichi runs
   ```

   Every decision taken in your absence is there: the baseline commit, each
   checkpoint, each verify attempt and its verdict, anything refused for being
   out of scope, and whether the tree actually changed. If the run ended red, the
   workspace is already back at the last green checkpoint — that is the rollback
   this envelope exists to give you.

**What to expect the first time.** A small, well-specified task with a real
verifier usually works. An open-ended one ("improve the code") usually does not,
and the measured reason is in the campaign notes: **bounded beats open-ended,
decisively.** Give it a task whose completion you could check yourself in a
minute, then read the journal and decide whether to widen the next run.

## The four pillars

### 1. Budgets

| Flag | Meaning |
| --- | --- |
| `--budget-tokens <n>` | Total input+output token cap (`200k`, `1m`, or a bare number). Counts tokens from the main agent **and** its subagents. |
| `--deadline <dur>` | Wall-clock cap (`30s`, `20m`, `2h`, or bare seconds). |
| `--max-tool-calls <n>` | Cap on tool calls the agent **attempts**, at any depth. A call refused by a gate (path fence, edit scope, permission, constraint, kinetic/privileged posture) still spends the cap — see below. |
| `--preserve-discarded` (retrieval: `jichi attempts` lists preserved work; `jichi recover <commit> --into <dir>` materialises one into a **worktree**, never the live tree) | **M336, off by default.** Before a rollback discards the tree, commit it and pin it under `refs/jichi/discarded/<run>/<n>` in the shadow repo, with a message carrying the outcome, verify command, token cost and file count. Recover with `jichi recover <sha> --into <dir>`. **M337b extends this to every destructive restore** — `/undo`, `/rewind`, the `undo`/`rewind` subcommands and `--revert-out-of-scope` — by preserving at the two chokepoints in `jc_snapshot.c` rather than at each call site, so the same config key covers a mechanism nobody has written yet. Refs are namespaced by cause (`refs/jichi/discarded/undo/…`, `.../revert/…`). Off by default because it writes to disk while a run is already failing — its failure mode overlaps its trigger. Config `preserveDiscarded` (the only gate; `undo` has no envelope, so a CLI flag could not have been one). See [proposals/2026-08-work-preservation.md](proposals/2026-08-work-preservation.md) |
| `--strict-green` | **M332, off by default.** Refuse a passing verify when the run changed a file outside `--edit-scope` — such a pass cannot be told apart from one obtained by editing the gate. Outcome `scope_tainted`, exit 1, **work kept** (it refuses a verdict, it does not discard the run). Config `strictGreen`. Also switches on a system-prompt paragraph declaring the verifier to be the run's contract. See [GATE_INTEGRITY.md](GATE_INTEGRITY.md) |
| `--max-reads <n>` | Cap on READ-category tool calls (read_file / search / list / find_* / git_*). Trips a normal budget stop (`budget_kind:"reads"`), so M80 keeps partial work. Prevents the read-heavy bust the M96 `starved` guard only detects post-hoc (M98). |

#### Which budget binds is arithmetic, not the shape of the work (M595)

The four budgets read like peers, and in practice **one of them decides and the
other three are decoration.** Which one is a division you can do in advance.

With a token budget `B`, a call cap `C`, and `t` tokens spent per model call,
**tokens bind when `B / t < C`, and calls bind otherwise.** `t` is a property of
the backend, not of the task: it is roughly (system prompt + tool schemas +
history) minus whatever the prompt cache serves for free. Measured on two
projects and three models:

| run | caps | `t` observed | ended on | at |
| --- | --- | --- | --- | --- |
| API migration ×3 (2026-08-20, one local model) | 70 / 120 / 90 calls, **no token cap** | 40–60k | **calls** | 70/70, 118/120, 76/90 |
| chrtext sweep (2026-08-26, `qwen3.5-9b`) | 800k tok, 80 calls | ~30k | **tokens** | 27 calls |
| chrtext sweep, retry | 1.2M tok, 60 calls | ~21k | **tokens** | 58 calls |
| chrtext docs | 2M tok, 50 calls | ~35k | **calls** | 1.73M tokens |

Two things follow.

**A cacheless backend makes `t` large and roughly constant**, so a token cap
converts almost exactly into a call count: 800k at 30k/call *is* a 27-call run,
whatever `--max-tool-calls` says. On a backend with a warm prefix cache `t`
collapses — one HRZ session here read 92% from cache — and the same token budget
buys several times the calls.

**Advice by task shape does not hold.** The natural guess is *migration and
exploration are priced in calls, generation in tokens*; the fourth row above is a
documentation task that ended on the call cap, and the second is a mechanical edit
that ended on tokens. Estimate `t` from `jichi telemetry` for the model you are
actually using — the `input/call (est)` line gives it directly — and set the two
caps so they agree, or accept that you have set only one.

**When a run stops, jichi says which budget did it** (M97): the warn line reads
`envelope: <kind> budget exhausted`, the journal carries a `budget` event with
`kind`, and the headless result carries it too. Read that before raising anything.

#### The flight plan — the limits are stated at takeoff (M355)

An armed envelope now states its limits in the system prompt: *"This run is
bounded; a budget stop ends it where it stands"*, followed by the **armed**
budgets only (token budget, wall-clock deadline in seconds, tool calls, read
calls), the pacing ask — *prefer finishing a smaller complete thing over
starting a larger unfinished one* — and the fact that the M347 bell exists. A
model told "30 tool calls" at call 1 can pace the whole run; one that learns at
80% can only scramble (finding 14's budget deaths were sized in calls the model
could have planned against). Top-level prompt only, like the notice itself.

#### The budget notice — the agent is told before the engine stops (M347)

The human watching the TUI reads the context percentage and running cost live in the
prompt line; until M347 the agent flying a bounded run was told nothing until a budget
ended it — 0/7 implementation runs completed in the 2026-08-07 driving session, every
one `budget_exhausted`, and M96's "starved analysis" dies with its report unwritten
precisely because nothing said *wrap up now*. So, **once per run**, when any armed
budget first crosses **four fifths** of its cap, one line lands in the conversation as
a user message the next model call sees (the control-inject shape, so the cached prefix
stays byte-stable):

```
[envelope] budget check: 410000 of 500000 budget tokens used (82%); 24 of 30 tool
calls. A budget stop ends the run as it stands: finish the step in flight, then write
the deliverable and your final answer before starting anything new.
```

Only armed budgets are named. Advisory only — it changes no outcome and moves no cap;
it is journaled as a `budget_notice` event. Once per run on purpose (a reminder per
round is noise a model learns to skip, the M323 lesson), and top-level only, like
operator steering — a subagent's iteration budget is its own affair (M62 tapers it).

#### Sizing them — measure, do not estimate

There was no guidance here for a long time, and its absence was expensive: a single
session lost ~3 M tokens to runs killed by budgets their operator had guessed at.

**A worked measurement** (`--budget-tokens` omitted entirely, so the figure is the task's
rather than a cap's): a change whose hand-written form is **fifteen lines** — two syscall
ports, three `errdefer`→`defer` fixes and two test un-skips in a 13 KB file — cost
**2,009,021 tokens over 56 tool calls** and finished `ok`. The operator's estimate
beforehand was 700 k–1.1 M and 15–25 calls: out by ~2× and ~2.5×.

Three rules follow, and each one contradicts an intuition worth naming:

- **Do not estimate from lines of code.** Output size predicts nothing. Almost all of the
  cost is *input* — the files read, and then the whole conversation re-sent on every
  subsequent call.
- **Do not copy what a previous run consumed.** That measures a past task; and if those
  runs were themselves truncated by a guessed budget, the figure is circular. This is how
  the 3 M went.
- ~~**Budget from calls**~~ — **overturned by the next measurement, and left here as a
  warning.** A second uncapped run on the same project spent **3,157,985 tokens over 53
  calls** where the first spent 2,009,021 over 56: *more* tokens from *fewer* calls. Per-call
  cost is a property of what each call must CARRY, not of the call count. Tokens-per-call is
  also not a constant within a run — it climbed **24.8 k → 29.4 k → 33.3 k → 35.9 k** across
  those 56 calls, because without prompt caching the history is re-billed every time, so a
  figure sampled early under-predicts the whole. Observed range on one project, one model:
  **22.3 k to 99.7 k per call.**

**The finding that should decide whether you cap at all.** Across 28 runs, expressing what
each capped run spent as a fraction of its cap: the ones that finished used
22/28/32/33/38/54/60/75 %, and the ones that died used 95/101/102/102/102/102/103/103/103 %.
**The band from 76 % to 100 % is empty.** Because cost is superlinear in call count, a run
that needs more than you gave it does not need *slightly* more — so a tight cap buys no
safety margin, only a coin flip between "finished with 25 % headroom" and "spent everything,
delivered nothing gateable". **Treat a cap as a circuit breaker, not a budget:** set it far
above any plausible need, or omit it and let `--deadline` be the backstop.

**Attempted, not permitted (M459).** The cap counts what the agent *tried*, before
any gate decides. It used to count only calls that survived every gate, which made
the cap weaker the more a run was refused: a measured probe watched a model repeat
one out-of-scope write five times under `--max-tool-calls 2`, never trip the cap,
report `tool_calls: 0`, and end `outcome: ok` having spent its entire token budget
on an action that could not succeed. Telling the model it is repeating was added
separately (M429), but that needs the model to cooperate, and the defences worth
having are the ones that do not ([HARDENING.md](HARDENING.md) §6b).

Both figures are recorded, because they answer different questions. The run journal's
`end` carries `tool_calls` (attempted) and `tool_calls_executed` (ran). Many attempts
with nothing executed is the machine-readable signature of a run thrashing against a
gate — the thing to alert on in an unattended fleet. A delegate's report to its parent
deliberately quotes the **executed** figure, so "0 tool calls" pairs honestly with the
denial that explains it.

**And mind that there are three axes.** `--budget-tokens`, `--max-tool-calls` and
`--deadline` each stop the run, and capping one while leaving another open just moves the
failure: one run died on 40-of-40 calls with tokens at 79 % of their cap, and another died on
50-of-50 calls with tokens *uncapped* — 4.47 M spent and the verifier one failing test from
green. If you constrain only one thing, constrain the deadline — **but see the next
paragraph for what a deadline is and is not.**

**`--deadline` bounds when jichi NOTICES, not wall-clock (measured M429).** Like the
other budgets it is checked at **tool-call boundaries**, so the run overruns by however
long the current model call takes. Measured on an ordinary run — no hang, nothing
pathological: `--deadline 25s`, the first tool call arrived at **+102s**, and the deadline
fired there. A 25-second cap produced a 102-second run, a **4× overrun**, and the size of
the overrun is the time to the next boundary, which is unbounded in principle. The extreme
form is already on record: a run that hung *before* its first boundary went 22 minutes with
a 0-byte journal ([DEFERRED.md](DEFERRED.md)), and no budget could see it.

So `--deadline` is the right *scheduling* control — it stops a run that is grinding through
tool calls — and it is **not** a hard wall-clock bound. If you need one, wrap the process:
`timeout -s INT 10m jichi -p …`, which is what every driver in this project's own probe
campaigns does. The honest fix shape, not yet built, is to check the deadline inside the
streaming loop, where libcurl's progress callback already ticks (it is what `--heartbeat`
rides on); that would make the flag mean what its name says.

**A provisional figure, offered to be replaced rather than trusted:** on a backend with no
prompt caching, a *small single-file increment with tests* runs on the order of **1–3 M
tokens and 18–56 calls** (eleven completed runs; the cheapest was 223 k over 10 calls for two
test assertions, the dearest 3.16 M over 53). Do not reuse it for a design task, a multi-file
refactor or a documentation pass — labelling a measurement by what it measured is half of its
value.

The full per-run table — what each run was capped at, whether the cap bound, and the gate and
brief that went with it — is in **[DRIVING.md](DRIVING.md)**, along with the rest of what one
28-run engagement taught about gates, briefs, steering a live run and reviewing its output.

**If you have never measured this workload, run it once with no `--budget-tokens` at all**
and a generous `--deadline` as the only backstop. Also raise `maxToolIters` (default 25,
and a per-turn *round* cap that will truncate the measurement regardless of tokens), and
omit `--max-tool-calls`. Then read `jichi runs` and `jichi telemetry` — and mind that the
two can disagree: see M329/M330, where a run's journal said 223 k and its true cost was
975 k.

Budgets are checked between iterations and before each tool call. When one is
hit the run stops and exits with a budget outcome. **The run's work is NOT
discarded just for hitting a budget (M80):** budget/deadline/tool-call exhaustion
is not a broken state (unlike a verify failure), so the envelope rolls back only
when a verifier is configured **and** its tree is red at exit — otherwise the
partial work is kept for you to review or resume (`--session <id>`). With a
verifier that still passes, or none configured, or `--no-rollback`, the edits
survive. (Before M80 a budget exhaustion always rolled back, which discarded valid
partial work — e.g. a design phase whose output document was reverted purely for
hitting the token budget.)

**Outcome clarity — kept vs rolled back (M92).** Because M80 makes
`budget_exhausted` the *normal, successful* end state of a budget-sized increment
(work banked, not reverted), the raw outcome name misleads: a shelf of green
bank-and-commit runs reads as failures. The terminal **disposition** is now
recorded — an orthogonal `rolled_back` flag, set only when the workspace is
actually reverted to green — on the journal `end` event and the telemetry
`turn_end` event, and rendered by the `telemetry` summarizer as an **Outcomes**
line (`budget_exhausted: kept=N reverted=N`). This changes nothing about *when* a
rollback happens (the M80 decision is unchanged); it only makes that decision
observable. The pure `jc_env_disposition_name` composes the label
(`"budget_exhausted (work kept)"` vs `"(rolled back)"`); the bare
`jc_env_outcome_name` (journal `outcome` field, exit-code logic) is unchanged.

#### When the gate disagrees with itself (M331)

The envelope's whole warrant is the verifier's exit code, and `jc_testparse` independently
reports what passed and failed. **M86** compares them on a *green* verify (hollow gate:
zero tests, fewer than before, a new test file that never got wired). **M331** checks the
red direction, which is the expensive one:

| finding | condition | what it means |
| --- | --- | --- |
| `hollow_red` | exit ≠ 0, `passed` > 0, `failed` == 0 | tests ran and none failed, so the fault is in the **harness** — a wrapper's exit code, a lint or build step beside the tests, output the build system treats as failure |
| `red_tests_gone` | exit ≠ 0, `passed` > 0, below an earlier green's count | tests were removed or stopped being reached, in a run that never went green — which is why M86 could not see it |

On a finding jichi warns, adds `consistency` to the verify journal record, pings
`on_status`, and puts **one sentence ahead of the parsed failures** in the fix-forward
message. That sentence is the return on the whole feature: a model handed "exit 1" and a
list of zero failures reads the exit code and starts editing code. Advisory — it never
changes an outcome.

**A red verify with no counts at all is deliberately silent.** That is a compile error:
normal, and the commonest failing verify there is (26 of 67 measured). The honest cost is
that the incident which inspired the check — Zig 0.16 failing a build step over stderr
output — also suppressed the counts, so M331 would not have caught that instance. It
catches the class, not its most famous member.

> **Scope covers the file tools, not the shell.** `--edit-scope` refuses `write_file`,
> `edit_file` and `apply_patch` outside its globs — and a shell command reaches past it.
> M83 diffs the tree afterwards and `--revert-out-of-scope` repairs it afterwards, both
> **after the outcome is decided**. A run has therefore been observed modifying its own
> verifier, passing the modified gate, and exiting 0. Analysis and recommendation:
> [GATE_INTEGRITY.md](GATE_INTEGRITY.md); incident: [ANECDOTES.md](ANECDOTES.md) #45.

### 2. Edit-scope fence

`--edit-scope <glob>` (repeatable) restricts the **`edit_file` / `write_file` /
`apply_patch`** tools to matching paths; an out-of-scope edit is refused and the
model is told so. Globs support `?`, `*` (within a path segment) and `**`
(across segments), e.g. `src/**`, `tests/*.c`.

**Delegated agents are held to the same scope (M133).** The fence now applies at
*any* agent depth, so a `spawn_subagent` child or a `spawn_parallel` write child
is bound by the same `--edit-scope` as the top-level run — previously they
inherited the envelope but not its fence, so a write child could edit any file in
its worktree and have it merged back. As a second layer, the parent also refuses
to **merge** any parallel-child change whose path is outside the scope (catching
a file a child touched via the shell inside its worktree). See docs/HARDENING.md
§5.

The fence governs the structured edit tools only — `run_terminal_command` is not
path-checked (a shell command's writes can't be known statically). **The real,
tool-agnostic guarantee is rollback** (below): `git reset --hard` + `clean -fd`
reverts *every* change in the work tree regardless of how it was made. Treat
edit-scope as "stay in your lane" guidance enforced for the edit tools, and the
verify gate + rollback as the safety net that catches anything else.

**Out-of-scope guard (M83).** Because the shell can write/delete outside the edit
scope *and* a green run isn't rolled back, an out-of-scope shell change (e.g. a
stray `rm .jichi/memory.md`) could otherwise pass silently. At the end of a top-level
`--auto` turn, jichi diffs the final tree against the run-start baseline and **warns
(stderr + a `out_of_scope` journal event) about every changed path not in the edit
scope** — even changes made via the shell, and even on a green run. By default
that is detection, not prevention: the change already happened; rollback, git,
or manual review recovers it — but it is surfaced, not lost. No-op when no
`--edit-scope` is set.

**Each path is reported once per run (M289).** The guard runs at *every* top-level
turn end and diffs against the **fixed** run-start baseline, so a file changed once
stays changed — and used to be re-reported every turn afterwards. One real run
logged **17 `out_of_scope` events that were all the same path**, which reads as 17
violations in `runs` and is the noise that led the operator to widen `editScope`
rather than look at the one file. Reported paths are now suppressed on later turns.
A path that was **reverted** (auto-revert, below) is deliberately not suppressed:
the revert makes the tree clean, so a later change to that path is a genuinely new
violation and is reported again.

**Auto-revert (M142, opt-in).** `--revert-out-of-scope` (config
`"revertOutOfScope": true`; `--no-revert-out-of-scope` overrides the config)
upgrades the guard to prevention: at turn end the flagged paths are restored
**individually** to the run-start baseline — a modified file checked back out,
a created file removed, a deleted file resurrected — while all in-scope work is
left untouched (per-path restore, not a tree reset). The journal `out_of_scope`
event gains `reverted`/`revert_failed` counts and the status line says what was
put back. Off by default: reverting the agent's work is a policy decision, and
a wrongly-narrow edit scope would silently undo legitimate changes — opt in
when the scope is trustworthy. Needs snapshots (the baseline) like the guard
itself.

Add **`--strict-scope`** to close the shell escape: while an edit-scope is in
force it refuses `run_terminal_command` outright, so *every* mutation must go
through the scoped `edit_file`/`write_file` tools. Use it when you want the
edit-scope to be a hard boundary rather than guidance (at the cost of the agent
having no shell).

#### Path-containment fence (M24)

Independent of `--edit-scope` (which is an allow-list of paths *within* the
project), the **path fence** keeps the file tools from escaping the workspace at
all — defeating a `../../etc/passwd` argument or a symlink that climbs out of the
tree. It canonicalizes each `read_file`/`write_file`/`edit_file`/`apply_patch`/
`list_files` path with `realpath()` and refuses anything that resolves outside the
canonical workspace root. A path that does not exist yet -- every fresh write -- is
resolved by canonicalizing its parent; and since M607 a leaf that is a **symlink to a
target that does not exist yet** is resolved to that *target* (relative targets
against the link's own directory, at most 40 hops, a cycle fails closed). Before
M607 such a dangling link was re-appended verbatim, judged inside, and `fopen`
then followed it: a `notes.md -> /elsewhere/escaped.txt` planted in the workspace
carried a `write_file` out of it (`tests/smoke/pathfence_dangling.sh`).

**`list_files` joined that list at M324**, and it is worth saying why it was not on
it before: it returns *names*, not contents, so an unfenced one-level listing was a
mild leak. M324 gave it an optional recursive `pattern` (so a model's invented `glob`
call resolves instead of failing), and a recursive walk is a different proposition —
`list_files {"path": "/", "pattern": "**/*.pem"}` would have enumerated the
filesystem. The feature and the fence shipped together on purpose.

It is a **tri-state** (`pathFence`, or `--path-fence`/`--no-path-fence`): default
**auto** = on only in the autonomous postures (`--auto` / auto-approve), where
there is no human in the loop to catch a stray path; force it on for every mode,
or off when you deliberately want the agent to read sibling repos / `~/.config`.
Unlike edit-scope it also covers **reads**, and it applies to the ACP fs delegate
too. Companion resource bounds landed alongside it: a 64 MB cap on a single file
read, path-length errors instead of silent truncation, and a cap on untrusted SSE
field growth.

**Reference roots (M54).** When the work legitimately needs to *read* a tree
outside the workspace — a read-only upstream you port from, a shared spec — you
no longer have to disable the fence wholesale. Configure `referenceRoots`:

```jsonc
{
  "pathFence": true,
  "referenceRoots": ["/home/me/refs/upstream-project"],
  "editScope": ["src/**", "docs/**"]
}
```

(or `--reference-root <path>`, repeatable). The fence then permits **reads**
under `app->root` *or* any reference root, while **writes** stay confined to the
workspace — so an autonomous run can study an external reference but cannot edit
it or anything else outside the project. Reference roots apply to reads only;
pair them with `editScope` to bound writes further within the workspace.
`doctor` lists the configured reference roots and warns if one isn't a directory.

Reference roots are **pre-declared by the human, before the run, for reads** — that
is the safe shape. Whether a *mid-run* one-off exception should exist (an interactive
"grant read of this path for the session?" prompt when a file tool hits the fence),
and why silence at such a prompt has no timeout (it is structural, not temporal), is
designed in [proposals/2026-08-fence-exceptions.md](proposals/2026-08-fence-exceptions.md).
A path named **in the prompt** never opens the fence: the model reads untrusted
content, so an in-prompt grant would be an injection surface — pre-declaration stays a
human command-line/config action, where content cannot forge it.

### 3. Verification gate + auto-rollback

Under `--auto`, before the verify gate, a **self-review** pass runs first
(default on in AUTO mode): the agent reviews its own diff once and fixes problems
before the verifier runs. See [GIT.md](GIT.md); disable with `--no-self-review`.

`--verify <cmd>` runs `cmd` (via `/bin/sh -c` in the workspace) when the agent
produces a final answer **and** the run changed files. Then:

- **Exit 0** — a new `green:` checkpoint is taken and the run succeeds (exit 0).
- **Non-zero, retries remain** — the verifier output is **parsed** (JUnit-XML /
  TAP / generic `file:line` scan; see [TESTING.md](TESTING.md)) and the agent is
  fed a focused summary — which tests failed and where — ahead of a short raw
  tail, then tries again (*fix-forward*: the agent's progress is preserved). When
  nothing parses, it falls back to the raw output tail. `--verify-retries <n>`
  sets the budget (default 3; `0` disables fix-forward — roll back on the first
  failure).
- **Non-zero, retries exhausted** — the workspace is rolled back to the green
  checkpoint and the run exits 1.

A verifier that runs longer than **`--verify-timeout <dur>`** (e.g. `5m`) is
killed (`SIGKILL`) and counts as a failure, so a hung build/test can't stall the
run; it also honours Ctrl-C. With no timeout the verifier runs to completion.

**Periodic verify (`--verify-every <n>`, M81).** By default the gate runs only at
completion, so a long implementation turn can batch dozens of edits, build once at
the end, and thrash on the errors. `--verify-every <n>` runs the verifier every
`n` tool calls *mid-turn*: on **pass** it banks a fresh green checkpoint (so a
later budget-exit rolls back only to that point, not the whole turn); on **fail**
it feeds the parsed failures back to the agent so it fixes them before making more
changes (no mid-turn rollback). This keeps the loop green incrementally and
composes with mid-turn compaction and the M80 budget rule.

**It requires snapshots**, and says nothing when it doesn't have them. Banking a
green checkpoint is half of what the flag does, so the gate is `env_active &&
depth 0 && snapshotted && verify_cmd` — with `snapshots: false` (or no `git`, or
the huge-un-managed-workspace guard) `--verify-every` is accepted and then never
fires. Discovered writing the M422 driver, whose first version set
`"snapshots": false` and reported a periodic verifier that had simply never run.
If you pass `--verify-every` and see no mid-turn `verify` events in the journal,
check snapshots before suspecting the count.

**Hollow-gate sanity check (M86).** A gate can *pass while running nothing* — a
green exit 0 over a test suite that never compiled the code under change (the
"hollow gate": in a dogfood run against `zig build test`, the whole gdscript
suite silently never ran, so every "proven green" commit was gated by a disjoint
subset). After every **green** verify (completion *and* periodic) jichi extracts
the number of tests observed from the output (`jc_test_report_count` over the
`jc_testparse` report — junit/tap counts, `N passed`/`out of N`, and Zig's
`All N tests passed.`) and applies the pure `jc_env_verify_sanity`:

- **`no_tests`** — green but the observed test count is **0** ("verify passed but
  ran 0 tests — is the gate wired?").
- **`fewer_tests`** — green but ran **fewer** tests than an earlier green verify
  this run (the gate *shrank*, e.g. 91 → 29).
- **`tests_not_wired`** — green, a **test-looking file was written** this run, and
  the count did **not grow** ("you edited a test file and the count did not grow —
  is the new test reachable from what the gate compiles?"). This narrows M86's
  blind spot: the two checks above ask whether the gate ran *too few* tests, and
  neither can see a gate that runs the right *number* while never exercising the
  change. A measured run added a 13-test file, the suite stayed at exactly 253
  passing because nothing referenced it, and both the gate and a count-based check
  called that green. Requiring "the run itself wrote a test file" keeps the
  false-positive rate low — it cannot fire on a run that only touches source.
  A test-looking path is `jc_env_is_test_path` (the M88 heuristic, now shared), and
  the write is noticed at the `jc_app_write_file` chokepoint so a *created* file
  counts, not only an edited one.

**The model is told too (M351).** The loop feeds every *red* verify back
(fix-forward), so the model always hears its failures — but until M351 a hollow
*green* went to the operator alone (the log line, the journal `sanity` field,
`on_status`), while the model banked the false confidence at the periodic
checkpoint and built on "tests pass". Now the first hollow green of a run also
lands one `[envelope]` note in the conversation at the **periodic** verify
boundary — the finding (`0 tests` / `2 where an earlier green ran 5` / `the new
test likely never runs`) plus the conduct asked for: check the gate, and say
plainly that the result is unverified if it cannot be fixed. Once per run;
advisory only; the **completion** site deliberately stays journal-only — a green
there ends the run, so a note would be written to nobody.

> **What is still not covered.** `tests_not_wired` fires only when the run itself wrote
> a test file. A gate that runs the right *number* of tests while never exercising code
> the run changed in *source* files remains undetectable without coverage data — a
> dogfood run found `zig build test` green at 253/253 while a gate-reached file held six
> compile errors, because Zig analyses function bodies lazily and importing a file never
> compiles the body of a `pub fn` nothing calls. That session's full write-up, including
> four more ways a `--verify` command can pass while the work is absent, is in
> [`analysis/2026-08-07-driving-zigodot-harness-findings.md`](analysis/2026-08-07-driving-zigodot-harness-findings.md).

This is **advisory** (detection, not prevention — like the M83 out-of-scope
guard): it logs a warning, pings `on_status`, and records `tests`/`sanity` on the
verify journal entry, but never changes the run's outcome or triggers a rollback.
The observed count becomes the run's high-water so a subsequent shrink still
warns. When no test count is parseable (a build-only verifier such as a bare
`make` or `zig build`), the count is *unknown* and nothing is flagged — a
count-less green is never treated as suspicious.

**"Don't re-run the gate yourself" guidance (M87).** On a backend without prompt
caching, the dominant cost of an `--auto` run was the model re-running the verify
command (e.g. `zig build test`) itself via `run_terminal_command` — re-billing its
large output into context every turn (a dogfood increment cost ~12× more when the
model self-built vs. relied on the gate). So when the run is in AUTO mode with a
verifier configured, the system prompt now carries a short standing instruction —
via the pure `jc_sysmsg_append_verify_gate` — naming the gate command and telling
the model not to run it (or any build/test) itself, since the gate runs it
automatically and reports back. No-op outside AUTO or with no verifier configured.

**Stuck-on-the-same-error guardrail (M89).** A fix-forward loop could burn the
whole budget re-attempting the same broken fix — e.g. re-guessing a moved stdlib
API, producing the identical compile error every retry. The pure
`jc_env_fail_signature` reduces verifier output to a stable signature (the first
`error:` line, minus its varying `file:line` prefix), and `jc_env_note_failure`
counts consecutive failures sharing it (reset by any green verify). When the same
error recurs (≥2), the failure fed back to the model gains a "this is the SAME
error as before (Nx) — try a *different* fix, not a variation" note (which can
break the loop), a warning is logged, and a `verify_stuck` journal event is
emitted. Detection + nudge, never a hard stop.

**Moved-goalpost guard (M88).** M86 catches a gate that runs too *few* tests; M88
catches one whose tests were quietly *rewritten to pass*. A fix-forward run could
edit a test's expected value (e.g. swap the keys an assertion checks) to turn a red
gate green — the code stays buggy but every automated check is satisfied, and the
test count can even *grow*, so M86 sees nothing. After a successful file write in
an autonomous run, `edit_file` and `apply_patch` apply the pure
`jc_env_test_assertion_edit(path, old, new)`: it flags an edit to a **test file**
(path contains `test`/`Test`/`spec`/`Spec`) where **both** the removed and added
text carry an assertion marker (`expect`/`assert`/`EXPECT_`/`ASSERT_`/`testing.`/
`GDTEST`/`toEqual`/`toBe`) and the change **modifies** rather than **adds** (the new
text does not contain the old — a pure append is a new case, not a moved goalpost).
A hit logs a warning, pings `on_status`, records a `test_assertion_edit` journal
event (with `path` + `tool`) and a telemetry `test_edit` event (M417), and increments
`env->test_edits`, which M410 turns into the **TAINTED** verdict and M420 prints as
`goalposts=N` in `runs`. All six side effects live in one place —
`tu_report_test_edit` (`src/tools/tool_util.c`) — because `edit_file` and
`apply_patch` previously carried a near-identical block each. Like M83/M86 it is
**advisory** — it never blocks the edit or changes the outcome, so a false positive is
only a reviewable warning.

**And the model is told (M435).** The five destinations above are all read by the
*operator*, three of them only after the run ends. For a long time the model — the
party that just moved the goalpost — was told nowhere: the tool result it read carried
the summary and the diff. One measured run adjusted expectations **ten times** while
that warning fired to nobody who could act (ANECDOTES #51). So `edit_file` and
`apply_patch` now append one sentence to their own result, rendered by the pure
`jc_env_test_edit_note(nth, path, …)`: the change is **recorded**, it **does not
count** if the goal was to make a failing gate pass, and *if the test itself was
genuinely wrong, say so in the final answer and name the correct expectation*. From
the second edit on it names the running count and the tainted verdict.

Three things about the wording are deliberate, and unit-tested as such:

- **It is not a refusal.** Correcting a test that encodes a mistaken expectation is
  fair work, so forbidding it would force the model to stall or misreport. A refusal
  is also *routable*: an `edit_file` becomes a `sed -i` under
  `run_terminal_command`, which M88 does not inspect at all and which the M83
  out-of-scope guard sees only at turn end, and only if the path is out of scope. The
  honest path has to stay cheaper than the dishonest one.
- **It names the price of the escape hatch.** "Say so and name the correct
  expectation" is answerable when the test really was wrong and awkward when it was
  not, which is the asymmetry doing the work.
- **It is not throttled to once per turn**, unlike M432's loop note. Each assertion
  edit is a distinct decision rather than a repetition of one, so suppressing the
  second would hide new information.

**All-reads-no-synthesis guard (M96).** A read-only (or otherwise no-edit) `--auto`
run's only deliverable is its final answer — there is no file work to keep. If such
a run exhausts a token budget *before* writing that answer (a read-only analysis
over a broad `--reference-root` that over-reads until the budget dies, leaving an
empty report), the ordinary `budget_exhausted` outcome hides the failure: M80 keeps
nothing (no edit was made) and the verify gate is trivially green (nothing changed).
`env_stop_for_budget` applies the pure `jc_env_analysis_starved(outcome,
snapshots_on, made_edits)` — true iff the run was budget-exhausted, snapshots are on
(so "no edits" is trustworthy), and no edit was made this run (`green_commit` is
still NULL, since the pre-edit checkpoint fires only on the first mutating tool). A
hit logs an actionable hint — **narrow `--reference-root`, raise `--budget-tokens`,
or instruct the task to read fewer files before it writes** — and adds a `starved`
flag to the `budget` journal event, distinguishing it from an edit run's budget stop
(which banks partial work). Like M83/M86/M88 it is **advisory** — detection only,
never a change to the outcome.

The **green baseline** is the pre-edit checkpoint the agent loop already takes
before its first file change; each passing verify advances it. `--no-rollback`
leaves a failed workspace in place (for inspection) instead of reverting. Pass
**`--verify-baseline`** to run the verifier once *before* the agent starts: it
records the starting state in the journal and warns if the tree is already
failing (so you know rollback-to-green would restore a still-broken build — which
is expected when the task itself is to make the verifier pass).

#### Two kinds of verifier: invariant and goal (M343)

`--verify` serves two purposes that look identical on the command line and behave
differently once a run is bounded. An **invariant** verifier answers *is the tree
healthy?* — `make test`, green before the work, green after, red only when
something broke. A **goal** verifier answers *has the work happened?* — red before
the work **by construction**, which is exactly what makes it unfakeable (a gate
that passes without the work forces nothing; two such gates cost ~3M tokens in one
driven session — see finding 14 of
[the 2026-08-07 driving analysis](analysis/2026-08-07-driving-zigodot-harness-findings.md)).

**Before either question, a third one: can the verifier SEE the change you are
asking for?** `--verify-kind` makes you think about *when* the gate is true; it says
nothing about *what it can observe*, and a gate blind to the change is worse than no
gate, because it converts "unchecked" into "checked".

The worked example (M473, `analysis/2026-08-18-dogfooding-on-chrtext.md` §2): a run
gated on `zig build` was asked to fix escape sequences in string literals. It changed
three files, two of them wrongly -- turning a Graphviz DOT label's `\n` escape, which
DOT requires, into a raw newline, which does not render as a line break. `zig build`
passed throughout, because the defect is in what the program *prints at runtime* and
compilation cannot observe that. The envelope reported no failure because there was
none to report.

So: a change to what a program **prints** wants a gate that reads the output; a
change to a **format** wants a gate that parses it (rendering the DOT through
`dot -Tplain` would have caught both edits in one command); a change to
**behaviour** wants a test, not a build. `make test` is a good default precisely
because it usually can see more than `make`.

Declare which one you wrote with **`--verify-kind <invariant|goal>`** (config
`verifyKind`). The declaration is optional — undeclared keeps every pre-M343
behaviour — and it is **checked, not taken on faith**: declaring a kind arms the
run-start baseline probe (no separate `--verify-baseline` needed), and the probe
compares the tree against the declaration:

| declared | baseline result | jichi says |
|---|---|---|
| `invariant` | green | nothing — the expected state |
| `invariant` | red | the existing "not known-good" warning, now trustworthy |
| `goal` | red | **nothing** — red-before-the-work is the normal state, so the standing false alarm is gone |
| `goal` | **green** | **"it forces nothing"** — the gate can pass without the work; fix the gate or the declaration, before a token is spent |

The `baseline` journal event carries `kind` and, on the last row, `forces_nothing`.
The check is advisory (it never changes the run's outcome); a bad kind on the CLI
is a hard usage error, while a bad `verifyKind` in the config warns and is ignored
— a silently mis-declared gate is the exact trap the declaration exists to remove.
With a declared goal gate, `--verify-every` additionally warns once that it banks
nothing until the gate first passes (each mid-turn verify still costs a full run
of it); the M80 budget-exit rule already keeps partial work in that situation,
because no green checkpoint exists to roll back to.

The honest limit: the declaration cannot prove a goal gate is *satisfiable* — only
that it is red without the work. Proving it can go green (against a stub or a
hand-completed fixture, the way the curriculum's graders are proven two-sided) is
still the operator's rehearsal; see docs/TEST_INTEGRITY.md.

### 3c. The in-turn loop detector (M432)

Every other recovery jichi has is **reactive to one failure** — the retry ladder,
JSON repair, the verify fix-forward, mid-turn compaction, rollback. None of them
notices a *pattern across attempts within a turn*, so a model could call the same
tool, fail the same way, and try again — dozens of times — and never be told.

Measured before it was built, on three corpora:

| corpus | tool calls | error-turns | repeated an identical failing call |
|---|---|---|---|
| 294 Continue sessions (a *different* agent) | 1,081 | 50 | 25 (50%) |
| zigodot (campaign) | 595 | 13 | 4 (31%) |
| chrtext (ordinary use) | 17,553 | 241 | 97 (40%) |

Worst single turns: `apply_patch` **34 fails / 0 successes**, and one
`run_terminal_command` repeated **59× identically**.

**Two keys, because they catch different loop shapes and neither alone sees both.**
An **exact** key on `(tool, arguments)` fires at **3** — byte-identical repetition is
never a legitimate strategy. A **class** key on `(tool, failure cause)` fires at
**4**, catching the loop that varies one constant: a real one escalated
`git log -N` from 300 to 20,000,000, against which an exact key scored *zero* over
logs holding it 96 times. Thresholds are fitted to the union of both jichi corpora,
clear of the 2× tail — which is thick (78 turns on chrtext) and usually a legitimate
retry.

**The advice is per-cause, because a wrong cause amplifies a loop.** The failure is
classified where the result text is still in hand (`not_found` / `denied` /
`bad_args` / `killed` / `nonzero_exit` / `other`), and each gets advice that is true
of it: *find the real name* for a missing target, *narrow it* for a call killed on a
cap, and for a denial M429's inversion — *it will not succeed by rephrasing*. Where
the cause is unknown the note says less rather than guessing.

**Told once per loop, at any depth**, folded into the tool result. Journalled as a
`tool_loop` event and surfaced as `loops=N` in `runs`
([OBSERVABILITY.md](OBSERVABILITY.md)) — because a detector that only tells the
model leaves the operator's table blank, which is how mid-turn thrashing stayed
invisible before M422.

**Advisory. It never changes a run's outcome** — it is the counterpart of the two
notices that already shipped: M89 for a verify that keeps failing, M429 for a
policy-blocked call retried.

### 3z. Before you spend a run: `brief-check`

```sh
jichi brief-check brief.md --verify "zig build test" --verify-kind goal
```

**No model call.** It reports three things and exits — 0 normally, **1** only when a
declared `goal` gate is already green, so a wrapper can gate on it:

1. **Every constraint the brief would infer, and the line that produced it.** This is
   the part that matters. Measured across 7 driven runs, **4 silently adopted a
   constraint** from prose — and the phrases responsible are *descriptions*, not
   instructions: *"force never-compiled core code to compile"*, *"47 of 88 files never
   compiled"*. They do not look like constraints when you read them, which is why the
   line number is most of the value: the canonical text (`do not run build commands`)
   appears nowhere in your brief. One run was banned from the sweeps it depended on
   and died with no deliverable.
2. **The envelope the flags declare** — budgets, edit scope, verifier, declared kind —
   including the absences, so `writes are NOT fenced` is stated rather than left blank.
3. **The gate's baseline colour**, via M343's probe. A `goal` gate that already passes
   **forces nothing**; two runs cost ~3M tokens against gates that could not pass.

**What it cannot prove:** that a goal gate is *satisfiable*. Only that it is red
without the work. Proving it can go green needs a stub or a hand-completed fixture —
see [TEST_INTEGRITY.md](TEST_INTEGRITY.md).

Nothing here is new capability: `jc_constraint_scan` and the baseline probe both
already existed. What was missing was a way to run them **without spending a run**.

### 3a. The budget panel (`--budget-panel`, M431f) — off by default

The envelope states its caps at takeoff and rings **once**, at the first ~80% crossing
(M347). That is the right shape for an alarm and the wrong shape for a gauge: this
page's own §on budgets notes that **the band from 76% to 100% is empty**, and that is
where runs die.

`--budget-panel` (config `budgetPanel`, `budgetPanelEvery`) adds the reading:

```
[envelope] 412000/1500000 tokens (27%) . 18/60 tool calls . 7m of 20m
           . ~34000 tokens/call . ~32 calls left at this rate. Pace the work: ...
```

Delivered as one user-role message at the tool-call boundary — the same mechanism as
M347's notice, so the [prompt cache](PROMPT_CACHING.md) prefix stays byte-stable.
Cadence: every `budgetPanelEvery` tool calls (default **5**) *plus* each quintile
crossing of the token budget, so a short run sees one reading and a long one a dozen —
never one per round. Armed budgets only. Top-level only: the parent paces the run,
while a subagent's bound is the M62 iteration taper. `--no-budget-panel` turns it back off
for one run when a config has enabled it.

**The number that matters is the rate.** Tokens-per-call is what turns "300k left"
into "about eight calls left", and on a backend without prompt caching it **rises
within a run** (measured 24.8k → 35.9k across 56 calls), so the projection is an upper
bound on what remains rather than a promise. The rate is omitted, never guessed, when
no model call has reported usage yet.

**Why it is off.** M347's decision register explicitly rejected "a reminder per round
or a countdown", on the evidence that one unthrottled condition once produced 1,038
warnings (M323) and on the principle that *a nag a model learns to skip is worse than
one clear ask*. That objection is a judgement about model behaviour, not a
measurement — so this ships as a flag to be **measured against** that decision rather
than as a default that overrules it.

**How to measure it.** Run the same bounded brief twice, once with and once without,
and compare from the journal and telemetry rather than by impression:

```sh
for arm in off on; do
  [ "$arm" = on ] && P=--budget-panel || P=
  jichi -p "$(cat brief.md)" --auto $P         --budget-tokens 1.5M --max-tool-calls 60         --journal "runs/$arm.jsonl" --output jsonl > "runs/$arm.log"
done
jichi runs --output json          # outcome, tokens_used, tool_calls per arm
```

What would justify making it a default: the panel arm **finishes** where the control
arm ends `budget_exhausted`, or reaches the same deliverable in fewer tokens. What
would justify discarding it: the panel arm hedges early and delivers less. Either way
the answer is a number, which is the point of shipping it off.

### 3b. One run per workspace (`--lease`, M431e)

jichi has **no lock of any kind** — and the envelope assumes **one actor per tree**.
`revertOutOfScope` makes that assumption load-bearing: the M83 end-of-turn sweep
diffs the whole tree against a run-start baseline and cannot tell a sibling run's
edits, or *your* mid-run merge, from an out-of-scope write by the model it polices.
So a second run can make the first revert work nobody asked it to touch.

When an envelope arms, jichi writes an advisory lease to
`~/.jichi.d/leases/<workspace-key>.json` (pid, run id, start time, mode; 0600, keyed
by the same derivation as the checkpoint repo, so one project is one number in both
stores) and drops it when the run ends. It re-reads the file first and unlinks
**only** its own, so a run that proceeded past someone else's lease cannot delete it.

| `--lease` | On finding a **live** holder |
|---|---|
| `warn` (default) | say so, name the holder's pid and run id, and proceed |
| `fail` | refuse to start, exit **2**, naming the holder and the way past it |
| `off` | neither take nor consult a lease |

A **stale** lease — one whose pid no longer exists — is taken quietly in every mode,
including `fail`. A crashed run must not wedge the workspace forever, which is the
classic lockfile failure, and you cannot tell a stale file from a live holder by
looking.

**Warn is the default deliberately.** Two read-only runs over one tree are routine —
it is how you inspect a running job — and refusing them to prevent a write–write
hazard would be a cure worse than the disease. Reach for `--lease fail` in a
supervisor that wants serialisation.

**The honest limit:** a lease does **not** fix the one-actor assumption. It makes
violating it loud. If you need real concurrency, give each run its own tree
(`spawn_parallel`'s write children already do exactly that, via git worktrees).

**The second honest limit — it is per-`$HOME`, so it does not see another
user.** The path above is `~/.jichi.d/…`: two *users* working in one shared
directory compute the **same** workspace key into **different** homes, each take
a lease, and neither ever sees the other's. Staged and confirmed at M478 in a
two-user VM: with one user's run holding its lease, a second user's run
completed on the same tree under **`--lease fail`** — not warned, not delayed,
not refused. The lease was designed for one user's concurrent runs and does that
correctly; it is not an arbiter between people. The mitigation is a workspace per
person, not a bigger lock — see [`DEPLOYMENT.md`](DEPLOYMENT.md) §6 and, for the
worked case, [`JUPYTERHUB.md`](JUPYTERHUB.md) §8a.

*Why not `flock`?* It dies with the process — which sounds like a feature until a
crashed run leaves no evidence it ever held the tree, and "who was in here?" is
precisely the forensic question a supervisor asks. A file plus a liveness check
answers it; a released lock cannot.

### 4. Audit journal

Every run appends newline-delimited JSON to
`~/.jichi.d/runs/<run-id>.jsonl` (override with `--journal <path>`;
`--journal -` disables it). Events: `start` (the resolved limits), `baseline`
(`exit`, when `--verify-baseline` or a declared `--verify-kind` armed the probe;
plus `kind` and `forces_nothing` under a declaration, M343), `budget_notice`
(`kind` — the four-fifths warning was injected, M347), `tool_call` (`name`, `error`, or `blocked`
for out-of-scope or strict-scope), `verify` (`exit` — `-2` means the verifier
timed out — `retries_left`, and `failed`/`passed` test counts when parseable),
`checkpoint` (`commit`, `green`), `budget`
(`kind`), `rollback` (`to`), and `end` (`outcome`, `tokens_used`,
`tool_calls`).

> Keep logs **outside** the workspace. A rollback runs `git reset --hard` +
> `git clean -fd` over the work tree, so any file *inside* the workspace is
> reverted/removed — including the journal (its default path is outside, under
> `~/.jichi.d/runs/`) **and** a shell redirect of the agent's own output
> into the workspace (e.g. `jichi --auto --verify … 2>run.log` run from
> the project root: `run.log` would be rolled back to its checkpointed state,
> truncating everything written after the snapshot). Redirect such logs to a
> path outside the project (or to the terminal), which is the normal case.

Example trace of a run that couldn't pass and was reverted:

```json
{"ts":...,"event":"start","verify":"make test","max_tool_calls":60}
{"ts":...,"event":"tool_call","name":"write_file","error":false}
{"ts":...,"event":"verify","exit":1,"retries_left":1,"failed":2,"passed":40}
{"ts":...,"event":"tool_call","name":"edit_file","error":false}
{"ts":...,"event":"verify","exit":1,"retries_left":0,"failed":1,"passed":41}
{"ts":...,"event":"rollback","to":"a1b2c3..."}
{"ts":...,"event":"end","outcome":"verify_failed","tokens_used":27123,"tool_calls":12}
```

## Modes and exit codes

In headless mode, envelope flags **imply `--auto`** (a bounded unsupervised run
must act without prompts) unless you pin a mode with `--plan`/`--auto`.

| Outcome | Exit |
| --- | --- |
| Verified ok / no verifier configured | 0 |
| Verifier still failing after retries | 1 |
| A budget was exhausted | 1 |
| Interrupted (Ctrl-C) | 130 |

The agent loop itself returns success (the *turn* completed); the envelope's
outcome determines the process exit code.

## Config

Project-stable defaults can live in the config file; CLI flags override them.

```json
{
  "verify": "make && make test",
  "verifyKind": "invariant",
  "editScope": ["src/**", "tests/**"]
}
```

(`verifyKind` declares what the verifier asserts — see *Two kinds of verifier*
above; an unknown value warns and is ignored, and the CLI flag wins.)

Budgets, retries, rollback and the journal path are CLI-only (per-invocation).

## Surfaces

- CLI: the flags above; the audit journal.
- TUI: `/verify` runs the configured verifier on demand and prints the result.

## Implementation

`src/chat/jc_envelope.c` (+ `include/jc_envelope.h`) holds the envelope: the
pure helpers (`jc_env_parse_size`/`jc_env_parse_duration`, `jc_glob_match`,
`jc_env_path_in_scope`, `jc_env_over_budget`) are unit-tested, and the verifier
runner (`jc_env_run_verify`, fork/exec `/bin/sh -c`) + JSONL journal
(`jc_env_journal_begin`/`_end`) are verified end-to-end. The agent loop
(`src/chat/jc_agent.c`) meters tokens in `stream_once`, checks budgets and the
edit-scope per call, captures the green baseline at the first snapshot, runs the
verify gate at completion, and rolls back via the new
`jc_snapshot_restore_commit` (`src/snapshot/jc_snapshot.c`). The envelope lives
on `jc_app.env`; a NULL there leaves every code path unchanged.

See also: [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md) — driving the envelope in a
looped/scheduled supervisor over a task queue, with the reporting channels and a
loop-specific threat model.
