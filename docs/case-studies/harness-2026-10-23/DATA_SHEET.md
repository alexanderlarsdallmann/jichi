# The harness drive in numbers

*A data sheet for planning: how long a jichi team took to build `jichi-harness`, what it produced, how often
the gate said no and why. Every number on this page was read from the drive's own records, and every count
and duration was computed twice, by two scripts written separately. None is an estimate. The
[lessons](README.md) page builds on it.*

---

## 0. What the drive was, and how to read this page

From 2026-10-23 to 2026-10-27 a team of jichi agents worked on jichi itself. No person wrote its code.

- **The lead** was a 27B model. It planned each milestone, wrote its design note, dispatched workers,
  reviewed their diffs, kept the team's log and marked a batch ready.
- **The workers** were configured to use a coding model. Each ran in its own git worktree and returned a
  diff. From each task's second model call on, they ran on the lead's model (section 5).
- **The CI follower** ran the full `make ci` on every head the team pushed, every 10 minutes when there was a
  new one.
- **The supervisor**, a second agent outside the team, checked in every 20 minutes, ran the gate on each READY
  head, and moved master to it as a fast-forward when it was green.
- **The independent reviewer**, the 27B model in a fresh context, reviewed each new range of commits.

The team built phase P2 of the harness plan: `jichi-harness`, jichi's third C89 program, in batches H0 to H5,
and, right after H0, one fix to jichi itself (batch G18). That is milestones M1048 to M1059.

**The span.** The page counts from the loop's start, 2026-10-23 20:16:55 +0200, to its end by a STOP file,
2026-10-27 05:37:25 +0100. Master moved from `0bcece87` to `561e91b4`. The clocks moved from +0200 to +0100
at 03:00 on 2026-10-25, during the drive; "Z" means UTC.

**The machines.**
- One desktop workstation ran the loop's containers, every worker's tool calls, the gate and every build.
- The models ran remotely, on the institution's free model gateway, reached through a VPN. No call was priced,
  so the money cost is zero. The time cost is on this page.

**How to read a number.** "Median" is the middle value: half the cases took longer. "p25", "p75" and "p90"
mean that one in four, three in four, or nine in ten cases took this long or less (nearest rank). Plan with
the p90, not the median: the slow tenth is what makes a schedule slip.

## 1. At a glance

| | |
|---|---|
| Wall-clock span | 82.3 h (3.43 days) |
| Runs of the lead | 20: 19 ended normally, 1 ended on used-up retries during a gateway outage |
| Hours inside runs | 78.8 h (95.7 % of the span) |
| Batches landed | 7: H0, G18, H1, H2, H3, H4, H5 |
| Milestones landed | 12 (M1048 to M1059) |
| Commits landed on master | 179 |
| Lines landed | +39,565 / −118 in 169 files |
| `make ci` verdicts | 88: 40 green, 48 red |
| Model calls of the team | 8,883: 38.4 M tokens in (and 897.4 M read from the cache), 12.3 M out |
| Worker tasks | 88, together 45.2 worker-hours |
| Independent reviews | 65 |
| Supervisor log entries | 303, of them 221 check-ins |
| Defects found after the drive ended | 4: three in this drive's code (two at its first real use, one on another architecture) and one in jichi that changed the workers' model |

## 2. Time

**The loop's span:**

| span | length | share |
|---|---|---|
| inside the 20 runs | 78 h 48 min 06 s | 95.7 % |
| run 0014 parked: the gateway refused new connections | 3 h 22 min 02 s | 4.1 % |
| between runs 0015 and 0016: two pushes failed while the VPN was down | 4 min 43 s | 0.1 % |
| all other gaps between runs, and the loop's last 17 s | 5 min 39 s | 0.1 % |

**Runs.** The loop starts a fresh run of the lead whenever the previous one ends. A run ends when the lead
ends it, or when it fails. Each new run starts with a handoff built from the previous run's records.

| runs (20) | hours:minutes |
|---|---|
| shortest | 0:04 |
| one in four took at most | 1:19 |
| median | 2:20 |
| three in four took at most | 4:29 |
| nine in ten took at most | 9:41 |
| longest (run 0015) | 15:24 |
| total | 78:48 |

