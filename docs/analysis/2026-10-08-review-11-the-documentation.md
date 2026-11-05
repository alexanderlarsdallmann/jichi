# Review 11 — The documentation: how true it is, and how well it teaches

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on two reports
that were **paused** at the operator's request: one on the learner pages, one on
the design pages and the registers. Each had read part of its slice, and every
finding below was checked against the source by its reviewer. §6 lists what
neither reached. Written by Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner who reads jichi's documentation to learn, and for anyone
who writes or maintains it. The documentation is a large part of what jichi is:
1,111 files and 13.4 MB in `docs/`, shipped in full with the public release
because "jichi's complete, honest project documentation … is a feature".

**After reading it you will be able to:**

- find your way through the documentation by genre, and know which genre answers
  which question;
- recognise the ways a documentation page goes false, each with a real example;
- know which pages the reviewers found the best to learn from, and why;
- judge a claim in a page before you rely on it.

---

## 1. The documentation as a system

**The genres, and the question each answers:**

| Genre | Where | Answers |
|---|---|---|
| the router | [`docs/README.md`](../README.md) | where do I start, and in what order? |
| guides and tutorials | `docs/*.md` (TUTORIAL_BEGINNER, CONFIG_TUTORIAL …) | how do I do this? |
| references | `docs/*.md` (COMPACTION, AUTONOMY, SCRIPTING …) | how exactly does this work? |
| reading guides | [`docs/reading/`](../reading/) | how do I read the code? (Annai, Fukabori, Tsuiseki, Kiroku) |
| registers | ROADMAP, DECISIONS, DEFERRED, ANECDOTES | what happened, what was chosen, what was not done, what went wrong |
| analyses | [`docs/analysis/`](.) | one measured investigation each, dated |
| proposals and plans | [`docs/proposals/`](../proposals/), [`docs/plans/`](../plans/) | what should be built, and how |

**Who reads which genre:**

- **To use jichi,** start at the router.
- **To learn the code,** start with the reading guides.
- **To find out why something is the way it is,** search the registers by
  milestone number: [Kiroku](../reading/KIROKU.md) teaches how.

**The house rules** for writing are in `DOC_REVIEW.md` §7. Each page should
open with who it is for, what you will be able to do, what you need first and
where it fits. Every term is defined at first use, every rule names the incident
behind it, measured and expected are kept apart, and a correction stays visible
beside what it corrected.

**Lints keep claims true.** Flags, config keys, commands, telemetry events,
counts, line locators and placeholders are all checked by smoke drivers. Every
chapter of this review passed nineteen of them before it was committed.

---

## 2. What the reviewers checked mechanically

The learner-pages reviewer ran mechanical checks over 65 core pages, each
checker first proven able to fire on a planted defect:

- **Links and anchors: 1,011 checked, 0 broken.**
- **Symbols in code spans** (`jc_*`, `JC_*`, function names): 0 unresolved. Five
  unresolved *paths* name files in the learner's own project, as intended.
- **C excerpts outside the reading guides, compared line by line with `src/`:**
  one has drifted (§3, finding 2.9).
- **The four programs in ALGORITHMS.md were compiled and run exactly as
  published,** offline:
  - all four compile cleanly, even without the `make` the page asks for first;
  - two reproduce the printed output byte for byte;
  - two differ only where the tree has grown since, as the page itself predicts.

**What this shows.** The documentation's *mechanics* are sound. Its weaknesses
are in *meaning*: what a sentence claims about behaviour. No link checker can see
that.

---

## 3. What the reviews found

Status is the reviewers'; each was checked against the source.

### 3.1 Safety nets described as more universal than they are

This is the most important pattern, because a learner relies on a safety net
exactly when something has already gone wrong.

- **"Indefinitely without overflowing context"** (learner 2.3, medium). The front
  page (`README.md:1063-1066`) and the first lines of COMPACTION.md promise that
  compaction lets a session run "indefinitely without overflowing context".
  - The summary runs only between turns, and only at the top level.
  - Within one turn, only elision bounds the prompt, and the project's own
    measurement says it is no guarantee.

  [Review 2](2026-10-08-review-02-when-the-window-fills.md) measured the same gap
  independently: 0 summaries in 366 headless runs.
- **"/undo always gets you back"** (learner 2.6, medium). The beginner page
  states the checkpoint net without its limits. It covers only what a checkpoint
  captured, and only in the workspace.
- **"Sessions are saved every turn"** (design F12, low). True, and it understates
  the risk for a run that is one long turn: a SIGKILL loses all of it
  ([Review 5](2026-10-08-review-05-sessions-memory-control.md) F6).

**The rule this breaks** is the house rule "limits are stated where the reader
will meet them". The fix is a sentence each, on the first screen.

### 3.2 Pages that contradict each other

- **A supervisor that follows AUTONOMOUS_LOOPS.md files an unfinished run as
  done** (design F1, medium).
  - **What AUTONOMOUS_LOOPS.md and QUEUE_PROTOCOL.md say:** route on the exit
    code, and exit 0 means done. The shipped `loop.sh` moves exit 0 to `done/`.
  - **What SCRIPTING.md says:** a run that hit its iteration cap exits **0** with
    `stop_reason: max_iters`, and warns *"Do not branch on `$?` to detect this"*.
  - **The result:** a queued task capped in mid-work is filed as done and never
    resumed. Experiment A in Review 2 is the same failure with a different stop
    reason.
- **The beginner tutorial makes "the API key line shows a tick" mandatory**
  (learner 2.2, high for a lone learner). The recommended path, a free local
  model, needs no key, so it can never produce that tick. A learner following the
  free path meets a hard stop on the first page.
- **The entry pages give three different pictures of the supported platforms**
  (learner 2.1, high). A Mac user is sent to pages that say macOS was "never
  compiled".
- **The front page and the install page still teach "first match wins"** for
  configuration files, where the loader *merges* them (learner 2.2b,
  medium-high).
- **Three routers give three different first steps** (learner 2.4, medium), and
  the beginner's source guide is missing from the docs map's learning list.
