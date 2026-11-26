# Why each release retests the platforms, and what a row costs

*For self-learners, junior developers and whoever plans a release. During the harness-port drive the bench
retested jichi's platform rows twice: in a pause on 2026-11-12, and for the 0.14.0 release on 2026-11-13.
Every gate the team used runs on one operating system, and all of them were green. The retests found
defects anyway. This page shows which, why no gate could see them, and what each row costs in hardware,
set-up and hours. When a row needs a re-run is on [`PLATFORM_RETEST.md`](../../PLATFORM_RETEST.md); the
full account of the 11-12 retest is [the retest analysis](../../analysis/2026-11-12-platform-retest.md).
Section 8 corrects the v0.14.1 release texts, which overstated this retest.*

---

## 0. The short answer

"It builds and passes here" is one sample. Every gate the drive used ran on the same machine type: Ubuntu
24.04, glibc 2.39, gcc 13 and clang 18, x86-64. That is three gates in all: the team's fast gate, the
host's pre-gate sweep and the full `make ci`.

On other systems, code that passed all three:
- failed to compile;
- read files wrongly;
- passed garbage to a child process;
- or timed out in its own tests.

None of those defects was exotic. Each came from one of five ordinary mechanisms (section 2), and each
would have reached a user on that system.

## 1. What the 2026-11-12/13 retests found

All three Ubuntu gates were green on every commit below.

| defect | found on | why the Ubuntu gates could not see it |
|---|---|---|
| a test tool used `offsetof` without including `<stddef.h>`, so the smoke tier did not build | Raspberry Pi 500 (Debian 12, glibc 2.36); FreeBSD | Ubuntu's glibc 2.39 supplies the macro through another header |
| reading a `/proc` file seeked to its end to learn its size; musl refuses that | the 20-architecture sweep and a native x86-64 musl build | glibc allows it and reports size 0 |
| a variable that is only incremented | FreeBSD (clang 19.1.7) | clang 18, gcc 13 and gcc 15.2 do not warn |
| `SIGXFSZ`, `struct ifreq` and a Linux-only function used without a guard | FreeBSD | Linux headers expose them by default |
| an `argv` array whose last slot was never set to NULL | Raspberry Pi 500 (aarch64) | on x86-64 that slot happened to hold NULL |
| a `va_list` passed by value, after which C89 leaves the caller's list indeterminate | every board that is not x86-64 (five) | the x86-64 calling convention hides it |
| a unit test that waited a fixed 4 s for a writer | an Athlon with Ubuntu 26.04's new coreutils (the writer took 4.85-6.43 s) | the workstation's writer takes 0.88 s |
| unit tests with fixed 2 s wait windows | LicheeRV Nano (one core, 229 MB) | a fast machine always finishes inside them |
| two functions jichi declares itself (`fchdir`, `environ`), which gcc's `-Wredundant-decls` rejects there | NetBSD, illumos | only there do the system headers declare them too |
| unit tests that assumed Linux and glibc (the `/proc` formats, how leniently an IPv4 address is parsed) | NetBSD, illumos | the assumptions are true on the gate machine |
| a feature-test level too low for `mkdtemp` | FreeMiNT (Atari) | the Makefile passes that lower level only on FreeMiNT |
| an old glibc's `<time.h>` and an old libcurl constant | Debian 9 and the old-userland containers | Ubuntu 24.04 has the new ones |
| signal names DOS does not have | FreeDOS (DJGPP) | Linux has them |

The fixes landed with 0.14.0, in 12 milestones between M1162 and M1177. Each row also left failures nobody
had classified yet. Those are listed as known issues in `CHANGELOG.md` [0.14.0]: for example OpenBSD 16 unit
checks and 24 smoke drivers, and FreeDOS 154 unit checks.

## 2. Five mechanisms, so you can expect them

1. **A header that arrives by accident.** You use a name without including its header, and it works
   because another header you include happens to include it. Another C library does not.
2. **The same call, a different library.** POSIX leaves room, and C libraries use it: musl refuses a seek
   that glibc allows.
3. **A different compiler version.** A newer compiler adds a warning. Under `-Werror` a warning is an
   error, so a tree that built yesterday does not build with the next compiler.
4. **Undefined behaviour that one machine hides.** An uninitialised slot that happens to be zero, or a
   `va_list` reused after it was passed on, works on x86-64 by luck.
5. **A test that timed the machine.** A wait of a fixed number of seconds is a benchmark of the machine it
   was written on. A slower board, or a slower tool on a fast one, fails it.

A gate on one system can catch none of these, however many checks it runs. Only another system can.

## 3. What a row is, and when it counts

A row in [`PLATFORMS.md`](../../PLATFORMS.md) is one system: an operating system and version, a C library,
a compiler and an architecture. It is measured at one commit.
- **Verified** means the full gate ran there and passed.
- **Partly verified** means some of it did.
- **Driven** means a real model was driven there through a text turn and a tool-calling turn whose result
  it had to use. Every gate is offline, so a row is not tested until it is driven.

