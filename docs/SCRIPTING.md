# Scripting & pipes

`jichi`'s headless mode is a well-behaved Unix filter: the assistant's
answer goes to **stdout**, every diagnostic goes to **stderr**, and exit codes
are meaningful — so it composes cleanly with pipes and scripts.

```sh
jichi -p "explain this repo" > answer.txt        # answer on stdout
cat error.log | jichi -p "what caused this?"     # stdin = context
jichi --output json -p "hi" | jq -r .text        # structured output
```

## How the prompt and stdin combine

| Invocation | stdin (TTY?) | Behavior |
|------------|--------------|----------|
| `jichi -p "P"` | TTY | Prompt is `P`. stdin not read. |
| `jichi -p "P"` | piped | Prompt is `P` + `"\n\n"` + the piped data (context). |
| `jichi -p "P" --no-stdin` | piped | Prompt is `P`. stdin ignored. |
| `jichi -p -` | any | Prompt **is** stdin (always read, even with `--no-stdin`). |
| `jichi` (no prompt) | piped | Prompt **is** all of stdin (one headless run). |
| `jichi` (no prompt) | TTY | Interactive TUI. |
| `echo … | jichi` empty stdin | piped | Exit 2 (`no prompt`). |

> **Footgun:** with a prompt and a *non-terminal* stdin, jichi reads stdin as
> context. In a script where stdin is inherited (not what you meant to send),
> redirect it: `jichi -p "P" </dev/null`, or pass `--no-stdin`.

> **Two more, refused loudly since M375** — each had silently misdelivered a
> prompt in the field (ANECDOTES #50). `-p` does **not** accept a flag-shaped
> argument: `jichi -p --no-session "Q"` used to send the model the literal
> text `--no-session` and drop `Q` unasked; it now exits 2 and names the
> flag (a prompt genuinely starting with an option-like `-` goes through
> `--prompt-b64`, or positionally after `--`; `-p -` and `"- bullet"` text
> stay valid). A stray positional beside a `-p` prompt (`jichi -p "Q"
> extra`) exits 2 instead of being ignored. And a **multi-word positional
> prompt is joined**: `jichi -- what is a symlink` asks all four words, as
> the `--` help line always promised — it used to ask the model `what`.
> M376 extends the same rule to run modes: `-p` beside a dispatched
> subcommand (`jichi -p "Q" describe` used to run `describe` and drop `Q`)
> or beside `--acp` exits 2 instead of silently picking one.

## Output format

**Is headless mode streaming? Yes — twice over** (asked often enough to
answer up front, M182): the provider connection always streams SSE, and two
of the three output formats pass it through live. `--output text` writes
each token to stdout the moment it arrives (flushed per delta, clean
broken-pipe behavior — `jichi -p … | head` just works); `--output jsonl`
streams one JSON event per text delta, tool call, tool result, and status.
Only `--output json` buffers, **deliberately**: its contract is "one
parseable object", so nothing is printed until the run ends. There is no
flag to unstream the streaming modes or stream the buffered one.

- `--output text` (default): the answer is streamed to stdout as it arrives.
- `--output json`: nothing is streamed; after the run, one JSON object is
  printed to stdout:
  ```json
  {"v":1,"type":"done","text":"…","model":"…","session_id":"…",
   "tokens":{"input":N,"output":N},"cost":0.0,"tool_calls":N,
   "aborted":false,"stop_reason":"done","work_kept":true}
  ```
  `text` is the final assistant message. Use it for robust parsing
  (`jq -r .text`). `stop_reason` is one of `done` / `interrupted` / `timeout` /
  `budget` / `verify_failed` / **`max_iters`** / `error`; on a failure an
  `error{code,type,message}` object is added. `work_kept` (M92-S1) is a bool: after a `budget` or
  `verify_failed` stop it tells whether the run's edits survived (`true`) or were
  rolled back to the last green checkpoint (`false`) — so an automation knows if
  there is partial work to resume; it is `true` whenever nothing was reverted.
  `session_id` is present when the run was persisted (omitted
  with `--no-session`). The object is emitted for **every** terminal state, so a
  failed run still yields a parseable result. `"v"` is the schema version.

### `max_iters`: the turn stopped, the task did not fail (M322)

`stop_reason: "max_iters"` means the turn hit **`maxToolIters`** — the per-turn
tool-iteration cap. This is the one stop reason that is **neither success nor failure**, and
a driver has to treat it as its own case:

- **The exit code is 0 and nothing is rolled back.** The cap is a circuit breaker against a
  runaway loop, not a verdict on the work. Everything the turn did is real and committed.
- **`text` is usually empty**, because the model was mid-tool-loop and never wrote a final
  answer. An empty `text` with `stop_reason: "done"` used to be all a driver saw — that is what
  this value fixes.
- **The history is intact, so another prompt resumes.** Send any follow-up (`-c` / `--continue`,
  or the same session id) and the model sees its own tool results and carries on with a fresh
  iteration budget. `"continue"` works; so does a more specific instruction.
- **A resume reconciles the model with the disk (M350).** The restored history describes files
  as they *were*; if any file the conversation's successful file-tool calls touched has a
  strictly newer mtime than the session's last save — or is gone — one `[resume]` user-role
  note names the drifted files (up to 8, then a count) and asks for a re-read, and the session
  is saved immediately so a second resume detects nothing twice. An unchanged workspace injects
  nothing; a fresh session has no "last ran" to drift from. Scope is the four file tools'
  `path` arguments (a stale `search_code` result cannot be attributed to files from its
  arguments — a stated limit, not a hidden one). Applies to `-c`/`--continue`/`--session` and
  the TUI's `/resume`; ACP `session/load` is deliberately untouched (the editor replays the
  transcript under its own contract).

