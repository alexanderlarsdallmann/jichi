# C and Rust — the clean-boundary track (why this one is different)

*The systems-language family's fourth member, and the deliberate odd one
out. [ZIG_INTEROP.md](ZIG_INTEROP.md) and [CPP_INTEROP.md](CPP_INTEROP.md)
are gradual **compile → extend → refactor** tracks: the new language joins
behind an unchanged C header, seam by seam, and every intermediate state
ships. This track is **not** that — and understanding why is the lesson.
The full argument is [Fukabori chapter 12, the migration
road](reading/fukabori-12-the-migration-road.md); this page is its Rust
specialization.*

## Why there is no "gradual refactor to Rust" here

The Zig and C++ tracks work because those languages' benefit **meets C at
the header**: you port one function, link it against the untouched rest,
and the improvement is real and shippable at every step. Rust's central
benefit — **ownership and borrow-checking across the whole program
graph** — is precisely what a piecewise boundary breaks. Every C↔Rust seam
is `unsafe` FFI where the ownership contract is re-stated by hand: a raw
pointer crosses, and the compiler that would have proven its lifetime is
switched off for exactly that crossing. Migrate a large C tree
function-by-function into Rust and you pay Rust's full cost (the borrow
checker, the ecosystem, the learning curve) while withholding its central
payoff until very late — the opposite of "every intermediate state ships
improved."

So there are **no graded compile→extend→refactor assignments** on this
track, on purpose: grading the gradual-migration arc for Rust would be
grading the wrong thing. (The absence of `rustc` on the reference box that
authored this is a coincidence, not the reason — a graded track this
project could not verify two-sided against its own fixtures would not ship
regardless; see docs/TEST_INTEGRITY.md.)

## Where Rust IS the right call

The recommendation table (shared across the three interop pages and
Fukabori 12) names it: **a new, security-critical component at a clean
boundary**, and **a full rewrite of a small, well-specified program**.
Both are conditions where the benefit-spanning-the-graph property is
satisfied — the new component *is* the graph Rust governs — so the seam is
crossed once, deliberately, at a narrow API, not smeared through a living
tree.

| situation | likely right move |
|---|---|
| living C tree, must keep shipping | modern-C hygiene first; Zig or C++ at the seams next |
| new security-critical component at a **clean boundary** | build it in Rust (or another memory-safe language) beside the C |
| small, well-specified tool + freedom to freeze | a full rewrite is honestly on the table — in any of them |

## The clean-boundary seam, concretely

When Rust *is* the answer, the seam is a narrow C ABI, and the discipline
is the mirror of the gradual tracks' "header pinned, tests green, costs
named" — applied at one boundary instead of many:

1. **The Rust side exposes `extern "C"` functions** with `#[no_mangle]`,
   compiled as a `staticlib` (or `cdylib`), taking and returning only
   C-ABI-safe types (`*const c_char`, `c_long`, opaque `*mut` handles —
   never a `String`, `Vec`, or a borrowed reference across the line).
2. **Every crossing is an `unsafe` block** on the Rust side that validates
   the raw pointer, converts to a safe Rust type, does the safe work, and
   converts back — the `unsafe` boundary is small, auditable, and *named*,
   exactly as `extern "C"` on the C++ side switches off name mangling at a
   point you can see.
3. **Ownership is a hand-written contract at the seam**: who allocates,
   who frees, whether a returned pointer is borrowed or owned. Write it
   down (a header comment is the whole spec) — this is the M218 lesson
   (lifetimes are the bug class) with the type system's help removed at
   the line, so the contract lives in prose and review instead.
4. **A panic must not cross the boundary**: a Rust panic unwinding into C
   is undefined behavior, so wrap the body in `catch_unwind` (or compile
   with `panic = "abort"`) and return an error code — the same rule as
   "an exception must not cross an `extern "C"` boundary" from
   [CPP_INTEROP.md](CPP_INTEROP.md), for the same reason.
5. **The build**: `cargo build --release` produces `libcomponent.a`; the C
   link line adds it plus Rust's runtime (`-lpthread -ldl -lm` and
   friends, or let `cc` driven by the target do it). The C header declares
   the `extern` prototypes; `main.c` calls them like any other function.

The transferable skill is the one the whole family teaches: **name the
seam, pin the contract, and know which side of it each guarantee lives
on.** Zig and C++ let you move the seam gradually through a tree; Rust
asks you to place it once, at a boundary you chose deliberately — and the
judgment of *which* boundary is the actual engineering.

## Where to go from here

- Read [Fukabori 12](reading/fukabori-12-the-migration-road.md) for the
  structural argument in full (why whole-graph-benefit languages lose that
  benefit at a piecewise seam, generalized beyond Rust to GC runtimes and
  managed VMs), and place a real project you work on in the table's rows.
- Walk a gradual seam end to end first, so the contrast is in your hands:
  the [Zig](assignments/25-extend-in-zig.md) or
  [C++](assignments/27-extend-in-cpp.md) extend/refactor assignments. Then
  the difference between "move the seam gradually" and "place it once" is
  experience, not assertion.
- When you have a clean-boundary component to build for real, its
  CONTRIBUTING.md and the narrow API are your rulebook; the panic-catching
  and ownership-contract rules above are the seam checklist.
