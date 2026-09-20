---
title: The word-frequency report (Python capstone)
audience: student
phase: implementation
stage: python
difficulty: advanced
points: 4
verify: "sh docs/assignments/84-python-capstone/test.sh"
hints:
  - "Read `test_freq.py` before you write a line. It is the requirements document: eight assertions, each one a sentence about what the code must do, including the three cases that are easy to miss (a tie, an n larger than the data, n = 0)."
  - "`word_counts`: lower-case the text, pull the letter runs out of it, and count them. `re.findall(r\"[a-z]+\", text.lower())` gets you the words; a dict and `counts.get(word, 0) + 1` counts them. `collections.Counter` does both, and naming it in DESIGN.md is a fine answer."
  - "`top_n` is a sort with a compound key, and the two halves go in opposite directions: count descending, word ascending. `sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:n]` — negating the count is how you reverse one half of a key without reversing the other. Then write one line in DESIGN.md naming that shape."
---

> **Prerequisite: `python3`.**

> **Read this first, in your Python snapshot:** `tutorial/datastructures.txt`
> (*"Dictionaries"*, and the list-`sort` notes) and `tutorial/inputoutput.txt`
> (*"Fancier Output Formatting"*) if you extend it to print a report.

The capstone. Two functions, one specification, and the specification is a test
file: `test_freq.py`. **Read it first and read all of it** — this is the closest
this track gets to real work, where the requirements arrive as something
executable and your job is to satisfy them exactly rather than approximately.

- `word_counts(text)` → `{word: count}`, lower-cased, split on anything that is
  not a letter.
- `top_n(counts, n)` → the `n` most common `(word, count)` pairs, **ties broken
  alphabetically**.

That last clause is the one worth slowing down for. "Most common" is not a total
order — two words can tie — and a sort that does not say what happens then gives
you a result that is correct on your machine and different on someone else's.
The spec says what happens; make your code say it too.

```sh
# in the jichi checkout (repository root)
cd docs/assignments/84-python-capstone && python3 -m unittest test_freq
```

**Then write `DESIGN.md`** in that directory: one line naming the shape of your
solution — the dict, the `Counter`, the sort key. This is not paperwork. Being
able to say what you built in one sentence is how you find out whether you built
one thing or three, and it is the habit the whole process track is about.

**Do not edit `test_freq.py`.** It is the spec, and a spec you may edit is a
wish.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/84-python-capstone.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/84-python-capstone.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/84-python-capstone.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
