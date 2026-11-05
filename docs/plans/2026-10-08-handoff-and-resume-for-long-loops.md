# Plan: handoff-and-resume for long development loops

**Date:** 2026-10-08. **Status:** proposed, not started.
**Trigger:** context-window pressure on a long-running session (this one), and
the question *"would calling a subagent help — or do we need first-class
store-and-resume?"*

---

## 0. The problem, precisely

A jichi development session (or a drive, or a capstone) runs long. Its model
context fills. When it does, three things can happen, and none is good:

1. **Elision.** Old tool output is dropped from the window to make room. The
   model loses the *details* (a commit hash, a measured figure, a decision and
   its rejected alternatives) even though it knows it *had* them. It then
   re-derives or re-measures — or, worse, re-asserts a number it can no longer
   see, and says "I verified X" when X fell out of the window.
2. **Compression.** Mid-turn compaction summarizes old history. Summaries
   preserve *gist*, not *facts*: a milestone's exact commit, a test's red-first
   count, the wording of a deferral. The resumed session is confident but
   under-informed.
3. **Restart.** The session is abandoned and a fresh one started. Unless the
   work was persisted, the new model starts from zero: it does not know what is
   done, what is decided-but-not-merged, what the gate status is, or why a
   choice was made. (This is exactly the situation the peer handoff note we
   processed this morning put us in — and it worked *only* because a human
   wrote a careful `local/` note with verified claims.)

The failure mode in all three is the same: **the work survives, the *record of
the work* does not, and the next context window is left to reconstruct it.**

A long development loop is not one session. It is many context windows in
sequence, each of which must be able to pick up where the last one left off
without re-deriving, re-measuring, or mis-remembering. That is what this plan
builds.

## 1. What already exists (and is reused, not reinvented)

None of the pieces are new; the plan composes them into one flow.

- **`.jichi/memory.md`** — durable notes, loaded at the start of every
  session. Good for *facts worth keeping across sessions* (a convention, a
  decision). But it is a free-text note file: the model appends to it, it is
  not structured, not scoped to a task, and not something a *fresh session is
  handed a pointer to* and told to resume.
- **Checkpoints** — a files snapshot in a shadow git repo, restorable via
  `/undo`, walkable via `/rewind`. These recover *the tree*, not *the
  reasoning*. A checkpoint tells you what the files were, not what was decided
  or why.
- **`--session` / `--resume` / `-c`** — resume a *persisted conversation* by
  id. This resumes the *history* (subject to compaction, point 2) — the raw
  transcript, not a distilled handoff. For a long loop the transcript is often
  exactly what is too big to fit in the next window.
- **The M911 preservation store** (`$HOME/.jichi.d/tool-output/<pid>/`) — a
  0600 directory where oversized tool output and capped subagent answers are
  written, named in the marker that replaces them. This is the *elision
  contract*: "this byte was dropped, and it is here."
- **`spawn_subagent` / `spawn_parallel`** — a fresh context window that does
  one bounded task and hands back a *capped* answer. Already a form of
  "new session, narrow scope" — but the child is disposable and the hand-back
  is short. It is the right tool for *a subtask*, the wrong tool for *the
  whole loop*.
- **`docs/internal/`** — the gitignored, export-ignored scratch space for
  exactly the kind of note the morning handoff was. The pattern exists and
  worked; it is just manual and unstructured.

So the gap is not storage — jichi has several. The gap is a **disciplined
transition**: a first-class action that (a) captures the *current* window's
state into the durable record, (b) hands the next window a pointer to it, and
(c) makes the next window *resume from the record*, not from re-derivation.

### A sibling design exists — read it before implementing

A second machine is working the same problem from the other side, and its
work is on a branch: **`origin/threadwork/2026-10-08-review-and-context-relay`**
(`docs/proposals/2026-10-context-relay.md`, ~1,243 lines, plus a 2026-10-08
Claude Code audit, an experiment log, and a **measurement**: an 80/80 manual
relay across four fresh sessions where the previous single-session attempt
died on a stall). Its framing is deeper than this plan's and should be read
as the primary design input:

- **Legs, not sessions.** A *leg* is "the agent + context, from first prompt
  to handover"; a *handover* is "the moment the work moves from one leg to
  the next." The relay is N legs, and the failure mode is at the *seams*.
- **The note is written by machinery, not by the model** — `jichi handover`
  emits a `relay-note` (phase, attempt, budget, cost, next-thing, decisions,
  untested-changes) even from a *crashed* leg, because the model is exactly
  the party that is about to be unable to answer.
- **The trigger is an envelope, not a model judgment.** "jichi picks the
  moment, not the model" — a measured stall (the experiment's 11,677-second
  run) is the trigger, so a run that is *about to* exhaust itself is the one
  that is relayed, rather than one that believes it still has time.
- **Each leg gets its own budget and checkpoint**, and `make ci` is the gate
  per leg (never per relay).

**Where this plan and the proposal diverge, and what to do about it.**
- This plan's P0 (a hand-written `.jichi/handoff.md`, the shape §2.1
  describes, with verify-on-arrive) is a **subset** of the proposal's P0
  (the relay note) — and the proposal has already *done* that subset: the
  80/80 experiment **is** the evidence the handoff shape carries a long loop.
  The next session should not re-run it; it should *adopt* the note format
  and the seam checklist.
- This plan's `/handoff` + `/resume` pair and the proposal's `jichi handover`
  + `jichi --resume` are the same action described from two sides. The
  proposal's `handover` (model's part written *now*, mechanically written
  even on crash) is the stronger design; `/resume`'s verify-on-arrive is a
  detail the proposal's "the next leg starts from the note" implies but does
  not state, and it is the detail that caught the F-J3 defect (a register
  commit claiming a gate that had not run). Keep both.
- This plan's "subagent as context valve" and the proposal's "leg 0" (the
  fresh-session recon subagent) are the same mechanism; the proposal names
  it and the experiment measures it.

**Recommendation for the next session:** treat
`docs/proposals/2026-10-context-relay.md` (on the threadwork branch) as the
design of record; this plan is the shorter statement of the same gap and the
place to record what the threadwork proposal does *not* cover (the F-J3
lesson on register integrity, the "pointers not claims" rule, and the
subagent-as-valve pattern for *read-only* heavy lifting within a leg).

## 2. The proposed unit: a **handoff**

A **handoff** is a small, structured, versioned note — the bridge between one
context window and the next. It is *not* the transcript, *not* a summary of
the whole session, and *not* a file-snapshot. It is the thing a competent
colleague writes at the end of a shift: what is done, what is decided, what
remains, what must not be re-litigated, and the exact references (commits,
test counts, deferral ids) to re-ground on arrival.

### 2.1 Shape

A handoff lives at a stable, discoverable path:
`.jichi/handoff.md` (per-project, next to `memory.md`). Optional richer data
(the full evidence, a large transcript excerpt) goes to the preservation store
or `docs/internal/` and is *named in the handoff by path*, never inlined —
the handoff stays small enough to fit in one window, always.

```markdown
# Handoff — <task name>, <UTC date-time>

## Goal
One or two sentences: what the loop is trying to finish.

## Done (with evidence)
- <thing> — commit <hash>, gated (make ci OK / 15,090 checks 0 failures).
- <thing> — deferral <def-NNN> closed.
  *Every bullet carries a pointer the next window can verify, not a claim.*

## Decided (do not re-litigate)
- <decision> — rejected alternatives: <X> because <why>.
  *This is DECISIONS.md-shaped, but scoped to this task and kept here so the
   next window need not dig.*

## Remaining (ordered)
1. <next concrete step> — how to verify it is done.
2. ...

## Invariants / gotchas
- <the thing that bit the last window>: e.g. "the M912 register commit added
  the def-323 detail block but not its table row — check both."

## Grounding references
- gate log: <path>   (the measured ci output, not a memory of it)
- peer note: <path>  (e.g. the morning findings)
- memory:  .jichi/memory.md lines <n>..
```

