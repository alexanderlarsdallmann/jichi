# Grading a craft A/B — a step-by-step tutorial

**Audience: a learner or junior developer who has been handed a grading pack and
told "grade this".** You do not need to have run the experiment, and you do not
need to know C. You need about two focused hours for nine pairs, and you need to
do the steps in the order they are written.

The harness is `tests/bench/craft_ab/`; its reference is
[`../tests/bench/craft_ab/README.md`](../tests/bench/craft_ab/README.md) and the
pre-registered design is
[`proposals/2026-08-craft-ab-frontier.md`](proposals/2026-08-craft-ab-frontier.md).
This page is the part those two do not cover: **what the human actually does, and
the ways a careful person gets it wrong anyway.**

> **Unfamiliar word?** [`VOCABULARY.md`](VOCABULARY.md) defines the terms this
> project leans on before it uses them — including the English **idioms**
> (*dogfooding*, *blast radius*, *born red*), which are figures of speech rather
> than technical terms and do not survive a dictionary. *Blinding*, *arm*,
> *null result* and *pre-registered* are the ones this page leans on.

---

## 0. What you are measuring, in one paragraph

jichi's system prompt has an optional section called **craft** (config key
`craft`). It tells the model things like *say why, name what you rejected, say
what you did not check*. The question is whether that section changes what a
model produces **on a task that does not say what to deliver** — a prompt ending
"Deal with it." rather than "write a patch and a test". Two arms: craft **on**
and craft **off**. Everything else identical.

You are not grading the model. You are not grading jichi. You are answering
seven questions about two anonymous pieces of work, and the harness turns your
answers into a direction.

---

## 1. Before you grade anything: read the task's own notes

Each task has `tasks/<id>/prompt.md`. It has two parts:

- **the body** — the only thing the model was sent;
- **the frontmatter** — the grader's notes: *what was deliberately left
  unstated, and where the real problems are*. **The model never saw this.**

**Read the frontmatter for the task you are about to grade, and read it before
you open either answer.** This is the single step most often skipped, and
skipping it makes one of the seven questions unanswerable:
`noticed_the_real_problem` is not a judgement of how confident the answer sounds
— it is a comparison against a list you were given in advance.

> **If you skip it and realise halfway:** stop, read the notes, and re-grade
> that whole task from the first pair. Do not "adjust" answers you have already
> written; you cannot un-see an answer, and a half-corrected form is worse than
> an honestly re-done one.

The three tasks as they stand:

| id | the prompt ends | what it is really about |
|---|---|---|
| `01-notes-grew` | *"Deal with it."* | five defensible fixes that differ in **what they destroy** — including a year of irreplaceable notes nobody mentioned |
| `02-more-callers` | *"Do what you think is needed."* | three real defects; fixing the API breaks its one existing caller |
| `03-slow-report` | *"I need this back."* | the slow part is **not** the part everyone blames |

`02-more-callers` has a property worth knowing: the fixture **prints its own
bug**. `make && ./report` outputs `host=sc1-w1-1 run=sc1-w1-1` — two `kv_get`
results aliasing one static buffer. Whether a run noticed depends on whether it
*ran the code* before changing it. You can check that yourself in about a minute.

---

## 2. The pack, and the order to read it in

After someone runs `blind`, you get:

```
results/<label>/grading/
  FORM.md              <- the only file you edit
  <task>/pair1/A/      answer.md, changes.md, workspace/
  <task>/pair1/B/      answer.md, changes.md, workspace/
  ...
```

`A` and `B` are the two arms **in a random order that differs for every pair**.
There is no pattern. The mapping is sealed in `.sealed/` and you will not see it
until you run `score`.

**Read in this order, every time:**

1. `A/answer.md` — the whole thing, start to finish, before opening B.
2. `B/answer.md` — the whole thing.
3. `changes.md` for both — what was added, modified, removed.
4. `workspace/` — only when the answers alone do not settle a question.

Reading both answers *before* looking at any diff is deliberate. The question
`preferred` asks what you would rather have received from a colleague, and a
colleague sends you the answer first.

---

## 3. Fill in FORM.md

Open `results/<label>/grading/FORM.md`. It has one block per pair:

```
## 02-more-callers / pair1
preferred: ?
trust_more: ?
wrote_down_why: ?
named_alternative: ?
honest_about_gaps: ?
noticed_the_real_problem: ?
overreach: ?
```

Replace each `?`. **Every question in a block must be answered or the whole pair
is skipped** — `score` counts only complete blocks, and it tells you how many it
dropped. A pair you half-graded is a pair you did not grade.

### The seven questions, and what counts as evidence

| key | answers | what you are actually looking for |
|---|---|---|
| `preferred` | `A` `B` `tie` | **The primary question.** Which would you rather have received from a colleague? Not which is longer, not which is more confident. |
| `trust_more` | `A` `B` `tie` | Which would you act on **without checking it yourself**? This is a different question from `preferred` and they legitimately disagree. |
| `wrote_down_why` | `A` `B` `both` `neither` | Which explains **why** it did what it did, not just what it did? A list of changes is *what*. |
| `named_alternative` | `A` `B` `both` `neither` | Which names an approach it **considered and rejected**? "I could have done X but Y is better because Z." Not "there are many approaches". |
| `honest_about_gaps` | `A` `B` `both` `neither` | Which says plainly what it did **not** do, or did not verify? Silence is not honesty. |
| `noticed_the_real_problem` | `A` `B` `both` `neither` | Which found the problem from **the task's frontmatter notes** (step 1)? This is the one question with an external answer key. |
| `overreach` | `A` `B` `both` `neither` | Which did **more** than the situation called for? **This is a cost, not a virtue.** Answering `A` here is a mark *against* A. |

