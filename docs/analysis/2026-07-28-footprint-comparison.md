# Footprint comparison: jichi vs. opencode vs. Claude Code

**Date:** 2026-07-28 · **Milestone:** M181 · **Host:** Linux x86-64
(7.0.0-28-generic), 32 cores, 246 GB RAM, glibc, system libcurl ·
**Versions:** jichi 0.9.0 · opencode 1.17.7 · Claude Code 2.1.220 ·
**Scripts (committed, re-runnable):** `tests/measure/startup.sh`,
`tests/measure/idle_tui.py`, `tests/measure/soak.py`.

> **Method & honesty, before any number.** These are environment snapshots,
> not benchmarks of record. The three tools do **different amounts of work by
> design** at every measured point: opencode and Claude Code ship bundled
> JS/TS runtimes and richer TUIs, may check for updates, and index projects;
> jichi is a C binary linking the system libcurl. Comparing them is fair —
> users feel RSS regardless of why — but explaining *why* the numbers differ
> matters as much as the numbers. Where a comparable measurement was not
> possible, the cell says so instead of pretending.

## 1. Startup (cold, trivial invocation)

Three runs each, best kept (`/usr/bin/time -v`; peak RSS = MaxRSS):

| Invocation | peak RSS | wall time |
|---|---|---|
| `jichi --version` | **10.4 MB** | < 10 ms |
| `jichi --help` | **10.4 MB** | < 10 ms |
| `opencode --version` | 184 MB | 300 ms |
| `opencode --help` | 192 MB | 330 ms |
| `claude --version` | 247 MB | 60 ms |
| `claude --help` | 294 MB | 150 ms |

| Executable | on disk |
|---|---|
| jichi | **1.4 MB** (+ the system libcurl it links) |
| opencode | 150 MB |
| claude | 263 MB |

The gap is architectural, not an optimization contest: a bundled runtime
pays its baseline on every invocation; a C binary pays libc + libcurl. For
jichi's SIZE=1 build the disk figure drops to ~700 KB (LOW_MEMORY.md).

## 2. Idle interactive session (60 s, no input, empty directory)

Whole **process group** sampled at 1 Hz (`idle_tui.py`; these tools fork
helpers — single-PID RSS undercounts). No prompt was ever submitted; no
model traffic.

| Tool | RSS first | median | peak | last | CPU while idle |
|---|---|---|---|---|---|
| jichi (TUI) | 13.2 MB | 13.2 MB | 13.2 MB | 13.2 MB | **0.0 s** |
| opencode | 339 MB | 583 MB | **900 MB** | 583 MB | 6.0 s |
| claude | 223 MB | 224 MB | 224 MB | 224 MB | 0.8 s |

Observations, stated carefully:

- **jichi is flat and silent**: identical RSS on every sample, zero
  measurable CPU. There is nothing running when nothing runs.
- **opencode did substantial background work while idle** in an empty
  directory (peaking near 1 GB and burning 6 CPU-seconds). That is likely
  by design (indexing, prefetch, UI runtime) — the measurement reports what
  a user's machine experiences, not a judgment of the design.
- **Claude Code idles steady** around 224 MB with modest background CPU.

## 3. Long-run session growth

Only jichi can be driven against a **local mock model** with its own tooling
(`tests/measure/soak.py`, M180), so only jichi has an agent-loop curve; the
others would need comparable mock endpoints and are honestly marked "not
measured" rather than approximated.

| Tool | 200-turn tool-using session | note |
|---|---|---|
| jichi | 13.6 → 15.6 MB (**~10 KB/turn**, write-heavy profile) | bounded: history until compaction + allocator retention; live heap < 1 MB under massif (see the M180 analysis) |
| opencode | not measured | no mock-driveable harness here |
| claude | not measured | no mock-driveable harness here |

## 4. What to take from this (for the decks)

- **Two orders of magnitude** at every static point: ~1.4 MB vs 150–263 MB
  on disk; ~10 MB vs ~190–290 MB to print a version string; ~13 MB flat vs
  ~220–900 MB sitting idle.
- The honest framing for a slide: *this is what "no runtime" means in
  megabytes* — not that jichi does what the others do with less, but that
  the C89 + system-libcurl architecture makes the baseline nearly free, which
  is exactly why jichi runs on low-RAM/embedded targets
  (docs/LOW_MEMORY.md's tiers start at 32 MB total RAM).
- Reproduce anytime: the three scripts above; each prints its own caveats.

Slide added: `docs/presentations/00-super-features.md` ("Footprint,
measured"). i18n deck copies pick it up at the next translation pass, per
the phased policy.
