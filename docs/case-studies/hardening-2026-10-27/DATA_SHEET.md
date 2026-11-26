# The second hardening drive in numbers

*A data sheet for planning: how long a jichi team took to fix review findings in jichi itself, what it produced, how
often the gate said no and why, and what stopped it. Every number on this page was read from the drive's own records,
and nearly all were counted again by a second route; section 8 names the few that were not. None is an estimate. The
[lessons](README.md) page builds on it. Every time is local time (CET, UTC+1).*

---

## 0. What the drive was, and how to read this page

From 27 October to 5 November 2026 a team of jichi agents worked on jichi itself. No person wrote its code. Its task
was to fix the findings of a security and robustness review of 17 October, severity first. The drive is **paused**,
not ended: it stopped on 5 November at 16:16 and resumes after the harness-port drive.

- **The lead** is a 27B model. It ran every one of the 130 runs. It planned, briefed workers, reviewed their
  diffs, kept the drive's log and wrote the READY lines that ask for the gate.
- **The workers** are mostly a coding model, each in its own worktree. Reviews ran on the 27B model, read-only.
- **The loop** is `jichi-harness drive`, run inside a container on the development workstation. The container held
  no credential for the Git server.
- **The host's steps:** pushing, gating with the full `make ci`, landing on master and installing the new build
  (the re-pin). The supervisor's session ran them until 5 November; from then on, timers ran them. (A push timer
  set up on the first day stopped at a workstation reboot on 2 November, unnoticed.)
- **The supervisor** is a second agent outside the drive. It checked in every 20 minutes, and hourly on the last
  day.
- **The models** ran on the institution's free gateway. Both were declared at a price of 0 per million tokens, so
  the money cost is zero. The time cost is on this page.

**How to read a number.**
- "Median" is the middle value: half the cases took longer.
- "p90" means nine cases in ten took this long or less. Plan with the p90, not the median, because the slow tenth
  is what makes a schedule slip.
- Section 8 says how each number was produced.

## 1. At a glance

| | |
|---|---|
| Wall-clock span | 224.9 h (9.37 days), 2026-10-27 07:22 to 2026-11-05 16:16; paused since |
| The loop | running 214.4 h in 7 segments; stopped 10.5 h in 6 gaps, 9.6 h of them parked for outages |
| Runs of the lead | 130 (0077 to 0206): 114 ended normally, 16 with an error (15 model failures, 1 end-of-run check) |
| Milestones | 84 numbers, M1060 to M1143 |
| Review findings closed | 55: 24 medium, 31 low (2 of the lows by documenting, not by a code change) |
| Commits landed on master | 640, in 20 landings |
| Lines landed | +62,798 / -1,298 in 468 files (section 3 splits them) |
| `make ci` on the host | 28 runs: 20 green, 8 red; 19.2 h in all |
| The automated gate (last day only) | 4 landings; 4 false verdicts from its own scripts |
| Model calls | 25,367: 103.8 M tokens in, 27.8 M out |
| Failed model calls | 286 (1.13 %); jichi retried 268 times |
| Worker dispatches | 203: 159 ran and returned ok, 40 refused by jichi, 4 ran and reported not ok |
| The supervisor's log | 704 dated entries, 546 of them check-ins |
| Release | 0.13.0, published 2026-11-05 20:37 |

## 2. Time

**The span.** The first run started on 27 October at 07:22:44. The loop ended on the STOP file on 5 November at
16:16:14. That is 224 h 53 min.

**The loop's segments.** The loop ran seven times. It started again after each stop:

| segment | from | to | hours | why it ended |
|---|---|---|---|---|
| 1 | 10-27 07:22 | 10-27 09:01 | 1.7 | a single shakedown run, by design |
| 2 | 10-27 09:05 | 10-28 19:38 | 34.6 | a restart onto a new build of the loop (README section 2.4) |
| 3 | 10-28 19:38 | 10-31 01:02 | 53.4 | parked: the model gateway accepted no connections |
| 4 | 10-31 04:10 | 11-02 02:23 | 46.2 | parked: the same |
| 5 | 11-02 05:31 | 11-02 17:09 | 11.6 | parked: the same |
| 6 | 11-02 20:31 | 11-05 03:30 | 55.0 | stopped while batch 12 went through the gate |
| 7 | 11-05 04:19 | 11-05 16:16 | 11.9 | the pause |

