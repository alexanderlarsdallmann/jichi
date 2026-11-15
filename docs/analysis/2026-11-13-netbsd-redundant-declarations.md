# NetBSD stopped building: two declarations, and a warning only one compiler has

*2026-11-13. Milestone M1173. Written for self-learners, junior developers, and
agents. A companion to
[the va_list page](2026-11-13-a-va-list-passed-by-value.md) from the same day.*

## In three sentences

The NetBSD 10.1 row had last run on 2026-09-19. On 2026-11-13 jichi no longer
built there, because two declarations that jichi writes itself became duplicates
on NetBSD, and gcc's `-Wredundant-decls` turns a duplicate into an error under
`-Werror`. No gate could see it: the gate machines that use gcc compile only one
of each pair, and the rows that compile both use clang, which does not implement
that warning.

## 1. The warning, and who implements it

A declaration says "this exists, with this type". Declaring the same thing twice
in one file is legal C; gcc's `-Wredundant-decls` flags it, and jichi's build
uses that warning with `-Werror`.

Measured on 2026-11-13, with a file that declares `extern char **environ;`
twice:

| compiler | `-Wredundant-decls -Werror` |
|---|---|
| gcc 13.3 (x86-64) | `error: redundant redeclaration of 'environ'` |
| clang 18.1 | accepted, no diagnostic |
| zig cc (clang) | accepted, no diagnostic |

clang accepts the flag and stays silent. The FreeBSD and OpenBSD rows build
with clang, so they cannot report this class at all.

## 2. Duplicate one: `fchdir`

`fchdir` changes the current directory through an open directory descriptor.
jichi builds with `-D_POSIX_C_SOURCE=200112L`, and under that flag glibc's
`<unistd.h>` does not declare `fchdir`. So M1116 (2026-11-02) added its own
declaration in `src/util/jc_spawn.c`:

```c
#ifdef JC_HAVE_FCHDIR
extern int fchdir(int);
#endif
```

**NetBSD declares it once `_XOPEN_SOURCE` is defined, and jichi's Makefile
defines `_XOPEN_SOURCE=600` on NetBSD.** It does so because its `lstat` probe
finds `lstat`, `readlink` and `symlink` hidden under the bare POSIX flags there
(measured in the guest), and the X/Open level exposes them. The Makefile's own
comment had said this probe adds nothing on "the BSDs"; NetBSD 10.1 is the
exception, and the comment now says so. So on NetBSD the
header declared `fchdir`, the extern declared it again, and gcc stopped the
build:

    src/util/jc_spawn.c:43:12: error: redundant redeclaration of 'fchdir' [-Werror=redundant-decls]

**The fix** asks the question the first probe did not. The Makefile already
asked whether `fchdir` *links*. A second probe, `JC_FCHDIR_DECLARED`, asks
whether `<unistd.h>` *declares* it under the build's flags. `jc_spawn.c` adds
its own extern only where the header does not:

```c
#if defined(JC_HAVE_FCHDIR) && !defined(JC_FCHDIR_DECLARED)
extern int fchdir(int);
#endif
```

**The fixer's own mistake, kept here because it is the lesson.** The first draft
of that probe compiled with the bare POSIX flags. It sat next to the link probe,
before the line that adds `_XOPEN_SOURCE=600`. On NetBSD it answered "not
declared", and the build failed exactly as before; this was measured in the
guest.

Run by hand in the NetBSD guest, the probe answered:
- under `-D_POSIX_C_SOURCE=200112L` alone: `implicit declaration of function
  'fchdir'`;
- with `-D_XOPEN_SOURCE=600`: declared.

The probe now compiles with `$(STD) $(POSIX)`, the flags the build itself uses,
and sits after the last probe that can add a feature macro. jichi's own rule
says "a capability probe must ask the right question". Asking it under different
flags than the code is compiled with is the same mistake.

## 3. Duplicate two: `environ`

`environ` is the process environment, a `char **` that POSIX defines but no
header has to declare under jichi's flags. `src/util/jc_proc.c` declared it
twice:

- once inside the `JC_HAVE_ENVIRON_SCRUB` section (M796, 2026-09-27), which
  wipes secret variables in place where nothing else hides them;
- once hoisted to file scope for doctor's environment scan (M1043-a,
  2026-10-22). Its comment even mentions the other declaration ("re-declares it,
  line below").

Linux compiles only the hoisted one: it has no environ scrub. OpenBSD compiles
both, but with clang. NetBSD is the first row that compiles both with gcc:

    src/util/jc_proc.c:314:15: error: redundant redeclaration of 'environ' [-Werror=redundant-decls]
    src/util/jc_proc.c:165:15: note: previous declaration of 'environ' was here

**The fix:** declare it once, at file scope. The hoisted comment also claimed
that `<unistd.h>` declares `environ` under the build's flags. That is false:
glibc does so only for `_GNU_SOURCE`, and NetBSD's headers do not declare it at
all, which was checked in the guest. The comment now says so.

## 4. The proof

**In the NetBSD 10.1 guest (gcc 10.5), with both fixes:**
- `gmake -k WERROR=1 all run_tests`: rc 0, 0 warnings;
- the rig's row: `ok - WERROR=1 build clean on NetBSD (12s)`.

Before the fixes: the two errors above. `make -k` with only the `fchdir` fix
showed that `environ` was the only error left.

**On the x86-64 host:**
- the probe answers "not declared" under jichi's flags, so the extern stays;
- with `-D_XOPEN_SOURCE=700` it answers "declared". The probe can say both.
- gcc `make WERROR=1 test` and the clang `-Werror` build stay clean.

**The unit suite on NetBSD after the fix:** `37975 checks, 2 failures`. Both
failures are test assumptions, recorded here and not fixed in M1173:
- **`tests/test_http.c:346`** compares jichi's strict IPv4 parser with libc's
  `inet_pton`. NetBSD's `inet_pton` accepts `"1.2.3.4 "`, with a trailing space,
  and jichi's parser rejects it. jichi's strictness is the intended behaviour:
  the parser decides what counts as an address for the fetch tool's SSRF fence.
- **`tests/test_platform.c:201`** expects Linux's `/proc/self/status` format,
  with a `Name:` field. NetBSD's procfs has the BSD one-line format: `head 12713
  10970 ...`.

**Fixed the same day, in M1174 (the operator: "Include the fixes in 0.14.0").**
- **The procfs check** now accepts either format, and proves the read in both.
  Linux needs its `Name:` field. NetBSD's one-line status must carry this
  process's own pid as its second field, so an empty or wrong read still fails.
- **The inet_pton comparison** pins jichi's verdict on `"1.2.3.4 "` (rejected)
  on every row. Where libc is the lenient side on an input ending in
  whitespace, it requires the strict answer. Any other disagreement still
  fails.

## 4b. illumos, the same day: the same two errors, and two more test assumptions

**The build.** The illumos row (OmniOS r151058, gcc 14.3) on the unfixed tree
also failed to build. That rig's excerpt showed only compiler command lines, so
the error was first read by hand, in the kept guest, on the old code:

    src/util/jc_proc.c:314:15: error: redundant redeclaration of 'environ' [-Werror=redundant-decls]
    src/util/jc_spawn.c:43:12: error: redundant redeclaration of 'fchdir' [-Werror=redundant-decls]

These are the same two errors as NetBSD. illumos exposes `fchdir` through the
`-D__EXTENSIONS__` the Makefile adds there. With M1173 the tree builds clean:
`WERROR=1 build clean on illumos (15s)`.

**The unit suite** then showed `37984 checks, 7 failures`. They were test
assumptions again, all measured in the guest. M1175 fixes them.

- **The status file is binary.** illumos serves `/proc/self/status` as a binary
  `pstatus_t`: NUL bytes from offset 1, `st_size` 1136. No text format applies.
  But the file has a real size, so the test now requires the read to return all
  of it, which is a stronger check than matching a format.
- **There is no `/proc/cpuinfo`.** That file is Linux's, and NetBSD's procfs
  serves it too. The test reads it only where it exists.
- **`inet_pton` accepts leading zeros.** `01.2.3.4` becomes 1.2.3.4, as do
  `1.02.3.4`, `1.2.3.04` and `00.0.0.0`; illumos reads the zero as decimal.
  - jichi refuses these on purpose: in `inet_aton`-style readers a leading zero
    means octal. An address that two parsers read differently is the shape of an
    SSRF bypass.
  - A known answer already pins `01.1.1.1` as rejected.
  - The differential now tolerates a lenient libc on exactly two input classes:
    trailing whitespace (NetBSD) and a leading-zero octet (illumos). Any other
    disagreement still fails, above all jichi accepting what libc refuses.

**After M1175,** in the illumos guest: `37982 checks, 0 failures`.

## 4c. FreeMiNT, the same day: a per-file macro that deferred to a lower one

The FreeMiNT row (ARAnyM, MiNTLib, cross-built with gcc 4.6.4) had last been
green in September. On 2026-11-13 its cross-build failed:

    src/platform/jc_platform_posix.c:253:13: error: implicit declaration of function 'mkdtemp' [-Werror=implicit-function-declaration]

**Why.** M1134 (2026-11-04) began using `mkdtemp`, which glibc and MiNTLib
declare only at `_XOPEN_SOURCE >= 700` (MiNTLib's `stdlib.h` guards it with
`__USE_XOPEN2K8`; the symbol is in its `libc.a`). The file raised the level
itself:

```c
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
```

But on FreeMiNT, and on NetBSD, the Makefile's lstat probe already passes
`-D_XOPEN_SOURCE=600` on the command line. So the `#ifndef` saw it defined and
left 600. Linux never passes the macro on the command line, which is why every
gate was green. A `make -k` cross-build then showed three harness files failing
the same way (`jc_hp_gateci.c`, `jc_hp_gateref.c` and `jc_hp_gatesweep.c`; each
calls `mkdtemp`).

**The fix (M1176).** Each guard that needs 700 now raises a lower level instead
of deferring to it:

```c
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#elif _XOPEN_SOURCE < 700
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
```

**Proof.**
- **FreeMiNT:** the `make -k` cross-build goes from six errors to rc 0 and 0
  warnings. The rig's step 2 boots FreeMiNT under ARAnyM and runs the unit
  suite there: `37859 checks, 42 failures`.
- **NetBSD:** it also gets the raise now. It stays green at `37982 checks, 0
  failures`, built clean in 11 s.
- **`portability_lint` check 42:** it reads the five 700-level guards. It names
  all five on the old code, and its floor fires on an empty tree.

**The 42 FreeMiNT failures are not classified here.** In September the row had
19, mostly utilities the minimal guest lacks. Today's are in `test_proc` (11),
`test_bg` (10), `test_tool` (9), `test_pdf` (3), `test_app` (2), and one each in
`test_walk`, `test_platform`, `test_gradecore` and `test_envelope`. They belong
to the platform-widening work, measured one by one, never assumed.

**The lesson.** A per-file feature macro and a command-line one meet in the
same compiler. `#ifndef` asks "is it set?", not "is it high enough?". This is the second time in one day that a file and the build disagreed about which
feature macros were in force; the first was the probe in section 2.

## 4d. The old-userland ladder, the same day: every rung red, for two reasons

`scripts/tier-v-ladder.sh` builds and tests jichi in containers of old
distributions, newest first. In September every rung from Debian 9 down to
Debian 5 was green. On 2026-11-13, on the release code, every rung failed to
build.

**The first reason: `struct timespec` was incomplete.** Debian 6 to 9 and
CentOS 6 and 7 (glibc 2.11 to 2.24) failed in the system's own header:

    /usr/include/x86_64-linux-gnu/bits/stat.h:91:21: error: field 'st_atim' has incomplete type

At `_XOPEN_SOURCE 700` these glibc versions give `struct stat` its
`struct timespec` fields, but `<sys/stat.h>` includes `<time.h>` there only
for `time_t`. So `struct timespec` stays undeclared unless the file includes
`<time.h>` itself, first. Three of the files that set 700
(`jc_platform_posix.c`, `jc_hp_gateref.c` and `jc_hp_gatesweep.c`) did not.
A current glibc fixed its headers years ago, so no gate saw it.

**The fix, in two attempts.** The first attempt included `<time.h>` just before
`<sys/stat.h>`. Debian 9 to 7 went green, and Debian 6 (squeeze, glibc 2.11)
still failed:

    /usr/include/sys/stat.h:374: error: array type has incomplete element type

On glibc 2.11, `<fcntl.h>` includes `<sys/stat.h>`, and `<fcntl.h>` came
earlier in the file. The second attempt puts `<time.h>` **first**, directly
after the `_XOPEN_SOURCE` guard, in all five files that set 700. Then no
header can include `<sys/stat.h>` before it.

**The second reason: Debian 5's libcurl.** Debian 5 (lenny) ships libcurl
7.18.2, and the failure classifier (M877) asks for
`CURLINFO_APPCONNECT_TIME`, which arrived in 7.19.0:

    src/net/jc_http.c:1777: error: 'CURLINFO_APPCONNECT_TIME' undeclared (first use in this function)

Both calls are now inside `#if LIBCURL_VERSION_NUM >= 0x071300`. Below that
the handshake time reads 0, which changes only a telemetry label.
`docs/INSTALL.md`'s libcurl row says so, because `portability_lint` check 5
holds every guarded libcurl identifier to a line there.

My first edit of that guard went wrong too. A scripted edit nested a second
guard inside the first and left the second call bare. lenny caught it, and I
redid the edit by hand, one call at a time.

**Proof.** Every rung ran its build and the whole unit suite on the fixed tree:

| rung | glibc | libcurl | result |
|---|---|---|---|
| Debian 9 (stretch) | 2.24 | 7.52.1 | `37898 checks, 0 failures` |
| Debian 8 (jessie) | 2.19 | 7.38.0 | `37898 checks, 0 failures` |
| CentOS 7 | 2.17 | 7.29.0 | `37898 checks, 0 failures` |
| Debian 7 (wheezy) | 2.13 | 7.26.0 | `37898 checks, 0 failures` |
| CentOS 6 | 2.12 | 7.19.7 | `37898 checks, 0 failures` |
| Debian 6 (squeeze) | 2.11 | 7.21.0 | `37898 checks, 0 failures` |
| Debian 5 (lenny) | 2.7 | 7.18.2 | `37898 checks, 0 failures` |

Debian 4 and CentOS 5 are below the documented glibc 2.7 floor and fail as
before. (The suite counts 37898 here and 37992 on the host because the rig
ships a `git archive`, which has no `.git`; the checks that need one skip.)

**A wrong cause I recorded, and withdraw.** After its first green, CentOS 6
failed twice more, in 2 s and 1 s, without installing gcc. I wrote down the
cause as "the vault redirects http to https". I had measured the redirect,
but not that it was the cause. When I reran the rung's exact install step
with yum's output kept, the install went through, redirect and all: yum
follows it. The two failures were transient, and their cause is not known.
The ladder sends yum's output to `/dev/null`, so the log could not say.

## 4e. FreeDOS, the same day: two signals DOS does not have

The FreeDOS row cross-builds jichi with DJGPP and runs the unit suite in a
FreeDOS guest under QEMU. On 2026-11-13 the cross-build failed in four test
files and one source file:

    tests/test_progress.c:448:16: error: 'SIGXFSZ' undeclared (first use in this function)
    tests/test_proc.c:71:22: error: 'SIGCHLD' undeclared (first use in this function)

DJGPP has no `SIGCHLD`, because DOS runs no child asynchronously, and no
`SIGXFSZ`, because it has no file-size limit. The tree's convention for both
is `#ifdef SIGCHLD`, which `src/platform/jc_platform_posix.c` and `src/main.c`
already follow (M834). Tests written on 2026-10-19 and 20, for M987 and the
M997 follow-up, did not: 11 code lines in `test_envelope.c`, `test_proc.c`,
`test_tool.c` and `test_progress.c`. Every gate is Linux, where both signals
exist.

**The fix.** Each use is now inside an `#ifdef` of its signal, and so is
everything that exists only for it. In `test_proc.c`, a `nanosleep` call and
its `struct timespec` sat inside a block that waits for `SIGCHLD`, so the
guard covers them too. In `jc_platform_posix.c`, the `struct sigaction`
variable is declared only where `SIGCHLD` is, or `-Wunused-variable` fires.
`make -k` was run after each round, so the next hidden error showed at once.

**Proof.** The DJGPP `make -k` cross-build finishes with 0 errors. In the
guest the suite runs: `37692 checks, 154 failures`. In September it was 103.
**The 154 are not classified here.** Some are known kinds: no `mkfifo`, and
DOS file modes that cannot say `0600`. Others need reading, such as
`test_str.c:143`: it expects glibc's exact digits for `1e40`, and DJGPP prints
17 significant digits and then zeros. They belong to the platform-widening
work.

## 5. What keeps it from coming back

**`portability_lint` check 41.** No file may declare the same identifier
`extern` twice at file scope. A lint reads the text, so it sees both declarations
on every machine, whichever `#if` is taken, and whichever compiler the gate
uses.
- It reads every C file under `src/`, `include/` and `tests/`.
- It requires exactly the 14 file-scope extern declarations the tree has today.
  An extraction that reads nothing fails instead of passing.
- Old tree: `jc_proc.c: extern char **environ (2x)`, red. Fixed tree: green.

**`portability_lint` checks 42, 43 and 44** hold the later sections the same
way. Each reads the text, so it fails on a Linux gate where the compiler would
not.

| check | rule | floor | red on |
|---|---|---|---|
| 42 | every `_XOPEN_SOURCE` guard above 600 raises a lower command-line level (4c) | 5 guards | the five guards before M1176 |
| 43 | every code line naming `SIGCHLD` or `SIGXFSZ` is inside a conditional that names the same signal; comments do not count (4e) | 26 uses | the 11 bare lines before the fix |
| 44 | in each file that sets `_XOPEN_SOURCE 700`, the first `#include` after the guard is `<time.h>` (4d) | 5 files | all five before the fix; after the first attempt, the two files it missed |

Each floor fires on an empty tree. Check 43 was also run on a synthetic file:
a use under `#elif defined(SIGCHLD)` counts as guarded, a use under its
`#else` is reported, and a comment naming the signal is not read.

## 6. Lessons

1. **A warning your gate's compiler does not implement is a warning you do not
   have.** clang accepts `-Wredundant-decls` and never fires it. A flag in the
   Makefile is not a check unless some machine that runs it implements it.
2. **A probe must compile with the flags the build compiles with.** The first
   draft of this fix asked under different flags and gave the wrong answer.
   Measure the probe on the machine that failed, not on the one where it passes.
3. **A row that has not run for two months has two months of other people's
   changes waiting for it.** NetBSD's last row predated M796, M1043-a and M1116,
   and each of those was right on every machine that checked it. Run every row
   at each release, not only the ones a batch touched.
4. **Run `make -k` when the first error is fixed.** A build stops at the first
   failing file, so one error can hide the next, as with FreeBSD's five blockers
   on 2026-11-12. `-k` lists them all at once.
5. **A fix that is right in principle can still be in the wrong place.**
   "Include `<time.h>` before `<sys/stat.h>`" was right, and still failed,
   because another header included `<sys/stat.h>` first. "First, before any
   header" is the rule that holds on every libc.
6. **A rig that ships `HEAD` tests the last commit, not your working tree.**
   The ladder packs `git archive HEAD`. One of my ladder runs tested the old
   code, because the fix was not yet committed. Commit, even to a scratch
   branch, before you run a rig.
7. **A measured fact next to a failure is not its cause.** The vault redirect
   was real and measured. It was not why CentOS 6 failed (section 4d). The
   cause needs the failing step's own output, and this rig discarded it.
