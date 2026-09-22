# The Cygwin row, re-measured — and what the deadlines were hiding

**Machine:** HRZNB-U020390 — Windows 11 Pro 26200, i7-1265U (12 threads), 15.7 GB
**Tree:** `bac4c535` · **Date:** 2026-09-21
**Row:** Windows + Cygwin (`docs/PLATFORMS.md`, *Partly verified*)
**Status:** the offline half is measured; **the row is NOT updated by this page** —
see §8.

*This page reports measurements. It deliberately does not restate the row, because
the row's re-measurement is incomplete: MSYS2 has not been re-run and neither
platform has been driven with a model.*

---

## 1. Why the row was re-run

`PLATFORM_RETEST.md` §3 puts the staleness line at a coverage debt of **100**
drivers, past which a row is "a historical datum, needs a new run before any claim
rests on it". Cygwin was measured at M477 against **209** drivers; the tree now
holds **317**. Debt **108**, crossed with nothing saying so, because the row does
not appear in that section's debt table.

## 2. The row was not reproducible at all, and that came first

`ptydrive` had not compiled on Cygwin since **M683**, whose illumos pty fix
introduced `ioctl(FIONREAD)`. FIONREAD is not a POSIX ioctl, so each libc files it
elsewhere; M683 added `<sys/filio.h>` behind `JC_HAVE_STREAMS_PTY` for illumos and
nothing for anyone else. Measured here, one header per probe, with the build's own
flags:

| header | `FIONREAD` under `-D_XOPEN_SOURCE=600` |
|---|---|
| `sys/ioctl.h` | no — and still no without the feature macro |
| `sys/filio.h` | no |
| **`sys/socket.h`** | **yes** |

`ptydrive` is what the tier drives a terminal with, so `make smoke-tools` failed and
the **entire tier** was unrunnable — not one driver, all of them. The row's "smoke
209 drivers / 1,081 checks" has therefore been unreproducible for weeks, and nothing
could say so, because no gate builds this tooling anywhere except the platform where
it already works. Fixed at `1587d3ab`.

## 3. The build

| | |
|---|---|
| Cygwin / compiler | 3.6.10 (`CYGWIN_NT-10.0-26200`), gcc 14.4.0, GNU Make 4.4.1 |
| `make clean && make WERROR=1`, median of 3 | **117 s** (117 / 117 / 123), rc=0, **0 warnings** |
| bench reference, same day, median of 3 | **9.65 s** (10.96 / 9.65 / 9.46) |
| build ratio | **12.1×** |
| unit suite | **13,438 checks, 0 failures** |

`make info` reports the full feature set — `c89 (strict)`, `HAVE_CURL`,
`CLOCK_GETTIME = in libc`, `WINSIZE = visible under strict POSIX`,
`SOCKET_LIBS = in libc`, `STREAMS_PTY = no`, `HARDEN = 1`. Nothing is compiled out,
which is what M476 had to fix before this row meant anything.

## 4. The tier at shipped deadlines

`JC_SMOKE_KEEP_GOING=1 make smoke`, **no multiplier**, 95m59s:

| | |
|---|---|
| drivers | **317** |
| driver-level failures | **20** |
| bad TAP count | 0 |
| drivers whose checks failed | **6** (§6) |
| median driver | **10 s** · mean 18.1 · p90 **29 s** · p99 131 · max 132 |
| drivers ≥ 30 s / ≥ 60 s | 32 / 17 |

The failures cluster at 126–132 s, which decodes as a 60 s first attempt plus the
M201 standalone retry at another 60 s plus ~11 s overhead — the retry doubling the
cost of every timeout, and accounting for roughly 44 of the 96 minutes.

**No TAP totals are quoted, and the reason is a correction.** An earlier revision
of this page reported *"TAP checks passing 1,598, TAP `not ok` **0**"* and drew a
conclusion from the zero. Both numbers were wrong, and the second was wrong in the
direction that mattered — see §4a. Nor is there an honest replacement: when a
driver fails, `run.sh` prints its output **twice**, once in-suite and once from the
M201 retry, so every count over a failing run double-counts precisely the drivers
under discussion. The tier states an authoritative check count only when it
passes. What this page reports instead is driver counts and the **distinct**
failing checks, named.

