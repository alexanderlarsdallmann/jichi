# Local-GPU bench, session one — measured results and findings

**Date:** 2026-07-27
**Hardware:** NVIDIA RTX 4070 Ti SUPER, 16 GB VRAM
**Server:** LM Studio, OpenAI-compatible, `http://127.0.0.1:1234/v1`
**Model:** `google/gemma-4-e4b` — 7.5B, `gemma4` arch, 8192 context
**Executes:** `docs/DEFERRED_LOCAL_GPU.md` (the work-order)
**Procedure:** `docs/BENCH_LOCAL_GPU.md`
**Corpus:** `tests/bench/` — 8 tasks, 13 points, graded by each spec's `verify`

## Summary

The bench was stood up, the missing corpus was written, and the work-order's four
deferred items were taken as far as evidence allows. The headline result is not
one of the four items: **jichi was sending every OpenAI-compatible model a
malformed request**, and on a small local model that made agentic work impossible.
Fixing it took one predicate shared by both providers and moved the corpus from
2/8 to 8/8.

Consequences for the work-order, in one line each:

- **Item 1 (compact schemas):** the token claim holds (−40%), the *quality* claim
  is void — the full schema works fine once the request is well-formed. Build it
  as a budget optimization, not a rescue. **Deprioritize.**
- **Item 2 (`doctor --live`):** **promote.** Its real value is not classifying
  models; it is that it would have caught this session's root cause on day one.
- **Item 3 (text protocol):** **keep deferred, and harden the diagnosis path.**
  The one apparently-non-native model here was jichi's own bug wearing a costume.
- **Item 4 (measurement plan):** executed. Every target met or exceeded — but one
  of its rows is actively misleading as specified (F3).

## The numbers

Corpus of 8 tasks / 13 points. `prefix` is the **server's own** `prompt_tokens`
for the first call — not the byte/4 estimate.

| Run | Tasks | Points | Prefix tok | Core ok-rate (median) | Worst tool | Sweep wall |
|---|---|---|---|---|---|---|
| pre-fix, `--tool-profile core` | 2/8 | 2/13 | 1176 | 100% *(see F3)* | 100% | 4.4 s |
| pre-fix, `--tool-profile full` | 2/8 | 2/13 | 2654 | 100% *(see F3)* | 100% | 6.9 s |
| **post-fix, core** | **8/8** | **13/13** | **1180** | **100%** | 100% | 52.9 s |
| **post-fix, full** | **8/8** | **13/13** | **2659** | **100%** | 50% (`apply_patch` 1/2) | 67.0 s |
| post-fix, core, **remote** `jlu/gemma-4-31b-it` | 8/8 | 13/13 | 1218 | 100% | 100% | 53.9 s |
| post-fix, full, **remote** | 8/8 | 13/13 | 2696 | 100% | 100% | 66.9 s |

The remote rows are the regression check on the fix: M166 changed the wire format
for *every* provider, so it was re-run against the HRZ remote model (a different
server, a different model family, a real network). Clean on both profiles. The
Anthropic half of the change is covered by unit tests only — no Anthropic key was
available — though the Messages API rejects empty text blocks outright, so the
placeholder can only ever have been harmful there.

After the corpus grew to 11 tasks (see the follow-up section), the same model
scores **10/11 (19/21 points)**, with task 09 failing about half the time — the
bench now has headroom to measure improvement, not just catastrophe.

Per-tool, post-fix `core`: `read_file` 9/9, `write_file` 3/3, `edit_file` 4/4,
`apply_patch` 1/1, `list_files` 2/2, `search_code` 1/1,
`run_terminal_command` 1/1. Zero nudges, zero argument repairs, zero mid-turn
compactions.

### Against the work-order's §4 targets

| Metric | Target | Measured (post-fix, core) | Verdict |
|---|---|---|---|
| Tool ok-rate (core tools, median) | ≥85%, none <60% | 100%, none below 100% | **met** |
| Stale-`old_string` share of edit failures | halved vs baseline | 0 edit failures (core); 1/1 of the single `full` failure | **met**, tiny sample |
| `nudge_fired` / `nudge_recovered` | recovery ≥60% of fires | 0 / 0 — never fired | **n/a** (see F6) |
| `args_repaired` success share | majority of parse failures | 0 / 0 — never fired | **n/a** |
| System+tools prefix fits 8k with ≥5k room | ≥5k working room | 1180 tok → **7.0k room** (core); 2659 → 5.5k (full) | **met on both profiles** |

