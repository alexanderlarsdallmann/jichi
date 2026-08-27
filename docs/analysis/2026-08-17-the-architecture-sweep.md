# The architecture sweep: twenty-one targets, one real bug, and two impostors

*2026-08-17, threadwork. M469. jichi's portability claims rested on **four**
architectures — x86-64, aarch64, armhf, s390x — of which exactly one was
big-endian. This workstation had ~31 `qemu-user` binfmt handlers registered and a
`zig cc` shipping musl for 33 Linux triples, so a dozen untested architectures were
reachable at zero install cost and nobody had looked.*

## What a row here is, stated before the results

Every row is a **cross-build run under emulation**, not a machine — the same
distinction `PLATFORMS.md` already draws for s390x. Two further limits:

- **`HAVE_CURL=`.** `zig cc` bundles libcs, not dependency trees, so these rows link
  *without* libcurl. They exercise the core, the arenas, the JSON, the pure helpers
  and the unit suite. They do **not** exercise a model call. A green row here is
  weaker evidence than a green FreeBSD row.
- **qemu-user emulates the ISA, not the machine.** It will not find a cache-coherency
  bug, a timing bug, or anything about real hardware.

What the sweep *is* good for is the class C89 portability actually fails on: `long`
assumed 64-bit, byte order, unaligned access, and `%lu`-with-casts where `long` is
four bytes.

## The one real product bug: undefined behaviour in the JSON number parser

`src/json/cJSON.c:262` read:

```c
node->valueint = (int)val;
```

`(int)val` is **undefined** when `val` is outside `int`'s range, and jichi parses JSON
it did not write — model responses, fetched pages, MCP results. `{"x":1e300}` is
valid JSON and enough to reach it.

Found on **armeb**, where `zig cc` traps UB by default, so the unit suite *aborted*
mid-run with a stack trace instead of failing a check:

```
panic: 3942649999999999865282985437692106…000 is outside the range of
       representable values of type 'int'
src/json/cJSON.c:262:22 in parse_number
```

Then reproduced on **x86-64**, which is what makes it a finding rather than a curio:

```
$ clang -fsanitize=undefined … && ./ub
src/json/cJSON.c:262:22: runtime error: 1e+300 is outside the range of
                         representable values of type 'int'
```

**Why `make ci` never caught it.** The gate *does* run ASan+UBSan. Nothing in the unit
corpus had ever fed the parser a number outside `int` range — `grep -rE '1e[0-9]{2,}'
tests/*.c` returns nothing. The sanitizer was there; the input was not. That is the
sharpest lesson of the sweep: **a sanitizer only sees the inputs you give it**, and a
new architecture is a cheap way to be handed inputs you would not have thought of —
here, by a compiler that traps what the others silently absolved.

Fixed by clamping to `INT_MAX`/`INT_MIN`, with a NaN arm that JSON cannot reach but
whose fall-through would be the same undefined cast. `tests/test_json.c` now asserts
it and **fails without the fix** (on x86-64 the undefined `cvttsd2si` yields `INT_MIN`
for every out-of-range value, so `1e300` reads as −2147483648 where `INT_MAX` is
required): 2 failures perturbed, 0 restored.

## The test suite could not validate on m68k, and that was ours too

The operator suggested a Debian/Ubuntu `gcc-m68k-linux-gnu` cross-toolchain, since
zig cannot reach m68k (below). It builds jichi clean at `WERROR=1` in **6 s**, strict
C89, and the result runs under `qemu-m68k`: `long=4 bytes, ptr=4, big-endian` — the
one combination nothing else in the matrix covers.

Five checks failed, all floating-point equality:

```
FAIL tests/test_config.c:78:  jc_config_cost(…) == 7.05
FAIL tests/test_convert.c:159: jc_json_get_num(model,"temperature",-1.0) == 0.2
FAIL tests/test_embed.c:46/48/61: s[i] == 0.20 / 0.90 / 0.10
```

