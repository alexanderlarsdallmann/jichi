# Prompt caching works on this backend — measured, with the config that gets it

*Every cost analysis in this project so far has been written against a backend that returned
**zero** cached tokens: 967 calls, 0 hits, ~28k tokens re-sent per call
(docs/analysis/zigodot-jichi-review.md, M326w). That was true of the model being used, not of
the backend. Measured 2026-08-09.*

---

## 1. What bears on caching, and what is withheld

*The gateway's model inventory — how many models it fronts, in which vendor families,
and which of them a given key reaches — was measured and is **not reproduced here**. It
describes another organisation's configuration and is theirs to publish; the same
reasoning withholds the report in
[`2026-08-09-hrz-gateway-findings.md`](2026-08-09-hrz-gateway-findings.md), which also
keeps the lessons in full.*

One fact from that survey is load-bearing for everything below, and is a property of the
**vendors**, not of the operator:

- The proxy fronts **vendor-hosted Anthropic models, which cache**, alongside
  **locally-hosted models, which report no cached tokens at all**.

That single split is the whole argument of this note. Every cost analysis this project
had written was measured on the second kind — 967 calls, 0 cache hits, ~28k tokens of
prefix re-sent per call — and then generalised as though it were a fact about the
backend. It was a fact about **the model being used**. A conclusion drawn on one side of
that split inverts on the other, which is §6's point and the reason M440's cost-model
prompt section is gated on whether caching is on.

## 2. Caching works, and jichi already speaks the protocol

Two facts had to hold and both do:

1. **The proxy exposes `/v1/messages`** (the Anthropic-shaped endpoint), not only
   `/v1/chat/completions`.
2. **It honours `cache_control` breakpoints.** A 25 KB system block, sent twice:

   ```
   pass 1: input 11  cache_write 6163  cache_read 0
   pass 2: input 11  cache_write 0     cache_read 6163
   ```

jichi's Anthropic provider (M31b) emits exactly those breakpoints — one on the system block
(tools + system) and one on the conversation tail — so **no code was written for this**. It is a
configuration:

```json
{ "name": "cache-haiku", "provider": "anthropic",
  "model": "anthropic/claude-haiku-4-5",
  "apiBase": "https://api.hrz.uni-giessen.de",
  "apiKeyEnv": "JLU_API_KEY",
  "roles": ["chat","edit","summarize"],
  "contextLength": 180000, "maxTokens": 2048, "promptCache": true }
```

Note `provider: "anthropic"` with an **HRZ** `apiBase`. The provider name selects the wire
protocol, not the vendor.

## 3. Measured, 14 model calls in the jichi repository

A sequential read-and-check task, `craft: true`, `repoMap: true`, `toolProfile: "full"`:

```
 call  uncached  cache_rd  cache_wr
    1         3     20250       394
    3         6     20644       381
    5         6     21025      2456
  ...
   28         6     33070       142

calls 14   uncached 81   cache_read 363,677   cache_write 19,557
hit rate 94.9%
```

**The uncached remainder is 6 tokens per call.** Applying Anthropic's multipliers (write 1.25x,
read 0.10x of base input):

| | input-token-equivalents |
| --- | --- |
| with caching | **60,895** |
| without (what this project has been paying) | 383,315 |
| | **84.1% cheaper** |

Two things in that table deserve emphasis:

- **The prefix grows and stays cached.** `cache_rd` climbs 20k → 33k as history accumulates,
  with the uncached remainder flat at 6. That is the growing-tail breakpoint working.
- **The cache survived across processes.** Call 1 of this run read 20,250 tokens from a cache
  written by a *previous* jichi process minutes earlier. It is keyed on content, not on
  session, so a supervisor running many short jichi invocations over one workspace gets hits
  from the first call — the opposite of what a per-session cache would give.

## 4. The trap: a small prefix silently gets nothing

The first attempt measured **zero** caching with `promptCache: true` on a caching-capable model.
The cause was in the same log: `sys_tok: 397`.

**Anthropic will not cache a block below its minimum — 2048 tokens for Haiku, 1024 for
Sonnet/Opus.** My config had turned `craft` and `repoMap` off to keep the prefix small, which is
correct advice for an *uncached* backend and exactly wrong here. The two settings invert:

| | uncached backend | caching backend |
| --- | --- | --- |
| big system prefix (rules, repo map, craft) | pay for it **every call** | pay ~1.25x **once** |
| trimming the prefix | the main lever | can push you under the minimum and cost you everything |

jichi reports the number needed to see this (`sys_tok` on every `model_call`), and nothing warns
when a prefix is too small to cache while caching is on. That is a `doctor` check worth adding.

## 5. What this does not say

- **Not measured: quality.** This is a token-cost measurement on one task. Whether
  `claude-haiku-4-5` is better or worse than `jlu/gemma-4-31b-it` for zigodot's work is a
  separate question and the bench (`tests/bench/`) is how to answer it.
- **Not measured: cost in money.** HRZ is unpriced in our configs, so `cost_usd` is 0 and the
  84.1% is in token-equivalents, not currency.
- **Short runs benefit least.** A 2-call run measured 34.1%, because the first write can never
  be amortised and the tail breakpoint written on the last call is never read. Caching pays as
  calls accumulate; it is close to free, not free, on a one-shot.
- **The 5-minute TTL applies.** `promptCacheTtl: "1h"` (M31e) exists for runs with long pauses
  and was not exercised here.

## 6. What to change

1. A **`doctor` warning** when `promptCache` is on and the resolved prefix is below the model's
   minimum — the silent-zero case above, which cost an entire measurement round.
