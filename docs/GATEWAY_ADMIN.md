# Configuring an LLM gateway for agentic coding tools

*What an agent needs from a proxy that a chat UI does not, why each item matters, and a
command to verify it. Written from measurements against one liteLLM deployment, but the
requirements are vendor-neutral: they apply to liteLLM, LiteLLM-compatible gateways, vLLM
behind a router, or a hand-rolled proxy.*

*The findings were reported to that gateway's operators privately and **that report is not
published** — it describes another organisation's configuration. This page is the part that
generalises, and
[analysis/2026-08-09-hrz-gateway-findings.md](analysis/2026-08-09-hrz-gateway-findings.md)
keeps what the same measurements taught **jichi**, in full.*

Audience: whoever administers the gateway. Nothing here requires jichi — the checks are
`curl` and the reasoning applies to any agentic tool.

---

## Why an agent is a different client from a chat UI

A chat UI sends one request per human message and shows the answer. An agent sends **30-50
requests for one task**, each carrying the whole conversation plus tool output, holds a
streaming connection open for minutes at a time, and makes automated decisions from the
metadata your gateway returns. Metadata a chat UI ignores is load-bearing for an agent:

- it sizes its own context management from the declared window;
- it decides whether to advertise tools from the declared capability;
- it enforces spending limits from the declared price;
- it meters progress from the usage numbers on the stream.

Where those are absent, an agent does not fail loudly. It guesses, and the guesses are wrong
in ways that look like model quality problems.

## The eight things to get right

### 1. Declare the real context window

**Why.** An agent summarises or trims history when it approaches the window. Too low a
figure and it compacts constantly, losing information it needs and paying for summarisation.
Too high and every long turn dies on an HTTP 400 the user reads as a crash. Absent, and the
client falls back to a built-in default — commonly 8k or 32k — regardless of the truth.

**Verify.**
```sh
curl -s -H "Authorization: Bearer $KEY" $BASE/model/info \
  | python3 -c "import json,sys
for m in json.load(sys.stdin)['data']:
    i = m.get('model_info') or {}
    print(m['model_name'], i.get('max_input_tokens'))"
```

Any `None` is a model whose clients are guessing. In liteLLM this is
`model_info: {max_input_tokens: N}` per entry; vendor models inherit it from the built-in
cost map, **locally-registered models do not and must be set by hand.**

### 2. Declare whether the model does native tool calling

**Why.** This is the single most consequential flag for a coding agent. If a model cannot
emit structured tool calls, an agent that advertises tools gets **prose describing a tool
call** instead — text like "I'll now read the file" with no call attached. The agent stalls,
retries, and burns the budget, and every symptom points at the model rather than at the
configuration.

**Verify** with a real request that advertises one trivial tool, and look at whether the
answer contains `tool_calls`:
```sh
curl -s -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" -d '{
  "model":"'"$MODEL"'","max_tokens":64,
  "messages":[{"role":"user","content":"List the files in the current directory."}],
  "tools":[{"type":"function","function":{"name":"list_files",
    "parameters":{"type":"object","properties":{"path":{"type":"string"}}}}}]
}' $BASE/v1/chat/completions | head -c 400
```
Three outcomes worth distinguishing: a `tool_calls` array (native — good), prose mentioning
the tool (text-shaped — the agent must parse heuristically, if it can), and prose ignoring it
(none). Set `model_info: {supports_function_calling: true|false}` accordingly, and **publish
the list** — users cannot infer it.

### 3. Populate pricing, even if nobody is billed

**Why.** An agent's spending controls are all derived from price. With cost per token unset,
every readout is `$0.00`, a budget cap cannot be expressed in money, and the operator loses
the one number that makes an unattended run reviewable. `0` is a legitimate value and is very
different from absent: absent means "unknown", and a careful client will say so rather than
claim free.

### 4. Say which models cache prompts, and what the minimum block is

**Why.** Prompt caching is the difference between paying for the prefix once and paying for
it on every one of 50 calls — measured at **84% of input cost** on one workload. But caching
has a **minimum block size, below which the request caches nothing and reports nothing**: no
error, no warning, just a cache-hit count of zero.

**Measured minimums** (bracketed against the wire, and they differ from the published
figures for these model versions):

