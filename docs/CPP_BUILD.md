# Compiling jichi with a C++ compiler — and what it buys you, honestly

A tutorial written from the real attempt (2026-07-28, g++ 15), not from
speculation. Short version: **it works** —

```sh
make clean
make CC=g++        # builds and links jichi as C++; the binary runs
```

— and the honest verdict is that this buys you **no runtime advantage
whatsoever**. The value lies elsewhere; read on.

## What the attempt found (the empirical part)

Before M188, an audit compiled every translation unit with
`g++ -fsyntax-only` at both `-std=c++98` and `-std=c++17`. The result says
something about the house style: of ~200 source files, **only 4 failed**,
with 12 errors total — every one an implicit `void*`→`char*` conversion
(`malloc`/`memchr` results, which C converts silently and C++ refuses)
plus one const-qualification slip. No C++ keyword collisions anywhere, no
designated initializers (C89 has none), and the in-tree cJSON compiled
clean. The reason ~94 % of the tree was already C++-clean: the house style
casts allocation results explicitly (125 of 174 sites did) — the 12
misses were drift, not policy.

Those 12 casts were then fixed **on their own merits** (they restore the
existing convention), and every public header in `include/` gained
`extern "C"` guards — previously **0 of 114** had them. The guards change
nothing for the C build (they preprocess away) and enable the one real
interop win below.

## What it actually buys

1. **A second compiler front-end as a free lint tier.** C++'s stricter
   type system makes implicit pointer/const conversions hard errors. That
   is now a standing check:

   ```sh
   make cpp-check    # g++ -std=c++17 -fsyntax-only over the whole tree
   ```

   Deliberately *not* part of `make ci` — the sources are C89 and the C
   gate stays the gate; this is an optional extra sieve that has already
   paid for itself once (the 12 sites).

2. **Linking jichi's objects from C++ programs.** With the `extern "C"`
   guards, a C++ application can `#include "jc_patch.h"` (or any public
   header) and link against jichi's objects without mangling mismatches —
   the pure modules (diff, patch, testparse, JSON…) are usable as a
   library from C++ code.

3. **A teaching exercise about language boundaries.** Compiling one
   language's idioms under another's rules is a compact lesson in what a
   standard actually promises — the same spirit as the Windows porting
   survey ([PORTING_WINDOWS.md](PORTING_WINDOWS.md)); C++'s C subset is
   simply a much shorter walk than Win32's POSIX subset.

## What it does NOT buy — the honest part

- **No performance.** Same code, same optimizer, same machine
  instructions (modulo name mangling). g++ compiling C-style code emits
  what gcc emits.
- **No features.** jichi uses none of C++ — no classes, no RAII, no
  templates, no exceptions. Compiling *as* C++ does not add them, and
  rewriting *into* them would be a different project (the arena/status
  discipline is the design, not an accident of language).
- **Not a supported build.** `make CC=g++` works today and `cpp-check`
  keeps the property cheap to maintain, but the shipped, tested, CI-gated
  build is `-std=c89 -pedantic` with gcc/clang. C++ compilation is a
  *property* of the codebase, not a target of it.

## M636 — the tier had rotted, and three published commands did not work

Everything above was true in 2026-07 and three of its commands had stopped
working by 2026-09, which nobody knew because **nothing ran them**. M188 kept
`cpp-check` out of `make ci` on purpose and put nothing in its place. Measured
from a clean tree on 2026-09-16, before the fix:

| Command | Then | Why |
|---|---|---|
| `make CC=g++` | builds, **913 warnings** | 900 of them are five C-only flags × 180 files; 13 are real |
| `make CC=clang++` | **fails outright** | clang++ rejects `-std=c89`; g++ only warns |
| `make cpp-check` | **fails from clean** | no prerequisite on the generated `jc_buildrev_stamp.h` (M495), which `make clean` deletes (M593) |
| `make cpp-check CXX=clang++` | **fails from clean** | same |
| `make cpp-check CXX="zig c++"` | **fails every file** | the driver dispatches on `.c` and meets `-std=c++17` |

