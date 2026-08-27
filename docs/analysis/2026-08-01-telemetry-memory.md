# Telemetry-driven memory hardening: what a marathon workload still grew (M218–M220)

**Date:** 2026-08-01. **Data:** two full-tier telemetry snapshots (~40k
events, Jul 2 – Aug 1) plus run journals and a session store, copied from a
dev machine running long unattended `--auto` loops over a private downstream
workspace. **Symptom on that machine (pre-M197 install):** RSS grew from a
few MB to ~1 GB over a day of runs. **Question:** with M197–M202 landed, what
does the *current* build still not bound under that exact workload?

## The workload, measured

The telemetry describes a shape none of the existing soak profiles modeled:

| signal | value |
| --- | --- |
| tool calls in ONE top-level turn | up to **340** (275 model calls) |
| cumulative input tokens per turn | 24–63 M |
| per-call input | routinely **80–130k tokens** (~0.5 MB request text) |
| transient retries (status 0, backoff 500 ms→) | **~2,400** — each a full request rebuild (M20e) |
| mid-turn compaction elisions | **572** (vs 12 between-turn) |
| history share of every request | **83%** |
| model calls in one session | up to **2,149** |
| hallucinated tool names | `glob` ×39, `todoedit` ×14, `run_shell_command` ×6, `create_file`, … |
| repeat reads | the same ~80–93 KB file re-read dozens of times |

Two structural facts follow. First, **between-turn compaction never runs**
in this workload — a "turn" is hours long, so anything only reclaimed at a
turn boundary is effectively never reclaimed. Second, the per-call input
kept ramping to ~131k tokens *despite* 572 mid-turn elisions: the
result-side trims were working, and the growth was living somewhere they
never touch.

## What the code still didn't bound (each verified by reading, then gated)

1. **Assistant `tool_calls[].arguments_json`** — the other half of history.
   `write_file`/`apply_patch` arguments carry full file bodies, and no trim
   pass touched them. In a marathon single turn this grows monotonically.
   *Fix:* mid-turn argument elision with a valid-JSON marker keeping the
   path (the Anthropic serializer re-parses arguments; a non-object degrades
   to `_unparsed_arguments`). Also the biggest token/cost lever, given the
   83% history share.
2. **glibc heap high-water.** The request path is balanced malloc/free
   (~3× request text live per attempt), but ~2,675 attempts × ~1.6 MB of
   transient churn is exactly the pattern that ratchets glibc's *dynamic*
   mmap threshold: each freed mmap'd body teaches malloc to serve the next
   from brk, whose high-water never returns to the OS. No leak checker can
   see it; `/context` shows the shape (big "Process resident", near-empty
   arenas). *Fix:* pin `M_MMAP_THRESHOLD` (128 KB) at startup +
   `malloc_trim` at turn boundaries (`jc_memtrim`, probed as
   `JC_HAVE_MALLOC_TRIM`). Retry-soak A/B: slope 28.3 → 9.7 KB/turn,
   last-RSS 13,224 → 12,596 KB, and the curve now *decreases* in-run.
3. **Per-tool-call copies on the per-turn scratch.** The loop's
   name/args/id copies (M180) accumulate full args × hundreds of calls for
   the whole turn — and the per-call arena is off-limits (nested `spawn_*`
   runs reset it while the copies are live). *Fix:* malloc-owned per
   iteration, single `next_call` exit. Gate: `tests/smoke/turn_scratch.sh`,
   observed **189 KB → 1 KB** on the /context gauge.
4. **Provider stream buffers** kept the largest-ever message's capacity ×2
   until provider destroy (`jc_sb_clear` keeps cap). *Fix:*
   `jc_sb_clear_shrink` with 64 KB/8 KB bounds.
5. **Session save** re-serialized the whole history (2–3× peak) on every
   call, up to six TUI sites per boundary. *Fix:* a history `gen` counter +
   dirty-skip; the sentinel-survives test was red before the skip.
6. **Mid-turn dedup CPU**: ~165k cJSON parses per marathon turn from the
   every-round eager pass and a forward-from-zero originating-call scan.
   *Fix:* gate on a round-appended-a-read hint (bit-identical outcome) +
   backward adjacency scan. Pure churn, but it fed vector 2.

Residuals in the same commit series: envelope commit SHAs inline
(`char[48]`), `/map` render malloc-owned, `summarize_chunked` intermediates
on a build-local arena. Enforcement: the arena lint now scans
chat/provider/net/session/index (teeth demonstrated against the pre-fix
tree); `soak.py` gained `retry`/`save` profiles and a VmHWM report so this
workload's shape stays measurable.

## The error ledger (M219)

- 25% of model calls failed transport-level and were retried; the ladder had
  never been exercised deterministically. `JC_FAULT_NET` (FAULT=1) now fails
  `jc_http_stream` before any bytes move; `faults_net.sh` asserts the exact
  sequence (500 ms, 1,000 ms, exactly `maxRetries`, final diagnostic).
- Unknown-tool guesses cost a failed round-trip at ~100k input tokens each.
  Schema-compatible ones (`create_file`, `new_file`, `run_shell_command`,
  `shell_command`) are now transparently aliased; schema-incompatible ones
  deliberately stay hints (ANECDOTES #15: only resolution moves the
  ok-rate).
- The session store (243 files / 17 MB here; 480 MB measured elsewhere at
  M197) now has `prune` + a `doctor` size warning — the rotation half
  deferred at M197.

## Post-wave A/B (reference box, 4.8 GB / 3 cores)

| profile | before (HEAD `4da2eda`) | after M218 |
| --- | --- | --- |
| retry (25 turns, 2 fails/call, 0.4 MB history) | last 13,224 KB · slope 28.3 | last 12,596 KB · slope 9.7, curve decreases in-run |
| write (50 turns, 8 KB args) | last 13,248 KB · tail slope 27.8 | last 12,600 KB · tail slope 17.0 |
| save (100 turns, +8 KB history/turn) | last 14,344 KB · tail slope 24.7 | last 13,304 KB · tail slope 17.8 |

The residual write/save slope is *legitimate* history growth (the payload
retained in an uncompacted history plus the session file) — at marathon
scale that is exactly what the argument-side elision bounds once compaction
pressure exists (these short profiles never cross the 80% trigger).

## Lessons

- **A workload profile beats a hypothesis.** Every vector above was ranked
  by what the telemetry said this workload actually does — the top two fixes
  (arguments elision, malloc tunables) were not on any prior list.
- **"Turn-scoped" is unbounded when a turn is unbounded.** M199's insight
  (per-turn reset can't bound a 200-call turn) applied to two more sites
  that M199 itself couldn't move; the third arena is not always the answer —
  plain ownership sometimes is.
- **The allocator is part of the program.** Balanced malloc/free with a
  hostile size pattern still grows RSS forever on glibc defaults. The fix is
  two lines of `mallopt` — once you know to look below your own code.
- **Slope lies; keep the peak.** Reaffirmed: the soak report now prints
  VmHWM next to the slope for the same reason the tool-arena analysis
  recorded ("slope alone would have reported 'no problem'").

## See also

- `docs/analysis/2026-07-29-tool-arena.md` — the M197–M199 predecessor.
- `docs/COMPACTION.md` (argument-side elision), `docs/LOW_MEMORY.md`
  (malloc tunables, new soak profiles), `docs/ROADMAP.md` M218–M220.
- `docs/proposals/2026-08-curl-handle-reuse.md` — the deliberately deferred
  follow-up this data also motivates.
