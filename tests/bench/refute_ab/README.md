# `tests/bench/refute_ab/` — the refute A/B

Does a `refute` workflow stage — a frame the spec author cannot weaken: three
forced headings, "nothing found" permitted, read-only — find planted false
claims that a `synthesize` stage told to "review critically" does not?

Design, hypothesis, planted-claim rules and what will not be concluded were
fixed before any run: **[`docs/proposals/2026-09-refute-ab.md`](../../../docs/proposals/2026-09-refute-ab.md)**.
The stage itself is M634. This harness is a **measurement, not a gate**: it
needs a live model and must never run in `make ci`. Free models only
(`CLAUDE.md`): the model id is pinned in the script and `priced_model_lint`
holds it.

## Quick start

```sh
make                                         # the harness drives ./jichi
. ~/.jichi.env                               # the key, never on a command line

python3 refute_ab.py reports                 # 12 first-seat reports (wall-clock)
#   ... plant ONE false claim in each report by hand; list them in planted.tsv ...
python3 refute_ab.py run   --label <label>   # 24 runs (wall-clock)
python3 refute_ab.py blind --label <label>   # the blinded grading pack
#   ... fill in results/<label>/grading/FORM.md, one row per arm ...
python3 refute_ab.py score --label <label>
```

## Layout

| Path | What it is |
|---|---|
| `files.txt` | the twelve `src/util/*.c` files reviewed by the first seat |
| `reports/` | generated once by `reports`, then **edited by hand** to plant one false claim each; never overwritten without `--force` |
| `planted.tsv` | `report-file<TAB>subject<TAB>what was planted`, written by hand **before** `run`; the subject is for the grader's eyes only |
| `refute_ab.py` | `reports` / `run` / `blind` / `score` |
| `results/<label>/` | generated, git-ignored: `runs/<opaque id>/answer.md`, the grading pack, and `.sealed/` |
| `results/<label>/.sealed/` | the only place the condition is written: `runs.json` (which id was which arm) and `mapping.json` (which of A/B) |

## What the harness does, and what it does not

**Two arms, one report each.** The refute arm is a spec with one `refute`
stage whose `prompt` is the report; the stage's fixed frame is the system
message and the report arrives as "the claim under review". The control arm is
one `synthesize` stage whose prompt is `Review the following critically and
list anything wrong:` followed by the same report. Same config, same model.

**Opaque ids, sealed condition.** Run directories are `r-<8 hex>`; the spec file
that names the stage type is removed from the run directory once the run is
over; the arm appears only in `.sealed/runs.json`. `blind` copies the two
answers per report to `A.md` and `B.md` in an order drawn per report and
flattens every mtime. The grader's form has one entry **per arm** — a hit and a
false-attack count for A and for B — because the question is which arm named
the planted claim, and one answer per report cannot say.

**A person scores; the script counts.** `score` reads only the filled form and
the sealed mapping. It never opens an answer file: a substring grep for the
planted subject would count a run that mentioned the function while agreeing
with the false claim, which is the opposite of a hit.

**What it does not do, said plainly.** It does not preflight that the two arms
differ — the difference is the stage type, checked by `workflow_refute.sh`, not
by a prompt diff this script could take. It does not refuse truncated answers;
there is no token budget to truncate them (`--deadline` is headroom), and an
answer cut by the deadline is recorded with its `rc` and is the grader's to
judge. And it cannot make the grader blind to their own planting: the same
person plants and grades, which the pre-registration names as the main way this
can mislead, and which the form's per-arm structure and the sealed mapping
reduce but do not remove.

## Cost

Wall-clock only. On the HRZ gateway `jlu/qwen3-coder-next` takes one to several
minutes per report review and per arm run; a full session (12 reports, 24 runs)
is an hour-class job. There is no token budget, on purpose.