A supervisor loop therefore wants roughly:

```sh
out=$(jichi -p "$task" --output json --continue)
case $(printf '%s' "$out" | jq -r .stop_reason) in
  done)       ;;                                  # finished
  max_iters)  # more work to do -- nudge, and count the nudges
              nudges=$((nudges + 1))
              [ "$nudges" -lt 5 ] && jichi -p "continue" --output json --continue ;;
  budget|verify_failed) ;;                        # see work_kept
  *)          ;;                                  # interrupted / timeout / error
esac
```

**Count the nudges.** An unbounded resume loop is a runaway loop with extra steps — which is
what `maxToolIters` existed to prevent. Bound the nudges, or bound the whole run with
`--budget-tokens` / `--deadline` (see [AUTONOMY.md](AUTONOMY.md)) and let the envelope stop it.

**Why not raise `maxToolIters` instead?** You can, and for a genuinely long task you should
(the default is 25; under an autonomy envelope jichi already forces at least 200, because
there the real bound is the budget). But the cap also catches the failure it was built for: a
model looping on the same tool. Raising it converts a fast, visible stop into a slow, expensive
one — so raise it deliberately, and keep an outer bound.
  **Run economics (M97)** for a driving agent are also included:
  `starved` (bool — the M96 all-reads-no-synthesis read-heavy bust),
  `budget_kind` (which budget tripped: `tokens`/`deadline`/`toolcalls`, or absent),
  `budget:{used,limit}` (token budget), `peak_input` (largest single-call input
  tokens — the context-ramp signal), `cache:{read,write}` (cache tokens; 0 on a
  cacheless backend), and `tools:{read,write,shell,other}` (the per-run tool mix).
  A driver reads these to decide whether to re-scope, raise the budget, split the
  task, or switch backend — e.g. `starved:true` + a high `tools.read` means the run
  over-read and produced nothing, so narrow `--reference-root` and retry.