### 4a. The instrument was blind to what it was measuring

The run's output is timestamped by one `awk`, so every line is
`<epoch>\t<text>`. Counting TAP results with a grep anchored to that tab —
`grep -c '\tnot ok '` — is correct for a **passing** driver and wrong for a
failing one, because `run.sh` indents a failed driver's captured output with
`    | ` before printing it. The line is `<epoch>\t    | not ok 7 - …`, and the
anchored pattern does not match it.

So the count excluded exactly the drivers it was being used to investigate, and
returned **0**. Stripping both prefixes first gives **12** raw `not ok` lines in
this run — **6 distinct**, the rest being the retry's duplicate copy.

This is `TEST_INTEGRITY.md`'s "audit the universe, not the result" with the page's
author on the wrong side of it, and the tell was available: a tier reporting
twenty failed drivers and zero failed checks is describing either twenty timeouts
or a broken counter, and only one of those was checked. The conclusion drawn from
the zero — *"Not one driver fails a check. Cygwin finds no defect in jichi"* — is
withdrawn in §6.

## 5. The censored re-run, and the ratios

Twenty drivers were killed at their limit, so each yielded a **bound** — "needed more
than 60 s" — and no duration. The deadline was therefore widened to 1200 s **so that
it could not fire**, in order to remove the censor and observe the true runtime. That
is the opposite of choosing a multiplier to make a failing run report green, and the
distinction is intent: a number set so nothing truncates, whose output is the
durations themselves.

Bench figures come from running the same twenty **as a subset on the bench**, with
the same instrument — a driver's in-tier and standalone times are different numbers,
and mixing them would invent a ratio. On the bench all twenty pass:
`smoke: OK (20 drivers, 218 checks)`.

| driver | bench | Cygwin | ratio | outcome |
|---|---|---|---|---|
| `snapshot_lint` | 5 s | **> 1200 s** (2793 s incl. retry) | **> 240×** | killed twice |
| `posix_utils_lint` | 4 s | **1218 s** | **~300×** | passed |
| `license_lint` | 2 s | 565 s | **283×** | passed |
| `config_keys_lint` | 1 s | 223 s | 223× | passed |
| `docs_locators_lint` | < 1 s | 149 s | > 149× | passed |
| `i18n_tracks_lint` | 1 s | 146 s | 146× | passed |
| `hint_ladder` | 1 s | 107 s | 107× | passed |
| `cppcheck_lint` | 4 s | 102 s | 26× | passed |
| `reading_trace` | 4 s | 97 s | 24× | passed |
| `reading_refs_lint` | < 1 s | 87 s | > 87× | passed |
| `reading_quotes_lint` | 1 s | 85 s | 85× | passed |
| `assignment_guard_lint` | < 1 s | 69 s | > 69× | passed |
| `smoke_lint` | 8 s | 1072 s | **134×** | passed |
| `install_no_build` | 13 s | 93 s | 7× | passed |
| `subagent_tool_ad` | 2 s | 64 s | 32× | passed |
| `setup_keyfile` | 19 s | 68 s | — | **FAILED a check** |
| `bool_dialect` | < 1 s | 26 s | — | **FAILED a check** |
| `daemon_auth` | 3 s | 19 s | — | **FAILED a check** |
| `preprompt_discard` | 4 s | 16 s | — | **FAILED a check** |
| `accessible` | **57 s** | **84 s** | **1.5×** | passed |

**The spread is the finding, and the last row explains it.** The same tier on the
same host ranges from **1.5×** to **~300×** — two orders of magnitude — while the
build-time ratio that conventionally sets the multiplier says **12.1×**.

`accessible` is the control the rest of the table needed. It is the slowest driver
of the twenty on the bench (57 s) and the *fastest* relative to it on Cygwin
(1.5×), because it is **wall-clock and pty bound**: it waits on timers and on a
model that answers at its own speed, and waiting costs the same everywhere. The
lints at 100–300× spawn a process per file, and process creation is what this
platform charges for. The ratio is not a property of the host; it is a property of
**what a driver spends its time doing**.

