# Learning loop (mentor)

jichi records what its agents do — telemetry, the autonomy journal, and
session transcripts. The **learning loop** feeds that back: it mines recurring
problems and turns them into durable lessons (memory notes + skills) so the
agent stops repeating the same mistakes. It is **manual** and **propose-only** —
you run it deliberately and review before anything is written.

Three steps:

```
jichi learn analyze        # 1. evidence: what keeps going wrong (offline)
/learn        (or jichi -p "/learn")# 2. mentor: draft lessons from that evidence
jichi learn apply          # 3. commit the reviewed draft to memory/skills
```

## 1. `learn analyze` — evidence (deterministic, no model)

Mines this workspace's telemetry log (`~/.jichi.d/telemetry/<workspace>-<key>.jsonl`
— written **by default** since M599, at the content-free `metrics` tier; before
M599 you had to run with `--log-level metrics`, and on this project nobody had)
plus a bounded scan of recent sessions, and prints a ranked list of recurring
problems:

- **tools that fail often** — a tool below a 60% ok-rate (with ≥3 calls), but
  **only if still failing recently**: a tool that has since recovered (its most
  recent call succeeded) or gone quiet (no calls in the last ~3 days of the log)
  is aged out, so a long cumulative log doesn't re-surface already-fixed problems
  (see ANECDOTES #15);
- **models that stall** — repeated `timeout` results;
- **retry storms / routing escalations / heavy compaction** — run-wide pressure;
- **redo loops** — the same file edited ≥3 times in one session (fix/break/fix);
- **autonomy outcomes** (M134) — repeated **verify-gate failures**, **budget
  rollbacks** (runs that hit the budget and lost work), and **hard model errors**
  (failed calls beyond retries). These come straight from the telemetry summary's
  outcome counts (collected since M92) and are the most direct evidence of the
  agent doing the wrong thing; a budget stop that *kept* its green work is not
  flagged. See docs/HARDENING.md §6.

```
jichi learn analyze                 # this project (current dir)
jichi learn analyze --workspace /p  # a specific project
jichi learn analyze path/to.jsonl   # a specific log
```

It is offline and side-effect-free — safe to run anytime. The pure analyzer is
`jc_insights` (`src/util/jc_insights.c`).

## 2. `/learn` — the mentor (one model pass, propose-only)

### Surfaces at a glance

| Step | CLI | TUI |
|---|---|---|
| 1. analyze (offline) | `jichi learn analyze [log]` | **`/learn analyze [log]`** (M292) |
| 2. mentor draft | `jichi -p "/learn"` | `/learn` |
| 3. review the draft | your editor | your editor |
| 4. apply | `jichi learn apply [--force]` | **`/learn apply [--force]`** (M293) |
| 4b. corrections only | `jichi learn corrections` | **`/learn corrections`** (M294) |

`/learn analyze` resolves the log most-specific-first: an explicit path argument,
then **the log this session is writing**, then the configured `logging.path`. It
filters to the current workspace (a shared telemetry dir mixes projects), names
the file it read, and needs no model call. Nothing to read is an actionable
message, not a silent empty report.

`/learn apply` (M293) commits the reviewed draft **without leaving the session**,
which matters for correctness and not just convenience. `jc_memory_add` does not
refresh `app->memory` — the `remember` tool calls `jc_memory_refresh` itself — and
a `learn apply` running in a *second* process cannot refresh this one at all. So
before M293 a live TUI session went on serving notes that a `## Corrections`
section had already superseded, until it was restarted. Applying in-process
reloads the notes **and** the skill catalog, and says so (`this session now sees
them — no restart needed`). `--force` must be typed explicitly; it overwrites an
existing `SKILL.md`.

Both surfaces call one function (`jc_learn_apply`) and render one set of counts
(`jc_learn_apply_summary`), so the CLI and the TUI cannot describe the same result
differently — the M286 lesson, applied for the third time in this chain. The CLI
keeps its `printf` voice and its exit code; the no-parseable-sections advice keeps
its stderr channel there, so stdout stays the record of what was committed.

`/learn` is a scaffolded command (`init` writes `.jichi/commands/learn.md` +
`.jichi/agents/mentor.md`). It runs the **mentor** agent as an isolated subtask:
the command injects the `learn analyze` report and the current `.jichi/memory.md`,
and the mentor — after investigating root causes in the code — writes a
**proposal** to `.jichi/lessons.draft.md` and nothing else. Run it in the TUI
(`/learn`) or headless (`jichi -p "/learn"`).

**The mentor's own prompt reaches it since M596.** `mentor.md` carries the format
block the parser below depends on (`FORMAT IS STRICT`, the five headings, the
`remove:`/`replace:` syntax) — and from M28 to M595 the subtask path never
delivered it: the mentor ran under the generic sub-agent prompt and was asked, in
one sentence, to "write durable lessons". A draft with headings the parser does
not know, or prose where directives were required, is what that produced;
`tests/smoke/subtask_persona.sh` now reads the captured request to prove the
block arrives. Whether a given model *obeys* it once told is a separate
measurement (`docs/analysis/2026-08-27-the-language-of-lessons.md` §8).

**Lessons are written in your language (M597).** The mentor receives the same
`language` directive the session does, so with `"language": "Deutsch"` the draft
-- and, after `learn apply`, `memory.md` -- is German. The operator's decision
(2026-08-27): a self-learner's lessons belong in the language they think in;
English-canonical storage is an **option**, one line on the command --
`language: English` in `.jichi/commands/learn.md` -- and then the session still
tutors in Deutsch over English notes, which the model translates as it reads.
Two things stay English in every case, because a tool parses them: the five
section headings below (the mentor is told they are a machine format, like a
file path), and the `remove:` / `replace:` / `=>` directive syntax.

**The mentor is given the project's words (M603).** `learn.md` inlines
`.jichi/glossary.md` when it exists — the starter glossary of jichi's own terms
ships with the `default` pack now, not only with `assignments` — so the draft
uses *envelope*, *fence*, *hollow green* the way the notes and the analyze report
do. A project without a glossary gets nothing inlined; the inline is a shell
block (`cat … 2>/dev/null`), so a missing file cannot leave a "[could not read]"
note for the model to echo into a lesson.

The command's embedded `learn analyze` is **wrapper-agnostic**: it runs
`./jichi learn analyze --workspace .` when a project `./jichi` wrapper exists, else
`jichi learn analyze --workspace .` (on `PATH`). So `/learn` works whether
the binary is on `PATH` or invoked through a wrapper, and the `--workspace .`
scope feeds the mentor the project's aggregated history rather than one recent
log.

The draft has three sections:

```
## Memory notes
- one-line, specific gotchas/fixes   [evidence: …] [pins: tests/…]
## Skills
### <slug>: <description>
<a step-by-step procedure for a recurring multi-step fix>
## Corrections
- remove: <substring of a remembered note that is now WRONG>
- replace: <substring of a stale note> => <the corrected one-line note>
## Project rules
- <a project-wide convention for AGENTS.md>
## Checks
- constraint: <a lesson stated as something jichi should REFUSE>
## Suggested (manual)
- config / agent / rule changes for you to weigh by hand
```

**`## Checks` — a lesson that is a refusal (M602).** Every other section is prose
the model reads and may forget; a constraint is *enforced* at the tool gate and
re-injected every turn ([CONSTRAINTS.md](CONSTRAINTS.md)). The mentor is asked,
for each lesson: *can it be stated as something jichi would refuse?* If so it goes
under `## Checks` as `constraint: <phrase>` in the scanner's own vocabulary —
`do not run the build` / `tests` / `commit` / `push` / `deploy` / `install
packages`, `do not use the tool <name>`, `read-only` — and `learn apply` commits it
as an **authored** constraint in `.jichi/constraints.md`, exactly as
`/constraints add` would. A phrase the scanner cannot read is counted and named
(*"1 check(s) skipped: the constraint scanner reads …"*), never guessed at; a
bullet of another kind (`hook:`) is counted as unsupported — hooks live in
`config.json` and need you to enable them, so that path stays yours
([DEFERRED.md](DEFERRED.md)). Propose-only is untouched: the mentor proposes, you
run `apply`. What changed is the *kind* of thing you are asked to approve.
`tests/smoke/learn_checks.sh` reads the effect: the next run's `make` is refused.

The `/learn` command declares `output: .jichi/lessons.draft.md` in its frontmatter
(M79): the mentor is expected to write that file with `write_file`, but if a
model *narrates* its proposal instead of calling the tool (a common small-model
failure), `jc_agent_run_command_subtask` detects the file was left unchanged and
persists the mentor's returned answer there — so a narrated draft still lands in
the file for review instead of being lost to stdout.

**It will not overwrite a draft you already have (M423).** If the declared output
file is already non-empty, the fallback leaves it alone and writes the answer to
`<path>.answer` instead, warning where it went — so you diff the two rather than
losing one. This matters because **learn-on-stop fires after *any* completed
`--auto` run**, so an unrelated run's mentor can reach a draft you have curated but
not yet applied: measured on 2026-08-13, the mentor of a run about listing `.zig`
files replaced an 85-line reviewed draft — `## Corrections` citing an anecdote and a
`memory.md` line — with four lines of mid-thought narration. It was recoverable only
because that workspace had the draft committed to git. The fallback exists so the
model's work is not lost; it may not decide that the model's work outranks yours.

**A note carries its provenance, and says whether anything holds it (M600).**
The `[evidence: …]` trailer the mentor writes is **kept** when a note is committed
(until M600 `learn apply` stripped it, so the one place a note's origin could
travel to `memory.md` was discarded). A second trailer, `[pins: tests/smoke/x.sh]`,
names the test, lint or constraint that holds the lesson mechanically — the
`doc_claims_lint` convention ("a claim about behaviour names the check that pins
it") applied to lessons, because a lesson with no check to cite is the tier the
project's own record says does not hold (`analysis/2026-08-22-learning-from-errors.md`
§3). `learn analyze` now reports three things about the memory file that a
machine *can* check: the injection budget as a fraction (`Memory: 8092 of 8192
bytes injected (98%)` — measured on a real project, one note from silently losing
its oldest lesson), the pinned share (`1 of 14 remembered note(s) are pinned`), and
every note that names a path which no longer resolves in the workspace. And a
`## Corrections` bullet that is prose rather than a `remove:`/`replace:` directive
is **counted and named** by `learn apply` — *"3 correction bullet(s) ignored …
they retract nothing"* — where before it vanished as if the section were empty.

**A correction can take a rule back (M601).** `remove:` / `replace:` directives
now also act on the rules file's `## Learned conventions` — the one store the loop
could append to but never retract from. Only bullets under that heading are
touched; the hand-written rules above it are never reached (`learn apply` only
ever wrote below it). The apply summary says *"N learned convention(s) retracted
from the rules file by the same directives"*, and a directive that matched a
convention but no memory note is not reported as unmatched.

**Review and edit the draft** — keep what's right, delete the rest. The draft is
a *starting point*: capable models emit the sections directly, but smaller
local models often write a richer, prose-y analysis under their own headings.
`learn apply` only commits `- ` bullets under `## Memory notes` and
`### name: desc` blocks under `## Skills` (by design — it won't guess which prose
is a lesson), so curate the keepers into those exact headings before applying.
If `apply` reports it found nothing, that's the cue to reformat.

A heading may also name **two** sections at once, and then the more specific noun
wins: `## Memory Note Corrections` is a *corrections* section, not a memory one.
The keyword tests in `jc_learn_parse_draft` are ordered most-specific-first for
exactly this reason — "memory" is the most generic of these words (all of these
sections are about remembered state), so it is matched last. Before that ordering
was deliberate, such a heading resolved to `## Memory notes` and its `remove:` /
`replace:` directives were committed as durable notes: instructions to modify
memory became facts.

## 3. `learn apply` — commit (deterministic, no model)

Parses the (edited) `.jichi/lessons.draft.md` and commits it:

- **Memory notes** → appended to `.jichi/memory.md` via the same deduped path as
  the `remember` tool (already injected into the system prompt every session).
- **Skills** → `.jichi/skills/<slug>/SKILL.md` (skipped if it exists, unless
  `--force`).
- **Corrections** (M78) → the loop's way of *correcting*, not only teaching.
  `- remove: <substr>` drops every memory note containing `<substr>`;
  `- replace: <substr> => <new note>` drops the matches and appends the corrected
  note. This retracts a lesson that has become false — e.g. a note about a bug a
  commit has since fixed — instead of leaving it to mislead forever (before M78,
  a reworded note was just *appended* beside the stale one, since dedupe is
  exact-line). The mentor is told to check the already-remembered notes against
  the current code and emit these; `learn analyze` adds a staleness-review
  reminder (counting notes, flagging those that cite a specific line/range as
  most prone to drift). Applied via the pure `jc_memory_apply_correction` +
  `jc_memory_correct`.
- **Suggested (manual)** → left untouched; apply those yourself (config / agent
  profiles / `AGENTS.md`).

```
jichi learn apply           # commit memory + new skills + corrections
jichi learn apply --force   # also overwrite an existing skill
```

Or, without leaving a running session (M293):

```
/learn apply                # same work, in-process -- the notes and the skill
/learn apply --force        # catalog are reloaded, so this turn already sees them
```

Both go through `jc_learn_apply`, which takes a **section mask** — so committing
only part of a draft is a selection over machinery that already exists rather than
a second code path.

### `learn corrections` — retract without adding (M294)

```
jichi learn corrections     # commit ONLY the draft's `## Corrections` section
/learn corrections          # same, in a running session
```

This is the command for one specific situation, and it is the situation jichi
itself warns about: **`memory.md` has outgrown the 8 KB injection budget**, so the
oldest notes are no longer reaching the prompt. What you need then is to *retract*
stale notes, not to add more — and a full `learn apply` would add. Both warnings
(the `remember` tool's and the load-time one `doctor` surfaces) name this command.

It had been *named* by those warnings long before it existed: until M292 they said
"`/learn corrections`", which printed a usage error and exited 2. M292 pointed them
at the real mechanism; M294 made the named operation real, so they point at it
again — this time at something that runs.

`--force` is **refused**, not ignored: it only affects skills, which this command
does not write, and silently accepting a no-op flag teaches the wrong model of what
the command does.

A masked run is a partial apply by design, so it reports what it left:
`N other draft item(s) not applied by this command -- run \`learn apply\` for the
rest.` A full apply leaves nothing, so it never prints that.

**The draft is deliberately NOT rewritten.** M294 left this open — whether applying
corrections alone should strip the applied directives so re-running is idempotent —
and the answer is no, for three reasons. The draft is the *human's* review artifact
and may be open in an editor; rewriting it behind them is a footgun. `learn apply`
does not rewrite either, so doing it only here would make two commands behave
differently on one file. And re-running is already harmless: a retracted note is
gone, so the directive simply finds nothing. The only cost was a message that read
like a broken draft, so that message now names the likely cause — `correction
skipped: no note matches "…" (already applied, or the substring does not occur in
.jichi/memory.md)`.

Re-running is a no-op (memory dedups, existing skills are skipped, and a
correction whose `<substr>` no longer matches anything is skipped). The draft
parser is the pure `jc_learn_parse_draft` (`src/util/jc_learn.c`).

## Auto-run after `--auto` (opt-in)

By default the loop is fully manual. To have the mentor draft lessons
automatically at the end of a **completed `--auto` run**, set `learnOnStop`
(config) or pass `--learn-on-stop` (`--no-learn-on-stop` overrides a config
that turned it on):

```
jichi --auto --learn-on-stop -p "implement X"
```

After the run finishes successfully, the mentor runs once (the same `/learn`
pass) and writes `.jichi/lessons.draft.md`. It is still **propose-only** — nothing
is committed until you review the draft and run `learn apply`. It fires only on
a clean completion, only in `--auto`, and only when the
`learn` command is scaffolded (`init`). Off by default.

**"Clean completion" means the envelope's outcome is `ok`** — not merely that the
agent loop returned without an error. The distinction is load-bearing and was
wrong until M328: the loop returns `JC_OK` for *every* terminal state (the
envelope's `outcome` is what carries budget exhaustion and verify failure, and the
process exit code derives from that, not from the loop's return). So the mentor used
to run after a run that had just hit its token budget — and its turn spent tokens
**outside the envelope's accounting**, because the journal's `end` event was already
written. Measured once on a real drive: a `--budget-tokens 1m` run cost **1.64 M**,
61% over, with the overshoot invisible in both the journal and `jichi runs`.

A budget stop is also where the mentor is least useful: the run was cut off
mid-task, so lessons drafted from it describe an interrupted attempt rather than a
finished one. When it is skipped, jichi says so and names the outcome — silence
there is indistinguishable from a broken feature, and the operator's next move
(re-brief the increment smaller, then let the mentor run on a clean completion)
depends on knowing which it was.

**The run reports what its draft would commit (M598).** After the mentor writes,
learn-on-stop parses the draft with the same parser `learn apply` uses and prints
`(learn-on-stop: draft parsed -- N memory note(s), N skill(s), N correction(s), N
project rule(s))`. A draft with bytes and **none** of the parseable sections is
named at WARN level -- *"applying it would commit nothing"* -- so `-q` cannot hide
it, and the run's journal `learn_on_stop` event carries the counts
(`draft_items`, `draft_parsed_nothing`), which `jichi runs` shows as `draft=empty`
on a row whose outcome is otherwise `ok`. Measured on 2026-08-27: a real project's
draft had two invented headings and zero directives, and nothing had said so for
weeks -- the mentor that produced it had never received its format block (M596).

If you *want* lessons from a run that did not complete, run the mentor by hand:
`/learn` in the TUI, or `jichi -p /learn`. That way its cost is a choice you made
rather than an invisible addition to a bounded run.

## Scope

Per-project by default: `learn analyze` filters to the current workspace and
`apply` writes this project's `.jichi/`. Memory is per-project by design. Use
`--workspace`/a global skills dir for cross-project lessons.

## Why propose-only / manual

Lessons shape every future turn, so a bad one is costly. The loop keeps a human
in the middle: the deterministic `analyze`/`apply` halves are auditable and
testable, and only the `/learn` mentor step uses the model — and it merely
drafts. Nothing mutates `.jichi/` until you run `apply` on a draft you've read.

See also: [MEMORY.md](MEMORY.md), [SKILLS.md](SKILLS.md),
[TELEMETRY.md](TELEMETRY.md), [SUBAGENTS.md](SUBAGENTS.md), and the
one-page map of every human↔agent / agent↔agent learning and
collaboration surface: [AGENT_COLLABORATION.md](AGENT_COLLABORATION.md).
