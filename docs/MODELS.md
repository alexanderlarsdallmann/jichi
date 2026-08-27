# Models, servers & fallback

jichi talks to any number of **OpenAI-compatible servers** at once. Each
entry in the config's `models` array names a server (`apiBase`) and a model, and
the agent selects among them by **role**, by **routing tier**, by an explicit
`spawn_subagent`/`spawn_parallel` model argument, or by `/model`. Servers are
**health-checked**, and a model can declare a **fallback** to use when its server
is unreachable — so a config that mixes a reliable remote with an
occasionally-running local server (e.g. LM Studio) "just works" either way.

## The smallest config that works

Three keys, one file, and two commands to prove it. Start here before reading
anything else on this page.

`~/.jichi`:

```json
{
  "models": [
    { "name": "main", "provider": "openai", "model": "your-model-id",
      "apiBase": "https://your-server/v1", "apiKeyEnv": "JICHI_API_KEY",
      "roles": ["chat"] }
  ]
}
```

That is the whole minimum: **where** the server is (`apiBase`), **which** model
(`model`), and **how to authenticate** (`apiKeyEnv` naming an environment
variable — never the key itself in the file). `name` is your label for it and
`roles: ["chat"]` says this one answers conversations.

Now check it, in this order:

```sh
jichi models
jichi doctor
jichi -p "Reply with the single word: ready."
```

- **`jichi models`** lists what the config declares and which role each entry
  serves. If your entry is missing here, the config did not parse — `jichi config validate`
  will say why.
- **`jichi doctor`** goes further: it resolves the config, checks the key is
  present in the environment, and reports whether the server is reachable. A
  warning about a missing context length or missing pricing is normal for a local
  or gateway model; a *failure* here means the run would fail too.
- **The one real call** is the only thing that proves tool-free chat works
  end-to-end. If you want to know whether *tool calling* works — a different
  question, and the one that actually decides whether a small model is usable —
  run `jichi doctor --live`.

**Then add exactly one thing at a time**: a `contextLength` (so the budget is
enforced by jichi rather than discovered as an HTTP 400), a second entry with
`roles: ["embed"]` if you want semantic search, a `fallback` if your server is
sometimes down. Every one of those is documented below, and none of them is
needed to start.

## A model entry

```jsonc
{
  "name": "local-gemma",          // selector-friendly display name
  "provider": "openai",           // "openai" (any OpenAI-compatible server) | "anthropic"
  "model": "google/gemma-4-e4b",  // the server's model id
  "apiBase": "http://127.0.0.1:1234/v1",
  "apiKeyEnv": "SOME_ENV",        // or "apiKey": "..."; omit for keyless local servers
  "roles": ["chat", "embed", ...],// chat|edit|autocomplete|embed|rerank|summarize|apply|image|audio|transcribe
  "fallback": "jichi-gemma",        // model selector to use if this server is down
  "temperature": 0.2, "maxTokens": 0, "contextLength": 8192,
  "inputCostPer1M": 0, "outputCostPer1M": 0,
  "promptCache": true,            // emit Anthropic cache_control breakpoints (default on); see PROMPT_CACHING.md
  "cacheReadCostPer1M": 0, "cacheWriteCostPer1M": 0, // optional; fall back to inputCostPer1M
  "toolCalling": "native"         // "native" (default) | "none"; "text" reserved (M149)
}
```

**`toolCalling`** (M149) declares whether the model can emit native tool calls.
`"native"` (the default) is the normal agentic path. `"none"` tells jichi **not
to advertise any tools** to this model — it runs as a Q&A / plan agent
(the skills catalog and all prompt-side context still load), instead of the
silent no-op an unaware non-tool-calling model would otherwise produce; a
one-time notice explains the degraded posture, and `doctor` warns if such a
model is paired with autonomy (`verify`/`testCommand`/routing) that needs
tools. `"text"` is **reserved** for a future prompt-based fallback and is read
as `"native"` today (with a warning). Most 7–14B coder models have native tool
calling — see [LOCAL_MODELS.md](LOCAL_MODELS.md) and
[proposals/2026-07-small-model-agentics.md](proposals/2026-07-small-model-agentics.md).

