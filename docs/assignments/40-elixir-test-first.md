---
title: Write the test first (Elixir)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/40-elixir-test-first/test.sh"
hints:
  - "`list_max` passes on the lists people usually try. Which list would it get *wrong*? Think about what value it starts reducing from."
  - "Write `test_list_max.exs` — start from the ExUnit skeleton in the task body, `import ListMax`, and add at least three `assert` checks. Include one for a list of *only negative* numbers, and watch it fail first."
  - "`Enum.reduce(list, 0, &max/2)` seeds the reduce at 0, so an all-negative list can never beat 0. Seed from the data — `Enum.max(list)`, or `Enum.reduce(list, &max/2)`. Write the failing test, then fix."
---

> **Prerequisite: Elixir (`elixir`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. To get it: install Elixir/OTP (elixir-lang.org).

`docs/assignments/40-elixir-test-first/list_max.exs` returns the largest element
of a list. It looks right and passes on the lists people usually try. It is
wrong — the same bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test_list_max.exs` next to `list_max.exs` and write an **ExUnit** suite that
pins the correct behaviour — including the case that exposes the bug. Start from
this skeleton:

```elixir
Code.require_file("list_max.exs", __DIR__)
ExUnit.start()

defmodule ListMaxTest do
  use ExUnit.Case
  import ListMax

  # ... at least three `assert` checks, incl. an all-negative list ...
end
```

Watch it **fail first** (that is the proof the bug is real), then fix
`list_max.exs` so your test goes green.

The grader checks all three: your test exists and passes, it has at least three
`assert`s (a hollow suite is not proof), and an independent probe confirms the
bug is actually gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/40-elixir-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/40-elixir-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/40-elixir-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
