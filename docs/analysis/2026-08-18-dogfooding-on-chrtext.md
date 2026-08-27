# Dogfooding jichi on chrtext: a controlled A/B, and a green gate that lied

*2026-08-18. Three headless `--auto` runs against a real second project (chrtext,
140k lines of Zig), driven with the live HRZ gateway and `--log-level full`. The
work landed — chrtext's build was broken and now is not — but the reason to write
this up is what the telemetry says about **jichi**.*

*Three findings are worth a reader's time: a **controlled A/B** confirming
[`TOOL_OUTPUT_COST.md`](../TOOL_OUTPUT_COST.md) §6's recommendation with numbers it
did not previously have; a run where **`zig build` passed while the agent broke two
files** — a lesson about what an envelope's assurance is worth; and **a real defect
in the envelope** that the runs exposed and M473 fixes.*

---

## 0. What was driven, and what landed

| Run | Task | Outcome | Correct? |
|---|---|---|---|
| 1 | make the ONNX system-library link optional in `build.zig` | ok, 23 tool calls | **yes**, verified independently |
| 2 | *identical prompt*, after adding read guidance to chrtext's `AGENTS.md` | ok, 9 tool calls | **yes**, same result, tighter diff |
| 3 | judge 16 `\\n`-in-a-string sites: fix the bugs, leave the intentional ones | `budget_exhausted` at 40 calls, work kept | **1 of 3 edits right** |

chrtext gained: a build that works (`zig build` exited 1 before this, on a hard link
to an absent `onnxruntime`), a duplicate `linkSystemLibrary("crypto")` removed, a
corrected Zig-version claim, and a print that ends its line. Committed as `d7dda49`
and `2a9db9c`.

---

## 1. The A/B: four sentences in AGENTS.md, −70% tokens

### What run 1 did

`build.zig` is 1,764 lines / 79 KB. The change needed was ten lines at the top. The
agent read the whole file **five times**, never passing `offset` or `limit`.

jichi's own telemetry reader diagnosed it without being asked:

```
Compaction pressure: 4 of 4 mid-turn pass(es) ran with the high-water trigger fired (100%)
Compaction UNRELIEVED: 3 of 4 pressured pass(es) ended still above the high-water (75%),
  so each re-triggered on the next round. Eliding cannot relieve those turns --
  the lever is SMALLER tool output, not a lower threshold.
Compaction SHORT: 3 mid-turn pass(es) could not reach the target -- requests went out
  over the configured contextLimit.
```

Three requests went out **over the configured context limit**. For a ten-line edit.

### The intervention

None of this is new to jichi. `TOOL_OUTPUT_COST.md` §2.1 already measured it across a
larger corpus — `offset`/`limit` used in **3%** of the reads where they would have
mattered — and §7 **deliberately rejected auto-bounding reads**, on the grounds that
the lever is the agent's instructions, not the tool. §6 then lists four rules "for an
agent's instructions (`AGENTS.md`, a skill, or an output style)".

chrtext's `AGENTS.md` carried none of them. It carried three unfilled template
placeholders (`<e.g. 4-space indent, no tabs>`) instead.

So: put the four rules in, name the actual files and their actual sizes, change
nothing else, and run the **identical prompt** again.

### The result

| | run 1 (no guidance) | run 2 (guidance) | |
|---|---|---|---|
| model calls | 25 | **11** | −56% |
| input tokens | 499,407 | **151,334** | −70% |
| history compactions | 4 | **0** | eliminated |
| …of which went over the context limit | 3 | **0** | eliminated |
| `read_file` bytes returned | 98,735 | **6,285** | −94% |
| history share of input | 45% | **18%** | −27 pts |
| tool calls | 23 | **9** | −61% |
| diff produced | 42 lines | 18 lines | tighter |

Same task, same model, same verifier, both results independently verified as correct
(`zig build` exits 0; `-Donnx=true` still fails on the missing library, which is the
proof the option re-enables the link rather than silently dropping it).

**This is the number `TOOL_OUTPUT_COST.md` §7's decision deserved and did not have.**
That section rejected auto-bounding on reasoning; this is a controlled measurement
that the alternative it chose actually works. Worth propagating:

- the `init` scaffold's `AGENTS.md` template should ship these four rules, not
  angle-bracket placeholders — every project jichi scaffolds starts without them;
- the rules work better when they name **the actual large files and their sizes**.
  "Be careful with big files" is advice; "`build.zig` is 1,764 lines, use a 60-line
  window" is an instruction.

### And the limit of the intervention — run 3

Run 3's task touched 16 sites across 8 files. Reads averaged **810 bytes** (21 calls,
17 KB) against run 1's ~20 KB per call, so the guidance was being followed. It still
hit **6 compactions, all 6 SHORT** — every one going out over the context limit.