The last row is the one that changes a design decision: **the prefix already fits
with room to spare, on the full profile.** That was item 1's entire justification.

### Token-estimate calibration

jichi's own learned ratio for this model, from `~/.jichi.d/calibration.json`:

```json
{"google/gemma-4-e4b":{"ratio":1.30026,"samples":2}}
```

Real `prompt_tokens` were **1.30×** the byte/4 estimate here — the estimate
running optimistic, as `docs/COMPACTION.md` says, but by 30% rather than the ~2×
cited there.

> **Correction (same day, after M170).** That figure came from **two** samples and
> is wrong. With 168 samples the converged ratio for `gemma-4-e4b` is **0.69×** —
> on the *other side of 1.0*, i.e. the byte estimate **over**-states this model's
> token count. Two samples got the sign wrong, not just the magnitude. The full
> measured table (0.69× to 2.53× across seven real models) now lives in
> `docs/COMPACTION.md`. The recommendation below stands and is strengthened:
> calibration is tokenizer-specific, and you must read your own
> `calibration.json` — after a few dozen turns, not after the first one.

## F1 — the in-flight assistant placeholder (root cause; fixed as M166)

**Symptom.** Against `gemma-4-e4b`, jichi's `--tool-profile full` runs made no tool
call at all. `[tokens in=2.649 out=1]`, an empty answer, exit 0 — five times out of
five. The `core` profile passed 4/5. Nothing logged a warning.

**Dead ends, and why each was wrong.**

- *"LM Studio can't stream tool calls."* Disproved by one `curl` with
  `stream: true` — the deltas arrive correctly, `finish_reason: "tool_calls"`.
- *"gemma-4-e4b lacks native tool calling."* Disproved by a non-streaming `curl`:
  a clean `tool_calls` array on the first try.
- *"It's the reasoning model."* The model emits `reasoning_content`; both
  providers already parse it (`saw_reasoning`, plus an explicit
  budget-exhaustion hint). Not it.
- *"The tool schema is too large for a small model."* This one was seductive
  because a count sweep *appeared* to confirm it: 4 tools worked, 8+ never did,
  with a threshold around 2.6 KB. It was noise from the real defect. After the
  fix, the identical sweep is 4/4 at every count from 1 to 18. **A plausible
  threshold measured over an unfixed defect is worse than no measurement.**

**How it was actually pinned down.** Two steps, both mechanical. Point `apiBase`
at a sink that records the request body verbatim; then replay that exact body at
the real endpoint with `curl --data-binary`. The replay failed identically —
proving the request, not the model, was at fault. Diffing the body against a
hand-written working request left one difference:

```json
"messages":[ {"role":"system",...}, {"role":"user",...},
             {"role":"assistant","content":""} ]
```

Removing only that message: 4/4 tool calls with all 18 tools. Keeping it: 0/4.

**Root cause.** `run_agent_loop` appends an empty assistant message to stream
into, and records its index so it can re-fetch after the stream
(`src/chat/jc_agent.c:902`). That placeholder is part of the history when
`build_request` runs, and neither provider's `build_messages` skipped it — the
OpenAI one emitted `{"role":"assistant","content":""}`, the Anthropic one an empty
text block. So *every* request jichi ever sent ended with a content-free assistant
turn. A frontier model ignores it. `gemma-4-e4b` reads it as "the assistant turn
already happened" and closes the turn with a single end-of-turn token.

**The fix (M166).** One shared predicate, `jc_prov_msg_is_placeholder`
(`src/provider/jc_provider.c`), used by both providers' message loops: skip an
assistant message with no content *and* no tool calls. An assistant message with
tool calls and no text is a real turn and is still serialised. Unit-tested for
both providers in `tests/test_provider.c`; 7197 checks and the full e2e suite
green.

**Takeaways.**

- A small local model is a request **validator**. It fails loudly where a
  frontier model silently compensates. That is a reason to keep one installed,
  not merely a reason to support one.
- The placeholder was correct as an internal accumulator and wrong as wire
  content. Any buffer that lives in a structure which is also serialised needs an
  explicit "not for the wire" rule — the fix is a predicate, not a special case.