- `--output jsonl`: **streaming structured events** — one JSON object per line,
  written to stdout *as the run unfolds*, so an automation or another AI agent
  can monitor progress in real time and still get the rich final result. Event
  `type`s:
  ```jsonc
  {"v":1,"type":"run_start","run":"<uuid>"}
  {"v":1,"type":"message_start","model":"…","mode":"…"}
  {"v":1,"type":"text","delta":"partial answer text"}
  {"v":1,"type":"tool_call","name":"read_file","args":"{\"path\":\"x\"}"}
  {"v":1,"type":"tool_result","name":"read_file","is_error":false,"preview":"…"}
  {"v":1,"type":"usage","input":N,"output":N,"cost":0.0}
  {"v":1,"type":"status","line":"retry 1/3 in 500ms (transient)"}
  {"v":1,"type":"done", … same fields as the json object above … }
  ```
  **`tool_call.id` / `tool_result.id` pair a call with its result** (M442). It is the
  provider's own `tool_calls[].id`, present whenever the provider sent one. Pair by it,
  never by order: a round with two calls to the *same* tool is indistinguishable by name,
  and order is exactly what a reordered or concurrent result set does not preserve.

  **`done.degraded` names decisions the run made in your absence** (M444's sibling,
  M443). It is an object — `unanswered`, `approval_unavailable`, `privilege_refused` —
  and it is **present only when at least one occurred**, so the test is
  `if ("degraded" in done)`, not a boolean compare. `unanswered` counts `ask_user`
  questions the run answered for itself; `approval_unavailable` counts tools refused only
  because nobody could approve them; `privilege_refused` counts privileged or kinetic
  actions refused for want of a confirmer. A tool **auto-approved under `--auto` is not
  counted** — that is your instruction, not a decision taken for you, and counting it
  would set the flag on every unattended run. `stop_reason` is unaffected: the block
  reports, it does not judge.

  **`tool_result.preview` is capped at 512 bytes and cut on a UTF-8 boundary**
  (M439). Before M439 the cap was applied in bytes with no regard for character
  boundaries, so a preview of non-ASCII output could carry a half-written character
  into the JSON string — and 512 is not a multiple of 3, so for text made of 3-byte
  characters that was the normal outcome, not a rare one. A strict parser may reject
  such a line outright, which turned a truncated preview into a lost event. The cut
  now lands between characters, so the field is always well-formed; it may therefore
  be a few bytes shorter than the cap.

  **`run_start` (M431c) is the join key, delivered first.** `run` is the envelope's
  run id — the same value the audit journal stamps on every line and telemetry
  stamps on every event. Before M431c it appeared in neither machine surface, so a
  supervisor could not correlate a worker's stdout with its own audit trail unless
  it had passed `--journal <path>` itself and remembered the path. It arrives
  **before the work starts**, which is the point: a supervisor can begin tailing
  `~/.jichi.d/runs/<run>.jsonl` while the run is still live. The terminal `done`
  object carries the same `run` for a driver that only keeps the last line.

  Emitted **only when an envelope is armed** — any `--budget-*`, `--verify`,
  `--edit-scope`, `--journal`, `--max-tool-calls`/`--max-reads` flag, or a config
  `verify`/`editScope`. A plain `-p` run has no run id and no journal, so there is
  nothing to correlate; do not wait for the event unconditionally.

  The `status` event (M99) surfaces mid-run adaptation the driver would otherwise
  only find in the separate telemetry file: transient **retries**/backoff, model
  **route** escalations (fast→strong on a stall/verify-fail), context
  **compaction**, and periodic-verify banking. Free-form `line` (human-readable);
  a driver can log it or match on it (e.g. count retries) while relying on the
  terminal `done` for the structured outcome.
  Read line-by-line, parse each as JSON, branch on `type`, and treat `done` as
  the terminal event. This is the recommended path for agents driving jichi that
  want live tool/usage visibility without the bidirectional ACP protocol.

For mid-run *control* (approve/deny individual tools, cancel a turn), use the
**ACP** server (`jichi serve` / `--acp`, see [ACP.md](ACP.md)) — headless
is fire-and-forget; ACP is bidirectional.

## Three levels of progress on stderr

stdout is always the answer alone. What changes is how much jichi tells you about
what it is *doing* while it works, and all of it goes to stderr (M326h):

| | stderr during a turn | reach for it when |
|---|---|---|
| `-q` / `--quiet` / `--silent` | nothing | you want a clean capture, or a supervisor parses stdout / jsonl |
| *(default)* | one bounded line per tool call — `[tool] write_file  out.txt` — plus its result and a token line | a human is watching an `--auto` run |
| `-v` / `--verbose` | the same, plus the raw argument JSON and debug logging | you are diagnosing a malformed tool call |

The default line is the **summary**, not the arguments: the subject of the call
(a path, a command, a query) rendered by the same `jc_tool_arg_summary` the TUI
uses for its `▸` lines. Before M326h it was the raw argument JSON, so a
`write_file` of a large file wrote the whole file to stderr.

`-q` silences everything diagnostic (tool logs, token/usage lines, MCP/resume
notices, the no-API-key warning, debug logs). Genuine errors and usage errors
still print. Every level combines with any output format.

