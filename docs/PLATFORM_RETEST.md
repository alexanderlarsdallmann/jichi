# When a verified platform needs re-running

*The operator's question, M647: "At which point do we need a retest on all the
various platforms and operating systems?" This page is the answer, and it is
deliberately not a calendar.*

Read with [`PLATFORMS.md`](PLATFORMS.md) (the matrix and the per-row evidence)
and [`LOW_MEMORY.md`](LOW_MEMORY.md) (RAM tiers and the rig inventory).

---

## 1. Why not a calendar

A row in `PLATFORMS.md` is **a stamped datum about a commit**, not a
subscription. "FreeBSD 15.1, full gate green, M465" does not decay on 1 October.
It is still exactly as true as the day it was measured, and it will be true
forever, because it is a claim about a tree that still exists.

What changes is not the row's truth but its **reach**: the tree has moved, and
the row speaks for a smaller and smaller fraction of it. So the trigger for a
re-run is never "it has been N weeks". It is one of two things:

1. **The tree changed under the row** in a way that could plausibly break that
   platform (§2), or
2. **The row's coverage debt** has grown large enough that citing it in the
   present tense is no longer honest (§3).

This follows the rule the matrix already states: *each verified platform is kept
as its own stamped datum rather than overwritten*. A re-run adds a row; it does
not refresh one.

---

## 2. Triggers: the changes that invalidate a row

Every trigger below is named after an incident in this project, because a
trigger list assembled from imagination is a list of the failures nobody had.

| # | Change in the tree | Which rows it puts in doubt | Why — the incident |
|---|---|---|---|
| **T1** | A **capability probe** added, removed, or reworded | **every non-glibc row** | M449: uClibc-ng declares `malloc_trim` only under `__USE_GNU`, so the symbol *linked* while the declaration was hidden and the probe answered yes. M458: `CC ?= cc` means a system shipping neither `cc` nor `c99` reports every feature absent rather than the compiler missing. A probe is the single most platform-sensitive thing in the build. |
| **T2** | A **platform conditional** (`#ifdef`, a `uname` branch, a `/proc` path) added or changed | the rows on the *other* side of the branch | M400: `jc_mem_total_mb`'s Darwin `sysctl(HW_MEMSIZE)` path was un-compilable under this project's own C89 flags and nobody noticed, because no Darwin row exists to notice it. A branch nobody runs is a branch nobody tests. |
| **T3** | **Signal, process, or terminal discipline** changed | every BSD and every row whose `/bin/sh` is not bash | M467: a backgrounded subshell's `$!` names the subshell on ksh, so the signal and abort drivers failed on OpenBSD for a reason that had nothing to do with the product. Solaris 11's `/bin/sh` is ksh93, which makes this load-bearing for the illumos row that does not yet exist. |
| **T4** | **libcurl usage** changed — a new `CURLOPT`, a raised version floor, a new callback | the musl/static rows, the curl-free row, and any row on an old distribution | The floor is libcurl 7.19.4 and the tree has a deliberately minimal-curl build (`scripts/minimal-curl.sh`). A `CURLOPT` added on the development box's 8.5 is invisible here and absent there. |
| **T5** | **Filesystem or permission semantics** relied on | Windows layers, anything on a network or translated filesystem | M475: the same commit reads 0 modified on ext4 and **1,639 modified** through v9fs. M490: MSYS2's `noacl` mount makes `chmod` return success and change nothing, so jichi's file-privacy guarantees **do not hold there** and the page says so. |
| **T6** | A **new smoke driver** exercising a syscall, a device, or a terminal behaviour | every row, in proportion to §3 | This is the ordinary case, and the only one that accumulates silently. It is what §3 measures. |
| **T7** | The **timeout model** changed | every row with a `JC_SMOKE_TIMEOUT_MULT` above 1 | The multiplier is a *ratio* — device build seconds ÷ this bench's build seconds — so it is invalidated by a change to what the bench does, not only by a change to the device. |

**T1–T5 and T7 are step changes: they invalidate rows at a stroke, and the
milestone that makes such a change owns the decision about which rows to re-run
or to re-label.** T6 is the creeping one, and it is the reason for §3.

---

## 3. Coverage debt: the one number that makes staleness measurable

Most rows record how many smoke drivers they ran. The tree records how many
exist. The difference is the row's **coverage debt** — drivers that have never
executed on that platform:

    coverage debt = (smoke drivers in the tree today) − (drivers the row ran)

It is deliberately crude. It counts drivers, not risk, and a driver that tests
JSON parsing is worth less here than one that tests signals. What it has going
for it is that **it is computable without re-running anything**, it cannot be
argued with, and it goes in one direction.

Worked, at M647 with **298 drivers** in the tree:

| Row | Drivers it ran | Debt | What that means |
|---|--:|--:|---|
| FreeBSD 15.1 (M465) | 201 | **97** | Verified, and 97 drivers have never run on this kernel since |
| NetBSD 10.1 (M480) | 209 | **89** | 88 drivers unexercised on a BSD that ships GNU userland tools |
| OpenBSD 7.9 (M481) | 209 | **89** | 88 drivers unexercised under ksh as `/bin/sh` — the axis nothing else in the matrix covers |
| Windows 11 + WSL2 (M475) | 209 | **89** | same, and this row also carries T5 (the `/mnt/c` translation layer) |
| Raspberry Pi Zero 2 W (M272) | 94 | **204** | the aarch64 row speaks for under a third of today's tier |

**The thresholds, and they are conventions rather than discoveries:**

- **Debt < 25 drivers** — the row is current. Cite it in the present tense.
- **Debt 25–100** — the row is **sound but partial**. It may be cited with its
  stamp ("green on FreeBSD at M465") and **not** without one ("jichi runs on
  FreeBSD"). This is where most rows live and it is not an emergency.
- **Debt > 100** — the row has become a *historical* datum. It stays in the
  matrix, because deleting measurements is how a project forgets what it
  learned, but a claim resting on it needs a new run first.

A threshold in drivers rather than milestones is on purpose: milestones vary
enormously in how much test surface they add, and the thing that actually went
unexercised is a driver.

---

## 4. The retest ladder — cheapest rung that answers the question

Re-running a row is not one thing. Pick the rung by what the change touched
(§2), not by what is available:

1. **`make` alone** — answers *does it still compile here*. Minutes. The right
   rung after a pure-C89 or header change, and the only rung most T2 changes need.
2. **`make test`** — adds the unit suite. Answers *do the pure cores still agree
   with this libc*. The right rung after T1.
3. **`make check-target`** (`test` + `smoke`) — the honest floor for re-labelling
   a row **Verified**, because the smoke tier is what exercises processes,
   signals, terminals and the filesystem. The right rung after T3, T5, T6.
4. **`make ci`** — two compilers, sanitizers, valgrind, fuzz, e2e. Only the
   development box and the rows whose original verdict was "full gate" need
   this, and only when the verdict itself is being restated.

**Rig output goes to `$TIER_V_DIR`, never the repo tree** — a rig that dirties
the tree it tests has been three separate mistakes in this project.

---

## 5. So: when do we retest everything?

**Never all at once, and that is the recommendation, not an evasion.** A
simultaneous sweep of every row costs days of VM and hardware time, produces one
enormous undifferentiated result, and — because it is expensive — gets deferred
until the answer is stale anyway. The measured alternative:

- **On a T1–T5 or T7 change:** the milestone making the change re-runs the rows
  that change puts in doubt, at the ladder rung that change requires. Usually
  one to three rows, usually rung 2 or 3. This is the main mechanism and it is
  the cheap one.
- **Before a release that claims platform support:** re-run every row whose debt
  is over 100, at rung 3. At M647 that is the Pi Zero 2 W row and nothing else
  among the full-gate rows.
- **Never on a schedule.** A calendar re-run tests the calendar.

**The honest gap in this policy:** it assumes somebody notices that a change is a
T1–T5 change. Nothing enforces that, and `platform_retest_lint.sh` does not try
to — it checks that every Verified row carries a stamp, so debt is *computable*
at all, which is the precondition for this page rather than the policy itself.
Automating the trigger detection would mean classifying every diff, and a
classifier that is wrong in the quiet direction is worse than a rule somebody
has read.

---

## 6. Which row to run next

From `PLATFORMS.md` and `LOW_MEMORY.md`, in order of value per hour:

1. **illumos** (OpenIndiana or OmniOS) — **the cheapest remaining row**, and the
   operator's separate question. Free, ISO-installable under KVM, and
   `scripts/tier-v-openbsd.sh` is the pattern to copy. Its hazards are already
   identified *from the source rather than guessed* (`PLATFORMS.md` §"Solaris /
   illumos"): procfs is **present but different** — `/proc/self/stat` is absent
   so the probe takes the degradation path the BSDs already proved, while
   `/proc/self/status` **exists as a binary `pstatus_t`**, so `fopen` succeeds
   where a reader might hope it failed and `jc_meminfo_parse` hunts for `VmRSS:`
   in binary bytes, finding nothing and reporting zero. That is *no data* rather
   than *wrong data*, which is the right failure — and **the parsing half of
   that prediction is now measured** (M647,
   `tests/test_meminfo.c:test_binary_status`), because it reduces to a pure
   function over bytes and needed no illumos box. What still needs the box: that
   `fopen` succeeds there, that the `pstatus_t` has this shape, and that
   `jc_meminfo_self`'s `fread` path behaves the same. `/bin/sh` is ksh93
   on Solaris 11, which makes T3 above load-bearing.
2. **macOS** — the only never-compiled row with a known Darwin-specific code
   path (T2, M400). Blocked on hardware, not on work.
3. **Oracle Solaris 11.4** — needs an Oracle account, and its licence terms are
   the operator's decision rather than a technical one. A green *illumos* row
   would still leave Oracle Solaris unmeasured, and the matrix will say so.

A non-Linux row is a **defect detector, not compatibility work**: two OpenBSD
findings were bugs on every platform, and ten defects came out of FreeBSD and
OpenBSD between them with none of them wanting a BSD conditional.
