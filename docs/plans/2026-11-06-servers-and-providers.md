# Servers and providers: what jichi speaks, what it has been measured on, and what to add

*Proposal, 2026-11-06. Written by the supervisor (Claude Opus 5.5) at the operator's request. The operator,
2026-11-05: "We ought to support more providers, servers, and honestly document the gaps, as well as the
strengths of jichi." The facts below were read from master `9ada7cd7` or measured on 2026-11-05/06. Each one
says which. Nothing here is built yet. Section 7 lists what the operator decides.*

## 1. What jichi speaks (read from the source)

jichi has exactly two wire dialects (`src/provider/`):

| `"provider"` | Protocol | Endpoint jichi calls |
|---|---|---|
| `"openai"` | OpenAI Chat Completions, streamed | `<apiBase>/chat/completions` (`jc_provider_openai.c`) |
| `"anthropic"` | Anthropic Messages | `<apiBase>/v1/messages` (`jc_provider_anthropic.c`) |

Any other name is refused with one sentence (`jc_config_provider_problem`, M718). Before M718, jichi guessed the
dialect from the model id, so `"provider": "ollama"` sent the Anthropic protocol to an Ollama server.

jichi does not speak: the OpenAI Responses API, Google's native Gemini API, AWS Bedrock (it signs requests with
SigV4), Ollama's native `/api/chat`, or Cohere's API. Whether Azure OpenAI's URL and header shapes work through
`"openai"` has not been checked.

## 2. What it has been measured on

The verdict words are `docs/PLATFORMS.md`'s. **Driven** means a model completed a real tool-calling turn whose
result it had to use.

| Server | Verdict | Evidence |
|---|---|---|
| LM Studio | **Driven** | LOCAL_MODELS.md's capability table (five native models; gemma-4-12b-qat through a two-round tool turn) and the platform rows' live turns; again on 2026-11-06 with qwen/qwen3.5-9b on the GPU: `doctor --live` native, Step 7 passed (2.0 / 3.0 / 2.2 s) |
| The institution's gateway (`jlu/*`; a source comment says LiteLLM in front of vLLM) | **Driven** | the drives' leads and workers since 2026-10-17 run on it |
| llama.cpp `llama-server` | **Driven** (b11836, CPU build) | 2026-11-05: `docs/VERIFY_A_PLATFORM.md` Step 7 as published, the same qwen3.5-9b GGUF, the maker's sampler settings: native, the pass phrase found. The published LOCAL_MODELS.md command (`-c 16384 --port 8080`) also passed. Earlier: the edge-AI study of 2026-10-23, models of 1B or less |
| Ollama | **Driven** (v0.40.14, CPU, GGUF imported by Modelfile) | 2026-11-05: Step 7 as published passed, and so did LOCAL_MODELS.md's 8,192-context example. Ollama's library models (`ollama pull`) were not measured |
| vLLM (directly), SGLang, TGI, llamafile, KoboldCpp, MLX | **Unmeasured** | vLLM only behind the gateway |
| LocalAI | **Claimed, not re-checked** | LOCAL_MODELS.md calls it "the tested local backend for media generation" |
| OpenAI, Anthropic, Gemini (its OpenAI-compatible endpoint), Mistral, OpenRouter, Azure OpenAI | **Unmeasured since the free-models rule** | each needs a priced key, so a measurement needs the operator's grant for that run (`CLAUDE.md`, Models) |

The evidence for 2026-11-05/06 is on the workstation, outside the tree (a results page, one directory per run).

## 3. The gaps (each found or read, never assumed)

1. **`doctor` cannot verify the context window on llama-server or Ollama.** It asks `/v1/model/info`, which is
   LiteLLM's endpoint, and says so. The servers report the window elsewhere: llama-server at `/props` (`n_ctx`),
   Ollama at `/api/show` (`num_ctx`). The probe belongs in the provider layer, never in the agent loop.
