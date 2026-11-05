# A queue of tasks for several agents: the protocol, and what each step relies on

*Written at M765. Section 10 of the twelve-questions plan asked for the queue to be written down
as a protocol, from what [`examples/autonomous-loop/`](../examples/autonomous-loop/) already
does, with the POSIX rule each step relies on. It also asked for an experiment to measure the
queue. The protocol is below. The experiment is designed in the last section and has not run.*

## Why a protocol, and not a feature

[`AGENT_COLLABORATION.md`](AGENT_COLLABORATION.md) decided that agents cooperate through files,
not through a negotiation protocol or a shared memory. The operator's decision on the plan
kept that stance: fix what is broken, run the experiment, and treat its numbers as evidence,
not as a verdict. A convention nobody has written down cannot be checked, so this page writes
it down. It names the property each step needs, the system rule that provides the property,
and the places where today's loop does not keep it.

## The objects

| Object | What it is | Where it lives |
|---|---|---|
| a **task** | a file whose contents are the prompt | `pending/<name>.task` |
| a **claim** | the task, renamed into `running/` with the claimer's pid in its name | `running/<name>.task.<pid>` |
| a **result** | the run's `--output json`, and its exit code, which routes the task | `<name>.task.result.json`; the task moves to `done/`, `failed/` or back to `pending/` |
| a **heartbeat** | a file whose modification time the worker keeps fresh | *proposed*, not built: `running/<name>.task.<pid>.beat` |
| the **lease** | one run per workspace, recorded in `~/.jichi.d/leases/` | [`AUTONOMY.md`](AUTONOMY.md); taken under a lock since M760 |
| the **journal** | the run's own record, one JSON event per line | `~/.jichi.d/runs/<run>.jsonl`, or `--journal` |

## Each step, and the rule it relies on

**1. Claiming a task needs exactly one winner.** The claim is a `rename(2)` of
`pending/<name>.task` to `running/<name>.task.<pid>`. When several supervisors rename the same
file, the first rename moves it. Every later one fails with `ENOENT`, because the source is no
longer there. That is the whole mechanism: no lock file, no coordinator.
- **It holds within one filesystem only.** Across filesystems `rename(2)` fails with `EXDEV`,
  and the C supervisor then simply does not claim. That is safe. `mv`, which `loop.sh` uses,
  does something else: it falls back to copying and then deleting. Two supervisors can both
  copy before either deletes, so both claim the task. `pending/` and `running/` must be on one
  filesystem, and a supervisor should use `rename(2)` itself, or refuse to start when the two
  directories are on different devices.
- A network filesystem is where this gets hard.
  [`AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md)'s fourth topology already says what to do
  across machines: push the assignment over ssh, and do not share a directory.

**2. Creating something only if it is absent** (a marker, a lock) is `open(2)` with
`O_CREAT|O_EXCL`. On a local filesystem, exactly one caller creates the file and the rest get
`EEXIST`. On old NFS (version 2) the server could not guarantee that. The traditional NFS-safe
form is to create a file with a unique name and `link(2)` it to the shared name. Then check that
the unique file's link count is 2, even when `link` reported an error, because the reply may
have been lost after the link succeeded. jichi's own two shared files do not use either form.
The lease (M760) and the board (M761) serialise their read-and-write under an `fcntl` lock,
which the system drops when its holder dies. Where the filesystem has no record locks (some NFS
mounts answer `ENOLCK`), jichi goes ahead without the lock and logs that at debug level.

**3. A result must be read whole or not at all.** Write it under a temporary name in the same
directory, then rename it into place: a reader sees the old file or the new one, never half of
one. jichi's own sinks do this (`jc_write_file_atomic`, M146). `loop.sh` writes the result with
`cp`, which is not atomic, so a reader that arrives mid-copy reads a truncated JSON.

**4. A heartbeat must mean "still working".** The worker refreshes a file's mtime every *N*
seconds; a supervisor treats a claim whose heartbeat is older than a few *N* as stale.
- Two cautions. Across machines the two clocks differ, so compare a heartbeat's successive
  mtimes rather than one mtime against the reader's clock.
- Some filesystems keep mtimes to 2 s, so *N* must be well above that.
- Neither supervisor has a heartbeat today.

**5. A claim whose claimer died must come back.** A supervisor killed mid-run leaves
`running/<name>.task.<pid>` behind, and neither supervisor ever returns it to `pending/`.
- On one host the pid in the name says whether the claimer lives. `kill(pid, 0)` failing with
  `ESRCH` means it does not; `EPERM` means it does, as the lease already reads it.
- A pid can be reused, so a live pid proves nothing on its own. The heartbeat's age is what
  settles it.
- Across hosts a pid means nothing, and only the heartbeat can.
- One measured caution, from GNU/Hurd (M767): there, `kill(pid, 0)` answered *exists* for
  tens of seconds after the process was killed through its process group. A recovery rule should not
  act on one reading.

**6. One run per workspace.** Several agents editing one tree is the hazard the lease exists
for: the out-of-scope guard cannot tell one run's edits from another's. Give each agent its own
tree, a `git worktree` of the same repository, as
[`AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md) says, or serialise them with `--lease fail`. That
flag is a reliable serialiser since M760: before it, two runs started together could both take
the lease.

