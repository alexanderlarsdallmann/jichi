# Ada, SPARK and C — proving what this project tests

*A curriculum **extra** (the map is [CURRICULUM.md](CURRICULUM.md)), and
deliberately **not** an Ada course. Ada appears here for one reason, and it is the
same shape as [PYTHON_AND_C.md](PYTHON_AND_C.md)'s: it is a **mirror for C**. Every
rule this project has about the limits of testing — a floor of zero cannot
validate, perturb per check, audit the universe — circles one question:* **what
would it take not to need the test?** *Ada answers part of that with its type
system, and SPARK answers more of it with a prover. Reading them here sharpens
the C; it is not a proposal to rewrite anything.*

> **Unfamiliar word?** [`VOCABULARY.md`](VOCABULARY.md) defines the words this
> project uses before it uses them.

## Not an Ada course — a C lesson wearing Ada

This page will not teach you Ada, and the curriculum is not gaining a graded Ada
course. The language families stay what they are: **systems** (C, C++, Zig, Rust)
and **functional** (Racket, Guile, Elixir, Haskell, Clojure). Ada's place is
*here*, beside Python, as a lens.

What makes the lens worth grinding is that Ada is the one mainstream systems
language that **institutionalised by declaration the things jichi does by
convention** — and SPARK is the only one that replaces a class of test with a
proof.

## What jichi does by discipline, Ada does by declaration

| jichi, in C89 | Ada |
|---|---|
| `jc_snprintf` everywhere, `sprintf` banned, and `tests/smoke/sprintf_lint.sh` to enforce the ban | bounded strings are a type; there is no unbounded `sprintf` to ban |
| a comment saying this `int` is a percentage, and hoping | `type Percent is range 0 .. 100;` — out of range is a hard error, not a comment |
| a `jc_status` return every caller must remember to check | an unhandled exception propagates; ignoring a failure takes effort |
| `include/jc_*.h` as the interface, by convention | a package **specification** is a separate compilation unit; the body cannot widen it |
| "the three arenas, by lifetime" in `CLAUDE.md`, enforced by `arena_lint.sh` | scope and accessibility rules the compiler checks |
| `-Wall -Wextra -Werror`, plus **65** `*_lint.sh` drivers | much of the same, before the flags |

Read that table as a description of **where the checking lives**, not a scoreboard.
C89 puts it in reviewers, conventions and lints — sixty-five lint drivers'
worth of instruments this project built because the language would not, and which
it can at least point at. Ada puts more of it
in the language, where it costs nothing per project and cannot be forgotten.

## The exhaustive case: the same trick, from opposite ends

[`include/jc_outcome.h`](../include/jc_outcome.h) is the clearest place the two
meet. jichi switches on `enum jc_run_stop` with **no `default:` label**, so
`-Wswitch` under `-Werror` turns a new enum value into a build error at every site
that renders it. Its own header states the reason: *"The compiler holds the matrix;
a reviewer does not have to."*

That is a C idiom assembled from three separate decisions — omit the label, enable
the warning, promote it to an error — any one of which a future edit can undo
silently. In Ada, a `case` over an enumeration that misses a value **is simply
illegal**; there is nothing to enable and nothing to forget.

Same insight, arrived at from opposite directions. Worth knowing that the trick
you are proud of in one language is the floor in another — and worth knowing it is
a *floor*, so you stop treating it as a ceiling.

## SPARK: proving what this project tests

SPARK is a subset of Ada with a prover (`gnatprove`). Within it you can establish,
statically and for all inputs, things this tree currently establishes by running
code:

- **Absence of runtime errors** — no overflow, no division by zero, no array index
  out of bounds, no read of an uninitialised variable. jichi reaches for the same
  assurance with ASan, UBSan, valgrind and 23 fuzz targets, all of which sample
  inputs. A proof does not sample.
- **Contracts** — `Pre`, `Post`, `Type_Invariant` — checked by the prover rather
  than by a test that must think to exercise the case.