| run | start | length | ended | what it did |
|---|---|---|---|---|
| 0001 | 10-23 20:16 +0200 | 2:31 | normally | H0 (M1049, M1050) |
| 0002 | 10-23 22:48 +0200 | 6:50 | normally | H0 follow-ups; G18 (M1048) |
| 0003 | 10-24 05:38 +0200 | 1:53 | normally | H1: the configuration loader (M1051) |
| 0004 | 10-24 07:31 +0200 | 2:06 | normally | H1: the secret refusals |
| 0005 | 10-24 09:37 +0200 | 3:05 | normally | H1: `doctor` (M1052) |
| 0006 | 10-24 12:43 +0200 | 3:42 | normally | H2: `init` (M1053) |
| 0007 | 10-24 16:25 +0200 | 2:09 | normally | H2: the review fixes, redone |
| 0008 | 10-24 18:35 +0200 | 1:19 | normally | H3: the design note (M1054) |
| 0009 | 10-24 19:54 +0200 | 4:56 | normally | H3: `mirror fetch` (M1055) |
| 0010 | 10-25 00:51 +0200 | 11:09 | normally | H3: `mirror push` and the credential helper (M1056) |
| 0011 | 10-25 11:00 +0100 | 1:47 | normally | a missing test, the glossary |
| 0012 | 10-25 12:47 +0100 | 4:29 | normally | a boundary test stage (def-350) |
| 0013 | 10-25 17:17 +0100 | 4:15 | used-up retries | H4: `run` (M1057) |
| 0014 | 10-26 00:55 +0100 | 1:12 | normally | H4: `run`'s records |
| 0015 | 10-26 02:06 +0100 | 15:24 | normally | H4: the handoff (M1058) |
| 0016 | 10-26 17:35 +0100 | 9:41 | normally | H5: `drive` (M1059) |
| 0017 | 10-27 03:16 +0100 | 0:06 | normally | nothing; committed nothing |
| 0018 | 10-27 03:22 +0100 | 2:03 | normally | two follow-ups |
| 0019 | 10-27 05:26 +0100 | 0:07 | normally | nothing; committed nothing |
| 0020 | 10-27 05:33 +0100 | 0:04 | normally | nothing; committed nothing |

**Where the lead's 78.8 hours went**, from its own model calls and tool calls:

| the lead was | hours | share |
|---|---|---|
| waiting for its pool workers (`spawn_parallel`) | 41.4 | 53 % |
| in its own model calls | 18.8 | 24 % |
| waiting for in-process reviewers (`spawn_subagent`) | 9.6 | 12 % |
| running shell commands (builds, tests, git) | 8.9 | 11 % |
| in all other tools | 0.0 | 0 % |

The lead's calls add up to 78.7 h. A dispatch blocks the lead until every task in it returns, so the slowest
worker sets the pace.

**Outages.**
- **The VPN, 2026-10-24**, 08:29:55 to 08:49:54 +0200: it went down with the desktop session. No model and no
  git server for 20 minutes; 2 pushes failed; 1 gate was refused at setup and retried.
- **The model gateway, 2026-10-25/26**: new connections refused from about 20:40 +0100. The lead's last
  successful call was at 19:50:04Z and the next at 23:55:08Z: 4 h 05 min without a model. 26 calls of run
  0013 failed between 20:05:19Z and 20:32:42Z. The gateway watch reported it down from 20:55:33 to 00:50:32
  +0100 (3 h 55 min).
- **The VPN, 2026-10-26**, from about 14:47 to about 17:56 +0100: 17 pushes failed between 14:50:10 and
  17:57:21; the CI follower refused 12 gates at setup between 15:42:52 and 17:32:42. The model gateway still
  answered, and the team kept working.

**After the loop.** The work did not end with the loop:

| when | what |
|---|---|
| 2026-10-27 | First real use: two defects in this drive's code found and fixed the same day |
| 2026-10-28 | The three code follow-ups carried over by the next drive's lead |
| 2026-11-13 | A missing NULL in an argument list found on an aarch64 board |
| 2026-11-20 | The defect that put the workers on the lead's model found |

## 3. Work landed

**179 commits** reached master in 7 gated batches, carrying **12 milestones**.

