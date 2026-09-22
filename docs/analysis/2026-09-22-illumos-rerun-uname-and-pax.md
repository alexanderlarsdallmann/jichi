# The illumos row, re-run: two defects, four boots, and the first green

*2026-09-22. `scripts/tier-v-illumos.sh` against OmniOS CE r151058 under KVM,
four times. The row went `18 ok / 1 failed` → `19 ok / 0 failed`, and the two
things that stood between were a portability defect in `src/` and a test-tooling
defect at a site a previous milestone had fixed one call away from.*

## Why it was re-run at all

The row was last measured at **M683 (2026-09-20 15:45)**. Thirty-nine commits
landed on `master` afterwards, and `docs/PLATFORM_RETEST.md`'s two halves
disagreed about what that meant:

- **Coverage debt said the row was current.** 317 drivers in the tree minus the
  311 the row ran is **6**, well inside the page's `< 25` "cite it in the present
  tense" threshold.
- **Five of its seven triggers had fired**, three of them naming this row's exact
  axis: **T2** (M695 added a `uname` branch whose table literally contains
  `"SunOS"`), **T3** (an unconditional `tcflush` added to `enter_raw` for Cygwin,
  on the one row whose `/bin/sh` is ksh93), **T6** (six new drivers), **T7** (the
  tier runner's kill semantics changed), and **T1** (a new capability probe in the
  `Makefile`, whose scope is "every non-glibc row").

**The triggers were right and the debt number was not.** Debt counts drivers that
never ran; it cannot see a driver that runs and now asserts something new. Worth
recording on that page: a row can be at debt 6 and still be two defects stale.

## Defect 1 — `uname()` succeeds with any non-negative value

POSIX: *"upon successful completion, a non-negative value shall be returned."* It
does not say zero. Linux, FreeBSD, NetBSD, OpenBSD, Cygwin and MSYS2 all return
0, so four call sites written as `uname(&u) == 0` were correct on every row this
project had ever run. **illumos returns a positive value**, and all four took the
failure path on a system where the call had worked.

What it cost was not cosmetic:

| | before | after |
|---|---|---|
| `jc_platform_describe()` | returns 0 | fills the buffer |
| `doctor`'s platform line | `! host platform not recognised` | `! SunOS 5.11 (i86pc)` |
| `jc_platform_row_verdict()` | `UNKNOWN` | `PARTLY` |
| `tests/smoke/doctor.sh` | **fails** | passes |

So **M695's entire platform-verdict feature was dead on illumos.** That milestone
shipped `jc_sys_partly[] = { "SunOS", "CYGWIN_NT-", "MSYS_NT-" }` and the `SunOS`
entry could not be reached: `doctor` told an illumos user jichi had never been
compiled there, while `PLATFORMS.md` partly-verifies the row.

`doctor.sh` **failed correctly** — it derives the expected tier from
`PLATFORMS.md`, read `partly` for SunOS, and found `partly=0 never-compiled=0` in
doctor's output. Two of M695's three rows are Windows layers and were re-measured
by another machine this week. This was the third, and only running it could show
the entry was dead.

The fix is `< 0` for the error test and `>= 0` for the success test, at all four
sites. **Identical behaviour on every platform already measured, which is exactly
why nothing caught it** — and why `portability_lint` check 20 now refuses a
`uname()` result compared against zero, floored at today's four call sites so an
empty extraction cannot pass.

## Defect 2 — `pax_global_header`, at the site M683's fix could not reach

M683 found that `git archive <commit>` writes a **pax global header** — a tar
entry named `pax_global_header` with typeflag `g` — and that illumos
`/usr/bin/tar`, which does not recognise that typeflag, writes it out as a
**regular file**. It fixed `scripts/make-snapshot.sh` and reported the tier green.

It was not green. The last failing driver here was:

```
not ok 16 - unexpected file(s) at the root of the publishable tree: pax_global_header
```

**The second site is the one every VM rig takes.** `jc_rig_ship_tar` in
`scripts/_rig_ship.sh` — written at M466 precisely so a ship fix lands once —
used the commit form. So the stray file arrived at the root of the *shipped*
tree; the rig then made that tree a git repo with `git add -A`, which **tracked**
it; and `make-snapshot.sh`, correctly archiving a tree, faithfully published a
file that should never have existed.

Measured on this tree, both forms:

```
git archive HEAD          | first tar entry: pax_global_header
git archive 'HEAD^{tree}' | first tar entry: .gitattributes
entry count, either form  : 2354
```

