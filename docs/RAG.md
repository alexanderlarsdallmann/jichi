# Retrieval-augmented generation (RAG)

jichi grounds answers in your code and documentation by retrieving the
most relevant passages and feeding them to the model. There are two layers:

1. **Explicit retrieval** (always available): the model calls the
   `codebase_search` / `search_docs` tools, or you type an `@`-reference
   (`@docs:<name>`, `@folder:<dir>`). See [DOCS.md](DOCS.md),
   [REFERENCES.md](REFERENCES.md).
2. **Automatic retrieval** (M61, opt-in): before each plain turn, jichi retrieves
   relevant context and injects it for you — no tool call or `@` needed.

Both layers share one retriever, so improving it (M60) improves everything.

## The retriever (M60)

For a query, the retriever:

1. **Embeds** the query and takes the cosine-nearest chunks (dense search).
2. **Hybrid fusion** (default on): also ranks the chunks by **BM25-lite**
   keyword overlap, then fuses the dense and lexical lists with **Reciprocal
   Rank Fusion**. Lexical scoring catches exact identifiers and rare keywords
   that dense embeddings blur (e.g. a query naming a function surfaces the chunk
   that defines it). Tokenization splits on non-alphanumeric characters, so
   `jc_lexical_topn` is matched by a query for `lexical`.
3. **Reranks** the fused candidates with the rerank-role model, when configured.
4. **Trims** to the requested number of results.

### Query rewrite / HyDE (opt-in)

Recall is bounded by how you phrase a query. When `retrieval.queryRewrite` is
set, one extra (non-streaming) model call expands the query before embedding:

- `hyde` — writes a short *hypothetical* passage that would answer the query and
  embeds that (Hypothetical Document Embeddings).
- `multiquery` — lists alternative phrasings / synonyms.

The expansion is appended to your original query, so both the dense and lexical
passes benefit. Off by default (it adds latency). Uses the summarize-role model,
falling back to the active model.

### Config

```jsonc
{
  "retrieval": {
    "hybrid": true,            // fuse lexical + dense (default: on)
    "queryRewrite": "hyde",    // "off" (default) | "hyde" | "multiquery"
    "rrfK": 60                 // Reciprocal Rank Fusion constant (default 60)
  }
}
```

`hybrid` is a tri-state: omitted ⇒ auto (on); set explicitly to override.
Requires a model with `roles: ["embed"]` (a rerank model is optional but
improves precision).

## Automatic context (auto-RAG, M61)

When enabled, jichi retrieves the chunks most relevant to a plain user turn from
the **codebase index** and the configured **docs sources**, and appends them as
a bounded `--- automatically retrieved context ---` block on the user message.

It is **off by default**. Turn it on per the conservative-defaults convention:

```jsonc
{
  "autoContext": true,             // master switch (default false)
  "autoContextSources": "both",    // "both" (default) | "codebase" | "docs"
  "autoContextTopK": 5,            // chunks per source (default 5)
  "autoContextMaxTokens": 3000     // injection budget (default 3000)
}
```

Or per run / per session:

- CLI: `--auto-context` / `--no-auto-context`
- TUI: `/autocontext on|off` (bare shows the current state)

### Behavior & guarantees

- **Injected on the user message, not the system prompt.** Per-query content
  would invalidate the cached system+tools prefix (see
  [PROMPT_CACHING.md](PROMPT_CACHING.md)); riding on the user turn keeps prompt
  caching intact.
- **Budget-bounded.** The injected block is capped to `autoContextMaxTokens`,
  itself clamped to a third of the context limit, so retrieval never crowds out
  the conversation. Oversized hits are truncated with a `[truncated]` note.
- **Gated.** Active only when an embed model exists, the turn is top-level (not a
  subagent), the message is plain (not a `/command`), and it carries no explicit
  `@`-references (to avoid duplicating what you asked for). Otherwise it is a
  silent no-op — headless / `--auto` runs are unaffected unless you enable it.
- **Visible.** The TUI prints `(auto-retrieved context attached)`; `/context`
  shows the per-turn budget; the `retrieve` telemetry event records block/token
  counts; `doctor` reports the status (and warns if on with no embed model).

## Indexing

Auto-context and `codebase_search` build the workspace index on first use
(cached under `~/.jichi.d/index/<key>/`, incremental on file mtime). Build
it ahead of time with `jichi index` (or `index --reindex`). Docs sources
are indexed lazily too; see [DOCS.md](DOCS.md).

### What the walkers skip, and `ignoreDirs`

Both the search index and the **repo map** descend the workspace with the same
skip list: every dot-directory (`.git`, `.zig-cache`, `.venv`, …) plus
`node_modules`, `target`, `build`, `dist` and `__pycache__`.

**`.gitignore` is deliberately not consulted.** It answers a different question —
*what must not be committed* — and the two sets differ in both directions: a
tracked 3 GB reference corpus is uninteresting to read, while a gitignored
generated header may be exactly what a model needs.

So the remaining cases are named by the operator:

```jsonc
{ "ignoreDirs": ["advenv", "rulebooks", "htmlcov"] }
```

Matched **by exact name, at any depth**, like the built-ins — not as a prefix, so
`advenv` does not also hide `advenv2`.

**Why this key exists (M520, measured).** On a project mid-rewrite from Python to
Zig, the Python virtualenv was called `advenv` — not a dot-directory, so no
built-in rule caught it. The repo map that reached the model was **87 lines of
pip's internals**, with the project's own Zig sources below the truncation line:
the map named the dependencies of the reference implementation and never
mentioned the code being written. With the three names above configured, the same
map reads `tests`, `zig`, `scripts`, `extras`, `tools`, `translations`.

A repo map is charged against every request, so a diluted map is not merely
unhelpful — it is *paid for* on every turn. If `jichi map` does not look like your
project, that is the symptom.

## See also

- [DOCS.md](DOCS.md) — external documentation sources (`search_docs`, `@docs:`)
- [REFERENCES.md](REFERENCES.md) — `@`-references
- [COMPACTION.md](COMPACTION.md) — the context budget auto-context sizes against
- [PROMPT_CACHING.md](PROMPT_CACHING.md) — why retrieved context rides the user
  message
