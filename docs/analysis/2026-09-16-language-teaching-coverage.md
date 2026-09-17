# What jichi teaches, per language — a measured coverage review

**Date:** 2026-09-16 (M636). **Method:** counted over the whole documentation
tree — 505 markdown files under `docs/` plus the root pages — with
`/usr/bin/grep`, not the shell's shim (CLAUDE.md names that trap; it bit this
review once before the absolute path was used, reporting `0 files` for a term
present in nine).

**Why now.** The toolchain sweep in the same milestone built the tree with every
front-end it claims to support and found one tier rotted. This is the same
question asked of the *teaching* material: the curriculum claims coverage of C,
C++, Zig and Rust, and nobody had counted what that coverage actually is.

**What this is not.** Not a judgement that the gaps must be filled. Three of the
four are deliberate, and the page says which.

---

## 1. The coverage matrix, counted

| | C89 | modern C | modern C++ | Zig | Rust |
|---|---|---|---|---|---|
| Language/interop page | [C_STANDARDS.md](../C_STANDARDS.md) | [C_STANDARDS.md](../C_STANDARDS.md) | [CPP_INTEROP.md](../CPP_INTEROP.md) | [ZIG_INTEROP.md](../ZIG_INTEROP.md) | [RUST_INTEROP.md](../RUST_INTEROP.md) |
| Toolchain survey | — *(it is the build)* | — | [CPP_BUILD.md](../CPP_BUILD.md) | [ZIG_BUILD.md](../ZIG_BUILD.md) | — |
| Graded systems course | tasks 51–54 | — | tasks 59–62 | tasks 55–58 | tasks 63–66 |
| Other graded tasks | sets A–D, process track | tasks 23, 29, 30 | — | task 19, 25 | — |
| Source reading guides that name it | 9 of 16 chapters | — | 4 | 3 | 2 |
| Bibliography entries (M636) | 17 | *(shared with C89)* | 11 | 8 | **9** *(M636c)* |
| **Can you read the real thing?** | **yes — this tree** | no | no | no | no |

The last row is the one that matters and the one no other row substitutes for.
C89 is the only language here where the learner has a complete, working,
97k-line program with four reading guides written about it. Everything else is
taught **by contrast with** that program. That is the design
([CURRICULUM.md](../CURRICULUM.md): "the exercise language is a parameter, and C
is the deliberate choice of parameter"), and it is worth restating because every
gap below is a consequence of it rather than an oversight.

## 2. Three findings

### 2.1 "Modern C" is taught as a *fence*, not as a language

Counted across all 505 files:

| Modern-C construct | Files naming it |
|---|---|
| `<stdint.h>` | 8 |
| designated initializer | 5 |
| compound literal | 3 |
| VLA | 3 |
| `_Generic` | 2 |
| `uint32_t` | **0** |
| `_Static_assert` | **0** |

Read the shape, not the totals. The constructs that appear are exactly the ones
**CONTRIBUTING.md forbids** — they are named in order to be banned, because this
codebase is C89 and `-Wvla`/`-Walloca` are tripwires for them. The constructs
that appear *zero* times are the ones a modern-C programmer would simply
**use**: a fixed-width integer type, a compile-time assertion.

So a self-learner following this curriculum finishes able to read and write the
C of 1989, and knowing precisely which of today's constructs jichi may not use —
which is not the same as being able to write the C their next employer uses.
[C_STANDARDS.md](../C_STANDARDS.md) and its three graded tasks (23, 29, 30) are
the whole of the bridge, and all three are *portability* tasks: port down to
C89, catch an undefined behaviour, catch an implementation-defined one.

**Partly closed by M636 rather than argued about:** the C section of
[BIBLIOGRAPHY.md](../BIBLIOGRAPHY.md) now carries Gustedt's *Modern C* (free,
C23) and the WG14 working drafts, with the "read it for" line saying exactly
this — read it *against* this tree. A book is not a graded track. It is,
however, the honest size of the gap for a project whose subject is the craft
rather than any one dialect.

### 2.2 Modern C++ is thinner than the C++ *course* implies