- Bisect a suspicious request by **replaying it**, not by reasoning about it.
  Capture-and-replay took thirty minutes and ended four wrong theories.
- Tolerance is not compatibility. Every provider that "worked" was working around
  us.

## F2 — a prompt phrase permanently disables a tool (not fixed; recommended)

Two defects compounding, found because corpus task 07 tripped both.

**Over-broad extraction.** `jc_constraint_scan` (`src/chat/jc_constraint.c`) finds
a negation cue (`do not`, `don't`, `never`, …), takes a 95-character window after
it, and scans that window for target nouns — with no regard for the verb. So:

> "Run `./test.sh`. … Fix it. **Do not change the test file.**"

becomes `deny-tool run_tests` + `deny-cmd test`, i.e. *"do not run tests"*. The
instruction was about editing a file; it was enforced as a ban on running the
suite.

**Silent persistence.** The adopted constraints are written to
`<workspace>/.jichi/constraints.md` (`jc_app_constraints_save`) and reloaded at
startup for every later run in that directory. Verified in fresh workspaces:

| Prompt | Constraints adopted |
|---|---|
| "… Do not change the test file." | 2 — `deny-tool run_tests`, `deny-cmd test` |
| "… Leave `test_math.c` unchanged." | 0 |
| "Run ./test.sh and report its output." | 0 |

Once poisoned, the third prompt is blocked too — the file, not the prompt, is
doing the work. Nothing is printed to stderr when a constraint is adopted or
loaded; the operator learns about it only when the agent refuses mid-task and
explains itself in prose.

**Observed impact on the bench.** Under `core` the model recovered — it read the
sources, found the bug by inspection, and passed. Under `full` it gave up and
asked the operator for the failure logs. Same defect, different tool budget,
opposite outcomes: a reminder that a refusal's *blast radius* depends on how much
else the model has to work with.

**Why it is documented rather than fixed here.** Unlike F1 it does not block
measurement, and the right behaviour is a product decision, not an obvious
correction. Concrete options, cheapest first:

1. **Require a verb.** Only adopt `deny-cmd test` when the window matches a
   *running* verb (`run`, `execute`, `invoke`, `call`) near the noun — the noun
   alone should not imply the prohibition. Pure change to `scan_window`,
   unit-testable with a corpus of both phrasings.
2. **Announce adoption.** One stderr line — `[constraint] adopted: do not run
   tests (from your prompt)` — plus a line at load. Silence is the half of this
   bug that turns a misparse into a lasting mystery.
3. **Do not persist prompt-derived constraints by default.** A constraint the
   operator typed into `.jichi/constraints.md` is a policy; one inferred from a
   sentence in one turn is a guess. Persist explicit ones; keep inferred ones
   session-scoped, or require confirmation.

Recommendation: do all three; (2) alone would have saved this session an hour.

## F3 — tool ok-rate is a trap as the headline metric

The pre-fix runs scored a **100% core-tool ok-rate while failing 6 of 8 tasks**.
The metric is a ratio over attempted calls, so a model that has stopped calling
tools cannot fail one. The work-order's §4 table leads with this number.

The fix is presentational and cheap: ok-rate must always be reported beside the
**task pass-rate** and the **absolute call count**. `report.py` prints all three
adjacently. Anyone re-stating the §4 table should carry that constraint with it.

## F4 — the measurement plan's counters have no reader

`nudge` (M147) and `args_repair` (M148) events are emitted, but
`jichi telemetry` parses only `turn`, `model_call`, `tool_call`, `route`
and `compact` (`src/util/jc_telemetry.c`). Two of the five §4 rows are therefore
invisible to the shipped tooling — the work-order's claim that "the instrumentation
to measure them is already in place" is true of the emitters and false of the
reader. `tests/bench/report.py` reads the raw JSONL to compensate.

Recommended: two summary lines in `jc_telemetry.c` (`nudge fired=N recovered=N`,
`args_repair ok=N/N`), matching the existing per-tool block. Small, pure,
unit-testable, and it makes the plan self-service.

## F5 — the edit taxonomy needs the `full` tier

