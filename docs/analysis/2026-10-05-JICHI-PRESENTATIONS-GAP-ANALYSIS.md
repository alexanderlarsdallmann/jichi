# jichi presentations: the one thing they definitely lack — a room

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified there. All figures cited below were re-measured for this analysis
> (appendix, reproducible).
> **Companions:** `JICHI-CURRICULUM-GAP-ANALYSIS.md`,
> `JICHI-UX-GAP-ANALYSIS.md`, `JICHI-DEBUGGER-GAP-ANALYSIS.md`.

---

## 1. TL;DR

jichi ships **nine** Marp slide decks in English and **28** localized deck
files in four languages (de · es · ja · zh, decks 00–06), with a render
pipeline (`make slides`), a lint that pins each localized deck to its English
twin's slide count down to the number, a native-review process, speaker notes
in presenter mode, and an explicit rendering target set (HTML/PDF/PPTX). The
decks include a release-argument deck ("What we are claiming, and how you can
check it" — *"the deck to open first"*), a university deck, a school deck, a
self-learners deck, and a build-retrospective deck.

They have **never been presented**. There is no record in the tree of a deck
being shown to any audience — no conference, no class, no demo, no release
event — and the deck that exists *to* be the first artifact of such an event
(07-the-release, *"First release, August 2026"*) is addressed to a reviewer
who has never been in the room: its final slide is titled
*"What we would tell a reviewer to check first."*

This is the fourth member of the same class the previous three analyses
found. The capability analysis found an instrument (debugging) that has
never observed a real crash. The UX analysis found an instrument (the
learner study) that has never observed a real user. The curriculum analysis
found a product (the course) that has never been delivered to real students.
The presentations are the *public voice* of all three: the argument for a
tool, a course, and a release — written, measured, localized, lint-pinned,
and delivered to nobody.

## 2. What the presentation set *does* have (for precision)

- **Nine English decks** (`docs/presentations/00-super-features` …
  `08-self-learners`), each with `marp: true` front-matter, paginated, with
  speaker notes in HTML comments (Marp presenter mode). A `README.md`
  indexes them and declares `07` "the deck to open first".
- **28 localized deck files** across four languages (de · es · ja · zh),
  each carrying decks 00–06. `07` and `08` are deliberately English-only:
  the README says "a confident wrong translation of a *claims* deck would be
  worse than an absent one" — the deck that most needs an audience is the
  one most guarded against a premature one.
- **A render pipeline** (`make slides`, Makefile line 1571): renders every
  English deck to `docs/presentations/out/` and every localized set to
  `out/<lang>/`; a no-op with a note when `marp`/`npx` is unavailable, "so
  it never breaks a build or CI".
- **A slide-count lint** (`tests/smoke/i18n_tracks_lint.sh` check 4): every
  localized deck must match its English twin's separator count or declare
  the gap with `<!-- slides-behind: N -->`, **N exact** — "An approximate
  declaration is refused on purpose: a number nobody maintains is how the
  previous convention rotted."
- **A declared, bounded, measured lag**: 16 markers, 8×1, 4×2, 4×3 (the four
  short decks in all four languages), re-read at M658 from the markers when
  the prose had gone stale — the register's own drift story, recorded.
- **A native-review process** (def-156): Japanese has a real native reader
  (underway since M657, "her first impression is recorded rather than acted
  on"); Spanish and Chinese have no route ("writing in three languages this
  repository cannot review").
- **Measured, re-stamped content**: deck 07 says *"Every number on these
  slides was measured for this revision, not carried forward. That rule
  exists because the counts had drifted once, and the fix was a lint."*
- **Named audiences for each deck**: headline capabilities (00),
  introduction (01), day-to-day use (02), roadmap (03), research/coursework
  (04), classroom (05), build retrospective with "four delivery models"
  (06), the release argument (07), learning alone (08).

Every *artifact* of a presentation exists. What does not exist is a
presentation.

## 3. The gap, stated

> **jichi's presentation decks have no delivery record: nine English decks
> and 28 localized files, a render pipeline, a slide-count lint, a
> native-review process, and a release-argument deck addressed to a reviewer
> are in the tree, and there is no record of any deck being presented to any
> audience — no conference, classroom, demo, or release event. The decks'
> stated audiences (the university, the school, the self-learner, the
> release reviewer) are populations the tree contains no evidence of
> having met the product.**

Two strength levels, as in the companion analyses:

* **(Strong) Zero recorded presentations.** A search of the tree for
  evidence of a deck having been shown (appendix, check F) returns nothing
  beyond the decks themselves and the render target.
* **(Weak, but the one with teeth) The argument has never met its
  counter-argument.** Deck 07's four claims (C89/one dependency; runs where
  the alternatives do not; the testing is the argument; the documentation
  ships in full) are each *checkable* — the deck says how. Checkability
  was exercised on the numbers (re-stamped, linted). The claims as
  *persuasion* — do they land on a skeptical reviewer? do the 512 MB and
  "one dependency" figures survive a room's questions? — are untested in
  the only way that tests persuasion: delivery. The final slide, "What we
  would tell a **reviewer** to check first", is a rehearsal for an
  interrogation that has not happened.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| Localized content lag (decks 00/03/04/05 short) | **Recorded, bounded, linted, and measured** (16 exact `slides-behind` markers, M582 row, re-read M658). Not a hole; a declared gap with a working declaration mechanism. |
| Native review of the localized decks | **Recorded and underway where a route exists** (def-156: Japanese has a reader since M657; es/zh "have none" — parked with the right revisit trigger). |
| Translating decks 07/08 | **Recorded as deliberately deferred** ("a confident wrong translation of a *claims* deck would be worse than an absent one"). A boundary, not a hole. |
| Rendered artifacts (HTML/PDF/PPTX) | **Not shipped, by design**: `out/` is gitignored; `make slides` is a no-op without marp. The decks are the source of truth, "plain markdown, so they diff and review like any other doc." |
| **A presentation — any deck, any room** | **No row, because no deferral was needed:** it is the *state* the other rows' triggers point at. Native review needs a *reader*; the localized lag needs a *translator*; the release argument needs a *reviewer*; the university/school/self-learner decks need an *audience*. All four populations are external, and the record contains none of them. |

The distinctive form, the fourth instance: the decks are **argUMENT**
documents — 07 is titled "the release argument" and closes on what a
reviewer should check; 08 states "the feedback deficit a self-learner
actually has"; 06 compares "four delivery models". An argument that has
never been delivered is a *written* claim about an *oral* event. Every
internal property is measured (numbers re-stamped, counts linted, gaps
declared exactly); the one external property — that the argument works on a
person — is the thing the measure cannot reach.

## 5. Evidence

### 5.1 The address of the release deck

`docs/presentations/07-the-release.md`, created at M307 (2026-08-06,
commit `e3be2d15`):

> *# jichi / ### What we are claiming, and how you can check it /
> \*First release, August 2026\**
>
> > jichi is a working agentic coding tool in **C89**, with **one**
> > dependency, that runs on a 512 MB single-board computer — and it ships
> > the **whole** record of how it was built, including the parts that went
> > wrong.
>
> *Four claims. Each one is checkable, and this deck says how.*

… and its final slide:

> *# What we would tell a reviewer to check first*

A deck written as a first-release argument, whose last slide rehearses the
questions the reviewer would ask, is a document whose completion criterion
is **an event**: being reviewed. The tree contains the rehearsal and not
the review.

### 5.2 There is no delivery record anywhere

Grep across `docs/` and the README for evidence of a deck having been
presented — "presented to/at", "gave the talk", "showed the slides",
"conference", "keynote", "public demo" — returns **four hits, none of
them a presentation** (two ROADMAP false-matches on "under-represented
at" and "presented to userspace"; two BIBLIOGRAPHY entries — a cited CHI
'90 paper, and a note that "conference talks age well"). The ROADMAP
entries that mention decks are about *restamping figures*
(M390: "The 28 localized decks still carry the old figures — a DEFERRED
row"), *localization* (M164), and *the deck's creation* (M307, M648) —
never a room. The render target `out/` holds HTML that was generated by
`make slides` on the maintainer's machine (gitignored, present on disk: 8
English + 4 localized sets) — evidence of *rendering*, not of *audience*.

**Flagged inference:** this proves no presentation was *recorded*. A talk
given off-record would be invisible here. The project's register culture
is the reason to discount that, not a proof — same bound as the curriculum
analysis §8.1.

### 5.3 The audience decks' audiences

- **04-university** — "Research, coursework, reproducibility, low-
  resource." The curriculum analysis found the JLU course never happened;
  the university deck is its voice to the same absent room.
- **05-school** — "Classroom use and the guardrails that make it safe."
  The INSTRUCTOR guide's younger-learners section is unwritten pending
  "a real school run" — the school deck is the pitch for that run.
- **08-self-learners** — "the feedback deficit a self-learner actually
  has… and what jichi will not do for you." The curriculum analysis found
  no recorded self-learner completion. The deck that speaks to that
  population has not, in the record, spoken to one.
- **07/08 English-only** — the two *argument* decks (release, learning)
  are the two decks excluded from localization. The guarded content is
  precisely the content that needs the audience; the translated content
  (00–06, capability/usage/retrospective) is the content that can be read
  as documentation.

### 5.4 What "measured" reaches, and what it cannot

The re-stamping discipline is real and was exercised: M390 caught a
"50% optimistic" size figure *on the slide whose thesis is 'not should run
— measured'*; deck 07 carries the rule forward; the lint keeps 28 decks'
counts exact. But every one of those measures is **internal**: the deck's
claims *about the tree* are checkable against the tree. The deck's
function — persuading a person in a room, surviving questions, landing the
512 MB figure with someone who has not read the roadmap — is external, and
the tree has the same relationship to it as the curriculum has to its
students. The "north-star dogfood" the README cites (zigodot: "jichi
driving a large, autonomous Godot→Zig rewrite") is referenced across the
decks as an audience-facing story; in this tree it appears in **27
`docs/internal/` files (166 mentions)** plus the project's own PHILOSOPHY
pages and decks 00/04 in all five languages — as a *story being told*, not
a record of the project itself (it lives in a private repo). **Flagged
inference:** an audience told the zigodot story would have questions this
tree cannot answer, because the answers live in another repository's
record.

## 6. Implications

1. **The release argument is unargued.** 07 is "the deck to open first" —
   the project's public voice about itself. An argument that has never met
   a counter-argument is a hypothesis, not a persuasion. The four claims
   are internally verified; whether they *convince* — the only property a
   release argument exists to have — is unmeasured in the only way it can
   be.
2. **The external-facing surfaces form one untested ring.** Read in order:
   the curriculum (never delivered), the decks (never presented), the UX
   study (never run), and the localized prose (native review pending).
   These are the four places jichi addresses *someone else*. All four are
   instrument-complete and audience-zero. The tree's rigor is maximal where
   it can be checked by a machine, and honestly declared exactly where it
   cannot.
3. **The guarded decks are the needed decks.** 07/08 — release argument
   and self-learners — are English-only because "a confident wrong
   translation of a claims deck would be worse than an absent one." The
   same caution, applied consistently, has kept the decks themselves from
   a wrong delivery: a claims deck shown to the wrong room (or a room
   without the follow-up mechanism) would be a confident wrong argument.
   The guard is correct; it is also the reason the release argument's
   truth is as private as the code's.
4. **The render pipeline's no-op design is a symptom, not a bug.** `make
   slides` degrades to a note rather than failing — "same pattern as the
   Emacs/elisp targets". The pattern exists so the build is hermetic for
   the *maintainer's* environment; the decks' *audiences'* environments
   (a projector, a conference kit, a school lab) have no such graceful
   path — which is one reason a presentation would be a *different event*
   from a build, and why the tree contains no trace of one.
5. **The "four delivery models" of deck 06 are unauditable from inside.**
   06 is the build retrospective whose point is comparing delivery models
   (including "one dev + AI"). The comparison is the deck's subject; the
   deck's own delivery is a model with no data point — the same
   self-reference the register keeps catching (def-26's grep counting its
   own row).

## 7. Recommendation

1. **Present deck 07 once, to a reviewer who has not read the roadmap.**
   The deck's own final slide is the agenda: "What we would tell a
   reviewer to check first" — give it to someone who will check it. The
   cheapest such room already exists in the tree's own register: the
   JLU connection (the university deck's audience, the curriculum's
   pending course). One arrangement, as in the companion analyses — and
   the deck is already written *for* this exact event.
2. **Keep the audience's questions as the artifact.** The register's form
   fits: claim → grounds that resolve → warrant → counter-argument →
   revision (GROUNDED_DISCOURSE.md). A presented deck yields counter-
   arguments; the unmeasured property of 07 (persuasion) becomes a
   recorded one.
3. **Then the unblocking chain is the register's, already parked:** a
   *reader* exists for Japanese review (def-156) — the presentation's
   audience is the same population; the *translator* route for es/zh is
   the same; the *school* deck's run is the curriculum analysis's
   recommendation; the *self-learner* deck's feedback deficit is filled by
   the learner study's first observed learner. One event feeds four rows.
4. **Re-stamp 07 after the first delivery** — "First release, August
   2026" becomes "First release, presented <date>, <venue>", and the
   re-stamp lint's discipline (numbers measured, not carried) extends to
   the argument's own status.

Rejected, in the register's spirit:

* *Ship the rendered HTML as the release artifact* — `out/` is
  gitignored on purpose; the markdown is the source of truth and the
  render is a convenience. Shipping the render would make the *artifact*
  look delivered while the *event* still hasn't happened — the M326b
  shape: a check that passes because its universe shrank.
* *Translate 07/08 now, to widen the deck's reach* — refused by the
  README's own argument ("a confident wrong translation of a *claims* deck
  would be worse than an absent one") and by the review situation def-156
  records: Japanese has a real native reader (M657) but es and zh "have
  none." A wider, unreviewed claims deck is a worse
  object than a narrow, unreviewed one.
* *An internal "dry run" presentation to the maintainer's own notes* —
  first-person, by the builder: the UX analysis's limit, the curriculum
  analysis's limit, and here the deck's own "the reviewer" is named as
  someone who checks. A dry run produces confidence; the gap is the
  confidence's absence of a counterparty.

## 8. Limitations and honest counterarguments

1. **Absence-based, on a self-reporting absence.** The strong claim rests
   on a zero grep plus the decks' own self-address. Off-record talks are
   invisible. The register culture is the discount, not a proof.
2. **A deck is not a promise of a talk.** The README's "the deck to open
   first" is an ordering instruction for *reading*, not a commitment to
   present. The decks can be documentation in Marp's clothing — and in
   their current state (read, diffed, linted, translated) they *are*
   documentation. The gap claim is that their *title* (presentations;
   argument; what a reviewer should check) asserts a function the record
   does not show. If the operator's intent was always "documented decks",
   §3 weakens to: *the decks' argumentative framing is unfulfilled, not
   that a delivery was due.*
3. **The rendered HTML on disk is weak evidence of a private audience.**
   Someone (the maintainer) has generated the HTML — possibly to view the
   decks in a browser, which is *presentation to self*. That is first-
   person, by the builder: a rehearsal, not a delivery. I count it as
   zero, and I say so.
4. **The zigodot inference is flagged and thin.** The "north-star
   dogfood" is referenced as an external project; this tree has the *story*
   of it (27 `docs/internal/` files, 166 mentions, PHILOSOPHY/deck pages in
   five languages) but not the project's own record — it lives in a
   private repository. If that repository has its own presentation record,
   §5.4's last point softens — but this deck set's own delivery record
   would still be zero.
5. **Tree vintage.** The M307 deck (2026-08-06) predates 0.12.0 (the
   latest release per `CHATBOT.md`) and M840; "First release, August
   2026" is the deck's own stamp and I have not re-verified it against an
   external changelog — there is none in the tree beyond the PROJECT_
   TIMELINE's release-day entries (v0.10.0 on 2026-09-22 is the first
   release-day the timeline names explicitly). **This is a small
   discrepancy in the deck's own title slide that a first delivery would
   surface** — included here because the point is that nothing yet has.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the scale
ls docs/presentations/*.md | wc -l                       # 10 files = 9 decks + README index
ls docs/presentations/[0-9][0-9]-*.md | wc -l            # 9 English slide decks
for f in docs/presentations/[0-9][0-9]-*.md; do printf '%s: ' $(basename $f); grep -m1 "marp:" $f; done   # all nine: marp: true
ls docs/i18n/*/presentations/[0-9]*.md | wc -l           # 28 localized deck files
for l in de es ja zh; do printf '%s: ' $l; ls docs/i18n/$l/presentations/ | grep -oE '^[0-9][0-9]' | tr '\n' ' '; echo; done   # 00..06 each

# the argument deck and its address
git log --format="%ci %s" -1 e3be2d15                     # M307: 2026-08-06, "a release deck that argues"
sed -n '10,17p' docs/presentations/07-the-release.md      # "First release, August 2026"
grep -n "reviewer" docs/presentations/07-the-release.md | head
grep -n "measured for this revision" docs/presentations/07-the-release.md

# the record's silence on any presentation
grep -rin "presented to\|presented at\|gave the talk\|showed the slides\|conference\|keynote\|public demo" docs/ README.md --include=*.md | grep -iv "presenter mode\|presentation mode"   # 4 hits, none a presentation: 2 ROADMAP false-matches ("under-represented at", "presented to userspace"), 2 BIBLIOGRAPHY (a cited CHI '90 paper; a note that "conference talks age well")

# the bounded, linted localization lag
grep -rho "slides-behind: [0-9]*" docs/i18n/*/presentations/*.md | sort | uniq -c   # 8×1, 4×2, 4×3
grep -n "slides-behind" -A4 tests/smoke/i18n_tracks_lint.sh | head -12              # exact-N or the lint refuses
for d in 00-super-features 03-roadmap 04-university 05-school; do en=$(grep -c '^---' docs/presentations/$d.md); de=$(grep -c '^---' docs/i18n/de/presentations/$d.md); echo "$d: en=$en de=$de delta=$((en-de))"; done   # 1/3/2/1, matching the markers

# the render pipeline (gitignored out/)
sed -n '1571,1576p' Makefile
git check-ignore docs/presentations/out/07-the-release.html && echo "out/ is a local render, not a shipped artifact"

# the guarded claims decks
grep -n "English-only" docs/presentations/README.md
grep -n -i "native review" -A2 docs/DEFERRED.md | head -8   # def-156 (title is "Native review…" — needs -i)

# the release-day the timeline names explicitly (for §8.5)
grep -n "v0.10.0 release day" docs/PROJECT_TIMELINE.md
grep -n "latest release" docs/CHATBOT.md
```

**Verdict line for the register this could seed:** *jichi's presentation
decks have no delivery record: nine English decks (07-the-release: "What
we are claiming, and how you can check it — First release, August 2026";
its final slide: "What we would tell a reviewer to check first") and 28
localized files (de/es/ja/zh, decks 00–06), with make slides, a
slide-count lint (16 exact slides-behind markers: 8×1, 4×2, 4×3), and a
native-review process — and zero recorded presentations to any audience
(grep clean at 1b71247d). The decks' stated audiences (reviewer,
university, school, self-learner) are the same external populations the
curriculum (never delivered), the UX study (never run), and the debugger
(never observed on a real crash) await. Revisit when: a presentation of
07 is recorded — date, venue, and the counter-arguments it produced —
which unblocks def-156's readers and the curriculum's first classroom with
one event.*
