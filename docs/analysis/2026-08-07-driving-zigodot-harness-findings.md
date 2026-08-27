# Driving zigodot headless — what the harness got wrong (2026-08-07)

*jichi-side findings only. zigodot facts (its parser, its gate debt) belong in that
project's own docs; what follows is about **jichi and the way a run is bounded**.*

**Setting:** the low-resource box (3 cores, 4.8 GiB, no sudo) driving the zigodot
workspace with jichi 0.9.0 (HEAD `d9cb80f`, M326z) against the HRZ proxy —
`jlu/gemma-4-31b-it` (256k window) as the strong tier, `jlu/qwen3-coder-next` (150k)
as fast, **0% prompt cache**.

**Evidence base:** 17 bounded runs, **16.75M tokens**, journals in
`~/jichi-runs/zigodot/journals/`, read with `jichi runs --since 1d --output json`.
Every number below is from those journals or from a command re-run to check a claim.

---

## 1. The headline split: design runs succeeded, implementation runs did not

| Run class | Reached `outcome: ok` | Tokens |
|---|---|---|
| Documentation / spec (10 runs) | **8 / 10** | 5.77M |
| Implementation (7 runs) | **0 / 7** | 10.98M |

**Every** implementation run ended `budget_exhausted` and needed a human to finish the
last step -- seven for seven, at 1.6M tokens each on average. The documentation runs
produced ~120 KB of usable, largely accurate material at roughly a third of the cost per
run.

The two documentation failures are worth naming, because neither was the model's doing:
one died `budget=reads starved` after a phrase in *my* brief triggered M110 and banned
the sweeps it depended on (§4), and one was `verify_failed` because *my* gate demanded
9000 bytes of a document that arrived at 8396 (§8). Configured correctly, that class of
run succeeded 8 times out of 8.

That is not a statement about the model's intelligence; it is a statement about
**feedback loops**. A documentation task's output is checkable in one pass. An
implementation task on this codebase is an iterative debugging loop against a
compiler, and each iteration re-bills the full prompt at 0% cache.

**Measured per-call cost:** ~**28k input tokens/call** on implementation runs,
~**13k/call** on documentation runs (less history, fewer large tool results). So a
500k budget buys ~18 implementation calls and a 2M budget ~55. Sizing a budget in
*tokens* without dividing by ~28k is how three separate runs died mid-task.

## 2. A verify gate that checks reachability is not checking exercise — PARTLY FIXED

The sharpest finding, and it generalises well past Zig.

zigodot's `zig build test` was green at 253/253 while `src/agent/serializer.zig`
contained six compile errors. Both true simultaneously: **Zig analyses function
bodies lazily**, so `test "gate: agent" { _ = @import("agent/index.zig"); }` compiles
that file's *declarations* and never the body of a `pub fn` nothing calls. Proven
with a two-test probe — import-only passes; a test that *calls* `serializeVariant`
fails to compile instantly.

zigodot's own `tests/gate_lint.py` measures file reachability, so it structurally
cannot see this class. jichi's **M86 hollow-gate guard** (`no_tests` /
`fewer_tests`) is the closest existing check and it would not catch it either: the
test count does not change, because the un-exercised code was never counted.

**Implication for jichi:** M86 asks *"did the verifier run fewer tests than before?"*
A complementary question is *"did the verifier's coverage of the changed files
change at all?"* Not obviously cheap to answer in general, but worth stating as a
known blind spot in `docs/AUTONOMY.md` beside M86, because "green gate, broken code"
is exactly the situation the envelope is supposed to prevent.

## 3. Three gates, each satisfiable without the goal

Written by hand, in sequence, each fixed after it let something through:

1. `zig build test` alone — passes at 253 before *and* after the tests are added.
   A run spent 8 consecutive edits against that false green. **Fix:** assert the
   count *grew*.
2. `grep -q "UnexpectedToken at '^'"` — passed the moment the tokenizer consumed the
   caret, while the parser still rejected the literal and the script still failed one
   column later. **Fix:** assert the script *parses* (see below), not that one
   message stopped appearing.
