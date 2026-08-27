# When the server knows its own context window (2026-08-19)

Measured against `https://api.hrz.uni-giessen.de/v1`, the JLU HRZ gateway, using the
development key. Every number here came from a live call; nothing is inferred from
documentation.

## The thing that changed

jichi has always had to be **told** a model's context window. `docs/COMPACTION.md`
resolves the budget as `contextLimit` -> the active model's `contextLength` -> a
built-in 32000, and `src/chat/jc_agent.c` carries a warning built entirely around the
consequence of getting it wrong:

> its real `max_model_len` was 256000. jichi compacted seven times toward a target it
> never needed, achieved nothing (7/7 unrelieved), and advised shrinking tool output
> when the fix was one config number.

That comment then states the assumption this row invalidates:

> An under-declared window is the common case, not an exotic one: doctor's own text
> says jichi assumes ~32000 when `contextLength` is absent, and **every local server
> this project has met declares nothing.**

The HRZ gateway declares something. It is a **LiteLLM proxy**, and LiteLLM serves an
endpoint the OpenAI surface does not have.

## Where the number lives, and where it does not

`GET /v1/models` -- the standard endpoint -- lists the gateway's models and
says **nothing** about any window:

    {"id":"jlu/qwen3.8-27b","object":"model",...}

`GET /v1/model/info` -- a **LiteLLM extension** -- returns the whole model table
(1.6 MB here) with a `model_info` block per entry:

    {"model_name":"jlu/qwen3.8-27b",
     "litellm_params":{"custom_llm_provider":"hosted_vllm","model":"qwen3.8-27b"},
     "model_info":{"mode":"chat",
                   "max_input_tokens":196608,
                   "max_output_tokens":65536}}

`jlu/qwen3.8-27b` answers chat normally (`finish_reason` `stop`), and is a
**reasoning** model: it returns `reasoning_content` alongside `content`, and spent 20
completion tokens -- 16 of them reasoning -- to answer "Say OK". That matters for a
separate reason already recorded in `docs/PLATFORMS.md`: a model that spends its whole
output budget reasoning can return an EMPTY answer with exit 0, which a newcomer reads
as a broken setup.

## Which number to declare, and why not the other one

**`max_input_tokens`, 196608.** Not 262144, the tempting sum of input and output.
jichi's `contextLength` is the budget it FILLS WITH PROMPT -- `effective_limit()` in
`src/chat/jc_compact.c` -- and compaction triggers at 0.8 of it, which is already the
headroom `COMPACTION.md` tells you to leave. Declaring the sum would over-state the
window and invite the HTTP 400 the budget exists to avoid.

    { "provider": "openai", "model": "jlu/qwen3.8-27b",
      "apiBase": "https://api.hrz.uni-giessen.de/v1",
      "apiKeyEnv": "JLU_API_KEY",
      "contextLength": 196608 }

Left undeclared, jichi budgets 32000 against a real 196608 -- a **6x**
under-statement, and precisely the shape that produced the seven wasted compactions
quoted above.

## The finding that shaped the code: 404 is not how you detect an absent endpoint

`/v1/model/info` is LiteLLM-specific, so jichi has to recognise the servers that do
not have it. The obvious rule -- "404 means absent" -- is **wrong**. Measured against
the LM Studio instance at `134.176.150.160:1234`:

    HTTP 200, 63 bytes
    {"error":"Unexpected endpoint or method. (GET /v1/model/info)"}

**HTTP 200, carrying an error object.** So the status alone cannot separate "this
server has no such endpoint" from "the request failed"; only the *shape* of the body
can. This is the M476 rule met again in a new costume: *a probe that cannot
distinguish "the feature is absent" from "I could not run the test" is not a probe.*
Had jichi trusted the status code, every non-LiteLLM user would collect a warning
about a perfectly healthy server -- which is how people learn to ignore warnings.

`jc_net_parse_model_limits()` therefore returns `JC_ERR_PARSE` for "not a model
table", and `doctor` renders that as a **fact about the endpoint**, not a fault in the
user's config.

