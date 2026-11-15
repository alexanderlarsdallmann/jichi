# Platform widening: which operating systems next, and why

*2026-11-13. The operator: "Let's make jichi support as many platforms, and
operating systems with jichi's use-cases, as meaningful, and feasible. Document
your findings honestly, and comprehensively for self-learners, junior
developers, and the agents." This is the plan. The findings so far are in the
analysis pages it links. Nothing below is claimed as measured unless it says
where.*

## 1. What "meaningful" means for jichi

jichi is a coding agent. A platform is worth a row when someone could use it
there for that, so the row needs:
- a C89 compiler, and a libc with the POSIX file, process and terminal calls;
- a way to reach a model: libcurl with TLS, or plain HTTP to a local server;
- a shell, so the tool calls can run commands.

A platform that lacks one of these can still be worth a row as a **defect
detector**. Every non-Linux row so far has found bugs that existed on every
platform. Today's examples:
- a `va_list` passed by value, wrong everywhere and visible only off x86-64;
- duplicate declarations, wrong everywhere and visible only to gcc on NetBSD and
  illumos.

The verdict words stay as `docs/PLATFORMS.md` defines them: Verified, Partly
verified, Driven, Never compiled. They are used strictly.

## 2. The method (unchanged; it is what found today's defects)

1. **Run the row's rig on the exact release code.** That means the `WERROR=1`
   build, the unit suite, the smoke tier, and a driven text turn and tool turn.
2. **Classify every failure by measurement:** a product defect, a test
   assumption, or the environment. Never by guess.
3. **For a defect, write the smallest program with no project code that shows
   it.** Then write the fix, a test that is red without the fix (in the
   emulated sweep where possible, not only on the board), and a lint where the
   class can recur. Prove the fix red then green on the platform that found it.
4. **Write an analysis page for learners the same day,** including the
   supervisor's own wrong turns.

## 3. Where the rows stand (re-measured 2026-11-13 for 0.14.0)

Each row was measured at the commit its dated line in `docs/PLATFORMS.md`
names. "Unclassified" means the failures are counted and not yet read: those
are the first work this plan orders, because a row's unread failure may be a
defect every platform has.

| row | result | page |
|---|---|---|
| Linux glibc x86-64 (the gates), native x86-64 musl | green; musl 37985 checks, 0 failures | — |
| the 20-target zig sweep | all 20 build clean; 17 run the unit suite green; armeb, hexagon and x32 as before | — |
| m68k (cross gcc, under qemu-m68k) | 37985 checks, 0 failures | — |
| FreeBSD 15.1 | builds; unit 2 failures and 25 smoke drivers, unclassified; not driven this time | — |
| NetBSD 10.1 | build fixed (M1173); unit green (M1174); 19 smoke drivers unclassified; Driven | [redundant declarations](../analysis/2026-11-13-netbsd-redundant-declarations.md) |
| OpenBSD 7.9 | builds; unit 16 failures and 24 smoke drivers, unclassified; Driven | — |
| illumos (OmniOS) | build fixed (M1173); unit green with M1175 (by hand in the guest); smoke unclassified; Driven | the same page, section 4b |
| Pi 500 (aarch64) | unit green; the one smoke failure fixed by M1172 and proven on the board; Driven | [va_list by value](../analysis/2026-11-13-a-va-list-passed-by-value.md) |
| Debian 12 in 256 MB, one core | unit green; Driven; the smoke tier stops at `release_mode`, unclassified | — |
| Debian 9 | build fixed (M1177); unit green; Driven; the smoke tier stops at `platform_retest_lint`, unclassified | the same page, section 4d |
| the old-userland ladder (Debian 9 to 5, CentOS 7 and 6) | every rung green (M1177) | the same page, section 4d |
| Guix System 1.5.0 | unit green; Driven | — |
| FreeMiNT (ARAnyM) | cross-build fixed (M1176); unit 42 failures, unclassified | the same page, section 4c |
| FreeDOS 1.3 (DJGPP) | cross-build fixed (M1177); unit 154 failures, unclassified (103 in September) | the same page, section 4e |
| tiny-RAM guests | not run: the rig needs the static musl build | — |
| the Hurd, Haiku | not run; by hand, later | — |
| Pi Zero 2 W, Pi 400, VisionFive 2, LicheeRV Nano | not re-run; to be reconnected by the operator | — |
| Windows (WSL2, Cygwin, MSYS2), macOS, Android | not re-run; need the operator's machines | — |

