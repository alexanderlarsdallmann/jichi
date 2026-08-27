# Vocabulary — the words this project uses

Every project builds a private language, and every private language is a tax on
the newcomer. This page is the receipt. It defines the words jichi's own
documentation uses **before** it explains anything, so you can read any page here
without inferring meanings from context.

> **Not to be confused with [GLOSSARY.md](GLOSSARY.md)**, which documents a
> *feature*: the `.jichi/glossary.md` file, where **you** define **your** project's
> domain terms so the agent speaks them. That page is about a config file. This
> page is about our words.

If a term you met is missing, that is a defect in this page — the honest kind, and
worth reporting.

## Talking to a model

- **turn** — one exchange: your message, everything the agent does in response
  (including many tool calls), and its answer. Budgets and notices are mostly
  per-turn.
- **token** — the unit a model reads and bills in; roughly ¾ of a word for English
  prose, far less for code. Every "cost" in this project is tokens.
- **context window** — how many tokens a model can hold at once. `contextLength`
  in your config *declares* it; declaring it larger than the server's real window
  means your budget is unenforced until the server rejects the request.
- **system prompt** — the instructions jichi sends ahead of your conversation:
  who the agent is, what tools exist, the repo map, your rules and memory.
  `jichi sysmsg` prints it; `jichi context` sizes it.
- **role** — what a configured model is *for*: `chat`, `embed`, `rerank`,
  `audio`, `transcribe`, `vision`. One entry can hold several. jichi picks a model
  by role, so "no embed-role model" means semantic search is simply off.
- **routing tier** — `fast` or `strong`: a cheap model for mechanical work, a
  capable one for reasoning, chosen per call.
- **compaction** — summarising older history to stay inside the context window.
  It happens between turns, and *mid-turn* when a single turn overflows.
- **elision** — dropping the middle of a too-large tool output rather than
  truncating it silently. What is dropped leaves a **claim ticket**: a marker the
  model can redeem to read the omitted part.
- **quantized** — a model shrunk (4-bit, 8-bit …) to run on modest hardware, at
  some cost in reliability. Most local models you download are quantized.

## Who may do what

- **posture** — how much the agent may do *without asking*. The three modes,
  widest first: `auto` (approve everything permitted), `chat` (ask before
  changing anything), `plan` (read-only; nothing changes at all).
- **verdict** — the resolved answer for one tool call: **ASK**, **ALLOW** or
  **DENY**. See [TOOL_DECISIONS.md](TOOL_DECISIONS.md) for how the six mechanisms
  compose into it.
- **fence** — a boundary that refuses rather than warns. The *path* fence keeps
  file tools inside the workspace; the *tool* fence limits which tools an agent is
  offered; the *edit-scope* fence limits which paths a run may write.
- **reference root** — an external directory you explicitly allow **reads** from
  while the fence is on. Writes stay in the workspace.
- **privileged gate** — the separate check for a model-issued `sudo`/`doas`/
  `pkexec`/`su`/`run0`, applied *after* the verdict, so no blanket approval can
  cover it.
- **kinetic** — a tool whose call moves mass or energy in the physical world: a
  motor, a valve, a siren. Gated and audited like a privileged command.

## Unattended runs

- **envelope** — the whole safety boundary around an unsupervised run: budgets,
  scope, a verifier, rollback, and a journal.
- **budget** — a hard cap: tokens, wall-clock (`--deadline`), tool calls, reads.
  Hitting one stops the run.
- **verifier** — the command whose exit status defines success (`--verify`,
  or `testCommand`). A verifier you have never watched **fail** is not a gate.
- **green** — the verifier passed. A **green checkpoint** is a workspace state
  known to pass, and the point a failed run is rolled back to.
- **hollow green** — a "pass" that proves nothing: an empty test suite, a verifier
  that cannot fail, a gate that ran 0.5% of its checks. jichi now tells the model
  when its success was empty.
- **checkpoint** / **snapshot** — a saved workspace state in a *shadow* git
  repository under `~/.jichi.d/checkpoints/`. Your own `.git` is never touched.
  `/undo` walks back through them.