2. **Reconsider the zigodot config's prefix trimming** (`craft: false`, `repoMap` limits, the
   17-name deny list) if it moves to a caching model. Every one of those decisions was
   correctly derived from a no-cache measurement and every one inverts.
3. Correct the standing project note that this backend does not cache. It does — for the
   vendor-hosted models, and not for the locally-hosted ones.

---

## 7. CLOSED: the minimum is 4096, not 2048 -- and the documentation is wrong for this generation

*Left open at M340, closed at M341 by the request-body dump that section named as the missing
diagnostic. The order of the wrong hypotheses is the useful part.*

**The symptom.** A config whose cacheable prefix measured ~3700 real tokens -- comfortably over
Haiku's *documented* 2048 minimum -- reproducibly cached nothing, with a model object
byte-identical to one achieving 94.9%.

**What the dump settled immediately.** `--dump-requests` writes the exact JSON handed to libcurl.
The request was **correct**: `system` as a one-block array carrying `cache_control`, 18 tools, the
right endpoint. So jichi was not the problem -- which I had briefly concluded it was, from
grepping a `--log-level full` log that records prompt *content* and not the request.

**What replay then allowed.** With the body on disk it could be replayed and bisected, which no
amount of further reasoning was going to achieve:

| variant | block | result |
| --- | --- | --- |
| as sent | ~3700 | no cache |
| without `stream` | ~3700 | no cache -- **not a streaming artefact** |
| tools removed | ~268 | no cache |
| 1 tool | ~918 | no cache |
| 9 tools | ~2185 | no cache -- **over 2048 and still nothing** |
| system x30 | big | **cached** |
| tools **doubled** | ~6668 | **cached** -- same shape, twice the size |
| breakpoint moved onto the last tool | -- | no cache |

The doubled-tools row broke the deadlock: identical structure, twice the size, and it cached. So
size *was* the discriminator and the threshold simply was not 2048. Bracketing it:

```
pad     0 bytes: block~3700  no cache
pad  1200 bytes: block~3900  no cache
pad  2400 bytes: block~4101  CACHED (wrote 4098)
```

**The minimum for `claude-haiku-4-5` is 4096 tokens.** Measured for the others too, since the
documentation had already been wrong once:

| model | measured | published figure |
| --- | --- | --- |
| `claude-haiku-4-5` | **4096** (3900 no / 4101 yes) | 2048 |
| `claude-opus-4-5` | **4096** (3160 no / 6310 yes) | 1024 |
| `claude-sonnet-4-5` | **<= 1024** (caches at 1060) | 1024 |

**The lesson is about M340, not about Anthropic.** M340 shipped a check whose threshold came from
a documentation page, and it therefore emitted a confident `OK` -- *"~3897 tokens, over this
model's 2048-token minimum"* -- through precisely the failure it existed to catch. **A check is
only as good as its constant, and a constant taken on authority is not measured.** M341 replaces
the table with bracketed values and inverts M340's stated preference: where a family's minimum is
uncertain the **higher** figure is reported, because a warning you can dismiss costs less than a
reassurance that is wrong.

With the corrected table, doctor reports the anomalous config as
*"~3886 tokens and this model will not cache a block under 4096"* -- the warning that would have
saved the measurement round this whole page is about.

---

## 8. Deferred: what still needs a caching model (decided 2026-08-09)

**Operator decision: zigodot stays on the JLU models (`jlu/qwen3-coder-next`,
`jlu/gemma-4-31b-it`) with no caching.** The reasons are good ones -- a small demanding model is
the instrument that finds jichi's protocol defects (ANECDOTES #19), and switching the workload to
a tolerant frontier model would quietly stop that. So the caching work is parked, not abandoned,
and the debt is written down here rather than left in a conversation.

Each of these needs a caching-capable model and is **blocked until then**:

| # | What | Why it needs caching |
| --- | --- | --- |
| C1 | Field-validate M340's **OK arm** on a real workload | The WARN arm is validated (est. 1754 -> observed 1964, `cache_write=0`). The OK arm has only been seen on a synthetic prefix, never across a long run where the prefix grows |
| C2 | Decide whether **4096 is Anthropic's minimum or liteLLM's** | Needs a second caching endpoint to compare against. Until then the constant is "measured on one proxy on one day" and `jc_promptcache_min_tokens` says so |
| C3 | Minimums for **other generations** (haiku-3-5, opus-3, sonnet-3-7) | The table currently returns the 4.5 figure for a whole family, deliberately erring high. Each older id is one bracketing run |
| C4 | **`promptCacheTtl: "1h"`** (M31e) exercised end to end | Never run against a backend that caches at all |
| C5 | The **Context Epoch** design (opencode idea 2) | Its value case is a cost argument, and the cost is zero without caching. The diagnostics half could be built blind, but should not be |
| C6 | The **frontier craft A/B** | Deferred for a different reason -- it needs a frontier model, which the JLU-only decision also excludes. Still unblocked on credentials |
| C7 | Re-tune a **caching** zigodot config (prefix >= 4096) | Directly contradicts the current decision; keep the recipe in section 2 for whenever it is wanted |

**What does NOT need caching, and is therefore live work:** the M337-M341 features all function
identically without it (`preserveDiscarded`, `checkpoints gc`, `attempts`/`recover`, the M339
spill, `--dump-requests`); M340's check is silent for non-Anthropic providers by design, so it
cannot misfire on a JLU model; and the remaining measurement of whether the spill pays for
`search_code`/`git_*`/`fetch_url` is a driven-run question, not a caching one.

**One thing to keep straight while on JLU models:** `docs/TOOL_OUTPUT_COST.md`'s advice to trim
the prefix is **correct for this configuration** and stays primary. It inverts only on a caching
backend, and that page now says so.