**7. The journal is the record.** Each run appends to its own journal, with one writer. A
supervisor reads the journal after the run, and `jichi runs` summarises many. `jichi ps` (M759)
answers the question a supervisor asks during the run: which runs are alive, and where their
control sockets are.

## What the two supervisors do today (read 2026-09-25)

| Step | `loop.sh` | `jichi-supervisor.c` |
|---|---|---|
| claim | `mv` into `running/`: atomic within one filesystem, **a silent copy across two** | `rename(2)`: atomic, and refuses across filesystems |
| result | `cp` of `--output json`: **not atomic** | the same output, routed by exit code |
| routing | by exit code: 0 done; 2 failed; 130 or 143 requeued; anything else retried, then failed | the same |
| heartbeat | **none** | **none** |
| stale claims | **never recovered** | **never recovered** |
| lease | not passed: the default warns and proceeds | the same |

The four bold cells are the gaps. None of them has shown up yet, because nothing has run several
supervisors against one queue for long. That is what the experiment is for.

## The experiment (plan section 10, step 3): the design

**The question.** When N headless agents work one queue, what goes wrong, and how often? And
would a built-in queue inside jichi prevent it, or only move it? The decision this feeds is
between a built-in queue (`--loop`, deferred in [`AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md)) and
jichi staying a good citizen of external orchestration, as today.

**The arms**, on one queue of tasks from one project:

| Arm | What | What it isolates |
|---|---|---|
| A | one supervisor | the baseline: what one agent does with the tasks |
| B | three supervisors, one host, one workspace each | contention on the queue alone |
| C | as B, with one supervisor killed (`SIGKILL`) mid-run, twice | stale claims and recovery |
| D | as B, with a person watching (`jichi ps`, the control socket) and allowed to intervene | what supervision adds, and what it costs the person |

**The counts**, taken from the queue directories, the journals and the branches, not from the
models' reports:
- duplicate work: two runs of one task;
- claims left in `running/` after the arm ended;
- conflicts when the agents' branches are merged;
- the moments a person had to step in, each with its reason.

**The conditions**, each from a lesson this project paid for:
- **The model's shell must not reach the record** (M744, ANECDOTES #108). Each agent runs as
  another user or in a container. Each run's journal and result are copied off the machine as
  the run ends.
- **Fences on, caps off** (CLAUDE.md, *caps versus fences*). Edit scopes and read-only agents
  stay on. Deadlines and token budgets stay off, because a cap that fires manufactures a
  different result. `connect` stays on.
- **Local models only**, named per run. A model that `doctor --live` does not call *native*
  executes nothing, and that looks exactly like a queue failure (*Infra failure looks like a
  capability verdict*).

**The readings.** The operator asked for more than one reading of the result, and the numbers
are read at least three ways before any decision:
1. the counts above, as they fall;
2. the transcripts of every failure, read for its cause (the queue, the model, or the task);
3. a second reader's account, from someone who did not run the arms: the operator, or an agent
   on the other machine.

A decision that holds only under one reading is not taken.

**What would change the decision.** If the failures are the four gaps above, the protocol is
fixed where it lives, in the supervisors, and jichi stays outside the queue. If the failures
need knowledge only jichi has, such as its lease, its journal and its control socket together,
that is the argument for a built-in queue. It would have to be made with these numbers.

**Not yet decided, for the operator:** the section's own question, *is the zigodot project the
right testbed?* The arms need a project with a dozen independent tasks that can each be
verified without a person. And the sandbox needs a second user account or a container runtime
on the machine that runs it.
