# Lost time in four jichi drives: notes to the table

*Written 2026-11-22 for a Pareto chart of the time that was not progress, by cause. Data: `lost-time.tsv` (106 rows, each a set-back with a measured cost) and `lost-time-unmeasured.tsv` (71 set-backs the records cannot put a time on). Only measured costs carry hours; a set-back without two recorded ends went to the unmeasured list, not into an estimate. Every total below is computed from the rows.*

## 1. "hours" is not one currency

Each row's `what_was_lost` names what its hours measure. An hour of a stopped loop stopped the whole team. An hour of gate machine time occupied the gate host while the team worked on. An hour of "time to green" is a delay of master that also holds the fix work, which was useful. Adding rows of different currencies adds unlike things. Section 3 therefore gives three charts, one per kind of loss.

| group | what_was_lost | what one hour is | was the team stopped? |
|---|---|---|---|
| A | loop stopped or parked (no lead run) | wall clock in which no lead run existed (stopped, parked, or runs dying at start) | yes |
| A | loop stopped (deliberate hold) | wall clock of a loop stopped on purpose (a review, a release decision, a gate, a retest) | yes, by decision |
| A | no successful model call (runs kept retrying) | wall clock from the last successful model call to the next one, while runs kept retrying | yes, in effect |
| A | lead run time that produced no change | lead run hours that changed nothing: runs that only confirmed a hold, runs that died, waits on read-only workers | the lead, yes |
| A | lead run time redoing work done once | lead run hours spent redoing work that had been done once and lost | no, but the work was done twice |
| A | worker time that produced no change | worker hours that produced no change: hung, looping, cut off | that worker, and a lead waiting on it |
| A | model call time ending in an error | the duration of model calls that ended in an error and were retried | the caller, yes |
| A | check run with no verdict | hours of a test run that ended without a verdict (killed by a limit or a cap) | partly |
| B | time to green after a red (includes fix work) | wall clock from a batch's first red gate to its green gate or landing; it includes the fix work and the final green gate | no; master stood still |
| B | master did not move (a ready head waited) | wall clock in which a head ready to gate or land waited: for the operator, a READY line, a false verdict to be withdrawn | no; master stood still |
| B | no push left the machine (team kept working) | wall clock in which no commit left the workstation | no |
| B | landed fix or build not in use (team kept working) | wall clock in which a landed fix or build was not what the drive ran | no |
| C | gate machine time | host hours of `make ci` (or the gate) that ended red or invalid | no; the gate host was busy |

Group A is lost work; group B is delay; group C is machine time. "Deliberate hold" rows (group A) are time the operator chose to stop the loop; a chart of set-backs may leave them out, and section 3.1 shows both.

## 2. Totals by cause class, all currencies

"Without overlap" counts each stretch of time once inside the class. Rows of the class whose spans overlap are joined and their union is counted. A row that lies wholly inside other rows of the class adds nothing and is listed as dropped; rows that overlap only in part are listed too, and their shared time is counted once. Rows of different classes that share time keep their hours in both classes (section 6 lists the pairs), so these class totals must not be added across classes. They also mix currencies; use section 3 for a chart.

| cause_class | rows | hours, all rows | hours, without overlap | dropped: wholly inside | overlapping in part, counted once |
|---|---|---|---|---|---|
| model outage | 12 | 28.31 | 21.87 | L02 (in L01), L23 (in L22) | - |
| network/VPN | 5 | 4.12 | 4.04 | L26 (in L25) | - |
| host (reboot, disk, toolchain) | 3 | 4.44 | 3.49 | L72 (in L84) | - |
| waiting for the operator | 7 | 14.15 | 14.15 | - | - |
| supervisor absent | 2 | 38.99 | 22.27 | - | L45 with L46 |
| supervisor mistake | 2 | 1.99 | 1.99 | - | - |
| gate red, re-gate | 32 | 155.00 | 147.60 | L09 (in L10), L11 (in L12), L13 (in L15), L18 (in L19), L48 (in L49), L50 (in L51), L52 (in L53), L54 (in L55), L98 (in L91, L96, L92) | L71 with L83, L71 with L81, L71 with L80, L71 with L79 |
| gate-script false verdict | 8 | 10.31 | 7.76 | L75 (in L86), L76 (in L86) | - |
| worker failure (cut off, stuck, no write access) | 9 | 16.09 | 16.09 | - | - |
| lead mistake (rewrite, dirty tree, wrong dispatch) | 10 | 11.50 | 10.97 | L16 (in L17) | - |
| record slip | 5 | 21.88 | 19.63 | L77 (in L82), L78 (in L85) | - |
| other | 11 | 17.81 | 17.81 | - | - |

