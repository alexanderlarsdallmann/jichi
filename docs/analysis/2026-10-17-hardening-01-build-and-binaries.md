# Hardening review 1 — The build and the binaries it makes

*Part of the 2026-10-17 hardening and platform review of jichi; start with
[the overview](2026-10-17-hardening-00-overview.md). Drafted by reviewer E (Claude Opus 5.5), edited by the
review's lead. Commit `e5d832a4`; every `path:line` in this chapter refers to that commit. The
measurements were made on an x86-64 workstation (Ubuntu 24.04, gcc 13.3, clang 18, glibc 2.39) in
private copies of the tree; the review lead re-checked the central claims by hand and measured the build
matrix (cited below as the lead's measurements L-01 to L-15).*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who knows a little C and wants to know what a C build can do
to make a program harder to exploit -- and how to check that it really did. You do not need to know
jichi; the chapter explains each term the first time it appears. If you want to know how jichi runs on
other operating systems, read [chapter 6](2026-10-17-hardening-06-platforms.md) instead or afterwards.

**After reading it you will be able to:**

- name the binary-level mitigations a modern Linux toolchain offers (PIE, RELRO, BIND_NOW, a
  non-executable stack, the stack protector, stack-clash protection, CET, `_FORTIFY_SOURCE`), say what
  each one stops, and check each one in a binary with `readelf`;
- explain why jichi's default build is unoptimised, what that costs, and how to get an optimised,
  fortified build today;
- read a Makefile probe and say what question it asks the compiler;
- judge what a static analyzer's silence is worth;
- act on the five build findings below, in the order given in "Recommendations".

**You need first:** a terminal, a C compiler, GNU make, and a checkout of jichi built once with `make`
(see `docs/BUILD.md`). `readelf` comes with binutils.

---

## 1. How the build decides what to ask the compiler for

jichi has no `configure` script. Its `Makefile` asks the compiler questions every time it runs and turns
each answer into a flag or a macro. A *probe* is a tiny C program compiled with the flags the real build
will use: if it compiles, the answer is yes. The most important probe is the one that decides whether a
compiler flag may be used at all (`Makefile:201-202`):

```make
cc_warn_ok = $(shell o=$$(printf 'int main(void){return 0;}\n' \
  | $(CC) -Werror $(1) -xc - -o $(PROBE_OUT) 2>&1) && [ -z "$$o" ] && echo $(1); ...)
```

A flag is accepted only if the compiler takes it with `-Werror` **and prints nothing**. Each half of
that rule came from a platform that broke the simpler version. OpenBSD's clang accepted
`-fstack-clash-protection` and then warned that the argument was unused, which `-Werror` turned into a
failure of every file (M479). DJGPP's gcc printed "-fPIC ignored" on standard output with exit status 0,
and that sentence ended up on the compile line (M833). The lesson for any build system: a probe must ask
the exact question the build will ask, and "the compiler did not complain" is part of the question.

All the security flags are chosen this way (`Makefile:744-754`). `make info` prints what was chosen.
On the workstation:

```sh
make info | grep -E "^(HARDENFLAGS|HARDENLDFLAGS|OPT) "
```

```text
HARDENFLAGS    = -fstack-protector-strong -fstack-clash-protection -fcf-protection -Wformat -Werror=format-security -fPIE
HARDENLDFLAGS  = -fstack-protector-strong -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -pie
OPT            =  (none: _FORTIFY_SOURCE inert -- see the Makefile note)
```

(I ran this command, and every other command block in this chapter, in exactly this form, from the top
of a built checkout.) A toolchain that lacks a flag builds without it, and `make info` shows that.
`HARDEN=0` switches the whole block off, for a bisect or a toolchain whose probe lies.

## 2. The mitigations, one at a time, and how to check each one

A *mitigation* does not remove a bug. It makes a bug harder to turn into an attack: an overflow that
would have let an attacker run their own code becomes a crash instead. Here is what each flag asks
for, what it stops, and how to see it in the binary. All outputs below are from the default build of
`e5d832a4` on the workstation. The commands are binutils' `readelf` and grep, run from the top of the
built checkout; none of them is a jichi command.

**PIE (position-independent executable), `-fPIE` + `-pie`.** The operating system loads the program at
a random address each run (*ASLR*, address space layout randomisation), so an attacker cannot know in
advance where its code lives. Without PIE only the libraries move; the program itself sits at a fixed
address.

```sh
readelf -hW jichi | grep "Type:"
```

```text
  Type:                              DYN (Position-Independent Executable file)
```

A non-PIE program says `EXEC` here.

**RELRO and BIND_NOW, `-Wl,-z,relro -Wl,-z,now`.** A dynamically linked program calls library functions
through a table of addresses, the GOT (global offset table). If an attacker can overwrite one entry,
the next call goes where they want. *RELRO* (relocation read-only) makes the table read-only after
start-up; *BIND_NOW* resolves every entry at start-up, so the whole table can be made read-only ("full
RELRO") instead of only part of it.

```sh
readelf -lW jichi | grep -E "GNU_RELRO|GNU_STACK"
readelf -dW jichi | grep -E "BIND_NOW|FLAGS"
```

```text
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x10
  GNU_RELRO      0x1ecd30 0x00000000001edd30 0x00000000001edd30 0x0112d0 0x0112d0 R   0x1
 0x000000000000001e (FLAGS)              BIND_NOW
 0x000000006ffffffb (FLAGS_1)            Flags: NOW PIE
```

A `GNU_RELRO` segment plus `BIND_NOW` is full RELRO.

**A non-executable stack, `-Wl,-z,noexecstack`.** The stack holds data, never code. The `GNU_STACK` line
above says `RW` -- readable and writable, not executable (an executable stack would say `RWE`). Code an
attacker writes into a stack buffer cannot run.

**The stack protector, `-fstack-protector-strong`.** The compiler puts a secret value (a *canary*)
between a function's local arrays and its return address, and checks it before returning. A buffer
overflow that runs over the canary is caught and the program stops (`__stack_chk_fail`) instead of
jumping to an address the attacker chose. `-strong` protects every function with a local array or a
local whose address is taken.

```sh
readelf -sW --dyn-syms jichi | grep -c "__stack_chk_fail"
```

```text
2
```

Any count above 0 on glibc means the check is linked in. (On OpenBSD the handler has another name, which
is why the project's own lint first proves its detector works on each toolchain -- see section 4.)

**Stack-clash protection, `-fstack-clash-protection`.** A function with a very large local frame could
move the stack pointer past the guard page below the stack and into another memory region. The compiler
touches every page as the frame grows, so the guard page is always hit. There is no single `readelf`
line for it; `make info` is the evidence that it was asked for.

**CET, `-fcf-protection` (x86 only).** Intel's control-flow enforcement: *IBT* marks the legal targets
of indirect jumps, *SHSTK* keeps a second, protected copy of return addresses.

```sh
readelf -nW jichi | grep "x86 feature"
```

```text
      Properties: x86 feature: IBT, SHSTK, x86 ISA needed: x86-64-baseline
```

The note says the binary is compatible; whether it is *enforced* depends on the CPU, the kernel and the
C library [GK]. The ARM equivalent, `-mbranch-protection=standard`, is not asked for today (finding
E-05).

**`_FORTIFY_SOURCE`.** With this macro, glibc replaces calls such as `memcpy`, `vsnprintf` and
`realpath` with checked variants (`__memcpy_chk`, ...) wherever the compiler knows the size of the
destination, and stops the program on an overflow. Level 2 uses sizes known at compile time; level 3
also uses sizes known only at run time (`__builtin_dynamic_object_size`). It needs optimisation to work:
the compiler only learns object sizes while optimising. The next command lists the checked functions
the binary imports:

```sh
readelf -sW --dyn-syms jichi | grep -o "__[a-z0-9]*_chk@" | sort -u
```

On the default build it prints nothing. After `make clean` and `make OPT=-O2 jichi` it prints ten:

```text
__fdelt_chk@
__fprintf_chk@
__memcpy_chk@
__printf_chk@
__realpath_chk@
__sprintf_chk@
__strcat_chk@
__strncat_chk@
__vfprintf_chk@
__vsnprintf_chk@
```

The lead measured the same: 0 at `-O0`, 10 at `-O2`, 11 with `SIZE=1` (L-02). Two of them are worth a
second look. `__vsnprintf_chk` is jichi's own `jc_snprintf` being checked: every formatted string in the
program goes through it. `__fdelt_chk` is the check on `FD_SET`; the tree has no guard of its own against
a descriptor number above `FD_SETSIZE` (recorded by the project's internal portability analysis of 2026-10-15), and in an
optimised build glibc supplies one.

### Summary

| mitigation | stops | jichi asks for it | in the default binary | check |
|---|---|---|---|---|
| PIE | fixed code addresses | yes, probed | yes | `readelf -h`: `DYN` |
| full RELRO | GOT overwrite | yes, probed | yes | `GNU_RELRO` + `BIND_NOW` |
| NX stack | code on the stack | yes, probed | yes | `GNU_STACK RW` |
| stack protector | return-address overwrite | yes, probed | yes | `__stack_chk_fail` |
| stack clash | jumping the guard page | yes, probed | asked for | `make info` |
| CET (x86) | control-flow hijack | yes, probed | marked | `readelf -n` |
| aarch64 branch protection | the same on ARM | **no** | -- | -- |
| `_FORTIFY_SOURCE` | overflows in libc calls | only with `OPT=`/`SIZE=1`, level 2 | **no** (0 `_chk`) | `_chk@` symbols |
| `-ftrivial-auto-var-init=zero` | reading uninitialised stack | **no** | -- | -- |

The installed binary on the development workstation (`/usr/local/bin/jichi`, built 2026-10-16) has all
of the "yes" rows and zero fortify symbols (L-01).

## 3. Design decision: no optimisation by default, fortify only on request

**The decision.** The default build passes no `-O` flag at all. An `OPT` variable, empty by default,
adds one; `_FORTIFY_SOURCE=2` is added only when `OPT` or the size profile (`SIZE=1`, which brings `-Os`)
is set (`Makefile:755-768`):

```make
  ifneq ($(strip $(OPT)$(SIZEFLAGS)),)
    HARDENFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
  endif
```

**Why.** The comment at `Makefile:763-768` gives build speed and debuggability: an unoptimised binary
compiles fastest (the lead measured a serial build at 8 s against 21 s at `-O2`, L-02) and a debugger
shows every variable. The milestone that added the hardening block, M472 (ROADMAP, "the fence had four
channels and covered two"), introduced `OPT` so that the default build stayed byte-identical while
fortify became reachable at all. The 2026-08-17 source-hardening audit had recommended more: `-O1` (or
`-Og`) as a floor for the default build, so fortify would always be live
(`docs/analysis/2026-08-17-source-hardening-audit.md:815-830`). That alternative was not adopted. The
2026-10-04 security audit (§2.1) calls the result a conscious trade-off.

**What it implies -- the good.** The developer loop is fast, the gate's many clean rebuilds stay
cheap, and the debugger experience is the best it can be. Nothing about the trade-off is hidden: `make
info` says "`_FORTIFY_SOURCE` inert".

**What it implies -- the bad.**

- The documented install path is `make -j4` followed by `sudo make install` (`docs/BUILD.md`,
  "Install"), and `install` does not build (M586) -- it copies whatever the last `make` produced. So the
  binary people run is the debug build: unoptimised and without fortify (L-01).
- Optimisation is also a *performance* question. On the workstation, the 23 fuzz targets (the SSE, JSON,
  YAML, patch and provider parsers, 20,000 iterations each) take 0.95 s of CPU at `-O0` and 0.49 s at
  `-O2` -- about half -- and the binary is 16% smaller (2,230,056 against 1,881,040 bytes; L-02 also
  measured `SIZE=1` at 1,552,072, -30%). A whole mock agent turn shows the same: 4.54 s of CPU at `-O0`
  against 2.54 s at `-O2` ([chapter 7](2026-10-17-hardening-07-resources-and-small-boards.md), section
  3.5, which also has the small-board numbers).
- No stage of `make ci` builds with optimisation (section 5), so the fortified program is a program the
  gate never runs.
- `docs/HARDENING.md` (§7b, "What the review checked and found sound") says that fortify "produces no
  `__*_chk` symbols even in an optimised build -- and that is correct", because jichi bans `sprintf` and
  `strcpy`. The measurement above contradicts it: ten checked functions in an `-O2` build, including
  `__vsnprintf_chk` behind `jc_snprintf`. The page should be corrected (it is a claim, not a mitigation;
  the 2026-08-17 implementation report had already listed `__memcpy_chk` and others at `-O2`).

**The lesson.** A default that is right for the person editing the code is not automatically right for
the person running it. When the two differ, give the release its own named mode and point the install
instructions at it, rather than letting the install copy whatever the last edit-compile cycle produced.

## 4. Design decision: every hardening flag is probed, and the binary is checked

**The decision.** Before M472 jichi set no hardening flags at all, and the binary on the developer's
Ubuntu machine had most mitigations anyway, because Ubuntu's gcc turns them on by itself. On four of
the five C libraries the project supports, a compiler has no such defaults
(`tests/smoke/harden_flags_lint.sh:4-12`). So the flags were added -- each one probed with
`cc_warn_ok`, never assumed -- and a lint was written that compares what the build *asked for* (read
from `make info`) with what the binary *has* (read with `readelf`).

**Why the lint compares "asked" with "got".** A fixed list of required mitigations would fail an honest
platform whose linker has no `-z relro`, and a lint that fails honest platforms gets weakened until it
checks nothing (`docs/analysis/2026-08-17-hardening-implementation.md`, "The lint took three tries").
Comparing ask with got fails only on a broken promise. And the lint first proves, on each toolchain, that
its detectors can see a difference: it builds a trivial program twice, without and with the flags, and
trusts a detector only if its answer changes (M479, after OpenBSD's differently named canary handler made
it report a missing protector that was in fact present).

**What it implies.** Good: every platform row builds, every row can state its own posture, and a flag that
silently stops working is caught (it was: a `comma` variable defined after its use made every `-Wl,`
probe fail, and `make info` showing `-pie` alone was the only symptom). The limits: the lint inspects only
the default `jichi`, so a static or `SIZE=1` binary is never checked (finding E-07), and it does not check
fortify at all (finding E-05).

**What each compiler gives without being asked** (measured with `gcc -dM -E` and `clang -dM -E` on the
workstation; L-15 for the first line):

```sh
echo | gcc -O2 -dM -E - | grep FORTIFY
```

```text
#define _FORTIFY_SOURCE 3
```

Ubuntu's gcc 13 enables PIE, the strong stack protector, CET, stack-clash protection, full RELRO and --
at `-O1` and above -- `_FORTIFY_SOURCE=3` on its own. Ubuntu's clang 18 enables only PIE. So on this
machine the explicit flags matter most for clang builds, and for everyone on musl, on the BSDs, and on
hand-built toolchains.

**The lesson.** Hardening you did not ask for is hardening you cannot rely on: it changes with the
distribution. Ask for it explicitly, probe for it so an old toolchain still builds, and then check the
binary -- not the command line -- because a flag can be accepted and still do nothing.

## 5. The gate never builds an optimised binary

`make ci` is the project's full gate (`Makefile:1346-1446`): gcc and clang at `-Werror`, a C++ build,
AddressSanitizer and UndefinedBehaviorSanitizer, valgrind, the fuzz sweep, the smoke and end-to-end
tiers. Every one of those builds uses the default, empty `OPT`. The lead built the tree in every mode
and ran the unit suite in each (L-02):

| build | product at `WERROR=1` | `make test` at `WERROR=1` | `make test` without `-Werror` |
|---|---|---|---|
| default (`-O0`) | 0 warnings | 15,398 checks, 0 failures | -- |
| `OPT=-O2` (fortify 2) | 0 warnings | does not compile: `tests/test_harness.c:127` ignores `symlink`'s result | 15,398 / 0 |
| `OPT=-O2` + fortify 3 | 0 | same | 15,398 / 0 |
| `OPT=-O2` + `-ftrivial-auto-var-init=zero` | 0 | same | 15,398 / 0 |
| `SIZE=1`, `SIZE=1 LTO=1` | 0 | same | 15,398 / 0 |
| `CC=clang OPT=-O2` | 0 | does not compile: `tests/test_snapshot.c:467` | 15,398 / 0 |

Three things follow. The product is clean at `-O2` today, which is a strong result. But some of gcc's
most useful warnings (`-Wmaybe-uninitialized`, the `-Wstringop-*` family, `-Warray-bounds`) only run
when the optimiser does, so the gate cannot see the class they catch. And the unit suite has never run
against a fortified binary in the gate: two test lines already break an optimised `-Werror` build,
because they ignore a result the C library marks "warn if unused" once fortify is on --
`(void)symlink(...)` at `tests/test_harness.c:127`, which a `(void)` cast does not silence under gcc
[GK], and `system(cmd);` at `tests/test_snapshot.c:467` under clang. Both need their result checked,
with a test failure when it is wrong.

**Recommendation:** one gate stage, `make clean && make WERROR=1 OPT=-O2 test`, after fixing the two
lines. It costs one more clean build (about 21 s serial, L-02) and turns fortify and the optimiser's
warnings from "never exercised" into "exercised on every merge".

## 6. Static analyzers, and what their silence is worth

The lead ran two static analyzers over all 188 translation units (L-03, L-05):

- `gcc -fanalyzer` reported 9 warnings in 6 files. Re-run with `-Wanalyzer-too-complex`, it turned out
  that the analyzer **gave up on parts of 84 of the 188 files** -- 1,802 times in `src/main.c` alone,
  hundreds of times in the session, parallel-task, TUI, agent-loop and config files. Where it gave up it
  said nothing, and its silence there means nothing.
- `clang --analyze` reported 18 warnings plus 11 dead stores.
- Hand triage: two were real, both of the same shape -- a pointer used and *then* checked for NULL
  (**L-05a**, `src/config/jc_config.c:2071-2073`; **L-05b**, `src/snapshot/jc_snapshot.c:503-510`;
  both low severity, [V]). Most of the
  rest were false positives the analyzers could not avoid because they do not look across files (a
  function in another file that already handles NULL). About a dozen clang reports were not triaged;
  each is a question for the maintainers about whether a pointer may be NULL at that point.

**The lesson.** "The analyzer found few warnings" and "the code has few defects" are different
statements, and only an analyzer that reports where it stopped lets you tell them apart. Run gcc's with
`-Wanalyzer-too-complex` and count the files it abandoned before you quote its result. The cheap fix for
the "used then checked" pattern is a decision about the contract: if the pointer may be NULL, check
first; if it may not, say so with an assertion -- which also tells the analyzer.

## 7. Findings

Severity follows the 2026-10-08 review: **high** = data loss, a crash, a security boundary or a fence
that does not hold; **medium** = a wrong result or a misleading report in a plausible case; **low** = an
edge case, an inconsistency, a maintainability cost; **note** = worth knowing. Marks: [V] verified by
reading the code path, [M] measured, [R] read but not traced to the end, [GK] general knowledge not
checked here.

### E-04. jichi's own formatting functions carry no printf-format attribute, so the compiler cannot check them

- **Severity:** low-medium. **Mark:** [M]. **Status:** partly recorded -- the 2026-10-08 review (its
  finding R10-F8) found two test calls that pass a non-literal format and named the missing attribute as
  the reason no compiler warned; it recommended fixing the two calls. The calls are still there
  (`tests/test_session.c:600-601, 677-678`), and the product finding below is new.
- **What happens.** gcc and clang check `printf`-style calls -- that each `%d` gets an `int`, each `%ld` a
  `long` -- only for functions they know are printf-like. jichi's formatting goes through five functions
  of its own: `jc_snprintf` and `jc_vsnprintf` (`include/jc_snprintf.h:30-31`), `jc_sb_append_fmt`
  (`include/jc_str.h:42`), `jc_logf` (`include/jc_log.h:29`) and `jc_ir_warn`
  (`include/jc_convert.h:138`). None carries `__attribute__((format(printf, ...)))`, so the build's
  `-Wformat` checks almost nothing. With the attribute added in a private copy, gcc 13 found one
  mismatch in the product and the two test calls; clang 18 found the same product line:

  ```c
          st = jc_sb_append_fmt(out, "- Changed since the relay began (%ld): ",
                                f->changed_n);
  ```

  (`src/util/jc_relay.c:336-337`; `changed_n` is an `int`.) Passing an `int` where `%ld` expects a `long`
  is undefined behaviour. It prints the right number on x86-64 and aarch64 because of how those calling
  conventions pass small integers [GK], which is exactly why nobody saw it. The rest of the tree was
  clean -- a strong statement about its discipline.
- **Recommendation.** Define, in `include/jc_platform.h`:
  `#if defined(__GNUC__)` / `#define JC_PRINTF(f, a) __attribute__((format(printf, f, a)))` /
  `#else` / `#define JC_PRINTF(f, a)` / `#endif`, and put it on the five declarations. A C89 compiler
  without attributes sees nothing; gcc and clang accept the attribute under `-std=c89 -pedantic -Werror`
  without a diagnostic (measured). Fix `jc_relay.c:336` (`%d`) and the two test calls (`"%s"`).
- **Test that goes red first.** None beyond the compiler: with the attribute in place, putting the
  `%ld` back fails `make WERROR=1`. A one-line lint can pin that the five declarations keep the macro.
- **Effort:** S.

### E-05. `_FORTIFY_SOURCE` is inert in the installed build, lowered from the compiler's level 3 when it is on, and reported live when it is not

- **Severity:** low. **Mark:** [M] for the compiler defaults and `make info`, [V] for the Makefile.
  **Status:** "inert by default" is recorded (2026-10-04 security audit §2.1, a conscious trade-off); the
  downgrade and the false `make info` line are new.
- **What happens.**
  1. *A downgrade.* `Makefile:760` adds `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2`. On Ubuntu 24.04 gcc
     already defines level 3 at `-O2` (section 4, L-15), so `make OPT=-O2` and `make SIZE=1` build *less*
     fortified than the compiler would by itself. Level 3 matters for jichi in particular: most of its
     buffers come from arenas, whose sizes are only known at run time.
  2. *A false report.* The condition tests whether `OPT` or `SIZEFLAGS` is non-empty, not whether there is
     an optimisation level. `LTO=1` on its own puts `-flto` in `SIZEFLAGS` without any `-O`, so fortify
     is set at `-O0`, where it does nothing, and `make info` claims the opposite:

     ```sh
     make info LTO=1 | grep "^OPT "
     ```

     ```text
     OPT            =  (none, but SIZEFLAGS carries -Os so _FORTIFY_SOURCE is live)
     ```

     `SIZEFLAGS` is `-flto` there, not `-Os`. Older glibc versions also print a `#warning` for fortify
     without optimisation, which `WERROR=1` turns into a failed build [GK].
  3. *The installed binary* is the default build, so it has no fortify (section 3, L-01).
- **Recommendation.** Decide on the optimisation level, not on non-empty variables: set fortify when
  `$(OPT) $(SIZEFLAGS)` contains `-O1`, `-O2`, `-O3`, `-Os`, `-Oz` or `-Og`, and print what was decided.
  Probe level 3 with the existing `cc_warn_ok` shape and fall back to 2 (the suite passes at level 3,
  L-02). Then add a release mode (Recommendations, step 3).
- **Test that goes red first.** Extend `harden_flags_lint.sh`: when `make info` says fortify is live, the
  binary must import at least one `_chk` function (the `readelf` line in section 2), with the same
  detector self-test the lint uses for the canary; and `make info LTO=1` must not claim it is live. Both
  red today.
- **Effort:** S for the condition and the probe.

### E-06. Distribution build flags are ignored from the environment and break the build on the command line

- **Severity:** low (packaging). **Mark:** [M]. **Status:** new.
- **What happens.** Linux distributions build C programs by passing their own `CFLAGS`, `CPPFLAGS` and
  `LDFLAGS` (hardening, reproducibility, optimisation). In GNU make, an assignment in the Makefile
  overrides a variable from the environment, and a variable given on the command line overrides the
  Makefile [GK]. jichi's `CFLAGS` (`Makefile:780`) and `LDFLAGS` (`:825`) are plain assignments that hold
  the *whole* compile and link line, and `CPPFLAGS` is never used. So flags from the environment vanish
  without a word, and flags on the command line replace the include paths, the C dialect and the POSIX
  level:

  ```sh
  make -n -B CFLAGS="-O2 -g" src/util/jc_str.o
  ```

  ```text
  cc -O2 -g -c src/util/jc_str.c -o src/util/jc_str.o
  ```

  That command cannot compile (no `-Iinclude`). With the same three variables exported in the
  environment instead, none of them reaches the compile line (measured with `make -n -B`). A package
  build therefore "succeeds" without any of the distribution's flags. `make OPT=-O2` is the knob that
  works today.
- **Recommendation.** Follow the GNU convention: keep the project's own flags in an internal variable
  (`JC_CFLAGS = $(STD) $(WARN) $(POSIX) $(INCLUDE) ...`), compile with
  `$(JC_CFLAGS) $(CPPFLAGS) $(CFLAGS)` and link with `$(JC_LDFLAGS) $(LDFLAGS)`, and leave `CFLAGS`,
  `CPPFLAGS` and `LDFLAGS` empty for the user. Let the probes see the user's flags too, or they answer a
  different question from the build (the M476 lesson). Print the three in `make info`.
- **Test that goes red first.** A lint that runs `make -n -B` with a marker define in `CFLAGS` and
  requires both the marker and `-Iinclude` on the line; red today.
- **Effort:** S-M (mechanical, but every gate stage must be re-run).

### E-07. The documented static builds are not position-independent, although `make info` lists `-pie`

- **Severity:** low. **Mark:** [M] with gcc 13 and glibc; the musl recipe itself is [GK] (no `musl-gcc`
  on the workstation). **Status:** new.
- **What happens.** `docs/LOW_MEMORY.md:816` and `docs/DEPLOYMENT.md:212` build a static binary by
  overriding `LDLIBS` with `-lm -lcurl -static`. On the link line `-static` then comes after the `-pie`
  that `HARDENLDFLAGS` placed earlier (`Makefile:1195`). Measured with a trivial program: `gcc -fPIE -pie
  -Wl,-z,relro -Wl,-z,now ... -static` produces `Type: EXEC`, a fixed-address program -- not PIE. The
  override also drops the libraries the probes chose (`$(RT_LIBS)`, `$(SOCK_LIBS)`; `Makefile:549`),
  harmless on Linux and wrong on illumos and Haiku. And `harden_flags_lint` never looks at a static
  binary, so nothing notices.
- **Recommendation.** A `STATIC=1` knob that probes `-static-pie` (gcc 8 and later with a C library that
  supports it [GK]) and otherwise links `-static` without `-pie`, says which in `make info`, and is what
  the two documents use.
- **Test that goes red first.** `harden_flags_lint` run on a `STATIC=1` build: its existing PIE check
  ("asked for `-pie`, binary is `EXEC`") fails today and passes with `-static-pie`.
- **Effort:** S.

### L-02 (the lead's). No gate stage builds or tests an optimised binary

- **Severity:** low (no defect hides there today, measured; the class is unwatched). **Mark:** [M] by the
  lead. **Status:** new. Details and the two failing test lines in section 5.
- **Recommendation and test:** section 5. **Effort:** S.

## 8. Recommendations, ordered by value for effort

1. **Fix the format checking (E-04).** Add `JC_PRINTF`, annotate the five declarations, fix
   `jc_relay.c:336` and the two test calls. One commit; the compiler is the test.
2. **Add an optimised stage to the gate (L-02).** Fix `tests/test_harness.c:127` (`symlink`) and
   `tests/test_snapshot.c:467` (`system`) to check their results, then add
   `make clean && make WERROR=1 OPT=-O2 test` to `make ci`, and a clang line if the budget allows.
3. **Give the release a name (E-05, section 3).** Add `RELEASE=1`, meaning `-O2`, fortify at the highest
   probed level, and every probed flag below; change `docs/BUILD.md`'s install section to
   `make RELEASE=1` followed by `sudo make install`. Candidate flags, each probed with `cc_warn_ok`:
   `-fhardened` first (GCC 14's umbrella flag for fortify 3, PIE, full RELRO, the strong protector,
   stack-clash protection, CET and zero-initialised automatics [GK]); otherwise
   `-ftrivial-auto-var-init=zero` (gcc 12, clang 16 [GK]; the suite passes with it, L-02, and
   [chapter 6](2026-10-17-hardening-06-platforms.md) describes a 32-bit defect it would have masked) and `-mbranch-protection=standard` on aarch64, the
   Raspberry Pi rows' architecture [GK].
4. **Correct the fortify condition and report (E-05).** Key it on an `-O` level; probe level 3; make
   `make info` say the level the build really gets; add the `_chk` check to `harden_flags_lint`.
5. **Correct `docs/HARDENING.md` §7b** ("no `__*_chk` symbols even in an optimised build"): ten at `-O2`.
6. **Respect the packager's flags (E-06).** Move the project's flags into `JC_CFLAGS`/`JC_LDFLAGS`.
7. **Make static builds PIE where possible (E-07)**, and run `harden_flags_lint` on the static and
   `SIZE=1` artefacts in whichever rig builds them.
8. **Make the analyzers' reach visible (section 6).** Record, with each analyzer run, how many files it
   abandoned; fix the two "used then checked" sites and decide the contracts behind the untriaged
   clang reports.

## 9. What is done well

- **Probes that ask the build's own question, and can be shown wrong.** `-Werror` plus silence for flags;
  `-Werror=implicit-function-declaration` for declarations, so a function that links but is not declared
  under the build's flags reads as absent (M449); a real output file, because `/dev/null` broke the
  Cygwin link (M476); a C++-driver probe that asks what the driver does with a `.c` file, not what it is
  called (M636). Each comment names the platform that taught it -- a design history in the code.
- **Hardening is measured, not assumed.** Few projects check their own binary's mitigations at all; this
  one compares ask with got and proves its detectors discriminate first.
- **A clean tree under every compiler tried.** Zero warnings at `-Werror` with gcc, clang and zig cc
  (`docs/CPP_BUILD.md`, M867; the C++ builds compile with ten documented `-Wwrite-strings` warnings);
  zero in the product at `-O2`, at `SIZE=1`, with fortify 3 and with zero-initialised
  automatics (L-02); one format mismatch in about 113,000 lines (E-04).
- **The installer refuses to do the wrong thing**: it will not build as root (M586: a `sudo make` had left
  352 root-owned files) and refuses a binary stamped from a dirty tree (M593).
- **Every fallback branch is compiled by the gate** (`fallbacks-compile`, `Makefile:1451-1483`), so a
  branch only an exotic platform takes cannot stop compiling unnoticed. [Chapter 6](2026-10-17-hardening-06-platforms.md) explains this in
  detail.

## 10. What this chapter did not review

The toolchains of the platform rows other than the workstation (FreeBSD's clang, NetBSD's gcc, illumos,
Haiku, the Hurd, Cygwin, MSYS2, DJGPP) were not measured: their `make info` hardening lines are not
recorded anywhere this review read, and a column for them in `docs/PLATFORMS.md` would answer it. The
zig cross-builds' hardening was not checked. Runtime enforcement of CET was not tested. The test helper
binaries under `tests/tools/` are built without hardening flags; they are test-only and were not
reviewed further.

## Read further

- `docs/BUILD.md` -- the build knobs, `make ci` stage by stage, and the per-platform build notes.
- `docs/HARDENING.md` -- jichi's hardening record (M130-M134, M300, M472, M693, M770).
- `docs/analysis/2026-08-17-source-hardening-audit.md` and
  `docs/analysis/2026-08-17-hardening-implementation.md` -- the audit that found "no hardening flags at
  all" and the milestone that answered it, including why the lint compares ask with got.
- `tests/smoke/harden_flags_lint.sh` -- read it as a worked example of checking an instrument before
  trusting it.
- `docs/C_STANDARDS.md` -- why the tree is C89, and the difference between undefined and
  implementation-defined behaviour (E-04 is the first kind).
- The glibc manual's section on source fortification, and the GCC manual's "Instrumentation Options"
  (`-fstack-protector-strong`, `-fstack-clash-protection`, `-fcf-protection`, `-fhardened`).
- [Chapter 6](2026-10-17-hardening-06-platforms.md) -- the platforms, signals, and the 32-bit Landlock
  defect mentioned in Recommendations step 3; [chapter 7](2026-10-17-hardening-07-resources-and-small-boards.md)
  -- what the build modes cost and save on the small boards.