All five now pass, and the numbers after are:

| Command | Now | Wall (32-core bench) |
|---|---|---|
| `make CC=g++` | builds, **13 warnings** | 1 s |
| `make CC=clang++` | builds, **13 warnings** | 2 s |
| `make cpp-check` | 302 files | 6 s |
| `make cpp-check CXX=clang++` | 302 files | 8 s |
| `make cpp-check CXX="zig c++"` | 302 files | 41 s |

The 13 are the `-Wwrite-strings` residue this page already described — measured
at 13 sites, not the "~20" claimed above. Four changes did it: `$(STAMP)` became
a prerequisite of `cpp-check`; `-x c++` is passed explicitly (clang++ has
*deprecated* compiling a `.c` input as C++, so the 180 notices were a warning
about a future hard failure); the syntax-only mode is probed rather than assumed,
because `zig c++` accepts `-fsyntax-only` and then fails every file with
`error: FileNotFound`; and the presence probe uses `$(firstword $(CXX))`, so a
two-word driver is not reported missing.

**`make CC=clang++` works because the Makefile now asks what `$(CC)` is.** It
compiles a C++-only program named `.c` and reads the exit status — g++ and
clang++ accept it, gcc and clang do not — then selects `-x c++ -std=c++17` and
drops the four C-only warning flags. The old C89 probe answered this question
wrongly *and blamed the machine*: for clang++ it fell through to
`STD_DIALECT = gnu89 (this platform's headers are not C89-parseable)`, which is a
false statement about the host when the truth is a fact about the compiler.

**`zig c++` is not a third C++ build.** Measured: given a file named `.c` it
compiles C, exactly as `zig cc` does. `make CC="zig c++"` therefore builds the
ordinary C89 jichi; only `make cpp-check CXX="zig c++"`, which passes `-x c++`,
reaches zig's C++ front-end at all.

**The tier stays out of `make ci`** — the M188 decision holds, the C gate is the
gate — but `tests/smoke/cppcheck_lint.sh` now runs in every `make smoke`. It
checks the four properties above plus, behaviourally, that `cpp-check` survives a
missing stamp, at ~2 s rather than the 55 s a full three-front-end sweep costs.

**What it found.** One real defect, in its first run after the repair:
`src/index/jc_docs.c` carried `pending_nl = pending_nl || 0;` — a statement that
compiles to nothing, which gcc never mentions and clang++ flags as
`-Wconstant-logical-operand`. It was correct in intent (a literal newline in HTML
source is inline whitespace, and must not clear a queued block boundary) and
wrong as code: a reader who trusts it looks for an effect that is not there. It
is now the comment it always was. That is the second time this tier has paid for
itself — the first was the 12 implicit-conversion sites at M188.

## Practicalities, if you try it

- `make CC=g++` and `make CC=clang++` both work and both print the same 13
  warnings. Don't add `WERROR=1`: those 13 are real, deliberately left, and
  described below.
- Don't override `STD=` on the command line: the Makefile's feature
  probes append `-DJC_HAVE_VSNPRINTF`/`-DJC_HAVE_CURL` to it, and a
  command-line variable would clobber both (make semantics). The C++ dialect is
  selected by the probe, not by you.
- One residue class remains as warnings under `-Wwrite-strings`: the
  `argv[0] = "/bin/sh"` idiom (string literal into `char *argv[]` for
  `execv`-family calls) — **13 sites** (measured 2026-09-16; this page said
  "~20" from 2026-07 to M636), correct C, and left alone
  deliberately: POSIX's `execv` signature is the constraint, not our
  style. The `sk.description` cast in `jc_learn.c` similarly mirrors what
  C did silently; a full const-correctness pass is noted, not scheduled.
- Want to read *about* C++ rather than compile C as it? The C++ section of
  [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) is 11 checked entries, five of them free.

*See also: [PORTING_WINDOWS.md](PORTING_WINDOWS.md) (the sibling boundary
survey), CONTRIBUTING.md (the C89 rules that made this nearly free).*
