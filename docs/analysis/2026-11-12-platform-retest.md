# Retesting the platforms in a pause: what three Ubuntu gates could not see

*2026-11-12, at master `7b23e620` (M1160: batch H9b of the harness-port drive had just landed). The
drive was paused on purpose so the operator could decide what came next, and the operator asked a
question first: would a few platform retests give meaningful data before the drive continues? This
page is the answer. It records what was run, what failed, how each failure was taken apart, what
was withdrawn, and what is still open. Every number on it was measured on that day; the exact
commands, addresses and raw logs sit in the project's internal record.*

## Why retest in a pause, and why then

Every gate this project runs before code reaches master runs on **one** kind of machine: Ubuntu 24.04,
glibc 2.39, x86-64, gcc 13 and clang 18. The container's fast gate, the host's pre-gate sweep and
`make ci` differ in what they run, not in where. Three gates that agree are worth less than they look
when they share a libc, a header set and a compiler release.

Two things made 2026-11-12 the right day:

- **Shared code had moved.** Since 0.13.0 the drive had changed 1,345 lines outside the harness in 12
  files: the per-worker stop and inject, the control socket, background commands, doctor's TLS
  check. Only Linux on x86-64 had run any of it.
- **The next batches touch process semantics.** The queue after the pause cancels running commands
  and detects when a shell exits. Kernels and libcs differ most in exactly that, and without a
  baseline taken *before* those batches, a later failure could not be told from an old one.

The last board runs were from 2026-10-15. In between, the unit suite had grown to 37,960 checks and
the smoke tier to 554 drivers.

## What was run, and what each row can and cannot show

| row | how | what it can show | what it cannot show |
|---|---|---|---|
| Raspberry Pi 500 | physical board, aarch64, **16 KB pages**, Debian 12 (gcc 12.2, glibc 2.36); `scripts/tier-b-device.sh` | build, unit, smoke, footprint, a driven model turn | anything x86-specific |
| Raspberry Pi Zero 2 W | physical board, aarch64, 415 MB of RAM, an SD card, Debian 13 (glibc 2.41); same rig | the same, on a small, slow board | anything a faster board would time differently |
| Raspberry Pi 400 | physical board, aarch64, 4 KB pages, Debian 13 (gcc 14.2, glibc 2.41); same rig | the same, on the board the bench knows best | what only 16 KB pages or Debian 12 would show |
| Athlon II X3 | physical x86-64 box, Ubuntu 26.04.1, kernel 7.0, gcc 15.2, glibc 2.43; same rig | a newer compiler and libc on x86-64 | non-x86 behaviour |
| FreeBSD 15.1 | KVM guest, clang 19.1.7, no procfs; `scripts/tier-v-bsd.sh` | a non-Linux kernel and libc | a model turn: this row was not driven |
| the architecture sweep | 20 `zig cc` musl triples, unit suite under `qemu-user`; `scripts/tier-v-arch.sh --all` | byte order, word size, alignment | anything about real hardware, and no model call (`HAVE_CURL=`) |
| native x86-64 musl | `zig cc -target x86_64-linux-musl`, run on the host, **no emulator** | musl's behaviour without qemu in the way | — |

Two choices shaped the rows, and both are stated so a reader can weigh them:

- **The driven turns used a free model on the institution's gateway**, not a local one. Local
  inference on this workstation's display GPU had crashed the desktop twice in October, under long
  contexts. A short turn is unlikely to repeat that, but the operator's session is not the thing to
  gamble. So the boards are **Driven** (a text turn and a tool-calling turn whose answer only a real
  tool call could produce), and the VM rows are **Verified, not Driven**.
- **The multiplier's denominator was re-measured first.** `JC_SMOKE_TIMEOUT_MULT` is a ratio: a
  device's build seconds over this bench's. The serial `make WERROR=1` on the workstation took 10.35,
  10.27 and 10.33 s at `7b23e620`; the median, **10.33 s**, is the denominator for every row here. It
  was 8.59 s in October: the tree's build has grown by a fifth, and a stale denominator would have
  given every board a deadline that was too tight.

## Results

Every count below comes from the runner's own verdict lines: the unit suite's `N checks, M failures`,
and the smoke runner's `FAILED (in suite)`, `ALSO fails standalone` and summary lines. Why that matters
is told at the end, among the mistakes.