**The values were right.** Measured side by side, `strtod("0.2")` prints
`0.20000000000000001` on m68k — byte-for-byte what x86-64 prints. The *comparison* was
wrong: m68k evaluates floating point in the 68881's 80-bit extended format
(`sizeof(long double)` is **12** there against 16 here), so the literal `0.2` carries
more bits than the nearest double and the two are unequal at extended precision. C89
permits exactly this (`FLT_EVAL_METHOD` 2), and historically i386/x87 behaves the same
way — so the suite had a **latent inability to validate on a whole class of targets**.

Fixed with `JC_CHECK_NEAR` (1e-9 absolute, no `<math.h>`, C89-clean) at the five
at-risk sites. Deliberately **not** applied to `== 0.5`, `== 0.75` or any `== 3.0`:
those are exact in both formats, and fuzzing a precise assertion buys nothing. Of 80
exact float comparisons in the suite, only 8 involve a non-integral literal and only
5 a non-representable one — the audit is in the commit.

## Two impostors, and how each was told from a real finding

**armeb aborted before printing anything.** Classified `no-run` by the rig. A trivial
static armeb hello-world *runs fine* under `qemu-armeb`, so it was not a blanket
emulator limitation — which is what justified digging, and the dig produced the cJSON
bug above. The *residual* armeb misparse (3.9e33 where the file holds 1.19) is
**not** explained: `aarch64_be` is also big-endian and passes 11,646/0, so byte order
alone is not it; armeb is BE8, a thin configuration. Recorded, not diagnosed.

**All four MIPS variants reported 11,627 checks / 73 failures** — big-endian *and*
little-endian, 32-bit *and* 64-bit, so not byte order and not word size. The failures
sat in exactly seven files, every one of which spawns a subprocess or reads `/proc`:
`test_snapshot` (26), `test_bg` (17), `test_proc` (12), `test_envelope` (6),
`test_app` (6), `test_user_tools` (4), `test_pdf` (3).

Basic `fork+exec+wait` works under `qemu-mips` (probed: child status 0). The
mechanism is one level lower:

```
=== mips-linux-musleabihf ===      pipe FAILED
=== powerpc-linux-musleabihf ===   select rc=1 / read rc=2 / /proc openable
```

**`pipe()` itself fails.** MIPS is the one Linux architecture where the `pipe` syscall
returns *both* descriptors in registers rather than writing a user array, and that
convention is broken here under `qemu-mips` with zig's musl. Every one of the 73
failures is downstream of it. This is an **emulation/libc-ABI result, not a jichi
defect** — and the rig was wrong to file it as one: it reported "unit suite RAN and
reported failures", which reads as an accusation. A per-target preflight probe
(`pipe`, `select`, fork/exec) belongs in the rig, mirroring `tier-v-openbsd.sh`'s
procfs check, so the row says *the environment cannot run the subprocess tests* and
counts the ~11,565 that do pass.

## Findings about the instrument, in the order they bit

1. **`zig targets`' libc table is not its backend list.** zig advertises
   `m68k-linux-musl` and its LLVM cannot compile it:
   `No available targets are compatible with triple "m68k-unknown-linux5.10.0-musl"`.
   The rig now compile-probes every triple, so `--list` reports 21 runnable and names
   m68k as *ships musl, cannot compile* rather than claiming 22 and testing 21.
2. **A configure probe that cannot ask reports "absent".** The m68k attempt selected
   `-std=gnu89` and `-DJC_NO_CLOCK_GETTIME` before dying — the probes failed to
   compile *for any reason* and the Makefile read each failure as a missing platform
   feature. Third instance of this shape: M449 (uClibc hiding `malloc_trim` behind
   `__USE_GNU`), M458 (Guix shipping no `cc`), now this. With a *working* m68k
   compiler the same probes report `c89 (strict)`, vsnprintf yes, malloc_trim yes,
   clock_gettime in libc — the degradation was purely the missing backend.
