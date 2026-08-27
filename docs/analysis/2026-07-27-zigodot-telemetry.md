# Driving jichi on zigodot to refine jichi — telemetry session, 2026-07-27

**Purpose:** run jichi against a real, hard workload and mine the logs for defects in
jichi itself. The zigodot program (a bottom-up Godot→Zig rewrite) is the workload; it
was *not* advanced here, and its tree is unchanged at `e947840` with the gate green
at 239/239.

**Sources:** 30 MB of historical dogfood telemetry
(`~/.jichi.d/telemetry/zigodot-full.jsonl`, 137 turns / 4 154 model calls /
254 M input tokens, July) plus one fresh bounded `--auto` drive against
`jlu/qwen3-coder-next` (29 model calls, 20 tool calls, 1.56 M tokens).

**Models exercised:** `jlu/qwen3-coder-next`, `jlu/gemma-4-26b-it`,
`jlu/gemma-4-31b-it` (HRZ), `google/gemma-4-e4b` (local LM Studio).

Three defects found and fixed (M168); two confirmed-already-fixed; one operator
config problem to hand back.

## Summary of findings

| # | Finding | Status |
|---|---|---|
| 1 | A dead fallback chain in the **global** config that `models` calls "reachable" | **hand back to the operator** |
| 2 | `nudge` / `args_repair` never fire for a competent coder model | no action — mechanisms are small-model-specific |
| 3 | Tool ok-rate conflates a red gate with a broken tool | **fixed (M168a)** |
| 4 | A stringified numeric argument silently selects the most expensive default | **fixed (M168b)** |
| 5 | A *descriptive* "read-only" adopts a global read-only constraint | **fixed (M168c)** |
| 6 | Cacheless re-billing quantified: 85 % of input tokens are re-sent prefix | no action — `--cache-audit` already reports it |
| 7 | Hallucinated tool names (`grep`, `glob`) in the July log | already fixed by M91 semantic aliases |

## 1. `doctor --live` earned itself on its first real outing

Run across four models on the day M167c shipped:

```
jlu/qwen3-coder-next   ✓ native  (probe prefix 316 real prompt tokens)
jlu/gemma-4-26b-it     ✓ native  (106)
google/gemma-4-e4b     ✓ native  (104)
jlu/gemma-4-31b-it     ✗ probe failed — HTTP error
```

`models` reports that last one **reachable**, because reachability probes
`GET /models`, not the model. The actual request 400s:

```
litellm.BadRequestError: You passed in model=hosted_vllm/gemma-4-31b-it.
There are no healthy deployments for this model.
```

The **global** config `~/.jichi` carries three entries with a stale
`hosted_vllm/` prefix (`jlu/gemma-4-31b-it`, `jlu/qwen3-coder-next`,
`jlu/gemma-fallback`); the HRZ proxy serves the `jichi/`-prefixed ids. zigodot's
project config overlays `jlu/qwen3-coder-next` by name, which is why the active
model works — but `local-gemma`'s `fallback` resolves to `jlu/gemma-4-31b-it`,
i.e. **to a model that cannot serve a request**. If LM Studio goes down, the
fallback fails too.

**For the operator:** fix the three model ids in `~/.jichi` (drop the
`hosted_vllm/` prefix). Left alone deliberately — it is outside the repo and
outside this session's remit; the drives here pinned models with `--model` and
`--no-route` so a dead fallback could not contaminate the measurements.

**For jichi:** this is the argument for `doctor --live` in one screenshot. Nothing
else in the toolchain distinguishes "the server answers" from "the model works".

## 2. The nudge and repair counters are structurally silent here

Across 137 turns and 4 154 model calls against `qwen3-coder-next`, the new M167e
`Self-correction:` block reports **nothing**: zero `nudge` fires, zero
`args_repair` attempts. That is not a reader bug (it was verified against a
synthetic log and a live run); it is the honest answer.

M147's prose-call nudge and M148's argument repair are **small-model mechanisms**.
A competent coder model emits native tool calls with parseable arguments, so the
two rows of the small-model measurement plan they feed are `n/a` for this
population — exactly as the bench predicted for `gemma-4-e4b`. Worth stating
plainly so nobody reads "0 / 0" as broken instrumentation.

