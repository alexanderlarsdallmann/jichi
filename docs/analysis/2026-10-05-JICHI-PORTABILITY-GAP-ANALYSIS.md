# jichi gap analysis — portability across hardware platforms and operating systems

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

The question asks how portable jichi is, and whether it should "stick to its
C89-Linux-POSIX stance." The honest answer starts by correcting the premise:
**jichi's portability is already its broadest surface, and the stance is not
"Linux" — it is "POSIX," pushed to 14 architectures and ~30 operating-system
rows including FreeDOS 1.3 under QEMU.** The C89 floor is *why* the breadth
exists: the M469 sweep cross-builds all 14 architectures with one `zig cc` +
musl toolchain (five big-endian) and **found two real defects invisible to a
green `make ci`** (PLATFORMS.md:84); M472 measured that four of the five
libcs ship *no* hardening flags, which the `harden_flags_lint` (M472) now
catches; the build floor is linted (`portability_lint.sh`, M326u) and the
probe machinery is incident-hardened (M449/M476/M833). So the answer to
"should it stick to C89-Linux-POSIX?" is: **keep C89; the stance is already
beyond Linux — and native Windows should stay a decided, documented "no"**
(PORTING_WINDOWS.md is the survey of exactly where POSIX ends, and it doubles
as a graded assignment; WSL2 is the supported path).

The one thing *definitely lacking* is **the *driven* half of the retest
regime.** The register's own most-correct self-description (PLATFORMS.md:40-52)
is that a row "Verified and not Driven says *'it compiles and its tests pass
here'*; it does **not** say 'it works here,' because the agent loop is the
product and an offline tier never runs it end to end. This page is where
driven-ness goes missing, **and that is the actual gap**." The *offline* half
of portability is fully mechanized: the `PLATFORM_RETEST.md` §3 **coverage
debt** counts drivers in the tree against drivers last run per row, a lint
(`platform_retest_lint.sh`) keeps every row *datable* and the debt *computable*
("the debt is not a verdict; a stale row is a row the operator must re-run"),
and the T1–T7 trigger table says *when*. But the *driven* half — "has the
agent loop run on this platform, and when must it be re-run" — has **no debt
number, no trigger class, and no lint**: the driven register (PLATFORMS.md
§Driven) is a *per-row fact table* a human maintains, the `Partly verified`
rows (MSYS2/MINGW64, macOS, Haiku, containers, FreeDOS) each state "no model
call" or "make ci has never run here" in free prose, and **none of the T1–T7
trigger classes points at the driven side** (T7 is "the *timeout model*
changed"; the driven state appears in the §3 debt table only as a *human prose
note* — "the Driven verdict rests on M665/M668" — not a computed field). The
asymmetry is the gap: **the
register mechanized the claim it can re-run (offline green) and left the
claim it *cannot* re-run cheaply (the agent loop on N platforms) as prose a
human must restate per row.** The consequence the register itself recorded:
the first driven-ness number published was **wrong** ("it counted what THIS
PAGE records, and was written as though it counted what has been done",
PLATFORMS.md:47) — the *offline* debt exists precisely so that number is
always recomputable; the *driven* debt has no such mechanism, so its number
is only as good as the last human who checked. A secondary, smaller gap:
the five per-platform guarantee lines in `doctor` (row verdict 12607,
memBudget guard 12627, Landlock shell fence 12649-12652, uid-0 12811, M503
chmod ~11937) are each **pinned by a driver** (faults.sh:208/214,
setup_keyfile.sh:391) — but added *ad-hoc per incident* and with **no
manifest or completeness driver** asserting "every platform-conditional
guarantee produces a line"; M476 ("all EIGHT probes answered 'no' at once …
jichi built a maximally degraded binary … reporting success the whole way",
Makefile:49-54) is the register's own standing precedent that *the un-
enumerated degradation is the dangerous one*, and that precedent is applied
to the build probes, not to the runtime guarantee set.

## 1. What exists — the strongest surface in the register