3. `[ "$n" -lt "$BASE" ]` — "did not shrink". A run implemented an entire feature
   with **zero tests** and passed. **Fix:** `-le`, i.e. strictly greater.

The generalisation: **a gate phrased as the absence of a known failure is weaker
than one phrased as the presence of the goal.** The strong form found for the parse
case was to exploit a property of the tool — zigodot's `gd_runner` prints a
`file:line:col:` diagnostic *only* on parse failure — so the absence of that line is
positive evidence of parsing, and cannot be satisfied by suppressing one message.

Worth a short section in `docs/AUTONOMY.md`: when writing `--verify`, prefer
asserting the deliverable exists and has the property you want, over asserting that
a particular error is gone.

## 4. M110 constraint inference fires on descriptive prose — 4 of 8 runs — FIXED (surfaced)

Counted across the runs' jsonl (`grep 'adopted constraints'` /
`'blocked by an active constraint'`):

| | runs |
|---|---|
| silently adopted a constraint | **4 of 8** |
| actively blocked by one | 1 (three times) |

The triggering text was **descriptive, not imperative** in every case:

- *"force **never**-compiled core code to **compile**"*
- *"its body has **never been compiled**"*
- *"47 of 88 files **never compiled**"*

The inferred constraint was `do not run build commands (make / cmake / compile /
...)`. Consequence depended entirely on whether the run needed that tool: one brief
was banned from the `zig build run-gd` and `tests/demo_sweep.py` calls it centrally
depended on, read files instead, and died `budget=reads starved` **with no
deliverable**. Rewriting that one phrase to *"make dormant core code compile for the
first time"* produced a clean run with zero adopted constraints, which then executed
`corpus_sweep` ×1, `demo_sweep` ×2 and `zig build run-gd` ×1.

Notably, another run adopted the same ban and finished fine — because `python3
gate_lint.py` and `zig ast-check` are not classified as build commands, so only
`zig build` was blocked.

**Worth noting honestly:** `docs/AUTONOMOUS_LOOPS.md` already warns about this
footgun. I wrote that warning into my own plan for this work and then tripped it in
the next brief I authored. A prose warning is not sufficient mitigation for a
mechanism that pattern-matches prose. Two cheaper defences:

- a **lint** the operator can run on a brief before spending on it:
  `grep -niE "(do not|don't|never|avoid|without)[^.]{0,80}(build|compile|make|run|test)"`
- surfacing adopted constraints in `jichi runs` the way `steered=N` already is, so a
  post-mortem shows it without grepping the jsonl. A silent adoption currently
  announces itself once on stderr and never again.

## 5. A docs run that writes nothing exits 0 with no deliverable — FIXED (surfaced)

`jc_agent.c` gates the completion verify on `snapshotted`, which is only true after a
mutating tool call. So a run that produces no file **never runs its own verifier**
and exits 0. M96's `starved` flag does not cover it either — that fires on budget
exhaustion, not on giving up early.

This is easy to work around once known (the supervisor post-checks the deliverable
independently, which is what caught it here), but it is a real gap between "exit 0"
and "the work happened", and `docs/SCRIPTING.md`'s exit-code table currently implies
otherwise.

## 6. `cmd | tee log` reports tee's status, not jichi's

Mundane and expensive: a wrapper that tees jsonl to a file reads **every run as exit
0**, so a supervisor routes failures to `done/`. `set -o pipefail` (or
`${PIPESTATUS[0]}`) is mandatory. I made this mistake twice in one session,
including inside a test *of* the fix.

`examples/autonomous-loop/loop.sh` does not tee and is unaffected, but the
`--output jsonl` examples in `docs/SCRIPTING.md` are exactly the shape that invites
it. One sentence there would have saved this.

## 7. A command's `model:` is silently overridden by turn-start routing — FIXED

Two separate mechanisms defeat an author's attempt to pin a model, and together they
made six zigodot agent profiles ineffective for their stated purpose.

