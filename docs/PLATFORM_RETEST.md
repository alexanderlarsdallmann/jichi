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
| **T3** | **Signal, process, or terminal discipline** changed | every BSD and every row whose `/bin/sh` is not bash | M467: a backgrounded subshell's `$!` names the subshell on ksh, so the signal and abort drivers failed on OpenBSD for a reason that had nothing to do with the product. Solaris 11's `/bin/sh` is ksh93, which is why this is load-bearing for the illumos row — added at M658 and green since M703. |
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

Worked, at M664 with **303 drivers** in the tree (298 at M647, 300 at M658 —
the figure moves with every driver added, which is the point of it):

| Row | Drivers it ran | Debt | What that means |
|---|--:|--:|---|
| FreeBSD 15.1 (**M665**) | 302 | **1** | re-run 2026-09-18; was 201 drivers and debt 102, past the threshold. Six drivers failed and **five were harness defects, not platform ones** — the row's value was finding them. `parallel_abort` remains, measured and not diagnosed |
| NetBSD 10.1 (M480) | 209 | **94** | 94 drivers unexercised on a BSD that ships GNU userland tools |
| OpenBSD 7.9 (M481) | 209 | **94** | 94 drivers unexercised under ksh as `/bin/sh` — the axis nothing else in the matrix covers |
| Windows 11 + WSL2 (M475) | 209 | **94** | same, and this row also carries T5 (the `/mnt/c` translation layer) |
| **Windows + Cygwin (M698, 2026-09-22)** | **317** | **0** | measured at the 317-driver tree **through a rig** (`scripts/tier-v-cygwin.sh`), so the row is reproducible rather than hand-made. 3 killed at a 780 s deadline and 4 failed. This row had never appeared in this table, though it is one of only two carrying **T5** *and* a documented safety difference |
| **Windows + MSYS2 (MSYS) (M698, 2026-09-22)** | **317** | **0** | same tree, same day, through `scripts/tier-v-msys2.sh`; 3 killed at 660 s and 2 failed. Measured in the **configured** (`acl`) state, which the rig records, because a row measured only in the configured state is true for nobody |
| Raspberry Pi Zero 2 W aarch64 (**2026-09-18**) | 302 | **1** | measured at the 303-driver tree, `lite_context_cap` the only failure. That driver was **the driver rather than the board** (M672) — checks 4-5 assumed the absence of `--lite` meant the normal profile, but lite auto-enables below the resource tier and this board reports `tier: minimal (lite)` on 415 MB. Fixed and **verified 5/5 on the board**; the row's full re-run at the current tree is pending and this number is the one that was measured, not the one that is expected. Also **Driven**: text and agentic turns, 17 s each |
| Raspberry Pi Zero 2 W armhf (**M658**) | 297 | **6** | re-run 2026-09-18; was 194 at M454, debt 106 |
| Raspberry Pi 400 aarch64 (**2026-09-18**) | 303 | **0** | the whole tier, 0 failures, reproduced three times. The row read *n/a* for months because `tier-b-device.sh` ran the gate **without `JC_SMOKE_KEEP_GOING=1`**, so the tier stopped at the first failing driver and never printed a summary — the missing denominator was a rig defect, not a device one |

> **A rig that fails must still report its denominator (M665).**
> `scripts/tier-v-bsd.sh` captured `smoke: OK (N drivers, M checks)` only when the
> tier PASSED; on failure it captured the failing checks and nothing else. So a
> partly-green row came back with **no driver count**, and its debt stayed
> uncomputable — the same gap this page records for the Pi 400, met again. The
> count is free at the time and unrecoverable afterwards, and a row that failed is
> precisely the one whose denominator a reader wants.
>
> **The device rig was then checked, and its cause was different and worse
> (2026-09-18).** `tier-b-device.sh` did not merely fail to *extract* a count — it
> ran the gate **without `JC_SMOKE_KEEP_GOING=1`**, so the tier stopped at the
> first failing driver and never *printed* one. Nothing could have been extracted.
> That is why the Pi 400 stood at **n/a** here for months; with the variable set it
> reports `smoke: OK (303 drivers, 1,761 checks)`, debt **0**. The BSD rig has set
> it since M466. **Checked at M703:** `scripts/tier-v-illumos.sh` runs the tier under `JC_SMOKE_KEEP_GOING=1` and captures the failing checks and the count, so neither shape can hide.

