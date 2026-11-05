# JICHI I18N (localization) — gap analysis (the denominator the lint never names)

## TL;DR

*Read-only survey of `docs/i18n/`, `docs/presentations/`, and
`tests/smoke/i18n_tracks_lint.sh` at HEAD `1b71247d` (2026-10-05). The lint is
green 5/5 — and, measured against the corpus, it is checking exactly what it
can see. The thing the localization surface definitely lacks: **no check of
the *denominator* — the set of English documents that *should* have
translations.** Two English decks were added 2026-09-17 (M648 `07-the-release`,
M650 `08-self-learners`) and are translated into **none** of de/es/ja/zh — 8
missing deck-translation pairs, all invisible to the lint, and the i18n
README's own coverage table says "the seven decks" — a frozen number from
before they existed. The pattern: **every jichi inventory so far (man-page
ENVIRONMENT, the `init --list` bench literal, the reading anchors, the plan
status lines, the deferred line-anchors) has the same shape — a prose
denominator with no parity check; the i18n coverage table is the sixth
instance, and it is the one that is *meant* to be the live inventory.**

## 1. What the i18n surface is

- **10 English decks** in `docs/presentations/` (9 content + README); each
  of **de, es, ja, zh** has **7** decks (00–06); **ko** has `GETTING_STARTED.md`
  only, declared "by design" in the README table.
- **19 non-deck pages** across the five languages (GETTING_STARTED, PHILOSOPHY,
  JOURNEY, PROJECT_TIMELINE per language, plus `ja/VOCABULARY.md`), each
  carrying a `<!-- tracks: <relpath> @ <commit> -->` provenance header (M582).
- **`i18n_tracks_lint.sh`** (M582, 5 checks): corpus floor (52 pages / 50
  markers / 28 decks vs 10 English), marker existence, marker-path
  resolution, **slide-count parity per existing deck**, **no invented
  three-digit figure per existing deck**. All content-reading, deliberately
  not git-dependent (the public snapshot ships a fresh history).
- **`docs/i18n/README.md`**: the policy ("phased localization") and the
  **coverage table** — the document whose job is to say *what is translated*.

The lint's defect comment is the best-written "why" in the tree: four decks
promised a ~700 KB binary (measured 1.2 MB) and 7,170 checks (measured
12,960) for months because the *existing* translations were checked
silently against a moving English source. M582 fixed *that* class — a
translation that exists but lags. It did not fix the class this analysis
names: **a translation that should exist but does not**.

## 2. The census (every number re-measured at HEAD)

| Quantity | Value | Verified |
|---|---|---|
| English content decks | **9** (00-06, 07-the-release, 08-self-learners) | `ls docs/presentations/*.md` minus README |
| Decks per language (de/es/ja/zh) | **7** each — 00 through 06, identical set | `ls docs/i18n/<l>/presentations/` |
| `07-the-release` translations | **0** (none of de/es/ja/zh/ko) | `grep -r 07-the-release docs/i18n/` → nothing |
| `08-self-learners` translations | **0** | same |
| Missing deck-translation pairs | **8** (2 decks × 4 languages) | arithmetic of the two rows above |
| English deck that `07` was added with | **2026-09-17**, M648 (commit 79f54918) | `git log -1 -- docs/presentations/07-the-release.md` |
| `08` added | **2026-09-17**, M650 (commit 079a8e2b) | same |
| README's "Slides" definition | **"the seven decks under `presentations/`"** — dated 2026-07-24 | `git log -1 -S "the seven decks under"` → 2026-07-24 |
| Lint status | **green 5/5** (52 pages / 50 markers / 28 decks vs 10 English) | `sh tests/smoke/i18n_tracks_lint.sh` |
| ko row in the table | "GETTING_STARTED.md only, **by design**" | README table — a *stated* gap, unlike the 8 un-stated ones |

The date line is the whole story: the README's "seven decks" is **55 days
older** than decks 07 and 08. Every one of the eight missing pairs postdates
the number that defines the set.

## 3. Why this is "definitely lacking" (alternatives and their status)

1. **"The lint covers it."** It doesn't, by construction: check 4 iterates
   `find docs/i18n -path '*/presentations/*.md'` — the translations that
   *exist* — and compares each to its English source. A deck with *no*
   translation never enters the loop; the lint's corpus floor (28 decks) is
   a floor, so 20 would pass. The two-state shape (fix it, or declare the
   gap *with the number*) applies to slides and figures **within** a
   translation; there is no "deck absent" state to declare, because nothing
   asks the question.
2. **"Phased localization makes this intended."** The policy is
   *reference docs stay English-canonical, narrative set gets translated* —
   and decks are the narrative set. Decks 00–06 were translated into four
   languages; 07 and 08 were not. If the phase line were "07/08 not yet",
   the README table would say so — the way it says so for ko ("by design").
   The absence of a row *is* the finding: the coverage table has no
   mechanism for "exists in English, not yet started" — only for
   "done / draft / missing-but-declared".
3. **"Just translate them."** Eight more decks × four languages is real
   work (each deck is a full Marp document) and needs the same native-review
   process the table already documents — so the *mechanical* gap is the
   inventory, not the effort. But the correction set includes a cheap
   interim: the table gains a column or a footnote naming 07/08 as
   "not started", which is what ko's row does.
4. **"M582 already measured the deck drift and the README is that
   measurement."** The README's table is a snapshot *of* the measurement
   ("figures corrected M582; 4 decks short a slide") — it has no update
   mechanism and no lint that compares it to the corpus. It froze at
   "seven decks" for the same reason the plan headers froze at "planned":
   a prose denominator nobody re-runs.