**(a) An agent profile's `model:` is subagent-only, by design.** `jc_app.c:51`
(`jc_app_command_agent_apply`) applies the profile's persona, style and `readonly`, and
its comment is explicit: *"The profile's model/tools fence are a subagent-only concern;
a command runs in the current turn with the current model and permission posture."*
So `agent: cpp-analyst` + `model: strong` in that profile has no effect on a top-level
`/analyze`. Documented, but surprising enough that zigodot has six profiles written on
the opposite assumption — each with a comment explaining *why* it wants the wide-context
tier. `doctor`'s M284 selector lint validates that the selector RESOLVES; it cannot know
the selector sits somewhere that is ignored.

**(b) A command's own `model:` IS applied, and then clobbered.** `main.c:1892` calls
`jc_app_command_model_apply` *before* `jc_agent_run_turn`, and `jc_agent.c:2549` then
calls `jc_app_route_to(..., "turn-start")` unconditionally whenever
`jc_config_routing_resolve` succeeds. Measured with a probe command declaring
`model: zigodot-strong` (= `jlu/gemma-4-31b-it`) against a fast tier of
`jlu/qwen3-coder-next`:

| invocation | model that answered |
| --- | --- |
| `jichi -p /modelprobe` | `jlu/qwen3-coder-next` — declaration ignored |
| `jichi --no-route -p /modelprobe` | `jlu/gemma-4-31b-it` — honoured |

The consequence in practice: an analysis command intended to run on a 256k window ran
on a 150k one, silently. The workaround is `--no-route` per run; the fix would be for
turn-start routing to leave an explicitly-pinned model alone (the routing comment at
`jc_agent.c:2538` explains why routing runs before compaction, which is sound — the
question is only whether it should override an explicit pin).

Worth noting for the docs either way: `docs/ROUTING.md` does not mention the
interaction, and `docs/COMMANDS`-side docs present `model:` as effective.

## 8. The opposite gate failure: an arbitrary threshold destroyed good work

Findings 3 and 7 are about gates that were too weak. This one is the mirror image, and
it cost more in a single run than any of them.

The gate for a `/plan-port` document required `>= 9000` bytes, a number copied from the
analysis gate where the target happened to be a 25 KB document. The run produced
**8396 bytes** — with the required mermaid diagram, all four sections a port plan is for
(allocator, ownership, test strategy, implementation order), proposed `src/` module
paths, and `path:line` citations that all resolved. A 7% shortfall on a size heuristic
failed the verify, `--verify-retries 2` spent two further attempts, and the rollback
then **discarded the entire document**. It was not recoverable: checkpoints are taken
*before* a mutating tool runs, so the pre-edit snapshot did not contain the file, and
without `--verify-every` no intermediate green had been banked.

**Byte count is a proxy for substance and must never stand in for it.** The content
checks in that same gate — sections present, diagram present, citations resolve — are
the actual assertions and they all passed. The size floor was doing no work except
adding a failure mode. It is now a stub-detector (2500) and nothing more, in both gates.

Two smaller lessons fall out of it:

- **`--verify-retries` on a document run is mostly waste.** The only recoverable failure
  is "you did not write the file"; a document that fails a *content* assertion will
  usually fail it again. 1 is the right number, not the default 3.
- **`--verify-every` is cheap insurance even on a docs run.** It banks a green
  checkpoint, so a later failure rolls back to the last good state instead of to
  nothing.

The general form, which is now the rule used for every gate in this project: **assert
properties the deliverable must HAVE, never proxies for how good it is.** A threshold on
a proxy metric is a coin flip weighted by how verbose the model happened to be.

## 9. `learn apply` misroutes an AMBIGUOUS heading into memory notes — FIXED

The learn loop was exercised end to end on this project for the first time, and it
failed in a way worth fixing in jichi rather than in the project.

**What happened.** `learnOnStop` fired after each clean run and drafted genuinely good
lessons -- it independently found the hollow-gate class, the allocator-convention
mismatch and the Zig 0.16 API removals. But the mentor emitted its own headings,
`## Fix/Break/Fix Loops` and `## Memory Note Corrections`, with bullets of the form
`- **Remove**: "..."` and the replacement on an indented sub-bullet.