jichi's reader again names it exactly: *"a history of many small ones leaves nothing
to elide."* The guidance fixes read **size**; it does nothing about read **count**,
and elision cannot reclaim a history made of many small items. That is a real
residual, and it is the one §2.3 of `TOOL_OUTPUT_COST.md` already points at from the
shell side ("the cost is the 8,530 — *making fewer calls* would").

---

## 2. The green gate that lied

Run 3 was deliberately a **judgement** task: 16 occurrences of `\\n` inside Zig string
literals, some genuine bugs (a print meant to end a line) and some **intentional**
(writing a literal `\n` into generated output). Fix only the bugs. The prompt said so
explicitly.

The agent proposed three fixes. One was right. Two were wrong — and both were in the
category the prompt had named:

```
zig/analysis/proof/graph.zig:208        a Graphviz DOT label
zig/analysis/explanation/types.zig:642  a Graphviz DOT label
```

In DOT, a line break inside a quoted label **is** the two-character escape `\n`; the
emitted file must literally contain a backslash and an n. The surrounding function is
called `serializeToDot` and emits `digraph ProofGraph {`. The original code was right.

I verified that rather than asserting it, by rendering both forms through graphviz:

```
escaped  node A ... "line1\nline2" solid ellipse black lightgrey     <- one clean label
raw      node B ... "line1                                            <- breaks apart
```

**And `zig build` passed the whole time.** It passes whether or not those labels are
correct, because the defect is in what the program *prints at runtime*, which
compilation cannot observe. The envelope reported no verify failure because there was
none to report.

### The lesson, which is not "the model was wrong"

Models get judgement calls wrong; that is priced in, and it is why a human reviews.
The transferable part is about **the gate**:

> An envelope's assurance is bounded by what its verifier can observe. `zig build` is
> a strong verifier for "does it compile" and a **null** verifier for "does it print
> the right bytes". A green gate on the wrong verifier is worse than no gate, because
> it converts "unchecked" into "checked".

Concretely, for jichi's own docs: [`AUTONOMY.md`](../AUTONOMY.md) and
[`GATE_INTEGRITY.md`](../GATE_INTEGRITY.md) tell operators to set `--verify`. Neither
says **choose a verifier that can see the class of change you are asking for**. A
task that changes output should be gated on output, not on the build. That belongs
next to the existing `--verify-kind invariant|goal` guidance, which already makes
operators think about *when* the gate is true but not about *what it can see*.

### The budget stop, and a recommendation I got wrong before checking

Run 3 stopped at the 40-call cap having never passed a mid-run verify, and said:

```
[jichi warn] envelope: no verify passed during this run, so there is no known-good
checkpoint -- keeping the work rather than reverting to an unverified baseline. If
the gate was already red before the run, fix it first: a rollback could not have
helped.
```

**My first conclusion was that the operator should pass `--verify-baseline`
alongside `--verify-kind invariant`. That is wrong**, and `AUTONOMY.md` says so
plainly: "declaring a kind arms the run-start baseline probe (no separate
`--verify-baseline` needed)". I had written the recommendation before reading the
section. Checking it turned a wrong tip into the real finding:

The journal for that run contains, two lines above the warning:

```
baseline {"exit": 0, "kind": "invariant"}
```

**The probe ran, and it passed.** The starting tree was verified green. Yet the
envelope reported "no known-good checkpoint" and asserted "a rollback could not have
helped" — when a rollback was exactly what would have helped, and would have
discarded the two incorrect DOT edits.

The cause is a seam between two milestones, and `jc_envelope.h` states one half of
it itself:

> `green_verified` — 1 once a verify actually PASSED in this run, so `green_commit`
> is known-good rather than assumed-good. The first pre-edit checkpoint is recorded
> as green **on the premise that the tree started green; nothing checks that
> premise**, and a run whose gate is already red had its work discarded at the
> budget stop in favour of an equally red baseline.

M207 added `green_verified` **because nothing checked that premise**. M343 then
added the baseline probe, which checks *exactly* that premise. The two were never
connected: the probe journals its verdict and warns on the two bad ones
(`NOT_KNOWN_GOOD`, `FORCES_NOTHING`) and drops `JC_BASELINE_OK` — the good news — on
the floor. `green_verified` is set at two mid-run sites and nowhere else.

**Fixed in M473**: a `JC_BASELINE_OK` verdict now sets `green_verified`. Only that
verdict, which `jc_env_baseline_check` already restricts to unset-or-invariant — a
GOAL gate passing at the start proves nothing about the tree, and gets
`FORCES_NOTHING` instead.