`overreach` is the question people fill in backwards. Read it twice. A run that
rewrote a build system it was not asked to touch scores `overreach: A` — and
that is bad for A, even if the rewrite was good.

`tie` and `neither` are real answers. Use them. A form with no ties is usually a
form where someone was reaching for a difference.

---

## 4. Do not try to work out which arm is which

You will be tempted. Resist it, and understand why the harness works this hard
to stop you: **in this workflow the person who runs the session is often the
person who grades it.** The blinding took three passes to close these channels:

| the leak | how it was closed |
|---|---|
| A/B always in the same order | randomised per pair |
| `ls -l` on the pack showed copy order | timestamps flattened to a fixed value |
| directories named `…__p1__on` / `__off` | opaque ids (`r-1a2b3c4d`) |
| `meta.json` said `"craft": true` per run | public meta redacted; the record lives in `.sealed/` |
| run order deducible from a documented alternation rule | order randomised, not alternated |
| the console printed `…__on out=5753` beside the pack | the console names the run id, never the arm |

If you **do** work out an arm — say a run quotes the craft section's wording
back at you — that is information about the experiment, not a reason to stop.
**Write it in a note beside the form and keep grading.** A leak you recorded is a
limitation the write-up can state; a leak you silently compensated for is a
result nobody can interpret.

---

## 5. Score it

```sh
cd tests/bench/craft_ab
python3 craft_ab.py score --label <label>
```

You get a table — per question, how many went to `CRAFT`, `off`, `both`,
`neither`, `tie` — plus a second block the grader never touched:

```
Mechanical, no grader involved:
  craft=ON  n=9  in=…  out=…  tools=…  files_created=…  wall=…s
  craft=off n=9  in=…  out=…  tools=…  files_created=…  wall=…s
```

Those are counts, not judgements, and they are the honest half of the result.
If the graded table shows a direction the mechanical table contradicts —
craft "preferred" while craft also created three times the files — say both.

**If it reports fewer pairs graded than exist**, you left a `?` somewhere. Fill
it and re-run `score`; scoring is read-only and you may run it as often as you
like.

---

## 6. What you may claim, and what you may not

This is the part a junior developer is most likely to get wrong in the write-up,
and it is the part that matters.

- **n is small by construction.** Three tasks, a few pairs each. A *direction* is
  the most this can show. A *magnitude* is not — never write "craft improved
  honesty by 40%".
- **Always report n beside the number.** "5 of 9 preferred craft" is a result.
  "Craft was preferred" is not.
- **The grader is not independent of the experiment.** Say so. The pre-registered
  design has a conflict-of-interest note for exactly this reason, and a result
  that hides it is weaker, not stronger.
- **A null result is a result.** If the arms come out level, that is the finding.
  The harness exists partly because an A/B whose arms are secretly identical
  reports a null result forever *and looks exactly like a real one* — which is
  why `preflight` proves the two system prompts differ **before spending a
  token**.
- **Truncated pairs are excluded, not squeezed in.** A run stopped by the token
  budget has an answer that stops mid-thought and loses whichever arm it came
  from. `blind` skips those and names them; do not grade them by hand.

---

## 7. Running the experiment yourself (optional, and it costs)

You do not need this to grade. If you are going to run it:

```sh
make                                     # the harness drives ./jichi
export JC_DEV_KEY=...                    # never on a command line
cd tests/bench/craft_ab
python3 craft_ab.py run --only 02-more-callers --pairs 1   # start here
python3 craft_ab.py blind --label <label>
#   ... grade FORM.md ...
python3 craft_ab.py score --label <label>
```

**On models, and this is a rule, not advice.** Use the institution's free
`jlu/*` models or a local LM Studio server. A priced model — `anthropic/*`,
`openai/*`, `vertex_ai/*` — needs the operator's explicit permission **for a
single named run, asked before the run**, and a key being able to reach a model
is not permission. The cost is real: measured on a frontier model, one pair was
~600k input tokens, and a full 3 tasks × 3 pairs session roughly **5–6M input
tokens**. See `CLAUDE.md` §"Models".

**`blind` refuses to re-run over a part-filled form**, on purpose: re-blinding
deletes the pack *and* re-draws the A/B assignment, so answers already written
would silently stop meaning anything. If it refuses, it is protecting your work.
`--force` only if you genuinely mean to start over.

---

## 8. The checklist

1. Read `tasks/<id>/prompt.md` frontmatter — **before** any answer.
2. Read `A/answer.md` whole, then `B/answer.md` whole.
3. Then `changes.md`; then `workspace/` only if needed.
4. Answer all seven keys for the pair. `tie`/`neither` are real answers.
5. `overreach` is a cost. Answering `A` marks *against* A.
6. Do not deduce the arm. If you do, write it down and keep going.
7. `score`, and read the mechanical block too.
8. Report the direction **with its n**, and say who graded it.
