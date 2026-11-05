# Gate durations, and working while the gate runs (2026-10-12)

**Audience:** the operator and any contributor who runs `make ci` in this
repository and wants the tree back while it runs. Not for: a reader who just
wants the gate's verdict (that is the log's last line). **Read further:**
[BUILD.md](../BUILD.md) "The working cadence", [SESSION_RUNBOOK.md](../SESSION_RUNBOOK.md),
[TEST_TIERS.md](../TEST_TIERS.md).

## 1. The distribution of complete gate runs (measured)

Source: the box's gate logs, one file per run (fresh name per run), with
durations taken from filesystem birth-time (start) to mtime (end). Machine:
Ryzen 9 3900X / 32 GB / NVMe, the machine the M936 table was measured on.
Window: 2026-10-06 → 2026-10-09 (thirteen uncontended complete gate runs
across the M910–M921 arc and the merge of 10-06) plus the 2026-10-12 gate on
`bce78f68` (below).

**Green, complete `make ci` runs: thirteen uncontended, every one 27.6–28.1
minutes (mean 27.8, stdev 0.17)**; and one more on 2026-10-12 of at least
31 minutes (log end 17:17; launch after 16:16 — lower bound). The
contended-vs-uncontended delta is inferred, not measured: the log name was
reused and the run journals record no launch timestamp for this gate.
The tightness of the uncontended group is
the point: on a box that owes the gate its whole CPU/RAM budget the gate's
duration is a *stable property*, not a per-run surprise. The measured stage
table in BUILD.md accounts for it — fuzz ~13 min (23 targets × 2,000
iterations) + the smoke tier ~10 min + e2e ~2 min + the clean builds, the
sanitizer/valgrind/leak passes and the rest.

**Red runs are shorter by construction** — a gate that fails exits at its
failing stage, so the red durations are a histogram of *which stage died
first*, not a second distribution. The 10-06→10-09 red gates cluster at
~10–24 min (compile / `WERROR` / valgrind / smoke-tier deaths). This
session's full gate died at valgrind in ~9 min; its three *standalone
smoke-tier re-runs* (the dead-stage discipline, not full gates) died at
~10 / 15 / 16 min in the smoke lints. The spread is the stage order made
visible.

**Three files looked like 25–53-hour gates; they are filename reuse, not long runs.**
`/tmp/ci.log`, `/tmp/ci_final.log`, `/tmp/ci-final.log` each carry one or two
gate verdicts with a birth-time days older than the run — the name was
recreated by an earlier session and reused. They are excluded; the lesson is
recorded in §4 (a durable, per-run-named log removes this ambiguity for free).

The 2026-10-12 green gate itself: end 17:17 (log mtime), launched after the
last pre-gate commit at 16:16 — at least 31 minutes (the launch time is not
recorded in the reused log, so this is a lower bound). It sits above the
27.6–28.1 uncontended cluster; the delta’s cause is not measured (box
contention per §2 is the most likely, but the run journals hold no
contemporaneous record to confirm it). This run is the §4.1 lesson made
concrete: a reused log name loses the start time.
(The M940 stall incident that preceded it — a background `make ci` blocked
2 h+ on its 64 KiB output pipe — is the
reason the gate now runs with its output spooled to a file; the stall is
fixed in the binary by M940, and the workflow below keeps the file habit.)

## 2. What actually blocks the tree while the gate runs

The gate is not a single process; it is a *sequence of tree rewrites*.
`make ci` runs `make clean` **nine times** (plus `make smoke-mutant`'s own
build), and each clean deletes every object file, `run_tests`, and `jichi`
from the build directory and rebuilds it for the next stage (gcc WERROR, clang WERROR, g++, ASan, valgrind, fuzz,
curl-free, shipping). Concretely, while a gate runs in a tree:

| resource | what the gate does to it | a second `make`/`run_tests` in the same tree |
|---|---|---|
| `*.o`, `run_tests`, `jichi` | deleted and rewritten at each of the nine cleans | **builds against a half-replaced object set** — a race whose failure mode is a confusing link error or, worse, a binary that mixes stages |
| git working tree | untouched (the gate commits nothing) | safe for `git add/commit` at any moment |
| `~/.jichi.d/` (HOME state) | smoke/e2e stages write under a sandboxed HOME (`smoke_home`); the gate's own run journals land in `runs/` | safe, already collision-guarded since M940 (`bg-<id>-<pid>.out`, age-guarded sweep) |
| CPU/RAM (32 GB) | ASan/valgrind/fuzz stages are the heavy users; a concurrent clean build steals cores | **does not corrupt, but inflates both durations unpredictably** — the 27.6–28.1-minute distribution assumes an uncontended box |

Two consequences follow, and both shape the recommendation:

1. **The corruption class is build-artifact sharing** — the same tree
   directory. It is avoided by giving the gate its *own* tree, not by
   timing.
