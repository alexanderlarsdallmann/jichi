# The harness-port drive in numbers

*A data sheet for planning: how long a self-driving jichi team took, what it produced, how often the gate
said no and why. Every number on this page was read from the drive's own records. None is an estimate.
The [lessons](README.md) and the [platform retests](PLATFORM_RETESTS.md) pages build on it.*

---

## 0. What the drive was, and how to read this page

From 2026-11-05 to 2026-11-15 a team of jichi agents worked on jichi itself. No person wrote its code.

- **The lead** is a 27B model (`jlu/qwen3.8-27b`). It planned each milestone, wrote its behaviour notes,
  dispatched workers, reviewed their diffs, kept a log and marked a batch ready.
- **The workers** are mostly a coding model (`jlu/qwen3-coder-next`). Each one ran in its own worktree and
  returned a diff.
- **The host gate** is a script outside the team. It ran the full `make ci` on every head the lead marked
  `BATCH <name> READY` and fast-forwarded master only when it was green.
- **The supervisor** is a second agent outside the drive. It checked in every hour and answered by notes.

The team's main task was to port the drive's own host scripts into jichi's C harness (`jichi-harness`, see
[`HARNESS.md`](../../HARNESS.md)). The rest of the milestones were platform fixes, two releases (0.14.0
and 0.14.1) and their records. Together that is milestones M1144 to M1192.

**The cutoff.** The page counts up to the end of run 0059, on 2026-11-15 at 20:58. Master was then
`bbaf6a7b` (M1192). The drive went on after that.

**The machines.**
- One desktop workstation ran the loop's container, every worker's tool calls, the gate and every build:
  12 cores / 24 threads, 32 GB of RAM, one NVMe disk, Linux.
- The models ran remotely, on the institution's free `jlu/*` gateway. No call was priced, so the money
  cost is zero. The time cost is on this page.

**How to read a number.** "Median" is the middle value; half the cases took longer. "p90" means nine in ten
cases took this long or less. Plan with the p90, not the median: the slow tenth is what makes a schedule
slip. Section 8 says how each number was produced and how to reproduce it.

## 1. At a glance

| | |
|---|---|
| Wall-clock span | 237.9 h (9.9 days), 2026-11-05 23:04 to 2026-11-15 20:58 |
| Runs of the lead | 59: 46 worked, 13 died at start (one cause, section 2) |
| Hours the lead ran | 229.4 h; the loop was down 8.2 h in 7 gaps |
| Milestones landed | 49 (M1144 to M1192) |
| Commits landed on master | 480, in 17 gated batches |
| Lines landed | +121,834 / -660 in 632 files (section 3 splits them) |
| Gate attempts | 32: 17 green and landed, 15 red |
| Model calls | 21,305: 94.8 M tokens in, 32.1 M out |
| Pool workers | 203, together 149.9 worker-hours |
| Failed model calls | 66 (0.31 %); one model outage of 31 min |
| Supervisor notes delivered into a live run | 174 |

## 2. Time

**Runs.** The loop starts a fresh lead run whenever the previous one ends. A run ends when the lead stops,
usually because its context is nearly full. Before it stops, it commits its state and writes a handoff for
the next run.

| working runs (46) | hours |
|---|---|
| shortest | 0.26 |
| one in four took at most | 2.18 |
| median | 4.08 |
| three in four took at most | 7.57 |
| longest | 20.10 |
| total | 229.4 |

**The 13 runs that died at start.** Each of them ended after 10 s with exit status 127, an empty stream and
an empty error log: 12 on 2026-11-07 between 07:04 and 07:38, and one on 2026-11-12.
- The cause was one defect. The harness passed the whole prompt as one command-line argument. Linux refuses
  any single argument of 131,072 bytes or more, and the spawning child then exits 127 without a word.
- The prompt embedded every note in the inbox, and delivered notes were not moved out. So each retry built
  the same oversized prompt.
- The cost was 44 minutes of a stopped drive on 2026-11-07, from the last good run's end at 07:04 to the
  next good start at 07:48, and 20 seconds on 2026-11-12. No work was lost.
