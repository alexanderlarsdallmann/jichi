# Does the craft section earn its tokens on the model actually in service? (pre-registration)

*Written 2026-08-22 (M545), **before any run of this arm**. Third registration of this
A/B, and it asks a question neither predecessor asked. Read the honesty section first
if you read nothing else.*

## Why a third registration, and not a re-run of either predecessor

| | Model | Verdict |
|---|---|---|
| **M318** ([analysis](../analysis/2026-08-06-craft-ab.md)) | `jlu/gemma-4-31b-it` (31B, 32k) | *"no measurable benefit on this model; default now off under `--lite`."* |
| **M326g** ([proposal](2026-08-craft-ab-frontier.md)) | `anthropic/claude-opus-4-5` | **Ran 2026-08-10, 18/18 done, ~3.73M input tokens — and the blinded pack was lost before it was graded.** `results/` is git-ignored; the directory exists on no machine. No result was produced. |
| **this one** | `jlu/qwen3-coder-next` | — |

M318's own "what would change my mind" was *the same A/B on a frontier model*, because
**the craft section's claimed value is about larger models**. That arm was paid for and
produced nothing, and the operator's standing rule is local and free models only, so it
is not being re-run: reopening it now costs money again.

**So this is not that experiment, and it must not be reported as if it were.** It asks
the question that is actually actionable:

> `src/config/jc_config.c:875` — `out->craft = lite ? 0 : 1;`
>
> The section ships **on** for every non-`--lite` model. `jlu/qwen3-coder-next` is one,
> it is the model this project uses in daily service, and **nothing has ever tested the
> section there.**

A default that applies to a model nobody measured is the same species of claim as the
four M536 fixed: something the project asserts about itself on no evidence.

## The hypothesis, stated so it can fail

> On `jlu/qwen3-coder-next`, given a task that names a problem and **no artifact**, the
> craft section produces work a human would rather receive — and the difference is
> visible to a grader who was not told what to look for.

**If the primary measure comes out flat or negative, the recommendation changes.** Not
to "inconclusive, keep the default": to *the default rests on nothing measured for this
model class, and should be reconsidered for it.* M318 already moved the default for
`--lite`; a null here is evidence for moving it further, and will be recorded that way.

**If it comes out positive**, the honest scope is exactly one model. It says nothing
about a frontier model, which remains unmeasured and now expensive to measure.

## What is fixed before the run

- **Model:** `jlu/qwen3-coder-next` on the HRZ gateway. Free (`input_cost_per_token: 0`),
  which is why this arm can run at all.
- **Tasks:** the three existing ones, unchanged — `01-notes-grew`, `02-more-callers`,
  `03-slow-report`. Each names a situation and **no deliverable**; none asks for a design
  note, a test or an explanation, and that is the entire point. Changing them would make
  this incomparable to M318 as well as to the frontier arm.
- **Pairs:** 3 per task = **9 pairs, 18 runs**.
- **Arms:** craft on / craft off, the same `--pairs` harness, order randomised per pair.
- **Primary measure:** the operator's blind pairwise preference across the 7 form
  questions. **Secondary, mechanical:** input/output tokens, model calls, tool calls,
  files touched, wall clock.
- **Grader:** the operator. **Not** Claude — Claude wrote the section under test, the
  tasks, and the questions, and the author of a thing is the last whose unblinded
  judgement of it is worth having.
- **Decision rule:** a direction, not significance. n = 9 by construction.

## Amendment 1, 2026-08-22: the effort envelope, calibrated (`service-01`)

**Registered before `service-02` ran; `service-01` is the measurement that motivated
it and is kept.**

The harness's defaults are `--budget-tokens 500000 --max-tool-calls 30`, and the help
text states their basis: *"~14k per tool call was measured on **Opus 4.5**, so 500k ~=
35 calls."* That is a frontier-model calibration, and this arm is not a frontier model.

`service-01` was launched on the defaults and stopped after 9 of 18 runs. What it
measured:

| outcome | tool calls | files touched |
|---|---|---|
| **done** (5) | 17, 17, 19, 20, 23 | 3-5 |
| **budget** (3) | 26, 30, 30 | 0-8 |

**Completed runs finish in 17-23 tool calls; every cut run wanted 26 or more**, and
one reached exactly 30 -- the `--max-tool-calls` fence. So the two caps bind at the
same place, and **raising the budget alone would not help**: those runs need more
*calls*, not only more tokens. `blind` discards any pair whose either arm is
incomplete -- correctly, since "a truncated answer loses to a complete one on every
question" -- so on the defaults roughly a third of runs, and therefore more than a
third of pairs, would be thrown away.

**The finding, worth more than the fix:** a smaller model needs more steps for the
same task, so an effort envelope sized against Opus 4.5 truncates it. The tasks are
unchanged and comparable; what was wrong was the room they were given.

**Amended, from the measurement:** `--max-tool-calls 45` and
`--budget-tokens 1500000` (45 calls x ~24k input/call, measured independently on this
model class on 2026-08-22, plus headroom). The intent is that **neither cap binds** on
a task the model can finish, so an incomplete run means the model gave up rather than
that the harness cut it off.

**What this does not change:** the model, the three tasks, three pairs per task, the
arms, the measures, the grader, or the decision rule. The registration never fixed the
effort envelope, and the harness's own comment says a budget-cut run *"is not a
sample"* -- so removing the cut is in the registration's spirit. Recorded here rather
than adjusted quietly, because a cap changed after seeing data is exactly the kind of
thing a pre-registration exists to make visible.

