# Module 9 — The agent is sometimes wrong

*Stage 3 (離（り） Ri — Leave the form) · ~4–6 h · assignments:
[`14-the-hollow-gate`](../assignments/14-the-hollow-gate.md) (4 pt),
[`15-the-confident-misdiagnosis`](../assignments/15-the-confident-misdiagnosis.md)
(4 pt) · map: [CURRICULUM.md](../CURRICULUM.md)*

The signature module. Everything before it taught you to work *with* the
agent; this one teaches the skill nobody else teaches: recognizing when the
confident, fluent, well-formatted thing in front of you is **wrong** — and
producing the check that proves it. The grade is *detection*, both tasks,
mechanically.

## The work

**1. Read the source material first.** These traps are not hypothetical;
each is a war story from this project's own record, and the record is the
textbook ([ANECDOTES.md](../ANECDOTES.md)): **#17** — a push chained on
`cat ci.log &&` went out on a red gate, green because the log file *existed*;
**#19** — weeks of requests ended in an empty assistant turn, and every
instinct said "the model is weak" when the request was malformed; **#20** —
twice in one day, a self-test validated a *sibling* of the thing it claimed
to test; **#21** — a run burned 1.56M tokens and every obvious reading
blamed the model, but the cause was a tool default. Four shapes, one lesson:
*the convincing surface and the true state are different objects; only a
check connects them.*

**Three more, added 2026-08-16, and this time the broken instruments are the
*operator's*:** **#54** — a toolchain probe that found a version-manager shim it
could not run, so a grader reported the reference solution as a *wrong answer*
when the truth was *no usable toolchain*; **#55** — a supervisor that accused
jichi of mis-reporting, three separate ways, while jichi was right all three
times and the supervisor was reading a dead run's journal entry; **#56** — a
two-sided proof that tested the very fix it had just removed, because the
removal did not compile and the old binary was still there. Read them for the
*dead ends*: each was diagnosed wrongly two or three times first, and the wrong
diagnoses are more instructive than the fixes. The catalogue and the design
decisions that came out of them are in
[analysis/2026-08-16-instruments-that-lie.md](../analysis/2026-08-16-instruments-that-lie.md).

**2. The hollow gate, in your hands.** Work
[`14-the-hollow-gate`](../assignments/14-the-hollow-gate.md) — #17 made
playable. A green gate, a wrong program, and your job in the *detection
order*: catch it, repair the gate, watch the repaired gate go red, then fix
the code. The grader replays your gate against the original buggy code —
your gate must fail there, or you have rebuilt the disease with better
manners.

**3. The confident misdiagnosis.** Work
[`15-the-confident-misdiagnosis`](../assignments/15-the-confident-misdiagnosis.md)
— a fluent analysis whose one-line patch really does fix the reported
symptom, and whose theory of the bug is still wrong. Your verdict file is
graded structurally (claim / evidence / verdict); the fix is graded by tests
that cover what the patch quietly ignores. Run the agent's patch and watch
it half-work — a half-working fix is the most dangerous artifact in this
whole curriculum, because it *ends investigations*.

**4. Generalize the reflex.** The pattern under both tasks: when something
asserts "X is true" (a gate, a diagnosis, an `ok:true`, your own hunch), ask
*what would be different if it were false, and have I looked?* You built
every piece of this reflex already — two-sided checks (M5), isolation probes
(M4), journals as evidence (M8). This module is only the permission to aim
them at the agent itself. Do it routinely, not suspiciously: the agent that
drafted your fix in seconds deserves the same review a colleague's diff
gets — no more, and never less.

**Worked evidence, if you want the shape of it first.**
[追跡（ついせき）*Tsuiseki* chapter 4](../reading/tsuiseki-04-the-call-that-was-wrong.md)
records two runs of jichi that differ in one field of one tool call. One edits
the file; the other changes nothing and announces success. Their summary events
are **byte-identical apart from the sentence the model wrote** — same
`stop_reason`, same tool counts, same cost — and the run exits 0 with an empty
`stderr`. It is this module's thesis with artifacts behind it, and the chapter
ends with the four checks that would have caught it. Read it before
[`15-the-confident-misdiagnosis`](../assignments/15-the-confident-misdiagnosis.md),
not after.

**Measuring a reviewer, for real.** This module asks you to catch a confident
wrong answer. `examples/self-hosting/` is the project doing that to itself: a
**read-only** slice whose stated purpose is to find out *whether a given model is
a good enough reviewer of this codebase before you ever let it write*. Its
"What good looks like" section is the rubric — real `file:line` findings, MUST-FIX
separated from nice-to-have, a verdict that still points at `make ci` — and its
failure list is the one you will recognise: invented nitpicks, a missed `long
long`, a change declared "safe" without naming the gate. Run it read-only
(`examples/self-hosting/config.jichi-dev-local.json` needs no key, only a local
model server) and judge the reviewer, not the diff.

## The gate

Both trap tasks `passed`. There is no partial credit for detection — a trap
half-caught is a trap.

## Reflection

*(toward [JOURNEY.md](../JOURNEY.md)'s Ri marker)* — the tool is starting to
disappear; what remains is judgment. If these two tasks felt like Modules 4
and 5 wearing a costume, that is not an accident — that is arrival.

> **If you are stuck alone:** for 14, run the gate and the test binary
> back to back and stare at the two exit codes (`echo $?`). For 15, the
> ladder's first rung is the whole method: try the inputs the diagnosis
> never mentions. If you catch yourself trusting the diagnosis because it
> is well-written — good, *notice that*, and write the noticing into your
> record; that entry is worth more than the points.

---

[◀ Prev](08-bounded-autonomy.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](10-teach-it-teach-with-it.md)
