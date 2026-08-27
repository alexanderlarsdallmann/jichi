# Web search (M27)

The `web_search` tool lets the agent search the web for current information
(beyond the workspace and beyond `fetch_url`, which only retrieves a known URL).

It is **opt-in**: the tool is registered only when a search backend is configured
under `"search"`.

## Config

```jsonc
{
  "search": {
    "url": "https://api.tavily.com/search",
    "apiKeyEnv": "TAVILY_API_KEY",
    "provider": "tavily",
    "maxResults": 5
  }
}
```

- `url` — the full search endpoint (required; absent ⇒ the tool is not
  registered).
- `apiKey` / `apiKeyEnv` — the key, literal or from an env var. Sent as a
  `Bearer` header **and** as an `api_key` body field (covering backends like
  Tavily that take it either way). The key is registered for log redaction.
- `provider` — informational.
- `maxResults` — default result cap (the model may pass a smaller `max_results`).

## Request / response

The tool issues `POST <url>` with `{"query": "...", "max_results": N}` (plus
`"api_key"` when set). The response parser accepts the common shapes: a results
array under `results` (Tavily), `data`, or `web_results`; per item the title is
`title`|`name`, the URL is `url`|`link`, the snippet is
`content`|`snippet`|`description`|`text`. Output is rendered as a numbered list
(title / URL / trimmed snippet) and byte-capped (`searchMaxBytes`).

A missing/unreachable endpoint or an unparseable response returns a tool **error
value**, never a crash.

## Implementation

`src/tools/jc_tool_websearch.c` — the pure, unit-tested `jc_websearch_format`
renders the results; the tool POSTs via `jc_http_perform`. Config lives in
`include/jc_config.h` / `src/config/jc_config.c`; registration is gated in
`src/main.c`. Tests: `tests/test_websearch.c`, `tests/e2e/websearch.py`.
