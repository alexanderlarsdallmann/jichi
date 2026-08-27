# `tests/bench/` — the fixed dogfood prompt suite

The small-model bench corpus. `docs/DEFERRED_LOCAL_GPU.md` referred to "the fixed
dogfood prompt suite" as the thing every measurement is taken against; this
directory *is* that suite. It is deliberately separate from the offline
`tests/e2e/` gate: it needs a live model, it measures rather than asserts, and it
must never gate `make ci`.

Full procedure, hardware notes and interpretation: **`docs/BENCH_LOCAL_GPU.md`**.
Measured results from the first session: **`docs/analysis/2026-07-27-local-gpu-bench.md`**.

## Quick start

```sh
make                                   # the bench drives ./jichi
# LM Studio (or llama.cpp / Ollama) serving an OpenAI-compatible endpoint on :1234
python3 tests/bench/run_bench.py --profile core
python3 tests/bench/report.py
```

## Layout

| Path | What it is |
|---|---|
| `config.bench.json` | the fixed bench config: one small model, no routing, no fallback, so a number attributes to exactly one model |
| `corpus/<id>/spec.md` | one task: `jc_assign` frontmatter (`title`, `audience`, `verify`, `points`) + the prompt as the body |
| `corpus/<id>/files/` | that task's fixture tree, copied fresh into a throwaway workspace per run |
| `run_bench.py` | runs every task as one bounded headless `--auto` turn, then grades it with the spec's own `verify` |
| `report.py` | turns the collected telemetry into the §4 measurement-plan table |
| `check_graders.py` | proves every grader is two-sided, using the runner's own spec parser |
| `schema_probe.py` | replays a captured request body while varying the advertised tool array (count / compact / drop-one) |
| `version_probe.py` | the language-version probe (docs/CHOOSING_A_MODEL.md §4): self-report + compile-judged behaviour probes, context-free; prints a MODEL_KNOWLEDGE-style record block; `--mode self-test` proves the compile gate two-sided offline |
| `cache_probe.py` | the prompt-cache latency probe (docs/PROMPT_CACHING.md "cached=0 is not proof"): times a cold call vs warm repeats of a byte-identical prefix; can prove a cache live, never absent; `--mode self-test` covers the verdict function |
| `results/<label>/` | generated: per-task JSON, per-task telemetry + journal, `summary.json` |
| `craft_ab/` | the craft A/B: blinded pairwise grading of runs with and without the `craft` prompt section (its own README) |
| `mentor_ab/` | the mentor A/B (M605): the `/learn` mentor under two jichi binaries, one draft per arm per workspace, blinded for a grader who did not write the mentor (its own README) |

The spec format is `jc_assign`'s on purpose, so `jichi assign
corpus/03-edit-single/spec.md` renders a task and `grade` scores it with the same
`verify` the bench uses — one format, three consumers.

## The tasks

| id | exercises | points |
|---|---|---|
| `01-read-report` | `read_file` → `write_file` | 1 |
| `02-list-search` | `list_files` / `search_code` to locate a symbol | 1 |
| `03-edit-single` | one exact `edit_file` replacement | 1 |
| `04-edit-multi` | a constant raised consistently across two files | 2 |
| `05-write-new` | `write_file` a C89 header from a description | 1 |
| `06-run-command` | `run_terminal_command`, and trusting its output over a guess | 1 |
| `07-fix-failing-test` | the compound loop: run → read failure → fix → re-run | 3 |
| `08-refactor-rename` | rename across three files, no stale occurrence left | 3 |
| `09-ambiguous-edit` | one of two identical lines — a naive `old_string` is ambiguous | 2 |
| `10-search-then-edit` | find a constant nobody named, rename it in sources only | 3 |
| `11-recover-from-error` | the obvious path does not exist; adapt instead of guessing | 3 |

11 tasks, 21 points total (counted by `tests/smoke/docs_counts_lint.sh`, so
this line cannot rot the way the old "eight tasks" heading did). Tasks
03/04/08/09 are the stale-`old_string` generators.

Tasks 09–11 exist because the original eight all passed: a bench that always
scores full marks has stopped measuring. `09` is the best discriminator in the
set — the reference model passes it about **half** the time, and its failures are
legible (an ambiguous match refused with an actionable error, a literal `\n` in an
`old_string`, then recovery).

## Invariants worth keeping

**Every grader is two-sided.** A `verify` that cannot fail is the hollow gate of
ANECDOTES #17; one that cannot *pass* is worse, because it reports a working model
as broken. Run `python3 tests/bench/check_graders.py` — it uses
`run_bench.parse_spec`, the same parser the runner uses, so a YAML-escaping
difference cannot make a grader look fine while it is broken. That is not
hypothetical: task 09's first grader was validated by an ad-hoc snippet that
unescaped differently, and it then scored five correct edits as failures.
**Adding a task means adding its reference solution to `solve()` in
`check_graders.py`.**

**The corpus is fixed.** Numbers are comparable across sessions and machines only
if the input never moves. Add tasks; do not quietly reword existing ones. Task
`07`'s wording carries a comment explaining a phrasing that must not be
"simplified" — it would re-trigger the M110 constraint false positive that
session one found.

**A throwaway `HOME` per task.** Telemetry, the M77 calibration ratio, snapshots
and run journals all live under `$HOME/.jichi.d`. Isolating `HOME` keeps
the bench out of the operator's real state and stops task *N* inheriting task
*N-1*'s calibration.

**Bounded.** Every task runs under the autonomy envelope's token / wall-clock /
tool-call caps, so a wedged model costs one timeout rather than the session.

## The version probe

`version_probe.py` automates the mechanical two-thirds of the
`docs/CHOOSING_A_MODEL.md` §4 ritual: the self-report question and the
behaviour probe whose arbiter is the locally installed compiler. It sends
bare, context-free requests (isolating the variable being measured — the
incident behind this rule was in fact a `-p` argument-parsing bug, fixed
and smoke-pinned in M375; ANECDOTES #50), and prints a dated
record block to stdout for the operator to review and paste — never writes
a file, because the record is the operator's claim, not the probe's.

```sh
python3 tests/bench/version_probe.py --mode self-test   # offline; no model
python3 tests/bench/version_probe.py \
    --url http://127.0.0.1:1234/v1/chat/completions \
    --model MODEL_ID --lang zig --lang c89
```

Two invariants it keeps: the probe can **fail** a model, never certify one
(an all-pass verdict is worded as "not refuted — weak evidence"); and the
compile gate is proven **two-sided** before being trusted (`--mode
self-test` requires a canned old-dialect source to be refused and a canned
current-idiom source to compile — pointing the compilers at `/bin/true`
makes it fail, which was demonstrated before first use). `c89` is the
control language: it has not changed since 1990, so a model failing it
indicts the probe, not the model. Compiler discovery: `zig`/`cc` on
`$PATH`, overridable via `$VP_ZIG`/`$VP_CC`.

## Generated files

`results/`, `body.json` and `*.log` are outputs, not sources — they are
git-ignored. Commit the numbers by writing them into a dated
`docs/analysis/` note, which is the durable record.
