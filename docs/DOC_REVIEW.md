# Reviewing the documentation — the rubric, and how to run a pass

*The instrument, written down so the next pass does not have to reinvent it. It
exists because the two most valuable findings of the M392 review were **prose that
is internally coherent and simply untrue of the program** — the class no lint in
this repository can catch. Lints own the vocabularies (65 of them, see
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) §"Prefer a lint to an audit"); this page owns
what is left, which is everything a reader experiences.*

## 1. The audience is one person, and it is not you

Review for a **self-learner working alone**: no instructor, no colleague, possibly
limited prior programming experience, a Linux/macOS terminal, and **nobody to ask**.
That last clause does the work. It converts "this is a little unclear" into "this
ends the run", and it is the reason the M392 review's two hard stops were found on
the *first page* while three reviewers had walked past them before.

Every finding should be phrased against that reader: *what do they do next?* A
finding with no way forward is not a finding, it is a complaint — the same rule the
code follows for refusals (M342/M360).

## 2. The rubric — six questions per page

| | Question | The failure it catches |
|---|---|---|
| **What** | Is the thing defined before it is used? | Jargon on first use: "envelope", "fence", "posture", "role", "chokepoint", "green", "quantized", "TAP", "lint", "invariant" |
| **Where** | Which file, directory, config key, command's output? | "Set it in your config" — *which* config, at *which* path |
| **When** | When to use this, **and when it is the wrong tool**? | A page that only sells; the reader cannot tell if it applies to them |
| **How** | Copy-pasteable commands, with expected output so success is distinguishable from failure? | The command that exits 2; the sample output no build can produce |
| **By whom** | Who acts — the human, the agent, or the machinery? For safety features: who decides, who is protected, from what? | A propose-only agent whose document the reader waits for forever |
| **Decision** | Where something is recommended, is the choice stated with its **reason** and its **rejected alternative**? | Advice that cannot be evaluated, and rots invisibly when the reason changes |

Then three cross-cutting questions that produced the most valuable findings:

- **Is the safety net described as more universal than it is?** Every gap M392
  found leaned this way. Check the *conditions*: rollback needs a verifier; the
  checkpoint net excludes git-ignored files; `snapshots` is off under auto-lite.
- **Is this page reference or tutorial, and which does its reader need?** Most
  feature pages are written for someone who already knows, and are met by someone
  who does not.
- **What does the page mention but never explain?** That list is the register of
  what to write next, and it is worth more than any single fix.

## 3. How to run a pass

1. **Slice the corpus** so each reviewer reads a coherent journey, not a random
   set: the entry path *in order* (router → build → install → setup → first
   session → config → doctor); the curriculum and its tasks; the feature/reference
   pages; the newest material (most likely to be wrong, least likely to be read).
2. **Reviewers are read-only.** Say so explicitly, and say "return a report" —
   otherwise a capable reviewer starts fixing, and you lose the ability to verify.
3. **Ask for ranked findings with an anchor and a suggested fix**, capped (six per
   page). Say: *prefer ten concrete findings to forty vague ones*, and *say plainly
   where a page is already fine*. Both instructions materially improve the output.
4. **Ask what is already good, by name.** Without it, the next pass sands off the
   best writing in the corpus — and the M392 reviewers named things worth
   protecting that no author would have thought to defend.
5. **Verify every finding before acting on it.** Three of M392's reviewer claims
   needed correction; several others were *worse* than reported. Run the command,
   read the function, check the default. This is not distrust of the reviewer — it
   is the same rule the project applies to itself, and it is where the sharpest
   findings actually came from.
6. **Fix the blockers; register the rest.** A review generates more work than one
   pass can hold. Fix every hard stop, every false safety claim, every broken
   command, and every teaching defect that makes a faithful reader fail. Then write
   the remainder into [DEFERRED.md](DEFERRED.md) with reasons, and the full report
   into `docs/analysis/`. Pretending to fix it all is the one outcome worse than
   deferring honestly.

## 4. What is worth turning into a lint afterwards, and what is not

The M392 review produced two lints and one refusal, which is the right ratio:

- **Command *shapes*** are lintable — the argument parser refuses some forms
  deterministically, needing no model or network
  ([doc_commands_lint.sh](../tests/smoke/doc_commands_lint.sh), M394).
- **Documented *defaults*** are lintable — the parser's default is in the source
  next to the comment claiming it
  ([config_defaults_lint.sh](../tests/smoke/config_defaults_lint.sh), M393; and its
  own stated exclusion was where the two worst findings lived).
- **Prose is not lintable.** A fabricated `doctor` sample and a missing
  git-ignore caveat are coherent, well-written and false. Attempting a prose lint
  would produce a checker that passes exactly the defects that matter. This is why
  the pass repeats with fresh eyes instead.

## 5. When to run it again

- Before the **public release** — the first external reader gets no benefit of the
  doubt.
- After a **wave of new pages** (M386 added six tutorials; seven of their commands
  were broken, and the review was three days later).
- After any milestone that **changes a default, a refusal, or a safety condition**
  — those are the changes whose documentation silently becomes false. M375 added a
  refusal on a Tuesday; by Thursday the page telling that bug's story was still
  teaching the bug.

## 6. The worked example

[`analysis/2026-08-12-docs-review.md`](analysis/2026-08-12-docs-review.md) is a
complete pass: four reviewers, 30 pages, ~35 findings fixed, 21 register items and
six structural findings deferred with reasons, and the two findings that argued for
doing this again rather than automating it. Read it before running your own — not
for its conclusions, but for the shape of an honest report.