A **selector** (used by `fallback`, `routing`, `/model`, `--model`, and the
spawn tools) is a model **name** or **model-id** substring (case-insensitive), a
**1-based index**, or a **role** name.

### `--model` resolution (and why it works this way)

`--model <sel>` resolves in two tiers:

1. **Select a configured model.** `<sel>` is matched against your `models` list
   with the selector rule above (`jc_config_find_model`). On a match, that entry
   becomes active — so its **name, model id, apiBase, API key, and roles** all
   switch together, and the prompt/header show the model that actually answers.
   This is the same matching `models`, `embed`, and `rerank` use, so `--model`
   behaves consistently across the tool.
2. **Raw-id fallback.** If `<sel>` matches no configured entry, it is used as a
   raw backend model id on the *active* entry (keeping that entry's apiBase and
   key), and a `[model] … matches no configured model …` note is printed to
   stderr.

*For a beginner:* normally just pass a model's name (or part of it) — e.g.
`--model qwen` — and jichi uses that configured model. *Design rationale (for the
curious / advanced reader):* tier 1 fixes an old footgun where `--model`
unconditionally overwrote only the active entry's `model` id — so `--model qwen`
would keep the wrong name/apiBase/key while silently sending a different id (the
prompt still said "gemma"). Tier 2 is deliberately kept as an escape hatch: it
lets a power user point at a model the config doesn't list *yet* but that the
active entry's server can serve (e.g. trying a new id on the same endpoint),
without editing the config — and the stderr note makes that intent explicit
rather than silent.

### `apiBase` and `/v1` (important)

Chat resolves `{apiBase}/chat/completions` when `apiBase` already contains `/v1`,
else `{apiBase}/v1/chat/completions`. Embeddings/rerank/the health probe behave
the same way. **The simplest correct form is to end `apiBase` in `/v1`** (e.g.
`http://127.0.0.1:1234/v1`, `https://host/v1`) — then chat *and* embeddings
resolve correctly. A base without `/v1` also works (the `/v1` is inserted).

## Reachability-checked fallback

When a model declares a `fallback` and is actually used, jichi probes its
server (a short-timeout `GET {apiBase}/models`; **any** HTTP answer — even
401/404 — counts as reachable, only a connect/timeout failure is "down"). If the
server is down, it walks the `fallback` chain (cycle-bounded) to the first
reachable model and logs e.g.:

```
[fallback] local-gemma unreachable -> jichi-gemma
```

- Probing is **opt-in**: a model with no `fallback` is never probed, so there's
  zero added latency unless you use the feature. Results are cached per server.
- Fallback applies to: the **startup active model**, the **routing tiers**, and
  **role** lookups (embed/summarize/rerank). (An explicit `spawn_subagent
  model=...` is the agent's deliberate choice and is not auto-fallen-back in this
  version.)
- If every model in a chain is down, the original is kept and the request fails
  /retries as usual — there's no silent hang.

> The `autocomplete` role selects the model for the one-shot completion
> surfaces (M9): the `complete` and `fim` subcommands and the TUI's Ctrl-G
> ghost-text suggestion (see [`AUTOCOMPLETE.md`](AUTOCOMPLETE.md)). When no
> model declares it, those surfaces fall back to the active model.

> The `image` and `audio` roles (M32) select the backends for the
> `generate_image` and `generate_audio` tools — a model with `roles:["image"]`
> serves the OpenAI-compatible `/v1/images/generations` endpoint, one with
> `roles:["audio"]` serves `/v1/audio/speech`. The tools are registered only
> when the corresponding role is present. See [`MEDIA_GEN.md`](MEDIA_GEN.md).

> The `transcribe` role (M33) selects the backend for the `transcribe_audio`
> tool — a model with `roles:["transcribe"]` serves the OpenAI-compatible
> `/v1/audio/transcriptions` endpoint (speech-to-text). See
> [`TRANSCRIBE.md`](TRANSCRIBE.md).

Check it anytime:

```sh
jichi models     # lists each model with roles, fallback, and a live
                        # [reachable] / [UNREACHABLE] probe of its server
```

## Routing + fallback together