3. **Two of my own, both in the first row.** `make` builds `jichi` and
   `jichi-convert`, not `run_tests`, so the row reported `./run_tests: not found` and
   I read the shell's "not found" as a missing ELF interpreter when the file had
   simply never been built. And zig's musl output is **dynamically** linked against
   `/lib/ld-musl-<arch>.so.1`, which does not exist here, so it needed
   `LDFLAGS=-static`.
4. **The rig snapshots per target, not per sweep.** `jc_rig_ship_tar` runs inside the
   loop, so committing mid-sweep would give later rows a different tree than earlier
   ones and quietly make the table non-comparable. It is why this session's commit
   waited for the sweep to end. The fix is to snapshot once, up front.

## The table

Twenty-one zig-reachable targets plus m68k via a Debian cross-toolchain. Every row is
`make CC="zig cc -target <triple>" HAVE_CURL= LDFLAGS=-static WERROR=1`, then the unit
suite under binfmt, at HEAD `140c075` plus this milestone's two fixes.

| target | endian / width | build | unit suite |
|---|---|---|---|
| `x86_64-linux-musl` | LE 64 | clean 34s | **11,646 / 0** |
| `x86-linux-musl` | LE **32** | clean 48s | **11,646 / 0** |
| `aarch64-linux-musl` | LE 64 | clean 49s | **11,646 / 0** |
| `aarch64_be-linux-musl` | **BE** 64 | clean 52s | **11,646 / 0** |
| `arm-linux-musleabihf` | LE **32** | clean 48s | **11,646 / 0** |
| `powerpc-linux-musleabihf` | **BE 32** | clean 35s | **11,646 / 0** |
| `powerpc64-linux-musl` | **BE** 64 | clean 48s | **11,646 / 0** |
| `powerpc64le-linux-musl` | LE 64 | clean 46s | **11,646 / 0** |
| `riscv32-linux-musl` | LE **32** | clean 51s | **11,646 / 0** |
| `riscv64-linux-musl` | LE 64 | clean 52s | **11,646 / 0** |
| `s390x-linux-musl` | **BE** 64 | clean 49s | **11,646 / 0** |
| `loongarch64-linux-musl` | LE 64 | clean 47s | **11,646 / 0** |
| `hexagon-linux-musl` | LE 64 | clean 73s | **11,646 / 0** |
| **`m68k-linux-gnu`** (gcc 15.2, glibc) | **BE 32**, 2-byte aligned | clean **6s** | **11,646 / 0** *(after the JC_CHECK_NEAR fix; 5 failures before)* |
| `armeb-linux-musleabihf` | **BE 32** (BE8) | clean 47s | 11,657 / **5** — toolchain, see below |
| `mips-linux-musleabihf` | **BE 32** | clean 49s | 11,627 / **73** — emulation, see below |
| `mipsel-linux-musleabihf` | LE 32 | clean 48s | 11,627 / **73** — same |
| `mips64-linux-muslabi64` | **BE** 64 | `-Woption-ignored` | 11,627 / **73** — same |
| `mips64el-linux-muslabi64` | LE 64 | `-Woption-ignored` | 11,627 / **73** — same |
| `mips64-linux-muslabin32` | **BE** n32 | clean 49s | 11,627 / **73** — same |
| `mips64el-linux-muslabin32` | LE n32 | clean 48s | 11,627 / **73** — same |
| `x86_64-linux-muslx32` | LE x32 | clean 45s | *did not execute* — `Exec format error`; this kernel has no `CONFIG_X86_X32` |

**Fourteen architectures green, five of them big-endian**, against the four (one
big-endian) this project could claim yesterday. `11,646` against the host's `11,661`
is the fifteen curl-gated checks `HAVE_CURL=` skips, plus the four the m68k row also
skips — not a gap.

## armeb, resolved: a `double` that cannot be printed

With the cJSON clamp in place armeb runs to completion — 11,657 checks, 5 failures —
and the residual is unambiguous:

```
FAIL tests/test_convert.c:159: -2.35344e-185 != 0.20000000000000001
```

The probe that settles it, on armeb:

