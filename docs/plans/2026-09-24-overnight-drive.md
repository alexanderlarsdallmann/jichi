# The overnight drive, 2026-09-24: two new behaviours in the field, and delegation on real work

*A plan, written on threadwork before the drive, so the reading afterwards is held to
questions asked in advance. The drive runs `scripts/corpus-drive.sh` (M735) against
zigodot; its output lives in `~/.cache/jichi-corpus/2026-09-24-zigodot/`, outside every
repository. The analysis page that reads it is written after, and cites this one.*

## 0. Why this drive, and why tonight

- **D1's note (M733) and D2's advice (M734) were decided on corpora, not observed.** The
  note fires at the third identical successful call; the replay says it would have fired
  in four futile turns of the 2026-09-23 drive and in none of the other 44. Whether a
  model *stops* when told is a question only a run on the new build can answer.
  Likewise, M734 made inferred constraints advisory on M730's offline measurement; how
  often an inferred rule meets the work is now journalled (`constraint_advisory`), and
  nothing has been journalled yet.
- **A before/after on the same work.** zigodot's `master` is still `70801ac`, the base of
  the 2026-09-23 drive, so the same 24 tasks with the same two configs, on the M734 build
  instead of M714, isolate what the new build changes.
- **Delegation has no corpus on real work.** `spawn_subagent` and `spawn_parallel` are
  tested offline and have been driven on toy tasks; no drive has asked a model to
  delegate a real edit and then measured what came back. The operator asked for exactly
  this on 2026-09-24: *"many jichi agents in various modes (with subagents, and parallel
  agents) that work on the zigodot project (possibly on different tasks in their own
  branches)"*.

## 1. The questions, and the number that answers each

