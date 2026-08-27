# ACP server

jichi can run as an **Agent Client Protocol (ACP)** server, so an editor
(e.g. Zed) can drive it as an agent: the editor opens a session, sends prompts,
and renders the streamed reply, tool activity, and permission requests jichi sends
back. This is the inverse of jichi's MCP/LSP roles, where jichi is the *client* —
here jichi is the *server* and the editor is the client.

```sh
jichi serve --acp      # speak ACP over stdin/stdout
jichi --acp            # equivalent
```

The editor spawns that command and exchanges newline-delimited JSON-RPC 2.0
messages over the process's stdin/stdout. All of jichi's normal configuration
applies — the active model, tools, MCP servers, LSP, snapshots, permissions,
project rules, and the repository map are set up exactly as for the TUI.

## Protocol

Transport is **newline-delimited JSON-RPC 2.0** (one compact message per line,
never containing an embedded newline) over stdin/stdout — the same framing as
the MCP stdio transport, with the roles reversed. jichi speaks protocol
version **1**.

### Methods jichi handles (client → agent)

| Method | Effect |
| --- | --- |
| `initialize` | Returns the protocol version + agent capabilities (`loadSession:true`, prompt capabilities, no auth); reads the client's fs + terminal capabilities to enable filesystem / terminal delegation (see below). |
| `authenticate` | No-op success (jichi needs no ACP-level auth; model API keys come from its own config). |
| `session/new` | Starts a fresh conversation, returns a generated `sessionId` (also the jichi session id). |
| `session/load` | Reloads a previously-persisted session by id and replays its transcript (see below). |
| `session/prompt` | Runs one turn against the model and returns `{stopReason}` when it completes. |
| `session/cancel` | (Notification) aborts the in-flight turn. |

### Session persistence & `session/load`

ACP sessions are **persisted to the same store as the CLI** — the JSON files
under `~/.jichi.d/sessions/<id>.json` — and **the ACP `sessionId` *is* the jichi
session id**. `session/new` creates a session and returns its id; after every
`session/prompt` turn jichi saves the updated history (and auto-titles it from the
first user message), exactly as the TUI/headless paths do. `--no-session`
suppresses persistence here too.

Because the ids are unified, a session started in the TUI can be reloaded from an
editor and vice-versa. On `session/load` jichi loads the stored history and
**replays the transcript** to the client as `session/update` notifications, in
order, *before* returning the load response (per ACP, the response means "replay
complete"):

| Stored message | Replayed as |
| --- | --- |
| user turn | `user_message_chunk` |
| assistant text | `agent_message_chunk` |
| assistant tool call | `tool_call` (`status:"completed"`) |
| tool result | `tool_call_update` (`completed`/`failed`) |

*Beginner view:* close your editor, reopen it, point it at the same session id,
and the whole prior conversation reappears — no model call, no network.

*Design decisions (advanced):*
- **One id space.** Rather than invent ACP-only session ids and a parallel store,
  the ACP sessionId is the jichi session id, so the existing `jc_session`
  persistence (and the `ls`/`--continue`/`--session` CLI surfaces) just work for
  editor-driven sessions. The session's saved *mode* is restored on load.
- **Replay is pure-data, never a model call.** Loading walks the saved history
  and emits notifications built by the same pure `jc_acp_proto` builders used for
  live streaming (`jc_acp_update_user_message_chunk` was added for the user side).
  That keeps load offline, deterministic, and unit/E2E-testable without a model.
- **Per-session arena.** The loaded session's strings live in a dedicated arena
  that is freed when the session is replaced or the connection closes, so
  repeated new/load cycles in one connection don't accumulate memory.

### Messages jichi sends (agent → client)

During a `session/prompt` turn, jichi streams `session/update` **notifications**:

| `sessionUpdate` | When |
| --- | --- |
| `agent_message_chunk` | Each streamed delta of the assistant's text. |
| `tool_call` | A tool is about to run (`toolCallId`, `title`, `kind`, `status:"in_progress"`, `rawInput`). |
| `tool_call_update` | The tool finished (`status:"completed"`/`"failed"` + its output as content). |

When a tool needs approval (the same `jc_perm` policy as everywhere else — ASK
in chat mode for mutating tools), jichi sends a `session/request_permission`
**request** and blocks until the client answers:

```jsonc
// agent -> client
{"jsonrpc":"2.0","id":1,"method":"session/request_permission",
 "params":{"sessionId":"…","toolCall":{"toolCallId":"…","title":"write_file notes.txt","kind":"edit"},
           "options":[{"optionId":"allow_once","name":"Allow","kind":"allow_once"},
                      {"optionId":"allow_always","name":"Always allow","kind":"allow_always"},
                      {"optionId":"reject_once","name":"Reject","kind":"reject_once"}]}}
// client -> agent
{"jsonrpc":"2.0","id":1,"result":{"outcome":{"outcome":"selected","optionId":"allow_once"}}}
```

- `allow_once` → run the tool this time.
- `allow_always` → run it, and auto-allow that tool for the rest of the session.
- `reject_once` (or any non-selection) → refuse the tool (returned to the model
  as an error, so it can adjust).
- `{"outcome":"cancelled"}` → treated as a cancel: the turn aborts.

A `session/cancel` notification arriving while a permission request is
outstanding also aborts the turn.

### `stopReason`

`session/prompt` returns `{"stopReason":"end_turn"}` normally, or
`{"stopReason":"cancelled"}` if the turn was cancelled.

### Client-side filesystem delegation

If the editor advertises filesystem capabilities in `initialize`
(`clientCapabilities.fs.readTextFile` / `writeTextFile`), jichi routes its file
tools through the editor instead of touching disk directly:

| jichi tool | Delegated request |
| --- | --- |
| `read_file`, `edit_file` (the read half) | `fs/read_text_file` |
| `write_file`, `edit_file` (the write half) | `fs/write_text_file` |

*Beginner view:* this means jichi sees the file **as it currently is in your
editor** — including edits you haven't saved yet — and its writes land in the
editor's buffer, so nothing is lost or fought over between jichi and your unsaved
work. If the editor doesn't offer fs (or jichi runs in the TUI/headless), tools
read and write the real files on disk exactly as before.

*Design decisions (advanced):*
- **Capability-gated, off by default.** The delegate is installed only when the
  client advertises the capability. A `jc_fs_delegate` (read/write function
  pointers) is set on `jc_app`; the file tools call `jc_app_read_file` /
  `jc_app_write_file`, which use the delegate when present and **fall back to
  disk** if it's absent or returns an error. In the TUI/headless paths `app->fs`
  is NULL, so the disk path is byte-for-byte unchanged (and the headless/SSH
  contract is preserved).
- **Same blocking round-trip as permissions.** A delegated read/write is a
  JSON-RPC request to the client that jichi blocks on — reusing the exact
  `acp_request_await` helper that drives `session/request_permission` (so an
  interleaved `session/cancel` still aborts cleanly). No new concurrency model.
- **Absolute paths.** ACP's fs methods require absolute paths, so the delegate
  resolves the tool's (workspace-relative) path against the cwd before sending.
- **Graceful degradation.** If the editor can't serve a path (no open buffer,
  error response) jichi silently falls back to reading/writing the disk copy, so a
  partial fs implementation in the client never breaks a tool.