```
  strtod("0.2")   = -2.35344e-185
  1.0/5.0         = -2.35344e-185
  literal 0.2     = -2.35344e-185
  bytes of strtod result: 3f c9 99 99 99 99 99 9a
  bytes of literal 0.2  : 3f c9 99 99 99 99 99 9a
```

`3f c9 99 99 99 99 99 9a` **is** IEEE-754 0.2 in big-endian byte order — the stored
value is bit-correct (little-endian arm prints the same bytes reversed and reads 0.2
fine). What is broken is the handling of a `double` in the FPU/ABI path: the **literal**
`0.2` misprints, and jichi cannot cause a compile-time constant to misprint. This is a
musl/qemu `armeb` (BE8 + hardfloat) defect, one level below the program.

**Classification: toolchain, not jichi.** `aarch64_be` and `powerpc`/`powerpc64` are
also big-endian and pass 11,646/0, so big-endian itself is fine — armeb specifically is
not usable for this measurement, and the row says so rather than being quietly dropped.

## mips64's warning was zig's own driver

```
zig: error: ignoring '-fno-PIC' option as it cannot be used with implicit usage
of -mabicalls and the N64 ABI [-Werror,-Woption-ignored]
```

Not a diagnostic about jichi's source at all — zig passes `-fno-PIC` for the static
link and clang objects that it is meaningless under MIPS N64. `-Werror` promoted it.
The `abin32` variants of the same architectures build clean, which is the tell.

## Score

| | count | |
|---|---|---|
| **Real product bug** | 1 | the cJSON `(int)val` UB — fixed, regression-tested, reproduced on x86-64 |
| **Real test-suite defect** | 1 | exact float equality unusable under excess precision — fixed at 5 sites |
| **Impostors** | 3 | qemu-mips `pipe()`, armeb `double` ABI, zig's `-Woption-ignored` |
| **Legitimate rig no-run** | 1 | x32 (kernel lacks `CONFIG_X86_X32`) |
| **Instrument findings** | 4 | listed above |

Two real defects for about two hours of machine time, and — the part worth keeping —
**both were invisible to a green `make ci` on this workstation**, one because the
sanitizer had never been handed the input and one because the architecture had never
been built. That is the argument for breadth over depth on an established codebase.

## Design decisions, and what each rejected

Recorded here in brief; the full rows with rejected alternatives are in
[`../DECISIONS.md`](../DECISIONS.md) under *Testing* and *Safety*.

| Decided | Rejected |
|---|---|
| Compile-probe every triple | Trusting `zig targets`' libc table — it is not the LLVM backend list, and the matrix would claim 22 and test 21 |
| Link `HAVE_CURL=`, and say so in every row | Cross-building a minimal libcurl 21 times; or reporting the rows without the caveat, leaving a reader to infer why 11,646 ≠ 11,661 |
| Clamp an out-of-range JSON number to `INT_MAX`/`INT_MIN` | Rejecting the document (it is valid JSON); leaving `valueint` 0 (asserts a wrong value where a saturated one is at least ordered); a new error 40-odd call sites would not check |
| `JC_CHECK_NEAR` only at non-representable sites | Converting all 80 exact comparisons — `== 0.5`, `== 0.75`, `== 3.0` are exact in both formats, so it trades precision for fuzz; a relative epsilon, where absolute 1e-9 is the one a reader can check by eye |
| Classify MIPS as the emulator's limitation | Filing 73 failures as a jichi defect, which is what the rig's own wording did; or dropping the rows silently, when the number is real and the reasoning should be reproducible |
| m68k via a Debian glibc cross-toolchain | Skipping m68k because zig cannot reach it — it is the only big-endian **and** 32-bit **and** 2-byte-aligned target available, and it found the second defect |
| Probe before dismissing, every time | Assuming emulation for armeb on MIPS's precedent: that dig produced the real cJSON bug, so the habit paid for itself inside one sweep |

## Recommendations

