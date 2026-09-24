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
`/constraints` marks each one `[saved]` or `[this session]`.

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
- `deny-cmd` matches the command text; a determined workaround via an unusual alias
  isn't fully caught. Pair with the edit-scope fence and permissions for defense in
  depth.

See also: docs/AGENT_MODES.md (permissions), docs/AUTONOMY.md (edit-scope /
envelope), docs/COMPACTION.md (why the system prompt survives).
