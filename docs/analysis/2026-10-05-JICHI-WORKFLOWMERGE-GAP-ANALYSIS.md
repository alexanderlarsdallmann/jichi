# jichi gap analysis: subagent / workflow worktree merge (data flow)

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey), HEAD `1b71247d`.
Method: find the one thing the area is definitely lacking; verify every
load-bearing claim against source; flag inferences; appendix reproducible.*

## TL;DR

The one thing lacking: **`run_workflow` has a *second*, simpler worktree merge
that silently destroys a conflict-loser's work — the exact class
`spawn_parallel` hardened and preserved (M803) — with no test that plants the
conflict.** `spawn_parallel`'s merge (M144 per-child verify gate, M133
edit-scope fence, M803 keep-the-loser's-worktree + a `discarded/parallel-
conflict` ref, a per-file conflict list, M888 new-dir holds) is one of the
most carefully-built paths in the tree. But `run_workflow`
(`main.c:11391`, a live subcommand) does not reuse it: it runs its **own**
bare loop — `jc_parallel_claim` → `wf_apply_change` → `conflicts++` — reports
only "(conflicts skipped)", applies **none** of M803/M133/M854/M888, and then
**unconditionally removes every worktree**, so the losing step's un-merged
changes are gone with no Kept ref and no per-file report. Two parallel
features, two merge implementations, one hardened; the weaker one is live,
untested for its one destructive case, and its divergence is nowhere recorded.

## 1. The two merges, side by side

**`spawn_parallel`** — `src/tools/jc_tool_parallel.c:2499+` (the hardened one):
- **M144** per-child verify gate: a red child is quarantined, not merged.
- **M854** a child that did not end on its own merges only after a green
  worktree verify; otherwise its work is **Kept** (a named ref + note).
- **M133** edit-scope fence at merge time (a shell-escape can't smuggle an
  out-of-scope file back).
- **M888** held new-directories kept out of the merge.
- **M803** a conflict **keeps the loser's worktree** → `discarded/parallel-
  conflict` ref + a per-file conflict **list** + a `had_conflict` flag.
- **M855** stash leftovers preserved.

**`run_workflow`** — `src/main.c:11391`, merge at `:11545-11600` (the bare one):
- `jc_parallel_claim(&seen, c->path)` → `wf_apply_change(...)` on win;
  `conflicts++` on loss. That is the *entire* conflict handling.
- Report: a single `conflicts > 0 ? " (conflicts skipped)" : ""` string.
  No per-file list, no Kept ref, no preserve.
- **None** of M803/M133/M854/M888/M855.
- Then, unconditionally: `jc_snapshot_worktree_remove(&wfm, wts[j],
  "workflow", NULL)` for **every** step — the loser's un-merged changes are
  destroyed along with its worktree.

They share exactly one helper — `jc_parallel_claim` (`jc_parallel.c:449`, a
path-set) — which is what makes the divergence look like a copy that was
hardened in one place and left alone in the other.

## 2. The crux: destructive, live, untested, and unrecorded

1. **Destructive.** The losing step's work is not kept anywhere: no
   `discarded/*` ref, no Kept note, no stash. It is in the worktree until
   `worktree_remove` runs, then it is gone. A workflow step that edited
   `foo.c` alongside a sibling that edited `foo.c` gets its edits to `foo.c`
   silently thrown away, with the only trace being "(conflicts skipped)" in
   the report — a *count*, not a *list*, so the user cannot even tell which
   step lost which file.
2. **Live.** `workflow` is a real subcommand (`main.c:17551`), driven by
   `workflow.sh` / `workflow_refute.sh`. It is not an aspirational path.
3. **Untested for the one case that matters.** `workflow.sh` (t_plan 7) drives
   an **empty** stages file and fence refusals; `workflow_refute.sh` counts
   refusals across its stages. **No test plants two steps that write the same
   path** — the one shape that reaches `conflicts++` and the
   `worktree_remove` that destroys the loser. Contrast `spawn_parallel`, whose
   conflict-loser preservation is driven by four tests (`parallel_preserve`,
   `parallel_end_record`, `parallel_kept_section`, `parallel_dropped_top`).
4. **Unrecorded.** The divergence (workflow = the un-hardened twin) is not in
   `DEFERRED.md`, `SNAPSHOTS.md`, or any doc. The `spawn_parallel` merge's
   hardening history (M803's run-0070, M854's run-0085/0104, M888) reads as if
   worktree-merge conflict is *a solved problem* — it is solved for one of the
   two features that have it.

## 3. Why "definitely lacking" (and the alternatives)

- *spawn_parallel's merge* — the most thoroughly hardened and tested
  worktree-merge path in the tree. Not the gap.
- *The 3-way resume (`jc_snapshot_resume_apply`, `from:` field)* — real git
  3-way merge; a conflict is "a tool-error value naming the files, never a
  crash." Not the gap (that is the *good* merge).
- *run_workflow's conflict-loser handling* — the one place a worktree merge
  **destroys** work, **untested** and **unrecorded**. The gap.

## 4. The register's honesty about the rest

- `spawn_parallel`'s merge hardening is *documented run-by-run*: M803
  (run 0070's kept ref was empty, its change in the stash), M854 (a capped
  child merged unverified before), M855 (the shared stash stack), M888
  (held new-directories, "Name all, …" operator decision 2026-10-03), M144
  (the per-child verify gate). The `parallel_*.sh` suite drives each.
- `jc_snapshot_resume_apply` (the 3-way `from:` merge) — "a conflict (or a
  bad ref) is a TOOL-ERROR VALUE naming the files, never a crash."
- `workflow.sh` itself says it: the edit-scope fence was "a gate … held only
  by a neighbour's driver … one refactor away from unheld" — the project
  knows that a property held by one feature's test does not transfer to its
  twin. That is exactly the situation with the conflict-loser handling.

## 5. Evidence

- `src/main.c:11391` — `run_workflow`; `:17551` — the live `workflow`
  subcommand.
- `src/main.c:11545-11600` — the bare merge: `jc_parallel_claim` →
  `wf_apply_change` / `conflicts++`; the `conflicts > 0 ? " (conflicts
  skipped)" : ""` report; then `jc_snapshot_worktree_remove(&wfm, wts[j],
  "workflow", NULL)` for every step.
- `src/tools/jc_tool_parallel.c:2499-2795` — the hardened twin (M144/M854/
  M133/M888/M803/M855), conflict losers → `discarded/parallel-conflict`.
- `src/tools/jc_parallel.c:449` — `jc_parallel_claim` (the shared helper,
  a path set: returns 1 and records, or 0 if already claimed).
- `tests/smoke/parallel_preserve.sh`, `parallel_end_record.sh`,
  `parallel_kept_section.sh`, `parallel_dropped_top.sh` — the conflict-loser
  preservation, driven (grep: `refs/jichi/discarded/parallel-conflict`).
- `tests/smoke/workflow.sh` (t_plan 7) — empty-stages + one scripted map;
  **no same-path conflict** (grep for conflict/same-file/both-write: empty).
- `grep -in "conflict\|first-wins" docs/*.md` → no workflow conflict note;
  `DEFERRED.md` → no row naming the workflow merge's destructive-loser
  behaviour.

## 6. Implications

- **A user running a multi-step `jichi workflow` can lose a step's work
  with no per-file trace.** If two steps touch the same file, the second
  (or first, by claim order) wins and the other's edits vanish; the report
  says only a count. Unlike `spawn_parallel` there is no `jichi attempts`
  ref to recover from, no Kept list, no "these paths conflicted" line.
- **The hardening did not propagate.** The register's M803 lesson ("a
  conflict-loser's work must survive the merge") was learned for
  `spawn_parallel`. `run_workflow` shares the *claim* helper but not the
  *preserve* logic — the two call sites of `jc_parallel_claim`
  (`jc_tool_parallel.c:2742`, `main.c:11557`) have since diverged, and
  nothing keeps them in lockstep. That is the register's "wiring is the
  other half" shape (M439/M536) at the *merge* layer.
- **The untested destructive path is the one most likely to ship a bug
  silently.** A future change to the shared base/worktree setup that makes
  the workflow merge's `apply_change` misbehave would not be caught —
  `workflow.sh` never produces a conflict, so the `conflicts++`/
  `worktree_remove`-destroys path is the least-exercised line in a live
  feature.

## 7. Recommendation

1. **Make the workflow merge non-destructive on conflict, at minimum as
   badly as the twin.** Before `worktree_remove`, for every step that had a
   conflict, either (a) reuse the M803 pattern — preserve the un-merged
   changes to a `discarded/workflow-conflict` ref and name them in the
   report — or (b) the cheap version: emit the **per-file list** of lost
   paths (not just the count) and a `jichi attempts`-style recovery hint.
   The count-only "(conflicts skipped)" is the specific defect.
2. **Red-first test: two workflow steps write the same path.** `workflow.sh`
   gains a check that plants two map items both writing `foo.c` with
   different content, and asserts (i) exactly one version lands, (ii) the
   report names the lost step's path, and (iii) [after step 1] the loser is
   recoverable. Before the fix it is red on (ii) — the report is a bare
   count. This is the direct analogue of `parallel_preserve.sh`.
3. **Share the merge, not just the claim.** The root cause is that
   `jc_parallel_claim` is shared while the surrounding loop (preserve,
   scope, verify, report) is duplicated and has diverged. Extract the
   preserve-on-conflict + per-file-report into a shared helper both call
   sites use — the register's "the wiring is the other half" fix, at the
   merge layer.
4. **A DEFERRED row** naming the divergence until it is closed: `run_workflow`
   merges first-wins and destroys conflict losers (no M803 preserve, no M133
   scope fence, count-only report), unlike `spawn_parallel`; untested for the
   conflict shape.

Secondary (recorded, not the primary fix): the workflow merge has **no
edit-scope fence** (`wf_apply_change` copies any file a step touched, in or
out of scope, via the shell) — the M133 gap, same divergence.

Rejected: *make workflow refuse to run if it would conflict* — that changes
the feature's semantics (map steps are meant to run concurrently); the fix is
recoverable loss + a per-file report, which is what the twin already chose.

## 8. Limitations and honest counterarguments

1. **`spawn_parallel`'s hardening may postdate the workflow and simply not
   have been back-ported.** That is a *history* explanation, not a defence:
   both are live at HEAD, the register's doctrine is that the state described
   is the state the tree has, and the tree has two divergent merges. The gap
   is the divergence, whenever it arose.
2. **A workflow is usually a *map* (fan-out over items), where same-file
   conflicts are less common than in adversarial parallel edits.** True, but
   "less common" is the exact weight M803's preservation was designed for —
   the unlikely-but-destructive case. The teeth doctrine (analysis 8) exists
   because "however it is stopped" includes the unlikely stop.
3. **I have not driven a workflow conflict.** §2's claims are from the merge
   loop, `wf_apply_change`, the `worktree_remove` ordering, and the test
   inventory (read, not run). The recommendation's red-first test is what
   would prove the destruction and close it.
4. **The per-file report gap is real but small in isolation.** The *large*
   gap is the destruction; the report is the tell. I weight the fix by the
   destruction and name the report as the minimum the test should assert.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the live workflow feature
grep -n "workflow\"" src/main.c | head -2          # :17551 subcommand
awk 'NR>=11391 && NR<=11393' src/main.c             # run_workflow

# the BARE merge: claim -> apply/count, no preserve, then remove
sed -n '11545,11600p' src/main.c                    # conflicts++, worktree_remove
sed -n '11310,11340p' src/main.c                    # wf_apply_change (bare copy)

# the HARDENED twin (M144/M854/M133/M888/M803/M855)
sed -n '2499,2560p' src/tools/jc_tool_parallel.c    # verify gate, no_merge, M854
sed -n '2765,2790p' src/tools/jc_tool_parallel.c    # M803 keep-on-conflict
grep -n "discarded/parallel-conflict" src/tools/jc_tool_parallel.c

# the shared helper they both call (and only they)
sed -n '449,465p' src/tools/jc_parallel.c          # jc_parallel_claim
grep -rn "jc_parallel_claim" src/ --include=*.c | grep -v "\.o"

# the conflict-loser preservation, driven (spawn_parallel only)
grep -ln "discarded/parallel-conflict" tests/smoke/parallel*.sh

# the workflow test never plants a same-file conflict
grep -n "conflict\|same\|both.*write\|stages" tests/smoke/workflow.sh
grep -in "workflow" docs/*.md | grep -i "conflict"    # (empty)
grep -n -i "conflict" docs/DEFERRED.md | grep -i workflow  # (empty)
```

**Verdict line for the register this could seed:** *`spawn_parallel`'s
worktree merge is the most hardened in the tree — per-child verify (M144),
edit-scope fence (M133), keep-on-conflict to a `discarded/parallel-conflict`
ref (M803), new-dir holds (M888), a per-file report — and it is driven by
four conflict tests. `run_workflow` is a live subcommand that does its own
worktree merge with the *same* `jc_parallel_claim` helper and *none* of that
machinery: on a same-path conflict it counts the loss, applies the first
writer, and then `worktree_remove`s every step's tree — destroying the
loser's un-merged changes with no ref, no per-file name, and no recovery
hint. `workflow.sh` (empty stages + one scripted map) never produces a
conflict, so the one destructive line in a live feature is the least-
exercised one, and the divergence from the twin is not in DEFERRED.md or any
doc. Revisit when: a red-first test plants two workflow steps writing one
file, the merge reports the lost path per-file, and a conflict loser's work
is preserved (shared M803 logic, or a documented `discard`-equivalent) — at
which point the two `jc_parallel_claim` call sites stop silently diverging.*
