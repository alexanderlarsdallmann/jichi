# Does the craft section earn its tokens on a frontier model? (pre-registration)

*Written before any graded run, per the M299 craft rule — which is again the thing under
test, so this note is written by the subject of the experiment. Stated here so a reader can
weigh it. The M318 pre-registration ([`2026-08-craft-ab.md`](2026-08-craft-ab.md)) is the
direct predecessor; this one exists because M318 could not answer the question it raised.*

---

## What M318 settled, and what it could not

M318 measured the `craft` section (M299, config `craft`) on **one 31B local model** across
three **graded** curriculum tasks: **18/18 passes in both conditions**, including the design
task. It shipped the conclusion its evidence licensed — `craft` off under `--lite`, on
everywhere else — and said plainly what it had not shown.

Its own limiting analysis named the reason: **every graded task states its deliverable.**
`10-design-before-code` lists the five section headings its grader then greps for. A task like
that measures instruction-following. The section's claim is about a *habit* — writing the
design, and the rejected alternatives, when nobody asked.

`DEFERRED.md` recorded three requirements for a real test. All three are now met or built:

| Requirement | Status |
|---|---|
| A frontier model | **Met.** A JLU HRZ liteLLM key with access to the Claude, GPT-5.x and Gemini families, verified 2026-08-07 by real inference and native tool calls, not by a model list. |
| A task whose deliverable is genuinely unstated | **Built.** `tests/bench/craft_ab/tasks/`, three of them. |
| A grader who is not the author of the section | **The operator.** Not this harness, and not the model that wrote the section, the tasks, or the questions. |

## The hypothesis, stated so it can fail

> On a large model, given a task that names a problem and no artifact, the craft section
> produces work a human would rather receive — and the difference is visible without a grader
> being told what to look for.

**If the primary measure comes out flat or negative, the recommendation changes.** The
section currently ships on by default for every non-`--lite` model, on the strength of a
claim about larger models and vaguer tasks that has never been tested. This is that test. A
null result here is not "inconclusive, keep the default"; it is evidence that the default
rests on nothing measured, and it should be recorded that way.

## The tasks

Three, each naming a situation and no deliverable. The full notes — what is deliberately
unstated, and where the real problems are — live in each task's frontmatter, which is **never
sent to the model** and is meant for the grader.

| Task | The prompt ends | The design decision nobody asks for |
|---|---|---|
| `01-notes-grew` | *"Deal with it."* | Rotation, truncation, splitting, compression and "fix the reader instead" all differ in **what they lose**. A year of irreplaceable notes is at stake and the prompt never says so. |
| `02-more-callers` | *"Do what you think is needed."* | Three real defects (a returned pointer to a static buffer, a silent truncation, an unchecked `malloc`). Fixing the API means changing its one existing caller — which nobody authorised. |
| `03-slow-report` | *"I need this back."* | The cost is an O(n·m) membership scan, not the file reading everyone blames; and the data grew 40×, so *some* slowdown is correct. "Measure first" is the honest move and is not requested. |

`02-more-callers` carries a discriminator that needs no grader at all: the fixture **prints
its own bug**. `make && ./report` outputs `host=sc1-w1-1 run=sc1-w1-1`, because both `kv_get`
results alias one static buffer. It is wrong single-threaded, before threads enter the story.
Whether a run notices depends entirely on whether it ran the thing before changing it.

## What is measured

**Primary — blind pairwise preference.** *"Which would you rather have received from a
colleague?"* Phrased with **none of the section's vocabulary**: it does not mention design,
alternatives, or honesty, so it cannot be answered by pattern-matching the thing under test.

**Secondary — the claimed behaviours**, derived openly from the section's own text: did it
write down *why*, name a rejected alternative, say what it did not verify, find the problem
that actually matters, and — as a cost, not a virtue — did it **overreach**.

**Mechanical, no grader** — input/output tokens, model calls, tool calls, files touched, wall
time, and whether anything was read before the first edit.

The questions are fixed in `craft_ab.py` before any run, so they cannot be chosen after seeing
the outputs.

## How the conflict of interest is handled

I wrote the section, the tasks, and the questions. That is three of the four roles in this
experiment, and the fourth is the only one that decides anything.

- **Grading is blind.** A/B order is randomised per pair; the mapping is written to `.sealed/`
  and read only by `score`. File timestamps in the pack are flattened, because run order
  alternates by pair parity and `ls -l` would otherwise unblind half the pairs.
- **The primary measure avoids the section's language**, per above.
- **The pre-registration is the check on me.** Metrics and questions fixed in advance is what
  makes "I would have reported a negative result" verifiable rather than a promise.

What none of this fixes: I chose the tasks, and a task can be chosen to favour an outcome.
The mitigation on offer is that the tasks are in the repository to be read and argued with
before the runs are graded — and that the operator can add or replace one.

## How this could still mislead

- **n is small and the model is nondeterministic.** Three pairs per task is nine pairs. A
  direction is the most this can show; a magnitude is not, and every number is reported with
  its n.
- **A truncated run is not a sample.** The first pilot pair hit a 150k token budget at 9 tool
  calls, because every call resends the history. A cut-off answer loses on every question
  whichever arm it came from, so `blind` **refuses** pairs where either arm did not finish.
- **One model.** A finding about `anthropic/claude-opus-4-5` is a finding about that model.
- **`craft: false` is not "no guidance".** The base persona still asks for reading before
  editing and minimal targeted edits. This measures the M299 addendum only.
- **Cost is measured in tokens, not money**, unless `--price-in`/`--price-out` are given: the
  gateway's billing is not something this repository knows.

## What will not be concluded

- **Not** "delete the section" or "keep it forever". The outcome is a recommendation about a
  **default for a model class**, plus numbers an operator can weigh.
- **Nothing about candour over time.** "Say plainly what is unverified" is the half of the
  section I most believe in, and a single graded turn cannot see whether a habit holds.
- **Nothing about the next reader.** A design note's value is largely to whoever arrives in
  six months. No A/B of this shape reaches that.

## Procedure

```sh
export JC_DEV_KEY=...                       # never on a command line
python3 tests/bench/craft_ab/craft_ab.py run   --pairs 3
python3 tests/bench/craft_ab/craft_ab.py blind --label <label>
#   ... read tasks/<id>/prompt.md frontmatter, then grade the pack ...
python3 tests/bench/craft_ab/craft_ab.py score --label <label>
```

Measured cost of one pilot pair on `claude-opus-4-5`: **~600k input, ~9k output tokens** for
the two runs together. A full 3×3 session is roughly **5–6M input tokens** — worth deciding
deliberately before starting, and worth pointing `--pairs 1` at a single task first.

The durable record of a graded session is a dated note in `docs/analysis/`; the raw pack and
the sealed mapping are git-ignored.
