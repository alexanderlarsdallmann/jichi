# What the deadline was hiding, and what the rig was hiding (M698)

**Measured 2026-09-22 on HRZNB-U020390**, the one machine in this project that
carries WSL2, Cygwin and MSYS2 side by side — same hardware, same NTFS, same
user, same commit (`83775025`). Two rigs were written, both rows ran end to end,
and both were driven with a model by the rig itself.

This page is written for somebody who was not here, and its most useful section
is §6, where a defect this project published turns out not to exist.

---

## 1. Why a rig, and why only now

`DEFERRED.md` recommendation 4 — *give Cygwin and MSYS2 a rig* — was the oldest
live item on that page. It stayed open on purpose, under a rule this project set
for the Guix rig in May: **a rig written blind for a platform nobody can run is a
never-executed artifact.** A script that has never run is not a tool, it is a
guess with a filename.

Both rows had now run by hand — Cygwin at M696, MSYS2 at M697 — so the rule
permitted writing them, and required that what got written be the transcript of
those runs rather than a design for them.

**They are not shaped like the other rigs, and the reason was already written
down.** Every other `tier-v-*` boots a guest and drives it over ssh. Cygwin and
MSYS2 are not guests: they are POSIX-emulation runtimes installed on the machine
running the rig, so there is nothing to boot and nothing to reach.
`scripts/_rig_live.sh` had anticipated exactly this in its own header —

> The transport is each rig's own business… A rig with a different shape calls
> the task functions directly — see `tier-b-device.sh`.

— so the two rigs share the **task** (both prompts, the per-run nonce, the
fixture, the config) and supply a local `sh` as the **transport**. The task is
what makes two rows comparable; the transport is not. `jc_rig_live` itself is
untouched, so the golden-command lint still pins it byte for byte.

---

## 2. The two rows

| | **Windows + Cygwin** | **Windows + MSYS2 (MSYS)** |
|---|--:|--:|
| rig verdict | 13 ok, 1 failed | 14 ok, 2 failed |
| build median of three | **125 s** (128/123/125) | **98 s** (99/97/98) |
| multiplier | **13** = ceil(125 ÷ 9.65) | **11** = ceil(98 ÷ 9.65) |
| unit suite | **13,438 / 0** | **13,425 / 1** |
| smoke tier | 317 drivers, 3 killed, 4 failed, **10,970 s** | 317 drivers, 3 killed, 2 failed, **8,826 s** |
| `doctor` | 25 ok, 4 warnings, 0 problems | 24 ok, 5 warnings, 0 problems |
| agentic turn | `TIER-V-cygwin-3C2C3F` | `TIER-V-msys2-D1A043` |

Every multiplier is printed **with its denominator**, because a multiplier is a
ratio and a ratio quoted without its reference is a number pretending to be a
measurement. The published Cygwin figure of `10` came from 130 s ÷ 13 s with both
operands compile-inclusive, so neither term was a runtime.

**MSYS2 is consistently faster than Cygwin** on identical work: build 98 s vs
125 s, `license_lint` 358 s vs 553 s, `docs_locators_lint` 96 s vs 147 s — about
0.65–0.78×. This is the first clean like-for-like comparison of the two layers
this project has, and it is only clean because both ran on one machine.

Full per-driver data, all 317 rows each:
[`2026-09-22-tier-durations-cygwin.txt`](2026-09-22-tier-durations-cygwin.txt),
[`2026-09-22-tier-durations-msys2.txt`](2026-09-22-tier-durations-msys2.txt).

---

## 3. The experiment: what a deadline conceals

M696 published the Cygwin row at the **shipped** deadlines — multiplier 1, 60 s
per driver — and recorded *"317 drivers, 17 killed at 60 s and zero genuine check
failures"*, in 98 minutes.

M698 ran the same tier at the host's **measured** multiplier. The result:

| | multiplier 1 (60 s) | multiplier 13 (780 s) |
|---|--:|--:|
| killed | **17** | **3** |
| failures reported | 0 | 4 |
| wall clock | 98 min | 183 min |

**Fourteen of seventeen bounds became durations for 1.9× the time.**
`license_lint` is the clean example: at 60 s it could only be recorded as *"more
than 60 s"*; it now reads **553 s**, corroborating the 565 s M696 had measured
separately by hand.

### 3a. The three that stay censored are one species