- **SUBAGENTS.md contradicts itself** on what a delegation returns, and does not
  mention M911's cap (design F5).

### 3.3 Pages that fell behind the code

Each of these was true when written. A milestone changed the code and not the
page:

- **COMPACTION.md says the `[context]` note fires once per loop** (design F4).
  Since M900 it fires on every pressed round.
- **COMPACTION.md says elided arguments cannot be recovered** (design F3). Since
  M909 and M912 they usually can.
- **COMPACTION.md calls the claim tickets "turn-lived"** (design F2). The store
  lives for the process.
- **COMPACTION.md names constants that do not exist,** and miscounts the prompt
  sections (design F6).
- **AUTONOMOUS_LOOPS.md quotes code that has moved on** (design F8), and one
  "whole feed loop" excerpt in a learner page has drifted from the source
  (learner 2.9).
- **Two tutorials teach a structure the tree no longer has** (learner 2.7b), and
  the state-machine tutorial's SSE example predates M775 (2.7c).
- **The approval prompt shown on the front page** is not the one the build prints
  (learner 2.5).
- **The one telemetry key the beginner page shows** is in a shape the loader
  ignores (learner 2.6b): a copyable config that silently does nothing.
- **A register entry was left open** when its index row was closed (design F9),
  and a proposal's status line was not updated after its work shipped (F11).

**Why this happens, and what helps.** A page about one subsystem goes stale when
milestones change that subsystem and the page has no check of its own. The
project already knows the answer: *a lint finds forever what an audit finds
once*. def-319 records the plan to extend the documentation lints to top-level
design pages. The learner reviewer adds that lint coverage stops short of most
learner tutorials (2.8).

### 3.4 Teaching pages whose thesis a learner can refute

- **"No linked list. No tree."** (learner 2.6d, medium). DATA_STRUCTURES.md says
  jichi uses neither, but the JSON parser's nodes are linked lists, and a parsed
  document is a tree. A learner can refute the page's thesis in one file
  (`src/json/cJSON.c`). The fix is a sentence: *jichi's own code allocates no
  general-purpose linked list or tree; the JSON tree is the exception, and here
  is why it is one*.
- **The beginner never learns what the percentage in the prompt is**
  (learner 2.6c, low). It is the context gauge, and the most useful number a
  learner has for the problem in Review 2.

---

## 4. The best passages to learn from

Both reviewers were asked to name the passages a learner should read first. Their
lists, merged:

| Passage | What it teaches |
|---|---|
| `INSTALL.md:399-413`, "Two directories, two purposes" | build in one place, run in another, with the why |
| `INSTALL.md:415-484`, "Which `jichi` am I actually running?" | a debugging method: symptom, cause, one-line diagnosis, fix |
| `LOCAL_MODELS.md:171-218`, "Five traps, all measured" | check your instrument before you believe it |
| `CHOOSING_A_MODEL.md:66-93` and `:262-285` | "code fails loudly, prose fails silently"; a retracted theory kept beside the correction |
| `TUTORIAL_BEGINNER.md:310-333`, "When a turn stops before it is finished" | the exact warning, "nothing broke", and the next action |
| `PSEUDOCODE_TUTORIAL.md:45-59` | pseudocode as a list of decisions a reviewer can catch |
| `DATA_STRUCTURES.md:97-136` | a claim, the measurement that refuted it, both kept |
| `ALGORITHMS.md:305-402`, the SSE lesson | state a property, test it at every chunk size, report what broke |
| `analysis/2026-09-22-compaction-decided.md:137-159`, "The worst turn, named" | a symptom versus the disease: the passes were the thermometer |
| `COMPACTION.md:559-579`, "Why the note is a directive" | text a model reads is an interface |
| `COMPACTION.md:923-942`, why a lower threshold does not help | measure before tuning; the lever is upstream |
| `AUTONOMOUS_LOOPS.md:165-183`, "done is not a success verdict" | read the fields that answer the question |
| `SUBAGENTS.md:68-74`, "It starts with a fresh context" | what a fresh agent does not get; the design constraint of any handoff |

**What they share is the house honesty rule, demonstrated:**

- a correction is kept next to what it corrected;
- an instrument is checked before its reading is trusted;
- a rule is shown with the incident that produced it.

That is the most valuable thing jichi's documentation teaches, beyond jichi.

---

## 5. What to fix first

1. **The safety-net sentences (§3.1).** One sentence each, on the first screen of
   README.md, COMPACTION.md and TUTORIAL_BEGINNER.md. Cheapest, and highest
   value.
2. **The supervisor contradiction (design F1).**
   - Make `loop.sh` and the C supervisor read `stop_reason` when `jq` is present,
     and requeue `max_iters`.
   - Add the missing rows to the exit-code tables of AUTONOMOUS_LOOPS.md and
     QUEUE_PROTOCOL.md.
   - *The proof:* extend `tests/smoke/supervisor.sh` with a mock that never
     answers, and assert the task is not in `done/`.
3. **The beginner's first page (learner 2.1, 2.2, 2.2b).** A learner who stops on
   page one never reaches the rest.
4. **COMPACTION.md's stale sections (§3.3).** Then the def-319 lints, so the next
   milestone cannot leave them stale again.
5. **The relay's documentation, when it ships:** the pages of §8 below.

---

## 6. What was not reviewed

**The learner-pages report stopped before:**

- CONFIG_TUTORIAL, VOCABULARY, GLOSSARY, CURRICULUM and `docs/curriculum/`;
- LEARNING and TESTING_TUTORIAL;
- **all 66 files of `docs/reading/`**;
- the samples of `docs/assignments/`, `docs/i18n/` and `docs/case-studies/`.

(Chapters 1–10 of this review link into the reading guides, and the Annai and
Fukabori chapters they cite were read for those chapters; that is not a review of
them.)

