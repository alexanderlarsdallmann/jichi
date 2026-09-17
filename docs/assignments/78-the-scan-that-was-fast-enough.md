---
title: The scan that was fast enough
audience: student
phase: implementation
stage: c-io
difficulty: advanced
points: 4
verify: "sh docs/assignments/78-the-scan-that-was-fast-enough/test.sh"
hints:
  - "Read lookup.h before ht.c; then read scan.c, which is given and correct -- your table must keep exactly its contract. Start with chaining: an array of bucket heads, a node per key holding a COPY of the string (malloc + strcpy), FNV-1a or djb2 into an unsigned accumulator. Get all 5,000 keys back before you touch deletion."
  - "The grader's first traps are not about hashing. It overwrites a key and checks the count did not move; it scribbles on the buffer it passed to ht_put and asks again; it deletes every even key and expects every odd one still found. Chaining makes deletion an unlink. If you chose open addressing, a blanked slot breaks the probe chain -- you need tombstones or a backward shift, and MEASURE.md must say which."
  - "Build the bench with -O2 (`cc -std=c89 -O2 -o bench bench.c scan.c ht.c`), run it three times, and paste a table into MEASURE.md under `## Results` -- rows of `| N | scan ns | hash ns |`. Then fill `## Machine` (your CPU), `## Method` (what bench.c does, in your words, and what it does not measure), `## Crossover` (the N where the table started winning, or the plain statement that it never did), `## Deletion` (your position). The grader checks the shape of the measurement, never which way it came out."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

jichi has no hash table. Every tool call is resolved by
`jc_tool_registry_find` -- a linear scan with `strcmp` over a contiguous
array of **17** entries -- and [`DATA_STRUCTURES.md`](../DATA_STRUCTURES.md)
§3 argues that at that size the scan is not a compromise but the fast answer.
That argument rests on a claim about *where the crossover is*, and a claim like
that is not something to believe. It is something to measure. This is the task
that could prove the page wrong, and the grader is built so that it can.

`docs/assignments/78-the-scan-that-was-fast-enough/` holds the contract
(`lookup.h`), the baseline (`scan.c`, given and correct -- do not edit it), a
stub (`ht.c`, yours to replace), and a benchmark (`bench.c`, given) that times
both across N and prints a table.

**What you must do.**

1. Build the hash table in `ht.c` to `lookup.h`'s contract: collisions handled
   (the grader puts 5,000 keys into 64 buckets), overwrite keeps the count, the
   table owns a **copy** of each key, deletion removes exactly that key and
   leaves every other one findable, re-insertion after deletion works, and
   nothing leaks under LeakSanitizer.
2. Measure. Build the bench with `-O2`, run it three times, and write
   `MEASURE.md` with five sections: `## Machine`, `## Method`, `## Results`
   (the table, at least three rows), `## Crossover` (the N where the table
   started winning on *your* machine -- or the plain statement that within the
   N you tried it never did), and `## Deletion` (the position you took:
   chaining, tombstones, backward shift, rehash -- and why).

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/78-the-scan-that-was-fast-enough.md
```

## What jichi does, and why

One general container, `jc_vec`, and a scan when it needs to find something.
Three reasons, and `DATA_STRUCTURES.md` gives each its evidence: C89 has no
generics, so a type-safe map is `void *` or macros; arena lifetimes remove most
of the need for removal; and **N is small** -- the tool registry, the model list,
the skill table are dozens to hundreds. The scan also does something a hash
lookup would not: it matches the exact name *or a canonical alias* in the same
pass, so a model that guesses `todoadd` lands on `todo_write`. That is a real
feature of the linear shape, not an accident.

## When that decision is wrong

When N grows past your measured crossover -- and it is *yours*, not the page's.
When lookups dominate the workload (a scan per item over N items is O(n²)). When
keys are long strings sharing prefixes, so every `strcmp` walks the prefix. And
jichi has one place where N is not small: the repo map is capped at
`JC_REPOMAP_MAX_FILES` = **4000** entries. That is where this page's argument is
under real pressure, and it is where to point your result if the crossover comes
out low. **If your measurement says the table wins below 4,000, the honest
conclusion is that jichi should have one there** -- write that down; it is a
finding, and the grader will not mark you down for it.

## What you would reach for instead -- and what to get right

Chaining (a node per key, deletion is an unlink) or open addressing (a flat
array, cache-friendly, deletion needs tombstones or a backward shift): pick one
and say why. A hash into an **unsigned** accumulator -- jichi's own history has
a hash that accumulated into a signed `int`, and signed overflow is undefined
behaviour, not wrap-around; it survives as a trap in the curriculum's grader.
Resize around load factor 0.7 if you resize at all; the grader does not require
it. And the measurement discipline is task 22's: *slope lies, keep the peak* --
`bench.c` says in its own header what it does and does not measure, and a row
that moves between runs by more than the gap between its columns has measured
nothing yet.

## What the grader checks, and what it cannot

Correctness is checked by a probe under AddressSanitizer and LeakSanitizer, and
the failure messages name the classic defects: the stored pointer, the overwrite
that inserts, the deletion that breaks a neighbour's chain. The measurement is
checked for its **shape** -- a machine, a method, three rows with both numbers, a
crossover statement, a deletion position -- and **never for which way it came
out**.

Two things it cannot see, said plainly. It cannot tell a hash table from a scan
that keeps the same contract: a `ht.c` that searches a list would pass the
correctness probe. The bench can tell, and so can you, which is why the
measurement is the second half of the task and not decoration. And it does not
run the bench: a timing check in a grader is the flaky check nobody trusts, and
the number that matters is the one from your machine, recorded by you.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/78-the-scan-that-was-fast-enough.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/78-the-scan-that-was-fast-enough.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
