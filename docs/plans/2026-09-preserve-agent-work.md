# Keep an agent's work when it is stopped, and stop it before the disk fills

**Opened** 2026-09-27, during the zigodot weekend drive, at the operator's request (§0).
**A plan, not work:** no code has changed. The steps are named P1–P6 rather than
numbered, because milestone numbers are taken by whichever machine merges first
(`2026-09-windows-family.md` §1); each step takes the next free number when it starts.

It extends the preservation line of M336–M338
([`../proposals/2026-08-work-preservation.md`](../proposals/2026-08-work-preservation.md)),
which kept the *workspace's* discarded states, to the *worktrees* that line never
covered. It also adds the one resource check jichi does not make while it runs: free
disk space.

---

## 0. The request

> "Please, note, that we may need a way to store an agent's work, even if it gets
> aborted, so it can be anaylzed, reused, built-upon by the same agents, or another
> agent, later. This has to be an option, possibly on by default. System ressources
> must be checked, so that agents can stop their work if disk space is limited. Design
> the plan, and store it in a jichi branch, for later, please."
>
> — the operator, 2026-09-27

Four requirements, each with the observation that would show it met:

| | requirement | met when |
|---|---|---|
| **R1** | **Kept.** Whatever an agent changed survives any way that agent can end: an answer, a red gate, a merge conflict, a watchdog kill, a signal from outside, or the death of its parent. | A child killed in the middle of an edit leaves that edit readable afterwards. |
| **R2** | **Usable.** A kept state can be *analysed* (what it changed, why it stopped, what it did last), *reused* (materialised as a tree), and *built upon* (a new task starts from it), by the same kind of agent or another. | A second batch resumes a stopped task from its kept state and finishes it. |
| **R3** | **An option, on by default where that is safe.** | One key, with each default argued in §4.5. |
| **R4** | **Resources checked.** An agent stops, keeping its work, before the disk it writes to runs out. | A run whose disk crosses the floor stops with the reason `disk`, its work kept and its records intact. |

## 1. What happened

Run 0198 of the drive, 2026-09-27. The lead ran one `spawn_parallel` batch with two
write children, both on `jlu/qwen3-coder-next`:

