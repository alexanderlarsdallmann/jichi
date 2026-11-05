# The context relay: one long task, more than one session

**Who this page is for.** A self-learner who wants to understand what a long
headless run can outgrow, and a junior developer deciding which of jichi's
unattended shapes fits a task. You already know
[AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md) (the task queue) and
[AUTONOMY.md](AUTONOMY.md) (the envelope); this page is the missing row in the
table between them: **the task that is too big for one context window but too
small for a queue**.

## The problem the relay exists for

A headless run is one long turn. When its context window fills, the mechanisms
that hold it — mid-turn elision and the pressure note — can keep it alive for a
while, but they cannot buy more window: the elided output has to be re-read or
re-done, a long run ends on the gateway's hard limit with nothing written down,
and a crash at the worst moment leaves no record the next session can start
from. The project measured all three (the review of the 2026-10-08 zigodot
drives, `docs/analysis/2026-10-11-context-management-analyzed.md`).

A human meets the same problem at the end of a shift and writes a handover note
for the next person. The relay does that mechanically: a **leg** (one session
running the task) ends at a clean point, writes a **baton** (notes the next
leg can start from), and the task continues in a **fresh session** with a full
window. What is carried is the *state of the work* — the task verbatim, what
changed, where the tree stands, the last verify result, the model's own account
— not the conversation.

## Where it sits

jichi gives you several shapes for non-interactive work. Pick the smallest one
that fits, in this order:

| You want to… | Use | What ends the work |
|---|---|---|
| Run **one** bounded task that fits one window | a single `jichi --auto … -p …` ([DEPLOYMENT.md](DEPLOYMENT.md)) | the task, or the envelope |
| Serve **many quick** requests on a warm box | the [daemon](DAEMON.md) | each request |
| Fan **one** task across fixed inputs | [`workflow`](WORKFLOWS.md) | the spec's stages |
| Work a **growing queue** of independent tasks | the external supervisor loop ([AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md)) | the queue, per task |
| Push **one long task** through more context than one window holds | **the relay (this page)** | the task, across legs |

The last row is the one the queue loop cannot serve: a queue's worker is a
*fresh, independent* run per task by design, and a task that outgrows its
window dies at the hard limit with no handover. The relay continues *the same*
task, across legs, under *one* envelope.

**It is a loop, and that is a decision.** jichi used to ship no built-in loop at
all — scheduling and persistence were left to the OS, because for *many
independent tasks* that is the strongest blast-radius boundary. The operator
overturned that rule for exactly one shape, in 2026-10 (the decision row is in
[DECISIONS.md](DECISIONS.md)): a loop over **one** task, bounded by the
task's own end, the envelope, and a leg ceiling. The three reasons the old rule
gave (scheduling, persistence, isolation between tasks) all concern *many*
tasks; a relay continues one, inside one envelope, with the same fences. The
queue loop is unchanged, and the relay is not a replacement for it.

## The two topologies: who starts the next leg

The same relay, two shapes. You choose per run; they are not a version
difference, they are an option.

### Topology A — handover only (the supervisor continues)

The leg ends when its window is exhausted: jichi writes the baton, explains the
stop reason, and exits. A **supervisor** — your shell loop, your CI, a person —
starts the next process with the baton as its seed.

- **Pros:** every leg is a fresh, independently bounded process (the OS gives
  isolation for free); it fits the existing supervisor tooling; it is the shape
  the queue loop already runs.
- **Cons:** it needs a supervisor, and it cannot run where there is none — a
  single CI step, an embedded device, a nested subagent.

### Topology B — in-process legs (jichi continues)

The leg ends as above, and **jichi starts the next leg itself**, in the same
process: same window of work, same fences re-applied at every leg start, with
two bounds the process cannot outgrow — at most a ceiling of legs, and a
memory guard (if the process has grown too much, B stops and hands over to a
supervisor instead of starting another leg in place).

- **Pros:** works with no supervisor; works for subagents, which have none; the
  process stays warm (index, LSP, MCP connections survive across legs).
- **Cons:** one process lives longer, so its memory carries over between legs —
  which is exactly what the ceiling and the memory guard are there for.

Choosing: **A when something is already supervising** (a queue worker, CI, a
human watching); **B when the task runs alone** (one unattended process, a
child agent). The baton, the stop reasons, the envelope and the fences are
identical in both; only *who starts the next leg* differs.

## Use cases, and which topology each wants

**1. The overnight drive (B).** A multi-hour task on a machine with no daemon
and no queue — the kind the 2026-10 zigodot drives ran by hand. Topology A
would have worked, but the harness's shell supervisor was the part that kept
dying; B removes it. The legs carry the work; the baton is the record.

**2. The CI step that cannot assume a shell (B).** A pipeline job runs one
bounded task with a verify gate, and the job itself is the whole environment:
no second process is available to act as supervisor. B's in-process legs run
until the verify is green or the envelope is used, inside the one job.

**3. The nested subagent (B, the only option).** A `spawn_subagent` child is
in-process and has no supervisor above it; its context can exhaust mid-task.
The relay's child form is B running inside the child, with the hand-back to the
parent staying one capped answer.

