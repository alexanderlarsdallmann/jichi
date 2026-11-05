# FreeDOS / DJGPP — the porting record, and what DOS teaches a C programmer

*Written at M839 for a reader who has never used DOS. Every number here was measured
with the rig, [`scripts/tier-v-freedos.sh`](../../scripts/tier-v-freedos.sh), and each
one is recorded in the [ROADMAP](../ROADMAP.md) entries M833 to M839. The plan the row
follows is [`plans/2026-09-29-the-fork-less-rows.md`](../plans/2026-09-29-the-fork-less-rows.md).*

## Where the row stands

- **Never compiled on DOS itself.** No compiler running *on* DOS has seen the tree. That
  is the strict meaning of the verdict in [`PLATFORMS.md`](../PLATFORMS.md).
- **It cross-compiles from the tree with no workaround** (since M835). DJGPP, a GCC that
  runs on Linux and produces DOS programs, builds all 322 objects with 0 warnings and links
  both `jichi.exe` and `run_tests.exe`.
- **The unit suite runs in FreeDOS 1.3 under QEMU** (since M836), with **103 failures**
  since M839 (108 at the first run). `jichi --version`, `describe`, `doctor`, `ls` and
  `sysmsg` also run there.
- **No model has been called from DOS.** DOS has no network sockets unless an add-on
  TCP/IP library is linked, and this build has neither that library nor libcurl.

## Reproduce it, step by step

This was run on Ubuntu 24.04 (x86-64). Each step says what you should see. If you see
something else, stop there: later steps depend on it.

1. **Install the emulator and the disk tools.** On Ubuntu they are the packages
   `qemu-system-x86` (which provides `qemu-system-i386`) and `mtools`. Check:

   ```sh
   command -v qemu-system-i386 mcopy
   ```

   You should see two paths. If one is missing, install that package and check again.

2. **Get DJGPP.** The build this row uses is build-djgpp v3.4, a prebuilt GCC 12.2.0. Its
   release publishes no checksum, so the SHA-256 below is the one this project measured
   and pinned:

   ```sh
   mkdir -p ~/Downloads/jichi-toolchains ~/.local/opt
   cd ~/Downloads/jichi-toolchains
   curl -fL -o djgpp-linux64-gcc1220.tar.bz2 https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2
   echo "8464f17017d6ab1b2bb2df4ed82357b5bf692e6e2b7fee37e315638f3d505f00  djgpp-linux64-gcc1220.tar.bz2" | sha256sum -c -
   tar -xjf djgpp-linux64-gcc1220.tar.bz2 -C ~/.local/opt
   ~/.local/opt/djgpp/bin/i586-pc-msdosdjgpp-gcc --version
   ```

   You should see `djgpp-linux64-gcc1220.tar.bz2: OK` and then `i586-pc-msdosdjgpp-gcc
   (GCC) 12.2.0`. If the checksum line says `FAILED`, delete the file and download it
   again. Do not unpack a file whose checksum does not match.

3. **Cross-build, in a copy of the tree.** Build in a copy, not in your checkout: a
   cross-build leaves DOS object files behind, and the next ordinary `make` would try to
   link them. From your jichi checkout:

   ```sh
   D=$(mktemp -d) && git archive HEAD | tar -x -C "$D" && cd "$D"
   PATH=$HOME/.local/opt/djgpp/bin:$PATH make -j8 CC=i586-pc-msdosdjgpp-gcc HAVE_CURL= WERROR=1 jichi run_tests
   file jichi.exe
   ```

   You should see the build finish without an error and `file` report *MS-DOS
   executable, MZ for MS-DOS, COFF for MS-DOS, DJGPP go32 DOS extender*. `HAVE_CURL=`
   builds without libcurl, and `WERROR=1` makes any warning an error, which is how this
   row is held to 0 warnings. If the build stops, the last lines of its output name the
   file and the line.

4. **Boot FreeDOS and report back.** From your checkout again, run the rig's step 1. It
   downloads FreeDOS 1.3's LiteUSB image once (16 MB) and refuses it unless its SHA-256
   matches the one the FreeDOS release publishes:

   ```sh
   TIER_V_DJGPP=$HOME/.local/opt/djgpp/bin sh scripts/tier-v-freedos.sh --step 1
   ```

   You should see `step 1 ok: booted, wrote the sentinel and powered off` after about 12
   seconds. Exit code 2 means a tool is missing, and the message names it. Exit code 3
   means the guest never reported back: that is a failure of the rig, not a result, and
   the serial log it names shows how far the boot got.

5. **Run the unit suite on DOS.**

   ```sh
   TIER_V_DJGPP=$HOME/.local/opt/djgpp/bin sh scripts/tier-v-freedos.sh --step 2
   ```

   After about two minutes it prints the suite's summary line. Today that is exit code 1
   with 103 failures, which is expected; the section at the end lists them by cause. The
   run directory it names holds `results-freedos.txt` (every failing check, and one line
   for each command that ran), `doctor.txt` (`jichi doctor`'s full report from inside
   DOS) and `serial.txt` (the DOS console). `--dirty` builds your working tree instead of
   the last commit, which is how a fix is checked on DOS before it is committed. It ships
   **tracked** files only, so `git add` a new file before a `--dirty` run, or the build on
   the guest will not find it.

