# Keeping work in progress: what jichi keeps today, what it loses, and the design for the rest

*A design page for self-learners, junior developers and agents. **Status: proposed, not built** (2026-11-23).
Section 2 is what you can use today; section 4 is the proposal, and the milestone that builds it will have its own
note. Read section 5 before you start any long or unattended task.*

---

## 0. The requirement

The operator, 2026-11-23: "jichi needs to be able to store work-in-progress, even if not green, or known to be
flawed for later reference, analysis, correcting, and completion. Self-learners, and agents have to learn, and make
use of that."

Two words in that sentence matter most:
- **even if not green**: half-done work, a test that still fails, a fix that does not build yet;
- **known to be flawed**: work whose defects are already found, kept so that someone can study them, correct them or
  finish the job, instead of starting over.

Today jichi keeps finished work well (a commit) and keeps *discarded* work when you ask it to. It does not keep
*unfinished* work while a session is still running. This page says exactly where the gaps are.

## 1. Why: what has been lost, and what was saved

From the records of the jichi drives (a lead model and its workers developing jichi itself, section 7):

1. **Fixtures in `/tmp`, lost between runs (2026-11-23).** The lead had built nine test fixtures for a new module
   and pinned the reference's output on each. They lived in the container's `/tmp`. A later run found them gone and
   had to rebuild all nine by hand before it could continue.
2. **A run that ended at its context limit (2026-11-22).** Run 0097 ended at about 91 % of its context window, with
   "elision cannot recover". Nothing was lost, only because it had committed everything first and written the next
   steps into its log. Had it been one step slower, its uncommitted work would have been gone: the drive's harness
   keeps no copy of uncommitted work (section 3).
3. **What was saved, and why it mattered (2026-11-23).** Four worker attempts at the same module came back hollow:
   a test whose expected values contradicted the reference, a driver that could never pass, and finally the core
   file missing from the commit. jichi kept each rejected attempt as a ref instead of deleting it. The lead read
   them in full, named four distinct failure patterns, and named the parts worth salvaging (a build wiring and a
   count update). The kept flawed work became the evidence for the decision to split the next attempt into small
   single-file tasks. Those produced the module and its unit test; the third, the test driver, was still being
   rejected as hollow when this page was written. That is
   the case the requirement asks for, everywhere.
4. One more loss is recorded in the supervisor's private notes, outside this repository's records, so it is not
   counted here.

## 2. What jichi keeps today, and how to use it

| Mechanism | What it keeps | When | How to read it back | Its limits |
|---|---|---|---|---|
| **A commit** | the tracked files | when you commit | `git` | permanent once pushed; in a drive, workers start from it, so commit only verified steps (section 5) |
| **Checkpoints** ([SNAPSHOTS.md](../SNAPSHOTS.md)) | the workspace before a turn's first file-changing tool | lazily, once per top-level turn | `/undo` | an unattended run is ONE long turn, so it gets one checkpoint, at its first edit; snapshots turn themselves off in four situations, three of them silently; local only |
| **`preserveDiscarded`** ([AUTONOMY.md](../AUTONOMY.md), M336/M337b) | a tree that a rollback, `/undo`, `/rewind` or an out-of-scope revert is about to discard, committed under `refs/jichi/discarded/<cause>/...` with its outcome | just before the destructive restore | `jichi attempts` lists; `jichi recover <commit> --into <dir>` materialises it into a worktree, never the live tree | off by default; only at a discard event; local only |
| **The worker pool's kept states** ([PARALLEL.md](../PARALLEL.md)) | a worker's result that was not merged (its verify was red, it could not merge, it edited a test it should not) | when the parallel task ends | `jichi attempts show <ref>` | workers only; local only |
| **The relay's baton** ([RELAY.md](../RELAY.md)) | a written account in five headings (Done, In progress, Next step, Decided, Dead ends) | at a relay leg's end | the next leg's prompt | words, not files |
| **A drive's `work/*` branches** ([HARNESS.md](../HARNESS.md)) | anything the team commits on a branch under `<prefix>/work/` | the harness fetches and pushes them with `main`, every 20 minutes | `git`, on the remote | unused so far; each update must be a fast-forward; the push's key scan and size limits apply |

A run's own records (its stream and telemetry) hold the tool calls a model made, including the text of its edits.
They are evidence, not a recovery mechanism: nothing reads them back into a tree.

## 3. The gaps