**The design-pages report read** COMPACTION.md, AUTONOMOUS_LOOPS.md,
SUBAGENTS.md, the run-handoff, sustained-task and work-preservation proposals,
and the two compaction analyses in full, and parts of SCRIPTING, DEFERRED,
DECISIONS and ROADMAP.

**It did not finish:**

- ARCHITECTURE.md, DECISIONS.md in full, AUTONOMY.md, PARALLEL.md;
- DRIVING_A_TEAM.md, DRIVING.md, CONTROL.md, DAEMON.md;
- MEMORY.md, STATE.md, OBSERVABILITY.md, TELEMETRY.md, TOOL_OUTPUT_COST.md;
- DEPLOYMENT.md, QUEUE_PROTOCOL.md, SUPERVISOR_OF_MANY.md,
  AGENT_COLLABORATION.md, ROUTING.md, PROMPT_CACHING.md;
- the targeted passes over DEFERRED, ROADMAP and ANECDOTES.

Its sub-readers' partial drafts on DECISIONS, the state and observability pages
and the registers are kept with the internal records.

---

## 7. Lessons for anyone who writes documentation

1. **A safety net described without its limits is a trap,** because it is read
   at the worst moment. State the limit where the reader meets the net.
2. **Two pages that answer the same question will disagree eventually.** Give
   each question one owner page, and link to it from the others.
3. **A sentence about behaviour is a claim about code.** Date it, check it with
   a lint where you can, and expect it to go stale when the code moves.
4. **Keep the corrections visible.** The best passages in §4 are good *because*
   they show what was wrong.

---

## 8. Where the context relay will need explaining

The design-pages reviewer collected the existing decisions a relay must respect
or overturn, and the [proposal](../proposals/2026-10-context-relay.md) §6.0 uses
them. When the relay ships, these pages must change in the same milestone:

| Page | Why |
|---|---|
| README.md and COMPACTION.md, first screen | the "indefinitely" sentence (§3.1) and what the relay adds to it |
| `reading/annai-08-when-the-conversation-gets-too-long.md` | the beginner's chapter on compaction stops before its limits |
| `reading/fukabori-05-context-economics.md` | a third regime: hand over, besides summarize and elide |
| AUTONOMOUS_LOOPS.md, QUEUE_PROTOCOL.md, SCRIPTING.md | the new stop reasons and exit 75 (proposal §6.14) |
| SUBAGENTS.md, PARALLEL.md | a child's legs, and what the parent sees |
| CONTROL.md | the `relay` verb and the new `status` fields |

---

## 9. Something to do

**Without a bench (the read-only twin):**

1. Open `README.md` at line 1063 and COMPACTION.md at line 5. Rewrite the
   "indefinitely" sentence so it is true for a headless run, in at most two
   sentences.
2. Read SCRIPTING.md lines 76–121 and AUTONOMOUS_LOOPS.md lines 276–286 side by
   side. Write the row the exit-code table is missing.
3. Pick one passage from §4 and one finding from §3. Explain in a paragraph why
   the first teaches and the second misleads.

**With a bench:** run the documentation lints yourself, as every chapter of this
review was:

```sh
for s in docs_flags doc_claims_lint docs_locators_lint docs_placeholder_lint; do
  JC_SMOKE_BIN=$PWD/jichi sh tests/smoke/$s.sh | tail -1
done
```

Then read the header of `tests/smoke/doc_claims_lint.sh`. Which kind of claim
does it check, and which kinds of §3's findings could a lint like it have caught?

## 10. Read further

- [DOC_REVIEW.md](../DOC_REVIEW.md): the six questions to ask of every page, and
  the house rules. *Primary for jichi.*
- [plans/2026-09-25-docs-structure-and-writing.md](../plans/2026-09-25-docs-structure-and-writing.md):
  how the documentation is organised, and why it stayed flat. *Primary for
  jichi.*
- [Kiroku](../reading/KIROKU.md): how to read the registers without drowning.
  *Primary for jichi.*
- Daniele Procida, *Diátaxis*, diataxis.fr: the four kinds of documentation
  (tutorials, how-to guides, reference, explanation). *Secondary.*

---

## Completed on 2026-10-17 (at e5d832a4)

*This section completes the chapter above. Its two reports — the learner pages and
the design pages with the registers — were paused on 2026-10-08 at `17bbb895`
(M913), and §6 listed what neither reached. They were finished on 2026-10-17 at
commit `e5d832a4`, about 240 commits later, as the second phase of that day's
hardening review ([its overview](2026-10-17-hardening-00-overview.md)). The text
above is not changed, because people may already have acted on it; this section
says what became of it and adds what the finished reading found. Written by
Claude Opus 5.5, 2026-10-17.*

### Who this section is for, and what you will know

**For** a self-learner or junior developer who reads jichi's documentation to
learn — alone, with nobody to ask, the reader `DOC_REVIEW.md` §1 names — and for
whoever maintains those pages.

**After reading it you will know:**

- which findings of the chapter above are fixed, which still stand, and which
  changed shape;
- whether a learner can follow the entry path, the reading course and the
  curriculum step by step, and exactly where they cannot;
- which design pages promise something the code does not do, and why that
  happened to the newest feature first;
- the design decisions behind the worst findings, what each implies, and the
  lesson you can carry to any codebase;
- what to fix first.

**What you need first.** The chapter above (§1 explains the genres). A few terms,
each defined where it is first needed below and collected here:

- A **lint** is a script in `tests/smoke/` that fails the build when a mechanical
  claim in a page is false (a flag that does not exist, a count that changed).
- A **fence** bounds what a run may touch (the edit scope, the path fence,
  `--max-tool-calls`); a **cap** bounds how long it may take (`--deadline`, a
  token budget). The project keeps fences on and caps off for measurements
  (`CLAUDE.md`, "Caps versus fences").
- The **envelope** is the set of bounds around an unattended run: budgets, an
  edit scope, a **verify gate** (`--verify <cmd>`, a command that must pass when
  the run ends) and a journal ([AUTONOMY.md](../AUTONOMY.md)).
- A **checkpoint** (or snapshot) is a copy of the workspace taken before the
  first edit of a turn, in a separate git repository, so `/undo` can restore it.
