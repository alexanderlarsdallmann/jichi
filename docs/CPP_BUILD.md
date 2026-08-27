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

## Practicalities, if you try it

- Plain `make CC=g++` warns once per file that `-std=c89` "is valid for
  C/ObjC but not for C++" and then compiles under g++'s default C++
  standard. Harmless. (`WERROR=1` would promote that warning to an error;
  don't combine them.)
- Don't override `STD=` on the command line: the Makefile's feature
  probes append `-DJC_HAVE_VSNPRINTF`/`-DJC_HAVE_CURL` to it, and a
  command-line variable would clobber both (make semantics).
- One residue class remains as warnings under `-Wwrite-strings`: the
  `argv[0] = "/bin/sh"` idiom (string literal into `char *argv[]` for
  `execv`-family calls) — ~20 sites, correct C, and left alone
  deliberately: POSIX's `execv` signature is the constraint, not our
  style. The `sk.description` cast in `jc_learn.c` similarly mirrors what
  C did silently; a full const-correctness pass is noted, not scheduled.

*See also: [PORTING_WINDOWS.md](PORTING_WINDOWS.md) (the sibling boundary
survey), CONTRIBUTING.md (the C89 rules that made this nearly free).*