This is the intended pattern for "use the local server when it's up": set the
routing `fast` tier to the local model with a `fallback` to the remote, and the
`strong` tier to the remote. Routine turns run locally when LM Studio is up
(`[route] -> local-gemma`); when it's down they transparently run on the remote
(`[fallback] local-gemma -> jichi-gemma`); hard turns escalate to `strong` either
way. See [ROUTING.md](ROUTING.md). The small local model is further constrained
by the [autonomy envelope](AUTONOMY.md) (verify/budget) and routing escalation.

## Where the config lives

**Explicit** `--config <path>` or `$JC_CONFIG` loads exactly that one file (a
predictable override — no merging). Otherwise jichi **merges** the global config
`~/.jichi` with a project config (`./local/config.json`, else
`.jichi/config.json`): the project **overlays** the global — scalar keys (e.g.
`contextLimit`) take the project's value, and list keys (`models`, `docs`,
`aliases`, `lspServers`, …) are **unioned with the project's entries first** (so a
project model/alias takes precedence and is the default active). Either file alone
works; `doctor` prints the resolved "config source(s)". So global defaults (your
providers/keys) live in `~/.jichi` and a project adds only what's specific
to it.

The project half is found **relative to the directory you start jichi in** —
there is deliberately no walk up to a git root, because the invocation cwd *is*
jichi's workspace everywhere else too (`.jichi/memory.md`, snapshots, the path
fence, the tools). Start jichi from the project root; started in a
subdirectory, only `~/.jichi` applies — if you see the wrong model active,
check where you launched from before checking the config (M379).

`examples/config.multi-server.json` is a secret-free template (keys via
`apiKeyEnv`) — copy it to `local/config.json` and adjust.

## Mixed providers, keys, and specialists

Every model entry is self-contained: its own `provider` (anthropic/openai-
compatible), `apiBase`, key (`apiKeyEnv` → an env-var name, or a literal
`apiKey`), and `roles`. So one config can freely mix providers and distinct keys
— e.g. an Anthropic model on `$ANTHROPIC_API_KEY`, an OpenAI-compatible one on
`$OPENAI_API_KEY`, and a keyless local server. Switching model (`/model`, routing,
fallback) rebuilds the provider against that model's own base + key.

**Specialists for orchestration** are just **named agent profiles**
(`.jichi/agents/<name>.md`) with a `model:` in their frontmatter — a name/id
substring, a 1-based index, or a **role** name — plus `spawn_subagent`/
`spawn_parallel`'s `model:` argument. So a "deep-reviewer" profile can pin the
strong model and a "fast-coder" the fast one, each with its own provider + key,
and the orchestrator delegates to them by name (see `docs/SUBAGENTS.md`).

`doctor` audits **every** model's key (not just the active one) — a specialist or
fallback model with no key would otherwise 401 only once orchestration switches to
it. A keyless entry is fine for a local server; the warning just names the models
so a mixed setup is debuggable up front.

## Model-call timeouts

A model call is bounded so a stalled or unreachable server can't hang the agent
forever (M22). Three knobs, all in **seconds**:

- `connect` — TCP connect timeout (fail fast on an endpoint that won't accept the
  connection). Default **10**.
- `stall` — abort a response stream whose throughput stays at ~0 bytes/s for this
  long. This is the key control: it kills a **frozen** stream (a hung local
  model) **without** capping a slow-but-progressing generation. Default **30**.
- `request` — a hard overall cap on the whole call. Default **0** (off); set it
  only if you want a strict wall-clock limit.

`0` (or `off` on the CLI) disables a tier. Set them globally and/or per-model — a
slow local model can get a longer `stall` than a fast hosted one:

```json
{
  "timeouts": { "connect": 10, "stall": 30 },
  "models": [
    { "name": "local-gemma", "model": "google/gemma-4-e4b",
      "apiBase": "http://127.0.0.1:1234/v1",
      "timeouts": { "stall": 90 } }
  ]
}
```

The effective value resolves by precedence **CLI > per-model > global > built-in
default** (`--timeout-connect`/`--timeout-stall`/`--timeout-request <s|off>`
override for one run). A timeout is treated as a transient error, so the normal
retry/backoff applies and the [telemetry](TELEMETRY.md) records it
(`model_call result:"timeout"` → `model_retry`). The `timeouts` subcommand prints
the resolved values for the active model (also `/timeouts` in the TUI).

