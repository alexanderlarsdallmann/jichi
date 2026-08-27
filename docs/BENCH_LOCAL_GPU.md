# The local-GPU bench: measuring jichi against a small model on your own hardware

A remote frontier model forgives almost everything. It tolerates a malformed
request, infers intent from a vague prompt, and recovers from a bad tool result
without being told how. A 4–8B model on your own GPU forgives nothing — which is
exactly what makes it the best test instrument jichi has. Every sloppy edge in the
request, the system prompt, or the tool schema shows up as a *failure* instead of
being quietly absorbed.

This page is the procedure for that measurement: how to stand the bench up, what
to record, and how to read the result. The work-order it executes is
`docs/DEFERRED_LOCAL_GPU.md`; the first session's numbers and findings are
`docs/analysis/2026-07-27-local-gpu-bench.md`.

> **The bench is a measurement, not a gate.** It needs a live model, so it must
> never run in `make ci`. `make e2e` stays offline. Nothing here changes that.

## 1. What it is for

Three distinct jobs, and it is worth keeping them apart:

| Job | Question it answers | Artifact |
|---|---|---|
| **Regression measurement** | did a change make jichi better or worse at driving a small model? | a labelled run under `tests/bench/results/` |
| **Capability probe** | can *this* model do agentic work at all, and at what context cost? | `report.py`'s prefix/peak token rows |
| **Self-test of jichi's own requests** | is jichi emitting a well-formed request? | a total tool-calling collapse — see §7 |

The third is the one nobody planned for and the one that paid off first. A small
model is a *differential* instrument: when it stops calling tools entirely, the
likeliest cause is not the model.

## 2. Hardware and software

The reference bench, and what the first session ran on:

- **GPU:** NVIDIA RTX 4070 Ti SUPER, 16 GB VRAM (`nvidia-smi` to confirm)
- **Server:** LM Studio, OpenAI-compatible endpoint on `http://127.0.0.1:1234/v1`
- **Model:** `google/gemma-4-e4b` — 7.5B, `gemma4` arch, 8192 context, ~6.3 GB on disk
- **Embeddings (optional):** `text-embedding-nomic-embed-text-v1.5`

