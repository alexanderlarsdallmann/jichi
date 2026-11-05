# Proposal: a run handoff, what one run leaves the next

*A proposal, written on threadwork on 2026-09-28 by the supervisor of the zigodot
drives (Claude), for the other machine, on the operator's request. The operator: "Maybe we should
have an option in jichi to carry data over between runs. Losing information is bad." And: "write
the jichi proposal". Code and docs were read at `64ad93ff` (M830). A harness version of this runs
in the zigodot demos drive since 2026-09-28 (§4).*

## 0. In short

- **A driven project runs jichi many times.** A loop restarts the lead when a run ends. Each run
  starts from the repository and nothing else, unless the project writes its own notes.
- **The zigodot demos drive measured the cost at its first restart:** run 0002 spent about a
  quarter of an hour re-reading files run 0001 had already read (§1).
- **What survives a run today:**
  - the repository;
  - a team's own log, if it writes one;
  - the board;
  - `memory.md`, of which the last 8 KB is loaded;
  - kept attempts (M803).

  `--resume` and `--continue` carry the whole session, which drives avoid on purpose: a history
  of 130k tokens makes a tired run.
- **What is lost:**
  - the working knowledge: which files matter, and where;
  - the todo list and the plan;
  - above all, the reason and the last words of a run that did not end by itself. That covers an
    abort, Ctrl-C, the deadline, the tool-call cap, and a crash.
- **The proposal:**
  - `--handoff-out FILE` writes, at **every** exit, a small record that is mostly mechanical;
  - `--handoff-in FILE` puts it into the next run's context, pinned and never cut from the top;
  - it is stamped with the commit and the time it was written at.

## 1. What a run loses today

- **The drive's runs 0001 and 0002, 2026-09-28** (zigodot, lead `jlu/qwen3.8-27b`):
  - **run 0001** read 21 files (the plan, the board, the profiles and skills, `vm.zig`,
    `runtime.zig`, `build.zig`, and more) and committed its first card. About 20 minutes after it
    started, it started a batch.
  - **The batch lacked `write: true`,** and was aborted with the operator's consent. The abort
    ended the run.
  - **Run 0002 started 6 s later** and read the same files again: 11 tool calls in 18 minutes
    before its first batch.
- **An interrupted run's last message** exists only in its stream: *"I've committed the
  housekeeping changes and now I'm launching the first batch of 5 independent workers ..."*. A
  successor never sees it. Nor does it see the inject the supervisor had sent the run, which was
  queued for a model call that never came.
- **The weekend drive's 248 runs** carried over only what their lead chose to write into its log,
  at the end of a run that ended normally.
- **The objection, in `DECISIONS.md`** (M348, "Tickets are turn-lived"): persisting elision
  tickets across runs was rejected, because "the store would grow without bound, and a stale ticket
  naming content from a previous run is worse than none". §2.4 answers both points.

## 2. The proposal

### 2.1 What is written

**One file, replaced at every run's end and never accumulated,** at most 8 KB by default. Its
sections, in order of priority:
1. **The end record** (mechanical):
   - why the run ended: done, abort, a signal, the deadline, the tool-call cap, or an error;
   - its counters, and its last tool call with the outcome;
   - the commit the workspace is at;
   - **whether the tree held uncommitted changes.** A merged but uncommitted batch is exactly what
     a successor must know, since the weekend lost merged work twice to a `git checkout`.
2. **The todo list and the plan,** as the `todowrite` and `write_plan` state stood (mechanical).
3. **The files read and edited,** with the line ranges read and the number of edits, from the tool
   loop rather than from the model (mechanical). This is what saves the re-reading.
4. **An open batch:** a `spawn_parallel` running at exit, with its kept states' refs (M803, M809),
   so that a successor resumes it with `from:` (M816) instead of redoing it (mechanical).
5. **The last assistant message,** quoted. It is model-written, and labelled so.
6. **Optionally, what the run learned:** a compaction-style summary the model writes at a normal
   end (`--handoff-summary`). There is none on an abort, which makes no model call at exit. It is
   labelled, and it is the first part cut when over budget.

