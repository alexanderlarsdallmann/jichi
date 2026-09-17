---
title: When a vector is wrong
audience: student
phase: implementation
stage: c-io
difficulty: intermediate
points: 3
verify: "sh docs/assignments/79-when-a-vector-is-wrong/test.sh"
hints:
  - "Run the grader and read ASan's report for fix 1: heap-use-after-free, in team_vec_captain, on memory freed by realloc inside team_vec_add. The captain is stored as a POINTER into an array that moved. Store WHICH player instead -- an index -- and compute the address on every call."
  - "For fix 2, start from DATA_STRUCTURES.md §4: a dummy head node so add/remove have no first/last special case; doubly linked so remove is O(1) given the node; a `next` saved BEFORE free in every loop that frees. Each node holding its owning list makes team_list_move and team_list_remove able to refuse a foreign node in O(1)."
  - "team_list_move must unlink the SAME node and link it into the other list -- no malloc, no copy. The probe keeps the node's address across the move and checks the goals it wrote through that address are still there. If you find yourself copying the player, you have built a vector with worse locality, not a list."
---

> **Prerequisite: a C compiler with AddressSanitizer (`cc`/`clang`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/79-when-a-vector-is-wrong/team_vec.c` keeps a team in a
growable array and remembers the captain by **address**. `realloc` may move
the array. After the move, that address points into freed memory -- and the
program keeps working until the day the array happens to move, then reads
garbage. Under AddressSanitizer it stops on the spot and names the line.

This task asks for the fix **twice**, because the lesson is the comparison:

1. **The jichi way**, in `team_vec.c`: keep the array, remember *which* player
   is captain (an index), and compute the address on every call. The header
   `team_vec.h` says exactly what each pointer's lifetime is.
2. **The list**, in `team_list.c` (a stub): nodes whose addresses are stable
   for their whole lifetime, and an O(1) *move* of a node between two teams --
   the same node, the same address, no copy. `team_list.h` is the contract.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/79-when-a-vector-is-wrong.md
```

## What jichi does, and why

jichi has one general container, `jc_vec`, and **nothing in the source holds a
pointer into one across a push** -- it cannot, and task 51 (*The dangling
pointer*) is the memory half of this lesson. The discipline is *hold an index,
not a pointer*, or allocate the elements from an arena so their addresses never
move while a vector of *pointers to them* grows. The tool registry is exactly
that shape: a `jc_vec` of `struct jc_tool *` whose targets live elsewhere and
stay put. There is no list module because, for jichi's shapes, indices plus
arenas are the cheaper discipline: contiguous memory, no node per element, no
pointer chase.

## When that decision is wrong

A list earns its place when you need **stable addresses** (a node's address
survives every insertion, with no invalidation rule to remember), **O(1)
splice** (move an element between lists without shifting anything), or
**intrusive membership** (an object on several lists at once, the links inside
it -- the kernel idiom). The cost is real and usually decisive: a node per
element, a pointer chase per step, and no locality. Iterating a list of a
million elements is dramatically slower than iterating an array of them, at the
same asymptotic complexity. The course's honest statement is that the list
trades locality for address stability, and **your** use-case is buying one of
those two. Say which, in a comment at the top of `team_list.c`.

## What to get right, and what the grader sees

A dummy head removes almost every special case. Doubly linked makes removal
O(1) given the node. The interesting design axis is *intrusive versus external*
links, not singly versus doubly -- here the links must be external, because
`struct player` is shared with the vector and must not know about lists. And
the classic bug: removing a node while iterating and then reading `next`
through the freed node. **Save `next` first.** The grader's own loops do; your
`team_list_free` must too, and ASan is watching it.

The grader grows the vector by 200,000 players so that any "big enough"
reserve still moves, re-reads the captain through the API, walks the list in
order, moves a node between teams and checks its **address** did not change,
removes every other node while walking, and frees everything under
LeakSanitizer. Both fixes must exist and both must be clean; the probe cannot
tell you which one your own program needs -- that is the question the brief
leaves you with.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/79-when-a-vector-is-wrong.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/79-when-a-vector-is-wrong.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