## 4. The one thing definitely lacking

**A deck-coverage parity check: "every English content deck has, for each
non-canonical language, either a translation or a *declared* not-started
marker in the i18n README."** Concretely, two additions:

- **README**: replace the frozen footnote `("Slides" = the seven decks
  under presentations/.)` with a live list or a count that names 07 and 08
  as not started (mirroring ko's "by design" row) — a one-paragraph edit
  that makes the current state true *today*;
- **Lint**: `i18n_deck_coverage` — `ls docs/presentations/*.md` (minus
  README) is the numerator; for each of the four languages the intersection
  must equal the full set, or each missing name must appear in the README
  as a declared-not-started entry. Two loops, ~15 lines, no git dependence
  (pure file listing), same style as the existing checks. Red today on
  exactly eight pairs; green after the README edit (or the translations).

The class is now five-deep and the shape is invariant: the register's
self-asserting inventories (ENVIRONMENT in the man page, the `init --list`
literal, reading anchors, plan `Status:` headers, deferred line-anchors) all
have *membership* checks or none; the i18n table is the only one of the six
whose *entire purpose* is to be the live membership list — and it is the one
whose freeze is most costly, because it hides work from the four audiences
who would most want the decks: new German/Chinese/Japanese/Spanish readers
at a university or school talk.

## 5. What holds (verified, not assumed)

- **The lint's five checks are exactly as green as they read** — re-run at
  HEAD: 52 translated pages, 50 markers, 28 decks against 10 English, all
  parity/figure checks pass. The M582 defect (invented figures, vanished
  slides) is *genuinely* policed; I am not claiming the lint is weak, only
  that its denominator is open-ended.
- **The provenance convention is real and enforced**: all 50 markers are
  well-formed and resolve (checks 2–3). Every translation names the English
  source and the commit it tracked — the discipline this analysis wants for
  the *set* itself.
- **ko's row is the model of the missing thing**: "GETTING_STARTED.md only,
  by design" — a *declared* partial coverage with the reason, exactly the
  shape 07/08 would need. The README already knows how to say this; it
  simply never learned that the deck set had grown.
- **The "figures behind" / "4 decks short a slide" cells are the two-state
  declarations in action** — counts that re-verify against the lint's own
  arithmetic, so they can't rot silently (the lint fails if the declared
  number stops matching). The *missing* cells have no counterpart: a deck
  with no translation produces no number to declare.

## 6. The corrections (ready to apply)

1. `docs/i18n/README.md` — footnote: "the seven decks" → the nine English
   content decks, with 07-the-release and 08-self-learners declared
   not-started (the ko-row phrasing). ~3 lines.
2. `tests/smoke/i18n_tracks_lint.sh` — add a deck-set-parity check here
   (or in a sibling `i18n_deck_coverage.sh`): the parity described above.
   Red today (8 pairs), green after 1.
3. Optional, deferred: the 8 translations themselves — real work, out of
   scope for this register fix, and the policy's "native review welcome"
   applies.

## 7. Limitations (honest)

- I verified *absence* of 07/08 translations by name and by directory
  listing; I did not read the nine English decks' contents for any claim.
- The "285 English docs" policy number is the README's own (stated to have
  grown from ~60); I did not re-count the full doc set — the finding is
  about the *decks*, which are countable.
- Whether 07/08 were *intended* to be translated is a policy question the
  maintainer can settle in one sentence; I argue the existing practice
  (four decks in × every language, ko declared) makes the default
  "translate", and the default is cheap to state either way.
- No i18n lint existed *before* M582, and the README predates 07/08 by 55
  days; "who should have updated it" is a process question, not a blame one
  — the process is exactly what correction 2 installs.

## 8. Appendix: reproducible (all from the jichi checkout)

    sh tests/smoke/i18n_tracks_lint.sh            # green 5/5 at 1b71247d
    ls docs/presentations/*.md | grep -v README | wc -l    # 9
    ls docs/i18n/de/presentations/ | wc -l                # 7 (es/ja/zh too)
    grep -r "07-the-release\|08-self-learners" docs/i18n/   # nothing
    git log -1 --format="%ad %h %s" --date=short -- docs/presentations/07-the-release.md
    git log -1 --format="%ad %h %s" --date=short -- docs/presentations/08-self-learners.md
    git log -1 --format="%ad" --date=short -S "the seven decks under" -- docs/i18n/README.md
    grep -n "the seven decks" docs/i18n/README.md
    grep -n "by design" docs/i18n/README.md        # the ko row: the declared shape

## 9. Verdict

The i18n machinery is **excellent**: the M582 lint is one of the best-written
defect-driven checks in the register (its own comments document an illumos
port failure and a measured 2.6× figure error), the provenance convention is
enforced, and partial coverage is *declared* when it is meant (ko). The gap
is exactly one, and it is the sixth instance of the pattern this audit has
now measured: **the denominator is prose, and the prose is frozen.** Nine
English decks against seven translated, five decks of narrative missing from
four languages, and a coverage table that still counts "seven" — while the
lint that exists goes green on the 28 it can see. Correction 1 is three
lines of README; correction 2 is a fifteen-line check that makes "the deck
set grew" impossible to miss again. Revisit when: the README names nine
decks and the parity check exists and is green.

*Analysis 40. Read-only against jichi at 1b71247d; every number re-measured
2026-10-05.*