| row | build (multiplier) | unit suite | smoke tier | model turns |
|---|---|---|---|---|
| Pi 500 (aarch64, 16 KB pages, Debian 12) | clean, 16.4 s (2) | 37,960 / 0 | **did not build** (Finding 1); with the missing include: 551 of 554 | driven |
| Pi Zero 2 W (aarch64, Debian 13, 415 MB) | clean, 117.1 s (12) | 37,960 / 0 | 552 of 554 | driven |
| Pi 400 (aarch64, Debian 13) | clean, 49.3 s (5) | 37,960 / 0 | 553 of 554 | driven |
| Athlon II X3 (x86-64, Ubuntu 26.04, gcc 15.2) | clean, 77.4 s (8) | 37,960 / **1** (Finding 5) | not run by `check-target`; run alone: 553 of 554 | driven |
| FreeBSD 15.1 (clang 19) | **failed** (Finding 3) | **did not compile** | **did not build** | not driven |
| FreeBSD, with five fixes in a throwaway copy | clean, 10 s (1) | 37,935 / 2 | 527 of 554 | not driven |
| 20 musl triples under qemu-user, zig 0.17.1 | 19 of 20 clean (Finding 4) | 17 ran, all failing only Finding 2 | — | not driven |
| x86-64 musl, native | — | 37,960 / 7 (Finding 2) | — | — |

The smoke failures on the boards, by driver:

- **`snprintf_fallback` check 15**: the Pi 500, the Zero and the Pi 400. Every aarch64 board, and no
  x86-64 machine. See "Open" below.
- **`harness_drive` check 7**: the Pi 500 only. See "Open".
- **`parallel_abnormal_endings` cell 15**: the Zero only. See "Open".
- **`snapshot_lint` check 15**: the Pi 500 only, and my own artifact. See "Withdrawn".
- **`setup_keyfile`**: the Athlon only. The setup wizard said *"in this shell -- nothing to store"*,
  because the machine's own shell start-up file exports an API key variable. That is a test-isolation
  gap, not a defect in the wizard: the smoke tier gives every driver a private home directory but
  passes the caller's environment through, and this driver assumes no key is set.

## Finding 1 — a header that arrived by accident

**Symptom.** On the Pi 500, `make check-target` passed the unit suite (37,960 checks, 0 failures) and
then printed no smoke summary at all. The rig said so honestly — *no driver-count marker of either
shape* — instead of reading green from the exit code.

**Cause.** The smoke tier's helper programs are built first, and one of them did not compile:

```
tests/tools/sockbind.c:98:26: warning: implicit declaration of function 'offsetof'
tests/tools/sockbind.c:98:35: error: expected expression before 'struct'
```

`offsetof` is a C89 macro from `<stddef.h>`, and `sockbind.c` never includes that header. On the
development machine it compiles anyway: with the Makefile's `-D_GNU_SOURCE -D_XOPEN_SOURCE=600`,
glibc 2.39's own headers happen to pull in the full `<stddef.h>`. Debian 12's glibc 2.36 on aarch64
does not. The file was added on 2026-11-08 (M1152-g) and reached master the next day, after the last
board runs, and every gate since ran on the one libc that hid it.

**Why it matters more than one file.** A missing smoke tier is not one red driver; it is 554 drivers
that never ran. The row would have read "unit suite green" and nothing else.

**Fix.** One line: `#include <stddef.h>`. The only other file that uses `offsetof`
(`src/chat/jc_agent.c`) already includes it, so a lint that holds the two together has a universe of
two files today and a tooth anyone can test by deleting the include.

## Finding 2 — a libc that will not seek in `/proc`

**Symptom.** The emulated aarch64 and armhf rows each failed 5 unit checks, all in
`tests/test_platform.c`, all reads of `/proc/self/status`.

**The first hypothesis, and how it died.** `qemu-user` fakes parts of `/proc`, and jichi makes itself
non-dumpable at startup, which changes who may read `/proc/self`. Both were plausible. A probe of
fifteen lines — open, read, with and without `PR_SET_DUMPABLE=0` — read 511 bytes in every
combination, natively and under qemu. The hypothesis was wrong.

**The real cause, measured on the exact call.** `jc_read_file` learns a file's size by seeking to its
end. On procfs that size is unknown, and a change of 2026-11-04 (M1138) handled it on the assumption that
"the seek to end succeeds" and reports 0. A second probe asked each libc directly:

| libc | `fseek(f, 0, SEEK_END)` on `/proc/self/status` |
|---|---|
| glibc 2.39, x86-64, native | `0`, and `ftell` gives 0 |
| musl, x86-64, **native, no emulator** | `-1`, errno 22 (*Invalid argument*) |
| musl, aarch64, under qemu-user | `-1`, errno 22 |