**Do not parse any of it.** stderr is not an interface — see
[`EMBEDDING.md`](EMBEDDING.md). The stable machine surface is `--output jsonl`,
whose `tool_call` events carry the **complete** raw arguments: the bounding above
is for human eyes only, and the two are deliberately different.

## Self-contained config with `--config-json`

For a run that carries its own configuration — no file on disk — pass the whole
config as inline JSON:

```sh
jichi --config-json '{"models":[{"name":"m","provider":"openai",
  "model":"gpt-4o-mini","apiBase":"https://api.example.com",
  "apiKeyEnv":"OPENAI_API_KEY"}]}' --no-session -p "explain this repo"
```

The string is parsed as **the** config: a single explicit source, with no
global/project merge (exactly like `--config <path>`). This makes a headless or
remote run fully self-contained in one command — ideal for SSH-ing into a fresh
box, CI jobs, or one agent driving another (see [REMOTE_SSH.md](REMOTE_SSH.md)).

- `--config-json` and `--config` are **mutually exclusive**.
- Empty input or malformed JSON is a usage/runtime error (non-zero exit).
- **Security**: the JSON appears in `ps`/process listings, so use `apiKeyEnv` (an
  environment-variable *name*) — never a literal `apiKey`. `doctor` warns if you
  embed one, and jichi never prints the value.
- **`ARG_MAX`**: a very large config can exceed the OS argument-length limit; use
  `--config <file>` or `--config-json -` (below) for those.

### Transport variants (base64, stdin)

Inline JSON is awkward to quote (nested `"`, `$`, newlines) and — like any argv —
visible in `ps`. Two variants address that (see
[REMOTE_SSH.md](REMOTE_SSH.md) for the SSH recipes):

```sh
# base64: one flat, quoting-safe token (still visible in ps -- NOT secrecy):
B64=$(printf '%s' "$CONFIG_JSON" | base64 -w0)
jichi --config-json-b64 "$B64" -p "…"

# stdin: keeps the config OFF argv (not in ps); prompt then via -p / --prompt-b64:
printf '%s' "$CONFIG_JSON" | jichi --config-json - -p "…"
# (--config-stdin is an alias for `--config-json -`)

# base64 prompt (multiline / quote-safe):
jichi --config-json - --prompt-b64 "$(base64 -w0 < prompt.txt)" < config.json
```

- All config sources — `--config`, `--config-json`, `--config-json-b64`,
  `--config-stdin`/`--config-json -` — are **mutually exclusive** (one source).
- **base64 is transport, not secrecy** — it is trivially decodable and still shows
  in `ps`; `apiKeyEnv` is still mandatory. To keep the config out of `ps`, use the
  **stdin** form; for confidentiality at rest / over an untrusted hop, decrypt into
  the pipe with tooling you trust — `age -d cfg.age | jichi --config-json -
  -p "…"` — rather than any built-in cipher (jichi has none, by design).
- Config-on-stdin **consumes stdin**, so the prompt must come from `-p "text"` or
  `--prompt-b64` (not a stdin pipe); jichi errors clearly if both want stdin.
- `--prompt-b64` and `-p` are mutually exclusive.

## Other flags

- `--no-session` — don't persist the run under `~/.jichi.d/sessions` (good for
  automated/scripted invocations). `--resume` still loads a prior session.
- `--` — end of options; everything after it is the prompt, so a prompt that
  starts with `-` works: `jichi -- -weird`.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | success (also: downstream pipe closed early, e.g. `… | head`) |
| 1 | runtime error (provider/network/IO/…); message on stderr |
| 2 | usage error (bad/unknown flag, `--output` value, empty prompt) |
| 130 | interrupted (SIGINT / Ctrl-C) |
| 143 | terminated (SIGTERM, graceful — M146: the turn aborts, teardown runs; a second SIGTERM terminates immediately) |

A closed downstream pipe (`jichi … | head`) is detected and the run stops cleanly
with exit 0 — no `SIGPIPE` crash.

## Notes & limits

- **Custom commands**: a prompt that is exactly `/name args` (no appended stdin)
  is expanded as a [custom command](COMMANDS.md) before the turn.
- **Text prompts only**: stdin is treated as text and truncated at the first NUL
  byte — don't pipe binary data as the prompt.
- CRLF in stdin is passed through unchanged.
- `-q` overrides `-v` (quiet wins).