`jichi learn corrections` correctly applied **0** -- it looks for `## Corrections` with
`- remove: <substr>` / `- replace: <substr> => <new>`. Then `jichi learn apply`
**filed the two correction DIRECTIVES as plain memory notes**, appending them verbatim:

```
- **Remove**: "The GDScript parity oracle is modules/gdscript/tests/scripts."
- **Replace**: "C++ navigation ... is NOT available on this machine..."
```

So instructions to *modify* memory became durable *facts*, self-contradictorily (both
notes they name were still present above), and `memory.md` grew to 8178 B -- **14 bytes
from the 8192 cap**, at which point the oldest orientation notes start dropping
silently. Restored by hand.

**This is not the scaffold's fault.** `.jichi/agents/mentor.md` states the contract as
forcefully as prose allows: *"FORMAT IS STRICT (a tool parses it). The file must contain
ONLY these four level-2 headings, verbatim and in this order, and NO other `##`/`###`
headings (do not invent per-tool or per-problem sections)."* The model invented two
headings regardless. A propose-only loop with a human review is exactly the right design
for that, and the review is what caught it.

**Root cause, corrected.** My first diagnosis here said "bullets under an
*unrecognized* heading are filed as memory notes". That was wrong, and the real
mechanism is narrower and more interesting: the heading WAS recognized -- twice.
`## Memory Note Corrections` contains both "memory" and "correction", and
`jc_learn_parse_draft` classified headings by substring with **first match winning in a
fixed order**, testing `ci_contains(hdr, "memory")` before `"correction"`. So the
section became `SEC_MEMORY` and the `- remove:` / `- **Remove**:` bullets beneath it
were filed as notes. A genuinely unrecognized heading was always handled correctly:
it leaves `section` unchanged, and bullets under `SEC_NONE` are dropped.

**Fixed** by ordering the keyword tests most-specific-first -- "memory" is the most
generic of these words, since every one of these sections is about remembered state, so
it is now matched last. Six checks in `tests/test_learn.c` pin each precedence pair
(corrections, rules and skills each beat "memory") and all six fail if the ordering is
put back.