> **A DEBT OF ZERO IS NOT THE WHOLE STORY, AND M698 IS WHY.** Coverage debt counts
> drivers that *executed*, and a driver killed at its deadline executed. Both
> Windows rows above read debt **0** while three drivers on each were stopped
> rather than finished, so their outcome is a bound and their output is not
> evidence. Worse, the count cannot see what a deadline conceals: at the shipped
> deadlines this project recorded *"17 killed and zero genuine check failures"* for
> Cygwin, and at a measured multiplier the same tree reports `setup_keyfile`
> **failing on both emulation layers** — it needs ~400 s and was being killed at
> 60 s. **Debt measures reach, not verdicts.** Read it beside the killed count and
> the multiplier, and treat a row whose multiplier is 1 on slow silicon as
> unmeasured rather than clean.

**The thresholds, and they are conventions rather than discoveries:**

- **Debt < 25 drivers** — the row is current. Cite it in the present tense.
- **Debt 25–100** — the row is **sound but partial**. It may be cited with its
  stamp ("green on FreeBSD at M465") and **not** without one ("jichi runs on
  FreeBSD"). This is where most rows live and it is not an emergency.
- **Debt > 100** — the row has become a *historical* datum. It stays in the
  matrix, because deleting measurements is how a project forgets what it
  learned, but a claim resting on it needs a new run first.

**Debt is computable for seven of eighteen Verified rows, and the table above
works five of them (counted 2026-09-18, M657; the M658 re-runs add one computable
row and confirm the gap on another).** The other eleven — the **Pi
400's M451 full `check-target`** among them — record *green* without recording
*how much of the tier ran*, so their debt is not a large number, it is not a
number. `platform_retest_lint` check 2 asks that a row be **datable**, which is
strictly weaker than **debt-computable**, and the gap is invisible from the table
because a table can only show the rows that carry the figure. It matters in one
concrete way today: M451 is four months newer than M272 and cannot be weighed
against it, so the freshest aarch64 evidence sits in the blind spot while the
oldest sets the debt. **The fix costs nothing at the time and is unrecoverable
afterwards:** state the driver count in the row, on the next re-run of any row.

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

1. ~~**illumos**~~ — **RUN, M658–M662.** It was the cheapest remaining row and it proved it: one session from a cloud image, three defects in jichi and six in the rigs. Now *partly verified* and reproducible in one command (`scripts/tier-v-illumos.sh`); what remains is 19 smoke drivers, mostly the `grep -o` first-match-per-line difference. The original reasoning is kept because it is why this row was picked: it was the cheapest remaining row, and the
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

   **Checked on this machine 2026-09-18 (M657), because "blocked" is the kind of
   factual claim the M326b rule says to go and look at:** `/dev/kvm` exists, the
   account is in group `kvm`, `qemu-system-x86_64` and `qemu-img` are installed,
   24 threads report virtualisation extensions, and 137 GB is free. **This row is
   therefore not blocked on access** — which separates it sharply from macOS
   below. It costs a session, not a resource, and that is the whole difference
   between the two never-compiled rows.
2. **macOS** — the only never-compiled row with a known Darwin-specific code
   path (T2, M400). Blocked on hardware, not on work — and unlike illumos, no
   amount of willingness here changes that.

   Between the two sits a third category, which is neither: **the ARM bench
   rows**. The Pi Zero 2 W was re-run on 2026-09-18 at **302 drivers, debt 1**
   (§3's own table), superseding M272's debt of 205 — and on 2026-09-18 the Pi Zero, the Pi
   400 and the UNO Q all failed to answer, so re-running it is blocked on
   somebody switching a board on. Worth naming separately: not a resource gap,
   not a design question, just hardware that is off.
3. **Oracle Solaris 11.4** — needs an Oracle account, and its licence terms are
   the operator's decision rather than a technical one. A green *illumos* row
   would still leave Oracle Solaris unmeasured, and the matrix will say so.

A non-Linux row is a **defect detector, not compatibility work**: two OpenBSD
findings were bugs on every platform, and ten defects came out of FreeBSD and
OpenBSD between them with none of them wanting a BSD conditional.