## 3. Three Pareto charts that do not mix kinds of loss

Each table ranks the classes by their hours without overlap inside the group (the same union rule, applied to the rows of that class and that group). Within group A the union is taken on wall clock, so two workers idle in the same minute would count once; no such pair occurs in the rows.

### 3.1 Group A: the team, or part of it, stopped or did work that was lost

| rank | cause_class | rows | hours without overlap | of which deliberate holds | without the holds |
|---|---|---|---|---|---|
| 1 | model outage | 12 | 21.87 | 0.00 | 21.87 |
| 2 | worker failure (cut off, stuck, no write access) | 8 | 13.22 | 0.00 | 13.22 |
| 3 | other | 9 | 10.02 | 6.98 | 3.04 |
| 4 | waiting for the operator | 3 | 4.06 | 2.55 | 1.51 |
| 5 | lead mistake (rewrite, dirty tree, wrong dispatch) | 4 | 3.46 | 0.00 | 3.46 |
| 6 | supervisor mistake | 1 | 1.70 | 0.00 | 1.70 |
| 7 | network/VPN | 3 | 0.66 | 0.00 | 0.66 |

Group A in all: 40 rows, 54.98 h without overlap across classes.

### 3.2 Group B: delay while the team kept working

| rank | cause_class | rows | hours without overlap |
|---|---|---|---|
| 1 | gate red, re-gate | 17 | 128.88 |
| 2 | supervisor absent | 2 | 22.27 |
| 3 | record slip | 5 | 19.63 |
| 4 | waiting for the operator | 4 | 10.09 |
| 5 | lead mistake (rewrite, dirty tree, wrong dispatch) | 4 | 7.24 |
| 6 | other | 1 | 7.22 |
| 7 | gate-script false verdict | 3 | 5.43 |
| 8 | network/VPN | 2 | 3.45 |
| 9 | worker failure (cut off, stuck, no write access) | 1 | 2.87 |
| 10 | host (reboot, disk, toolchain) | 1 | 2.50 |

Group B in all: 40 rows, 197.95 h without overlap across classes.

### 3.3 Group C: gate host time on red or invalid verdicts

| rank | cause_class | rows | hours without overlap |
|---|---|---|---|
| 1 | gate red, re-gate | 15 | 26.12 |
| 2 | gate-script false verdict | 5 | 2.38 |
| 3 | host (reboot, disk, toolchain) | 2 | 1.94 |
| 4 | lead mistake (rewrite, dirty tree, wrong dispatch) | 2 | 0.80 |
| 5 | other | 1 | 0.57 |
| 6 | supervisor mistake | 1 | 0.29 |

Group C in all: 26 rows, 32.09 h without overlap across classes.

Group B is dominated by "time to green after a red", which holds the fix work. Its two other large items are not lost work either: the team went on working while nothing was pushed (L45) and while it ran an outdated build (L46).

## 4. Subtotals by what_was_lost

Without overlap means the same here, within one currency, across all classes and drives.

| group | what_was_lost | rows | hours, all rows | hours, without overlap |
|---|---|---|---|---|
| A | loop stopped or parked (no lead run) | 9 | 15.54 | 15.54 |
| A | loop stopped (deliberate hold) | 5 | 9.53 | 9.53 |
| A | no successful model call (runs kept retrying) | 4 | 9.62 | 9.62 |
| A | lead run time that produced no change | 9 | 10.24 | 10.24 |
| A | lead run time redoing work done once | 1 | 2.16 | 2.16 |
| A | worker time that produced no change | 7 | 11.07 | 11.07 |
| A | model call time ending in an error | 2 | 1.15 | 1.15 |
| A | check run with no verdict | 3 | 2.14 | 2.14 |
| B | time to green after a red (includes fix work) | 23 | 156.86 | 156.86 |
| B | master did not move (a ready head waited) | 10 | 22.44 | 22.44 |
| B | no push left the machine (team kept working) | 5 | 22.27 | 22.27 |
| B | landed fix or build not in use (team kept working) | 2 | 29.47 | 29.47 |
| C | gate machine time | 26 | 32.09 | 32.09 |

