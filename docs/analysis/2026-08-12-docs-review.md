# Documentation review for the self-learner — findings, fixes, and the register of what is owed

*Four independent reviewers read 30 pages against one rubric — **what / where /
when / how / by whom**, plus **the decision and its explanation** — for a single
audience: a **self-learner working alone**, no instructor, no colleague, nobody to
ask. Their slices: the six design tutorials + CHOOSING_A_MODEL; the beginner entry
path walked in order; the curriculum, modules, tasks and reading tracks; the
feature/reference pages. Every finding below was re-verified against the code or by
running the command before it was acted on — three reviewer claims that sounded
right turned out to need correction, and several turned out to be worse than
reported.*

## 1. The shape of what they found

The corpus is strong on mechanism, honesty and cross-linking (**every relative
link across ~30 pages resolves**; the quantitative claims are conservative rather
than inflated) and weak in exactly three places:

1. **Runnable commands rot silently.** 7 of ~15 copy-pasteable `jichi` invocations
   in the six newest tutorials could not work as written. The dominant cause was
   **the guard M375 added yesterday**: flags after `-p` are refused, so
   `jichi -p --no-session "…"` — the form the probe documentation itself
   prescribed — exits 2. The bug M375 diagnosed was still being *taught* by the
   page that tells its story.
2. **The safety net is documented as more universal than it is.** Every gap leaned
   the same way, which is the direction that matters: toward a reader trusting more
   than the code delivers.
3. **Reference pages are asked to do a tutorial's job.** `AGENT_MODES`, `MODELS`,
   `AUTONOMY`, `SKILLS`, `REFERENCES` are written for someone who already knows;
   the self-learner meets them at the moment they know least.

## 2. Fixed in this pass (M392)

**Hard stops — a lone reader's run ended here:**
- `git clone <REPOSITORY-URL>` with no URL anywhere in the repo, at step 3 of the
  first page. Now says plainly that **there is no public URL yet** (the licence is
  pending), and gives the tarball path.
- The `doctor` sample output in `PREPARE_AND_BUILD.md` was **fabricated**: it showed
  `! no models configured` as an ignorable warning. That string is a `JC_DOC_FAIL`
  (`✗`, exit 1) *and it cannot fire* — `push_model(out, NULL, a)` guarantees a
  model. Real output verified on an isolated `HOME` and pasted in, including the
  three things that surprise everyone: the stderr hint fires *before* the
  checklist, a **built-in default model** (`claude-opus-4-8`, Anthropic) is active
  though the reader chose nothing, and the `?` in `active: ? (claude-opus-4-8)` is
  a missing *name*, not an error.

**A false security claim:** `SETUP_WIZARD.md` §"Secrets are never written" — the
interactive wizard's last question offers, **default yes**, to write the key to
`~/.jichi.env`. Retitled "Where the key goes" and now states both halves.

**A data-loss hazard:** `CONFIG_TUTORIAL` §1.4 used `cat > ~/.jichi.env`, which
**truncates the file the wizard appends to** — destroying a stored key, and any
second key. Now appends.

**Safety documentation (all verified in code):**
- `SNAPSHOTS.md`: **git-ignored files are outside the net** — a checkpoint is
  `git add -A`, so `.gitignore`d files are never captured and `/undo` cannot
  restore them. Other pages steer beginners into exactly those files (a
  git-ignored `local/config.json`, a git-ignored `.jichi/memory.md`, `.env`).
- `SNAPSHOTS.md`: `snapshots` defaults to **`false` under `--lite`**, which is
  auto-enabled below ~1 GB RAM — so on the 512 MB board the README advertises as
  green, **there is no undo**, and the documented default said `true`.
- `SNAPSHOTS.md`: a new **"Is my net armed?"** section — three of the four ways
  snapshots switch off are silent.
- `AGENT_MODES.md`: mentioned undo/checkpoints **zero times** (grep-verified) on
  the page where a nervous beginner decides whether to leave chat mode. Now opens
  with the checkpoint guarantee, its two caveats, and a "if you are starting out".
- `AUTONOMY.md`: the intro promised automatic rollback; **rollback requires a
  verifier and a green checkpoint**, so `--auto` without `--verify` never rolls
  back. Stated where the promise is made.