**The gaps between segments**, 10 h 30 min in all:
- three parks for gateway outages: 3 h 8 min, 3 h 8 min and 3 h 22 min, together 9 h 38 min;
- 49 min while batch 12 was gated;
- 3 min between the shakedown run and the loop's start, and 8 s for the restart.

**Runs.** The loop starts a fresh lead run whenever the previous one ends. A run ends when the lead stops, usually
after it has recorded its state and written a handoff for the next run, or when a model failure outlasts its
retries.

| all 130 runs | length |
|---|---|
| shortest | 50 s (run 0127, a model failure) |
| one in four took at most | 6 min |
| median | 59 min |
| three in four took at most | 2 h 18 min |
| nine in ten took at most | 4 h 35 min |
| longest | 8 h 2 min (run 0087) |
| total | 214.0 h |

42 runs were shorter than 10 minutes. 36 of them committed only the drive's records, most of them the lead's
"pause checks" while a batch waited for the gate. The other 6 committed nothing and died on a model failure.

**Runs started per day:**

| 10-27 | 10-28 | 10-29 | 10-30 | 10-31 | 11-01 | 11-02 | 11-03 | 11-04 | 11-05 |
|---|---|---|---|---|---|---|---|---|---|
| 7 | 6 | 8 | 6 | 5 | 9 | 16 | 10 | 15 | 48 |

The 16 on 2 November are mostly runs that died in model outages. The 48 on 5 November are mostly pause checks.

**What the runs produced:**

| runs that... | runs | hours |
|---|---|---|
| committed changes beyond the drive's own records | 72 | 202.7 |
| committed only the drive's own records | 47 | 9.3 |
| committed nothing | 11 | 2.0 |

All 11 runs that committed nothing ended on a model failure.

**Where the lead's 214.0 hours went**, from the lead's own tool and model calls:

| the lead was | hours | share |
|---|---|---|
| waiting for workers (`spawn_parallel`) | 77.3 | 36 % |
| in its own model calls | 70.2 | 33 % |
| running shell commands: builds, tests, git | 35.6 | 17 % |
| waiting for in-process reviewers and helpers (`spawn_subagent`) | 29.7 | 14 % |
| everything else | 1.2 | under 1 % |

**Delays that did not stop the loop:**
- The supervisor's session was away for 16.5 hours on 4 November, from about 03:20 to 20:01.
- No push left the workstation for 16 h 44 min, from 03:17 to 20:01 that day.
- The drive ran a superseded build for 22 h 15 min. Master moved at 03:18 on 4 November; the re-pin came at 01:33
  the next night. Thirteen runs, 0147 to 0159, ran the old build.
- A landed fix in the loop's own code waited 7 h 13 min for the loop to restart, on 28 October.

**The pause.** On 22 November, when this page was written, the drive had been paused for 17 days. The push timer
still ran every 20 minutes and found nothing new to push.

## 3. Work landed

**640 commits** reached master in 20 landings, from 561e91b4 to 9ada7cd7.
- Every landing was a fast-forward; there are no merge commits.
- Three of the 640 are the drive's setup commits.
- 20 more commits wait on the team branch. All of them are the lead's log entries, written after its last READY
  line.
- Every commit on the team branch names the operator as author and committer.
- 635 of the landed commits name the model that wrote them, in the `Jichi-Model` trailer: 405 the 27B model, 230
  the coding model.
- Five have no such trailer:
  - the supervisor's setup commit;
  - two of the lead's commits on the first day; for one of them, the commit tool had silently dropped the trailer
    the lead passed;
  - two worker commits.

**Lines, by area** (`git diff --numstat 561e91b4 9ada7cd7`):