### 4.1 Class by currency (for a stacked chart)

Each cell is without overlap inside the cell; empty cells are left out.

| cause_class | group | what_was_lost | rows | hours, without overlap |
|---|---|---|---|---|
| model outage | A | loop stopped or parked (no lead run) | 5 | 13.02 |
| model outage | A | no successful model call (runs kept retrying) | 3 | 9.29 |
| model outage | A | lead run time that produced no change | 3 | 5.11 |
| model outage | A | model call time ending in an error | 1 | 0.90 |
| network/VPN | A | loop stopped or parked (no lead run) | 1 | 0.08 |
| network/VPN | A | no successful model call (runs kept retrying) | 1 | 0.33 |
| network/VPN | A | model call time ending in an error | 1 | 0.25 |
| network/VPN | B | no push left the machine (team kept working) | 2 | 3.45 |
| host (reboot, disk, toolchain) | B | time to green after a red (includes fix work) | 1 | 2.50 |
| host (reboot, disk, toolchain) | C | gate machine time | 2 | 1.94 |
| waiting for the operator | A | loop stopped (deliberate hold) | 2 | 2.55 |
| waiting for the operator | A | lead run time that produced no change | 1 | 1.51 |
| waiting for the operator | B | master did not move (a ready head waited) | 4 | 10.09 |
| supervisor absent | B | no push left the machine (team kept working) | 1 | 16.74 |
| supervisor absent | B | landed fix or build not in use (team kept working) | 1 | 22.25 |
| supervisor mistake | A | loop stopped or parked (no lead run) | 1 | 1.70 |
| supervisor mistake | C | gate machine time | 1 | 0.29 |
| gate red, re-gate | B | time to green after a red (includes fix work) | 17 | 128.88 |
| gate red, re-gate | C | gate machine time | 15 | 26.12 |
| gate-script false verdict | B | time to green after a red (includes fix work) | 1 | 3.53 |
| gate-script false verdict | B | master did not move (a ready head waited) | 2 | 4.40 |
| gate-script false verdict | C | gate machine time | 5 | 2.38 |
| worker failure (cut off, stuck, no write access) | A | lead run time redoing work done once | 1 | 2.16 |
| worker failure (cut off, stuck, no write access) | A | worker time that produced no change | 7 | 11.07 |
| worker failure (cut off, stuck, no write access) | B | master did not move (a ready head waited) | 1 | 2.87 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | A | lead run time that produced no change | 3 | 2.82 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | A | check run with no verdict | 1 | 0.64 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | B | time to green after a red (includes fix work) | 1 | 2.33 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | B | master did not move (a ready head waited) | 1 | 2.83 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | B | no push left the machine (team kept working) | 2 | 2.07 |
| lead mistake (rewrite, dirty tree, wrong dispatch) | C | gate machine time | 2 | 0.80 |
| record slip | B | time to green after a red (includes fix work) | 3 | 19.63 |
| record slip | B | master did not move (a ready head waited) | 2 | 2.25 |
| other | A | loop stopped or parked (no lead run) | 2 | 0.74 |
| other | A | loop stopped (deliberate hold) | 3 | 6.98 |
| other | A | lead run time that produced no change | 2 | 0.80 |
| other | A | check run with no verdict | 2 | 1.50 |
| other | B | landed fix or build not in use (team kept working) | 1 | 7.22 |
| other | C | gate machine time | 1 | 0.57 |

### 4.2 Drive by group

