# The fork-less rows, after M832: FreeDOS + DJGPP, AROS, NuttX

*Written 2026-09-29, during M832's gate run; the order below was chosen by the operator the same
day ("proceed in the recommended order"). The rows are the three the
[twelve-questions plan](2026-09-24-twelve-questions.md) parked "after the fork-free design", and
M832 is that design. Every row starts the same way: provision, run `make -k`, save the log, and
report the gap list before fixing anything (twelve-questions §4). §4 below was written, and
committed, before the step it predicts was run.*

## 1. What M832 changed for these rows

M769 measured that eight of jichi's ten `fork()` sites fork only to run another program. Since
M832 all eight, and `jc_proc_popen`, describe their child in one `jc_spawn_req` and call one
function. A row without `fork` therefore needs **one backend function** in `src/util/jc_spawn.c`,
not eight call-site rewrites, and two drivers already state the contract any backend must meet:
`tests/smoke/spawn_backend.sh` (the loop closes, the key is scrubbed, SIGPIPE is at its default, a
timed-out pipeline dies as a group) and `tests/smoke/child_fds.sh` (no descriptor above stderr).
The parallel pool and the daemon still fork jichi's own code (DEFERRED def-34). On these rows they
are off, which M769 measured survivable.

## 2. The three rows, in the order chosen

| | 1. FreeDOS + DJGPP (QEMU i386) | 2. AROS (x86, QEMU) | 3. NuttX (`sim` first) |
|---|---|---|---|
| How a program starts | `spawnvp(P_WAIT)`: **synchronous**; `fork` is declared and always fails | `CreateNewProc` / `SystemTags` (dos.library); whether the POSIX layer's `vfork`/`exec` is usable is to be measured | `posix_spawn` exists, so M832's second backend applies unchanged |
| The jc_spawn backend | new: synchronous spawn, output through a temp file, a pid table for `waitpid` | new: an Amiga process launch | none |
| Networking to a model | Watt-32 over a packet driver; curl documents a DJGPP build, to be measured on current curl | bsdsocket.library (AROSTCP); libcurl availability to be measured | BSD sockets; **no upstream libcurl**: the wall |
| TLS | not needed: LM Studio at 10.0.2.2:1234 over plain HTTP, as the emulated-architecture rows do | same | same |
| Toolchain | a DJGPP cross-gcc on the workstation, cross-built like FreeMiNT (M722) | native gcc on an AROS distribution, or the Linux-*hosted* AROS build, to be measured | NuttX's own build (kconfiglib), jichi as a built-in app |
| Provisioning | headless: cross-build here; FreeDOS in QEMU later | GUI-first like Haiku unless the hosted build works headless | headless: `sim` is a Linux process |
| Concurrency features | background commands, MCP stdio and LSP refuse as tool errors: a single-tasking OS cannot run them | to be measured per backend | probably available (tasks) |
| What it teaches | the hardest case for jc_spawn's contract: a synchronous launch with no pipe | the Amiga API; a second non-POSIX process model | the RAM floor for real (jichi's own peak 1,668 KB, [LOW_MEMORY.md](../LOW_MEMORY.md)) |

**Why this order.** FreeDOS + DJGPP is the only row that is fully headless from the workstation
today, and it is the case jc_spawn's contract is least sure to fit. If the contract bends, it is
better learned before the Amiga backend is written. AROS comes second, with the hosted build tried
first so that provisioning might stay headless. NuttX comes third, as the research row it was
always labelled: its wall is libcurl, not jc_spawn.

## 3. FreeDOS + DJGPP: the toolchain

*Provisioned 2026-09-29.*

- **Compiler:** build-djgpp v3.4 (2023-06-12), the prebuilt `djgpp-linux64-gcc1220.tar.bz2`:
  gcc 12.2.0 targeting `i586-pc-msdosdjgpp`, with DJGPP's own C library. 80,596,981 bytes, the
  size the release API states, SHA-256
  `8464f17017d6ab1b2bb2df4ed82357b5bf692e6e2b7fee37e315638f3d505f00`. **The release publishes no
  checksum, so that hash is ours**: it pins what this row was measured with, and nothing upstream
  vouches for it. The trust is the same as the FreeMiNT row's PPA compiler, a binary from the
  project that builds it.
- **Where:** `~/.local/opt/djgpp/`, outside the repository, no root.
- **Not yet provisioned:** Watt-32 (the TCP/IP stack), a DJGPP libcurl, FreeDOS itself. Step 0 is
  a cross-compile and needs none of them. The build is **curl-free** (`HAVE_CURL=`), which is the
  configuration Tier V row V0 and every static recipe already use.

## 4. Step 0: the predictions, written before the run

Step 0 is `make -k` of a `git archive` of M832 with the DJGPP compiler, curl-free. Nothing is
executed, because nothing here can run a DOS program yet.

