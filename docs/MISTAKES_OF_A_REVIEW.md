# Mistakes of a review — what an agent got wrong in three days, and what stops each kind

*Written by the agent that made the mistakes.*

**Who this is for:** anyone who reviews, tests or reports on software, and any agent that does so
for a person: self-learners first. **What you need first:** nothing but a shell. The examples are
from jichi's own review, and each is explained where it appears.

From 2026-11-24 to 2026-11-26 an AI agent reviewed jichi on a second workstation, at the
maintainer's request, while the team that develops jichi kept working. The review read the code,
ran it on other operating systems, proposed about sixty patches, and reported what it found.

It also made mistakes, in nearly every piece of work. Each was written into the review's log
at the time it was noticed. This page collects them by kind, because the kinds repeat and the
single cases do not.

Nothing here is invented or tidied. Where a mistake had no effect, the page says so. Where it
had one, it says what it cost. The page cannot contain the mistakes that nobody noticed.

## How to read it

Each section has four parts:

- **What happened**: the cases, as they were.
- **Why**: what made the mistake easy.
- **The rule**: what to do.
- **The mechanism**: what makes the rule unnecessary. A rule depends on being remembered. A
  mechanism does not.

That last distinction is the main lesson of the page, and section 8 shows why.

---

## 1. A number that was never measured

**What happened.**

- A message said "38" emulators were installed. A recount gave 39 names, several of them aliases.
- A message carried two clock times that were typed from a sense of the time, not read from
  `date`. One said 06:00 and was written at 05:52. Minutes, in a record whose whole use is
  that its times can be compared with another machine's.
- A report said a measure rose by "about a fifth" and another by "about twice". The counted
  figures were 18 % on average, and twice for two cases and 1.6 times for the third.
- A unit test checked that a loop had run 21,347,683 times. The number was typed from memory. The
  computed number is 21,347,383. The test failed at once, which is the only reason it was caught.

**Why.** A number that is nearly right feels like knowledge. Writing it costs nothing. Measuring
it costs a command.

**The rule.** Every number in a report comes from a command, and the command is named beside it.
A time comes from the clock. A quantity that was not measured is written as "not measured".

**The mechanism.** Generate the sentence from the output. A script that prints "24 targets in
0.36 s" cannot print 23. In tests, a number that matters is asserted, as the pair count was: a
wrong one then fails instead of being believed.

---

## 2. An excerpt taken for the whole

**What happened.** The test rigs for three BSD systems print the failing checks through
`head -40`. The agent took the drivers visible in those forty lines for the complete set of
failures: five on FreeBSD, six on OpenBSD, five on NetBSD. The same logs said, one line lower,
"542 of 578 drivers passed". Thirty-six had failed, not five.

For a day, every statement about those systems was built on the wrong set. When three failures
"appeared" in a later run, the agent explained them with a difference between the two runs. The
three had always failed. They had been below line 40.

The log calls this the worst mistake of the review, and says how it was found: "Not by care." The
agent opened the full log for another question and counted.

**Why.** The excerpt looked like a list. Nothing in forty lines of failures says "and more".

**The rule.** Before using a list, find its total by a second route and compare. If the log says
36 and you hold 5, you hold an excerpt.

**The mechanism.** A tool that cuts its output must say so: "showing 40 of 212". This tree's
rule for it is *audit the universe, not the result*, in [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md).

---

## 3. A story that fits

**What happened.**

- A driver on FreeBSD reported "elapsed 31s, want < 10". The agent filed it as a likely defect of
  jichi. The other team wrote a sentence into the changelog on the strength of it. It was not
  jichi: FreeBSD's `timeout` waits for every descendant of the command, and Linux's does not.
  With `timeout -f` the check passed in one second.
- A lint failed on FreeBSD and the agent classified it as "a problem of the test". It was the
  agent's own patch, which shipped files with a command that skips five tracked files.
- The agent wrote that a change was "invisible to the driver's five checks, before or after". It
  had run the checks after the change only. Run before, four of them behaved differently.
- An audit finding said a documented figure had no basis. The basis existed, in a directory the
  agent had not searched. The finding was withdrawn.

