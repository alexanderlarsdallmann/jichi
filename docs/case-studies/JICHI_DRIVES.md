# jichi on jichi: four drives, and how long software takes

*A case study for self-learners and junior developers, and for anyone who plans work with a team of AI
agents. From 2026-10-18 to 2026-11-22, four drives in a row developed jichi itself. This page puts the four
side by side: how long each took, where the time went, and what stayed the same from one drive to the next.
Each drive has its own pages, linked in section 7. Every number on this page comes from the drives' own
records; section 8 says how it was produced.*

*The harness-port drive is still running. Its numbers on this page are as of 2026-11-22 17:22 +0100.*

---

## 0. What a drive is

A **drive** is a supervised run of jichi agents on one task list, for days.

1. **The lead** is a 27B model running inside jichi. It plans each piece of work, writes a task for a
   worker, checks what comes back, and commits. It works on a drive branch, never on master.
2. **The workers** are mostly a coding model, also running inside jichi. A worker that may edit gets its own
   git worktree, a second checkout of the repository in another directory.
3. **The harness** starts one lead run after another in a container. The next run starts from a handoff the
   last one wrote.
4. **The gate** runs the project's full `make ci` outside the team's container, on exactly the commit the
   lead marked `READY`. Only a green gate moves master, by a fast-forward. Moving master is a **landing**.
5. **The supervisor** is a second agent outside the drive. It reads the records, re-checks the team's
   claims, and sends notes.
6. **The operator**, jichi's maintainer, is a person. The operator decides the start, the stops, the order
   of the work, and anything that cannot be taken back.

## 1. The four drives at a glance

| | Hardening 1 | Harness | Hardening 2 | Harness port |
|---|---|---|---|---|
| Task | fix the findings of a review of jichi's own code | build jichi's own drive harness | continue the hardening, now run by that harness | port the host's gate and push scripts into the harness |
| Start | 2026-10-18 04:07 | 2026-10-23 20:16 | 2026-10-27 07:22 | 2026-11-05 23:04 |
| End | 2026-10-23 20:05, paused at a switch point | 2026-10-27 05:37, stopped | 2026-11-05 16:16, paused since | running |
| Wall clock | 136.0 h (5.67 days) | 82.3 h (3.43 days) | 224.9 h (9.37 days) | 402.3 h (16.76 days) so far |
| Runs of the lead | 76 | 20 | 130 | 96 |
| Milestone numbers | 82 (M960-M1047) | 12 (M1048-M1059) | 84 (M1060-M1143) | 67 (M1144-M1210) |
| Landings on master | 11 | 7 | 20 | 30 |
| Commits landed | 516 | 179 | 640 | 852 |
| Lines landed (net, all files) | +45,763 / -1,059 | +39,565 / -118 | +62,798 / -1,298 | +179,358 / -1,311 |
| Model calls of the team | 36,923 | 8,883 | 25,367 | 37,167 |
| Input tokens, newly processed | 112.0 M | 38.4 M | 103.8 M | 163.9 M |
| Input tokens served from the prompt cache | 3,611 M | 897 M | 2,227 M | 3,731 M |
| Output tokens | 19.8 M | 12.3 M | 27.8 M | 53.0 M |

Times are local: UTC+2 until 2026-10-25, UTC+1 after it.

**All four together:**
- 845.5 hours of drive time, or 35.2 days. The calendar from the first start to 2026-11-22 17:22 is 35.6
  days. The four drives ran one after another; the gaps between them add up to 8.75 hours.
- 322 lead runs, 68 landings, 2,187 commits and 108,340 model calls.
- 418.1 M newly processed input tokens, 10.47 billion input tokens served from the prompt cache, and 112.9 M
  output tokens.
- 245 milestone numbers.

**Read the lines with care.** They count every file, and the drives wrote a lot of records and documentation
beside the code. The harness-port drive's 179,358 added lines include its plans, its log, its worker reports and
the docs handovers it applied.

## 2. Where the time went

### 2.1 A landing about every twelve hours

Divide each drive's wall clock by its landings:

| Drive | hours per landing |
|---|---|
| Hardening 1 | 12.4 |
| Harness | 11.8 |
| Hardening 2 | 11.2 |
| Harness port | 13.4 |
| All four | 12.4 |

