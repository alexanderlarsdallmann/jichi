# References (@-context)

Pull specific context into a turn by mentioning it with `@` in your message.
Before the turn runs, jichi resolves the references and appends a bounded
"referenced context" block to the message, so the model sees the actual content
— not a guess.

```
› Summarize @src/stack.rkt and explain @diff
```

becomes (what the model receives):

```
Summarize @src/stack.rkt and explain @diff

--- referenced context ---
@src/stack.rkt:
```
…file contents…
```
@diff:
```
…git diff…
```
```

## Providers

| Reference | Resolves to |
| --- | --- |
| `@<path>` | The file's contents (relative to the workspace, or an absolute path), bounded to 32 KB. Only expanded if the file actually exists. |
| `@diff` | The working-tree diff (via the `git_diff` tool; only in a git repo). |
| `@url:<url>` | The fetched page (via the `fetch_url` tool). |
| `@rss:<url>` | An RSS 2.0 / Atom feed fetched and reduced to a plain-text digest (title, date, link, and a short snippet per item) via the pure `jc_rss` reducer — clean text, not the raw XML `@url:` would return. A bare `@rss:` with no url is ignored. (W4) |
| `@sym:<Name>` | A symbol's definition via the language server (the `find_definition` tool when `lspServers` is configured), falling back to a code search for the name (`search_code`). A bare `@sym:` with no name is ignored. |
| `@img:<path>` | The image file attached to the turn as **vision input** (M29; only useful when the active model has the `vision` flag — a non-vision model drops it with a warning). A bare image path like `@photo.png` is recognized by its extension and attaches the same way. |
| `@audio:<path>` | The audio file transcribed to text (via the `transcribe_audio` tool; only when a `transcribe`-role model is configured, M33). A bare `@audio:` with no path is ignored. |
| `@docs:<name>` | The most relevant passages of a configured external documentation source (via `search_docs`; only when a `docs` source + an `embed`-role model exist, M34a). The whole message is the query. |
| `@problems` | Current language-server diagnostics for the files touched this session (via the `lspServers` client). Reports a note when no LSP is configured or nothing is wrong. (F5) |
| `@folder:<dir>` | A bounded listing of the directory's source files with their top-level symbols — a scoped repository map for fast onboarding (reuses the repo-map scanner). A bare `@folder:` with no dir is ignored. (F5) |
| `@mcp:<uri>` | The text of a resource exposed by a connected MCP server, addressed by its exact `uri` (via the `read_mcp_resource` tool; only when a server advertises resources, M47). A bare `@mcp:` with no uri is ignored. |
| `@ref:<name>` | A **config-defined alias** (#6). Its `type` selects resolution (see below). A bare `@ref:` with no name is ignored; an unknown name yields a short "no such alias" note. |

## Named aliases (`@ref:<name>`)

Define reusable references once in config (merged global + project) and cite them
by name. Each alias has a `name` and a `type`:

```json
{ "aliases": [
  { "name": "notes", "type": "file", "path": "docs/design.md" },
  { "name": "core",  "type": "dir",  "path": "src/core" },
  { "name": "spec",  "type": "url",  "url":  "https://example.com/spec" },
  { "name": "prod",  "type": "ssh",  "host": "deploy@prod.example:/srv/app" },
  { "name": "or-key","type": "key",  "env":  "OPENROUTER_API_KEY" },
  { "name": "op-key","type": "key",  "cmd":  "op read op://vault/api/key" }
] }
```

The value is read from the type-appropriate key shown above (`path`, `url`,
`host`, `env`, `cmd`); a generic `value` key works for any type as the
fallback spelling.

| type | resolves `@ref:<name>` to |
| --- | --- |
| `file` | the file's contents (like `@<path>`) |
| `dir` | a scoped repo-map of the directory (like `@folder:`) |
| `url` | the fetched page (like `@url:`) |
| `ssh` | the connection string, injected as context text |
| `text` | a literal snippet, injected as-is |
| `key` / `token` | **a secret** — resolved from the env var (`env`) or a fetch command (`cmd`), **never inlined**: `@ref:` emits only a redacted "present/NOT set" note, and the resolved value is registered with the log redactor. Use these to *name* a credential, not to paste it into a prompt. |

`doctor` lists alias **names and types** (never values). Secrets follow jichi's
`apiKeyEnv` posture: the config stores an env-var name or a command, not the
literal secret.

## What counts as a reference

A reference is an `@` at the **start of the message or right after whitespace**;
the token runs to the next whitespace. This keeps ordinary `@` usage literal:

- `email me@host.com` — not a reference (the `@` is mid-word).
- `@notafile` — left literal (no such file).
- Trailing sentence punctuation is trimmed, so `… see @main.c.` references
  `main.c`.

Only **non-slash** messages are scanned — custom commands (`/name …`) keep their
own template `@file`/`!`cmd``/`$ARGUMENTS` expansion.

## Notes & limits

- **Bounded:** each reference is capped (32 KB) and the whole block is capped
  (~96 KB), with truncation notes, so a turn can't be blown up.
- **Persisted:** the resolved context becomes part of the saved conversation
  (faithful, and visible on resume).
- **`@url` is synchronous:** fetching happens at submit time (you asked for it)
  and inherits `fetch_url`'s timeout.
- **`@diff` outside a repo:** resolves to a short "(git diff unavailable here)"
  note rather than failing the send.
- **`@problems` scope:** the LSP serves diagnostics per file, so `@problems`
  checks the files **touched this session** (bounded to the first 16 it handles),
  not the whole project — the set you've actually been working on.
- **Default:** on — **except under `--lite`/`lowResource`, where it defaults to
  `false`**, and jichi enables that profile automatically on a machine with less
  than ~1 GB of RAM. So if your `@file` mentions stay literal and nothing is
  added, check `jichi doctor` and set `"references": true` explicitly.
- **Disable:** set `"references": false` in config to turn the whole pass off
  (an `@` then stays literal).

For very large contexts, a future progressive-disclosure mode (advertise +
fetch-on-demand, like skills) is noted in [ROADMAP.md](ROADMAP.md).