Metrics-tier `tool_call` events carry `ok` but not the error text, so
"stale-`old_string` share of edit failures" cannot be computed at `metrics`. At
`--log-level full` the event gains `output` and `args_full` and the taxonomy is
readable. `run_bench.py --log-level full` exists for this; the §4 row should say
which tier it requires.

## F6 — nothing warns on an empty answer

Through six consecutive total failures, jichi printed no diagnostic beyond a token
line. The pieces that could have spoken did not:

- **The M147 prose-call nudge** never fired, correctly: it detects a tool call
  *narrated as text*, and there was no text. The empty-response failure mode is
  outside its design.
- **`oa_reasoning_empty_hint`** is gated on `saw_reasoning`. These responses
  carried no reasoning either, so it stayed silent.
- **The envelope** reported `verified ok (tokens 5.902, tool calls 0)` — a green
  verdict for a run that did nothing, since no verifier was configured. The M86
  hollow-gate check watches test counts, not tool counts.

Recommended: warn once per session when a turn ends with **tools advertised, zero
tool calls, and an empty or single-token answer**. That is not a normal outcome
for an agentic turn, and it is the exact signature of a malformed request. It
belongs next to the existing reasoning hint in `src/provider/jc_provider_openai.c`,
or as an envelope-level advisory beside M96's analysis-starved check.

## Item-by-item verdicts

### Item 1 — compact schema mode: **build later, as a budget knob**

Measured directly, without writing any C, by applying the proposal's own §5.4
transformation (first-sentence descriptions, per-argument prose dropped) to a
captured request body and replaying both variants:

| Variant | Tool JSON | Prompt tokens | Tool calls |
|---|---|---|---|
| full | 10 804 B | 2640 | 5/5 |
| compact | 6 191 B | 1592 | 5/5 |

**−1048 tokens, −40%, with no loss of tool-calling ability.** The token claim is
confirmed (the proposal's absolute estimates — full ~4.5k → ~1.5k — were high;
the *direction* and rough proportion were right). The quality risk it worried
about did not materialise.

But its motivation is gone. The full 18-tool prefix costs 2659 real tokens of an
8192 window — 5.5k of working room, above the §4 target. Item 1 is now a
nice-to-have that buys ~1k tokens of history on a small model, not a prerequisite
for using one.

Revised recommendation: keep the design; implement it when small-context work
needs the headroom; **do not** wire `toolSchema: "compact"` into the `small-local`
preset. Session one shows the preset does not need it, and a default that trims
tool descriptions is a default that makes tool selection harder to debug. Note
also that its stated acceptance test — "`/context` shows the compact
tool-definition token count" — cannot be met by the `context` subcommand, which
registers all built-ins regardless of `--tool-profile` (`run_context`,
`src/main.c:2314`). Real `prompt_tokens` from telemetry are the better evidence
and need no new code.

### Item 2 — `doctor --live`: **promoted, and built (M167)**

Cheap to satisfy as specified: `gemma-4-e4b` classifies `native` in one request.
The better justification is that the probe doubles as an **end-to-end self-test of
jichi's own request construction** — but building it taught a lesson worth recording,
because the obvious implementation does *not* do that.

**A minimal probe does not catch F1.** The first implementation sent one user
message and one tool, and it reported a clean `native` against a deliberately
reintroduced pre-M166 build. The reason is structural: the placeholder is appended
by `run_agent_loop`, so a one-shot request that builds its own history never
contains it. The probe was testing `build_request` against a hand-made history —
not the request the agent actually sends.

Measured directly (same endpoint, four trials each):

| Request shape | prompt tokens | Result |
|---|---|---|
| user + 1 tool | 104 | tool, tool, tool, tool |
| user + 1 tool + **empty assistant** | 99 | EMPTY ×4 |
| system + user + 1 tool | 229 | tool, tool, tool, tool |
| system + user + 1 tool + **empty assistant** | 224 | EMPTY ×4 |

The placeholder is decisive at any size; the system prompt is irrelevant. So
`jc_oneshot_probe` now deliberately mirrors the loop's history shape, placeholder
included. With that, the pre-M166 build reports:

```
✗ --live: tool calling observed "none" (configured "native")
    the model answered a one-tool request with NOTHING. Suspect jichi's request
    before the model: capture and replay it ... -- probe prefix was 99 real
    prompt tokens
```