**4. The queue worker that outgrows its window (A).** The external loop
already supervises: a worker whose task is bigger than one window ends as a
handover, and the supervisor starts the next leg from the baton — per-leg OS
isolation kept, the queue's retry policy intact.

**5. The person-in-the-loop handoff (A).** An interactive or TUI run offers the
relay at the end of a turn when the window is exhausted, instead of ending on
a cut answer: the next leg starts from the baton with the same envelope. The
person is the supervisor.

**6. One bounded task in CI (no relay).** A task that fits one window under
one envelope does not want a loop at all; the single-run shape is the answer,
and the relay off is the default for good reason. **7. The queue of many
small tasks (no relay).** Each fits one window; the external loop is the
shape, and adding a relay to workers that never exhaust the window would buy
nothing but a longer-lived process. **8. A long task whose *definition* is the
loop** (monitoring, polling, a daemon) — that is a daemon or a scheduled run,
not a relay: the relay continues *one task*, and its legs end because the
task is done, not because a timer fired.

The test for each case is the same question: **when is this work finished?**
If the answer is "when the window is full", the task was mis-shaped. If the
answer is "when the verify is green / the task is done", and the window might
fill before then, a relay fits — A if a supervisor exists, B if not.

## How a relay ends (both topologies)

A relay stops when one of these is measured, never guessed:

- **the task is done** — the run's verify gate is green (or the model finishes
  with nothing left); `done`;
- **the envelope is used** — the operator's budgets (tokens, wall-clock, tool
  calls) are the relay's, summed over all legs, never re-armed per leg;
  `budget`;
- **the leg ceiling is reached** — at most a bounded number of legs per relay;
  `relay_stalled`, reason `max_legs`;
- **two legs in a row made no progress** — no commit, no file change, no todo
  advance, no verify change; `relay_stalled`, reason `no_progress`;
- **a leg cannot start** — the notes cannot be written, or (B) the process has
  grown too much for another in-place leg: the relay hands over to a
  supervisor instead, `handover`;
- **the operator aborts** — `interrupted`, with the baton written on the way
  out.

The last five are the "until a constraint, or a stop condition, is met" half
of the relay's contract: it loops **until**, and the until is always one of
these, journaled with the numbers that triggered it.

## What it never does

The relay is an *option with bounds*, not a permission. It changes who starts
the next leg; it changes nothing else:

- **It never widens a fence.** Edit scope, path fence, permissions, the
  privileged gate, the shell fence — every one is re-applied at every leg
  start. The baton cannot carry a permission: its notes are data, the model's
  account is labelled and wrapped as untrusted, and secrets pass the same
  redaction as the session stream.
- **It never re-arms the budget.** One envelope per relay, summed over legs;
  a five-leg relay can spend the operator's fence, not five times it.
- **It never changes the model.** Every leg runs the run's model.
- **It stays off unless asked.** The default is the single-run shape; the relay
  turns on per run, and an untrusted project can turn it *off*, never on.

## What is built, and what is in design (honestly)

This is the page's one status table, and it changes as milestones land:

| Piece | State |
|---|---|
| The decision core (when a window is exhausted, by measurement) | built |
| The baton (mechanical notes + the model's account, written at a handover; `relayNotesBytes` caps the notes' size, default 8 KB) | built |
| The next leg from a baton (a fresh session seeded with it) | built |
| The explained stop reasons for a handover and a stalled relay | built |
| The leg budget (a leg ends at its measured capacity, not the fixed fence) | built |
| Topology B's in-process legs (a plain headless run continues its next legs in the same process: the `relayInProcess` switch, default off, up to the `relayMaxLegs` ceiling) | built |
| Topology B's memory guard (a leg ends as a handover before the process grows past four times its first leg's resident set) | built |
| The leg's surfaces (the session store carries a leg's `relay_id`/`leg`/`prev_session`/`notes_path`, so the chain reads from the store alone; each leg journals `leg_start`/`leg_end`) | built |
| The supervisor's exit code for a handover, and the control-channel verb | designed, deferred (operator calls) |

If you are reading this before the "in design" rows have landed, the page
describes the whole shape and the built rows are what your binary has; the
milestone entries in [ROADMAP.md](ROADMAP.md) record each row as it flips. The
working design for the two topologies, the use cases above and the decision
record live in the project's record: the design proposal in `docs/proposals/`
(search it for "context relay"), the internal design page for the next slice,
and those milestone entries.

## Read further

- [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md) — the external queue loop this page extends, and the "no built-in loop" decision the relay overturns for one shape.
- [AUTONOMY.md](AUTONOMY.md) — the envelope: what bounds every leg.
- [COMPACTION.md](COMPACTION.md) — the mechanisms that hold a leg alive until the relay takes over.
- [CONTROL.md](CONTROL.md) — steering a leg mid-run.
- [RESUME.md](RESUME.md) — what a fresh session inherits, and how the baton differs from a resume.
- [QUEUE_PROTOCOL.md](QUEUE_PROTOCOL.md) — the queue's protocol, for the topology-A worker.
- [DECISIONS.md](DECISIONS.md) — the overturn, with its rejected alternatives.
