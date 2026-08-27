# Machine learning with jichi: what ships, what doesn't, how to extend

Findings + recommendations (2026-07-28, M182). The question: *what
machine-learning use does jichi support, and what is the recommended way to
extend it?*

## What ships (all inference-side, all via standard endpoints)

jichi contains **no ML runtime** — by design, forever (C89 + libcurl and
nothing else). What it has is first-class *plumbing to* ML services, keyed
by the per-model `roles` mask:

| Role | Endpoint | Used by |
|---|---|---|
| `chat` / `edit` / `apply` | chat completions / messages | the agent loop |
| `autocomplete` | chat (one-shot) | `complete`, `fim`, ghost text |
| `summarize` | chat (one-shot) | auto-compaction |
| `embed` | `/embeddings` | the RAG index, `codebase_search`, docs sources |
| `rerank` | `/rerank` | retrieval reranking |
| `image` | `/images/generations` | `generate_image` (incl. img2img) |
| `audio` | `/audio/speech` | `generate_audio` (TTS) |
| `transcribe` | `/audio/transcriptions` | `transcribe_audio`, `@audio:` refs, ACP audio |

Plus **vision input** (per-model `vision` flag; `--image`, `@img:` refs) and
the *classical-IR* pieces implemented in-tree because they are math, not
runtime: cosine top-N over embeddings, BM25-lite lexical scoring, and
reciprocal-rank fusion for hybrid retrieval ([RAG.md](RAG.md)).

## What deliberately does not ship

No training, no fine-tuning, no gradient anything, no dataset builders, no
model-file loaders. A C89 agent that vendored an ML runtime would be a
different (and worse) project — the same rule that keeps audio playback an
external `aplay` and PDF extraction an external `pdftotext`: **shell out,
never vendor** ([SOUND.md](SOUND.md), the M42 pattern).

## The three recommended extension patterns

**1. A user-defined tool wrapping your ML script** — the sanctioned escape
hatch ([USER_TOOLS.md](USER_TOOLS.md)). Arguments arrive as JSON on stdin
and `JICHI_ARG_*` env vars (never the command line), output is captured and
capped, the permission system applies. A scikit-learn classifier becomes an
agent tool in one config entry:

```jsonc
{
  "tools": [
    {
      "name": "classify_ticket",
      "description": "Classify a support ticket (urgency, area) with the local model.",
      "schema": { "type": "object",
                  "properties": { "text": { "type": "string" } },
                  "required": ["text"] },
      "command": "python3", "args": ["tools/classify.py"],
      "timeout": 30, "readonly": true
    }
  ]
}
```

`tools/classify.py` reads the JSON from stdin, loads its pickle/ONNX model,
prints the label. Train the model however you like — jichi neither knows
nor cares. The same pattern wraps torch inference, statistical profiling of
a dataset, or a whole `delegate_to_worker`
([SUPERVISOR_OF_MANY.md](SUPERVISOR_OF_MANY.md)).

**2. An MCP server** (README → *MCP servers*) — when the ML
capability already speaks Model Context Protocol (vector DBs, experiment
trackers, labeling platforms), configure it under `mcpServers` and its
tools/resources appear natively, with per-server `autoApprove`/`deny`
policy. Prefer this over a user tool when the integration is long-lived
and multi-tool.

**3. Telemetry as a dataset** — jichi's own logs are mineable training/eval
material. `--log-level full` captures bounded prompt/response/tool-I/O text
per event ([TELEMETRY.md](TELEMETRY.md)); one jq turns a month of real
usage into an eval set:

```sh
jq -c 'select(.event=="model_call") | {in: .prompt, out: .response}' \
   ~/.jichi.d/telemetry/*.jsonl > evalset.jsonl
```

Mind the obvious: `full` tier logs *content* — treat the file like the
code it quotes, and scrub before sharing (keys are already redacted at
write time, code is not).

## Honest recommendation

If your ML need is *"the agent should call a model"* — a role + config
entry covers it today, including local servers (LM Studio, llama.cpp,
LocalAI — [LOCAL_MODELS.md](LOCAL_MODELS.md)). If it is *"the agent should
run my ML code"* — pattern 1, ten minutes. If it is *"jichi should learn
weights from usage"* — that is not this project; the self-improvement that
IS this project operates in prompt/asset space (memory, skills, lessons,
corrections — [SELF_IMPROVEMENT.md](SELF_IMPROVEMENT.md),
[LEARNING.md](LEARNING.md)), where a human can read every learned thing.
