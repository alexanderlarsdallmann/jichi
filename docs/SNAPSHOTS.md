# Git snapshots & undo

The agent edits files. **Snapshots** let you take those edits back: jichi
checkpoints the workspace before the agent's first file-changing action in a
turn, and `/undo` restores the workspace to the checkpoint — reverting that
turn's edits (including files the agent created).

This is implemented with a **shadow git repository** kept entirely separate from
your own `.git`, so your real history, staging area, and branches are never
touched.

## Is my net armed? Check before you rely on it

Snapshots switch themselves off in four situations (config `snapshots: false`,
the `--lite`/low-RAM profile, no `git` on `PATH`, or a huge un-git-managed
workspace), and **three of them are silent** — nothing warns you at the moment
of an edit; you only notice the absence of checkpoints afterwards. So check:

```sh
jichi doctor          # reports git availability and whether snapshots are on
jichi checkpoints     # lists the checkpoints that exist for this workspace
```
…and `/status` inside a session. If you want undo, confirm it *before* the work,
not after.

## How it works

- A shadow repo lives at `~/.jichi.d/checkpoints/<key>/`, where `<key>`
  is derived from the workspace path. Its **work tree is your workspace**; every
  git command runs as `git --git-dir=<shadow> --work-tree=<workspace> …`.
- A checkpoint is `git add -A` + `git commit` in the shadow repo. Because the
  work tree is your project, this records the project's current state without
  creating anything inside it. Your project's `.gitignore` is respected, and the
  shadow repo additionally excludes `.git/` so your real repo metadata is never
  captured.

> **Ignored files are outside the net — read this before you rely on `/undo`.**
> A checkpoint is a `git add -A`, so **anything your `.gitignore` excludes is
> never captured, and therefore never restored.** If the agent edits or deletes
> an ignored file, `/undo` will not bring it back: `reset --hard` cannot restore
> what was never staged, and `clean -fd` leaves ignored files alone.
>
> This matters because other pages send you to exactly such files: a git-ignored
> `local/config.json` ([MODELS.md](MODELS.md)), a git-ignored `.jichi/memory.md`
> ([MEMORY.md](MEMORY.md)), and the classic `.env`. Keep a copy of anything that
> is both ignored **and** precious.
- A checkpoint is taken **lazily**: just before the first mutating tool
  (`write_file`, `edit_file`, `run_terminal_command`, …) actually runs in a
  top-level turn. Read-only turns and chat turns create no checkpoints.
- **Undo** restores the most recent checkpoint with `git reset --hard <commit>`
  followed by `git clean -fd` (so files the agent *created* after the checkpoint
  are removed too), then pops it off the stack. A second `/undo` steps back to
  the checkpoint before that, and so on.

## Using it (TUI)

```
/undo          revert the workspace to the last checkpoint
/checkpoints   list the workspace's checkpoints (newest first)
```

`/undo` reports what it reverted (the request that produced those edits). When
there's nothing to undo it says so.

**The model is told too (M349).** `/undo` reverts the *files* and keeps the
*conversation* (that split is the point — [REWIND.md](REWIND.md) is the
both-halves form), so every earlier tool result describing the reverted files
is stale the moment the restore runs, and a model that is not told builds its
next edit on phantom state. After a successful `/undo` that changed any file,
one `[undo]` user-role note lands in the live conversation — the checkpoint's
label, the reverted files (up to 8, then a count), and the ask to re-read
before trusting earlier results — and the session is saved immediately, so a
later `--continue` cannot revive the stale beliefs without the correction
beside them. An undo that changed nothing injects nothing: nothing became
stale. `/rewind` deliberately gets no note — truncation removes the beliefs
along with the work. Known residual gap: the CLI `undo` subcommand runs with
no live conversation, so a saved session's beliefs stay stale there (recorded
in `DEFERRED.md`).

## Using it (CLI)

The same checkpoints are reachable without a chat session — handy after a
headless run, or to undo across sessions:

```sh
jichi checkpoints     # list snapshots, newest first (index 1 = latest)
jichi undo            # restore the work tree to checkpoint 1 (latest)
jichi undo 3          # restore to the 3rd most recent checkpoint
jichi undo --dry-run  # preview what undo would change, without applying
```

