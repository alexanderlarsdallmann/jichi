# Conversational rewind (`/rewind` / `rewind`)

`/undo` reverts the **files** a turn changed, but it leaves the conversation
intact — so the history now describes edits that no longer exist on disk.
*Rewind* (M34c) is the missing half: it returns the workspace **and** the
conversation to an earlier point in one step, so the two stay consistent and you
can cleanly re-try from there.

## What it does

Each workspace checkpoint is taken just before the first file-changing tool of a
turn, labelled with that turn's user request (see
[SNAPSHOTS.md](SNAPSHOTS.md)). Rewinding to checkpoint *n*:

1. **Restores the files** to that checkpoint (`git reset --hard` + `clean` in the
   shadow repo — the same machinery as `undo n`), undoing that turn's edits and
   any later ones.
2. **Truncates the conversation** back to the user message that started that
   turn — removing that turn and everything after it.
3. **Re-saves the session**, so the trimmed history persists.

The turn boundary is found by matching the checkpoint's label to the user
messages in chronological order (the pure `jc_rewind_match`); truncation always
lands on a user-message boundary, keeping the history well-formed.

## TUI: `/rewind`

```
/rewind                  # list checkpoints as rewind targets
/rewind 2                # rewind to the 2nd most recent checkpoint
/rewind 2 --dry-run      # preview: which files revert + how many messages drop
```

Bare `/rewind` lists the checkpoints (newest = 1), same as `/checkpoints`. A
number applies the rewind to the live session; `--dry-run` shows the file diff
that would be reverted and the message count that would be dropped, changing
nothing.

## Subcommand: `rewind`

For scripting or fixing up a session between runs:

```sh
jichi rewind            # rewind to the latest checkpoint (n=1)
jichi rewind 3          # the 3rd most recent
jichi rewind 3 --dry-run
jichi --session <id> rewind 2   # a specific session
```

The session is the one named by `--session` (id or prefix), else the most recent
for the current project (`--all` to consider any project). The subcommand
restores the files and rewrites the saved session file; the next `--continue`
resumes from the rewound point.

## Notes & limits

- **n counts checkpoints, not turns.** Turns that changed no files take no
  checkpoint, so they aren't rewind targets; rewinding to the nearest checkpoint
  still truncates the conversation correctly to that checkpoint's turn.
- Rewind needs snapshots available (git present, `"snapshots"` not disabled, the
  workspace not a huge non-git tree). When unavailable, the command says so.
- The session **title** is left as-is even when the history becomes empty; it's
  just a label and a fresh turn re-derives it.
- A checkpoint whose triggering turn isn't in the loaded session's history (for
  example after heavy auto-compaction dropped that prefix) can't be mapped; the
  command reports that rather than truncating to the wrong place.

## Internals

- **`jc_rewind_label_match` / `jc_rewind_match`** (`src/util/jc_rewind.c`) —
  pure: clean a user message the same way checkpoint labels are cleaned, then
  prefix-match (tolerating the collapsed-full vs git-subject label forms); the
  ordered greedy assignment maps each checkpoint to a distinct, increasing user
  index. Unit-tested in `tests/test_rewind.c`.
- **`jc_snapshot_rewind_cut`** (`src/snapshot/jc_snapshot.c`) — gathers the
  history's user messages + the checkpoint labels and runs the matcher, returning
  the truncation length for the n-th most recent checkpoint.
- **`run_rewind`** (`src/main.c`) and the TUI `/rewind` (`src/tui/jc_tui.c`) —
  the restore + `jc_history_truncate` + save orchestration.
- e2e: `tests/e2e/rewind.py` drives a mutating turn, then asserts both the file
  and the conversation moved (and that `--dry-run` is a no-op).
