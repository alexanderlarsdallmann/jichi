# Long sessions, measured: the peak is transient, the floor is not

*2026-09-22. A measurement, not a change — nothing in `src/` or `tests/` moves on
this page. Run to test the endurance rung (S1) of
[`proposals/2026-09-sustained-task.md`](../proposals/2026-09-sustained-task.md)
against evidence before building it, the way
[the compaction measurement](2026-09-22-mid-turn-compaction-measured.md) tested S2
the same night.*

## First, how long is a long session?

This machine's telemetry holds **49,523 events across 241 sessions**, with
`turn_start` / `turn_end` per turn. Real usage is sharply **bimodal**:

| turns in a session | sessions |
|---|---:|
| **1** | **211** (89%) |
| 2–3 | 4 |
| 4–9 | 8 |
| 10–19 | 5 |
| **20+** | **8** (max **76**) |

So the endurance regime is real but rare: 13 sessions of 236 reach ten turns, and
the design's proposed "N of 12–20" sits inside the tail rather than beyond it.
Every session past 20 turns is one workload (`chrtext`).

*(Also visible and not pursued here: **12 turns started and never ended** across
the corpus — `turn_start` 664, `turn_end` 652.)*

## The result: RSS does not drift, it steps

`turn_end` records `rss_kb`. Across the **8 sessions with ≥20 top-level turns**:

| session | turns | min RSS | max RSS | shape |
|---|---:|---:|---:|---|
| 98c041b1 | 75 | 16 MB | 32 MB | flat |
| 40fed466 | 53 | 15 MB | 17 MB | flat |
| f7a21087 | 36 | 14 MB | 22 MB | flat |
| d4480d45 | 23 | 15 MB | 16 MB | flat |
| 050a43ba | 42 | 15 MB | **464 MB** | **step** |
| e7df3091 | 38 | 15 MB | **471 MB** | **step** |
| de0ba7a6 | 27 | 15 MB | **332 MB** | **step** |
| 0bf75213 | 21 | 16 MB | **608 MB** | **step** |

**Four of eight are flat** over 23–75 turns — the arena discipline holding
exactly as designed.

**Four of eight step**, and the per-turn series shows the shape is not a drift:

```
0bf75213  turn:RSS_MB
  1:17  2:17  3:17 ... 17:17  18:17  19:608  20:180  21:173
```

Flat for eighteen turns, one spike to 608 MB, then a **new plateau at ~175 MB** —
ten times the starting floor, held to the end of the session. `050a43ba` does it
twice (turn 16 → 339 MB, settling ~130; turn 20 → 464 MB, settling ~245).

**The peak is transient. The floor moves and stays moved.**

## What it is not

**Not tool-output volume.** Corpus-wide the largest single tool output is
**184,900 bytes**; median 300. Summing *every* tool output inside the stepping
windows gives **1.3 MB** (050a43ba) and **0.5 MB** (0bf75213) — against RSS jumps
of 322 MB and 591 MB. Three orders of magnitude apart.

**Not one tool.** `codebase_search` appears only in stepping sessions (2 of the 4)
and in none of the flat ones, which is suggestive — but `de0ba7a6` steps with
neither `codebase_search` nor `spawn_subagent`, and `98c041b1` runs 75 turns flat
*with* a `spawn_subagent`. No single tool separates the two groups.

**Not a slow leak.** In every stepping session RSS falls back part-way after the
spike, and in `98c041b1` a 32 MB bump at turn 7 returns fully to 16 MB by turn 10.
Memory is being returned — just not to the original floor, and not after the
large events.

## Why this is worth a page

`LOW_MEMORY.md` documents the mechanism that should prevent exactly this: jichi
pins `M_MMAP_THRESHOLD` at 128 KB at startup so large transient bodies always
mmap/munmap, and *"sweeps free heap back with `malloc_trim` at top-level turn
boundaries and after a between-turn compaction (`src/util/jc_memtrim.c`)"*.

Its validating measurement is stated just as plainly:

> *"Measured on the retry soak profile (25 turns, 2 injected failures/call,
> **~0.4 MB history**): last-RSS 13,224 → 12,596 KB, per-turn slope 28.3 → 9.7 KB,
> and the in-run curve now decreases after compaction instead of staircasing."*

That profile is 25 turns with **0.4 MB of history**. The stepping sessions here
are 21–42 turns of real work — `codebase_search`, `spawn_subagent`, 143
`run_tests` — and they reach a regime the soak never enters. **The fix is not
shown to be wrong; its evidence is shown not to cover the case where the floor
moves.** A synthetic that never allocates hundreds of megabytes cannot observe
hundreds of megabytes failing to come back.

## What this does to S1

The design's endurance rung asked *"does the loop survive N turns?"* The corpus
answers that for free: **yes** — eight sessions to 75 turns, no failures
attributable to length.

So S1's question is the wrong one, and the corpus supplies the right one:

- **Measure the FLOOR, not the peak.** Peak RSS is dominated by legitimate
  transients; the number that matters is where RSS sits at turn *N* versus turn 1.
- **N must reach the regime.** The steps here land at turns **16, 19, 20** — a
  12-turn rung would have missed every one of them. **N ≥ 20**, and with real tool
  use, not an idle loop.
- **The assertion is per-session, not per-platform.** Half the long sessions are
  flat. A rung that fails a platform for a step would fail it on the workload, not
  the platform.

That is a smaller and more useful rung than the one proposed, and — as with S2 —
the correction came from history rather than from a rig that did not exist yet.

## What is NOT established here

- ~~**The cause.** Telemetry names no allocation site.~~ **Followed up the same
  night with massif:
  [`2026-09-22-index-embedding-allocation-massif.md`](2026-09-22-index-embedding-allocation-massif.md).**
  92% of peak heap during an index build is `cJSON_Parse` of the embeddings
  response — one cJSON node per float, 1,509,888 of them, explaining the
  magnitude to within 1%. Transient per batch, not a leak. Whether it is *the*
  cause of these four steps is **not** established: the sessions are pruned, and
  one of them stepped without `codebase_search` at all.
- **Whether it is a defect at all.** A resident 175 MB on a workstation is
  unremarkable. On the 96 MB tier it would be fatal — and that tier has never run
  a 20-turn session, so nothing here says it breaks.
- **Generality.** One machine, one operator, and every 20+ turn session is the
  same project. The *shape* repeats across four independent sessions; the
  proportions are this corpus's.
- **The 12 unfinished turns** are noted, not investigated.
