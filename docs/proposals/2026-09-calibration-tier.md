# Measure the machine before testing it: a calibration tier (design)

*Written 2026-09-21, from the M696 re-measurement of the Cygwin row and the numbers
`PLATFORMS.md` already carries for MSYS2, illumos and the Pi fleet. **Design only —
nothing in `src/` or `tests/` changes on this page.** It proposes replacing one
hand-chosen number with a measured vector, and it says plainly which of its own
figures are censored and therefore not yet usable.*

## 0. The proposal, in the operator's words

> *"We have to have jichi test run on different platforms, some of them
> high-performance workstations, some of them constrained. During development, and
> testing we have to take measurements. What do you recommend regarding another
> defined test run for such cases that measure a system before applying the standard
> tests?"*

The question contains the design. Today the tier is applied to a machine and the
machine's speed enters as an **operator-supplied constant**,
`JC_SMOKE_TIMEOUT_MULT`, conventionally derived from a build-time ratio. This page
argues that the constant is the wrong shape, that the ratio is the wrong proxy, and
that the fix is a bounded **Tier 0** which measures the host and hands the runner a
profile.

## 1. What is wrong today, stated as a measurement

The smoke tier's deadlines are compiled into `tests/smoke/run.sh` as two constants —
**60 s** for the large driver group, **120 s** for the wall-clock and multi-scenario
drivers — calibrated on the development bench. `JC_SMOKE_TIMEOUT_MULT` multiplies
both. Unset means ×1.

Those deadlines are not the defect and must not be removed. `run_driver`'s own
comment states the reason: *"a tight timeout is what turns a hang into a failure."*
This project has paid for that twice in one week — `posix_utils_lint` sat in
`pipe_read` for **15h47m** at M694, and a driver left running under a widened cap sat
for nine minutes here before it was stopped. A tier without deadlines does not report
a hang; it becomes one.

The defect is the **multiplier**: one scalar for a slowdown that is not scalar.

### 1.1 The proxy does not predict the thing

Measured on this bench, 2026-09-21, Cygwin 3.6.10 / gcc 14.4.0 against WSL2 / gcc
13.3.0 on the same hardware:

| what was measured | ratio to the bench |
|---|---|
| `make clean && make WERROR=1`, median of 3 (117 s vs 9.65 s) | **12.1×** |
| unit suite | ~7× |
| median smoke driver (10 s) | ~1× — needs no scaling at all |
| the file-scanning lints | **> 60×** — see §1.1a |

### 1.1a The ratio is not one number, and the spread is two orders of magnitude

Measured 2026-09-21 by running the twenty drivers Cygwin could not finish **as a
subset on both hosts**, with the same instrument — a driver's in-tier time and
its standalone time are different numbers, and mixing them would invent a ratio.
On the bench all twenty pass: `smoke: OK (20 drivers, 218 checks)`.

| driver | bench | Cygwin | ratio |
|---|---|---|---|
| `bool_dialect`, `assignment_guard_lint`, `docs_locators_lint`, `reading_refs_lint` | < 1 s | > 60 s | **> 60×** |
| `config_keys_lint`, `hint_ladder`, `i18n_tracks_lint`, `reading_quotes_lint` | 1 s | > 60 s | **> 60×** |
| `license_lint`, `subagent_tool_ad` | 2 s | > 60 s | > 30× |
| `smoke_lint` | 8 s | > 60 s | > 7.5× |
| `install_no_build` | 13 s | > 60 s | > 4.6× |
| `setup_keyfile` | 19 s | > 60 s | > 3× |
| `accessible` | **57 s** | > 60 s | **> 1.05×** |

The Cygwin column is a lower bound in every row — see §1.3 — but the *spread* is
already decisive: the same tier on the same host spans **> 1× to > 60×**
depending on the driver, while the build-time ratio that currently sets the
multiplier says **12.1×**. No single number is correct for that distribution, and
a number in the middle of it is wrong in both directions at once.

### 1.1b A driver at 95% of its deadline on the reference machine

`accessible` takes **57 s on the bench** against a **60 s** limit
(`run.sh` line 200, in the block ending `run_driver "$t" 60` on line 214). That
5% margin is the whole of the intermittent tier failure this project spent a
morning diagnosing as a resource-accumulation problem: in-tier it tips past 60 s
and is killed, while all 22 of its checks have already printed `ok`. Under a
widened multiplier it cleared and the failure simply moved to the next-tightest
driver.