1. **No way to save the live tree on purpose, with a declared state.** You can commit (which says "this is a step
   of the history") or let jichi preserve what it is about to discard. You cannot say: "keep this as it is, it is
   known to be flawed in these two ways, I will come back to it".
2. **One checkpoint per unattended run.** Checkpoints follow turns, and an unattended run is one turn. Everything
   between the first edit and the next commit is unprotected.
3. **No periodic snapshot in the drive harness.** The first hardening drive's scripts took a snapshot of the lead's
   uncommitted work every 30 minutes, outside git. The C port, `jichi-harness`, did not carry this over. Its
   `snapshot` lines are the run records', not the work's.
4. **Everything kept is local.** The shadow repository lives in the session's home. In a drive that home is inside
   the drive's folder: a lost container or host takes it along.
5. **`/tmp` is not the tree.** Work kept outside the workspace is invisible to every mechanism above.

## 4. The design (proposed)

These are the requirements the card for this feature carries. The milestone that builds it writes its own note,
and details may change there.

1. **A work-in-progress entry.** A commit of the whole workspace (tracked and untracked files that are not ignored)
   kept outside the branch history, under `refs/jichi/wip/<session or run>/<n>`, in the same shadow repository as
   the checkpoints and the preserved discards.
2. **A declared state, always shown.** Each entry carries the state its writer declares (verified, red, known
   flawed, unknown), a note in the writer's words (what is done, what is wrong, what is next), the base commit, the
   time, and who wrote it (the session or run, the model).
3. **Saving on demand.** A tool for agents and a command for people, both with the state and the note as arguments.
4. **Saving automatically.** When the context-pressure note fires; at a relay leg's end; at the end of a run that
   leaves a dirty tree; before every destructive restore (where `preserveDiscarded` already acts).
5. **Reading back.** List (with each entry's state, age and size), show, diff against its base, and recover into a
   worktree. Never over a live tree.
6. **Leaving the machine, in a drive.** With each 20-minute fetch, the harness saves the team tree's uncommitted
   changes as an entry and carries it to the remote on the existing `work/*` refset, through the same key scan and
   size check as every push.
7. **Honesty rules.** An entry never counts as done. No gate, ledger or READY line reads it. Nothing is deleted
   automatically; a listing reports the size the entries take.
8. **Found by agents.** jichi's built-in guide and its skill text name the feature. A run's handoff lists the
   entries the previous run saved, with their states.

## 5. How to work today, until it is built

1. **Commit every verified step, before you start the next one.** A commit is cheap; lost work is not.
2. **Keep nothing that a later step needs only in `/tmp`.** Fixtures, a reference's output, a spike's source: put
   them in the tree (a notes folder) and commit them, marked for what they are.
3. **Turn `preserveDiscarded` on for unattended runs.** It costs some disk while a run is failing, and it keeps
   what a rollback would otherwise destroy. The harness-port drive runs with it on.
4. **Before any command that discards changes** (`git checkout -- <path>`, `git clean`, `git reset --hard`,
   `git stash`), read `git status --short` and `git diff --stat` for exactly that path. The drive's lead prompt asks
   for the same before a path is discarded.
5. **When you keep flawed work, say so where the next session will look.** Name the ref or the branch, its state
   and its known defects in your log or handover. Kept work that nobody can find is lost work.
6. **Never present kept work as finished.** Say "kept, known flawed: these defects", not "done".

### For an agent

1. Commit each verified step; never commit an unverified step to the branch others build on.
2. Before your context runs out, commit what is verified and write the rest down: what is done, what is not, what
   is wrong, what is next.
3. If you keep unfinished or flawed work, record where it is and its state, in the log the next run reads.
4. Read the previous run's handoff and log for kept work before you start the same task again.
5. Never delete kept work; never restore it over a live tree; recover it into a worktree and compare.

## 6. What is not decided yet

- Where entries live outside a drive, for a single user without a remote: the shadow repository alone, or also a
  branch in the user's repository.
- How long the harness keeps periodic entries, and how they are named so that each update stays a fast-forward.
- Whether the automatic triggers are on by default for interactive sessions, or only for unattended runs.

## 7. Sources

- The drive's records: `docs/internal/drive/2026-11-05-harness-port/LOG.md` (runs 0097, 0101 and 0103: the context
  limit, the kept attempts and their audit, the rebuilt fixtures).
- [SNAPSHOTS.md](../SNAPSHOTS.md), [AUTONOMY.md](../AUTONOMY.md) (`preserveDiscarded`), [PARALLEL.md](../PARALLEL.md)
  (kept states), [RELAY.md](../RELAY.md), [HARNESS.md](../HARNESS.md) (the `work/*` refset).
- The first hardening drive's case study: [docs/case-studies/hardening-2026-10-17/](../case-studies/hardening-2026-10-17/README.md)
  (its WIP snapshot).
- [docs/analysis/2026-11-23-switching-units-to-the-port.md](../analysis/2026-11-23-switching-units-to-the-port.md)
  (the same night's records).
