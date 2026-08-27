# Writing use cases — a tutorial

A learner's guide to the use case: a precise, testable description of one goal of
one actor, written so that an acceptance test could be transcribed from it step by
step. The thesis: **a use case is done when someone could turn it into a passing
test without asking you a single question.** Everything vague in a use case is a
decision you deferred onto whoever implements it — usually the agent, usually
wrongly.

This tutorial is explicitly *not* about user stories; §6 draws the line, and user
stories get their own tutorial.

## 1. What a use case is

One use case = **one goal of one actor**. "Customer places an order." "Admin
revokes a key." "Scheduler retries a failed job." Not "manage orders" (that is
several goals) and not "the system processes data" (no actor, no goal).

The shape jichi teaches (the `use-case-writing` skill, and the graded task
[`assignments/68-process-use-cases.md`](assignments/68-process-use-cases.md)):

- **Actor** — who wants this (a person, another system, a scheduled trigger).
  An **actor** is whoever or whatever starts the interaction.
- **Goal** — what they want to be true when it is done.
- **Trigger** — the event that starts it. Often the same fact as a precondition
  stated as an event ("the nightly job fires", "the customer submits the cart"),
  and worth writing separately because it is what a test will simulate.
- **Preconditions** — what must already hold for this to start.
- **Main flow** — numbered steps, actor ↔ system, in the actor's vocabulary.
- **Extensions** — for each step that can fail, a numbered branch (3a, 3b…)
  naming *who notices* and *what they can do*.
- **Postconditions** — what is true afterward (and, if it failed, what is *not*).

## 2. The main flow: the actor's vocabulary, not the UI's

Write the steps in the language of the goal, not the screen or the code:

```
Main flow — UC-3: Customer places an order
  1. Customer submits a cart of items.
  2. System confirms every item is in stock.
  3. System charges the customer's payment method for the total.
  4. System reserves the items and records the order.
  5. System returns a confirmation with an order number.
```

No "clicks the button", no "POSTs to /orders", no "sets order.status = PAID". Those
are implementation choices; a use case that names them has quietly decided the
design and closed off alternatives. The vocabulary test: could this flow survive a
complete rewrite of the UI and the backend? If not, it is describing the mechanism,
not the goal.

## 3. Extensions are where the real work is

The main flow is the easy, happy path. The value of a use case is the
**extensions** — the numbered failure branches — because that is where software
actually lives and where an agent, left to guess, guesses wrong:

```
Extensions
  2a. An item is out of stock:
      System tells the Customer which items, and offers to remove them or wait.
  3a. The payment is declined:
      System tells the Customer, keeps the cart, and does not reserve anything.
  4a. Reservation fails after a successful charge:
      System refunds the charge and reports the failure — the invariant is that a
      charged customer always has either the goods reserved or the money back.
```

Each extension names **who notices** (the Customer, the System) and **what they
can do** next. `4a` above is the kind of branch that separates a real use case
from a wish: it states an *invariant* (never charged-without-goods-or-refund) that
the implementation must preserve, and that a test can check directly.

## 4. The done test: could you transcribe an acceptance test?

A use case is finished when an acceptance test could be written from it
mechanically — each main-flow step and each extension becomes an assertion. If a
step is too vague to test ("system handles the error appropriately"), it is not
done: *how* it handles the error is the decision the use case exists to make.
This is the same discipline as [testing](TESTING_TUTORIAL.md) — behaviour stated
precisely enough to check — applied one phase earlier, before any code exists.

Number them **UC-1, UC-2…**, and cross-reference with requirements: a requirement
names the use cases that satisfy it, and each use case names the requirement it
serves. That traceability is what lets you later prove nothing was dropped.

## 5. From use case to design

A use case feeds the next phase directly:

- Its **main flow** becomes a [`sequenceDiagram`](UML_TUTORIAL.md) — one per use
  case is a good rule; the actor-↔-system steps *are* the messages.
- Its **nouns** (Customer, Order, item, payment) are candidate entities for the
  [domain model](DOMAIN_MODELLING_TUTORIAL.md).
- Its **extensions** become the error paths the design must handle and the tests
  must cover.

## 6. Use cases vs. user stories — the deliberate distinction

They are not the same tool, and conflating them loses what each is good at:

| | Use case | User story |
|---|---|---|
| Form | Actor + goal + numbered flow + extensions | "As a ⟨role⟩, I want ⟨goal⟩, so that ⟨benefit⟩" |
| Length | A page; the flow and every failure branch | A sentence; the detail lives in conversation + acceptance criteria |
| Answers | *How* the interaction goes, step by step, including failure | *Who* wants *what* and *why* — a placeholder for a conversation |
| Best for | Complex interactions where the failure modes matter | Prioritizing and planning; lightweight, negotiable scope |

A user story is a promise to have a conversation; a use case is (much of) that
conversation, written down. This tutorial teaches use cases; the user-story
tutorial is separate, precisely because treating one as the other — a use case as
if a sentence sufficed, or a user story as if it were a full flow — is the common
mistake.

## 7. Doing it with jichi

```sh
jichi init sdlc                 # ships the use-case-writing skill + /usecases command
jichi -p "/usecases"           # the requirements-analyst drafts use cases for the project
```

The `/usecases` command runs the read-only `requirements-analyst` persona in the
shape of the `use-case-writing` skill (the command file is
`.jichi/commands/usecases.md` — read it, it is the prompt you are running). Two
things to expect: it reads `docs/REQUIREMENTS.md`, so run
`jichi -p "/requirements <your goal>"` first, and because the profile is
**read-only** it *prints* the use cases rather than writing `docs/USE_CASES.md` —
redirect or copy them yourself. This tutorial is that skill's learner-facing half. Task
68 grades the structural floor (actors, triggers, failure branches present); the
judgment — whether the flow is in the actor's vocabulary and the extensions name a
real remedy — stays yours.

## 8. Extra curriculum — the reading track

In reading order:

   > **If you do the graded task, label your lines with the grader's words.**
   > Task 68's check is a structural floor: it counts `##` sections and looks for
   > the literal words *actor*, *trigger*, and a failure word
   > (*failure*/*alternate*/*error*) at least three times each. So write
   > **Trigger:** and **Failure:** explicitly, even where you would rather say
   > *Precondition* and *Extension* — the grader cannot read a synonym, and it
   > says so about itself.

1. [`assignments/68-process-use-cases.md`](assignments/68-process-use-cases.md) —
   the graded task; write a use case, run `jichi grade` on it.
2. The `use-case-writing` skill (run `jichi init sdlc`, read
   `.jichi/skills/use-case-writing/SKILL.md`) — the convention in its original
   form.
3. [`assignments/67-process-requirements.md`](assignments/67-process-requirements.md)
   — requirements, which use cases trace to and from.
4. [SDLC.md](SDLC.md) — where use cases sit in the phases, and the honest note
   that they are *documents* (judgment), not mechanically gateable like code.
5. [UML_TUTORIAL.md](UML_TUTORIAL.md) §3 — turning a use case's main flow into a
   sequence diagram.

External concepts worth reading up on (search these; prefer primary sources):
*Alistair Cockburn, "Writing Effective Use Cases"* (the canonical treatment, and
the source of the goal-levels and extension-numbering conventions); *acceptance
testing* and *acceptance-test-driven development* (the use case as the test's
source); *user stories* and *the "3 Cs" (Card, Conversation, Confirmation)* — so
you understand the tool this tutorial is deliberately *not* teaching, and can
choose between them on purpose.
