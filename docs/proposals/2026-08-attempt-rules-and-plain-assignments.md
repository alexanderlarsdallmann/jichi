# `attempt` and project rules · plain-language assignments (M309)

*Design note written before implementation, per the M299 craft rule. Decisions,
rejected alternatives, and the risks are stated here so a reviewer can disagree with
the reasoning and not merely the result.*

---

## Part 1 — `attempt` should not inject the project's rules file

### The problem, measured

`jichi attempt` on two curriculum tasks against a real HRZ model (M308):

| Task | Points | Result | Tokens |
|---|---|---|---|
| `00-hello` — write one line to one file | 1 | **FAIL** at `--budget-tokens 30k`; PASS at 120k | ~128k |
| `06-make-the-test-pass` | 3 | PASS | ~130k |

The one-point task cost the same as the three-point task. Of ~21k tokens sent per
model call, **70% was the system prompt**: the assignments live inside the jichi
repository, whose `CLAUDE.md` is 121 KB and is loaded as a project rules file on
every call (fitted to ~45% of the window by M73, so still ~12k tokens). Six model
calls to write one line.

**Why this is a defect and not just a cost.** The audience is the one least equipped
to diagnose it: a first-time self-learner follows a tutorial, gets `FAIL` on
assignment 00, and concludes they did the exercise wrong. The gate lies.

### Decision

**`attempt` does not inject the project's rules file (`AGENTS.md`/`CLAUDE.md`/
`instructions`) by default.** A `--with-rules` flag restores the old behaviour.

### Why it is safe

An assignment spec is self-contained and its **`verify` command is the grader**. The
host project's contributor guide is not graded, is not what the exercise teaches, and
in the curriculum's case describes a codebase the learner is not modifying. The thing
the exercise measures is unchanged.

### Alternatives rejected

- **Document it and change nothing.** Rejected: the trap stays, aimed at exactly the
  people who cannot see it. A documented trap is still a trap.
- **Trim the rules harder for `attempt`** (say a 2k cap). Rejected: an arbitrarily
  truncated contributor guide is *worse* than an absent one — neither complete nor
  absent, with a cut point that means nothing, and it still costs tokens for text the
  learner will not use.
- **A config key instead of a default change.** Rejected as the primary mechanism:
  the default is the entire problem. A key nobody sets fixes nobody's failure. (The
  flag exists as an *escape hatch*, which is a different job.)
- **Skip rules for every bounded/`--auto` run.** Rejected as too broad: a genuine
  autonomous run on a real project *should* honour that project's conventions. The
  narrow claim is about graded exercises, so the narrow change is the honest one.
- **Make the curriculum's own `CLAUDE.md` smaller.** Rejected as a fix (though it is
  a fine thing to do anyway): it repairs one repository and leaves the behaviour
  wrong everywhere else. Every user doing coursework inside a documented codebase has
  this problem.

### Risks, and what would catch them

- **An assignment that implicitly relied on the rules file would change behaviour.**
  Caught by the two-sided curriculum graders, which run in CI and must still fail on
  untouched fixtures and pass on reference solutions.
- **A silent prompt difference is its own trap.** Mitigated: the run states that
  rules were skipped, so an operator comparing an `attempt` with an interactive
  session is not left guessing.

### Not in scope

`assign` (which only prints a spec) and `grade` (no model call) are unaffected.
Interactive sessions, `-p`, and subagents keep loading rules exactly as before.

---

## Part 2 — Plain-language assignments

### The gap

Checked (M308): **no assignment is written in plain register.** The curriculum's
prose assumes a reader comfortable with dense technical English, which is the
audience the plain-language pages
([`PLAIN_LANGUAGE.md`](../PLAIN_LANGUAGE.md), [`Einfache Sprache`](../i18n/de/EINFACHE_SPRACHE.md))
exist to serve. Those readers currently have an on-ramp to *the tool* and none to
*the coursework*.

### Decision

Add a small set of **plain-register graded assignments**, mechanically graded like
every other, in a new `plain` difficulty tier. Same machinery, different prose.

### Design constraints

1. **Plain register, same rigour.** Short sentences, one idea each, no metaphor, no
   nested clauses, jargon explained on first use. The `verify` command is as strict
   as any other — plain language is not easier marking.
2. **Small, cheap tasks.** Given Part 1, an assignment for this audience must pass on
   a modest budget. One file, one observable change.
3. **Say what success looks like, in the task itself.** The M308 beginner review
   found missing success criteria in the docs; the same failure in an assignment is
   worse, because the learner is being graded.
