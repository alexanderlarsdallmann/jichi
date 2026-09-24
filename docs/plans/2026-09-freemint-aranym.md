# FreeMiNT under ARAnyM: a platform row for a libc and a kernel this project has never met

*Plan, 2026-09-24. **Step 0 ran the same day** — its outcome is §8, set against the
predictions of §3, which are kept as written — and its findings were fixed as M722 and
M723. Steps 1 and 2 then ran by hand (§9), and M726 fixed what they found. M727 wrote
the rig script, and its first fresh disk found what §9's runs had hidden (§10). It exists because the question was asked on
2026-09-22, answered in a chat, and recorded nowhere, and a register that promises
*"things deliberately not done, with the reason"* ([`DEFERRED.md`](../DEFERRED.md))
cannot keep a promise made in a chat. Written for the developer who will run it, and
for a self-learner who wants to see a platform row planned before any of it exists:
what each step can prove, what it costs, where it stops, and what would change the
answer. Every fact below carries its source — the web pages were read on 2026-09-23 —
and everything else is labelled a prediction.*

---

## 0. The answer first

1. **Worth doing — for the operating system and the C library, not for the CPU.**
   Big-endian 32-bit m68k is already covered: M469 cross-built jichi for
   `m68k-linux-gnu` and ran its unit suite under `qemu-m68k` — **11,646 checks, 0
   failures** once the sweep's own finding was fixed: five checks compared doubles
   with `==`, which m68k's 80-bit extended precision makes unequal
   ([`analysis/2026-08-17-the-architecture-sweep.md`](../analysis/2026-08-17-the-architecture-sweep.md);
   `JC_CHECK_NEAR` in `tests/jc_test.h`). What no row has met is **MiNTLib**, the C
   library, and **FreeMiNT**, a kernel that is none of Linux, a BSD, SysV or Windows.
   A new libc is where assumptions hide that every tested platform shares: when
   illumos, the first SysV kernel, joined the matrix, it found four call sites
   testing `uname() == 0` — right on Linux, three BSDs and both Windows layers, and a
   working call read as a failure there (M703, ANECDOTES #97).
2. **Under ARAnyM it can be *Driven*.** The 2026-09-22 answer said it could not:
   *"jichi does support `HAVE_CURL=` builds, but those call no model — so the row
   could never be Driven."* That reads *no TLS* as *no HTTP*, which is the mistake
   M699 corrected for the emulated architecture rows: a libcurl built with no TLS at
   all talks plain HTTP to a local LM Studio, and 19 of 19 rows were driven that way.
   The drive had been written the night before (`f4d4594e`, 2026-09-21 23:10) and
   reached master at 15:08 on 2026-09-22, four hours after the answer. Memory is no
   limit either: ARAnyM's FastRAM goes to 4 GB (§2). One difference is real:
   `qemu-user` shares the host's loopback and ARAnyM does not, so the guest reaches
   the model over ARAnyM's point-to-point network link (§4, step 4).
3. **The first step needs no emulator.** A cross-compile of jichi against MiNTLib,
   with libcurl left out, measures the whole POSIX gap in one build log (§4, step 0).
   Everything after it depends on that number.
4. **Real Atari hardware is out of scope** — but not for the reason the 2026-09-22
   answer gave (§6).

---

## 1. How this page came to exist

On **2026-09-22 at 11:30** (CEST), as topic 5 of a list, the operator asked: *"Out
of curiosity: could jichi be compiled for, and run on an emulated Atari (ST, Falcon,
etc.) platform. There are virtual machines available."* The answer, in that session
on the workstation: real hardware *"is out on memory"*; TOS is not POSIX, so FreeMiNT
and MiNTLib are the target; the cheapest informative version is a curl-free build
under Hatari or ARAnyM running the unit suite, *"accepting permanently 'Partly
verified, never Driven'"*; worth doing *"for the FreeMiNT libc, not for the m68k"*,
ranked below the Ada work — *"a good 'when the tree is calm' project"*. None of it
reached the tree. The only trace was one clause in commit `ec6be593`: *"`TOS` matched
`CentOS` eleven times while checking whether Atari had ever been considered"*.

On **2026-09-23** the operator asked when the analysis had been planned. It had not
been, anywhere in the tree. The operator supplied the FreeMiNT and ARAnyM sites and
asked for this plan. Two of the 2026-09-22 conclusions do not survive it — *never
Driven* (§0, item 2) and *out on memory* (§6) — and each is corrected where it
occurs rather than silently dropped.

---

## 2. The facts, with their sources (read 2026-09-23)

| | what the source says | source |
|---|---|---|
| **FreeMiNT** | *"Unix-like kernel for Atari ST and compatible computers"* | the repository's description, [github.com/freemint/freemint](https://github.com/freemint/freemint) |
| | *"FreeMiNT hasn’t got a proper release since the 1.18.0 version in 2013 due to lack of dedicated maintainer(s)."* Snapshots are *"built after each commit"*, and the site offers *"bootable, preconfigured and maintained FreeMiNT/XaAES archives for the ST, 020+ machines with the FPU, ARAnyM and the FireBee"* | [freemint.github.io](https://freemint.github.io/) |
| | *"FreeMiNT development has been community-driven without a dedicated maintainer since 2016."* The build is `.github/workflows/build.yml`, which has an `ara` target | the repository's README and tree |
| | the ARAnyM archive is `freemint-latest-aranym.zip` (9,978 KiB on 2026-09-23); per-commit archives are named `freemint-1-19-<hash>-aranym.zip` | [atari.joska.no/snapshots/freemint/bootable/](https://atari.joska.no/snapshots/freemint/bootable/), linked from the site |
| **ARAnyM** | *"a software virtual machine (similar to VirtualBox or Bochs) designed and developed for running 32-bit Atari ST/TT/Falcon operating systems (TOS, FreeMiNT, MagiC and Linux-m68k)"*, with *"the most complete CPU (68040 with MMU) and FPU (68882)"* and *"loads of RAM (up to 4 GB)"* | [aranym.github.io](https://aranym.github.io/) |
| | one release: **v1.1.0, 2019-04-14** — *"there are automated builds for x86/amd64, armhf and macOS"*; AppImages in three builds, plain, JIT and MMU | [releases](https://github.com/aranym/aranym/releases) |
| | Ubuntu 24.04 packages **`aranym` 1.1.0-2**, shipping `aranym`, `aranym-jit`, `aranym-mmu` and `aratapif`; not installed on the workstation | `apt-cache policy aranym` here; [the noble file list](https://packages.ubuntu.com/noble/amd64/aranym/filelist) |
| | *"ST-RAM size is always 14MB. FastRAM size is adjustable from 0 up to 4GB."* The JIT is enabled at build time (`./configure --enable-jit-compiler`) and in the configuration (`JIT = Yes`); the configuration lives in `.aranym` in the home directory | [wiki: manual](https://github.com/aranym/aranym/wiki/manual) |
| | HostFS maps a host directory to a guest drive, and `hostfs.xfs` is its MiNT driver. *"These drives aren't bootable. They are also not designed as alternative to the harddisk images but rather as a way to transfer files between HOST fs and ARAnyM."* Without a trailing colon on the host path *"the HostFS filesystem is case-insensitive + case-preserving"*; with one, it switches to *"full case-sensitivity mode"* | the manual |
| | networking: *"the 'aratapif' is to be setuid root and located in $PATH and the tun/tap modules must be loaded"*; the guest loads *"FreeMiNT (with the nfeth.xif driver)"*, which *"requires FreeMiNT 1.16.0+ with NatFeat support"* | the manual |
| | `aratapif` is *"basically stripped down version of standard ifconfig command that works on the TAP device only and so it's safe to setuid root"* | [aratapif(1), Debian bookworm](https://manpages.debian.org/bookworm/aranym/aratapif.1.en.html) |
| | **running without a display is not documented.** The nearest the manual comes is *"No X Windows is required for running ARAnyM on framebuffer"* — a console, not the absence of one | the manual, by absence |
| | a guest can write to the host's stream: NatFeat `NF_STDERR` exists to *"Emit a null-terminated string of printable ASCII chars on a particular output stream, intended for use to display debug messages"* | [wiki: NF_STDERR](https://github.com/aranym/aranym/wiki/natfeats-nfapi-nf_stderr) |
| **Toolchain** | `ppa:vriviere/ppa` publishes 15 packages for Noble (24.04), among them `cross-mint-essential` 1.0, `gcc-m68k-atari-mint` 4.6.4-mint-20251014, `binutils-m68k-atari-mint` 2.30-mint-20180703, `mintlib-m68k-atari-mint` 0.60.1.Git-20220821, `zlib-m68k-atari-mint` 1.2.13, `openssl-m68k-atari-mint` 1.0.1e and `ncurses-m68k-atari-mint` 5.9 — and **no curl, in any series** | [Launchpad](https://launchpad.net/~vriviere/+archive/ubuntu/ppa) |
| | Thorsten Otto's cross tools: gcc up to **15.2.0**, binutils 2.45 and 2.30, as `.tar.xz` archives, and library packages that include **curl 7.56.0** — a development archive and binaries for 68000, 68020+ and ColdFire V4e. Of his gcc 4.6.4: *"I configured this version to generate 68000 code by default"* and *"You can also generate code for the 68020 and higher and for the FPU by using the -m68020-60 option"* | [tho-otto.m68k.eu/crossmint.php](https://tho-otto.m68k.eu/crossmint.php) |
| **curl on MiNT** | built before: *"here is an new curl RPM for freemint OS (Atari)"* (2011, with SSL); curl's install guide lists *"Atari FreeMiNT"* among the systems curl has been compiled for | [curl-library, 2011-02-22](https://curl.se/mail/lib-2011-02/0287.html); [curl.se/docs/install.html](https://curl.se/docs/install.html) |
| **jichi** | builds against libcurl as old as 7.29 (M264): both version guards in `src/net/jc_http.c` fall back to an older option | the tree |

**Not established by anything read:** how much of the POSIX surface jichi uses
MiNTLib provides; whether today's curl builds against MiNTLib 0.60.1; whether
ARAnyM runs unattended; which guest filesystem the gates can run on; whether any
FreeMiNT distribution carries a native C compiler that can build this tree.

---

## 3. What the row is worth — and what it is not

**The argument is the one every non-Linux row has made:** a platform row is a defect
detector, not compatibility work. Two OpenBSD findings were bugs on every platform;
the Guix row found a probe that answered *absent* when it meant *could not ask*
(M458); illumos found `uname()` (M703). Each came from a system that disagreed with
the ones already tested.

**Where to look first — predictions, not findings.** Step 0 settles each of these in
one build log. The list is written down beforehand so that the log is read against
it:

- **The monotonic clock.** `jc_now_millis` (`src/platform/jc_platform_posix.c`)
  uses `clock_gettime(CLOCK_MONOTONIC)`, and falls back to whole seconds from `time()`
  when the macro is absent or the Makefile's probe cannot link the function (M326u).
  A fallback fails nothing, so on MiNTLib it would make every millisecond deadline
  coarse in silence. The probe table says which, and the row records it.
- **`select()` on a pipe.** Both shell paths read a command's output through it: the
  popen path since type-ahead (M258), and `run_command_watched`, which enforces a
  timeout (`src/chat/jc_app.c`).
- **Process groups.** The watched path gives the command its own group with
  `setpgid`, and stops a timed-out command, with everything it started, by signalling
  the group — `kill(-pid, SIGTERM)`, then `SIGKILL`.
- **`termios`** — the TUI's raw mode (`src/tui/jc_term.c`).
- **The filesystem's case.** HostFS is case-insensitive unless the host path ends in
  a colon (§2), and the tier writes files by name; a working directory that folds
  case can merge two of them.
- **Two things already handled**, which a regression here would re-find: 32-bit
  `long` (the 32-bit rows of M469) and the FPU's 80-bit extended precision
  (`JC_CHECK_NEAR`, M469).

**What it is not.** Not a claim that jichi supports the Atari. Not a new CPU. Not real
hardware.

---

## 4. The steps — what each earns, and where each stops

The words are [`PLATFORMS.md`](../PLATFORMS.md)'s, used strictly, and two of its
definitions decide the shape of this section. *Verified* and *Partly verified* both
require jichi **compiled there**, so a binary cross-built on this workstation and run
in the guest earns neither: the page records the M469 rows as *"cross-builds run
under emulation, not machines"*, weaker than a green FreeBSD row. *Driven* has no such
clause — M699 drove cross-built rows.

The rig these steps build is modelled on `scripts/tier-v-illumos.sh` (M658): results
to `$TIER_V_DIR`, never the tree; `--keep`, so a diagnosis survives the guest;
`--ref-secs`, so every multiplier is a ratio against **this bench's** reference
(6.19 s), measured on the row and never copied. Its working name is
`tier-v-freemint.sh`, and it does not exist yet.

### Step 0 — cross-compile against MiNTLib, no emulator

**Earns no word.** A compiler *for* the platform is not a compiler *on* it; this step
measures the gap.

1. **Operator, `sudo`:** `add-apt-repository ppa:vriviere/ppa`, `apt update`,
   `apt install cross-mint-essential`.
2. **The check to write first:** the rig refuses to read any probe's answer until
   `m68k-atari-mint-gcc --version` has succeeded. A probe that cannot ask reports
   *absent* — at M458, `CC ?= cc` on a system with no `cc` reported every feature
   missing — and a gap measured without a compiler would list every feature in the
   tree.
3. `make clean && make info CC=m68k-atari-mint-gcc` — the probe table. The Makefile's
   probes compile and link and never run what they built, so a cross-compiler answers
   them honestly. Check two answers against MiNTLib's own headers by hand, because a
   probe must ask the right question (M449).
4. `make -k CC=m68k-atari-mint-gcc HAVE_CURL= WERROR=1 jichi run_tests` — the
   curl-free configuration every static and cross recipe uses, which `make ci` links
   on every run (M447). The `-k` was missing from the first version of this page:
   without it make stops at the first failed file, and the log shows one gap, not
   all of them. Build it twice: with the compiler's default 68000 code, and with
   `CC='m68k-atari-mint-gcc -m68020-60'` for ARAnyM's FPU. The default code does not
   use the FPU and `-m68020-60` does (§2), so the two builds put M469's floating-point
   repair under two different arithmetics. Keep both logs whole.
5. **Record:** every undeclared function and missing header, grouped by subsystem;
   the probe table; the binaries' sizes; `m68k-atari-mint-gcc -v`, so the row names
   its compiler.

**Stop condition.** If the gap is a handful of calls that can be guarded, go on. If
it needs a platform layer of its own, stop: the
measured gap goes into this page and the register row, and whether to write a MiNT
layer is the operator's decision.

### Step 1 — FreeMiNT boots under ARAnyM, unattended

**Earns no word; it builds the rig.**

1. **Operator, `sudo`:** `apt install aranym`. The package ships three emulator
   builds — `aranym`, `aranym-jit`, `aranym-mmu` (§2) — so time a boot under each
   before choosing one, and before choosing any timeout.
2. The FreeMiNT archive for ARAnyM, pinned by the commit hash in its per-commit file
   name (§2). There is no release to pin to, and `freemint-latest` changes under you.
3. The rig's own configuration, passed with `-c`, so the operator's `~/.aranym` is
   never written; it lives under `$TIER_V_DIR` with every other output. The manual
   shows `-c` only in an example, so read `aranym --help` before relying on it.
4. **The shape.** HostFS is for moving files, not for working in (§2), so the rig
   ships the tree **in** through a HostFS drive and brings the results **out** the
   same way; the gates run on a guest disk, and which guest filesystem keeps case is
   a measurement of this step. A boot script in the guest runs the step's gate,
   writes `results.txt` and a sentinel carrying the rig's run id, and halts the
   machine. The host waits with a measured deadline and **checks the run id**,
   because a waiter that trusts whatever sentinel it finds reads an earlier run's
   result as this one's. `NF_STDERR` (§2) is a second channel, so a hung boot says
   where it hung.
5. **Running without a display is undocumented** (§2). Try SDL's `dummy` video driver
   first (`SDL_VIDEODRIVER=dummy`), then Xvfb. A rig that needs the operator's display
   is still a rig, but it cannot run from a background session, and the results file
   says which it was.

**Stop condition.** If ARAnyM cannot run unattended at all, the row runs by hand, the
way [`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md) asks a volunteer to, and is
recorded the same way.

### Step 2 — the unit suite in the guest

1. **Cross-built first.** Step 0's `run_tests`, run in the guest. Cheap, and it earns
   **no word** (above): it is recorded with M469's label, *cross-built, emulated*,
   with its check count beside the host's.
2. **Native second — *Partly verified*.** If a FreeMiNT distribution carries a C
   compiler that can build this tree, compile there and run the suite: compiled
   there, some gate green, the incomplete part named (the smoke tier, the live
   turns). The guest's build seconds give the row its `JC_SMOKE_TIMEOUT_MULT` —
   guest build seconds ÷ 6.19.
3. **Record** every failing check with its cause. A diagnosed failure is a finding;
   an undiagnosed one is the named gap *Partly verified* exists to carry.

**Stop condition.** If no native compiler can build the tree, the row cannot earn
*Partly verified* or *Verified* — only the cross-built count and, in step 4,
*Driven*. That is a legitimate end state, and the row says it in those words.

### Step 3 — the smoke tier: *Verified*

The tier needs `make`, its C helpers (`mockmodel`, `ptydrive`, `jsonq`, `sockq`)
built in the guest, and a POSIX userland: its drivers are POSIX `sh`
([`TEST_TIERS.md`](../TEST_TIERS.md)). Which FreeMiNT distribution provides all of it
is unknown. Two predictions: `ptydrive` needs pseudo-terminals, which every TUI
driver depends on; and this step finds the most, because one of illumos's two M703
defects was a `tar` behaviour, and a thinner userland has more utilities to
disagree about. Run it with `JC_SMOKE_KEEP_GOING=1` for the whole failure set
([`SESSION_RUNBOOK.md`](../SESSION_RUNBOOK.md) §5).

**Stop condition.** If the userland cannot carry the tier, the row stays *Partly
verified* and names that gap.

### Step 4 — two live turns: *Driven*

1. **libcurl for MiNT, with no TLS.** Build curl with the PPA's toolchain on M699's
   configure line (`scripts/minimal-curl.sh --tls none`): `CC=m68k-atari-mint-gcc`,
   `--host=m68k-atari-mint`, and `--build` named explicitly, which the script calls
   load-bearing because it forces `cross_compiling=yes`. The script's own `--target`
   rung cannot do it — it builds with `zig cc`, and zig bundles no MiNTLib — so this
   is new work in that script. **The escape route:** if curl does not build against
   MiNTLib 0.60.1, Thorsten Otto's prebuilt curl 7.56.0 (§2) is inside jichi's range;
   then use his toolchain for the whole row, so that the compiler, MiNTLib and libcurl
   come from one place, and the results file says so.
2. **jichi needs no change.** `CURL_CFLAGS` and `CURL_PC_LIBS` on the make line point
   it at the build, and the Makefile already relaxes `-Wno-long-long` for the one
   object built from `src/net/jc_http.c` when curl's header is not clean under C89 —
   as on every 32-bit target, where `curl_off_t` is `long long` (M699,
   `portability_lint` check 19).
3. **Operator, `sudo`:** `aratapif` setuid root and the tun/tap modules loaded — a
   privileged helper, named here before it is needed. Then `[ETH0]` as a
   point-to-point link, and `nfeth.xif` in the guest.
4. **The model server stays loopback-bound**, as the platform rules require. The rig
   starts a forwarder on the host bound **only to the TAP link's host address** —
   never `0.0.0.0`, never the LAN — and stops it at teardown.
5. **The task is M676's**, in `scripts/_rig_live.sh`: `reply with OK`, which exercises
   the provider, the request and the SSE framing and nothing else, then a token
   minted that second and written into a file the model can only reach by calling a
   tool. Use a model `doctor --live` calls native, because a text caller executes
   nothing and looks exactly like a platform failure. Task, model and seconds are
   recorded **on the row**.

**Stop condition.** If neither libcurl links, the row keeps what steps 2 and 3
earned, and says *not Driven: no libcurl for MiNTLib*.

---

## 5. Decisions and actions that are the operator's

1. **Three `sudo` actions**, each at the step that needs it: the PPA and
   `cross-mint-essential` (step 0), `aranym` (step 1), and `aratapif` setuid with the
   tun/tap modules (step 4). Nothing on this page is installed by an agent.
2. **When.** The operator asked out of curiosity; step 0 is small enough to run on its
   own, and its number decides whether the rest is worth the time.
3. **Model time** for step 4 — the free local models only.
4. **Whether to write a MiNT platform layer**, if step 0 finds the gap needs one.

**The toolchain is an engineering choice, not the operator's:** the PPA's gcc 4.6.4
first — it installs from `apt` on this Ubuntu, and C89 needs nothing newer. If it
miscompiles, or rejects a flag the Makefile passes, Thorsten Otto's 15.2.0 is the
escape route, and the results file names the compiler of every result.

---

## 6. Rejected

- **Real hardware — but not on memory.** Out of scope because the question was about
  emulation and there is no Atari on the bench. The 2026-09-22 answer said *"out on
  memory"*, setting the machines' RAM — an [ST](https://en.wikipedia.org/wiki/Atari_ST)
  or STE at most 4 MB, a [Falcon030](https://en.wikipedia.org/wiki/Atari_Falcon)
  about 14 MB — against `doctor`'s ~16 MB peak from [`LOW_MEMORY.md`](../LOW_MEMORY.md).
  That figure is `doctor` opening TLS, and it is dominated by shared library code. The
  same page's measured floors are the fairer comparison (M265, 2026-08-02, x86-64
  Linux): one headless turn completes in **2 MB**, the unit suite needed **16 MB**,
  the smoke tier **≤ 32 MB**. They do not transfer to a static m68k binary, but they
  say a stock Falcon could plausibly hold a turn and could not hold the gates — a
  different sentence from *"out on memory"*. Anyone who owns a Falcon, a
  [TT](https://en.wikipedia.org/wiki/Atari_TT030) or a FireBee can follow
  [`VERIFY_A_PLATFORM.md`](../VERIFY_A_PLATFORM.md).
- **Hatari.** It emulates the original machines closely, which is what *"does it run
  on a real Falcon"* would need, and not what this plan asks. ARAnyM is the fast,
  large-memory FreeMiNT machine.
- **Debian m68k under ARAnyM.** Real glibc and drivable, but its only new axis is a
  native m68k Linux kernel in place of `qemu-user`'s syscall translation — narrower
  than a new libc. A different plan, if ever.
- **TOS or EmuTOS without MiNT, or libcmini.** Not POSIX: a platform layer from
  scratch, for a row that would then measure the layer rather than jichi.
- **TLS through the PPA's OpenSSL 1.0.1e to the HRZ gateway.** The hardest part of
  the cross-compile, for a connection the row does not need: plain HTTP over the
  point-to-point link to a loopback-bound server removes it, as M699 did.
- **Exposing LM Studio on the LAN for the guest.** Against the loopback rule. The
  forwarder bound to the TAP address is the whole of the exception, and it is torn
  down with the run.

---

## 7. What this page does not claim

- That jichi runs on FreeMiNT. The platform is **never compiled**, and nothing here
  changes that.
- That MiNTLib covers what jichi needs. Step 0 found that it provides every function
  jichi calls except `mmap` (§8) — which is not the same as the tree building there.
- That ARAnyM runs unattended, or that any guest filesystem can carry the tier.
  Step 1 measures both.
- That a native compiler exists in the guest. Step 2 finds out.
- Any duration. The 2026-09-22 answer's *"roughly a day"* was a guess; every timeout
  in the rig comes from a measurement ([`SESSION_RUNBOOK.md`](../SESSION_RUNBOOK.md) §2).

---

## 8. Step 0, run on 2026-09-24 — the outcome against the predictions

*Run on the workstation from a `git archive` of `8cb7cd9e`, so the working tree was
never built in. Every log is in `~/.cache/jichi-tier-v/freemint-step0-20260924-003605/`,
with `results.txt` as the summary. The toolchain came from the PPA: gcc 4.6.4 (MiNT
20251014), binutils 2.30, MiNTLib 0.60.1, PML 2.03. Nothing was executed, because
there is no emulator yet.*

**First, the instrument.** A C89 hello-world compiled and linked to an *"Atari ST M68K
contiguous executable"* before any probe answer was read. The FPU claim in §2 holds:
the default code defines only `__mc68000__`, and `-m68020-60` adds `__HAVE_68881__`.

**The build as shipped failed in 1.1 seconds, on the build system rather than the
source.** All 314 compiles died on *"cc1: error: unrecognized command line option
'-Walloca'"*. The flag arrived in GCC 7, and M472 put it on the mandatory list; gcc 4.6
rejects it with or without `-Werror`, as it rejects a made-up flag. This is the kind of
finding a platform row exists for, because it is not MiNT's: it breaks every gcc older
than 7. Two recorded rows, CentOS 7 (gcc 4.8.5) and Debian 9 (gcc 6), were green before
M472, and no milestone since records a re-run of either, so the oldest-toolchain rows
are very likely unbuildable today — an inference from the versions, not a re-run. §5 anticipated a
rejected flag in general terms; nothing on this page predicted it would be the first
thing to fail.

**With `-Walloca` probed in a scratch copy, four items remain, identical for 68000 and
for `-m68020-60`:**

| | predicted in §3 | measured |
|---|---|---|
| the monotonic clock | the fallback might apply, silently | **Confirmed.** No MiNTLib header declares `clock_gettime` and `libc.a` has no such symbol, so the probe's *absent* is a true answer, checked by hand. Deadlines would run on whole seconds. `gettimeofday` exists and is declared under strict POSIX. |
| `select()` on a pipe | the call might be the gap | **The call links; its argument does not compile.** `struct timeval` is incomplete at 13 sites in 10 files: MiNTLib's `<sys/select.h>` only forward-declares it, where POSIX.1-2001 says that header defines it. |
| process groups | `setpgid`, `kill(-pid, …)` | **No gap.** Both compile and link. |
| `termios` | the TUI's raw mode | **No gap of its own.** `jc_term.c` failed only on `struct timeval`. |
| the filesystem's case | HostFS folds case | Not measurable without the emulator — step 1. |
| 32-bit `long`, the 80-bit FPU | already handled | No compile-time sign; the FPU half is a step 2 question. |
| *(not predicted)* | | `lstat`, `readlink` and `symlink` are hidden under strict POSIX; `-D_XOPEN_SOURCE=600` exposes them. |
| *(not predicted)* | | No `<sys/mman.h>` and no `mmap`; `jc_index` already falls back to a copy (M141). |
| *(not predicted)* | | `pid_t` is undeclared in `tests/test_proc.c`. The file includes `<unistd.h>`, which POSIX says defines it, and MiNTLib's does so only at an X/Open level. *This row first blamed `<signal.h>`: the check behind it tried that header alone and generalised. Corrected at M723, measured.* |

**The link, measured two ways.** For the 299 objects that compiled, 294 external
symbols were needed, and every one that is not jichi's own came from `libc`, `libm` or
`libgcc`. Then, with the four gaps worked around in the scratch copy — a
declaration-only `sys/mman.h`, `-include sys/time.h`, `-include sys/types.h` and
`-D_XOPEN_SOURCE=600` — all 314 objects compiled, and the link lacked exactly `mmap`
and `munmap`. Relinked against a stub whose `mmap` always fails, both binaries link:
`jichi` at 2,022,933 bytes (1,451,300 of text) and `run_tests` at 3,172,410, both
relocatable TOS programs. `file` calls them *"no relocation tab"*; the header says
`absflag` 0 with a relocation table present. They contain the shims, so they are not
the tree's binaries.

**The stop condition's answer: go on.** A handful of guardable items, and one missing
function the tree already falls back from; no platform layer is needed. The rows are
in [`DEFERRED.md`](../DEFERRED.md) under *"Open — found by the FreeMiNT step 0"*, and
the platform is in [`PLATFORMS.md`](../PLATFORMS.md) as **Never compiled**: a compiler
*for* it has seen the tree, and none *on* it has.

**What changes for the next steps.** Step 2's cross-built `run_tests` can only come
from the tree once the four gaps are fixed there — the shimmed binary proves the link,
not the tree. And the `-Walloca` fix does not wait for MiNT, because it restores two
rows that have nothing to do with it. It landed as M722 the same night: with the flag
probed, gcc 4.6.4 compiles the same 299 files the patched copy did, and a wrapper that
rejects `-Walloca` as gcc 6 does builds and links `jichi`. M723 then fixed the four
MiNTLib gaps in the tree, for every file rather than the ones that failed: the tree now
cross-compiles clean under `WERROR=1` for 68000 and `-m68020-60`, and links `jichi` and
`run_tests`. So step 2's cross-built `run_tests` comes from the tree, with no shim.

---

## 9. Steps 1 and 2, run by hand on 2026-09-24 — and what they found

*`aranym-mmu` 1.1.0 from Ubuntu, the build the FreeMiNT archive's own launcher uses. Every fact
and number is in `~/.cache/jichi-tier-v/freemint-step1-20260924-025919/results.txt`. The rig
script, `scripts/tier-v-freemint.sh`, was written from this procedure at M727 (§10).*

**Step 1: FreeMiNT boots unattended.** Boot, script, halt and exit take **about 12 seconds**.
Four things the plan did not know, each now measured:

1. **The archive's EmuTOS does not start on this ARAnyM.** It is a 1024k image, and ARAnyM
   1.1.0 answers *"This 1024k ROM isn't supported by your ARAnyM version. Please use
   etos512*.img instead"* — and then does not exit. EmuTOS 1.4's own ARAnyM build is 1024k too.
   The generic `etos512us.img` from EmuTOS 1.4 boots FreeMiNT.
2. **The halt is a 132 KB program calling `Shutdown(0)`.** The kernel closes files, syncs,
   unmounts C: and E:, and ARAnyM exits by itself. No halt command ships in the image.
3. **`/dev/nfstderr` reaches the host's stderr, and the console does not.** `RedirConsole`
   carries nothing of MiNT's console. The rig's progress channel is `nfstderr`.
4. **The archive maps drive D: to the host's whole home directory, and its network sits on
   192.168.0.x,** the LAN's own subnet. The rig writes its own configuration: no D:, a
   case-sensitive E: for results, no network until step 4, no mouse grab, no audio.

**Step 2: the cross-built unit suite in the guest.** It crashed first, and the crash was a
jichi fact:

- **The crash.** At `910daa2c` the suite died with a bus error after about 156 tests, in
  `getenv` and on a rerun in `setenv`, reading address **`0x78787878`** — ASCII `xxxx`.
  FreeMiNT gives a program a **fixed 64 KB stack** (MiNTLib's `_stksize`), and
  `tests/test_vision.c` keeps a 64 KB buffer on it, filled with `x`. The stack ran over the
  environment beside it.
- **The fix, M726.** A 4 MB stack does not start ("insufficient memory": MiNT's initial
  allocation is 1,024 KB); **512 KB** carries the whole suite. M726 defines `_stksize` in the
  platform layer for every MiNT program, and `portability_lint` check 32 holds it at the top
  level of the file, since my first placement compiled to nothing inside
  `#if defined(__APPLE__)`.
- **The result.** The tree's own build **completes: 13,680 checks, 25 failures**, 23 seconds
  from power-on to halt. And **jichi runs on FreeMiNT**: `jichi --version` and
  `jichi describe` both exit 0 in the guest.

**Correction (M727).** *512 KB carries the whole suite* was measured with one test not
running. `test_symlink_escape` names its fixture after the pid, the suite is pid 5 on every
boot, and the test's cleanup never removed `sym_dir`, so the fixture outlived each run. These
hand runs shared one guest directory, so every run after the first 64 KB crash found the
fixture, failed its `mkdir`, and returned without a word. On a fresh disk the suite crashed at
512 KB, in that test's symlink loop: §10.

**What the 25 are, so far.** Most shell out to utilities this minimal guest lacks —
`sleep`, `grep`, `echo`, `false` — which is step 3's userland question arriving early. A
few may be platform differences: a FIFO on HostFS, a directory check, and file creation
under `TMPDIR=u:/tmp`, which the guest's own `mkdir -p` cannot handle ("cannot create
directory 'u:'"). None is classified one by one yet; the register row says so.

**The words.** This is *cross-built, emulated*, the M469 table's label. It is still **Never
compiled**: no compiler on FreeMiNT has seen the tree, and step 2's native half needs one.

## 10. The rig, run on 2026-09-24 — and the recursion its fresh disk found

*`scripts/tier-v-freemint.sh`, written from §9's procedure at M727. The runs are
`~/.cache/jichi-tier-v/freemint-20260924-040439-step1` and `…-041806-step2`, each with its
`results-freemint.txt`; `…-040522-step2`, before the rig ran `describe`, counted the same
checks and the same 19 failures.*

**The rig.** Step 1 boots, writes the sentinel and halts in 9 seconds. Step 2 cross-builds
`run_tests` and `jichi` from the tree (the working tree, with `--dirty`), runs both in the
guest, and halts in 31 seconds, 23 of them in the emulator. It carries every fact §9
measured:
- the archive, pinned by commit hash and sha256;
- EmuTOS 1.4's 512k image, pinned by the zip's sha256 and the image's;
- its own configuration: no D:, a case-sensitive E:, no network;
- the `mint.cnf` hook, an `sh` link to `bash`, and the `Shutdown(0)` helper;
- `TMPDIR=/tmp`, and a sentinel that carries the run id.

Exit 3 means the guest never wrote this run's sentinel, which is not a result.

**Its first step 2 crashed where the hand runs had completed.** It was a bus error at a wild
program counter (`0x20424A80`), in the same binary that completed in the hand directory. What
differed was the disk: the rig extracts a fresh one, and the hand directory had kept its
`/tmp` from boot to boot. Once that `/tmp` was emptied, the hand directory crashed too, and a
line-buffered build of the suite put the crash in `test_path`.

**Why the hand runs had passed.** `test_symlink_escape` makes
`$TMPDIR/jichi_path_test_<pid>`, and on FreeMiNT the suite is pid 5 on every boot. Its cleanup
never removed `sym_dir`, so the directory outlived every run. That happens on Linux too: 125
of them sat in this workstation's `/tmp`, harmless there only because pids vary. In the guest
the first run to create it was the 64 KB crash, at 03:03:56. Every later run failed its
`mkdir` and returned without a word (*"skip silently"*), so 18 checks never ran and nothing
reported it. M726's *512 KB carries the whole suite* was measured that way.

**The defect: a recursion sized by its bound.** `jc_path_resolve` followed each symlink hop
with a recursive call, up to 40 hops, and each call's frame held its path buffers: 16,564
bytes on m68k, and 16,704 on x86-64 (gcc, no `-O`). So a `loop -> loop` took about 700 KB of
stack before it was refused. MiNT's stack is a fixed 512 KB, and past its end lies the heap.
The host shows the same thing once it is given MiNT's stack: the curl-free suite under
`ulimit -s 512` segfaults in `realpath`, 30 frames deep on the loop, and passes under 1 MB.
The path fence calls this function for every file a model names, so a cycle in a workspace
was one `write_file` away from the same crash on FreeMiNT. That is reasoned from the code;
it was not run in the guest.

**The fix.** The resolver is a loop now: one hop per pass, in one frame (16,556 bytes on
m68k). It carries the missing tail as a suffix and appends it to the answer, as the recursion
did on its way back. The bound is the same, and so are the answers: the host suite is
unchanged.

**The checks, each shown red first.**
- **`make ci` runs the curl-free suite, the build FreeMiNT runs, under `ulimit -s 512`.** It is
  red with the old resolver (a segfault, rc 139) and green with the loop: 13,766 checks, 0
  failures. Here the fixed suite passed every run from 112 KB up, and two runs in four
  crashed at 96 KB. Its peak is just under 96 KB, so 512 KB is about five times it.
- **`portability_lint` check 33 holds that limit at or below `_stksize`.** It is red with no
  limit, with 1024, and with the limit moved to another recipe line.
- **`test_path` clears its fixture before making it, fails if `mkdir` still fails, and must
  leave nothing.** Each was proven by planting a fixture under the pid the suite would run as:
  a shell that `exec`s keeps its pid. With a stale fixture, the old test skipped silently
  (13,748 checks, 0 failures) and the new one ran all 13,766. With a fixture it cannot clear,
  the new test fails at its `mkdir`. With the cleanup made to leave `sym_dir`, it fails at the
  cleanup.

**The result in the guest: 13,707 checks, 19 failures.** There was no bus error and no
fixture left behind, and `jichi --version` and `jichi describe` exit 0. Six of M726's 25 went with
`TMPDIR=/tmp`: `test_glossary` 3, `test_refs` 2 and `test_memory` 1. The 19 are `test_tool`
5, `test_proc` 5, `test_pdf` 3, `test_app` 2, and one each in `test_gradecore`,
`test_platform` (a FIFO on HostFS), `test_envelope` and `test_bg`. They are still
unclassified, and the register row still says so.

**Found while proving it, and not fixed here.** Several tests build a path under `$TMPDIR` in
a fixed buffer and hand it to `rm -rf`. With a 127-character `TMPDIR`,
`tests/test_bounds.c`'s 128-byte `home` truncates to the `TMPDIR` itself. A traced run
executed `rm -rf "$TMPDIR"` four times. A longer `TMPDIR` truncates to a prefix, and a
prefix can be a parent directory. `tests/test_session.c` builds its `rm -rf` with `sprintf`
into 128 bytes, and would overflow instead. The gate here runs with `TMPDIR` unset, so in
`/tmp`, where nothing truncates. The register has the row.

**The words.** Unchanged: *cross-built, emulated*, **Never compiled**, and not **Driven**.

## 11. Step 4, run on 2026-09-24 — Driven, and the emulator that had no network

*The rig: `scripts/tier-v-freemint.sh --step 4` (M737). Results in
`~/.cache/jichi-tier-v/freemint-20260924-135250-step4/results-freemint.txt`.*

**The result.** Both turns of the driven task, from inside the guest: the text turn answered
`OK` (4,023 tokens in, 27 out), and the tool turn read `note.txt` and reported the phrase
minted that second, `TIER-M-5D5D0C`, 17 seconds from power-on to halt. jichi was the tree at
`a79e1357`, cross-built with `m68k-atari-mint-gcc` 4.6.4 against libcurl 8.18.0 built for MiNT
without TLS, 2,631,215 bytes. The row is **Driven**; the words stay *cross-built, emulated*
and **Never compiled**, because no compiler on FreeMiNT has seen the tree.

**What the plan did not know, in the order it was found.**

1. **libcurl builds for MiNT unchanged.** The plan named Thorsten Otto's prebuilt 7.56.0 as the
   escape route; it was not needed. curl 8.18.0's own configure, with `--host=m68k-atari-mint`,
   `--without-ssl`, `--disable-threaded-resolver --disable-ipv6 --without-zlib`, builds the
   library and the tool. `scripts/minimal-curl.sh --tls none --target m68k-atari-mint` is that
   recipe now.
2. **Ubuntu 26.04's ARAnyM has no ethernet at all** -- the first run's guest had only `lo0`,
   and `strace` showed ARAnyM never opening `/dev/net/tun`. The binary holds none of the
   TunTap code. The cause is in ARAnyM's `configure.ac`: its TUN/TAP probe calls `memset`
   without including `<string.h>`, gcc 14 made implicit declarations an error, the probe
   fails, and ethernet is compiled out with no message. That is also why the package ships
   no `aratapif`. Built from the 1.1.0 source with `ac_cv_tun_tap_support=yes`, ARAnyM has it.
   The same shape as jichi's own probes' lesson (M449), from the other side: there a probe
   said *yes* to a function the build could not see; here a probe said *no* to a feature
   that was present, because the probe's own program was not valid C.
3. **No setuid helper is needed.** In `bridge` mode ARAnyM only opens an existing tap device,
   so the operator's one-time `sudo` is three `ip` commands for a tap owned by the user, and a
   firewall rule for the one port; `ptp` mode would have needed `aratapif` setuid root.
4. **`nfeth.xif` loads from the archive's `aranym/` directory** once ARAnyM has the device;
   copying it to the system directory's root, tried while ARAnyM had none, changed nothing.
5. **The model server here listens on every address**, so the guest reached it directly at
   the tap's host end, and the rig's forwarder -- for a loopback-bound server -- was not
   used on this run.

**One more finding, registered.** Every boot of the guest produced the same session id,
`03bead13-…`: the guest has no `/dev/urandom`, and `jc_uuid`'s fallback seeds from `time()`
and an address, which are the same on every boot of this image.

**Step 3 is still not run.** The guest has `bash` and little else a POSIX smoke tier needs --
no `grep`, `sed`, `sleep` or `echo` binaries -- so step 3 waits for a MiNT userland that
carries them.

