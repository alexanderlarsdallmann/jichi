# Plan: how jichi's documentation is organised, and how it is written

*A plan, 2026-09-25. **Draft for discussion: nothing is moved or rewritten until the operator
decides the points in §7.** It starts from the operator's findings of 2026-09-25 and from what the
tree measures today. It is written for whoever carries it out, person or agent, and for a reader
who wants to know why the documentation is shaped the way it is.*

---

## 0. The operator's findings

In the operator's words, 2026-09-25:

- `PLATFORMS.md` has grown and is difficult to parse.
- Its matrix, a Mermaid diagram, is unreadable on most screens and should be removed, with its
  content placed in a table.
- The other tables' rows carry far too much information. They should be cut down, and the full
  content should get its own headings and well-structured text.
- Some texts must be revised to support self-learners and junior developers who need more
  context.
- Installation on each platform, with its possible and its measured difficulties, belongs in its
  own Markdown file.
- In general, jichi's documentation must be honest, thorough and comprehensive, readable for
  self-learners and junior developers, and give context and references.

## 1. What the tree measures today

Measured on `master` at `62278d8f`. The commands are in §8, so the numbers can be taken again.

| Measure | Value |
|---|---|
| top-level pages in `docs/` | 170 |
| subdirectories of `docs/` | 14 |
| size of `docs/` | 18 MB |
| `PLATFORMS.md` | 127 KB, 958 lines |
| its longest table row | 8,180 characters |
| pages with a table row over 500 characters | 19 of 170 |
| such rows in `DECISIONS.md` / `DEFERRED.md` / `PLATFORMS.md` | 218 / 156 / 29 |
| Mermaid blocks | 104, in 47 pages |
| test and script files that parse `PLATFORMS.md` | 17 |
| distinct `docs/*.md` paths named in `src/` | 50, in 34 files |
| pages linking to `PLATFORMS.md` | 55, with 8 anchor links among them |
| relative `.md` links in the top-level pages | 2,258 |

What the numbers say:

- **The table problem is concentrated.** Three pages hold 403 of the over-long rows. They are the
  two registers, where a row is a whole argument, and `PLATFORMS.md`, where a row is a whole
  history. Elsewhere it is a handful of rows.
- **The diagram problem is not one diagram.** 104 Mermaid blocks exist. Some are small and read
  well, and some are as wide as the platform matrix. Each needs a look, not a blanket rule.
- **Moving a page is expensive.** Lints parse pages by path and by structure, and the binary prints
  paths in its messages (`doctor`, refusals). A page moved without its references breaks the gate,
  or it breaks a message a user reads.
- **Installation is spread over several pages.** `INSTALL.md` (25 KB), `BUILD.md` (27 KB),
  `PREPARE_AND_BUILD.md` (20 KB), `PLATFORM_TESTING.md` (17 KB), `PLATFORM_RETEST.md` (18 KB) and
  `LOW_MEMORY.md` (71 KB) all carry some of it, and the platform rows of `PLATFORMS.md` carry the
  rest as history.

## 2. How the documentation is written: proposed house rules

These extend [`DOC_REVIEW.md`](../DOC_REVIEW.md), which already names the reader, a self-learner
working alone with nobody to ask, and six questions to ask of every page. The review rubric says
what to check. These rules say how to write so that the check passes.

### 2.1 A table is for looking something up

- **One fact per cell, and short cells.** A proposal to argue about: a row fits on one line of a
  wide screen (about 200 characters), and a cell holds a value, a verdict, a date or a link, not a
  story.
- **The story goes under a heading.** When a row needs a paragraph, the paragraph gets its own
  heading below the table (or on its own page), and the row links to it. A reader can then scan the
  table and read one section without scrolling a cell sideways.
- **Registers keep one line per entry.** `DEFERRED.md` and `DECISIONS.md` rows become a one-line
  summary, a status and a link to the entry's own section. That is a large change and is phased
  (§6).

### 2.2 A diagram must be readable where people read

- **It must read at the width of a laptop and of a phone.** A flowchart with more than a handful of
  nodes, or nodes that carry sentences, becomes a table or a list.
- **Mermaid stays where it shows what prose cannot:** a sequence of messages, a state machine, a
  small data flow. Each of the 104 blocks is reviewed against this rule, and none is removed blindly.