The fix needed no new error path: it *unblocked* an existing one. Running `learn apply`
against the exact draft that caused the corruption now reports "Applied nothing: ... has
no '## Memory notes' bullets or '## Skills' (### name: desc) sections. Edit the draft
into those exact headings ... then re-run", and leaves `memory.md` byte-identical. That
message already existed; the misrouted heading was bypassing it.

**The feature is sound when the format is right.** Rewriting the same correction by hand
in canonical form worked exactly as designed:

```
$ jichi learn corrections
  corrected memory: replaced "HOLLOW GATE, SECOND ORDER" => ...
Applied 1 correction(s).
```

`memory.md` went 7987 -> 7760 B, and a note that still claimed the gate was "green at
253/253" with six live compile errors now reads in past tense, citing the commit that
fixed it. Worth noting for the record: **both corrections the mentor proposed were wrong
on the merits anyway.** It wanted to delete "the GDScript parity oracle is
modules/gdscript/tests/scripts" because "the project has moved toward a corpus sweep" --
but `tests/corpus_sweep.py:13` reads exactly that directory. The corpus sweep *is* that
oracle. The format failure prevented a bad edit.

---

## 10. Two kinds of verifier: an invariant and a goal, treated as one

`--verify` serves two purposes that look identical on the command line and behave
very differently once a run is bounded.

An **invariant** verifier answers *is the tree healthy?* — `zig build test`. It is
green before the work, green after, and red only when something broke.

A **goal** verifier answers *has the work happened?* — the four-check gates used for
these gating increments: the suite passes, the test count grew past a baseline,
`gate_lint` is clean, and the dark count fell. It is red before the work **by
construction**. That is not a defect in the gate; it is what makes the gate
unfakeable, and §3 and §8 are the record of what happens when a gate is weakened
until it can pass without the work.

jichi treats the two the same, and three features quietly change meaning:

| Feature | With an invariant | With a goal gate |
| --- | --- | --- |
| `--verify-baseline` | detects a broken starting tree | warns on **every** run: "the starting tree is not known-good" |
| `--verify-every N` (M81) | banks a green checkpoint mid-run | **can never bank one** — the gate cannot pass until the work is complete |
| M80 budget-exit rollback | keeps work iff the verifier is green | the verifier is red until the last edit, so a budget exit near the end looks identical to one at the start |

The `--verify-baseline` case is the mild one, but it is the corrosive kind of noise:
a warning that fires on every correct run is a warning an operator learns to skip,
and it is the same line that would announce a genuinely broken tree.

`--verify-every` is the substantive loss. Its value is banking green checkpoints so a
later budget exit reverts less (M80/M81), and against a goal gate it produces nothing
to bank while still paying for a full verifier run every N tool calls. On these
increments that is an eight-minute `zig build test` spent for no checkpoint.

**No jichi change is proposed here yet**, because the fix is not obvious and the
wrong fix is worse than the gap. Two candidates, with what is wrong with each:

- *Split the flag* — `--verify` (invariant) and `--goal` (checked only at
  completion). Honest, but it doubles a surface every driving script already uses,
  and an operator who picks the wrong one gets today's behaviour silently.
- *Infer it* — run the verifier once at start; if it is red, treat it as a goal and
  suppress the baseline warning + mid-run banking. Free at the call site, and it
  guesses: a genuinely broken tree would be reclassified as a goal, which is exactly
  the case `--verify-baseline` exists to catch. Inferring the *opposite* of the
  intended meaning from the symptom is how M110's misparses happened.

The honest interim position is the one this section documents: **know which kind of
verifier you have written.** With a goal gate, expect the baseline warning and read
it as noise, and set `--verify-every` high or omit it, since it buys nothing.

Evidence: run 15 (`gate-string`) logged
`envelope: baseline verification failed (exit 1); the starting tree is not
known-good` at startup, against a tree whose unit suite was green at 299/299 — the
gate was red only because the test count had not yet grown past its own baseline,
which was the assignment.

---

## 12. Two operator errors that cost a run each, both about the environment

Neither is a jichi defect. Both are the shape of mistake that wastes a bounded run and
looks like a tool failure, so they are worth the space.

**`export $(grep KEY file)` does not strip quotes.** The launch script used

```sh
export $(grep -h JLU_API_KEY ~/.jichi.env | head -1)
```

and the file's line is `export JLU_API_KEY='sk-…'` — with single quotes. Command
substitution and word splitting do NOT remove them, so the exported value was
`'sk-…'`: 27 characters beginning with a quote. The backend replied

```
HTTP 401: LiteLLM Virtual Key expected. Received='sk-****HzA', expected to start with 'sk-'
```

which reads like a key problem, and is not. The masking even hides the leading quote,
so the error message shows a value that looks correct. Twenty minutes went into
verifying the key three ways — `GET /models` 200, both chat models 200 by curl, all
three key sources byte-identical by hash — before the launch command itself was the
suspect. `. ./local/env` (which the shell parses, quotes and all) is the form that
works, and is what the earlier runs used.

**The lesson generalises past this key:** when a credential fails in a wrapper but
works by hand, compare the *bytes the wrapper exported*, not the bytes in the file.
`len`/prefix/suffix of the exported variable found it in one command.

**A "goal" gate makes `--verify-baseline` a permanent warning.** Every gating run so
far opened with

```
envelope: baseline verification failed (exit 1); the starting tree is not known-good
```

against a tree whose suite was green, because the gate is red until the work lands —
by design (§10). It is correct, it fires on every run, and it is the same line that
would announce a genuinely broken tree. The second run dropped `--verify-baseline`
rather than keep reading past it, which is the honest response to a warning that
carries no information for this gate shape.

---

## 13. M328 fixed the wrong half: learn-on-stop's cost is invisible even when it SHOULD run

M328 stopped learn-on-stop firing after an interrupted run. M329 added a guard for the
class — a model call metered after the envelope's outcome is set. On the very first clean
completion after both landed, the guard fired, and it was right to.

```
[jichi warn] envelope: a model call was metered AFTER the run ended (ok) -- its tokens
             are outside the run's accounting and will not appear in the journal or
             `jichi runs`
```

Because the run **completed**, learn-on-stop correctly ran. And learn-on-stop runs *after*
`run_agent_loop` returns, so by construction it happens after the outcome is set and the
`end` event — carrying the run's totals — is already written.

| source | tokens | tool calls |
| --- | --- | --- |
| journal `end` event | **223,346** | 10 |
| telemetry (actual) | **975,312** | 53 |

**752k tokens — 3.4× the run itself — spent after the journal closed**, by the mentor.
`jichi runs` renders the row honestly *because of M329*:

```
0d4b525e...  ok  223.3k  10  2/0  post_outcome(totals_short)
```

Without that flag an operator reads 223.3k and is wrong by 4.4×. With it they know the
number is short but not by how much.

**So M328 addressed the case where the mentor should not run at all, and left the case
where it should.** The remaining gap is not "learn-on-stop is wrong to run" — on a clean
completion it is exactly right — it is that its cost is attributed to nothing.

**Proposed fix, not yet made:** after `learn_on_stop` returns, emit a `learn_on_stop`
journal event carrying its token cost, and have `jichi runs` render it beside the run's
own total. The journal file is still open at that point, so this needs no restructuring.
Rejected alternative: setting the envelope's outcome *after* the mentor runs, which would
fold a separate activity into the task's verdict — the outcome answers "did the work
succeed", and a lesson-drafting pass is not part of that answer. M329's `post_outcome`
flag then stays as the general alarm for calls nobody expected, while learn-on-stop
becomes a known, priced line item.

**What this says about the guard.** M329 was written from a bug found by hand-comparing
three sinks, and it caught a *different* instance within hours — one that is by design
rather than by defect. That is the argument for a lint over an audit in one paragraph: the
audit found what it was looking for, the lint found what nobody was.

---

## 14. The unsatisfiable gate, twice, and what it cost

Two runs in one session, ~3 M tokens, both `budget_exhausted` with the verifier never
green — and in both cases the gate could not be satisfied by a correct change.

| run | the gate demanded | the scope or contract allowed | cost |
| --- | --- | --- | --- |
| 17 | `gate_lint` exit 0 | writes to `tests/gate_lint.py` only — but exit 0 required editing `src/platform/linux/test.zig`, whose two tests contained no assertion | ~929 k tokens |
| 20 | the string `learn_on_stop` in `runs --output json` | the brief said to follow how `steered`/`post_outcome` are plumbed, which produces the keys `learn_tokens`/`learn_calls` | ~1.53 M tokens |

Run 20's work was **complete and green** when it died: 10,755 unit checks (up from
10,734), a registered smoke driver passing all four of its own checks. Only the gate
disagreed.