### Client-side terminal delegation

If the editor advertises the terminal capability in `initialize`
(`clientCapabilities.terminal:true`), jichi runs shell commands **in the editor's
terminal** instead of a local subprocess:

| jichi tool | Delegated requests |
| --- | --- |
| `run_terminal_command` | `terminal/create` → `terminal/wait_for_exit` → `terminal/output` → `terminal/release` |
| `run_tests` | same lifecycle |

*Beginner view:* the command runs in (and is visible in) your editor's terminal
pane, in the editor's environment, and the editor can show or kill it — while jichi
still gets the captured output and exit code to feed back to the model. If the
editor doesn't offer terminals (or jichi runs in the TUI/headless), commands run in
a local `/bin/sh` subprocess exactly as before.

```jsonc
// agent -> client : start the command (always via /bin/sh -c)
{"jsonrpc":"2.0","id":1,"method":"terminal/create",
 "params":{"sessionId":"…","command":"/bin/sh","args":["-c","make test"],
           "cwd":"/abs/work","outputByteLimit":65536}}
// client -> agent
{"jsonrpc":"2.0","id":1,"result":{"terminalId":"t1"}}
// agent -> client : wait for it, then read the captured output, then release
//   terminal/wait_for_exit -> {"exitStatus":{"exitCode":0}}
//   terminal/output        -> {"output":"…","truncated":false,"exitStatus":{…}}
//   terminal/release       -> {}
```

*Design decisions (advanced):*
- **One predicate, one delegate.** Capability-gated like fs: a `jc_cmd_delegate`
  (a single `run` function pointer) is installed on `jc_app` only when the client
  advertises `terminal`. The run/test tools call `jc_app_run_command`, which uses
  the delegate when present and **falls back to a local `/bin/sh` subprocess** if
  it's absent or fails — so the TUI/headless path (`app->cmd == NULL`) is
  byte-for-byte unchanged.
- **Same blocking round-trips.** Each `terminal/*` call reuses `acp_request_await`
  (the helper behind permissions and fs), so an interleaved `session/cancel`
  aborts cleanly: jichi best-effort `terminal/kill` + `terminal/release`s the
  terminal and returns without re-running the command locally (never a
  double-execution of a possibly-mutating command).