2. **A stale number on the verification page.** `VERIFY_A_PLATFORM.md` says jichi's first request is "about twelve
   thousand tokens (12,182 measured)" and derives a 32,768 minimum from it. With the page's own `live.json`, jichi
   counted 4,364 to 4,439 input tokens on all three servers (Qwen3.5's tokenizer), and 2,041 with
   `"contextLength": 8192` declared. Re-measure it before rewriting the page.
3. **Unmeasured example models.** LOCAL_MODELS.md's llama.cpp and Ollama examples use `qwen2.5-coder-7b`. That model
   is unmeasured, and its 14B sibling calls tools as text, which jichi cannot use (the page's own table). An example
   should use a model with a recorded `native` verdict.
4. **Server claims without a row.** README, CONFIG_TUTORIAL, CHATBOT and BENCH_LOCAL_GPU name servers as working.
   No single page holds a verdict per server, so a claim and its evidence drift apart, as they did for llama.cpp
   and Ollama until 2026-11-05.
5. **Protocols not spoken** (section 1). A user of Gemini, Bedrock or the Responses API needs a gateway (LiteLLM,
   OpenRouter) or nothing.

## 4. The strengths (each with its evidence)

- **One protocol covers most servers.** The same configuration shape, with only `apiBase` and `model` changed,
  passed the same Step 7 on LM Studio, llama-server and Ollama with the same model file on 2026-11-05/06.
- **No guessing.** An unknown provider name is refused with the fix in the sentence (M718), instead of sending
  one vendor's protocol, and its key, to another vendor's server.
- **The agent loop never branches on provider.** A dialect enters only through a provider's `build_request`
  (`docs/reading/fukabori-02-the-provider-abstraction.md`), so a new protocol is a new vtable and touches no turn
  logic.
- **`doctor --live` tells native from text tool calling** with one request (308 prompt tokens). A text-form model
  looks exactly like a broken setup, so this check is what a user needs first.
- **It fits small windows.** With `"contextLength": 8192` declared, jichi's first request shrank to about 2,000
  tokens, and the tool turn still passed on Ollama.
- **Free and local by default.** Nothing in the measurements above cost anything.

## 5. The proposal

1. **One page holds the verdicts** (`docs/SERVERS.md`, or a section of `docs/MODELS.md`). It has a row per server and
   per protocol, giving the verdict, version, date, model and seconds, in the PLATFORMS.md vocabulary. It has a
   short section of gaps and one of strengths, written as section 3 and section 4 are here. Every other page links
   to it instead of listing servers.
2. **A lint keeps the claims tied to the rows:** a server named as working on any shipped page has a row. The lint
   states its universe, floors its extraction and is shown red on a planted claim.
3. **A script anyone can run:** `scripts/verify-server.sh`, which is Step 7 for one server, the way
   `scripts/_rig_live.sh` serves the platform rows. A reader then adds a row the same way this proposal's rows were
   made.
4. **`doctor` learns the servers' window endpoints** (gap 1), each with a red-first test against a recorded real
   response.
5. **The page fixes:** gaps 2 and 3, with each new number measured first.
6. **More protocols**, each a new provider vtable with a red-first test against a recorded real response, in the
   order the operator chooses. The candidates, with what each costs:
   - **OpenAI Responses API:** OpenAI's newer protocol. It needs a priced key to Drive.
   - **Gemini native:** Gemini also offers an OpenAI-compatible endpoint, so measuring that first may make a new
     dialect unnecessary. A priced key either way.
   - **Bedrock:** SigV4 signing is a new security surface (credentials, clock, region). A priced account.
   - **Azure OpenAI:** first check whether `"openai"` already reaches it (its `api-version` query and `api-key`
     header). A priced account.
   - **More local servers** (vLLM directly, SGLang, LocalAI re-checked, llamafile): free, measured like
     llama-server.

## 6. Where it fits

Items 1 to 5 are documentation and doctor work. They can join the docs batch planned for the hardening drive's
resume, next to the README version lint and the "this machine" fix (2026-11-05). Item 6 is product work: its own
batch, or its own drive, after the harness-port drive.

## 7. For the operator

1. **Which protocols matter** beyond the two spoken today, and in what order (section 5, item 6)?
2. **Priced measurements:** may any of OpenAI, Anthropic, Gemini, Mistral, OpenRouter or Azure be measured, one named
   run each, under a grant asked before the request?
3. **The schedule:** items 1 to 5 with the hardening resume's docs batch (recommended), or sooner?

## Decided 2026-11-06 00:44 +0100 (the operator)

*This section answers section 7. It supersedes nothing above.* The operator: "Regarding the model servers, and
providers: Docs part with the resume batch, and we will discuss the protocols, later. No paid vendor API tests,
unless I explicitly say so. We are going to plan the tests for this month."

1. **Section 5, items 1 to 5** (the verdict page, the lint, `verify-server.sh`, doctor's window probes and the page
   fixes) go into the documentation batch when the hardening drive resumes. This answers question 3.
2. **New protocols** (section 5, item 6): to be discussed with the operator later. Nothing is built or ordered
   until then. This defers question 1.
3. **No paid vendor API test** unless the operator explicitly says so. This answers question 2, as the standing
   rule (`CLAUDE.md`, Models) already says.
4. **The tests for this month** (November 2026) will be planned with the operator. Until then, only free servers are
   measured, the way section 2's rows were.

Registered for the drive that builds it: a `docs/DEFERRED.md` row, asked of the harness-port drive's lead on
2026-11-06.