That is the calibration proposal's central claim, arrived at by measurement rather
than argument: a single multiplier cannot be right, because there is no single
number to be right about. A vector, consumed per driver class, is the shape the
data has.

**And it re-reads `accessible`'s own failure.** At 84 s against a 60 s limit, it
fails here — but not because Cygwin is slow. A 1.5× host is enough to tip a driver
that already sits at 95% of its deadline on the bench (§7). The Cygwin failure and
the bench flake are the same defect, not two.

### 5a. An instrument note, because it was wrong first

The `censored` column in the raw CSV was computed as `duration >= 1195 s`. That is a
**crude proxy and it mislabels**: `posix_utils_lint` ran 1218 s and *passed*, the
excess being runner overhead outside the `timeout` wrapper. Censoring is derived here
from the exit status and the log, not from the duration. The CSV's column is left as
recorded rather than silently corrected.

## 6. The deadlines were concealing real failures

An earlier revision of this page read the counter's zero as a result and wrote
*"Not one driver fails a check. Cygwin finds no defect in jichi; twenty drivers run
out of time."* **That is withdrawn.** The counter was blind (§4a). Counted
properly, this run has **six** drivers emitting failing checks:

| driver | failing check | now |
|---|---|---|
| `preprompt_discard` 3 | *"type-ahead from before the prompt was sent to the model — the safety property the flush exists for is gone"* | **fixed** — `TCSAFLUSH` does not discard on Cygwin |
| `bool_dialect` 4 | *"readonly: `true` did NOT fence the profile"* — while `1`, `yes`, `True` do | **fixed** — a fixture that collapsed on a case-insensitive filesystem |
| `daemon_auth` 7 | *"over-long line got: "* — empty | **fixed** — a close-with-unread-data race the driver depended on |
| `setup_keyfile` 27 | *"doctor says nothing about the platform"* | **fixed** — a regression this band shipped (§6a) |
| `cppcheck_lint` 1 | *"the generated-header universe changed or the extraction broke."* | **artifact** — §6b |
| `install_no_build` 8 | *"the dirty refusal does not name: `make -j4` …"* | **artifact** — §6b |

The last two were invisible until the counter was fixed. Both are artifacts of the
deadline, not findings, and so is `accessible` 19 from the re-run — §6b.

**The deadlines still conceal, and that half stands.** Four of the six were killed
partway: a driver killed after printing some of its plan contributes its failures
to no total anyone reads, and in the tier's own summary a killed driver and a clean
driver are indistinguishable. So a platform slow enough to trip the deadlines does
not merely manufacture false failures — it suppresses true ones. What changed is
that this page originally reached that conclusion through a broken count, and it is
restated here on one that was checked.

### 6b. A killed driver does not stop — it fabricates findings

The three that survived diagnosis were not defects. They are what a **deadline
kill** looks like from the outside, and the mechanism matters because it cost this
investigation most of an afternoon.

`timeout` TERMs the process **group**. A `$(...)` command substitution in flight
dies and yields an **empty string** — and the driver shell, which defers the signal
while waiting on that foreground child, then resumes, compares against the empty
value, and prints `not ok` lines **accusing the product** before it dies. `wd`'s own
header documents that deferral for the case where a driver absorbs a TERM; what it
does not say is that the same deferral lets the shell keep going and publish
conclusions drawn from values the kill erased.

Reproduced deliberately, and byte for byte:

```
$ timeout -k 5 25 sh tests/smoke/cppcheck_lint.sh
not ok 1 - the generated-header universe changed or the extraction broke.
  by Makefile variable: include/jc_buildrev_stamp.h
  by untracked include:  none
```

identical to what a 96-minute tier reported. `accessible` under the same treatment
is blunter still — `not ok 1 - rc default=0 accessible=143`, where **143 is
128+15: the SIGTERM itself**, presented as a product defect. Every one of the three
failure texts is a captured value that came back empty: *"by untracked include:
none"*, *"accessible prompt wrong: []"*, *"the dirty refusal does not name …"*.