| C++ term | Files |
|---|---|
| RAII | 15 |
| `C++17` | 12 |
| `std::vector` | 8 |
| `C++20` | 1 |
| `unique_ptr` | 1 |
| `std::span` | **0** |

The graded C++ course (tasks 59–62) teaches RAII, the standard containers and
exceptions. That is a fair and useful curriculum, and it is **C++98/11 C++**.
The things that make C++ *modern* for someone arriving in 2026 — move semantics,
smart pointers as the default, `constexpr`, ranges, concepts — are named once or
not at all.

The curriculum's own text says "the **C++ systems course** (tasks 59–62: RAII/
ownership, the standard containers, exceptions)", which is accurate. It is the
word *modern* in a reader's head, not on the page, that overstates it. The C++
section of the bibliography is the current answer: *A Tour of C++* (3rd, C++20)
and *Effective Modern C++* are both listed with "read it for" lines that say
which decade they cover.

### 2.3 Rust had a graded course and no literature — **closed the same day (M636c)**

Four graded tasks (63–66), an interop page, two reading-guide mentions, and
**zero** bibliography entries — the only language in the matrix with a graded
course and nothing to read. A straightforward consequence of M636's scope
decision (craft + C + C++ + Zig), and the cheapest gap here to close.

**Closed 2026-09-16** as [BIBLIOGRAPHY.md](../BIBLIOGRAPHY.md) §5: 9 entries, 7
of them free, weighted toward ownership (what tasks 63–66 are really about) and
toward the FFI seam, since `RUST_INTEROP.md` is the clean-boundary track and the
seam is where a C programmer actually arrives. **Every language with a graded
systems course now has literature behind it.** The three findings below stand.

## 3. What was checked and found *sound*

Not everything reviewed was a finding, and saying so is the point of reviewing
rather than hunting.

- **Every systems track has exactly four graded tasks** (51–54, 55–58, 59–62,
  63–66) — verified by file, not by the index's claim.
- **Zig's coverage is proportionate to its maturity.** Three reading-guide
  chapters, a toolchain survey re-verified the same day, a migration track, two
  extra assignments, and eight bibliography entries every one of which is free —
  appropriate for a pre-1.0 language whose reference moves with the compiler.
- **The self-learner-first rule holds.** No task in any systems track requires a
  second person or hardware the reader may not have; the reading guides carry
  Appendix A (a read-only twin for every experiment) for exactly that reason.
- **The four doors in README.md still route.** Run it, find the page, know what
  it believes, learn the craft — each lands on a page that exists and opens with
  what the door promised.

## 3b. Topic coverage: two things a self-learner would expect (asked 2026-09-16)

The sections above ask *which languages* are taught. This one asks *which
subjects*, for two the operator named: *does the curriculum teach a learner to
read and write files, and to build data structures?*

**Method.** Counted over the teaching universe — `CURRICULUM.md`,
`docs/curriculum/`, all of `docs/assignments/` including fixtures, `docs/reading/`,
`C_STANDARDS.md`, `READING_OPEN_SOURCE.md`, `ASSIGNMENTS.md`: **147 markdown
files plus the fixture trees**. Word-bounded matching with `/usr/bin/grep`, and
that detail is load-bearing: the first pass of this count reported `trie` in 14
files (it was matching *en**trie**s*), `stack` in 25 (*stack trace*) and `ring`
in 266 source files (*st**ring***). Every headline number below survived being
re-measured with `\b` anchors. Substring matching produced a wrong answer three
times in one session; it is the same shape as CLAUDE.md's grep-shim warning, one
level down.

### 3b.1 File I/O is taught **nowhere**, and jichi's own source is full of it

| Where | Files that teach reading/writing a file |
|---|---|
| The 79 graded assignments | **0** |
| The 16 reading-guide chapters | **0** |
| jichi's own `src/` | **26** (`fopen`/`fread`/`fwrite`), plus 17 with POSIX `read`/`write`, 5 with `stat`, 4 with atomic `rename` |