| batch | milestones | commits | files | lines | from the previous landing |
|---|---|---|---|---|---|
| H0 | M1049 the third program builds; M1050 its learner's page | 8 | 56 | +5,348 / −50 | 3 h 39 min (from the loop's start) |
| G18 | M1048 no "verify green" when the verifier never ran | 16 | 33 | +2,430 / −88 | 6 h 02 min |
| H1 | M1051 the configuration; M1052 `doctor` | 30 | 31 | +4,286 / −74 | 7 h 11 min |
| H2 | M1053 `init` | 23 | 28 | +3,948 / −60 | 5 h 38 min |
| H3 | M1054 a design note; M1055 `mirror fetch`; M1056 `mirror push` and a credential helper | 37 | 40 | +10,066 / −22 | 17 h 40 min |
| H4 | M1057 `run`; M1058 the handoff | 48 | 49 | +8,972 / −96 | 30 h 52 min |
| H5 | M1059 `drive`; a fix to M1058 | 17 | 29 | +4,837 / −50 | 9 h 55 min |
| **all** | 12 | **179** | **169** | **+39,565 / −118** | 80 h 56 min |

The run table in section 2 shows which run built what. The lines of a batch count every file the batch
changed, records included. The last landing came 80 h 56 min after the loop started; the last 1 h 25 min of
the loop were follow-ups and idle runs.

**The harness's own files at the end** (`561e91b4`):

| what | files | lines |
|---|---|---|
| the program, `src/harness/` | 10 | 10,708 |
| its headers, `include/jc_harness_*` | 9 | 712 |
| its smoke drivers, `tests/smoke/harness_*.sh` | 8 | 4,752 |
| its unit tests, `tests/test_harness*.c` | 2 | 990 |
| its learner's page, `docs/HARNESS.md` | 1 | 392 |

The smoke tier gained 9 drivers (from 492 to 501 shell files in `tests/smoke/`); the gate on H5's head ran
499 drivers with 3,012 checks. The register of known gaps gained 28 rows (def-333 to def-360), each a decision
not built or a check not written yet.

**Who is named on the commits.**
- All 179 name the operator as author and committer.
- 170 carry the `Co-Authored-By` trailer for jichi. The nine without it: six commits of H3, all made in run
  0010, which carry no trailer at all; two earlier slips in runs 0001 and 0002; and the supervisor's own plan
  commit.
- 170 carry a `Jichi-Model` trailer: 106 name the 27B model, 64 the coding model. These are labels of what was
  configured, not of what answered (section 5).

**Commits per day** (by author date, UTC):

| 10-23 | 10-24 | 10-25 | 10-26 | 10-27 |
|---|---|---|---|---|
| 16 | 79 | 47 | 30 | 7 |

**Not landed by this drive:** 6 commits after `561e91b4` (three of code, three of the team's log; 8 files,
+532 / −21). They had no READY line, and their gate was red on one lint. The next drive's lead carried the three
code commits over on 2026-10-28; they are on master.

**Not done:** the plan's exit test for P2, a rehearsal beside the old script with the same records at the end.
It needs podman, which the container did not have.

## 4. The gate

**The CI follower** ran `make ci` on every head the team pushed: 99 results on 89 heads.

| result | count |
|---|---|
| green | 37 |
| red | 48 |
| refused at setup: the git server could not be reached, `make ci` did not run | 13 (1 on 2026-10-24, 12 on 2026-10-26) |
| stopped by the supervisor: the red was already measured by hand | 1 |

**The supervisor's own gates:** one stopped after 16 min 18 s (the first READY head, red), and three green:
the H0 landing, the H4 landing (offline, during the VPN outage) and the H5 landing.

So **`make ci` ran to a verdict 88 times: 40 green, 48 red.**

| | minutes per gate |
|---|---|
| a gate that ran its stages (83 gates over 10 min) | median 34.7; p90 37.0; longest 45.3 |
| a green gate (40) | median 35.0 |
| a gate that stopped at an early stage (5) | 0.4 to 4.3 |

Host gate time on red gates: 25 h 13 min. The team did not wait for it, but the host did.

**From the first READY line to the landing:**

