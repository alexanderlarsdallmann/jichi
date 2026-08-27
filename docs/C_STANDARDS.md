# C89/90 vs modern C — and what portability really costs

*A curriculum extra with three graded companions:
[`23-the-time-traveling-c.md`](assignments/23-the-time-traveling-c.md) ports
modern C *down* to C89 (what the standard lacks),
[`29-works-on-my-machine.md`](assignments/29-works-on-my-machine.md) catches
undefined behaviour a C89 compiler will not diagnose (what it refuses to
define), and [`30-the-signed-byte.md`](assignments/30-the-signed-byte.md)
catches implementation-defined behaviour (what it leaves to the compiler);
the map is [CURRICULUM.md](CURRICULUM.md). jichi itself is the living
example: every rule below is enforced on this repository by `make WERROR=1`,
and `CONTRIBUTING.md` is the house rulebook this page explains the reasons
behind.*

## The standards, in one honest paragraph each

**C89/C90** (ANSI 1989, ISO 1990) is the baseline the whole C world can
compile: declarations at block top, `/* */` comments only, no `long long`,
no `snprintf`, 509-character string-literal minimum, function prototypes
young enough that you must write them everywhere. Every compiler that calls
itself a C compiler accepts it — including the small, strange, and ancient
ones on exactly the machines `docs/LOW_MEMORY.md` cares about.

**C99** added what most people think of as "just C": `//` comments, mixed
declarations, `for (int i …)`, `long long`, `snprintf`, designated
initializers, compound literals, `stdint.h`, VLAs (later made optional),
`inline`. It is the biggest usability jump the language ever took — and the
version support tables stayed ragged over for a decade (MSVC most famously).

**C11/C17** added threads and atomics (both optional), `_Generic`,
anonymous unions, and alignment control; C17 is C11 with defects fixed.
**C23** removes old-style function definitions, adds `nullptr`, `constexpr`
for objects, `typeof`, and binary literals — modern C is now visibly
converging with the subset of C++ that C programmers always used.

## Why this project is C89 anyway

Not nostalgia — a chain of consequences you can audit in this repo:

1. **Reach.** The lowest tier in `docs/LOW_MEMORY.md` is a ≤64 MB uClibc
   box; `docs/plans/2026-07-hardware-testing.md` targets decade-old
   single-board computers. The compilers *there* are the constraint, and
   C89 is the interface every one of them honors.
2. **Discipline as a feature.** Declarations-at-top forces the reader to
   see a function's whole state up front; no-`//` keeps the comment
   style greppable; the 509-char literal limit forces long text into
   reviewable line-chunks (`src/scaffold/` is built on that). The
   constraint set reads like a style guide because it is one — with a
   compiler enforcing it instead of a reviewer.
3. **Costs, paid knowingly.** No `long long` means 32-bit bounds unless
   you build them (`jc_size`, and careful `%lu` + casts); no `snprintf`
   means the project carries `jc_snprintf` (a probed fallback formatter);
   no mixed declarations costs vertical space. Assignment 23 makes you
   pay each of these once, by hand, and write down the price.

The point the curriculum wants you to take is **not** "C89 is better." It
is that *a standard version is an engineering decision with a blast
radius* — reach, tooling, guarantees, style — and honest projects write
the decision down (`CLAUDE.md` line one) instead of inheriting whatever
their compiler defaulted to.

## What the standard *won't* pin down: two grey categories

A standard version tells you what the language *has*. Just as important is what
it declines to nail down — and it declines in **two different ways**, which the
code compiles clean through in both cases. Confusing them is a portability bug
waiting to happen, because the *instrument* for each is different.

**Undefined behaviour (UB)** — the standard refuses to give the construct any
meaning at all, so the compiler may do *anything*. This category does not
shrink as you move to C89. Signed integer overflow is the canonical example,
unchanged from C89 (6.1.2.5) through C23: `int` overflow is undefined,
`unsigned` overflow is *defined* to wrap modulo 2^N. A rolling hash
`acc = acc*31 + x` on a signed `int` "works on my machine" at `-O0` and is a
latent miscompilation at `-O2`, because the optimizer is entitled to assume
the overflow never happens. The same trap hides in `INT_MIN / -1`, shifting
into or past the sign bit, `p + n` walking off an array, and reading a value
through the wrong type. `-pedantic` cannot catch UB — it is a run-time
property — so the instrument is a **sanitizer**: `-fsanitize=undefined` turns
"the compiler may do anything" into a loud, located run-time trap. (This is
`assignments/29-works-on-my-machine.md`, and it is why this project runs its
whole CI under ASan/UBSan and valgrind rather than trusting a clean compile.)