## What `doctor` now says, measured in all four cases

| Config | Verdict |
|---|---|
| `contextLength` absent, gateway publishes 196608 | **!** the server publishes a context window, the config declares none |
| `contextLength` 196608 | **ok** declared context window agrees with the server |
| `contextLength` 300000, over the real window | **!** declared context window is larger than the server's |
| LM Studio: no such endpoint, HTTP 200 + error | **ok** this server does not publish model limits |

The third case deserves its own line: an **over**-declared window is the more
dangerous direction. Under-declaring wastes work and is noisy; over-declaring means
the budget is not enforced until the provider enforces it, as an HTTP 400 mid-run.

## Cost, stated rather than discovered later

The response is the entire model table -- **1.6 MB on this gateway** -- parsed into
cJSON. That is acceptable for a one-shot diagnostic, and it is why
`jc_net_model_limits()` is called from `doctor` and from nothing on the hot path. It
also means `doctor` now issues one extra request per run against a gateway that has
the endpoint.

## What was NOT done, and why

The in-run warning in `jc_agent.c` was left alone. It infers an under-declared window
*after the fact*, from a request larger than the declared limit that the server
nonetheless accepted -- evidence that costs nothing, because the run already paid for
it. Teaching it to fetch the published limit would put a 1.6 MB download inside the
agent loop to learn something `doctor` can say before the run starts. The right fix
for "my window is wrong" is to be told at configuration time, which is now what
happens.


## Why `setup` does not probe by default

`setup` is **offline by design**, and the code says so in two places rather than one:
`setup_validate()` is described as a *"Lightweight, offline validation pass … (no
provider/network)"*, and `setup`'s own closing line prints

    jichi --config <path> doctor   # full validation (+ network)

So the division of labour is deliberate: **setup writes, doctor checks.** It also
means setup has neither of the two things a probe needs — it takes the env-var
*name* via `--key-env` and never reads the key, and it makes no requests.

That is why the discovery is reached by an **explicit flag** rather than made
automatic:

    setup --context-length <tokens>   # offline, exact, reproducible
    setup --context-length auto       # one request, opted into

The number form is the one an agent driving another jichi should use: it already
knows the window, and the run then needs no egress and produces the same config
every time. `auto` exists for the case where nobody knows it yet.

The two failure kinds are kept apart on purpose, because they belong to different
people:

- **Missing prerequisites** — no `--api-base`, or the `--key-env` variable not
  exported — **refuse with exit 2** and name what is absent. The request cannot be
  attempted, and answering an explicit flag by silently writing nothing is the
  M458 failure.
- **A server that publishes no limits** is *not* the caller's mistake; most servers
  do not. That is reported, the run continues, and the message says what jichi will
  assume instead (32000), so the reader can decide whether to pass a number.

Measured, all five paths: a number is written offline; `auto` against the gateway
writes 196608 and states that the 65536 output limit is **not** part of the budget;
`auto` without an endpoint and `auto` without an exported key each refuse with a
message naming the missing piece; `auto` against LM Studio reports *"this server
does not publish model limits"* and finishes successfully with the key unset.

The wording of that last message is shared with `doctor` deliberately. The first
draft printed the internal status — *"parse error, HTTP 200"* — which is accurate
and useless: it describes jichi's difficulty rather than the reader's situation.

## For a learner: the shape of this whole change

Three of the four things worth noticing here are not about context windows at all.

1. **The standard endpoint did not carry the fact, and the vendor extension did.**
   Reaching for the extension is correct *and* creates an obligation: handle every
   server that lacks it, and say which case you are in.
2. **Absence and failure look alike unless you check.** Two servers were needed to
   learn that, and the second one (LM Studio) is the one that produced the rule. One
   endpoint, tested against one server, would have shipped a warning that fires on
   healthy systems.
3. **The parse was split from the fetch so it could be tested.** The network half
   needs a server; the parse half needs a string. `jc_net_parse_models` in the same
   file was already split exactly this way -- the pattern was there to copy, and
   copying it is why the LM Studio body is now a permanent test case rather than a
   story in a commit message.
