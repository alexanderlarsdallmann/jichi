# Re-measuring OpenBSD: a build I broke, and a lint that passed 6/6 measuring nothing

*2026-08-18. `PLATFORMS.md` recorded the OpenBSD row as **199 of 201 smoke drivers,
1068 checks**, measured at M471. Seven milestones and eight new drivers later, the
row was re-run against the working tree. It did not build.*

*The headline is not the platform. It is that **M472's hardening block broke the
OpenBSD build the day it landed**, that six subsequent milestones shipped over the
break because nothing compiled there, and that the lint written to prove the
hardening flags were real reported **6 ok / 0 failures** on the platform where the
build was broken — including the check added specifically to stop it passing
vacuously.*

---

## 0. What was run

```sh
make clean && time make WERROR=1        # x3, median -> 6.66 s  (the denominator)
scripts/tier-v-openbsd.sh --ref-secs 6.66 --reuse --dirty
```

`--reuse` boots the cached OpenBSD 7.9 amd64 disk (3.3 GB, installed unattended at
M461); `--dirty` ships the **working tree**, which is the only way to verify a
portability fix before committing it. Three rows were run: the first found the
break, the second confirmed the fix, the third confirmed the lint fix.

**The bench reference drifted and that matters.** The runbook's published rows
divide by **6.19 s** (this bench) or threadwork's **4.00 s**; the last OpenBSD row
quoted **4.38 s**. Measured today: **6.66 s** (6.80 / 6.66 / 6.64). Some of that
drift is mine — M472 added eleven warning flags and a hardening block, and a slower
build raises the denominator, which *lowers* every multiplier computed from it. This
is exactly why `SESSION_RUNBOOK.md` says to copy the formula and never a row's
number: the number is a property of the tree as well as the machine.

---

## 1. The break: a probe that asked the wrong question

The first row got as far as the first translation unit:

```
cc -std=c89 -pedantic ... -Werror ... -fstack-protector-strong \
   -fstack-clash-protection -fcf-protection ... -c src/main.c -o src/main.o
cc: error: argument unused during compilation: '-fstack-clash-protection'
    [-Werror,-Wunused-command-line-argument]
```

M472's probe asked **"does the compiler accept this flag?"**:

```make
harden_ok = $(shell printf ... | $(CC) $(1) -xc - -o $(PROBE_OUT) 2>/dev/null && echo $(1))
```

OpenBSD's clang 19.1.7 *accepts* `-fstack-clash-protection` and then **ignores** it.
So the probe said yes, the flag went into `CFLAGS`, and under the build's own
`-Werror` the resulting `-Wunused-command-line-argument` became an error in **every
translation unit**. The row could not build at all — not "199 of 201 drivers", not
"one undiagnosed stop": no binary.

### This lesson was already written down, and then written down again correctly

`CLAUDE.md` records it from M449:

> a probe carries `-Werror=implicit-function-declaration`, because uClibc-ng
> declares `malloc_trim` only under `__USE_GNU` — the symbol linked while the
> declaration was hidden, so the probe said yes and every TU then failed under the
> build's own `-Werror`. **The question a capability probe must ask is "is it
> DECLARED under the flags I build with", not "does the symbol exist somewhere in
> libc".**

Identical shape, 23 milestones later. And the sequence is worse than a simple
repeat: **M476 added `cc_warn_ok`**, which asks the question correctly —

```make
cc_warn_ok = $(shell printf ... | $(CC) -Werror $(1) -xc - -o $(PROBE_OUT) ...)
```

— for the GCC-only warning flags, **forty lines above the broken probe, in the same
file**, without noticing that the thing below it was the same question asked wrong.

**The fix is a deletion.** `harden_ok` is gone; all twelve call sites route through
`cc_warn_ok`. Linux/gcc selects a byte-identical flag set; OpenBSD now builds clean
in 8 s.

### `make ci` was structurally blind to this

`make ci` builds with gcc **and** clang. It could never have caught this: on Linux,
clang genuinely supports `-fstack-clash-protection`, so the flag is used rather than
ignored and no warning appears. The defect requires a target where clang
accepts-but-ignores — that is, a platform outside the local gate. Six milestones
(M473–M478) shipped over a broken OpenBSD build for exactly that reason, and
M475's own commit subject was *"four ci-only configurations nothing compiled until
an outsider ran the gate."*

### The tripwire

`portability_lint.sh` checks 9–11 (it reads the Makefile as a *file*, so it is
portable by construction):

| # | Asserts |
|---|---|
| 9 | a flag probe exists at all — the floor, so a rename cannot make 10 and 11 vacuous |
| 10 | every flag probe passes `-Werror`, as the build does |
| 11 | there is exactly **one** flag probe, so a second cannot drift from it |