## What DOS does differently, and what jichi does about it

Each part below has the same shape: the idea, as a C programmer meets it; what it broke
in jichi, as measured; and what jichi does now.

### 1. Strict C89 hides all of POSIX (M834)

**The idea.** A C library's headers can show or hide declarations depending on
*feature-test macros*. `-std=c89` defines `__STRICT_ANSI__`, which means "standard C
only". `_POSIX_C_SOURCE=200112L` asks for POSIX.1-2001 on top of it. On glibc the second
wins. DJGPP's headers are older than `_POSIX_C_SOURCE` and look only at
`__STRICT_ANSI__`, so under `-std=c89` they hide `pid_t`, `struct stat`, `O_CREAT` and the
rest of POSIX.

**In jichi.** 14 of the first 16 failing files, and three capability probes that
wrongly reported features missing.

**Now.** The Makefile detects the situation and adds `-U__STRICT_ANSI__`. That changes
only what the headers declare: the compiler itself still checks strict C89.

### 2. A compiler that talks on standard output (M833)

**In jichi.** DJGPP's gcc prints a notice on *stdout* and still exits with success. The
Makefile's probes kept a compiler's stdout together with their own answer, so the notice
became part of the compiler flags and every one of the 322 compiles failed.

**Now.** Every probe reads only the exit status. This was a defect for every compiler
that prints on stdout, not only DJGPP's.

### 3. Text mode and binary mode (M837)

**The idea.** Standard C distinguishes *text* streams from *binary* ones: `fopen(p, "w")`
versus `fopen(p, "wb")`. On POSIX systems the two are identical, which is why most C
programmers never notice the `b`. DOS stores a line ending as the two bytes `\r\n`. In
text mode, the C library turns `\n` into `\r\n` when writing, turns `\r\n` back into `\n`
when reading, and stops reading at the first Ctrl-Z byte. Raw `open()` works the same way
unless it is given `O_BINARY`.

**In jichi.** The input history appended with `"a"`, which is text mode, and read the file
back as bytes, so every line came back with a stray `\r`. A scan then found 27 text-mode
opens and 4 raw `open()` calls that relied on text mode being the same as binary.

**Now.** Every file jichi opens is binary: a `b` in every `fopen` mode, and `JC_O_BINARY`
(from `include/jc_fcntl.h`) in every `open()`. `tests/smoke/portability_lint.sh` fails the
build when a new call leaves them out. The console stays in text mode, because a DOS
terminal needs its carriage returns.

### 4. Eight-character file names (M838)

**The idea.** Plain DOS stores names as *8.3*: at most eight characters, a dot, and three
more. It cannot hold a name that begins with a dot. A driver called DOSLFN adds long file
names; FreeDOS ships it but does not load it by default.