**Run 17 also produced a misread worth correcting.** Blocked by `--edit-scope`, it
achieved the edit with `sed -i` through the shell — which the fence cannot see — and M83's
end-of-turn tree diff caught and reverted it (`reverted: 1, revert_failed: 0`). That was
first written up as a model routing around a boundary. With the scope/gate contradiction
understood, it is a cornered model taking the only route left. The defence worked either
way; the characterisation was wrong.

**A third, quieter flaw.** Run 20's gate had a check meant to *force* the milestone's
ROADMAP entry. It ran `docs_counts_lint`, which asserts the banner's "latest milestone"
matches the newest `### M...` heading — a consistency property that holds trivially when
no entry is added. This is the hollow-gate failure one level up: not a check that cannot
fail, but a **requirement that was never imposed**. Verify a forcing check by removing the
deliverable and confirming the red, exactly as a test is verified.

**Budget sizing was wrong on a different axis.** 1.5 M was chosen by copying what earlier
runs had consumed. The number that matters is calls: ~45 k tokens per call on the jichi
codebase with no prompt caching means 1.5 M is ~33 calls, and run 19 spent 24 of them
reading windows of an 11 k-line `main.c`. A C change plus a unit test plus a smoke driver
plus three documentation artifacts does not fit in the remainder. Estimate calls from the
work; and since the figure is a spending decision, ask the operator for it.

