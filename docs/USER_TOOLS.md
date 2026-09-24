# User-defined tools

Add your own tools to the agent without writing C or standing up an MCP server:
declare them in config and jichi registers each as a regular tool the model can
call. When the model calls one, jichi runs the configured command, feeds it the
arguments, and returns its output as the tool result.

## Config

```jsonc
{
  "tools": [
    {
      "name": "countbytes",
      "description": "Count the bytes in the given text.",
      "schema": {
        "type": "object",
        "properties": { "text": { "type": "string" } },
        "required": ["text"]
      },
      "shell": "printf '%s' \"$JICHI_ARG_TEXT\" | wc -c",
      "readonly": true
    },
    {
      "name": "deploy_preview",
      "description": "Build and deploy a preview; returns the URL.",
      "schema": { "type": "object",
                  "properties": { "branch": { "type": "string" } },
                  "required": ["branch"] },
      "command": "scripts/deploy.sh",
      "args": ["--preview"],
      "env": { "CI": "1" },
      "timeout": 120
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Tool name shown to the model (required). Must not shadow a built-in/MCP tool — collisions are skipped with a warning. |
| `description` | What the tool does (shown to the model). |
| `schema` | A JSON Schema for the arguments (passed to the model verbatim). Omitted → an empty object schema. |
| `command` + `args` | Run this executable argv-style (no shell). |
| `shell` | Alternatively, run `"/bin/sh -c <shell>"`. |
| `env` | Extra environment variables for the command. |
| `timeout` | Seconds before the command is killed (default 30). |
| `readonly` | `true` ⇒ usable in plan/read-only mode and allowed without a prompt; default `false` (mutating). |

## How arguments reach the command

The model's arguments (parsed and repaired as JSON — not checked against the `schema`) are delivered two ways — **never on the command
line**, so a tool's command string is fixed by your config and can't be
injected by an argument value:

1. **As JSON on stdin** — the whole arguments object. Parse it however you like
   (`jq`, a script's JSON reader, …).
2. **As `JICHI_ARG_<NAME>` environment variables** — each *scalar* argument
   (string/number/bool), with the name uppercased and non-alphanumerics mapped
   to `_` (e.g. `text` → `$JICHI_ARG_TEXT`). Convenient for shell one-liners.
   **A string longer than 1023 bytes is cut in its variable, silently** (a fixed
   buffer); stdin always carries it whole. For anything a model may write long —
   SQL, a patch, a document — read stdin. A cut SQL query can still be *valid*
   and run with a different meaning ([`SQLITE.md`](SQLITE.md) §4.1, measured
   2026-09-24).

The command's combined stdout+stderr (so the model sees errors too) is captured,
byte-capped (32 KB), and returned with a trailing `[exit status: N]`; a non-zero
exit marks the result as an error.

## Safety

User tools flow through the normal permission system (`docs/AGENT_MODES.md`):
in chat mode a **mutating** tool prompts for approval, `--auto` runs it, plan/
read-only mode hides it, and config `permissions.allow`/`deny` (by tool name)
apply. Tools exist only if you declare them. Because arguments never touch the
command line, the surface is a fixed command you chose plus model-supplied
stdin/env data.

## Recipe: web search

jichi ships a built-in `web_search` (M27, [`WEBSEARCH.md`](WEBSEARCH.md)) — and
it is **opt-in**: the tool is registered only when you configure a backend under
`"search"`, and `describe` lists it as *"when search.url is configured"*. The
reasoning that produced that design is the one this recipe is about, and it still
holds: there is no single search backend worth hardcoding (Tavily, Brave,
SearXNG, Google CSE … all differ in API and auth), so jichi ships the *shape* of
the tool and you bring the provider and the key.

So you have two routes, and the recipe below is still the more flexible one. Use
the built-in when your backend fits its request/response shape; use a
user-defined tool when it does not — a search tool is just "take a query, call
some HTTP API, return results", which is exactly what a user-defined tool
already does, with the response parsing under your control.

`examples/config.web-search.json` is a ready `web_search` tool: it reads
`JICHI_ARG_QUERY`, calls a search API with `curl`, and prints the top results.
Because arguments arrive as env vars (never on the command line) the API key
stays in your environment (`export TAVILY_API_KEY=…`). Swap the `curl` line for
any backend — the only contract is *read the query, print results to stdout*.

```sh
jichi --config examples/config.web-search.json -p "what changed in C23?"
```

*Beginner note:* the agent will call `web_search` on its own when a question
needs current/external information; you don't invoke it directly.

## When to use this vs MCP

User-defined tools are the lightest way to expose "run this script as a tool."
Reach for an **MCP server** (`docs/` MCP section) when you want a richer,
stateful, or shareable tool server with its own protocol; user tools are ideal
for a quick local command.