A row is a dated fact about a commit. It does not expire, but it speaks for less of the tree as the tree
moves on. [`PLATFORM_RETEST.md`](../../PLATFORM_RETEST.md) says which changes put a row in doubt.

## 4. What each row cost on 2026-11-12/13

Times run from the rig's logged start to its results file. Several rows ran at the same time on the same
workstation, so the virtual machines' times include load from the others. "×N" is the smoke tier's timeout
multiplier for that machine: its build time divided by the workstation's, rounded up.

**Physical boards.** Each needs the board, a network path from the workstation, and the rig's ssh key on it.

| row | hardware | set-up it needs | build | one full run | result |
|---|---|---|---|---|---|
| Raspberry Pi 500 (aarch64, 16 KB pages, Debian 12, gcc 12.2) | 4 cores, 16 GB | network | 16.4 s (×2) | 41 min | 37,985 / 0 unit; 554 of 555 smoke; driven |
| Raspberry Pi 400 (aarch64, Debian 13, gcc 14.2) | 4 cores, 3.8 GB | network | 49.3 s (×5) | 57 min | 37,960 / 0; 553 of 554; driven |
| Raspberry Pi Zero 2 W (aarch64, Debian 13) | 4 cores, 415 MB | a USB network link; a password for sudo | 117.1 s (×12) | 1 h 21 min | 37,960 / 0; 552 of 554; driven |
| VisionFive 2 (riscv64, Debian 13, gcc 14.2) | 4 cores, 7.9 GB | network; its clock is wrong for about a minute after boot | 107.0 s (×11) | 1 h 9 min | 37,960 / 0; 553 of 554; driven |
| LicheeRV Nano (riscv64, Debian sid, gcc 13.2) | 1 core, 229 MB | a USB network link, the clock set by hand (no RTC), a route through the workstation | 284 s (×28) | 2 h 34 min | 37,966 / 4; 548 of 555; driven |
| Athlon II X3 (x86-64, Ubuntu 26.04.1, gcc 15.2) | 3 cores, 7.4 GB | network | 77.4 s (×8) | not recorded: the rig stopped at the unit failure after 9 min | 37,960 / 1; 553 of 554 run alone; driven |

**Virtual machines and emulators on the workstation.**

| row | how | set-up it needs | one full run | result |
|---|---|---|---|---|
| FreeBSD 15.1 (clang 19.1.7) | KVM guest | `gmake` | 47 min | 37,963 / 2; 530 of 555; not driven |
| NetBSD 10.1 | KVM guest | `gmake`, `pkg-config`; a local model through a tunnel | 56 min | 37,974 / 2; 536 of 555; driven |
| OpenBSD 7.9 | KVM guest | `gmake`; a local model through a tunnel | 56 min | 37,963 / 16; 531 of 555; driven |
| illumos (OmniOS r151058) | KVM guest, 4 CPUs, 4 GB | gcc 14 and `gmake` from the package system | not recorded: the smoke tier printed no summary | 37,983 / 7; driven |
| Debian 12 in 256 MB, one core | KVM guest | — | 4 min, the gate stopped early | 37,980 / 0; driven |
| Debian 9 (gcc 6.3, glibc 2.24) | KVM guest, 512 MB | — | 2 min, the gate stopped early | 37,996 / 0; driven |
| Guix System 1.5.0 | QEMU | — | 2 min; no smoke tier | 37,900 / 0; driven |
| seven old userlands (Debian 5 to 9, CentOS 6 and 7) | containers | the images | 5 min for all seven | 37,898 / 0 each |
| FreeMiNT (Atari) | the ARAnyM emulator | a cross compiler | 2 min | 37,859 / 42 |
| FreeDOS 1.3 (DJGPP) | QEMU | a cross compiler | 3 min | 37,692 / 154 |
| 20 architectures, musl | `qemu-user`, built with `zig cc` | zig | 17 min for all 20 | 17 at 37,980 / 0; 3 could not run |
| x86-64 musl, native | no emulator | zig | the unit run took 14 s | 37,985 / 0 |

The unit counts differ between rows because some checks compile only where their feature exists.

**The total.** The rows with a recorded time add up to about 10 hours of machine time, without the four
rows marked "not recorded". They ran in parallel: boards side by side, virtual machines beside them on the
workstation. So the wall-clock was set by the slowest row, the LicheeRV Nano at 2 h 34 min. Some rows ran
more than once, on each candidate commit of the release batch. The Pi 500 and FreeBSD ran four times each.

## 5. What the 0.14 retest did not cover

`PLATFORMS.md` states it per row:
- **Windows** (WSL2, Cygwin, MSYS2), **macOS** and **Android** need the operator's own machines. They were
  not re-run, and their rows stand as last measured.
