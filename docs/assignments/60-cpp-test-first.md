---
title: Write the test first (C++)
audience: student
phase: testing
difficulty: easy
points: 3
verify: "sh docs/assignments/60-cpp-test-first/test.sh"
hints:
  - "`list_max` passes on the vectors people usually try. Which one would it get *wrong*? Think about what value it starts from."
  - "Write `test_list_max.cpp` — `#include \"list_max.hpp\"`, a `main`, and at least three `assert(...)` checks. Include one for a vector of *only negative* numbers, and watch it fail first."
  - "`long m = 0;` seeds at 0, so an all-negative vector can never beat 0. Seed from the data — `long m = xs[0];` and loop from index 1 (or `*std::max_element(...)`). Write the failing test, then fix."
---

> **Prerequisite: a C++ compiler with AddressSanitizer (`g++`/`clang++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/60-cpp-test-first/list_max.hpp` returns the largest element of
a vector. It looks right and passes on the usual vectors. It is wrong — the same
bug the C course's `stats_max` had, in a functional coat.

This task is **tests as proof**. There is no test file yet; you write it. Create
`test_list_max.cpp` next to `list_max.hpp` — a base-only suite that pins the
correct behaviour, including the case that exposes the bug:

```cpp
#include "list_max.hpp"
#include <cassert>

int main() {
    // ... at least three assert()s, incl. an all-negative vector ...
    return 0;
}
```

Watch it **fail first**, then fix `list_max.hpp`. The grader checks all three:
your test exists and passes under ASan, it has at least three `assert()`s (a
hollow suite is not proof), and an independent probe confirms the bug is gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/60-cpp-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/60-cpp-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/60-cpp-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
