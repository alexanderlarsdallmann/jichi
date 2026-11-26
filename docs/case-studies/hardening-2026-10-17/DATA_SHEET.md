# The first hardening drive in numbers

*A data sheet for planning: how long the first jichi-on-jichi hardening drive took, what it produced, how
often the gate said no and why. Every number on this page was read from the drive's own records and counted
twice, by two different routes. None is an estimate unless the line says so. The [lessons](README.md) page
builds on it.*

*All times are local time, UTC+2, unless a line says otherwise.*

---

## 0. What the drive was, and how to read this page

From 2026-10-18 to 2026-10-23 a team of jichi agents worked on jichi itself. Its task was to fix the
findings of jichi's own hardening review of 2026-10-17. No person wrote its code.

- **The lead** is a 27B model running inside jichi. It planned each fix, dispatched workers, checked their
  results, kept a log and declared a batch ready.
- **The workers** are a coding model running inside jichi, at most six at a time, each in its own
  worktree.
- **The gate** is the full `make ci`, run on the workstation outside the team's container. At first the
  supervisor ran it by hand on each batch the lead declared ready. From 10-20 a timer, the follower, also
  gated every head the team pushed.
- **The supervisor** is a second agent outside the drive. It checked in every 20 to 30 minutes and answered
  with notes.

**The span.** The page counts from the start of run 0001 (10-18 04:07:12) to the end of run 0076 (10-23
20:05:30). Master was then `0bcece87`. The drive was paused there, and its work was taken up 3.5 days later
by a new drive.

**The machines.**
- One development workstation ran the loop, every lead container, every tool call, the gate and every
  build. Each lead container was limited to 16 CPUs, 24 GiB of memory and 16,384 processes.
- The models ran remotely, on the model gateway, at no charge. The money cost is zero; the time cost is on
  this page.

**How to read a number.** "Median" is the middle value: half the cases took longer. "p90" means nine in ten
cases took this long or less; it is the nearest-rank value. Plan with the p90, not the median: the slow tenth
is what makes a schedule slip. Section 8 says how each number was produced.

## 1. At a glance

| | |
|---|---|
| Wall clock | 135 h 58 min (5.67 days), 2026-10-18 04:07:12 to 2026-10-23 20:05:30 |
| Runs of the lead | 76: 60 ended normally, 15 with an error (14 of them in the gateway outage), 1 aborted |
| Hours the lead ran | 132.7 h; the loop was stopped 2 h 51 min, in 2 stops |
| Master moves | 11 landings of gated batch heads |
| Commits landed on master | 516 (511 without merges) |
| Lines landed | +45,763 / −1,059 in 319 files (section 3 splits them) |
| Milestone numbers in commit subjects | 82, from M960 to M1047 |
| Review findings fixed | 31 of 111; all 15 rated high |
| Gate runs | 143: 57 green, 86 red or invalid |
| Gates on READY heads | 24: 14 green, 9 red, 1 invalid |
| Model calls | 36,923; 19.8 million output tokens |
| Failed model calls | 276 (0.75 %); one outage of 4 h 01 min |
| Pool worker tasks | 247 |
| Notes that reached a live lead | 246, all the supervisor's |

## 2. Time

**Runs.** The loop starts a fresh lead run whenever the previous one ends. A run ends when the lead stops,
and it leaves a handoff for the next run.

| all 76 runs | duration |
|---|---|
| shortest (run 0028, an idle run during a hold) | 1 min 28 s |
| median | 1 h 15 min 37 s |
| p90 | 4 h 20 min 03 s |
| longest (run 0069) | 7 h 59 min 54 s |
| total | 132 h 42 min 42 s |

**How the runs ended.**
- 60 ended normally (exit status 0).
- 15 ended with an error (exit status 1). Run 0040's end-of-run check failed. Runs 0047 to 0060 lost the
  model gateway.
- 1 was aborted (exit status 130): run 0043, by the supervisor, after a worker hung twice.

**Runs started per day.** A run counts on the day it started.

| 10-18 | 10-19 | 10-20 | 10-21 | 10-22 | 10-23 |
|---|---|---|---|---|---|
| 9 | 24 | 11 | 18 | 8 | 6 |

10-19 has the most runs because of the hold: 17 short runs only confirmed it (below).

**Where the lead's run hours went**, from the lead's own model and tool calls:

| the lead was | hours | share |
|---|---|---|
| in its own model calls that succeeded | 51.8 | 39 % |
| waiting for pool workers (`spawn_parallel`) | 46.8 | 35 % |
| running shell commands: builds, tests, git | 16.9 | 13 % |
| waiting for in-process reviewers and helpers (`spawn_subagent`) | 13.5 | 10 % |
| in model calls that failed | 3.2 | 2 % |
| in other tools, and between calls | 0.5 | 0 % |

