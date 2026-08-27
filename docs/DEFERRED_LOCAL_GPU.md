# Deferred until a local-model + GPU bench

**Status:** ~~deferred pending hardware~~ — **session one ran 2026-07-27** on an
RTX 4070 Ti SUPER + LM Studio (`google/gemma-4-e4b`, 8192 ctx). Results, findings
and revised verdicts: **`docs/analysis/2026-07-27-local-gpu-bench.md`**. The
reusable procedure is **`docs/BENCH_LOCAL_GPU.md`**; the corpus this page called
"the fixed dogfood prompt suite" now exists as **`tests/bench/`**.
**Date:** 2026-07-23 (written); 2026-07-27 (first executed)
**Needs:** a machine running a small local model over an OpenAI-compatible
endpoint (LM Studio, or llama.cpp / Ollama) with a GPU, so real agentic turns
can be run and measured.
**Follows:** `docs/proposals/2026-07-small-model-agentics.md`,
`docs/LOCAL_MODELS.md`, `docs/proposals/2026-07-web-frontend.md`.

## What session one changed about this page

The bench found that its own premise was partly wrong, so read the items below
together with these verdicts:

- **The headline result was none of the four items.** jichi was ending every
  request — to every provider — with a content-free `{"role":"assistant",
  "content":""}` turn, the in-flight streaming placeholder. Frontier models
  ignore it; the small model closed the turn with one end-of-turn token and never
  called a tool. Fixed as **M166**; the corpus went 2/8 → 8/8. See
  `docs/ANECDOTES.md` #19.
- **Item 1 (compact schemas): deprioritized.** The −40% token claim is confirmed
  (10 804 B / 2640 tok → 6 191 B / 1592 tok, no quality loss). But the full
  18-tool prefix costs only 2659 of 8192 real tokens — 5.5k of working room,
  already above the §4 target — so the *rescue* motivation is void. Do not wire
  `toolSchema: "compact"` into the `small-local` preset.
- **Item 2 (`doctor --live`): promoted and BUILT (M167c).** Its real value is not
  classifying models but self-testing jichi's request construction — with one
  caveat learned in the building: the probe must mirror the agent loop's history
  shape (placeholder included), because a probe that constructs its own tidy
  request passes against a broken build. It now reports `✗ none` + exit 1 on a
  reintroduced pre-M166 build.
- **Item 3 (text protocol): still deferred**, and the evidence points further away
  — the only "non-native-looking" model all session was our own bug. What to build
  instead is the diagnosis path (§ item 3 below).
- **Item 4 (measurement plan): executed**, all targets met, with two amendments —
  tool ok-rate is misleading alone (a model that calls no tools scores 100%), and
  the stale-`old_string` row needs `--log-level full`. The `nudge`/`args_repair`
  rows now have a reader in `telemetry` (M167e).
- **Everything else on the recommendation list shipped as M167**: the
  empty-answer warning, the golden-request test, the M110 verb rule + adoption
  notices, model-specific calibration docs, and a corpus grown to 11 tasks / 21
  points with a two-sided grader self-check. Still unbuilt: item 1, item 3, and
  unwrapping a double-nested argument object.

## Why these wait

The small-model tool-calling band (M145–M151) is built and green under the
offline CI gate. What remains cannot be *honestly finished* on this machine:
each item either needs a **live small model to validate** (a token/quality
claim is not real until measured) or needs a **network probe that `make ci`
cannot exercise offline**. Shipping them "green" without a real model would
be exactly the hollow-gate mistake ANECDOTES #17 warns against — a check that
passes because it ran nothing.

So they wait for a bench with a local model on a GPU. This page is the
work-order for that session: each item carries its **already-decided design**,
**why it needs the bench**, and a **concrete acceptance test**, so the next
session can execute rather than re-derive.

The instrumentation to measure them is already in place: the `nudge` and
`args_repair` telemetry events (M147/M148) and the per-tool ok-rate summary
(`telemetry`) were built precisely so the before/after can be a number.

## The bench, once

```jsonc
// local/config.json — a small coder model over an OpenAI-compatible endpoint
{
  "models": [
    { "name": "local-7b", "provider": "openai",
      "model": "qwen2.5-coder-7b-instruct",
      "apiBase": "http://127.0.0.1:1234/v1",   // LM Studio default port
      "contextLength": 8192, "temperature": 0.2,
      "toolCalling": "native", "roles": ["chat","edit","apply","summarize"] }
  ]
}
```

Baseline first: run the fixed dogfood prompt suite with `--log-level metrics`
and record today's numbers (tool ok-rate median, stale-`old_string` share,
nudge-fired rate, redo loops) *before* changing anything. Every item below is
measured against that baseline.

> **As built (session one):** the suite is `tests/bench/` — 8 tasks, 13 points,
> each a `jc_assign` spec graded by its own `verify`, run by
> `tests/bench/run_bench.py` and summarised by `tests/bench/report.py`. The live
> config is `tests/bench/config.bench.json` (one model, no routing, no fallback,
> `contextLimit` deliberately below the real window). Procedure:
> `docs/BENCH_LOCAL_GPU.md`.

## The deferred items

### 1. Compact schema mode (proposal candidate #5) — build, then measure