| batch | first READY line (UTC) | landed (UTC) | READY to landed |
|---|---|---|---|
| H0 | 10-23 20:44 (named a red head) | 10-23 21:55 | 1 h 11 min |
| G18 | 10-24 03:36 | 10-24 03:57 | 21 min |
| H1 | 10-24 09:39 (written before the tier finished) | 10-24 11:08 | 1 h 28 min |
| H2 | 10-24 14:23 (held: a known segfault) | 10-24 16:46 | 2 h 23 min |
| H3 | 10-25 09:57 | 10-25 10:26 | 29 min |
| H4 | 10-26 16:27 | 10-26 17:17 | 51 min |
| H5 | 10-27 02:07 | 10-27 03:12 | 1 h 05 min |

Median: 1 h 05 min. Every landing was a fast-forward after a green `make ci` on exactly the landed head. The
landing times are the times the pinned jichi binary was rebuilt after each landing; H3 has no such record, and
its time comes from the supervisor's log (to the minute).

**Why the gate said no.** Each red was read from the gate's log:

| cause | reds | gate time | what it was |
|---|---|---|---|
| only lints failed | 23 | 13 h 14 min | counts in docs pages one behind, a translated page with an old number, a milestone cited before its roadmap entry, non-portable shell in a test |
| a milestone's own new driver was red | 15 | 8 h 57 min | 8 by design (red-first, code not yet there); 3 a fixture or driver defect; 2 red for the wrong reason; 2 killed at 120 s |
| another driver failed | 3 | 1 h 43 min | a real regression from the G18 fix; two gates with seven drivers red, among them a process count the new command changed |
| a stage outside the smoke tier | 7 | 1 h 19 min | a unit test crash; a red-first unit test; the mutant sweep twice; the C++ build twice; a new stage's guard (a `make clean` gap) |

The unit test crashed only with an unwritable temporary directory. The mutant sweep did not cover the new
program. Each lint runs in seconds by hand. Many lint reds came from the record step: a new driver changes a
count in several pages, in English and in Japanese.

The drivers that appeared most often in red gates: `docs_counts_lint` 15, `posix_utils_lint` 7,
`i18n_tracks_lint` 7, `harness_run` 7, `portability_lint` 6, `milestone_currency_lint` 6, `docs_flags` 6.

**The independent reviews.** 65 reviews, one per new range of commits.

| | |
|---|---|
| length | median 55 min; p90 1 h 46 min; longest 2 h 51 min |
| summaries | 63 (two reviews ended without one) |
| what the 63 summaries add up to | 766 checks read, 56 vacuous checks, 101 wrong claims, 19 lost behaviours, 57 unknown |
| model calls | 6,291 (37 failed): 15.2 M tokens in, 9.1 M out |

The reviewer's prompt defines the words. A **vacuous** check stays green when the code it guards is disabled.
A **wrong claim** is a concrete claim in a commit message or a log entry (a count, "red-first", "verified")
that the reviewer checked and found false. A **lost behaviour** is a behaviour of the old script that the port
does not keep. Reviewer findings were wrong too, and were checked before the team acted on them.

## 5. Models and workers

| role | model calls | tool calls | tokens in | tokens out | read from the cache |
|---|---|---|---|---|---|
| the lead | 3,192 | 3,477 | 15.5 M | 3.2 M | 380.3 M |
| workers and in-process sub-agents | 5,691 | 6,820 | 22.9 M | 9.2 M | 517.1 M |
| the independent reviewer | 6,291 | | 15.2 M | 9.1 M | |

**The workers.**
- The lead dispatched pool workers 93 times and in-process sub-agents (mostly reviews) 25 times.
- 88 worker tasks made model calls. A task took a median of 18 min from its first to its last recorded event,
  with a p90 of 1 h 23 min. The longest took 4 h 41 min. Together they took 45.2 worker-hours.
- 10 worker calls hit the output ceiling of 32,768 tokens; 5 of them in run 0015. One task ended with two such
  hits and no file written.

**Which model answered.** The workers were configured to use the coding model, and the telemetry labels 4,937
worker calls with it. A defect in jichi, present in every build the drive used and found on 2026-11-20, sent
every call after a task's first to the lead's model. The decode speed (output tokens per second after the
first token) shows it:

| calls | n | median tokens per second |
|---|---|---|
| a worker task's first call (100 or more output tokens) | 56 | 155.0 |
| a worker task's later calls (300 or more output tokens) | 2,769 | 55.1 |
| the lead's calls (300 or more output tokens) | 1,990 | 55.5 |

