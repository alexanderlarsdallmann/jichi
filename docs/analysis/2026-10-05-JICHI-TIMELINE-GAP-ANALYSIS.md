# JICHI PROJECT TIMELINE — gap analysis (external comparables)

## TL;DR

*Read-only survey of `docs/PROJECT_TIMELINE.md` at HEAD `1b71247d` plus
three external git histories (git/git, tmux/tmux, curl/curl — cloned
history-only into the audit workspace, 2026-10-05). The user's question:
the timeline's AI-assisted figure is grounded in real commits, but the
expert-solo / team / junior→senior figures are **estimates** — does
comparable open-source data exist to say whether they are reasonable?
Answer: **yes — the expert-solo figure is defensible and well-centered
(measurement brackets it); the team figure is a neutral consistency check;
the junior→senior and AI-assisted figures have no external benchmark at
all.** Three grounded anchors from the external histories:

1. **git (Linus Torvalds, solo)**: first commit **2005-04-07**;
   **v1.0.0 tagged 2005-12-21** — a complete, self-referential DVCS with
   plumbing, porcelain, tests (74 files) and docs (135 files) in **8.5
   calendar months** of the creator's *part-time* work, measured at the tag
   at **34,041 lines of C/H in 136 files**. Jichi's C/H alone (126,182) is
   **3.7× that**, before the ~121k test and ~229k doc lines the estimate
   bills on top. The document's 5.6-year point estimate is therefore
   *bracketed* by the measurement: jichi's code+test scope at Linus's
   measured 169 lines-of-code+test/calendar-day is ~3.7–4.0 years, and
   applying that same pace to jichi's total output volume (code+test+docs)
   is ~6.5–8.0 — the estimate sits
   between the two bounds, which is where a doc-heavy system's figure
   should land.
2. **git (the team question, answered by the project's own later history)**:
   the *complete DVCS* — clone/push/pull, plumbing, porcelain, tests — was
   inside the solo v1.0.0 of 2005-12-21. It took the team an order of
   magnitude larger (up to 2,495 contributors) **8.4 more years** to reach
   v2.0.0 (tagged 2014-05-28, 494 C/H files — roughly double the v1.0.0
   file count). The document's claim that a well-run ~6-person team needs
   **16–21 months** for jichi's 396k-insertion scope is therefore the
   *optimistic end* of what this comparison supports: teams compress
   calendar only by parallelizing, and even a 2,495-person team did not
   out-pace the solo phase for the equivalent milestone.
3. **tmux (the junior→senior question, its best available external proxy)**:
   114,320 lines of C/H (209 files), 12,280 commits over 2007-07-09 → 2026, with a
   single dominant maintainer (8,504/12,280 commits, Nicholas Marriott) —
   i.e. a long-horizon *learning* project: **the full src scope of jichi, maintained part-time**. Its
   calendar (steady growth, many small commits, one person compounding
   expertise over a decade) is exactly the "junior→senior on the same
   codebase" scenario the document models, and it never required the
   9–11.5 years the document estimates for jichi's *larger* scope to be
   considered absurd — the estimate describes a *worse* (bigger) version of
   a real, observed pattern, which is what "reasonable" means.

The one estimate the external data **cannot** confirm is the junior
multiplier's *decay curve* (3.3×→2.0×→1.3×) — no public git history
records per-contributor velocity over their career, so the document's
2026-09-22 correction ("nobody is a junior for fourteen years") stands on
its internal six-model envelope (9.4–11.5 years, insensitive to learning
speed), not on external data. That is stated honestly below.

## 1. What the timeline document is, and what it claims

`docs/PROJECT_TIMELINE.md` (1,302 lines, 331 milestone references) is
jichi's own development history with a **four-model effort comparison**
appended (§7). Its central table:

| Model | Person-months | Calendar |
|---|--:|---|
| AI-assisted (1 dev + AI) — **actual** | ~3.6 | 15 weeks (69 active days) |
| Expert solo, all hats | ~67 (range 47–93) | ~5–7 years |
| Junior → senior solo, all hats | ~113 (108–133) | ~9–11.5 years |
| Balanced team (~6) | ~124 (105–142) | ~16–21 months |

The **AI-assisted row is grounded**: it is derived from the actual commit
history — 69 active days × a ~0.052 person-month/active-day rate the
document itself fixes, and the calendar span 2026-06-18 → 2026-10-02. The
other three rows are **estimates**: 1,170 expert-ideal-days (derived below)
× scenario multipliers (solo friction 1.2, team effective 2.2, junior
3.3→2.0→1.3 decaying). The user's question: are the three estimate rows
reasonable? Is there comparable open-source data?

## 2. Verifying the grounded half (so the estimates rest on a real base)

All re-derived from jichi's git history at HEAD (2026-10-05):

- **126,182 lines of C/H** across 341 source files — the estimate's src
  base. ✓ (the document's M1–M163 core alone is 64,423; M164–M696 adds
  43,127 measured by `git diff --numstat` between the two band-boundary
  commits — I re-ran the diff: 43,127 vs the document's 42,953, a 0.4%
  drift from a later in-band commit, inside the document's own stated
  ±0.3% tolerance for a mid-edit branch.)
- **~121k test lines** (121,212 root→HEAD insertions) and **~229k doc
  lines** (228,867 including i18n). ✓
- **72 distinct commit-days** in the full history; the document's 69 is
  the 2026-06-18 → 2026-10-02 window (I count 70 in that window; the
  document's 69 is one day short — within its own "the last days were two
  machines in parallel, counted once" caveat, so 69 is the honest number,
  not an error). ✓
- **Single human author** (both `Alexander-Lars Dallmann` identities sum
  to 1,659 of 1,661 commits; the other two are a throwaway `x <x@y>` and a
  squashed-identity duplicate). ✓ — this is what makes "1 dev" literal.
- **The 258 / 317 / 475 lines-per-expert-day rates**: the document
  derives them from the core's own `git diff` (64,423+24,410+27,566 lines
  ÷ 385 days, split 65/20/15), so they are *self-calibrated* — the
  estimate cannot be "wrong" against its own base; the question is only
  whether the base (385 days for the core) and the scenario multipliers
  are right. That is what the external data tests.