### Which timeout is biting you (measured, M321)

The two timeouts fail in ways that look identical and want opposite fixes, so this is worth
recognising:

| Symptom | Cause | Fix |
|---|---|---|
| failures clustered at **exactly** the connect limit, `result: "error"`, `status: 0` | the endpoint never accepted the connection — **the request was never sent** | raise `timeouts.connect` |
| failures after a long wait, `result: "timeout"` | connected, then the stream went quiet | raise `timeouts.stall`, or let [routing](ROUTING.md) escalate |

**The signature to look for is a cluster at the limit value.** In one measured workload, 2,402
of 16,075 model calls (15%) failed with latencies inside 2 ms of exactly 10,000 ms — the
default `connect` of 10 s — while the *successful* calls had a p90 of 9,942 ms. That
distribution is not flakiness; it is a limit being hit by the tail of a normal latency spread.
It cost **6.5 hours of wall clock**, and the operator had raised `stall` (to 90 s) because
nothing pointed at `connect`.

Since M321 jichi says which one fired: the error names the knob, and the telemetry event
carries a `transport` field that the `telemetry` summary reports. **On a shared or remote
endpoint, `connect: 10` is optimistic** — the endpoint's accept queue is what you are waiting
on, and it lengthens under exactly the load your own workload creates. The same measurement
found two sessions of 3,968 and 1,674 calls with *zero* connect failures, so this is the
endpoint's load rather than anything local: raise the limit rather than hunting a bug.

When [routing](ROUTING.md) is configured, a stall on the **fast** tier escalates
to the **strong** tier and the turn recovers there rather than erroring
(`escalateOnStall`, default on) — the intended answer to "the small local model
froze on the hot path".

## Choosing a model for a fast-moving toolchain

A model's training data has a centre of mass at some version of your language and
libraries, and on a toolchain with breaking changes that shows up as code which is
internally consistent, confidently written, and two years out of date. Putting the
version number in your rules does **not** fix it: a version label only helps a reader who
already knows what changed. What works is a table of old form → new form.

How to detect it, how to measure whether a fix landed (harder than it looks -- the old
API appears in your own brief, in compiler errors, and in the `old_string` of a *correct*
fix), and what it costs:
[`MODEL_TOOLCHAIN_DIALECT.md`](MODEL_TOOLCHAIN_DIALECT.md).

The learner-facing tutorial on the whole choice — tiny/small/large tiers, coding vs
documentation tasks, and the version probe that must precede trusting a model with a
language: [`CHOOSING_A_MODEL.md`](CHOOSING_A_MODEL.md).

## Caveats

- **The index is embedding-specific.** Its manifest records the embedding model;
  changing the embed model (including a fallback to a different one) triggers a
  full re-embed on the next `index`/`codebase_search`. Switching embed servers is
  therefore correctness-safe but can be costly — pin one embed model per run when
  you can, and run `jichi index --reindex` after deliberately changing it.
- **Keyless local servers**: omit `apiKey`/`apiKeyEnv`; no `Authorization`
  header is sent. (A "no API key" warning may print for the active model; it is
  harmless for a local server.)

## Implementation

`fallback` lives on `struct jc_model_cfg`; the pure, unit-tested
`jc_config_fallback_chain` walks it over a reachability array.
`jc_net_reachable` (`src/net/net_util.c`) is the probe; `jc_app_effective_model`
/ `jc_app_model_for_role` (`src/chat/jc_app.c`) cache reachability per server and
resolve the chain (used at startup, in routing, and for roles). The `models`
subcommand and the `local/config.json` precedence live in `src/main.c` /
`src/config/jc_config.c`.

Timeouts live on `struct jc_timeouts_cfg` (global, per-model, and the CLI tier on
`struct jc_config`); the pure `jc_config_resolve_timeouts` picks the effective
values, `stream_once` (`src/chat/jc_agent.c`) applies them to the request, and
`apply_common` (`src/net/jc_http.c`) maps them onto curl's
`CONNECTTIMEOUT`/`LOW_SPEED_*`/`TIMEOUT`. The built-in defaults are
`JC_HTTP_*_DEFAULT` in `include/jc_http.h`.