## 3. M168a — a red gate is not a broken tool

The raw per-tool report looked alarming:

```
run_terminal_command  calls=1278 ok=1065/1278 (83%)
run_tests             calls=309  ok=228/309  (73%)
```

Classifying every failure by its output text:

| Tool | red build/test (expected) | genuine tool failure |
|---|---|---|
| `run_terminal_command` | 126 | 87 |
| `run_tests` | 73 | 8 |

**199 of 294 apparent failures were red gates** — the agent running the gate,
seeing it fail, and fixing it. That is the fix-forward loop working. The cause is
`jc_tool_run.c`: `out->is_error = (status != 0)`, so a perfectly working tool that
ran a red build is recorded identically to a tool that could not run at all.

Fixed by carrying the command's own `exit` status into the `tool_call` event and
splitting the two in the summarizer:

```
run_tests   calls=309 ok=228/309 (73%) ...
              of which 73 were red commands (non-zero exit), not tool failures
              -> tool-level ok=301/309 (97%)
```

Judge the **tool** by the second line and the **work** by the first. `ok` was
deliberately *not* redefined: that would have silently changed the meaning of the
30 MB this finding came from. Logs without the field render exactly as before
(verified against the historical file).

## 4. M168b — a stringified number silently picked the most expensive default

The highest-value finding, and invisible without full-tier telemetry.

The model repeatedly emitted `{"path": "...", "limit": "200.0"}` — the `limit` as
a **string**. `tu_arg_int` accepted only a JSON number and returned the default
otherwise; `read_file`'s `limit` default is `0`, which means **no limit**. So a
request for 200 lines returned the entire file, silently, with no error and no
warning.

Interleaving the tool calls with the input-token counts of the fresh drive shows
the damage exactly:

```
  model_call in=  22677
      tool read_file .../codegen.zig  <-- returned 172 KB
  model_call in=  62605   <== +39928
      tool read_file .../vm.zig       <-- returned 169 KB
  model_call in= 113551   <== +50946
  ...
      tool read_file .../codegen.zig  <-- returned 172 KB
  model_call in= 127021   <== +56537
      tool read_file .../codegen.zig  <-- returned 172 KB
  model_call in= 130234   <== +52211
```

Five whole-file reads, each adding 40–57 k input tokens, each then **re-billed on
every subsequent call** of a cacheless backend, and driving six history
compactions in a 29-call run. A large share of that run's 1.56 M tokens traces to
one silent type mismatch.

`tu_arg_int` now accepts a numeric string (`"200"`, `"200.0"`, `" -3 "`), and
`tu_arg_bool` the unambiguous boolean spellings. Prose (`"200 lines"`) still falls
through to the default rather than being guessed at — guessing at prose is the
same class of error this fix exists to prevent. Tested through the real
`read_file` dispatch, not just the accessor.

**The generalisable rule: a type mismatch must never silently select the most
expensive behaviour available.** `limit=0 means unlimited` is a perfectly
reasonable default until it becomes the *fallback* for a parse failure.

## 5. M168c — a descriptive "read-only" silently disabled the whole drive

The fresh drive read 20 files over 20 minutes and 1.56 M tokens, then produced an
analysis and **no edits at all**, despite `--auto`. It would have been natural to
conclude the model was lazy or the task too hard.

The real cause was in my own brief:

```
Oracle files (read-only, outside the edit scope):
```

`jc_constraint_scan` treated any occurrence of "read-only" as an instruction and
adopted a global `read-only` constraint — forbidding every mutating tool for the
entire run, and persisting it to `.jichi/constraints.md` for every later run in that
directory. The phrase was *describing the oracle inputs*.

**M167d, shipped hours earlier, is what made this diagnosable.** The adoption
notice — raised that morning from an invisible `JC_LOG_INFO` count to a named
`JC_LOG_WARN` — said it in one line:

```
[jichi warn] [constraint] adopted from your request and now enforced:
read-only: do not edit files or make changes -- clear with `/constraints clear`
or by deleting .jichi/constraints.md
```

Before that change this session would have ended with a shrug about model quality.

