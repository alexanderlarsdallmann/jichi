# Completion notification

Long tasks mean waiting. The notification feature (M34f / F6) lets jichi ping you
when a turn finishes — ring the terminal bell and/or run a command of your choice
— so you can step away and come back when the agent is done or wants input.

It's **opt-in** (nothing fires by default) and reuses the project's
fork/exec command runner; the front-ends fire it, the agent core and the ACP
server never do.

## When it fires

- **Interactive TUI** — after **each** completed turn (the agent has finished
  responding and is waiting on you).
- **Headless `--auto`** — once, when the unattended run completes. A plain
  interactive `-p` run does **not** notify (you're watching it); only the AUTO
  posture does.

## Configuration

```jsonc
{
  "notifyBell": true,                 // ring the terminal bell on completion
  "notify": "notify-send 'jichi' \"$JICHI_NOTIFY\""  // run a command on completion
}
```

Or per-run on the command line (these override the config):

```sh
jichi --auto --bell -p "refactor the parser"
jichi --auto --notify 'notify-send jichi \"$JICHI_NOTIFY\"' -p "..."
```

- **`notifyBell` / `--bell`** — write a BEL (`\a`) to **stderr**, so a terminal
  rings without ever corrupting stdout (a piped answer or `--output json` stays
  clean).
- **`notify` / `--notify <cmd>`** — run `<cmd>` via `/bin/sh -c` when the turn
  finishes, with a short timeout and its output discarded (a notifier must be
  quick and can't wedge the front-end). Two environment variables are exposed:
  - **`$JICHI_NOTIFY`** — a one-line summary (the session title).
  - **`$JICHI_CWD`** — the workspace directory.

Typical commands: `notify-send` (Linux desktop), `terminal-notifier` (macOS),
`tmux display-message`, or anything that posts to chat.

## Internals

- **`jc_notify_fire(command, bell, cwd, summary)`** (`src/util/jc_notify.c`) —
  writes the BEL and runs the command via the shared `jc_proc_capture` (output
  discarded, 10 s kill deadline), exporting `$JICHI_NOTIFY` / `$JICHI_CWD`. It takes
  explicit fields (not the whole `jc_app`) so the util layer stays decoupled.
- Config: `notify` (string) and `notify_bell` (bool) on `struct jc_config`,
  parsed from `notify` / `notifyBell`; CLI `--notify` / `--bell` override.
- Wiring: the TUI loop fires it after each turn's save (`src/tui/jc_tui.c`);
  `run_headless` fires it on completion when in AUTO mode (`src/main.c`).
- e2e: `tests/e2e/notify.py` runs a headless `--auto --bell --notify` turn and
  asserts the BEL reached stderr and the command ran with the env vars set.
