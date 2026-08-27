# LSP diagnostics & code navigation

> **Formatting has a second backend.** `format_file` falls back to a configured
> `formatCommand` (a shell command) when no language server formats the file —
> the path for languages with no LSP formatter. See
> [`FORMATTING.md`](FORMATTING.md).

jichi runs language servers (LSP) for two things: real compiler/type
**diagnostics** (so after the agent edits a file it sees the actual errors), and
**code navigation** — symbol-accurate go-to-definition, find-references, and
file outlines, far more precise than grep on a large codebase. (No
completion/hover/rename.)

## Configuration

Add an `lspServers` array to your config. Each entry spawns one server and is
matched to files by extension:

```json
{
  "lspServers": [
    { "name": "clangd", "command": "clangd", "extensions": ["c", "h", "cpp"] },
    { "name": "pyright", "command": "pyright-langserver",
      "args": ["--stdio"], "extensions": ["py"] }
  ]
}
```

- **name** — used in messages.
- **command** / **args** — the language server to spawn (must be installed and
  speak LSP over stdio).
- **extensions** — file extensions (no dot) this server handles.

The server is started **lazily** on first use and reused for the session.

## How you get diagnostics

- **Automatically after edits.** When the agent's `edit_file` or `write_file`
  touches a file an LSP server handles, jichi opens it, waits for the
  server's diagnostics, and — if there are any — appends them to the tool
  result so the model sees and addresses them. Clean files add nothing.
- **From the CLI.** `jichi lsp <file>...` prints diagnostics for each
  file (exit 1 if any are reported). Needs no API key — handy for checking your
  `lspServers` config:
  ```sh
  jichi lsp src/main.c
  ```
  ```
  === src/main.c ===
  Diagnostics (1):
  src/main.c:2:13: error: Expected expression
  ```

## Code navigation

When `lspServers` is configured, four **read-only** tools are advertised to the
agent (and surfaced on the CLI):

| Tool | Args | What |
| --- | --- | --- |
| `find_definition` | `symbol`, `path?` | Where a symbol is defined. With `path`, locate the symbol in that file; omit `path` to search the whole project (`workspace/symbol`). |
| `find_references` | `symbol`, `path?` | All uses of a symbol (the blast radius of a change). |
| `list_symbols` | `path` | A file's outline (functions/types/variables). |
| `list_code_actions` | `path`, `line` | Quick-fixes/refactors the server offers at a line (M44; apply one with `apply_code_action`). |

From the CLI (needs no API key — great for checking your setup):

```sh
jichi lsp symbols src/chat/jc_envelope.c     # outline
jichi lsp def     src/chat/jc_agent.c jc_env_run_verify
jichi lsp refs    src/chat/jc_envelope.c jc_glob_match
jichi lsp actions src/chat/jc_agent.c 200    # code actions at line 200
jichi lsp actions src/chat/jc_agent.c 200 quickfix  # only quick-fixes (M58)
```
```
function jc_glob_match  .../src/chat/jc_envelope.c:75
...
.../src/chat/jc_envelope.c:75:5     # definition
.../src/chat/jc_envelope.c:89:25    # use
.../src/chat/jc_envelope.c:101:25   # use
```

Results print as `<path>:<line>:<col>` (1-based). The agent addresses symbols by
*name* (it rarely knows coordinates); jichi finds the symbol's position in
the file, or resolves it project-wide via `workspace/symbol`, then queries the
server.

> **Cross-file accuracy needs build metadata.** For C/C++, generate a
> `compile_commands.json` (e.g. via `bear -- make`) so clangd knows the include
> flags; without it, within-file results are reliable but cross-file
> resolution is best-effort. Other servers (pyright, gopls, rust-analyzer) work
> from their own project detection.

## Refactors (M40)

Two **mutating** tools turn the server's edits into real file writes, so the
agent can refactor safely instead of grep-and-replace:

| Tool | Args | What |
| --- | --- | --- |
| `rename_symbol` | `symbol`, `path`, `new_name`, `line?` | Rename a symbol everywhere via `textDocument/rename`. |
| `format_file` | `path` | Reformat a file via `textDocument/formatting`. |
| `apply_code_action` | `path`, `line`, `title` | Apply a server quick-fix/refactor via `textDocument/codeAction` (M44). |

There is also a read-only **`list_code_actions`** (`path`, `line`) that lists
what the server offers at a line — `"<title>  [<kind>]"` per action — so the
agent (or you, in plan mode) can choose one to apply.

The server returns a **WorkspaceEdit** (rename, code action) or **TextEdit[]**
(format); jichi applies them with the pure `jc_lsp_apply_text_edits` — line/character
ranges are resolved to byte offsets, edits are sorted into document order,
overlaps after the first are skipped, and every write goes through the
path-fenced `jc_app_write_file` (so edits outside the workspace are refused, not
applied). `rename_symbol` reports the edit/file counts; `format_file` reports
`Already formatted (no changes).` when the server returns nothing;
`apply_code_action` matches the chosen `title` (case-insensitive substring),
**resolves** a lazily-computed edit (`codeAction/resolve`) when needed, and
applies it. The mutating three are `readonly = 0`, so they flow through the usual
permission gate (ASK in chat, auto-approved under `--auto`, hidden in plan mode);
`list_code_actions` is read-only (usable in plan mode).

> `character` offsets are treated as bytes — exact for ASCII/UTF-8 single-byte
> source, an approximation where LSP counts UTF-16 code units (rare in code).
> **Code actions apply edits and/or run the action's server command (M50):** an
> edit-bearing action is applied directly; a `command`-bearing action is run via
> `workspace/executeCommand`, and any `workspace/applyEdit` the server pushes back
> while handling it is acked and applied through the path fence (an action with
> both does the edit first). **The `codeAction` request now carries the
> structured diagnostics on the requested line (M57):** after opening the file
> jichi waits briefly for the server's `publishDiagnostics`, filters to the
> diagnostics whose range covers that line, and passes them in
> `context.diagnostics` — so diagnostic-tied quick-fixes ("add the missing
> import", "fix this error", "remove unused") are offered, not just
> refactor/source actions. An optional `kind` argument (comma-separated
> CodeActionKinds, e.g. `quickfix` / `refactor` / `source.organizeImports`) sends
> `context.only` so the server returns just those kinds (M58).

## How it works

JSON-RPC over the server's stdio with LSP's `Content-Length` framing:
`initialize` → `initialized` → `textDocument/didOpen` (with the file's current
text) → the server pushes `textDocument/publishDiagnostics`, which we collect
for that file's URI within a timeout and format as
`<path>:<line>:<col>: <severity>: <message>`. Server-to-client requests (e.g.
capability registration) are answered with a null result so the server never
stalls. On shutdown each server gets `shutdown`/`exit`. Navigation reuses the
same transport: one id-matched request/response round-trip
(`textDocument/definition` / `references` / `documentSymbol` /
`workspace/symbol`), with the response's locations/symbols formatted as above.

## Limitations

- **Diagnostics, navigation, rename + format + code actions** (edit- and
  command-bearing) — no completion/hover.
- **First-call latency** — a server may take a few seconds to start and index;
  subsequent files on the same server are fast. Diagnostics collection is
  time-bounded.
- One server per extension (first match in config order).
- File URIs are plain `file://<abs path>` (no percent-encoding).
- The language server must be installed separately (e.g. `clangd`, `pyright`).
