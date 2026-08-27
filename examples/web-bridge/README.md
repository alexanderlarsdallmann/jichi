# jichi web bridge (minimal, single-file)

A ~1-file Python-**stdlib** web front-end that watches jichi autonomous
runs from a browser (or a phone over an SSH tunnel): submit a task, watch its
`--output jsonl` events stream in as Server-Sent Events, see its cost, and
cancel it. Nothing more — by design.

This is the *minimal alternative track* from the design proposal
[`docs/proposals/2026-07-web-frontend.md`](../../docs/proposals/2026-07-web-frontend.md)
and the operator manual [`docs/WEB_FRONTEND.md`](../../docs/WEB_FRONTEND.md).
It adds **no C and no dependency** to jichi; it drives the already-stable machine
contract (`-p … --output jsonl`, `--config-stdin`, graceful SIGINT).

## Run it

```sh
# 1. Point the config at your model; keep the key as an env-var *name*.
cp config.example.json config.json     # edit apiBase/model
export JICHI_API_KEY=sk-...               # the name the config references

# 2. Start the bridge (binds 127.0.0.1; prints a one-time token URL).
JICHI_BIN=../../jichi python3 bridge.py --config config.json --root "$PWD"
#   -> BRIDGE READY http://127.0.0.1:8765/?token=XXXXXXXX
```

Open the printed URL. The `token` is a capability — treat the URL like a
password. `--root` lists the workspace directories a run may target (repeatable;
defaults to the current directory).

## What it does — and what it deliberately doesn't

| Capability | This bridge | Graduate to Phoenix (the proposal) |
|---|---|---|
| Watch an autonomous run, cancel, see cost | ✅ | ✅ |
| Interactive chat with **tool approvals** | ❌ (headless has no ask channel — autonomous, envelope-bounded runs only) | ✅ (ACP `session/request_permission`) |
| Session resume / attach | ❌ | ✅ |
| Multi-instance governance | one mutex per workspace | leases, queues, caps |
| Auth beyond a localhost bind | ❌ | ✅ (real accounts) |
| Telemetry dashboards | ❌ | ✅ |

The moment you want approvals in the browser you are re-implementing the ACP
SessionServer state machine here — that is the sign to graduate.

## Security (read this)

This service is **remote code execution by design**: a click drives an agent
that runs shell commands as your Unix user. Accordingly:

- **Binds `127.0.0.1`.** A non-local bind is refused unless you pass
  `--allow-remote` *and* set `--token`. The sanctioned remote path is an SSH
  tunnel (`ssh -L 8765:127.0.0.1:8765 host`) or a mesh VPN — never a public
  listener.
- **Boot token** (the Jupyter model): printed once, required on every request;
  a wrong/absent token gets `403`.
- **Secrets stay off argv.** The config is fed to each child on stdin
  (`--config-stdin`); keep API keys as `apiKeyEnv` *names* so they live only in
  the bridge's environment, never in `/proc/*/cmdline`.
- **Every run is envelope-bounded** (`--auto` with `--budget-tokens`,
  `--deadline`, `--max-tool-calls`; tune the defaults or pass per-run overrides
  in the `POST /run` body).
- **Least privilege:** run the bridge as a dedicated low-privilege user. jichi's
  path fence and edit scope bound the agent, but an approved run still executes
  shell — the Unix user is the real boundary.

## HTTP surface

| Method | Path | Purpose |
|---|---|---|
| `GET`  | `/?token=…` | the inline single-page UI |
| `POST` | `/run` | `{prompt, workspace?, budget_tokens?, deadline?, max_tool_calls?, heartbeat?}` → `{id, workspace}` |
| `GET`  | `/runs` | in-memory list of runs |
| `GET`  | `/runs/<id>/events` | SSE stream of jsonl events (`text`/`tool_call`/`tool_result`/`usage`/`heartbeat`/`done`) |
| `POST` | `/runs/<id>/cancel` | SIGINT the run's process group (graceful; exit 130) |

The event shapes are jichi's own `--output jsonl` contract — see
`jichi describe --output json` (the `jsonl` block) and
[`docs/SCRIPTING.md`](../../docs/SCRIPTING.md). Pass `heartbeat` (seconds) to get
periodic `heartbeat` events so the UI can tell a long model call from a wedged
process.