- **Live terminal in the tool card.** After `terminal/create`, jichi sends a
  `tool_call_update` whose content embeds `{type:"terminal",terminalId}`, so the
  editor can stream the running command's output inside the tool call's UI.
- **Normalized exit code.** Both paths report the real exit code (`exitCode`, or
  `128+signal` when a process is killed), so `[exit status: N]` reads the same
  whether the command ran in the editor or locally.

## Image prompt content (M29d)

`promptCapabilities.image` is advertised **true when the active model is
vision-capable** (config `"vision": true`). Image blocks
(`{type:"image", data:<base64>, mimeType}`) in a `session/prompt` are parsed by
`jc_acp_prompt_images` and attached to the turn's user message (the data is
already base64, so it's used directly — no file read). When the active model
isn't vision-capable the capability is advertised false and any image blocks are
ignored. Text and embedded text resources / resource links are read as before.
See docs/VISION.md.

## Audio prompt content (M33)

`promptCapabilities.audio` is advertised **true when a `transcribe`-role model is
configured**. Audio blocks (`{type:"audio", data:<base64>, mimeType}`) in a
`session/prompt` are decoded, transcribed via that model
(`/v1/audio/transcriptions`), and the transcript is folded into the turn's user
message text — so the model sees the speech as text. When no transcribe-role
model exists the capability is advertised false and audio blocks are ignored.
See docs/TRANSCRIBE.md.

## What's not implemented

- **Code fill-in-the-middle as an ACP method.** The headless `fim` subcommand
  exists (editor "tab autocomplete"), but ACP defines no standard completion
  method; exposing FIM would mean a non-portable vendor extension. It's left to
  the `fim` subcommand (which an editor can shell out to) until ACP standardizes
  one.
- **Code fill-in-the-middle.** The roadmap's M9b (editor "tab autocomplete" via
  prefix/suffix) is a natural next ACP method, reusing the `autocomplete` model
  role and the one-shot call behind the headless `complete` subcommand.

## Internals

- **Pure shaping** — `src/acp/jc_acp_proto.c` (`include/jc_acp.h`): the
  JSON-RPC response/error envelopes, the `initialize` result, the `session/update`
  payload builders, the `request_permission` params, the permission-outcome
  parser, the prompt-text concatenator, the tool-kind classifier, and the
  filesystem-delegation shapes (client fs-capability parser, `fs/read_text_file`
  / `fs/write_text_file` param builders, read-result parser), and the
  terminal-delegation shapes (terminal-capability parser, `terminal/create` +
  shared `{sessionId,terminalId}` param builders, terminalId / exitStatus /
  output result parsers, and the live-terminal tool-card update). All pure and
  unit-tested offline (`tests/test_acp.c`); the JSON-RPC request/notification
  envelopes are reused from `src/mcp/jc_mcp_proto.c`.
- **Filesystem delegate** — `jc_fs_delegate` on `jc_app` (`include/jc_app.h`),
  routed by `jc_app_read_file`/`jc_app_write_file`; the ACP server installs
  `acp_fs_read`/`acp_fs_write` (which `acp_request_await` the editor) when the
  client advertises fs. The disk fallback keeps the TUI/headless paths
  unchanged. The routing decision is unit-tested with a stub delegate.
- **Terminal delegate** — `jc_cmd_delegate` on `jc_app`, routed by
  `jc_app_run_command` (used by both `run_terminal_command` and `run_tests`); the
  ACP server installs `acp_cmd_run` (create → wait → output → release, with a
  cancel-time kill/release) when the client advertises `terminal`. The local
  `/bin/sh` fallback keeps the TUI/headless paths unchanged. The routing decision
  is unit-tested with a stub delegate (real local `echo` for the fallback); the
  full editor round-trip is covered by the model-gated `tests/e2e/acp_terminal.py`
  (a Python ACP client that services the `terminal/*` requests).
- **Server loop** — `src/acp/jc_acp.c` (`jc_acp_serve`): newline-framed stdin
  reader, the method dispatch, and the agent-callback → notification mapping.
  `on_assistant_text` → `agent_message_chunk`; `on_tool_start`/`on_tool_result`
  → `tool_call`/`tool_call_update`; `confirm_tool` → the blocking
  `session/request_permission` round-trip. Single-threaded and synchronous: a
  prompt drives `jc_agent_run_turn`, whose callbacks write notifications inline.
  Sessions live in a `jc_session` backed by a per-session arena (`new_session`/
  `clear_session`), persisted after each prompt (`save_session`) and rebuilt by
  `handle_load` → `replay_history` on `session/load`.
- **CLI** — `serve` (or `--acp`) selects the ACP front-end in `main.c` after the
  app is fully configured (the same setup the TUI gets).