4. **English canonical, German gated.** The curriculum's translation policy applies
   unchanged: English is canonical, translations wait for the operator's trigger. A
   confident bad translation of a *graded* task is worse than an absent one.

### Alternatives rejected

- **Rewrite existing assignments in plain language.** Rejected: it would either
  destroy prose that serves its current readers, or fork every task into two
  variants that then drift. Separate tasks, like the separate pages (M305).
- **A separate "plain" curriculum.** Rejected as premature: three tasks do not need
  their own map, and splitting the curriculum invites one half to rot.
- **Skip grading and make them tutorials.** Rejected: the point is that this audience
  gets the *same* mechanically-graded feedback as everyone else. An ungraded exercise
  for beginners only is a soft bigotry.

### The open question, and how it was answered

I left this for the operator: whether the plain tier should carry German versions
*before* the general translation trigger — the Einfache-Sprache audience is
specifically German, so this is the one place where the gated-translation policy is
arguably wrong.

**Answered yes** (operator, 2026-08-06). The reasoning that makes it a *narrow*
exception rather than a policy change: *Einfache Sprache* is not "German" the way a
translated reference page is German — it is a **defined German register**, and
[`EINFACHE_SPRACHE.md`](../i18n/de/EINFACHE_SPRACHE.md) is the *original* of the plain
pair (M305), with the English page as its sibling. Withholding the exercises from the
audience the register was written for, on a policy meant to protect *other* audiences
from bad translations, gets the policy backwards. Every other tier stays
English-canonical.

---

## Part 3 — The German plain editions (added 2026-08-06)

### What was built

`docs/i18n/de/assignments/p{1,2,3}-*.md`, in Einfache Sprache, and a lint.

### The decision that matters: the graded frontmatter is code, not prose

These are the **first translated documents in this repository that carry an executable
field**. `verify:` is a shell command — the grader itself. (Checked before writing
them: `grep -rln '^verify:' docs/i18n/` returned nothing, so there was no precedent to
follow.)

A translator working through a German page will reasonably localise every string on
it. Localising *that* string silently breaks grading — in the tier whose readers are
least able to tell a broken grader from their own mistake. That is the same failure
shape as the M309 rules-file cost: a gate that lies to the audience least equipped to
diagnose it.

So five frontmatter fields are copied **byte-for-byte** from the English original —
`verify`, `points`, `difficulty`, `phase`, `audience` — and
[`assignment_i18n_lint.sh`](../../tests/smoke/assignment_i18n_lint.sh) holds them
there. Title, body and hints are free prose; comparing those would defeat the purpose
of a translation.

*Shown to fail first:* localising the graded string in p3's `verify` **and** bumping
its `points` to 3 — the two edits a translator would most plausibly make — each
reported as a separate finding.

### Consequences that follow from the byte-match

- **One set of fixtures.** A byte-identical `verify` names the English fixture
  directories, so both editions grade `docs/assignments/pN-…/`. Fixtures duplicated
  under `i18n/` would have needed syncing, which is the drift the lint exists to
  prevent.
- **A German exercise contains English strings** (`I asked and it wrote this line`),
  and both editions say why: the sentence is text for the computer, not for the
  reader. This is honest, and it is a real cost — noted rather than hidden.
- **The curriculum total does not change** (77 stays 77): `docs_counts_lint` globs
  `docs/assignments/*.md`, so the German editions are not counted twice. They are the
  same three tasks, said in another language, not three more.
- **Either spec may be handed to `jichi grade`.** Verified two-sided through the German
  files themselves: FAIL on the untouched fixture, PASS on the reference solution.

### Alternatives rejected

- **Translate the `verify` command too, for consistency of the page.** Rejected — that
  is the defect this part exists to prevent. Consistency of *register* does not extend
  to code.
- **Give the German editions their own fixtures.** Rejected: two fixture sets for one
  exercise is a synchronisation job nobody will do, and the divergence would show up
  as a mysterious FAIL rather than as a diff.
- **A German-only `points` value** (e.g. worth less because the reader is a beginner).
  Rejected on the same ground as the plain tier's strict marking: a translation must
  not change what a task is worth.
- **Machine-translate the whole curriculum now that one tier is translated.** Rejected:
  the exception was argued from *this* audience being specifically German. Nothing
  about it generalises, and 77 confidently-mistranslated graded tasks would be a much
  worse artefact than 74 English ones.
