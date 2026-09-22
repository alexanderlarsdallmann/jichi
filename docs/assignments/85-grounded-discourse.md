---
title: Grounded discourse — make the discussion resolve
audience: student
phase: review
stage: extras
difficulty: advanced
points: 4
verify: "sh docs/assignments/85-grounded-discourse/test.sh"
hints:
  - "Pick a claim the project makes about ITSELF, from `CLAUDE.md` or a docs page. A rule is better than an opinion, because a rule is either obeyed or it is not."
  - "Ask the agent for the grounds SEPARATELY from the claim. \"Where in the tree does that happen?\" is a different question from \"does that happen?\", and a model will answer the second confidently having no answer to the first."
  - "The strong objection is not \"maybe the claim is false\". It is \"your evidence does not distinguish the case where you are right from the case where your check read nothing\". Attack the warrant, not the claim."
---

This is the discussion rung of the review track. [Task 74](74-read-the-turn.md)
graded a **reading** you performed on the source; this one grades a
**conversation** you had with the agent about a claim, and whether what came
out of it resolves to anything.

The instrument is [`GROUNDED_DISCOURSE.md`](../GROUNDED_DISCOURSE.md): five
moves — claim, grounds that resolve, warrant, counter-argument, revision — in
that order, because each one depends on the one before it.

## Why this is a task and not advice

A model does not usually fail by being wrong. It fails by producing an answer
with the **shape** of a grounded one: a confident reason, a plausible file name,
a function that sounds like it belongs to that file. Every part is the right
kind of thing, and nothing in it resolves.

You cannot fix that by reading harder, because a fluent wrong answer reads
exactly like a fluent right one — that is what fluent means. You fix it by
making the conversation **produce a record whose citations something else can
follow**, and then looking only at what fails to follow. That is the same move
the rest of this curriculum makes: take the judgement down a tier, from
something you must remember to do into an artifact a script can check.

## The task

Pick **one claim this project makes about itself** — a rule in `CLAUDE.md`, an
invariant in a docs page, a property some lint says it holds. Discuss it with
jichi until you can say whether it is true, and write
`docs/assignments/85-grounded-discourse/DISCOURSE.md` with exactly these five
sections:

```
## 1. Claim
## 2. Grounds
## 3. Warrant
## 4. Counter-argument
## 5. Revision
```

**The rules the grader enforces**, each for a reason:

- **Section 4 must contain a line beginning `Objection:` and, after it, a line
  beginning `Reply:`.** In that order. An objection written after its rebuttal
  is a straw man with extra steps, and line numbers are the cheapest possible
  check for it.
- **At least four distinct `path/file.c:symbol` citations**, and each must
  resolve — the file exists and the symbol is in it. This is the whole point of
  the task, so the grader is strict about it.
- **At least one page cited by name**, so it is clear which claim you mean.
- **The Revision must not repeat the Claim word for word.** If the objection
  changed nothing, either it was not the strongest one or you did not mean it.

## Choosing well

A good claim is **refutable in one command**. "The config system is well
designed" cannot be argued to a conclusion; "no first-party source calls
`sprintf`" can be settled, and settling it teaches you something about the
tree. If your claim survives untouched, it was probably too weak to be worth
making.

A good objection attacks the **warrant**, not the claim. "Maybe there is a
counter-example somewhere" is a shrug. "Your evidence is a green check, and a
green check tells you about what the check *read* — did you confirm that is the
whole tree?" is an objection, and it is one this project has been caught by
more than once.

## What the grader cannot see

Whether your warrant is sound, whether your objection is the strongest one
available, and whether your revision is honest. Those stay yours. What a script
can refuse is the two failures that need no judgement: **a citation that does
not resolve, and a discussion that concluded nothing.**

Passing the floor does not make the discussion good. It makes an ungrounded one
visible, which is a smaller claim and the only one the structure supports.

## When you are done

Grade with `jichi grade docs/assignments/85-grounded-discourse.md`.

Then read [the worked example](85-grounded-discourse.solution.md) — **after**,
not before. Its subject is a real claim, its objection was actually raised and
actually measured, and its claim came out of the discussion narrower than it
went in. A worked example read first becomes the answer you copy.

---

**Running this task.** Work from the **project root** (the directory that holds `docs/`). Grade with `jichi grade docs/assignments/85-grounded-discourse.md` in a **terminal**, not inside jichi — leave the agent with `/exit` first, or use a second terminal. The discussion itself happens **inside jichi**; only the grading is a terminal command. Stuck? `jichi hint docs/assignments/85-grounded-discourse.md` (or `/hint`) gives one rung at a time — free, and recorded. No toolchain is needed: the grader reads your `DISCOURSE.md` and, from the checkout, the tree it cites.