| area | files | lines added | lines removed |
|---|---|---|---|
| `src/harness/` | 6 | 507 | 99 |
| the rest of `src/` | 79 | 6,446 | 695 |
| `include/` | 25 | 622 | 54 |
| `tests/` | 111 | 14,595 | 200 |
| public docs (`docs/` outside `internal/`) | 46 | 1,109 | 133 |
| `docs/internal/` (the drive's log, reports, the findings index) | 194 | 39,041 | 64 |
| build and project files (`Makefile`, README, CHANGELOG, ...) | 7 | 478 | 53 |

For every line of code added in `src/` and `include/`, the drive added 1.9 lines of test and 5.2 lines of
internal record.

**Commits landed per day** (by commit date):

| 10-27 | 10-28 | 10-29 | 10-30 | 10-31 | 11-01 | 11-02 | 11-03 | 11-04 | 11-05 |
|---|---|---|---|---|---|---|---|---|---|
| 59 | 69 | 67 | 48 | 53 | 96 | 39 | 65 | 73 | 71 |

**The 20 landings:**

| # | landed | commits | what |
|---|---|---|---|
| 1 | 10-27 09:17 | 6 | M1060, the shakedown, with the 3 setup commits |
| 2 | 10-27 10:02 | 4 | M1061, a credential fix the drive needed to push |
| 3 | 10-28 05:46 | 78 | the first large batch, after four reds |
| 4 | 10-28 12:25 | 13 | batch 14 |
| 5 | 10-28 20:23 | 12 | batch 13 |
| 6 | 10-29 06:24 | 36 | batch 7: the release build is the tested build |
| 7 | 10-29 21:25 | 35 | batches 8 and 9: numbers and casts, resources |
| 8 | 10-31 00:13 | 57 | batch 10, split by the operator |
| 9 | 10-31 15:01 | 28 | batch 10b-1 |
| 10 | 10-31 17:38 | 13 | batch 10b-2 |
| 11 | 11-01 03:59 | 19 | batch 10b-3 |
| 12 | 11-01 07:58 | 14 | batch 10b-4 |
| 13 | 11-01 22:20 | 57 | batches 10b-5 and 10b-6a |
| 14 | 11-02 02:18 | 23 | batch 10b-6b |
| 15 | 11-03 02:39 | 37 | batch 10b-6c |
| 16 | 11-04 03:18 | 68 | batch 11, the second 0.13.0 release candidate |
| 17 | 11-05 04:17 | 82 | batch 12, the first landing by the automated gate |
| 18 | 11-05 07:04 | 15 | batch 12b, part 1: the fix for a WRONG verdict |
| 19 | 11-05 08:44 | 2 | batch 12b, part 2 |
| 20 | 11-05 15:44 | 41 | batch 12c: the 0.13.0 release record |

The first 16 landing times are the supervisor's log entries, written minutes after each push. The last four come
from the automated gate's state file, to the second.

**Milestones.** All 84 numbers from M1060 to M1143 appear in the subjects of landed commits.

**Review findings.** At the pause, the review's findings index (111 rows) marks:

| severity | rows | closed before this drive | closed in this drive | open at the pause |
|---|---|---|---|---|
| high | 15 | 15 | 0 | 0 |
| medium | 39 | 14 | 24 | 1 (B-02) |
| low | 46 | 2 | 31 | 13 |
| note | 11 | 0 | 0 | 11 |

Two of the 31 lows were closed by documenting the behaviour, as the finding itself recommended, not by a code change.

## 4. The gate

**Two regimes.**
- **27 October to 4 November: by hand.** The supervisor read the commit records, ran its own checks by effect, ran
  a pre-gate sweep (from 31 October), then `make ci` on the host, then landed and re-pinned.
- **From 5 November: by timer.** A watcher read the last READY line every 10 minutes and ran the same steps.

**28 runs of `make ci`: 20 green, 8 red.**

| | minutes |
|---|---|
| a green run, median | 45.8 |
| a green run, shortest and longest | 43.6 and 49.3 |
| a red run that failed late (5 of the 8) | 42 to 45 |
| a red run that failed early (3 of the 8) | 1 to 3 |
| all 28 runs | 19 h 9 min |

A green run grew from about 44 minutes at the first landings to about 49 minutes on the last day, as stages and tests
were added.

So **a batch costs most of an hour of gate time.** A red in the last stages costs another hour once the fix is in.

**Landings and attempts.** 16 of the 20 landings needed one `make ci`. Four needed more:

| landing | `make ci` runs | from the first red's start to the green's end |
|---|---|---|
| 3, the first large batch | 5 | 9 h 24 min |
| 7, batches 8 and 9 | 3 | 10 h 6 min |
| 13, batches 10b-5 and 10b-6a | 2 | 4 h 42 min |
| 15, batch 10b-6c | 2 | 9 h 56 min (a 3 h 22 min outage inside) |

**Why `make ci` said no.**

| stage | reds | what it was |
|---|---|---|
| lints | 2 | five lints at once; then a lint the first fix exposed |
| the C++ build | 1 | a `malloc` result without a cast |
| a recorded model exchange | 1 | the exchange drifted when a new message entered it |
| the mutant sweep | 2 | test drivers that stayed green with a program that does nothing |
| sanitizers | 1 | a leak in a test's own oracle |
| the `-O2` unit build | 1 | a use-after-free in a unit test |

All eight were real defects in the batch or its tests. None was a false red of `make ci` itself.

**Stopped before `make ci`.** Two READY lines never reached `make ci`, because the supervisor's checks by effect
found a defect first:
- a fix that turned a 2-second DNS error into a misleading 30-second one (27 October);
- the first release candidate's guard, which refused legitimate input (3 November).

**The automated gate's first day** (its state file, 23 lines):
- 6 starts and 4 landings. From start to landing took 52 to 66 minutes, of which `make ci` took about 49.
- 2 REDs from the pre-gate sweep, both false.
- 3 withdrawals: 1 genuine (the supervisor's), 2 false.
- 3 re-gates, each after a false verdict.
- 4 re-pins, one of which reported a false failure of `doctor`: its reachability probe has a 3-second limit, and the
  gateway's TLS handshake took about 10 seconds.
- From each false verdict to the gate's next start on that commit: 37, 20 and 57 minutes, 1 h 54 min in all.

**The advisory reviewer.** From 5 November 02:51 the 27B model reviewed the gated batches in a fresh context. It
rated each milestone HOLDS, WEAK or WRONG. On batch 12 it found one WRONG fix that the gate had passed. Its verdicts
informed the supervisor; they did not hold a landing.

## 5. Models and workers

**Model calls.**

| | model calls |
|---|---|
| the lead (depth 0) | 10,743 |
| workers and in-process helpers (depth 1) | 14,624 |
| all | 25,367 |

The split by model is not given here. A failed call, and a call of an in-process helper, carries the lead's model
label whoever made it, so per-model counts from the labels would be wrong.

- A successful call took a median of 9.8 s, with a p90 of 56.2 s. The longest took 2,498.5 s.
- Tokens: 103.8 million input tokens newly processed, 2,227 million more served from the gateway's prompt
  cache (jichi's telemetry counts the two in separate fields; this page does not add them), and 27.8 million
  out. The lead's own calls: 48.0 million in, 10.6 million out; the workers': 55.8 million in, 17.2 million out.

**Tool calls: 29,851**, of 30 different tools:
- shell commands 15,348;
- reading files 5,451;
- searching code 2,995;
- editing files 2,303.

**Worker dispatches** (`spawn_parallel`): 203.
- 159 ran and returned ok.
- jichi refused 40:
  - 19 because the tree held uncommitted changes before a writing dispatch;
  - 21 because the arguments were malformed.
- 4 ran and reported not ok.
- Of the 174 dispatches whose arguments parse, 145 sent one task, 21 two, 6 three, 1 four and 1 eight.
- An ok dispatch blocked the lead for a median of 18.4 minutes, with a p90 of 73.8 minutes. The longest took 127.1
  minutes.
- The lead also started 101 in-process reviewers and helpers (`spawn_subagent`).

**Failures and retries.** 286 of 25,367 model calls failed (1.13 %):

| failure | calls |
|---|---|
| HTTP 503, a backend unavailable | 137 |
| no HTTP answer: connect, TLS, DNS or a timeout | 133 |
| an error inside a stream that had started with HTTP 200 | 7 |
| HTTP 502 | 6 |
| HTTP 500 | 3 |

jichi retried 268 times. Fifteen runs still died on model failures; together they had run 10 h 33 min. They came in
two shapes:
- the lead model's backend failing in stretches of up to about 10 minutes while the gateway answered: 9 runs;
- the gateway accepting no connections at all for about four hours, three times: 6 runs, and 9 h 38 min of parked
  loop.

**Builds.** The runs used 19 different builds of jichi: the one the drive started with, and 18 re-pinned after
landings. 15 were of version 0.12.0, then 4 of 0.13.0.

**Configuration changes** during the drive, each the operator's decision:
- 2 November: retries per model call 12 to 120;
- 3 November: an end-of-run check, a clean build and the unit suite at `-Werror`;
- 4 November: the limit per shell command 30 minutes to 2 hours.

## 6. Supervision

- **Check-ins:** every 20 minutes from 27 October 07:26; hourly from 5 November 02:11.
- **The log:** 10,787 lines in 704 dated entries, 546 of them check-ins.

| 10-27 | 10-28 | 10-29 | 10-30 | 10-31 | 11-01 | 11-02 | 11-03 | 11-04 | 11-05 | later |
|---|---|---|---|---|---|---|---|---|---|---|
| 69 | 88 | 81 | 77 | 75 | 80 | 96 | 84 | 14 | 39 | 1 |

The 14 entries of 4 November all come before about 03:20 or after 20:01: the supervisor's session was away in
between.

- **Notes to the team** are labelled `SV:<hhmm>`. The log names 132 distinct labels. That is not a count of notes
  delivered: some labels are only mentioned, and some notes carry a suffix. Delivered notes were not counted for this
  page.
- About 75 entries a day (704 over 9.4 days) is what supervising this drive took. Running a self-driving team is
  not "start it and come back next week".

## 7. Planning with these numbers

These are one drive's numbers, on one workstation, with two models. Use them as a first estimate for a similar team,
and replace them with your own as soon as you have a week of records.

| plan for | from this drive |
|---|---|
| one gated batch | about 46 minutes of `make ci` when green; one landing in five needed two to five runs and 4.7 to 10.1 hours |
| one lead run | 1 hour typical; 4.6 hours for one run in ten; up to 8 |
| one worker dispatch | 18 minutes typical; 74 minutes for one in ten |
| a milestone | 2.7 hours of wall clock each (224.9 h over 84 milestone numbers) |
| outages | 4.3 % of the span parked (9.6 h); 15 runs lost to model failures |
| supervision | a check-in every 20 minutes; about 75 log entries a day |
| the unexpected | one absence of 16.5 h; a build 22 h out of date; four false verdicts from new gate scripts in one day |

## 8. How these numbers were produced

- **Sources:**
  - the loop's log (`harness.log`), and each run's `meta.txt` and `telemetry.jsonl`;
  - the host's `make ci` records (one start, end and exit status per run) and the automated gate's state file;
  - the push log;
  - git history in the host's copy of the repository, between master 561e91b4 (the start) and 9ada7cd7 (the last
    landing), and the team branch's head 47735f3f;
  - the findings index as committed at 47735f3f;
  - the supervisor's log.
- **Two routes for nearly every number.** A fact base was extracted first. Then the numbers used here were counted
  again by a different route: shell tools instead of a script, the loop's own run lengths instead of the metadata's,
  `rev-list` instead of `log`, raw-line patterns instead of a JSON parser. Where the two routes disagreed, the
  disagreement was traced and the right value is used. The scripts, their output, and a list of corrections to the
  fact base stay with the internal records.
- **Taken from a single record, not counted twice:**
  - the length of the gateway outages (about four hours each) and of the backend stretches (up to about 10 minutes):
    the supervisor's outage analysis, `docs/internal/2026-11-02-hardening-drive-outage-telemetry.md`; the three parks
    themselves are counted from the loop's log;
  - the TLS handshake of about 10 seconds: the supervisor's log, measured at the time with `curl`.
- **Definitions that decide the counts:**
  - A run's length is the loop's own "end ... after N s" line. The 130 runs add up to 214.0 h; the metadata's start
    and end times give 213.8 h.
  - A "record-only" run changed only files under `docs/internal/drive/` and `.jichi/`.
  - A worker dispatch is one `spawn_parallel` call of the lead. A dispatch can send several tasks.
- **Not measured here:**
  - the operator's time, and how long a question waited for the operator's answer;
  - the supervisor's own working time and tokens;
  - how many of the supervisor's notes reached a live run, and how many waited for the next one;
  - the model gateway's load: a slow call may be a busy server or a long answer;
  - what the lead's slips cost in hours of its own work.
