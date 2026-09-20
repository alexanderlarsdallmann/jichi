---
title: Make the failing test pass (Python)
audience: student
phase: testing
stage: python
difficulty: intro
points: 2
verify: "sh docs/assignments/81-python-make-it-pass/test.sh"
hints:
  - "Run it first: `python3 -m unittest docs/assignments/81-python-make-it-pass/test_tags.py` from that directory, and read which test failed and what it got."
  - "Two tests fail, and `test_single` fails only when another test ran before it. A function whose answer depends on what happened earlier is holding state somewhere. Where could `add_tag` be keeping a list between calls?"
  - "`def add_tag(tag, tags=[])` — that `[]` is evaluated **once**, when the function is defined, not once per call. Every call with no list appends to the same one. The idiom is `tags=None`, then `if tags is None: tags = []` inside. Fix the function; leave the test module alone."
---

> **Prerequisite: `python3`.** The grader fails loudly, naming the tool, so a missing toolchain never looks like a wrong answer. Any Python 3 will do; nothing here needs a recent one.

> **Read this first, in your Python snapshot:** `tutorial/controlflow.txt` — the
> section *"Default Argument Values"*. If you have not built a snapshot yet,
> [`LANGUAGE_COURSE.md`](../LANGUAGE_COURSE.md) §1 is four commands. The tutorial
> states this behaviour plainly and calls it *important*; this task is what it
> feels like when you meet it in code instead of in a warning box.

The graded **Python track** opens the way every other language track here does —
with a failing test and the fix-forward loop.
`docs/assignments/81-python-make-it-pass/tags.py` holds a four-line function and
`test_tags.py` holds its truth. Run it:

```sh
# in the jichi checkout (repository root)
cd docs/assignments/81-python-make-it-pass && python3 -m unittest test_tags
```

Two tests fail. Drive the loop this course is built on: run the test, read the
failure, form a hypothesis, make the fix, run again.

**Read the two failures together before you touch anything.** One of them fails
for a reason that is not in its own test — it fails because of what a *different*
test did first. That is the whole lesson, and it is worth a minute of looking
before a minute of fixing: a function that remembers is a function that cannot be
tested in isolation, and the bug is not in the test that reported it.

The fix belongs in the **function** — leave `test_tags.py` exactly as it is,
because the test is the truth here, not the obstacle. When it is green:

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/81-python-make-it-pass.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/81-python-make-it-pass.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/81-python-make-it-pass.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
