# Background commands (M26)

Long-running commands — dev servers, file watchers, slow builds — can be started
**detached** so the agent keeps working instead of blocking on them. This is the
jichi equivalent of Claude Code's `run_in_background` + `BashOutput` + `KillShell`.

## Tools

- `run_terminal_command` gains a `run_in_background` boolean. When true it starts
  the command detached and returns a **background id** immediately (no output).
- `read_background_output` (`{id}`) — return the output produced since the last
  read, plus the running/exited status and exit code. (Claude Code's BashOutput.)
- `kill_background` (`{id}`) — SIGTERM, brief grace, then SIGKILL the whole
  process group. (Claude Code's KillShell.)

```
run_terminal_command{command:"npm run dev", run_in_background:true}  -> id 1
read_background_output{id:1}   -> recent stdout/stderr + "[background 1: running]"
kill_background{id:1}          -> "Killed background process 1."
```

## Behaviour

- Output is captured into a per-process buffer (capped at 256 KB; truncation is
  flagged). Children are drained and reaped opportunistically at each tool-call
  boundary, and on demand when read.
- Background children intentionally **survive an aborted turn** — that is the
  point of detaching. They are SIGTERM/SIGKILL'd and reaped at **session exit**.
- The registry is bounded (`JC_BG_MAX` = 8). Starting a 9th returns an error
  telling the model to kill one first.

## Constraints

Background mode uses the **local** fork/exec path only. When an editor drives jichi
over ACP and provides a terminal delegate, that delegate is a single blocking
call with no poll/kill surface, so `run_in_background` reports that it is
unavailable there — run such commands in the foreground (the editor shows them in
its own terminal).

## Implementation

`src/chat/jc_bg.c` / `include/jc_bg.h` — a bounded registry on `jc_app` (each
entry owns a pid + non-blocking pipe; malloc/free managed, reaped in
`jc_bg_mgr_free`), reusing the fork/`select`/SIGTERM discipline of the parallel
pool. The tools live in `src/tools/jc_tool_bg.c`; `run_in_background` is handled
in `src/tools/jc_tool_run.c`. Tests: `tests/test_bg.c`, `tests/e2e/bg.py`.
