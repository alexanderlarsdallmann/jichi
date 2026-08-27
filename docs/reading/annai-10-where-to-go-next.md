# Annai 10 — Where to go next

*[案内（あんない）*Annai* — the guided tour](ANNAI.md) · chapter 10 of 10*

## What you can now do

Nine chapters ago, jichi was a black box that edited your files. Now you
can: follow a turn end to end (2, 3), read a tool and its guards (4, 5),
trace a byte from socket to screen (6), say who owns any allocation and
for how long (7), predict what a long session forgets (8), and — the
durable one — read a *test suite* as a map of where a project has been
burned (9). That is not "knows the codebase"; it is **oriented**, which
is the only thing a guide can honestly promise. The rest is mileage.

## Three roads, by appetite

**1. Depth: the same code, argued.** The
[深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md) revisits what
you just toured as *decisions with alternatives*: why one loop and not an
event system, why arenas and not ownership conventions, why C89 at all —
and the migration tracks' closing question, what this codebase would
gain and lose in Zig or C++. Read it when "how does it work?" has turned
into "would I have built it this way?".

**2. Breadth: the craft, graded.** If you arrived here without the
curriculum, you have been reading its textbook: [CURRICULUM.md](../CURRICULUM.md)
is the map, and the assignments this guide kept pointing at
([set D](../assignments/INDEX.md) for chapter 7's bug classes, 09/14/22
for chapter 9's honesty toolkit) are where reading becomes reflex.
The reading method itself — contract first, tests as a trust map,
convict mechanically — is [READING_OPEN_SOURCE.md](../READING_OPEN_SOURCE.md),
and its graded floor (assignment 24) hands you a foreign library with
one lie in it.

**3. Contribution: a first real change, with the agent.** The honest
finale is to *use* the workflow this program embodies, on this program:

```
1. Pick an itch: a doctor check you wish existed, an unclear error
   text, a missing test for something you read this week.
2. Read first (chapter 5's guards set the example): the file, its
   header, its tests. Have jichi survey with you -- /map, @file,
   codebase_search -- but hold chapter 4's rule: reasoning is
   auditable text, not testimony.
3. Write the failing test BEFORE the fix (chapter 9, rule 1).
4. Make the smallest change that turns it green; `make check-target`.
5. Read CONTRIBUTING.md and shape the diff to the house rules --
   C89-pedantic-clean, declarations at block top, the commit message
   explaining WHY.
```

If the change touches what a chapter covered, re-read that chapter's
*Where this bit us* first — most first patches step exactly where the
scars are, because the scars mark the load-bearing walls.

## The habits worth exporting

Leaving this codebase, four of its practices travel to any project you
will ever work on, with or without an agent:

- **Write the decision down where the code is** — this repository's
  comments cite incidents and measurements; that is why a guide like
  this could exist at all.
- **A test must be seen to fail** before it counts as evidence.
- **Prefer a lint to an audit** — automate the re-check, not the
  finding.
- **Keep the ledger of being wrong** (`docs/ANECDOTES.md`,
  `docs/TEST_INTEGRITY.md`) — the cheapest senior engineer you will
  ever hire is your own documented history.

And one for the agent era specifically, the thread of every AI sidebar
in this guide: **the model proposes text; the program, and you, decide
what text gets to do.** Systems that make that boundary legible — in
code, in permissions, in events you can read — are systems you can
trust with more. That is the whole design, and now you have read it.

*— end of the 案内（あんない）Annai. The
[深掘り（ふかぼり）*Fukabori*](FUKABORI.md) picks up where orientation
ends and judgment begins.*
