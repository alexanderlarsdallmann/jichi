---
name: supervise-long-command
description: When running a build, test suite, server, or any command that may take a long time or hang, run it supervised so it can be stopped instead of blocking the whole turn.
allowed-tools:
  - run_terminal_command
  - read_background_output
  - kill_background
---
A foreground `run_terminal_command` blocks until the command exits -- so a
build or test run that hangs (a wedged compiler, a server that never
returns, a test waiting on input) stalls the entire turn with no way to
recover. For any command that might be slow or hang, supervise it instead:

1. **Start it detached.** Call `run_terminal_command` with
   `run_in_background: true`. You get a background `id` back immediately --
   the command keeps running, and you keep control.

2. **Poll for progress.** Call `read_background_output` with that `id`. It
   returns the new output since your last read plus a running/exited
   status. Read again after doing a little other work, or after a brief
   pause, so you are sampling progress over time rather than once.

3. **Decide.** After each poll ask: is it still making progress, or is it
   stuck? A command is likely hung when, across two or three polls, it has
   produced no new output, its status is still "running", and enough time
   has passed for the work it should be doing (a compile that normally
   takes seconds; a test suite past its usual runtime). A server that is
   *supposed* to keep running is not hung -- recognise the difference.

4. **Stop a hang.** If you conclude it is stuck, call `kill_background`
   with the `id`, then report: what you ran, that it hung, the last output
   you saw, and what you will try next (a smaller build, a single test, a
   different flag). Do not silently retry the same hanging command.

5. **On clean exit,** read the final output and continue as normal --
   report the result (pass/fail, the relevant lines), not the whole log.

Notes:
- This is for commands that *might* hang or run long. A quick command
  (`ls`, `grep`, a one-file compile you expect to finish instantly) can
  stay a normal foreground `run_terminal_command` -- do not add ceremony to
  fast commands.
- A long-running server you started on purpose (a dev server, a watcher)
  is a legitimate background command you leave running; only `kill_background`
  it when you are done with it or when it has clearly failed to start.
- If the command's output is large but the command itself is well-behaved,
  the value here is also that its log does not flood your working context --
  read enough to judge the outcome, not every line.