The four drives had different tasks, different sizes of batch, and different gates. Still, master moved
about twice a day, every day, for five weeks. A landing is one gated batch: a few milestones, tested,
reviewed and recorded. That is the unit to plan with, not the hour.

![Cumulative landings on master from 2026-10-18 to 2026-11-22: one step line, coloured by drive, rising to 68. It climbs steadily in all four drives; the flattest stretch is 11-09 to 11-13 in the harness-port drive, when a large batch and two pauses held master still. Grey bands mark the loop parked, stopped or paused.](charts/landings-cumulative.svg)

*Each step is one landing. Grey bands: the loop was parked for a model outage, stopped or paused. The table above holds the counts; [data/landings.tsv](data/landings.tsv) holds every landing.*

### 2.2 The lead spends half its time waiting for other agents

The lead's own telemetry says where its run time went. "Waiting" counts both pool workers
(`spawn_parallel`) and the reviewers and helpers it ran in its own process (`spawn_subagent`):

| Drive | waiting for other agents | of which pool workers |
|---|---|---|
| Hardening 1 | 45 % | 35 % |
| Harness | 65 % | 53 % |
| Hardening 2 | 50 % | 36 % |
| Harness port (runs 1-95) | 61 % | 53 % |

A dispatch blocks the lead until every task in it returns. While it waits, it can neither commit nor read a
note. So more workers do not make a drive proportionally faster: the slowest task of a dispatch sets the
pace.

### 2.3 The gate takes about an hour, and it got slower as the project grew