- **Card 48** (the editor's inspector, E6 slice 3) looped for about three hours on a
  window check that hung. It made 165 tool calls and used 218.6k tokens, past its
  soft budget of about 150 calls. It was stopped from outside with SIGTERM at 11:07.
- **Card 37** (a leak at pong_full's exit, pass 3) spent 11,677 s, about three and a
  quarter hours, inside one model call that never returned: no status and no bytes.
  It was stopped from outside at 11:11:41, and that ended the batch.

Both reported `(error: interrupted)`. This is what was left afterwards:

| what | where it was | after the batch |
|---|---|---|
| card 48's uncommitted edits | its worktree | **gone.** The pool removes every worktree when a batch ends (`src/tools/jc_tool_parallel.c:1035–1042`), and merges only children that answered (`:907–917`). |
| card 48's own commit `7777385` | the shadow repository, through the worktree | **an unreferenced object.** No ref names it, so the next `git gc --prune=now` that `do_prune` runs deletes it. |
| card 37's edits | its worktree | **gone.** |
| both conversations | the children's memory | **gone.** A parallel child saves no session. |
| their model calls and tool calls | the full-tier telemetry | **present, but not attributable.** Every depth-1 record carries the lead's `sid`, `run` and `turn`, and the lead's model name (`qwen27b`, while both children ran coder-next). Two children running at once cannot be told apart. |

Across the whole drive, the lead ran 19 batches with 37 tasks, 32 of them write tasks.
**Seven write tasks ended with their work thrown away:**

- four were quarantined by their verify gate (three in run 0184, one in 0197);
- one was lost to the model's context window;
- two were the children above.

These counts come from the pool's summary lines in the telemetry. The copy of each
batch's result in the lead's saved session is cut short: only 27 of the 37 task reports
can be read there.

The pool's own comment calls a child that fails its gate (M144) "quarantined --
reported, not merged". About a hundred lines later, the teardown deletes it. A quarantine that
destroys what it holds is not a quarantine.

## 2. What already exists

From the M336–M338 line:

- **`jc_snapshot_preserve`** commits a tree and pins it under `refs/jichi/discarded/…`
  before the destructive step. The rule, from §4.1 of the proposal: *no code path may
  discard a tree state it has not first committed.*
- **`preserve_before_destroy`** applies that rule to `undo` and `revert` (M337b). The
  ref is `refs/jichi/discarded/<why>/<unixtime>-<n>`, so the path records which
  mechanism discarded the state.
- **`preserveDiscarded`** is a tri-state; `-1` means unset. When it is unset,
  preservation is **on** for the interactive restores and **off** for the envelope's
  rollback. This is the proposal's §10.3, shipped with M338.
- **`jichi attempts`** lists the kept states, and **`jichi recover <commit|ref> --into
  <dir>`** materialises one as a worktree.
- **`jichi checkpoints gc`** bounds the kept states: seven days, plus the newest
  twenty (`jc_snapshot_retain`, §10.4).
- **M410**'s `--keep-worktree` keeps an `attempt`'s sandbox. **M616** sweeps stale
  `att-`, `imp-` and `wf-` worktrees.

For resources:

- **`jc_disk_free_mb`** (statvfs `f_bavail`) exists, and has one caller: `doctor`'s
  machine profile (`src/main.c:13261`).
- **`jc_resource_tier`** classifies a machine as NORMAL, LITE or MINIMAL.
- **`memBudgetMb`** (M117) is a resource watchdog on shell commands. It follows
  M326q's rule: a key that silently does nothing where it cannot work is worse than a
  key that refuses.
- **`jc_lock_take`** (M760) is an fcntl lock that the kernel drops when its holder
  dies.

## 3. The gaps, precisely

### G1. Four destroyers of worktrees, and preservation covers none

| site | destroys | kept today |
|---|---|---|
| `jc_tool_parallel.c:1038`, the pool's teardown | every child's worktree, merged or not | nothing |
| `src/main.c:10244`, `run_improve_attempt` | the attempt's worktree | nothing |
| `src/main.c:10628`, `run_assignment_attempt` | the attempt's worktree | only with `--keep-worktree` (M410) |
| `src/main.c:11009`, `run_workflow` | a stage's worktrees, after the merge | nothing |

All four go through `jc_snapshot_worktree_remove` (`src/snapshot/jc_snapshot.c:868`).
Four `rm -rf` sites can also reach a worktree:

- the function's own fallback for a git older than 2.17 (`jc_snapshot.c:901`, M272);
- M616's `prune_worktrees`, which deletes stale trees, as a cleanup should
  (`src/main.c:3235`);
- the improve loop and `attempt`, which then remove the *base directory* the tree lived
  in with `rm -rf` (`src/main.c:10272` and `:10631`).

The pool and the workflow remove their base directory with `rmdir`, which fails on a
directory that still has something in it. Once the chokepoint can keep a tree (§4.1),
the two `rm -rf`s of a base directory must become `rmdir` as well. Otherwise they
delete what the chokepoint kept.

M336 and M337b listed the destroyers of the *workspace*. Worktrees were never on that list, because a worktree was a sandbox
and a sandbox is disposable. That holds until the thing in the sandbox is the only
copy of three hours of work.

### G2. A kill leaves no time to preserve

The pool's watchdog sends SIGTERM and then SIGKILL 300 ms later
(`JC_PAR_TERM_GRACE_MS`). A kill from outside may give no time at all. A child cannot
be relied on to save itself. The parent must preserve from the worktree after it has
reaped the child, and if the parent dies too, nobody preserves.

### G3. An orphaned pool worktree is swept by nothing and reported by nobody

The pool names its base directory `~/.jichi.d/worktrees/<pid>` after the parent's pid
(`jc_tool_parallel.c:813`). M616's sweep recognises only the names it mints and skips
the rest: "not ours to sweep (e.g. the parallel pool's trees)". So when a parent is
killed during a batch, its trees stay on disk for good.

It gets worse in a container. Under `--init`, every run's lead is pid 2; this was
checked in the drive's container during run 0199. So the base a dead run leaves
behind *is* the next run's base. `git worktree add` into an existing directory
fails, and then the pool runs that task **read-only** with a one-line note
(`jc_tool_parallel.c:822–828`). That consequence **has not been observed yet**. It
follows from the code and must be confirmed before P3 is built on it.

Pid reuse also defeats every liveness test that is a bare `kill(pid, 0)`: M616's
sweep and the run lease (`jc_lease_pid_alive`) both use one.

### G4. Nothing is written that a successor could read

A child writes no transcript and no end record. The pool's aggregated result is the
only report. It is written after the whole batch, and the copy the lead keeps is cut
short (§1). A lead that re-issues a stopped task cannot tell its next worker *why* the
last one stopped, so the next worker can walk into the same loop.

### G5. No disk check while a run is running, and the one function cannot be a guard

Nothing stops a run as its disk fills. A full disk fails every write at once,
including the session, the journal and the shadow repository, which are the records a
post-mortem needs. Worktrees multiply the build cache: each child builds in its own
tree, so a batch of four holds four build caches until its slowest child ends.

`jc_disk_free_mb` cannot be the guard as it stands, for two reasons:

- **It returns 0 for "could not measure" as well as for "full".** A guard built on it
  would stop every run wherever statvfs fails. The classifier rule applies: the
  else-branch must say *unknown*, not make a finding (CLAUDE.md, "Tests — the rules").
- **It computes `f_bavail * (bsize / 1024)`.** That is 0 whenever the fragment size is
  below 1024 bytes. ZFS commonly reports 512, which would mean `doctor` reports 0 MB
  free on the illumos and FreeBSD rows today. This is **to be checked on those rows**.
  On a 32-bit build the KiB intermediate also wraps above 4 TiB free.

## 4. The design

### 4.1 Preserve at the chokepoint, and keep the tree when preserving fails (P1)

`jc_snapshot_worktree_remove` gains a reason and an end record, and preserves inside
itself:

```c
jc_status jc_snapshot_worktree_remove(struct jc_snapshot_mgr *m,
                                      const char *path, const char *why,
                                      const char *record);
```

- **`why`** names the mechanism and the outcome: `parallel-interrupted`,
  `parallel-watchdog`, `parallel-unanswered`, `parallel-verify-red`,
  `parallel-conflict`, `parallel-orphaned`, `attempt`, `improve`, `workflow`. It becomes
  the ref path, as in M337b: `refs/jichi/discarded/<why>/<unixtime>-<n>`. `jichi
  attempts` then says which mechanism discarded a state, with no new field.
- **`record`** is the end record (§4.2), used as the commit message.
- **There is exactly one way to skip.** A write child merged in full, with no
  conflicts, is passed as `JC_WT_MERGED`: its content is already in the parent's
  tree. Every other caller must name a reason. Changing the signature makes the
  **compiler enumerate every caller**, and a lint pins that set (§5, T5). That is the
  difference from preserving at each call site: a call site can be forgotten, but a
  signature cannot.

**How it preserves.** It runs `git -C <wt> add -A`, then `write-tree`, then
`commit-tree -p HEAD`, then `update-ref`, using plumbing. The kept state is then the
child's last commit plus its uncommitted changes. The child's own commits (such as
`7777385`) become ancestors of the kept state instead of garbage. `.gitignore` is
respected. A file over `preserveMaxFileMB` (proposed, default 16) is left out and
named in the record, so a build output that escaped `.gitignore` cannot swell the
shadow repository.

**When preserving fails, the tree stays.** This is stronger than M337b. There, a
restore the user asked for must not fail because preservation failed. Here, nobody
asked for the removal: it is housekeeping. So when the commit cannot be made (a
fault, or a disk below the stop floor, §4.6), the worktree is **kept and reported**
rather than removed, and P3's salvage picks it up later. Nothing jichi does on its
own initiative may destroy the only copy of work.

**The fifth site.** M616's `prune_worktrees` preserves before it deletes, with the
reason `pruned`. Pruning then frees the checkout and keeps the delta, and a kept delta
costs kilobytes where a checkout with its build cache costs gigabytes.

### 4.2 The end record, the transcript, and the result the lead reads (P2)

**The end record** is the kept state's commit message. It extends the proposal's §4.2
format. In this example the task, the model, the counters and the last call are card
48's; the rest are placeholders:

```
discarded: parallel task 1 of 2, interrupted after 165 tool calls

task:       Card 48 (E6 slice 3) -- the editor INSPECTOR must show ...
reason:     interrupted (signal <n>, from outside the pool)
model:      coder-next (jlu/qwen3-coder-next)
run:        <run id>    task-run: <run id>-t1
base:       <the batch's base commit>
counters:   165 tool calls, 218.6k tokens, <wall time>
last call:  run_terminal_command (killed)
repeated:   the same failing call, <n> times in a row
verify:     not run
files:      <n> changed, <k> left out (over 16 MB): <paths>
transcript: attempts/<run id>/t1.jsonl
```

`repeated` is the line a successor most needs, because it says the loop happened.
jichi does not count it yet. The envelope counts consecutive *verify* failures with
the same signature (`repeat_fails`, M89), and the delegation report names the last
failing call (M437). P2 adds the count for tool calls: consecutive calls that failed
with the same tool and the same signature.

**The transcript.** Each write child appends one JSONL line per model reply and one
per tool call to `~/.jichi.d/attempts/<run>/t<k>.jsonl`. Tool results are cut at a
cap. The file is 0600 in a 0700 directory and is flushed line by line, so a SIGKILL
loses at most the line being written. It follows `--no-session`: when sessions are
off, nothing is written. It is removed by `checkpoints gc` together with the ref that
names it; an attempt file whose ref is gone is garbage.

**Telemetry names the child.** Every record from a child carries `task` (its index),
`task_run`, and the child's own model name. This is a small separate fix. It belongs
in P2 because the transcript and the telemetry should agree on how a child is named.

**The lead reads the kept states first.** The pool's aggregated result gains a
section at the **top**, *Kept*, with one line per kept state: its ref, its reason, and
the `repeated` line. The copy of the result the lead keeps is cut short (§1), and a
ref at the bottom of a report is a ref nobody reads.

### 4.3 Kills and orphans (P3)

- **A child killed while its parent lives** is P1's case. The parent reaps the child
  and preserves from its worktree. The reason says which way the child ended:
  `interrupted` (a signal from outside), `watchdog` (the pool's own timeout), or
  `unanswered` (it stopped with no final answer).
- **A parent that dies** leaves its worktrees behind. Three changes handle that:
  1. **The base is named by a run id, not a pid:** `worktrees/par-<run>`. It is never
     reused, so a dead run's trees never collide with a live run's. This removes G3's
     read-only fallback.
  2. **Liveness is a lock, not a pid.** The parent holds `jc_lock_take` on
     `<base>/.lock` for the whole batch. M760's header says the lock dying with its
     holder "is exactly right" for a question that is only *is the holder alive?*
     Beside the lock sits a manifest, `<base>/batch.json`, with the run id, the task
     texts, the models and the start time. That follows M431e's split: the record is
     the evidence, and the lock answers liveness. An fcntl lock is not inherited
     across `fork()`, and it does not need to be: the parent's death is what makes
     the children's trees orphans.
  3. **Salvage.** An orphan is any base whose lock can be taken. At the start of
     every `spawn_parallel`, and in `checkpoints gc`, each orphan's trees are
     preserved as `parallel-orphaned`, with the manifest's task text in the record,
     and then removed. The next batch's result says so at the top: *a previous batch
     that did not finish left the work of 2 tasks: kept as …*. In a setup like the
     drive's, that message reaches the very next lead run.
- M616's `att-`, `imp-` and `wf-` names use the same pid test and fail the same way in
  a container. Moving them to the lock is a separate change and is optional.

### 4.4 Analyse, reuse, build upon (P5)

- **Analyse:** `jichi attempts show <ref>` prints the end record, the diffstat against
  the recorded base, and the transcript's tail, with `--output json` for tools. The
  existing `jichi attempts` list stays as it is.
- **Reuse:** `jichi recover` exists and works on these refs unchanged.
- **Build upon:** a `spawn_parallel` task may name `"from": "<ref>"`. The pool creates
  that task's worktree at the batch's current base, as it does now, and then applies
  the kept state's change onto it with a three-way merge. The change is the kept tree
  against the base recorded in its end record, squashed and applied with `cherry-pick
  --no-commit`. **If that merge conflicts, the task is refused as a tool-error value**
  that names the files. The lead can then re-issue the task without `from:` and pass
  the predecessor's diff as context instead.
- **The predecessor's record comes with the tree.** It is prepended to the task text,
  marked as the predecessor's: *a previous agent worked on this task and stopped:
  interrupted after 165 calls; its last failing call, repeated <n> times, was …; its
  changes are in your tree.* The drive's workers never had this. A successor learns
  *why* its predecessor stopped, not only what it left. This is the communication the
  operator's guidance of 2026-09-27 pointed at.
- **Same agent or another:** `from:` works with any model. The record names the
  predecessor's model, so the lead can choose.

### 4.5 The default

`preserveDiscarded` stays **one key**; §10.3 rejected a key per mechanism, because
such keys can contradict each other. When it is unset, it resolves as follows:

| mechanism | unset means | why |
|---|---|---|
| interactive `undo` / `rewind` / `revert` | on (M338) | unchanged |
| the envelope's rollback | off (M336) | unchanged: it writes while a run is failing |
| **worktree teardown** (pool, attempt, improve, workflow, prune) | **on** | the parent is healthy, and the tree is about to be destroyed whatever happens |
| **orphan salvage** | **on** | the trees are already abandoned |
| **transcripts** | **on**, off with `--no-session` | the same exposure as a saved session |
| WIP commits (§4.7) | off | a cost in every child, and salvage covers the common case |

The proposal's §4.0 asked whether a feature's failure mode overlaps its trigger. For
teardown, the trigger is a child's end, and the failure mode is a disk write in the
healthy parent. The two overlap only when the disk is full, and §4.1 handles that case
by keeping the tree instead of writing. §10.1 asked whether the store has eviction. It
does: `checkpoints gc`'s removal half (M338) bounds the refs by age and count. **P1
must extend gc to the attempt files, and P3 to the orphan bases**, before either
default applies.

**The default waits for one measurement.** M338's 5 KB per kept state was measured on
a 40-file tree with about 10 KB of new content. A worker's state in a Zig project can
be larger. P1 measures the kept states of real batches after packing and records the
figure in its commit, as M338 did.

### 4.6 The disk guard (P4)

**What is measured.** Every filesystem a run writes to: the workspace, `~/.jichi.d`
(shadow repositories, worktrees, sessions and attempts) and `TMPDIR`, deduplicated by
device. Both blocks (`f_bavail`) and files (`f_favail`) are measured, because build
caches are many small files, and a filesystem can run out of inodes with gigabytes
still free.

**The measuring function returns a status, never a sentinel:** `JC_OK` with MiB and
files, or an error that means *unknown*. An unknown result **never stops a run**. It
is logged once and reported by `doctor`. The arithmetic moves to `double`, since C89
has no `long long`, and that also fixes the case of fragments under 1024 bytes (G5).

**When it is measured:**

1. **At run start.** Below the stop floor, the run refuses to start, so no model call
   is spent on a run that cannot write its records.
2. **At every tool boundary**, beside `jc_env_over_budget` (`src/chat/jc_agent.c:1459`).
   The sampled value goes into the envelope, so the budget check stays a pure function
   that a unit test can drive.
3. **Before `spawn_parallel` creates worktrees.** If N trees of the tracked tree's size,
   plus the warn floor, do not fit, the call is refused **as a tool-error value** that
   says how many would fit. The lead decides; the pool does not quietly run fewer.
4. **Before each preservation** (§4.1). Below the stop floor, the tree is kept rather
   than committed, and the record says so.

**Two floors per filesystem**, each in MiB and in files:

- **warn** (`resources.warnFreeMB`, `resources.warnFreeFiles`): a notice to the model
  at the next tool boundary, once per crossing, in the budget-notice idiom
  (`jc_env_budget_notice_due`). It names the filesystem, the free space, and the
  likely cause, such as build caches in worktrees.
- **stop** (`resources.stopFreeMB`, `resources.stopFreeFiles`): the run stops at the
  tool boundary and keeps its work. A child stops "where it stands" and keeps its
  work, which is what `env_budget_should_stop` already does at depth > 0. At depth 0
  the stop reason is a new budget name, `disk`. **The verifier is not run on this
  stop.** A build on a full disk goes red for a reason that is not the code's, and a
  red verdict the guard manufactured would be exactly the "plausible different answer"
  the caps rule warns about.

**The defaults follow `jc_resource_tier` and are provisional until measured:**

| tier | warn MiB | stop MiB |
|---|---|---|
| NORMAL | 4096 | 1024 |
| LITE | 1024 | 256 |
| MINIMAL | 256 | 64 |

The file floors are 50,000 to warn and 10,000 to stop. The stop floor is the reserve
jichi needs to finish cleanly: the session, the journal, the end records, and packing
the kept states. The warn floor is a guess at one more build. Both are guesses, so P4
starts by measuring them (P4a).

**A fence, not a cap.** The caps-versus-fences rule (CLAUDE.md) keeps caps off for
measurement runs, because a cap that fires changes the answer. The disk guard fires
only when the disk is about to be full. At that point the run would fail anyway, and
fail worse: with a truncated session and a damaged shadow repository. It is in the
class of `connect`, which "can only fire when nothing is listening" [M877: not so -- it
also fires on a slow TLS handshake; CLAUDE.md corrected], so it **stays on
in measurement runs**. Setting a floor to `0` turns it off, for an operator who
watches the disk some other way.

**`doctor`** prints one line per filesystem: free space, free files and the floors.
It warns below the warn floor, and it lists orphaned pool bases and trees that were
kept but not preserved (§4.1). A config that sets the floors on a platform where the
measurement is unknown gets the M326q treatment: `doctor` says the key does nothing
there.

### 4.7 WIP commits (P6, optional)

A child can commit its tree every N tool calls, to a ref in the shadow repository and
not to the worktree's HEAD. This bounds what a parent's death *and* a lost worktree
directory can take. It is **off by default**: it adds git work to every child every N
calls, and the orphan salvage already covers the common case.

It does **not** protect against a shell that deletes `~/.jichi.d`. The shadow
repository lives there too (ANECDOTES #108). Records that must survive that have to
leave the machine, which is a harness's job.

### 4.8 Invariants kept

- **Tool errors are values.** A refused `from:`, a batch the disk cannot hold, and a
  disk stop are all results that the model reads.
- **The agent does not branch on provider.** Nothing here touches a provider.
- **Preservation writes only to the shadow repository and `~/.jichi.d`**, never to
  the user's repository (the proposal's §8.4).
- **Files are 0600 and directories 0700.** A transcript is as sensitive as a session
  and is treated like one. The pool's worktree base is created 0755 today, and that
  changes with P3.
- **Config values are read leniently** (M168, M519): the floors with
  `jc_json_get_num_lenient`, and `preserveDiscarded` as it is read today.
- **Every destroyer names a reason.** The signature makes that true for the snapshot
  manager's callers, and the T5 lint makes it true for the rest.

## 5. Tests

Each test is shown to fail without its fix, and each check is perturbed on its own
(`TESTING_RUNBOOK.md`). The FAULT-gated tests join `make smoke-faults`, which
`smoke_lint` check 16 enforces.

| | step | the test |
|---|---|---|
| T1 | P1 | A mock-model batch of two write children. One answers; a FAULT-gated hook kills the other in the middle of its task. Afterwards `jichi attempts` lists one `parallel-interrupted` state, `recover` of it contains the killed child's edit, and the answered child's merge is unchanged. **Red:** revert the teardown's preservation, and the edit is gone. |
| T2 | P1 | A red `parallelVerify` gate leaves a `parallel-verify-red` state with the verifier's tail in its record. |
| T3 | P1 | For a conflict, the losing child's version of the conflicting file is in a `parallel-conflict` state. |
| T4 | P1 | When preservation fails (a FAULT write), the worktree is kept and reported, not removed. |
| T5 | P1 | A lint over the callers of `jc_snapshot_worktree_remove` and every `rm -rf` that can reach a worktree. The extracted set is printed and floored at today's: four calls and four `rm -rf` sites (G1). Enumerate it a second way too: every `rm` argv in `src/`, classified by hand once. |
| T6 | P2 | A child killed with SIGKILL leaves a transcript whose last line is its last completed event. With `--no-session`, no transcript is written. |
| T7 | P2 | Telemetry from two children running at once carries two distinct `task` values and each child's own model name. |
| T8 | P3 | A parent is killed during a batch. The next `spawn_parallel` salvages its trees and names them at the top of its result. |
| T9 | P3 | A base left by a dead run, whose pid now belongs to a live process, is still salvaged: the lock decides, not the pid. |
| T10 | P4 | A FAULT-gated override of the measured free space, a site that carries a value like M760's `LEASE_PAUSE_MS`. Below warn: one notice. Below stop: the run stops with `disk`, keeps its work, and does not run the verifier. Unknown: the run continues. |
| T11 | P4 | A unit test on the measurement's arithmetic, with a 512-byte fragment size and more than 2^32 free blocks. |
| T12 | P5 | A second batch resumes a stopped task with `from:`, and its result contains both agents' work. A `from:` whose change conflicts with the new base is refused as a tool error that names the files. |

## 6. Steps

| step | what | depends on | default |
|---|---|---|---|
| **P1** | preserve in `jc_snapshot_worktree_remove`; keep the tree when preservation fails; list kept states first in the pool's result; measure the packed cost | — | on |
| **P2** | end records, child transcripts, telemetry that names the child | P1 | on; off with `--no-session` |
| **P3** | bases named by run id, a lock for liveness, orphan salvage; `gc` covers attempts and bases | P1 | on |
| **P4a** | measure: free space and free files per tool call in full-tier telemetry, over one drive | — | — |
| **P4b** | the disk guard | P4a | on |
| **P5** | `attempts show`, and `from:` in `spawn_parallel` with the predecessor's record | P1, P2 | — |
| **P6** | WIP commits every N tool calls | P1 | off |

P1 and P4a are independent and can start at once. P4a is a telemetry field, not a
guard: measure the population before building the gate.

## 7. What this does not do

- **It does not unblock the lead.** A lead waiting in `spawn_parallel` still waits for
  its slowest child. The batch's synchronous shape, the lack of a channel to a running
  child, and a model call with no liveness bound are the subject of a companion
  analysis of the same drive. It is internal for now, and its public version will be a
  rewrite. This plan is a precondition for that work: a lead
  can only afford to abandon a child if abandoning it loses nothing.
- **It does not survive the loss of the machine or of `~/.jichi.d`** (§4.7).
- **It does not outlive its workspace.** Kept states live in the workspace's shadow
  repository. Once the workspace is gone, `checkpoints gc --yes` removes that
  repository and every kept state in it (M338's `gc_remove_repo`), as intended.
- **It does not decide what a kept state is worth.** Retention stays age plus count
  (the proposal's §10.4).

## 8. Rejected alternatives

- **Preserve at each call site.** There are four sites today, and a fifth whenever
  someone adds a mode. M616 already found the pool's trees outside its own sweep. A
  call site can be forgotten; a signature cannot.
- **Keep every worktree**, as M410's `--keep-worktree` does for one attempt. That is
  an unbounded store of full checkouts and their build caches, where a kept commit
  costs a delta.
- **One key per mechanism.** §10.3 rejected it: keys that can contradict each other.
- **Let a child preserve itself on SIGTERM.** The watchdog allows 300 ms, and a kill
  from outside may be SIGKILL. The parent can do the same work after reaping, with no
  deadline.
- **A disk floor as a percentage.** 10% of 1.8 TB is 180 GB, while 10% of a 16 GB card
  is 1.6 GB. The reserve jichi needs is an absolute amount.
- **Quietly reduce the pool's parallelism when the disk is short.** That changes what
  the lead asked for without telling it. A tool-error value states the numbers and
  leaves the choice to the lead.
- **Delete kept trees to free space at the stop floor.** That trades the only copy of
  work for disk space. The operator can recover from a full disk; nobody can recover
  lost work.

## 9. What would make this wrong

- **Large kept states from real batches**, such as build outputs escaping
  `.gitignore`: then the default waits until the per-file cap has been measured. P1's
  measurement decides.
- **A linked worktree of a shadow repository behaving differently** under
  `commit-tree`, since `core.worktree` is set in the common config. M272 already found
  one git-version difference here: `worktree remove` does not exist before git 2.17.
  If commits misbehave in the same way, P1 preserves with `git --git-dir … --work-tree
  <wt>` instead. Test on the oldest git row first.
- **Salvage at the start of a batch being slow** on a large tree. Then it moves to
  `gc` only and is reported by `doctor`.
- **The floors firing in normal work on a small board.** That would mean they were set
  before P4a measured them.

## 10. Open questions

1. **Should the disk stop get its own outcome**, or be `budget_exhausted` with the
   budget name `disk`? The budget name reuses the whole stop path. Its own outcome
   would keep a dashboard that reads "budget exhausted" as "a cap fired" from
   misreading a fence. *Recommendation:* the budget name, with the fence's own words in
   the disposition: `stopped: disk low (work kept)`.
2. **Should `from:` also apply to `jichi attempt` and workflow stages?** Later, once
   the pool's version has been used.
3. **Should the transcript be the child's session proper**, a `.json` rewritten each
   turn? JSONL survives a kill. A rewritten JSON survives only if it is replaced
   atomically at every event, which costs a full rewrite per event.

## 11. Related

- [`../proposals/2026-08-work-preservation.md`](../proposals/2026-08-work-preservation.md):
  M335–M338, the line this extends.
- M410 and M616: the attempt's kept worktree, and the worktree sweep.
- M431e and M760: the run lease, and the lock that dies with its holder.
- M117 and M326q: `memBudgetMb`, a resource guard that refuses where it cannot work.
- M144: `parallelVerify`, the per-child gate whose quarantine this makes real.

## 12. After review (2026-09-27)

The other machine reviewed this plan against the tree and against how other systems solve the
problem: LangGraph and Temporal checkpoint at every step, write-ahead logs and Kafka's committed
offsets, idempotent replay, and OTP supervision
([`../analysis/2026-09-27-preserve-agent-work-review.md`](../analysis/2026-09-27-preserve-agent-work-review.md)).
It re-verified the code claims in §2 and §3. Its verdict was "endorse, ship P1 as written". The
changes adopted, with the review's numbering:

1. **The transcript is the record of reasoning (R1).** Files are already durable at every step:
   each edit lands in the worktree, and P1 and P3 keep that tree. What is lost between steps is
   the reasoning. So P2's transcript is what a successor reads:
   - P3's salvage builds the end record from the transcript's tail when the parent died, since no
     teardown ran to write one;
   - P5's `from:` resumes from the tree **and** the transcript.

   P6 stays optional.
2. **A resume states the effects outside the tree (R2, adjusted).** The predecessor's record
   lists the actions whose effects persist outside the worktree: writes outside it, the network,
   git refs, installs. They are classified by effect and given as information. The successor
   re-runs builds, tests and gates, which a blanket "do not repeat them" would forbid.
3. **The transcript is born redacted (R3).** A tool result is whatever a command printed, and a
   command can print a registered secret. The review's premise about jichi's existing sinks was
   measured on 2026-09-27 and does not hold for every sink that stores tool results. That is a
   separate security fix, tracked privately until it lands, as `SECURITY.md` orders. The
   transcript must not add another such sink.
4. **The disk probe is throttled by time (R4, adjusted).** At most one sample about every 10 s at
   tool boundaries, plus unthrottled checks at run start, before a batch allocates worktrees, and
   before each preservation. The throttle is not by call count, because one build can write
   gigabytes in a single call.
5. **Salvage is idempotent and manifest-first (R5).** A salvaged base is marked, or deduplicated
   by its tree's sha, and `batch.json` is written before the worktrees exist.
6. **Loops are surfaced by the detector jichi already has (R6, adjusted).** Its tool-loop
   detector warns a child after 3 identical failing calls or 4 of the same class. It missed the
   drive's loop because the arguments varied, the outcome classes varied, and a pipe into `head`
   reported success.
   - The fix normalises its key (strip `timeout N`, `cd X &&`, environment assignments and
     trailing filters) and adds an "ended at its own bound" class.
   - The end record's `repeated` line reads the same counter, so there is not a second one.
7. **The order changes.**
   1. The telemetry-attribution fix comes first. It is independent, and P4a measures from
      telemetry.
   2. Then P1 alongside P4a.
   3. Then P2.
   4. Then P3, once G3 has been reproduced.
   5. Then P4b and P5.
   6. P6 only if measured.

**Tests added from the review:**
- a transcript whose tool output holds a canary secret stores it redacted;
- a `from:` resume does not repeat an effect outside the tree;
- two salvages of one base give one ref;
- T11 runs on the illumos and FreeBSD rigs, not only on synthetic fragment sizes.

## 13. The companion finding: a busy lead cannot be reached, and a dead child is not bounded

The operator's second request of 2026-09-27 was that workers should not block the lead. This plan
does not do that (§7). Three offline experiments, using jichi's mock model only on the drive's
build, measured why:

- **A lead cannot be reached during any long tool call, not only during a delegation.**
  - With quick tool calls (the positive control), `status` was answered in 0–1 s.
  - Inside one 20 s shell call, and inside a 20 s `spawn_parallel` batch, it was answered after
    15 s, at the next boundary.
  - The control socket is served only between tool calls and at round boundaries (M159, M438).
- **With caps off, nothing bounds a dead child.**
  - A child whose model stream received its headers and then nothing held the batch past a 45 s
    outer deadline.
  - With a 10 s stall timeout the same run ended by itself in 16–17 s.
- **The number a dead-stream bound needs.** Across the drive's 12,464 successful model calls, the
  slowest took 540 s. A zero-bytes window of 900 s would have fired on none of them. It would have
  ended the drive's dead call, which lasted 11,677 s, after 15 minutes.

Two small milestones follow, and they can come before P1 or alongside it:
1. Serve `status`, `pause` and `abort` poll-only inside long tool calls: the shell's wait loop and
   the pool's `select` loop. An inject still waits for the round boundary.
2. Add a dead-stream bound that stays on in measurement runs.

The second changes the caps-versus-fences rule, which lists the stall timeout as a cap. A window
far above the healthy tail can fire only when nothing is arriving, which is the property the rule
grants `connect`. Reclassifying it is the operator's decision.
