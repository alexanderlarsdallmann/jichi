# Prompt caching

jichi reuses the cached prefix of a request — the system prompt, tool definitions,
and prior conversation — instead of having the model re-process it every turn.
On a multi-turn session this cuts both cost and latency: a cache **read** bills
at roughly 0.1× the normal input rate, and consecutive jichi turns are almost
always less than the 5-minute cache TTL apart, so the prefix stays warm. The
feature is **on by default**.

Two backends, two mechanisms:

- **Anthropic (Messages API)** — caching is *explicit*. jichi places
  `cache_control: {"type":"ephemeral"}` breakpoints on the stable prefix (M31b).
- **OpenAI-compatible (vLLM / LM Studio / OpenAI — jichi's primary backends)** —
  caching is *automatic / server-side*. jichi does not restructure the request;
  it only **reports** the cached-token counts the server already returns (M31a).

## What jichi caches (Anthropic)

The Messages API renders a request as `tools → system → messages`, and a
breakpoint caches everything up to and including the block it sits on. jichi emits
two breakpoints per request (well within the API's 4-breakpoint limit), decided
by the pure `jc_promptcache_plan` (`src/util/jc_promptcache.c`):

1. **System block.** The `system` string is sent as a one-element content-block
   array with `cache_control` on it. Because tools render *before* system, this
   single marker caches the tools + system prefix together — the part of the
   request that never changes within a session.
2. **Conversation tail.** The last message carries a breakpoint, so the entire
   prior history caches incrementally: turn *N* writes the cache at its tail, and
   turn *N+1* reads everything up to that point before writing a new tail
   breakpoint. (A plain-text message — whose content is a bare string — is
   promoted to a one-block text array so the marker has a block to ride on.)

This relies on the rendered prefix being **byte-stable across turns**, which jichi
already guarantees: the system prompt injects only cwd + platform (no timestamp),
and the repo map, rules, memory, skills catalogue, and tool array are
session-stable and built deterministically.

For backends whose minimum cacheable prefix isn't met (short sessions), the
markers are a silent no-op — no error, just no cache write.

## Configuration

Prompt caching is a tri-state, resolved per model. From lowest to highest
precedence:

1. **Global** `promptCache` (top-level config key) — `true` / `false`; default
   **auto = on**.
2. **Per-model** `promptCache` (in a `models[]` entry) — overrides the global for
   that model.
3. **CLI** `--prompt-cache` / `--no-prompt-cache` — overrides the global for the
   run.
4. **TUI** `/cache on` / `/cache off` — toggles it live for the session; bare
   `/cache` prints the current state and the last call's cached read/write and
   hit-rate.

```jsonc
{
  "promptCache": true,             // global default (optional; auto = on)
  "models": [
    {
      "provider": "anthropic",
      "model": "claude-opus-4-8",
      "promptCache": true          // per-model override (optional)
    }
  ]
}
```

Disabling makes the Anthropic request byte-for-byte the pre-caching shape
(system as a plain string, no `cache_control`). For OpenAI-compatible models the
flag controls only the `prompt_cache_key` (see below) — caching itself is
automatic server-side.

### Cache TTL (M31e)

By default the Anthropic breakpoints use the 5-minute ephemeral TTL. A global
`promptCacheTtl` selects it explicitly:

```jsonc
{ "promptCacheTtl": "1h" }   // "5m" (default) | "1h"
```

`"1h"` emits `cache_control: {"type":"ephemeral","ttl":"1h"}` so the cached
prefix survives gaps longer than 5 minutes — useful for an interactive session
where you pause to read, think, or run something between turns. The trade-off:
a 1-hour cache **write** costs ~2× input (vs ~1.25× for 5-minute), so it pays off
only when the warm prefix is read several times across those longer pauses.
Inert for OpenAI-compatible models (their caching is automatic).

### Cache pricing (cost accuracy)

Cached reads are cheaper than fresh input, and cache writes cost a little more.
To make jichi's cost numbers reflect that, add per-model rates (USD per 1M tokens):

```jsonc
{
  "provider": "anthropic",
  "model": "claude-opus-4-8",
  "inputCostPer1M": 5.0,
  "outputCostPer1M": 25.0,
  "cacheReadCostPer1M": 0.5,    // cached reads ~0.1x input
  "cacheWriteCostPer1M": 6.25   // cache writes ~1.25x input
}
```

When a cache rate is omitted it falls back to `inputCostPer1M`, so a config
without these keys is unaffected beyond the more-accurate split. `doctor` warns
when `promptCache` is on for a priced model that has no `cacheReadCostPer1M`.

### OpenAI `prompt_cache_key`

With caching on, the OpenAI-compatible request carries a stable per-session
`prompt_cache_key` (a UUID minted once per provider instance). It is harmless for
vLLM / LM Studio and nudges OpenAI's automatic caching to route the session to a
backend that already holds its prefix warm. No effect on Anthropic requests.

## Observing cache usage

jichi surfaces the cache-token counts both backends report (M31a):

- **TUI** — the per-message token line gains a `cached=N` suffix when the
  provider reported a cache hit (so non-caching backends read exactly as before):

  ```
  [tokens in=5120 out=180 cached=4608]
  ```

- **Telemetry** — the `model_call` event carries `cache_read_in` (input tokens
  served from a cache hit) and `cache_write_in` (tokens written to the cache;
  Anthropic only — OpenAI-compatible servers report reads only). See
  [TELEMETRY.md](TELEMETRY.md).

> **`cached=0` does not prove there is no cache.** An OpenAI-compatible
> backend may cache server-side and report nothing: measured 2026-08-11 on a
> LiteLLM→vLLM proxy, an identical 18.9k-token prefix prefilled in 2.97s cold
> and 0.16s on the next call (18.6×) while the usage carried no
> `prompt_tokens_details` at all. On such a backend the win is **latency**,
> invisible to jichi's counters and cost lines — so judge caching by repeat-call
> latency before concluding it is off, and keep the prefix byte-stable (which
> jichi already guarantees, M31d) even when `cached=` reads zero. The latency
> tell is mechanized: `python3 tests/bench/cache_probe.py --url <endpoint>
> --model <id>` times a cold call against warm repeats of a byte-identical
> prefix and gives an asymmetric verdict — it can prove a cache live, never
> prove one absent (M378).

- **`telemetry` subcommand** — the per-model summary adds a cache line with the
  read/write totals and a **hit-rate** (cached reads over total input tokens),
  shown only when caching was observed:

  ```
  claude-opus-4-8           calls=12 err=0  in=8400 out=2100  cost=$0.0840  ...
                              cache read=42000 write=900  hit-rate=83.3%
  ```

**If the hit-rate is zero across repeated turns**, a silent prefix invalidator is
at work — verify the system prompt and tool list aren't changing between turns.
Note that switching models mid-turn (the M20 fast↔strong routing) invalidates the
model-scoped cache, so a routed turn writes its prefix fresh on the new model.

## Internals

- `jc_promptcache_plan(enabled, n_messages, &plan)` — pure, unit-tested
  (`tests/test_promptcache.c`); returns `cache_system` + a tail `msg_index`.
- The Anthropic provider (`src/provider/jc_provider_anthropic.c`) wires the plan
  into the request JSON, gated on `model->prompt_cache`.
- Cache-token parsing: Anthropic reads `cache_read_input_tokens` /
  `cache_creation_input_tokens` from the `message_start` usage; OpenAI-compatible
  reads `prompt_tokens_details.cached_tokens` and subtracts it from
  `prompt_tokens` so the reported input is the uncached (full-price) remainder,
  matching Anthropic. Both feed `get_cache_usage` on the provider vtable.
- `jc_config_cost(m, in, out, cache_read, cache_write)` bills cached reads/writes
  at `cache_read_cost` / `cache_write_cost`, each falling back to `input_cost`.
  The autonomy-envelope budget meters `in + cache_read + cache_write` (the full
  input processed).

## When the backend does not cache

`jichi doctor` reports a 0% hit-rate since M326w, and
`jichi telemetry --cache-audit` breaks it down per model and per session. Without
a cache, every tool result in the history is re-billed on every subsequent call
of the turn — a multiplier of tens to hundreds.
[TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) measures that cost and lists the
levers, in order of measured effect.

## Measured on a real proxy: what it takes to actually get a hit (2026-08-09)

Prompt caching is a **configuration**, and it has a floor that fails silently. Measured against
the HRZ liteLLM proxy with `provider: "anthropic"` and an `anthropic/claude-haiku-4-5` model:
**94.9% hit rate over 14 calls, 84.1% cheaper on input**, with the uncached remainder at 6
tokens per call.

The first attempt measured **zero**, with `promptCache: true` on the same model. The cause:
**Anthropic will not cache a block below its minimum — measured at 4096 tokens for Haiku 4.5 and
Opus 4.5, 1024 for Sonnet 4.5** (the widely published 2048/1024 figures are an older generation's
and are wrong here) — and the config had trimmed the system prefix to 397 tokens, which is the right
move on an uncached backend and precisely wrong here.

So the two regimes invert, and a config tuned for one is mistuned for the other:

| | uncached backend | caching backend |
| --- | --- | --- |
| a big prefix (rules, repo map, craft) | billed **every call** | billed ~1.25x **once** |
| trimming the prefix | the main cost lever | may push you under the minimum and lose everything |

`sys_tok` on every `model_call` telemetry event is the number to check. Nothing warns yet when a
prefix is too small to cache while caching is on.

Two further findings worth knowing: the cache is keyed on **content, not session**, so a
supervisor running many short invocations over one workspace gets hits from its first call; and
a short run benefits least (a 2-call run measured 34.1%, since the first write cannot amortise).

Full measurement: [analysis/2026-08-09-hrz-prompt-caching.md](analysis/2026-08-09-hrz-prompt-caching.md).
## Prefix caching has a BLOCK SIZE, and below one block you get nothing (M590)

Measured 2026-08-25 against two deployments of the same gateway, four calls each. Cached tokens
are always a whole multiple of a block; the partial final block is never cached:

| deployment | block | cached, for a 2.2k / 4.4k / 8.9k-token prefix |
|---|---:|---|
| A | **128** | 2,176 / 4,352 / 8,704 — i.e. 17 / 34 / 68 blocks |
| B | **1,568** | 1,568 / 3,136 / 7,840 — i.e. 1 / 2 / 5 blocks |

The uncached remainder is exactly `prompt mod block` (688, 1,320, 1,016 on B — all three
exact), which is why B's hit rate *climbs with prefix length*: **69.5% at 2.2k, 88.5% at 8.9k**,
while A sits at 98%+ from the start.

**Stated as a prediction and then tested**, because a model fitted to three points and asserted
is not a measurement:

    prompt=606   cached=0      predicted 0      MATCH
    prompt=1376  cached=0      predicted 0      MATCH
    prompt=1596  cached=1568   predicted 1568   MATCH   <- the boundary
    prompt=2256  cached=1568   predicted 1568   MATCH

**What this means when you size a system prompt.** A stable prefix shorter than one block buys
**nothing at all** — a lean subagent prompt can be entirely uncached on a backend where the main
agent's prompt is 98% cached, and the two look identical in every other respect. If your hit
rate is surprisingly low, measure at two prefix lengths before concluding the cache is off: a
single short sample cannot distinguish "no caching" from "under one block".

**And two blocks sizes differing by 12× is a per-deployment property**, not a per-model one.
Deployments behind one gateway do not share it.

**What caching does NOT do:** cached tokens still **occupy the context window**. Caching makes a
prefix *cheap*, not *smaller*. Compaction pressure, mid-turn eliding and the fit budget are
untouched by a cache hit — see M588, where a 93%-unrelieved workload had nothing to do with
cost. Cost and window occupancy are different axes and conflating them undoes both analyses.

## The prefix sentinel, and the fit-budget deadband (M365)

The byte-stability this page depends on was a contract upheld by every
system-prompt section author by hand, with no runtime checker — and it had a
live violator: the M77 calibration ratio moves a little on every model call,
the M73 fit caps derive linearly from the deflated budget, and on any project
whose rules + repo map exceed that budget, **every ratio wobble moved the
truncation byte**. The system prompt churned each turn; the cached prefix —
the largest span in every request — re-billed forever, silently.

Two halves close it:

- **The deadband** (`jc_sysmsg_fit_budget`): the deflated fit budget is HELD
  through jitter until the raw value drifts a full band away (limit/8, capped
  at 1024 tokens, floor 64), then re-fits once and holds there. A stateless
  quantization was the first design and only relocates the problem — a value
  near a step boundary still flaps on tiny jitter. Holding through a
  drift-down can over-fit by at most one band; bounded, and stated.
- **The sentinel** (`jc_prefix_watch`, checked at every top-level turn): if
  the system prompt's hash changes on **3 consecutive turns**, jichi warns
  once per session (`[prefix] …`) and emits a `prefix_churn` telemetry event.
  Deliberately slow to accuse: one change is normal life (a memory write, a
  mode switch, midnight moving the date line) — no legitimate cause fires
  every turn. A real per-turn churn pattern the sentinel catches: an agent
  told to `remember` a note on every step refreshes the memory section every
  turn (`tests/smoke/prefix_churn.sh` drives exactly that).

Symptoms to check when it fires: `/cache` and the `cached=` token line near
zero, `telemetry`'s per-model hit-rate line collapsed.

## Caching is a property of the DEPLOYMENT, not the model (2026-08-24)

Four chat models on one gateway, one API, one client. Two cached at **98%** and two cached
**nothing** — and the two that worked shared a backend deployment while the two that failed sat
on two others:

| model | backend | prompt tokens | cached | hit |
|---|---|---|---|---|
| A | deployment 1 | 2426 | 2400 | **98%** |
| B | deployment 1 | 2426 | 2400 | **98%** |
| C | deployment 2 | 2661 | **0** | **0%** |
| D | deployment 3 | 2700 | **0** | **0%** |

**So when some of your models cache and others do not, look at which backend serves each one
before suspecting the model or the client.** The gateway's own response headers usually name it
(`x-litellm-model-api-base` on a LiteLLM proxy). This was a partly-completed rollout: the setting
had been enabled on one deployment and not the others.

Verified as real **prefix** caching rather than exact-match — a longer prompt sharing only the
system prefix still hits (2400 of 2439) — and it survives a `tools` array and streaming with
`stream_options.include_usage`, which is the shape an agent actually sends.

### The warm-up trap, which is worth more than the table

On a healthy backend the **first** call to a cold prefix returns the *inverse* of a hit:

```json
"prompt_tokens_details": { "cached_tokens": 0, "created_cache_tokens": 2400 }
```

The cache is being **written**; only the second and later calls read it. **A two-call test
therefore catches a working system mid-warm-up and looks like a failure.** That produced two
false conclusions during this measurement — first that a healthy model "writes but never reads",
then that *jichi itself was not using caching at all* — and both dissolved on a four-call re-run.

**Send at least three identical requests before concluding anything**, and read the write counter
as well as the read counter:

| observed over ≥3 calls | meaning |
|---|---|
| first call writes, later calls read | working |
| every call writes, never reads | check the TTL, or you are hitting different replicas |
| **never writes, never reads** | **not enabled on that backend** |

`created_cache_tokens` is not in the OpenAI specification; a gateway that omits it leaves you
with the [latency probe](#observing-cache-usage) above as the only tell.

**And hits are not universal even where it works.** Over four consecutive turns against a caching
model, three hit at ~99% and one missed entirely. Quote a range, not a number.

### Do not confuse it with response caching

Some gateways can also cache **the reply** — returning a stored answer without running the model.
Different feature, different consequences, and it would make a prompt-cache measurement look in
the wrong place entirely. To tell them apart, send the same request several times at
`temperature: 1.0`: **varying answers mean there is no response cache**, whatever the
`cached_tokens` figures say.
