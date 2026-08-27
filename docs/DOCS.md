# External documentation index (`search_docs` / `@docs`)

jichi's codebase index (`codebase_search`, the `index` subcommand) covers the
**workspace** only. The *docs* feature (M34a) lets jichi index and retrieve
**external** documentation — a library's reference, a house style guide, an API
spec, a book of standards — so the agent can ground its answers in that material
without it living in your repo.

It is pure reuse of the embeddings stack: the same chunker, embedder, on-disk
vector cache, cosine ranker, and optional reranker that back codebase search,
pointed at a *named, arbitrary directory* instead of the workspace.

## Configuration

Add a `docs` array to your config; each entry names a source and the local
directory to index:

```jsonc
{
  "docs": [
    { "name": "react",  "path": "/home/me/refs/react-docs" },
    { "name": "style",  "path": "./docs/styleguide" },
    { "name": "spec",   "url": "https://example.com/api/spec.html" }
  ]
}
```

- **`name`** — the selector used by the `search_docs` tool's `name` argument,
  the `@docs:<name>` reference, and the `docs` subcommand.
- **`path`** — a local directory of text/markdown (relative paths resolve
  against the working directory). Markdown, plain text, and source files are
  indexed; binary/asset files are skipped (the same negative filter codebase
  indexing uses). **PDFs are indexed too** (M45): a `.pdf` in a docs source is
  extracted to text via the M42 extractor (`pdftotext`, or config `pdfCommand`)
  and chunked like any document — so specs/papers/manuals are retrievable, not
  just readable by path. (The *codebase* index still skips PDFs; this is opt-in
  to docs sources, where it's clearly wanted, and a missing extractor just skips
  the PDF.)
- **`url`** — an http(s) page to index instead of a directory (M51). On first
  use jichi fetches it, reduces the HTML to plain text (tags dropped, `<script>`/
  `<style>` skipped, block tags → line breaks, common entities decoded), and
  caches it as a text file under `~/.jichi.d/docs/<name>/`, then indexes
  that cache exactly like a local directory. The page is re-fetched when the
  cache is older than a day (a stale cache is reused if the re-fetch fails, so an
  offline run still works). Give a source **either** `path` **or** `url`.
- **`type`** — set to `"rss"` (aliases `"atom"`/`"feed"`) on a `url` source to
  index an **RSS 2.0 / Atom feed** (W4): the fetched XML is reduced to a plain-text
  digest (per item: title, date, link, and a short snippet, with entities/CDATA and
  any HTML in the body decoded) by the pure `jc_rss` reducer before caching +
  indexing — otherwise the fetch is treated as HTML. A feed is also **auto-detected**
  (a `url` that serves XML is read as a feed even without `type`). Then `@docs:<name>`,
  `search_docs`, and `docs search` all work over the feed unchanged, with the same
  1-day cache TTL. Example: `{ "name": "news", "url": "https://ex.com/feed.xml", "type": "rss" }`.

An entry missing a name (and both `path` and `url`) is skipped with a warning.
The feature requires a model with the **`embed`** role (the same one codebase
search uses); without one the tool/reference are inert and `doctor` warns.

> **URL scope (v1):** a single page per source — the HTML→text reduction is
> best-effort, not a full parser, and recursive site crawling is deferred.

## How it is indexed

Each source is indexed independently and cached on disk under
`~/.jichi.d/index/<key>/` (keyed by the directory path), exactly like the
codebase index — so the first query pays the embedding cost and later queries
only re-embed files whose mtime changed. The index is built lazily on first use
and rebuilt incrementally; `docs index` forces a full refresh.

## Retrieval

A query is embedded, ranked against the source's chunks by cosine similarity,
optionally reranked (when a `rerank`-role model is configured), and the top
passages are returned as `path:start-end` blocks — the same shape
`codebase_search` returns.

### The `search_docs` tool

Registered only when at least one `docs` source **and** an `embed`-role model
are configured (like `web_search` is gated on `search.url`). Read-only.

| Argument | Required | Meaning |
| --- | --- | --- |
| `query` | yes | what to look for |
| `name` | when >1 source | which source to search (optional with a single source) |
| `max_results` | no | how many passages (default 5) |

With more than one source configured and no `name`, the tool returns the list of
available source names so the model can retry with one.

### The `@docs:<name>` reference

In a plain (non-slash) message, `@docs:<name>` retrieves the most relevant
passages of that source for the **whole message** and appends them as a
referenced-context block — the same mechanism as `@url:` / `@sym:` / `@audio:`.
It resolves through the `search_docs` tool, so it is inert when the tool isn't
registered.

```
How do I memoize an expensive selector? @docs:react
```

## The `docs` subcommand

```sh
jichi docs                       # list configured sources
jichi docs index                 # build/refresh every source
jichi docs index react           # build/refresh just "react"
jichi docs search react "hooks"  # retrieve passages for a query
```

`index`/`search` need network (embeddings) and an `embed`-role model; the bare
list is offline.

## Internals

- **`jc_docs_run`** (`src/index/jc_docs.c`) — orchestrator: resolves the source
  directory, builds/reloads a local `jc_index` over it, embeds the query, ranks
  (cosine + optional rerank), formats the hits, and frees the index. A sibling of
  `jc_search_run` but over a named path rather than `app->cwd`.
- **`jc_docs_find`** — resolves a source by name (or the sole source when the
  name is omitted).
- **`search_docs`** (`src/tools/jc_tool_docs.c`) — the tool wrapper.
- **`JC_REF_DOCS`** (`src/command/jc_refs.c`) — the `@docs:<name>` reference,
  resolved via `search_docs`.
- Config: `struct jc_docs_cfg {name, path}` in a `struct jc_vec docs` on
  `struct jc_config` (`src/config/jc_config.c`).
- Tests: config parse (`tests/test_config.c`), `@docs:` scan (`tests/test_refs.c`),
  ranking e2e (`tests/e2e/docs.py`, a loopback embeddings mock).