- `README.md`: `--auto` in the usage block now carries the same warning;
  orchestration corrected from "**single-level**" to depth-bounded **default 2**;
  the skill `allowed-tools` "fence" corrected to **advisory** at the top level;
  the **four mutating git tools** (`git_add`/`git_commit`/`git_branch`/`git_stash`)
  named, since a learner otherwise meets `git_commit` first in an approval prompt.
- `REFERENCES.md`: `references` also defaults **off under lite** — so `@file`
  silently stays literal on a small machine.
- `SKILLS.md`: documented the `style:` frontmatter key, which the parser and
  `jc_assetval`'s table accept and the page denied.

**Teaching defects:**
- `USE_CASE_TUTORIAL` taught a shape that **fails the grader it recommends**: task
  68 counts the literal words *actor*, *trigger*, *failure* ≥3 times each, and the
  tutorial's shape had no **Trigger** at all. Trigger added to the shape, with a
  box telling the reader to use the grader's words.
- `curriculum/00`: the Module-0 gate ("`jichi assignments` shows … passed")
  **cannot be reached by following Module 0** — only `--record` (or the TUI
  `/grade`) writes `.jichi/progress.jsonl`. Now says so.
- `curriculum/04`: the record format said "the ANECDOTES.md format — the same one
  this project keeps". It is **not** the same: graders grep four literal `##`
  headings; ANECDOTES uses bold run-ins (`**Symptom.**`, 45 of them, and **zero**
  `^## Symptom`). Now: take the *content* from ANECDOTES, the *form* from the
  block — and the record has a named home (`docs/RECORD.md`), which it never had.
- `CURRICULUM.md`: the study-bench recipe's `cp -r … docs/assignments` fails
  (`setup` creates no `docs/`). Fixed with `mkdir -p docs`.
- Broken commands repaired in `TESTING_TUTORIAL` (three, plus the undocumented
  zero-config `jichi test '<cmd>'`), `PSEUDOCODE` (`-c --auto`, since a second
  `jichi -p` is a fresh session that never saw the pseudocode), `DOMAIN_MODELLING`
  (`@docs/USE_CASES.md` + `-c`), `ARCHITECTURE`, `CHOOSING_A_MODEL` (both probes,
  plus fence-stripping and the context-free config), and zigodot's
  `MODEL_KNOWLEDGE.md`.
- The read-only-agent contract stated in three tutorials: `/requirements`,
  `/usecases`, `/design` **print** their document, they do not write it, and they
  read inputs that must exist first.
- Smaller: mermaid **rendering** instructions (nothing in jichi renders it, and
  138 mermaid blocks had no note saying so); the ADR **template** in the section
  that argued hardest for ADRs and gave no way to start; `run.sh`'s real location;
  the M175/C6 jargon in the first sentence a newcomer reads; the "vendored cJSON
  is exempt from `-pedantic`" contradiction (the Makefile says the opposite);
  `TUTORIAL_ADVANCED`'s two broken MCP links; a self-link; two garbled sentences
  and leaked internal item-numbering in `ARCHITECTURE`.

## 3. The register — concepts, features and processes needing extended documentation

Consolidated and de-duplicated across all four reviewers. Ordered by what a lone
self-learner loses without it.