So the coding model made only the first call of each task. No number on this page compares the two models.

**Failures and retries.** 48 of the team's 8,883 model calls failed (0.5 %); 26 of them in run 0013, during
the gateway outage. jichi retried 46 times. The run that met the outage ended when its 12 retries were used up.

**Context.** The lead, the workers and the in-process sub-agents compacted their context 215 times, dropping
old tool output to stay inside the model's window.

**Guards that fired.** The guard against a worker changing a test held a worker's result 17 times. jichi kept
53 states before it discarded them; in run 0010 the lead recovered a worker's files from one of them.

## 6. Supervision

- **303 log entries**, 221 of them check-ins. The check-in ran every 20 minutes.

  | 10-23 | 10-24 | 10-25 | 10-26 | 10-27 |
  |---|---|---|---|---|
  | 18 | 94 | 81 | 85 | 25 |

- **85 notes** went into the team, counted from 2026-10-24 05:58 +0200, when delivered notes began to be moved
  out of the inbox; earlier notes are not in this count. One more note, written after the loop had ended, was
  still waiting.
- **91 records of teeth**: the supervisor broke a guard on purpose and checked that its test went red.
- **106 alerts** from the drive's watch:
  - 51 gate results that were not green (stops and refusals included);
  - 19 failed pushes;
  - 12 dispatches of read-only tasks (some of them intended);
  - 6 gateway down, and 6 back up;
  - 6 times the lead itself edited one source or test file three times or more;
  - 3 trees left uncommitted for more than 30 minutes;
  - 2 periods without progress;
  - 1 dropped batch signal.
- **3 dated corrections** of its own entries, each naming what it supersedes.
- About 18 to 94 entries a day. Supervising a team like this is not "start it and come back next week".

Not recorded: the supervisor's own tokens and hours, and the operator's.

## 7. Planning with these numbers

These are one drive's numbers, on one machine, with two configured models. Use them as a first estimate for a
similar team, and replace them with your own after a week of records.

| plan for | from this drive |
|---|---|
| one gate | 35 minutes when green; a red at an early stage stops in minutes |
| one batch, from "ready" to landed | about 1 hour; up to 2.5 hours |
| one lead run | 2 h 20 min typical; one in ten over 9 h 40 min |
| one worker task | 18 minutes typical; one in ten over 1 h 23 min |
| a milestone | 6.6 lead-hours and 3.8 worker-hours on average (78.8 h and 45.2 h over 12 milestones) |
| outages | three in 3.4 days, of 20 minutes, 4 hours and 3 hours |
| the unexpected | 23 of 48 reds were lints; 4 defects found after the drive ended |
| supervision | a check-in every 20 minutes, 18 to 94 log entries a day |

## 8. How these numbers were produced

- **Sources.**
  - the loop's log, each run's meta file, telemetry and error log;
  - the CI follower's results and the gate's logs and timing files;
  - the independent reviews' results, meta files and telemetry;
  - the alerts log and the supervisor's log, notes and teeth records;
  - the drive's pages in the tree, `docs/internal/drive/2026-10-23-harness/`;
  - git history from master `0bcece87` to `561e91b4`, and the drive branch to `2dff8aad`.
- **Two scripts.** A first script extracted the facts. A second, written separately, re-derived every number
  on these pages by another route (shell, `awk` and `date` against the raw files; its own reading of the
  telemetry). Where the two disagreed, the record was read again and the right value is used here.
- **Definitions that decide the counts.**
  - A run's hours come from its meta file's start and end lines.
  - The lead is depth 0 in the telemetry; workers and in-process sub-agents are depth 1 or more.
  - A worker task is one `task_run` with at least one model call. Its length runs from its first to its last
    recorded event.
  - A red gate's cause is read from the failing drivers named in its log.
  - Two counts come from reading records, not from a script: the split of the 15 reds with a milestone's own
    driver (by design, defects, wrong reason, killed; from the supervisor's log), and the defects found after
    the drive (from commit messages, the lessons ledger and the supervisor's notes).
- **Not measured here.**
  - The time a person spent: the operator's decisions are in the supervisor's log, but not timed.
  - The supervisor's own cost.
  - Which model answered each worker call: the records carry labels; only the decode speed tells.
  - The causes of the outages.
