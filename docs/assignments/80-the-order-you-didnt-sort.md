---
title: The order you didn't sort
audience: student
phase: implementation
stage: c-io
difficulty: advanced
points: 4
verify: "sh docs/assignments/80-the-order-you-didnt-sort/test.sh"
hints:
  - "Start with sorted.c, because it is the smaller one and jichi's own answer: a lower_bound binary search gives you the slot for get, for range (walk from lower_bound(lo) while key <= hi) and for put -- where an existing key overwrites in place and a new one needs a memmove of everything above the slot. That memmove is the cost the bench measures."
  - "For bst.c, put and get are a walk down from the root by comparison; an existing key overwrites and does NOT change the count. Range is an in-order walk that skips left subtrees below lo and stops at the first key above hi. Then read ordered.h's warning about SORTED input: the tree becomes a list 3,000 deep in the grader and 64,000 deep in the bench, so walk and free iteratively (an explicit stack), or a recursive version will run out of stack exactly where the lesson is."
  - "Build the bench with -O2, run it three times, paste the table under ## Results, then write ## Finding (what the sorted rows did to the tree -- say the word 'sorted'), ## Balancing (unbalanced and why that is acceptable here, or which balanced form you would reach for), ## Deletion (what you would do, and why it is out of this contract). The grader checks the shape of the measurement, never which way it came out."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

A hash table cannot answer "every key between `lo` and `hi`, in order". Two
structures can. This task has you build both to one contract, `ordered.h`,
and then measure them against each other on your own machine -- because which
one is right depends on the workload and on nothing else.

`docs/assignments/80-the-order-you-didnt-sort/` holds the contract
(`ordered.h`), two stubs (`sorted.c`, `bst.c`), and a benchmark (`bench.c`,
given) that runs two workloads across N and prints a table.

**What you must do.**

1. `sorted.c`: a sorted array searched with binary search -- **what jichi
   does**. Insert must *keep* the array sorted: find the slot, `memmove` the
   tail up by one. Overwrite an existing key in place.
2. `bst.c`: a binary search tree. **Unbalanced is allowed** and is enough for
   the grader; balancing is reading, not grading. But the grader and the bench
   both feed the tree **sorted** input, on which an unbalanced tree becomes a
   linked list N deep -- still correct, only slow -- so walk it and free it
   without recursion, or it will run out of stack exactly where the lesson is.
3. Measure: build the bench with `-O2`, run it three times, and write
   `MEASURE.md` with `## Machine`, `## Method`, `## Results`, `## Finding`
   (what sorted input did to the tree), `## Balancing` (your position) and
   `## Deletion` (what you would do about the operation this contract leaves
   out, and why it is the hard one).

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/80-the-order-you-didnt-sort.md
```

## What jichi does, and why

**`qsort` + `bsearch` is the tree jichi did not write** -- `qsort` in four
source files, `bsearch` in two. Sort once, then answer ordered queries in
O(log n) over contiguous memory: no nodes, no balancing, perfect locality. It
is the right structure for a set that is **built, then queried** -- the repo
map, a sorted session list, a converted config's key table -- and every one of
jichi's uses is that shape. One trap it has already paid for: `qsort` is not
stable, and a sort keyed on second-granularity mtimes once made an output differ
between runs. If ties must be broken, break them in the comparator.

## When that decision is wrong

When insertions are **interleaved** with queries. Each insert into a sorted
array shifts everything above it -- O(n) -- or you re-sort; under real churn a
tree wins, and a *balanced* tree wins reliably. When the set is large and
changes constantly. When you need ordered iteration *while* mutating. The
bench's two workloads are exactly this split: insert-heavy against query-heavy,
each on random and on sorted keys, and the finding you write down is where
**your** churn puts you.

## What you would reach for instead -- and the honest part

An unbalanced BST degenerates to a list on sorted input, and sorted input is
the common real-world case: log lines, ids, timestamps. **Test with sorted
input or you will ship the degenerate path** -- the grader does, and the bench
has a safety cap on the sorted tree row past 20,000 keys because the
degenerate insert is O(n²); a row that says *skipped* is a result, and belongs
in your finding. Balancing (AVL, red-black) is where the complexity lives, and
B-trees win for anything that touches a disk or a cache-line budget; this task
leaves them to [`DATA_STRUCTURES.md`](../DATA_STRUCTURES.md) §5 on purpose,
because a grader can check that a balanced tree is *a tree*, not that it is
balanced, and a shape-only check for the hardest part would be a hollow one.
**Deletion** is left out of the contract for the same reason: in an array it
is a `memmove`; in a tree it is where the classic mistakes live (the two-child
case, the successor swap), and it deserves its own task rather than a corner of
this one. Say what you would do, in `## Deletion`.

## What the grader checks, and what it cannot

One probe, compiled twice (once per prefix): 3,000 random keys from a space of
65,536, so duplicates *will* arrive and must overwrite; every key found;
absent keys absent; min and max; seven range walks compared with a brute-force
reference -- ascending, inclusive bounds, each key once, the empty range, a
range past the end -- and then the same probe on 3,000 sorted keys. Under
AddressSanitizer and LeakSanitizer throughout. The measurement is checked for
its shape and never for its direction. What the grader cannot see: whether your
tree is balanced (it does not ask), and how your numbers compare with anyone
else's (it does not run the bench). The number that matters is the one from
your machine.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/80-the-order-you-didnt-sort.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/80-the-order-you-didnt-sort.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