So the emulator was innocent: **musl refuses the seek and glibc allows it**, and `jc_read_file` treats
the refusal as an I/O error. Every `/proc` read that goes through `jc_read_file` fails on musl:
measured natively on x86-64 and on seventeen emulated targets, and by the same libc on Alpine and in
the static builds.

**Why the emulated rows showed 5 failures and the native run 7.** Run natively as x86-64 musl, the unit
suite fails **7** checks: the same five, plus two reads of `/proc/cpuinfo`. `qemu-user` serves
`/proc/cpuinfo` itself, from a regular file that *can* be seeked, so on some emulated targets two of
the failures disappear. The emulator did not cause the defect; it hid part of it.

**Fix.** Treat a failed seek like a reported size of 0: the size is unknown, so read in bounded
chunks, which the code already does for the glibc case. The native musl run is the red-first
instrument, and it costs about a minute on the development machine.

## Finding 3 — FreeBSD: five blockers in a row, each hiding the next

FreeBSD had been **Verified — the full gate** since September. At `7b23e620` its gate cannot run: the
`WERROR=1` build fails, the unit suite does not compile and the smoke tools do not build. The reason is
not one defect but five, each added after the last FreeBSD row and each stopping the
run before the next could show itself. They were found one at a time, by fixing each in the virtual
machine's throwaway copy only and running again.

| # | where | what FreeBSD says | added |
|---|---|---|---|
| 1 | `src/harness/jc_hp_gate.c:1786` | `variable 'k' set but not used [-Werror,-Wunused-but-set-variable]` | 2026-11-11 |
| 2 | `tests/test_progress.c` (3 lines) | `use of undeclared identifier 'SIGXFSZ'` | 2026-10-20 |
| 3 | `tests/test_landlock.c` | `undefined symbol: jc_landlock_pack_u64` (link) | 2026-10-21 |
| 4 | `tests/tools/lo_up.c` | `incomplete type 'struct ifreq'`, `IFNAMSIZ` undeclared | 2026-10-27 |
| 5 | `tests/tools/sockbind.c:98` | `offsetof` — Finding 1 again | 2026-11-08 |

- **A compiler upgrade is a new reviewer.** In the gate orchestrator, `k` is set to 0 and incremented
  in a loop, and never read. clang 18 and gcc 13 (the gates) and even gcc 15.2 (the Athlon) count an
  increment as a use and stay silent; FreeBSD's clang 19.1.7 does not. Under `WERROR=1` the whole build
  fails — on a variable in harness code that only ever runs on Linux, because the shared build
  compiles the harness everywhere.
- **Standards visibility, twice.** The tree builds with `-D_POSIX_C_SOURCE=200112L`. `SIGXFSZ` belongs to
  the X/Open extension rather than base POSIX, and FreeBSD hides it under that macro; glibc shows it.
  FreeBSD's `<net/if.h>` hides `struct ifreq` unless `__BSD_VISIBLE` is set, the same wall `jc_proc.c`
  met in September. `lo_up` brings up a loopback interface for the Linux network-namespace tests — and
  it is built as a smoke tool on every platform, so its failure removes the whole smoke tier.
- **Declared everywhere, defined on one kernel.** The Landlock helper `jc_landlock_pack_u64` is a pure
  function — a `memset` and a byte loop — but it is defined inside `#if defined(__linux__)`, while its
  declaration and its unit test are not. The link fails.
- **And the rig hid the first one.** FreeBSD's first run reported *"build failed — first diagnostics
  follow"*, then twenty compiler command lines and no diagnostic. The rig's filter,
  `grep -E "error|warning"`, matched `-Werror` in every command line before it reached the error. The
  error was read by keeping the virtual machine up and opening its build log.

With all five fixed in the throwaway copy, the `WERROR=1` build is clean: FreeBSD's clang found nothing
else to object to. Behind them:

- **The unit suite:** 37,935 checks, **2 failures**, both in one test (`tests/test_tool.c:1265/1267`: a
  refusal that should mention "reading the path"). Not yet classified.