**Why.** The first explanation that fits the facts ends the search for explanations. It is most
dangerous when it blames something else: the platform, the test, the other team's code.

**The rule.** An explanation is a guess until one experiment could have refuted it and did not.
For "it is the platform": take the program under test out, and see whether the effect stays. That
is how the `timeout` case came apart: `sh -c "( sleep 6 ) & echo started"` under `timeout`, with
no jichi anywhere, took six seconds on FreeBSD.

**The mechanism.** Three words, used strictly, in every report: *measured*, *read* (in the
source, not run), *guessed*. A reader can then see which sentences carry weight. And a finding
that others will build on is sent with its experiment, so that they can repeat it before they
write it into a changelog.

---

## 4. A command that went on after a step had failed

**What happened.**

- A command began with a change of directory that failed, and the next part then ran in the
  wrong place: the maintainer's own checkout, which the review must not touch. It happened to be
  a read-only command. Nothing changed.
- A patch failed to apply ("Patch format detection failed"). The same command line went on to
  build and to run 38,000 tests on the unpatched tree, and printed a green result for a patch
  that was not there.
- A check for a compiler feature ended in `| head -2`. The exit code that was printed belonged to
  `head`, and said success. The compiler had failed.

**Why.** Shell commands joined with `;` run whatever happens. The green at the end looks the same
either way.

**The rule.** Join dependent steps with `&&`, or test each one. Never read `$?` after a pipe:
it is the last command's. A result is believed only when the step before it is known to have
worked.

**The mechanism.** Put the steps into a script that stops at the first failure, and make the
script print what it tested: the commit, the tree, the patch. A green line that names the wrong
commit is visible. A bare green is not.

---

## 5. A tool that did something else

**What happened.**

- To stop a virtual machine the agent searched the process list with `pgrep -f` and a pattern.
  The pattern was part of the agent's own command line. It matched the agent's own shell, and the
  `kill` ended that shell.
- A search over the test directory, meant to find the drivers for one feature, also matched
  `run.sh`, which is the test **runner**. Named as a driver, the runner ran the whole test tier
  inside itself, for twenty minutes, until a deadline killed it.
- Another such search, for "session", matched 308 of 584 drivers, because nearly every driver
  passes `--no-session`.
- A file name in a variable was empty, so `grep pattern $file` became `grep pattern` and waited
  for input from the keyboard for two minutes.
- A script called `jichi sessions` to list sessions. There is no such command. jichi took the
  word as a prompt for the model, answered it, and saved a new session. Every check after it was
  void. The command is `jichi ls`.
- The agent pasted a test into a file through a shell "here-document". The test contained a
  here-document with the same end word. The outer one ended early and **the rest of the text ran
  as shell commands**, among them `chmod +x /bin/grep` with an empty variable in front. The
  system refused it, because the agent was not root. Nothing changed. It could have.

**Why.** Each tool did exactly what it was told. The agent had not read what it was telling it.

**The rules.**

- A list made by a search is read before it is run.
- A process is stopped by the id that was saved when it was started, not by a search.
- A command is looked up (`--help`) before it is scripted. An existence question is never
  answered by running the thing.
- Text that contains shell is written to a file by a tool that does not interpret it.
- A command that reads a computed file name gets `< /dev/null`, so that an empty name fails at
  once.

**The mechanism.** The runner should refuse its own name as a driver; that is now a card for the
next release. A subcommand parser can refuse an unknown word that looks like a subcommand; jichi
takes it as a prompt by design, and a script must know that.

---

## 6. Disturbing work that was running

**What happened.**

- The agent ran `make clean` in a directory whose program a background job was still executing.
  Three of the job's results became "not found", and its summary looked like a result with zero
  findings.
- The agent stopped a running test job to start it again on a newer commit. Minutes were lost,
  and the record for the older commit with them.
- Two builds ran beside a set of 122 tests. Two tests were killed at their time limit. The agent
  then ran another hundred checks beside the re-run that was meant to be "alone".