It is recorded here rather than only in `ANECDOTES.md` because it is the
strongest argument on this page: **the defect was visible in data nobody was
keeping.** Pass/fail cannot distinguish 57-against-60 from 5-against-60 until the
day it fails, which is why §4 is the part to build first.

The MSYS2 row found the same split independently and by a different route:
`docs_flags` 24 s against under 1 s on the bench, the unit suite ~7×, `arena_lint`
modest. Its conclusion is the one this page builds on — **the penalty tracks how many
processes a driver spawns**, not how fast the machine is. A build-time ratio measures
compilation, which forks per translation unit; that is why it was chosen, and it is
still a proxy for a different distribution.

### 1.2 A single number is wrong in both directions

The Cygwin tier, run at the shipped deadlines with `JC_SMOKE_KEEP_GOING=1`
(`bac4c535`, 95m59s, 317 drivers):

| | |
|---|---|
| TAP checks passing | **1,598** |
| TAP `not ok` | **0** |
| driver-level failures | **20** |
| of those, bad TAP count | **0** |
| median driver | **10 s** |
| mean | 18.1 s |
| p90 | **29 s** |
| p99 | 131 s |
| drivers ≥ 30 s | 32 |
| drivers ≥ 60 s | 17 |

**Not one driver fails a check.** Cygwin finds no defect in jichi; twenty drivers run
out of time. The failures cluster at 126–132 s, which decodes exactly: a 60 s first
attempt, the M201 standalone retry at another 60 s, and ~11 s of overhead. That retry
doubles the cost of every timeout and accounts for roughly 44 of the 96 minutes.

Now size a single multiplier against that distribution. To clear the slow tail it must
be at least 3. Applied uniformly, every 10 s driver receives a 180 s deadline — **18
times its need** — so a genuine hang in any of 300 drivers takes three minutes to
announce itself rather than one. Size it for the median instead and the lints die. The
project's own rule names which error is worse: *"too large and a genuine regression
simply waits out its own timeout and passes. The second is worse."*

### 1.3 And the data a killed driver yields is censored

A driver killed at 60 s tells you it needed **more than 60 s**. It does not tell you
how much more. Twenty of the measurements above are right-censored at their limit, so
**this page cannot state Cygwin's true factors and does not**. That is the immediate
practical argument for everything below: the current mechanism produces exactly the
data that cannot calibrate it.

