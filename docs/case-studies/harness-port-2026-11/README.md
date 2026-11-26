# The harness-port drive: what it taught about design, development and testing

*A case study for self-learners and junior developers. For ten days in November 2026 a team of jichi
agents worked on jichi itself, under a gate that ran the full `make ci` and a supervisor that checked in
every hour. This page tells what went wrong and what each mistake taught. The numbers are in the
[data sheet](DATA_SHEET.md). Why each release retests every platform is on
[the platform retests page](PLATFORM_RETESTS.md).*

---

## 0. The drive in one paragraph, and how to read this page

A 27B model led the team. It planned each milestone, briefed workers (mostly a coding model, each in its own
worktree), reviewed their diffs and marked a batch `READY`. A script outside the team gated each `READY`
head with the full `make ci` and landed it on master only when green. A second agent, the supervisor, read
the records every hour and answered with notes.

In 237.9 hours the team landed 49 milestones in 480 commits. The gate said no 15 times, and the supervisor
sent 174 notes into the live run.

Each lesson below has three parts:
- **What happened**, with the numbers from the record;
- **The rule** the project now follows;
- **Why it matters to you**, even if you never run a team of agents.

The lessons come from the project's lessons ledger, where each was written down the day it was learned.

## 1. Design

### 1.1 Everything that leaves the team's machine runs on a timer from day one

**What happened.** On the eve of this drive, the drive before it lost its supervisor's session for about 17
hours. The team kept working and made 46 commits, but none of them left the team's machine. Pushing,
gating and landing were steps that only the supervisor's session ran. The backlog grew to 81.7 MB, and the
remote's 20 MB push limit refused it. A landed build also went 22 hours without reaching the team, because re-pinning it was a
supervisor's step too.

**The rule.** The push, the gate, the landing, the re-pin and the delivery of every verdict each run on a
timer of their own. The supervisor reads and reports; no step waits for it. Porting those timers' scripts
into jichi's own harness was this drive's task.

**Why it matters to you.** Any process that needs you to be awake to make progress will stop when you are
not. Automate the hand-offs first, then the work.

### 1.2 Machines decide on structured lines, not on sentences

**What happened.** On the day this drive was set up, the gate of the drive before it decided "is this batch
withdrawn?" by reading the lead's prose. Its first rule withdrew a batch the lead had never withdrawn. Its fix passed six test cases, two of them real and four
invented, and then withdrew another batch the lead had meant to keep. The lead had written the sentence in a
form none of the invented cases used.

**The rule.**
- A decision a program makes reads a fixed line: `BATCH <name> READY at <commit>`, `BATCH <name> WITHDRAWN
  at <commit>`.
- Before you trust a classifier of text an agent writes, run it over every real instance you have.
  Invented cases come second.
- An ambiguous line does nothing.

**Why it matters to you.** Agents, like people, write the same thing a dozen ways. Give them one line to
write when a program must act on it.

### 1.3 A gate's verdict needs a class for "this is the gate's fault"

**What happened.** On the drive before this one, four false REDs came from the gate's own scripts in one
day. Each told the lead "Fix it, then write a new READY line". Twice the lead spent most of a run hunting
for a fault in a batch that had none.

**The rule.** A verdict caused by the gate's own tools is withdrawn by a dated note, and the head is
re-gated as it stands. Its message says "change nothing, wait". In this drive one of the 15 reds was such a
verdict. It was withdrawn within two and a half hours, and the lead was told not to touch its batch.

**Why it matters to you.** A test that blames the code for its own fault teaches people to distrust tests.
Make it easy to say "the test was wrong" and cheap to re-run.

### 1.4 A port that agrees with its original has been compared only where events went

**What happened.** The team ported the gate watcher from a shell script to C. For days both ran side by
side on the same inputs, and they agreed on 524 of 524 ticks. A replay of one state that no real event had
reached gave a different answer: the port would have started a second `make ci` while one was running.
- The cause was two leading spaces in `systemctl` output. The port's filter read them as an empty unit
  name.
- Separately, the port's command for starting a gate had never been run against the real `systemd-run`.
  Its tests used a stand-in, which accepted a form the real program refuses.

**The rule.**
- Before switching over, list the port's decision branches, find the ones no real event reached, and
  replay those.
- Run every external program the port calls once, for real, with harmless arguments.
- The switch-over happened only after both defects were fixed (M1178 and M1188). The port then started its
  first real gate exactly as the script would have.

