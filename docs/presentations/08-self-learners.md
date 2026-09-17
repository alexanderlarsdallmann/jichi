---
marp: true
title: jichi for self-learners
theme: default
paginate: true
---

<!-- _class: lead -->

# Learning software development alone

### What an agent can honestly give you, and what it cannot

*Stamped 2026-09-17 (M648). Figures on these slides are bounds or dated stamps,
never live counts.*

---

# Who this is for

The person learning to build software **without a team**.

No colleague to review the change. No teacher to say *"that test does not test
what you think"*. No standup where somebody notices you have been stuck for
three days.

This deck is not about learning a language. It is about the part that is hard to
get alone: **finding out whether your work is any good.**

---

# Name the actual deficit

It is not information. The internet has the information, and so does the model.

What a self-learner lacks is a **feedback loop with teeth**:

| A team gives you | Alone you get |
|---|---|
| Review that disagrees with you | Agreement, from a model trained to be helpful |
| A test somebody else wrote | Tests you wrote against your own assumptions |
| *"Why did you do it that way?"* | No question at all |
| Someone who has seen this fail before | A search result without the scar tissue |

An agent that answers every question fluently makes three of those four **worse**,
not better.

---

# So the design question is not "can it help?"

It is: **how do you keep the help from replacing the learning?**

jichi's teaching layer answers with four mechanisms, and each is a *refusal* as
much as a feature:

1. **Grading you cannot argue with** — a script decides, not a conversation.
2. **Help as a dial you turn**, one notch at a time, on request.
3. **Trap cases** that teach you to distrust a green result.
4. **Real source to read**, with the project's own failures written down.

---

# 1. The curriculum, and why it is graded by `sh`

- **Twelve modules** in four 守破離 (*shu-ha-ri*) stages, **each stage severable** —
  you can stop after stage 1 and have learned a whole thing.
- **Over 80 graded tasks**, plus nine standalone language courses (Racket, Guile,
  Elixir, Haskell, Clojure; C, Zig, C++, Rust) and a **process track that needs no
  compiler at all**.
- Graded by **pure POSIX `sh`**. No Python, no test framework, no service.

`jichi assignments` prints your stages with points and status:

```
-- shu  (0/17 pts, 0/9 passed)
  00-hello.md                  implementation   1pt  -
  06-make-the-test-pass.md     testing          3pt  -    (+solution)
```

**Why `sh` matters:** the grader runs on the machine you have, offline, forever.
A curriculum that needs a service is a curriculum that expires.

---

# 2. Two-sided grading: the part most courses skip

Every grader must **provably reject the broken version** *and* **accept the
reference solution**.

> A grader that only ever says "pass" is not a grader. It is a congratulation.

This is the same rule the project applies to its own tests: *a test never
observed failing has never been observed working.* Over **70 trap cases** exist
to prove the graders themselves can fail — lazy checkers, half-fixes, hollow
gates.

**What you learn from it:** that "the tests pass" is a claim with a universe
attached, and the universe is usually smaller than you assumed.

---

# 3. Help is a dial, not a faucet

- The tutor is instructed to **never write the solution**.
- Hints come as a **graded ladder**, revealed one rung at a time, and **only when
  you ask** — asking is a deliberate act, and the run records that you did.
- The tutor **cannot spend your ladder for you** (M617). It was able to once;
  that was treated as a defect and fixed.

**The point of the ladder is the pause before you pull it.** That pause is where
the learning is, and it is exactly what a fluent answer removes.

---

# 4. Read real code, not a toy

Four reading guides over **this** codebase — a real program, ~108,000 lines of
C89, with its defects still in the history:

| Guide | What it is |
|---|---|
| 案内 *Annai* | the guided tour |
| 深掘り *Fukabori* | one design decision per chapter |
| 追跡 *Tsuiseki* | replayed real traces |
| 記録 *Kiroku* | the record itself |

Plus **reading for review**: abstraction → concrete, control flow, data flow, and
*did it actually run that way* — checked against a recorded trace, not against
your assumption.

---

# The textbook is the project's own failures

Most projects publish the polished result. This one publishes the record, on
purpose:

- **`ANECDOTES.md`** — debugging war stories: symptom → dead ends → root cause →
  lesson. Including the ones that are embarrassing.
- **`DECISIONS.md`** — every decision **with the alternatives that were
  rejected**. If nothing was rejected, it was not a decision.
- **`analysis/`** — dated measurements, each stating what was *not* measured.

**A worked example of being wrong, repeatedly, in public, is the thing a
self-learner cannot get anywhere else.** A tutorial shows you the path. This
shows you the wrong turns and what they cost.

---

# What jichi will not do for you

Said plainly, because a tool that overpromises to a beginner does real harm:

- **It will not tell you your design is good.** It will grade structure and
  traceability; *"quality is your judgment; the floor is what a script can check."*
- **It will not make you a reviewer.** Reading for review is a discipline you
  practise; the grader only checks that your cited anchors resolve in the real tree.
- **It will not notice you are stuck.** A loop detector catches a *machine* loop,
  not a human one.
- **It will not replace other people.** It narrows the gap. It does not close it.

---

# Day one, honestly

1. `jichi init assignments` — the pack lands in your project.
2. `jichi assignments` — see the stages, points, and what you have passed.
3. Pick **00-hello**. Do it without asking for a hint. Then look at the hint
   ladder anyway and see how far down your answer was.
4. When a task ships a reference solution, **write yours first**, then diff.
   The diff is the lesson; reading the solution first is not.

**Do not start with the agent writing code for you.** Start with it grading code
you wrote. The order is the whole point.

---

# What this deck cannot tell you

- **No learning-outcome study has been run.** Nothing here measures whether
  people taught this way learn better. The mechanisms are described because they
  are *built and tested*, not because they are proven pedagogy.
- **The curriculum has one author**, working with one agent. It has not been
  taught to a cohort.
- **"Graded" means a script agreed with you**, which is a floor and not a
  ceiling. Every grader says so in its own words.
- The honest summary: this is a **feedback loop you can run alone at 2 a.m.**,
  not a substitute for a reviewer who disagrees with you.
