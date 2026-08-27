# The tool-definition half of assignment cost (M310)

*Design note written before implementation, per the M299 craft rule. What was to be
measured, what the measurement found, what was changed, and what was deliberately not
changed.*

---

## The question

M309 removed the project rules file from `attempt` and measured `00-hello` falling
from **~128k to ~32k tokens** and **6 to 3 model calls**. It also recorded the next
lever without pulling it:

> with the rules gone, tool definitions are ~50% of each call, so `toolProfile: core`
> (M74) is where the remaining cost lives.

So: **does `toolProfile: core` reduce the cost of a graded attempt, and does the
attempt still pass?** Both halves matter. A cheaper run that cannot solve the task is
not an improvement, it is a broken gate — the M308 lesson.

## What to measure, and in which order

Two measurements, cheap-and-deterministic first:

1. **Static.** The serialized tool array under `full` vs `core`, for the same config.
   No model, no network, reproducible by anyone: `jichi context --tool-profile …`.
2. **Live.** `jichi attempt` on curriculum tasks under both profiles against the real
   HRZ model, recording tokens, model calls, and PASS/FAIL.

Doing the static half first is not just thrift. It establishes the *upper bound* on
what the live half can possibly save, so a live result that beats the bound is a
measurement error rather than a discovery.

---

## What the static measurement found: the gauge was wrong

```
$ jichi context --tool-profile full
  tool definitions  ~2989  (16 tools)
$ jichi context --tool-profile core
  tool definitions  ~2989  (16 tools)
```

Byte-identical. `jc_context_report`'s `tool_tokens` called
`jc_tool_build_neutral` with **no allow-list**, while `jc_agent_run_turn` applies the
resolved core fence via `jc_tool_build_neutral_ex`. So the report sized a tool array
the model would never receive.

**This is worse than a wrong number.** `/context` is the surface a user consults to
find out where their window went. Under `core` it over-reported the single line the
user came to read — and `core` is not an exotic setting: `toolProfile: auto` resolves
to it automatically under `--lite` or an effective context below
`JC_TOOL_PROFILE_AUTO_BELOW` (12000). **The gauge was wrong in exactly the
configuration a user adopts to fix the problem the gauge exists to diagnose.**

It also silently invalidated the M309 estimate that prompted this milestone. "Tool
definitions are ~50% of each call" came from the full array — true for the default
profile, and not the number a `core` user would have.

### Decision

`jc_context_report` sizes **what will actually be sent**: it resolves
`jc_config_tool_profile_core(&app->config, jc_compact_context_limit(app))` — the same
call, with the same limit, that the agent loop makes — and applies
`jc_tool_core_allow` through `jc_tool_build_neutral_ex`. When the fence is active the
line says so, because a count that dropped from 16 to 7 with no explanation is its own
puzzle:

```
  tool definitions  ~1180  (7 tools, core profile)
```

### Alternatives rejected

- **Report both numbers** ("16 tools, 7 advertised"). Rejected: the report answers
  *where is my window going*, and the window is spent on what is sent. The unsent
  definitions cost nothing, so giving them a number invites them to be budgeted for.
  `doctor` already names the profile and warns about MCP/user/LSP tools that `core`
  drops — that is the right place for "what you are not getting".
- **Fix only the TUI `/context`.** Rejected: they are one function on purpose, and the
  subcommand is what a scripted supervisor calls.
- **Leave it and document it.** Rejected on M309's own reasoning: a documented trap is
  still a trap.
- **Have `tool_tokens` take an explicit `allow` parameter** so callers choose.
  Rejected: two callers, one correct answer. A parameter would let the wrong one be
  chosen again.

### A second gauge gap, found and *not* fixed here

The `context` **subcommand** prints `rules ~0` always: `run_context` is dispatched
before `app.rules = jc_rules_load(&app)` in `main`. The documented example output in
[COMPACTION.md](../COMPACTION.md) shows `rules ~800`, and the prose explains that
history reads 0 and tools are the built-in set — it says nothing about rules.

So the subcommand under-reports the contributor M308 measured at **70% of every
call**. That is a bigger error than the one above, and it is *not* in this milestone's
scope: fixing it means moving asset loading ahead of subcommand dispatch, which
changes startup cost and ordering for every subcommand. Recorded here, and in the
ROADMAP, as the next honest slice rather than smuggled in. The TUI `/context` is
unaffected (rules are loaded before the REPL).

---

## What the live measurement found

Full numbers: [`analysis/2026-08-06-tool-profile-cost.md`](../analysis/2026-08-06-tool-profile-cost.md).

**~55% fewer tokens, two fewer model calls, and both tasks still pass** —
`00-hello` 66k → 29k (6 → 4 calls, three runs each), `06-make-the-test-pass`
91k → 46k (8 → 6). The per-call telemetry confirms M309's estimate exactly: tool
definitions were **53%** of the prefix under `full` and **20%** under `core`.

**The unpredicted result is the better one.** Three `core` runs produced identical input
totals — 29,004 tokens, to the token, same 4 calls — while `full` took 6, 7 and 6 calls.
On a small model the lean profile is *more repeatable*, which matters more for a graded
run than the token count: variance in the tool budget is what turns a pass into a FAIL
at a fixed `--budget-tokens`.

### The recommendation

`core` is **recommended for a learner on a modest budget** and **not** made the default
for `attempt`. See the capability cost below — M309's change was free, this one is not.

### The capability cost, stated up front

`core` is seven built-ins: `read_file`, `write_file`, `edit_file`, `apply_patch`,
`list_files`, `search_code`, `run_terminal_command`. An `attempt` under `core`
therefore loses the assignment tools **`hint` and `ask_for_help`** — the tiered-learner
machinery `attempt` exists to exercise, and the thing its own report counts
("hints used"). A cheaper attempt that cannot ask for a hint is not the same
experiment.

This is why `core` is measured and recommended-for-some-runs rather than made the
default for `attempt`. The rules-file change (M309) was free: nothing the exercise
measures depended on the host's contributor guide. This one is not free, and a change
that costs a capability must be the operator's choice, not a default someone discovers
by having a feature stop working.