- The **context relay** (new since the chapter above, M920–M954,
  [RELAY.md](../RELAY.md)) ends a long run cleanly when its context window is
  exhausted, writes a **baton** (notes for the next session) and continues the
  same task in a fresh session, a **leg**. In **topology A** a supervisor starts
  each next leg as a new process; in **topology B** jichi continues in the same
  process.
- The **trust step** (M851) withholds the risky keys of a project's own config
  file — a file that arrives with a cloned repository — until you run
  `jichi trust`.
- A **register** is one of the project's records: `DECISIONS.md`,
  `DEFERRED.md`, `ROADMAP.md`, `ANECDOTES.md`.

---

### 1. How the work was finished

Nine readers, each with one part of the slice and the same rules: read the page,
read the code it describes end to end, and **run every published command in its
published form** in a private copy of the tree at `e5d832a4`, against the
repository's own mock model (`tests/tools/mockmodel`), with every headless run's
stdin pinned to `/dev/null`, no network, no real model, and no time limit around
anything they measured. The lead re-read the central code paths of every high
finding by hand before writing them down. Every claim below carries the
evidence mark used throughout this review: **[V]** read end to end, **[M]**
measured, **[R]** read but not traced, **[GK]** general knowledge.

What was read this time, beyond the chapter above:

- **the entry path, walked as a learner** on Ubuntu 24.04: the front page's
  doors, the docs map's "Start here", `PLAIN_LANGUAGE`, `INSTALL`,
  `PREPARE_AND_BUILD`, the Ubuntu install page, `SETUP_WIZARD`, `DOCTOR`,
  `TUTORIAL_BEGINNER`;
- the learner pages never reached: `CONFIG_TUTORIAL`, `TESTING_TUTORIAL`,
  `LEARNING`, `VOCABULARY`, `GLOSSARY`;
- **all of `docs/reading/`**: Annai, Fukabori, Tsuiseki (with its recorded
  traces) and Kiroku;
- **the course**: `CURRICULUM.md`, the twelve module pages, the instructor
  guides, and the graders of all 90 graded assignments;
- the design pages never reached: `RELAY`, `SCRIPTING`, `OBSERVABILITY`,
  `TELEMETRY`, `TOOL_OUTPUT_COST`, `QUEUE_PROTOCOL`, `SUPERVISOR_OF_MANY`,
  `EMBEDDING`, `ROUTING`, `PROMPT_CACHING`, `AGENT_COLLABORATION`, `DEPLOYMENT`;
- claims in `HARDENING`, `SECURITY`, `PLATFORMS`, `LOW_MEMORY` and
  `ARCHITECTURE`, and targeted passes over the four registers.

The mechanics are still sound: 1,252 relative links on 82 learner pages, none
broken (the checker was first shown to fire on three planted defects); the four
programs in `ALGORITHMS.md` compile and two reproduce their printed output byte
for byte; the recorded traces replay (14 of 14). As before, the weaknesses are
in what sentences claim.

---

### 2. What became of the earlier findings

**Learner pages (26 findings).** One was mostly fixed: the platform verdicts on
the entry path. Eight of the twelve sentences that told a Mac user jichi had
never been compiled were corrected at M943, and M945 added a lint
(`install_verdict_lint`) that holds each install page's verdict to
`PLATFORMS.md`. Re-planting three of the old sentences turned it red; the
others (prose sentences on five pages) it does not read. The other findings **all still stand**, among them:

| Finding | Still true at `e5d832a4` |
|---|---|
| The beginner tutorial makes "the API key line shows a tick" a mandatory checkpoint | The keyless local path it recommends can never pass it: `doctor` prints `! no API key for the active model — ok for keyless local servers` (measured twice) |
| "Indefinitely without overflowing context" (front page, `COMPACTION.md`) | Unchanged, and now also contradicted by `RELAY.md`, which exists because compaction cannot do this |
| "First match wins" for config files | Unchanged; the loader merges, and gates a project file behind `jichi trust` |
| "`/undo` always gets you back" | Unchanged; git-ignored files are outside the checkpoint |
| The one telemetry key shown | Still a shape the loader ignores; measured: `"logging": "off"` still writes a telemetry file |
| "No linked list. No tree." | Unchanged; the arena is a list of blocks and the JSON parser builds a tree |
| The approval prompt without `[e]dit`; three routers, three orders | Unchanged |

`DATA_STRUCTURES.md` and `ALGORITHMS.md` have not changed at all; the craft
tutorials changed only by gaining `< /dev/null` on their headless runs (M944).

**Design pages and registers (54 items).** One is fixed: the subagent hand-back
note that printed its two byte counts in the wrong order (fixed with a unit test
that would fail if it came back). Forty-six still stand, seven changed shape. The
ones that changed shape changed for the worse, because the relay added to them:

- **The supervisor that files an unfinished run as done** (the chapter's §3.2).
  A run that stops at its iteration cap exits 0; so, now, does a relay
  **handover**. The shipped `loop.sh` and the C supervisor both route exit 0 to
  `done/`. Measured with the mock: `loop.sh` filed a handover as done, the C
  supervisor filed a capped run as done. The relay's own design rejected exit 0
  for exactly this reason ("Exit 0 would let every existing `rc == 0 -> done`
  branch file a half-done task as finished"), and `RELAY.md` honestly marks the
  exit code "designed, deferred".
- **`ARCHITECTURE.md`** has had no commit since the chapter above; it does not
  mention the relay, and its list of stop reasons is now four values behind.
- **A register lint that cannot see its register.** `DEFERRED.md` was rewritten
  to one line per entry at M791; the lint that checks it was written for the old
  format. Proven in a copy of the tree: a closed entry under an "Open" heading,
  and an "Open" section holding only a table header, both pass; the positive
  controls (a section with no table at all, a row in the old capitals) still
  fail, so the instrument works — it is looking at the wrong shape. Twenty-seven
  closed entries now sit under Open headings, and three entries have no index
  row at all.
- **A safety hold with an unstated limit** (from the parallel pool's record,
  M888): a write child's new directory over 1 MB or 100 files is held out of the
  merge — but the scan counts only the first 16 new directories. Measured: with
  16 small directories first, a 120-file scratch directory merged into the live
  tree, unnamed and unheld.

The lesson is the one the hardening review's [chapter 8](2026-10-17-hardening-08-earlier-findings.md)
draws: a page goes stale when a milestone changes its subject and nothing ties
the page to the code. Here it is visible as a count — of the pages the chapter
above found stale, almost none has had a commit since.

---

### 3. Can a learner follow the path?

The operator's first question, asked literally: a lone learner on Ubuntu 24.04
follows the docs map's "Start here" in order and types each command as printed.

**The build half works.** `make`, `make test` (15,402 checks, 0 failures),
`make info`, `./jichi --version` and a per-user `make install` ran as printed.
The page that teaches "trust the exit code, not the scrollback"
(`PREPARE_AND_BUILD.md:278-281`) is right, and it is the one check on the path
that holds on every build.

**The model-setup half breaks in five places**, each one a hard stop for a
reader with nobody to ask:

1. **The setup wizard's default is a paid vendor.** M709 decided that jichi
   picks no vendor and reordered the provider menu so "any OpenAI-compatible
   endpoint" comes first — but the call still passes the old default position,
   so the prompt reads `choice [2]` and Enter selects `anthropic`
   (`src/main.c:7475`, `setup_menu(t, PROVS, 3, 1)`). The model list has the
   same off-by-one. **[M][V]**
2. **The tutorial names the wrong menu entry.** `TUTORIAL_BEGINNER.md:77-80`
   says to pick `openai` and give the local server's address; after `openai`
   the wizard never asks for an address (only the first entry does) and writes a
   config aimed at the vendor's cloud. **[V]**
3. **The checkpoint that cannot pass** (the old finding above), and "every line
   a tick" is unreachable on the local path anyway: a healthy keyless setup ends
   `30 ok, 5 warnings, 0 problems`. `DOCTOR.md` has the right rule — "a `!` is
   not a `✗`" — and three pages contradict it. **[M]**
4. **Setup run in the wrong directory.** The front page's first door and the
   Ubuntu page run `./jichi setup` inside the source checkout. That adds three
   untracked files, so the next build is stamped "dirty" and `make install`
   refuses it; the page's remedy reproduces the refusal, and `git stash` does
   not see untracked files. The scaffolded `AGENTS.md` also replaces jichi's own
   rules file for any session in that directory. `INSTALL.md`'s "two
   directories, two purposes" section is the right instruction; the other pages
   do not follow it. **[M]**
5. **The Ubuntu page's success test fails on every build** ("no line should
   contain the word `error`" — 188 of 190 compile lines carry
   `-Werror=format-security`), and its package line omits `git`, without which
   `doctor` reports a failure and `/undo` is off. **[M]**

**The reading course is followable and mostly true.** The readers checked about
220 claims in Annai, Fukabori, Tsuiseki and Kiroku and found them accurate;
the recorded traces replay. What a careful reader will trip over is a short
list: an exercise whose expected output contradicts the learner's own (the repo
map of the checkout contains no `src/` file at all, because it is capped at 12
KiB and sorted by path, so `docs/assignments/` fills it); a count a published
`grep` contradicts (13 on the page, 14 printed); and three statements that say
the opposite of the code (§4.5).

**The course is well built and has one trap.** Every module page has an order,
a gate and a "stuck alone" box; all 90 graders reject their untouched starting
state. But the Stage-2 gate task cannot be passed by a learner who does it
exactly right (§4.5).

---

### 4. New findings

Severity uses the review's scale: **high** = data loss, a crash, a security
boundary or a fence that does not hold (for a page: a learner stopped dead, or
told a safety net holds when it does not); **medium** = a wrong result or a
misleading statement in a plausible case; **low** = an edge case or maintenance
cost. Ids are the internal reports' (`R11-N…` learner side, `R12-N…` design
side). Security-class items are described by class, as `SECURITY.md` asks.

#### 4.1 A verify gate that reports a pass it never ran (R11-N1, high, [M][V])

**What happens.** Under the envelope, the end-of-run verifier runs only after a
checkpoint was taken (`src/chat/jc_agent.c:2713`):

```c
if (env_active(app) && app->env->verify_cmd != NULL && snapshotted) {
```

The run's summary, though, decides "green" from the outcome alone
(`src/main.c:2406-2407`):

```c
r->verifier_armed = (env->verify_cmd != NULL && env->verify_cmd[0] != '\0');
r->verify_green = r->verifier_armed && env->outcome == JC_ENV_OK;
```

So when no checkpoint is taken, the verifier never runs and the run still
prints `checked: verify green`, `not checked: (nothing …)` and
`[envelope] verified ok`, and exits 0. Measured by three readers independently, with `--verify` set to a command
that always fails and writes a marker file: no marker, no `verify` event in the
journal, exit 0. The `verified ok` footer
prints in every successful envelope run, with or without a verifier.

**When a checkpoint is not taken** — and this is why it is high: when snapshots
are off by choice; when `git` is not installed (the Ubuntu page omits it); and
**on any machine with less than 1 GB of RAM**, where jichi switches to its lite
profile automatically and the lite profile turns snapshots off. The small boards
this project measures on are exactly where an unattended run's verify gate
silently does not run. The hardening review found a different route to the same
false pass (an inherited signal setting, its E-01); this is a second, independent
one.

**The design decision behind it.** The completion verify was tied to the first
checkpoint on purpose: that checkpoint is the run's first known-good state, the
one a failing verify rolls back to (AUTONOMY.md). The implication nobody wrote
down: a run with no checkpoint has nothing to roll back to, so the code skips
the verify. The summary was written separately: since M688 it knows one way an
armed verifier can say nothing — "the run stopped first" — but on a run that
ended normally it still reads the outcome as the verifier's verdict.

