# Preserve before destroying: never lose a run's work

*jichi has a shadow git repository whose work tree is the workspace, a checkpoint chain, a
restore, and worktree support. It uses all of it to **undo** work and none of it to **keep**
work it is about to throw away. This is the design for closing that gap.*

Status: **proposal, measured and revised.** §6's two blocking measurements have been taken
and are in §0. One mitigation is available today with no code at all — see §7.

---

## 0. The measurements §6 demanded, taken

**Is rollback rare enough to ignore?** No. Across 46 terminal `end` events in this
engagement's journals:

| outcome | runs | rolled back |
| --- | --- | --- |
| `ok` | 23 | 0 |
| `budget_exhausted` | 15 | 2 |
| `verify_failed` | 4 | **4 (all of them)** |
| `running` (aborted) | 3 | 0 |
| `scope_tainted` (M332) | 1 | 0 |
| **total** | **46** | **6 (13%)** |

Thirteen percent of runs discard work, and **every** `verify_failed` run does. The two
`budget_exhausted` rollbacks are M80 working as designed (a budget stop rolls back only when
a verifier is configured *and* red at exit). `scope_tainted` correctly discards nothing — it
refuses a verdict, not the work.

So the hot-path objection in §6 is answered: this is not a case that fails to occur.

**How big is a discarded state?** Small. Measured on disk: 21 shadow repositories totalling
**61 MB**, the largest 19 MB across 75 commits — so a commit costs roughly a delta, not a
tree, because git stores it that way. The incident's own artefact was one 8.8 KB file plus a
three-line edit. A retention budget in the low hundreds is affordable.

**And a third thing the measurement found, which was not being looked for.** Of those 21
shadow repositories, **10 are orphaned** — `core.worktree` points at a directory that no
longer exists (`/tmp/m196-drive`, `/tmp/tmp.O0nR2oPd6W`, two deleted git worktrees, …).
`do_prune` bounds the *commits inside* a repo at startup; **nothing has ever removed a
repo.** Half the checkpoint store is garbage from workspaces that are gone, and jichi cannot
say so or clear it. That is a need independent of preservation, and §8 designs it.

## 1. The incident

A test-authoring run produced seven test blocks against a 634-line file that had none. Its
gate failed — not on a test assertion (that gate deliberately *accepts* failing tests) but on
a **shape** check about identifiers appearing in the file. The envelope rolled back to green.

**711,628 tokens of work were destroyed**, and with them the only record of what that attempt
had tried. The re-drive that replaced it cost 1,325,103 more and found five real defects,
including a panic — findings the discarded attempt may well have had too. Nobody can know,
because there is nothing left to look at.

The operator was watching it happen and could not retrieve it.

## 2. The defect, precisely

`src/chat/jc_agent.c`, the rollback path:

```c
if (jc_snapshot_changed_since(app->snapshots, e->green_commit, &disc_names) == JC_OK) {
    disc_n = jc_env_summarize_paths(disc_names.data, 8, &disc_sum);   /* names, for the log */
}
if (jc_snapshot_restore_commit(app->snapshots, e->green_commit) == JC_OK) {
```

It computes **the names of the files it is about to discard**, logs them, and then discards
their **content** without recording it anywhere. The shadow repo — which exists precisely to
hold tree states — is never asked to hold this one.

Three paths destroy work this way:

| path | what it discards | recorded? |
| --- | --- | --- |
| envelope rollback on `verify_failed` | every change since the green checkpoint | names only |
| `--revert-out-of-scope` (M142) | each flagged path, individually | names + counts only |
| `jichi undo` / `rewind` | the top checkpoint | nothing |

## 3. What already exists to build on

Nothing here needs new machinery. `include/jc_snapshot.h` already provides:

- `jc_snapshot_take(mgr, label)` — `add -A` + `commit` into the shadow repo
- `jc_snapshot_commit(mgr, i)` — the sha of checkpoint *i*
- `jc_snapshot_restore_commit(mgr, sha)` — restore an arbitrary commit
- `jc_snapshot_worktree_add/_changes/_remove` — a **separate work tree** off the same shadow
  repo, built for the parallel-agent pool
- `jc_snapshot_git_dir`, `jc_snapshot_clean_label` — pure, unit-tested

The shadow repo is already outside the workspace (`~/.jichi.d/checkpoints/<key>/`), which is
the ANECDOTES #1 lesson: observability must live outside the blast radius.

## 4. The design

### 4.0 Optional, and off by default

