# C → C++, gradually — the migration track

*A curriculum extra track, the twin of [ZIG_INTEROP.md](ZIG_INTEROP.md):
compile the C project with a C++ compiler, write the next feature in C++,
then refactor existing modules across — behavior pinned the whole way.
Graded floors: [CPP_BUILD.md](CPP_BUILD.md) documents step one on this
repo itself;
[`assignments/27-extend-in-cpp.md`](assignments/27-extend-in-cpp.md)
(extend) and
[`assignments/28-refactor-to-cpp.md`](assignments/28-refactor-to-cpp.md)
(refactor) grade steps two and three.*

## The same arc, a different seam

1. **Compile.** C++ contains *most* of C — jichi itself builds as C++
   ([CPP_BUILD.md](CPP_BUILD.md)), and the deltas that surface (implicit
   `void *` conversions, keyword collisions, stricter enums) are a free
   audit of your C. Do this to a project before believing anything about
   migrating it.
2. **Extend** (assignment 27): the next feature is C++ behind the same C
   header. The seam is `extern "C"` — the linker, not the language, is
   what changed first: C++ mangles names, and the boundary declaration
   switches that off for the symbols that cross. Link with `c++` so the
   runtime comes along.
3. **Refactor** (assignment 28): move a module across under green tests.
   The trap specific to C++ (the Zig twin does not have it): the compiler
   will happily accept your old C in a `.cpp` file, so a "migration" can
   be a file rename. The grader pins the outside; making the inside
   honest — RAII where there was manual cleanup, `std::string_view` where
   there were pointer walks, algorithms where there were loops — is the
   actual work.

## The seam, honestly

| C side | C++ side | what changed |
| --- | --- | --- |
| a symbol is its name | names are mangled | `extern "C"` at every crossing, or the link fails in riddles |
| `malloc`/`free` discipline | RAII owns lifetimes | the M218 lesson (lifetimes are the bug class) becomes a *type-system* concern |
| `const char *` + convention | `std::string_view` | length and non-ownership become part of the type |
| errors as return codes | exceptions exist | do NOT let one cross the C boundary — catch at the seam; a throwing `extern "C"` function is undefined behavior in practice |
| one runtime | libstdc++/libc++ joins the link | your binary's floor grows; on the LOW_MEMORY tiers, *measure* it |

## Choosing between the two tracks

Same method, different destinations: Zig buys a single hermetic toolchain
and checked builds at the cost of a pre-1.0 language; C++ buys thirty
years of libraries and hiring reach at the cost of a much larger language
to hold in your head — and a runtime whose size you should weigh against
[LOW_MEMORY.md](LOW_MEMORY.md)'s tiers before shipping to small machines.
Run both tracks and the *decision* becomes an experience, not an opinion;
either way the transferable skill is the seam discipline: header pinned,
tests green, costs named ([C_STANDARDS.md](C_STANDARDS.md)'s rule,
pointed forward).

## The migration road — a recommendation table

*Shared with [ZIG_INTEROP.md](ZIG_INTEROP.md) and derived in full in the
source reading guide,
[Fukabori chapter 12](reading/fukabori-12-the-migration-road.md), which
argues why a whole-program rewrite is the rarer path (a benefit that spans
the entire program graph loses it at a piecewise seam) and why modern C,
Zig, and C++ — meeting C at the header — are the usual destinations.*

| situation | likely right move |
|---|---|
| living C tree, small team, must keep shipping | modern-C hygiene first; Zig or C++ at the seams next |
| hermetic builds / cross-compiling pain dominates | the Zig track (toolchain first, language later) |
| team already fluent in C++, ecosystem needs libraries | the C++ track, with the rename-trap discipline |
| new security-critical component at a **clean boundary** | build it there in a memory-safe language (Rust the usual pick; the argument admits any) |
| small, well-specified tool + freedom to freeze | a full rewrite is honestly on the table — in any of them |

Find the row whose *enabling condition* you actually have, not the
language you would enjoy: the two rewrite rows each name a condition
("clean boundary", "small + freezable"); with neither, the seam rows are
your road.

The clean-boundary Rust row has its own page:
[RUST_INTEROP.md](RUST_INTEROP.md) — why Rust is the family's exception
(no gradual-refactor arc) and how the single deliberate seam is done.