A dispatch blocks the lead until every task in it returns. So the slowest task sets the pace, and the lead
cannot commit or read a note meanwhile.

**Stops, holds and the outage.**

| what | when | time |
|---|---|---|
| the loop stopped for the operator's review | 10-19 23:06 to 10-20 00:15 | 1 h 08 min 44 s |
| the loop stopped while the history was rewritten | 10-20 09:32 to 11:14 | 1 h 41 min 58 s |
| runs that only confirmed a hold (runs 0017-0033, 17 runs) | 10-19 21:29 to 23:06 | 1 h 30 min 39 s of run time |
| no model reachable (last good call to first good call) | 10-21 16:55:20 to 20:56:44 | 4 h 01 min 24 s |
| of it, runs that died without a single good call (runs 0048-0060, 13 runs) | 10-21 17:39 to 20:47 | 3 h 04 min 38 s of run time |

The loop was never parked: parking came on 10-21 at 21:14, and no outage happened after it.

**Builds the team ran.** The team ran a pinned jichi binary. It used 10 different builds over the 76 runs;
from the restart on 10-20 the pin followed master. The build of the last landing was made but never used by
a run of this drive.

**Before and after.**
- The container image was built on 10-17 at 09:34. Two rehearsals of the harness ran on 10-17 at 13:54 and
  on 10-18 at 03:32; each printed 6 checks ok and none failed.
- The next drive took up the work 84 h 17 min (3.51 days) after the loop ended, on 10-27 at 07:22 (UTC+1).

## 3. Work landed

**516 commits** reached master in 11 landings. All of them were fast-forwards.

| landing | content | commits | master moved |
|---|---|---|---|
| batches 0 and 1 | the open items of the project's own jichi agent; data loss (4 findings); the spool race fix | 131 | 10-19 10:57 |
| batch 2 | fences that did not hold (3 milestones) | 15 | 10-19 13:31 |
| batch 3 | process state and the verifier (3 milestones) | 23 | 10-19 22:09 |
| batch 4 | repository-controlled paths and project input; a new gate stage; fixes for a reviewer's findings | 128 | 10-21 08:17 |
| batch 5 | memory safety (5 findings); a lint fixed; a merge of master | 32 | 10-21 16:28 |
| batch 6 | secrets (4 milestones) and follow-ups | 39 | 10-22 07:23 |
| batch 13 | model outages (2 milestones) and batch 6's residuals | 60 | 10-22 15:53 |
| batch 13, residuals | three residual cards | 33 | 10-22 23:33 |
| batch 14a | a working-directory option and a secret-environment setting | 13 | 10-23 03:31 |
| batch 14c+d | worker reports as task results; the delivery state of a note | 21 | 10-23 07:56 |
| batch 14e | liveness at every depth; the dirty-tree guard; three design notes | 21 | 10-23 about 19:57 |

On 10-20 the history was rewritten to fix the commit identity, and master was pushed again with new commit
ids. The rewrite changed no content apart from one test baseline string.

**What the lines are.**

