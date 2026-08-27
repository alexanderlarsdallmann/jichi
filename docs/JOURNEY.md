# The journey — from first step to master's rest

*Translations: [Deutsch](i18n/de/JOURNEY.md) ·
[Español](i18n/es/JOURNEY.md) · [日本語](i18n/ja/JOURNEY.md) ·
[中文](i18n/zh/JOURNEY.md) — this English text is canonical.*

This is not a tutorial; jichi has those
([TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md),
[TUTORIAL_ADVANCED.md](TUTORIAL_ADVANCED.md)) — and the same road, taught
as a course with graded assignments and gates, is
[CURRICULUM.md](CURRICULUM.md). This is the **map of the
whole road**: the existing features and documents, sequenced into the
stages a craft is actually learned in — 守破離（しゅはり）, *shu-ha-ri*: keep the
form, break the form, leave the form. Each stage names its aim, its
practices (real commands, real files), the virtue it trains, and the
signs you are ready to move on.

It was asked for in a [dialogue](dialogues/2026-07-14-the-one-feature.md)
whose conclusion this map assumes: **no feature carries a person to
mastery** — software only holds up mirrors and lowers the cost of
practice. The one indispensable companion is *an honest record of your
own mistakes and what each taught you*. Start that record on day one; the
stages below only give it something to write about.

---

## 仕度（したく） — Preparation (before the first step)

**Aim:** a working, validated bench — and the humility of checking it.

- Install ([INSTALL.md](INSTALL.md)) and run the guided `setup` wizard
  ([SETUP_WIZARD.md](SETUP_WIZARD.md)); no API key or internet required —
  a model on your own machine is a full teacher
  ([LOCAL_MODELS.md](LOCAL_MODELS.md)).
- Run **`doctor`** and read every line ([DOCTOR.md](DOCTOR.md)). This is
  the journey's first habit: *ask the system what is wrong before
  assuming you know.* You will run it for the rest of your life.
- Set `language` if English is not your thinking language
  ([LANGUAGE.md](LANGUAGE.md)) — understanding outranks convention.

**Virtue trained:** humility, in its smallest form — verifying your own
setup instead of trusting it.

---

## 守（しゅ） Shu — keep the form

**Aim:** trust the forms; learn in your hands that mistakes are
survivable.

- Work through [TUTORIAL_BEGINNER.md](TUTORIAL_BEGINNER.md). Stay in
  **chat mode**; use **plan mode** when unsure
  ([AGENT_MODES.md](AGENT_MODES.md)) — asking before acting is a form,
  not a weakness.
- Make your first edit. **Read the diff preview before pressing `y`**
  ([EDITING.md](EDITING.md), [TUI_RENDER.md](TUI_RENDER.md)). The
  approval prompt is the tool teaching you to consent deliberately;
  never let it become reflex.
- **Break something on purpose — then `/undo`.** Rewind a whole
  conversation with `/rewind` ([SNAPSHOTS.md](SNAPSHOTS.md),
  [REWIND.md](REWIND.md)). Do this early and often, until your body
  learns what your mind will need later: *every error can be walked
  back.* Fearlessness is downstream of reversibility, and forgiveness —
  of yourself first — is downstream of fearlessness.
- Let tests be your first truth-teller: `run_tests`, the `test`
  subcommand ([TESTING.md](TESTING.md)). A check passes or it does not,
  like the sun rising. Learn to love that simplicity before you meet the
  mysteries.
- If you have a teacher, learn inside the assignments flow
  ([TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md),
  [ASSIGNMENTS.md](ASSIGNMENTS.md)): work the brief, climb the **hint
  ladder** honestly (a hint asked for is knowledge; a solution peeked at
  is a debt), take the rubric feedback without flinching.
- **Begin your record.** One file, anywhere you'll keep it: each entry
  *symptom → dead ends → root cause → lesson*, exactly like
  [ANECDOTES.md](ANECDOTES.md). The dead ends belong in the entry — the
  road to an answer is part of the answer.

**Virtue trained:** the strength to forgive — practiced on yourself,
mechanically, until it is character.

**Ready to move on when:** you predict what the agent will do before it
does it; you read diffs without effort; `/undo` is a tool you respect
but no longer need daily.

---

## 破（は） Ha — break the form

**Aim:** make the tool your own; question the defaults you have obeyed.