## 4. Candidates, in the order proposed

### Tier 1: cheap, and likely to find defects every platform has

1. **DragonFly BSD.**
   - *Why:* gcc is its system compiler, on a BSD libc. Today showed that a gcc
     warning class (`-Wredundant-decls`) is invisible on the clang BSDs, and
     only DragonFly runs gcc on a BSD.
   - *Cost:* the image is already downloaded. A rig on the NetBSD pattern is
     needed.
2. **FreeBSD built with gcc** (from ports), beside its clang row.
   - *Why:* the same reason. A compiler is part of the platform, and one
     compiler per OS hides half of the warnings.
3. **Alpine Linux:** dynamic musl with the busybox userland.
   - *Why:* the smoke tier shells out constantly. busybox's sh, grep, sed and
     awk differ from GNU's, and OpenBSD's ksh already found real defects of
     this kind.
   - *Cost:* the image is already downloaded.
4. **More C compilers on the host:** tcc, cproc, chibicc, pcc.
   - *Why:* strict C89 front-ends with their own diagnostics and ABI handling.
     `docs/PLATFORMS.md` says tcc is unmeasured.
   - *Cost:* minutes per compiler.

### Tier 2: real porting work, with a use case

5. **AROS, the open-source AmigaOS-compatible system.** The operator asked
   about AmigaOS.
   - **What exists:**
     - AROS One for 32-bit x86 (2.x) and x86_64 (1.0), as ISO and USB images,
       with a native GCC 6.5 according to its release notes.
     - The Linux-hosted builds on the AROS site are old (2019 nightlies);
       x86_64 hosted is reported unmaintained.
     - AmigaOS 3.2 and 4.1 and MorphOS are commercial and not free to
       download.
     - Emulating a real m68k Amiga needs licensed Kickstart ROMs.
   - **What a port needs:**
     - AROS has no `fork`. jichi already starts programs through one function,
       so AROS needs a `jc_spawn` backend on its own process API
       (`CreateNewProc` or `SystemTags`). The FreeDOS row showed the shape:
       M833 to M839.
     - Networking through `bsdsocket.library`.
     - A libcurl for AROS, whose availability is not yet measured.
   - **Provisioning:** GUI-first, like Haiku, so the operator's help is needed
     for the install.
   - **Status:** `docs/plans/2026-09-29-the-fork-less-rows.md` already orders it
     second, after FreeDOS.
6. **NuttX,** the third fork-less row in that plan. Its `sim` target runs as a
   Linux process.
7. **A big-endian full system.** For example Debian powerpc (32-bit,
   big-endian) under `qemu-system`. The sweep's big-endian rows run under
   qemu-user on a little-endian kernel; a big-endian kernel and libc test the
   system-call structures too.
8. **WASI:** `zig cc -target wasm32-wasi`, then `wasmtime`. No fork, sockets or
   signals, which makes it a cheap fork-less compile check.

### Tier 3: research rows, high cost

9. **Minix 3, Redox, SerenityOS.** Each has its own libc or kernel model. They
   are excellent libc-assumption detectors, but each is a project of its own.
10. **Cosmopolitan libc, Chimera Linux, NixOS.** NixOS's image is already
    downloaded, and it is non-FHS like Guix.

### Instruments that act like new platforms

`gcc -fanalyzer`, clang's MemorySanitizer, `scan-build`, and valgrind on the
aarch64 board. Today's argv bug (M1165-b) was uninitialised memory that MSan or
valgrind would flag on any architecture.

## 5. What the operator decides

The order above, and how much of tier 2 and 3 is worth its cost. Machines and
sudo actions are asked for when a row needs them: reconnecting boards, a tap
device for ARAnyM's driven step, and GUI installs. Nothing is assumed
unavailable because it is switched off.

## Sources for the AROS facts

- AROS Research Operating System, download and nightly pages: https://aros.sourceforge.io/download.php,
  https://aros.sourceforge.io/nightly.php
- AROS One release news: https://amiga-news.de/en/news/AN-2024-06-00001-EN.html,
  https://amiga-news.de/en/news/AN-2023-03-00121-EN.html
- AROS x86 status, May 2025: https://arosnews.github.io/aros-x86-may-2025