`smoke_lint`, `snapshot_lint` and `posix_utils_lint` were killed on **both**
layers, at 780 s on Cygwin and 660 s on MSYS2. All three scan the tree spawning
a process per file. `accessible`, which is wall-clock bound, ran in 82 s.

This is the shape of the Cygwin/MSYS2 fork penalty: it does not slow the tier
uniformly, it **destroys one kind of driver** — the kind whose cost is process
count rather than work done — and leaves everything else alone. A single
"×N slower" figure for these platforms is not a simplification of that, it is a
misdescription of it.

### 3b. A killed driver costs twice its deadline

The tier retries any failing driver once, standalone, to label it *in-suite only*
versus *also alone*. For a **failed** driver that label is diagnostic gold. For a
**killed** one, the rig's own log shows what the retry buys:

```
    23 --- smoke: smoke_lint
   809 smoke: retrying smoke_lint standalone to classify the failure...
  1594 smoke: smoke_lint was KILLED again (rc=137) -> it needs more than 780s
  1595 --- smoke: snapshot_lint
```

1572 seconds to re-learn what the first kill established. So raising the
multiplier is **not** linear in the deadline: it is linear for drivers the larger
deadline *rescues*, and **2×** for each one that stays censored. Recorded as
`proposals/2026-09-calibration-tier.md` §1.3a with two candidate remedies and no
decision, because the retry's value under load has never been measured and that
section's own §6 says it must be before anything is chosen.

### 3c. The multiplier flips on build jitter

Four clean builds measured 123, 125, 126 and 127 s against a 9.65 s reference.
The boundary between multiplier 13 and 14 sits at **125.45 s** — inside the noise
— so two runs of the same rig on the same machine an hour apart chose 13 and 14.
Neither is wrong. A denominator that flips on jitter is simply not a stable one,
and a median of three is not enough to fix it.

---

## 4. Six failures, and not one of them jichi

| failure | platform | cause |
|---|---|---|
| `setup_keyfile` | **both** | the rig exported `JICHI_API_KEY`; the wizard correctly refused to overwrite it |
| `pdf`, `docs_pdf` | Cygwin | `pdftotext` on PATH is a **native Win32** binary from Git for Windows |
| `reading_trace` | MSYS2 | **MSYS2's `sed` strips CR**; Cygwin's does not |
| `predict_record` | Cygwin | in-suite load artifact; passes alone in 10 s |

### 4a. A name on PATH is not a capability

Both PDF drivers opened with what looks like the right guard:

```sh
command -v pdftotext >/dev/null 2>&1 || t_skip "pdftotext not installed"
```

On MSYS2 that skipped cleanly. On Cygwin it did **not** — it ran and failed with
the driver's own accusation of itself, *"the fixture PDF does not extract —
driver bug, not jichi"*. The fixture was fine. Cygwin's PATH inherits the Windows
PATH, so:

```
$ command -v pdftotext
/cygdrive/c/Program Files/Git/mingw64/bin/pdftotext
$ pdftotext /tmp/tmp.IRTzFQ6Atk/doc.pdf -
I/O Error: Couldn't open file '/tmp/tmp.IRTzFQ6Atk/doc.pdf'
```

A native Win32 program cannot resolve a Cygwin path, so it fails on every fixture
it will ever be handed.

**Note the direction of the injustice: the tier was harsher on the host that had
more installed.** MSYS2, with no `pdftotext` at all, skipped and stayed green.
`command -v` answers *"is there something on PATH with that name"*; the question
needed was *"can it open my file"*. The probe is now functional and shared
(`smoke_pdftotext_works`): build a PDF, extract it, believe the result.

### 4b. Two runtimes disagree about a carriage return

`reading_trace` failed on MSYS2 and passed on Cygwin — the configuration that
makes a finding attributable. Three traces reported `req.1 drifted`, and the diff
showed eight identical-looking lines. At byte level:

```
expected (committed):  ... H T T P / 1 . 1  \r \n
captured on MSYS2:     ... H T T P / 1 . 1  \n
```

Three lines isolate it, on one machine carrying all three layers:

```
printf 'A\r\nB\r\n' | sed -e 's/X/Y/' | od -c
  WSL2    ->  A \r \n B \r \n
  Cygwin  ->  A \r \n B \r \n
  MSYS2   ->  A \n B \n
```