The discipline that makes it work: **a handoff bullet is a pointer, not a
claim.** "commit 17bbb895, gated" is checkable in ten seconds; "the fence
parity fix landed" is not, and the next window cannot tell if it is true. The
morning handoff note worked precisely because it *showed its evidence* (git
object reads, quoted output, `(inferred)` marks). That is the template.

### 2.2 Two actions

**`/handoff [reason]`** — write `.jichi/handoff.md` from the *current*
window. The current model (which still has the context) fills the shape above
from what it actually knows, and the tooling appends two things it should not
have to invent: the current `git rev-parse HEAD`, and a pointer to the most
recent gate log. This is a *write* — it is a tool the model invokes, it is
scoped to the per-project scratch, and it is reversible.

**`/resume`** (and the session-start path when `.jichi/handoff.md` exists and
is fresh) — on a *fresh* window, load the handoff and, before any substantive
work, **verify its pointers**: re-run the one-line `git log -1`, re-check the
gate-log existence, re-confirm the deferral ids. Then continue from
"Remaining." The verification step is the whole point: it converts a note
that *might* be stale into a note that is *proven* current, and it catches
the failure mode where the previous window's record was written before the
gate actually ran (this session's own F-J3 — see §5).

### 2.3 Why not "just use memory.md"?

Because memory.md is **append-only facts with no task structure and no
ordering** — a pile of things worth remembering. A handoff is **one task's
state, ordered, with a done/decided/remaining split and grounding refs.**
They are complementary: `/handoff` may *promote* a still-true fact to
`memory.md` on the way out, but the task bridge is its own file. Conflating
them is how "a durable note" becomes "a wall of stale notes I cannot tell
apart."

### 2.4 Why not "just spawn a subagent"?

A subagent is a **fresh, narrow, disposable window that hands back a short
answer** — ideal for *delegating a subtask* ("go verify these six claims,"
"find the M909 diff"). It is the *wrong shape for continuing the loop*
because (a) its hand-back is capped (M911: 4096 bytes), so it cannot carry a
handoff; (b) it is not the session — it has no history, no checkpoints, no
continuing identity; (c) the parent must then re-ingest its answer, which is
just the elision problem one level up.

**The two compose, and this is the direct answer to "would a subagent help?":**
yes, *for the reading*, and the handoff is *for the writing*. The pattern for
a window that is running low:

1. **Offload the reading to a subagent** (`spawn_subagent`, readonly): hand it
   the handoff's *Grounding references* and the specific question, and get back
   a capped verdict ("F-J5 is real, the fix is commit 64afd540, here are the
   three files"). The subagent's *work* floods a fresh window; its *verdict*
   does not flood this one.
2. **Write `/handoff`** with what remains.
3. **Start a fresh session**, `/resume`, and continue from "Remaining."

So subagents are the *context valve* for heavy reading; handoffs are the
*continuity valve* for the loop. Both are needed; neither replaces the other.

## 3. Design decisions and rejected alternatives

- **Decided: the handoff is a small structured file with pointers, not a
  dump of the transcript or a summary of the session.**
  *Rejected:* (a) auto-summarize the session into a new system prompt —
  summaries lose facts (the exact commit, the red count) and the model then
  re-asserts them confidently; (b) hand over the whole transcript — it is the
  thing that does not fit; (c) rely on `--session`/`--resume` alone — that
  resumes history subject to compaction, not a verified record.
- **Decided: `/resume` verifies the pointers before working.**
  *Rejected:* trust the handoff as written. The F-J3 defect this session
  (stamp written in the same commit as "Next gate: pending") is exactly a
  handoff that was *not* verified on arrival. Verification is cheap (a few
  `git` calls) and converts "probably current" into "proven current."
- **Decided: heavy reading is offloaded to readonly subagents; the handoff
  records the verdict and the path to the evidence.**
  *Rejected:* do the heavy reading in the window that is about to run out
  (wastes the context it is short of) and do it inline on resume (wastes the
  fresh window before it has done its work).
- **Decided: `/handoff` is a normal, reversible write tool (per-project
  scratch), not a privileged action.** It touches only `.jichi/handoff.md`
  (and may append to `memory.md`). No force-push, no tree mutation. This keeps
  it safe to call often, even speculatively, at a "this window is getting
  long" threshold.
- **Decided: evidence stays by-path, not by-inline.** Large logs, transcripts,
  peer notes are referenced (the morning handoff did this well), never pasted,
  so the handoff itself always fits one window.

## 4. Falsifiers (what would show the plan wrong)

- A `/resume` session continues from the handoff but **re-measures something
  the handoff already carried as a pointer** (the gate count, the deferral
  ids) — then the handoff's pointers were not verified or not checkable, which
  is the defect the plan exists to remove.
- Two windows in a row **both write a handoff claiming the same step done
  with a commit the gate never ran on** — then `/resume`'s verification is
  absent or not enforced, i.e. F-J3 again.
- A handoff file **grows past one context window** — then the "by-path, not
  by-inline" rule is not being held and it is a transcript, not a bridge.
- A heavy-reading subtask **is not offloaded** and instead consumes the
  failing window's last usable context — then the subagent valve is not wired.

## 5. Not goals (this is deliberately small and reversible)

- Not a general task/persistence framework, not a database, not a new repo
  format. One markdown file with a shape, two slash-actions, and a
  verify-on-arrive discipline.
- Not a replacement for `memory.md`, checkpoints, `--session`, or
  `spawn_subagent` — it composes with all of them.
- Not automatic. The model calls `/handoff` when the window is long and
  `/resume` verifies on start; nothing fires on a timer. (A future
  *suggestion* — "this window is ~80% full, consider a handoff" — is a
  non-goal for v1.)
- Does **not** resolve the *write* half of F-J3 by fiat: the stamp-must-match-
  the-gate problem is a separate, real process defect, and this plan makes it
  *visible* (verify-on-arrive) rather than pretending the handoff fixes it.

## 6. Phasing (each is a small, gated step on its own)

- **P0 — the file and the shape (no code).** Agree `.jichi/handoff.md` and
  the §2.1 shape; write the first one *by hand* for a real task and resume
  from it in a fresh window. If the shape does not carry a loop, fix the shape
  before writing a tool. (Cheap; proves the unit.)
- **P1 — `/handoff` (a write tool).** A small tool that renders the shape,
  fills `HEAD` and the last-gate-log pointer itself, and writes
  `.jichi/handoff.md`. Reversible, per-project, zero privilege.
- **P2 — verify-on-arrive.** On session start (and `/resume`), if
  `.jichi/handoff.md` exists and is fresh, load it and run the pointer
  checks; surface any that do not verify as an explicit warning before work.
- **P3 — the subagent valve.** A documented recipe (or a thin tool) for
  "offload this heavy read to a readonly subagent and fold the verdict into
  the handoff." Likely a recipe first, a tool only if it is used often.

P0 is the load-bearing part: if a hand-shaped handoff carries the loop, the
tools are just making the discipline cheap. If it does not, no tool rescues it.

## 7. Why now / relation to this session

This session hit the context-pressure wall mid-loop (a full `make ci` is ~30
min of wall-clock and a large window of tool output), and it is exactly the
kind of loop the plan targets: gate → merge → push → next deferral. The
morning peer handoff note was a *manual P0* that worked — which is the
evidence the unit is real. What is missing is the *write* action (so the
current window captures state while it still has it) and the *verify* action
(so the next window does not resume from a stale claim). This plan is the
design for those two, built on jichi's existing memory/checkpoint/session/
subagent/preservation primitives rather than a new system.