- The lesson for a planner: a fault that kills a run before its first model call leaves no telemetry. Count
  runs from the loop's own log, not from the telemetry.

**Where the lead's 229.4 hours went**, from the lead's own tool and model calls:

| the lead was | hours | share |
|---|---|---|
| waiting for pool workers (`spawn_parallel`) | 118.4 | 52 % |
| in its own model calls | 62.8 | 27 % |
| running shell commands (builds, tests, git) | 28.8 | 13 % |
| waiting for in-process reviewers (`spawn_subagent`) | 18.8 | 8 % |
| everything else | 0.6 | 0 % |

Half the lead's time was waiting. A dispatch blocks the lead until every task in it returns, so the slowest
worker sets the pace (section 5).

**Pauses.** The loop was down for 8.2 h in 7 gaps of more than 5 minutes:
- three holds by the operator, 7.6 h in all:
  - 2.7 h on 2026-11-12 after H9b landed, while the platforms were retested on the new master;
  - 1.4 h on 2026-11-13 while the operator decided the 0.14.0 release order;
  - 3.5 h on 2026-11-13 while the release batch went through the gate;
- the four 10-minute back-offs of the 2026-11-07 failure above.

The loop was started again 7 times:
- 3 times onto a newly landed build of the harness, between two runs, in under a minute each;
- 3 times to end the holds above;
- once after the 2026-11-10 alarm in section 5.

## 3. Work landed

**480 commits** reached master in 17 gated batches, carrying **49 milestones** (M1144 to M1192).
- All 480 name the operator as author and committer.
- 475 also name the model that wrote them, in the `Jichi-Model` trailer: 345 the 27B model, 130 the coding
  model.
- Five lack that trailer. Two are the supervisor's own internal-docs commits (the drive's plan and a docs
  handover), which no model wrote. Three are the lead's: the gate caught two (section 4), and the operator
  accepted the third by name before its gate.

