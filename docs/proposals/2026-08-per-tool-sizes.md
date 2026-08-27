# Per-tool definition sizes (M313)

*Design note written before implementation, per the M299 craft rule. The measurement
that shaped it was taken first, and it changed the design.*

---

## The question

M310 established that tool definitions are 20–53% of a call's prefix and that
`--tool-profile core` roughly halves an attempt. M312 broke down the system prompt and
explicitly deferred this: *"it does not measure the tool definitions per tool. That is a
different report."* This is that report.

## The measurement, before the design

Taken from a captured request body (18 advertised tools, `full` profile, no MCP/user
tools), which needed no new code:

```
n=18  sum=10885 bytes (~2721 tok)  array framing overhead: 19 bytes
  spawn_subagent   1305  12.0%      read_file          551   5.1%
  spawn_parallel   1184  10.9%      run_tests          495   4.5%
  apply_patch      1176  10.8%      search_code        443   4.1%
  todowrite         907   8.3%      read_background…   386   3.5%
  run_terminal…     710   6.5%      write_file         372   3.4%
  ask_user          626   5.8%      kill_background    298   2.7%
  edit_file         589   5.4%      fetch_url          270   2.5%
  remember          586   5.4%      list_files         248   2.3%
  codebase_search   584   5.4%      todoread           155   1.4%
```

**Top 5 = 49%. Largest single tool = 12%.** This is death by a thousand cuts, not one fat
schema. Array framing is 19 bytes, i.e. nothing.

*Why the shipped view shows 16 and this capture 18:* the capture came from a real turn,
which also registers `read_background_output` and `kill_background` (they need the
background-command manager). The `context` subcommand builds the **built-in** registry
only, as COMPACTION.md already says of the tools figure. The shape is the same either way.

## What the measurement changed

My first instinct was the M312 treatment: add the breakdown to `/context`, non-zero only,
largest first. **The numbers say don't.** In M312 six lines explained a 15,196-token block
whose top item was 10,441 tokens — the lines *were* the answer. Here eighteen lines would
explain a 3,095-token block whose top item is ~370 tokens. That is eighteen lines of
report to move a 12% share of a 17% component, printed every time anyone runs `/context`
— and it would push the system-prompt lines, which are the answer, off a terminal screen.

**Decision: `/context` keeps its single tools line. The per-tool breakdown is a separate
view, `context tools`.**

This is deliberately the opposite of the M312 decision, and the reason is the shape of the
data rather than a change of taste. Worth stating because a reader who sees M312 expand
one line and M313 refuse to will otherwise assume one of us was inconsistent.

## `context tools`

A sub-verb, matching `learn analyze` / `mcp prompts` / `board list`. Shows, largest first:

```
Tool definitions: 18 advertised, ~2721 tokens (full profile)

  tokens   share   cum.   core  tool
     326   12.0%  12.0%          spawn_subagent
     296   10.9%  22.9%          spawn_parallel
     294   10.8%  33.7%    *     apply_patch
     ...
  core profile would advertise 7 of these (~970 tokens, 36%)
```

Four deliberate choices:

1. **A cumulative column.** The flatness *is* the finding, and a cumulative percentage is
   how a reader sees it in one glance. Without it, someone reads "12%" at the top and
   concludes there is a fat tool to remove.
2. **A `core` marker plus the footer.** This is the actionable part, and it makes the
   report answer the question a cost-conscious user actually has — *what would
   `--tool-profile core` save me* — instead of leaving them to cross-reference M310.
3. **It respects the resolved fence.** Under `core` it lists the seven that are sent, and
   says so. Sizing tools the model never receives is the M310 defect, and it would be
   absurd to reintroduce it in the report about tool sizes.
4. **Tokens, not bytes**, through the same `jc_compact_estimate_bytes_cal` M312 added, so
   this report cannot disagree with the one line in `/context`.

### Alternatives rejected

- **Put it in `/context` behind a verbosity flag.** Rejected: a flag on a diagnostic
  report is a second thing to discover, and `/context` is consulted from the TUI where
  flags are awkward. A named view is findable in `--help`.
- **A `--tools` global flag.** Rejected: sub-verbs are the house pattern for a
  subcommand's second view, and a global flag that only affects one subcommand is a
  worse contract.
- **Show only the top N.** Rejected: the whole point of a dedicated view is that it can
  afford to be complete. The truncation problem is what pushed it out of `/context`.
- **A TUI `/context tools`.** Deferred, not rejected: the TUI's `/context` takes no
  arguments today, and the value is highest for someone tuning a config from a shell.
  Recorded rather than half-built.

## What it will not do

- **No advice about individual tools.** The report says `spawn_subagent` is 326 tokens; it
  does not suggest disabling it. Advice belongs in `doctor`, which already names the
  resolved profile and warns about tools `core` drops.
- **No per-tool history of use.** "You are paying 326 tokens for a tool you called zero
  times" is a genuinely better report — and it needs telemetry joined to the registry,
  which is the `telemetry` summarizer's territory, not this one's. Recorded as the next
  slice if this one proves useful.
- **No MCP/user-tool special-casing.** They appear like any other advertised tool, which
  is the point: a bloated MCP schema should show up here, and this is the first surface
  where it could.