**Being honest about what this fix would and would not have done for run 3:
nothing.** Run 3's verifier was `zig build`, which was green *with* the bad edits, so
the budget stop would have kept the work either way. The defect it fixes is the
neighbouring one: a run that starts verified-green, breaks the gate, and hits its
budget currently keeps the broken tree while telling the operator a rollback could
not have helped. `tests/smoke/baseline_checkpoint.sh` pins both halves — the rollback
that should now happen, and M207's rule that a run starting **red** still keeps its
work, which must not regress.

---

## 3. `learn analyze <path>` mixes sources without saying so

Running jichi's own miner on run 1's telemetry:

```
$ jichi learn analyze .../run1.telemetry.jsonl
Recurring problems (2):
  - 4 history compactions -- turns run long; consider tighter scoping or a larger contextLimit
  - '/home/u/development/adventure/chrtext/chrtext_web/lib/.../worker_pool.ex' was
    edited 4 times in one session -- a fix/break/fix loop; capture the gotcha as a lesson
```

The first finding is from the file I named and is correct. The second is not in that
file at all:

| check | result |
|---|---|
| occurrences of `adventure` in the telemetry file named on the command line | **0** |
| session files in `~/.jichi.d/sessions/` containing it | **25** |
| does `/home/u/development/adventure/chrtext` exist? | **yes** — a different checkout |

The behaviour is documented — `--help` says "Mine telemetry **+ recent sessions**" —
so this is not a bug. It is a **provenance** problem in the output: two findings are
printed in one list with no indication that one came from the argument and the other
from global session history, in a different working copy of the same project.

Why that matters more here than it looks: this repository's own history contains
`373941d`, *"stop six scripts pointing at a DIFFERENT checkout of this project"*. The
two-checkouts-of-chrtext confusion is a known, previously-costly failure mode in this
environment, and `learn analyze` reproduces it in its output.

**Fixed in M474.** Each finding now carries its source, and a session finding carries
the workspace it came from:

```
Recurring problems (2):

  - [telemetry] 4 history compactions -- turns run long; consider tighter scoping ...
  - [session: /home/u/development/adventure/chrtext] '.../worker_pool.ex' was
    edited 4 times in one session -- a fix/break/fix loop; ...

  [session] findings are mined from the global session store, not
  from the telemetry named on the command line -- check the workspace
  before acting on one.
```

Three implementation notes worth keeping:

- The workspace has to **travel with the finding**, stamped inside the session loop,
  because with no `--workspace` filter every session is scanned and each one may
  concern a different checkout. Inferring it afterwards is not possible.
- An already-stamped finding is **not overwritten**, so an inner scan that knows more
  (which session) beats the outer sweep that knows less.
- An **unstamped** finding renders exactly as before. `jc_insights_render` has another
  caller — `dream` — and a provenance change should not alter anything that did not
  ask for it. As it happens `dream` goes through the same miner and gains the labels,
  which is where they help most: it prints `Source log: <path>` directly above the
  list, so "which of these came from that log?" was previously unanswerable.

---

## 4. What I would change in jichi, ordered

1. **Ship the four read rules in the `init` scaffold's `AGENTS.md`** instead of
   angle-bracket placeholders. Measured: −70% tokens, −94% read bytes, all
   compactions eliminated. Every scaffolded project currently starts without them.
   (§1)
2. **DONE in M474: `learn analyze` findings carry their source**, and a session
   finding carries its workspace. (§3)
3. **Add "choose a verifier that can see this change" to the `--verify`
   guidance** in `AUTONOMY.md`/`GATE_INTEGRITY.md`, with the DOT case as the worked
   example. (§2)
4. **DONE in M473: a green baseline sets `green_verified`.** M207 wanted proof that
   the tree started green; M343 built the probe that provides it; nothing joined
   them. (§2)
5. **The read-count residual** (§1, run 3) is a genuine open question, not a fix:
   many small reads defeat elision just as whole-file reads defeat the context limit,
   and neither the guidance nor the compactor addresses it. `TOOL_OUTPUT_COST.md`
   §2.3 makes the same observation about shell calls. If anything is worth measuring
   next, it is that.

---

## 5. Honest limits of this exercise

- **Three runs, one model, one project.** The A/B is controlled (identical prompt,
  only `AGENTS.md` differing) but n=1. The direction is unambiguous; the magnitude is
  one measurement, not a distribution.
- **The A/B ran second.** Run 2 executed after run 1 on the same day against the same
  gateway; no prompt-cache effect should apply across separate processes with distinct
  cache keys, but I did not isolate for it.
- **Run 3 is not a controlled comparison with anything** — different task, broader
  scope. Its numbers say what that task cost, not what the guidance is worth.
- **I judged the DOT question myself** and then checked it with graphviz rather than
  trusting my own reading. The graphviz output is the evidence; my reasoning about
  DOT semantics was the hypothesis.
- **`learn analyze`'s second finding may be useful** to someone who wants
  cross-project patterns. The complaint is about labelling, not about mining
  sessions.