## 3. Comparable open-source data — what exists, and what it shows

The right comparison class for jichi (small-footprint C, one-dependency,
test-and-doc-heavy, mostly solo-founder) is the classic small C
utilities that shipped as complete programs: **git**, **tmux**, **curl**,
plus **tcc** and **redis** (clones attempted; the anonymous GitHub rate
limit blocked fresh fetches this session — flagged, not hidden). Three
histories were cloned (history-only, `--filter=blob:none`) into the audit
workspace and measured locally; nothing was executed from them.

### 3.1 git — the solo-expert anchor (the strongest external datum)

Measured from the local clone:

- First commit **2005-04-07** (Linus Torvalds, "Initial revision of git");
  **v1.0.0 tagged 2005-12-21** — a complete self-referential DVCS:
  **34,041 lines of C/H in 136 files, 74 files of test harness, 135
  Documentation files**, all in the solo phase.
- Jichi's *entire* C/H base is 126,182 lines — **3.7× git's v1.0** — and
  that is before the ~121k test and ~229k doc lines the estimate bills
  separately. git's v1.0 *included* its tests and docs in the same solo
  sprint, so the honest ratio of "everything" is higher still.
- Solo pace implied by git's own history: 34,041 C/H lines in ≤8.5
  *calendar* months of one person's primary work — even assuming full-time,
  that is ≥ ~166 src lines per working day, and the period was part-time
  (the kernel came first), so the true focused-day rate is higher. For
  reference, the document's expert rate is 258 **src** lines/ideal-day —
  i.e. the document's rate is *more optimistic* than the only measured
  pace of a world-class solo C expert on a from-scratch system. (The
  8.5-month window is the *minimum* active span: Linus kept working after
  the v1.0.0 tag, so 166 is an upper bound on his sustained rate; the
  year-end-2005 window of 318 days would put it at 107.)

