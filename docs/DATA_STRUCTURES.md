# Choosing a data structure — jichi has one, and that is an argument

jichi is 97k lines of C and has **exactly one general-purpose container**:
`jc_vec`, a dynamic array, 80 lines. No hash table. No linked list. No tree.
Counted 2026-09-16 ([the coverage review](analysis/2026-09-16-language-teaching-coverage.md)
§3b.2) — which also found that the curriculum never teaches those three, and
never says it isn't going to.

This page says what jichi chose, **why it is defensible**, **where it would be
wrong**, and how to build the three it does not have. The graded half — including
a task designed so that it can *refute* this page — is
[plans/2026-09-files-and-structures.md](plans/2026-09-files-and-structures.md).

**Three moves per section, in order:** what jichi does and why; when that is
wrong; what to reach for instead. If a section ever loses the middle move it has
become marketing.

**No compiler today?** [Appendix A](#appendix-a--the-read-only-twin) runs the
whole page as reading.

---

## 1. The one container

```c
struct jc_vec {
    void   *data;
    jc_size len;   /* number of elements  */
    jc_size cap;   /* capacity in elements */
    jc_size elem;  /* size of one element */
};
```

Byte-oriented and generic by `void *` plus an element size; typed access is a
cast, `((T *)jc_vec_at(v, i))`. Growth doubles from 8 via `realloc`
(`src/util/jc_vec.c`). That is the whole design.

**Look at what the API does not have.** The complete surface is `init`, `free`,
`reserve`, `push`, `push_slot`, `at`, `clear`. There is **no remove, no erase, no
insert-at, no find** — zero occurrences, checked.

That absence is the most informative thing on this page, and §2 is why.

### Why `void *` and not macros

C89 has no generics. Your two options are a `void *` container with an element
size (type safety lost at the boundary, one copy of the code) or macro-generated
per-type containers (type safety kept, and the error messages become
unreadable). jichi took the first and pays for it with casts at every call site.

**When that is wrong:** if your team ships a library whose users are not you,
the cast-at-every-site tax lands on them. C11's `_Generic`, or macro templates,
or writing the three concrete types you actually need, all beat it.

---

## 2. Arenas change the question

jichi has **three arenas, by lifetime** — session, per-turn, per-tool-call — and
`tool_scratch` is reset before *every* tool call at every depth.

This changes what a container must do. Ask when jichi removes a single element
from a collection, and the answer is almost never: things are appended during a
turn, read, and then the **whole arena dies at once**. `jc_vec_clear` sets `len =
0` and keeps the capacity — bulk disposal, no per-element bookkeeping.

**Half of the usual case for a hash table is O(1) *removal*.** If nothing is ever
removed individually, half the argument evaporates before you start.

**When that is wrong:** the moment you have a long-lived collection with genuine
churn — a cache with eviction, a connection table, a session registry that
outlives any one turn. Then you need removal, and a structure that supports it
cheaply, and arena lifetimes stop doing the work for you.

> **Something to do.** Find `jc_vec_clear` and every call site. Is any of them
> removing *one* element, or are they all resetting a whole collection?

---

## 3. The hash table jichi does not have

### What jichi does instead

`jc_tool_registry_find` (`src/tools/jc_tool.c:103`) resolves a tool by name —
**a linear scan with `strcmp`, over a `jc_vec`**:

```c
canon = jc_tool_canonical_name(name);
for (i = 0; i < r->tools.len; i++) {
    const struct jc_tool *t = *(const struct jc_tool **)jc_vec_at(..., i);
    if (strcmp(t->name, name) == 0 || strcmp(t->name, canon) == 0) {
        return t;
    }
}
```

**N is 17.** At that size a contiguous scan is not a compromise, it is the fast
answer: 17 pointers sit in a couple of cache lines, the branch predictor learns
the loop, and there is no hash to compute, no bucket to chase, no allocation.

And notice what the loop does that a hash lookup would not: it matches the exact
name **or a canonical alias in the same pass**, so a model guessing `todoadd`
resolves to `todo_write` instead of failing. A hash map needs two lookups or a
second table to do that.

> **Measured 2026-09-16 (M636i), and the paragraph below did not survive it.**
> Task 78's bench — 300,000 lookups of present keys, `k000000`-style seven-byte
> keys, `-O2`, a Ryzen 9 3900X — found the chained hash table **never slower
> than the scan at any N from 8 upward**: 62 vs 70 ns at N = 8 (inside the
> run-to-run noise), 69 vs 104 at N = 32, 67 vs 142 at N = 64, 77 vs 6,093 at the
> repo map's 4,096. "Often in the hundreds" was a belief; for keys that share a
> prefix — the common case for names — `strcmp` walks that prefix on every
> compare and the scan loses early. The claim is kept as written, so a reader can
> see what the measurement corrected. What still stands: at N = 17 the scan costs
> about 15 ns more on a call that costs microseconds elsewhere, so it is *fast
> enough*; it is not *faster*, and at 4,000 it is eighty times slower. The
> reference `MEASURE.md` in the e2e proof is that run; yours may differ, which is
> the point of the task.
>
> **Re-measured on a second machine, 2026-09-17 (M639).** A Raspberry Pi 400
> (4x Cortex-A72 at 1.8 GHz, 1 MB shared L2) ran the same bench: the table was
> never slower there either, everything 4x to 5x slower, the same shape -- and the
> N = 8 gap that sat inside the Ryzen's noise is measurable on the A72 (262 vs 233
> ns, with the hash column moving 0.3 ns between runs). A smaller cache and a slower
> `strcmp` punish the scan earlier, not later. Task 80's ordered-map bench moved
> more: point lookups, a wash on the Ryzen, favour the sorted array from N = 4,000
> on the Pi and by 2.7x at 64,000, and range walks by up to 20x -- a pointer chase
> costs more behind 1 MB of L2 than behind 64 MB of L3. The workload decides the
> structure; the machine moves where the lines cross. Both tables are in the
> reference `MEASURE.md` of tasks 78 and 80 (`tests/e2e/curriculum_graders.py`).