- **The Hurd and Haiku** need a session by hand. They were not run.
- **Four boards ran, but not on the 0.14.0 code.** The Pi Zero 2 W, the Pi 400, the VisionFive 2 and the
  LicheeRV Nano ran on 2026-11-12/13 on earlier commits, and section 4 quotes those runs. Their rows in
  `PLATFORMS.md` stand at their own, older commits.
- **Older rows** were not re-run either: the 32-bit Pi Zero 2 W rows, the Arduino UNO Q, s390x, Debian 12
  in 160 MB and the fully static musl builds.

A retest is only as wide as the machines on the bench. Say which rows ran.

## 6. Cheaper checks between full retests

A full retest is a day's work. These checks catch a good part of what it finds, in minutes. They are
proposed for every batch before it is marked ready, not yet required:

| check | catches | cost |
|---|---|---|
| build and run the unit suite natively for x86-64 musl (`zig cc`, no emulator) | mechanism 2, much of 1 | about a minute with the build; the unit run itself took 14 s |
| compile the batch's new C with every compiler the gate uses | mechanism 3 | minutes |
| the 20-architecture emulated sweep | mechanism 4 for byte order, word size and alignment | 17 minutes |
| one board of another architecture on the candidate commit | mechanisms 4 and 5 on real hardware | 41 minutes on the Pi 500 |

## 7. Planning a release

- **Before each release, retest every row the bench can reach.** In this drive that took one evening and
  one night.
- **Run one board on the candidate commit.** It found the `argv` defect that the first retest had only seen
  as a symptom.
- **At each pause of a long project, retest one other C library at least.** The two defects of 2026-11-12
  had been green through three gates and two landings.
- **Budget for what the retest will find.** This one produced 12 milestones of fixes and a list of known
  issues.
- **Write down what was not run.** A row that was not retested is not a failure. A release note that says
  it was is one.

## 8. Correction, 2026-11-16: the v0.14.1 release texts overstate this retest

*This section corrects five passages in the v0.14.1 tag annotation and in the message of the commit the tag
points to. The drive's supervisor wrote both texts, and they were published on 2026-11-15. A published
commit or tag is not rewritten, because other clones already hold it. The tag therefore stays as it is,
and the correction is made here and in the next release's notes in `CHANGELOG.md`.*

**What the texts say.**
- The first line of the tag annotation: "every platform row is retested".
- The body of the tag annotation: "Every platform row was retested, with fixes for musl, the C89 snprintf
  fallback, NetBSD, illumos, FreeMiNT, FreeBSD and the old glibc and FreeDOS rows."
- The commit message:
  - "the rest answers a retest of every platform row";
  - "Every platform row was retested.";
  - "Every docs/PLATFORMS.md row carries its 2026-11-13 verdict (M1181)."

**What is true.**
- Apart from the development box, whose gate runs on every commit, no row was run for 0.14.1. Its platform
  rows are 0.14.0's re-measure of 2026-11-12/13.
- That re-measure did not reach every row (section 5):
  - Windows, macOS and Android were not re-run;
  - the Hurd and Haiku were not run;
  - four boards ran only on earlier commits;
  - several older rows were not re-run.
- Ten rows of the Verified table in `PLATFORMS.md` carry no entry of their own from 2026-11-12/13:
  - the older rows of section 5: the two 32-bit Pi Zero 2 W rows, the Arduino UNO Q, s390x, Debian 12 in
    160 MB and the three fully static musl builds;
  - the two architecture sweeps, M469 and M542.

  The 20-architecture sweep did run in this retest (section 4), but its result is recorded on the
  development box's row.
- `PLATFORMS.md` and `CHANGELOG.md` [0.14.0] state the scope correctly. The changelog says the matrix was
  "re-measured on 2026-11-13 at the commits PLATFORMS.md names". Only the two release texts overstate it.

**What the texts should have said.** "The rows the bench could reach were re-measured for 0.14.0 on
2026-11-12/13. `docs/PLATFORMS.md` says for each row what ran, on which commit, and what did not."

**How it happened.** The texts summarised the platform work from memory. They did not copy the scope from
the changelog, which had been checked. The rule since then is that a release text takes the scope of each
claim from the changelog, in the changelog's words, with the date of the measurement.

## Sources

- [`PLATFORMS.md`](../../PLATFORMS.md), the matrix and the per-row evidence;
- [`PLATFORM_RETEST.md`](../../PLATFORM_RETEST.md), when a row needs a re-run;
- [`VERIFY_A_PLATFORM.md`](../../VERIFY_A_PLATFORM.md), how to run one;
- [the retest analysis](../../analysis/2026-11-12-platform-retest.md), the 11-12 retest finding by finding;
- [a va_list passed by value](../../analysis/2026-11-13-a-va-list-passed-by-value.md), one of the defects
  told in full;
- `docs/plans/2026-11-13-platform-widening.md`, the plan for the failures still unclassified.