**And the "in-suite-only" signature was never about the suite.** These drivers
appeared to pass standalone and fail in the tier, which reads as a cross-driver
effect. It is not. `run.sh` gives a **named subset 120 s** and the full tier **60 s**
for this group — deliberately, and the code says why:

```sh
# The wider of the two bounds, deliberately: a subset run cannot know which
# list a driver came from ...
run_driver "$t" 120 || driver_failed "$t"
```

`cppcheck_lint` takes **95–98 s**, `accessible` **84 s** on Cygwin,
`install_no_build` similar. All three sit **between the two limits**. So "run it
standalone to check" was a *more generous test*, not an isolated one, and the
difference it measured was the deadline — not the presence of other drivers.

That distinction matters for reading the tier's own retry too. `run.sh`'s M201
retry uses the **same** limit as the in-suite run, so its "PASSES standalone" is a
real signal about repetition. A human re-running `run.sh <name>` by hand is doing
something else entirely, and will disagree with the tier for a reason that has
nothing to do with what either was testing.

**Fixed in the runner**, because the cheapest correction is to stop the tier
making a claim it cannot support. `run.sh` now distinguishes a killed driver from
a failed one, says the output is not evidence, and no longer reports *"ALSO fails
standalone -> a real defect"* for a driver that merely ran out of time twice:

```
smoke: cppcheck_lint KILLED at its 60s deadline (rc=124)
smoke:   The output below is NOT EVIDENCE about jichi. A killed shell continues
smoke:   past a command substitution the kill emptied ...
smoke: cppcheck_lint was KILLED again (rc=124) -> it needs more than 60s on this host.
```

Proved by making a driver outlive its deadline and watching the banner appear, and
disappear on restore.

### 6a. `setup_keyfile` 27 was M695 regressing `doctor`, and this page got it wrong twice

The check is four lines:

```sh
plat=$(cd "$ws2" && with_deadline 30 "$BIN" doctor < /dev/null 2>&1 \
       | grep -c "platform")
if [ "$plat" -ge 1 ]; then
    t_ok "doctor reports the platform"
else
    t_fail "doctor says nothing about the platform"
```

It asserts that `doctor`'s output contains the lowercase word **platform** at least
once. That is platform-neutral and entirely reasonable.