| # | Owed | Why it matters / where the reader lands today |
|---|---|---|
| 1 | **`docs/MCP.md`** | No page exists. `TUTORIAL_ADVANCED`'s 14 lines are the entire user-facing account of MCP, and its reference links were broken (now honest). Needs: what MCP is, the two transports, `autoApprove`/`deny`, `mcp resources\|prompts\|call`, prompts-as-slash-commands, and the deliberate decision that a hand-configured server's tool results are *not* untrusted-fenced. |
| 2 | **"You need the source checkout, not just the binary"** | `make install` ships binaries, man page, completions and editor files — **not** `docs/`, `tests/` or the assignments. Every tutorial's reading track and every graded task depends on files a binary-only user does not have. One prerequisite line per reading track, or one short page they all link. |
| 3 | **The full decision chain for one tool call** | Six mechanisms compose — mode baseline → `permissions` → MCP per-server policy → the privileged gate *below* the verdict → the kinetic gate → a `PreToolUse` hook that can block — and **no page assembles them**. `ask_user`, hooks and the kinetic gate appear on none of the reviewed feature pages. |
| 4 | **Where state lives, as one page** | `~/.jichi`, `~/.jichi.env`, the nine `~/.jichi.d/` subtrees, `./local/config.json`, `./.jichi/`. `INSTALL.md`'s table is the best attempt and omits `~/.jichi.env` (the one holding a secret), `telemetry/` and `audit/`. |
| 5 | **The lite / low-resource profile as a behaviour change** | It silently flips snapshots, references, repoMap, markdown, subagent depth and tool caps — and auto-enables below ~1 GB. Three of this pass's safety fixes were consequences. Only `LOW_MEMORY.md` carries the list; no feature page cross-references it. |
| 6 | **Config merge semantics, single-sourced** | Correct only in `CONFIG_TUTORIAL` §0 (global ⊕ project, scalars win, **lists union**). `INSTALL.md`'s diagram and README both still describe first-match-wins. One canonical block the others point at. |
| 7 | **The work-preservation family** | `preserveDiscarded`, `jichi attempts`, `jichi recover`, `checkpoints gc` — the "get your work back" feature, living in SNAPSHOTS' last section and one AUTONOMY table cell. No beginner-facing home. |
| 8 | **`jichi attempt`** | Used in runnable blocks at the top of `assignments/INDEX.md`, defined nowhere. It has the *agent* attempt the task — the inversion of the course premise — so a learner who copies that line has jichi do their homework. Needs one sentence at first use. |
| 9 | **Set B/C toolchain prerequisites** | Graders in tasks 09/12/14/15 shell out to `cc`; Sets A and D carry prerequisite boxes with the `apt`/`xcode-select` commands, Sets B and C carry none — and Stage 2's *required* task 09 is one of them. |
| 10 | **The gate arithmetic and the permission to skip** | Set A is 17 points with a 14-point gate: exactly one 3-point task may be dropped, not two. Never stated, so a permanently-stuck lone learner cannot tell whether they may move on — the missing sentence that prevents abandonment. |
| 11 | **The self-learner's substitute for the judgment layer** | `INSTRUCTOR.md` holds four things the *self*-learner needs: the Annai reading schedule (the only per-module mapping), the failure-modes-to-expect catalogue, permission to re-tier a task's hints (`audience:` is one word), and the honest-limits table. Behind a door marked "for instructors". Module 11's live-test-on-yourself is the answer and is not generalised. |
| 12 | **The process track as the day-one, no-toolchain path** | Tasks 67–73 are 17 graded points needing no compiler — and they sit at the bottom of a 488-line index, unrouted from the map and from every module page. A compiler-less beginner has a complete path and no way to find it. |
| 13 | **`PLAIN_LANGUAGE.md` is unreachable from the curriculum map** | The tier written for the reader with least prior experience is discoverable only at line ~155 of INDEX.md. One line in §Who it is for. |
| 14 | **Tutorial-shaped sections for five reference pages** | "Your first hour" (AGENT_MODES: chat → a read-only question → decline an approval → approve one → `/diff` → `/undo` → `/status`); "Your first bounded run" (AUTONOMY: scratch branch, deadline-only cap, a verifier *watched failing first*, one-directory scope, `jichi runs`); "Your first skill" (SKILLS: four commands, the `▸ load_skill` line you should see, and the commonest failure — a `description` that does not say *when* it applies); "The smallest config that works" (MODELS: three keys, `jichi models`, `doctor`, one real call); DOCTOR's **"a check failed — now what"** symptom table. |
| 15 | **Definitions of the load-bearing words, before use** | "role" (MODELS' core concept, never defined), "posture", "fence", "envelope", "green", "chokepoint", "fix-forward", "quantized", "TAP", "lint", "immutable", "invariant". Each is cheap; together they are most of the beginner tax. |
| 16 | **Three caps a learner will conflate** | `maxToolIters`, the envelope budgets, and `maxSubagentIters`. `AGENT_MODES` explains one well. |
| 17 | **`verify` vs `testCommand`** | Both name a command; the precedence lives only in asides. |
| 18 | **A user-story tutorial** | Promised twice in `USE_CASE_TUTORIAL` and out of scope at M386. Either write it or make the promise an honest deferral. |
| 19 | **`jichi assignments` as an orientation command** | Lists all 77 tasks name-sorted with no stage or module column, while CURRICULUM annotates it "the set A table" and claims "am I ready for the next stage? is a command, not a feeling" — it is not; the learner still hand-adds points. Either the command grows a filter/total, or the claim shrinks. |
| 20 | **README structure** | No "Start here" (the build pointer is buried mid-Status, ~14 screens in); two different "latest milestone" claims 900 lines apart; the approval-prompt example omits `[e]`dit; the exit-code list omits 143. |
| 21 | **`SDLC.md` over-claims** | "every document lands in the project's `docs/` tree" — the propose-only read-only agents print rather than write. Three tutorials now say so; the source page does not. |

## 4. Structural findings recorded but deliberately not acted on

Each is a page reorganisation, not a defect, and each wants its own pass:
`INSTALL.md`'s 35-line "how old a system" archaeology sitting inside
*Requirements* (→ an appendix); `SETUP_WIZARD.md` written as a change-log against
a previous wizard the reader never used (five sections lead with milestone
numbers) **and** its flow diagram now describing an order the wizard no longer
follows (the mermaid has drifted twice — a numbered list would not);
`TUTORIAL_BEGINNER` §6b (telemetry and cost) standing between a two-prompt
beginner and the section they actually need; `assignments/INDEX.md` opening with 80
lines of cost forensics before defining what a task is; `TUTORIAL.md`'s 13
equal-weight rows; `DOCTOR.md`'s sample output being internally impossible (two
contradictory server lines, and a summary that does not add up).