- **What a diagram shows is also in the text.** A screen reader, a terminal and a plain-text export
  cannot draw Mermaid (ACCESSIBILITY.md).

### 2.3 Every page gives its context

For a self-learner or a junior developer, context is what makes a page usable. Each page opens
with four things:

- **Who it is for**, and who should read something else instead.
- **What you will be able to do** after reading it.
- **What you need first**: the prerequisites, and a link for each.
- **Where it fits**: the page before it, the page after it, and the reference it relies on.

Inside a page:

- **Terms are defined at first use** (DOC_REVIEW's *What* question), or linked to
  [`VOCABULARY.md`](../VOCABULARY.md).
- **The why comes with the what.** A step names the reason for it, and a rule names the incident
  that made it, so a reader can tell a rule from a habit.
- **A background section** introduces, where it helps, the concept a page leans on (what a PTY is,
  what a lease guards against) with a pointer to a primary source.

### 2.4 Honest, and says so

These rules already hold in this project. They are written down here so they apply to every page:

- **Measured, expected and unknown are kept apart.** A number names its source, its date and its
  build. The platform words *Verified*, *Partly verified*, *Never compiled* and *Driven* keep their
  strict meanings.
- **A correction stays visible.** Mistakes and the numbers they changed are kept, with the
  correction next to them.
- **Limits are stated where the reader will meet them**, not only in a register.

### 2.5 References

Every technical page ends with **Read further**, and each entry is marked as a primary source
(POSIX, a man page, an RFC, a paper, a standard) or a secondary one (a book, a tutorial), with a
line on why to read it. [`BIBLIOGRAPHY.md`](../BIBLIOGRAPHY.md) holds the full entries in its
house shape, and pages link into it rather than repeating it.

### 2.6 One question per page

A page that answers several questions for several readers is split: by task (install, use,
extend), or by reader (a first-time learner, a contributor, an operator). The current state comes
first. How it came to be — milestones, corrections, dead ends — goes in its own section at the end,
or in the ROADMAP and the analysis pages, linked from there. Beginners read the page, and the
record stays whole.

## 3. Installation: one page per platform

A new directory **`docs/install/`**, one page per platform or platform family. Every page has the
same sections, so they can be compared:

1. **Status**: the verdict word, the date, the build, and whether it is *Driven*, with a link to its
   row in `PLATFORMS.md`.
2. **Who this is for**, and what "installed" means here (built from source, which binary is on
   `PATH`).
3. **Before you start**: the packages, the versions, and the disk and memory needed, as measured.
4. **Steps**, copy-pasteable, each with the output that means it worked.
5. **Measured difficulties**: every problem this platform has actually shown, one heading each, each
   saying what you see, why it happens, what to do, and the milestone that found or fixed it.
6. **Possible difficulties**: what has not been seen yet but follows from how the platform works,
   marked as not measured.
7. **Verifying the install**: `make check-target`, `doctor`, and one live turn.
8. **Reporting back**: the `PLATFORM_TESTING.md` §5 template.
9. **Read further**: the platform's own documentation, primary first.

The first set, from the rows `PLATFORMS.md` already has: Debian and Ubuntu, the Fedora family,
musl/Alpine, old Linux userlands, Raspberry Pi and other boards, FreeBSD, OpenBSD, NetBSD,
illumos/OmniOS, WSL2, Cygwin, MSYS2, Guix, FreeMiNT/ARAnyM, GNU/Hurd (measured on the other machine's branch),
and macOS. macOS is *Never compiled*, and its page says so, together with what is known and what is
guessed.

`INSTALL.md` stays the entry point. It becomes a short page: the generic build, then "find your
platform" with a table linking to `docs/install/`. `BUILD.md` keeps the build system's reference,
and the per-platform prose moves out of it.

## 4. `PLATFORMS.md`, concretely

- **The matrix becomes a table** (§2.2). The columns are platform, architecture and libc, verdict,
  Driven, last measured, and a link to the page. Every cell is short, and the Mermaid block goes.
- **The verdict tables use the same short columns.** Each row's evidence — the numbers, the gates,
  what failed and what was fixed — moves to its platform page's *Measured difficulties* and
  *Status* sections.
- **The long row histories** (illumos, OpenBSD, Cygwin, WSL2, FreeBSD, the architecture sweep) move
  to their platform pages as headed sections. `PLATFORMS.md` keeps a summary of a paragraph or two
  and the link.
- **The four words keep their definitions at the top.** They are the page's contract, and they get
  shorter.
- **The 17 files that parse the page are updated in the same milestone.** A lint that parses a
  table's cells has to move with the table: `platform_retest_lint`, `rig_live_lint`,
  `rig_ports_lint`, `portability_lint`, `deferred_register_lint`, `harden_flags_lint`, `doctor.sh`,
  `docs_flags` and the rig scripts. Each is changed, then proven red against the old shape, and
  green against the new one.

## 5. The directory structure

### 5.1 What exists

The 170 top-level pages are an alphabetical list. [`docs/README.md`](../README.md) groups them by
reader's task, and `docs_index_lint` keeps that map complete. The 14 subdirectories are already
grouped by kind: `analysis/`, `plans/`, `proposals/`, `reading/`, `assignments/`, `curriculum/`,
`case-studies/`, `presentations/`, `i18n/`, `internal/` and the rest.

### 5.2 Three options

- **(a) Keep the flat layout, and add `docs/install/`.** This is the cheapest. Nothing else moves,
  and the map in `docs/README.md` stays the way in.
- **(b) Group by the reader's task.** The top-level pages move into `start/`, `guides/`,
  `reference/`, `explanation/` and `project/`, along the lines of Daniele Procida's *Diátaxis*
  framework. It is the clearest for a newcomer, and the most expensive: 2,258 links, 50 paths in
  `src/`, the lints, the i18n mirrors, and every outside link to the public tree.
- **(c) Group only what is already a group, in steps.** `install/` first, then the two registers
  as directories with one file per entry, then the platform histories. A page moves only when it
  is being rewritten anyway, and the old path keeps a one-line stub that links to the new one.

**Recommendation: (c).** It gets the benefit where the pages are hardest to read, costs a move
only when the page is being worked on anyway, and never breaks an outside link without a stub.

**Decided at S6 (M795), after S1–S5: stay flat — go no further than (c) already reached.** The
two topical directories worth having exist (`install/`, `platform-notes/`, from S2). The rest of
the readability benefit that (b) and (c)'s later steps promised was delivered *in place*: S3's
ordered router (`docs/README.md`) is the newcomer's way in, and S4 made the registers one line per
entry in their own files rather than splitting them into directories. A further directory move —
Diátaxis (b), or registers-as-directories — would now touch ~2,258 relative links, ~50 `src/`
doc-path strings, ~25 path-keyed lints, the i18n mirrors and every outside link into the public
snapshot, for no remaining readability gain. If a future need appears (e.g. a public-snapshot
tidy), move pages one at a time as they are already being edited, with a stub at the old path —
never as a big-bang move.

### 5.3 Moving a page safely

These rules apply to any option:

- The old path keeps a stub page, *"moved to …"*, for good (decided on 2026-09-25, §7).
- `src/` strings are updated in the same milestone, and `doctor` checks that every docs path it
  prints exists (a lint for that is part of the first move).
- The lints that read the page are moved with it and proven red first.
- The i18n mirrors follow in the same milestone, or their tracks record the lag.

## 6. Sequencing

Milestones in order, each one gated, and none before the two machines' branches are merged. The
other machine's branch changes `PLATFORMS.md` too (the Hurd row).

| Step | What |
|---|---|
| S1 | **Done (M772).** The house rules (§2) are in `DOC_REVIEW.md` §7, and `tests/smoke/docs_table_rows_lint.sh` refuses a new table row over **200** characters in a top-level `docs/*.md` (the operator's limit, not the 500 this plan first drafted). Per-page floor at today's counts (60 pages, 929 rows, mostly the two registers), a ceiling that only falls; registers grandfathered until S4. Byte count under the tier's `LC_ALL=C`. |
| S2 | `docs/install/` and the new `PLATFORMS.md`, with its lints moved (§3–4). **In progress.** M777 created `docs/install/` and the first five pages; M778 rewrote them as
self-learner walkthroughs (numbered steps, which directory, what to type, what you should see,
escape routes) after the operator's review; M779 added the other eight — **13 platform pages** now,
`PLATFORMS.md` still untouched. FreeMiNT deliberately has no page (cross-build only). A subagent found
only **5** files parse `PLATFORMS.md`, not the 17 §4 named (`portability_lint`, `platform_retest_lint`,
`doctor.sh`, `docs_table_rows_lint`, `deferred_register_lint`); the load-bearing invariants for the
short table are the four verdict headings, the `\| Platform \| Arch / libc \| Evidence \|` header,
`uname -s = <token>` cells, and the Driven register (row-count == Platform-table count). M780 moved the ~440 lines of per-platform history prose out of `PLATFORMS.md` (961 → 532 lines) into
a new `docs/platform-notes/` — a developer-facing home the operator chose over the install pages,
which stay lean — with the 5 parsers untouched (they read the tables, not the prose) and the
`docs_table_rows` floor lowered 57 → 46. M781 shortened the fifteen giant Evidence cells (the 8,198-char MSYS2 row down to ~1,090) to the
tokens the 5 parsers require, moving their measurement logs to `platform-notes/` — each edit by exact
line number, parsers re-run after every batch, every invariant held. **`PLATFORMS.md`: 129 KB → 54 KB.
S2 is complete** (13 install pages, histories relocated, a short matrix). |
| S3 | **Done (M782–M789).** The 16-page "Start here" group reviewed (four read-only reviewers) and fixed: onboarding defaults to a free local model (M782); the router became a numbered path with off-path pages regrouped (M783); `VOCABULARY` corrected against source (M784); the build/setup/config/doctor teaching defects fixed (M785–M788); the review recorded and the remainder deferred (M789). Report: [analysis/2026-09-26-docs-s3-entry-path-review.md](../analysis/2026-09-26-docs-s3-entry-path-review.md). |
| S4 | **Done (M790–M791).** Both registers are one line per entry over per-entry `####` sections: `DECISIONS` (M790, overlong rows 354→9) and `DEFERRED` (M791, 225→3), the rejected-alternatives / why moved **verbatim**, every lint green. |
| S5 | **Done (M792–M794).** ~34 of 39 flagged diagrams converted to tables/lists or kept (genuine sequence/state/topology and the teaching pages'); the unreadable `PLATFORMS` matrix removed. Five low-value diagrams (a chronological `timeline`, four historical ROADMAP-entry diagrams) registered in `DEFERRED`. |
| S6 | **Done (M795): stay flat — no directory move.** With S1–S5's experience: S3's ordered router captured Diátaxis's navigation benefit without moving a page, and S4's one-line registers made the register-directory step unnecessary. A full move would touch ~2,258 links, ~50 `src/` paths, ~25 path-keyed lints, the i18n mirrors and the public-snapshot links for no remaining readability gain (§5.2). Revisit page-by-page only if a future need appears. |

## 7. Decisions for the operator

1. **The directory model**: (a), (b) or (c) of §5.2. The recommendation is (c).
2. **Stubs at old paths**: keep for one public release, for good, or not at all.
3. **The table rule**: the limit per row (about 200 characters is proposed), and whether a lint
   enforces it.
4. **The Mermaid rule**: review every block (proposed), or remove only the unreadable ones.
5. **The first pages to revise for self-learners**, beyond `PLATFORMS.md` and `INSTALL.md`.

**Decided by the operator on 2026-09-25,** each as recommended: 1 is (c); 2, the stubs stay for
good; 3, about 200 characters per row, enforced by a lint (S1); 4, every block is reviewed (S5).
Question 5 is asked when S3 starts.

## 8. How the numbers in §1 were taken

```sh
ls docs/*.md | wc -l                       # top-level pages
du -sh docs                                # size
wc -c -l docs/PLATFORMS.md
# table rows over 500 characters, per page
# over 200 (the operator's limit, M772); byte count under LC_ALL=C, as the lint uses
LC_ALL=C awk 'length($0) > 200 && /^\|/ {n[FILENAME]++} END {for (f in n) print n[f], f}' docs/*.md | sort -rn
grep -c '^```mermaid' docs/*.md | grep -v ':0$' | wc -l      # pages with Mermaid
grep -rl 'PLATFORMS.md' tests/ scripts/ | wc -l             # files that parse or name it
grep -rhoE 'docs/[A-Za-z_/.-]+\.md' src/ | sort -u | wc -l  # docs paths the binary names
```