**1. Do not add the sweep to `make ci`.** It needs `zig`, ~31 binfmt handlers and about
17 minutes; `make ci` is a ten-minute gate developers run before pushing. Run it
**on changes to endianness- or width-sensitive code**: `src/index/jc_index.c` (the
endian tag), anything formatting `long`/`size_t`, the arenas, and `src/json/`. One
command: `scripts/tier-v-arch.sh --all`.

**2. Fix the two rig defects before the next sweep**, both in `DEFERRED.md`: snapshot
the tree **once per sweep** rather than per target (otherwise a mid-sweep commit makes
the table non-comparable, which is why this milestone's commit had to wait for 21
targets), and **preflight `pipe()`/`select()`/fork+exec per target** so an emulator gap
reports as *the environment cannot run the subprocess tests* instead of as a jichi
failure.

**3. Add `-fsanitize=float-cast-overflow` inputs, not just the sanitizer.** The cJSON
bug proves the gate's UBSan pass is only as good as the corpus. A handful of hostile
JSON numbers (`1e300`, `-1e300`, `99999999999999999999`, deep nesting) belong in
`tests/fuzz/corpus/` where the fuzzer will keep exercising them.

**4. Next platforms, ranked by cost on this machine.**

- **NetBSD** — cheapest new *kernel*. `PLATFORMS.md` records its live image booting to
  a login in 7 s under KVM with sshd already running, needing only `consdev com0` at
  the loader prompt. A third non-Linux kernel for roughly the effort of
  `tier-v-openbsd.sh`.
- **illumos** (OpenIndiana / OmniOS) — the answer to *"does jichi run on Solaris?"*,
  and testable here: free, x86-64, ISO-installable under KVM. See below for what to
  expect.
- **WSL2** — blocked on the operator's other machine; the walkthrough is prepared.
- **macOS** — no Mac. Unchanged.

### Solaris and illumos, specifically

**Status: never compiled, so the honest answer is unknown.** But the risk areas are
identifiable from the code rather than guessed, and they are narrower than they look:

- **procfs is present but *different*.** jichi touches four Linux paths.
  `/proc/self/stat` (the capability probe in `jc_platform_posix.c`) and
  `/proc/%s/stat` (`jc_proc.c`) **do not exist** on illumos, so the probe answers "no
  procfs" and takes the degradation path FreeBSD and OpenBSD already proved.
  `/proc/self/status` **does** exist there — as a binary `pstatus_t`, not text — so
  `fopen` succeeds where a reader might hope it failed; `jc_meminfo_parse` then looks
  for `VmRSS:` in binary bytes, finds nothing, and reports zero. That is *no data*
  rather than *wrong data*, which is the right failure, but it is **reasoned from the
  source, not measured**, and it is the first thing a row should check.
  `/proc/self/exe` is absent (illumos uses `/proc/self/path/a.out`) and its `readlink`
  failure is already handled.
- **`/bin/sh` is ksh93** on Solaris 11. That makes M467's finding load-bearing rather
  than incidental: a backgrounded subshell's `$!` names the subshell on ksh, so without
  the `exec` fix the smoke tier's signal and abort drivers would have failed there for
  the same reason they failed on OpenBSD.
- **Non-POSIX symbols** are the class the campaign keeps finding, and here the odds are
  better than usual: `_SC_NPROCESSORS_ONLN` *originated* on Solaris, and
  `clock_gettime` is present. `malloc_trim` is glibc-only and already probed.
- **Licensing splits the two.** **illumos** (OpenIndiana, OmniOS) is free and
  ISO-installable, so a `scripts/tier-v-illumos.sh` on the OpenBSD pattern is ordinary
  work. **Oracle Solaris 11.4** needs an Oracle account and its licence terms are the
  operator's decision, not a technical one — which is why the recommendation is
  illumos, and why any claim about *Oracle* Solaris would remain unmeasured even after
  an illumos row is green.

**Expected outcome, stated in advance so it can be wrong:** a clean C89 build and a
green unit suite, with the smoke tier's subprocess and PTY drivers the place to look
for trouble — which is exactly where FreeBSD and OpenBSD each yielded their findings.