**MSYS2's `sed` performs CRLF text-mode translation and Cygwin's does not.**
`capture.sh` normalises artifacts through `sed`; an HTTP head is CRLF by the
standard; so the **recording** lost the carriage returns. jichi was never
involved — `mockmodel` writes `req.N` with `fopen(…, "wb")` and the bytes
reaching it were correct throughout.

The driver now probes CR survival before `t_plan` and skips with the
measurement, the same shape as `pathfence_dangling`'s dangling-symlink probe, and
for the same reason: asserting would report drift in the documentation for a
fixture the platform cannot produce.

---

## 5. Five defects in the apparatus, none in the measurement

Before the long run could start, the rig had to be repaired five times. It is
worth listing them together, because they have one shape:

1. **A counter that printed zero twice.** `n=$(grep -c PATTERN f || echo 0)` —
   `grep -c` prints the count *including* `0` and separately exits 1, so the
   no-match path captured grep's `0` and the fallback's `0`. Harmless in a
   warning count; it also fed the KILLED and FAILED tallies, where `0` is exactly
   the value a trustworthy run holds.
2. **A run raised to a generous deadline that recorded no durations.** The whole
   purpose of multiplier 13 was to turn bounds into durations, and
   `tests/smoke/run.sh` prints no per-driver timing at all. Three and a half
   hours would have produced a log saying what ran and never what anything cost.
3. **Scratch logs written into the tree being measured**, inflating the row's own
   provenance stamp to *"7 path(s) differ"* when four were the rig's source and
   three were its litter. A provenance line that counts its own rubbish is worse
   than none, because it is the line a reader trusts.
4. **`jcw_surfaces` overwriting one log four times**, discarding `doctor`'s
   findings — the one surface a platform row is actually read for.
5. **An fstab check whose fallback could never fire**: `grep … | sed … | tee || note`
   takes the exit status of `tee`, which succeeds whether or not `grep` matched.
   The stock (no-`acl`) configuration — the single most important fact this rig
   can report about an MSYS2 host — would have printed nothing.

**Not one was in the measurement.** The compiler, the tier and the platforms were
fine throughout. All five were in the *recording*: a counter, a timestamp, a
provenance line, a log path, a fallback. The apparatus that turns a run into
evidence is code too, and it is the code nobody reviews, because it is "just
logging" right up until it is the only thing left of a run that cannot be
repeated.

Two of the five were caught by **running the instrument against a fake tier** —
three `echo`s and two `sleep`s, four seconds — which also caught a sixth: with
timestamps in place, every line gains a `%6d ` prefix, so the driver counter's
anchored pattern `^--- smoke: ` matched nothing and the denominator silently
became **0**.

> Before a run you cannot afford to repeat, run the **instrument** against a
> fixture you can.

---

## 6. The defect that was not there

This is the section worth reading twice.

Running at the measured multiplier surfaced `setup_keyfile` failing on **both**
emulation layers:

```
not ok 1  - PTY drive failed (rc=3): in this shell -- nothing to store.
not ok 3  - key file missing or malformed
not ok 4  - key file mode is , want -rw-------
not ok 7  - still reports the key as missing
not ok 16 - no guidance before the optional questions
not ok 28 - no sound/notify question in the optional block
```

~400 s on Cygwin, ~350 s on MSYS2, *"also fails standalone"* on both. It was
written up as the run's headline result — **the 60 s deadline had been killing
this driver before it could fail, so "zero genuine check failures" was an
artifact of censoring** — committed, and shipped with a `CHANGELOG` entry warning
users of both platforms, naming a suspect.

**The suspect was excellent.** The driver failed on exactly two rows, Windows +
Cygwin and Windows + MSYS2, and never on the Linux bench where `make ci` is
green. Those two rows are **Partly verified**. And M695 — three commits earlier,
*in this same band* — had changed what the setup wizard prints on precisely that
verdict class, replacing `!jc_platform_is_linux()` with a three-valued
`jc_platform_row_verdict()`. A change to a class of platforms, failing on that
class and no other, with the bench structurally unable to show it.

**It was the rig.**

The rig drives its live turns through a gateway, and jichi takes the key by
`apiKeyEnv` — the *name* of an environment variable, so no secret sits in a
config file. So the operator exports `JICHI_API_KEY` before launching the rig.
That export is inherited by everything the rig runs, including the smoke tier.
And `setup_keyfile` drives the setup wizard through a pty, telling it to store a
key in — `JICHI_API_KEY`. jichi looked, found it already set, and answered
correctly:

```
  $JICHI_API_KEY is already set in this shell -- nothing to store.
```

It therefore never asked for a key. The pty script waited for a prompt that would
never come, burned its `expect` timeouts, and returned rc=3; every later check
cascaded from a key file that was never written. **jichi was right at every
step.** A wizard declining to overwrite a key the user already has is a feature,
and it fired.

The control took four minutes:

| | `setup_keyfile` |
|---|---|
| `JICHI_API_KEY` unset | **28 of 28 pass** |
| `JICHI_API_KEY=sk-RIGLEAK-TEST` (synthetic) | rc=3, identical cascade |

The positive control used a **fabricated** key, so what breaks the driver is the
variable being *present*, not the key being valid — exactly what the wizard's
guard tests and exactly what the rig supplied. And check 27 is *"doctor reports
the platform"*, the very check M695 was suspected of breaking. **It passes.**
M695 is exonerated by the same run that would have convicted it.

### Why this shape is the dangerous one

A contaminating rig does not produce noise. It produces a **correlated** signal,
and it correlates with exactly the thing the rig is for. This rig instruments
only the Windows-family rows, so its contamination appears only on the
Windows-family rows — which is, from outside, indistinguishable from a defect
specific to those platforms. Every instinct that says *"a failure on two related
platforms and nowhere else is a platform defect"* is ordinarily right. Here it
was the trap. The bench being green was not evidence of innocence; **the bench
simply has no gateway key exported.**

> Before believing a defect that appears only where one instrument reaches, run
> the subject with the **apparatus removed**. Not a re-run — a run without the
> instrument. If the defect needs your rig to be present, it is your rig's.

The repair is a property rather than a rule: the rig stashes the key under a name
jichi has no opinion about, `unset`s the operator's variable for the whole
offline half, and restores it only inside the live turns. The environment the
tier sees is now the environment a user has, which is what a tier is for. A note
in a runbook would have been the fix this project already rejected once — a rule
to be recalled at the right moment rather than a property the code holds.

The original claim is kept and marked in `CHANGELOG.md`, `ROADMAP.md` and both
platform rows rather than deleted. **A retracted defect report is worth more to a
reader than a silent edit**, because the reasoning that produced it was sound and
will be available to produce it again.

---

## 7. What two rows proved that one could not

The stated reason for measuring both layers is that MSYS2's runtime is a fork of
Cygwin's, so **agreement** is evidence about emulating POSIX, while **divergence**
isolates Cygwin-version behaviour from emulation behaviour. This run paid that
argument out in both directions at once:

- they **agreed** on the three killed drivers — `smoke_lint`, `snapshot_lint`,
  `posix_utils_lint`, at different deadlines on independently maintained
  runtimes — which attributes the cost to emulation rather than to either
  implementation;
- they **disagreed** about `sed` and CR, and about what `pdftotext` on PATH even
  is — facts neither row alone could have established, having nothing to be
  different from.

A single Windows row would have reported three failures and offered no way to
separate a platform property from an implementation accident.

**Git for Windows is implicated three times** and deserves naming: it maps
`msys-2.0.dll`, so a guard keyed on that DLL refused every run; and it supplies
the unusable `pdftotext`. It is a *third* cygwin-family installation present on
any developer Windows machine, and it leaks into both layers.

---

## 8. What this page does not claim

- **Not** that these platforms are now fully verified. `make ci` still cannot run
  on either — no valgrind, no clang — so the gate that everything else merges
  under has never run there, and the rows say `Partly verified` for that reason.
- **Not** that the tier is clean on them. Three drivers are still **bounded**
  rather than measured on each row, and a bound is not a duration. Their output
  is not evidence about jichi in either direction.
- **Not** that `predict_record` is understood. It fails under load and passes
  alone; *in-suite-only* was misdiagnosed once already in this band, so it is
  recorded as an observation and not as a cause.
- **Not** that MSYS2's row generalises to a fresh install. It was measured in the
  **configured** state, with `acl` mounts for `/tmp` and `/home`, and the rig
  says so on the row. M697 measured the stock state separately, and that is where
  the file-privacy findings live.
- **Not** that the multiplier used here is the right policy. It is a proportional
  bound derived from a measured median over a measured denominator, and §3b shows
  the cost of raising it is not what this project assumed.