**Also observed, and left alone:** one `service-01` run hung for ~13 minutes on a
`run_terminal_command` child before its 15-minute deadline could fire. The fixtures
include a program the model builds and runs; one of them can block. The deadline
caught it, which is what a deadline is for, and the run counts as incomplete. Not
amended, but named so a future session recognises it.

## Amendment 2, 2026-08-22: what `service-02` yielded, and why n is 6 and not 9

**Run completed: 18/18, `RUN_RC=0`, 28 minutes on `jlu/qwen3-coder-next`.** Preflight
confirmed the arms differ by **+1316 bytes of system prompt** -- the same delta the
lost frontier session reported, so the manipulation is identical.

Outcomes: **15 `done`, 2 `budget`, 1 with no terminal event.** The tool-call
distribution across all 18 runs:

```
3 4 4 7 9 9 13 15 25 27 27 28 30 32 38 39 45 45
```

Bimodal, and worth noting for anyone sizing a future envelope: `03-slow-report` is
answered in 3-9 calls while `01-notes-grew` and `02-more-callers` take 25-45. The two
runs at exactly **45** hit the amended fence. The old default of 30 would have
truncated **five** runs (32, 38, 39, 45, 45), not the three it truncated in
`service-01`.

**`blind` discarded 3 of 9 pairs**, all on the effort side:

| pair | failing arm |
|---|---|
| `01-notes-grew/pair1` | craft-on: budget (the 45-call fence) |
| `02-more-callers/pair1` | craft-on: no terminal event |
| `02-more-callers/pair3` | craft-off: budget |

**So n = 6, and it stays 6.** Re-running the three skipped combinations under a higher
fence was considered and rejected: it would put two different effort envelopes inside
one experiment, and **a mixed envelope is a worse confound than a smaller n.** The
registration's decision rule was already "a direction, not significance"; six pairs
supports that as four would not, and nine would not have made it significant either.

**A confound to state rather than resolve:** two of the three discarded arms were
craft-on. With n = 3 that is not evidence, but the mechanism is plausible in one
direction -- a section that asks for more considered work could make a model take more
steps -- and if it were real it would systematically discard craft-on runs. Recorded so
the grader knows it, and because a future session with more pairs could test it
directly. It does not touch the blind: the discarded pairs are not in the pack.

**Preserved.** `~/development/journey/craft-service-02.tgz`, 383 KB, containing
`grading/FORM.md` **and** `.sealed/mapping.json` -- the session is scoreable from the
archive alone, which is the thing `session-02` was not.

## What will not be concluded

- Nothing about frontier models. That arm is unmeasured.
- Nothing about `--lite`; M318 settled that and the default already reflects it.
- No significance claim. Nine pairs on a nondeterministic model supports a direction and
  a recommendation, and no p-value.
- Nothing about other `jlu/*` models. The finding is about one model, as M318's was.

## How this could still mislead

- **n is small and the model is nondeterministic.** Nine pairs. A single vivid pair can
  move a human grader's overall impression more than it should; the form asks per-pair
  questions to blunt that, not to remove it.
- **The grader ran the session.** The blind therefore has to survive the operator looking
  around their own results directory — which is why the condition is written in exactly
  one place (`.sealed/`) and nowhere in the run directories, `meta.json`, the run order,
  the file timestamps, or the progress line.
- **A coder-specialised model on prose-shaped tasks.** `qwen3-coder-next` is tuned for
  code; the craft section is about *what to produce when nobody said*. That may suit it
  worse or better than a general model, and either way it is a property of this model and
  not of the section.
- **The section's own author chose these tasks.** Inherited from M326g deliberately, so
  the arms remain comparable — but it is a real limit, not a neutral one.

## Procedure, and the one thing added since M326g

```sh
export JC_DEV_KEY=$(cat /path/to/keyfile)          # never on a command line
python3 craft_ab.py run   --pairs 3 --label service-01
python3 craft_ab.py blind --label service-01
tar czf craft-service-01.tgz -C results/service-01 .    # M545: DO THIS, whole dir
#   ... operator fills results/service-01/grading/FORM.md ...
python3 craft_ab.py score --label service-01
```

**The `tar` line is not optional, and it is why M545 exists.** The frontier arm's pack
was built and then lost because `results/` is git-ignored and the grading — the slow
step, and the only one a machine cannot do — outlived the directory. `blind` now prints
that warning itself.

**Archive the WHOLE label directory, `.sealed/` included.** `score` reads
`.sealed/mapping.json` to attribute A and B to arms, so a tarball of `grading/` alone
can be filled in and never scored. The blind is kept by **not opening** `.sealed/` --
which is exactly what the sealed layout is for, per the harness's own docstring: the
condition lives in one place *"so `results/<label>/` can be browsed without spoiling a
grading in progress."* Deleting the mapping does not protect the blind; it destroys the
result.

*The first draft of this section advised archiving `grading/` alone -- the same defect
this milestone is about, committed inside the fix for it. And the edit that corrected
it silently did not apply, because the script asserted on one replacement and not the
other; that is [the runbook](../TESTING_RUNBOOK.md)'s step 3 in miniature -- an
unasserted extraction reports success for work it did not do.*

## Where this fits

- [`analysis/2026-08-06-craft-ab.md`](../analysis/2026-08-06-craft-ab.md) — M318, the 31B arm.
- [`proposals/2026-08-craft-ab-frontier.md`](2026-08-craft-ab-frontier.md) — the frontier
  registration, whose run was paid for and lost.
- [`DEFERRED.md`](../DEFERRED.md) — the row that named the lost pack as "exactly one
  thing remaining" for twelve days, now corrected.
- `tests/bench/craft_ab/README.md` — the harness, the tasks, and the blinding design.
