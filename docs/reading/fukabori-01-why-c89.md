# Fukabori 1 — Why C89, and what it cost

*[深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md) · chapter 1 of 12*

## The decision, stated falsifiably

`CLAUDE.md`, line one, commits this codebase to "C conforming to the C89
(ANSI C / C90) standard," enforced as `-std=c89 -pedantic -Wall -Wextra`
with **zero warnings across every translation unit** — no vendored-code
exemption (the one exemption that existed was removed when the exempted
file turned out to be first-party). This chapter treats that as an
engineering decision with a ledger: what it bought, what it cost, and
what would falsify it. The companion argument for learners is
[C_STANDARDS.md](../C_STANDARDS.md); this chapter is the version that
argues with you.

## What it bought

**Reach as a requirement, not a preference.** The design targets are
written down: `docs/LOW_MEMORY.md`'s tiers end at a ≤64 MB uClibc box,
and `docs/plans/2026-07-hardware-testing.md` commits to decade-old
single-board hardware. On those machines the *available compiler* is the
constraint, and C89 is the one interface every candidate accepts —
including the other camps' own toolchains (`zig cc` and `g++` both build
this tree; `docs/ZIG_BUILD.md` and `docs/CPP_BUILD.md` keep the receipts,
and the smoke tier's helpers are themselves C89 so the *test suite*
travels too). Note the direction of the argument: the tiers justify the
standard, not the reverse. A project without those targets loses this
whole column of the ledger.

**A minimal, uniform review surface.** This program executes model-chosen
shell commands; its security posture (chapter 4) is only as good as the
worst-reviewed line. C89-with-house-rules is brutally uniform —
declarations at block top, no hidden control flow, no operator
overloading, one allocation vocabulary — so a reviewer's pattern library
is small and a *diff* is legible without the file. `CONTRIBUTING.md`
reads as style, but its enforcement mechanism is the deeper point: the
compiler is the reviewer of record (`make WERROR=1`, gcc *and* clang in
`make ci`), and rules a compiler can hold are the only rules that survive
contributor turnover.

**Pedagogy, coupled on purpose.** The repository doubles as curriculum
substrate. C89 makes the invisible visible — lifetimes (chapter 3),
bounds, formatting — which is why set D's fixtures could be written *in
the exercise language itself* and why the Annai can teach AI concepts and
C idioms from the same files.

## What it cost, itemized

The honest column. Each row is a real absence with a real replacement
that had to be built and maintained:

| C89 lacks | this repo carries instead | the residue |
| --- | --- | --- |
| `snprintf` | `include/jc_snprintf.h:jc_snprintf` over a configure probe (`JC_HAVE_VSNPRINTF`), with a hand-rolled fallback formatter | a probe that once silently selected the fallback under clang — the probe pattern needed its own bugfix |
| `long long`, `stdint.h` | `jc_size`, `%lu`-with-cast discipline | 32-bit bounds are a *convention*, reviewed not compiled |
| mixed declarations | block-top declarations | verbosity; also the single most common agent-authored compile error in this tree's history |
| 4095-char literals | 509-char chunked tables (`src/scaffold/`) | an entire content-encoding convention |
| language-level cleanup (RAII) | three arenas + a lint (chapter 3) | the M197–M199 incident series *was* this row's interest payment |
| checked UB | `SAN=1` builds, valgrind in CI, `include/jc_fault.h` injection | coverage is opt-in at build time, not ambient |

Two rows deserve the emphasis the table can't carry. The **RAII row** is
the expensive one: chapter 3 documents ~1 GB-class retention bugs whose
C++ equivalents are hard to write and whose Zig equivalents are
compile-time errors in safe builds. The machinery that answers them
(arenas, the lint, footprint gauges) is genuinely good — better, this
guide will argue, than reference-counted ubiquity, because lifetimes
became *architecture* — but it exists because the language would not
carry the load. The **probe row** generalizes: anything the libc might
not have gets a configure-time probe (`Makefile`, the
`HAVE_MALLOC_TRIM` probe feeding `src/util/jc_memtrim.c:jc_mem_tune` is
the newest), never an `#ifdef __GLIBC__` — uClibc masquerades as glibc,
so the macro lies where the probe cannot. That is a *discipline* C89
projects need and richer ecosystems get from package managers.

## The alternatives, taken seriously

**C++ from the start** buys RAII against chapter 3's entire bug class,
`std::string_view` against a decade of pointer-length conventions, and a
library ecosystem. It costs: the reach column (embedded C++ toolchains
exist but the tiers' floor drops out), idiom fragmentation (which C++? —
every reviewer answers differently), exception-boundary policy around
`fork`/`longjmp`-adjacent code, and a runtime whose size
`docs/LOW_MEMORY.md` would have to carry. The repo's own
`docs/CPP_INTEROP.md` track exists because the *seam* is cheap even when
the wholesale choice was declined.

**Zig from the start** buys checked builds (the UB row), a hermetic
cross-compiling toolchain (the hardware plan's dream), and C interop
good enough that half this argument dissolves. It costs: pre-1.0 churn
under a codebase meant to outlive language fashion — this repo's own
Zig track fixtures are version-pinned because syntax moved between
releases — and the reviewer-pool argument cuts harder (who audits the
security-relevant agent in a language its auditors are still learning?).

The falsifier, stated: if the LOW_MEMORY tiers and the
teach-from-the-source mission were dropped, the reach and pedagogy rows
zero out, and this ledger flips — C89 would then be nostalgia carrying
real costs. The decision is only as good as the requirements that feed
it, which is the transferable lesson.

## Prove it to yourself

Run the enforcement, then read one probe end to end:

```sh
# in the jichi checkout (where you ran `make`)
make WERROR=1            # every TU, both warnings flags, zero tolerance
make info                # what the probes decided on YOUR machine
```

Then in the `Makefile`, find the `HAVE_MALLOC_TRIM` probe (a compile of a
five-line program with the real flags) and follow it to
`src/util/jc_memtrim.c:jc_mem_tune` — capability detected by *attempting
it*, the pattern in miniature. Finally, work
[assignment 23](../assignments/23-the-time-traveling-c.md) if you have
not: its PORT.md cost table is this chapter's ledger, priced by your own
hands.

## Where this bit us

The costs are not hypothetical: the M197 series (chapter 3) is the RAII
row realized at 491 MB; the clang-probe bug above shipped a width-less
formatter silently; and `docs/ANECDOTES.md` #19/#20 show the flip side —
the *bench* discipline that C89's small surface made cheap to build is
what caught a wire bug every richer stack would have hidden under
tolerance. Ledgers, honestly kept, stay useful even when the balance is
negative for your project. Especially then.

*Next: [chapter 2 — the provider abstraction](fukabori-02-the-provider-abstraction.md).*
