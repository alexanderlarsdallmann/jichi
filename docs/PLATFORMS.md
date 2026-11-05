# Platforms — what has actually been compiled and run

> **This page owns the verdict.** Every other page — [`README.md`](../README.md),
> [`TUTORIAL.md`](../TUTORIAL.md), [`INSTALL.md`](INSTALL.md),
> [`BUILD.md`](BUILD.md), [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) — links
> here for platform support instead of stating its own. That rule exists because
> those five pages had drifted into **three different confidence levels** for the
> same untested platform: "works", "well-supported", and "untested". A newcomer
> read whichever one they opened first.

Three words are used strictly on this page:

- **Verified** — jichi was compiled on that platform *and* its test gates were
  run there, by a person, with the numbers written down. Nothing is called
  verified because it "should work".
- **Partly verified** — compiled there, and *some* gate ran green, with the
  incomplete part named. FreeBSD earned this word on 2026-08-16 rather than
  being rounded up to Verified on a green unit suite: 185 of 198 smoke drivers
  passed and the stop was undiagnosed, so the honest label sat between the other
  two. **It graduated at M465**, when that stop turned out to be a harness defect
  and the full gate went green — which is the point of the word: it names a gap
  precisely enough that closing it is a piece of work someone can finish. OpenBSD
  holds the label now. A platform stays here until the named gap closes.
- **Never compiled** — no build this project has verified: either no compiler on
  that platform has seen this source tree, or the only reports of one are unverified
  (no evidence sent, source changes unknown). Not "probably fine", not "unsupported"
  — unmeasured.

And a fourth word, added 2026-09-18 (M665) because the three above do not cover
the thing jichi is for:

- **Driven** — a **live agentic task with a real model** ran on that platform:
  a request over the wire, an SSE stream, a tool the model chose, the tool
  executed, and a second turn that used the result. Named with the task and the
  model, like every other datum here.

  **Every gate the other three words describe is OFFLINE.** The build, the unit
  suite, the smoke tier and the four surfaces (`--version`, `doctor`, `describe`,
  `context`) can all be green on a platform where jichi has never once called a
  model. A row that is *Verified* and not *Driven* says "it compiles and its
  tests pass here"; it does **not** say "it works here", because the agent loop
  is the product and an offline tier never runs it end to end.

  **This page is where driven-ness goes missing, and that is the actual gap.**
  The word was added on 2026-09-18 after the FreeBSD row was re-run to 302 of 303
  drivers having never made a model call — and the first count published with it,
  *"three of nineteen"*, was **wrong**. It counted what THIS PAGE records, and
  was written as though it counted what has been done. What has been done is more
  than that:

  The full register is **[### Driven](#driven--which-rows-have-run-the-agent-loop)**
  below: every platform row on this page, driven or not, so the answer to
  *"has the agent loop ever run here?"* is a lookup rather than a search.

  So the honest statement is not "it has never been done". It is that **the
  matrix does not carry it**, so a reader of this page cannot tell which rows
  have run the agent loop and which have only compiled it — and neither could I,
  which is how the wrong number got published. Recording it per row is the work;
  `DEFERRED.md` holds what is left.

---

## The matrix

The verdicts are the tables below, grouped by state -- each with the platform, its
architecture and libc, and the evidence, and readable at any width. The four groupings
are the four verdict words defined at the top of this page.

### Verified

| Platform | Arch / libc | Evidence |
|---|---|---|
| **Linux, glibc** — the development box (Ubuntu 24.04, kernel 6.8, glibc 2.39, 3 cores / 4.8 GiB) | x86-64 | `uname -s = Linux`. every milestone; full `make ci` green — **gcc 13.3 and clang 18.1** (12,422 unit checks each under `WERROR=1`), clang ASan/UBSan, Valgrind 0 errors, 22 fuzz targets, smoke, e2e (M368) |
| Linux, glibc — **the low-resource reference box** (3 cores, 4.8 GiB) | x86-64 | M229: 9,619 checks / 0 failures, Valgrind 0 errors, smoke 75 drivers / 323 checks, e2e OK |
| **Debian 12 in 256 MB with one core** (KVM) | x86-64 | **Re-run 2026-09-24: unit suite 13,814 / 0, `smoke: OK (324 drivers, 1,887 checks)` in 772 s, peak RSS 8.7 MB / 11.8 MB, and Driven.** M272/M273: gcc builds the whole tree *inside* the ceiling |
| **Debian 9, kernel 4.9** (KVM) | x86-64 | **Re-run 2026-09-24 (M736)** and **re-verified 2026-10-16 at `702f9fab`**: the 492 MB guest, `smoke` KEEP_GOING **455 of 456 drivers, 2,612 checks**, unit **15,396 / 0**, `WERROR=1` clean, still **Driven** (M736, `TIER-V-D4BD81`). The 2026-10-16 re-run found **seven test-tier reds and zero product defects** -- the product is green on the 492 MB Debian 9 guest; the test tier assumed the modern host's tooling. All seven fixed in-tree, each proven red->green on the guest: three read/write config asymmetries a low-RAM host's auto-lite profile exposes (a bare `attempts show` / `ps` / `undo` inherits the lite default the write run pinned off), mawk 1.3.3 silently ignoring `[[:space:]]` (Debian 9's awk; `[ \t]` is portable), `length(array)` needing mawk 1.3.4, git 2.11 predating the `interpret-trailers` parse option (git >= 2.22), and a pty `/exit` that lost a speed-dependent input race. Named gap: `peer_line_cap` -- its 9.4 MB no-newline peer read crossed the 60 s deadline under 1-vCPU full-suite load and passes standalone in the same run (a timing measurement, not the cap). Earlier: M272/M273 twelve guest passes; three product fixes in-row (pre-2.13 `git stash push`, a pre-2.17 worktree leak, an `Expect: 100-continue` second per model call). Rig record: [`internal/2026-10-16-v2f-red-set.md`](internal/2026-10-16-v2f-red-set.md). |
| **Raspberry Pi Zero 2 W** — physical hardware, re-flashed to 64-bit | aarch64 | **RE-RUN AND DRIVEN M677, 2026-09-20** (`TIER-B-526D6F`): `WERROR=1` clean (multiplier 12), **13,362 unit checks / 0 failures**, `smoke: OK (308 drivers, 1,780 checks)` — the whole tier. The smallest thing in this matrix to run the agent loop. This row found the M664 quadratic reader (every platform had it; only a 512 MB board exposed it). Earlier 32-bit era: M272, multiplier 28. |
| **Raspberry Pi 500+** (Cortex-A76, aarch64, Debian 12) -- the 16 KB-page row | aarch64 | `getconf PAGESIZE` = **16384** -- the first jichi on 16 KB pages, where x86-64's 4 KB assumption is a latent defect detector. **GRADUATED 2026-10-15** at `1e0eb8e4` (`TIER-B-BCFB96`, bonsai-27b): `WERROR=1` build **13.19 s** (multiplier **2**), 0 diagnostics; unit suite **15,402 / 0**; `smoke: OK (456 drivers, 2,642 checks)`, the whole tier, **0 red**; driven with the rig's `"timeouts":{"stall":0}` (a cap, off for a measurement). Its first row (Partly verified, `7f5ca791`) was the board that found `tests/tools/mocklsp.c` building LSP frames off uninitialised memory -- x86-64's stack zeroes it by luck, the Pi's did not; fixed in-tree and proven red-to-green here. Rig record: [`internal/2026-10-15-pi500plus-row.md`](internal/2026-10-15-pi500plus-row.md). |
| **VisionFive 2 v1.3B** (StarFive JH7110, riscv64, Debian 13 on NVMe) -- physical board | riscv64 | **GRADUATED 2026-10-16** at `f73e63fa` (`TIER-B-D84038`, `jlu/qwen3.8-27b` over the HRZ gateway -- the first driven turn this row made without a loopback model server): `WERROR=1` build **85.15 s** (multiplier **10** against 8.59 s), 0 diagnostics; unit suite **15,402 / 0**; `smoke: OK (456 drivers, 2,657 checks)`, the whole tier, **0 red** (14 checks under the x86 host -- platform-conditional skips); both live turns green through the rig's new `--live-key-env` (key file in the board's isolated `.home`, `apiKeyEnv` in the config, M698). First model call jichi has made on RISC-V hardware -- every earlier riscv row ran under `qemu-user`. Its first row (Partly verified, `7f5ca791`, 2026-10-14) was the board whose ~12 k-token prefill the 30 s stall cap cut, and where the two in-tree lint defects the re-run closed were found. Rig record: [`internal/2026-10-14-visionfive2-bringup.md`](internal/2026-10-14-visionfive2-bringup.md). |
| **Windows 11 + WSL2** (Ubuntu 24.04, kernel 5.15.167.4-microsoft-standard-WSL2, 12 cores / 7.6 GiB) | x86-64 | M475 (2026-08-18): full `make ci` green in 10m34s — gcc 13.3 **and** clang 18.1 (12,418 unit checks each under `WERROR=1`), clang ASan/UBSan, Valgrind 0 errors, fuzz 2000 iters/target, curl-free link, smoke **209 drivers / 1,104 checks at timeout multiplier 1**, e2e OK; plus a live round trip against an OpenAI-compatible local server. `make ci` needed four fixes first — all pre-existing, **none WSL-specific** (see [the WSL2 row](platform-notes/windows.md))  **DRIVEN** (M475): chat, reasoning, **tool use** (agent loop -> `read_file`), embeddings via a `roles:["embed"]` model, and the model-gated e2e drivers `headless_model` and `acp_terminal` both green -- see the WSL2 section below. |
| **Raspberry Pi Zero 2 W, re-flashed 32-bit** — physical | armhf (`LONG_BIT=32`) | M276: 9,731 + 95 green at multiplier **26** — the first real test of the `%lu`-with-casts convention where `long` is four bytes |
| **Raspberry Pi Zero 2 W, 32-bit** — physical, second bench | armhf (`armv7l`) | M454/M658: `make check-target` green, **13,329 unit checks / 0 failures**, 297 of 299 smoke drivers at multiplier 13. Needs `TMPDIR` on disk (the 213 MB `/tmp` tmpfs fills to 100%). `cppcheck_lint` (absent) and `lite_context_cap` fail; neither is word-size-specific. |
| **Raspberry Pi 400** — physical, Cortex-A72 | aarch64 | **RE-RUN AND DRIVEN 2026-10-04** at `c7390b68` (`TIER-B-8838B9`, `google/gemma-4-12b`, through the rig's own reverse forward; Debian 13, libcurl 8.14.1): build 34.26 s (multiplier 5 against 8.15 s), **14,952 unit checks / 0 failures**, `smoke: OK (420 drivers, 2,473 checks)`. The sixth of the hardware weekend's dress rehearsals, all 17 ok since the third: the fifth at `e31563e2` (`TIER-B-36D89E`), the fourth at `3bbe507c` (`TIER-B-579E55`), the third at `b36615e3` (`TIER-B-F007D0`); the first two found M879-M882. Earlier: M677 (`TIER-B-2B7078`, 308 drivers), since which the row names the transport that carried the turn; M451; `install_no_build` diagnosed at M661 (`check-target` now builds `all`). |
| **Arduino UNO Q** (Qualcomm SoC) — physical | arm64 | M282: 9,770 + 96 green at multiplier **19**; the `SIZE=1` binary is **byte-identical** to the Pi's aarch64 build |
| **s390x — big-endian** | s390x, emulated (`qemu-user-static` + binfmt) | M266: passes, and retired a standing assumption about the index cache's endianness tag |
| **The M469 architecture sweep — 14 architectures** | x86-64, x86 (32), aarch64, **aarch64_be**, arm, **powerpc** (BE 32), **powerpc64** (BE), powerpc64le, riscv32, riscv64, **s390x**, loongarch64, hexagon — all `zig cc` + musl under `qemu-user`; plus **m68k** (gcc 15.2, glibc) | M469, 2026-08-17, `scripts/tier-v-arch.sh`: each row `WERROR=1` C89 clean and **11,646 unit checks / 0 failures**. **Five are big-endian**, against one before. **These are cross-builds run under emulation, not machines**, and they link `HAVE_CURL=` — so they exercise the core, arenas, JSON and pure helpers but **no model call**; a green row here is weaker than a green FreeBSD row, and the count 11,646 vs the host's 11,661 is the curl-gated checks. **m68k is the sharpest instrument in the set** (big-endian, 32-bit, 2-byte aligned) and is the only row needing a non-zig toolchain, since `zig targets` advertises `m68k-linux-musl` while its LLVM has no m68k backend. **The sweep found two real defects, both invisible to a green `make ci`** — see below |
| **The M542 architecture re-sweep — 20 targets** | every `zig cc` + musl triple with a registered `qemu-user` handler | M542, 2026-08-22: all 20 cross-build `WERROR=1` C89 clean; **13 run the unit suite at 12,724 checks / 0 failures** (three big-endian). Six mips rows are `no-run` (the emulator cannot run the subprocess tests). One toolchain failure, `armeb` — a `printf` variadic-double bug reproduced in a minimal C program with no jichi in it, not a product defect. **Partly verified** — builds/links clean, unit suite unreliable under this toolchain. |
| **aarch64 musl, fully static** | aarch64, emulated | M266: 9,693 checks / 0 failures; the binary runs (`--version`, `doctor`) |
| **aarch64 musl, fully static, curl-free** — row V0 re-executed on a second bench | aarch64, emulated (`qemu-user-static` + binfmt) | M447, 2026-08-15, `alex-X570-AORUS-MASTER` (Ryzen 9 3900X, kernel 6.8), zig 0.16.0 standalone: **11,593 checks / 0 failures**, 8.6 s emulated; `./jichi --version` runs. The run **found a regression**: `run_tests` would not link under `HAVE_CURL=` because a pure predicate sat inside `#ifdef JC_HAVE_CURL` — the same drift M189 repaired once. `make ci` now compiles this configuration |
| **Debian 12 in 160 MB with one core** (KVM) | x86-64 | **DRIVEN M679, 2026-09-20** (`TIER-V-EA7E77`) — the smallest whole machine in this matrix to run the agent loop (160 MB RAM). Build 8 s. The gate fails on one driver, `bibliography_lint` (also fails on illumos; undiagnosed, DEFERRED). Historic M430: `make check-target` green inside the ceiling; the image panics at 128 MB. |
| **x86-64 musl, fully static, WITH a minimal libcurl** | x86-64 | M430: 0 shared libraries, 804 KB `--version` RSS, and a **verified model call** — the ≤64 MB tier's own recipe, built for the first time |
| **FreeBSD** | **Verified — the full gate** (M465); **DRIVEN** M665/M668 | `uname -s = FreeBSD`. FreeBSD 15.1-RELEASE amd64, clang 19.1.7 (`scripts/tier-v-bsd.sh`; needs `gmake`). `gmake WERROR=1` clean, c89 strict, **11,643 unit checks / 0 failures**; `smoke: OK (311 drivers, 1,782 checks)` (M683). M771: `/proc`-key protection via `procctl` ([HARDENING.md](HARDENING.md) §2). Full log and the seven defects this row found: [platform-notes/freebsd.md](platform-notes/freebsd.md). |
| **NetBSD** | **Verified — the full gate** (M480); **DRIVEN** 2026-09-19 (`TIER-V-B34D04`) | `uname -s = NetBSD`. NetBSD 10.1 amd64 (`scripts/tier-v-netbsd.sh`; needs `gmake` + `pkg-config`). **12,416 unit checks / 0 failures**; `smoke: OK (312 drivers, 1,795 checks)` (M685) — the cleanest non-Linux row. The only non-Linux kernel with procfs, so it verifies M472's `child_fds` fence. Log: [platform-notes/netbsd.md](platform-notes/netbsd.md). |
| **OpenBSD** | **Verified — the full gate** (M481); **DRIVEN** 2026-09-19 (`TIER-V-2C6E97`) | `uname -s = OpenBSD`. OpenBSD 7.9 amd64 (`scripts/tier-v-openbsd.sh`; needs `gmake`). **12,415 unit checks / 0 failures**; `smoke: OK (312 drivers, 1,783 checks)` (M685). Its axis is the shell — `/bin/sh` is **ksh**, so ~200 POSIX-sh drivers run under it. The findings (a nullable `grep -o`, a day-old ksh `case` break) are why the project keeps a kernel it does not ship on. Log: [platform-notes/openbsd.md](platform-notes/openbsd.md). |

Alternate build front-ends on the development box, **re-measured from a clean
tree 2026-09-16** (M636) — the previous sentence here claimed `make cpp-check`
passed for g++ *and* clang++ with 0 failures, and on the day it was re-run it
passed for neither:

| Front-end | `make CC=<it>` (whole tree) | `make cpp-check` (C++17 sieve, 302 files) |
|---|---|---|
| gcc 15.2.0 | green, 0 warnings, suite green | — (it is the C build) |
| clang 21.1.8 | green, 0 warnings, suite green | — |
| `zig cc` 0.16.0 | green; static musl cross green | — (`zig c++` compiles `.c` as **C**) |
| g++ 15.2.0 | green, **13** warnings | **OK**, 6 s |
| clang++ 21.1.8 | green, **13** warnings | **OK**, 8 s |
| `zig c++` 0.16.0 | builds C, not C++ | **OK**, 41 s |

The right-hand column had rotted: `cpp-check` had no prerequisite on a generated
header, so it was green only after some other build had produced it. All rows
above are post-repair. See [`CPP_BUILD.md`](CPP_BUILD.md) §M636 and
[`ZIG_BUILD.md`](ZIG_BUILD.md). No `tcc` on the host, so `tcc` is unmeasured.

Each row is **kept as its own stamped datum** rather than overwritten, because
"it passes on a small machine" and "it passes on that architecture" are different
claims from "it passes here". Where a board has a passing `JC_E2E_TIMEOUT_MULT`,
that multiplier is part of the result.

### Partly verified

| Platform | State | What that means for you |
|---|---|---|
| **illumos / Solaris** (OmniOS CE r151058) | **Partly verified, fully driven, green since M703** (`19 ok, 0 failed`) — `make ci` has never run here, which is what *Partly* names | `uname -s = SunOS`. OmniOS r151058 under KVM, gcc 14.3.0, ksh93; build with **`gmake WERROR=1 CC=gcc`** (both load-bearing). **13,458 unit checks / 0 failures**; `smoke: OK (317 drivers, 1,839 checks)`; **DRIVEN** M703 (`TIER-V-52FE94`). Needs `-D__EXTENSIONS__` and `-lsocket -lnsl`, both probed; no source conditional. Full log, and the ten drivers one `grep -o` hid: [platform-notes/illumos.md](platform-notes/illumos.md). |
| **GNU/Hurd** (Debian, hurd-amd64: GNU Mach 1.8, Hurd 0.9, under QEMU with KVM) | **Partly verified, and DRIVEN — first run 2026-09-25 (M767, M768).** `make -k`: 0 errors, 0 warnings, 14 s, with gcc 15.2.0 (multiplier 3). Unit suite **13,967 checks, 1 failure**: M461's pipeline-kill check. On the Hurd, `kill(pid, 0)` reads a group-killed member as alive for tens of seconds after it has left the process table (DEFERRED). **`smoke: OK (342 drivers, 1,988 checks)`** in 1,229 s, after M768 fixed the one product defect the row found: the TUI's key poll trusted `VMIN=0`, which the Hurd's term server does not honour, so the TUI froze before every model call. Driven: agentic phrase `TIER-V-F67C82` (`google/gemma-4-12b`). | jichi builds and runs on a microkernel. The TUI needs M768 or later there. The run was by hand, with no rig script yet, per the plan's *script the rig only after a hand run*: [`analysis/2026-09-25-gnu-hurd-first-row.md`](analysis/2026-09-25-gnu-hurd-first-row.md). `turn_scratch` stalled once in three solo runs before the full run (DEFERRED). |
| **Linux, old userlands** -- Debian 5, 6, 7, 8 and 9, CentOS 6 and 7, each in a container on the development box's kernel | **Partly verified -- builds, and the unit suite passes (0 failures), on every one; no smoke tier, and the kernel is the host's.** | The userland ladder, [`scripts/tier-v-ladder.sh`](../scripts/tier-v-ladder.sh), 2026-09-24 (M736): each distribution's own gcc (4.3.2 to 6.3), glibc (**2.7** to 2.24) and libcurl (7.18.2 to 7.52), 13,840 checks each -- the unit suite less the checks that need git, which the rungs do not install (with git 2.11 on Debian 9: 13,892 / 0). **Before M736 the two distributions INSTALL.md named as the floor, CentOS 6 and Debian 7, did not compile at all**: three libcurl identifiers newer than the promised 7.19.4 were used bare. Debian 5 also needed the `snprintf` probe to ask at the X/Open level, or it formatted numbers wrong. Below the ladder: Debian 4 (gcc 4.1.2, glibc 2.3.6, libcurl 7.15.5) does not build, its libcurl predating the socket callbacks, and CentOS 5 cannot be provisioned. The table is [INSTALL.md](INSTALL.md)'s. |
| **Guix System 1.5.0** -- Linux-libre 6.17.12, the published VM image; non-FHS, glibc, the store's toolchain | **Partly verified -- builds `WERROR=1` clean and passes the unit suite, and Driven; `make check-target` has never been green on ONE Guix system.** | [`scripts/tier-v-guix.sh`](../scripts/tier-v-guix.sh), 2026-09-24 (M738): headless under KVM, GRUB edited over the serial line, `guix shell` with gcc 15.2.0 and libcurl 8.6.0, **13,846 / 0**, both model turns (`TIER-G-B31F57`). The gap is the smoke tier: M458's headless Guix System completed `make check-target` except `parallel_abort`, and M468 passed that driver 2/2 in this image, two systems and two revisions apart. **Build with `CC=gcc`** -- Guix ships neither `cc` nor `c99`, and without it every capability probe reports its feature absent (M458). The row's history and its libc facts are in [libc, and RAM tiers](#libc-and-ram-tiers). |
| **Windows + Cygwin** | **Partly verified — the unit suite and the full smoke tier, but not `make ci`** | `uname -s = CYGWIN_NT-10.0-26200`. Cygwin 3.6.10 on Windows 11, gcc 14.4.0 (M477; needs M476's probe fix). `make WERROR=1` clean, fully featured. **13,438 unit checks / 0 failures**; tier 317 drivers (`13 ok, 1 failed`, M698). **DRIVEN** M697/M698 via the HRZ gateway over TLS (`TIER-V-cygwin-3C2C3F`). Fork penalty ~7–9× — set `JC_SMOKE_TIMEOUT_MULT`. Full log and the rig/pdftotext findings: [platform-notes/windows.md](platform-notes/windows.md). |
| **Windows + MSYS2 (MSYS)** | **Partly verified — builds and runs; file-privacy needs an `acl` mount** | `uname -s = MSYS_NT-10.0-26200`. MSYS2 3.6.10, gcc 15.3.0 (the MSYS env; MINGW64 is a separate row). `make WERROR=1` clean, fully featured. **13,428 unit checks / 0 failures** (configured). **The root is mounted `noacl`, so `chmod` is a no-op and the API key file, daemon socket and audit log are world-readable** — fix with an `acl` mount for `/tmp` and `/home` (`doctor` names it). **DRIVEN** M697/M698 (`TIER-V-msys2-D1A043`). Full log: [platform-notes/windows.md](platform-notes/windows.md). |
| **macOS / Darwin** (Apple Silicon) | **Partly verified, driven** (M817, 2026-09-26) | `uname -s = Darwin`; Apple clang, SDK libcurl ([notes](platform-notes/macos.md)) |
| **Haiku** (R1~beta6, hrev60160 x86_64) | **Partly verified, driven** (M830, 2026-09-28) | `uname -s = Haiku`; one Makefile line ([notes](platform-notes/haiku.md)) |
| **LicheeRV Nano** (StarFive VF2 v1, riscv64, 256 MB) -- physical board, Debian 13 | **Partly verified + Driven (stall-0 replay), 2026-10-15** at `7f5ca791` (`TIER-B-7BE9B9`, bonsai-27b) | `WERROR=1` build 219.81 s (multiplier **26**), 0 diagnostics; unit suite **15,377 / 0**. Driven with `"timeouts":{"stall":0}` (a cap, off for a measurement): text turn 64 s on 11,366 input tokens; tool turn 77 s. Without the replay the same prefill is what the 30 s cap cut (36.25 s to first byte, LM Studio log). Rig record: [`internal/2026-10-15-licheervnano-setup.md`](internal/2026-10-15-licheervnano-setup.md). |

### Per-platform histories — moved to `platform-notes/` (M780)

The long records of how each row was proven and the defects it found used to sit here, between the tables. They are developer/porting material, so they moved to [`platform-notes/`](platform-notes/) to keep this matrix short and the [install pages](install/) lean:

- [`platform-notes/freebsd.md`](platform-notes/freebsd.md) — the FreeBSD row and the seven defects it found
- [`platform-notes/openbsd.md`](platform-notes/openbsd.md) — the OpenBSD rows: the hardening break, what it found beyond FreeBSD, and its failure set
- [`platform-notes/illumos.md`](platform-notes/illumos.md) — the illumos row and the ten drivers one `grep -o` hid
- [`platform-notes/windows.md`](platform-notes/windows.md) — the Cygwin and WSL2 rows and their defects
- [`platform-notes/architectures.md`](platform-notes/architectures.md) — the architecture sweep
- [`platform-notes/macos.md`](platform-notes/macos.md) — the macOS row
- [`platform-notes/haiku.md`](platform-notes/haiku.md) — the Haiku row
- [`platform-notes/freedos.md`](platform-notes/freedos.md) — the FreeDOS/DJGPP row, and what DOS teaches a C programmer

### Never compiled

| Platform | State | What that means for you |
|---|---|---|
| **FreeMiNT (Atari) / MiNTLib** | **Never compiled.** No compiler *on* FreeMiNT has seen this tree; a cross-compile did (M722/M723) | **Cross-compiles clean since M723** (gcc 4.6.4, MiNTLib 0.60.1) and **runs under ARAnyM since M726**; the cross-built unit suite completes in the guest, **13,707 checks, 19 failures** (mostly utilities the minimal guest lacks). **Driven since M737** with a TLS-free libcurl (`TIER-M-5D5D0C`), still cross-built and emulated. Four of the five gaps step 0 found were POSIX-header assumptions, fixed tree-wide at M723; one was jichi's own `-Walloca` (M722). Rig `scripts/tier-v-freemint.sh` (M727); plan [plans/2026-09-freemint-aranym.md](plans/2026-09-freemint-aranym.md). |
| **FreeDOS / DJGPP** | **Never compiled.** A cross-compile has seen the tree | Its unit suite runs under QEMU (M836); 103 failures since M839 ([plan](plans/2026-09-29-the-fork-less-rows.md)) |

### Driven — which rows have run the agent loop

*Added 2026-09-19. The three verdicts above are earned by gates that are **all
offline** — build, unit suite, smoke tier, `--version`/`doctor`/`describe`/
`context`. A row can hold any of them on a machine where jichi has never called a
model. This table exists so that question is a **lookup rather than a search**,
which is the whole complaint that produced the word: evidence living in an
analysis page the matrix does not carry is evidence nobody finds.*

**Every platform row on this page appears here.** A row that has not been driven
says so; it is not omitted, because omission is what made the first published
count wrong.

**What "driven" means is one file, and the rows run it rather than describe it**
(M676): [`scripts/_rig_live.sh`](../scripts/_rig_live.sh). Two turns, and the
second is the one the verdict is named for:

1. **The wire** — `reply with OK`. This exercises the provider, the request and
   the SSE framing, and nothing else: it chooses no tool, executes none, and
   consumes no result.
2. **The loop** — *read `note.txt` with the `read_file` tool and report the pass
   phrase*. The phrase is a **token generated that second** and written into a
   file the model can only reach by calling the tool, so a pass is evidence
   rather than an impression. A quoted sentence would not be: measured, a model
   quoted three passages and dropped an article from one.

**A pass means the loop closed once, here** — request build, SSE framing, a
native tool call, tool execution, and a second turn that consumed the result. It
does **not** mean the platform runs long sessions, survives compaction, or
handles concurrency. A row read as more than that is a row read wrong.

Until M676 that task was copied into three rigs. It had not drifted yet — the
three copies still agreed on both prompts and both assertions — which is the only
moment at which extracting it is cheap, and `_rig_ship.sh` had already written
down the rule: *a second rig is where drift starts, not the fourth.* Two lints
keep it single ([`rig_live_lint.sh`](../tests/smoke/rig_live_lint.sh)) and keep
the commands unchanged ([`rig_live_commands_lint.sh`](../tests/smoke/rig_live_commands_lint.sh)),
the second by sourcing the helper with recording stand-ins for `ssh` and reading
what it tried to do — no VM, no model, no network.

| Row | Driven? | What ran |
|---|---|---|
| Linux, glibc — development box | **yes**, continuously | every live e2e driver and every day's work; this is the bench |
| Linux, glibc — low-resource reference box | no | offline gates only |
| Debian 12, 256 MB, one core | **yes** | both turns through `tier-v-vm.sh --live-port`, **2026-09-24**: agentic phrase `TIER-V-5CDDC7`, `prism-ml/bonsai-27b` on the development box's LM Studio over the rig's reverse tunnel, in the 256 MB guest (217 MB visible) with one core |
| Linux, old userlands (Debian 5-9, CentOS 6-7, containers) | no | builds and the unit suite only (the userland ladder, M736); no model call was made inside a container |
| Debian 9, kernel 4.9 | **yes** | both turns through `tier-v-vm.sh --live-port`, **2026-09-24**: agentic phrase `TIER-V-D4BD81`, `prism-ml/bonsai-27b` over the reverse tunnel -- on the run whose smoke tier went green only after three test-tooling fixes (M736) |
| **Guix System 1.5.0** -- Linux-libre 6.17, non-FHS | **yes** | both turns through `tier-v-guix.sh --live-port`, **2026-09-24** (M738): agentic phrase `TIER-G-B31F57`, `prism-ml/bonsai-27b` over QEMU's host alias, in the **published** `guix-system-vm-image-1.5.0`, built there with `guix shell`'s gcc 15.2.0 and libcurl 8.6.0: `WERROR=1` clean, unit suite 13,846 / 0. Headless -- no person at the console, which M468 needed; earlier runs of the same rig reported `TIER-G-43F8FC`, and by hand `TIER-G-E09BE4` |
| **Raspberry Pi Zero 2 W** (aarch64) | **yes** | both turns through the rig, **2026-09-20**: agentic phrase `TIER-B-526D6F` on a **512 MB** board, and the row names the transport that carried it |
| **Windows 11 + WSL2** | **yes** | chat, reasoning, tool use, embeddings, and the model-gated e2e drivers `headless_model` + `acp_terminal` (M475) |
| Raspberry Pi Zero 2 W, 32-bit | **partly** | the **M459 fleet push** (2026-08-15/16) drove a Pi with a model over ssh; which physical row it was is not recorded to this page's precision |
| Raspberry Pi Zero 2 W, 32-bit (second bench) | no | offline gates only |
| **Raspberry Pi 400** | **yes** | both turns through the rig's own forward, **2026-10-04** at `c7390b68`: agentic phrase `TIER-B-8838B9`, `google/gemma-4-12b`; first through the rig 2026-09-20 |
| Arduino UNO Q | no | offline gates only |
| **s390x — big-endian** | **yes** | both turns, 2026-09-21, through `tier-v-arch.sh --drive`: agentic phrase `TIER-A-E2891D` on a big-endian binary under `qemu-s390x` |
| **The M469 architecture sweep — 14 architectures** | **yes** | all of them, 2026-09-21; see the re-sweep row |
| **The M542 architecture re-sweep — 20 targets** | **yes, 19 of 19 runnable** | both turns on every triple that executes here, 2026-09-21 — five big-endian rows and all six MIPS among them, each reporting its own nonce. The twentieth, `x86_64-linux-muslx32`, does not execute on this host at all |
| aarch64 musl, fully static | **yes** | both turns through `tier-v-arch.sh --arch aarch64-linux-musl --drive`, **2026-09-24** (M736): agentic phrase `TIER-A-DA0D95`, `prism-ml/bonsai-27b`, the static binary with a TLS-free minimal libcurl under `qemu-aarch64`; unit suite 13,898 / 0 there |
| aarch64 musl, fully static, curl-free | **cannot be, as built** | curl-free by construction |
| Debian 12, 160 MB, one core | **yes** | both turns through the rig, **2026-09-20** (M679): agentic phrase `TIER-V-EA7E77` — the smallest whole machine here to close the loop |
| x86-64 musl, static, with minimal libcurl | **yes** | both turns through `tier-v-arch.sh --arch x86_64-linux-musl --drive`, **2026-09-24** (M736): agentic phrase `TIER-A-6177E8`, `prism-ml/bonsai-27b`, a statically linked binary with a TLS-free minimal libcurl, run natively; `WERROR=1` build clean, unit suite 13,898 / 0 |
| **FreeBSD** | **yes** | text turn **4 s**, agentic `read_file` turn **7 s** (M665); re-confirmed **through the rig** at the 0.9.2 release tree — `16 ok, 0 failed`, tier `OK (305 drivers, 1,742 checks)` |
| **NetBSD** | **yes** | text turn and agentic turn through the rig, **first try** (2026-09-19) |
| **OpenBSD** | **yes** | text turn and agentic turn through the rig, **twice** (2026-09-19) |
| **GNU/Hurd** | **yes** | both turns by hand, **2026-09-25** (M767), with the rigs' own task (`scripts/_rig_live.sh`) over an ssh reverse tunnel: agentic phrase `TIER-V-F67C82` (`google/gemma-4-12b`) |
| **VisionFive 2 v1.3B** (riscv64, physical) | **yes, twice** | both turns at `7f5ca791` through `tier-b-device.sh` (the rig's own forward, bench key `id_ed25519_jichi_bench`): text turn ok (after one retry — the 30 s stall cap cut the ~12 k-token prefill), tool turn **`TIER-B-DAD57B`** (`prism-ml/bonsai-27b`) — the first model call jichi has made on RISC-V hardware. Re-driven **2026-10-16** at `f73e63fa` after the in-tree lint fixes: full tier **0 red** (456 drivers, 2,657 checks), both turns over the HRZ gateway (`jlu/qwen3.8-27b`, the rig's new `--live-key-env`), tool turn **`TIER-B-D84038`** — the row graduated to Verified |
| **LicheeRV Nano** (riscv64, 256 MB, physical) | **yes, 2026-10-15** | both turns at `7f5ca791`, replayed with `"timeouts":{"stall":0}` (a cap, off for a measurement): text turn **64 s** on 11,366 input tokens, tool turn **`TIER-B-7BE9B9`** in 77 s (`prism-ml/bonsai-27b`). Without the replay the same prefill is what the 30 s cap cut (LM Studio's log: 36.25 s to first byte, 3.09 s generation) |
| **Raspberry Pi 500+** (aarch64, 16 KB pages, physical) | **yes, twice** | both turns at `7f5ca791` through `tier-b-device.sh` (stall-0 replay after the 30 s cap cut the tool turn at 38.35 s): tool turn **`TIER-B-7B7B74`** in 73 s (`prism-ml/bonsai-27b`). Re-driven **2026-10-15** at `1e0eb8e4` after the in-tree fixes: full tier **0 red**, tool turn **`TIER-B-BCFB96`** — the row graduated to Verified. The 16 KB-page row |
| **illumos / Solaris** | **yes** | both turns through the rig, re-driven **2026-09-22**: agentic phrase `TIER-V-2B095E` (`google/gemma-4-12b`); first driven 2026-09-20 (M678), `TIER-V-52FE94`. Was *partly* — a hand-run text turn only — until the rig could drive it |
| **Windows + Cygwin** | **yes** | both turns, **2026-09-22**: driven BY THE RIG at M698 (`scripts/tier-v-cygwin.sh`), agentic nonce `TIER-V-cygwin-3C2C3F`; first driven by hand at M697 (`M697-cygwin-3FF107`). Over the **HRZ gateway** rather than a loopback tunnel -- the row says which |
| **Windows + MSYS2 (MSYS)** | **yes** | both turns, **2026-09-22**: driven BY THE RIG at M698 (`scripts/tier-v-msys2.sh`), agentic nonce `TIER-V-msys2-D1A043`; first driven by hand at M697 (`M697-msys2-63B3BA`). Same transport |
| **macOS / Darwin** (Apple Silicon) | **yes** -- by hand | both turns, **2026-09-26**, `MAC-50FFBE` ([notes](platform-notes/macos.md)) |
| **Haiku** (hrev60160 x86_64) | **yes** -- by hand | both turns, **2026-09-28** (M830), `TIER-V-HAIKU-82a3a0` ([notes](platform-notes/haiku.md)) |
| **FreeMiNT (Atari) / MiNTLib** | **yes** -- cross-built, emulated | both turns from inside the guest, **2026-09-24** (M737, `tier-v-freemint.sh --step 4`): agentic phrase `TIER-M-5D5D0C`, `prism-ml/bonsai-27b`, jichi cross-built with a TLS-free libcurl 8.18.0 for MiNT, reaching the host over ARAnyM's point-to-point tap link. Still **never compiled** there: no compiler on FreeMiNT has seen the tree |
| **FreeDOS / DJGPP** | no | the unit suite runs on DOS under QEMU (M836); no model call: no sockets yet |

**Two things this table deliberately does not hide.**

The **M459 fleet push** (2026-08-15/16) drove a Raspberry Pi, an **Android
tablet** and a **proot guest** with a model over ssh — fenced `--auto` tasks that
executed tools and wrote files. The tablet and proot rows live in the *libc and
RAM tiers* section rather than in the three tables above, so they are named here
and not given rows of their own. Their findings are still doctrine:
*"done" is not a success verdict*, and *scope every journal read to its run id*.

~~The **fourteen `qemu-user` architecture rows cannot be driven as they are built**,
and saying "not driven" about them without that reason would read as neglect. They
link `HAVE_CURL=` deliberately, so the binary has no HTTP at all; a driven
architecture row needs a curl-enabled cross-build and a network path under
emulation, which is its own milestone.~~

**All nineteen runnable architecture rows were driven on 2026-09-21**, and the
struck-through paragraph above is kept because it stood in two documents and a
register for two milestones. What it got right was the route; what it got wrong
was the cost. `HAVE_CURL=` is what the **rig passes**, not something the platform
imposes: `zig cc` does not *ship* libcurl and cross-**builds** one in about half
a minute per triple. The sweep runs through
[`scripts/tier-v-arch.sh --drive`](../scripts/tier-v-arch.sh), and three things
make it cheap:

* **No transport.** `qemu-user` has no guest — it translates syscalls and hands
  them to this kernel, so the emulated process shares the host's loopback and
  reaches the model server directly. Every VM row needs an `ssh -R`; these need
  nothing. Measured first: 19 of 20 triples opened a socket to the server and
  read a response, the six MIPS rows included, although `pipe()` fails there.
* **A TLS-free libcurl.** The task talks plaintext to `127.0.0.1`, so
  `--without-ssl` removes the hardest part of the cross-compile.
* **A second binary, not a changed one.** The curl-free build and its unit-suite
  count are untouched, so every number these rows have ever recorded stays
  comparable and the driven claim is separable from the old one.

**What a driven `qemu-user` row does NOT say.** It is still not a machine: no
cache coherency, no timing, and **RSS measures the emulator** — 7,088 KB for
`--version` is qemu's, not jichi's. **TLS is unexercised by construction.** And
`armeb` passing does not retire the M542 finding about `printf` and variadic
doubles below: the shared task configures no `temperature`, the only clearly
non-integral double in the request body, so that path was probably never
reached. A row that says more than it measured is worse than no row.

### libc, and RAM tiers

The platform rows above are all glibc unless noted. Two adjacent axes have their
own state:

| | State | Evidence |
|---|---|---|
| **glibc** | **Verified** | every row in the matrix |
| **musl** | **Verified** | a static `x86_64-linux-musl` build (one `zig cc -target` command) and an aarch64 musl static binary executed under qemu-binfmt — M266, 9,693 checks |
| **Guix (non-FHS, glibc)** | **Verified** (unit suite + parallel/signal drivers); **Driven** (M738) | M450: **11,592 unit checks / 0 failures** in `guix shell -C` (no `/usr`, store gcc 12.2.0 / glibc 2.33). Build with **`CC=gcc`** (Guix ships no `cc`/`c99`). M458 headless VM: 11,599 / 0. M468: the published 1.5.0 image booted and driven, and `parallel_abort` passes 2/2 (it had deadlocked under `guix shell -C`, an environment limit not jichi). **M738: driven by a rig** (`scripts/tier-v-guix.sh` edits the GRUB entry over serial): **13,846 / 0**, both turns (`TIER-G-B31F57`), gcc 15.2.0. `make check-target` has not been fully green on one single Guix system. See [install/guix.md](install/guix.md). |
| **FreeBSD libc** | **Verified** (M465) | M460, 2026-08-16: FreeBSD 15.1 amd64, clang 19.1.7, libcurl 8.16 via `pkg`. The **first non-Linux libc** — glibc, musl, bionic, uClibc and Guix's glibc are all Linux libcs, so every prior `HAVE_*` probe result came from one kernel's ecosystem. `HAVE_MALLOC_TRIM` correctly probes **absent**; `HAVE_CLOCK` and `JC_HAVE_VSNPRINTF` present. No `/proc`, so `jc_have_proc_rss()` correctly reports the RSS watchdog unavailable and the memory guard degrades rather than crashing — the prediction this row was run to test, and it held. Three symbols four Linux libcs made look portable are **not** in FreeBSD's POSIX namespace; see the FreeBSD section above |
| **OpenBSD libc** | **Partly verified** | M461, 2026-08-17. The **second** non-Linux libc, and the one that answered whether FreeBSD's fixes were portable or fitted: clean build first try. `HAVE_MALLOC_TRIM` correctly absent, `HAVE_CURL`/`CLOCK_GETTIME`/`JC_HAVE_VSNPRINTF` present. Also the only row where `/bin/sh` is **ksh** rather than an ash derivative |
| **bionic (Android)** | **Verified** | M456, 2026-08-15: Lenovo TB336FU, **Android 16 (SDK 36)**, arm64-v8a, 8 cores / 7807 MB. Built with the NDK's clang 19, dynamically linked, interpreter `/system/bin/linker64`: **11,537 unit checks / 0 failures**, all offline surfaces OK. Requires **`-std=gnu89`** rather than the project's usual `-std=c89`, because bionic's kernel UAPI headers use `inline` and are not C89-parseable — jichi's own code needs no change and builds with zero diagnostics. `HAVE_MALLOC_TRIM` correctly probes absent. This is `adb` + `/data/local/tmp`, **not Termux/proot**, so this is a DIFFERENT claim from the Termux row below, which builds on-device (M459) |
| **uClibc** | **Verified** | M449, 2026-08-15: built with the Bootlin buildroot toolchain `x86-64--uclibc--stable-2025.08-1` (gcc 14.3.0, uClibc-ng), `HAVE_CURL=` + `-static`, **zero diagnostics** under `-std=c89 -pedantic -Wall -Wextra -Werror`, and **11,593 unit checks / 0 failures** run natively. Offline surfaces `--version`/`map`/`context`/`describe` all OK; `--version` peak RSS **384 KB**, 0 shared libraries. The row also **found a defect**: the `malloc_trim` probe answered "yes" on a libc that hides the declaration, because the probe omitted the build's own warning flags — see M449. `JC_HAVE_MALLOC_TRIM` is now correctly **off** here, and `jc_memtrim` is the documented no-op. |
| **RAM below ~256 MB** | **Verified on whole VMs down to 64 MB, and DRIVEN at 96 MB** | **M680/M686: the agent loop closes on a 96 MB machine** — kernel + busybox + a static musl jichi with a minimal mbedTLS libcurl. Both turns ran, the agentic one reporting a per-run nonce (`TINY-8E8082`); peak RSS ~1,668 KB. 256/192/128/96 MB all complete a real turn; **64 MB panics before userspace**. Plain HTTP only — the TLS path is unexercised here. Historic M430: stock Debian 12 passes the full gate at 160 MB, cannot boot at 128. Grades: [LOW_MEMORY.md](LOW_MEMORY.md#how-much-of-this-has-been-verified-m403). |
| **Termux (bionic, on-device toolchain)** | **Verified** | M459, 2026-08-15: Lenovo TB336FU tablet, Android 16 (SDK 36), arm64. Built **on the device** by Termux's clang 21.1.8 against Termux's libcurl 8.21, in the non-FHS `$PREFIX` — a different claim from M456's NDK cross-build. **11,620 unit checks / 0** and the **full smoke tier, 194 drivers / 1027 checks**: the first time `make check-target` has completed on Android. Build 42.8 s → `JC_SMOKE_TIMEOUT_MULT=7`. Needs `$TMPDIR`, never `/tmp` (present on Android 16 but not writable by an app uid). No `/etc/os-release`, no `ldd`, `HAVE_MALLOC_TRIM` correctly absent |
| **proot-distro (glibc on an Android kernel)** | **Verified** | M459, 2026-08-15: Debian 13 (trixie) under `proot-distro` on the same tablet, gcc 14.2 / glibc 2.41 / libcurl 8.14 — a second, entirely different userland on the same silicon. **11,625 checks / 0**, smoke **196 drivers / 1038 checks**. Build 109.4 s → mult **18**, i.e. proot costs ~**2.6x** over native, measured same-day. **Every proot guest is uid 0**, so `doctor --unattended` refuses the hardened posture by design — use native Termux for unattended work. Driven by the unchanged `tier-b-device.sh` via a Debian sshd inside the rootfs, so the row is comparable to every other |
| **a phone specifically** | **Verified** (unit suite + offline surfaces) | M461, 2026-08-17: Motorola moto g(30), Android 12 (SDK 31), arm64-v8a, kernel 4.19 — the first phone this project ran on. NDK 28.2 clang 19, `HAVE_CURL=`, over adb: **11,571 unit checks / 0 failures**, all four offline surfaces. `doctor` does not auto-enable lite (the 1024 MB threshold, confirmed from above). Re-run M507: 12,499 / 0 over **wireless debugging** (the first row reached with no USB data path); `smoke_lint` 17/17 — the first smoke driver to run on a phone. One flaky unit check (the platform's doze power management) is fixed — see [Measuring on a phone](#measuring-on-a-phone-doze-makes-every-wall-clock-number-wrong-m507). |
| **Android, static musl armv7** (a different claim from the row above) | armv7, physical | M452, 2026-08-15, Archos 101b Copper (Android 4.4.2, 2013): a static `arm-linux-musleabihf` build pushed over `adb` **runs** — `--version`, `describe`, `context`, `map`, `doctor`. It bypasses the platform libc, so it says **nothing about bionic**, which is still untested. `doctor` there reports `1 core(s), 965 MB RAM -- tier: lite auto-enabled`, confirming M448's threshold on physical silicon. **The unit suite does not run on THIS device**: 158 fixture paths across 35 test files hardcode `/tmp`, which Android 4.4 lacks. Not true of Android generally — see the bionic row below (M456) |

### Measuring on a phone: doze makes every wall-clock number wrong (M507)

The phone row was re-run on 2026-08-20 to add the smoke tier, and the tier is not
what the session produced. **The instrument was wrong**, and on a handset it is
wrong by default.

Android **dozes**. With the screen off the kernel suspends, and `/proc/uptime` —
unlike `CLOCK_BOOTTIME` — **does not count suspended time**. Every derived figure
inherits that: `ps` `ETIME` under-reports, so a process that has been alive for ten
minutes of wall clock reports `01:43`, and `time` reports a real-seconds figure with
an idle gap baked in. The signature is a run whose **wall clock vastly exceeds its
CPU**: 200 process spawns measured `21–24 s real` against `2.1 s user + 2.8 s system`.

Held awake with `dumpsys deviceidle disable`, the same 200 spawns cost **4.4–6.2 s**,
and wall clock ≈ CPU. So the honest fork penalty against this bench is about **27x**
(22 ms per spawn against 0.81 ms), **not the 139x the dozing measurement showed** — a
**5x phantom** that a row would have published as a property of the silicon.

Three consequences, all of them load-bearing for anyone adding a handset row:

- **Hold the device awake, and verify it stayed awake.** `svc power stayon true` is
  not enough: it holds only while charging, and this handset could not charge from
  the host because the same dead USB port is the charge path. `dumpsys deviceidle
  disable` works, and **still lapses back** — every run here ended `mWakefulness=Dozing`.
  Read `dumpsys power | grep mWakefulness=` *after* the measurement, not before.
- **A multiplier taken from one tier does not transfer to another.** The unit suite is
  storage-bound here (43 s wall for 3.5 s CPU) and the smoke tier is fork-bound, so the
  unit suite's ratio of **5** under-predicts the smoke tier's by more than an order of
  magnitude. `smoke_lint` alone wanted 9m35s where the default per-driver deadline,
  even at `JC_SMOKE_TIMEOUT_MULT=5`, is 300 s. This is why the published formula is a
  **build**-time ratio: a build forks per translation unit, so it is the tier whose
  profile the smoke tier actually shares. A cross-compiled row has no on-device build
  and therefore **no honest multiplier** — which is a reason to install Termux, not a
  reason to substitute a different tier's number.
- **A wall-clock upper bound in a test is not sound here.** `tests/test_app.c:209`
  asserts `elapsed < 4.0` around a command killed at a 1 s cap. The kill is correct on
  this device — `code == 124` and the `timed out` marker both pass — and only the bound
  fails, when the kernel suspends between the two `gettimeofday` calls. Same binary,
  same handset, three runs: **0 failures, 0 failures, 1 failure**. It is the M461 shape
  again (a test asserting something about its host rather than about jichi). **Fixed the
  same session:** the test now reads `jc_now_millis()` — the *same clock the deadline is
  armed from* — so it freezes in lockstep with what it is measuring. The mechanism was
  then proven directly on the handset rather than inferred: a probe sleeping 20 s while
  dozing measured **`CLOCK_MONOTONIC` 20.001 s against `CLOCK_REALTIME` 51.603 s**, a
  **31.6 s divergence** in one window — an order of magnitude more than the 3 s of margin
  needed to break the old bound. Three further suite runs, all confirmed `Dozing`
  throughout: **12,499 / 0, 12,499 / 0, 12,499 / 0**.

### Not supported by design

**Windows, natively.** The POSIX process, terminal, signal and socket layers jichi
is built on have no Win32 equivalent without a port. This is a decision, not a
gap: the survey of *exactly* where POSIX ends — and what a port would have to
replace — is [`PORTING_WINDOWS.md`](PORTING_WINDOWS.md), which doubles as a
graded assignment. Use WSL2.

---

## The finding that made this page (M400)

Asked to document the macOS and WSL gap honestly, the honest form of the task
turned out not to be prose. It was this:

```c
/* src/platform/jc_platform_posix.c, as it stood before M400 */
#if defined(__APPLE__)
    unsigned long long bytes = 0;                       /* <- illegal C89 */
    ...
    return (unsigned long)(bytes / (1024ULL * 1024ULL)); /* <- twice more   */
#endif
```

[`CLAUDE.md`](../CLAUDE.md) requires that first-party code compile with **zero
warnings** under `-std=c89 -pedantic -Wall -Wextra`, *every* translation unit, no
exemptions. `long long` is not a C89 type; gcc's `-Wlong-long` fires on the type
**and** on each `ULL` constant, and `WERROR=1` turns all three into a failed
build. So jichi's only macOS-specific code could not have built under jichi's own
mandatory flags — and it had been that way since it was written.

It survived every `WERROR` build, all four compilers of the M368 matrix, and four
separate claims-audit passes, for one reason: **no machine here compiles it.** A
platform guard for a platform you cannot build is a hole in the compiler's
coverage, and the only instrument that reaches inside is `grep`.

Fixed (`unsigned long` — 64-bit on both Darwin ABIs, which are LP64, and a safe
degrade to the `_SC_PHYS_PAGES` path if it ever is not), and pinned:
`tests/smoke/portability_lint.sh` now fails the build on `long long` anywhere in
`src/` or `include/`. **Scope is the whole tree, not the Darwin branch**, because
the rule is tree-wide and the next never-compiled guard — a BSD, an illumos —
would otherwise repeat the same trick.

**M817 — the first real macOS build confirmed both halves of this.** When Dima
Mellnik compiled the tree on Apple Silicon (2026-09-26), the `HW_MEMSIZE` branch
this section is about built and ran correctly (`doctor` read the true 16384 MB) —
M400's fix held. But the *same* branch exposed a **second, distinct** build blocker
that no machine here could have hit: `src/platform/jc_platform_posix.c` is compiled
with `-D_POSIX_C_SOURCE`, which puts the macOS SDK into strict-POSIX mode, and there
the BSD typedefs `u_int`/`u_char`/`u_short` that `<sys/sysctl.h>` needs are not
declared — eight errors before any jichi code. The fix is one `__APPLE__`-guarded
`#define _DARWIN_C_SOURCE 1` before the first system header (a no-op everywhere
else). The lesson repeats exactly: a platform guard is untested until a machine of
that platform compiles it — and the instrument that finally reached this one was a
user with a Mac, which is why [`VERIFY_A_PLATFORM.md`](VERIFY_A_PLATFORM.md) exists.

The transferable lesson, which is the one this project keeps re-learning: a
compile-time guard for an absent platform is **untested code that looks tested**,
because it sits in a file that compiles cleanly. Prefer a lint to an audit — but
notice which lint. Not one that checks the code you build; one that checks the
code you *cannot*.

**The FreeBSD row (2026-08-16) found the mirror image of this**, and it is worth
stating because no lint in the tree would have caught it. M400's defect was a
guard nobody could compile. FreeBSD's was the opposite: code with **no guard at
all**, in the one branch every build takes, portable-looking because
`_SC_NPROCESSORS_ONLN`, `INADDR_LOOPBACK` and `SIGWINCH` are present in glibc,
musl, bionic and Darwin alike. Four libcs agreeing is not a standard; all four
were Linux's. The instrument that finds this class is not `grep` and not a
reviewer — it is **a fifth libc from a different kernel**, which is the whole
argument for the row.

---

## Helping verify a platform

If you build jichi on macOS, WSL2, or a BSD other than FreeBSD, that is a
measurement this project does not have. **The whole procedure is
[`VERIFY_A_PLATFORM.md`](VERIFY_A_PLATFORM.md)** (M716): the commands, each run
before it was published; what each one proves; which of the four words above it
can earn; what to send and what never to send; and how a report is checked
rather than trusted. It replaced the three commands that stood here, which named
the gates but not the commit, the zero-warnings build, the smoke tier's
keep-going mode or the live turns a **Driven** row needs.

> **If your platform has no writable `/tmp`, set `TMPDIR` (M457).** The unit suite used to
> write its fixtures to 158 literal `/tmp/…` paths and consult `TMPDIR` nowhere, so on such
> a target it failed — or worse, **aborted early**, reporting nothing — for a reason that
> had nothing to do with jichi, which may run there perfectly. It now honours `TMPDIR`
> throughout, and the same 11,599 checks run wherever fixtures live.
>
> Some checks will still fail on a platform lacking `/bin/sh`, because tests that fork or
> shell out cannot work there — on Android 4.4 that is 47 of them, and they are a property
> of the platform, not of jichi. The suite **runs to completion** and reports them as reds,
> which is the behaviour to expect: a short output means a crash, and crashes are now the
> exception worth reporting.

A row above is exactly the information that page collects. `make ci`
additionally wants clang, Valgrind and a fuzzer; `make check-target` is the
portable gate and the one that matters on a new platform.

`jichi doctor` and `jichi setup` name an unverified host themselves, in the tool,
and since M716 they point at that page as well as this one — because the person
on a Mac finds out from the program, not from a page they did not open.
`tests/smoke/portability_lint.sh` check 25 fails if they stop naming it, or if it
is renamed.

## Where the rest lives

- Per-platform build steps and prerequisites: [`BUILD.md`](BUILD.md) (terse) and
  [`PREPARE_AND_BUILD.md`](PREPARE_AND_BUILD.md) (from an empty terminal).
- System requirements, minimum and recommended, and the documented floors
  (libcurl 7.19.4, glibc 2.17 for `clock_gettime` linkage):
  [`INSTALL.md`](INSTALL.md).
- Running on small and embedded machines: [`LOW_MEMORY.md`](LOW_MEMORY.md),
  [`DEPLOYMENT.md`](DEPLOYMENT.md).
- The hardware-testing programme that produced the verified rows:
  [`ROADMAP.md`](ROADMAP.md) M264–M282.


---

## The rigs and their lessons, as CLAUDE.md carried them (moved at M516)

*This text lived in `CLAUDE.md`'s `## Platforms` section until M516. It is
reference — which libcs and kernels are green, which rig rebuilds which row, what
the OpenBSD and NetBSD rows taught. The **rules** distilled from it stayed in
`CLAUDE.md`. The M516 note said overlap with the verdict tables above was
possible and a stated follow-up. **Measured at M527: there is none** — not one
identical sentence and no paragraph pair above a 0.28 Jaccard on content words,
comparing this section against everything preceding it. The worry was reasonable
and it was wrong, and it is recorded so nobody searches again.*

**Start every session with `docs/SESSION_RUNBOOK.md`.** Every rule in it was written
after breaking it, and the bracketed numbers are how many times that mistake was made
in one day. Step 0 is `scripts/preflight.sh`, which refuses a tree whose gate is
already running (a build or edit in a busy tree is worthless in both directions); the
step order does not vary; and §5 covers moving to another machine.

**jichi now runs on five libcs and four non-Linux kernels**, and the honest verdicts
live in `docs/PLATFORMS.md` (Verified / Partly verified / Never compiled, used
strictly). glibc, **musl**, **uClibc** (M449, unit suite only), **bionic** (M456 via
the NDK; M459 built on-device by Termux's own clang), and glibc-on-an-Android-kernel
under `proot-distro`. **FreeBSD** (M460),
**OpenBSD** (M461) and **NetBSD** (M480) all run the **full gate green** — OpenBSD's last
two red checks were diagnosed at M481, so **every measured platform in the matrix is
green**.
Guix System is verified for the unit suite and, since M738, **driven** -- headless, by a rig, in the published image. **illumos is partly verified, fully driven and GREEN since M703** (OmniOS r151058 under KVM: clean `WERROR=1` build, 13,458 unit checks, **317 of 317** smoke drivers, coverage debt 0). **macOS is partly verified and driven since M817** (Apple Silicon, first verified by Dima Mellnik: `WERROR=1` clean after one Darwin build guard, unit suite green after three test-tolerance fixes, smoke OK, driven `MAC-50FFBE`). **Haiku joined at M830** (R1~beta6 hrev60160 under qemu: `WERROR=1` clean after one Makefile socket-lib line and no source guard, 14,119 unit checks / 0 failures, 364 of 370 smoke drivers, driven `TIER-V-HAIKU-82a3a0`). The full hardware/VM overview, in tables, is in `docs/LOW_MEMORY.md`.

**The NetBSD row cost the product nothing and the harness everything** (M480): zero source
changes, six rig attempts, five of them failing for reasons unrelated to jichi. Its lesson is
**report the effect, never the attempt** — an `ok - console moved to com0` printed after merely
*writing* the keystrokes was green on a run where the loader had dropped a character, which is
M479's lesson reproduced in new code the same day. Where the target echoes, the echo is the
check. See docs/analysis/2026-08-18-netbsd-first-row.md.

**Two of the OpenBSD row's findings were bugs on EVERY platform** — children
inheriting jichi's ignored SIGPIPE, and a timed-out capture orphaning the rest of its
pipeline — found only because a different userland made them observable. That is the
argument for the non-Linux rows: they are not compatibility work, they are a defect
detector. Three more findings were **non-POSIX symbols that four Linux libcs made look
portable** (`_SC_NPROCESSORS_ONLN`, `INADDR_LOOPBACK`, `SIGWINCH`).

**The rigs, in `scripts/`** — each rebuilds its own row from nothing, unattended, so a
platform result is reproducible rather than remembered:
`tier-v-vm.sh` (whole-VM Debian rows v2e–v2k, 32–512 MB), `tier-v-bsd.sh` (FreeBSD),
`tier-v-illumos.sh` (OmniOS CE, M658),
`tier-v-freemint.sh` (FreeMiNT under ARAnyM, M727),
`tier-v-openbsd.sh` (OpenBSD, installed via `autoinstall(8)` over a serial console),
`tier-v-netbsd.sh` (NetBSD, from the vendor live image — the only row that can run
`child_fds.sh`, because it has procfs; M480),
`tier-v-tiny.sh` (a kernel + busybox initramfs below any distro's boot floor),
`tier-v-terminals.sh` / `tier-v-console.sh` (real terminal emulators; a Linux VT),
`tier-b-device.sh` (a physical device over ssh), `minimal-curl.sh`, and
`fleet-run.sh` (M459 — push work to jichi agents across *machines*; the three loop
topologies in docs/AUTONOMOUS_LOOPS.md all assume one filesystem, and a fleet of a Pi,
a tablet and a proot guest has none). Results go to `$TIER_V_DIR`, **never the repo
tree** — a rig that dirties the tree it tests has been three separate mistakes.

**`JC_SMOKE_TIMEOUT_MULT` is a RATIO, not a constant**: device build seconds ÷ *this
bench's* build seconds. Published rows divide by 6.19 s or by threadwork's 4.00 s, and
both terms are printed at every figure. On a new machine the denominator is wrong and
nothing tells you. **Copy the formula, never a row's number.**

**Capability probes are compile probes, and they must ask the right question.** The
Makefile probes the C dialect (bionic's kernel headers are not C89-parseable → gnu89),
`vsnprintf`, `malloc_trim`, `clock_gettime` (the one *negative* macro), and libcurl.
Two hard-won rules: a probe carries `-Werror=implicit-function-declaration`, because
uClibc-ng declares `malloc_trim` only under `__USE_GNU` — the symbol linked while the
declaration was hidden, so the probe said yes and every TU then failed under the
build's own `-Werror` (M449); and `CC ?= cc` means a system shipping neither `cc` nor
`c99` (Guix) makes **every** probe report its feature absent rather than the compiler
missing, so pass `CC=gcc` there (M458).

