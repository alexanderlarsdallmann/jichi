# Constraints — captured and enforced (M110)

Constraints are hard limits you set on what the agent may do — "do not run the
build", "never commit or push", "read-only". Unlike a hint in a prompt (which the
model can forget after a long turn or a compacted context window), a constraint is
**enforced**: a tool call that would violate it is *mechanically refused*, and the
constraint text is re-injected into the system prompt every turn so it survives
compaction.

**Since M734, only the constraints you write are enforced.** The ones jichi
*infers* from the wording of a prompt in `--auto` are shown to the model as advice,
and a call that goes against one runs — see [Inferred rules advise](#inferred-rules-advise-m734).

## Why

Telling an agent "don't run the tests" in a message is advice. On a long
autonomous run — especially after the context window fills and history is
compacted — the instruction scrolls out of the model's attention and it runs them
anyway. Constraints fix this at two layers:

1. **System prompt (compaction-proof).** The active constraints render into a
   `# Active constraints` block that `jc_sysmsg_build` emits every turn. The system
   prompt is never compacted (M73), so the limit is always in front of the model.
2. **Tool gate (binding).** Before any tool runs, `jc_constraint_judge` checks the
   call against the active constraints. A call that violates one you wrote is
   refused with `blocked by an active constraint: <reason>` — the model *cannot*
   work around a forgotten instruction. A call that violates only an inferred one
   runs, and is told (M734).

## Kinds

| Kind | Example phrasing | Enforced as |
|------|------------------|-------------|
| `deny-cmd <key>` | "do not run the build" | shell commands matching the key (build/test/commit/push/deploy/install) are refused |
| `deny-tool <name>` | "do not run the tests" | the named tool (e.g. `run_tests`, `git_commit`) is refused |
| `read-only` | "keep this read-only" | every mutating tool is refused |
| `note <text>` | a free-form rule | injected into the prompt (advisory; not gate-enforced) |

The command keys carry synonym tables: `build` matches `make`/`cmake`/`compile`/
`gcc`/…; `test` matches `pytest`/`ctest`/`cargo test`/`zig build test`/…; word
boundaries keep `latest` from matching `test`.

## How constraints are added

- **Explicit (any mode).** TUI `/constraints add <text>` scans your phrasing and
  enforces what it recognizes; or edit `.jichi/constraints.md` directly (see below).
- **From a reviewed lesson (M602).** The mentor may propose a constraint under
  `## Checks` in `.jichi/lessons.draft.md`, and `jichi learn apply` — your action,
  after you have read the draft — commits it as **authored** through this same
  scanner. A lesson that can be stated as a refusal thereby stops depending on the
  model remembering it ([LEARNING.md](LEARNING.md)).
- **Auto-adopt in `--auto` (default on).** In unsupervised AUTO mode jichi scans
  each request for constraints and puts what it finds in front of the model, so an
  instruction that scrolls out of a long run's context is not lost. What it infers
  is **session-scoped**, announced by name at WARN level, and, since M734,
  **advisory**: it is told, not enforced — see Provenance and
  [Inferred rules advise](#inferred-rules-advise-m734) below.
- **Interactive TUI.** Auto-adopt is *off* interactively; state a constraint and
  add it with `/constraints add`, so a casual mention isn't turned into a hard rule
  by surprise.

## Provenance: what outlives the session (M169)

What differs is whether a constraint *persists*, and, since M734, whether it
*binds*:

| Origin | How it arrives | Lifetime | At the tool gate |
|---|---|---|---|
| **authored** | `.jichi/constraints.md`, or `/constraints add <text>` | persists — it is a policy you wrote | **refuses** a violating call |
| **inferred** | scanned out of a prompt by auto-adopt | **this session only** | **advises**: the call runs, and its result names the rule (M734) |

**Design decision — a guess must not outlive the turn that produced it.**
Extraction is a keyword scan over prose, so it will sometimes be wrong. Being
wrong for one turn is a nuisance; being wrong *and written to disk* means the
mistake silently governs every later run in that directory. Both of the misparses
that motivated this cost real work:

- "Do not change the **test** file" was read as *do not run tests*, banning the
  suite for a task that had to run it.
- "Oracle files (**read-only**, outside the edit scope)" — describing some inputs —
  was read as an order, and a 1.56 M-token `--auto` drive spent twenty minutes
  reading files it was forbidden to change. The outcome looked like a lazy model
  (`docs/ANECDOTES.md` #21).

Both phrasings are narrowed now, but the general problem is not closable by a
better keyword list. Capping the blast radius is. So an inferred constraint is
never written; the store keeps only what you authored. M169 capped a misparse's
reach in *time*; M734 capped what it can *do* within the run.
`/constraints` marks each one `[saved, enforced]` or `[this session, advisory]`, and shows the sentence
an inferred one was read from (M872; until then the tags were `[saved]` and `[this session]`, under a
header that called both enforced).

If jichi infers something you *do* want to keep, the notice names it — add it with
`/constraints add` (which also promotes an already-inferred one to saved) or write
it into the store by hand.

Corollary: when nothing is authored, jichi **removes** `.jichi/constraints.md` rather
than leaving a header-only file, and does not create `.jichi/` just to hold nothing.
A run that only inferred a constraint leaves the workspace exactly as it found it.

## Inferred rules advise (M734)

**The measurement.** M730 ran the scanner over the first message of every stored
session on the development workstation, 289 prompts, with no model call. It
inferred nine constraints. Three were right — *"Do not run build. Do not run
tests!"* — and six were wrong, in three prompts, and **each of the six forbade the
gate its own task named**: `zig build test` in two, *"verify by running the command
yourself"* in the third. A refusal is what turns such a misparse into a lost run:
`1d31473d` read *"do not compile"* in a description, refused its own builds for 21
minutes and 2,215,762 tokens, and was rolled back.

**Two changes, the operator's choice of both (plan D2, option (c)).**

1. **A negation reaches to the end of its sentence.** All six errors had one
   mechanism: a negation joined to a word in the *next* sentence — *"Do not touch
   build.zig. Verify by running"* became *do not run build commands*. The scanner
   used to look at the 95 characters after a negation wherever they fell; it now
   stops at a sentence's end (`.`, `!` or `?` before white space, a blank line, or
   a new list item). A dot inside `build.zig` does not end a sentence, and neither
   does a single line break in hard-wrapped prose. On M730's sample this alone keeps
   the three right constraints and drops the six wrong ones.
2. **An inferred constraint advises.** It is rendered in its own section of the
   system prompt, *"Inferred from the request (ADVISORY — not enforced)"*, which
   says where it came from and asks the model to report it if it acts against one.
   A call that goes against it **runs**; the first such call per rule in a turn
   gets a note in its result naming the rule. The adoption notice says
   `ADVISORY for THIS SESSION (not enforced, not saved)`.

**What the operator sees.** Every call that goes against an inferred rule is a
`constraint_advisory` event in the run journal, and `jichi runs` shows `advised=N`.
That count is the rate at which a guessed rule met the work — the number the
decision was taken without, measured from now on by every `--auto` run with a
journal.

**What did not change.** An authored constraint refuses exactly as before, and it is
checked first, so a guess sitting beside a policy can never turn the policy's
refusal into advice. The M459 rule still holds, one step softer: on a path the
operator's `--edit-scope` names, an inferred read-only is not even advised against,
and that is announced. To make an inferred rule binding, add it with
`/constraints add` or write it into the store.

## What the scanner hears, measured again (M743)

**Why again.** The 2026-09-24 zigodot drive's ten complete arms made 240 `--auto` runs and journalled
**no** `constraint_advisory` event at all, although sixteen of its read-only tasks
end *"Do not modify any file."* The zero measured the scanner, not the work: its
read-only rule knew one verb, *edit*. So M743 measured the scanner again, on the
drive machine's store: every **distinct user message** in it (every turn, since
`--auto` scans every turn, not just the first, and in both of jichi's store layouts
and the Continue-format sessions it inherited), plus both drives' task files, 656 in
all. It inferred 73 rules and **23 were wrong**, by nine mechanisms, none of which
crossed a sentence. M730's 289 prompts came from the workstation's store, so this is
a second population, not a re-run of the first; on this machine's version of M730's
population (first messages only) the scanner inferred six rules, all six wrong. The page is [analysis/2026-09-25-the-scanner-read-again.md](analysis/2026-09-25-the-scanner-read-again.md).

**What it hears now.** Each row comes from a sentence in that corpus, and
`tests/test_constraint.c` quotes the sentence.

| The scanner now reads | For example | It used to |
|---|---|---|
| *modify*, *change* and *touch* as edit verbs, the plural *make any changes*, and an object after *to*/*in*/*on* | "Do not modify any file."; "Make NO edits to any file." | infer nothing |
| *other*, *else* and *but* as narrowing words | "Do not modify any other file."; "Do not touch anything else." | infer read-only |
| *code*, *source* and *src* as a **kind** of file, and a kind as a scope | "Do NOT edit any source; your only file output is the design doc" | infer read-only |
| a backticked, quoted or bracketed object as a **named** one | "Do NOT modify \`src/root.zig\`." | infer read-only |
| a verb list up to its one object | "Do not edit, create, or modify the parser." | infer read-only |
| a subject before *do not* as a statement | "These commands do not modify any files." | infer read-only |
| *read-only* after an article as an adjective | "remains the read-only reference" | infer read-only |
| a cue glued to a hyphen or a letter as part of a word | "forces never-gated dead code to COMPILE" | ban the build |
| *not just* / *only* / *merely* / *simply* as asking for more | "(do not just build blindly)" | ban the build |
| the end of a **clause** -- a semicolon, a spaced `--`, an em or en dash -- as the end of a negation's reach | "do NOT hardcode an external absolute path -- the test must be portable), run it through" | ban the tests |
| *make* as the build tool only when it is run | "Do not run make." (a ban); "Do not make changes to prepare_sourcedata.py." (not one) | ban the build |
| a keyword followed by a file extension as a file name | "Do NOT modify \`src/root.zig\`." | ban privileged commands |

**On the same corpus:** 69 rules, 2 of them wrong (both open in
[DEFERRED](DEFERRED.md)), and every one of the sixteen drive tasks now infers the
read-only rule. To measure a scanner change the same way:

```sh
python3 tests/measure/scanner_corpus.py ./jichi --against /path/to/old/jichi
```

It prints only the messages whose verdict moved, with the line each rule came from;
read every one of them. The corpus holds the operator's own prompts, so the script
writes it to a temporary directory and removes it.

**The trade.** *"Do not modify the code."* no longer infers read-only: *code* names
a kind of file, and a brief that writes a document says *"do not edit any source"*.
M207's rule still decides the direction. A missed rule gives up a backstop for an
instruction the prompt still states in full. A false one tells the model something
the operator never said. Both only advise now, but the model reads the advice.

## What a request quotes or asks, and the words a rule came from (M872)

**The case.** The demos drive's run 0091 (2026-10-02) carried its supervisor's question
`should a task text say "write REPORT.md, do not commit it", or name a per-task file in /tmp?`,
and jichi told the lead agent, whose job is to commit merges, for the whole session: *do not use the
tool `git_commit`, do not commit*. The notice could not show why: it named the canonical rules, and
"do not commit" was in no message.

**What the scanner skips.** A cue it would read as an order infers nothing when the request
**quotes** it or **asks** it:

| Skipped | For example |
|---|---|
| a fenced code block | a pasted script whose comment says `# never push from here` |
| an inline code span | `` `tool disabled in read-only mode` `` |
| a closed pair of double, single or typographic quotes in one paragraph | *the note says "do not commit it"* |
| a sentence that ends in `?` | *Do not commit?* or *Should I keep this read-only?* |

A straight quote opens only where a word may begin and closes only where one may end, so the
apostrophes in *don't* and *users'* do neither, and an inch mark (*the 3" screen*) or a quote left
open quotes nothing. A cue outside the quotes still counts in the same sentence (*the note says "keep
it short", so do not push*), and so does a target quoted after an unquoted cue (*never invoke
"make"*, M743).

**The words a rule came from.** Every inferred rule keeps the sentence it was read from, as written
(blanks collapsed; at most 200 bytes, a window around the cue with `...` where cut), and every place
that names the rule quotes it:

- the adoption notice: ``... (not enforced, not saved): do not use the tool `git_commit`, do not
  commit (from "Do not commit.") -- the model is told ...``;
- the system prompt's advisory block: `- do not commit (read from: "Do not commit.")`;
- the note on a call that goes against it, and its `[constraint] ... went against` line;
- the run journal: `from` on `constraint` (the sentences) and on `constraint_advisory` (the rule's);
- `jichi constraints scan` (`read from: "..."`), `brief-check` (`from line 3: "..."`) and `/constraints`.

**Measured** against M871 with `tests/measure/scanner_corpus.py`: on the workstation's sessions, 761
distinct messages, no verdict moved; with the internal notes and the drives' task texts, 816 messages,
3 moved and none added (rules 38 to 33), each a quoted phrase the scanner had read as an order.

**The costs.** A rule the request quotes because it means it (*the policy: "never push to main"*)
infers nothing, and nor does an order put as a polite question (*can you make sure you don't push?*).
The request still says both; the restatement and the per-call note are what is lost, and an inferred
rule only advises. A rule that loses its sentence's object still does: *"Don't commit the build
output."* infers *do not commit* -- the quote shows the object, it does not narrow the rule.

## Who a read-only phrase speaks of (M878)

**The case.** A supervisor's note for the demos drive's lead said a child "runs in read-only mode", and
`jichi constraints scan` adopted a session-wide read-only rule from it -- for a lead whose job is to
merge. The cue before the phrase ("in", "stays") says how, not who.

**What the scanner asks.** Where a cue has made "read-only" an instruction, the clause holding it
decides:

| The clause | Reads as | For example |
|---|---|---|
| starts with a verb whose doer is in the mode | an order | *Stay read-only.* *Review the code in read-only mode.* |
| speaks to the reader, or names the reader's own scope | an order | *You must stay read-only.* *This session is read-only.* *Everything stays read-only.* |
| a verb puts the reader's own scope in the mode | an order | *Run everything in read-only mode.* *Keep this read-only.* |
| a verb puts someone else in the mode | a statement | *Run the children in read-only mode.* *You must keep the workers read-only.* |
| names someone else | a statement | *The child runs in read-only mode.* *Your children run in read-only mode.* |
| is taken by "make sure", "check that", "ensure" | judged on its own | *Make sure the child runs in read-only mode.* is a statement |
| is an aside behind `,`, `(` or a dash | judged by what it interrupts | *figures from this machine, read-only, 2026-08-27* is a statement |

A role after the phrase makes it an adjective, as an article before it does: *Read-only children have
no shell.* The clause runs back to a sentence's end, a blank line, a heading, a list item's line, a
table's cell, or a `;`, `:`, `,`, `(` or a dash, so a markdown note is read a block at a time: a
statement under a heading, in a blockquote or in a table's cell is a statement.

**Measured** against M877: on the workstation's sessions, the internal notes and the drives' tasks,
816 messages, none moved; the one read-only order that corpus holds on this path is kept. On jichi's
own docs, 617 paragraphs that mention read-only, the rule was read from 39 and is read from 6.

**The costs.** Where the clause cannot be told it reads as a statement: *Rules -- read-only, no
commits.* infers nothing, and the request still says it. A paragraph that opens with "Read-only ..."
(*Read-only tasks investigate the live workspace*) is still read as an order, and so is a list item
that continues its lead-in's subject (*The children:* then *- run in read-only mode*) -- DEFERRED
def-312.

## Turning inference off: `constraintsAutoAdopt` (M874)

```json
{ "constraintsAutoAdopt": false }
```

turns off reading rules out of an `--auto` request. It is for a harness that assembles its prompt from
several documents -- a standing prompt, a handoff, a supervisor's notes -- where a "do not ..." in any
of them would otherwise become a rule for the run. Rules you wrote, in `.jichi/constraints.md` or with
`/constraints add`, still load and bind.

- **On by default**, with a config file or without one. It is read as a person writes it: `false`,
  `0`, `"off"` and `"no"` turn it off; anything else leaves the default.
- **Who may turn it off:** `--config`, `$JC_CONFIG` and your own `~/.jichi`. A project config
  (`.jichi/config.json`) may leave it on, but its `false` waits for `jichi trust` (M851), and the
  notice names it until then.
- **Where it shows:** `jichi doctor` lists "constraint inference off (constraintsAutoAdopt)" when it is
  off, and `jichi brief-check` adds that an `--auto` run under that config adopts none of what it
  found.

## The store: `.jichi/constraints.md`

Plain, human-editable, one directive per line (like `.jichi/memory.md`). It holds
**authored** constraints only:

```
# jichi constraints -- enforced every turn (M110). One per line.
deny-cmd build
deny-tool run_tests
read-only
note prefer small, reviewable commits
```

Edit it by hand, or manage it from the TUI:

- `/constraints` — list the active constraints.
- `/constraints add <text>` — recognize + enforce constraints from a phrase.
- `/constraints clear` — drop them all.

`sysmsg` shows the injected block, so you can confirm exactly what the model sees.

## Observability

Each refusal logs `[constraint] refused <tool>`, surfaces via the status line in the
TUI, and — when telemetry is on — emits a `constraint` event (with the tool name),
so a driving agent can see "blocked N build attempts" in the log.

## Checking a brief *before* you spend the run (M327)

```sh
jichi constraints                     # list this workspace's persisted store
jichi constraints scan brief.md       # what WOULD this prompt get adopted?
jichi constraints scan -              # ... from stdin
```

`scan` runs the same pure `jc_constraint_scan` the agent runs, offline and with no
model call, and prints each constraint it would adopt by kind and subject. **Exit 0
means nothing would be adopted; exit 1 means something would** — a finding to
review, not necessarily an error, since a brief that deliberately says "work
read-only" is *meant* to adopt one. The polarity is chosen so a supervisor can gate
a brief on the exit code, and it follows `doctor`, where the exit code reports
findings rather than whether the check ran.

**Why this exists.** Auto-adopt is on in `--auto`, which is exactly where nobody is
watching. Before this, the only way to learn what a brief would produce was to start
the run and read the `[constraint]` line — on a long unattended drive, that means
finding out after the budget is gone. One recorded misfire cost a 1.56 M-token run.
The scanner was already pure and unit-tested, so predicting it costs a file read.

It earned itself immediately: swept over thirteen briefs written for one project, it
confirmed two misfires their operator had diagnosed by hand from run output, **and
found a third nobody had noticed** — a brief that silently banned every build
command for the whole run.

Use it on any brief you are about to hand to `--auto`, and reword until it is clean
unless a reported constraint is one you meant. The reword is always the same move:
replace the prohibition with a positive scope and let `--edit-scope` /
`--reference-root` carry the boundary, since those are enforced identically and
cannot misparse prose.

`tests/smoke/constraints_scan.sh` keeps the predictor honest by comparing it against
the adoption notice from a real run over the identical prompt — a predictor that
drifts from what it predicts is a green light that means nothing.

## Scope & limits (v1)

- Extraction is keyword-based (a negation cue + a known target), deliberately
  conservative; the explicit `/constraints add` and hand-edited store are always
  exact. Because extraction can misfire, what it infers never persists (M169) and
  is always announced by name (M167d) — read that line when a run does less than
  you expected.
- Enforcement can only *narrow* (like hooks and the path fence) — it never widens
  permissions.
- The scanner reads a cue the request quotes or asks about as mentioned, not said
  (M872), and misses a rule whose cue and verb a line break separates (DEFERRED
  def-308).
- `deny-cmd` matches the command text when you wrote the rule, and what the command
  runs when jichi inferred it (M873: its command word, the action of a program that
  takes one, a `make` target -- so `grep -n install build.zig` does not go against an
  inferred "do not install packages", and `pip install x` does). For a rule you wrote,
  a determined workaround via an unusual alias isn't fully caught. Pair with the
  edit-scope fence and permissions for defense in depth.

See also: docs/AGENT_MODES.md (permissions), docs/AUTONOMY.md (edit-scope /
envelope), docs/COMPACTION.md (why the system prompt survives).
