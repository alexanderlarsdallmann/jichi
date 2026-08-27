# Proposal: an optional wall-clock timeout for run_terminal_command

**Status: IMPLEMENTED (2026-08, M226) — off by default, opt-in.**
Motivated by the observation (2026-08-01) that a foreground
`run_terminal_command` has no wall-clock bound, so a hung build or test
blocks the whole turn with no path for the agent to recover — the
`supervise-long-command` skill (`examples/skills/`) is the advisory
counterpart; this proposal is the mechanism that makes the guarantee
real rather than prompt-dependent.

> **Implementation note (one refinement from the design).** `--lite` does
> **not** set a conservative default timeout after all: the value of a
> timeout is catching an *infinite* hang, but a wall-clock cap also kills a
> *slow-but-progressing* build — and `--lite`'s target is slow hardware,
> where everything legitimately takes longer. An aggressive lite default
> would misfire exactly where it hurts most. The timeout is therefore
> purely opt-in on every profile; distinguishing a hang from slow progress
> is the skill's job (poll for output), the timeout is the blunt backstop.

## Today

`run_terminal_command` (`src/tools/jc_tool_run.c`) runs a command through
`jc_app_run_command` (`src/chat/jc_app.c`). The default path is a blocking
`popen` with **no time limit**: a wedged compiler, a server that never
returns, or a test waiting on stdin stalls the agent loop until the user
sends SIGINT. Three facts make the gap sharp:

- The **only** self-rescue today is `run_in_background: true` +
  `read_background_output` + `kill_background` — which the model reaches
  for rarely (the affordance gap this pairs with), and which it must
  *choose* per command.
- Output is already capped (`runMaxBytes` / `jc_config_cap`), but a cap on
  *bytes* does nothing for a command that hangs producing *no* output.
- The machinery to enforce a timeout already exists in two places:
  `jc_app.c:run_command_watched` (a fork path, used when `mem_budget_mb`
  is set, that tracks the child and can kill a runaway) and
  `src/util/jc_proc.c:jc_proc_capture` (which already takes a `timeout`
  and SIGKILLs on expiry for user tools and hooks). Nothing wires a
  wall-clock timeout into the model-facing foreground path.

## What this proposes

A resolved-by-precedence **`runTimeout`** (seconds; `0`/unset = no limit,
preserving today's behaviour exactly), mirroring the existing
`runMaxBytes` cap and the model-call `timeouts` block:

- config `runTimeout` (global);
- `--run-timeout <s>` CLI;
- per-call: an optional `timeout` argument on `run_terminal_command`
  itself, so the *model* can say "give this build 120s" — which is the
  point, because the model knows what it is running (a one-file compile
  vs a full suite) better than a global default can.

Precedence: per-call `timeout` > `--run-timeout` > config `runTimeout` >
unset (no limit). `--lite` may set a conservative default (e.g. 300s) so
small/embedded targets are not left hanging, matching how `--lite` already
tightens the other caps.

When a command exceeds its limit, jichi **kills the process group**
(SIGTERM, then SIGKILL after a short grace — the `jc_proc_capture` and
`run_command_watched` pattern) and returns a tool **result value**, not a
control-flow error (the house rule): the model sees, e.g.

```
error: command timed out after 120s and was terminated.
[last 2 KB of output before the kill]
```

so it can adapt (narrow the build, run one test, add a flag) exactly as it
does for any other tool error — the same self-correction loop the whole
agent is built on.

## Design sketch

1. **Resolve the effective timeout** with a pure helper beside
   `jc_config_cap` (call it `jc_config_run_timeout(per_call, cli, cfg)`),
   unit-tested for the precedence table — the pattern the model-call
   `jc_config_resolve_timeouts` already establishes.
2. **Route foreground runs with a nonzero timeout through the watched
   fork path.** `run_command_watched` already forks, tracks the child by
   pgid, samples in a `select` loop, and kills on a condition (RSS today);
   generalise its stop condition to "RSS over budget **or** wall-clock
   over `runTimeout`". A `0` timeout keeps the plain `popen` path
   untouched, so the default build changes nothing.
3. **Kill the group, not just the pid** (`setpgid` in the child, `killpg`
   on expiry) so a `make`-spawned compiler subtree dies with it — a
   timeout that leaves orphans is worse than none.
4. **Return the partial output + a timeout marker**; set the tool result's
   `is_error`. The background tools are unchanged (they already have an
   explicit `kill_background`).
5. **Surface it**: `doctor` notes the effective `runTimeout`;
   `--output jsonl` already carries `tool_result` with `is_error`, so a
   supervisor sees the timeout without new event types.

## Risks and non-goals

- **A legitimate slow build must not be killed.** This is why the default
  is *off* and the per-call `timeout` argument exists: the safe rollout is
  advisory-first (the skill) and opt-in timeout second, never a surprise
  default that reverts a long CI build mid-run. `--lite`'s conservative
  default is the one exception, justified by its target hardware.
- **Interaction with the terminal delegate (ACP):** an editor running the
  command in its own terminal (`app->cmd`) owns its own lifecycle; the
  timeout applies to the local fork path only, and the delegate path is
  documented as out of scope (as `run_in_background` already is there).
- **Not a replacement for background supervision.** A server you *want*
  to keep running still uses `run_in_background`; the timeout is for
  commands expected to *finish*. The skill teaches the model to tell them
  apart; the timeout is the backstop for when it does not.
- **Small models over-trigger a per-call `timeout`** the same way they
  over-trigger any argument — so the per-call arg needs a description that
  says "only for commands you expect could hang," and it belongs on the
  `tests/bench/` schema-probe watch list before it ships as advertised.

## Acceptance criteria (when implemented)

- Default build behaviour byte-identical when `runTimeout` is unset
  (the `popen` path unchanged; a prefix-stability-style assertion on the
  run path is overkill, but a unit test that `timeout=0` selects `popen`
  and `timeout>0` selects the watched path is not).
- A unit test of the kill path (`test_run_command_timeout` in
  `tests/test_app.c`): `jc_app_run_command_ex` on a blocking `sleep 5`
  with a 1s timeout returns exit 124 with the marker in well under the
  full sleep; a fast command under the cap runs to 0 via the same path;
  `timeout=0` is unbounded — observed red first by disabling the deadline
  (3 failures). **A finite command killed sub-second, not an indefinite
  hang** — deliberately: a smoke driver whose job is to spawn an infinite
  hang cannot run in a guarded CI/sandbox that kills never-terminating
  process trees, which makes it a fragile build gate; the in-process unit
  test exercises the same `run_command_watched` deadline portably and
  fast. The pure resolver has its own precedence table in
  `tests/test_config.c`.
- `make ci` green including ASan/valgrind over the watched-fork path
  (kill/reap under a sanitizer is where fork-path bugs surface).

## Relationship to the skill

The `supervise-long-command` skill (`examples/skills/`, scaffolded into
the language packs) makes the model *choose* background supervision when a
command might hang — advisory, and only as reliable as the model's
judgement. This timeout is the *mechanism* that bounds the case the model
misjudges. Ship the skill first (no code, immediate); build this second,
opt-in, once the schema-probe cost of the per-call argument is measured.