Fixed: "read-only" now counts only with a stative/imperative cue nearby ("keep
this read-only", "stay read-only", "read-only mode") and not when a determiner or
object noun follows ("read-only files", "read only the header"). The unambiguous
imperatives ("do not edit", "no edits") still need no cue. `"is"` is deliberately
excluded as a cue — "the corpus is mounted read-only" states a fact about someone
else's filesystem. Verified live in both directions.

Note this is the **third** over-eager inference in the same scanner in one day
(M167d's "do not change the test file" → "do not run tests", and now this).
The pattern is consistent: a keyword scanner over natural language will keep
producing these. The durable mitigation is not a better keyword list, it is that
every inferred rule is now announced by name — see the recommendations.

## 6. Cacheless re-billing, quantified

From the fresh drive (29 calls, 1.54 M input tokens):

- `cache_read_in` total: **0** — the HRZ proxy does no prompt caching at all
- input ramp: 7.5 k → 130 k (hitting `contextLimit`, then compaction, then
  climbing again)
- **85 % of all input tokens (1.31 M of 1.54 M) are re-sent prefix**

`telemetry --cache-audit` already reports this correctly and gives actionable
advice (shorter sessions, smaller always-sent prefix, `toolProfile=core`, or a
caching backend). No jichi change needed — but it is the throughput ceiling for this
program, and it is what makes finding #4 so expensive: every wasted token is
re-billed on every later call.

## 7. Confirmed already fixed

The July log shows 18 calls to tools that do not exist (`grep` ×7, `glob` ×6,
`todoedit` ×5), each answered with a bare `error: unknown tool`. Current jichi
answers with an edit-distance suggestion **and** an M91 semantic-alias table that
maps `grep` → `search_code` explicitly. The historical log predates M91; no work
needed. Worth recording so the next reader of that log does not re-open it.

## Recommendations

**1. Fix the three stale model ids in `~/.jichi` (operator, XS).** Drop the
`hosted_vllm/` prefix. Until then, `local-gemma`'s fallback is dead and any
`--auto` run that loses LM Studio fails instead of falling back.

**2. Prefer `doctor --live` over `models` before a long drive (S).** Reachability
is not capability. One request per model, and it is the only check that would have
caught #1.

**3. ~~Treat the constraint scanner's precision as bounded~~ — BUILT as M169.**
Three false positives in one day, all fixed individually, all of the same shape.
Rather than continue tightening keywords, prompt-inferred constraints are now
**session-scoped**: enforced exactly as hard, never written to
`.jichi/constraints.md`, so a misparse costs one turn instead of every future run in
that directory. Authored constraints (the store, `/constraints add`) persist
unchanged. The announcement (M167d) already turned these from mysteries into
one-line diagnoses; this removes their permanence. See ROADMAP M169 and
docs/CONSTRAINTS.md ("Provenance").

**4. Audit the other silent-default paths (S).** #4 was one accessor. Any place
where a malformed input falls through to a default deserves the same question:
*is the default the cheapest or the most expensive behaviour?* `tu_arg_str`
returning NULL is safe (callers error); the numeric and boolean accessors were
not, and are now fixed.

**5. Run drives with `--log-level full` when the goal is diagnosis (XS).** #4 was
only visible because `output_bytes` and `args_full` were recorded. At `metrics`
the five 172 KB reads look like five ordinary successful reads.

**6. A drive brief is an input to the constraint scanner (XS, docs).** Operators
writing `--auto` briefs should avoid "read-only", "do not …", and bare
`test`/`build`/`commit` nouns unless they mean them as instructions. Worth a line
in `docs/AUTONOMOUS_LOOPS.md`.

## Reproducing

```sh
cd zigodot
./jichi --model jlu/qwen3-coder-next doctor --live      # per-model capability
jichi telemetry ~/.jichi.d/telemetry/zigodot-full.jsonl
jichi telemetry <a run's log> --cache-audit
./jichi learn analyze ~/.jichi.d/telemetry/zigodot-full.jsonl
```

See also: `docs/TELEMETRY.md` (the `exit` field and how to read an ok-rate),
`docs/ANECDOTES.md` #21, `docs/analysis/2026-07-27-local-gpu-bench.md`
(the same day's bench session), `docs/CONSTRAINTS.md`.