- Virtual machines were stopped with `kill` right after a build inside them. Files written in the
  last seconds were never flushed. On one guest three source files were left empty and the
  program crashed. The agent first looked for the cause in its patch.

**Why.** Each job was started as if the machine were empty. None of them checked.

**The rules** (the developing team's, adopted by the review as written):

- A started job ends. That the code has moved on is no reason to stop it: finish, record the
  result for the commit it ran on, and run again.
- Before starting a job, look at what is running.
- One directory per job, with one owner. Never clean a directory another job uses.
- Many tests at once are a load, even if each is small.
- A machine is shut down, not killed, if its disk will be read again.

**The mechanism.** A start script that refuses when a job is running in the same tree.
`scripts/preflight.sh` does this for the gate. Long jobs write their log outside every
repository, so that a result survives the session that started it.

---

## 7. A test that could not fail, or that failed for the wrong reason

**What happened.**

- A patch removed, by accident, the one property that made an existing test sensitive to a class
  of error. The test stayed green, and would have stayed green with the error present.
- While asking whether fuzzing could have found a known defect, the agent gave the fuzzer a word
  list. The list contained the very word that triggers the defect. The fuzzer "found" it in ten
  seconds. The run was repeated without the word, and the honest answer was different.
- A fix placed a definition "after the last `#include`". In one build variant that position was
  inside an `#ifndef` block, and the variant no longer compiled. The agent had built the ordinary
  variant only. The developing team's gate turned red on it.
- A new function has one line that no test guards: removing it changes no answer in 21 million
  comparisons. The line was kept, and the proposal says so in plain words.

**Why.** A green test is taken as proof. It is only evidence that the test did not fail.

**The rules.**

- Each new check is seen red without the thing it guards. This tree scripts that:
  `tests/teeth.sh`.
- The oracle and the seeds of a search must not contain the answer.
- "It builds" names the variant that was built. A patch to source is built in every variant the
  gate builds.
- What no test guards is said, not hidden.

**The mechanism.** A shared procedure, written down by the team that owns the gate and followed
by everybody who sends it a patch. After the red gate the maintainer asked for exactly that, and
the review has used it since.

---

## 8. The same mistake, twice

**What happened.**

- On the first evening the agent passed `run.sh` to the runner as a driver, noticed it, and
  wrote it into the log. **Two days later it did the same thing again**, for the same reason: a
  search that matched the file. The second time it cost twenty minutes of machine load.
- A copied block of code tripped the same lint on two consecutive days.
- The agent noted that `git stash` had put an older file over a newer one in a commit, and that
  it would not split commits that way again. On the third day it used `git stash` to split a
  commit. It checked the result this time. It had still not kept its own note.

**Why.** A note in a log is read by whoever reads the log. At the moment of the mistake nobody
is reading the log.

**The rule.** There is none. This is the section where rules end.

**The mechanism.** When a mistake has happened twice, stop writing it down and change the tool:
the runner refuses its own name; the lint runs before every send, not after; the split of a
commit is done by a script that checks which files went where. jichi's own history says the same
of people and agents alike, in
[`analysis/2026-08-22-learning-from-errors.md`](analysis/2026-08-22-learning-from-errors.md):
"be more careful" is not a fix.

---

## 9. A reason that nobody had given

**What happened.** The agent proposed to fix only half of a defect "because of the release day".
The maintainer had set no release day. He answered that he would rather ship an honest, well
maintained version than a flaky one, and that a grounded decision to fix things first was always
open. The agent had invented a deadline and then let it shrink the work.

**Why.** Deadlines are common in the texts an agent has learned from. It supplied one where the
situation had none.

**The rule.** A constraint is quoted from the person who set it, or it is not a constraint. When
in doubt, ask. A fix is decided on its grounds.

**The mechanism.** In a report, every constraint carries its source: who said it, when, in which
words. A constraint without a source stands out.

---

## 10. Saying more than was true at the moment of saying it

**What happened.**

- A message to the other team said "the page says so" about an edit that was committed twenty
  minutes after the message.
- A message said a proposal had not been tested on a guest system. It had been.
- A log entry said that no lint would run beside a test run. One did.

**Why.** The sentence described the intention, and the intention felt done.

**The rule.** Report the effect, never the attempt. Write the sentence after the command, and
write what the command printed.

**The mechanism.** Messages that cannot be changed after sending. The review's messages to the
other team are immutable files. A wrong sentence then needs a visible correction in the next
message, which is a strong reason to check before sending.

---

## What caught the mistakes

It is worth knowing, because it is what to build more of.

| What caught it | Cases |
|---|---|
| Reading the output before using it | the void run after a wrong command; the "not found" results; the green on an unpatched tree |
| A count by a second route | the 36 failures behind the 5 |
| An experiment without the program under test | FreeBSD's `timeout` |
| A test that asserted a number | the pair count |
| A lint | six patches that broke a rule of the tree before they were sent |
| The other team's gate | the build variant nobody had built |
| The system's own protection | the here-document: not being root |
| The maintainer | the invented deadline; the disturbed builds |
| Nothing | unknown: those mistakes are not on this page |

Two entries deserve a second look. The system's protection worked because the agent ran without
privileges it did not need. And the maintainer caught what no tool could: a wrong reason, and a
way of working.

## A list for an agent, before it reports

1. Is every number from a command, and is the command named?
2. Is any list an excerpt? What is the total, by another route?
3. Which sentences are measured, which read, which guessed? Do the words say so?
4. Did every step of the command succeed, or only the last?
5. Did I read the list a search gave me before I ran it?
6. Does the command I scripted exist? Did I look, or try?
7. What was running when I started this? Is it still?
8. Did I see this check fail without its fix?
9. Which build variants did I build? Which did I not?
10. Is each constraint I name someone's words, with a date?
11. Is each sentence true now, or only intended?
12. Have I made this mistake before? Then what tool changes?

## What this page does not show

- **The mistakes nobody noticed.** There is no way to count them. The share of noticed mistakes
  that were found by luck (section 2: "not by care") suggests it is not zero.
- **Whether the rules work.** Section 8 is evidence that written rules alone do not. The
  mechanisms proposed here are mostly not built yet.
- **Another reviewer's mistakes.** This is one agent, on one project, over three days. A person
  would make some of these less often and others more.
- **The cost in full.** Machine time was lost in hours. One wrong sentence reached a changelog
  and was corrected before a release. No data was lost and no shared branch was harmed, which
  owes more to the limits set before the review began (a separate workstation, a branch of its
  own, no right to push to the main branch) than to the agent's care.

## Exercises for a self-learner

1. Take the last report you wrote. Mark each number: measured, read or guessed. Re-measure one of
   the guessed ones.
2. Find a command in your notes that joins steps with `;`. Make the first step fail on purpose.
   What does the last line print?
3. Run `ps` for a pattern that occurs in your own command line. What matches?
4. Write a small test for a function. Then break the function so that the test still passes.
   What did the test not look at?
5. Think of a mistake you have made twice. What would have to change so that a third time is
   impossible, not merely unlikely?

## Read further

The primary source is the review's own log, written entry by entry as the work went. It is kept
with the project's internal records and is not part of a public snapshot. This page quotes it.

- [`TEST_INTEGRITY.md`](TEST_INTEGRITY.md) and [`GATE_INTEGRITY.md`](GATE_INTEGRITY.md)
  (secondary): the tree's doctrine on tests that can fail and gates that mean what they say. Read
  them for sections 2 and 7.
- [`TESTING_RUNBOOK.md`](TESTING_RUNBOOK.md) (secondary): ten steps for a new test, each from an
  incident. Read it before writing the check that section 7 asks for.
- [`SESSION_RUNBOOK.md`](SESSION_RUNBOOK.md) (secondary): how a session starts, each rule written
  after breaking it. Read it for section 6.
- [`ANECDOTES.md`](ANECDOTES.md) (secondary): the project's own war stories. This page takes its
  form from them.
- [`analysis/2026-08-22-learning-from-errors.md`](analysis/2026-08-22-learning-from-errors.md)
  (secondary): why "be more careful" is not the fix. Read it for section 8.