| # | Prediction | Confidence |
|---|---|---|
| 1 | A C89 hello-world compiles and links to a DJGPP COFF `.exe`, before any probe answer is read. | high |
| 2 | The **largest** gap class is header gating: DJGPP's headers predate `_POSIX_C_SOURCE` and gate POSIX declarations on `__STRICT_ANSI__` / `_POSIX_SOURCE`, so under `-std=c89 -D_POSIX_C_SOURCE=200112L` some POSIX names and types will be hidden, as MiNTLib hid `pid_t` and `struct timeval`. | medium |
| 3 | **Sockets are absent**: no `<sys/socket.h>`, `<netinet/in.h>` or `<sys/un.h>` without Watt-32, so every translation unit that opens a socket fails to compile — the control channel, the daemon, the HTTP-free net helpers, `sockq`. The socket-library probe answers UNRESOLVED. | high |
| 4 | `fork` is **declared and links**; the gap is at run time, not here. The `posix_spawn` probe answers no, so jc_spawn builds with the fork backend only. | high |
| 5 | `mmap` is absent, and the `JC_NO_MMAP` fallback (M141) applies without a new gap; `clock_gettime` is absent, and its fallback applies too. | high |
| 6 | `termios`, `select` and `struct timeval` compile: DJGPP ships all three for its console. | medium |
| 7 | `pipe()` is declared; whether it works at run time is a step 1 question. | low |
| 8 | Nothing in step 0 sees DOS's **8.3 file names**: jichi's long state paths are a run-time question for step 1, with or without a long-file-name driver. | high |

**The stop condition, stated before the run:** if the gaps are guardable items plus missing
subsystems that already have a fallback or can refuse cleanly, go on to fixing them. If the tree
needs a platform layer, say so and stop.

## 5. Step 0: the outcome, against §4

*Run on 2026-09-29 from a `git archive` of `611defa5` (M832 and this plan), so the working tree was
never built in. Every log is in `~/.cache/jichi-tier-v/djgpp-step0-20260929-002531/`, with
`results.txt` as the summary. Curl-free, nothing executed.*

**The instrument held.** A C89 hello-world linked to *"MS-DOS executable, MZ for MS-DOS, COFF for
MS-DOS, DJGPP go32 DOS extender"* before any probe answer was read (prediction 1).

**The build as shipped failed in one second, on the build system and not the source**, as
FreeMiNT's did. All 322 compiles died on `/bin/sh: Syntax error: "(" unexpected`. The hardening
probe had put a sentence into the flags: `HARDENFLAGS` read `… -fPIC ignored (not supported for
DJGPP) -fPIE`. DJGPP's gcc prints that notice on **stdout** and exits 0 even under `-Werror`, and
`cc_warn_ok` discarded only the compiler's stderr, so the notice joined the probe's own `echo` in
the captured value. It is not DJGPP's defect: **any** compiler that writes a diagnostic to stdout
would poison every probed flag list. Nothing predicted it.

**Then, one workaround at a time in the scratch copy:**