## 5. What was already good, and must not be "improved"

Named so a future pass does not sand it off: `CONFIG_TUTORIAL` as a whole (the
model the other pages should be measured against — recommendation, reason,
rejected alternative, every command runnable, §13 keyed by symptom);
`INSTALL.md`'s "Two directories, two purposes" and "Which `jichi` am I actually
running?" (symptom → cause → one-line diagnosis → fix → follow-on failure, with an
observation date); `PREPARE_AND_BUILD`'s "the terminal, in one paragraph" and
"read the **first** error, not the last"; `TUTORIAL_BEGINNER`'s "You are the
developer, not a spectator" and its max-iterations section (reassure, fix, then
argue for the better fix); `MODELS.md`'s `--model` section, which splits *"For a
beginner:"* from *"Design rationale (for the curious):"* — **the best
register-switching in the corpus and the template for the rest**; `MEMORY.md` end
to end; `SKILLS.md`'s design-note history (the removed fence, the concrete
72-iteration failure, and why the replacement is subagent-scoped);
`CHOOSING_A_MODEL` §3's verification asymmetry and §6's decisions; the curriculum's
three-layer doctrine *applied* rather than declared, its "If you are stuck alone"
boxes, module 0's shell-vs-TUI disambiguation, the uniform 77-spec footer, and
`annai-01`, the best-calibrated page in the teaching layer for a beginner alone.

## 6. The honest state of this work

**One pass of several.** Roughly 35 findings were fixed here — every hard stop,
every verified false safety claim, every broken command, and the teaching defects
that made a faithful reader fail a grader. **21 register items and six structural
findings remain**, and the largest of them (`docs/MCP.md`, the state-lives-here
page, the decision-chain page, the five tutorial-shaped sections) are each a
milestone, not a paragraph.

**The rubric used here is now a reusable artifact:**
[`../DOC_REVIEW.md`](../DOC_REVIEW.md) — the six questions, the three cross-cutting
ones, how to slice a corpus, why reviewers must be read-only and verified, and when
to run it again. This report is its worked example.

One methodological note worth keeping: the reviewers were right about far more
than they were wrong, but three of their claims needed correction on verification,
and the two most valuable findings in the whole review — the fabricated `doctor`
output and the git-ignored blind spot — were things **no lint in this repository
could have caught**, because both are prose that is internally coherent and simply
untrue of the program. That is the class of defect a reader finds and a checker
cannot; it is the argument for doing this again with fresh eyes, not for building a
lint.