**M695 broke it.** Before M695, `doctor` on Cygwin printed *"jichi has never been
compiled on this **platform**"* and *"**platform** is not Linux"* — two matches.
M695 replaced both with one line reading *"jichi is PARTLY verified **here**
(docs/**PLATFORMS**.md)"*, which contains the word only in capitals. Measured on
the captured output: **0** case-sensitive matches, against 1 for a case-insensitive
grep. The count went 2 → 0 and the check went red.

**It affects every Partly verified row** — Cygwin, MSYS2 **and illumos** — because
the partly branch is the one that lost the word, and none of those platforms is
reached by any gate.

**Two errors in reading it, both worth recording.** This page first stated that
check 27 *"is only true on a Verified platform"* and *"encodes this host is Linux"*.
Both are false, and they came from diagnosing the check **by its failure message**
— *"doctor says nothing about the platform"* — instead of by its code. The message
describes the failure; the assertion is its inverse. `DEFERRED.md`'s opening rule
covers exactly this: check the checkable part of a reason before writing it down.
The second error followed from the first: having decided the driver was wrong, the
page recorded whether it predated M695 as *"untested and not claimed"*, when one
`grep -c` against an output file already sitting on disk settled it.

**Fixed** by restoring the word — *"jichi is PARTLY verified **on this platform**
(docs/PLATFORMS.md)"*, which also reads better — and verified on Cygwin:
`ok 27 - doctor reports the platform`.

**And linted, because the shape of this defect is the point.** `doctor`'s platform
verdict has three branches and any host executes exactly one. The bench is
Verified, so `make ci` here can only ever see that branch; the partly and
never-compiled wordings are, from this machine, unreachable text no gate reads.
`portability_lint` check 7d now asserts in the **source** that all three name the
platform in lowercase, where all three are visible at once regardless of which one
the host would run. Proved red per branch: rewording the partly message reports
`verified=1 partly=0 never=1`; rewording the never-compiled message reports
`verified=1 partly=1 never=0`.

**The remaining three are not diagnosed, and this page does not guess.** `preprompt_discard`
is the one to look at first: it asserts a **safety property** — type-ahead typed
before the prompt must not reach the model — and it is also the driver M683 rewrote
for illumos pty semantics, so Cygwin may differ again in its own way. `bool_dialect`'s
pattern is peculiar in a way worth understanding: three spellings fence and the most
obvious one does not, which touches the lenient-boolean invariant directly.

## 7. `accessible` runs at 95% of its deadline — on the bench

Measured in the subset run: **57 s**, against standalone timings of 53.9 s and 57.5 s
the same day, and a **60 s** limit (`run.sh` line 200, in the block ending
`run_driver "$t" 60` on line 214).

That 5% margin is the whole of the intermittent tier failure diagnosed at length in
ANECDOTES #91 as resource accumulation. In-tier it tips past 60 s, `timeout` sends
SIGTERM, and a bare `Terminated` appears while all 22 checks have printed `ok`. Under
a widened multiplier it cleared and the failure moved to `typeahead_live` — the
next-tightest driver, not a different bug.

It is a **decision, not a patch** (`DEFERRED.md`): raising the limit is a cap change
on a driver that already fails in a way no check reports.

## 7a. The tier re-run, after the four fixes

Same instrument, same tree discipline, at `aa87bce8` — no multiplier,
`JC_SMOKE_KEEP_GOING=1`, one `awk` stamping the run's own output. The expectation
was written into the script header *before* the run, so it could be wrong:
*"~17 kills should remain and TAP not-ok should stay 0."*

| | first run (`bac4c535`) | re-run (`aa87bce8`) |
|---|---|---|
| drivers | 317 | 317 |
| unit suite | 13,438 / 0 | **13,438 / 0** |
| driver-level failures | 20 | **17** |
| drivers with failing checks | 6 | **4** |
| wall | 95m59s | **98m35s** |

The count half of the prediction held exactly. The second half did not, and for
two separate reasons.

**The four fixed drivers are gone** — `preprompt_discard`, `bool_dialect`,
`daemon_auth` and `setup_keyfile` all pass. **`doctor` is new**, and it was this
band's own doing: the `setup_keyfile` fix reworded doctor's partly-verified
sentence from *"PARTLY verified here"* to *"PARTLY verified on this platform"*,
and `doctor.sh` was grepping for the old tail. One sentence, two consumers, broken
in opposite directions within an hour, neither visible on a Verified bench.
`portability_lint` check 7f now pins the coupling rather than either side.

**`accessible` 19 is newly visible** — *"accessible prompt wrong: []"* — not new
behaviour but a check the first run never reached before the driver was killed.
It joins `cppcheck_lint` 1 and `install_no_build` 8 as found and not diagnosed.

So the honest tally after both runs: of the seven distinct check failures this
platform has surfaced, **four were real and are fixed** (one product defect, one
fixture, one driver dependency on socket teardown, one regression of ours), and
**three remain undiagnosed** — `cppcheck_lint` 1, `install_no_build` 8,
`accessible` 19.

## 7b. MSYS2, measured twice — and a documented fix that protects the tests, not the user

Measured 2026-09-22 at `1f5621ba`, in **both** configurations, because a row
measured only in the configured state is true for nobody.

| | stock (`noacl`, `MSYS` unset) | configured (`acl`) |
|---|---|---|
| build, median of 3 | **115 s** | — |
| unit suite | **13,428 / 4** | — |
| tier | **317 drivers, 14 killed, 9 failed** | **8 of the 9 clear** |

**All nine stock failures have one cause, and none is a defect.** `chmod` silently
does nothing, so the daemon **refuses to start** rather than expose a socket that
runs shell commands — `d.sock is group/other bits set -- must be 0600` — and the
API key file and audit log land at `-rw-r--r--`. Fences firing correctly on a
filesystem that cannot honour modes.

The ninth, `pathfence_dangling`, was not about the mount: **MSYS2 cannot create a
dangling symlink at all**, `ln -s` to a non-existent target failing with ENOENT
under every symlink setting. The driver was reporting *"no refusal in the tool
result"* — a hole in the **path fence** — beside a control check admitting the
fixture had failed. It now skips with the reason.

**The documented fix is partial, and that is the finding.** The MSYS2 row records
adding `C:/msys64/tmp /tmp ntfs binary,acl` and reports the privacy failures
clearing. True — because the tier's isolated HOME lives under `/tmp`. A real
user's `~/.jichi.env` is under `/home`, still `noacl`, still world-readable:

| fstab line | user's home file after `chmod 600` |
|---|---|
| `C:/msys64/tmp /tmp … acl` (documented) | **644** |
| `C:/msys64 / … acl` | **644** — the installation root is handled specially |
| **`C:/msys64/home /home … acl`** | **600** |

**`doctor` is right in both cases**, because it probes the actual state root: it
warns *"private files are NOT private on this filesystem"*, naming the mount, the
affected files and the fix, and says *"private files really are private"* once the
mount honours modes. That is M503's probe meeting a real `noacl` mount for the
first time — until now it could only be proved with a `JC_FAULT_CHMOD` injection
site, because there is no such mount on the bench.

## 7c. Both rows are Driven

Two turns each, following `scripts/_rig_live.sh`: a wire turn, then a loop turn
whose pass phrase is a **nonce minted that second** and written into a file the
model can only reach by calling `read_file`.

| row | wire | agentic | phrase |
|---|---|---|---|
| Windows + Cygwin | 2 s | **3 s** | `M697-cygwin-3FF107` |
| Windows + MSYS2 | 1 s | **2 s** | `M697-msys2-63B3BA` |

`jlu/qwen3.8-27b`, checked against the free-namespace listing before the first
request (8 of 376 ids), with the key supplied through `apiKeyEnv` so it stays out
of the config file and out of `ps`.

**Transport: the HRZ gateway over TLS, not the loopback tunnel** every other
Driven row used — LM Studio is not installed on this machine and the JLU instance
is unreachable from it. Both rows say so. A row that implies a transport it did
not use is worth less than no row.

## 8. What this page does not claim

- **The row is not updated.** The offline half is measured; MSYS2 is not re-run, and
  **neither platform has been driven with a model**, so by this project's own rule
  neither is tested.
- **No multiplier is proposed for the row.** All twenty are now measured, and that
  is precisely why none is proposed: the completed set spans 1.5× to ~300×, so any
  single figure would misrepresent it. What the row needs is the per-class vector
  the proposal describes, and that needs the probe built.
- **Nothing on this row is now an undiagnosed jichi defect.** Of the seven distinct
  check failures the platform surfaced, four were real and are fixed, and three
  were artifacts of the deadline (§6b). What remains is that seventeen drivers
  cannot finish in 60 s here, which is a fact about the deadline and the host.
- **`make ci` is not run here and cannot be**: no valgrind, no clang on this platform.

## 9. Next, and what it needs

| work | needs |
|---|---|
| ~~the four outstanding durations~~ | **done** — all twenty measured |
| diagnose `cppcheck_lint` 1, `install_no_build` 8, `accessible` 19 | this machine |
| MSYS2 re-run, twice — stock `noacl` and with `acl` | this machine |
| drive both rows with a model | this machine + the gateway |
| record `driver,seconds,limit,censored` in `wd` (proposal §4) | **any machine** |
| make the fork-heavy lints spawn fewer processes | **any machine** — see below |
| the `accessible` deadline decision | **any machine** |
| the M201 retry that re-runs rather than isolates | **any machine** |

**The portable half is worth more than it looks.** `license_lint` at 283× and
`posix_utils_lint` at ~300× are not Cygwin being slow in general — the tier's median
driver is ~1× — they are drivers that spawn a process per file. Reducing that helps
every platform, is ordinary Linux work, and would shrink the Cygwin tier from 96
minutes toward something a person will actually re-run.