**Process changed as a result**, and these are now standing rules rather than
observations: say what a run is meant to build before starting it; prove the gate is
satisfiable first, by hand-completing or stubbing the task and watching it go green; check
every gate assertion against the contract the brief cites; and ask about the token budget.

---

## Status of these findings

The first nine were addressed on `fix/harness-findings-batch` and
`fix/learn-draft-section-precedence`; 10 and 11 came out of the gating increments
that followed:

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | design vs implementation split | a measurement, not a defect |
| 2 | M86 cannot see an unexercised gate | `tests_not_wired` covers the detectable case: a test file was written and the count did not grow. A source-only change still needs coverage data — stated in `AUTONOMY.md` |
| 3 | four gates too weak | operator lesson; the rule is in the downstream project's `DRIVING_WITH_JICHI.md` |
| 4 | adopted constraints invisible | a `constraint` journal event; `jichi runs` renders `constraints=N` beside `steered=N` |
| 5 | a run that writes nothing exits 0 | `no_changes` on the `end` event, rendered by `jichi runs` and in its JSON |
| 6 | `\| tee` masks the exit code | operator lesson; a note belongs in `SCRIPTING.md` |
| 7 | `model:` overridden by routing | `app->model_pinned` suppresses turn-start routing; reactive escalation still allowed |
| 8 | a byte threshold destroyed a document | operator lesson; byte floors are stub-detectors only |
| 9 | `learn apply` misroutes an ambiguous heading | keyword tests ordered most-specific-first |
| 10 | invariant vs goal verifier conflated | documented, deliberately not "fixed" — both candidate fixes are worse than the gap (see §10) |
| 11 | no way to predict an inferred constraint offline | `jichi constraints scan <file\|->` (M327), exit 1 when anything would be adopted |
| 12 | `export $(grep ...)` kept the quotes; a goal gate warns on every baseline | operator lessons, no jichi change — the first is shell semantics, the second is §10 restated from the driver's seat |
| 13 | learn-on-stop's cost is unattributed even when it correctly runs | **fixed** — M330: a `learn_on_stop` journal event carries the token/tool-call deltas; `runs` renders `learn_on_stop(752.0k)` and emits `learn_tokens`/`learn_calls` in JSON |
| 14 | two gates could not be satisfied by a correct change; a third check forced nothing | operator lessons, now standing rules — prove the gate green by hand first, match every assertion to the cited contract, show a forcing check failing, and ask for the budget |

Three of these (3, 6, 8) are operator lessons with no jichi change to make — a
gate phrased as the absence of a failure, a wrapper that swallows an exit code, and a
threshold on a proxy metric are all mistakes the tool cannot prevent.

---

## What would have changed the outcome most

Ranked by measured effect, not by how interesting the mechanism is:

1. **Write the gate before the code, and phrase it as the goal.** Two of six
   implementation-run failures were the gate's fault, not the model's.
2. **Paste the real compiler errors into the brief.** One increment died at budget
   after 18 calls, most spent rediscovering errors that took one command to collect.
   Doing that first roughly halved the calls on the retry.
3. **Divide the token budget by ~28k** before believing it is generous.
4. **Read the failing line and its column, never a frequency count of a character.**
   Two separate mis-reads in this session — a plan proposing to implement XOR when
   the corpus contains zero uses of `^` as an operator, and my own claim that a `%`
   blocker was the string-format operator when the failure was at column 2 on
   `%UniqueName` — both came from reasoning about a character's usual ASCII meaning.
   The column number settled it both times.
5. **`--control ... inject` is the cheap save.** Watching the jsonl and injecting one
   correction recovered a run that was 8 edits into a hollow green.