**In jichi.** `~/.jichi.d` cannot exist in 8.3, and session files with long names are cut
short until two of them collide. The unit suite fails 271 checks on plain 8.3 names
against 108 with DOSLFN (both counts from M836's run).

**Now.** jichi asks the C library at run time (DJGPP's `_use_lfn()`). `jichi doctor`
reports long file names as ok or FAIL and names DOSLFN as the fix, and a command that
writes state warns once. To load it, put the line `DOSLFN` into `FDAUTO.BAT`.

### 5. The time zone (M839)

**The idea.** POSIX describes a time zone in the `TZ` environment variable. A complete
value has a name and an **offset**, and the offset counts hours *west* of UTC, so it is
the reverse of the usual "+1":

| `TZ` | Meaning |
|---|---|
| `UTC0` | UTC, offset zero |
| `EST5EDT` | US Eastern: 5 hours behind UTC, with daylight saving time |
| `CET-1CEST,M3.5.0,M10.5.0/3` | Central Europe: UTC+1, with CEST from the last Sunday of March (`M3.5.0`, at 02:00) to the last Sunday of October (`M10.5.0`, at 03:00) |

**In jichi.** DJGPP's C library computes garbage local time when `TZ` is unset, empty, or a
name without an offset such as `UTC`. It returned 2026-08-02 05:08:39 for 2026-08-10
00:00 UTC, and the error varied from run to run with the state of memory. Worse, it shifts
the clock itself, `time()`, by the same amount, so every timestamp jichi wrote was off by
days or weeks. FreeDOS sets `TZ` empty by default.

**Now.** At start-up, before anything reads the clock, jichi compares the local time with
UTC. If the difference is not one a real time zone could have (more than 26 hours, or not
a whole number of 15-minute steps), it runs with `TZ=UTC0` and says so, and `jichi doctor`
explains how to set `TZ` for your zone. On a system whose time zone works, nothing changes.

### 6. No sockets (M835)

**The idea.** A socket is how a program talks to a network, or to another program on the
same machine through a *Unix-domain* socket. DOS has neither. TCP/IP comes only from an
add-on library such as Watt-32, and Unix-domain sockets do not exist on DOS at all.

**In jichi.** Three parts used sockets: the daemon and `--connect`, the mid-run control
channel (`--control` and `jichi control`), which both use Unix-domain sockets, and the
check that stops a model from fetching private network addresses.

**Now.** Where `<sys/socket.h>` does not exist, the build defines `JC_NO_SOCKETS`. The
daemon, `--connect` and `jichi control` then refuse with exit code 2 and say to use
`jichi -p` instead. `--control` warns and the run continues, and `doctor` says the build
has no sockets. The address check stays: it reads IPv4 addresses with jichi's own parser,
which the unit suite holds equal to the C library's `inet_pton`.

### 7. One program at a time (not solved yet: def-269)

**The idea.** On POSIX, a program starts another with `fork()` and `exec()`: `fork` copies
the running process, and the copy becomes the new program. Both then run at the same
time, and they talk through *pipes*. DOS runs one program at a time. DJGPP's `fork()`
always fails, and a program can only *spawn* another and wait until it finishes
(`spawnvp(P_WAIT, …)`).

**Measured on DOS.** Spawning and waiting works, with the child's output redirected into a
file. A DJGPP pipe does not work for this: a child wrote 2,000 lines into one and none
could be read back. And `COMMAND.COM`, DOS's shell, loses exit codes: a command that
failed with "Bad command or filename" still reported success.

**Now.** Nothing yet: 79 of the 103 failures are this class. The plan is a third way for
jichi to start programs, alongside its `fork` and `posix_spawn` ones (M832): run the child
to completion, with its output in a temporary file. Commands written for a POSIX shell
also need a real shell on DOS, such as DJGPP's port of bash, or a clear refusal.

### 8. Why a DJGPP program needs CWSDPMI

DOS starts every program in *real mode*, the 16-bit mode of the original 8086, with one
megabyte of addressable memory. DJGPP programs are 32-bit and run in *protected mode*.
A *DPMI host* switches the processor between the two and gives the program its memory.
If none is running, the small DOS stub at the start of every DJGPP `.exe` loads
`CWSDPMI.EXE` from the program's directory or the `PATH`. The rig copies it from FreeDOS's
own `PACKAGES\UTIL\CWSDPMI.ZIP`.

### 9. Smaller differences, recorded

- **No permission bits** (def-272). A DOS file has no owner and no mode, so the private
  files jichi keeps at mode 0600 are readable by anyone with the disk. `doctor` already
  detects this by effect, but its advice is written for MSYS2. 6 failures.
- **Paths with drive letters** (def-271). An absolute path starts `C:/`, and a free-space
  query on a missing path answers for the whole drive instead of failing. 18 failures.
- **A 32-bit `time_t`.** Time on this row ends in January 2038.

## How the rig drives DOS, for someone writing a rig of their own

- **The guest is an unmodified, verified image.** The rig works on a copy of FreeDOS 1.3's
  LiteUSB disk and never on the downloaded file.
- **It replaces one file.** The image's `FDAUTO.BAT`, which DOS runs at start-up, would
  start the installer. The rig writes its own script there with mtools, which reads and
  writes FAT disk images without root.
- **The console goes to the serial port.** `CTTY COM1` makes the serial port the console,
  and QEMU writes that port to a file. This matters because DOS cannot redirect stderr, and
  a console on the serial port loses nothing, even if a program hangs.
- **The machine switches itself off.** `FDAPM POWEROFF` ends the run, and QEMU exits by
  itself.
- **Every run proves it is this run.** The script writes a sentinel carrying the run's id,
  to the serial port and to the guest's disk. A waiter that trusts whatever sentinel it
  finds would read an earlier run's result as this one's.
- **Names must fit 8.3.** `run_tests.exe` is nine characters before the dot, so the guest
  gets `RUNTESTS.EXE`.
- **The disk is small.** About 10 MiB is free after the binaries are copied in, which is
  jichi's disk-guard floor. The rig is enough for the unit suite; a real run will need a
  bigger disk.

## What is left

| Class | Failures | DEFERRED |
|---|---|---|
| Starting another program: no `fork`, no POSIX shell, and one test that assumes a process 1 | 79 | [def-269](../DEFERRED.md#def-269) |
| Paths and file-system semantics | 18 | [def-271](../DEFERRED.md#def-271) |
| No permission bits | 6 | [def-272](../DEFERRED.md#def-272) |

After those come Watt-32 with a DJGPP libcurl, which brings TCP back for the model
connection, and then a driven model turn. That turn is what would move the row off
**Never compiled** ([def-268](../DEFERRED.md#def-268)).