| model | minimum cacheable block |
| --- | --- |
| `claude-haiku-4-5` | **4096 tokens** |
| `claude-opus-4-5` | **4096 tokens** |
| `claude-sonnet-4-5` | ≤1024 tokens |

Publish these per model. A user who trims their prompt to save money can silently cross
below the line and pay *more*.

### 5. Expose the vendor-native endpoint, not only the OpenAI shape

**Why.** Anthropic-family caching needs explicit `cache_control` breakpoints, which have no
place in the OpenAI chat-completions schema. A gateway that offers only `/v1/chat/completions`
gives its users **no way to request caching at all** for those models.

**Verify** that the passthrough exists and preserves the markers:
```sh
curl -s -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -H "anthropic-version: 2023-06-01" -d '{
  "model":"'"$MODEL"'","max_tokens":8,
  "system":[{"type":"text","text":"'"$(python3 -c 'print("padding sentence. "*900)')"'",
             "cache_control":{"type":"ephemeral"}}],
  "messages":[{"role":"user","content":"Reply OK."}]}' $BASE/v1/messages \
  | python3 -c "import json,sys; u=json.load(sys.stdin)['usage']; print(u)"
```
A non-zero `cache_creation_input_tokens` on the first call and a matching
`cache_read_input_tokens` on an identical second call is the proof. Zero on both, with a
block over the minimum, means something between the client and the model is dropping the
marker.

### 6. Do not rewrite requests in ways that break a cached prefix

**Why.** Caching is keyed on an exact byte prefix. A gateway that reorders JSON keys, injects
a header field, renumbers tool definitions, or rewrites the system block per request will
produce a cache miss on **every** call while reporting nothing wrong. If your gateway
normalises requests, keep that normalisation deterministic for identical input.

The same applies to tool arrays: agents rely on the advertised tool list being stable across
a run.

### 7. Size timeouts and rate limits for bursts and long streams

**Why.** One agent turn is a single streaming response that can run for minutes — a large
model thinking, or a long generation. An idle timeout on a load balancer that is reasonable
for a chat UI (30-60s of silence) kills agent runs mid-answer, and the client sees a
truncated response it cannot distinguish from a model that stopped early. Meanwhile the
*request rate* is bursty: 30-50 calls back to back, then nothing for an hour.

Recommend, and document, three numbers: the maximum time a response may stream, the idle
timeout between bytes, and the per-key request rate. A client that knows them can be
configured to fail fast rather than hang.

### 8. Keep the auxiliary roles working: embeddings, rerank, transcription

**Why.** Retrieval features in coding tools need an embeddings endpoint, and they degrade to
nothing silently when it is broken — the tool simply finds no results, which reads as "this
feature is useless" rather than "this endpoint returns HTTP 500". Embeddings and rerank are
also the endpoints least likely to be exercised by a human tester, so they break quietly.

**Verify each one, with the key a user actually has:**
```sh
curl -s -o /dev/null -w '%{http_code}\n' -H "Authorization: Bearer $KEY" \
  -H "Content-Type: application/json" -d '{"model":"'"$EMBED"'","input":"hello"}' \
  $BASE/v1/embeddings
```

## A recommended rollout order

1. **Populate `model_info` for every locally-registered model** — window, price, tool-calling
   capability. Highest value for the least work, and it unblocks every client at once.
2. **Smoke-test every non-chat endpoint** with a user-grade key, not an admin one.
3. **Publish a capability table** for your users: model, window, tool calling, caching and
   its minimum block, and which endpoint shape to use.
4. **State the timeout and rate-limit policy.**
5. **Re-verify after every upgrade.** Minimums and defaults change between model versions —
   ours did, and the published figures were wrong for the current generation.

## How a client can check your work

jichi's `doctor` reports what it can see from the client side — reachability, key presence,
declared context length, missing embed/rerank roles, prompt-cache pricing, and (with
`--live`) whether a real request produced a native tool call. It is a useful second opinion
because it reports what a *client* infers, which is what your users experience.

## Honest limits of this guidance

These requirements come from operating one agent against one gateway. The failure modes are
general; the specific numbers are not. Re-measure on your deployment rather than trusting the
table above — every check here is a command you can run, which is the point.