| Drive | a green `make ci`, median |
|---|---|
| Hardening 1 | 33.6 min |
| Harness | 35.0 min |
| Hardening 2 | 45.8 min |
| Harness port | 57.8 min (62.6 min from the gate's start to its verdict) |

Each drive added tests. By 2026-11-22 the smoke tier alone held 578 drivers with 3,778 checks. The full gate
also builds with two compilers, runs the sanitizers and valgrind, and builds for 32 bits. A red gate stops at
its first failing stage, so a red is quicker. That makes it cheap to learn that a batch is wrong, and costly to
learn that it is right.

**How often the gate said no.** Gate runs that reached a verdict on the heads the lead marked `READY`, or on
the merge that landed:

| Drive | gate runs | green | red | invalid |
|---|---|---|---|---|
| Hardening 1 | 24 | 14 | 9 | 1: the supervisor's own gate script |
| Harness | 8 | 8 | 0 | 0; one more was stopped before its verdict, its head already red by hand |
| Hardening 2 | 30 | 20 | 8 | 2: false reds of the gate's own new scripts |
| Harness port | 52 | 30 | 21 | 1: a false red of a gate script |
| **All four** | **114** | **72** | **38** | **4** |

So 42 of 114 gates on a ready batch came back red or invalid: plan for one in three, or more, to need a second
gate. In the harness-port drive, 18 of its 30 landings were green at their first attempt.

The harness and the first hardening drive also had a follower that gated every head the team pushed, not only
the `READY` ones: 88 and 143 gate runs in all. In the harness drive, the follower's 48 reds fell on pushed heads
the lead had not marked `READY`. One `READY` head was red: the supervisor found a documentation lint red by hand
and stopped that head's gate before its verdict. The follower also refused seven gates while the VPN was down.

How long a batch took from its first gate to its landing, re-gates included:

![A dot strip, one row per drive, one dot per landing, on a log scale of hours from 0.5 to 20. The medians are 0.7 h (hardening 1), 0.7 h (harness), 1.0 h (hardening 2) and 1.1 h (harness port). Each drive also has a tail of batches that needed several gates: the longest took about 11 hours in the first hardening drive and about 15 hours in the harness-port drive.](charts/first-gate-to-landing.svg)

*Most batches landed within about an hour of their first gate. The tail is the planning risk: a batch whose first gate is red can take half a day or more. Every value is in [data/landings.tsv](data/landings.tsv) (first_gate_start, landed_at).*

### 2.4 The largest losses were outside the code

The biggest single losses of each drive, in time:

| Drive | what | time lost |
|---|---|---|
| Hardening 1 | the model gateway refused connections | 4 h 01 min without a model |
| Harness | the model gateway failed; then the VPN failed | 4 h 05 min without a model; about 3.5 h without pushes |
| Hardening 2 | three gateway blocks; and the supervisor's session reached its usage limit | 9.6 h parked; 16 h 44 min without a push |
| Harness port | a batch needed four gates; then a commit without its trailers waited overnight for the operator | 14 h 38 min from the first gate to the landing; 9 h 18 min, 5 h 38 min of it waiting |

Model outages, a VPN, a reboot, a person who is asleep, and a supervisor who is not there are as real as any
defect. A plan that counts only the time to write and test the code will be wrong by days.

**All the measured losses.** The records put a time on 106 set-backs across the four drives. Their hours measure
two different things, so they are drawn apart. Inside each chart, a stretch of time is counted once per cause.

![Horizontal bars: work lost while the team, or part of it, stood still, by cause. Model outage 21.9 h, worker
failure 13.2 h, lead mistake 3.5 h, other 3.0 h, supervisor mistake 1.7 h, waiting for the operator 1.5 h,
network 0.7 h.](charts/lost-time-stopped.svg)

![Horizontal bars: delay to master while the team kept working, by cause. Gate red and re-gate 128.9 h,
supervisor absent 22.3 h, record slip 19.6 h, waiting for the operator 10.1 h, lead mistake 7.2 h, other 7.2 h,
gate-script false verdict 5.4 h, network 3.5 h, worker failure 2.9 h.](charts/lost-time-delay.svg)

- **Work lost:** 55.0 hours in all, each stretch counted once; 9.5 of them were holds the operator chose (a
  review, a release decision), which the chart leaves out.
- **Delay:** 198.0 hours in all, each stretch counted once. The largest cause by far is a red at the gate: 17
  reds held master back 128.9 hours, a mean of 7.6 hours each, the fix work included. Running the gate's own
  stages before marking a batch ready is the cheapest time a team can buy.
- **Machine time:** the gate host also spent 32.1 hours on gate runs that came back red or invalid.
- **Not measurable:** 71 more set-backs have no cost the records can measure. They are listed in
  [data/lost-time-unmeasured.tsv](data/lost-time-unmeasured.tsv), each with the reason.

### 2.5 Tokens

Every model call sends the whole conversation again: the system prompt, the tool list and the history so far.
The model gateway keeps recent prompts in a cache, so most of each prompt is served from it and only the new
part is processed. jichi records both numbers for every call (`in_tok` and `cache_read_in` in its telemetry).
On one run of the harness-port drive (88 calls) the two added up to the whole prompt: their sum was a median
1.23 times jichi's own estimate of the prompt's size, and the new part alone a median 2 % of it. The table
in section 1 gives both numbers, and does not add them.

- **Output** is what the models wrote: code, tests, records and tool calls. In every drive the workers wrote
  more of it than the lead: 11.0 M of 19.8 M, 9.2 M of 12.3 M, 17.2 M of 27.8 M and 37.7 M of 53.0 M.
- **The models were free to use.** All of them ran on the institution's own model gateway, so the tokens cost
  time, not money. On a priced service the same drives would have a bill; work it out from these counts and
  the service's price per token before you start.
- **Not counted here:** the independent reviewers' calls. They ran outside the team's runs, with their own
  records.

![A dot strip, one row per drive, one dot per landing: the million output tokens the models wrote between the previous landing and this one. The medians are 1.2 M, 1.0 M, 1.1 M and 1.1 M; single batches reached 5.6 M in the first hardening drive and 9.4 M in the harness-port drive.](charts/output-tokens-per-landing.svg)

*A typical batch cost about a million output tokens; a few cost five to nine times that. The window of each dot runs from the previous landing to this one, so it includes the work on later batches that began before this one landed.*

## 3. What the drives produced

### 3.1 Commits, by what they changed

Each commit is counted once, by the most significant area it touched: **code** if it changed `src/` or
`include/`; else **tests** if it changed `tests/`; else **public docs** if it changed `docs/` outside
`docs/internal/`, or a top-level page; else **records only** if it changed nothing but `docs/internal/`, where
the drives keep their plans, logs, notes and worker reports.

| Drive | code | tests | public docs | records only | other | all |
|---|---|---|---|---|---|---|
| Hardening 1 | 99 | 101 | 93 | 218 | 5 | 516 |
| Harness | 32 | 42 | 35 | 69 | 1 | 179 |
| Hardening 2 | 126 | 99 | 115 | 290 | 10 | 640 |
| Harness port | 143 | 145 | 122 | 434 | 8 | 852 |
| **All four** | **400 (18 %)** | **387 (18 %)** | **365 (17 %)** | **1,011 (46 %)** | **24 (1 %)** | **2,187** |

Fewer than one commit in five changed the program. Almost half changed nothing but the drive's own records.

### 3.2 Lines, by where they landed

Net lines from each drive's first master to its last (`git diff`), by area:

| Drive | code | tests | public docs | records | other |
|---|---|---|---|---|---|
| Hardening 1 | +5,857 / -685 | +12,808 / -190 | +3,759 / -110 | +23,304 / -72 | +35 / -2 |
| Harness | +11,563 / -25 | +6,096 / -35 | +2,165 / -31 | +19,492 / -0 | +249 / -27 |
| Hardening 2 | +7,575 / -848 | +14,595 / -200 | +1,215 / -148 | +39,041 / -64 | +372 / -38 |
| Harness port | +29,193 / -794 | +23,816 / -207 | +27,800 / -268 | +98,030 / -1 | +519 / -41 |
| **All four** | **+54,188 / -2,352** | **+57,315 / -632** | **+34,939 / -557** | **+179,867 / -137** | **+1,175 / -108** |

Of the 327,484 lines added, 17 % were code, 18 % tests, 11 % public documentation and 55 % records. The
drives added **more lines of tests than of code**. In the two hardening drives the tests outgrew the code
by about two to one (2.2 and 1.9 times).

**What this means.** A drive that is run so that others can check and learn from it produces mostly evidence:
tests that fail without their fix, a log of every decision, a report from every worker. The code is the
smaller part. If you count a project's progress in lines of code, you will see about one line in six of what was
added.

### 3.3 Which check caught each defect

217 defects were found and confirmed during the four drives: faults in jichi's code, its tests, its build, the
gate and harness scripts, or the records, each with a fix or a confirmed reproduction. Each is counted once, by
the check that caught it first. The review findings the hardening drives were asked to fix are their input, not
catches, and are not counted.

![Horizontal bars, one per kind of check, counting the defects it caught first: supervisor re-check 87,
independent review 38, make ci unit or smoke tier 17, first real use 15, make ci release, 32-bit or other stage
14, other 13, platform retest 13, shadow or switch-over replay 6, make ci sanitizers 5, make ci compilers 4, the
team's fast gate 2, a red-first test 2, the operator 1.](charts/defects-by-catcher.svg)

| Caught first by | Defects | Hardening 1 | Harness | Hardening 2 | Harness port |
|---|---|---|---|---|---|
| the supervisor re-running a claim | 87 | 30 | 12 | 24 | 21 |
| an independent review | 38 | 5 | 12 | 1 | 20 |
| `make ci`, all stages | 40 | 7 | 12 | 8 | 13 |
| first real use | 15 | 6 | 3 | 5 | 1 |
| a platform retest | 13 | 0 | 0 | 0 | 13 |
| a shadow run or switch-over replay | 6 | 0 | 0 | 0 | 6 |
| the team's fast gate or a red-first test | 4 | 0 | 0 | 1 | 3 |
| the operator | 1 | 1 | 0 | 0 | 0 |
| other | 13 | 1 | 1 | 1 | 10 |

**Read the counts with care.** They measure what the records describe, not how often each check fires. The
supervisor's log is the fullest record of the four drives, so the supervisor's catches are over-counted. The team
fixed what its own fast checks caught before it pushed, and nobody wrote those down, so the fast gate's real
share is much larger than 2.

**What each layer saw that the others could not**, one example each:

1. **A sanitizer.** A test case created two small apps and never freed them. Every test passed; only memory was
   lost, 16,480 bytes, and only the gate's leak-checking build saw it.
2. **A busy machine.** A race in the background-command spool passed its test seven times. At the next gate the
   machine was busy, the window opened, and the last line of output vanished.
3. **More than one compiler.** Four gate reds in the harness-port drive were errors only one stage's compiler or
   flags reported: the fortified optimised build, the C++ build, clang. The team built with one compiler.
4. **A reviewer.** A fix made jichi retry a broken stream, and the gate was green. A reviewer model in a fresh
   context saw that it also retried "this request is too long", so one call became four. The tests allowed zero
   retries, so no test could see a retry at all.
5. **Re-running a claim on the real binary.** A change turned secret redaction off when no search key was set.
   The team's drivers passed because `make test` does not rebuild the agent: they ran a binary from before the
   change. Re-run on the pushed commit, three went red.
6. **A shadow and a replay.** The ported gate watcher agreed with its script on 524 of 524 ticks. A replay of a
   moment no real event had reached, a gate already running, showed it would start a second `make ci`. Later, a
   ported re-pin's copies compared equal byte for byte and still lost their execute bit: the comparison read
   contents, not file modes.
7. **Another machine.** An argument list had no terminating NULL. On x86-64 the next slot happened to hold one, so
   every gate passed; on an aarch64 board the call failed without a word.
8. **First real use.** A credential helper's tests wrote the remote's path with a leading slash. Real git sends it
   without one, and the first real drive's pushes were refused.

Each layer caught something the layers before it had missed.

## 4. Planning figures

Divided per landing, and per hour of wall clock. A landing is one gated batch on master (section 2.1).

| Drive | hours | model calls | new input tokens | output tokens | commits | of them code |
|---|---|---|---|---|---|---|
| Hardening 1 | 12.4 | 3,357 | 10.2 M | 1.80 M | 46.9 | 9.0 |
| Harness | 11.8 | 1,269 | 5.5 M | 1.76 M | 25.6 | 4.6 |
| Hardening 2 | 11.2 | 1,268 | 5.2 M | 1.39 M | 32.0 | 6.3 |
| Harness port | 13.4 | 1,239 | 5.5 M | 1.77 M | 28.4 | 4.8 |
| **All four** | **12.4** | **1,593** | **6.1 M** | **1.66 M** | **32.2** | **5.9** |

Per hour of wall clock, over all four drives: 128 model calls and 134,000 output tokens. Per commit: 52,000
output tokens.

The first hardening drive is the outlier in calls: it made nearly three times the calls per landing of the
three drives after it. It also ran up to six workers at once, on an older harness; the records do not show
which of the two made the difference. From the second drive on, a landing took about
1,250 model calls, 5.2 to 5.5 M new input tokens and 1.4 to 1.8 M output tokens.

### 4.1 For an agent that plans a drive

Use these as starting estimates, then replace them with your own drive's first week:

1. One gated batch: about 12 hours of wall clock, about 1,250 model calls, about 1.7 M output tokens.
2. One full gate: 34 to 58 minutes; expect a second gate for one batch in three.
3. Half of the lead's time is spent waiting for its workers; the slowest task of a dispatch sets the pace.
4. About one commit in five changes code. Plan the tests, the records and the documentation as work, not as
   overhead: together they are four fifths of the commits.
5. Count outages and waiting for a person as part of the schedule. They cost hours in three of these four
   drives.

## 5. What stayed the same from drive to drive

1. **Each drive found defects in the tools that ran it.** The first hardening drive's gate scripts
   invalidated a gate. The harness drive's own code failed at its first real use. The second hardening
   drive's new gate scripts gave four false verdicts on their first day. The harness-port drive exists to
   replace those scripts, and its shadows found defects in the ports before any unit switched.
2. **The supervisor made mistakes too, and the records keep them.** A wrong note sent a worker into
   fixture sizing; that task ran 4.7 hours, and how much of it the note caused cannot be measured. A reboot checklist missed a push timer. A session reached its usage limit and left the
   drive without pushes for 16.7 hours. Each is told on its drive's page, with what changed afterwards.
   On 2026-11-23, the night after these pages were written, two more: an edit that used a variable its script
   never defines, caught before it was installed, and a stand-in remote that could not answer the port's new
   call, which failed one shadow tick. [Switching host units to the port](../analysis/2026-11-23-switching-units-to-the-port.md)
   tells both, step by step.
3. **Records drift unless a machine writes them.** Leads wrote log headings from memory, an hour or more
   off. Worker reports quoted numbers no run produced. The remedy each time: take times from `date` and
   from commit dates, and correct a record by a new dated line, never by editing the old one.
4. **A check that has never failed proves nothing.** A green came from an old binary (hardening 1). A
   sanitizer gap let three memory defects through every green gate (harness). A count of "0 disagreements"
   read nothing (harness port). Each was found only when someone undid a fix, or ran the check where it could
   fail, and watched it go red.
5. **The work is not done when the code is.** Every batch needed its records, its log, its documentation
   and its gate. The drives' commits are as much records and tests as code.

## 6. Planning your own project with these numbers

1. **Plan in landings, not in hours.** Here a gated, reviewed, recorded batch took about 12 hours of wall
   clock, with a 27B lead and coding-model workers. Measure your own first week before you promise dates.
2. **Budget the gate.** One full gate took 34 to 58 minutes. A third or more of the batches needed a second
   one.
3. **Budget the waiting.** Half of a lead's time went to waiting for its workers. A day has hours when the
   operator sleeps; a decision only the operator can make waits for morning.
4. **Budget the outages.** Three of the four drives lost hours to a model gateway that did not answer. Park
   the drive automatically, page a person after a set time, and resume by itself.
5. **Automate every hand-off first.** The push, the gate, the landing and the re-pin each run on a timer that
   survives a reboot. Then check after every reboot that each timer still fires.
6. **Keep the records as you go.** These pages could be written because every run, gate and note left a dated
   record. A drive without them leaves nothing to learn from.

## 7. The drives' own pages

| Drive | Pages |
|---|---|
| Hardening 1, 2026-10-17/23 | [what it taught](hardening-2026-10-17/README.md), [data sheet](hardening-2026-10-17/DATA_SHEET.md) |
| Harness, 2026-10-23/27 | [what it taught](harness-2026-10-23/README.md), [data sheet](harness-2026-10-23/DATA_SHEET.md) |
| Hardening 2, 2026-10-27 to 11-05 | [what it taught](hardening-2026-10-27/README.md), [data sheet](hardening-2026-10-27/DATA_SHEET.md) |
| Harness port, 2026-11-05 to today | [what it taught](harness-port-2026-11/README.md), [data sheet](harness-port-2026-11/DATA_SHEET.md), [the platform retests](harness-port-2026-11/PLATFORM_RETESTS.md) |

## 8. How these numbers were produced

1. For each drive, a read-only pass over its records built a fact base: every claim with its source, every
   number computed by a saved script. The records are the run journals and telemetry, the harness log, the
   gate's verdict files and logs, the drive's LOG, its DECISIONS and PLAN, the supervisor's log, and git.
2. A second pass wrote each drive's pages and re-derived every number it used by a different route, for
   example `grep` and `awk` where the first pass used Python. A number that could not be re-derived was left
   out, or the page says what it rests on.
3. The supervisor then re-counted the headline numbers by hand, a third route: the wall-clock spans, the
   runs and their exit codes, the commits landed, the gate results and the model calls of each drive. All
   matched. One time on a drive's page came from the supervisor's private notes, not from a record; the
   page now gives the window the records support.
4. The spans run from the first run's start to the last run's end, or to 2026-11-22 17:22:29 for the running
   drive. Lines are `git diff --shortstat` from the drive's first master to its last. Milestone numbers are
   the distinct `M<n>` numbers in the landed commit subjects within each drive's range.
5. Tokens are the sums of `in_tok`, `cache_read_in` and `out_tok` over every `model_call` event in the runs'
   telemetry, the lead's and its workers' (depth 0 and deeper), cut at 2026-11-22 17:22:29 for the running
   drive. The call counts from the same pass match the model calls in section 1.
6. Commits by area (section 3.1) come from `git log --name-only` over each drive's range, each commit
   classified once by the rule in that section. Lines by area (section 3.2) come from `git diff --numstat`
   between the drive's first and last master; their sums equal the totals in section 1. The planning figures
   (section 4) are those totals divided by the landings and the hours.
7. Lost time (section 2.4) comes from every set-back on the drives' pages and records that has a measured cost:
   two timestamps or a count. Each is classified once by cause and by what its hours measure. Spans of one
   cause that overlap are joined and counted once. Set-backs without a measurable cost are listed apart, never
   estimated.
8. Defects (section 3.3) are counted once each, by the check that caught them first, from the same pages and
   records. The counts describe what the records hold, as section 3.3 says.
9. The charts are drawn by [charts/make_charts.py](charts/make_charts.py) from the tables in [data/](data/),
   with the standard library only. Their colours were checked for colour-blind separation in light and dark
   mode. Every chart's numbers are also in a table, on the page or in [data/](data/).