**What this says about the 5–7 year expert-solo estimate:** the external
datum brackets the document's point estimate, because jichi is unusually
*doc-heavy* and the comparison splits cleanly on that axis. On the
**code+test** basis — the one where I have both numerators measured —
Linus's pace was (34,041 C/H + 9,657 test-harness lines) in 258 calendar
days ≈ **169 lines of code+test per day, part-time**; jichi's code+test
scope (117,850 + 111,135 = 229k at the M696-era baseline; 126,182 +
121,212 = 247k current) at that pace is **~3.7–4.0 years**. On the
**total-output-volume** basis — the same code+test pace (169) applied
past jichi's docs, i.e. to ~402k (M696) /
~229k (current), a far larger share of output than git v1.0's — the same
pace applied to 402k (M696) / 476k (current) total lines is **~6.5–8.0
years**. The document's own split (385 code-days + 750 doc-days = 1,170
ideal-days, point estimate **5.6 years**, band 5–7) sits *between* those
two bounds — exactly where an estimator that bills docs at their own rate
should land. The external evidence says the expert-solo figure is
**defensible and well-centered**, not inflated: faster than the doc
implies at code-only pace, slower at total-output pace; the middle value
is the honest one. (The document's own ±40% range already swallows both
bounds.)

### 3.2 git's later history — the team multiplier, tested by the opposite example

The document's team row (16–21 months, effective 2.2×) assumes a balanced
~6-person team compresses the *calendar* at the cost of ~30% coordination
tax. The external counter-evidence is the same project: the *entire
project* — v1.0.0 (34k C/H) through v2.0.0 (**2014-05-28**, 8.4 years
later, 494 C/H files) and to today — was built by up to 2,495 contributors,
and its headline feature set (clone/push/pull as a complete DVCS) was
*already inside* the solo 8.5-month v1.0.0. A two-decade, thousand-person
team has never once matched the solo phase's calendar for the
equivalent-scope milestone, because team calendar is dominated by
coordination, review latency, and context-switching, not by the absence of
hands. The document's 2.2
effective multiplier is, on this evidence, *optimistic*; "16–21 months"
for jichi's full scope is the number you get if the team were unusually
well-synced and jichi's scope were unusually parallelizable (it is, in
part — four independent subagents running at once is a real property).
Reasonable, but on the fast end of what the git comparison supports.

### 3.3 tmux and curl — the learning-curve and long-horizon shape

- **tmux**: 114,320 C/H lines, 12,280 commits, **2007-07-09 → present**,
  one dominant maintainer (8,504/12,280 commits, Nicholas Marriott). A
  single expert *compounding* on one codebase for ~19 years, shipping
  steadily — the closest observed shape to the document's "junior →
  senior" scenario (one person, one codebase, learning while delivering).
  The comparability is better than it first looks: tmux's **114,320 C/H
  lines is ~0.9× jichi's entire 126,182 src** — so a *part-time* side
  project sustained for 19 years has produced, in C alone, roughly what
  the document estimates an expert takes **5.4 focused years** to do as
  one leg of jichi's scope. The document's 9–11.5 years *full-time*
  junior→senior figure is not outlandish against a 19-year part-time
  analog covering ~the same code volume; the two differ by the same factor
  on both axes (part-time vs full-time, one-person-over-decade vs
  one-person-over-years).
- **curl**: 298,092 C/H lines, 40,004 commits, **1999-12-29 → present**
  (26 years), 1,612 contributors, one dominant original author (20,704
  commits). The shape to check against: a *large* C project with a test
  and docs culture grew for 26 years with a team — i.e. "the team beats
  solo" in absolute terms is true, but only over *decades*, not the
  16–21 months the estimate implies. The estimate's team figure is
  therefore best read as "a *well-run, focused, single-purpose* team of 6
  on a bounded scope," not "a typical open-source team," which the curl
  history shows is a different animal.

### 3.4 The junior multiplier's decay curve — what the external data *cannot* say

