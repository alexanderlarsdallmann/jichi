# Module 5 — Write the check yourself

*Stage 2 (破（は） Ha — Break the form) · ~4–6 h · assignment:
[`09-grade-the-grader`](../assignments/09-grade-the-grader.md) (4 pt — the
**stage's required task**) · map: [CURRICULUM.md](../CURRICULUM.md)*

Stage 1 taught you to trust the forms; Stage 2 begins by handing you the most
dangerous one. Every grade so far came from a `verify` command someone else
wrote. This module puts you in that seat — because a check that passes wrong
code is worse than no check: it *manufactures confidence*, and everything you
will ever delegate to an agent rests on checks.

## The work

**1. Meet the hollow gate.** Read [ANECDOTES.md](../ANECDOTES.md) #17: this
project once had a "green" verify that passed because `cat` succeeded — whole
runs concluded "everything works" while testing nothing. Notice the shape of
the failure: nobody wrote a bad test on purpose; a gate quietly *became*
hollow. The counter-discipline is **two-sidedness**: a check earns trust only
when you have seen it *reject* something wrong, not just accept something
right. (Module 3's make-it-fail-first was this discipline in miniature; now
it is the whole subject.)

**2. Think in input classes, not test cases.** The assignment gives you four
plausible implementations of a three-line contract, one correct. You cannot
tell them apart by staring — each is wrong in a different *class* of input.
Enumerating classes (orderings, ties, boundaries, signs) is the design step;
the test cases fall out mechanically. An agent is a good sparring partner
here: ask it "what input classes does this contract have?" — then check its
list against yours, because *your* blind spot is the one that matters.

**3. Grade the grader.** Work
[`09-grade-the-grader`](../assignments/09-grade-the-grader.md). Your
deliverable is a checker; the runner grades its **discrimination** — it must
accept the one correct candidate and reject the three subtly wrong ones. A
happy-path checker accepts all four and scores zero. This is the same bar
jichi holds its own bench graders to (`tests/bench/check_graders.py` proves
every corpus grader two-sided), and the same bar this curriculum's own
assignments are held to (`tests/e2e/curriculum_graders.py`). You are not
doing a classroom exercise; you are doing the project's real discipline on a
small target.

**4. Turn it on your past work.** Pick any Stage-1 task and read its
`verify` line with new eyes: what wrong solution *would it accept*? (Try to
write one!) If you find a real hole, you have outgrown the form — which is
what this stage is for. Write it in your record.

**Further reading, once your check passes.** A check that passes tells you its
*universe* is clean — not that the universe is the one you meant.
[TESTING_TUTORIAL.md §6](../TESTING_TUTORIAL.md) is the skill of auditing that,
with four worked examples from this repository where a lint was green for months
while covering less than its own header claimed — including two graded assignment
traps that **passed** a forbidden solution on any machine without GNU `grep`. It
also collects the three ways a *measurement* lied in the same week, which is the
half of this module nobody teaches.

## The gate

The meta-assignment `passed` — this one is **required** for the Stage-2 gate,
not just points toward it. A hollow checker cannot be averaged away.

## Reflection

*(toward [JOURNEY.md](../JOURNEY.md)'s Ha marker)* — Breaking the form means
being able to *defend* the break. After this module you should be able to
argue with a `verify` command — and win, with a counterexample.

> **If you are stuck alone:** the ladder's rungs walk the class enumeration.
> If your checker rejects the correct candidate, your test `main` itself has
> a bug — debug it like Module 4 taught (isolate: run it against a median
> you hand-compute). Compile errors: paste them into the session; that is
> the workflow.

---

[◀ Prev](04-debugging-as-science.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](06-design-first.md)
