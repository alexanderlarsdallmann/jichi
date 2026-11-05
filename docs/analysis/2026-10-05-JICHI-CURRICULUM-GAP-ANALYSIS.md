# jichi curriculum: the one thing it definitely lacks — a delivery

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree and documentation at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`, M898-era tree). No
> files were modified there. All claims cite a source; inferences are flagged.
> **Companions:** `JICHI-DEBUGGER-GAP-ANALYSIS.md` (capability) and
> `JICHI-UX-GAP-ANALYSIS.md` (user experience).

---

## 1. TL;DR

jichi contains a complete teaching product: 12 modules, 90 two-sided-graded
assignments, a self-learner spine, a classroom layer, three course formats,
reading guides in four Japanese-named levels, localized learner decks, and a
teaching progression. It has **never been delivered once** — to a class, a
workshop, a school, or (as far as the record shows) to a single person who
worked it end-to-end. And it knows this, and has said so, in at least four
places written at four different milestones:

* *"**This guide predates its first classroom.** The lab timings are
  estimates from the self-learner ranges; the failure modes are the ones the
  material was designed around…"* — `docs/curriculum/INSTRUCTOR.md`, second
  paragraph of the document.
* *"**What remains is feedback from first real runs**, the translation
  trigger, and the expansion horizon."* — `docs/ROADMAP.md` (M179, the
  milestone that closed the curriculum proposal).
* The ROADMAP records that the guide's **younger-learners section is
  *"deliberately absent… (written only after a real school run)"* — a
  section that has therefore still not been written, because the run has
  still not happened.
* *"**You have arrived when:** your students break your forms, and it
  pleases you."* — `docs/JOURNEY.md:162`, restated in the capstone module
  as *"the course's last word"* (`docs/curriculum/11-the-capstone.md`,
  Reflection): the curriculum's own endpoint names students.

So the gap is not a missing module, a missing assignment, or a missing
localization. It is the missing **event**: the curriculum is a test suite
for a course that has never been executed — and, in a project where every
other claim is either machine-checked or recorded as deferred, the
*product* (the course) is the one thing the record shows as designed,
rehearsed in documents, and never once run in front of anyone.

## 2. What the curriculum *does* have (for precision)

Ruling the alternatives out requires knowing how complete the rest is:

- **12 learner modules** (`docs/curriculum/00` … `11`), each with assignment
  pointers, gates, and prev/next navigation (the navigation's uniformity is
  itself lint-pinned — def-212, *withdrawn* after a probe grep nearly
  "fixed" a unanimous convention).
- **90 assignment specs** in `docs/assignments/`, each graded by a
  two-sided verifier: *"pristine rejected, reference accepted"* — the same
  red-first bar the project applies to its own tests.
- **A self-learner spine** that is, per the map, *"a complete formative
  loop"* (floor + feedback) — `docs/CURRICULUM.md`: *"A self-learner gets
  Floor + Feedback — and that is a complete formative loop."*
- **A classroom layer** (`INSTRUCTOR.md`): per-module lab plans (15′ demo
  live / they work / failure modes / gate check), pacing for all three
  formats (introduction course, block course, 14-week semester), a
  verify-patterns cookbook, grading operations (a shell loop into
  `gradebook.jsonl`),
  provenance-as-integrity policy, set-regeneration, and a younger-learners
  section that is deliberately *unwritten* (its §9: "Deliberately absent
  (design §8) … gets written **after** someone runs the material with a
  school class"), plus a v1 feedback loop naming exactly what to record.
- **Four source-reading guides** (*Annai* the guided tour, *Fukabori* the
  deep dive, *Tsuiseki* the traced run, *Kiroku* the record) with explicit
  chapter-to-module assignments — the INSTRUCTOR page itself says this is
  "the only place their place in the course is written down."
- **Nine presentation decks** (`docs/presentations/00-super-features` …
  `08-self-learners`; a `README.md` indexes them) and **localized learner
  decks** in four languages
  (`docs/i18n/{de,es,ja,zh}`; `en` is the source, `ko` carries docs but no
  decks); the localization program is tracked as def-21.
- **A teacher's own progression** (`docs/TEACHING.md`): *"sit it as a
  learner, author, sit your own assignment in a student's bench, then
  distribute."*
- **Deliberate non-features, recorded:** no cohort view, no gradebook, by
  decision (`TEACHING.md` §Design decisions 7–8): *"A cohort view needs
  identity, storage and a policy about grades — someone else's software.
  jichi provides the machine surface… and a sidecar owns the
  aggregation."*

Every *artifact* of the course exists. What does not exist is the course.

## 3. The gap, stated

> **jichi's curriculum has no delivery record: no class, workshop, school,
> or self-learner is documented as having worked it, and the project's own
> documents state that the classroom layer is estimated, the younger-
> learners section cannot be written yet, and "what remains is feedback
> from first real runs." The product the teaching product was built to
> deliver — a person who learned through it — is the one output the tree
> contains zero evidence of producing.**

Two strength levels, as in the companion documents:

* **(Strong) Zero recorded deliveries of any format.** A search of `docs/`
  for evidence of a course having been run returns nothing (appendix,
  check F); the four self-statements in §1 are the record's own summary of
  the same fact.
* **(Weak, but the one with teeth) The classroom layer is a
  zero-sample design, and the project knows it.** The lab timings are
  *"estimates from the self-learner ranges"* — i.e. estimates derived from
  a population (self-learners) that has *also* no recorded instance. The
  failure modes per module are *"the ones the material was designed
  around"* — designed, not observed. Every module's gate check ("walk the
  room, it takes five minutes and catches silent stalls") has therefore
  never once walked a room.

## 4. Why "definitely lacking"

The candidate gaps and their status in the project's own records:

| Candidate | Status |
|---|---|
| Cohort view / gradebook | **Recorded as a deliberate non-feature** (`TEACHING.md` DD7; DEFERRED: *"Not done because it is a gradebook, i.e. someone else's software"*). Not a hole; a boundary, drawn twice. |
| Younger-learners section | **Recorded as deliberately absent pending a real school run** (ROADMAP M179; the INSTRUCTOR guide's own §9: "this section gets written **after** someone runs the material with a school class"). A hole *derived from* the main gap, correctly parked with the right revisit trigger — and still parked, which is live proof the run has not happened. |
| Translation of the course | **Recorded** (localized learner decks exist in 4 languages — `de`/`es`/`ja`/`zh`; def-21; the "translation trigger" is named as remaining in the same ROADMAP sentence as "first real runs"). |
| Scope boundary ("what this course does not teach") | **Built** (def-108, closed M711 at `CURRICULUM.md:253`); the *C++98/11* reader-misreading finding (def-262) tracks the residue. |
| 9 modules without next-links | **Withdrawn** (def-212) — a false alarm the project recorded with the probe that caused it. |
| **A delivery — any format, any population** | **No row, because no deferral was needed:** it is the *state* the other rows' revisit triggers are waiting on, self-declared in four places. Not parked; simply not yet happened, with the happening named as "what remains." |

The distinctive form, matching the other two analyses: the curriculum's
*artifacts* are all checkable and are checked (two-sided graders, lints,
coverage analyses — `analysis/2026-08-27-the-teaching-seams.md`,
`analysis/2026-09-16-language-teaching-coverage.md`); the curriculum's
*event* is the one thing no check exists for, and the documents say so
rather than pretending otherwise. That is the absence this project's method
can express — and the expression is present. What is missing is the thing
the expression describes.

## 5. Evidence

### 5.1 The self-statements (four, at four milestones)

1. `docs/curriculum/INSTRUCTOR.md:8`:
   > *"**Status, honestly.** This guide predates its first classroom. The
   > lab timings are estimates from the self-learner ranges; the failure
   > modes are the ones the material was designed around plus this
   > project's own dogfood record. Run it, mark where reality disagreed,
   > and revise."*
2. `docs/ROADMAP.md:9050` (M179, the milestone that built C7):
   > *"the guide predates its first classroom, says so, and lists what to
   > record."*
3. `docs/ROADMAP.md:9054` (same milestone):
   > *"With C7, the curriculum proposal is complete: C1–C7 all built
   > (M173a → M179). **What remains is feedback from first real runs**,
   > the translation trigger, and the expansion horizon."*
4. `docs/ROADMAP.md:9047`:
   > *"a deliberately absent younger-learners section (**written only
   > after a real school run** — the guide refuses the confident-without-
   > evidence move its own Module 9 teaches against)."*

The younger-learners section is still absent in the tree — the INSTRUCTOR
guide's own §9 says it is "Deliberately absent (design §8)" and "gets
written **after** someone runs the material with a school class" — which
is the live proof that the run it was waiting for has not occurred: a
parked item whose revisit trigger is "the course has been delivered"
stays parked until the delivery.

### 5.2 There is no delivery record anywhere

`docs/analysis/` contains three curriculum-related analyses and **none of
them is about a run**: `2026-08-27-the-teaching-seams.md` (finds seams in
the assignments/hints/grading *machinery*), `2026-09-16-language-
teaching-coverage.md` (counts what the course *covers*, over 505 files),
`2026-09-22-machine-translating-the-learner-pages.md` (measures a
translation pipeline). Grep for first-run evidence across `docs/`: **0
hits** (appendix F).

**Flagged inference:** this proves no delivery was *recorded* in the
project's own tree. A delivery that left no note — no INSTRUCTOR §10
record, no analysis, no ROADMAP entry — would be invisible here. The
project's register culture makes that unlikely, and its own four
statements make it more unlikely, but the proof is absence-based.

### 5.3 Even the self-learner path has no recorded instance

The self-learner spine is the format that *could* run without anyone
arranging anything — one person, a laptop, 90 graded assignments — and
it is the map's stated design target (*"A self-learner with a laptop,
alone — that is the design [goal]"*, `CURRICULUM.md:9`). Yet:

- The completion record is **per-bench**: `.jichi/progress.jsonl`,
  explicitly *"yours"* and *learner-owned* (`CURRICULUM.md:359`;
  `TEACHING.md:232`). The tree itself holds no record of any bench having
  progressed, because no bench's progress is committed — and by design
  the project's own benches are its own source of truth for *code*, with
  the curriculum's truth living in students' machines.
- The journey's arrival condition — *"**You have arrived when:** your
  students break your forms, and it pleases you."* (`JOURNEY.md:162`) —
  is reprinted in the **capstone module** as *"the course's last word"*
  (`docs/curriculum/11-the-capstone.md`, Reflection). The curriculum's
  own final line **requires students** — and the record contains none.
- The lab timings the INSTRUCTOR guide calls estimates are *"from the
  self-learner ranges"* — i.e. the design-time estimates for self-learners
  in `CURRICULUM.md` — not from observed self-learners.

This is the sharpest form of the finding: **the curriculum's own
completion criterion is a state the tree has no evidence of having been
reached, not once, in any format.**

### 5.4 The two-sided bar is proven *against the author's fixtures*

The grading discipline — *"pristine rejected, reference accepted"* — is
satisfied by construction: the reference solution is the author's, and
"pristine" is the untouched fixture. So every assignment's verify is
demonstrated to discriminate *author's work from no work*. What none of
the 90 verifiers has ever discriminated is **a real learner's partial,
clumsy, or wrong-but-almost work** — the population the grades are for.
`TEACHING.md:352` states the same gap for the teacher: *"The teacher's
walk cannot be enforced by a tool, so it is not pretended otherwise.
Nothing in jichi can verify you sat your own assignment."*

## 6. Implications

1. **The course's one output is unproven: that it teaches.** Every
   artifact is verified; the teaching itself — the thing a curriculum
   exists to do — is a design claim. A course can be internally
   consistent, honestly scoped, and complete on paper, and still fail in
   the one way its own lints cannot see: a learner arrives, and Module 2
   is a wall. The project's Module 4 (debugging as science) is built on
   symptom → hypothesis → experiment; the curriculum is currently at
   hypothesis.
2. **The classroom layer is a zero-sample design with live parameters.**
   12 modules × (timing, demo script, failure modes, gate check) × 3
   formats — every number in it an estimate, and the INSTRUCTOR guide's
   own §10 is the instrument for turning them into measurements. Until a
   delivery, the guide is simultaneously the best and the least
   trustworthy of jichi's teaching documents: best because it refuses to
   hide this, least trustworthy because its content is precisely what it
   cannot vouch for.
3. **The younger-learners section is blocked on the main gap, and the
   block is the correct behavior.** It is the register working as
   intended: a revisit trigger naming the missing event. But it is also a
   standing admission that the *introduction course* (the format that
   presupposes no prior programming) is the most under-tested path in the
   product, because it is the one the blocked section depends on.
4. **The self-learner claim carries the whole audience.** The README's
   audience is self-learners; the self-learner spine is the format with
   no arrangement required; yet it is also the format with zero recorded
   instances (unlike the screen-reader evidence in the UX analysis, here
   even the operator has — by the record's silence — not completed the
   journey). If a delivery does happen, the introduction-course cohort is
   the one that would most falsify or confirm the design, because it
   contains the people the course admits need no experience.
5. **The arrival condition makes the gap self-referential in an
   instructive way.** *"Your students break your forms, and it pleases
   you."* The journey teaches, as its final virtue, that a form's
   maturity is measured by having been broken by its users. The
   curriculum is, by its own endpoint's definition, a form that has not
   yet been allowed to break. Running the course is not an administrative
   step after the product; it is the last step *of* the product.
6. **Meta, the third of the same class.** Capability analysis: no runtime
   observation, unrecorded. UX analysis: no observation of non-builders,
   self-recorded. Curriculum analysis: no delivery, self-recorded and
   four times over. The pattern across the three is the project's
   epistemic shape: its checks reach exactly as far as machines can run,
   and at the boundary where the check is *a person having an experience*
   — debugging a crash, learning from an interface, being taught by a
   course — the record stops and says so. The UX and curriculum cases do
   what the debugger case lacks: they named their own blind spot.

## 7. Recommendation

1. **Run the smallest delivery: the introduction course.** Six sessions
   (build · M0 · M1 · M2 · M3 · M4+gate), deck skeletons already committed
   (`01-introduction`, `02-using-jichi`), pacing already estimated, and
   the population is the project's own: Module 0's config fork names the
   JLU HRZ gateway instance, the whole tree is a JLU project, and the
   learner-study protocol (a companion to the UX analysis) says "A JLU
   course is the likely source, and that is the operator's to arrange" —
   the same one-arrangement precondition, this time measuring a product,
   not just an interface.
2. **Keep the INSTRUCTOR §10 record as the first artifact of the run**,
   not a follow-up: actual lab minutes, first-stall locations, hint-rung
   usage, unlisted failure modes. That record is what turns every
   "estimate" in the classroom layer into a measurement, and what the
   younger-learners section's revisit trigger is waiting for.
3. **After (or with) a school run, write the younger-learners section** —
   the INSTRUCTOR guide's own §9 parked item, unblocked by its stated
   trigger ("after someone runs the material with a school class
   alongside a teacher who knows the age group").
4. **Then run the capstone rubric against real proposals.** Module 11 is
   the only module whose subject the curriculum does not provide (*"the
   itch must be yours"*); a rubric that has never graded a real itch is
   the last zero-sample parameter in the course.

Rejected, in the register's spirit:

* *A self-learner beta as the first delivery* — tempting (no arrangement),
  but it measures the *easiest-to-teach* population: motivated,
  computer-literate, willing to read 12 modules of docs. The
  introduction course's no-experience premise is the design assumption
  most in need of testing, and a beta would leave it untested.
* *Instructor-author delivery only* — the operator sitting the course as
  a learner satisfies `TEACHING.md`'s progression (and the INSTRUCTOR
  guide's own non-negotiable: *"an instructor who has never watched a
  gate go red teaches gates as decoration"*) but not the gap: it is
  first-person, by the builder — the exact evidence class the UX analysis
  showed to be the limit. It is a necessary step, not a sufficient one.
* *Shipping cohort tooling first* — ruled out twice by the project's own
  decisions (DD7/DD8): aggregation is a sidecar's job, and the walk
  cannot be enforced by a tool, so it is not pretended.

## 8. Limitations and honest counterarguments

1. **Absence-based, on a self-reporting absence.** The strong claim rests
   on four explicit self-statements plus a zero grep. If a delivery
   happened off-record (a course taught, notes kept elsewhere), §5.2 is
   wrong. The project's record culture (DEFERRED, ROADMAP, analysis/) is
   the reason to discount that; it is not a proof.
2. **"A curriculum" vs "the course" — the skeptic's strongest card.**
   The 12-module self-learner spine is, by the map's own words,
   *complete* for its target: floor + feedback is a closed formative
   loop, and a self-learner could finish it today without anyone
   delivering anything. The gap therefore does not touch the spine's
   self-sufficiency claim; it touches everything *around* the spine
   (classroom, introduction course, school, younger learners) and the
   spine's completion criterion (JOURNEY's arrival needs students). If
   the operator decides the spine is the whole product and the rest is
   a bonus, §3's strong form weakens to: *the classroom product has
   never been delivered and says so.*
3. **The curriculum's dogfood record is real but partial.** The
   INSTRUCTOR guide's failure modes are "the ones the material was
   designed around **plus this project's own dogfood record**" — jichi
   *has* been used to build jichi, extensively. That is evidence a
   sophisticated practitioner can work it. It is not evidence a novice
   can: dogfood is the opposite population from the introduction
   course, and it is why the estimate is still labeled an estimate.
4. **Not executed.** No learner was sat through any module for this
   analysis; the two-sided graders were not run here either. Everything
   is read from the record at HEAD `1b71247d`.
5. **Tree vintage.** The four self-statements are from M179 (July 2026)
   and its page; none is struck as of `1b71247d` (October 2026). If a
   run happened between the last ROADMAP entry and this commit without
   being recorded, §5.2's grep would not show it — same bound as §8.1.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the four self-statements
sed -n '7,12p' docs/curriculum/INSTRUCTOR.md                 # "predates its first classroom"
grep -n "predates its first classroom\|What remains is feedback from first real runs\|school run" docs/ROADMAP.md
sed -n '9040,9056p' docs/ROADMAP.md                          # the M179 close-out

# the record's silence on any delivery
grep -rln "first run of the course\|taught the course\|delivered the course\|course ran" docs/ --include=*.md | wc -l     # 0
ls docs/analysis/ | grep -iE "course|student|cohort"          # only machinery/coverage/translation analyses

# the scale
ls docs/curriculum/0*.md docs/curriculum/1*.md | wc -l        # 12 modules
ls docs/assignments/*.md | grep -v solution | grep -cv INDEX  # 90 specs
ls docs/presentations/*.md | wc -l                            # 10 files = 9 decks + README index
ls docs/presentations/[0-9][0-9]-*.md | wc -l                 # the 9 actual slide decks (NN-*)
ls docs/i18n/                                                 # localized docs: de en es ja ko zh
for l in de en es ja ko zh; do printf '%s decks: ' $l; ls docs/i18n/$l/presentations/ 2>/dev/null | wc -l; done   # de/es/ja/zh: 7; en/ko: 0
ls docs/reading/ANNAI.md docs/reading/FUKABORI.md docs/reading/TSUISEKI.md docs/reading/KIROKU.md   # the four source-reading guides

# the three formats and their (estimated) pacing
sed -n '286,325p' docs/curriculum/INSTRUCTOR.md
sed -n '/^## 9. Younger learners/,+2p' docs/curriculum/INSTRUCTOR.md   # "Deliberately absent (design §8)"

# the deliberate non-features (boundaries, not holes)
grep -n "no cohort view\|no gradebook" docs/TEACHING.md

# the completion criterion
grep -n "You have arrived when" docs/JOURNEY.md
grep -n "course's last word" docs/curriculum/11-the-capstone.md
grep -n "progress.jsonl" docs/CURRICULUM.md | head -2

# the two-sided bar and who it has ever discriminated
grep -n "two-sided proof" -A1 docs/curriculum/INSTRUCTOR.md
grep -n "The teacher's walk cannot be enforced" docs/TEACHING.md

# the self-learner claim
sed -n '9,12p' docs/CURRICULUM.md                            # "Who it is for"
sed -n '347,348p' docs/CURRICULUM.md                         # "complete formative loop"

# the population the delivery would need (one arrangement)
grep -n "JLU HRZ" docs/curriculum/00-a-working-bench.md
grep -n "JLU course is the likely source" docs/LEARNER_STUDY.md
```

**Verdict line for the register this could seed:** *jichi's curriculum has
no delivery record in any format: INSTRUCTOR.md:8 ("This guide predates its
first classroom; the lab timings are estimates"), ROADMAP.md M179 ("What
remains is feedback from first real runs"), the younger-learners section
still absent pending "a real school run" (M179, unstruck at 1b71247d), and
JOURNEY.md:162's arrival condition ("your students break your forms")
referencing students the tree never contains. The 90 two-sided graders
prove discrimination against the author's fixtures only. Revisit when: an
introduction-course run's INSTRUCTOR §10 record (actual minutes, first
stalls, unlisted failure modes) lands — that record, not this analysis, is
the measurement.*