- **rollback** — restoring the last green checkpoint when a run ends red.
- **fix-forward** — feeding parsed test failures back to the model for another
  attempt before giving up.
- **baseline** — the commit a run started from; the reference for "did anything
  actually change" and for what counts as out-of-scope.
- **journal** — a run's JSONL record of every decision taken in your absence
  (`jichi runs`). **Telemetry** is the opt-in metrics log; the **audit log**
  records privileged and kinetic attempts and is always on.
- **drift** — a claim that used to be true. A number nobody re-measured, a doc
  describing an older behaviour, a translation tracking an older revision.

## How this codebase is built and checked

You need these to read the source-reading guides, the tests, or any analysis note.

- **pure core / thin shell** — the design rule: decision logic in functions with
  no I/O (testable offline, exhaustively), with the smallest possible layer around
  them that touches the world.
- **chokepoint** — the single place every path must pass through, so a guarantee
  can be enforced once instead of at every call site.
- **seam** — a deliberate place to observe or substitute behaviour, usually so a
  test can stand where the outside world would.
- **invariant** — something that must be true at all times ("the arena is never
  freed mid-turn"). A test that pins one is worth more than ten that sample
  behaviour.
- **immutable** — never modified after creation. Cheaper to reason about, because
  no other code can have changed it behind your back.
- **lint** — a check that reads the *source or docs* rather than running the
  program: "every documented flag exists", "every tool name is registered". This
  project prefers a lint to an audit, because an audit is a person's afternoon and
  a lint is a build failure.
- **TAP** — Test Anything Protocol: the `ok 3 - <description>` / `not ok 3 - …`
  output format the smoke tier prints. A plan line (`1..8`) says how many checks
  to expect, so a suite that dies early cannot look green.
- **smoke tier** — the POSIX-sh test suite (`tests/smoke/`) that drives the real
  binary end to end: 217 drivers, ~1,200 checks, no Python, so it runs on a
  256 MB box and on four kernels.
- **two-sided proof** — a new test must be shown **failing** without its fix and
  passing with it. A test that has never been red proves nothing about the bug it
  claims to cover.
- **floor** — an assertion under a test's own extraction ("this scan found ≥ 400
  files"), so a check that has silently stopped looking fails instead of passing.
- **vacuous check** — a check that cannot fail: it greps for something always
  present, or asserts on a status a typo already produced. Several are documented
  in this project's own history, which is why floors exist.
- **rig** — a script that installs an operating system in a VM or on a board,
  builds jichi there, runs the tiers and reports. **Tier** is a level of the test
  pyramid (unit, smoke, e2e, platform).
- **dogfooding** — driving jichi at a real project to find jichi's defects. Most
  of the interesting bugs in this tree were found that way, not by inspection.
- **register** — a table that records decisions, deferrals or notices so they stay
  findable: `DECISIONS.md`, `DEFERRED.md`, `NOTICES.md`. If it is not in a
  register, it is folklore.
- **milestone (`M###`)** — one unit of work with a design note, tests, docs and a
  scoped commit. The ROADMAP is the per-milestone engineering record.

## Learning with jichi

- **assignment / spec** — one markdown file that is both the brief you read and
  the machine-checkable task; its `verify` line grades you.
- **hint ladder** — a spec's graded nudges, one **rung** at a time via `/hint`.
  Free, recorded, never penalised.
- **grade** — run a spec's verifier and score it. **attempt** is the *agent*
  solving the spec instead of you — useful for comparison, not for credit.
- **tier** (in a brief) — the audience framing: junior, student, senior, agent.
- **tutor stance** — while an assignment is active, the model guides and declines
  to hand over the solution.
- **gate** (in the curriculum) — a stage's mechanical exit condition: points plus
  a written record.
- **record** — your own debugging log: symptom, dead ends, root cause, lesson.

## See also

- [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md) — jichi explained without jargon at all
- [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — the permission chain, in order
- [STATE.md](STATE.md) — where everything jichi keeps actually lives
- [GLOSSARY.md](GLOSSARY.md) — the feature for **your** project's terms