**Design (decided, proposal §5.4):** a session-static `toolSchema: "full" |
"compact"`. Hand-written terse descriptions for the 7 core-profile tools;
first-sentence truncation + dropped per-arg prose for the rest. `apply_patch`
is **dropped** from the small preset rather than truncated (its ~1.2 KB is
load-bearing V4A format spec). Per-session static ⇒ the M31 prompt-cache
prefix stays byte-stable (a unit test guards this).

**Why the bench:** the *code* can be written offline, but its two claims are
empirical — the token drop (est. full ~4.5k→~1.5k, core ~2.5k→~1.2k, to be
confirmed with M77 calibration on the real model) and, more importantly, that
a small model still calls tools *correctly* from terse schemas. A terser
schema that raises the error rate is a regression, not a win.

**Acceptance test:** on the bench, `/context` shows the compact tool-definition
token count at the projected level; and the dogfood suite's tool ok-rate does
**not** regress vs the baseline (target: unchanged or better). Then wire
`toolSchema: "compact"` into the `small-local` preset (the one line M150
deliberately omitted) and note it in `docs/LOCAL_MODELS.md`.

### 2. `doctor --live` tool-calling probe (the deferred half of M149)

**Design (decided, proposal §5.3):** an opt-in `doctor --live` makes one
minimal request that advertises a single trivial tool and asks the model to
call it, then classifies the response: native tool call / JSON-in-text /
neither. It sets/confirms the per-model `toolCalling` flag empirically instead
of by hand.

**Why the bench:** it needs (a) a one-shot request path that *advertises a
tool* — `jc_oneshot` advertises none, so this is new plumbing — and (b) a live
model to classify. Neither is exercisable in offline CI; a stubbed probe would
test the stub, not the behavior.

**Acceptance test:** against the bench, `doctor --live` classifies the
known-native coder model as `native`; against a deliberately weak/non-tool
model it reports `text` or `none` and suggests the `toolCalling` setting. The
classifier reuses `jc_toolcall_scan` (M147) for the JSON-in-text case, so its
core stays pure and unit-tested; only the request+dispatch is bench-gated.

### 3. Text-protocol fallback (proposal candidate #8) — measure-need first

**Design (sketched, proposal §8, `"text"` enum reserved):** for a
`toolCalling: "none"` model, a prompt-based protocol — a single fenced
```` ```tool ```` block containing `{"name":…,"args":{…}}` — parsed by a pure
core reusing the same neutral tool array and `jc_tool_execute`.

**Why the bench — and why it may stay deferred:** this is **L effort, low
value** unless a real population of non-native models exists. The honest
prerequisite is *evidence*: run the M149 `doctor --live` probe (item 2) across
the models people actually use on the bench. If they all classify `native`
(the expectation — Qwen/Llama/DeepSeek coder models all ship native calling),
this stays deferred permanently and the `"text"` enum remains a reserved
no-op. Build it only if the probe finds genuine `none`/`text` models worth
serving.

**Acceptance test (only if built):** a configured `toolCalling: "text"` model
on the bench completes a real edit turn through the fenced-block protocol; the
parser's pure core has a unit corpus (partial fences, multiple blocks → take
first, malformed → fed back as a tool error).

### 4. The measurement plan (proposal §9) — validate the whole band

**What:** the before/after that turns M145–M151 from "plausible" into
"measured," on the fixed dogfood corpus against the bench model:

| Metric | Baseline (record first) | Target |
|---|---|---|
| Tool ok-rate (core tools, median) | 70–86% cluster (prior dogfood) | ≥85% median; no tool <60% |
| Stale-`old_string` share of edit failures | dominant | halved |
| `nudge_fired` / `nudge_recovered` (M147) | n/a | recovery ≥60% of fires |
| `args_repaired` success share (M148) | n/a | majority of parse failures |
| System+tools prefix (compact core, item 1) | ~full | fits 8k with ≥5k working room |

**Why the bench:** every row needs real turns against a real small model; the
counters exist but have never been read against one.

**Acceptance:** the numbers are recorded (commit them into a short
`docs/analysis/` note, like the zigodot review), and any target missed becomes
a concrete follow-up rather than a vague worry. This is also the evidence gate
for item 3.

## Order of work on the bench

1. Stand up the model; record the **baseline** numbers (§ measurement).
2. **Item 2** (`doctor --live`) — cheap, and it produces the evidence item 3
   depends on.
3. **Item 1** (compact schemas) — build, measure the token drop and the
   ok-rate no-regression; wire into the `small-local` preset if it passes.
4. **Item 4** — the full before/after, now including compact schemas.
5. **Item 3** (text protocol) — only if item 2 found real non-native models.

## Related, but not GPU-gated

These deferrals from other proposals do **not** need this bench and are tracked
where they were raised — listed here only so the "what's left" picture is whole:

- The web front-end milestone candidates still open (daemon non-AUTO mode, a
  warm ACP pool, advisory session `flock`) —
  `docs/proposals/2026-07-web-frontend.md`. (The jsonl heartbeat,
  `ls --output json`, and `export --output json` candidates **shipped as M165**.)
- The August-release human tracks (the license file, a verified macOS/BSD
  build, native review of the ja/zh doc drafts) — `docs/ROADMAP.md` top.