Five assignment files mention file I/O at all, and in every one it is **scaffolding
the learner is handed, never the subject**: `22-slope-lies-keep-the-peak`'s four
benchmark candidates read a file because a benchmark needs input (the task is
about reading a *measurement*, not a file), and `25-extend-in-zig`'s `wordtool`
reads one because the lesson is that a Zig `export fn` links behind an unchanged
C header. **No task asks a learner to open, read, write or close anything.**

This is the gap with the strongest claim on attention, for three reasons. It is
not an algorithms topic a craft course could reasonably decline — every program
does it. Its failure modes are exactly the ones beginners get wrong and a grader
*can* check: the file that is not there, the short read, the partial write, the
temp-file-plus-`rename` that makes a replace atomic, the `0600` on something that
holds a secret. And **the material is already in the tree**: jichi does all five,
`jc_path.c` and the session/snapshot writers are readable, M132 is the milestone
that put `0600`/`0700` on the on-disk sinks, and ANECDOTES #1 is a debugging war
story whose root cause was *a log file kept inside the rollback blast radius* —
a file-handling lesson with a scar already attached.

### 3b.2 Data structures are taught narrowly — honestly, but the boundary is never stated

**Taught, with a task behind each:** a growable array (52, and jichi's own
`jc_vec`), a ring buffer (04), an arena/bump allocator (54, and `jc_mem`), a
stack as an RPN evaluator (50, 62), the C++ standard containers (60–61). Rust's
six linked lists arrive through the bibliography (§5 above) rather than a task.

**Never taught anywhere:** linked list, hash table, tree, binary search, any
sorting algorithm.

**The reason is architectural, and it is a good one.** jichi has exactly **one**
general-purpose container — `jc_vec`, a generic dynamic array — and no hash map,
no tree, no list module; it reaches for `qsort` (4 `.c` files) and `bsearch` (2) when
it needs them. A curriculum that teaches by reading its own source cannot teach
what the source does not contain, and inventing structures the product does not
use would break the thing that makes this course unusual.

**What is wrong is not the boundary but its silence.** Measured: `CURRICULUM.md`
contains **no statement of what it does not teach** — zero matches for "does not
teach", "out of scope", "not a course in". A self-learner can finish all four
stages believing data structures were covered, because nothing told them
otherwise, and because the page's own words nudge that way:

> **C systems course** (tasks 51–54) — manual memory & **data structures** under
> AddressSanitizer

Tasks 51–54 are *The dangling pointer*, *The array that outgrew itself*, *Never
call sprintf*, *The arena*. Three of the four are memory and string safety; the
plural "data structures" rests on **one** task. This is the same species as §2.2's
"modern" C++ — accurate words that let a reader infer more than the tasks deliver
— and the cheapest fix is the same: say the smaller true thing.

## 4. What this suggests, in priority order

Recorded as DEFERRED rows rather than done, because each is a milestone and none
is urgent:

1. ~~**A Rust bibliography section**~~ — **done, M636c**, 9 entries. It was the
   only graded-course-without-literature row, and it is now closed.
2. **A "modern C" reading track**, in the genre that works here — the reading
   guide, not more graded tasks (the audience memo is explicit that graded sets
   should not be built on speculation). Its spine writes itself: for each modern
   construct, what jichi does instead and what that costs.
3. **Extend the C++ course or rename it.** Either tasks 59–62 gain a fifth on
   move semantics and `unique_ptr`, or the curriculum says "C++ fundamentals"
   where it currently lets the reader supply "modern". Renaming is free and
   honest; extending is better and is a milestone.
4. **The remaining six languages' literature** (Racket, Guile, Elixir, Haskell,
   Clojure, Python). Only worth doing at the standard M636 set — fetched, dated,
   and lint-checked — or not at all.
5. **A file-I/O task in the C systems course** (§3b.1). The largest gap found in
   this review, the one least defensible as a scope decision, and the one whose
   material is already in the tree.
6. **A "what this course does not teach" section in `CURRICULUM.md`** (§3b.2),
   and "data structures" → "a growable array" in the tasks 51–54 line. Two edits;
   the second is free.