**Implementation-defined behaviour** — the standard *does* give the construct
meaning, but lets each implementation choose which, and requires it to
document the choice. Nothing is "wrong"; the program is simply not portable if
it depends on the choice. Whether `char` is signed is the textbook case (C89
3.1.2.5): signed on x86, unsigned on most ARM, so summing raw bytes through a
plain `char` sign-extends `0x80`–`0xFF` on one and not the other — the same
source, two answers, *no* UB and no trap. A sanitizer sees nothing here; the
instrument is to **compile both ways and diff** (`-fsigned-char` vs
`-funsigned-char`), and the fix is to say what you mean: `unsigned char` for a
byte that is a *number* (a checksum, a table index, an image sample), plain
`char` only for characters. (This is `assignments/30-the-signed-byte.md`, and
the reason "signedness of `char`" sits in the *architecture* row below.)

The lesson that spans both: a clean `-Werror` build proves the *language* row
of the table — that you used only what the standard promises — and nothing
more. It does not prove the program is defined (UB), nor that it is portable
(implementation-defined). Those need their own instruments.

## What "portable" actually means

A portability claim always has a subject. Separate them or you will test
the wrong one (assignment 19's lesson, restated):

| subject | question | this repo's instrument |
| --- | --- | --- |
| **language** | does the code use only what the standard promises? | `-std=c89 -pedantic -Wall -Wextra -Werror`, every TU |
| **library** | POSIX? which version? glibc-isms? | `-D_POSIX_C_SOURCE=200112L`, probed features (`make info`) |
| **compiler** | does a *different* compiler agree? | `make ci` runs gcc **and** clang; assignment 19 adds `zig cc` |
| **architecture** | endianness, word size, alignment, signedness of `char` | the index's endian tag; `%lu`+cast rules; the aarch64 cross-build |
| **OS** | what happens where POSIX ends? | assignment 18 / [PORTING_WINDOWS.md](PORTING_WINDOWS.md) |

`jc_memtrim` (M218) is a compact worked example of the *library* row done
right: `malloc_trim` is glibc-only, so it is **probed** at configure time
(`HAVE_MALLOC_TRIM`) — not `#ifdef __GLIBC__`, because uClibc masquerades
as glibc; the macro lies, the probe cannot.

## Where to go from here

- **How to read the standard itself**, with the free C89 resources and a
  measured method for searching them: [READING_THE_STANDARD.md](READING_THE_STANDARD.md).

- Work [`23-the-time-traveling-c`](assignments/23-the-time-traveling-c.md)
  (the port, with the cost table) — the *language* row: what C89 lacks.
- Then [`29-works-on-my-machine`](assignments/29-works-on-my-machine.md) —
  the **undefined-behaviour** category above: a program that compiles clean
  and is still wrong, and the sanitizer that proves it. (Needs a UBSan-capable
  `cc`/`clang`.)
- Then [`30-the-signed-byte`](assignments/30-the-signed-byte.md) — the
  **implementation-defined** category: a program that is right on *your*
  machine and wrong on a Pi, and the compile-both-ways diff that proves it.
  (Needs a `cc`/`clang` with `-f{,un}signed-char`.)
- Then read `CONTRIBUTING.md`'s C89 rules against your own PORT.md — every
  rule there is one of your table rows, made permanent.
- **The graded C systems course** — [`assignments/INDEX.md`](assignments/INDEX.md)
  → Systems track, tasks 51–54 — is the *building* half of this reading: manual
  memory & data structures under AddressSanitizer (use-after-free, a growable
  array, `sprintf`→`snprintf`, and a bump allocator). Where tasks 23/29/30 pin
  *standards* behaviour, these pin *memory* behaviour. (Needs an ASan-capable
  `cc`/`clang`.)
- The other direction of travel: [`ZIG_INTEROP.md`](ZIG_INTEROP.md) and
  [`CPP_INTEROP.md`](CPP_INTEROP.md) take a C project *forward* into a
  newer language instead of backward into a stricter standard — the same
  discipline (behavior pinned, costs named) either way.