**The honest general claim** is not "hash tables are slow". It is that the
crossover between a linear scan over contiguous memory and a hash lookup sits
much further out than the asymptotics suggest — often in the hundreds — because
O(1) with a hash computation, a pointer chase and a cache miss loses to O(n) with
no misses while n is small.

**Where jichi's N is not small:** the repo map is capped at
`JC_REPOMAP_MAX_FILES` = **4000**. That is the one place this argument is under
real pressure, and it is the first place to look if you want to prove this page
wrong.

### When the scan is wrong

- **N grows past your measured crossover** — and it is *yours*, not this page's.
- **Lookups dominate the workload.** A scan per item over N items is O(n²).
- **Keys are long strings with shared prefixes.** `strcmp` walks the common
  prefix every time; a hash reads each byte once.
- **You need removal**, and §2's arena argument does not apply.

### Building one — what to get right

- **Collisions are not an edge case.** Chaining (a list per bucket: simple,
  allocation per insert) or open addressing (linear/robin-hood probing: cache
  friendly, deletion needs tombstones). Pick and say why.
- **Deletion is where naive tables break.** Under open addressing you cannot just
  blank a slot — you break the probe chain that runs through it.
- **Load factor drives everything.** Resize around 0.7; resizing means rehashing
  every key.
- **The hash must suit the keys.** FNV-1a or djb2 for short strings; and jichi's
  own history has a warning attached — a hash accumulating into a *signed* int
  was a real defect, because signed overflow is undefined behaviour, not
  wrap-around (it survives as a trap case in `curriculum_graders.py`).

> **Something to do.** Before writing anything: instrument
> `jc_tool_registry_find` with a counter and run a normal session. How many
> comparisons does it actually do? Multiply by the 17. Is this worth fixing?

---

## 4. The linked list jichi does not have

### What jichi does instead

Nothing holds a pointer into a `jc_vec` across a `push`. It cannot: `realloc`
may move the buffer, and every held pointer becomes dangling — a use-after-free
ASan catches instantly, and the subject of graded task 51.

jichi's discipline is **hold an index, not a pointer**, or allocate the elements
from an arena so their addresses never move while the *vector of pointers to
them* grows. The registry above does exactly that: a `jc_vec` of `struct jc_tool
*`, where the tools themselves live elsewhere and stay put.

### When that is wrong

A list earns its place when you need:

- **stable addresses** — a node's address survives every insertion, no index
  bookkeeping, no invalidation rules to remember;
- **O(1) splice** — move an element between lists, or reorder, without shifting;
- **intrusive membership** — an object on several lists at once, with the links
  inside the object and no separate allocation (the kernel idiom).

The cost is real and usually decisive: **a node per element, a pointer chase per
step, and no locality**. Iterating a list of a million elements is dramatically
slower than iterating an array of them, for the same asymptotic complexity.

### Building one — what to get right