| area | files | lines added | lines removed |
|---|---|---|---|
| `src/harness/` (the port itself) | 29 | 18,899 | 112 |
| the rest of `src/` | 16 | 1,877 | 134 |
| `include/` | 10 | 659 | 9 |
| `tests/` | 64 | 13,588 | 174 |
| public docs (`docs/` outside `internal/`) | 73 | 17,774 | 141 |
| `docs/internal/` (the drive's log, notes, briefs, reports) | 428 | 68,252 | 0 |
| build and project files (`Makefile`, `scripts/`, README, CHANGELOG, ...) | 12 | 785 | 90 |

Of the lines added, 56 % are the team's own records in `docs/internal/`. The drive writes down what it
measured and decided, every run. For every line of harness code it wrote 3.6 lines of record and 0.7 lines
of test.

**Commits per day:**

| 11-05 | 11-06 | 11-07 | 11-08 | 11-09 | 11-10 | 11-11 | 11-12 | 11-13 | 11-14 | 11-15 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 41 | 64 | 39 | 57 | 41 | 38 | 51 | 54 | 68 | 26 |

The count is by author date, so a commit counts on the day it was written, not the day it landed. 11-15
stops at the cutoff.

**The smoke tier grew** from 544 drivers and 3,282 checks (the first landing) to 560 drivers and 3,547
checks (M1192), as each green gate counted them.

## 4. The gate

**32 attempts.** 17 were green and landed; 15 were red.

| | minutes per attempt |
|---|---|
| a green attempt (all of `make ci` plus a pre-gate sweep) | median 59.6, from 53.9 to 77.8 |
| a red attempt (it stops at the first failing stage) | median 10.8 |

So **a batch costs at least one hour of gate time.** Batching several milestones per gate amortises that
hour. It also means a red stage costs a whole new hour once the fix is in.

**Batches.** 17 batches landed. One release batch went to the gate as REL and landed as REL2, with REL's head
inside it; this page counts the two as one batch. 9 of the 17 were green at their first attempt.

| batch | attempts, in order |
|---|---|
| H6 | red (sweep), landed |
| H7 | red (ci), red (ci), landed |
| H8 | red (ci), red (sweep), landed |
| H9a | red (ledger), red (ledger), red (ci), landed |
| H9b | red (sweep), red (ci), landed |
| PLAT | red (ci), landed |
| REL, then REL2 | red (ledger), red (ci), red (ci), landed as REL2 |
| REL3 | red (sweep), landed |
| the other 9 | landed first time |

The first ten batches, up to REL3 on 2026-11-14, needed 25 attempts, 2.5 each. The seven after them landed
at their first attempt.

From its first attempt to its landing, a batch took a median of 1.0 h. The longest was H7, at 12.9 h.

**Why the gate said no.** Each red was read against both logs:

| cause | reds | what it was |
|---|---|---|
| a defect in the batch | 8 | a sanitizer leak; four compiler errors that only one stage's flags showed (fortified `-O2` twice, C++, clang); a test that needed a git identity the container had and the host did not; two docs checks (a milestone cited before its ROADMAP entry existed, a driver count one behind) |
| a commit record | 3 | two commits without their trailers; one red twice, before the operator accepted it by name |
| the host changed | 2 | a toolchain upgrade (zig 0.16 to 0.17.1) with a missing library; a cross-compiler install that removed the 32-bit libraries |
| a flaky test already in the tree | 1 | a memory limit of 448 KB that sat inside its own measurement's spread |
| a false verdict of the gate's own script | 1 | the sweep script accepted only one spelling of a summary line; the supervisor withdrew the verdict |

Eight of the fifteen reds were defects in the batch. In the other seven the batch's code was fine: something
around it was wrong. A team that treats every red as "my code is broken" will waste
those hours. A team that treats every red as "the gate is flaky" will ship the eight.

**Second opinions.** After 26 gate events a 27B reviewer read the batch in a fresh context and gave each
milestone a verdict: HOLDS, WEAK or WRONG. They are advisory; the lead had to check a WEAK or WRONG by
effect. The [lessons](README.md) page tells which of them were right.

## 5. Models and workers

| role | model calls | tool calls | tokens in | tokens out |
|---|---|---|---|---|
| the lead | 6,938 | 7,701 | 38.1 M | 9.0 M |
| 203 pool workers | 13,194 | 15,004 | 50.1 M | 19.9 M |
| in-process reviewers and helpers (54 dispatches) | 1,173 | 2,140 | 6.6 M | 3.2 M |

**The lead's model calls** took a median of 14.7 s and a p90 of 74.1 s. The first token arrived after a
median of 2.1 s. So nearly all of a call's time comes after the first token: the model writing its reply,
reasoning included, not the server making it wait.

**The workers.**
- 203 workers in total: 180 on the coding model, 23 on the 27B model.
- A worker took a median of 29 minutes, with a p90 of 96 minutes. The longest took 6.5 hours.
- 174 dispatches. Of the 140 whose output printed a task count, 98 sent one task, 28 two, 6 three, 6 four,
  1 five and 1 six.
- Of the 199 endings the dispatch outputs printed, 195 were `done`. Three hit their iteration limit and one
  was aborted.

**Failures and retries.** 66 of 21,305 model calls failed (0.31 %):

| failure | calls |
|---|---|
| an HTTP error status | 30 |
| the connection failed | 21 |
| the provider returned an error | 13 |
| a timeout | 2 |

jichi retried 65 times. Once, failures lasted long enough to raise the alarm.
- On 2026-11-10 at 10:39, the page watcher saw model calls failing for 31 minutes and set the drive to stop
  after its current run.
- The run's own retry recovered, and the run ended normally at 12:05.
- The loop resumed 89 seconds later. No run was lost.

**Context.** The lead, the workers and the reviewers compacted their context 510 times, eliding old tool
output to stay inside the model's window.

## 6. Supervision

- **174 supervisor notes** reached a live lead as injects. Most pointed to a longer note in the drive's
  inbox: a correction, an answer to a question, a measurement, an order the operator gave.
- **52 host-gate notes** (greens, reds and reviews) reached it the same way. A note that found no live run
  waited in the inbox for the next one.
- Over the 9.9 days that is about 18 supervisor notes a day. Supervising a self-driving team is not
  "start it and come back next week". The [lessons](README.md) page says what the notes were for.

## 7. Planning with these numbers

These are one drive's numbers, on one machine, with two models. Use them as a first estimate for a similar
team, and replace them with your own as soon as you have a week of records.

| plan for | from this drive |
|---|---|
| one gated batch | 1 hour of gate time when green; budget 2 attempts until the team and the gate have settled |
| one worker task | 30 minutes typical; 1.5 hours for one task in ten |
| one lead run | 4 hours typical; up to 20 |
| a milestone | 4.7 lead-hours and 3.1 worker-hours on average (229.4 h and 149.9 h over 49 milestones) |
| supervision | an hourly check-in, about 18 notes a day |
| the unexpected | 7 of 15 reds were not the batch's code; one failure mode killed 12 runs in a row before it was seen |

## 8. How these numbers were produced

- **Sources.** Every number comes from:
  - the drive loop's log (`harness.log`);
  - each run's `telemetry.jsonl` and `journal.jsonl`;
  - the host gate's state file and digest;
  - git history between master `9ada7cd7` (the drive's start) and `bbaf6a7b`.
- **Reproducing them.** One script prints every count on this page except the git counts and the
  classification of the reds. The script, the exact commands, the host's details and the per-red evidence
  are in the internal appendix (`docs/internal/2026-11-15-harness-port-case-study/README.md`, kept out of
  public snapshots).
- **Two definitions decide the counts.**
  - A pool worker is one `task_run` in a run's telemetry. This page counts 203; the 2026-11-11 worker
    analysis counted 245 because it also counted in-process subagents as workers.
  - A run's hours come from the loop's start and end lines, not from the first and last telemetry event.
- **Not measured here.**
  - The time a person spent: the operator's decisions are in the supervisor's log, not timed.
  - The model server's load: a slow call may be a busy server or a long answer.
  - What the reds cost in hours of the lead's own work.

## Update, 2026-11-22: tokens and totals to date

*Added by the supervisor on 2026-11-22. This sheet's numbers above end at run 0059 (2026-11-15 20:58). The
drive is still running; the full update comes when it ends. Until then, these are the whole drive's totals
from its start to 2026-11-22 17:22:29 +0100, counted from the runs' telemetry and from git.*

| | runs 0001-0059 (above) | whole drive to 2026-11-22 17:22 |
|---|---|---|
| Wall clock | 237.9 h | 402.3 h (16.76 days) |
| Runs of the lead | 59 | 96 |
| Landings on master | 17 | 30 |
| Commits landed | 480 | 852 |
| Model calls | 21,305 | 37,167 |
| Input tokens, newly processed | 94.8 M | 163.9 M |
| Input tokens served from the prompt cache | not given above | 3,731 M |
| Output tokens | 32.1 M | 53.0 M |

**What the two input numbers mean.** Every model call sends the whole conversation again. The model gateway
serves most of it from its prompt cache and processes only the new part. jichi records both, in `in_tok` and
`cache_read_in`. The figure this sheet called "tokens in" is the new part only. On one run of this drive (88
calls), the two together matched the whole prompt, and the new part was a median 2 % of it.

**Who used them, whole drive to date:**

| | new input | output |
|---|---|---|
| The lead's own calls | 66.8 M | 15.3 M |
| The workers' and helpers' calls | 97.1 M | 37.7 M |

The workers wrote more than twice the lead's output.

**Not counted above, now counted:** replies cut at the output limit of 32,768 tokens. By run 0059, 21 calls had
been cut; to 2026-11-22, 36 calls in 27 workers. A cut reply is lost work: the worker must write the same
change again in smaller pieces.

**The drive so far, as a timeline:**

![A timeline of the harness-port drive from 2026-11-05 to 2026-11-22 in four lanes: the lead's runs as bars, each gate run as a green dot or a red cross, each landing on master as a triangle, and the spans the loop was down (pauses on the operator's word, restarts onto new builds, one model-outage park).](../charts/timeline-harness-port.svg)

*Every mark is a row in [the data tables](../data/).*

Across all four jichi drives, see [the overview](../JICHI_DRIVES.md).
