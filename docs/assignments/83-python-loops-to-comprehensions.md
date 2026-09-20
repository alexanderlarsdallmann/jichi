---
title: Loops to comprehensions, under green tests (Python)
audience: student
phase: implementation
stage: python
difficulty: core
points: 3
verify: "sh docs/assignments/83-python-loops-to-comprehensions/test.sh"
hints:
  - "Run the suite before you change anything: `python3 -m unittest test_report` in that directory. It is green. Keep it green after every single edit — that is what makes this a refactor rather than a rewrite."
  - "`even_squares` is a list comprehension with an `if`: `[n * n for n in numbers if n % 2 == 0]`. The other two are the same shape in braces — one with `key: value` for a dict, one with a bare element for a set."
  - "The grader rejects `.append(` and `.add(` anywhere in `report.py`, including inside a comprehension written only for its side effect. A comprehension you build for what it *does* rather than what it *returns* is a loop in a costume, and it is slower and harder to read than the loop was."
---

> **Prerequisite: `python3`.**

> **Read this first, in your Python snapshot:** `tutorial/datastructures.txt` —
> the sections *"List Comprehensions"*, *"Dictionaries"* and *"Sets"*. All three
> forms are in that one file.

Three small functions, all green, none broken. **That is the point.** This is a
refactor under passing tests, so the suite is your safety net rather than your
task: you are changing *how*, never *what*.

Rewrite all three as comprehensions — a list, a dict, and a set. Run the tests
after each one. If a test goes red, the last edit changed behaviour and you know
exactly which edit it was; that is the entire value of taking small steps, and it
evaporates the moment you rewrite all three and then run.

```sh
# in the jichi checkout (repository root)
cd docs/assignments/83-python-loops-to-comprehensions && python3 -m unittest test_report
```

**The grader checks both halves**, and it has to: green tests alone would pass a
tree you never touched, and "the loops are gone" alone would pass a tree where
you broke something. Behaviour identical, smell removed — neither on its own is
the work.

**Do not edit `test_report.py`.** A refactor that needs its tests changed is not
a refactor; it is a behaviour change wearing one as a disguise.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/83-python-loops-to-comprehensions.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/83-python-loops-to-comprehensions.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/83-python-loops-to-comprehensions.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