**Fix.** Record whether the verifier ran, and print "verify did not run" on the
not-checked side when it did not; print "verified ok" only after a verifier ran.
A smoke test with snapshots off and `--verify false` goes red today.

**The lesson.** *A report must say what ran, not what was armed.* "Armed" is a
configuration fact; "ran and passed" is an observation. A summary built from the
first will be wrong exactly when the machinery behind it quietly skipped a step.

#### 4.2 Safety nets described as more universal than they are

The chapter above called this the most important pattern; the finished reading
found five more instances, two of them high.

- **The path fence (R11-N2 and R12-N4, high, [M][V]).** `SECURITY.md` lists it
  first among "the defences that do not depend on the model's cooperation",
  with no condition; `VOCABULARY.md`, the starter glossary that `init` installs
  (and that is put into the model's own prompt), and Annai chapter 5 say the
  same. By default the fence is on **only in autonomous runs** (`--auto` and
  delegates) — `doctor` itself prints `path fence auto — active only in
  autonomous postures` — and in the default interactive mode a read-only tool
  runs without asking. Three readers measured it: with no flags, a file outside
  the workspace was read and its content reached the model; with `--path-fence`
  it was refused. The default is a deliberate design (a human approves writes in
  chat mode); the pages describe a different one. **Fix:** one clause on each
  surface naming the default and the switch (`--path-fence`, or
  `"pathFence": true`). Whether a headless `-p` run without `--auto` — which has
  no human in the loop — should count as autonomous is a design question for the
  maintainer.
- **Telemetry consent (R11-N6, medium, [M][V]).** The setup wizard tells the
  learner telemetry is "left unset" and prints a key in the shape the loader
  ignores; metrics telemetry is on by default since M599, and answering "no" to
  the wizard's telemetry question writes nothing, so it stays on.
- **"No model-issued write can reach the records" (R12-N10, medium, [M]).**
  `OBSERVABILITY.md` says so of `~/.jichi.d/`; the path fence covers the file
  tools, not the shell, and a model-issued shell command wrote a file next to the
  live journal. `QUEUE_PROTOCOL.md` already says the right thing: run unattended
  agents as another user if the record must be trustworthy.
- **"The metrics tier carries no content" (R12-N11, medium, [M][V]).**
  `TELEMETRY.md` calls it safe to share; it records the first 160 bytes of each
  tool call's main argument — a shell command line, a search query, a delegated
  task. Secrets are redacted; content is not.
- **Constraints that enforce nothing (R12-N12, medium, [M]).**
  `SUPERVISOR_OF_MANY.md` says to put "do not push" and "do not deploy" in
  `.jichi/constraints.md`; that file takes directives (`deny-cmd push`), and the
  two phrases are read as no rule at all, without a warning.

**The lesson** is the chapter's, made sharper: *a safety net is read at the
worst moment, so its condition belongs in the same sentence as its promise.*
Every one of these is a sentence whose fix is one clause.

#### 4.3 The relay: a design page ahead of its code

The relay is the newest large feature, and `RELAY.md` is a good page — it is
written for a learner, it explains when to use it by asking "when is this work
finished?", and its status table is honest about what is deferred. Read against
the code, it also promises three things the code does not do yet. All three are
in the opt-in in-process mode (topology B, `relayInProcess`, off by default).

- **Budgets stop binding after the first leg (R12-N1, high, [M][V]).** The page
  says the operator's budgets are "summed over all legs, never re-armed". The
  envelope, however, settles its outcome at the end of every turn
  (`src/chat/jc_agent.c:6176-6179`), and enforcement only runs while the outcome
  is unsettled (`src/chat/jc_agent.c:901`):

  ```c
  return app->env != NULL && app->env->outcome == JC_ENV_RUNNING;
  ```

  A relay leg is a turn, and the in-process loop never returns the envelope to
  "running". Measured: `--max-tool-calls 2` executed 5 calls; a 400,000-token
  budget spent 1,850,050; both runs ended as a handover with exit 0.
- **The task is lost from the second leg on (R12-N2, high, [M][V]).** The loop
  keeps a pointer to the operator's prompt (`src/main.c:3505`,
  `relay_task = prompt;`), but by then the prompt has been copied onto the
  per-turn arena, which is reset at the start of every turn. The second leg
  carries the task only inside the previous baton; the third leg's "task" was a
  fragment of the system prompt. This is the bug class `CLAUDE.md` names as the
  costliest of all ("pick the shortest arena that outlives the data"), and it is
  invisible to ASan and valgrind, because the arena's memory stays mapped.
- **"An untrusted project can turn it off, never on" (R12-N3, high while
  R12-N1 stands, [M][V], security class).** The configuration-trust table
  classifies the relay's keys so that an untrusted project config **can** switch
  the relay and its in-process loop on; the relay's own design had classified
  them the other way. Measured with the mock. Combined with R12-N1, a cloned
  repository's own config could turn an unattended run into one whose budgets
  stop binding.
- **Two further security-class items** (medium, [V], not reproduced on purpose):
  text taken from the relay's notes reaches a shell command line without
  validation (R12-N5); and three git child processes the relay starts lack the
  protection against repository-planted git settings that the project added to
  every other git child at M2 (R12-N6).
- **Smaller promises** (medium): the "stalled relay" stop reason is declared and
  marked built but never produced (R12-N7); the notes' byte cap does not cap the
  notes, which nest the previous leg's baton and grew to 15.8 KB over five legs
  at an 8 KB setting; an operator abort writes no baton although the page says
  it does; two use cases describe behaviour that is not built (R12-N8).