**Why it matters to you.** "It has worked for a week" measures the week's inputs, not the code.

### 1.5 A value that can outgrow its type needs a rule the compiler will not give

**What happened.** jichi is C89, which has no 64-bit integer. A design review found five defects with one
cause.
- The worker pool's millisecond clock was narrowed to `long`. On a 32-bit machine that wraps after 24.86
  days of uptime, and a stuck worker's timeout then never fires.
- A kernel attribute was packed in one byte order, so it failed on big-endian machines.

The clock's fix (M1194) widens it to `double` and adds a lint that refuses the old narrowing.

**The rule.** Keep a time, a size or a count in a type that holds every value it can take, or check its
range before narrowing it. Test the wrap by setting the clock in a unit test, not by waiting 25 days.

**Why it matters to you.** Most of these bugs sleep for weeks. You find them by asking "how big can this
get?", not by running the program longer.

## 2. Development

### 2.1 Commit a worker's result before you probe it

**What happened.**
- One worker's merged result, about four hours of its work, sat uncommitted in the team's tree while the
  lead started a hang hunt in that tree. The worker's own copy had already been deleted, so the tree was
  the only copy.
- On the last day of this study the same pattern came back: another merge stayed uncommitted for an hour
  while a review read it. Both times a supervisor's note asked for the commit first.

**The rule.** Commit what a worker returned first: on a side branch if it is not green, as "review pending"
if it is. Then probe it, review it, change it.

**Why it matters to you.** The most expensive loss is the work you had already verified. A commit costs a
second.

### 2.2 A brief prescribes the shape of the result, never the result

**What happened.** A lead's brief told a worker to commit under the subject "... + the driver R1-R9 now
green". The worker had to use that subject and then explain in its report that the drivers were not green.

**The rule.** A brief says what to measure and how to report it. The verdict is the worker's to measure.

**Why it matters to you.** Writing the conclusion before the experiment is how wishful results get into a
record. Write "result: ..." and fill it in after.

### 2.3 Never put a timeout around a reproduction

**What happened.** Over 251 runs of this project's drives, agents wrapped 530 commands in `timeout` "so they
cannot hang". At least 27 of those caps fired.
- In one case the kill in cell R2 left the driver's temp directory broken. Cells R3 to R9 then "failed" on a
  missing file: seven failures that described the cap, not the code.
- In another, a worker ran a stage in the foreground that could hang. It waited two hours until a limit
  ended the call.

**The rule.**
- Run what might hang in the background and watch it: its processes, its output.
- Never cap a reproduction. If a cap fired anyway, read only what ran before it.

**Why it matters to you.** A timeout that fires does not hide the answer; it manufactures a different one
that looks just as real.

### 2.4 Watch a worker for change, not for activity

**What happened.** One worker ran 387 minutes. Its last code edit was at minute 154. It stayed busy to the
end, still making tool calls, but it was going in circles. The supervisor measured 880 workers of the
project's drives to find a signal. "Sixty minutes without a code edit after the first one" would have
flagged 7 of 563 workers, this one included, at minute 214. One worker was silent for 170 minutes and then
finished, so the signal reports and does not stop.

M1194 builds that detector: each worker reports its edits, and the lead flags the stale ones.

**The rule.** Judge progress by what changes (code, test results), not by how much the worker does. Report
first; stop only by a decision.

**Why it matters to you.** The same holds for you on a hard bug: if nothing has changed in an hour, change
the approach, not the effort.

### 2.5 A record says what was measured, when, and by whom

**What happened.** Several records in this drive were wrong in small ways, and each was caught.
- A log said three gate stages were "covered" by a run that runs none of them.
- A note said a milestone was done; no commit named it.
- A log heading carried a time 20 minutes in the future, typed instead of read from the clock.
- A follow-up written only in a commit message ("follow-ups") was found three weeks later as a defect on
  big-endian Linux.

**The rule.**
- Cite the run that measured each claim.
- Take times from `date`.
- Register a known gap the same day, in the register, and put its number in the commit.
- Correct a record with a dated line that names what it supersedes. Never rewrite it silently: someone may
  already have acted on it.

**Why it matters to you.** A project's records are how its future readers, including you, decide what to
trust.

## 3. Testing

### 3.1 A red-first test shows that a check can fail, never that it can pass

**What happened.** The team writes each test before its fix and watches it go red. A gate driver went red
at 65 of 93 checks, as it should. But two of its helper functions could never pass, whatever the code did,
and most later checks depended on them. The next worker spent four hours finding that out.