`preserveDiscarded` (config) / `--preserve-discarded` / `--no-preserve-discarded`, **default
off**, resolved by the usual precedence: CLI wins, then config, then the built-in default.

**Why off, when §0 shows the case occurs in 13% of runs.** Because it writes to disk on a path
that currently does not, and it does so at the worst possible moment — while a run is failing.
A defect in this code would corrupt the shadow repo of a run that is already in trouble. That
is the same reasoning that shipped M83 (detect) a milestone before M142 (revert), and M332
opt-in: **when a feature's failure mode overlaps its trigger condition, earn the default.**

It should become the default once two things are true: it has run on real failures without
incident, and `gc` (§8) exists — turning on unbounded retention before there is a way to clear
it would be the wrong order.

### 4.1 PRESERVE-BEFORE-DESTROY (the one that matters)

> **Shipped as M336 for the rollback path, opt-in** via `--preserve-discarded` /
> `preserveDiscarded`. Proven end to end rather than merely built: a run whose verifier could
> never pass rolled back, `NOTES.md` vanished from the workspace, and its content — `preserve
> me` — was read back out of `refs/jichi/discarded/<run>/1` afterwards, with the message
> carrying run id, outcome, verify command, tokens, calls and file count. The negative case
> was checked too: with the flag absent, zero `preserved` events and no new refs.
>
> `--revert-out-of-scope` and `undo` (§2's other two rows) are **not** covered yet, by design
> — see §9 step 4.

**Rule: no code path may discard a tree state it has not first committed.**

Before each of the three paths in §2, take a snapshot and record its sha under a ref that is
*not* part of the undo chain:

```
refs/jichi/discarded/<run-id>/<n>
```

A separate ref namespace, deliberately — putting a discarded state on the checkpoint stack
would make `undo` walk into states the user never chose, and would consume the
`snapshotLimit` budget that protects the states they did.

The ref is written **before** the destructive operation, so a crash between the two loses
nothing.

Cost: one `jc_snapshot_take` plus one `git update-ref` per destructive event. Both already
shell out through the existing argv-style `git` runner.

### 4.2 A commit message that says what happened

Checkpoints are currently labelled with the turn's user request. A discarded state should
carry the *why*, because the message is the only thing a future reader has:

```
discarded: verify_failed after 3 retries

run:      roleA-tok4-20260809T101122Z
outcome:  verify_failed
verify:   sh .../verify-roleA-tokenizer.sh -> exit 1
tokens:   711628
calls:    17
reason:   gate shape check (not a test failure)
```

That turns `git log` in the shadow repo into a **record of attempts** rather than a list of
labels — which is the thing that was actually lost in §1, more than the files were.

### 4.3 A branch per run

`refs/jichi/run/<run-id>` advanced at each checkpoint, so a run's chain is readable as a
branch instead of inferred from commit order. Cheap, and it makes 4.2 navigable: `git log
refs/jichi/run/<id>` is the run's history.

This is also what makes several concurrent runs against one workspace legible, which they
currently are not.

> **Step 4 shipped as M337b.** Preservation moved from the envelope's rollback call site to
> the two chokepoints every destructive restore passes through, so `/undo`, `/rewind`, the
> `undo`/`rewind` subcommands and `--revert-out-of-scope` are all covered by one config key.
> The reasoning, and the five consequences of choosing a chokepoint over a call site, are in
> the ROADMAP's M337b entry.

### 4.4 Getting it back: `attempts` and `recover`

> **Shipped as M337.** `jichi attempts` lists preserved states (sha, date, subject, ref;
> `--output json` for a supervisor) and `jichi recover <commit> [--into <dir>]` materialises
> one into a detached worktree via the existing `jc_snapshot_worktree_add`.
>
> Proven as a whole loop rather than per-part: a rollback destroyed `NOTES.md`, `attempts`
> listed the preserved ref, `recover` retrieved its content — and the live tree was still
> missing `NOTES.md` afterwards, which is the safety property. Guards checked too: an existing
> `--into` path is refused rather than merged into, and a missing argument prints usage.
>
> Default `--into` is `/tmp/jichi-recover-<sha12>` — deliberately outside the workspace, so a
> path the user did not choose can never be mistaken for their own work.

Two read-only subcommands, following the `runs`/`checkpoints` pattern:

- **`jichi attempts [--since <dur>] [--output json]`** — list discarded states: run id,
  when, outcome, token cost, files, sha.
- **`jichi recover <sha|ref> [--into <dir>]`** — materialise a discarded state **into a
  worktree**, never into the live tree. `jc_snapshot_worktree_add` already does exactly this
  for the parallel pool. The user then diffs, cherry-picks, or discards at leisure.

Restoring into a worktree rather than the workspace is the important half: recovery must not
be able to destroy the current state in the act of rescuing an old one. Otherwise the feature
that exists to stop data loss becomes a way to cause it.

### 4.5 Retention

Discarded refs need their own budget, separate from `snapshotLimit`, because their value
profile is different: checkpoints are for undo and go stale fast; a discarded attempt is
evidence and may matter weeks later. Suggest `discardedLimit` (default generous, e.g. 200)
pruned oldest-first by the existing `do_prune` machinery, and say so in `doctor` when it
starts dropping.

## 4.6 Decisions, with the reason each went that way

| # | decision | why, and the alternative rejected |
| --- | --- | --- |
| D1 | **Optional, off by default** (§4.0) | Its failure mode overlaps its trigger: it writes while a run is failing. Rejected: on by default, because a bug here damages the repo of a run already in trouble |
| D2 | **A separate ref namespace**, `refs/jichi/discarded/…` | Rejected: pushing onto the checkpoint stack. `undo` would walk into states the user never chose, and it would consume the `snapshotLimit` that protects the ones they did |
| D3 | **The commit message carries outcome, verify result, cost** | The message is all a future reader has. Rejected: a bare label, which is what checkpoints use and what made the incident unreadable as well as unrecoverable |
| D4 | **Recover into a worktree, never the live tree** | Rejected: restoring in place. A feature that exists to prevent data loss must not be able to cause it; `jc_snapshot_worktree_add` already does this for the parallel pool |
| D5 | **Retention independent of `snapshotLimit`** | Different value profile: checkpoints go stale in minutes, a discarded attempt is evidence for weeks. Measured cheap (§0) |
| D6 | **`gc` is explicit and never automatic** (§8) | Rejected: pruning as a side effect of another command. Deleting history incidentally is how you lose the thing you were preserving |
| D7 | **`gc` requires `--yes` to act** | jichi runs non-interactively as often as not, so a confirmation prompt is not available. Printing a plan and requiring an explicit flag is the honest substitute |
| D8 | **Preservation and `gc` are separate features** | The store is already 61 MB with 10 orphaned repos and no preservation has ever run. `gc` is overdue on its own merits and should not wait behind D1 |

## 5. What this does not fix

- **It does not stop the rollback.** Rollback on a red verify is correct behaviour and M80
  already keeps work on budget stops. This makes the *discarded* branch recoverable, not
  unnecessary.
- **It does not preserve reasoning.** The tokens spent are gone regardless; what is preserved
  is the artefact and the record. The journal already holds the event trail, and 4.2 links
  the two by run id.
- **It does not decide anything for you.** `attempts` lists; a human chooses. Automating
  "restore the discarded work" would re-create the problem in the opposite direction.

## 6. What would make this design wrong

- If discarded states turn out to be large and frequent, the shadow repo grows without bound
  and §4.5's budget becomes the whole design rather than a footnote. **Measure the size of a
  typical discarded state before committing to a default.**
- If runs commonly discard nothing (because rollback is rare in practice), this adds a git
  commit to a hot path for a case that does not occur. The journals can answer that: count
  `end` events with `rolled_back: true` against the total. **That measurement should precede
  the implementation**, and it has not been done.

## 7. Available today, with no code

**Pass `--no-rollback` to any run whose deliverable is the artefact itself** — a design
document, a test-authoring pass, an analysis. Their gates commonly assert *shape* (sections
present, counts moved), and a shape failure says nothing about whether the artefact was
worth keeping.

That single flag is what rescued the re-drive in §1 after the first attempt was destroyed.
It is a workaround rather than a fix: it protects the runs you remembered to protect.

---

**Provenance:** ANECDOTES #48. Related: #45 (the gate as a file rather than a command) and
`docs/GATE_INTEGRITY.md`, which is the same shape of oversight one level over — jichi
protecting what a run produces while leaving what it is measured by, and now what it
discards, unguarded.

## 8. Cleaning the shadow store: `jichi checkpoints gc`

> **Shipped as M335, READ-ONLY.** `jichi checkpoints gc` scans the store, classifies each
> repository, and prints what is garbage. It **removes nothing.** Removal needs a `--yes`
> flag, a guarded recursive delete, and byte accounting, and it is a separate step for the
> same reason M83 shipped a milestone before M142: the classification is the safe half and
> should be trusted in the field before anything acts on it. `doctor` now reports the store
> so the command is discoverable (§8.3).
>
> Validated against the hand measurement in §0: it reports **11 live, 10 orphaned**, the same
> ten paths, which is what makes the classifier believable rather than merely plausible.

Found by §0's measurement rather than sought: **21 shadow repositories, 61 MB, 10 of them
orphaned** — their `core.worktree` points at a directory that no longer exists. `do_prune`
bounds the commits *inside* a live repo at startup. Nothing has ever removed a repo, so every
throwaway `/tmp` workspace and every deleted git worktree leaves one behind forever.

### 8.1 Three classes, one verb

```
jichi checkpoints gc [--orphans-only] [--older-than <dur>] [--yes] [--output json]
```

| class | criterion | default action |
| --- | --- | --- |
| **orphaned repo** | `core.worktree` gone, parent still exists | **listed (M335)**; removal deferred |
| **unreachable repo** | `core.worktree` gone AND its parent gone | **listed and refused (M335)** — looks like an unmounted volume |
| **over-limit commits** in a live repo | more than `2 × snapshotLimit`, the existing `do_prune` rule | already done at startup; `gc` reports the reclaim |
| **expired discarded refs** | older than `--older-than`, or beyond `discardedLimit` | listed; removed with `--yes` |

Without `--yes` it prints exactly what it *would* remove, with paths and byte counts, and
exits 0 having changed nothing. That is deliberate (D7): jichi is driven non-interactively as
often as not, so a y/n prompt is unavailable, and a destructive default in a command whose
purpose is tidiness would be the worst possible place for one.

### 8.2 The honest caveat about orphan detection

**A workspace on an unmounted volume is indistinguishable from a deleted one.** Both present
as "`core.worktree` does not exist". No check can separate them from inside jichi.

So orphan removal must never be automatic, must print every path it intends to remove, and
must require `--yes` — which is D6 and D7 arriving from a second direction. A user with an
external disk detached will see their own paths in the plan and stop. A user who cannot see
the plan cannot make that judgement, which is why there is no `--force` and no config key to
enable this silently.

A smaller mitigation worth adding: skip a repo whose worktree path is *under* a directory that
also does not exist, since that is the signature of an unmounted mount point rather than a
deleted project. It is a heuristic and should be stated as one, not relied on.

### 8.3 Where the need surfaces

`doctor` gains one line — store size, repo count, orphan count — because `doctor` is already
run routinely and `gc` is not. A cleanup command nobody knows to run is the same failure as
the multi-toolchain gate nobody ran (ANECDOTES #47): **the tool existing is not the same as
the tool being reached for.** Discoverability is part of the feature, not documentation for it.

### 8.4 What `gc` deliberately will not do

- **It will not touch the user's own repository.** Ever. The shadow store is the only thing in
  scope, and that boundary is the ANECDOTES #1 lesson.
- **It will not run on a schedule or at startup.** D6.
- **It will not delete a live repo's checkpoints below `snapshotLimit`**, however old. Those
  are what `undo` needs.

## 9. Recommended order

1. **`checkpoints gc` first** (§8), with `doctor` reporting the store (§8.3). It is overdue,
   independent of everything else, and 61 MB of measured garbage argues for it now. It also
   removes the objection to D1 ever becoming a default.
2. **PRESERVE-BEFORE-DESTROY on the rollback path only** (§4.1), opt-in, since §0 shows every
   `verify_failed` run discards work and that is the path the incident used.
3. **`attempts` / `recover`** (§4.4). Preservation without a retrieval surface is a store
   nobody can read — and the failure this whole page is about was as much "could not look at
   it" as "could not restore it".
4. **Extend to `--revert-out-of-scope` and `undo`** (§2's other two rows) once the rollback
   path has run in anger.
5. **Consider flipping D1's default**, with evidence, once 1–4 have been used.

Steps 1 and 2 are each a day's work of the kind jichi already does well. Step 3 is the one
worth taking time over, because a recovery command that can damage the working tree would turn
this proposal into its own counterexample.

---

## 10. Step 5: the default, and why it is not the next thing to change (2026-08-09)

Step 5 was written as "reconsider whether `--preserve-discarded` should default on, once
there is field evidence." Investigating it turned up a structural fact that settles the
ordering, so this section records the decisions rather than the flip.

### 10.1 The finding: preservation is a store with no eviction

**Nothing ever deletes a ref under `refs/jichi/discarded`.** Checked, not assumed: the only
code that touches that namespace is `update-ref` (write) and `for-each-ref` (read).

The consequences compound:

- A ref makes its commit reachable, so `git gc --prune=now` -- which `do_prune` already runs
  -- can never collect the objects behind it.
- `snapshotLimit` and `do_prune` bound the **checkpoint branch** only. They rewrite history
  with `commit-tree` and repoint the branch; a discarded ref is not on that branch and is
  untouched.
- So every preserved state is pinned permanently, and the store grows monotonically for the
  life of the workspace.

That is the same shape as the problem M335 was written to measure -- 61 MB across 21 shadow
repositories, 10 of them orphaned -- except self-inflicted and unbounded. **Defaulting on a
mechanism that pins objects forever, while the only tool that could reclaim them has just its
listing half, would re-create the problem the previous milestone measured.**

> **Steps 1-3 shipped as M338.** The packed measurement replaced the inflated figure below:
> **5 KB per preserved state**, not 42-52 KB -- an 8x overstatement, and the basis on which
> the interactive default was flipped on. See the ROADMAP's M338 entry.

### 10.2 Measured cost, and the number not to trust

| what | measured | note |
| --- | --- | --- |
| a preserved state, ~10 KB of new content | **+52 KB** of shadow repo | loose objects; packing should reclaim most of it |
| wall time added to `jichi undo` | **0.06 s** | latency is a non-issue and can be dropped from the argument |
| **the same, after `git gc`** | **+5 KB per state** (20 states, 40-file tree, +108 KB packed) | measured 2026-08-09; this is the defensible number |
| dedup of an identical re-preserved state | **not established** | repeated `undo`s had likely exhausted the checkpoint stack, so some runs no-op'd; the "+4 KB" may be a failed undo doing nothing |

The 52 KB figure is **known inflated** and is not a basis for a default. Git stores loose
objects zlib-compressed but one file each, and `gc` packs them; the honest number is N
preserved states on a real repository *after* `git gc`, which §10.4 step 2 measures.

Recording the untrustworthy row rather than dropping it, because "I measured this and it does
not support a conclusion" is the finding, and a table with only the convenient rows is how
docs/TEST_INTEGRITY.md's failures happened.

### 10.3 Decision: split the default, do not flip the key

M336's argument for off-by-default was precise: *it writes to disk while a run is already
failing -- its failure mode overlaps its trigger.* That is sound for the **envelope rollback**.
It does not apply to an interactive `undo`, where the tree is healthy, nothing is failing, and
the user is present and has just asked for something irreversible.

M337b bundled both under one key and defended it ("one gate, cannot disagree with itself").
The cost of that choice is now visible: one key, two risk profiles. The resolution that keeps
the benefit is jichi's existing tri-state idiom, which `pathFence`, `promptCache` and
`selfReview` all use:

| `preserveDiscarded` | interactive `undo`/`rewind`/`revert` | envelope rollback |
| --- | --- | --- |
| unset (`-1`, the default) | **on** -- irreversible, user present, healthy tree | off -- unchanged |
| `false` (0) / `true` (1) | follows the key | follows the key |

Still one key for the user to set; the *defaults* follow the risk instead of the
implementation's convenience. **Rejected alternative:** two separate keys
(`preserveDiscarded` + `preserveOnUndo`). It makes the asymmetry explicit at the cost of a
config surface where the two can contradict each other, which is the thing M337b's one-gate
decision was protecting.

### 10.4 Retention policy: age plus count, not size

The design question gc's removal half has to answer. **Keep everything from the last N days,
plus the newest K per repository regardless of age.**

- **Age, because the value decays fast.** You want a discarded attempt the same day you lost
  the work. A six-week-old one is noise, and its cost is permanent.
- **Plus a count floor, because age alone deletes the only copy** of a state from a workspace
  nobody has touched in a month -- which is exactly when you are least able to reproduce it.
- **Not size-based.** A size cap requires deciding which of two states is worth more, and the
  honest answer is that jichi does not know. Age and count need no judgement.
- **Report before removing.** `gc` prints what it would reclaim and removes nothing without
  `--yes`, matching `undo --dry-run` and the M335 refusal to guess about unreachable repos.

### 10.5 Order of work, and why this order

1. **`checkpoints gc`'s removal half**, covering orphaned repositories *and* discarded refs
   under the §10.4 policy, with byte accounting. This is the blocker: it is what makes the
   store bounded, and nothing about a default should change before it exists.
2. **Re-measure packed**, to replace the inflated 52 KB with a defensible figure.
3. **The §10.3 tri-state split**, with that measurement in the commit message.

The dependency is real rather than tidy-minded: step 3 turns on a growth rate that step 2
measures and step 1 bounds. Shipping 3 first would set a default from a number known to be
wrong, in a store known to have no eviction.