## 7. Writing so the review passes — the house rules

The rubric in §2 says what to check. These rules say how to write so the check
passes. They come from the docs-structure plan
([plans/2026-09-25-docs-structure-and-writing.md](plans/2026-09-25-docs-structure-and-writing.md)),
decided by the operator on 2026-09-25. Most are matters of review; one is a lint.

- **A table is for looking something up.** One fact per cell, and short cells: a
  row fits on one line of a wide screen, **about 200 characters**. When a row needs
  a paragraph, the paragraph goes under a heading below the table (or on its own
  page) and the row links to it, so a reader can scan the table and read one section
  without scrolling a cell sideways. A register (`DECISIONS.md`, `DEFERRED.md`) is
  one line per entry, linking to the entry's own section. **This one is a lint:**
  [`tests/smoke/docs_table_rows_lint.sh`](../tests/smoke/docs_table_rows_lint.sh)
  refuses a new table row over 200 characters in a top-level `docs/*.md` page. It
  carries a per-page floor of today's counts, a ceiling that only falls, so the
  existing long rows are grandfathered and come down as pages are revised; the two
  registers keep large floors until they are rewritten one-line-per-entry.
- **A diagram must be readable where people read it** — at a laptop's width and a
  phone's. A flowchart with more than a handful of nodes, or nodes carrying
  sentences, becomes a table or a list. Mermaid stays where it shows what prose
  cannot (a sequence of messages, a state machine, a small data flow), and what it
  shows is also in the text, since a screen reader and a plain-text export cannot
  draw it. Each existing block is reviewed against this rule, not removed blindly.
- **Every diagram comes in up to three forms** (decided by the operator on
  2026-09-29, for a presentation that works on every platform):
  - **an "In words" paragraph** that says everything the diagram shows, for a
    screen reader and for anyone who cannot read the picture;
  - **a small text diagram**, in a ```` ```text ```` block. It looks the same in a
    git preview, a terminal, `less`, a PDF made with pandoc, and on a phone;
  - **a Mermaid block, only where it is meaningful and readable**: it adds
    something the text diagram does not (a sequence, a state machine), and it
    stays legible at a phone's width.

  **No Graphviz.** Mermaid is for the git preview only, so a page must be complete
  without it. [WORKBENCH.md](WORKBENCH.md) is the first page written this way, and
  its conventions are a way to meet the rule, not the rule. It keeps text diagrams
  to plain ASCII, at most 64 columns wide, and never writes two hyphens directly before a letter,
  because `docs_flags` reads that as a jichi option. It puts the text diagram first, then the words,
  then Mermaid, because outside a git preview Mermaid is only source code. Existing
  pages are brought to the rule as they are revised, not in one sweep.
- **Every page gives its context.** It opens with who it is for (and who should
  read something else), what you will be able to do after reading it, what you need
  first (with a link for each), and where it fits (the page before, the page after,
  the reference it relies on). Inside, terms are defined at first use or linked to
  [VOCABULARY.md](VOCABULARY.md), and a rule names the incident that made it, so a
  reader can tell a rule from a habit. **A ceiling lint since M946:**
  [`docs_opener_lint`](../tests/smoke/docs_opener_lint.sh) counts the top-level pages
  without an opener a pattern can find, and the count may only fall.
- **Honest, and says so.** Measured, expected and unknown are kept apart; a number
  names its source, its date and its build; the platform words keep their strict
  meanings ([PLATFORMS.md](PLATFORMS.md)). A correction stays visible next to the
  number it changed. Limits are stated where the reader meets them, not only in a
  register. **A lint for the install pages since M945:**
  [`install_verdict_lint`](../tests/smoke/install_verdict_lint.sh) holds each one's
  verdict to its PLATFORMS.md row.
- **References.** A technical page ends with **Read further**, each entry marked
  primary (a standard, a man page, an RFC, a paper) or secondary (a book, a
  tutorial), with a line on why to read it; the full entries live in
  [BIBLIOGRAPHY.md](BIBLIOGRAPHY.md). The same ceiling lint (M946) counts the pages
  without one.
- **One question per page.** A page that answers several questions for several
  readers is split, by task or by reader. The current state comes first; how it
  came to be — milestones, corrections, dead ends — goes in a section at the end or
  in the ROADMAP and analysis pages, linked from there.

**What a revised page gains** (added 2026-10-04, from the plan of 2026-10-03, step 7.5,
after the reading guides were found to have the best structure in the tree):

- **A page that explains a mechanism gives pseudocode before the C**, as Annai's
  "Idea" sections do: the shape in ten lines a reader can hold, then the real code.
  It points at the code as `file.c:function`, never by line number, so the pointer
  survives the next edit ([`reading_refs_lint`](../tests/smoke/reading_refs_lint.sh)
  holds the reading guides to this).
- **A page that teaches a command shows three things:** the command, what you
  should see, and what to do if you see something else.
  [install/debian-ubuntu.md](install/debian-ubuntu.md) is the model page. The
  command is run first, in exactly the published form; a headless run carries
  `< /dev/null` ([`docs_stdin_lint`](../tests/smoke/docs_stdin_lint.sh), M944),
  because without it a run under ssh or CI waits forever.
- **A design decision links its DECISIONS row**, which names what was rejected. A
  reader who disagrees can then see whether their alternative was weighed, and why
  it lost.
- **A recommendation says what it rests on: measured, expected or unknown.** A
  measured one names its source, date and build; an expected one says what would
  confirm it; an unknown one says so instead of guessing.