A related case: the gate's own red-first replay never built the harness binary it tested, so 20 of its 30
harness REDs were red for the wrong reason.

**The rule.** Watch each check fail without its fix, and also feed each new helper one real input it must
accept. Say which half you checked: "can fail" or "can pass".

**Why it matters to you.** A test that is always red proves as little as one that is always green.

### 3.2 Build a fixture from the real output, not from memory

**What happened.** Twice in one day a test's planted input differed from the real output in one detail, and
the defect lived in that detail.
- A planted log put a summary line on a bare line; the real log has it inside another line.
- A stub printed unit names flush left; the real `systemctl` indents them by two spaces.

The first let a rig report `7 checks, 0 failures` for a suite of 37,966 checks with 4 failures.

**The rule.** Run the real command once, save its bytes, plant those. Check leading whitespace and text in the
middle of a line.

**Why it matters to you.** Your memory of an output format is a guess. A captured output is a fact.

### 3.3 When a change alters a log, re-run every reader of that log

**What happened.** Two changes in one batch were each right. One made a test target continue after a failing
unit suite. The other made a rig read the first summary line instead of the last. Together they made the rig
read a later stage's summary as the unit result: a false green on the first board whose unit suite failed.

**The rule.** When you change what a tool prints, find every program that parses it, and run each on a real
log of the new shape.

### 3.4 Count failures from the runner's own verdict lines

**What happened.** The supervisor reported a board's smoke tier as "0 failures so far" for over an hour.
It counted lines that begin with `not ok`. The runner prints a failing driver's output behind a `    | `
prefix, so one driver had already failed unseen.

**The rule.** Count what the runner itself says (its `FAILED` lines, its summary), or read its format first.
A pattern that misses everything reads as "0 failures", the most expensive wrong answer.

**Why it matters to you.** Before you trust "nothing found", check that your search could have found
something.

### 3.5 Compile with every compiler the gate uses before you ask for a gate

**What happened.** Of the gate's 15 reds, 8 were defects in the batch, and 4 of those were compiler errors
that only one stage's flags revealed:
- a fortified `-O2` build, twice;
- the C++ build;
- clang.

The team's own fast gate used one compiler. Each red cost a gate attempt; a green attempt takes an hour.

**The rule.** Before `READY`, compile the new code under every compiler and flag set the gate uses.

### 3.6 Read the wrong bytes before you name a failure

**What happened.** A check failed on five boards with the same line, `out=[%nM]`. It was recorded three times
by its symptom: "an aarch64 item", then "the %n case on non-x86 ABIs", then a release's known issue. Then
someone decoded the stray `M`. It is 77, the value behind the `%n` argument. That found the cause in fifteen
minutes: a helper received a `va_list` by value, after which C89 leaves the caller's list indeterminate.
x86-64 happens to hide it. A 30-line program with no project code showed it on nine ABIs. The account is in
[`a va_list passed by value`](../../analysis/2026-11-13-a-va-list-passed-by-value.md).

**The rule.** Decode the wrong output, write the smallest program that shows the difference, run it on a
machine that passes and one that fails. Only then name the problem, by its cause.

### 3.7 Code that no configuration compiles is never tested

**What happened.** jichi carries its own `snprintf` for systems without `vsnprintf`. Every machine in the
test matrix has `vsnprintf`, so no binary contained the fallback. A defect in it stayed green on every
x86-64 gate for two weeks, until the first board runs.

**The rule.** Compile such a branch on purpose into the unit suite, under private names, so the cheap
emulated sweep runs it on every architecture.

## 4. Supervising

### 4.1 One snapshot cannot tell a storm from a hang

**What happened.** The supervisor looked at a process that never finished, saw no child process in a
snapshot, and told the lead "the reap hang is real". A worker then counted 197 distinct child processes in
about 8 seconds, each reaped cleanly. A loop that never advanced kept re-spawning the same child. The
supervisor withdrew its claim in a dated note.

**The rule.** Count over time before you name a mechanism: distinct process ids over seconds, not one frame.

### 4.2 "I did not find it" is not "it does not exist"

**What happened.** The supervisor told the operator there was no way to stop one worker. It had read the
general `--help`. The command (`stop-task`) had been built three days earlier and was in the running
binary.

**The rule.** Search the plan, the register and the source before you report something missing. Until
then, say "not found by these searches".

