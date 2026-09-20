# A tutorial on interface design — including the ones made of text

*Plan, 2026-09-19. Written for the operator's ask: a UI/UX tutorial covering TUI,
CLI, curses, GUI, desktop and web — **and treating tutorial writing and other
texts as interfaces too**: text, illustrations, step-by-step guidance, glossary,
references, index, and a map of the text for navigation.*

## 0. The thesis, which is the operator's and is already in this tree

> **You are here. This is what you can do here.**
>
> — Alexander-Lars Dallmann

That is the property every surface below is judged against. It is not a metaphor
borrowed for the occasion — jichi already practises it, and the tutorial's best
worked examples are things a learner can run in the next thirty seconds:

| Surface | The orientation it gives |
|---|---|
| `jichi docs` | the documentation sources configured, by name and path |
| `jichi assignments` | the stages, what is earned, what is available, `--stage` to narrow (M626 made this an *orientation* rather than a list) |
| `jichi describe` | the program's own interface contract, machine-readable |
| `jichi doctor` | what is wrong, what is merely warned about, and what to type next |
| `docs/README.md` | every page in the tree, one line each |

And the same property is what the failures in this project's own history were
missing: a cap that fired without saying so, a refusal that named no way forward,
a `note:` line that vanished. Each was a system that knew where the user was and
did not tell them.

## 1. Why texts belong in a UI tutorial rather than beside one

A reader of a document is a user of an interface with the same three questions —
*where am I, what can I do, what happens if I do it* — and the same failure modes.
The tutorial makes that concrete rather than assertive by giving documents the
components an interface has:

| Interface element | Its form in a document |
|---|---|
| Navigation / map | a table of contents that states the **shape** of the argument, not just its headings |
| Status: "you are here" | section numbering, breadcrumbs, a banner naming what this page assumes you have already read |
| Affordances | "run this", "skip to §5 if you already have a model" — the visible next actions |
| Error states | the *when it goes wrong* table, which is a document's error message |
| Vocabulary / tooltips | a glossary, and idioms explained rather than assumed (this project's `VOCABULARY.md` exists because a native-speaker reviewer needed *dogfooding* explained) |
| Index / search | an index, and predictable heading words so the reader's own search works |
| Illustrations | a diagram that answers a question the prose cannot, and **no others** |
| Undo / escape | an escape route at each step: what to do if this step fails |

**The test the tutorial teaches, for any of the six surfaces and for a page:**
can a person who arrives in the middle tell where they are, what is available, and
what it will cost? That is checkable by watching one person, which is the method
the tutorial actually recommends.

## 2. The surfaces, and what each is for

Ordered as the tutorial will order them — cheapest feedback loop first.

1. **CLI.** Arguments, exit codes, `--help`, machine-readable output, and the
   contract that a script depends on. jichi's `describe` is the worked example of
   an interface that *documents itself*; its `--output json` is the example of one
   surface serving a human and a program without pretending they are the same user.
2. **TUI and curses.** A screen you redraw, a terminal you do not own, and the
   things that break it: width, colour, locale, a resize, a pipe instead of a tty.
   The worked example is jichi's own TUI and, more usefully, its **accessibility
   path** — the row where a screen-reader user gets line-oriented output instead of
   a redrawn panel.
3. **Desktop GUI.** Where the platform's conventions belong to the platform, and
   why a cross-platform app that invents its own is worse than one that concedes.
4. **Web UI.** The surface with the most literature and the most fashion; the
   tutorial's job is to separate the parts that are perception (contrast,
   pointer targets, motion) from the parts that are taste.
5. **Documents**, per §1.
6. **Agent interfaces**, briefly and with a stated shelf life: what a tool
   description is *for*, and why an agent reading your CLI is a user with no
   patience for prose and no tolerance for an undocumented exit code.

## 3. The bibliography, and its scope

A section per surface, following `BIBLIOGRAPHY.md`'s existing discipline exactly:
**official sources first, every entry checked with the date it was checked, no
paywalled texts, and a stated gap rather than a thin section.** The candidate
shape, to be checked entry by entry before anything is written:

| Section | What belongs |
|---|---|
| The craft | the small number of books that are still argued with rather than cited politely |
| CLI | POSIX utility conventions, GNU's argument syntax, the CLI guidelines that exist as living documents |
| TUI / curses | the terminal's actual standards — terminfo, ECMA-48 — rather than a library's tutorial |
| Desktop | the platform human-interface guidelines, each as its vendor publishes it |
| Web | the accessibility standard first (WCAG), because it is the part that is measurable |
| Accessibility | as its own section, not a footnote to Web |
| Typography & information design | for the documents half |
| Research method | how to watch one person use a thing without coaching them |

**A constraint this project will hold itself to:** no entry goes in unread, and no
section is written thin to look complete. `BIBLIOGRAPHY.md` already defers six
languages in those words, and that honesty is the reason it is worth reading.

## 4. What a script can check, and what it cannot

**Can:** that a page has a map, a glossary link, escape routes at each step, and
that its internal anchors resolve — the existing `docs_index_lint`,
`self_learner_lint` and `reading_refs_lint` already do versions of this, and a
`docs-as-interface` lint is a small extension rather than a new instrument.

**Cannot:** whether the page is any good. The tutorial should say so, and hand
that judgement to the method in §2 — one person, one task, no coaching — which is
the only instrument that has ever answered it.

## 5. Risks, stated before the work

- **Fashion.** Most of this field dates in months. The mitigation is the same one
  `BIBLIOGRAPHY.md` uses for LLM literature: prefer the standards and the things
  that have been argued with for a decade, and say plainly where a
  recommendation is current practice rather than settled.
- **Breadth.** Six surfaces plus documents is a book, not a page. The tutorial
  ships **CLI and documents first** — the two this project can demonstrate from
  its own tree and gate with its own lints — and the rest follow as they earn
  worked examples.
- **Assertion.** A UI/UX page is easy to write and hard to make true. Every claim
  should either point at a surface in this tree the reader can run, or at a
  standard, or be marked as the author's judgement.

## 6. Open questions for the operator

- **Is the audience the self-learner, or the person writing jichi's own
  surfaces?** The tutorial can serve both, but the worked examples differ: a
  learner wants *build a small CLI well*, a maintainer wants *why `describe`
  exists*.
- **Should the documents half be a separate page?** It is the part this project
  can gate, and it is the part the curriculum's writing track needs. Keeping it
  inside the UI tutorial makes the thesis; splitting it makes it usable sooner.
