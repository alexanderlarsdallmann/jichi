# Using an institutional gateway

*Companion to [Module 0](00-a-working-bench.md) · map:
[CURRICULUM.md](../CURRICULUM.md)*

Many universities and companies run an **LLM gateway**: one OpenAI-compatible
endpoint in front of the models the institution hosts or licenses, with a
personal API key per member. If yours does, the bench setup is one config
pattern — this page — and everything else in the curriculum is identical to
the bring-your-own-key and local-model paths.

## The pattern

A gateway gives you three facts: a **base URL**, a **key**, and the **model
ids** it serves. They map onto one model entry each:

```jsonc
{
  "models": [
    { "name": "gateway-chat",
      "provider": "openai",                        // OpenAI-compatible wire
      "model": "<the id your gateway lists>",
      "apiBase": "https://<your-gateway>/v1",
      "apiKeyEnv": "LLM_API_KEY",                  // an env-var NAME, never a key
      "roles": ["chat", "edit", "apply"] }
  ]
}
```

Two rules that never change:

- **The key lives in the environment, not the file.** `apiKeyEnv` names
  whichever variable you put it in (`export LLM_API_KEY=...` in your shell
  profile). `doctor` warns if a literal key ever appears in a config.
- **The env-var name is a placeholder.** Curriculum pages say `LLM_API_KEY`;
  your institution's docs may say something else. Any name works — it only
  has to match what you exported.

Validate with `jichi doctor` (it probes reachability per server) and
`jichi models` (lists each configured model with live reachability).

## Worked instance: the JLU HRZ gateway

For members of Justus Liebig University Gießen, the HRZ runs a LiteLLM
gateway. **Request a personal key from the HRZ** (course participants are
typically handed one in class), export it, and use:

| Fact | Value |
|---|---|
| Base URL | `https://api.hrz.uni-giessen.de/v1` |
| Context windows | `GET /v1/model/info` -- a LiteLLM extension; `/v1/models` does not carry them. `jlu/qwen3.8-27b`: `max_input_tokens` **196608**, `max_output_tokens` 65536, so declare `"contextLength": 196608`. `jichi doctor` checks this against the server for you. |
| Key env var (by convention here) | `JLU_API_KEY` |
| General model | `jlu/gemma-4-31b-it` — chat/edit/apply |
| Coding model | `jlu/qwen3-coder-next` — strong tier for routing |
| Embeddings | `jlu/qwen3-embedding` — the `embed` role (codebase search) |

The ready-made config is
[`examples/config.multi-server.json`](../../examples/config.multi-server.json)
(secret-free; it also wires a local fallback), walked through in
[CONFIG_TUTORIAL.md](../CONFIG_TUTORIAL.md) §11. Model ids are the gateway's
wire values — `jlu/…` is literal, not a placeholder.

### The cost fact worth knowing: no prompt caching

Measured across 967 real calls (~96.7M input tokens): the HRZ gateway
**reports zero prompt-cache hits** — jichi parses the cache fields
unconditionally and never saw one (`docs/analysis/zigodot-jichi-review.md`
F3). On a caching backend, the long repeated prefix of an agent conversation
(system prompt, tools, history) is re-billed at a heavy discount; here it is
re-processed **at full cost every call**, so input volume — not output — is
what grows with a long session. Practical consequences on this gateway (and
any non-caching one):

- Long sessions cost linearly in history size: **`/compact` earlier** than
  you otherwise would, and prefer fresh sessions per task over one epic
  session ([COMPACTION.md](../COMPACTION.md)).
- Setting `promptCache` changes nothing here — the machinery is correct but
  the backend has nothing to cache against. Don't chase it.
- `jichi telemetry` shows your per-session input ramp (`peak_in`) if you want
  to see the effect in your own numbers ([TELEMETRY.md](../TELEMETRY.md)).

For a class, this is also the instructor's budgeting fact: seat cost scales
with how long students let sessions grow, and teaching `/compact` and
per-task sessions in Module 0 is cheaper than any quota.

## No gateway? No problem

The other two forks of Module 0 are first-class: any OpenAI-compatible
provider key, or a local model with no key at all
([LOCAL_MODELS.md](../LOCAL_MODELS.md)). The curriculum never depends on
which fork you took.
