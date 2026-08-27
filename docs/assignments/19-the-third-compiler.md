---
title: The third compiler — build jichi with zig cc
audience: student
phase: testing
difficulty: intermediate
points: 3
verify: "sh docs/assignments/19-the-third-compiler/test.sh"
hints:
  - Zig is one downloaded archive; `zig cc --version` tells you more than you expect. Read what it prints CAREFULLY -- that is rung 3's question answering itself.
  - Time the cold build, then touch one file and time the rebuild. Two numbers, one design decision visible. Then compare binary sizes and ask where the extra bytes went (hint -- `strip` one copy, and read about zig cc's default sanitizer mode).
  - "For the cross target: the failure is the finding. Which symbols are undefined, which LIBRARY do they belong to, and what would you have to build to make it link? A toolchain that crosses is not a dependency tree that crosses."
---
jichi builds under gcc and clang (`make ci` gates both), and compiles as
C++ ([CPP_BUILD.md](../CPP_BUILD.md)). This task adds a third toolchain —
**`zig cc`** — which claims C projects build out of the box, and turns the
claim into an experiment. No hardware prerequisite: Zig is a single
archive from ziglang.org (or your package manager).

Run the experiment and write
`docs/assignments/19-the-third-compiler/REPORT.md`:

```markdown
## Environment
<zig version, host, jichi commit>

## The claim, tested
<did `make CC="zig cc"` build? did `make CC="zig cc" test` pass?
exact commands + outcomes>

## Measurements
| | cc | zig cc | zig cc -O2 |
|---|---|---|---|
<cold build wall time, warm rebuild, binary size, suite result --
your numbers, not the ZIG_BUILD.md ones>

## The cross target
<attempt `-target x86_64-linux-musl` (or aarch64): what failed, which
symbols, which dependency owns them, what it would take to link>

## What zig cc actually is
<the unmasking: what drives zig cc under the hood, what zig genuinely
adds around it, and therefore when it is worth using here>
```

Every claim needs the command that produced it. Your measurement numbers
will differ from [ZIG_BUILD.md](../ZIG_BUILD.md)'s — that is the point;
compare and explain the differences that matter (core count, cache state,
zig version). If your cross attempt *succeeds* where ours failed, document
what you had installed that we did not — that is a contribution.

**Grading:** the floor checks the report's structure and that findings are
command-anchored; whether your unmasking and verdict hold water is
judgment (`/check`, or your instructor). The deeper lesson sits behind
rung 3: a portability claim always has a subject — compiler, language,
dependencies, OS — and honest engineering names *which one* it tested.

Grade with `jichi grade docs/assignments/19-the-third-compiler.md`.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/19-the-third-compiler.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/19-the-third-compiler.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