The drives differ in length: hardening-1 135 h 58 min, the harness drive 82 h 20 min, hardening-2 224 h 53 min, harness-port 404 h 40 min (2026-11-05 23:04:26 to the last row's end, 2026-11-22 19:44:52; it is still running). Compare shares, not sums.

| drive | group | rows | hours, without overlap |
|---|---|---|---|
| hardening-1 | A | 6 | 9.04 |
| hardening-1 | B | 7 | 16.67 |
| hardening-1 | C | 8 | 3.00 |
| harness | A | 9 | 8.78 |
| harness | B | 1 | 3.12 |
| harness | C | 7 | 19.91 |
| hardening-2 | A | 11 | 15.76 |
| hardening-2 | B | 10 | 92.29 |
| hardening-2 | C | 4 | 3.68 |
| harness-port | A | 14 | 21.40 |
| harness-port | B | 22 | 85.87 |
| harness-port | C | 7 | 5.50 |

## 5. The largest rows

| id | hours | drive | setback | group | what_was_lost |
|---|---|---|---|---|---|
| L46 | 22.25 | hardening-2 | Supervisor's session away: drive ran an outdated build | B | landed fix or build not in use (team kept working) |
| L57 | 17.24 | hardening-2 | First release candidate failed by effect | B | time to green after a red (includes fix work) |
| L45 | 16.74 | hardening-2 | Supervisor's session away: no push for 16 h 44 min | B | no push left the machine (team kept working) |
| L96 | 14.64 | harness-port | MI to SEC: four reds before a landing | B | time to green after a red (includes fix work) |
| L32 | 13.23 | harness | Red gates that failed only on lints (23) | C | gate machine time |
| L80 | 12.93 | harness-port | H7: first red to landing (a leak; an -O2 error) | B | time to green after a red (includes fix work) |
| L58 | 11.44 | hardening-2 | A landed fix was wrong: the release cut moved | B | time to green after a red (includes fix work) |
| L83 | 10.56 | harness-port | H9b: first red to landing (-O2 and clang errors) | B | time to green after a red (includes fix work) |
| L51 | 10.10 | hardening-2 | Batches 8 and 9: from the first red to green | B | time to green after a red (includes fix work) |
| L55 | 9.93 | hardening-2 | Batch 10b-6c: from its red to green | B | time to green after a red (includes fix work) |

The largest rows of group A, lost work: L22 4.08 h (Model gateway refused new connections); L01 4.02 h (Model gateway refused connections for four hours); L87 3.88 h (A worker went in circles: 233 min without a code edit).

## 6. Rows of different classes that share time

Each class keeps its own hours for these. Whoever adds classes together must count the shared time once.

- L03 (waiting for the operator; A) with L15 (gate red, re-gate; B)
- L14 (gate-script false verdict; C) with L15 (gate red, re-gate; B)
- L22 (model outage; A) with L32 (gate red, re-gate; C)
- L23 (model outage; A) with L32 (gate red, re-gate; C)
- L28 (worker failure (cut off, stuck, no write access); A) with L30 (gate red, re-gate; C)
- L32 (gate red, re-gate; C) with L38 (other; A)
- L42 (model outage; A) with L54 (gate red, re-gate; C)
- L42 (model outage; A) with L55 (gate red, re-gate; B)
- L43 (model outage; A) with L54 (gate red, re-gate; C)
- L43 (model outage; A) with L55 (gate red, re-gate; B)
- L45 (supervisor absent; B) with L57 (gate red, re-gate; B)
- L56 (gate-script false verdict; B) with L58 (gate red, re-gate; B)
- L57 (gate red, re-gate; B) with L60 (other; A)
- L58 (gate red, re-gate; B) with L63 (other; A)
- L64 (other; A) with L71 (gate red, re-gate; C)
- L64 (other; A) with L80 (gate red, re-gate; B)
- L65 (other; A) with L83 (gate red, re-gate; B)
- L70 (other; A) with L73 (host (reboot, disk, toolchain); C)
- L70 (other; A) with L74 (gate-script false verdict; C)
- L70 (other; A) with L78 (record slip; B)
- L70 (other; A) with L85 (record slip; B)
- L71 (gate red, re-gate; C) with L82 (record slip; B)
- L73 (host (reboot, disk, toolchain); C) with L85 (record slip; B)
- L74 (gate-script false verdict; C) with L85 (record slip; B)
- L92 (gate red, re-gate; B) with L93 (lead mistake (rewrite, dirty tree, wrong dispatch); B)
- L94 (record slip; B) with L95 (waiting for the operator; B)
- L96 (gate red, re-gate; B) with L97 (waiting for the operator; B)
- L96 (gate red, re-gate; B) with L99 (gate-script false verdict; C)
- L102 (worker failure (cut off, stuck, no write access); A) with L105 (model outage; A)

## 7. How the classes were chosen

- **Read-only dispatches** (L35, L36, L106) are *lead mistake*: in every timed case the lead's dispatch left out `"write": true`. The class list's "no write access" under *worker failure* was therefore not used.
- **A false verdict of any gate check** is *gate-script false verdict*, whoever wrote the check: the supervisor's host scripts (L14, L56, L75, L76), the in-tree smoke runner's 120 s limit (L33), leakcheck matching a passing line (L99), a flaky in-tree test (L74). The supervisor's authorship is named in `how_measured`. A red caused by the supervisor running two test tiers at once is *supervisor mistake* (L20).
- **A red on a real defect of the batch or its tests** is *gate red, re-gate*. A red the lead brought about by declaring READY early is *lead mistake* (L16, L17, L27); a ledger red from a commit without trailers is *record slip*.
- **A red episode with several causes** takes the class of its first red; its other causes have rows of their own (for example the operator's overnight ledger-accept, L95, inside the H10 episode, L94).
- **Holds.** The operator's review hold and the hold while the operator decided the release order are *waiting for the operator* (L04, L69); holds while a gate ran or the platforms were retested are *other* (L63, L68, L70). All carry "loop stopped (deliberate hold)".
- **Waits for the operator's go or accept on a green or blocked head** are *waiting for the operator* (L06, L07, L95, L97).
- **Defects of the drive's own tooling** with no class of their own are *other*: runs that died at start on an oversized prompt (L64, L65), a landed loop fix the running loop never loaded (L47), the 30 min command limit that killed full smoke runs (L59, L60), idle runs after the plan was done (L38), and the extra gate a merge needed after another session pushed to master (L08).
- **L28** (run 0007 redid fixes) is *worker failure*: the worker's verified result was held by the test-edit guard and could not be applied. Its currency is the only "redoing" row.
- **L39** (a two-minute network gap that left one call hanging until the dead-stream fence) is *network/VPN*; the long failing stretch of 2026-11-10 (L66) is *model outage*.

## 8. Values chosen where the records disagree, and spans computed for this table

- hardening-1 outage: ends at the first successful call, 20:56:44, as ERRATA 1 decides from telemetry (the fact base and the supervisor's log said 20:56:59): 4 h 01 min 24 s (L01).
- hardening-2 outdated build: the re-pin at 01:33:26, as ERRATA 1 decides (not 01:50): 22 h 14 min 43 s (L46). The three parks follow harness.log (ERRATA 2).
- hardening-2 spans are taken to the second from the records; the pages round them: the release cut 11 h 26 min 37 s (page: 11 h 27 min, L58), batches 8 and 9 10 h 05 min 52 s (DATA_SHEET: 10 h 6 min; verify.out: 10h05m, L51), batch 10b-6c 9 h 55 min 55 s (page: 9 h 56 min, L55).
- harness drive, second VPN outage: the span runs to the last failed push, 17:57:21; ERRATA 3 shows the first answered push in the 17:57:32 tick, not at 18:17:34 (L25).
- harness drive, the 120 s kills (L33): both gates also failed lint drivers, so the kill was not their only red. The page's "1 h 25 min of gate time" is kept, with that caveat in the row.
- harness-port, 2026-11-10: the DATA_SHEET's "31 minutes" is the page watcher's reading at its alarm (10:39:03). Run 0032's telemetry, read for this table, shows no successful model call from 10:07:52 to 11:18:46, 1 h 10 min 54 s (L66).
- harness-port, task B (L87): 233 min = 387 min run minus the last edit at minute 154, both from the analysis; its timeline gives 12:09 to 16:03 at minute precision.
- Spans with an end known only to the minute: L47, L95, L97, L100, L102, L104, L87. The row says so.
- Spans computed for this table from machine records, not stated on a page: hardening-1 batch 5 and 14c episodes (L17, L19, from gate .meta files); the 2026-11-10 stretch (L66, telemetry); the harness-port reds and episodes before 2026-11-15 other than H7 (L71, L72, L73, L74, L75, L76, L79, L81, L82, L83, L84, L85, L86, from gate/state via gate_times-all.out, by the definition of red-episodes.out); the two ledger delays (L77, L78); run 0097's read-only dispatches (L106, telemetry, after the fact base's cutoff).
- The gate-machine hours of the harness drive's reds were split by cause from the 48 .meta files; their sum, 25 h 13 min 08 s, matches the fact base, and so do the lint-only 13 h 13 min 37 s and the 120 s kills' 1 h 24 min 53 s.

## 9. Left out on purpose

- **Red-first reds made by design.** The harness drive's follower gated every pushed head, including heads whose new test was meant to be red: 8 gates with a milestone's own driver and 1 unit-test gate, 4 h 40 min 22 s of gate time. That is the method, not a set-back. Two more red-first gates were red for the wrong reason; they are in the unmeasured list, because they would have been red anyway.
- **hardening-1's follower reds on heads not declared READY** (75 gates, 23 h 03 min 32 s): measured, but the records cannot separate the reds made on purpose, so they are in the unmeasured list with their total.
- **Green gates, check-ins and reviews**: the normal cost of the process.
- **Planned restarts and gaps**: the loop restarts onto new builds (harness-port 26 s, 27 s and 36 s; hardening-2 8 s), the 3 min after hardening-2's shakedown run, and the 14 to 23 s between runs.
- **Pauses between drives**: hardening-2 has been paused since 2026-11-05 16:16 by the operator's decision; the gaps between one drive's end and the next one's start are not part of any drive.
- **The operator's and the supervisor's own time**: the records hold their words and times, not their hours.

## 10. Windows and time zones

- hardening-1: 2026-10-18 04:07:12 to 2026-10-23 20:05:43 (+0200). harness: 2026-10-23 20:16:55 +0200 to 2026-10-27 05:37:25 +0100 (daylight saving time ended on 2026-10-25). hardening-2: 2026-10-27 07:22:44 to 2026-11-05 16:16:14 (+0100). harness-port: from 2026-11-05 23:04:26 (+0100); the fact base runs to 2026-11-22 17:22:29 and the supervisor's log, read for this table, to its 19:32 entry; the last row ends at 19:44:52.
- Gate `.meta` files and some telemetry are UTC ("Z"); each row's `how_measured` keeps the zone of its record.
- `hours` is the exact span in `how_measured` divided by 3,600 and rounded half-up to two places. Spans were not rounded before that.
- Some unmeasured rows of the harness drive are dated after it (2026-11-13, 2026-11-20): the day the defect was found.

## 11. Sources

- The four drives' pages and fact bases: `../../hardening-2026-10-17/`, `../../harness-2026-10-23/`, `../../hardening-2026-10-27/` (each `pages/README.md` section 2, `pages/DATA_SHEET.md`, `pages/ERRATA.md`, `facts.md` and the outputs beside it), `../../harness-port-2026-11/facts.md` section 6 with its outputs, and the published harness-port pages in the drive's team tree (`docs/case-studies/harness-port-2026-11/README.md`, `DATA_SHEET.md`) with their internal appendix.
- Machine records read for this table, read-only and by name: the gate `.meta` files of the first three drives, the hardening-2 gate state file and push log, the harness-port harness.log, page-watch.out and gate_times-all.out, the telemetry of runs 0077, 0108 and 0143 (hardening-2) and 0032, 0096 and 0097 (harness-port), the supervisors' logs at the lines cited in each row, and two analyses in the harness-port team tree (`docs/analysis/2026-11-11-the-worker-that-went-in-circles.md`, `docs/analysis/2026-11-11-the-hang-that-was-a-loop.md`).
- The rows were computed by a small read-only script in the session's scratch directory, which is not kept; every row's `how_measured` carries the timestamps it rests on, so each row can be recomputed by hand.