exit code 1, and the fixed build reports `✓ native`. **The general rule: a
self-test must construct its subject the way production does, or it tests a
sibling of the thing you care about.**

The advice text is pure and unit-tested precisely because its *ordering* is the
point: for configured-native/observed-none it names the request first, tells you
to replay it, and explicitly warns that setting `toolCalling: "none"` there would
hide a bug rather than fix one. Reporting the probe's real `prompt_tokens` came
free and gives every user the calibration figure §5 otherwise asks them to dig out
by hand — which required fixing a second small gap: `parse_full` never read the
`usage` object, so non-streaming calls had always discarded their token counts.

### Item 3 — text-protocol fallback: **stay deferred; fix the misdiagnosis instead**

The evidence gate is unsatisfied: the one model available classifies `native`, and
the only "non-native-looking" behaviour seen all session was our own malformed
request. Building a prompt-based protocol to serve models that mostly do not
exist remains L-effort, low-value.

The session did, however, expose a diagnostic hazard worth fixing in its place.
When the nudge fails to recover, jichi warns that the model "may lack native
tool-call support (see docs/LOCAL_MODELS.md)". Here that advice would have been
actively wrong and would have led a user to set `toolCalling: "none"` — degrading
a fully capable model to work around a bug in jichi. Recommended: soften that
warning to name both causes, and have `docs/LOCAL_MODELS.md` put "verify the
request is well-formed (capture and replay it)" *above* "your model may not
support tool calling" in its troubleshooting order.

### Item 4 — the measurement plan: **executed, with two amendments**

Numbers are in §"The numbers". Every target met. Two amendments to the table
itself, from F3 and F5: ok-rate must be reported with task pass-rate and call
count beside it, and the stale-`old_string` row must state that it requires
`--log-level full`.

## Recommendations for further design and development

Ordered by value per unit of effort.

**1. Ship the F6 empty-answer warning (S).** The single highest-value change
this session suggests. Six silent total failures is a diagnosability bug in its
own right; a one-line warning converts an hour of bisecting into a pointer.

**2. Build `doctor --live` as a request self-test (M).** Item 2, reframed per
above. It is the standing regression test for the whole class of defect F1
belongs to, and it is the natural home for reporting the calibration ratio.

**3. Fix the M110 constraint misparse and announce adoption (S+S).** F2's options
1 and 2. Silence is the worse half of that bug.

**4. Add `nudge` / `args_repair` lines to the telemetry summarizer (S).** F4.
Makes the measurement plan self-service instead of dependent on the bench's
private reader.

**5. Wire the bench into the development loop, not the CI gate (S).** A one-minute
sweep that catches what `make ci` structurally cannot. Suggest running it on every
change under `src/provider/`, `src/chat/jc_sysmsg.c`, or the tool schemas. It must
stay out of `make ci` — it needs a live model.

**6. Add a golden-request test to the offline gate (S).** F1 was invisible to
7000+ offline checks because none asserted on the *shape* of a built request. A
test that builds a request from a known history and compares it to a committed
golden JSON would have caught it, and would catch the next such regression, with
no network. This is the cheapest permanent guard.

**7. Describe calibration as model-specific (XS).** `docs/COMPACTION.md` cites
~2×; this model measures 1.30×. Cite the mechanism, give both data points, and
tell operators to read their own `calibration.json`.

**8. Grow the corpus deliberately (M, ongoing).** Eight tasks and 13 points is
enough to catch a catastrophe, not enough to resolve a 5% regression, and every
post-fix run scored 100% — the corpus currently has no headroom to show
improvement. Add tasks that the reference model *fails*: a multi-file refactor
with an ambiguous match, a task needing `search_code` before a correct edit, one
requiring a second tool round after a genuine tool error. A bench that always
scores full marks has stopped measuring.