- **The smoke tier:** **527 of 554 drivers passed**. Of the 27 that did not, the runner itself says
  what kind of failure each is:
  - **one deadline.** `docs_counts_lint` was *"KILLED at its 60s deadline"*, twice. The runner says so
    in words rather than printing a failed check. The board's multiplier was 1, because clang built the
    tree in 10 seconds, but this driver scans text with BSD's `grep` and `awk`. A multiplier taken
    from build time does not predict how fast the userland tools are.
  - **one in-suite-only failure.** `bg_spool_cap` failed in the suite and passed standalone.
  - **twenty-five failed in the suite and again standalone.** They were sorted by their first failing
    line only, so this is a first reading, not a verdict:
    - **one is my own artifact.** `license_lint` found a source whose first line is not the licence
      header: the `lo_up.c` I had patched, whose new first line was `#define __BSD_VISIBLE 1`.
    - **nine are the harness on a kernel it does not support.** jichi-harness is Linux-only by the
      operator's decision, and most of its drivers stop at `readlink /proc/self/exe failed` (FreeBSD
      mounts no procfs). The failure is that they do not *skip* off Linux.
    - **ten look like test tooling that assumes GNU or Linux**: `wc` output padded with spaces on BSD
      and compared as a string, `stat` called with GNU flags, a peak-memory reading of 0 from a
      GNU-only `time`, an `fsync` check whose tracer is Linux's, and instruments that found nothing to
      measure.
    - **five look like they could be real** and are recorded as open: a check that expected exit code 1
      three times and got 0, the report of a worker killed by signal 9, 59 MB of growth where a memory
      ceiling should have held, a symlinked search path, and a spill file's path.

The plainest summary is the honest one: FreeBSD has not been tested since September. Since then the tree
gained many drivers written and checked on Linux alone, and a platform that is not exercised drifts
— not in one place, but in thirty.

## The architecture sweep: one defect, seventeen times

The sweep cross-builds jichi for 20 musl triples with zig 0.17.1 and runs the unit suite under
`qemu-user`. **19 of the 20 build with zero warnings under `WERROR=1`**; the twentieth is
`x86_64-linux-musl`, the host's own triple, which hits Finding 4. Seventeen triples executed, and **all
seventeen failed only the `/proc` reads of Finding 2** — `test_platform.c:199-201` and `211-212` on all
of them, plus the two `/proc/cpuinfo` lines on the twelve where qemu does not fake that file. Nothing
else failed on any byte order or word size: big-endian MIPS, PowerPC and s390x, 32-bit x86, MIPS and
RISC-V, n32 and the 64-bit ABIs alike. Without Finding 2, the emulated matrix is clean.

Three triples did not finish. `x86_64-linux-muslx32` cannot run here at all (`Exec format error`: this
kernel has no x32 support). `armeb` and `hexagon` **did** run — their output shows jichi's own warning
lines and a suite name — but stopped before the summary, and the rig labelled them *"did not
execute"*. That label is wrong: the classifier calls any run without a summary line a non-run. Both
were run again with their build trees kept, and then once more with zig 0.16.0 at the same commit, to
separate the toolchain from the code:

| triple | zig 0.17.1 | zig 0.16.0 |
|---|---|---|
| `armeb` | 14 failures, all in the JSON double round-trip (`50.5871234` printed back as `-2.62e+135`), then `qemu: uncaught target signal 4 (Illegal instruction)` in `test_json_print_exact` | the suite completes: 37,941 checks, **80 failures** |
| `hexagon` | prints `test_harness` — the first suite's name — and stops, no message | the same |
| `x86_64-linux-musl` | builds with a warning (Finding 4); 7 failures (Finding 2) | **warning-free**; the same 7 failures |

- **armeb** has a known toolchain defect: in August a minimal C program with no jichi code reproduced
  musl-on-armeb `printf` mangling a variadic `double`. The JSON failures fit it exactly, and the suite
  uses doubles in many more places than it did then. The 80 failures under 0.16.0 have **not** been
  classified one by one, and the SIGILL under 0.17.1 is unexplained: `zig cc` compiles its undefined-
  behaviour checks as trap instructions, so a trap *can* mean undefined behaviour in the code, and that
  is exactly how a real defect was found on this target in August. Until someone reads the trap's
  address, this row says nothing either way.
- **hexagon** executed here in September: PLATFORMS.md records both driven turns on all 19 runnable
  triples, which includes it. Now its unit suite stops after the first suite name under both
  toolchains, so the change is in the tree or the emulator, not in zig. Not yet classified.
- The x86_64 control row settles two things: the warning of Finding 4 is zig 0.17.1's, and the
  `/proc` failure of Finding 2 does not depend on the toolchain.

## Finding 4 — a toolchain upgrade breaks `-Werror` for one kind of target only

