# Pseudocode — writing it, and turning it into real code — a tutorial

A learner's guide to pseudocode as a *thinking tool*: how to write it, when it
earns its place, and how to carry it down into a real programming language without
letting it become a second thing to maintain. The thesis: **pseudocode is for the
one algorithm you cannot hold in your head, written to be read once and thrown
away — not a language, not a deliverable, not documentation that outlives the
code it described.**

## 1. What pseudocode is for, and is not

It is for **the genuinely tricky part**: the loop with three interacting indices,
the state machine whose transitions you keep getting wrong, the recursion whose
base case is subtle. You write it to reason about control flow *before* the real
syntax gets in the way, and to have something a human can check line by line.

It is **not** for the easy 90% of code (which is clearer as the code itself), not
a spec you keep in sync forever, and not a way to avoid deciding how the thing
actually works. jichi's shipped convention says this in one hard rule (the
`design-doc` skill): *if the pseudocode is longer than the eventual code would be,
write the code instead.* Pseudocode that has grown past the code it describes has
stopped paying for itself.

## 2. How to write it

Four properties, from jichi's `design-doc` skill conventions:

- **Language-neutral.** No real syntax noise — no semicolons-because-C, no
  `self.` because Python. If a reader can tell which language you were secretly
  writing, you leaned too far in.
- **Real control flow.** The branches, loops, and early-exits must be the *actual*
  ones. This is the whole value: the shape of the logic, made visible. Vague
  hand-waving ("handle the error somehow") is where the bugs hide, so it is
  exactly what pseudocode must make concrete.
- **Invented names are fine.** `charge_the_card(order)` need not exist yet. Names
  are for the reader; you are describing intent, not calling an API.
- **Bounded.** A screen, not a chapter. If it does not fit, the algorithm wants
  decomposing — write pseudocode for each piece, or write the code.

A worked shape (finding the first duplicate in a stream):

```
seen = empty set
for each item in the stream:
    if item is in seen:
        return item          # first duplicate found
    add item to seen
return NONE                  # stream had no duplicate
```

Note what it commits to: a set (so membership is cheap), the *order* (first
duplicate, so the check precedes the insert), and the two exits. Those three
decisions are the reason to write it — they are exactly what a reviewer, human or
model, can now catch before a line of real code exists.

## 3. Turning it into real code

The move from pseudocode to a language is **decisions, not translation**. Each
neutral line forces a concrete choice:

- `empty set` → which set? A hash set (average O(1), needs hashable items), a
  sorted structure, a bit array (if items are small integers)? The pseudocode was
  silent on purpose; now you choose, and the choice is where performance lives.
- `for each item in the stream` → an iterator, an index loop, a callback? Does the
  stream fit in memory?
- `return item` → return a value, an optional, an error union? What does "no
  duplicate" look like in this language's type system (`NONE` above becomes
  `null` / `Option::None` / a sentinel / an out-parameter + bool)?

The discipline: **implement the pseudocode line by line, and where the real code
must diverge, notice why.** A divergence is information — it usually means the
pseudocode hid a decision (error handling, ownership, a resource that must be
freed) that the language forces you to face. That is the pseudocode doing its job:
surfacing the hard choices cheaply, before they are expensive.

Then the pseudocode's job is over. It does **not** become a comment block above
the function (the code says what the code does); it does not go in the design doc
verbatim (a pointer to the tricky spot suffices). Keeping it is how it rots.

## 4. Doing it with jichi

This is a strong use of the agent as a thinking partner:

```sh
jichi -p "Write language-neutral pseudocode for <the tricky algorithm>. Real
control flow, invented names, no language syntax. Then stop — do not implement yet."
# read it, correct the logic while it is cheap, then resume the SAME session:
jichi -c --auto -p "Implement that pseudocode in <language>. For each line, make the
concrete choice it left open, and flag any line where the real code must diverge."
```

The two-step is the point: you review the *logic* when it is a paragraph you can
hold in your head, not buried in real syntax. Two mechanics make it work — `-c`
resumes this directory's most recent session, so the second turn can actually see
the pseudocode from the first (without it, a second `jichi -p` is a fresh session
that has never heard of it), and `--auto` is what permits a file to be written at
all in a headless run. jichi's `design-doc` skill (shipped
in the `sdlc` scaffold pack, `jichi init sdlc`) carries these conventions for the
agent; this tutorial is the learner-facing half.

## 5. Extra curriculum — the reading track

In reading order:

1. [`assignments/10-design-before-code.md`](assignments/10-design-before-code.md)
   — the graded task that accepts pseudocode as one allowed design form; do it,
   then do it again writing the pseudocode first.
2. The `design-doc` skill (run `jichi init sdlc`, read
   `.jichi/skills/design-doc/SKILL.md`) — the shipped pseudocode conventions in
   their original form, and where pseudocode sits inside a full design document.
3. [SDLC.md](SDLC.md) — how design (pseudocode included) fits the phases between
   requirements and implementation.
4. [reading/ANNAI.md](reading/ANNAI.md) — the source-reading guide whose every
   chapter states the idea *as pseudocode* before showing the C; a worked corpus
   of the neutral-shape-first habit, applied to reading rather than writing.
5. Its sibling
   [UML_TUTORIAL.md](UML_TUTORIAL.md) — for logic whose *shape* is better drawn
   than written (a state machine, a call sequence), a diagram is pseudocode's
   visual cousin; pick by whether the hard part is the steps or the structure.

External concepts worth reading up on (search these; prefer primary sources):
*structured programming* (why sequence/selection/iteration are enough); *stepwise
refinement* (Wirth — pseudocode as a program you sharpen in passes); *literate
programming* (the opposite bet, that prose and code should live together — worth
knowing so you can decide against it deliberately); *invariants* and *loop
variants* (the properties a tricky loop must preserve, which good pseudocode makes
checkable).
