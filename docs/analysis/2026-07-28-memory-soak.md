# Long-run memory: the soak, the root causes, and the 12 GB question

**Date:** 2026-07-28 · **Milestone:** M180 · **Host:** Linux x86-64, glibc,
246 GB RAM · **Method:** `tests/measure/soak.py` (N-turn single-process ACP
soak against a mock SSE model; RSS sampled from `/proc/<pid>/status` per
turn, cross-checked against the new `turn_end` telemetry `rss_kb` field),
plus valgrind massif and an ASan suite run.

## The question

On another system, an **older jlu_continue** grew from a few MB to ~100 MB
and, over long runs, toward **12 GB**. No telemetry from that machine
survives — which is itself the first finding: nothing in the tool recorded
memory, so the report could not be diagnosed after the fact. M180 fixes the
observability gap (RSS in telemetry, heartbeat, and `/context`) and hunts
the growth in the current tree.

## Measurements (200-turn soaks, mock model, this host)

| Workload | Binary | RSS slope | Total growth |
|---|---|---|---|
| read profile (read_file each turn, chat) | pre-fix | 3.6 KB/turn | +1.6 MB |
| read profile | post-fix | 3.5 KB/turn | +1.6 MB |
| **write profile** (write_file, 2 KB args, checkpoint/turn) | pre-fix | **12.6 KB/turn** | **+3.4 MB** |
| **write profile** | post-fix | **10.2 KB/turn** | **+2.9 MB** |

massif (20 write turns): **live heap peaks at ~0.7 MB** — snapshots
oscillate with the per-turn session save and return to ~0.5 MB. There is
**no unbounded live-heap leak in the current tree.**

Reading the table honestly:

- The read-profile fixes moved nothing *in that workload* — its tool args
  are ~25 bytes and it never takes checkpoints, so the fixed sites barely
  fire. That is why the write profile exists.
- The write-profile delta (−2.4 KB/turn, −19 %) is the four arena-lifetime
  fixes doing exactly what the arithmetic predicts (≈2 KB args copy +
  checkpoint strings per turn).
- The remaining ~10 KB/turn is **by design and bounded**: history grows
  until compaction (deliberately disabled in the soak via a 400 k
  `contextLength`); the per-turn session save builds and frees a
  full-history string whose transient spike glibc partially retains
  (allocator retention raises RSS without live-heap growth — massif shows
  the live heap flat while RSS creeps). With a realistic `contextLimit`,
  compaction caps history and the curve plateaus.

## Root causes found and fixed (M180)

All four were session-arena allocations that lived until process exit
(arenas free only wholesale — an allocation's lifetime must match its
arena, and these didn't):

1. **Three copies per tool call** (`name`/`args`/`id`, jc_agent.c) — the
   hottest site: `args` can be kilobytes, marathon runs make thousands of
   calls. → per-turn scratch arena.
2. **The snapshot store** strdup'd SHA+label per checkpoint into an
   uncapped vec. → fixed-size fields in the element + rotation at
   `JC_SNAPSHOT_MAX_LIST` (every consumer copies immediately; verified).
3. **Constraint-scan temporaries** allocated per AUTO turn even when
   nothing was adopted. → scratch (adopted constraints still get their own
   session-arena copies).
4. **Headless prompt expansion** (command/`@`-refs/auto-context) used the
   session arena where the TUI used scratch. → scratch (history strdups
   the prompt before the turn-start reset — the TUI's existing contract).

Verified bounded (documented as such, not fixed because not broken): the
bg-process registry (≤ ~2 MB), MCP connections (startup-only), eventlog RAM
(per-event tree freed after write — but note: **no on-disk rotation**; a
months-long telemetry file grows on disk), session-save spike (transient,
∝ history), libcurl (handle per request, cleaned each time — no reuse
either, a *performance* note for the stress-testing work, not a leak).

## So what was the 12 GB?

The honest answer: **not reproducible from the current tree, and the
original data is gone.** The strongest historical candidate is the bug
fixed at M140: the repo-map scan read **every candidate file's full text
onto the session arena** and kept it for process life — on a large
workspace that is gigabytes, and it compounds with the pre-M140 compaction
intermediates that also landed on the session arena. An older build running
long sessions on a big repository fits the reported curve (fast ramp to
hundreds of MB, unbounded growth with activity). The M180 sites above were
the remaining slow leaks of the same class (KB-per-action, not MB), now
closed.

What we can say with measurement behind it: the current build's live heap
is sub-MB over a soak, RSS slope is explained by bounded mechanisms, and —
the real deliverable — **any future report arrives with a memory curve
attached**, because every `turn_end` telemetry event and every `--heartbeat`
now carries `rss_kb`, and `/context` shows the process figure live.

## Reproducing

```sh
make
JC_SOAK_BIN=$PWD/jichi python3 tests/measure/soak.py --turns 200 --profile write --csv soak.csv
valgrind --tool=massif ./jichi ... # or wrap the binary and hand it to the soak
```

See docs/LOW_MEMORY.md §Long-run measurement for the operator-facing
procedure.
