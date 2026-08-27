# M93 — superseded-read elision in mid-turn compaction

**Status:** planned (committed before implementation, per the loop).
**Grounded in:** the telemetry-optimization pass over `zigodot-full.jsonl`
(2048 model calls, 112.3M input tokens, cacheless HRZ backend).

## Problem (the data)

On a cacheless backend the entire cost is re-reading context to emit almost
nothing: the input:output ratio is **228:1** (p50 output = 80 tokens, p50 input =
56k, peak 150k), and context ramps **~13×** within a session. The dominant
*fixable* contributor is redundant re-reads:

- `read_file` emits **15.2 MB** of tool output, and **84% of its calls are repeat
  reads** of a file already read (646 of 772) — `codegen.zig` **93×**, `vm.zig`
  **72×**, `parser.zig` **71×**, each re-injecting the whole file (~20k tokens).
- Every re-read both re-injects the full file *and* leaves the prior copies in
  history until compaction — the mechanism behind the 13× ramp and the 51
  (all `midturn`) compactions.

M76 mid-turn compaction elides the **oldest** large tool output first, blind to
redundancy. So it may evict useful unique output while duplicate reads of the same
file remain.

## Design

Add a **superseded-read elision pass** that runs **before** the age-based pass in
`jc_compact_midturn`. A `read_file` result for path *P* is *superseded* iff a
**later `read_file` result for the same path** exists in history — the later read
carries the current full content, so the earlier copy is a pure duplicate. Elide
all but the latest (reusing M76's head+tail+marker format). **Zero information
loss** — the newest read of every file is retained.

**Scope:** read-after-read supersession only. Edit/write-as-supersession is a
non-goal (an `edit_file` result is a diff, not the full post-edit file, so
eliding the pre-edit read there *would* lose content — nuanced, deferred).

### Implementation (`src/chat/jc_compact.c`)

1. **Factor the elide-one-message step** out of `jc_compact_trim_tool_output`
   into a static helper `elide_tool_msg(m)` (head+tail+marker via the existing
   `ELIDE_HEAD`/`ELIDE_TAIL`; returns 1 on a real shrink) so both passes share
   identical formatting + the idempotence guard (already-elided < `ELIDE_MIN_BYTES`
   is skipped).

2. **Resolve a tool-result's origin.** A static helper
   `read_path_of(hist, msg_index)` → the path string (arena/borrowed) when message
   *i* is a `JC_ROLE_TOOL` result whose originating tool_call (matched by
   `tool_call_id` across the assistant messages) has `name == "read_file"`; else
   NULL. Path parsed from `arguments_json` via `jc_json` (`"path"`).

3. **New pass `jc_compact_trim_superseded_reads(hist, budget_tokens, keep_recent)`**
   (mirrors `jc_compact_trim_tool_output`'s signature/contract):
   - For each eligible message *i* (`i + keep_recent < n`, role TOOL, len >
     `ELIDE_MIN_BYTES`) that is a `read_file` result for path *P*, elide it iff
     there is a **later** `read_file` result for the same *P* (scan `j > i`).
   - Stop early once the calibrated estimate is under `budget_tokens` (same as the
     age-based pass), so it never over-trims.
   - Returns the count elided.

4. **Wire into `jc_compact_midturn`:** run the superseded pass first, then the
   existing age-based `jc_compact_trim_tool_output` for any remainder; sum the
   counts for the log/`on_status` line. Duplicates go before useful unique output.

### Header (`include/jc_compact.h`)

Declare `jc_compact_trim_superseded_reads`. `jc_compact_trim_tool_output` stays.

### Tests (`tests/test_compact.c`)

Build a synthetic history via `jc_msg_add_tool_call(a, id, "read_file", args)` +
`jc_history_add_tool_result`:
- Same path read 3× (large bodies) → the two earlier reads elided (head+tail
  marker present), the **latest kept** verbatim.
- A single read of a path → untouched.
- Two different paths each read once → both untouched.
- `keep_recent` protects the tail; an already-elided body isn't re-elided.
- A non-`read_file` large tool result is ignored by this pass (age-based handles
  it).

### Docs

ROADMAP.md M93 done entry + closing summary; a note in docs/COMPACTION.md
(mid-turn section); the 84%-redundant-read finding recorded in docs/ANECDOTES.md.

## Non-goals

- Edit/write-as-supersession (loses pre-edit full content).
- A read-time cache / "unchanged since your read above" short-circuit
  (prevention rather than cleanup — a separate, larger candidate).
- Changing the M76 thresholds (`MIDTURN_HIGH_PCT`/`TARGET_PCT`/`KEEP_RECENT`).

## Verification

- Unit tests (above) for the supersession + elision logic.
- `make WERROR=1` + `make test` + full `make ci` green.
- The change is pure history mutation with no model call, exercised entirely by
  the unit test (no live run needed).