- A **dummy head node** removes almost every special case in insert and remove.
- Doubly-linked costs one more pointer and makes removal O(1) given the node.
- **Intrusive** (links inside the element) versus **external** (links in a
  wrapper) is the interesting design axis, not singly versus doubly.
- The classic bug is removing a node while iterating and then dereferencing
  `node->next` through the freed node. Save `next` first.

> **Something to do.** Write the three-line program that holds a pointer into a
> `jc_vec`, pushes past capacity, and dereferences. Run it under
> `make SAN=1`. Read what ASan prints — then write the index version.

---

## 5. The tree jichi does not have

### What jichi does instead

**`qsort` + `bsearch` is the tree jichi did not write** — `qsort` in 4 `.c` files,
`bsearch` in 2. Sort once, then answer ordered queries in O(log n) over
contiguous memory, with no nodes, no balancing and perfect locality.

This is the right structure for a set that is **built, then queried**: the repo
map, a sorted session list, a converted config's key table.

**One trap jichi has already hit:** `qsort` is **not stable**, and the C standard
does not say what it does with equal keys. A sort keyed on
second-granularity mtime made an output nondeterministic — equal keys, arbitrary
order, a diff that changed between runs. If ties must be broken, **break them in
the comparator**, explicitly.

### When sorted-array is wrong

- **Insertions are interleaved with queries.** Each insert is O(n) to shift, or
  you re-sort; under real churn a balanced tree wins.
- **The set is large and changes constantly.**
- **You need ordered iteration *while* mutating.**

### Building one — what to get right

- An unbalanced BST degenerates to a linked list on sorted input, which is the
  common real-world case. **Test with sorted input**, or you will ship the
  degenerate path.
- Balancing (AVL, red-black) is where the complexity lives; B-trees win for
  anything touching a disk or a cache line budget.
- For ordered iteration, know whether you need recursion, an explicit stack, or
  threaded links.

> **Something to do.** Find the `qsort` comparator in `src/index/jc_repomap.c`.
> Does it break ties? Construct an input where the lack of a tiebreak shows.

---

## 6. Choosing, in one table

| You need | Use | Not |
|---|---|---|
| Append, iterate, bulk-discard | **dynamic array** | anything else |
| Lookup by key, N in the dozens | **linear scan** — measure before changing | hash table |
| Lookup by key, N large or lookup-dominated | **hash table** | scan |
| Ordered queries over a stable set | **sorted array + `bsearch`** | tree |
| Ordered queries under insertion churn | **balanced tree** | sorted array |
| Stable addresses, O(1) splice | **linked list** | array |
| Everything dies together | the arena does it | per-element removal |

**The honest summary of jichi's position:** one container, chosen when N is small
and lifetimes are short, both of which are true *for jichi*. Neither may be true
for you — and §3's "something to do" is deliberately the measurement that could
show jichi itself is wrong at 4000 files.

---

## Appendix A — the read-only twin

No compiler needed; every answer is in the source.

1. **The surface.** List every function in `include/jc_vec.h`. Which common
   container operation is missing, and what in jichi's design makes its absence
   survivable?
2. **The growth.** Find the doubling in `src/util/jc_vec.c`. Why 8, and why
   doubling rather than +1 or +1024? What does `realloc` do to pointers held into
   the old buffer?
3. **The scan.** Read `jc_tool_registry_find`. How many tools are registered
   (`jichi describe --output json` says, or count the `register` calls)? Name the
   thing that loop does which a hash lookup could not do in one pass.
4. **The pressure point.** Find `JC_REPOMAP_MAX_FILES`. Write the sentence you
   would need to be true for a scan over that many entries to still be right.
5. **The sort.** Find the `qsort` call sites. For each, is the comparator total —
   does it break every tie — or could two inputs compare equal?
6. **The absence.** `grep -rn "hash" src/ --include='*.c'` finds uses but no hash
   *table*. Pick one and say whether a table would have helped it.

*See also: [`FILE_HANDLING.md`](FILE_HANDLING.md) (its sibling) ·
[`plans/2026-09-files-and-structures.md`](plans/2026-09-files-and-structures.md)
(the graded tasks) · [`reading/fukabori-03-the-three-arena-lifetime-model.md`](reading/fukabori-03-the-three-arena-lifetime-model.md)
(the arenas in depth) · [`BIBLIOGRAPHY.md`](BIBLIOGRAPHY.md) §5 (Rust's six linked
lists, the best treatment of why ownership makes these structures hard).*