It is also how the existing numbers got their authority. The Cygwin row's **10** came
from 130 s ÷ 13 s with *both figures compile-inclusive*, so neither operand was a
runtime — recorded on the row itself, and reproduced by the author of this page, who
computed 13 the same way before being stopped (ANECDOTES #91, #92).


### 1.3a A killed driver costs TWICE its deadline, so raising the multiplier is not free

**Measured 2026-09-22 (M698), on the Cygwin row, from the rig's own stamps.** This
is the interaction nothing on this page anticipated, and it changes the arithmetic
of every proposal on it.

The tier's M201 idiom retries any failing driver **once, standalone**, to label the
outcome *"in-suite only"* versus *"also alone"*. That label is diagnostic gold for
a driver that **failed**. For one that was **killed**, the rig's own log shows what
the retry buys:

```
    23 --- smoke: smoke_lint
   809 smoke: retrying smoke_lint standalone to classify the failure...
  1594 smoke: smoke_lint was KILLED again (rc=137) -> it needs more than
  1594        780s on this host. No verdict about jichi follows
  1594        from either run; the checks below may be deadline artifacts.
  1595 --- smoke: snapshot_lint
```

One driver, **1572 seconds**, for the information *"it needs more than 780 s"* —
which the first kill had already established. At multiplier 1 the same driver cost
120 s for the same non-answer.

**Why this matters to §1.2's "wrong in both directions".** The obvious remedy for
censoring is a larger multiplier, and that page treats the cost as linear in the
deadline. It is linear only for drivers the larger deadline *rescues*. For a
driver that remains censored the cost is **2 × the new deadline**, so the
multiplier that was meant to buy durations instead buys a longer wait for the
same bound. On this row at multiplier 13 that is 26 minutes per stubborn driver,
and seventeen of them turn a 98-minute tier into an eight-hour one.

**The ceil boundary is fragile at exactly the wrong place, too.** Four clean
builds on this host measured 123, 125, 126 and 127 s against a 9.65 s reference.
The threshold between multiplier 13 and 14 sits at 125.45 s — *inside* the noise
of the measurement — so two runs of the same rig on the same machine an hour
apart chose 13 and 14. Nothing is wrong with either, but a multiplier is a
denominator, and one that flips on build jitter is not a stable one. A median of
three is the right instrument and it is still not enough here; §2.2's speed
vector should carry the spread, not just the centre.

**What follows for the design, as two candidate rules rather than one decision:**

1. **Do not retry a KILLED driver — only a FAILED one.** The retry exists to
   separate a defect from cross-driver interference, and a timeout is neither.
   It nearly halves the cost of a censored tier and loses only the confirmation
   that the driver is also slow alone. Against it: on a platform where the tier
   itself is the load, *"killed in suite, completed alone"* is a real and
   interesting outcome, and this rule would stop us ever seeing it.
2. **Retry a killed driver with a SHORTER deadline, not the same one.** If the
   question is only "is this censorship an artifact of the suite", a fraction of
   the deadline answers it at a fraction of the cost — and a driver that
   completes inside it has proved the interesting case outright.

Both are cheap. Neither should be chosen from this page: the retry's value on a
loaded platform is exactly the thing §6 says must be measured first, and it has
never been measured, because until this row nobody had paid the bill.

## 2. The design: Tier 0, in two halves

A bounded run, target **under 60 s on the bench**, executed before the standard tiers
and recorded with the row. It answers two different questions and must not blur them.

### 2.1 Half one — capability, pass or fail

Not speed. *Can this machine host the tier at all?* Each answer is a hard verdict with
a reason, and a failure here stops the tier rather than letting it fail obscurely 40
minutes later:

- is `$TMPDIR` writable, and what mode does a file created there actually get;
- can a pty be allocated — `openpty`, then `isatty` on the slave;
- the fd limit, RAM, and free space for `$TIER_V_DIR`;
- the utilities the drivers assume exist: `diff`, `cmp`, `patch`, `pgrep`.

This half is not speculative. Each item is a failure this project has already paid
for: MSYS2 ships without `diffutils`; `chmod` is a silent no-op under its `noacl`
mount, which broke jichi's file-privacy guarantees; `ptydrive` stopped compiling on
Cygwin at M683, so `make smoke-tools` failed and the **whole tier** was unrunnable
there for weeks with nothing able to say so; and `scripts/preflight.sh` prints *"tree
is quiet"* on a host with no `pgrep`, because `busy_pids` sends its errors to
`/dev/null` and reports zero — a gate that cannot fail, on the platforms being
surveyed.

### 2.2 Half two — speed, as a vector

Measure the costs the drivers actually incur, each as a median of N iterations, each
emitted with its absolute time *and* its ratio to a reference constant committed in
the tree:

| probe | predicts |
|---|---|
| `fork` + `exec` + `wait` of a trivial binary | the lints, and most of the tier |
| `openpty` + `fork` + `exec` + read + close | the `ptydrive` drivers |
| `stat` + `open`/`read` over M files in the tree | the file-scanning lints |
| a tight CPU loop | compute-bound work |
| loopback `connect` / `accept` | the `mockmodel` drivers |

Written as a C89 test-only tool beside `mockmodel` and `ptydrive`, so it adds no
dependency, needs no Python, and runs wherever the tier does.

## 3. Scale per class, not per tier

Each driver declares its class beside itself — a `# smoke-class: fork` line, so the
declaration lives with the thing it describes rather than in a table that drifts from
it. `run_driver` then computes `base × factor(class)` from the measured vector.

The lints take the fork factor. `accessible` and `typeahead_live` take the pty factor.
Everything else stays near its shipped deadline, so a hang still surfaces in seconds
on the 300 drivers that are not the problem.

`JC_SMOKE_TIMEOUT_MULT` stays, as an override for a host where Tier 0 cannot run and
for reproducing a historical row. The calibrated path becomes the default, and the row
records the **vector**, not a hand-chosen scalar.

## 4. Do this part first, whatever happens to the rest

**Have the runner record every driver's duration on every run.** `wd` already wraps
each driver, so it can emit one line per driver:

```
driver,seconds,limit,censored
```

`censored=1` when the driver was killed at its limit, because that is the distinction
this page exists to make: a killed driver's time is a **bound**, not a measurement,
and recording it as a number is how a bound becomes a fact.

It is small, it is independent of everything above, and it pays immediately. Every
platform row arrives with a duration profile instead of a single total. Driver-runtime
regressions become visible. Any calibration can be checked against reality afterwards
rather than trusted. And the censoring that blocks §1.3 announces itself, instead of
being hand-rolled with `awk` and `systime()` by whoever next notices.

## 5. Guardrails, each one from an incident

1. **Every probe floors, and fails loudly.** A calibration that silently yields 1 is
   the `bc`-missing incident exactly: a failed timing produced `secs=?`, the string
   sorted above `"0"` so the guard passed, `"?"/ref` evaluated to 0, and the clamp
   turned it into **multiplier 1 on the slowest board in the fleet**. Validate numeric
   first, and refuse rather than default.
2. **Never tighten below the shipped deadline** by default. A fast workstation
   manufacturing new flakes is a worse trade than a slightly loose bound; make
   tightening opt-in for whoever wants it as a regression detector.
3. **The row records the vector and its date.** `PLATFORM_RETEST.md` gains a trigger
   when the vector drifts, so a row goes stale on evidence rather than on a calendar.
4. **A cross-tier factor is not transferable.** M507 measured this on the phone: the
   unit suite is storage-bound, the smoke tier is fork-bound, and the unit suite's
   ratio of 5 under-predicted the smoke tier's by more than an order of magnitude. The
   vector must be consumed per class or not at all.
5. **The instrument must not be part of what it measures.** A `ps`-sampling loop was
   run beside the Cygwin tier to watch its progress, on the platform whose defining
   characteristic is a fork penalty. One long-lived process stamping the run's own
   output replaced it.

## 6. What must be measured before this is built

This page proposes a mechanism; these are the numbers it needs and does not have.

- **The true runtimes of the 20 censored Cygwin drivers.** Re-run only those, with a
  deadline set high enough that it *cannot* fire — not to make a failure disappear,
  but to remove the censor — and record the durations. Without this there is no
  evidence for any fork factor, including the one this page would otherwise assume.
- **Whether the class split is real or an artefact of the run order.** The slow set is
  contiguous near the front of `run.sh`'s list. The fork hypothesis predicts it; so
  would a warm-up effect. Shuffling the order once settles it, and is cheap.
- **The probes' own cost and stability** on the slowest target in the fleet. A Tier 0
  that takes four minutes on a Pi Zero to save six is not worth having, and one whose
  medians move between runs is worse than no calibration at all.
- **Whether `setup_keyfile` belongs to this family.** It failed at **68 s** in the
  tier, which does not fit the 60 + 60 + overhead shape the other nineteen share,
  and it takes **19 s** on the bench — the third-slowest of the twenty. Still
  unexplained, and recorded as such rather than folded into the pattern.

## 7. Implementation plan

Staged, each stage useful alone and none depending on the next being built.

1. **Duration recording** (§4). `wd` emits `driver,seconds,limit,censored`; the tier
   writes it to `$TIER_V_DIR`. No behaviour change, no new tool.
2. **Capability probe** (§2.1) as a shell driver, reusing `tests/tools/` where it can.
   Refuses the tier with a named reason instead of failing obscurely later.
3. **The measured 20** (§6, item 1) — the censored re-run, which produces the first
   real fork factor.
4. **`tests/tools/calibrate.c`** (§2.2), validated against the stage-1 recordings on
   at least three rows of different speed: this bench, Cygwin, and one constrained
   board.
5. **Per-class deadlines** (§3), behind an opt-in until a row has been run both ways
   and the failure sets compared.

## 8. What this must not claim

- **It is not a speedup.** Nothing here makes Cygwin faster; it makes the deadlines
  honest about what Cygwin is.
- **It does not remove deadlines.** §1 is explicit that they are load-bearing.
- **It does not turn the 20 failures green.** They are failures until their true
  runtimes are measured and either the deadline or the driver changes; a calibrated
  deadline that happens to clear them is a *result*, and must be reported as the
  measurement it came from rather than as a pass.
- **It cannot predict a row it has not run on.** The vector is a property of a host,
  measured on that host, on a date.

## 9. Questions, and they are real

- **Is the class declaration worth it, or is a two-tier split enough?** `run.sh`
  already has 60 s and 120 s groups. Perhaps the honest minimum is a third group —
  fork-heavy — and no per-driver annotation at all. That is less precise and much
  harder to get wrong.
- **Should Tier 0 run automatically, or only on a new row?** Running it every time
  costs seconds on every gate and keeps the vector current; running it only when the
  operator asks keeps `make ci` unchanged and risks a stale vector nobody notices.
- **Where does the reference constant live, and who re-measures it?** The vector's
  ratios need a denominator committed in the tree, and this project has already been
  bitten by rows that divide by two different benches (4.00 s and 6.19 s) without
  saying which.
- **Does any of this belong in the product rather than the tier?** `jichi doctor`
  already answers capability questions at runtime and could report "this host is ~12×
  the reference for process creation". That is a user-facing claim, and this page does
  not assume it is wanted.
