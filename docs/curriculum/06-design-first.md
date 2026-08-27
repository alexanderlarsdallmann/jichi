# Module 6 — Design first

*Stage 2 (破（は） Ha) · ~4–5 h · assignment:
[`10-design-before-code`](../assignments/10-design-before-code.md) (3 pt) ·
map: [CURRICULUM.md](../CURRICULUM.md)*

An agent makes producing code nearly free — which makes producing the *wrong*
code nearly free too. The discipline that survives cheap code is deciding
**what to build before building it**: requirements you could test, decisions
with named alternatives, and a design a stranger could implement. This module
is prose, and it is graded honestly as prose.

## The work

**1. Design in plan mode.** Open a session, `/mode plan` — the agent can now
read anything and change nothing, which makes it a pure thinking partner. Give
it the assignment's one-paragraph prompt and *interrogate the problem*: whose
notes? how many? what breaks with two writers? Do not let it (or yourself)
propose a design until the requirements stop moving. The failure this module
targets is real and specific: implementation vocabulary appearing before the
problem is stated.

**2. Decisions need named alternatives.** For every design choice, write one
alternative you **rejected and why**. This is the module's core move: an
assumption is a decision you didn't notice making. If you cannot produce an
alternative, you haven't decided — you've defaulted. (The agent is useful
here in the adversary's seat: "argue for the design I rejected.")

**3. A design is input, not archaeology.** When you later implement from a
design doc, jichi takes it as a first-class input: `--design <file>` injects
it into the system prompt for the whole run
([DESIGN_INPUT.md](../DESIGN_INPUT.md)). Design docs that drive runs is the
workflow Stage 2's remaining modules assume — write this one as if a bounded
`--auto` run were the reader, because next module's reviewer and Module 8's
delegated run effectively are.

**4. Know what the floor can and cannot grade.** The assignment's `verify`
is an *artifact check*: sections, bullets, a fenced block, length. That is
the honest limit of mechanical grading for prose — a script cannot tell a
good design from a confident one. The **feedback layer** is `/check`
(rubric-keyed model review); judgment stays human. Passing the floor and
stopping there is passing only a third of the assignment.

## The gate

`10-design-before-code` `passed`, **and** `/check` run on your document at
least once with its feedback either applied or explicitly rebutted in the
doc (an `## Objections` note is fine — disagreeing with the reviewer, with
reasons, is Stage-2 behaviour).

## Reflection

You are practising the inversion this stage is named for: the tool stops
telling you what is correct, and you start telling *it* what correct means.

> **If you are stuck alone:** blank-page paralysis → rung 1 (start from the
> user, not the file format). Over-engineering → the non-goals bullet is
> where scope goes to be declined; write it early. And read the assignment's
> honesty note twice — the floor is structure; the design is on you.

---

[◀ Prev](05-write-the-check-yourself.md) · [▲ Curriculum map](../CURRICULUM.md) · [Next ▶](07-review-and-refactor.md)
