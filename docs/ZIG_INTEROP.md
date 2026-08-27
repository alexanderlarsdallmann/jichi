# C → Zig, gradually — the migration track

*A curriculum extra track: compile a C project with Zig's toolchain, write
the next feature in Zig, then refactor existing modules across — behavior
pinned the whole way. Graded floors:
[`assignments/19-the-third-compiler.md`](assignments/19-the-third-compiler.md)
(compile), [`assignments/25-extend-in-zig.md`](assignments/25-extend-in-zig.md)
(extend), [`assignments/26-refactor-to-zig.md`](assignments/26-refactor-to-zig.md)
(refactor). Companion: [ZIG_BUILD.md](ZIG_BUILD.md) (this repo's own
step-one record) and [C_STANDARDS.md](C_STANDARDS.md) (the same
costs-named discipline pointed backward instead of forward).*

## Why this arc, and not a rewrite

The graveyard of migrations is full of big-bang rewrites; the survivors
went **seam by seam**. Zig is built for exactly that route:

1. **Compile** (assignment 19): `zig cc` builds your existing C unchanged.
   Nothing is migrated yet — but the new toolchain is now load-bearing,
   and its claims have been tested, not believed.
2. **Extend** (assignment 25): the *next* feature is Zig. `export fn`
   emits a C-ABI symbol; the C header declares it like any other function;
   `zig build-obj` + the existing link line is the whole integration. The
   old code never notices.
3. **Refactor** (assignment 26): move one existing module across, behind
   its unchanged header, under green tests — Module 7's discipline with a
   language boundary in the middle. Repeat until the seam has moved
   through the whole project, or stop at any point: **every intermediate
   state ships.**

## The seam, honestly

The C/Zig boundary is thin but not free — each crossing is a claim worth
reading out loud:

| C side | Zig side | what changed |
| --- | --- | --- |
| `const char *s` | `[*:0]const u8` | the NUL terminator is now part of the *type*, not a convention |
| `long` | `c_long` | Zig's own ints have exact widths; `c_long` exists precisely for this seam |
| `int flag` | `bool` internally, `c_int` at the boundary | booleans stop being integers the moment they cross |
| undefined behavior | checked in Debug/ReleaseSafe builds | your old bugs may finally crash — that is a feature; read the stack trace |

Zig can also import headers directly (`@cImport`) and translate whole
files (`zig translate-c`) — useful for *reading* what the seam really
involves, but the track's assignments keep the boundary explicit on
purpose: a migration you can grade is one whose seam you can point at.

## When Zig is the wrong answer

Same test as [C_STANDARDS.md](C_STANDARDS.md) applies in reverse: the
newer language costs you reach (a Zig toolchain everywhere you ship), a
second idiom in every reviewer's head, and a moving target (Zig is
pre-1.0; its syntax has churned between releases — this repo pins what
its fixtures were verified against). Name the price, then decide. The
track's point is the *method*, and the method transfers:
[CPP_INTEROP.md](CPP_INTEROP.md) runs the same three steps toward C++.

## The migration road — a recommendation table

*Shared with [CPP_INTEROP.md](CPP_INTEROP.md) and derived in full in the
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
