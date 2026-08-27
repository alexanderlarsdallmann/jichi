# Module 3 — Tests are the truth

*Stage 1 (守（しゅ） Shu) · ~4–5 h · assignments:
[`06-make-the-test-pass`](../assignments/06-make-the-test-pass.md) (3 pt,
with a worked [solution](../assignments/06-make-the-test-pass.solution.md)),
[`07-write-the-test-first`](../assignments/07-write-the-test-first.md) (3 pt)
· map: [CURRICULUM.md](../CURRICULUM.md)*

A check passes or it does not, like the sun rising
([JOURNEY.md](../JOURNEY.md)). This module makes tests your first
truth-teller: reading failures as *information*, and writing a failing test
before the fix so the fix has something to prove itself against.

## The work

**1. Read the failure, not the exit code.** Run assignment 06's tests and
look at the failing line before doing anything else:

```
not ok 2 - max of negatives: got 0, want -2
```

Three facts in one line: which function, which input class, and a wrong
answer that is not even in the input — a strong hint someone *initialized*
something. When the agent runs tests for you (`run_tests`, or the shell), the
parsed failure summary is what it reasons from ([TESTING.md](../TESTING.md));
learn to read it yourself so you can tell when the agent read it wrong.

**2. The fix-forward loop, supervised.** Drive the loop by hand this time:
run → read → hypothesize → smallest fix → run again. Let the agent propose
the fix, but *you* own the hypothesis. If its first patch doesn't make the
test pass, that is information too — feed the new failure back rather than
piling on a second guess.

**3. Make it fail first.** Assignment 07 inverts the order: the bug is known
to exist, and your first deliverable is a **test that fails because of it**.
A test you have never seen fail proves nothing — it might be testing the
wrong thing, or nothing (the hollow-gate disease; Stage 2's module 5 hunts it
for real). Watch your test fail *for the right reason*, then fix, then watch
it pass. The runner refuses a fix without a test and re-probes the fix
itself, but the moment that matters — seeing red before green — only you can
enforce.

**4. Compare against the worked solution.** After 06 passes, read
[`06-make-the-test-pass.solution.md`](../assignments/06-make-the-test-pass.solution.md)
— the only assignment in set A that ships one. The checklist at its end
("did your diff touch only the broken function?") is the habit of
self-review; Stage 2 mechanizes it.

## The assignments

| Spec | Practices | Pts |
|---|---|---|
| [`06-make-the-test-pass`](../assignments/06-make-the-test-pass.md) | failure reading; the fix-forward loop | 3 |
| [`07-write-the-test-first`](../assignments/07-write-the-test-first.md) | red before green; tests as proof | 3 |

## The gate

Both rows `passed`, and — honor clause again — your 07 test failed in front
of you before it passed.

## Reflection

Learn to love the simplicity of pass/fail before you meet the mysteries
([JOURNEY.md](../JOURNEY.md)): the deep bugs of Module 4 and the lying gates
of Stage 2 are only survivable from this footing.

> **If you are stuck alone:** `/hint` — 06's ladder walks the exact
> diagnosis, 07's names the input that breaks. Compile errors in your own
> test: paste them into the session and ask; that *is* the workflow. The
> worked solution for 06 is the escalation of last resort — reading it before
> solving converts knowledge into debt ([JOURNEY.md](../JOURNEY.md)).

---

[◀ Prev](02-the-smallest-change.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](04-debugging-as-science.md)