| # | question | the number | where it comes from |
|---|---|---|---|
| Q1 | When the note fires, does the repeat stop? | per `no_progress` event, the identical calls made **after** it in the same turn (0 = the model stopped) | journal + `full`-tier telemetry, `tests/measure/noprogress_replay.py` for the calls |
| Q2 | Does the note fire on a productive turn? | every noted turn, **read**, not counted -- the M731 method | the runs' streams |
| Q3 | Is the replay faithful to the build? | the journal's `no_progress` events against the replay's notes, over the same telemetry | both; a disagreement means the replay's hand-copied table drifted |
| Q4 | How often does an inferred rule meet the work? | `constraint_advisory` events per run; and whether the final answer says so when a run acts against one | journal, streams |
| Q5 | What does delegation do on real work? | per subagent/parallel run: delegations made, their outcomes (M437's delegation report), tokens, and whether the parent used the answer | journal, telemetry, streams |
| Q6 | The before/after | the 2026-09-23 arms against tonight's `qwen` and `bonsai` on the same 24 tasks: stops, capped runs, unchanged-repeat maxima, verifier results | both drives' runs.tsv and journals |
| Q7 | DEFERRED item 7 | capped one-shots, and their `answer_bytes` | journals |

Q1 and Q4 are the reason the drive runs tonight. Q6 is only meaningful because the base
and the configs are held fixed.

## 2. Arms

Every arm runs the same 24 tasks (`tasks/full.list` of 2026-09-23: 14 read, 10 edit),
from `70801ac`, one headless `--auto` turn each.

| arm | model | mode | endpoint | why |
|---|---|---|---|---|
| `qwen` | `jlu/qwen3-coder-next` | single | HRZ | the before/after (Q6): the 2026-09-23 config but for one field, its embedder (§3) |
| `bonsai` | `prism-ml/bonsai-27b` | single | LM Studio, loopback | the before/after, and the model whose four loops the note was fitted on |
| `qwen-ref` | `jlu/qwen3-coder-next` | single, with references | HRZ | the twin of the two below; against `qwen`, what the references change |
| `qwen-sub` | `jlu/qwen3-coder-next` | subagent | HRZ | Q5 |
| `qwen-par` | `jlu/qwen3-coder-next` | parallel | HRZ | Q5; `maxParallelAgents: 3` |
| `reason` | `jlu/qwen3.8-27b` | single, with references | HRZ | the operator's reasoning pick, not yet driven |
| `reason-sub` | `jlu/qwen3.8-27b` | subagent | HRZ | Q5 |
| `reason-par` | `jlu/qwen3.8-27b` | parallel | HRZ | Q5; `maxParallelAgents: 3` |
| `bonsai-sub` | `prism-ml/bonsai-27b` | subagent | LM Studio | Q5 on a local model, if the night allows -- its twin is `bonsai`, which has no references, and that difference is stated |
| `gemma`, `gptoss` | `jlu/gemma-4-26b-it`, `jlu/gpt-oss-20b` | single, with references | HRZ | optional, the other free chat models, if time remains |

**References and modes are preambles**, prepended by `--mode` to every task's prompt and
nothing else, so an arm differs from its twin by exactly one paragraph:

- `_mode-ref.md`: where the reference material is, read-only -- the Godot sources, its
  documentation and demo projects, and Zig 0.16.0's standard library -- because zigodot's
  `AGENTS.md` names a location that does not exist on threadwork, and a model following it
  would search a path the fence refuses. The configs of these arms list the same
  directories as `referenceRoots`.
- `_mode-sub.md`: the reference paragraph, then: delegate self-contained investigations
  or changes to `spawn_subagent` with a precise brief, check each result, and give the
  final answer yourself.
- `_mode-par.md`: the reference paragraph, then: where the task has independent parts,
  run them at once with `spawn_parallel`, integrate and verify; if it has none, say so and
  do it directly.

The exact texts are the files in the drive's `tasks/`, and each run's manifest names
the one it used.

## 3. Lanes: who may use which server at once

- **LM Studio has one user at a time** -- the M731 lesson, where a probe of mine on the
  same server spoiled a run. Its lane runs `bonsai`, then `bonsai-sub`, one task at a
  time, and nothing else on threadwork calls it: the platform rows' live turns finish
  before the drive starts.
- **The embedder, corrected before launch (14:25).** This section said the two
  before/after configs kept 2026-09-23's embedder, LM Studio's. All nine configs did,
  and a first `codebase_search` in a workspace embeds the whole workspace -- measured on
  the seed clone, 2097 chunks -- so any HRZ lane that searched would have put two
  thousand requests on LM Studio in the middle of a bonsai run. The seven HRZ configs
  now name **`jlu/qwen3-embedding`** on HRZ (in the free listing re-read at 14:18; a full
  index of the seed took 75 s, 4096 dimensions), and only the bonsai lane's two configs
  keep LM Studio's. The cost: `qwen` differs from 2026-09-23's config by that field, so a
  before/after difference in a run that called `codebase_search` -- once in 48 runs on
  2026-09-23 -- may be the embedder's. No lane had built an index yet, and the cache
  rebuilds on a model change.
- **HRZ: at most three agents at a time**, each lane in its own workspace clone:
  - lane 1: `qwen`, `qwen-ref`, `qwen-sub`, `qwen-par`;
  - lane 2: `reason`, `reason-sub`, `reason-par`;
  - lane 3 (optional): `gemma`, `gptoss`.
  A parallel arm adds at most three children, so the gateway sees at most about seven
  concurrent requests from this drive.
- **Timing, from 2026-09-23:** qwen took 77 s a task, bonsai 535 s. Lane 1 is about
  3 hours, lane 2 an estimate of 4, the LM Studio lane 7-8.

## 4. Fences, caps and branches

- **Fences on, caps off**, as on 2026-09-23: `--edit-scope src/** --edit-scope tests/**`,
  `--revert-out-of-scope`, the path fence, `--lease fail`; no budget and no deadline;
  `"timeouts": {"stall": 0, "request": 0}`; `maxToolIters: 200`; the outer timeout of
  3600 s is housekeeping (SIGINT, then SIGKILL), and every kill is in runs.tsv.
- **References, read-only,** for every arm but the two before/after arms: `referenceRoots`
  names the Godot sources, documentation and demo projects and the Zig standard library,
  so a model can read what zigodot mirrors without the path fence refusing it. Nothing
  outside the workspace is writable.
- **Free models only.** `corpus-drive.sh` refuses a config naming a model that is
  neither loopback nor in `jlu/`, before any request; the `jlu/` listing is re-read at
  launch.
- **Branches.** A run that leaves changes is committed on
  `drive/2026-09-24/<task>/<arm>`, from `70801ac`, one commit whose message carries the
  run's final answer, and pushed to zigodot's own repository -- **only with the operator's
  go**. Without it the branches wait in the lanes' workspaces and are pushed later, the
  same plain way, if the go comes. Never `master`, never a force-push, never a delete; a name that
  exists already fails and is recorded. About ten edit tasks times the arms run: on the
  order of a hundred branches if every edit leaves a diff. The token comes from
  the token file the operator provided, through a credential helper on the one push command.
- **The operator's checkout is never touched** (it holds an uncommitted
  `.jichi/lessons.draft.md`); every workspace is a fresh clone.

## 4b. The operator's decisions, 2026-09-24 afternoon

Start at **19:00**; all three HRZ lanes, the optional one included; the branches **pushed as
each run finishes**; the commits carry the name *jichi corpus drive (threadwork)* and the
operator's address, so the forge attributes them to the operator while they read as the
drive's.

## 5. Supervision

The drive runs in the background on threadwork. While this session is alive it checks
the lanes about every half hour -- runs.tsv, the servers' errors, LM Studio's health --
and stops a lane only for an **infrastructure** failure (a gateway answering 5xx to
every call, LM Studio gone), never for a task's own failure: a failed task is data. A
stopped lane and why is recorded in the drive's `SUPERVISION.md` and in the analysis.

## 6. What would spoil it, and what is done about each

| hazard | guard |
|---|---|
| a second user on LM Studio | the platform rows' live turns end first; no probe of mine touches it during the drive; the HRZ arms embed on HRZ (§3) |
| zigodot's `master` moving | the base is pinned (`--base 70801ac`); the handoff asks the other machine to say before pushing `master` |
| a build changing under a run | the binary is pinned with `scripts/pin-driver.sh` from the gated commit |
| a priced model | refused by the script before the first request |
| a branch name reused | a plain push fails and is recorded; nothing is overwritten |
| reading the numbers before the questions | this page, committed before launch |