Identical content; one carries the header. **Through GNU tar both report zero pax
entries, because GNU tar consumes it silently** — which is why no amount of
testing on this bench could ever have shown it.

### A discrepancy this did not resolve

M683 reports `smoke: OK (311 drivers, 1,801 checks)` on this kernel, i.e.
`snapshot_lint` passing. Tonight it failed on `pax_global_header`, reproducibly,
through the same **clean** ship path (`tree: HEAD <sha> (clean archive)` in both
results files, so `--dirty` is not the explanation), with
`scripts/_rig_ship.sh`, `scripts/make-snapshot.sh` and
`tests/smoke/snapshot_lint.sh` **all unchanged between the two runs**, and with
the guest's git-repo step predating M683 by a month. The run on disk from earlier
that same day (2026-09-20 10:57, also a clean archive) *also* had `snapshot_lint`
failing.

**I could not reconcile those, and am not going to guess.** What is established
is that the defect existed at M683 and before it, that the fix at
`make-snapshot.sh` could not reach it, and that from tonight the row is green
with its count recorded. The question is now only historical, which is the only
reason it is left open.

**Six other call sites still use the commit form** (`tier-v-netbsd`,
`tier-v-openbsd`, `tier-b-device`, `fleet-run`, and the two `jhub` scripts).
Every one targets a platform whose tar consumes the header, so none is known to
be broken, and changing a working rig that cannot be run tonight to satisfy a
rule is the trade `_rig_live.sh`'s own header argues against. They are named here
instead of edited blind.

## Two gaps in the rig, both the same shape as M665

M665's rule: *a rig that fails must still report its denominator — the count is
free at the time and unrecoverable afterwards.* Both gaps are that rule applied
one step further, and both cost a boot:

1. **The failing CHECKS were not captured, only the driver names.** Run 1 came
   back `2 driver(s): snapshot_lint doctor` and nothing else. `doctor` was
   diagnosable only because its cause happened to be visible in the
   offline-surfaces output three checks earlier; `snapshot_lint` was not
   diagnosable at all, because the guest was already destroyed and its
   `/tmp/smoke.log` with it.
2. **The count was recorded on failure and not on success.** This had survived
   unnoticed because nothing had ever exercised it: **until tonight this row had
   never passed.** Run 3 was green and the results file said `smoke tier: OK`
   with no numbers — and a green row is cited for exactly one number.

Both are fixed. Run 4 exists only because of the second one.

## The row, as it now stands

```
SunOS tier-v-illumos 5.11 omnios-r151058-516f7694c9 i86pc i386 i86pc
ok - WERROR=1 build clean on illumos (10s)     JC_SMOKE_TIMEOUT_MULT = ceil(10/7.44) = 2
ok - unit suite: 0 failures                    13,458 checks
ok - smoke tier: OK -- smoke: OK (317 drivers, 1839 checks)
ok - doctor ran and printed its summary -- 23 ok, 8 warnings, 0 problems
ok - live turn answered over the reverse tunnel (google/gemma-4-12b)
ok - agentic turn: the model called a tool and reported TIER-V-2B095E
== totals: 19 ok, 0 failed
```

**Coverage debt: 0** — 317 of 317. Against the bench's `317 drivers, 1,852
checks` the gap is **13 checks**, which is platform skips and not failures.

The multiplier denominator was **re-measured**, not copied: three serial clean
`make WERROR=1` builds on this bench gave 7.47 / 7.44 / 7.34, median **7.44 s**,
against the 6.19 s a row recorded earlier. `JC_SMOKE_TIMEOUT_MULT` is a ratio,
and this branch's `Makefile` added a configure-time compile to every `make`.

## What is still NOT claimed

- **Still `Partly verified`.** `make ci` has never run on this kernel, and the
  verdict names the gate, not the tier.
- **That illumos returns exactly 1 from `uname()`** is not measured. What is
  measured is that it returns something non-zero on success, twice, reproducibly.
  POSIX licenses any non-negative value and the fix is correct for all of them; a
  one-line probe on the next boot would pin the number.
- **That the six remaining `git archive <commit>` sites are harmless.** They are
  *believed* harmless because their targets' tar consumes the header. Nobody has
  run them since this was understood.
- **Generality of the model result.** One model (`google/gemma-4-12b`, local LM
  Studio over the reverse tunnel), classified `native` by `doctor --live` on the
  bench before the rig was started. The row says the loop closed here; it does
  not say every model closes it here.
