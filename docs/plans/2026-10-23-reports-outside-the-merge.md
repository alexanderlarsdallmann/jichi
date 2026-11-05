# Reports outside the merge (def-332) — design note

*Batch 14 item (c). The finding is [def-332](../DEFERRED.md#def-332). The full
candidate text (the machine-readable header, the read-back surface) is on the
`analysis/2026-10-18-orchestration` branch (`docs/plans/2026-10-21-jichi-harness.md`
§10.5, `docs/analysis/2026-10-22-one-report-file-for-every-worker.md`), which the
drive's container cannot reach (the remote is harness-managed and read-only from
here). This page settles the shape against the current tree and names the
follow-ups. Written 2026-10-23 (run 0071).*

**Status:** design settled for M1044 (the defect-closing core). The operator's
23:34 grant lets item (c) go ahead red-first; (c)/(d)/(e) are red-first
IMPLEMENTATION items.

## The defect, measured on the current head (run 0071)

Two `write: true` tasks, each running `write_file {"path":"REPORT.md", ...}` with
different content, against `jichi` built at `d2e93ccb` (the mock model, a git
workspace, `< /dev/null`):

```
$ ls $WS                      # the live tree after the batch
REPORT.md  seed.txt
$ cat $WS/REPORT.md
REPORT A: task A evidence     # task 1's content, merged
# task 2's report: "REPORT B: task B evidence" -- GONE from the tree
```

The batch result's merge section said `## Merge: 1 file(s) applied, conflicts
skipped (first writer kept):\n  REPORT.md (task 2)`, and `### Task 2` carried a
**Kept** state (`parallel-conflict`). Two facts, both wrong for a report:

1. **The second report is silently dropped as a code conflict.** It is evidence,
   not a change, and it lost to the first writer's copy with no way to recover
   the other from the live tree.
2. **A report collision forced task 2 into a preserved Kept state.** The only
   change task 2 made was its report, yet `had_conflict` (set by the dropped
   report) made the whole worktree kept as `parallel-conflict` — this is the
   "38 reports set aside" the finding names: a report made a code-clean task
   read as unfinished work a successor must triage.

The root cause, by reading: the per-task merge loop
(`src/tools/jc_tool_parallel.c:2749`) treats every path in the child's
worktree diff against the pre-batch base as a *code change* to copy
first-wins into `app->root`. There is no notion of "this path is the report".
A root `REPORT.md` is indistinguishable from any other added file, so it rides
the merge like code, collides like code, and is dropped like code.

## M1044 — the core (this milestone)

In the per-task merge loop, pull a **root report** out of the code merge
*before* the first-wins claim, and store it with the run's records instead.

**Recognition.** A change is a report iff its path has no `/` (it is at the
worktree root, not in a directory) and its name is `REPORT.md` or `REPORT-*.md`
(matching the `snapshot_lint` check 16 name that refuses a report at the root).
Status `A` (added) or `M` (modified) is captured; a `D` (deleted) root report is
not captured (a worker deleting the report is a code event and merges as such —
rare, and the report was in the base, not the child's to own).

**Kept out of the merge.** When a change is a report, the loop does NOT call
`apply_change` for it and does NOT record it as a conflict or a drop. So:
- two tasks that both write a root report no longer produce a
  "first writer kept" drop, and
- a report-only task no longer sets `had_conflict`, so it is not forced into a
  Kept state for the report's sake. Its other code changes (if any) still merge
  and still conflict as before.

**Stored redacted, with the run's records.** The child's copy of the report is
read from its worktree (`<wt>/REPORT.md`), passed through `jc_redact_secrets`
(so a key a worker pasted into its report does not reach the record store), and
written to `~/.jichi.d/attempts/<run>/report-t<N>.md` — beside the child's own
transcript `t<N>.jsonl` (the run's record store, opened at
`jc_tool_parallel.c:774`). The directory is `jc_mkdir_p`'d first. A store
failure is not fatal: the report is still kept out of the merge and the result
says the store failed (a lost report is the defect this fixes, so a failed
store must not be silent).

**Named in the result.** The aggregate result gains a line per stored report:
`[report] task <N>: stored at ~/.jichi.d/attempts/<run>/report-t<N>.md (kept
out of the merge)`. Placed where the Kept/Dropped sections are, so the lead sees
its own workers' evidence with the refs, not buried.

**Red-first.** A new driver `tests/smoke/parallel_reports.sh`: two write tasks,
each `write_file`s a root `REPORT.md` (distinct content), each also writes a
distinct code file (`a.txt`/`b.txt`) so the code-merge path is exercised in the
same batch.
- both reports are stored: `report-t1.md` and `report-t2.md` exist under
  `~/.jichi.d/attempts/<run>/` with the (redacted) child content;
- neither report is merged: no `REPORT.md` in the live workspace;
- nothing is set aside *for the report*: neither task is Kept as
  `parallel-conflict`, and the merge section lists no `REPORT.md (task N)`
  drop;
- the code still merges: `a.txt` and `b.txt` land in the live workspace;
- a task whose ONLY change is a report (a third task) is not forced into Kept.

On the unfixed head this driver is red at least on: `report-t1.md`/`report-t2.md`
absent (nothing is stored), `REPORT.md` present in the workspace (the first
writer merged), and a `REPORT.md (task N)` drop / a `parallel-conflict` Kept.

## Follow-ups (named, NOT in M1044)

- **M1044-b — the machine-readable header.** The stored report gets a small
  header naming task id, the lead's label (a label only), model, build
  (`jc_build_rev()`), base and final commit, outcome, and the red-first command
  and lines. Requires the child to report its own HEAD to the parent (the pool
  already reads the child's end-of-run state through its pipe; a final
  `git rev-parse HEAD` or reading the manifest's base + the diff's last commit).
- **M1044-c — the read-back surface.** `jichi attempts show` (or a new
  `jichi attempts report <run> -t <N>`) prints the stored report, mirroring the
  transcript tail it already prints (`src/main.c:9590`). M1044 names the path in
  the result; that is enough for the lead (a model) to `read_file` it, and it is
  the defect-closing part. The human-facing surface is separate.

These are split off because M1044 closes the measured defect (drop + spurious
Kept) with one recognition + one store, and the header/read-back add their own
red-firsts without changing the merge behaviour.

## Rejected

- **Redirect the report into the workspace convention (`docs/internal/.../reports/`)** —
  that keeps the lead filling in each path by hand, which is exactly the
  convention the finding says the milestone should retire; it also re-enters the
  merge as a code change.
- **Treat a report as `merge: false` for the whole task** — a report is one path,
  and the task's *code* must still merge; marking the task no-merge would keep
  its code worktree too.
- **Drop the second report loudly and stop there (no store)** — that fixes the
  spurious-Kept half but still loses the second worker's evidence; the finding's
  red-first is "both are stored", not "both are named-and-lost".

## Falsifier

The driver's "both reports stored / neither merged / nothing set aside" legs
pass only if the store path is written and the report is excluded from both
`apply_change` and the conflict count. A build that stores but still applies
(one copy appears in `$WS/REPORT.md`) fails the "neither merged" leg; a build
that excludes but does not store fails the "both stored" leg; a build that
excludes the merge but still sets `had_conflict` for the report leaves a Kept
state and fails "nothing set aside".

## Not goals

- The B1 non-blocking pool (item (b), operator's decision pending).
- Redaction of anything but the report body (the store is the new surface).
- Reports written below the worktree root (a report in `reports/x.md` is code).
