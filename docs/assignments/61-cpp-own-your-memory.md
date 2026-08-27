---
title: Own your memory (C++)
audience: student
phase: implementation
difficulty: medium
points: 3
verify: "sh docs/assignments/61-cpp-own-your-memory/test.sh"
hints:
  - "Run the grader: `sh docs/assignments/61-cpp-own-your-memory/test.sh`. LeakSanitizer reports that the `new int[n]` in Buffer's constructor is never freed — there is no destructor."
  - "You could add `~Buffer() { delete[] data_; }` — but the modern C++ move (and what the grader wants) is to not manage raw memory at all: replace `int* data_` with `std::vector<int> data_`, initialise it `data_(n)`, and `at`/`size` follow. The vector frees itself."
  - "The grader greps for `new`/`delete`/`malloc`/`free` (comments stripped), so the raw ownership must be *gone*, not just balanced by a destructor. `std::vector<int>` (or `std::unique_ptr<int[]>` via `std::make_unique`) owns it for you."
---

> **Prerequisite: a C++ compiler with AddressSanitizer (`g++`/`clang++`).** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer.

`docs/assignments/61-cpp-own-your-memory/buffer.hpp` is a fixed-size int buffer
that owns heap storage — and it **leaks**: the constructor calls `new int[n]`,
and there is no destructor to free it.

> **RAII is C++'s answer to the C leak.** In C you match every `malloc` with a
> `free` by discipline (and Set D shows how easily that slips). C++ ties a
> resource's lifetime to an object's scope — when the object dies, its
> destructor runs — so the standard containers *own* memory and free it for you.
> The instrument here is **LeakSanitizer** (part of ASan), which fails the test
> when the Buffer leaves scope without freeing.

This is a **refactor**: change *how*, not *what*. Let the standard library own
the storage — replace the raw `int*` with a `std::vector<int>` (or a smart
pointer) — with the suite (`test_buffer.cpp`) still green.

> **Two paired instruments** (like the C `sprintf` task): LeakSanitizer proves
> nothing leaks, *and* a grep proves you reached for RAII rather than hand-rolling
> a destructor with `delete[]`. The grade passes only when both hold: no leak,
> and no raw `new`/`delete`/`malloc`/`free`.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/61-cpp-own-your-memory.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/61-cpp-own-your-memory.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/61-cpp-own-your-memory.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