AdaCore's adoption levels (Stone → Bronze → Silver → Gold → Platinum) exist
precisely because this is incremental: **Silver** is absence of runtime errors and
is where most projects stop, and it is the level that would matter to a program
like this one. This is not exotic: it is how avionics (DO-178C), rail (EN 50128)
and several separation kernels are built.

## What SPARK does not fix, and this is the part worth reading twice

**The hollow green survives the move.** `VOCABULARY.md` defines a *hollow green* as
a pass that proves nothing — an empty suite, a verifier that cannot fail. A SPARK
postcondition of `Post => True` discharges instantly and says nothing. You have not
escaped the problem; you have **relocated it from "is this test vacuous?" to "is
this contract vacuous?"** — and the second question is harder, because a contract
*looks* like a specification while an empty test suite at least looks empty.

**And proof does not reach where this project's bugs actually live.** Two real
defects were found on 2026-09-22, hours before this page was written:

1. `uname()` returns a **non-negative** value on success, and illumos returns a
   positive one, so four call sites written `== 0` read a successful call as a
   failure. That is a fact about a platform's C library, not a property of the
   code's logic. No prover knows it.
2. `git archive <commit>` writes a `pax_global_header` that illumos `tar`
   materialises as a real file. That is a fact about two programs' behaviour.

Both were found by **running on the platform**, which is the rule
[`PLATFORMS.md`](PLATFORMS.md) already states. SPARK would have proved the
arithmetic around them flawless. *A proof is only as wide as its model, and the
model stops at the process boundary* — which is exactly where an agent that calls
models, spawns tools and talks to five kernels does most of its living.

So the honest reading: SPARK would retire a **category** of jichi's testing (the
memory-safety and arithmetic layer that ASan, UBSan and the fuzz targets sample),
and would leave the platform tier, the smoke tier and the driven-row rule entirely
intact. That is a real gain and a bounded one.

## Why jichi is not written in Ada

Not taste, and not the argument above. **The platform matrix decides it.**

`PLATFORMS.md` carries 24 rows: four kernels with the full gate, five libcs, two
Windows emulation layers, 20 `zig cc`+musl triples of which 19 are driven, illumos,
a 160 MB VM and a Pi Zero 2 W. Every one of them was reached because the program is
**C89 with libcurl and nothing else** — a language whose compiler is already on the
target, or one `pkg install` away, on systems whose vendors stopped shipping
updates a decade ago.

GNAT exists for far fewer of those, and a self-hosting Ada toolchain is a much
larger thing to get onto a strange kernel than `cc`. A rewrite would buy proof and
pay for it in the rows this project spent its whole platform campaign earning. For
a program whose thesis is *run everywhere and say honestly where it has not*, that
trade is not close.

The same reasoning, stated once more in the form this repository prefers: a
requirement met, not a language rejected.

## Prove it to yourself

1. **Find the `-Wswitch` seam.** `git grep -n 'JC_STOP_' src` — four switches
   across two files, none with a `default:`. Add a value to `enum jc_run_stop` and
   build: four errors with file:line. That is Ada's exhaustive `case`, hand-built.
2. **Find a vacuous contract in your own tests.** Take any assertion in
   `tests/` and ask what it would take for it to pass without the code being
   right. If the answer is "not much", you have found the hollow green that SPARK
   would not have saved you from either.
3. **Read one AdaCore proof example** ([BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) §13) and
   then count how many of the last ten entries in [`ANECDOTES.md`](ANECDOTES.md)
   it could have prevented. The number is not zero and it is not most of them.

## Where this sits in the curriculum

A curriculum **extra**, like [PYTHON_AND_C.md](PYTHON_AND_C.md),
[C_STANDARDS.md](C_STANDARDS.md) and
[READING_OPEN_SOURCE.md](READING_OPEN_SOURCE.md) — not a family member, not
graded. It teaches a **proof boundary** and engineering judgment, using Ada and
SPARK as the mirror.

If a graded Ada course is ever wanted, the honest precondition is a reason beyond
completeness: the functional family exists because five languages teach *one*
idea, and a lone Ada course joins nothing. The reading is in
[BIBLIOGRAPHY.md](BIBLIOGRAPHY.md) §13; the argument for what it would cost is
here.