| area | files | lines added | lines removed |
|---|---|---|---|
| `src/` | 53 | 5,262 | 652 |
| `include/` | 22 | 595 | 33 |
| `tests/` | 92 | 12,808 | 190 |
| public docs (`docs/` outside `internal/`) | 42 | 3,656 | 103 |
| `docs/internal/` (the drive's log, notes, reports, plans) | 101 | 23,304 | 72 |
| build and project files | 9 | 138 | 9 |
| **total** | **319** | **45,763** | **1,059** |

- 51 % of the lines added are records in `docs/internal/`. 1,466 of those came from two commits another
  session pushed to master during the drive.
- For every line of code (`src/` and `include/`), the drive added 2.2 lines of test.

**Who wrote the commits.** After the rewrite, all 516 carry one author and committer identity, the operator's. A commit
trailer names the model that wrote it:
- 337 name the 27B model, 142 the coding model, and 32 name no model;
- 11 carry the supervisor's co-author line.

The trailer is the lead's statement, not a measurement (section 5).

**Milestones.** 82 distinct milestone numbers from M960 to M1047 appear in commit subjects. Six numbers in
that range appear in none: M964, M971, M972, M973, M979 and M1004. M1048 was registered on the last evening
and not built.

**Review findings.** The findings index has 111 rows: 15 high, 39 medium, 46 low and 11 notes. 31 are marked
fixed by a milestone of this drive: all 15 high, 14 medium and 2 low.

**Kept, not landed.** 47 WIP snapshots in 21 run directories hold uncommitted work, copied out every 30
minutes. Rejected attempts were kept as a patch file, a snapshot, or a git reference.

## 4. The gate

**143 gate runs** started during the drive: 126 by the follower and 17 by the supervisor. 57 were green; 86
were red, or invalid.

| | minutes per gate |
|---|---|
| a green gate (all of `make ci`) | median 33 min 38 s; p90 34 min 37 s; longest 36 min 52 s |
| a red gate (it stops at the first failing stage) | median 16 min 08 s |
| all 143 together | 57 h 25 min of gate time |

So **a batch costs at least half an hour of gate time**, and a red stage costs a new half hour once the fix
is in.

**The follower** gated every newly pushed head, not only READY heads. Up to the loop's end its table holds 126
rows: 49 green, 76 red and 1 refused. Most of its reds were on heads the lead had not declared ready.

**Gates on READY heads.** 24 gates ran on heads the lead declared ready, or on the merge that landed:

| landing | gates, in order |
|---|---|
| batch 0 | red (a leak in a test), red (a crash behind a test guard), green; not landed alone |
| batches 0 and 1 | red (the race), red (stale documentation counts), red (a test the mutant sweep could not reach), green |
| batch 2 | green |
| batch 3 | invalid (the supervisor's script), red (a leak in a test), red (a sleep used as synchronisation, under valgrind), green |
| batch 4 | green |
| batch 5 | red (two documentation lints), green, green (the merge) |
| batch 14c+d | red (a documentation lint), green |
| batch 6 | green (the READY head, bed4ad99), green (the head that landed, b4d5a526) |
| batches 13, 13 residuals, 14a, 14e | green |

- 14 green, 9 red, 1 invalid.
- In 7 of the 11 landings the head that became master was green at its first gate.
- The first landings were the hardest. From batch 0's first gate (10-18 23:36) to the green gate that landed
  batches 0 and 1 (10-19 10:54) took 11 h 18 min, over 7 gates. Batch 3 took 2 h 33 min over 4 gate runs.

**From a READY head's commit to master** took a median of 55 min, from 41 min 34 s (batch 13) to 3 h 13 min
18 s (batch 6, where the operator's go came in the morning).

**Why the gate said no.**

The follower's 76 reds, by the `make` target that failed:

| failing target | reds |
|---|---|
| the smoke tier | 54 |
| compiling one object file | 12 |
| the unit test target | 6 |
| the top-level `ci` target (an early log format) | 2 |
| the mutant sweep | 1 |
| the fault-injection tier | 1 |

The supervisor's 10 red or invalid gates, each read by hand:

| head's batch | cause |
|---|---|
| batch 0 | a memory leak in a test case; the READY check had run no sanitizer |
| batch 0 | a crash: a test guard written as a statement did not stop the test |
| batch 1 | the spool race, in batch 0's code |
| batches 0 and 1 | stale counts in the documentation |
| batches 0 and 1 | a test that set its own binary path, so the mutant sweep could not reach it |
| batch 3 | invalid: the supervisor's gate script ran with an empty `TMPDIR` |
| batch 3 | a leak in the test the supervisor had asked for |
| batch 3 | under valgrind, a test that used a 10 ms sleep as synchronisation |
| the rewritten master | interference: two smoke tiers at once, and a test that scanned every process |
| batch 5 | two documentation lints |

**Lints, not code.** In the red gates' smoke tiers, 90 failing drivers were named. 78 of them were lint or
documentation checks: the code built and its tests passed, but a record, a count or a rule check was out of
date. The most frequent were the documentation-count lint (16), the smoke tier's own lint (11), the
milestone-currency lint (9) and the snapshot lint (8).

## 5. Models and workers

| depth | who | model calls | tool calls |
|---|---|---|---|
| 0 | the lead | 9,275 | 10,039 |
| 1 | pool workers and in-process helpers | 27,579 | 29,633 |
| 2 | helpers of helpers | 69 | 136 |
| | **total** | **36,923** | **39,808** |

**Tokens.** The models wrote 19.8 million output tokens. jichi's telemetry counts input in two fields: 112.0
million tokens not served from a cache, and 3,611 million read from the gateway's prompt cache. This page does
not add the two.
The lead's own calls account for 33.7 million of the new input and 8.8 million of the output; the workers' for
78.3 million and 11.0 million. Across the four jichi drives, see [the overview](../JICHI_DRIVES.md).

**The lead's model calls** that succeeded took a median of 10.2 s and a p90 of 47.7 s. The first token arrived
after a median of 2.0 s. So most of a call's time is the model writing its answer, reasoning included.

**The workers.**
- 167 dispatches (`spawn_parallel`) started 247 pool worker tasks; 56 calls of `spawn_subagent` ran
  in-process reviewers and helpers.
- A pool worker's time, from its first to its last telemetry row: median 9.3 min, p90 30.3 min, longest
  130.2 min; 58.5 h in all.
- **How busy were the six slots?** Count, for each minute of each run, the pool workers and in-process
  helpers that made a call in that minute. Over all 76 runs they used **7.7 %** of the six slots' minutes;
  pool workers alone used 6.6 %. In the first three runs the figures were 8 %, 28 % and 19 %.

**Which model served which call.** Not reliable from the records. The telemetry gives a failed call and an
in-process helper's call the lead's model label, whoever made it. The commit trailers (section 3) are the
lead's statements, not measurements.

**Failures.** 276 of 36,923 model calls failed (0.75 %). 169 of them were in the 13 runs that died in the
outage; those runs made no tool call at all.

## 6. Supervision

- **The supervisor's log** has 7,503 lines in 847 entries. 553 entries are check-ins: about 98 a day over
  5.67 days. 60 entry headings name the operator, and 18 are corrections.
- **Notes into a live run.** 246 notes reached a running lead as injects, all of them the supervisor's: answers,
  corrections, gate verdicts, the operator's decisions. One more control command aborted run 0043.
- **Notes into the next run.** 117 inbox notes were folded into a run's prompt.
- **Gates.** The supervisor ran 17 gates itself: 7 green, 10 red or invalid.
- **The watch** raised 148 alerts up to the loop's end. The largest groups: 61 when the lead edited one file
  itself three or more times, 22 red gates, 21 dispatch results with dropped or kept-back work, 20 dispatches
  of a shape the watch flags, and 15 for uncommitted work older than 30 minutes.
- **The reviewer.** From 10-20 at 02:21 a read-only reviewer ran 74 times over new commits (67 ended normally,
  7 with an error). Its findings were claims; the supervisor checked each before passing it on.
- **The register.** The drive's decision register holds 19 decisions: 15 by the operator, 2 proposed by the
  supervisor and approved by the operator, 2 by the supervisor.

Supervising this drive was not "start it and come back on Friday". Over 5.67 days it took about 98 check-ins
and 43 notes into a live run per day.

## 7. Planning with these numbers

These are one drive's numbers, on one workstation, with two models. Use them as a first estimate for a
similar team, and replace them with your own as soon as you have a week of records.

| plan for | from this drive |
|---|---|
| one gated batch | 34 min of gate time when green; the first batches needed several gates (7 for batches 0 and 1), later batches mostly one |
| a READY batch to master | about an hour; three hours when the go waits for a person |
| one lead run | 1 h 16 min typical; 4 h 20 min for one run in ten |
| one worker task | 9 min typical; 30 min for one in ten |
| per milestone | 1.6 lead-hours on average (132.7 h over 82 milestone numbers); the milestones differed widely |
| six worker slots | about one-thirteenth used (7.7 %): the lead's waiting and checking set the pace |
| supervision | a check-in every 20 to 30 minutes; about 43 notes a day |
| the unexpected | a 4-hour outage; a 1 h 42 min stop for a history rewrite; 2 h 39 min of hold for a review; a decision lost for three days |
| an estimate | the second day's estimate was "batches 4 to 9 in about 36 hours"; batches 4 to 6 alone took 55 h 08 min |

## 8. How these numbers were produced

- **Sources.**
  - each run's metadata, telemetry and journal, opened one by name (a run's directory holds named pipes);
  - the harness's log (loop starts, ends and stops);
  - the gate logs' start-and-end files and the follower's results table;
  - the watch's alert log and the reviewer's results table;
  - the supervisor's log;
  - git history from the start head (`3aeee47b`, the rewritten id of `185733ea`) to `0bcece87`;
  - the drive's records in `docs/internal/drive/2026-10-17-hardening/`, the lessons ledger and the findings
    index (kept out of public snapshots).
- **Counted twice.** A first script built a fact base. A second set of commands counted every number again by
  a different route: shell tools, `awk` and `git` where the first used Python. Both, and their outputs, are kept
  on the development workstation beside the drive's records. Where the two disagreed, the original record
  decided.
- **Definitions that decide the counts.**
  - A run's hours come from its metadata's start and end lines.
  - The outage window runs from the last successful model call before it to the first one after it, both read
    from telemetry. (The supervisor's log named a later call, 20:56:59, as the first; telemetry has a
    successful retry at 20:56:44.)
  - A pool worker is one `task_run` in a run's telemetry.
  - A gate run counts when it started between the first run's start and the last run's end.
- **Not measured here.**
  - The operator's time: the records hold the operator's words and their times, not hours.
  - Which model served each worker call (section 5).
  - Which check failed in run 0040's end-of-run verification: the journal records only its exit status.
  - Why the gateway refused connections.
  - The time lost to dispatches from a dirty tree over the whole drive: only single cases (30 to 50 minutes)
    were measured.
  - Host load and disk use over the drive: only point readings exist.