Both failure modes were proven red: restoring the broken probe fails 10; adding a
second probe fails 11.

---

## 2. The sharper finding: the lint reported 6 ok while measuring nothing

`harden_flags_lint.sh` is the driver M472 added to prove the hardening flags are
real rather than inherited from the distro. On the OpenBSD row it reported:

```
1..6
# asked for:
ok 1 - -z relro not available on this toolchain (not selected, not asserted)
ok 2 - -z now not available on this toolchain
ok 3 - -fstack-protector-strong not available on this toolchain
ok 4 - -pie not available on this toolchain
ok 5 - floor: not applicable (this toolchain selected neither flag)
ok 6 - the stack is non-executable
```

**Six ok, zero failures, and every word of it false.** `# asked for:` is empty.

### Why

The driver derives its expectation by running `make info` — and it is **the only
driver in the tier that executes the project's own make** rather than reading the
Makefile as a file. OpenBSD's `/bin/make` is BSD make, which cannot parse this GNU
Makefile; that is why the row runs `gmake` deliberately, and it is stated in
`PLATFORMS.md` and in the rig's own banner. So `make info` failed, `asked` came back
empty, every `want` returned false, and each check took its "not selected, not
asserted" branch.

Reproduced locally in one line — putting a `make` on `PATH` that exits non-zero
produces output byte-identical to OpenBSD's.

### The part worth sitting with

**Check 5 is the floor I added two days ago specifically to stop this driver passing
vacuously.** It exists because the four checks above it passed on Ubuntu with the
linker flags deliberately deleted, since the distro supplied the mitigations anyway.
Its message even says so: *"If this goes red, checks 1–4 are decoration."*

It did not go red. It is gated on `want`:

```sh
if want "-fstack-protector-strong" && want "-pie"; then ... else t_ok "floor: not applicable"
```

An empty `asked` makes both `want`s false, so the floor **disables itself in exactly
the case that makes the lint vacuous**. An anti-vacuity guard with the same vacuity
hole as the thing it guards.

### The fix, in two parts

1. **Resolve GNU make** — try `$MAKE`, `make`, `gmake`, `gnumake`, take the first
   whose `--version` says `GNU Make`; if none exists, `t_skip` the whole driver
   honestly. (And the skip is placed *above* `t_plan`, because `t_skip` emits its
   own `1..0` — the M450 rule, which I had already broken once in `child_fds.sh`
   this week and broke again here on the first cut, printing two plans.)
2. **Make "could not ask" a failure, not a skip.** New check 1 asserts that
   `<make> info` is readable and reports the hardening posture. Everything below it
   reads `asked`; if we could not ask, the driver now says so instead of concluding
   seven cheerful things from silence.

---

## 2b. The third defect, which only became visible once the second was fixed

With `asked` populated, the lint finally *measured* on OpenBSD — and went red:

```
1..7
ok 1 - gmake info is readable and reports the hardening posture
# asked for:-fstack-protector-strong  -fcf-protection -Wformat -Werror=format-security -fPIE -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -pie
ok 2 - asked for -z relro and the binary has GNU_RELRO
ok 3 - asked for -z now and the binary is BIND_NOW (full RELRO)
not ok 4 - stack protector selected but no __stack_chk_fail -- nothing was
           instrumented, so the flag is not doing what its presence implies
ok 5 - asked for -pie and the binary is a PIE (ELF type DYN)
not ok 6 - floor: the flags are not load-bearing on this toolchain
           (canary 0->0, pie 0->0, relro 0->1)
```

Two things to read there. First, the §1 fix is working: `-fstack-clash-protection`
is **absent** from `asked` — the `-Werror` probe correctly rejected the flag OpenBSD
accepts and ignores.

Second, check 4's message is **false**. `-fstack-protector-strong` was selected and
is fine; `__stack_chk_fail` is **glibc's symbol name**, not a universal fact about
stack protectors, and OpenBSD does not produce it. The lint had no way to tell "the
flag is inert" from "my detector is wrong for this platform", so it asserted the
first and was wrong.