### 2.2 When it is written

- **At every exit path:**
  - the normal end;
  - `control abort`;
  - SIGINT and SIGTERM;
  - the deadline;
  - `--max-tool-calls`;
  - a fatal error, best effort.
- **Signal-safe:** the handler only sets the flag, as jichi's rule requires, and the loop's exit
  path writes the file.
- **Atomically:** to a temporary file, then renamed.

### 2.3 How it is read

- **`--handoff-in FILE`,** or a config key `handoff: {"in": …, "out": …}`, puts it into the
  context after the rules and before memory. It goes under the heading *"What the previous run left
  you (written by jichi at <time>, at commit <sha>): verify before relying on it"*.
- **Pinned:** it is counted first against a budget of its own, and never cut from the top, which
  is the lesson of N1 and M825.
- **When the workspace's HEAD differs from the recorded commit,** it says so: *"the tree has moved:
  N commits since"*.

### 2.4 The objection, answered

- **It cannot grow:** there is one file per loop, and each run replaces it.
- **A stale record is visibly stale:** it carries its commit and its time, and it says when the
  tree has moved.
- **It is mechanical first:** items 1 to 4 cannot be wrong about what happened. Only items 5 and 6
  are the model's, and they are labelled.
- **Opt-in:** a flag or a config key, off by default, since a single interactive session has no
  successor.

## 3. Red-first tests

1. **A run that reads three files and edits one, then ends:** the handoff names them with their
   counts. Without the writer there is no file.
2. **A run aborted inside a batch,** extending M804's driver `parallel_abort_preserves.sh`: the
   handoff exists, says `abort`, and names the batch and its kept refs.
3. **Ctrl-C during a tool call:** the file is written by the exit path, complete.
4. **`--handoff-in` with a record from another commit:** the section says the tree has moved.
5. **Over budget:** the summary is cut first, then the tail of the files list; the end record never.
6. **In a real loop:** the zigodot drive's time from a run's start to its first batch, measured
   from its telemetry, before and after.

## 4. The harness version, as evidence and as the gap

- **Where:** zigodot, branch `demos/2026-10-03/plan`, `docs/demos/2026-10-03/harness/handoff.py`
  (`7263149`), run by `run-lead.sh` since 2026-09-28 at about 21:00.
- **What it does:** it builds items 1, 2, 3, 4 and 5 after the fact, from the previous run's
  `stream.jsonl` and `meta.txt`. It is mechanical, at most 6 KB, and it fails open.
- **Where it falls short, which is what jichi would fix from inside:**
  - it sees only a todo list the model wrote with `todowrite`;
  - it sees the tool results' shortened previews;
  - it does not know whether the tree held uncommitted changes, only the commit the harness
    recorded;
  - it cannot tell an abort from the deadline, since both exit 130; *(checked on 2026-09-30:
    for jichi's own `--deadline` this does not hold — a deadline stop exits 1 with
    `stop_reason: "budget"`, and an abort or interrupt exits 130 with `"interrupted"`, so the
    exit code and the final record tell them apart; the drive runs with no deadline anyway)*
  - it must decode the stream leniently, because the stream holds invalid UTF-8 (day one's
    finding 5: a truncated preview cuts a multi-byte character).
- **The first measurement:** run 0001's handoff was 3.1 KB. It named the batch's five tasks with
  `write=not set`, the defect the supervisor had to explain by a note.

## 5. Where it fits

- **Beside what carries over already:**
  - memory holds durable lessons;
  - the board holds tasks;
  - a team's log holds its reasoning;
  - the handoff holds working state, one run deep.
- **Against `--continue`:** it is the bounded alternative to resuming a whole session.
- **A place in `DRIVING_A_TEAM.md` §8** ("Long unattended drives: the harness", which now
  exists): a long drive's harness passes the previous
  run's handoff to the next.