**9. Re-run the sweep across a second and third model (M).** Every conclusion here
rests on one 7.5B model. A coder-tuned model of the same size (`qwen2.5-coder-7b`
was the work-order's reference) and one deliberately weak model would test both
the generality of the F1 fix and item 2's classifier — and would finally populate
item 3's evidence gate with real data.

## Follow-up: what was built the same day (M167)

Recommendations 1–8 were acted on rather than filed. What shipped, and what it
changed:

| # | Shipped | Evidence it works |
|---|---|---|
| 1 | empty-answer warning (F6) | `tests/e2e/empty_answer.py`: mock returns an empty delta with tools advertised; the warning names the tool count, points at capture-and-replay, and fires exactly once |
| 2 | `doctor --live` (item 2) | `✓ native` on the fixed build; `✗ none` + exit 1 on a reintroduced pre-M166 build; pure core in `tests/test_toolprobe.c` |
| 3 | M110 verb rule + adoption notice (F2) | "do not change the test file" now adopts 0 constraints; "do not run tests" still adopts 2 **and says so** at WARN; inherited constraints announced at startup |
| 4 | `nudge`/`args_repair` in `telemetry` (F4) | a `Self-correction:` block with recovery/success shares; omitted entirely on a clean log |
| 6 | golden-request test | catches the reintroduced placeholder at byte 318 with a readable diff, for both providers |
| 7 | calibration documented as model-specific | `docs/COMPACTION.md` now carries both data points and tells operators to read their own file |
| 8 | corpus grown 8 → 11 tasks (13 → 21 points) | the model now scores **10/11, 19/21** — the bench can finally show improvement as well as catastrophe |

Two further findings came out of that work.

**F7 — a "measured" pass rate can be a broken grader.** Task 09's first `verify`
used `\\[client\\]` in its YAML; after the runner's unescaping, awk matched a
literal backslash, so no section ever matched and a *correct* edit was scored as a
failure — 5 runs in a row, with the blame naturally landing on the model. The
grader had been "validated" by an ad-hoc snippet that unescaped differently from
`run_bench.parse_spec`. The fix is `tests/bench/check_graders.py`, which imports
the runner's own parser and asserts every grader both rejects the pristine fixture
and accepts a reference solution. **Validating a check through a different code
path than the one that runs it is not validation** — the same shape of mistake as
F1's minimal probe, twice in one day.

**F8 — a double-wrapped argument object is not repaired. → BUILT as M172.** On one
task-09 attempt the model emitted `{"edit_file": {"path": ..., "old_string": ...}}`
— the arguments nested under the tool's own name. That is *valid* JSON, so M148's
repair never fires (it triggers only on a parse failure), and the tool then finds
every required argument missing.

Fixed by the pure `jc_tool_unwrap_self_named`: exactly one member, keyed by the
tool's **own** name, wrapping an object. Provably unambiguous — no tool has a
parameter named after itself (parameters are short generics like `path`, `command`,
`query`; tool names are `verb_noun` compounds, and this was checked across every
registered schema). Counted as `args_repair` with `kind:"unwrap"`, so it shows up in
telemetry and stays distinguishable from a syntax repair — a *shape* error and a
*syntax* error call for different fixes. Tested through the real dispatch as well as
the helper, and the test was verified to fail with the unwrap disabled.

Task 09 also turned out to be an ideal discriminator: **3/6 pass rate**, and its
failures are instructive rather than opaque. The full-tier trace shows the M38/M148
error messages doing exactly their job — an ambiguous `old_string` refused with
"add more surrounding context, or set replace_all", a literal `\n` in
`old_string` answered with the similar-text hint, and the model recovering from
both.

Deliberately **not** built: item 1 (compact schemas — deprioritized above) and
item 3 (text protocol — evidence gate still unmet). F8 shipped as M172.

## Reproducing this session

```sh
make && make test
curl -s http://127.0.0.1:1234/v1/models | jq -r '.data[].id'

python3 tests/bench/run_bench.py --profile core --label core
python3 tests/bench/run_bench.py --profile full --label full
python3 tests/bench/report.py core full

# the item-1 evidence, and the F1 self-test
python3 tests/bench/schema_probe.py --body body.json --mode compact
python3 tests/bench/schema_probe.py --body body.json --mode count
```

`docs/BENCH_LOCAL_GPU.md` §7 documents the capture-and-replay procedure that
produced `body.json` and found F1.

See also: `docs/DEFERRED_LOCAL_GPU.md`, `docs/BENCH_LOCAL_GPU.md`,
`docs/ANECDOTES.md` #19, `docs/LOCAL_MODELS.md`, `docs/COMPACTION.md`,
`docs/CONSTRAINTS.md`, `tests/bench/README.md`.