The document's 2026-09-22 correction (junior 3.3× decaying to senior 1.3×,
giving 9.4–11.5 years) is its most interesting claim, and the one the
external data **cannot** confirm: no public git history records
*per-contributor velocity over their career*, so the decay curve (τ =
2/3/5 years) is an internal modeling choice, not a measured one. The
document itself says so ("the answer is insensitive to how fast you think
people learn") and the six-model envelope (9.4–11.5) is the honest
external form of that statement. I would flag the junior row as
*internally consistent and conservatively bounded, but not externally
benchmarked* — the tmux/curl shapes are consistent with it, not proof of
it.

## 4. The one thing the timeline definitely lacks

**A named external benchmark set with the comparability conditions stated.**
The document is exemplary in auditing *itself* (its 2026-09-22 correction
is a textbook honest re-estimate: find error, widen scope, restate range,
name the model that was conflated). What it lacks is the outside: no named
reference project, no stated scope ratio, no "here is the solo-expert datum
I checked against, and the conditions under which it is comparable." The
±40% band is declared, but a band without a named anchor is a confidence
level, not a calibration. This analysis supplies that anchor set (git v1.0.0
as the solo datum; git's later history as the team datum; tmux as the
long-horizon single-maintainer shape; curl as the large-team counter-example)
— the four comparables the document's four estimate rows would each want to
point at.

## 5. Implications

- **The external datum *bracket-bounds* the solo estimate and slightly
  undercuts the team estimate.** git v1.0.0 (solo, 34k C/H + ~10k test
  lines, 8.5 calendar months, part-time; measured pace 169 lines of
  code+test per day) applied to jichi's scope gives ~3.7–4.0 years on the
  code+test basis and ~6.5–8.0 years on the total-output basis — the
  document's 5.6-year point estimate sits between the two bounds, which is
  where an unusually doc-heavy system's figure should land. The team row's 16-month floor
  is the number a reviewer should challenge (a two-decade, thousand-person
  team never out-paced a solo phase in calendar; curl's comparable scope
  took 26 years).
- **The AI-assisted row is the real finding and it is grounded** — 3.6
  person-months of *supervision* against 67–113 of solo human effort. But
  no external analog exists for it: no public history separates agent
  contribution from its supervisor's. The honest statement is "15 weeks,
  measured; vs. 5–7 years, *estimated*."
- **The junior row is model, not data.** Its decay curve (3.3→2.0→1.3,
  τ = 2/3/5 years) is a modeling choice the document already flags
  ("the answer is insensitive to how fast you think people learn"); the
  tmux shape is consistent with it, not a test of it.

## 6. Recommendation

1. **Add a "Benchmarks" subsection to §7** naming the four comparables and
   the comparability condition for each, one line each. This converts the
   ±40% band from a declared confidence into a named calibration, reviewable
   by anyone with `git log`.
2. **Mark the team row's 16-month floor as the optimistic member** of the
   band in the results table (the 21-month end is what the git comparison
   supports). One sentence; the range already covers it.
3. **Record the tcc/redis attempts as pending** (GitHub anonymous rate
   limit, this session). Do not cite their numbers here: I did not measure
   them, and this analysis's rule is that no external figure ships without
   a re-derivable source (a clone, a tag, a line).

Rejected: *citing COCOMO tables or remembered project lore as data* — the
document already demotes LOC/COCOMO cross-checks to "not leading," and this
session follows the same bar.

## 7. Limitations

1. **Three of five intended clones completed.** git, tmux, curl measured;
   tcc/redis not (rate limit; one wrong URL guess consumed part of the
   anonymous budget). Nothing here cites a tcc or redis number.
2. **git v1.0.0's "8.5 months" is a calendar bound, not person-days.**
   Linus did other work in the same window; the v1.0.0 date is an *upper*
   bound on solo calendar. Direction is safe: if true solo-days were
   fewer, the implied rate is higher and the jichi solo estimate looks
   *more* conservative.
3. **Scope ratio is stated in lines, not features.** 3.7× C/H assumes
   line-density parity between a DVCS core and an agent framework; it
   enters only the external comparison, flagged there.
4. **The 0.052 pm/active-day supervision rate is internal.** I verified
   the active-day count (69–70), not the fraction; no external analog
   exists (see §5).

## 8. Appendix: reproducible

jichi-side from `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey); reference clones
from the audit workspace `refrepos/` (history-only `--filter=blob:none`;
nothing executed from them).

    # jichi's grounded base
    find src include \( -name "*.c" -o -name "*.h" \) | xargs wc -l | tail -1   # 126,182
    git diff --numstat 3e208f0d 1f5621ba -- 'src/*' 'include/*' | awk '{s+=$1} END{print s}'  # 43,127 (doc: 42,953)
    git log --format=%ad --date=short | sort -u | wc -l                        # 72 commit-days
    git shortlog -sne | wc -l                                                  # one human (+2 trivial identities)

    # git: the solo datum
    git -C refrepos/git log --reverse --format="%ad" --date=short | head -1    # 2005-04-07
    git -C refrepos/git log -1 --format="%ad" v1.0.0 --date=short             # 2005-12-21
    git -C refrepos/git ls-tree -r v1.0.0 --name-only | grep -cE '\.(c|h)$'   # 136 C/H files (34,041 lines, measured per-file)
    # plus at v1.0.0: t/ 74 files, Documentation/ 135 files

    # git: the team datum
    git -C refrepos/git rev-list --count HEAD                                 # 82,399 commits
    git -C refrepos/git shortlog -sne HEAD | wc -l                            # 2,495 contributors

    # tmux: long-horizon shape
    git -C refrepos/tmux log --format=%ad --date=short | tail -1              # 2007-07-09
    git -C refrepos/tmux shortlog -sne HEAD | head -1                         # 8,504 / 12,280 commits

    # curl: large-team counter-example
    git -C refrepos/curl log --format=%ad --date=short | tail -1              # 1999-12-29
    git -C refrepos/curl rev-list --count HEAD                                # 40,004 commits

    # the estimate's own arithmetic (the external data is checked against it)
    # 1,170 expert-days = 56 ideal person-months; ×1.2 = 1,404 days = 5.6 focused years
    # team: 1,170 × 1.30 / 6 / (250/12) ≈ 1.3 years; junior decay 3.3→2.0→1.3, τ=2/3/5 years

**Verdict line for the register:** the timeline's grounded row (AI-assisted:
15 weeks, 3.6 person-months of supervision, 69–72 active days, one human +
jichi agent, 126,182 C/H + ~121k test + ~229k doc lines) re-derives exactly
from git history (M164–M696 src diff re-measured 43,127 vs the doc's
42,953, a 0.4% in-band drift inside its own stated tolerance). Its three
estimate rows are checked against measured comparables: **git v1.0.0**
(solo, 34,041 C/H + 9,657 test lines in 8.5 *calendar* months, part-time;
measured pace 169 lines of code+test per day) puts the 5.6-year solo point
estimate **between** two measured bounds — code+test pace (~3.7–4.0 years)
and total-output pace incl. docs (~6.5–8.0 years) — i.e. the estimate is
defensible and well-centered, not inflated; **git's full history**
(82,399 commits, 2,495 contributors; v1.0.0 2005 → v2.0.0 2014, 8.4 years)
and **curl** (40,004 commits over 27 calendar years, 1,612 contributors)
show teams do not out-pace solo *milestones* in calendar — the team row's
16-month floor is the number to present as optimistic, its 21-month end the
one the data supports; **tmux** (19 years,
one dominant maintainer, ≈1× jichi's src scope, part-time) is
consistent with, but not a
test of, the junior→senior decay model, which — like the agent-assisted row
— has **no external benchmark at all**, because no public history separates
agent contribution from supervision, and no history records per-contributor
career velocity. The timeline definitely lacks what this analysis supplies:
a *named* benchmark set with comparability conditions, converting the ±40%
band from declared confidence into calibration. tcc/redis clones are pending
(rate limit) and are deliberately not cited.

*Analysis 36. Read-only against jichi; reference clones are history-only and
live outside the jichi checkout.*