- **A 14-architecture sweep, driven where runnable.** M469 (PLATFORMS.md:84):
  x86-64/x86-32/aarch64/**aarch64_be**/arm/**powerpc (BE 32)/**powerpc64 (BE)**/
  riscv32/riscv64/**s390x**/loongarch64/hexagon + **m68k** — all `zig cc` +
  musl under qemu-user; "five are big-endian, against one before"; "these are
  cross-builds run under emulation, not machines … no model call" — and then, in
  the Driven register (205-208), the *driven* half: s390x big-endian drove a
  real model turn (`TIER-A-E2891D`), all 19 runnable M542 triples drove a turn
  each ("five big-endian rows and all six MIPS among them"), aarch64 musl
  **fully static** drove a turn (M736), x86-64 musl static drove a turn.
- **~30 OS rows, each a stamped datum.** Verified (Linux glibc/musl, BSDs,
  MSYS2/MINGW64/Cygwin, containers, old userlands), Partly verified (macOS M817,
  Haiku M830, MSYS noacl), No (FreeDOS M836 — "the unit suite runs on DOS under
  QEMU; no model call: no sockets yet"). Each row names *when* it was measured
  and *which transport carried the turn*.
- **A retest regime with a *number*.** PLATFORM_RETEST.md §3: **coverage debt**
  = "drivers in the tree − drivers last run on the row", recomputed; T1–T7
  trigger classes (a *classifier*, not a calendar); `platform_retest_lint.sh`
  enforces *dateability* and that the debt stays *computable*. The M647
  incident that produced it: a row "re-run to 302 of 303 drivers **having never
  made a model call**" — the offline gate was green and the product had never
  been exercised, and the first published driven count was *wrong* because it
  "counted what THIS PAGE records" rather than what has been done (46-48).
- **The C89/POSIX stance is a *load-bearing decision*, documented.**
  PORTING_WINDOWS.md: native Windows is "a decision, not a gap … the survey of
  *exactly* where POSIX ends — and what a port would have to replace" (335-339).
  The build floor is linted (`portability_lint.sh`, M326u: the `CLOCK_MONOTONIC`
  compile-guard/link-failure defect); hardening is verified, not assumed
  (`harden_flags_lint.sh`, M472: "four of the five libcs … ship **no** hardening
  flags"); the probe machinery is incident-hardened (M449 flag-parity, M476
  `PROBE_OUT` per-PID, M833 verdict-not-chatter).
- **`doctor` surfaces the platform verdict *and* five per-platform guarantees.**
  main.c:12607 (row: OK/PARTLY/NEVER-COMPILED, each with the prose pointer),
  12627 (memBudget guard needs `/proc`), 12649/12652 (Landlock shell fence
  available/unavailable), 12811 (uid 0: "the privileged-command policy is moot"),
  ~11937 (M503: "does `chmod` actually DO anything here?"). Each of the last
  four is *pinned by a driver* (faults.sh:208/214, setup_keyfile.sh:391).
- **Physical hardware, not just emulators.** Pi Zero 2 W 64-bit (M677, driven,
  "the smallest thing in this matrix to run the agent loop"), Pi Zero 32-bit
  (M276/M454), **Pi 400 re-run 2026-10-04** (TIER-B-8838B9, 14,952/0, 420
  drivers) — the HEAD commit is itself a Pi 400 retest record.

## 2. The one thing definitely lacking

**The driven half of the retest regime has no debt, no trigger, and no lint —
while the offline half has all three.**

1. **The coverage debt is *offline* only.** PLATFORM_RETEST.md §3's number is
   "drivers in the tree vs drivers last run" — the smoke/unit tier. The *driven*
   state of a row (agent loop ran: yes/no/when) is not in the debt's arithmetic;
   `platform_retest_lint.sh` (1-30, 63-78) enforces *stamps* and *debt
   computability*, and its own comment concedes it does "NOT verify that the
   stamp belongs to the MEASUREMENT" — i.e. the lint is deliberately a
   dateability/arithmetical check; driven-ness does not appear in it at all
   (`grep -i driven tests/smoke/platform_retest_lint.sh` → zero matches).
2. **The driven register is a *human-maintained fact table*.** PLATFORMS.md
   §Driven (203-211) lists which rows ran the agent loop, with nonces and dates
   — but it is prose the operator updates; nothing recomputes it, nothing
   fails if a *newly Verified* row appears without a driven status, and the
   "three of nineteen" error (47) is the recorded consequence of a number that
   was *read off a page* instead of *computed*.
3. **No T1–T7 trigger class points at the driven side.** T7 is "the
   *timeout model* changed" (PLATFORM_RETEST.md:47); the driven state appears
   in the §3 debt table only as a *human prose note* — "the Driven verdict
   rests on M665/M668" (PLATFORM_RETEST.md:80) — not a computed field. The
   question "for *which platforms* does a tree change demand a driven
   retest?" therefore has no owner: the debt answers it offline; the driven
   register answers it by eyeball.
4. **The `Partly verified` rows state their driven-ness in free prose** ("no
   model call", "make ci has never run here", macos.md first-build notes) —
   not in a field, so a reader must re-derive the answer the register says it
   wants to make "a lookup rather than a search" (44).

Why this, and not the guarantee-aggregation seam or the Windows stance, is the
*one* thing:

- **The register named it.** "This page is where driven-ness goes missing, and
  **that is the actual gap**" (PLATFORMS.md:44). The register does not leave
  this ambiguous; my finding is to *state its mechanism* (debt/triggers/lint
  exist offline; none exist for the driven half) and its *consequence* (the
  2026-09-18 wrong count).
- **The guarantee set is *present and pinned* per line** (faults.sh:208/214,
  setup_keyfile.sh:391). Its real defect — no *completeness* driver, ad-hoc
  additions, the M476 "unenumerated degradation is the dangerous one" precedent
  unapplied to this class — is *secondary*: it is a "no mechanism for
  completeness" claim against a set that is *individually covered*. The
  driven-debt gap is a "no mechanism at all" claim against a class the register
  *itself* labels the actual gap.
- **The Windows stance is a *decision*, documented as such** (335-339) with the
  porting survey written. "Should jichi port to Windows" is *answered*: no, and
  here is exactly why, and here is the WSL2 path. A decided, argued, documented
  no is not a gap; arguing for the port would be the fabrication.
- **The C89 stance is the *reason* the breadth exists** (one toolchain, 14
  architectures, the M469 defects found). Recommending its abandonment would
  be the dishonest move; the honest recommendation is to *keep C89, keep POSIX
  as the ceiling, keep Windows a no*, and close the driven-debt gap.

## 3. Why "definitely lacking" — alternatives and their status

- *"jichi is Linux-only."* **False.** 14 architectures, ~30 OS rows, FreeBSD
  (M465, the first non-Linux libc, "Verified"), Haiku M830, macOS M817, MSYS2/
  MINGW64/Cygwin, FreeDOS M836, containers, old userlands (Debian 5-9, CentOS
  6-7). The premise of the user's question ("C89-**linux**-POSIX") is the
  register's own stale shorthand; the stance is C89-**POSIX**, and POSIX is
  where the breadth is won.
- *"The retest regime is missing."* **False — for the offline half.**
  PLATFORM_RETEST.md is a *classifier* ("deliberately not a calendar"), the
  coverage debt is a *number*, the lint is *enforced*, and the re-runs happen
  (Debian 9 re-run M736 with three product fixes found *in-row*; Pi 400 re-run
  2026-10-04 at HEAD). The gap is the *driven* half, not the regime.
- *"doctor doesn't tell you what's degraded on your platform."* **Mostly
  false, one true.** Five per-platform lines exist and four are pinned by
  drivers (12627/12649/12652/12811/M503). What's *true* is narrower: there is
  no *completeness* mechanism (a manifest or a driver asserting "every
  platform-conditional guarantee produces a line"), so the next silently-degrading
  guard (M476's class: "a probe that cannot distinguish 'the feature is absent'
  from 'I could not run the test' is not a probe") can land un-pinned. That is
  a *secondary* finding, and I keep it secondary.
- *"Should jichi port to native Windows?"* **The register says no, and the
  evidence supports it.** PORTING_WINDOWS.md surveys "exactly where POSIX ends";
  MSYS2/MINGW64 already give a *partly-verified* Windows row with the known
  noacl gap (127); the register's own cost of a port (the Win32 process/terminal/
  signal/socket layers) is the reason WSL2 is the documented path (339: "Use
  WSL2"). I do not recommend the port; I recommend *keeping the stance and
  stating it at the one place it is currently stale*.
- *"The gap is the breadth (one more OS/arch)."* **False — the breadth is the
  point.** The register's own M469 result ("two real defects, both invisible
  to a green `make ci`") and the M472 hardening result are the *value* of the
  breadth. The finding is not "add more rows"; it is "make the *driven* claim
  on existing rows as computable as the *offline* claim".

## 4. Evidence

- **The register names the gap.** PLATFORMS.md:40-52, verbatim: "A row that is
  *Verified* and not *Driven* says 'it compiles and its tests pass here'; it
  does **not** say 'it works here', because the agent loop is the product and
  an offline tier never runs it end to end. **This page is where driven-ness
  goes missing, and that is the actual gap.** … the first count published with
  it, *'three of nineteen'*, was **wrong**. It counted what THIS PAGE records,
  and was written as though it counted what has been done." (Verified — read
  40-52.)
- **The lint's own scope concedes the driven half is out of it.**
  tests/smoke/platform_retest_lint.sh:1-30, 63-78: "a platform verdict must
  stay **computable** (M647)"; checks (1) the "N drivers in the tree" figure
  exists (104-110), (2) "every row is stamped" (78) — and the tooth-proof
  (25-29): "check 2 does **NOT** verify that the stamp belongs to the
  MEASUREMENT. … Tying a stamp to a specific column …". No check reads a
  driven field: `grep -i driven tests/smoke/platform_retest_lint.sh` is
  empty. (Verified.)
- **The coverage debt is offline arithmetic.** PLATFORM_RETEST.md §3: "the
  debt is arithmetic on two numbers" (drivers in the tree; drivers last run on
  the row), with the M647 incident's 302/303-no-model-call as the motivating
  case. T7 (the last of the seven) is "The **timeout model** changed"
  (PLATFORM_RETEST.md:47); "driven" appears in the file only in §3's per-row
  provenance notes — e.g. :80, "the Driven verdict rests on M665/M668" —
  never as a field any trigger class or the lint computes. (Verified.)
- **The driven register is a per-row fact table, prose-maintained.**
  PLATFORMS.md:203-211: s390x "both turns, 2026-09-21, through
  `tier-v-arch.sh --drive` … agentic phrase `TIER-A-E2891D` on a big-endian
  binary"; M542 "yes, 19 of 19 runnable … each reporting its own nonce";
  aarch64 musl static "both turns … `TIER-A-DA0D95`"; the No-column rows:
  containers "no model call was made inside a container" (196), FreeDOS "no
  model call: no sockets yet" (222). The table *is* the mechanism — a human
  updates it. (Verified.)
- **The offline re-runs demonstrably happen (the regime is not dead).**
  Debian 9 M736 re-run (3 retest findings, 3 product fixes in-row: pre-2.13
  `git stash push`, pre-2.17 worktree leak, `Expect: 100-continue`); Pi 400
  2026-10-04 (TIER-B-8838B9, the HEAD commit itself: "the Pi 400's rows record
  rehearsal 6"); M469 found two defects "invisible to a green `make ci`".
  (Verified.)
- **The guarantee lines exist and are pinned per line.** main.c:12627
  (memBudget/procfs), 12649/12652 (Landlock fence available/unavailable),
  12811 (uid 0), ~11937 (M503 chmod-works-here); tests/smoke/faults.sh:208
  ("memBudgetMb is set but cannot be enforced"), 214-215 (M503 noacl),
  tests/smoke/setup_keyfile.sh:391 ("doctor now reports what this machine is,
  and warns when memBudgetMb is set [without procfs]"). (Verified.)
- **The completeness precedent: M476.** Makefile:44-54: "all **EIGHT** probes
  answered 'no' at once and jichi built a **maximally degraded** binary …
  reporting success the whole way. A probe that cannot distinguish 'the
  feature is absent' from 'I could not run the test' is **not a probe**."
  The per-probe machinery was fixed (PROBE_OUT per-PID); there is no
  analogous *completeness* driver for the runtime guarantee set
  (`grep -rln "platform-safety\|safety manifest\|every capability"
  tests/ src/` → empty). (Verified.)
- **The Windows stance is a documented decision.** PLATFORMS.md:335-339:
  "**Windows, natively.** … This is a decision, not a gap: the survey of
  *exactly* where POSIX ends … is `PORTING_WINDOWS.md`, which doubles as a
  graded assignment. **Use WSL2.**" The supported Windows rows (MSYS2 3.6.10
  "Partly verified … file-privacy needs an `acl` mount", 127) already exist.
  (Verified.)

## 5. Implications

- **The strongest *claim* in the matrix is the one that cannot be recomputed.**
  "Verified" (offline) is backed by a debt number and a lint; "Driven" is
  backed by a prose table. A row that is Verified-and-not-Driven reads, to a
  learner or a junior, as "it works here" — the exact conflation the register
  warns against at 40-43 — and the only thing that keeps the distinction honest
  is the human who maintains §Driven. The *offline* debt exists so the 2026-09-18
  error (302/303 drivers, no model call, "three of nineteen" published) can
  *not recur*; the *driven* half has no such recurrence-prevention, so it can.
- **No trigger class has a driven target.** T6 — "a new smoke driver
  exercising a syscall, a device, or a terminal behaviour" — puts "every row,
  in proportion to §3" in doubt, and the register calls it "the creeping one,
  and … the reason for §3" (PLATFORM_RETEST.md:46, 50-51); §3 is the *offline*
  debt. The register's own statement that "an offline tier never runs [the
  agent loop] end to end" (PLATFORMS.md:42) thus has no matching trigger:
  *which rows' driven state does a tree change update?* The debt answers
  offline (drivers in tree vs last run); the driven register answers by the
  operator opening §Driven and editing. The target has no trigger at all.
- **The breadth's value is *found bugs*, and the driven half is where the next
  class of them hides.** M469 (two defects invisible to `make ci`), M472
  (four libcs with no hardening defaults), the FreeBSD M465 row (three symbols
  "four Linux libcs made look portable" are absent), the Pi Zero's M664
  quadratic reader ("every platform had it; only a 512 MB board exposed it").
  Every one of these is a *driven* or *low-resource* finding, not an offline-
  green finding. The register's own history is the argument that the driven
  half is not ceremonial — it is where the product is found out.
- **The secondary completeness gap has a measured precedent.** M476's "eight
  probes answered no at once, reporting success" is the *build* version of
  "the unenumerated degradation is the dangerous one"; the five runtime
  guarantee lines (12627/12649/12652/12811/M503) are the *ad-hoc, per-incident*
  fix of the same class at the runtime. No manifest means the next guard that
  degrades (e.g. `JC_HAVE_POSIX_SPAWN` absent → `fork` fallback;
  `JC_HAVE_STREAMS_PTY` absent → pty-driven tests skip) can land un-pinned,
  exactly as M476 landed.

## 6. Recommendation

1. **A driven debt, mirroring the offline debt.** Add a per-row `driven:`
   field to the platform table (yes/no/nonce+date — the §Driven data, *in the
   row*), and a `driven coverage debt` = "rows whose code changed since their
   driven nonce" (the T8 set, per platform). The offline debt counts drivers;
   this counts *rows since last driven*. `platform_retest_lint.sh` gains a
   check: every row has a `driven:` field, and the debt is computable — the
   same arithmetic, one plane up. The "three of nineteen" class of error
   (a number read off a page) becomes structurally impossible.
2. **A T8.** The seven trigger classes are step-change invalidations of
   *offline* state (T1 probes, T2 platform conditionals, T3
   signal/process/terminal discipline, T4 libcurl, T5 filesystem/permission
   semantics, T6 drivers, T7 timeout model); none points at the driven side.
   Add the eighth: "any code
   that a live model turn executes changed — retest the *driven debt* rows:
   the platforms whose code moved since their nonce, in proportion to §3, the
   same arithmetic, one plane up." Then T6's "every row, in proportion to §3"
   and T8's "every driven row, in proportion to §3" form the two planes of one
   debt.
3. **A `driven` column in the matrix, rendered by the existing table.** The
   §Driven register (203-211) becomes *the table's column*, not a prose list —
   "the answer to 'has the agent loop ever run here?' is a lookup rather than
   a search" (44) becomes literally true: it is a cell in the row.
4. **A guarantee manifest + completeness driver (the secondary gap).**
   `docs/PLATFORMS.md` (or a new `docs/CAPABILITIES.md`): the table of
   platform-conditional guarantees (memBudget/procfs, Landlock, uid-0,
   chmod-works, posix_spawn, streams-pty, …) with the doctor line that
   surfaces each and the driver that pins it. A lint that walks the manifest
   and asserts *each* line is emitted and *each* driver fires — M476's
   "unenumerated degradation is dangerous" precedent applied to the runtime
   set, the way it was already applied to the build probes.
5. **Keep the stance; state it once, correctly.** C89 stays (the breadth is
   the payoff); POSIX stays the ceiling; native Windows stays a documented no
   (PORTING_WINDOWS.md + WSL2). If the "C89-linux-POSIX" shorthand survives in
   any header, it is stale and should read "C89-POSIX" — the matrix itself
   already says so (the M465 row: "the first non-Linux libc … every prior
   `HAVE_*` probe result came from one kernel's ecosystem").

Rejected, in the register's spirit:

* *Port to native Windows.* The register surveyed exactly where POSIX ends
  and *decided* (335-339). A port is a multi-layer effort (process, terminal,
  signal, socket) with MSYS2/MINGW64 already providing a partly-verified path
  and WSL2 the documented full path. Recommending the port would override a
  measured decision with an unmeasured one.
* *Add more OS/arch rows.* The breadth is the *value* (M469's two defects,
  M472's hardening finding, the Pi Zero's quadratic reader). The gap is the
  *driven* claim on *existing* rows, not the row count.
* *Abandon C89.* C89 is the *reason* the 14-architecture sweep is one toolchain
  (`zig cc`), one dialect, one `WERROR=1` bar. The M472/M449/M833 probe
  hardening all presuppose it. Its abandonment is the antithesis of the
  breadth.
* *Automate the driven debt into `doctor`.* The register's own M400 precedent
  (a single platform statement, unified once it self-contradicted) and the
  T1–T7 design (a *classifier*, not a calendar) say the driven state belongs
  in the *matrix + lint*, not in a `doctor` checkmark — the same D1-shape
  reasoning as the model-fit probe.

## 7. Limitations and honest counterarguments

1. **The register *named* this gap before I did.** PLATFORMS.md:44 ("This
   page is where driven-ness goes missing, and that is the actual gap"). My
   contribution is not the *claim* but the *mechanism* (offline debt/triggers/
   lint exist; none exist for the driven half) and the *secondary* finding
   (the guarantee-set completeness gap, with M476 as precedent). Framing it as
   "I discovered the register's actual gap" would be the dishonest move; the
   register is honest, and so is this analysis.
2. **The retest regime *works*.** The re-runs happen (Debian 9 M736, Pi 400
   2026-10-04, M469's re-sweep M542), the debt is a number, the lint is
   enforced, the T1–T7 classifier is argued. I am not claiming the regime is
   broken; I am claiming the *driven* half of it is prose where the *offline*
   half is arithmetic.
3. **The five guarantee lines *exist* and four are *pinned*.** The
   completeness finding is a "no *manifest* / no *driver for the set*" claim,
   not a "no lines" claim. faults.sh:208/214 and setup_keyfile.sh:391 are
   real teeth. Overstating it to "doctor doesn't tell you" would be false,
   and the register's own M400/M326q/M503/M831 history is the record that it
   *does* tell you, line by line, as it finds out.
4. **I have not run `tier-v-arch.sh --drive` or the retest scripts** (read-
   only; they need the target machines). The "driven register is prose-
   maintained" claim is verified by reading PLATFORMS.md:203-211 and the lint
   (1-30); the *behaviour* (a re-run actually happens) rests on the register's
   own dated records (M736, M677, TIER-B-8838B9), which I read but did not
   execute.
5. **"Should jichi port to Windows?" is a *product* question, and the register
   *answered* it.** My role is to confirm the answer is evidence-backed (it is:
   PORTING_WINDOWS.md, the M475 WSL2 full-gate row, the MSYS2 partly-verified
   row, the M833 DJGPP probe findings) and to keep the analysis from
   re-litigating a decided no. The recommendation "keep the stance" is the
   register's own stance, confirmed.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the register names the driven gap itself
    sed -n '40,52p' docs/PLATFORMS.md                    # "actual gap", "three of nineteen" wrong

    # the offline debt + its lint (dateability, not driven)
    sed -n '1,30p;78,110p' tests/smoke/platform_retest_lint.sh
    grep -in "driven" tests/smoke/platform_retest_lint.sh    # (expect: nothing — the lint never reads driven state)
    grep -n "coverage debt\|drivers in the tree" docs/PLATFORM_RETEST.md | head

    # the driven register: per-row prose, nonces, dates
    sed -n '203,211p' docs/PLATFORMS.md                  # s390x, M542, aarch64 static driven
    sed -n '196p;222p' docs/PLATFORMS.md                 # containers/FreeDOS: no model call

    # the breadth: 14 archs, driven where runnable, defects found
    sed -n '84,85p' docs/PLATFORMS.md                    # M469/M542
    sed -n '205,208p' docs/PLATFORMS.md                  # driven nonces on the same rows

    # the re-runs (the regime is alive)
    grep -n "M736\|M677\|TIER-B-8838B9\|2026-10-04" docs/PLATFORMS.md | head

    # the five per-platform guarantee lines in doctor, each pinned
    sed -n '12607p;12627p;12649p;12652p;12811p' src/main.c
    grep -n "M503.*chmod" src/main.c                     # the chmod-works-here line
    grep -n "cannot be enforced\|M503\|noacl" tests/smoke/faults.sh tests/smoke/setup_keyfile.sh | head

    # the completeness precedent: M476 (build) and its absence at runtime
    sed -n '44,54p' Makefile                             # "all EIGHT probes answered no"
    grep -rln "platform-safety\|safety manifest\|every capability" tests/ src/ --include=*.c --include=*.sh
                                                          # (empty = no runtime completeness driver)

    # the Windows stance: a documented decision
    sed -n '335,339p' docs/PLATFORMS.md                  # "a decision, not a gap … Use WSL2"
    sed -n '127p' docs/PLATFORMS.md                      # MSYS2 partly-verified, noacl

**Verdict line for the register this could seed:** jichi's portability is its
broadest surface and its stance is *already* beyond Linux: the M469 sweep
cross-builds **14 architectures** (five big-endian) with one `zig cc` + musl
toolchain under `WERROR=1`, the Driven register shows the sweep **drove a real
model turn** on s390x (`TIER-A-E2891D`), on all 19 runnable M542 triples, on
aarch64 musl **fully static** (M736), and on x86-64 musl static — "five
big-endian rows and all six MIPS among them, each reporting its own nonce"
(PLATFORMS.md:205-211). The OS matrix reaches ~30 rows including FreeBSD
(M465, "the first non-Linux libc"), macOS (M817), Haiku (M830), MSYS2/MINGW64/
Cygwin, containers, old userlands, and FreeDOS 1.3 under QEMU (M836); physical
hardware (Pi Zero 2 W 64-bit/32-bit, Pi 400 re-run at HEAD) is driven; the
build floor (`portability_lint.sh`, M326u), the hardening check
(`harden_flags_lint.sh`, M472: four of five libcs ship no hardening defaults),
and the probe machinery (M449/M476/M833) are incident-hardened; and native
Windows is a *decided, documented no* with the porting survey written
(PORTING_WINDOWS.md, 335-339: "a decision, not a gap … Use WSL2"). The one
thing *definitely lacking* is the **driven half of the retest regime**: the
*offline* claim ("Verified") is backed by a computable **coverage debt**
(PLATFORM_RETEST.md §3), a **T1–T7 trigger classifier** ("deliberately not a
calendar"), and a **lint** (`platform_retest_lint.sh`) that keeps every row
datable and the debt arithmetic — but the *driven* claim ("has the agent loop
run here, and when must it re-run") has **no debt number, no trigger target,
and no lint**: the Driven register (PLATFORMS.md:203-211) is a prose fact
table a human updates, the `Partly`/`No` rows state "no model call" in free
prose, and no trigger class updates driven *state*: T2 ("a platform
conditional … added or changed" → "the rows on the *other* side of the
branch", PLATFORM_RETEST.md:42) puts such rows in doubt but only the offline
side has a mechanism to act on it — a tier re-run plus the §3 arithmetic debt;
the driven side's re-run needs a live model and the physical rig, and no
number tracks which driven rows have drifted. The register *named* it ("This
page is where driven-ness goes missing, **and that is the actual gap**", 44)
and *recorded its consequence* (the 2026-09-18 FreeBSD row re-run to 302/303
drivers "having never made a model call," with the first published count
"three of nineteen" **wrong** because it "counted what THIS PAGE records" —
46-48). The answer to "should jichi stick to C89-Linux-POSIX?": **keep C89,
keep POSIX the ceiling, keep Windows a no — the stance is already broad, the
breadth is the value (M469's two defects, M472's hardening finding, the Pi
Zero's quadratic reader are all *driven* or *low-resource* finds) — and close
the driven-debt gap**: (1) a per-row `driven:` field in the matrix with a
driven coverage debt ("rows whose code moved since their nonce") and a
`platform_retest_lint.sh` check that it is computable — the offline debt, one
plane up; (2) a **T8** naming its target ("any code a live model turn executes
changed → retest the driven-debt rows; T7 is the timeout model, not the live
gate"); (3) the §Driven prose table becoming the matrix's own column, so "a lookup
rather than a search" (44) is literally true; (4) a guarantee manifest +
completeness driver for the five per-platform doctor lines (memBudget/procfs
12627, Landlock 12649/12652, uid-0 12811, M503 chmod ~11937, row 12607) — each
already pinned (faults.sh:208/214, setup_keyfile.sh:391) but added ad-hoc, with
M476's "a probe that cannot distinguish 'the feature is absent' from 'I could
not run the test' is not a probe" applied to the runtime set the way it was
applied to the build probes. Revisit when: a new Verified row cannot appear
without a `driven:` field; the driven debt is a number; T8 (the driven-side
trigger) names its target;
and a manifest lint fails when a platform-conditional guarantee lacks a doctor
line and a pinning driver — at which point the register's own "actual gap"
(44) is closed with the same arithmetic the offline half already uses, and the
"three of nineteen" class of error is structurally impossible.