| # | Prediction | Measured |
|---|---|---|
| 2 | header gating is the largest class | **Confirmed, and wider than predicted.** DJGPP's headers put *all* of POSIX inside `#ifndef __STRICT_ANSI__`, and `-std=c89` defines it, so strict C89 means no POSIX at all: `pid_t`, `mode_t`, `ssize_t`, `struct stat`, `DIR`, `struct passwd`, `O_CREAT`, `S_IRUSR`, `ENOENT`, `WNOHANG`, `SIGKILL`. 14 of the 16 failing files, and three **probes** read *absent* for the same reason — `vsnprintf`, `lstat` and `TIOCGWINSZ` — so a jichi built that way would have used the fallback formatter with no width or precision. One flag, `-U__STRICT_ANSI__`, clears all of it while the compiler stays in `-std=c89 -pedantic`: the macro only tells headers what to show. DJGPP predates `_POSIX_C_SOURCE`; its headers know only `__STRICT_ANSI__` and `_POSIX_SOURCE`. |
| 3 | sockets absent, three kinds of file fail | **Confirmed**: no `<sys/socket.h>`, the socket probe UNRESOLVED, three files — the control channel, the daemon, and `jc_http.c`, whose SSRF address classifier (M190) needs the socket headers even in a curl-free build. |
| 4 | `fork` declared and links; no `posix_spawn` | **Confirmed**, both. |
| 5 | `mmap` and `clock_gettime` absent, with fallbacks | **Confirmed**, both fallbacks apply. **Not predicted:** `nanosleep` and `struct timespec` are absent too (POSIX makes `nanosleep` optional), and `jc_sleep_ms` uses both. |
| 6 | `termios`, `select`, `struct timeval` compile | **Half wrong.** All three compile once the gating is cleared, but **`<sys/select.h>` does not exist**: DJGPP declares `select()` in `<time.h>` and `fd_set` in `<sys/types.h>`, the placement before POSIX 2001 added the header. Ten files include it. |
| 7 | `pipe()` declared | **Confirmed**: it links. Whether it works is step 1's question. |
| 8 | 8.3 names invisible here | **Confirmed**: nothing in step 0 could see them. |
| — | *(not predicted)* | `SIGCHLD` does not exist (the daemon's worker reaping), and neither does `_SC_PAGESIZE` (the Linux `/proc` memory reading). |

**The link, and a second build-system defect.** With every compile passing, the link failed on
**5,397 references to `__stack_chk_guard` and `__stack_chk_fail`**. The probe accepts
`-fstack-protector-strong` correctly, because gcc's driver adds the runtime (`libssp`) when the flag
is on the command line, and the probe compiles and links in one step. The build does not: the flag
is in `CFLAGS` only, so the link line never carries it. On glibc and musl the runtime lives in libc
and the omission is invisible; on DJGPP it is fatal. Measured both ways on one object: linked
without the flag, the two symbols are undefined; with it, rc 0. It is the compile-and-link pairing
the Makefile already gets right for `-fPIE` and `-pie`, missing for this one flag.

With the flag also on the link line, **the link lacked exactly seven symbols**: `socket`, `bind`,
`listen`, `accept`, `connect`, `inet_pton` and `nanosleep`. Everything else jichi calls, DJGPP's
libc, libm and libgcc provide. Relinked against a stub in which those seven fail with `ENOSYS`, both
binaries link: `jichi.exe` at 1,902,599 bytes (1,582,104 of text) and `run_tests.exe` at 2,840,642,
both go32 executables. They contain the shims, so they prove the link, not the tree.

**The stop condition's answer: go on.** Two build-system defects that are not DJGPP's, one header
knob, three guardable absences, and one missing subsystem (sockets) that the control channel and the
daemon can refuse cleanly. No platform layer is needed. The rows are in
[`DEFERRED.md`](../DEFERRED.md) under *"found by the DJGPP step 0"* (closed at M835), and the row is in
[`PLATFORMS.md`](../PLATFORMS.md) as **Never compiled**: a compiler *for* DOS has seen the tree, and
none *on* it has.

**The order of the fixes, as FreeMiNT's went.** The two build-system defects first, because they are
everyone's (M722's shape). Then the DJGPP header knob and the three absences, tree-wide (M723's
shape). Then sockets, which is a design question: AF_UNIX will never exist on DOS, so the control
channel and the daemon must refuse, while Watt-32 can bring AF_INET back for the model connection.

## 6. After step 0: the fixes, and where the row stands

*Updated 2026-09-29, at M835.* The fixes went in the order §5 set: the build-system defects first,
because they were everyone's; then the header knob and the absences, tree-wide; then sockets.

| Milestone | Closed | What changed |
|---|---|---|
| M833 | def-263, def-264 | the probes read a compiler's exit status, not its stdout; the stack protector reaches the link line |
| M834 | def-265, def-266 | `-U__STRICT_ANSI__` as a probed header knob; fallbacks for `<sys/select.h>`, `nanosleep`, `SIGCHLD`, `_SC_PAGESIZE` |
| M835 | def-267 | `JC_NO_SOCKETS`: the daemon and the control channel refuse, and the SSRF classifier reads IPv4 itself |

**Where the row stands.** DJGPP 12.2.0 now builds a fresh copy of the tree with no shim of any kind.
It compiles 322 of 322 objects with 0 warnings and 0 errors and links `jichi.exe` (1,890,240 bytes)
and `run_tests.exe` (2,841,399 bytes). As §5's stop-condition answer said, no platform layer was
needed. Measuring the build also found two warnings that M834's count of errors had not included, in
an M832 unit test that nothing had compiled without `posix_spawn`. They are fixed, and the gate now
compiles every file with each detected feature taken away. Nothing has run on DOS yet, so the row is
still **Never compiled** in [`PLATFORMS.md`](../PLATFORMS.md).

**Next, in order** ([`DEFERRED.md`](../DEFERRED.md) def-268). The rig comes first, because it needs
nothing new: it is what turns "nothing has run" into a measurement, as FreeMiNT's did (M726).

1. A FreeDOS rig in QEMU, in FreeMiNT's order: first an unattended boot that reports back to the
   host, then the cross-built unit suite. DJGPP's `crt0` reads a stack size the program may set
   (`_stklen`), the same kind of fixed stack that FreeMiNT's unit suite ran off the end of. How
   large it is for `run_tests.exe`, and whether that is enough, is for the rig to measure. The
   suite's failures there are also the measurement the next step needs: which tests need `fork`.
