---
title: Write the failing test first (Python)
audience: student
phase: testing
stage: python
difficulty: core
points: 3
verify: "sh docs/assignments/82-python-test-first/test.sh"
hints:
  - "Read `config.py` and ask what happens to a line with no `=` in it. Then ask how you would ever find out, from the outside, that it happened."
  - "Write `test_read_config.py` before you change anything. Three tests: the good lines parse, blanks and `#` comments are skipped, and a malformed line raises `ValueError` whose message contains the line. Run it — the third one must FAIL. That failure is the proof the bug is real."
  - "The fix: test `if \"=\" not in line:` and `raise ValueError` naming the line, then split. Delete the `try`/`except:` entirely — a bare `except:` also catches `KeyboardInterrupt` and `SystemExit`, so it swallows your Ctrl-C along with the bug."
---

> **Prerequisite: `python3`.** The grader names the tool if it is missing.

> **Read this first, in your Python snapshot:** `tutorial/errors.txt` — the
> sections *"Handling Exceptions"* and *"Raising Exceptions"*. Note what the
> tutorial says about the bare `except:` clause, and why it warns about it.

`config.py` reads `key=value` lines into a dict, and it has a bug whose whole
character is that **it hides**. A line with no `=` is silently dropped, so a typo
in someone's config file becomes a missing setting reported a long way from where
the mistake was made — or, more often, never reported at all.

The behaviour it must have: a malformed line **raises `ValueError`**, and the
message contains the offending line. Blank lines and `#` comments are still
skipped, because those are not malformed — they are absent on purpose.

**Do it in this order, and the order is the assignment.**

1. Write `docs/assignments/82-python-test-first/test_read_config.py` with at
   least three tests, one of which is the malformed line.
2. **Run it and watch it fail.** A test you have never seen fail is a test you
   have not yet verified — it may be passing because it asserts nothing, or
   because it never reached the assertion. This step costs ten seconds and is
   the only thing that makes the next one meaningful.
3. Now fix `config.py`, and run the test again.

That is the whole discipline, and it is why this task is worth more points than
the last one: the fix is four lines, and the sequence is the skill.

```sh
# in the jichi checkout (repository root)
cd docs/assignments/82-python-test-first && python3 -m unittest test_read_config
```

The grader checks all four things it can honestly check: your test file exists,
runs, and passes; it has at least three assertions, because a hollow suite is not
proof; an **independent** probe confirms the bug is really fixed rather than
merely re-described; and the bare `except:` is gone.

```sh
# in the jichi checkout (repository root)
jichi grade docs/assignments/82-python-test-first.md
```

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/82-python-test-first.md` (or `/grade` in a `/assignment`-loaded session). Stuck? `jichi hint docs/assignments/82-python-test-first.md` (or `/hint`) gives one rung at a time — free, and recorded. Any toolchain prerequisite is noted above and in [INDEX.md](INDEX.md).