2. **The cost class is contention** — CPU/RAM. It is not avoidable by
   worktree, only by discipline (no heavy concurrent `make` in the main
   tree during the gate's heavy stages; cheap `make test` in a warm tree
   is a ~7-second event and negligible next to 28 minutes).

## 3. Design: the gate runs in a worktree, the work runs in the main tree

**Recommendation:** run `make ci` in a **dedicated `git worktree`** checked
out at the gate head, with its output spooled to a per-run log; do the next
milestone's work in the main tree while it runs.

```sh
# in the main tree, after the series is merged & pushed:
git worktree add ../jichi-gate <gate-head>        # own object dirs, own binaries
cd ../jichi-gate
make ci </dev/null > /tmp/ci-<gate-head-8>-$(date +%H%M).log 2>&1
cd -                                              # back to the main tree: it is FREE
```

Why this, and what it costs:

- **Decisions grounded in §2.** The worktree removes every entry of the
  corruption table: the gate's nine cleans touch a second set of object files
  and binaries; the main tree's build state is untouched for the run's entire life.
  Git's working-tree entry is safe under both (the gate commits nothing).
- **Implication 1 — the gate must be on an already-pushed head.** The
  cadence rule ("one `make ci` on the final head before ff-merge") inverts
  for the worktree: the head is on master *first*, then the gate runs on it
  in the worktree, and the work on the *next* milestone starts in the main
  tree immediately. A red gate is then a forward fix on master (a new
  commit, a re-run of the dead stage, §2's red-run fact) — never a red gate
  on unpushed branch work. The operator's standing rule is only relaxed in
  direction, not in strength: still one gate per series, still on the exact
  head that ships.
- **Implication 2 — the gate duration assumes an uncontended box.** The
  main tree's cheap tiers (`make test` ~7 s, one driver ~1 min, one lint)
  sit inside the noise of the 27.6–28.1-minute cluster — the 0.17-min
  stdev of the uncontended 10-06→10-09 runs is the gate's own floor; the
  10-12 run at ≥31 minutes (lower bound) shows what contention adds. What must not run in the main tree while the gate runs is a
  second `make ci`, a `make fuzz`, or a clean full rebuild — those would
  rewrite the shared CPU/RAM budget the distribution was measured under.
- **Implication 3 — one worktree, reused.** `git worktree add` once;
  `git -C ../jichi-gate fetch && git -C ../jichi-gate checkout <head>`
  per gate. Two concurrent worktrees for the *same* head would reintroduce
  contention without removing anything.
- **Implication 4 — the running `jichi` binary is the main tree's.** If the
  session driving the next milestone uses `./jichi` (main tree), it keeps
  working across all nine cleans of the gate — that is the visible benefit,
  and it is exactly what is impossible in a shared tree.

**Rejected alternatives:**

- **Timing / stage-ordering discipline** (work only during fuzz, stop
  before the smoke stage): the 27.6–28.1-minute number is stable, but the *stage
  boundary* the work must fit inside moves as stages change, and a
  mid-stage `make clean` by the operator is one habit away from a corrupted
  object set. Discipline that fails on a single slip is not a workflow.
- **A second full clone** (not a worktree): works, but pays disk and
  re-fetches history per gate; a worktree shares the object store and is
  the tool for exactly this.
- **Waiting (the status quo)**: costs ~28 minutes of the main tree per
  series. For a series of ~5 commits that is more idle time than the series'
  entire working time — which is why the box ran one gate per milestone commit,
  not one per series.
- **Container/VM isolation**: removes contention too, but the box's RAM
  budget (32 GB; the gate's heavy stages want it to themselves) makes a
  *separate machine or VM* the honest version of this, and that is an
  infrastructure decision, not a workflow one. Noted, not recommended,
  until the gate's clean-build stages measurably need more RAM than the box
  gives a worktree.

## 4. Two small habits the workflow depends on

1. **A per-run log name** (`ci-<head-8>-<HHMM>.log`). This session's three
   25–53-hour phantom durations (see §1) came from reusing `/tmp/ci*.log`
   names across days. A name that cannot collide makes the duration
   measurable *from the log's own timestamps*, and the distribution in §1
   reproducible by anyone on this box.
2. **Output spooled to a file, always** (M940's operating note): a gate run
   inside a session whose binary predates M940 still pipes the child's
   output — the very stall this box met on 2026-10-12. `> log 2>&1` from a
   terminal is immune either way.

## 5. Not goals

- This does not change *when* the gate runs (one per series) or what it
  runs (`make ci`), or the merge order (gate first, push after — rule 8).
- It does not claim the worktree makes a red gate cheaper — a red gate is a
  dead-stage re-run either way, and BUILD.md's "re-run the dead stage"
  discipline applies to the worktree exactly as it does to the main tree.