**The design decision behind the first two.** The envelope was built for one run
that is one turn: settle the outcome when the turn ends, then switch to
"accounting mode" so post-run work (the mentor's `learnOnStop`) is not blocked
by a budget the run already spent — a decision the code comment records with
the test that caught the alternative. The relay's in-process mode made one run
contain many turns. **Implication:** a rule that was true ("the end of a turn is
the end of the run") stopped being true, and every place that relied on it —
the settle, and the per-turn arena — now acts one leg too early.

**The lesson.** *When a feature changes what a unit means — here, a turn
stopped being a run — search for every place that relied on the old meaning.*
The project's own tool for that is a red-first test per promise: extend the
existing in-process relay test with `--max-tool-calls 2` (must end `budget`)
and with a check that every leg's first message begins with the operator's
prompt. Both go red today.

#### 4.4 The contract an integrator codes against (R12-N9, medium, [V][M])

`jichi describe` declares ten stop reasons. `EMBEDDING.md` — the page that
defines which outputs are a stable interface — lists six, and says "exit codes,
all five" while a budget stop exits 1 (listed as "runtime error" beside "a
budget stop is not a failure"), a capped run and a relay handover exit 0, and
the control client exits 3. `SCRIPTING.md`'s supervisor snippet routes a
handover and a "scope tainted" verdict under "interrupted / timeout / error".

**The design decision.** M322 decided that hitting the iteration cap is a
circuit breaker, not a failure: the history is intact and another prompt
resumes it, so the exit code stays 0, and `SCRIPTING.md` warns "do not branch
on `$?` to detect this". **Implication:** exit 0 no longer means "done", and
every page that tells a supervisor to route on the exit code alone files
unfinished work as finished. The cheapest fix is one table — stop reason, exit
code, "is the answer complete?" — which already exists in
`src/util/jc_outcome.c:29-44`, linked from every page that routes on outcomes,
and a lint that compares each page's list with `describe`'s. The existing
`describe_fields_lint` takes "emitted" from the table of names, which includes
the stop reason nothing produces; taking it from the code that returns each
reason would have gone red.

#### 4.5 The course and the reading guides

- **The gate task that fails when done right (R11-N4, high, [M][V]).** Task 13
  ("delegate with a leash", 3 points, in front of the Stage-2 gate) prescribes an
  edit scope confined to the task's `work/` directory and a journal path
  outside it. The envelope writes its journal inside the workspace and then
  reports every changed path outside the scope — its own journal included — and
  the grader fails on that report. A mock model doing exactly the task: FAIL,
  twice, in both kinds of bench; the same run with the journal outside the
  workspace: PASS. The module page then tells the learner "tighten the prompt,
  not the fence"; the prompt is innocent. Continuous integration stays green
  because the reference solution writes a hand-made journal instead of running
  jichi — a test fixture that does not behave like the real thing (project rule
  5). The capstone has the same trap, and a Fukabori exercise trips it too.
  **Fix:** the scope check skips the envelope's own journal.
- **Graders that do not grade the lesson (R11-N28, R11-N29, medium, [M]).** The
  first three assignments pass answers their specs forbid ("the number alone":
  two numbers pass; "change nothing else": a whole-file rewrite passes), and the
  instructor guide says the graders reject an edited test file while task 06's
  grader passes one — the very move the project's own case study made famous.
- **Three statements that say the opposite of the code (medium, [V]).**
  - Annai chapter 8 says the compaction cut is "the newest point where everything
    after it fits"; `jc_compact_find_cut` keeps the **earliest** boundary whose
    tail fits, i.e. as much recent conversation as fits (R11-N24). Read
    literally, the page says jichi summarizes everything but the last exchange.
  - Annai chapter 4 and Tsuiseki chapter 2 say a model that narrates a call ("I
    will now read the file") is detected and nudged; the scanner's contract, and
    a unit test, say plain narration is deliberately **not** matched — only a call
    written out as JSON or XML text is (R11-N21).
  - Fukabori chapter 5 says the prompt-fitting budget "is only known when
    contextLength is configured"; it defaults to 32,000 tokens, and a large-window
    model that does not declare its window has its rules cut (measured: 450 of
    470 rules kept) (R11-N25).
- **"Every quoted block is held" (R11-N27, medium, [M]).** `TSUISEKI.md` says
  drift turns the build red; only blocks tagged with their source file are held,
  and four exercise outputs have drifted with both gates green — among them a
  `doctor` excerpt from before M709 that teaches the learner jichi silently picks
  a vendor's model, the opposite of today.
- **A lost decision (R11-N22, medium, [V]).** Two reading guides cite
  `def-317` and `def-318` for an operator decision of 2026-10-04 (a setting to
  show a model's separate reasoning). Those ids were reused on master for other
  rows, and the commit that re-landed the branch dropped the two original rows.
  The decision is now recorded nowhere.

**The design decision behind the Tsuiseki item.** The reading guides anchor
code by function name, never by line number, and the quote lint is opt-in per
block, so a guide can quote shell sessions and pseudo code freely. That keeps
the guides from rotting with every edit — and it means an untagged exercise
output is held by nothing. The page should say which kind each block is.

#### 4.6 The registers

- Twenty-seven closed entries under "Open" headings, three entries with no index
  row, and a lint blind to the format it checks (§2).
- A `DECISIONS.md` row labelled M909 describes ROADMAP's M914 and cites the
  wrong `DEFERRED` row; the same renumbering lost the decision in §4.5.
- The newest decisions are filed wherever the append happened (a documentation
  lint and a board tool under "Safety"), with three entry templates and no
  placement rule; one decision has an entry and no row.
- `PLATFORMS.md`'s definition of **Driven** requires the task, the model and the
  seconds on the row; 24 of 26 Driven rows lack the model or the seconds
  (R12-N14). `LOW_MEMORY.md`'s overview tells the reader to "read this and
  stop", then calls macOS never compiled and omits seven newer rows, among them
  a 256 MB board (R12-N15).

**The design decision.** M791 made `DEFERRED.md` one line per entry, linking to a
section per entry — the house rule that a table is for looking things up.
**Implication:** the closure text moved from the row into the section, and the
lint kept reading rows. **The lesson:** *when you change a format, re-prove every
check that reads it* — plant one defect of each kind the check exists for, and
watch it go red, before you trust its green.

#### 4.7 Smaller items, in one place

Among about 110 low and note items in the internal reports: `CONFIG_TUTORIAL.md`
calls `doctor` and `jichi models` "offline" (both contact every configured
server, sending its key); its environment table carries the key rules from
before M718; a config `verify` or `editScope` silently makes every headless run
an `--auto` run; "hitting a budget rolls back the run" (false since M80); the
Ubuntu page's start script fails until jichi is installed; `VOCABULARY.md`
lacks *session*, *tool call*, *cap* and the relay's terms; the relay is absent
from every learner page about a filling context window; `ROUTING.md` says a
missing command and a timeout escalate (they do not); `PROMPT_CACHING.md`'s
precedence is wrong for a per-model setting.

---

### 5. What is done well

- **Corrections kept in place, dated.** The macOS fix on
  `PREPARE_AND_BUILD.md:35-40` states the fact, names what it replaces and when
  it stopped being true — and became a lint.
- **The exit code over the scrollback** (`PREPARE_AND_BUILD.md:278-281`), and
  "a `!` is not a `✗`" (`DOCTOR.md:77-80`).
- **Where safety lives**, in one sentence (`reading/annai-04-tool-calling.md:68-71`):
  the gate sits between the model and the tool, and the model learns "no" the
  same way it learns file contents.
- **Measuring what a leak checker cannot see** (`reading/annai-07-memory-the-jichi-way.md:94-98`).
- **A method that states its own boundary**
  (`reading/tsuiseki-03-the-run-that-never-reaches-the-network.md:217-222`: a run can be recorded when its output
  depends only on its input).
- **A gate read as permission** (`CURRICULUM.md:57-60`: "leave it, go forward,
  come back later") — a grading rule turned into an escape route for the lone
  learner.
- **The relay's trigger** keys on measured exhaustion over several passes, not
  on one pressured pass — the 2026-09-22 analysis's lesson carried into code —
  and its notes are written atomically, owner-only, outside the workspace,
  redacted, and handed to the next leg marked as untrusted data.

---

### 6. Recommendations, ordered by value for effort

1. **Make the verify report honest** (§4.1): a `verify_ran` flag, "did not run"
   on the not-checked side, "verified ok" only after a run. Red-first: snapshots
   off, `--verify false`, one write. *Effort S.*
2. **Fix the relay's in-process mode before anyone relies on it** (§4.3): keep
   the envelope running across legs; copy the task to a session-lived arena;
   classify the relay keys off-only for an untrusted project; validate the
   commit token and route the relay's git calls through the protected helper.
   Each has a red-first test named in the internal report. *Effort S each.*
3. **One clause on every safety-net sentence** (§4.2): the path fence's default
   on `SECURITY.md`, `VOCABULARY.md`, the starter glossary and Annai 5;
   telemetry in the wizard and `VOCABULARY.md`; the records' reach on
   `OBSERVABILITY.md`; what the metrics tier records. *Effort S.*
4. **Unblock the beginner's first session** (§3): menu default 0; "pick 1) any
   OpenAI-compatible endpoint"; a conditional checkpoint; setup "in your project
   folder, not here"; `git` on the Ubuntu package line; "success = exit 0".
   *Effort S.*
5. **Exempt the envelope's own journal from its scope check** (§4.5), and make the
   task-13 reference solution run jichi. *Effort S.*
6. **One stop-reason table, and a lint that holds every page to `describe`**
   (§4.4); add the missing rows to the supervisor pages and fix `loop.sh`
   to read `stop_reason`. *Effort M.*
7. **Re-prove the register lint** against today's format, and give the reading
   guides' untagged exercise outputs a "measured when written" label (§4.5,
   §4.6). *Effort M.*
8. **Correct the three inverted statements** in the reading guides and re-add
   the lost reasoning decision under fresh ids (§4.5). *Effort S.*

---

### 7. What was still not reviewed

- The TUI exercises (Annai 2, 7, 8 and the Module 0 walk) were checked headless
  and in code, not driven in a terminal: the terminal driver carries expect
  deadlines, and this review runs no cap around a check of behaviour.
- `make check-target`, `make smoke`, `make ci` and any real model: not run, by
  rule. Two items were traced, not driven, because driving them would contact a
  vendor's endpoint or execute a planted input.
- 58 of the 68 decisions the relay design had to respect (ten were sampled);
  `docs/i18n/` beyond one German sample; `docs/case-studies/` beyond its index;
  `DECISIONS.md` and `DEFERRED.md` row by row (they were checked by script and
  sampled).

---

### 8. Lessons for anyone who writes documentation

1. **A report says what ran.** A summary built from configuration ("armed")
   is wrong exactly when the machinery skipped a step (§4.1).
2. **The condition goes in the sentence with the promise.** Every high finding
   about a page in this section is a safety statement missing one clause.
3. **A new feature is the likeliest place for a page to be ahead of its code**,
   and the likeliest place for an old rule to stop holding: when a unit changes
   meaning, search for everything that relied on it (§4.3).
4. **A test fixture must behave like the real thing.** A grader proven with a
   hand-written journal passed in CI and fails every learner who runs jichi
   (§4.5).
5. **Re-prove a check when the thing it reads changes shape** (§4.6).

### 9. Read further

- [DOC_REVIEW.md](../DOC_REVIEW.md): the six questions per page and the house
  rules; §1's lone reader is the yardstick used here. *Primary for jichi.*
- [RELAY.md](../RELAY.md) and the relay's design proposal: read them side by
  side with §4.3. *Primary for jichi.*
- [AUTONOMY.md](../AUTONOMY.md): the envelope, the verify gate and why a
  budget stop keeps the work. *Primary for jichi.*
- [The hardening review, chapter 8](2026-10-17-hardening-08-earlier-findings.md):
  how to re-check a project's own records, step by step. *Primary for jichi.*
- [Kiroku](../reading/KIROKU.md): how to read the registers. *Primary for
  jichi.*
- Daniele Procida, *Diátaxis*, diataxis.fr: why a tutorial and a reference page
  fail a reader in different ways. *Secondary.*