The operator made zig 0.17.1 (released 2026-11-02) the bench's default compiler driver on 2026-11-12.
Under 0.17.1, building for the **host's own** architecture prints a new warning:

```
warning: future releases of the clang compiler will prefer GCC installations containing libstdc++
include directories; '/usr/lib/gcc/x86_64-linux-gnu/13' would be chosen over '.../14'
[-Wgcc-install-dir-libstdcxx]
```

The development machine has GCC 13 and 14 installed side by side, and under `WERROR=1` the warning
fails the build on its first file. zig 0.16.0 does not print it. The ARM cross-builds, which find no
host GCC for their target, are clean under 0.17.1. This is a toolchain finding, not a jichi defect,
but it decides whether the documented static x86-64 build works with the new default.

## Finding 5 — a unit check that timed the machine, and a target that hid a tier

**Symptom.** On the Athlon II X3 box (Ubuntu 26.04.1, gcc 15.2, glibc 2.43) the unit suite failed one
check: `FAIL tests/test_bg.c:318: cap_exceeded_while_running`. Read cold, that says jichi's background
spool ignored its size limit.

**What was measured.** The check starts a shell writer — 130 rounds of `yes MARK | head -c 4096` with a
5 ms sleep, about 520 KiB — and polls a **fixed** 4 seconds for the spool file to pass 512 KiB. Timed
on its own:

| machine and tools | the writer takes |
|---|---|
| development workstation | 0.88 s |
| Athlon, GNU coreutils | 1.88–1.98 s |
| Athlon, uutils coreutils 0.10.0 (Ubuntu 26.04's default) | **4.85–6.43 s** |

Ubuntu 26.04 replaced GNU coreutils with uutils, the Rust rewrite, and on this older CPU its per-process
start-up costs about two and a half times as much. 390 processes later, the writer is still writing
when the 4-second window closes. **The product was fine; the test measured the machine.** The smoke
tier would have scaled its deadline by the board's measured multiplier (8 here) and said "deadline"
rather than "finding". The unit suite has no multiplier, and its failure line cannot say that a window
expired.

**Why it cost more than one check.** `make check-target` is `all test smoke`, and make stops at the first
failing prerequisite, so this one 4-second window kept **all 554 smoke drivers** from running on the
board. They were run separately afterwards (see the results).

**What follows from it** (written up as a card for the team): wait for the condition, not for a
duration; make an expired deadline its own outcome, retried once at twice the time and failing only if
it repeats; scale the unit suite by the same multiplier; let `check-target` run the smoke tier even
when the unit suite fails. The population is small — three fixed poll loops, and about three dozen
shell `sleep` fixtures across six test files — which is why it is worth doing before the next batches touch the
code those tests time.

**And slow disks?** The multiplier is a real build's seconds on the target, so it already includes the
disk's writes. The Pi Zero 2 W, running from an SD card with 415 MB of memory and a multiplier of 12,
passed its whole unit suite. A separate disk factor is only worth adding once a check is *measured*
failing on a slow medium.

## Open: failures that are not explained yet

These are recorded as open because naming a cause without a measurement is how wrong records get
written.

- **`snprintf_fallback` check 15 on aarch64.** The C89 `snprintf` fallback — used only where the C
  library has no `vsnprintf` — is checked against the system's own `snprintf`. One case, a `%n` that
  must write nothing while the arguments after it stay in step, fails on **all three** aarch64 boards,
  with the identical line `jc r=3 out=[%nM] cnt=77; want r in [0,63], cnt==77, ends in tail`. It passes
  on every x86-64 machine. The boards differ in page size (16 KB and 4 KB), distribution (Debian 12 and
  13) and kernel (6.12 and 6.18), so none of those is the cause. The case was added on 2026-10-29,
  after the last board runs. On aarch64 `va_list` is a structure rather than a pointer, which makes the
  fallback's argument walking a suspect, but that is a suspicion, not a finding.
- **`parallel_abnormal_endings` cell 15 on the Pi Zero 2 W.** The cell injects messages into a running
  worker's queue; on the slowest board the queue answered `task 0's queue is full`, in the suite and
  standalone. Timing is the obvious suspect, and an obvious suspect is still unmeasured.
- **`harness_drive` check 7 on the Pi 500.** The harness copies each run's records into a records
  repository, and files under `notes/` are always compressed with `gzip`. On the Pi 500 the planted
  `notes/n.txt` (10 bytes) existed at both snapshots (`17:31:08` and `17:31:19`), and the snapshot's
  `notes/` directory was **empty**: no `.gz`, no plain copy, and no log line. Reading the code shows
  why the loss is silent: when the `gzip` child fails, the helper deletes the half-written target and
  returns, and by design no plain twin is kept. Why `gzip` fails there is not yet measured. A first
  attempt to trace the child did not attach.

## Withdrawn before it was filed

A third smoke failure on the Pi 500, `snapshot_lint` check 15 (*untracked non-ignored files:
`live.json`, `ws/note.txt`*), was **my own artifact**. To get past Finding 1, I re-ran the gate inside
the board's kept directory, after the rig's live turns had written those two files there. In a normal
run the gate comes before the live turns. The lesson is general: a re-run in a directory a tool has
already used measures the tool's leftovers as well as the tree.

## Mistakes made on the day, by the one running the retest

Recorded because a record that hides its author's errors teaches the wrong thing.

- **A failure count that could not see failures.** For about an hour I reported the Pi Zero 2 W's smoke
  tier as having "0 failures so far". I was counting lines that *begin* with `not ok`, and the smoke
  runner prints a failing driver's lines behind a `    | ` prefix. Two notes to the team carried the
  wrong claim; both were corrected, with a dated paragraph each, before anyone acted on them. The lesson
  is the one this page keeps returning to: read the instrument's own verdict, or read its format before
  you write a pattern for it. The bench's own rig made the same mistake the same day: it took the *last*
  `N checks, 0 failures` line in a log, a smoke driver's, and reported a unit suite of seven checks.
- **A re-run in a used directory**: see "Withdrawn" above.
- **A first hypothesis stated too early.** I named qemu's `/proc` emulation as the likely cause of
  Finding 2 before the probe refuted it. It was said as a hypothesis, and the probe was run before
  anything was written down, which is the order that keeps such a guess harmless.
- **A release assumed.** I told the operator the Pi 400 ran Debian 12, from a note made
  in October, before logging in. It runs Debian 13 now. A recorded fact about a machine is a lead, not a
  measurement.

## What this retest does not show

- **No VM row was driven.** FreeBSD's row is Verified at best; no model was called from inside it.
- **qemu-user is an ISA, not a machine.** The sweep says nothing about timing, caches or real devices,
  and its rows link without libcurl.
- **One board per configuration.** The Pi 500 is the only 16 KB-page aarch64 board here, and it is the
  only Debian 12 board. Its `harness_drive` failure did not reproduce on the Zero or the Pi 400, which
  are both Debian 13 with 4 KB pages, so page size and distribution are still tangled for that one. A
  board that differs in only one of them would separate them: for example the Pi 500 booted on its
  4 KB-page kernel.
- **The harness is Linux-only by decision.** Nothing here says jichi-harness *should* run on FreeBSD.
  Finding 3 matters because the shared build compiles the harness everywhere, so harness code can
  break the agent's build on a platform where the harness never runs.

## Lessons, for anyone testing their own C

1. **"It builds here" is one sample.** Three gates on one libc are one sample taken three times.
   Build at least once on a second libc. A native musl build needs no second machine and no emulator.
2. **An included header is not an available header.** If you use a macro, include the header that
   defines it, even when another header already pulls it in today.
3. **Change one variable at a time.** The `/proc` failure had three suspects: the emulator, the
   dumpable flag and the libc. A fifteen-line probe per suspect took minutes and removed two.
4. **An emulator can hide part of a defect.** It fakes some files, and a faked file can behave better
   than the real one.
5. **Read the error, not your filter's idea of it.** A grep for `error` matched `-Werror` in every
   compiler command, twice on the same day, in two different tools.
6. **A compiler upgrade is a new reviewer.** clang 19 read a dead variable that clang 18, gcc 13 and
   gcc 15 accepted. That is a gift if you test with it, and a broken build if a user meets it first.
7. **Re-run in a clean place.** Withdraw what your own tools caused, and say so in the record.

## How to reproduce

The commands, the probes (`seek.c`, `proc-status-probe.c`), the cross-size script and the per-row
logs are kept in the project's internal record for this retest, which names the machines. The public
recipes are the rigs' own headers: `scripts/tier-b-device.sh` (boards), `scripts/tier-v-bsd.sh`
(FreeBSD) and `scripts/tier-v-arch.sh` (the sweep). For the native musl run:

```sh
make clean && make CC="zig cc -target x86_64-linux-musl" HAVE_CURL= run_tests && ./run_tests
```