2. The two things a driven turn needs. A synchronous `jc_spawn` backend: DOS runs one program at a
   time, so the child finishes before the launch returns. And Watt-32 with a DJGPP libcurl, which
   also splits `JC_NO_SOCKETS`: Watt-32 brings `AF_INET` back and never `AF_UNIX`, so the daemon
   and the control channel will key on `<sys/un.h>`, while the SSRF classifier and libcurl get their
   TCP headers.
3. A driven model turn, which is what moves the row off **Never compiled**.

## 7. The FreeDOS rig: something runs on DOS (M836)

*2026-09-29.* §6's first step, done by hand first and then written down as
[`scripts/tier-v-freedos.sh`](../../scripts/tier-v-freedos.sh), whose runs reproduced the hand runs
exactly. The guest is FreeDOS 1.3's LiteUSB image, pinned by the SHA-256 that the release publishes.
The rig replaces the image's `FDAUTO.BAT`, on a copy, with its own script. `CTTY COM1` puts the
console on the serial port, which QEMU writes to a file, and `FDAPM POWEROFF` ends the run. The rig
copies files in and reads results back with GNU mtools, so nothing needs root. The DPMI host that
DJGPP programs need comes from the image's own package (CWSDPMI r7).

**Step 1: an unattended boot that reports back.** 12 s from power-on to power-off, under KVM. The
sentinel carrying the run's id is on the serial port and on the guest's disk. A 2-second deadline
kills the guest before the sentinel, and the rig then exits 3 (not a result), never 0.

**Step 2: the unit suite on DOS.** The cross-built `run_tests.exe` (from `git archive`, `WERROR=1`)
ran to the end with DOSLFN loaded: **14,072 checks, 108 failures**, in 104 s. The hand run gave
14,071 and the same 108 failing lines. Nothing crashed, and DJGPP's default stack held. **`jichi
--version` printed `jichi 0.12.0` and exited 0: the first time the agent has executed on DOS.**
`jichi describe` exited 0 too, and its text still offered `jichi daemon` on this build, which M835
had made refuse. M836 marks the daemon and the control channel unavailable in the contract of a
socket-less build.

Every failing line was read. The causes:

| Cause | Failures | Row |
|---|---|---|
| Starting another program: no `fork`, no POSIX shell | 78 | [def-269](../DEFERRED.md#def-269) |
| Paths and file-system semantics: drive letters, a disk query per drive, emulated symlinks | 18 | [def-271](../DEFERRED.md#def-271) |
| No permission bits | 6 | [def-272](../DEFERRED.md#def-272) |
| Text mode: a `fopen` without `"b"` writes CRLF | 3 | [def-270](../DEFERRED.md#def-270) |
| The date line under `TZ=UTC`, not yet isolated | 2 | [def-273](../DEFERRED.md#def-273) |
| A test that assumes a process 1 | 1 | [def-269](../DEFERRED.md#def-269) |

**Without DOSLFN** (plain 8.3 names; the rig's option for it is in the script's header), the same
suite fails **271** checks. All 163 extra failures are file names, 67 of them in sessions, and a
name like `.jichi.d` cannot exist in 8.3 at all. jichi on DOS needs long file names
([def-274](../DEFERRED.md#def-274)).

**The clock, measured with a probe booted on the row.** `time_t` is 32 bits, so the row ends in
January 2038. With `TZ` unset, `localtime()` placed 2026-08-10 on 2026-03-24, 139 days off; with
`TZ=UTC`, `UTC0` or `GMT0` it was right ([def-273](../DEFERRED.md#def-273)). The probe also showed a
comment in `test_sysmsg.c` to be wrong: its timestamp is midnight UTC, not 06:40, and M836 corrects it.

**Next**, from §6: the synchronous `jc_spawn` backend, which 78 of the 108 failures wait for, and
Watt-32 with a DJGPP libcurl. Then a driven model turn, which is what moves the row off **Never
compiled**.

## 8. After the rig: three small classes closed (M837–M839)

*2026-09-29.* The three failure classes that did not need a new subsystem, in the order the
rig's table suggested. Each fix was measured on DOS through the rig's `--dirty` mode before
it was committed.

| Milestone | Closed | What changed | DOS unit failures |
|---|---|---|---|
| M837 | def-270 | every file opened binary; `portability_lint` 37–38 hold it | 108 → 105 |
| M838 | def-274 | a long-file-name check in `doctor`, and a warning before state is written | 105 (no test covered it) |
| M839 | def-273 | a broken time zone repaired before the first timestamp | 105 → 103 |

**What is left** is 103 failures in the three classes that do need design work: starting
another program (79, def-269), paths (18, def-271) and permission bits (6, def-272). The
design for the first, for review, is
[`2026-09-29-running-programs-on-dos.md`](2026-09-29-running-programs-on-dos.md). The
learner's account of all of it, from the cross-build to the time zone, is
[`platform-notes/freedos.md`](../platform-notes/freedos.md).
