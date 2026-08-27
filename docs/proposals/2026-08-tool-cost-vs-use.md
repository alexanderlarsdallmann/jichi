# Joining telemetry to the registry: paid-for vs used (M314)

*Design note written before implementation, per the M299 craft rule. This one is mostly
about the ways it could mislead, because that is where the difficulty is.*

---

## The claim to make possible

M313 ends by naming the better report it is not:

> "You are paying 381 tokens for a tool you called zero times" needs telemetry joined to
> the registry.

`context tools` knows what each definition **costs**. The telemetry log knows what was
**called**. Neither knows the other. Joining them turns a size listing into a question
worth acting on.

The cost model matters for the phrasing. A tool definition is not billed per use — it sits
in the request prefix and is paid on **every model call**, used or not. So the honest
framing is *"you pay this on every call; here is how often it earned it"*, and the
actionable finding is a tool with a non-trivial size and zero calls.

## Layering

`jc_telemetry` is pure and unit-tested; it must not learn about the tool registry.
`struct jc_telem_tool` already carries `calls`, `ok` and `last_ts`. So:

- `jc_telemetry_summarize` unchanged.
- `jc_context_tools_report(app, telem, label, out)` takes an **optional** summary and adds
  columns when it is present.
- `main.c` does the file I/O, reusing `newest_jsonl` and the `ws_filter` canonicalisation
  that `run_telemetry` already has.

No new module, no new dependency direction, and the pure summarizer stays testable
offline.

## The five ways this report could lie

This is the design, more than the columns are.

1. **Telemetry is off by default.** Most users have no log at all. A report that prints
   `0 calls` for every tool because there is no data would be actively harmful — it reads
   as "you use none of these". So: **no data is a stated absence, never a column of
   zeroes.** If no log is found, the join is skipped and the report says why, with the
   command that would start collecting.

2. **A short log proves nothing.** One session of one turn calling `read_file` does not
   convict the other fifteen tools. So the report **states its evidence base** — which
   log, how many turns and tool calls it covers — on the same screen as the conclusion.
   A reader who sees "1 turn" will discount it correctly; a reader shown only a verdict
   cannot.

3. **A different project's log is not this project's evidence.** Events are stamped with
   the workspace root (M56), so the join **filters to the current workspace** by default.
   "I never call `search_docs` here" is a per-project claim.

4. **Rare is not useless.** `kill_background`, `ask_user`, `apply_patch` on a project that
   rarely does multi-file edits — each may be called once a month and be exactly right
   when it is. So the report **describes and never advises**: it marks unused tools and
   totals their cost. It does not suggest a config change. (Contrast `doctor`, whose job
   *is* advice, and which already warns about tools `core` drops.)

5. **A never-called tool may be one the model cannot call.** M285 found dead fence
   entries; a tool advertised but denied by permissions, or unusable because its schema
   confuses the model, shows up here as "unused" with a completely different cause. The
   report cannot distinguish those, so it must not imply it can. Naming this in the footer
   is the honest minimum.

## Shape

```
Tool definitions: 16 advertised, ~2915 tokens (full profile)
  (~5 of that is JSON array framing, charged to no tool)

  tokens   share   cum.   core  calls  tool
     381     13%    13%             0  spawn_subagent
     343     11%    24%    *       12  apply_patch
     ...
Use: telemetry-2026-08-06.jsonl, this workspace, 34 turns, 210 tool calls.
9 advertised tools were never called in it, costing ~1,240 tokens on every call.
A tool can be rare and still right, and this cannot see a tool the model was
never able to call -- treat it as a question, not a verdict.
```

`calls` is a column, not a separate section, so cost and use are read on one line.

## Trigger: automatic, with the absence stated

**Decision: the join happens automatically when a log for this workspace exists**, rather
than behind a flag.

Rejected: a `--telemetry` flag on the sub-verb. The point of the report is that the
information reaches the person looking at costs *without* their knowing a second incantation
existed. A flag would be discovered by the people who already suspected the answer.

The cost of automatic behaviour is that the report's shape now depends on `~/.jichi.d`
state. That is acceptable because the absent case is explicit — and it is exactly why
risk 1 above is handled by a sentence rather than by zeroes.

Also rejected: putting this in `telemetry` instead. `telemetry` is the run-review surface
and already prints per-tool ok-rates; this is a *cost* finding, and the person with the
cost question is running `context tools`. (`telemetry` could gain it later; nothing here
prevents that.)

## What it will not do

- **No `--since` window.** `telemetry --since` exists for that; adding a second windowing
  path here would be a second definition of "recent". If the log is stale, the turn count
  in the footer is the signal.
- **No per-tool token attribution to calls.** Dividing definition cost by call count
  invents a per-use price for something billed per request. It would look precise and mean
  nothing.
- **No behaviour change.** Read-only report; no config edited, nothing disabled.