The detector self-test agrees and shows the shape precisely: `relro 0->1`
discriminates, `canary 0->0` and `pie 0->0` do not. (Why `-fPIE -pie` produces no
`Type: DYN` in the probe on OpenBSD, I do not know, and this document does not
guess — see §1's lesson about mechanisms that fit a symptom.)

### The restructure: the detector becomes a precondition

The old shape ran four assertions and then, at the end, a "floor" meant to stop them
passing vacuously. Two problems, both now fixed:

- **The floor overclaimed.** Its message said "the selected flags are what produce
  canary/PIE/RELRO", but what it proved was that the flags produce them *in a
  throwaway probe binary*. That is a fact about the toolchain, not about `jichi`.
- **It ran last**, so the checks it was supposed to qualify had already reported.

Now the self-test runs **first**, per mitigation, and gates each assertion. Three
outcomes, deliberately distinct, and only the middle one can fail:

| state | verdict |
|---|---|
| flag not selected | `ok` — not asserted |
| selected **and** the detector discriminates here | assert on the binary; **fail** if absent |
| selected but the detector does **not** discriminate | `ok` — *cannot verify*, and it says which detector and why |

That turns OpenBSD's false red into a stated limitation, keeps the check sharp where
the evidence works, and — the part that matters for trust — makes the "cannot
verify" case *visible in the output* rather than indistinguishable from a pass.

**One honest limit, now written into the driver.** On a distro whose compiler
supplies RELRO/PIE regardless of what we ask, "asked and present" cannot be
*attributed* to our flags. Verified locally: deleting `HARDENLDFLAGS` from the link
line leaves the lint green on Ubuntu, and correctly so — the binary really is still
hardened, because the distro did it. What the lint can catch is a **broken promise**:
asked for, detector works, and absent. Proven red by appending `-Wl,-z,norelro
-no-pie` to the link line while leaving `asked` intact (checks 3 and 6 fail).

---

## 3. The row, re-measured

| | recorded (M471) | measured today |
|---|---|---|
| build | clean, 5 s | **clean, 8 s** (`STD_DIALECT = c89 (strict)`) |
| multiplier | 2 (÷ 4.38 s) | **2** = ceil(8 ÷ 6.66) |
| unit suite | 11,643 / 0 | **12,415 checks / 0 failures** |
| smoke | 199 of 201, 1068 checks | **206 of 209, 1092 checks** |
| failing drivers | 2 | **3 reported, 2 real** (§4) |
| declined to run (TAP `1..0`) | — | **6** |

Linux is 12,422 unit checks against OpenBSD's 12,415; the seven-check gap is
environment-gated, as the FreeBSD row's twelve-check gap already documents.

**Six drivers decline to run**, and one of them is worth naming: `child_fds`, the
driver that proves M472's descriptor fence — *no model-issued shell inherits jichi's
journal, telemetry or provider socket*. It needs `/proc/self/fd`. FreeBSD and
OpenBSD both mount no procfs (the rig asserts `procfs is ABSENT` as a feature of the
row), so **that guarantee is verified on exactly one kernel.** NetBSD *does* have
procfs, which is a concrete argument for that row beyond "a third BSD".

---

## 4. The three smoke failures, classified

The tier retries a failing driver standalone to separate a real defect from
cross-driver load (its own M201 machinery). That classification is the finding:

| driver | in-suite | standalone | verdict |
|---|---|---|---|
| `ask_unattended` | fail (checks 2–6) | **passes** | in-suite only — load/resource on a 2-CPU VM. **Not a platform defect.** Passes 6/6 on Linux. |
| `sessions_footprint` | fail (check 2) | **also fails** | real |
| `turn_scratch` | fail (check 2) | **also fails** | real |

So `PLATFORMS.md`'s *failure set* of two was right all along; only its counts were
stale. `ask_unattended` is new noise, not a new defect, and saying so is the whole
point of the retry classifier.

### The two real ones are narrowed — off the product

Both fail on the same shape:

```
not ok 2 - could not read the /context arena gauge (before='' after='')
not ok 2 - could not read the /context turn-scratch gauge
```

`PLATFORMS.md` records them as *"a different, undiagnosed cause"*. Three facts
narrow that considerably, and all three are cheap to check:

1. **The gauge is unconditional in the source.** `src/chat/jc_context.c:200` emits
   `"Arenas: session %lu KB used (%lu KB reserved)"` with no platform guard, no
   feature gate, nothing `#ifdef`-ed. It cannot simply be absent on OpenBSD.
2. **ptydrive proves it was printed.** The driver's PTY script contains
   `expect "Arenas: session" 10` — twice — and **check 1 passed** (`ptydrive script
   rc=0`). The string reached the terminal stream, twice.
3. **The driver's own grep of that same transcript finds nothing.** It looks for
   `Arenas: session [0-9]* KB` after stripping ANSI with `smoke_plain`, and gets
   zero matches, hence `before='' after=''`.

Taken together: **jichi printed the gauge and the harness failed to parse it.** The
defect is in the transcript-to-number step, not in the product, and both drivers
share that step. The remaining question is narrow — what sits between
`Arenas: session` and ` KB` in the OpenBSD transcript that does not on Linux
(a wrap at `--cols 100`, a redraw boundary, an escape `smoke_plain` leaves behind).

**Not diagnosed, deliberately.** Each hypothesis costs a ~20-minute VM cycle, and
this document's own §1 is a cautionary tale about writing down a mechanism that fits
the symptom without testing it against the symptom. What is recorded here is what
was *measured*: the product is exonerated, the harness is implicated, and the next
person starts three steps further along.

---

## 5. A prediction I made, and the two ways it was wrong

Before running the row I predicted in writing that `harden_flags_lint`'s floor check
would go **red** on OpenBSD, because its control binary is built with
`-fno-stack-protector -no-pie -Wl,-z,norelro` and OpenBSD **enforces PIE**, so the
control should come back *as* a PIE and fail the comparison.

**Wrong twice, in opposite directions.**

- On the row that ran before the §2 fix, it went **green** — because the driver never
  reached the probe at all: `asked` was empty and the floor was gated on `want`. Had
  I stopped at "check 5 was green, so PIE enforcement is not a problem" I would have
  drawn precisely the wrong conclusion from a correct observation.
- On the row after, it went **red** — the outcome I predicted, by a mechanism I did
  not. The control binary was correctly **not** a PIE (`pie 0->0` means neither the
  control nor the treatment was detected as `DYN`). So the PIE-enforcement story was
  not the cause. The cause is that two of the three detectors are glibc-specific.

The prediction named the right check and the wrong reason, and being right about the
outcome would have let me ship the wrong explanation. That is the same failure this
document opens with, and the same one `chrtext`'s `GATE_INTEGRITY_FINDINGS.md`
collected three of: a mechanism that fits the symptom, never tested against it.

## 6. What is still open

| | |
|---|---|
| `sessions_footprint` / `turn_scratch` | **CLOSED at M481.** The narrowing in §4 was right — the harness parse. The mechanism was `grep -o '[0-9]*'`, a `-o` pattern that can match the empty string: OpenBSD's grep prints nothing and exits 0. Diagnosed by *difference* — NetBSD passes both, being a BSD that ships GNU grep. See [2026-08-18-openbsd-nullable-grep.md](2026-08-18-openbsd-nullable-grep.md) |
| why `-fPIE -pie` yields no `Type: DYN` in the OpenBSD probe | observed (`pie 0->0`), not explained; the lint now reports it as *cannot verify* rather than guessing (§2b) |
| `child_fds` on a non-Linux kernel | **CLOSED at M480.** NetBSD has procfs; the driver runs green there, so M472's descriptor fence is verified on two kernels. Still declines on both other BSDs |
| NetBSD | **DONE at M480** — Verified, the full gate, 209/209 smoke. `scripts/tier-v-netbsd.sh`, and it paid for itself immediately by supplying the control that solved the two rows above |

---

## 7. Lessons

1. **A capability probe must ask the question the build asks.** Not "is this flag
   accepted" but "does it survive `-Werror`". This is M449's lesson; it has now cost
   two milestones, 23 apart, and the correct implementation existed forty lines away
   when the second one was written.
2. **A green lint on an unmeasured platform is worth nothing, and reads like
   everything.** Six ok and zero failures, on a platform where the build was broken.
3. **An anti-vacuity guard can share the vacuity it guards against.** The floor was
   gated on the same value whose emptiness makes the checks meaningless. When you
   add a floor, ask what makes the *floor* skip.
4. **"Could not measure" must be a failure, not a skip.** Every one of those six
   cheerful lines was a skip wearing a pass's clothes.
5. **A detector is a platform assumption wearing a fact's clothes.**
   `__stack_chk_fail` is glibc's symbol name, not "how stack protectors look". Any
   check that reads evidence out of a binary should first ask whether that evidence
   *discriminates* on the toolchain in front of it — and when it does not, say
   "cannot verify" rather than picking the conclusion it happens to prefer. A check
   that cannot tell "the thing is broken" from "my instrument is wrong" will report
   the first.
6. **Fixing one layer reveals the next, and that is the argument for going back.**
   Three defects, each invisible until the one above it was fixed: the build could
   not compile, so the lint could not run; the lint could not read `make info`, so it
   could not measure; once it measured, its evidence was wrong for the platform. A
   single row run would have found only the first. The rig is cheap precisely so it
   can be run four times in an afternoon.
7. **Run the platform rows after touching the build.** Not because the build looks
   risky, but because the local gate cannot see a toolchain it does not have. The
   only reason any of this was found today is that somebody re-ran a row whose number
   had gone stale.
