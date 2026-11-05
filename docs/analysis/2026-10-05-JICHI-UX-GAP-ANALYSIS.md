# jichi UX: the one thing its interface definitely lacks — observation of real learners

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree and documentation at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`, M898-era tree). No
> files were modified there. All claims cite a source; inferences are flagged.
> **Companion:** `JICHI-DEBUGGER-GAP-ANALYSIS.md` (the same method applied to
> capability, this one to user experience).

---

## 1. TL;DR

jichi's UX is the most *theorized* part of the project — five documents of
principles, a message catalog of 29 strings with five catalog files (en and
de complete; es, ja, zh phased at 11 each, English fallback for the rest),
a 490-line accessibility page, a screen-reader-driven arc of twenty milestones, a
repeatable learner-study protocol, and a full desktop-GUI design. What it
does not have, in any recorded form, is **a single observation of a person
who did not build it using it**. The project says so, in writing, in three
places — and the absence is its one definitely-lacking UX feature:

* The project's stated audience is *"self-learners who want to learn software
  development"* (README, *Why this exists*).
* The project's own TUI principles page names its final checklist item —
  *"Have you watched one person use it without coaching them?"* — and
  records, in the same breath: **"the checklist's last item has never been
  recorded as done"** (`docs/TUI_PRINCIPLES.md`, §6).
* The learner study that was designed to fill exactly this hole says, on its
  second line: **"It has not been run"** (`docs/LEARNER_STUDY.md`), and
  promises its report format: *"That is the first time it will have been
  done."*
* The only usability evidence in the entire record is the **operator's own**
  screen-reader sittings of 2026-08-22/23 — which the project itself frames
  as the argument *for* doing the study: *"On the defects that mattered
  there, the analysis's headline is that the test suite found none. A person
  found them"* (`docs/LEARNER_STUDY.md`, §Why).

So the gap is not a missing button, a missing language, or a missing GUI.
It is the missing **evidence**: jichi's interface has been designed,
accessibility-argued, screen-reader-tested by its author, and lints-pinned —
but never once watched in the hands of the people it exists for.

## 2. Why the candidate gaps are *not* this one

For the claim to be "the one definitely lacking," the alternatives have to
be ruled out by the project's own record, the way the capability analysis
ruled out others via the DEFERRED register.

| Candidate UX gap | Status in jichi's own records |
|---|---|
| Desktop GUI / windows | **Recorded, designed, and postponed by decision.** `docs/proposals/2026-09-desktop-gui.md` is a complete design (checked on the wire at M842, 2026-09-29), postponed by the operator 2026-10-01 (`DECISIONS.md`, `dec-2026-10-01-desktop`). |
| Web front end | **Recorded, and deliberately scoped.** `docs/WEB_FRONTEND.md` + the 2026-07 proposal, with the rule *"justified by fan-out and observation, never by chat"*; M164–M165 shipped the enablers (JSON surfaces, heartbeat, stdlib bridge) and nothing more, on purpose. |
| Accessibility | **Not merely present — it is the arc that found the real defects.** Twenty milestones (M551–M570) driven by an actual screen-reader user (Orca), written up in `docs/analysis/2026-08-24-twenty-milestones-by-ear.md`; `docs/ACCESSIBILITY.md`, `docs/SCREEN.md`, an 8-item principle set. |
| Mouse support in the TUI (clickable UI, in-app selection) | Absent from `src/tui/` (verified: the term layer parses only keyboard escape sequences — arrows, function keys, paste markers — not mouse sequences), while the docs already handle the *terminal's own* mouse (the clipboard middle-button trap, `TUTORIAL_BEGINNER.md` §8 → `TMUX.md`, `WORKBENCH.md`). The gap, if any, is unrecorded as a desire — speculating it matters has precisely the evidence problem this document is about. |
| Broader UI localisation (beyond the phased 29-string catalog) | **Recorded and argued:** `docs/LANGUAGE.md` states the two layers (answer language any model knows; UI catalog the high-traffic strings, *phased* — complete in en/de, 11 of 29 in es/ja/zh); the deck/docs localisation program is tracked in DEFERRED (def-21 et seq.). |
| Keyboard model | **Recorded and pinned:** letters *and* digits per decision (M564/M565), *"a slip is not an answer"*, Esc stops and says so (M753), help-vs-registry lint (M751), saved input history (M763). |
| UX docs disagreeing with each other (wrap width 76/78/80; three approval-prompt wordings; stale "still to do") | **Recorded** as an open DEFERRED row (def-42, via `TUI_PRINCIPLES.md` §7), with the settlement mechanism stated: each is a decision for its page's owner. |

Every concrete UX question a reader could ask about jichi has a row, a
decision, or a proposal. The one without an answer is the empirical one:
**what does it actually do in front of the people it was built for?**

## 3. The gap, stated

> **jichi's user experience has never been observed in use by anyone who did
> not build it. All of its usability evidence is first-person (the
> operator), and the instrument designed to produce non-first-person
> evidence — a 3–5 learner, think-aloud, consented study — exists as protocol
> only. For a project whose stated raison d'être is self-learners, the
> learner experience of learning is the one part of the product that is
> unmeasured.**

Two strength levels, as in the companion document:

* **(Strong) Zero recorded observations of external users, ever.** No
  learner-study note in the mandated format exists in `docs/analysis/`; the
  CHANGELOG never marks the study run; the principles page says the
  checklist item *"has never been recorded as done."*
* **(Weak, but the one with teeth) The project's UX decisions are currently
  arbitrated by argument and lints, not evidence — and it knows this.** The
  five documented doc-vs-doc-vs-code disagreements (def-42) are exactly the
  kind a watched session resolves in one sitting; the desktop-GUI proposal
  (§7) asks *"for a given user, whether the application or the terminal
  workbench serves them better"* — a question the design page candidly
  cannot answer, because the users in question have never been asked.

## 4. Evidence

### 4.1 The project's own statement, three times

1. `docs/TUI_PRINCIPLES.md` §6 (Testing an interface):
   > *"Watch one person use it, without coaching. **The checklist's last
   > item has never been recorded as done.** `LEARNER_STUDY.md` is how to do
   > it."*

2. `docs/LEARNER_STUDY.md`, second line of the document:
   > *"It has not been run. This page is how to run it, so that the first
   > run and the tenth are the same study."*
   and, in its close-out steps:
   > *"Tick the checklist item in INTERFACE_TUTORIAL.md with a link to the
   > note. **That is the first time it will have been done.**"*

3. `docs/plans/2026-09-24-twelve-questions.md`, item 8's completion note
   (the decision that commissioned both pages):
   > *"built — M754 (the HCI section), M756 (the principles page, **the study
   > protocol; the study itself not yet run**)…"*

Three independent pages, written at three different milestones, agreeing.

### 4.2 The study's mandated output does not exist

`LEARNER_STUDY.md` specifies the report: a dated note in `docs/analysis/`,
participants as P1–P5, the build, problems by severity 0–4, and for each
problem the fixing milestone or DEFERRED row. Searching `docs/analysis/` for
that format finds no learner-study note. (The one file matching "P1…P5" is
`2026-09-27-preserve-agent-work-review.md`, a different review format.)
Searching the CHANGELOG for "learner study" finds no run entry.

**Flagged inference:** this proves no study was *recorded* in the project's
own mandated location. It cannot prove one was never run and simply not
written up — though the three pages in §4.1 would then themselves be false,
and this project's own register exists to make exactly that kind of drift a
defect.

### 4.3 The only usability evidence is the operator's own ears

`docs/analysis/2026-08-24-twenty-milestones-by-ear.md` attributes the M551–
M570 defects row by row, and the attribution column reads, over and over:
*"operator, listening"*, *"me — reading Orca's config"*, *"operator, running
the shipped build"*. Not one row says *a learner found this*. The analysis's
own headline:

> *"Of the defects that mattered, the test suite found none."* — found by
> the operator, not by the audience.

And `LEARNER_STUDY.md` converts that into the study's reason to exist:

> *"The only usability evidence jichi has is the operator's own screen-reader
> sittings of 2026-08-22/23. A person found them. That is the argument for
> doing it again, with people who did not build the program."*

Note what the author is arguing: even a defect found *by a real assistive
technology* was found by the **author's** use of it — with the author's
knowledge of what they had just changed, their Orca configuration they'd been
reading for a week, and their stake in it. The same page is self-aware about
the distortion: *"If the facilitator is also jichi's author, write that
down. The evaluator effect (Hertzum & Jacobsen) is larger when the evaluator
has a stake."* That is the highest form of the problem acknowledged in the
document — and it applies to **all** the UX evidence to date, including
screen-reader work, because the listener has been one person throughout.

### 4.4 The study protocol is complete and cheap by design

This matters for the "definitely lacking" claim: the gap is not one that
needs invention, it needs execution. `LEARNER_STUDY.md` already contains the
consent script, the five task cards (build it / ask it something / approve an
edit / undo it / report the platform), the think-aloud rules (with the
Ericsson & Simon rationale: never ask "why"), the slip/mistake
classification, the severity scale 0–4, and the reproducibility contract
(*"the same five cards, the same script and the same record sheet make two
runs comparable"*). It names its likely population (*"A JLU course is the
likely source, and that is the operator's to arrange"*). Written
2026-09-25 (M756); the tree is now at M898 — the protocol is roughly a
hundred and forty milestones old and still unexecuted, so any run would
measure an interface the cards were written against but that has since
changed.

### 4.5 The interface has been argued about, in documented disagreement

`TUI_PRINCIPLES.md` §7 (recorded as def-42) lists five places where the five
UX documents disagree with each other or with the code: the accessible reply
header (three descriptions, code prints a fourth), the spoken approval prompt
(*"three wordings; the catalog matches none"*), the wrap width (76 vs 78 vs
80), a stale "still to do" list, and smaller drift. Each is parked as
*"a decision for whoever owns the page it is in."* But the missing common
denominator is visible: none of the five can be settled from the documents
any more than they already have been, because **there is no observational
datum in the tree that speaks to any of them**. A watched learner picking
up the approval prompt, or failing to find `/undo` (task 4 exists for a
reason: *"How they find `/undo`, or do not, is the finding"*), would
adjudicate more of the list in an afternoon than they have been in
weeks of documented argument (the five disagreements were all found in
one reading on 2026-09-25, and remain unresolved at `1b71247d`).

### 4.6 The downstream decisions that are waiting on this evidence

* **The desktop GUI** (postponed 2026-10-01): its own design page's §7 asks
  which users the GUI serves better than the terminal workbench, and item 11
  concedes *"for someone who lives in the terminal, the workbench is richer
  today."* Both answers are the designer's judgement about imagined users.
  The study is the instrument the postponement's re-visit would need.
* **The web front-end rule** (*"justified by fan-out and observation, never
  by chat"*) is scoped down today by the absence of any observation of
  what chat users actually do.
* **The i18n of the UI layer**: 29 strings, five catalog files, and the
  catalog is *phased* — en and de are complete (29/29), while es, ja and zh
  carry 11 each, the untranslated rest falling back to English by design
  (`docs/LANGUAGE.md`'s stated policy; the raw-catalog API exists precisely
  so completeness "can only be checked" there). But *why* those 29, and why
  those 11 first? Pinned by lints (`i18n_tracks_lint` et al.) — the page says
  *"the highest-traffic interactive strings"*, a claim currently supported
  by the author's traffic, not measured learner traffic. (The answer-language
  layer has no such problem: it is the model's output, in whatever language
  the learner chooses.)

## 5. Implications

Ordered, each tied to a stated project value:

1. **The core product promise is unverified.** The README's first paragraph
   of purpose is *"jichi is built for self-learners who want to learn
   software development."* Every other claim about that audience — the
   curriculum (90 graded tasks, red-first provers), the reading guides, the
   onboarding pack — is verified mechanically, because tasks can be graded.
   The one claim that cannot be graded by CI is *"a self-learner can
   actually use this"* — and it is the only major product claim in the tree
   with zero recorded observations behind it. A project that lints its own
   documentation's numbers has left its defining promise as a faith claim.

2. **First-person evidence has already proven to miss things — and the
   project measured that.** The screen-reader arc found real defects (M551–
   M570) precisely because *the test suite found none*; but the defects
   found were the ones an experienced author's own ears catch while
   re-listening to things they just wrote. The study protocol's slip/mistake
   distinction (Reason) predicts the class the author cannot produce
   on demand: the *mistake* — the wrong intention carried out correctly,
   e.g. *"looking for undo in the config"* (the protocol's own example).
   An author does not make the users' mistakes; that is the entire
   epistemic content of watching.

3. **Decisions are being made, and postponed, on argument where evidence
   would decide.** The desktop GUI is postponed; the web front-end is
   ruled-out; the five doc-vs-doc disagreements (def-42) are parked; the
   wrap width has three candidate numbers. Each is a legitimate decision
   *today* — but each is the same species: the operator's reasoning about
   imagined users, with the one cheap instrument that would produce real
   users' behavior (3–5 learners, an afternoon each, a JLU course away)
   available and unused. The DEFERRED register's own M326b rule — *"check
   the checkable part of a reason before parking the item"* — applies: the
   checkable part of "learners need X / don't need Y" is asking learners.

4. **The accessibility program's blind spot is the same person.** Access
   work driven by one user's Orca configuration is real access evidence —
   and it is also the least generalizable kind, since assistive technology
   is heavily configured and idiosyncratic. The protocol anticipated this
   (new participants, repeated after large interface changes), which is why
   the *absence of the study* weakens even the strongest part of jichi's UX
   record.

5. **The protocol is aging, and that is a live cost, not a footnote.**
   Written 2026-09-25 at M756; the tree is M898. Every interface change
   since then (Esc/stop, `/why`, `/fences`, saved history, the mode-narrowing
   rule M845, the saved-mode ACP incident) is unmeasured by the instrument
   that was written to measure the interface. The study's own
   reproducibility clause (*"repeated after large interface changes, with
   new participants"*) means the debt is compounding: the first run will
   have to adjudicate a hundred and forty milestones of unobserved change
   at once.

6. **Meta-implication, the same class as the capability analysis.** jichi's
   method is to make every absence findable (the DEFERRED register) and
   every claim checkable (the lints). UX is the one domain where the
   method's output — arguments, principles, catalogs, protocols — is
   currently standing in for the method's input — observation. Everything
   else in the project is designed to refuse exactly that substitution.

## 6. Recommendation

1. **Run the study. As written.** It needs no design: consent script,
   cards, scale, and report format are done, and deliberately minimal. The
   only precondition is the operator's to arrange — a JLU course — which
   the protocol names. This is the one recommendation in this document
   with zero implementation cost and the whole difference between first-
   person and observed evidence.
2. **Run it *before* the desktop-GUI revisits, not after.** The GUI
   proposal's open questions (§7) are the study's natural task 6–10.
   Postponement was the right call on current evidence; the evidence the
   call was waiting for is cheaper to produce than the GUI.
3. **Point its output at def-42.** The five parked doc-vs-doc-vs-code
   disagreements each get their settlement datum from a watched session;
   the register's own rule then removes the resolved lines, in the same
   change (as `TUI_PRINCIPLES.md` §7 instructs).
4. **Tick the checklist item with the note's link**, per the protocol's
   step 5 — so that the next "is there a user study?" can be answered from
   `docs/` instead of from a grep that finds the protocol and the absence.

Rejected, in the register's spirit:

* *Surveys/questionnaires instead* — the protocol's own reasoning rules
  them out: self-report of an interface you have not been asked to think
  through is not the slip/mistake data, and "3–5 self-learners" was chosen
  precisely because it *finds* problems without claiming rates.
* *Instrumentation-only (telemetry from learner machines)* — the tree
  already has telemetry, and the `2026-09-23` analysis shows it answering
  only questions the author thought to ask; it cannot produce the verbatim
  quote column or the "how they find /undo, or do not" finding, and it
  carries a privacy proposition the interactive study does not.
* *Building the GUI first, then watching* — inverts the order; the GUI
  is a larger bet on unobserved needs.

## 7. Limitations and honest counterarguments

1. **This is an argument from the record, and the record could be
   incomplete.** I verified the study's *mandated* output does not exist
   and the three pages state it was not run; I cannot verify what never
   left a page. The project's own integrity doctrine (M657: "a walk that
   claimed to have verified … is the overclaim this register was built to
   prevent") is the reason I am saying this instead of asserting it.
2. **The operator's screen-reader arc is strong evidence — I am not
   claiming it is weak. (Three arithmetic corrections are folded into the
   draft, per the document's own rules, each caught by re-verification: the
   protocol is a hundred and forty milestones old, not "roughly forty";
   the UI catalog carries 29 strings, not the 31 first reported — 31 is the
   number of *distinct* `JC_MSG_*` names in `include/jc_msg.h`, which is two
   more than the 29 message ids because it also counts the sentinel
   `JC_MSG__COUNT` and the include-guard macro `JC_MSG_H`; the reliable
   number is 29, the count of ids in the `jc_msg_id` enum and of entries in
   a complete catalog (all reproducible, §8). The first draft's *explanation*
   of the over-count was itself wrong (it blamed the sentinel alone, which
   accounts for only one of the two), and a line-count `grep -c` returns 34,
   a third number; the "five languages" framing overstates — the catalogs
   are *phased*, 29/29 in en and de, 11 in es, ja and zh. All of it is the M326b
   class: checkable arithmetic parked unchecked, including by this
   document's own first pass.)** It found defects the suite missed and produced
   twenty milestones. The claim is narrower and structural: it is
   *first-person* evidence, by the same person, and the protocol (written
   by that person, citing the evaluator-effect literature) says so is the
   limit. No user-external row appears in its attribution table.
3. **A skeptic can say "the audience is the author's learners; one careful
   ear with Orca is a learner too."** Partially fair — which is why the
   gap statement in §3 keeps the word *observed*: the defect-finding
   capacity of a non-author listener is exactly what the project's own
   words argue for ("people who did not build the program"), and what its
   checklist has never marked done.
4. **"Definitely lacking" is graded.** Mouse support, broader UI
   localisation, and GUI surfaces are also absent; they are ruled out as
   *the* answer by the table in §2 (recorded decisions or unrecorded
   speculation). This one stands as the largest *evidenced-and-unfilled*
   hole: the project documented the hole, designed the filling instrument,
   and has not used it — for roughly a hundred and forty milestones (M756
   to M898), with a stated population one course-arrangement away.
5. **Not executed.** I watched no learner myself and ran no protocol; the
   analysis reads the tree as it stands at HEAD `1b71247d`. The strongest
   form of this finding is a run, not a document — this document is the
   case that the run is the cheapest high-value action the record
   supports.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the three self-statements
grep -n "never been recorded as done" docs/TUI_PRINCIPLES.md
sed -n '2,4p'   docs/LEARNER_STUDY.md        # "It has not been run."
grep -n "the study protocol; the study itself not yet run" docs/plans/2026-09-24-twelve-questions.md

# the mandated study output does not exist
ls docs/analysis/ | grep -iE "learner|study|participant"
grep -rin "learner study" CHANGELOG.md docs/CHANGELOG.md 2>/dev/null

# the only usability evidence, and its attribution
grep -n "surfaced by" -A 25 docs/analysis/2026-08-24-twenty-milestones-by-ear.md | head -30
grep -n "the test suite found none" docs/analysis/2026-08-24-twenty-milestones-by-ear.md

# the UI message catalog: 29 ids; phased (en/de complete, es/ja/zh at 11)
# RELIABLE number = 29: the ids in the jc_msg_id enum, = entries in a
# complete catalog. Naive header greps over-count: 31 distinct JC_MSG_*
# names (29 + the sentinel JC_MSG__COUNT + the include-guard macro
# JC_MSG_H), and a line-count grep -c returns 34. Quote-only array counts
# under-count the en catalog because several entries are parenthesized
# multi-line literals.
awk '/^enum jc_msg_id \{/,/^\};/' include/jc_msg.h | grep -oE 'JC_MSG_[A-Z0-9_]+' | grep -v '__COUNT' | sort -u | wc -l   # 29
grep -oE 'JC_MSG_[A-Z0-9_]+' include/jc_msg.h | sort -u | wc -l   # 31 = 29 + __COUNT + JC_MSG_H
grep -c 'JC_MSG_' include/jc_msg.h   # 34 lines (include-guard + comment mentions)
for l in en de es ja zh; do printf '%s: ' $l; sed -n "/^static const char \*const msg_$l/,/^};/p" src/util/jc_msg.c | grep -cE '^    "|^    \('; done   # en 29, de 29, es/ja/zh 11

# the five documented UX disagreements
sed -n '/^## 7. Where the five documents disagree/,/^$/p' docs/TUI_PRINCIPLES.md
grep -n "def-42" docs/DEFERRED.md

# the GUI: designed, postponed
sed -n '1,6p' docs/proposals/2026-09-desktop-gui.md
grep -n "2026-10-01-desktop" docs/DECISIONS.md

# the checklist item the study exists to tick
sed -n '376,390p' docs/INTERFACE_TUTORIAL.md
```

**Verdict line for the register this could seed:** *jichi's UX has no
recorded observation by anyone who did not build it: the learner-study
protocol (LEARNER_STUDY.md, M756, 2026-09-25) has not been run at
1b71247d (M898+); TUI_PRINCIPLES.md §6 says the checklist's last item "has
never been recorded as done"; the only usability evidence is the operator's
own screen-reader sittings (analysis/2026-08-24-twenty-milestones-by-ear.md,
no user-external attribution row). Blocking downstream: the desktop-GUI
revisit (postponed 2026-10-01), def-42's five doc-vs-doc-vs-code
disagreements, and the "built for self-learners" core claim. Revisit when:
the first study note lands in docs/analysis/ — then tick INTERFACE_TUTORIAL's
checklist item in the same change.*
