# Lifecycle hooks (M25)

Config-driven shell commands fired at agent lifecycle points — the jichi
equivalent of Claude Code's `settings.json` hooks. A hook can observe what the
agent is doing, inject extra context, or **block** an action.

Hooks run real shell commands, so they are **opt-in**: nothing fires unless
`"hooksEnabled": true` is set, and `--no-hooks` disables them for a run. They
fire only at the **top level** (never inside a subagent) and can only *further
restrict* an action, never widen it.

## Events

| Event | When | Can block? | Context injected |
|-------|------|-----------|------------------|
| `SessionStart` | once at startup | no | appended to the system prompt |
| `UserPromptSubmit` | before a turn's model call | yes (turn is skipped) | appended as a user message |
| `PreToolUse` | before a tool runs | yes (tool refused) | — |
| `PostToolUse` | after a tool runs | no | appended to the tool result the model sees |
| `Stop` | a top-level turn finished | no | — |

`PreToolUse`/`PostToolUse` match on the tool name (see `matcher` below).

## Config

```jsonc
{
  "hooksEnabled": true,
  "hooks": {
    "PreToolUse": [
      { "matcher": "write_file|edit_file|apply_patch",
        "commands": [ { "shell": "scripts/guard.sh", "timeout": 10 } ] }
    ],
    "UserPromptSubmit": [
      { "commands": [ { "command": "scripts/inject-context", "args": [] } ] }
    ],
    "SessionStart": [
      { "commands": [ { "shell": "git rev-parse --short HEAD" } ] }
    ]
  }
}
```

- `matcher` — a tool-name glob with `|` alternation (`*`/`?` wildcards). Absent or
  empty matches every tool. Events without a tool (`UserPromptSubmit`,
  `SessionStart`, `Stop`) ignore it.
- Each command is `{command,args}` (argv form, no shell) **or** `{shell}` (run
  via `/bin/sh -c`), plus an optional `timeout` (seconds, default 30).

## What the hook receives

A JSON object on **stdin**:

```json
{ "event":"PreToolUse", "tool_name":"write_file",
  "tool_input":{...}, "cwd":"/abs/path", "session_id":"..." }
```

`PostToolUse` also gets `tool_response`/`tool_error`; `UserPromptSubmit` gets
`prompt`. Env vars `JICHI_HOOK_EVENT`, `JICHI_TOOL_NAME`, `JICHI_CWD` are also set.

## How a hook responds

- **exit 0** — proceed. For `UserPromptSubmit`/`SessionStart`/`PostToolUse`,
  anything printed to stdout becomes additional context.
- **exit 2** — block (for `PreToolUse`/`UserPromptSubmit`); stdout is the reason
  shown to the model.
- **other non-zero** — logged as a warning; the action proceeds.
- **exit 126 or 127** — the command was **not executable** or **not found**, so
  the hook **did not run at all**. The action still proceeds, but this is not
  advice: a project with a hook configured believes it is guarded. jichi names
  the cause in the warning and records it (M584).

Advanced: stdout that is a JSON object honours
`{"decision":"block","reason":"…","additionalContext":"…"}`. A hook that answers
with the JSON contract has spoken deliberately, so its exit code is **not**
treated as a failure even when it is non-zero.

> **A hook that cannot run is silent unless you look (M584).** A real project's
> config named `.jichi/hooks/zig-fmt-check.sh` after that directory had been
> removed. Every `write_file` fired a hook that exited 127; the only trace was a
> warning line, and `-q` — which every headless and autonomous run uses —
> suppressed it. The gate had been dead for the project's entire recorded
> history. Two things changed: the warning now says *"the command was not found
> or is not executable, so THIS CHECK DID NOT RUN"*, and every hook failure is
> recorded as telemetry with a bounded `outcome`
> (`start_failed` · `timeout` · `not_runnable` · `nonzero_exit`) that
> [`jichi telemetry`](TELEMETRY.md) reports. **Check yours now:**
>
>     jichi telemetry | grep -i hook
>
> A clean hook still writes nothing — this is a failure log, not a trace.

## Safety

Hooks are top-level only and opt-in. Any output a hook writes is kept **outside**
the snapshot/rollback blast radius (the audit lives under `~/.jichi.d/`,
not the workspace — see `docs/ANECDOTES.md` #1). A hung hook is killed at its
timeout and treated as non-blocking.

## Implementation

`src/chat/jc_hooks.c` (`jc_hooks_fire` + the pure, unit-tested `jc_hook_matches`
/ `jc_hook_exit_blocks`), wired into `src/chat/jc_agent.c` at the five lifecycle
points and `src/main.c` for `SessionStart`. Config structs + parsing live in
`include/jc_config.h` / `src/config/jc_config.c`. Tests: `tests/test_hooks.c`,
`tests/e2e/hooks.py`.

## When a hook misbehaves

A hook that **fails to start** or **exceeds its `timeout`** is killed, logged as a
warning, and — since M326v — recorded in telemetry as a `hook` event carrying
`outcome` (`timeout` / `start_failed`), the configured `timeout_s`, and the event
name as you spelled it in the config. Successful hooks are not telemetered:
`PreToolUse` fires on every tool call and a line each would be noise.

This exists because a hook timeout used to be a stderr line and nothing else. Asked
whether a `SessionStart` hook's 10-second timeout explained a workload's model-call
failures, 36,925 telemetry events could not answer — the hook was invisible to the
instrument. A hook that quietly spends its whole timeout every session is exactly
the cost nobody measures.