### 4.3 A note to an agent becomes the record

**What happened.** The supervisor asked the lead to correct a list without giving the list. The lead wrote a
plausible wrong one, and the batch was withdrawn before its gate. Another time a note described something in
words that must not appear in public text; the lead copied the words into a public record.

**The rule.** If a note asks for a fact, state the fact. Write it in words the record may carry. Expand
every path: the reader's shell does not know your variables.

### 4.4 Say who decides

**What happened.** An advisory reviewer rated a milestone WRONG. The lead held a green batch back to fix it
first, believing the review blocked the landing. It did not: the gate decides, the review advises, and the
operator chose to land and fix afterwards.

**The rule.** Every brief and prompt states who decides what. An advisory signal says "advisory" every time.

### 4.5 The supervisor makes mistakes too

Several of the lessons above are the supervisor's own mistakes: the hang that was a storm, the missing
command that existed, the "0 failures", a vacuous "0 disagreements" count, a wrong run number. Each was
corrected by a dated note before anyone acted on it, or as soon as it was found.

**The rule.** The supervisor's notes get the same checks as the team's work. A wrong note costs more than a
wrong commit, because the team acts on it.

## 5. A checklist for your own project

Whether you run a team of agents or work alone, these held throughout the drive:

1. Automate every hand-off before you depend on it.
2. Let programs decide on fixed lines, and test the decision on all the real cases you have.
3. Give your checks a way to say "the check itself was wrong".
4. Compare a rewrite with its original on the branches real use has not reached.
5. Ask "how big can this get?" for every time, size and count.
6. Commit before you probe. Write the result after the experiment.
7. Never put a timeout around a reproduction.
8. Judge progress by what changes.
9. Watch every test fail without its fix, and every helper pass on a real input.
10. Build fixtures from captured output, and re-run every reader when an output changes.
11. Compile with every compiler the gate uses.
12. Decode the wrong bytes before naming a failure.
13. Count over time before naming a mechanism, and search twice before saying "it does not exist".
14. Correct records with dated notes, never silently.

## Sources

- [`DATA_SHEET.md`](DATA_SHEET.md), the numbers;
- [`PLATFORM_RETESTS.md`](PLATFORM_RETESTS.md), the platforms;
- [`DRIVING_A_TEAM.md`](../../DRIVING_A_TEAM.md), what an earlier, longer drive taught about tasks,
  definitions and supervision;
- the drive's longer accounts:
  - [the gate's own false verdicts](../../analysis/2026-11-05-the-gates-own-false-verdicts.md);
  - [the worker that went in circles](../../analysis/2026-11-11-the-worker-that-went-in-circles.md);
  - [reviewing our own decisions](../../analysis/2026-11-11-reviewing-our-own-decisions.md);
  - [the platform retest](../../analysis/2026-11-12-platform-retest.md);
  - [three defects the review found](../../analysis/2026-11-13-three-defects-the-review-found.md).

## Correction, 2026-11-22: section 1.1's account of the push

*Added by the supervisor on 2026-11-22. This section withdraws one sentence of section 1.1 for the push; the
rest of section 1.1 stands.*

Section 1.1 says that before this drive "Pushing, gating and landing were steps that only the supervisor's
session ran." For **gating, landing and re-pinning** that is true. For **pushing** it is wrong:

1. The drive before this one had a push timer from its first day, 2026-10-27. It pushed every 20 minutes,
   without the supervisor.
2. Its last push ran on 2026-11-02 at 18:03. The development workstation rebooted at 18:16, and the timer
   did not start again. The supervisor's reboot checklist did not name it, and no later check noticed that it
   was gone.
3. From then on, the only pushes were the supervisor's own, at its check-ins. They hid the lost timer for 33
   hours. When the supervisor's session reached its usage limit on 2026-11-04 at 03:17, the pushes stopped
   with it. The 17 hours without a push that section 1.1 tells began there.

**The rule, made sharper.** Section 1.1's rule stands: every step that leaves the team's machine runs on a
timer of its own. Two more points:
- A timer that does not survive a reboot is not automation. Install each timer so that it starts again after
  a reboot, and after every reboot check that each one still fires.
- A second process doing the same job can hide the first one's death. Watch each timer's own output, not only
  the result both produce.

The second hardening drive's page tells this in full: [the second hardening drive](../hardening-2026-10-27/README.md).
The lessons ledger's entry of 2026-11-05 has a dated line to the same effect.
