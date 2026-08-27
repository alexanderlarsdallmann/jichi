# The session arena: reproducing the 12.5 GB growth (M197)

*2026-07-29. Companion to `2026-07-28-memory-soak.md` (M180), which asked this
question and could not answer it.*

> **Status: reproduced, then fixed.** The measurements below are the *pre-fix*
> tree — they are what the harness reports when the bug is present, and are kept
> because they are the regression baseline. See **[The fix](#the-fix)** at the
> end for the after numbers: every session-command variant is now flat at
> **0.0 KB/call**, a single `/sessions` over a 480 MB store costs **0 KB**
> instead of 480 MB, and the per-tool-call retention no longer scales with file
> size at all.

## The question

On another system, jichi's RSS grew from ~1.5 MB to as much as **12.5 GB** while
using the TUI `/sessions` command and restoring sessions with `/resume`.

M180 hunted this exact report and concluded *"not reproducible from the current
tree, and the original data is gone."* That conclusion was wrong, for two
specific and instructive reasons:

1. **The M180 soak drives ACP, never the TUI.** No session-command call site was
   ever exercised.
2. **The soak's read fixture is 13 bytes** (`note.txt`, `"soak fixture\n"`).
   The bug it was looking for scales with file size, so at 13 bytes it is
   invisible by construction.

Both are now fixed, and the phenomenon reproduces on demand.

## Answer up front

Two independent instances of one bug: **read a file's full text onto the
session arena and keep it for the life of the process.** Both are the surviving
twin of the M140 repo-map bug.

`app->arena` is created once (`src/main.c:8425`) and freed only at exit.
`jc_arena_reset` has three call sites — `src/main.c:8003`, `:8014` (a local
`reqarena`), and `src/chat/jc_agent.c:2014` (`app->scratch`) — **never
`app->arena`**.

| | Mechanism A | Mechanism B |
|---|---|---|
| Site | `jc_session_list` (`src/session/jc_session.c:355`) | `read_file`/`edit_file`/… (`src/tools/jc_tool_read.c:79`) |
| Fires | per `/sessions`, per `/resume`, **per Tab keypress** | **per tool call** |
| Retains | every session file's full text | the whole file, regardless of `readMaxBytes` |
| Measured | **17.5 MB per `/sessions`** on a 250-file/17.9 MB store | **218 KB/turn** reading one 200 KB file |

This is **not a leak**. It is a lifetime bug, and that distinction is why it
survived M180's sanitizer sweep — see *The CI blind spot* below.

## Mechanism A: measurements

Harness: `tests/measure/mkstore.py` (synthesizes a store of exact size) +
`tests/measure/session_scan.py` (PTY-drives the real TUI in an isolated `HOME`,
reads the binary's own `/context` gauges plus process-group RSS from `/proc`).

Control first, per the pre-registered falsification rule — *if `/help` grows,
the hypothesis is wrong*:

| variant | slope | session arena after 20 | group RSS |
|---|---|---|---|
| `idle` (M181 baseline) | — | — | **11,636 KB flat**, 0.0 s CPU |
| `/help` × 20 | **0.0 KB/call** | **0 KB** | **10,752 KB flat** |
| `/sessions` × 20 | 206.9 KB/call | 4,346 KB | 11,136 → 19,200 KB |

### The scan-count inventory (50 files / 200 KB store)

| variant | slope KB/call | ratio | predicted | Δ |
|---|---|---|---|---|
| `/sessions` | 206.9 | 1.00 | 212.1 | −2.4% |
| **Tab on `/resume `** | 206.9 | 1.00 | 212.1 | −2.5% |
| `/resume <full-id>` | 419.0 | **2.03** | 424.2 | −1.2% |
| `/resume <alias>` | 626.2 | **3.03** | 636.3 | −1.6% |

Every fit had **R² = 1.000000** and a delta ratio of exactly 1.0000 (last-quartile
mean ÷ first-quartile mean) — **pure linear retention, no superlinear term.**

The 1 : 1 : 2 : 3 ratio is direct evidence for the static call-site inventory,
including the **literal duplicate** `jc_session_resolve_alias` at
`src/tui/jc_tui.c:2674` and `:2679` — without it the alias case would be 2×.

### The cost is bytes, not file count

Size sweep, 50 files, `bytes/file` varied 512×:

| bytes/file | slope KB/call | predicted | Δ | reserved/used |
|---|---|---|---|---|
| 1 KB | 57.0 | 62.1 | −8.2% | 1.04× |
| 8 KB | 407.0 | 412.1 | −1.2% | **1.97×** |
| 64 KB | 3,207.0 | 3,212.1 | −0.2% | 1.12× |
| 512 KB | 25,607.0 | 25,612.1 | −0.0% | 1.02× |

Count sweep at a fixed 4 MB total, file count varied 16×:

| files | bytes/file | slope KB/call |
|---|---|---|
| 25 | 164 KB | 4,099.3 |
| 50 | 82 KB | 4,102.7 |
| 100 | 41 KB | 4,109.0 |
| 200 | 20 KB | 4,123.3 |
| 400 | 10 KB | 4,150.0 |

**1.2% variation across a 16× change in file count.** So the per-scan cost is
`S + ~138·F` bytes, dominated by `S` (total store bytes). The per-file term
measured ~138 B, slightly under the 248 B estimate — which is why the smallest
store shows the largest relative deviation (−8.2%) and the largest shows −0.0%.

The `reserved/used` column is a **second, independent fingerprint**: it peaks at
1.97× exactly at the 8192-byte block boundary and falls off on both sides. That
is the arena's oversize path (`src/util/jc_mem.c:77-90`): an oversized request
gets a dedicated exact-cap block which becomes head with `used == cap`,
permanently orphaning the previous head's free tail (there is no free-list
search). RSS tracks `reserved`, not `used`.

### The headline, and the single-keystroke worst case

Mirroring this machine's real store (243 files, 17 MB):

```
store 250 files / 17,920,000 bytes,  /sessions x 12
  call   used_kb   reserved_kb   rss_group_kb
     0    17,535        19,517         30,080
    12   227,956       253,637        258,452
slope 17,535.1 KB/call   R^2 = 1.000000   (predicted 17,560.5 -> -0.1%)
arena reserved accounts for 102.5% of delta RSS; scratch 0 -> 4 KB
```

**One `/sessions` keypress costs 17.5 MB of arena and ~19 MB of RSS,
permanently, on a store the tool built by itself.** Twelve of them take the
process from 30 MB to 258 MB.

There is **no aggregate cap** — only the per-file `JC_READ_FILE_MAX` (64 MiB,
`include/jc_platform.h:78`). A single scan is therefore bounded only by
`files × 64 MiB`:

| store | one `/sessions` | arena used | group RSS | wall |
|---|---|---|---|---|
| 8 × 60 MB = 480 MB | 1 scan | **491,521 KB** (predicted 491,521.9, **−0.0%**) | **502,252 KB** | 2.25 s |
| 1 × 63 MB (under cap) | 1 scan | 64,512 KB (−0.0%) | 75,328 KB | 1.0 s |
| 1 × 65 MB (**over** cap) | 1 scan | **0 KB** | 10,624 KB | 0.9 s |

A single keystroke moved RSS from ~11 MB to **502 MB**. The over-cap row also
documents a second, user-visible defect: a session larger than 64 MiB is
silently skipped (`jc_session.c:357`), so it vanishes from `/sessions` and
`/resume <its id>` reports "no session matching" with no explanation.

### Attribution (massif)

Profiling the same `jc_session_list` through the non-interactive `ls --all`
(`src/main.c:1909`), 20 × 200 KB store:

```
179  141,248,000   7,874,240   7,866,324        <- peak snapshot
->54.20% (4,268,192B) block_new (jc_mem.c:36)
| ->54.10% (4,260,000B) jc_arena_alloc (jc_mem.c:86)
| | ->52.02% (4,096,160B) jc_read_file (jc_platform_posix.c:120)
| | | ->52.02% (4,096,160B) jc_session_list (jc_session.c:355)
| | |   ->52.02% run_ls (main.c:1909) -> main (main.c:8433)
| | ->02.08% (163,840B) jc_arena_strdup -> jc_session_list (jc_session.c:362)
->44.19% (3,479,607B) parse_string (cJSON.c:152)   <- transient, correctly freed
```

`4,096,160 B` is the 4,096,000-byte store plus alignment. The graph is a
**monotone ramp that ends at its peak** — contrast M180's *"snapshots oscillate
with the per-turn session save and return to ~0.5 MB."*

The two branches together are the whole point: the **44% cJSON tree is freed**
(`cJSON_Delete`, `:374`) and it is the one that held the four scalars the
function actually wanted; the **52% raw text is kept** and is never read again.
It is exactly backwards.

## Mechanism B: the soak's blind spot

`tests/measure/soak.py` gained a `--fixture-bytes` knob (default 13, preserving
the historical behaviour). Changing *only* that number, 60 read turns each:

| fixture | slope | tail-half | RSS |
|---|---|---|---|
| 13 B (M180's) | **6.4 KB/turn** | 5.1 | 11,656 → 12,036 KB |
| 200 KB | **217.7 KB/turn** | 206.3 | 12,272 → 25,116 KB |

**A 34× jump in the measured slope from a one-line fixture change**, tracking
file size ~1:1. `read_file` copies the whole file onto `app->arena`
(`src/tools/jc_tool_read.c:79`) and the `readMaxBytes` cap bounds only what is
appended to the *output*. `edit_file` (`:63`) does the same with the *pre-edit*
content, so read→edit→edit→edit retains one full copy per edit. Same shape at
`apply_patch:112`, `jc_tool_ls.c:30`, `jc_lsp.c:568,647,867`,
`jc_memory.c:81,102,134,253` (two copies per call), `jc_tool_todo.c:151`, and
`jc_tui.c:1474` (every Tab press on an `@path`).

The correct pattern already exists two files away: `src/chat/jc_app.c:248,715`
use `jc_app_scratch(app)`.

## The CI blind spot

```
$ valgrind --leak-check=full --errors-for-leak-kinds=definite,indirect \
      jichi ls --all          # a run that retained 4 MB it never needed
==11380==     in use at exit: 0 bytes in 0 blocks
==11380== ERROR SUMMARY: 0 errors from 0 contexts
```

Zero. `main` calls `jc_arena_free(arena)` on every exit path, and until then the
blocks are reachable from a live root, so LeakSanitizer would classify them
*still-reachable* rather than leaked. `make ci` runs exactly this command
(`Makefile:325-326`).

**`make ci` passing green is not evidence of absence for arena-lifetime bugs.**
Only peak/over-time instruments see them: massif's peak, `/proc` RSS, and
`jc_arena_used` via `/context`. That is the single most transferable lesson here.

## So what was the 12.5 GB?

Under the measured linear model (`S + 138·F` per scan), with no second mechanism
required:

| store bytes | scenario | `/sessions` presses | `/resume <alias>` (3 scans) |
|---|---|---|---|
| 17 MB | this machine's jichi-only store | ~750 | ~250 |
| 100 MB | `~/.continue/sessions` shared with Continue CLI | ~128 | ~43 |
| 500 MB | long `--auto` runs (full history rewritten every turn) | ~26 | ~9 |
| 2 GB | heavy shared store | ~7 | ~2 |

The amplifier is not a superlinear term but an **unbounded coefficient**: there
is no session equivalent of `JC_SNAPSHOT_MAX_LIST`. No rotation, no cap. Every
headless run, TUI launch, ACP `session/new`, and `/fork` adds a file
permanently; the only removal path is the interactive `/sessions clear`. The
17 MB measured today is a snapshot of a monotonically growing quantity, so every
`/sessions`, `/resume`, and `--continue` startup gets more expensive over the
tool's lifetime.

Given the reported workload was interactive TUI use with repeated `/sessions`
and `/resume`, mechanism A is the primary explanation, and mechanism B accounts
for any autonomous-run component. Neither requires a store larger than a
long-lived shared `~/.continue/sessions` plausibly reaches.

## Reproducing

```sh
make
python3 tests/measure/idle_tui.py --secs 60 -- ./jichi        # control: flat ~11.6 MB

# controls: must stay flat, else the hypothesis is wrong
python3 tests/measure/session_scan.py --variant help  --calls 20 --files 50 --bytes 4096
python3 tests/measure/session_scan.py --variant idle  --calls 20 --files 50 --bytes 4096

# the phenomenon, and the 1:1:2:3 scan inventory
for v in sessions tab resume_prefix resume_alias; do
  python3 tests/measure/session_scan.py --variant $v --calls 20 --files 50 --bytes 4096
done

# headline, mirroring a real 17 MB store
python3 tests/measure/session_scan.py --variant sessions --calls 12 \
    --files 250 --bytes 71680 --csv /tmp/headline.csv

# single-keystroke worst case (--calls 0 => exactly one scan)
python3 tests/measure/session_scan.py --variant sessions --calls 0 \
    --files 8 --bytes 62914560 --rss-ceiling-kb 900000

# mechanism B
JC_SOAK_BIN=$PWD/jichi python3 tests/measure/soak.py --turns 60 --fixture-bytes 13
JC_SOAK_BIN=$PWD/jichi python3 tests/measure/soak.py --turns 60 --fixture-bytes 204800
```

Both scripts are measurements, not CI gates (the `tests/bench` precedent). They
refuse to touch the real `$HOME`, cap peak RSS (`--rss-ceiling-kb`, default
400 MB), and clean up their temp store unless `--keep`.

**12.5 GB is not reachable on this 4.9 GB host** — every run above caps at
≤502 MB and the table extrapolates arithmetically from coefficients that track
prediction to within 2.5% across a 512× span of file size and a 16× span of
file count.

## Harness note worth keeping

The first driver silently glued commands together: it batched keystrokes, and
`jc_term` treats a newline as a *pasted row* rather than a submit whenever more
input is already buffered (`input_pending`, `src/tui/jc_term.c:780` — the M156
burst-paste fallback). The fix is two waits per command: a short quiet window so
the trailing newline is consumed alone, then a **unique output marker** for
completion. Output silence cannot mean "done" — a 480 MB scan produces no output
at all for its whole duration, which is what made an early attempt look like a
600-second hang.

Second gotcha, worth knowing before trusting any store-keyed measurement: **most
of the existing harnesses do not isolate `$HOME`**, and starting the jichi TUI
creates and saves a session. `tests/measure/idle_tui.py` leaves one empty file in
the *real* `~/.continue/sessions` per run (by design — it compares whole tools as
a user runs them), and `tests/e2e/_e2e.py` has no `HOME` override either, so a
full `run.sh` deposits ~8 sessions there, recognisable by their
`workspaceDirectory` of `/tmp/jichi_e2e_*`. Harmless in themselves, but they
silently perturb the very store mechanism A is measured against — and they mean a
developer's store is partly an artifact of their own test runs. `session_scan.py`
isolates `HOME`, `mkstore.py` refuses to write anywhere near the real one, and
`sessions_footprint.py` sets and restores `HOME` around its spawn. Worth fixing
in `_e2e.py` generally (see follow-ups).

## Not the cause (checked, so nobody re-checks)

No reachable overflow or infinite loop in `jc_vec` (`src/util/jc_vec.c:35-37`),
`jc_sb` (`src/util/jc_str.c:38-41`), or `jc_arena_alloc`'s size rounding
(`src/util/jc_mem.c:73`) — each needs a size backed by more memory than exists.
`jc_term`'s Tab path frees every candidate on every branch
(`src/tui/jc_term.c:892-897`). Session ownership across `/resume` and `/fork` is
clean: history does **not** accumulate (`jc_tui.c:2691-2694`, and
`load_from_text` inits history at `jc_session.c:269`) — measured store drift was
`+0 files, +0 bytes` for `/sessions`/Tab and `+1 file, +127 bytes` for the
`/resume` variants, exactly the expected save of the outgoing session.
Compaction and `jc_sysmsg_build` correctly use scratch; measured turn-scratch
stayed within 4 KB across every run.

## The fix

### Mechanism A — `jc_session_list` and its callers

Two build-local arenas inside `jc_session_list`, in the shape M140 used for the
repo map (`src/index/jc_repomap.c:666,719`): one for the dirent list, one for the
file text **reset every iteration** — so the transient peak is a single session
file rather than the whole store. Only the bounded per-session metadata is
allocated on the caller's arena. `jc_session_load_by_id` likewise reads into a
local arena it frees (`load_from_text` copies the four scalars onto the caller's
arena and the history into malloc'd messages, so nothing needs the raw text).

The three resolvers (`jc_session_load_recent_scoped`, `_resolve_prefix`,
`_resolve_alias`) list onto a local arena and copy the one id they return out
before freeing it. The three TUI call sites (`print_sessions`, `clear_sessions`,
`tui_complete`) do the same, because the metadata is consumed inside each call —
that is what makes repeated `/sessions` flat rather than merely 800× cheaper.

Two redundant scans removed: the literal duplicate `jc_session_resolve_alias`
(`src/tui/jc_tui.c:2674`/`:2679` — the condition already resolved a bare token),
and `jc_session_open`'s resolve-then-re-read, which now tries an exact id
directly before paying a whole-store scan. That path is guarded by a
`[A-Za-z0-9_-]`-only check so a token can never reach `session_path()` with a
`/` or `..` in it.

| variant | before | after |
|---|---|---|
| `/sessions` | 17,535 KB/call | **0.0 KB/call** |
| Tab on `/resume ` | 17,535 KB/call | **0.0 KB/call** |
| `/resume <full-id>` | 35,070 KB/call | **0.0 KB/call** |
| `/resume <alias>` | 52,605 KB/call | **0.0 KB/call** |
| 480 MB store, one `/sessions` | 491,521 KB arena / 502,252 KB RSS | **0 KB arena / 10,516 KB RSS** |

(250-file / 17.9 MB store, 12 calls, R² = 1.000000 in every case.)

### Mechanism B — per-tool-call file retention

Routed to `jc_app_scratch(app)`, which is reset at each top-level turn
(`src/chat/jc_agent.c:2014`) — the same remedy M180 used for its four fixes, and
semantically right: these are per-turn transients. Sites: `jc_tool_read.c:79`,
`jc_tool_edit.c:63`, `jc_tool_apply_patch.c:112`, `jc_tool_ls.c:30`,
`jc_lsp.c` (three), `jc_memory.c` (three transient reads; the bounded note text
it *keeps* still goes on the session arena, correctly).

`tui_complete`'s `@path` listing got a local arena rather than scratch, because
Tab completion runs *between* turns — scratch would not be reset between
keypresses.

| soak fixture | before | after | tail-half after |
|---|---|---|---|
| 200 KB | 217.7 KB/turn | 18.6 KB/turn | **2.4 KB/turn** |
| 1 MB | ~1,050 KB/turn (modelled) | 27.1 KB/turn | **1.1 KB/turn** |

The decisive property is not the smaller number but that it **no longer scales
with file size**: a 5× bigger file yields no more growth (1.1 vs 2.4 KB/turn is
noise). Before, the slope tracked file size 1:1.

### The gates

- **`test_session_footprint`** (`tests/test_session.c`) — a *footprint*
  assertion, since leak checkers cannot see this. Its load-bearing check is a
  shape, not a constant: *two stores with the same file count and a 128×
  difference in file size must cost the same arena bytes.* Also asserts the Nth
  scan costs what the 1st did, and that resuming a 64 KB session does not retain
  64 KB. Verified to have teeth: reverting only the `jc_session_list` arena turns
  it red with 4 failures.
- **`tests/e2e/sessions_footprint.py`** — the unit test cannot see a *call site*
  passing the wrong arena, which is where the user-visible bug lived. Drives the
  real TUI, reads `/context`, asserts 10 × `/sessions` over a 512 KB store grows
  the session arena by < 128 KB (measured: 0 KB). Registered in `run.sh`.
- `session_scan.py` now prints a **FIXED / partial / UNFIXED** verdict against
  the pre-M197 model, so it stays a regression detector rather than a description
  of one build.

Verification: `make WERROR=1 test` (9020 checks, 0 failures), `make SAN=1
CC=clang test` clean, `valgrind --leak-check=full` 0 errors / 0 definitely lost,
full `tests/e2e/run.sh` green.

## Follow-ups

1. **Session-store rotation**, or at least a `doctor` warning above some store
   size. The retention is fixed, but `/sessions` still *reads and cJSON-parses*
   every file, so a 480 MB store costs ~3 s of latency per invocation. The store
   has no cap and grows by one file per run.
2. **The residual re-strdup pattern**: `jc_memory.c:102` and
   `jc_tool_todo.c:151` replace a whole value on the session arena per call, so N
   updates retain N copies. Bounded (`JC_MEMORY_MAX`, todo-list size) and far
   below the mechanisms fixed here, but it is the same ownership smell and wants
   a different model than an arena.
3. ~~**Intra-turn bound for mechanism B.**~~ **Done in M199.** The audit changed
   the design: a blanket per-tool-call reset is unsafe (`spawn_subagent`'s seed
   task and tool fence live on scratch across the nested run; `remember`,
   `todowrite`, read-tracking and background processes are session-lived), so a
   **third** arena (`jc_app_tool_scratch`) takes only per-call transients and is
   reset before every tool call. Measured peak RSS on 40 reads of a 1 MB file per
   turn: **55,140 → 15,424 KB**, verified by disabling only the reset and
   re-measuring.
4. **`HOME` isolation in `tests/e2e/_e2e.py`.** The suite writes ~8 sessions into
   the developer's real `~/.continue/sessions` per full run (workspace
   `/tmp/jichi_e2e_*`). An opt-in `env` parameter on `spawn`/`run`, or a
   suite-wide temp `HOME`, would stop tests from mutating real user state — the
   same class of problem as the M180 blast-radius lesson, one level up.
5. **Smaller findings:** bare `/resume` saves the live session first
   (`jc_tui.c:2658`), setting its mtime to now, so `jc_session_load_recent_scoped`
   always picks the session you are already in and the "no earlier session"
   branch at `:2668` is unreachable; a FIFO named `*.json` in the store hangs
   `/sessions` forever (`jc_read_file` `fopen`s without an `S_ISREG` check);
   crash-left `*.json.tmp<pid>` files are never cleaned; second-granularity
   `jc_file_mtime` makes "most recent" non-deterministic under concurrent
   processes.