`undo` restores the chosen checkpoint with `git reset --hard` + `git clean -fd`.
It needs no API key or network.

`--dry-run` (combinable with an index, e.g. `undo 3 --dry-run`) prints the
tracked changes that would be reverted (`git diff --stat`) and the untracked
files that would be removed (`git clean -nd`), then exits without touching the
work tree — a safe way to see exactly what an undo would do first.

## Persistence

Checkpoints are **workspace-scoped and persistent**: they live as commits in the
shadow repo, so `/undo`, `/checkpoints`, and the CLI all see snapshots taken in
earlier sessions (including `--resume`d ones). At startup the stack is
repopulated from the shadow repo's history (most recent
`JC_SNAPSHOT_MAX_LIST` = 200).

## Scope & safety

- **Files only.** Undo restores the *workspace*; it does not rewind the
  conversation history. You can see what the agent did and undo the file changes
  independently.
- **Bounded history.** The shadow repo accumulates a commit per edit-turn but is
  pruned automatically (see below). Delete
  `~/.jichi.d/checkpoints/<key>/` to reset a workspace's history.
- **Huge un-git-managed workspaces are skipped.** If the workspace has no `.git`
  and no `.gitignore` and contains more than 20000 files (e.g. an accidental run
  in `$HOME`), snapshots disable themselves so `git add -A` can't churn through
  it. A workspace with a `.git` or `.gitignore` is trusted and never counted.
- **`/undo` is destructive to the work tree.** `reset --hard` + `clean -fd`
  discard *all* changes since the checkpoint in the affected files — including
  any you made by hand in the meantime. Ignored files (per `.gitignore`) are
  left alone by `clean`, so build output isn't deleted.
- **Per-user, so `/undo` is per-user.** The shadow repo lives under
  `~/.jichi.d/`, keyed by a hash of the *work tree*. Two **users** sharing one
  directory therefore hold **two independent checkpoint histories over one
  tree** — measured at M478, both keyed identically, one per home. `/undo`
  restores *your* view of a tree someone else may have changed since, and
  nothing warns about it, because jichi cannot see that another user exists.
  Give each person their own workspace; see [`DEPLOYMENT.md`](DEPLOYMENT.md) §6.
- Requires `git` on `PATH`. If git isn't available the feature silently disables
  itself (the agent still runs normally).

## Checkpoints do not know about your branches

The shadow repo's key is derived from the **workspace path**, not from the
branch, and its work tree is your workspace. So a checkpoint records *content*,
and `undo` makes the worktree match a recorded content — the branch you are
standing on is not part of that decision. That is the design working as
intended: your real `.git`, its history and its branches are never touched.

The consequence is sharp, and it sits on a path this project recommends
elsewhere ("run it on a branch, never master" —
[`examples/self-hosting/README.md`](../examples/self-hosting/README.md)).
**Measured 2026-08-21:** a checkpoint taken on a feature branch that carries a
file `master` does not have, then `git switch master`, then `undo` — and the file
**arrives on master** as an untracked file. Nothing is lost and nothing in git is
corrupted; a state from another branch has simply been written into your
worktree, because that is the state the checkpoint holds.

Two things make this a documented tool rather than a trap, and both are pinned by
`tests/smoke/undo_across_branch.sh`:

- **`undo --dry-run` names the file first.** It printed `feat-only.txt | 1 -`
  before anything happened. Reading the dry run is the whole defence.
- **The discarded state is preserved** with a `recover` handle in the same
  output: `jichi recover <sha> --into <dir>`.

The rule that follows: **an `undo` belongs to the branch the run happened on.**
If you have switched branches since, `--dry-run` first, every time — and if the
diff mentions files you did not expect, you have switched branches since.

## Pruning

Left unchecked the shadow repo would grow one commit per edit-turn forever. At
startup, when the history exceeds **2× `snapshotLimit`** commits, jichi
rebuilds it down to the most recent `snapshotLimit` checkpoints and reclaims the
disk:

- The retained commits are re-created with `git commit-tree` using their
  original trees, so their content (and `undo`'s `reset --hard`) is unchanged —
  only the SHAs and the now-orphaned older commits differ.
- The branch is repointed at the rebuilt tip, the reflog is expired, and
  `git gc --prune=now` drops the orphaned objects.
- It is **non-destructive to your work tree** and bails out leaving the repo
  intact if any git step fails.

The 2× trigger is hysteresis: pruning runs at most once per startup and only
after the history has doubled past the limit, so it doesn't churn every launch
once you're at the cap. `snapshotLimit: 0` disables pruning (unbounded history).

## Configuration

```json
{ "snapshots": true, "snapshotLimit": 100 }
```

- `snapshots` (default `true` — but **`false` under `--lite` / `lowResource`**,
  which jichi turns on **automatically** on a machine with less than ~1 GB of
  RAM. On a small box, then, there are no checkpoints and no `/undo` unless you
  set `"snapshots": true` explicitly. Check with `/status` or `jichi doctor`
  rather than assuming.) is the master switch; `false` disables
  checkpointing entirely.
- `snapshotLimit` (default `100`, `0` = unlimited) is how many checkpoints the
  shadow repo retains.

## Implementation

`src/snapshot/jc_snapshot.c` (+ `include/jc_snapshot.h`). The manager spawns
`git` via `fork`/`exec`/`pipe` (argv-based — no shell, so workspace paths with
spaces are safe) and keeps a stack of `{commit, label}` checkpoints. The pure
path-derivation helper (`jc_snapshot_git_dir`) and the label cleaner are
unit-tested; the git flow (init → checkpoint → edit → undo) is verified
end-to-end. The agent loop (`run_agent_loop`, top-level only) takes the lazy
pre-edit checkpoint; the TUI exposes `/undo` and `/checkpoints`, and the
`undo`/`checkpoints` CLI subcommands operate on the same shadow repo.
`jc_snapshot_refresh` rebuilds the stack from `git log` at startup (persistence);
a bounded file-count walk (`workspace_too_big`) implements the large-workspace
guard; `prune_if_needed`/`do_prune` bound the shadow history via `commit-tree` +
`gc`.

## Undo destroys, and there is no redo (M337b)

`/undo` and `jichi undo` are `git reset --hard` + `git clean -fd` against the shadow repo.
Everything in the tree that is not in the target checkpoint is gone, including untracked work,
and jichi has no `/redo`.

With config `preserveDiscarded` on, the state being discarded is committed and pinned under
`refs/jichi/discarded/undo/<time>-<n>` **first**, and the sha is printed:

```
$ jichi undo
reverted to checkpoint 1: add the parser skeleton
the discarded state is preserved at af1ce20b0965 (`jichi recover af1ce20b0965 --into <dir>`)
```

`jichi attempts` lists these alongside the envelope's, and `jichi recover` materialises one
into a worktree — never into the live tree, so recovering an old state cannot overwrite the
current one. `--revert-out-of-scope` preserves the same way, under `.../revert/<time>-<n>`.

**As of M338 this is on by default for `undo`/`rewind`/`revert`** and still off for the
envelope's rollback: `preserveDiscarded` is tri-state (unset / `false` / `true`), and unset
means each mechanism takes its own default because the risk differs. A rollback writes to disk
while a run is already failing; an interactive undo does not.

The store is bounded by `jichi checkpoints gc`, which removes expired preserved states (kept
within 7 days **or** among the newest 20 — `--older-than` and `--keep` change both) and
orphaned shadow repositories. It reports first and removes nothing without `--yes`:

```
$ jichi checkpoints gc
  11 live, 10 orphaned, 0 unreachable, 0 unknown  (62040 KB total)

  4 preserved attempt(s): 4 within the retention policy (7 days / newest 20),
  0 expired.

  nothing was removed. `checkpoints gc --yes` deletes the 10 orphaned repo(s)
  and the 0 expired attempt(s); --keep/--older-than change the policy.
```

Deleting the ref is the **only** way the objects behind a preserved state can ever be
reclaimed: a ref makes its commit reachable, so `git gc` alone will never collect it. Measured
cost is **5 KB per preserved state** once packed (42 KB loose, which is transient).