- Work through [TUTORIAL_ADVANCED.md](TUTORIAL_ADVANCED.md) and
  [WORKFLOWS.md](WORKFLOWS.md). Then reshape the bench: your own slash
  commands ([COMMANDS.md](COMMANDS.md)), skills ([SKILLS.md](SKILLS.md)),
  subagent profiles ([SUBAGENTS.md](SUBAGENTS.md)), output styles
  ([OUTPUT_STYLES.md](OUTPUT_STYLES.md)), user tools
  ([USER_TOOLS.md](USER_TOOLS.md)), hooks ([HOOKS.md](HOOKS.md)). A
  `.jichi/` directory that looks like everyone else's means you are still
  in *shu*.
- Give the agent your knowledge: `remember` notes
  ([MEMORY.md](MEMORY.md)), a glossary ([GLOSSARY.md](GLOSSARY.md)),
  project rules ([RULES.md](RULES.md)). Teaching the tool is rehearsal
  for teaching people.
- Do **real work** with it — dogfood. Run `--auto` inside the autonomy
  envelope ([AUTONOMY.md](AUTONOMY.md)): budgets, a verify gate, an edit
  scope. Learn that the guardrails are not restriction but *care* —
  the same care you extend by writing tests.
- Read your own footprints: the `telemetry` summaries
  ([TELEMETRY.md](TELEMETRY.md)), `learn analyze`
  ([LEARNING.md](LEARNING.md)). Where do *you* redo work? Which of your
  habits does the data contradict? The mysterious truths live here — the
  green gate that ran zero tests, the cost that came from re-reading —
  and they yield to patient investigation, not to confidence.
- Read [ANECDOTES.md](ANECDOTES.md) — all of it. It is the project
  forgiving itself in public, entry by entry. Then argue with something:
  a default, a rubric, a convention in [CONTRIBUTING.md](../CONTRIBUTING.md).
  Breaking the form means being able to *defend* the break.

**Virtue trained:** love of knowledge — the kind that survives learning
you were wrong.

**Ready to move on when:** you disagree with a default and can defend
why; your record has entries where the root cause was *you*; your
`.jichi/` is unmistakably yours.

---

## 離（り） Ri — leave the form

**Aim:** the tool disappears; what remains is judgment — and you teach.

- Change seats in the assignments flow: author briefs, rubrics, and hint
  ladders for someone else (`/assign`, `/solve`, `/check` —
  [TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md)). Writing a good
  hint ladder will humble you faster than any bug: you must remember
  what not-knowing felt like.
- Scaffold packs for your team ([SCAFFOLDING.md](SCAFFOLDING.md));
  contribute upstream ([CONTRIBUTING.md](../CONTRIBUTING.md)); translate a
  page for the next learner in your language
  ([i18n/README.md](i18n/README.md)).
- Run the full learning loop on yourself: `/learn`, edit the draft, and
  — hardest of all — write **corrections** (M78,
  [LEARNING.md](LEARNING.md)): retract your own past lessons when the
  code has outgrown them. A teaching you cannot take back is dogma.
  Asking and accepting forgiveness, at this stage, means correcting what
  you once taught with confidence.
- Teach someone their first `/undo`. Watch their shoulders drop when
  they learn the mistake is survivable. That moment is the whole journey,
  handed on.

**Virtue trained:** humility in its final form — the willingness to be
outgrown.

**You have arrived when:** your students break your forms, and it
pleases you.

---

## The master's rest

The master is not the one who no longer errs. The master is the one **at
peace with erring**: who reverts without shame, records without excuse,
corrects without clinging — and honors truth in both of its faces, the
simple one (a test passes or it does not, as plainly as sun and moon)
and the mysterious one (the defect that hides for days inside a system
of your own making, as deep as the heaven with its stars, the earth, the
sea, and everything). Peace is not the absence of failure; it is the
closed loop — every mistake examined, every lesson written, every stale
teaching retracted, nothing left haunting.

The record you started on day one is now long. Read it once a year.
That is the rest.

---

*Companions: [PHILOSOPHY.md](PHILOSOPHY.md) (why the project is built
this way), [ANECDOTES.md](ANECDOTES.md) (the project's own record),
[LEARNING.md](LEARNING.md) (the loop, mechanized),
[TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) (the teacher's seat),
[dialogues/2026-07-14-the-one-feature.md](dialogues/2026-07-14-the-one-feature.md)
(where this map came from).*