Any OpenAI-compatible local server works — llama.cpp's `llama-server`, Ollama,
LocalAI. `docs/LOCAL_MODELS.md` has the per-server quickstarts and the traps
(notably Ollama's `OLLAMA_CONTEXT_LENGTH`, which silently truncates).

Confirm the endpoint and the loaded model before anything else:

```sh
nvidia-smi | head -12
curl -s http://127.0.0.1:1234/v1/models | jq -r '.data[].id'
lms ps                      # LM Studio: loaded models, context, TTL
```

A model listed but not loaded will load on the first request and make that one
call an outlier. Warm it with a throwaway request before recording numbers.

## 3. The bench config

`tests/bench/config.bench.json`. It is deliberately austere, and every omission
is load-bearing:

```jsonc
{
  "models": [{
    "name": "local-small", "provider": "openai",
    "model": "google/gemma-4-e4b",
    "apiBase": "http://127.0.0.1:1234/v1",
    "contextLength": 8192, "temperature": 0.2,
    "toolCalling": "native", "promptCache": false,
    "roles": ["chat", "edit", "apply", "summarize"]
  }],
  "contextLimit": 6000,
  "routing": { "enabled": false },
  "snapshots": false, "repoMap": false, "references": false,
  "autoContext": false, "markdown": false, "hooksEnabled": false,
  "logging": { "level": "metrics" }
}
```

**Design decision — one model, no fallback, no routing.** A measurement is
worthless if you cannot say which model produced it. A `fallback` chain or an
enabled `routing` block means a stalled local model silently escalates to a
remote one and the run *succeeds* — with numbers from the wrong model. `--no-route`
on every invocation is belt and braces.

**Design decision — `contextLimit` below the real window.** The model serves 8192;
the config declares 6000. The byte/4 estimate that drives compaction runs
optimistic (see §5), so declaring the true window makes every context decision
fire late. Half to three-quarters of the real window is the right posture, and
`docs/COMPACTION.md` says so for the same reason.

**Design decision — subsystems off.** Repo map, references, auto-context and
markdown rendering all change the prompt or the output. Off means the corpus
measures tool-calling mechanics, not retrieval quality. Turn them on
*deliberately*, one at a time, as their own labelled run.

## 4. Running it

```sh
make                                              # the bench drives ./jichi

python3 tests/bench/run_bench.py --profile core   # the small-model default
python3 tests/bench/run_bench.py --profile full   # every built-in advertised
python3 tests/bench/report.py                     # the measurement table
```

Useful flags:

| Flag | Why |
|---|---|
| `--profile core\|full\|auto` | the tool-profile A/B. `auto` resolves to `core` below a 12k effective context, so `core` is what a real `--lite` user gets |
| `--label <name>` | names the results directory; use it to keep a before/after pair |
| `--only 07` | one task, while iterating |
| `--log-level full` | also records tool I/O content — **required** for the stale-`old_string` taxonomy (§5) |
| `--budget-tokens` / `--deadline` / `--max-tool-calls` | the envelope caps; defaults 200k / 10m / 25 |
| `--model` / `--config` | point at a different model or endpoint |

Each task runs in a throwaway workspace with a throwaway `HOME`, then is graded by
its own `verify`. One task's failure never affects another's.

A full sweep of the 8-task corpus takes about a minute of wall clock on the
reference hardware — cheap enough to run on every provider-layer change.

## 5. What to record, and the two rows that need `--log-level full`

`report.py` prints the rows of the work-order's §4 measurement plan. Two of them
need care:

**Tool ok-rate must never be read alone.** It is a ratio over *attempted* calls.
A model that has stopped calling tools altogether scores a perfect 100% — the
first session measured exactly that: 100% ok-rate alongside a 2/8 task pass-rate.
Always read ok-rate next to the task pass-rate and the call count. `report.py`
prints all three adjacently for this reason.

**The stale-`old_string` taxonomy needs the `full` tier.** Metrics-tier
`tool_call` events carry `ok` but not the error text, so at `metrics` an edit
failure cannot be classified. At `full` the event gains `output` and `args_full`
and the taxonomy becomes readable. Run the taxonomy pass separately:

```sh
python3 tests/bench/run_bench.py --profile core --log-level full --label taxonomy
```

**`nudge` and `args_repair` have no reader in `jichi telemetry`.** The
events are emitted (M147/M148) but the built-in summarizer parses only
`turn`/`model_call`/`tool_call`/`route`/`compact`. `report.py` reads the raw JSONL
instead. Closing that gap is the first recommendation in the analysis note.

**Record the calibration ratio.** After a run against a persistent `HOME`,
`~/.jichi.d/calibration.json` holds what jichi learned about this model's
tokenizer: `real prompt_tokens ÷ byte-estimate`. It is the single most useful
number for sizing `contextLimit`, and it is model-specific — the first session
measured 1.30 for `gemma-4-e4b`, not the ~2× that `docs/COMPACTION.md` cites as
typical.

## 6. Trusting the graders

A bench whose grader is wrong is worse than no bench, and the failure is
asymmetric: a `verify` that cannot fail silently inflates your score, while one
that cannot *pass* blames the model for your bug. Session one hit both.

```sh
python3 tests/bench/check_graders.py
```

That asserts, for every task, that the `verify` **rejects the untouched fixture**
and **accepts a reference solution** — and it does so through
`run_bench.parse_spec`, the same parser the runner uses.

**Design decision — validate through the production code path.** The obvious way
to check a grader is a quick shell or Python snippet that pulls `verify` out of the
YAML and runs it. Session one did that, and it lied: the snippet unescaped
`\\[client\\]` differently from the runner, so awk in the *real* run matched a
literal backslash, no section ever matched, and five correct edits were scored as
failures — with the blame landing naturally on the model. A check that exercises a
sibling of the thing under test is not a check. (The same shape of mistake bit the
first `doctor --live` implementation the same day; see §7 and ANECDOTES #20.)

Adding a task therefore means adding its reference solution to `solve()` in
`check_graders.py`. If that feels like friction, it is the friction of stating what
"solved" means in code instead of in prose.

## 7. Reading a total collapse

If the model makes **no tool call at all** and the answer is empty or a single
token, do not conclude "this model can't do tool calling". Work down this list;
session one's root cause was #1, and every hypothesis above it was wrong.

0. **Run `doctor --live`.** One request, one trivial tool, and a verdict:
   `native` / `text` / `none`. A `none` against a model configured `native` says
   the request is the likely problem and points here. It mirrors the agent loop's
   history shape on purpose — a probe that builds its own tidy request would pass
   on a broken build, which is exactly what the first implementation did.
1. **Is jichi's request well-formed?** Capture what jichi actually sends and replay
   it by hand. Point `apiBase` at a sink that records the body, then `curl
   --data-binary @body.json` it straight at the real endpoint. If the replay also
   fails, the request — not the model — is the problem. This is the highest-value
   30 minutes in the whole procedure.
2. **Does the bare endpoint do tool calling?** One `curl` with one trivial tool,
   `stream: true`. If that works and jichi does not, the difference is in the body.
3. **Is a persisted constraint blocking the tool?** Check
   `<workspace>/.jichi/constraints.md`. M110 constraints are extracted from prompts
   and **persist**; one badly-phrased sentence can disable a tool for every later
   run in that directory (§8, and finding F2 in the analysis note).
4. **Is the output budget the limit?** `max_tokens` is derived from
   `contextLimit`. A reasoning model can spend the whole budget on
   `reasoning_content` and emit no answer; jichi warns about this case explicitly.
5. **Only then**: the model genuinely lacks native tool calling — set
   `"toolCalling": "none"` and see `docs/LOCAL_MODELS.md`.

`tests/bench/schema_probe.py` automates the useful part of #1/#2: it replays a
captured body while varying one property of the tool array at a time.

```sh
python3 tests/bench/schema_probe.py --body body.json --mode count     # tool-count sweep
python3 tests/bench/schema_probe.py --body body.json --mode compact   # full vs terse schemas
python3 tests/bench/schema_probe.py --body body.json --mode drop-one  # single culprit?
```

A caution learned the hard way: a count sweep taken while a *different* defect is
present produces a beautifully plausible, entirely fictitious threshold. Session
one measured "tool calling collapses above 6–8 tools", published nothing, found
the real cause, and re-measured 4/4 at every count from 1 to 18. **Fix the
known defect, then re-run the sweep before believing its shape.**

## 8. Traps specific to this bench

- **Constraint poisoning.** A prompt containing a negation followed by a bare
  noun ("do not change the **test** file") is read as the prohibition "do not run
  tests", and it is written to `.jichi/constraints.md`. Everything run in that
  directory afterwards inherits it silently. `rm .jichi/constraints.md` (or the TUI
  `/constraints clear`) between experiments, and prefer phrasings without a
  negation cue.
- **A warm vs cold model.** The first call after a load pays the load cost. Warm
  up, or discard the first task.
- **`lms ps` TTL.** LM Studio may unload an idle model mid-sweep. Pin the TTL, or
  keep sweeps short.
- **Shared `$HOME` across architectures.** The index cache stamps its byte order
  (M136) and rebuilds rather than misreading, but the calibration file is shared —
  another reason the runner isolates `HOME`.
- **`context` does not model the tool profile.** The `context` subcommand
  registers all built-ins regardless of `--tool-profile`, so it cannot show the
  core-profile figure. Use the real `prompt_tokens` from telemetry instead; they
  come from the server's own tokenizer and are better evidence anyway.

## 9. Adding a task

A good bench task is deterministic, small, and graded by something other than the
model's prose. Add a directory under `tests/bench/corpus/`:

```
corpus/09-my-task/
  spec.md        # jc_assign frontmatter: title, audience: agent, verify, points
  files/         # the fixture, copied fresh per run
```

Then prove the grader both ways (§6). Keep the prompt free of negation cues
followed by `test`/`build`/`commit`/`push`/`deploy`/`install` unless the
constraint interaction *is* the thing under test.

Do not reword existing tasks — a moved goalpost invalidates every prior number.
The corpus is an append-only measuring stick.

## 10. Verification — try it end to end

```sh
make && make test                                   # offline gate still green
curl -s http://127.0.0.1:1234/v1/models | jq -r '.data[].id'

python3 tests/bench/run_bench.py --profile core --label check
python3 tests/bench/report.py check
```

Expected on the reference bench (`gemma-4-e4b`, 11 tasks / 21 points): **10/11,
19/21 points**, core-tool ok-rate median 100%, first-call prefix ≈1180 real
tokens, no mid-turn compaction. Task `09-ambiguous-edit` fails roughly half the
time by design — it is the discriminator, so treat 9/11 or 11/11 as normal
variance and only a persistent drop below that as a regression. Per §7, the first
suspect for a collapse is jichi, not the model.

Add `python3 tests/bench/check_graders.py` (offline, no model) before trusting any
number after you have touched the corpus.

## 11. The language-version probe

`tests/bench/version_probe.py` runs the [`CHOOSING_A_MODEL.md`](CHOOSING_A_MODEL.md)
§4 ritual against a live endpoint: one context-free self-report question ("what is
the newest released version of X you know?") plus behaviour probes whose arbiter is
the **locally installed compiler**, then prints a dated MODEL_KNOWLEDGE-style record
block for the operator to paste into the target project's documentation. Run it when
onboarding a model for a fast-moving toolchain, and re-run it when the model, the
backend, or the toolchain version changes.

```sh
python3 tests/bench/version_probe.py --mode self-test    # offline: proves the gate two-sided
python3 tests/bench/version_probe.py \
    --url http://127.0.0.1:1234/v1/chat/completions \
    --model MODEL_ID --key-env MY_KEY_ENV --lang zig --lang c89 --trials 3
```

It shares the bench's honesty rules: the verdict can only *fail* a model (all-pass
is worded "not refuted — weak evidence"); the compile gate must be shown able to
fail before being trusted (`--mode self-test` refuses a canned old-dialect source
and accepts a verified current-idiom one; a `/bin/true` compiler makes it exit 1);
and `c89` is the frozen control — a model failing C89 indicts the probe. First live
run (2026-08-11, `jlu/gemma-4-26b-it`): Zig self-report 0.11.0, behaviour probes
0/2 compiled under 0.16.0, C89 control 1/1 — the record now sits in zigodot's
`docs/MODEL_KNOWLEDGE.md`.

## 12. The cache probe

`tests/bench/cache_probe.py` is the version probe's sibling for
[`PROMPT_CACHING.md`](PROMPT_CACHING.md)'s "cached=0 is not proof" caveat: it
times one cold call against warm repeats of a byte-identical ~18k-token prefix
(stamped unique per run, so earlier runs cannot pre-warm it) and reports an
**asymmetric** verdict — wire counters or an unambiguous latency ratio prove a
cache live; a flat ratio proves nothing, and the verdict says so ("a
fast-prefill model can cache invisibly below measurement noise"). The verdict
function is offline-tested (`--mode self-test`, five canned cases including
both measured 2026-08-11 shapes). First live run reproduced the discovery:
5.28s cold → 0.29s warm on `jlu/gemma-4-31b-it`, 17.9×, nothing on the wire.

```sh
python3 tests/bench/cache_probe.py --mode self-test    # offline
python3 tests/bench/cache_probe.py \
    --url http://127.0.0.1:1234/v1/chat/completions --model MODEL_ID --trials 3
```

## Recommendations

- Run the corpus on every change to `src/provider/`, `src/chat/jc_sysmsg.c`, or
  the tool schemas. It is a one-minute sweep that catches a class of defect the
  offline gate structurally cannot see.
- Keep a small local model installed even when working against remote models. It
  is the cheapest well-formedness checker jichi has.
- When a bench number surprises you, capture and replay the request body before
  forming any theory about the model.
- Record every session as a dated `docs/analysis/` note. The numbers only mean
  something as a series.

See also: `docs/DEFERRED_LOCAL_GPU.md` (the work-order),
`docs/analysis/2026-07-27-local-gpu-bench.md` (session one),
`docs/LOCAL_MODELS.md` (standing a local server up),
`docs/COMPACTION.md` (context estimates and calibration),
`docs/TELEMETRY.md` (the event schema), `docs/ANECDOTES.md` #19 (the war story),
`tests/bench/README.md` (the corpus itself).
