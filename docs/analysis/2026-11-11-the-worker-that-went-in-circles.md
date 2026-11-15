# The worker that went in circles: four hours without progress, and why nobody could stop it

*Analysis, 2026-11-11, written at the operator's request: "document all of this honestly, and thoroughly
for self-learners, too!" It was written by the supervisor of the harness-port drive, a Claude Code session.
Some of the mistakes below are that supervisor's own, and they are told as such. Every time is local time
(CET, +0100). The times and counts come from the drive's telemetry, the team's commits, the workers'
reports and the supervision log. The numbers in §8 come from two scripts named in §13. The exercises in
§11 were run on Linux exactly as printed, before this page was written.*

## 1. Who this is for, and how to read it

This page is for you if you run a program that hands work to other programs and waits for them: a build
that starts jobs, a test runner, or, as here, an AI agent that leads other agents.

You need a shell. Each idea is explained when it first appears, and the glossary (§12) repeats the short
definitions.

- **For the story:** §2 to §4.
- **For the ideas:** §5.
- **For the mistakes, and what went right:** §6 and §7.
- **For the measurement:** §8 looks at 880 workers.
- **To see four of the mistakes on your own machine:** §11 takes about ten minutes.

A companion page, [reviewing our own decisions](2026-11-11-reviewing-our-own-decisions.md), tells
what the same day's design review found.

## 2. In one paragraph

A team of jichi agents was building a test gate in C. The lead agent gave one task to a worker agent and
waited. The worker made good progress for two and a half hours. Then it stopped making progress and kept
working for four more hours: it proved the same fault again and again, wrote the same helper script four
times, and its memory of the conversation was shortened every few minutes. Nothing stopped it, because
every safeguard looked for an exact repetition and the worker never repeated itself exactly. The lead
could not see any of this: a lead waits inside the call that started the worker. The supervisor (the
author) watched it for two hours and told the operator, wrongly, that there was no way to stop a single
worker. There was. Afterwards the operator asked what the records of all earlier runs say. This page
tells the story, the mistakes, and the answer.

## 3. The setting

- **jichi** is an AI coding agent. One jichi can lead others: the **lead** writes a task text (a
  **brief**) and starts **workers** with a tool called `spawn_parallel`. Each worker gets its own copy
  of the source tree and works alone.
- **The lead waits.** `spawn_parallel` returns when every worker has ended. Until then the lead does
  nothing and sees nothing.
- **The drive** is a long unattended run of such a team. Its **supervisor** is a separate session that
  looks in once an hour, reads the records, and writes notes to the lead. The operator is the human in
  charge.
- **The task** was the core of a test gate: a program that takes a finished batch of work, checks it in
  five stages, and writes a verdict. The team writes each test first and watches it fail, then writes the
  code (**red-first**). The test script planned 93 checks.

## 4. Timeline, 2026-11-11

| Time | What happened |
|---|---|
| 08:41 | The first worker's test script shows 65 of 93 checks passing and 28 failing, as planned for a script written before its code. The supervisor checks it and writes "the red-first claim holds". |
| 09:35 | The lead starts the second worker, Task B: write the gate's core. The brief expects 81 of 93 at the end. |
| 12:09 | The worker's last change to the source code. It is at 67 of 93. |
| 12:36 | The worker sends back a **placeholder** as if it were its own text (§5.3). It is the first of 11. |
| 13:01 | The worker finds out why it cannot get further: two faults in the *test script*. No correct program could pass those checks. |
| 13:38 | The supervisor confirms both faults independently and writes them to the lead. The lead cannot read notes while it waits. |
| 13:55 | The worker tries to make the *program* compensate for the broken test. It applies the change and undoes it two minutes later. |
| 14:49 | The supervisor builds the worker's tree in a scratch copy, fixes one fault at a time, and finds five more: one in the script, four in the program. |
| 14:11 to 15:17 | The worker proves the first script fault again, four times. |
| 15:36 | The supervisor reports to the operator that the worker is going in circles, and that it has found no way to stop one worker. The second part is wrong (§6.1). |
| 16:03 | jichi's own safeguard ends the worker: three copied placeholders in a row. The task ran 387 minutes. |
| 16:18 | The lead merges the worker's result as it is, at 67 of 93, and names every known fault in the commit message. It starts two fix tasks side by side. |
| 17:36 | Both fix tasks return. Together: 70 of 93. One fix is itself broken (§6.2). |
| 17:50 | The operator asks: "The lead must be able to identify broken workers, and stop them. Is that design, analysis, and implementation on the plan, already? What does telemetry data reveal?" |
| 19:05 | After the next round of fixes: 72 of 93. |

## 5. The ideas you need

### 5.1 Activity is not progress

A worker that runs commands is active. It makes **progress** only when something changes that brings the
task nearer its end: here, the source code. For four hours Task B was active and made no progress. Its
activity went down slowly, from 129 tool calls in its first hour to 8 in its last, and never reached
zero.

### 5.2 The context window, and compaction

A language model reads its whole conversation every time it answers, and the conversation has a maximum
length, the **context window**. When the conversation nears that length, jichi shortens it: it
summarises old parts and drops details. That is **compaction**. Without it the worker would simply stop.
But compaction can remove the very thing the worker just learned. Task B's context was compacted 6 times
before its last code change and 13 times after it, 7 of them in its last half hour.

### 5.3 Placeholders

To save room, jichi also removes the long arguments of old tool calls from the conversation and leaves a
short note in their place: a **placeholder**. Sometimes a model copies that note back as if it were the
arguments of a new call. jichi refuses such a call and says so. Three copies in a row end the worker.
Task B made 11 copies over three and a half hours, with other calls between most of them.

### 5.4 A safeguard that looks for exact repetition

jichi has four safeguards against a worker that loops. Each ends the worker when:

- the same call gives the same result nine times;
- the same refused call fails the same way again and again;
- the last 24 calls contain nothing the worker has not done before;
- three placeholders are copied in a row.

All four compare exactly. Task B's repeated checks differed each time by a temporary file name or a
timestamp, so none of them counted as a repetition.

### 5.5 Red-first, and its blind spot

Writing a test before the code has a well-known benefit: you see the test fail, so you know it can fail.
It has a blind spot. Before the code exists, a check that *will* pass and a check that *can never* pass
look the same. Both say "not ok".

The remedy is a **positive control**: give each new helper in a test one input that it must accept, and
watch it accept it.

## 6. What went wrong, by role

### 6.1 The supervisor (the author)

- **I checked that the tests fail, and wrote that the claim holds.** At 08:41 I confirmed 65 passing and
  28 failing, and the right 28. I did not check whether any of the 28 could ever pass. Two could not,
  because of faults in the script, and most of the later checks depended on them. Four hours of the
  worker's time went into finding that out. I corrected the scope of my note at 13:38.
- **I said there was no way to stop one worker.** At 15:36 I told the operator: "I haven't found how to
  stop a single pool worker; the control help only printed jichi's general options." I had asked the
  program's help, got the general page, and stopped looking.
  - The command existed. It had been built three days earlier for exactly this purpose, and it was in
    the running program: a `stop-task` request on the lead's control channel.
  - I found it two hours later, by searching the drive's own plan.
  - I would still have needed the operator's permission to use it. But I gave the operator a wrong
    picture of the choices.
  - *"I did not find it" is a statement about my search. It says nothing about the program.*
- **My first count of the records was wrong.** My first script grouped some workers together and counted
  799. After I fixed the grouping there were 881. It also counted scratch files as progress. I caught
  both before writing the numbers into a record. They were in my running commentary to the operator for
  a few minutes.
- **I proposed a change that an earlier decision had already rejected,** and wrote one false statement
  about what a worker can send while it waits for the model. Both were in a design note for the team.
  I found both on a second reading of the code and corrected them in a dated section. The companion page
  tells the first one.

### 6.2 The workers

- **Task B's worker found the real cause itself,** at 13:01, and wrote it down clearly. That part was
  good work.
- **Then it tried to fix the wrong side.** The test script was broken, and the worker's task was the
  program. So it wrote a change that made the *program* accept the script's broken input.
  - It wrote that change as a script four times.
  - It ran the first versions once, and undid the result two minutes later.
  - It never ran the later versions.
- **It did not stop and report.** Its brief said: "the same failure twice means stop and think; the third
  time, write the report with what you tried and saw, commit, and stop." The worker's failures were never
  the same twice, so the rule never applied to it.
- **A fix worker tested its helper and not the helper's use.** One of the two fix tasks replaced a broken
  list of options with a small function, `_gflags`. Its positive control checked that the function's
  words were right. But at all 17 places that should call the function, the script passed the *name* of
  the function as a word. The program received the word `_gflags` and none of the options. The count
  stayed at 70.

### 6.3 The lead

- **It could not see.** That is how `spawn_parallel` works, and it is not the lead's mistake. For six and
  a half hours the lead did nothing.
- **Its brief promised a number that the test could not reach.** The brief said 81 of 93. With the script
  as it was, the worker's program reached 67, and the script's faults blocked most of the rest.
- **Afterwards it did well** (§7).

### 6.4 jichi itself

- **Every loop safeguard needs an exact repetition** (§5.4). In all the records, the safeguards fired
  twice among 880 workers.
- **The lead gets no signal while it waits.** The worker tells its parent two things only: "I am starting
  a tool" and "I have used this many tokens". Neither says whether anything changed.
- **The ending was mislabelled.** The report line for Task B read `stop=max_iters`, which means "ran out
  of turns". It had been stopped for copying placeholders. A line further down said so.
- **A stop from outside does not bring the work home.** This was found the same evening, by reading the
  code:
  - A worker that ends *itself*, as Task B did, has its work merged into the lead's tree if the tree's
    checks pass.
  - A worker stopped *from outside* cannot send its last report. Its work is kept in a side reference,
    and it is not merged.

  So if the supervisor had used `stop-task` at 15:36, the lead would have had to fetch the work by hand.

## 7. What went right

- **The safeguard did fire.** It was late, but the worker ended on its own and its work was kept.
- **The lead merged honestly.** Its commit message said 67 of 93 and listed every fault that still
  blocked the rest. It did not round up.
- **The lead split the repair well.** There were two fix tasks, one for the script and one for the
  program. They touched different files and ran side by side.
- **The lead found the `_gflags` fault itself,** two minutes before the supervisor's note about it
  arrived, and later withdrew a wrong claim of its own in writing.
- **Building a scratch copy paid for itself.** The supervisor's most useful hour was the one spent
  building the worker's tree outside the drive, fixing one fault at a time, and counting which checks
  turned green: 67, then 68, then 70, then 71. That found five faults nobody had seen.
- **The lead took the measurement's result into its briefs the same evening** (§9).

## 8. What the records say: 880 workers

After the operator's question, the supervisor read the telemetry of every team run it could find: 254
runs in four drives over four weeks, with 880 workers and 363 worker-hours.

**How long the lead is blind.** Leads spent 310 of 563 hours inside the call that waits for workers. That
is 55%. On this drive it was 67%. Of 672 batches, 552 had a single worker, so the lead waited for each
of those from start to end.

**The ending tells you nothing.** Of 866 workers whose ending could be matched, 851 ended with "done".
Among them are workers that went in circles for an hour and then gave an answer.

**There are four kinds of broken worker,** and they look different:

| Kind | What it looks like | Example |
|---|---|---|
| Going in circles with a full context | tool calls fall slowly; no code change; many compactions; copied placeholders | Task B: 387 minutes |
| Blocked on a command | no calls at all for an hour | a worker waiting on a test stage that never finished |
| Crawling on a slow model | a few calls per hour; single answers that take 15 to 31 minutes | one worker: one code change in nearly five hours |
| Silent, then fine | no code change for a long time, then normal work | one worker: 170 minutes of silence, then it finished |

The last row is why no simple rule should stop a worker by default. Whether that worker's late work was
any good is not in the records.

**Two rules that would have flagged Task B early:**

| Rule | Workers it flags | It would have flagged Task B |
|---|---|---|
| 60 minutes without a code change, counted from the first code change | 7 of 563 that change code | 174 minutes before it was stopped |
| a second copied placeholder | 4 of 880 | 143 minutes before it was stopped |

Of the 7 workers the first rule flags, 4 never changed code again and 3 did. Of the 4 the second rule
flags, 3 ended at a limit or a safeguard, and 1 ended normally.

**A rule that sounds right and is wrong:** "60 minutes without a code change, counted from the start".
It flags 14 workers, and most of them were simply still reading: they made their first change a few
minutes later.

**What the records cannot say.** They do not record whether the lead kept a worker's result, redid it or
threw it away. So "this worker was broken" is a reading of its behaviour and never of its result. There
is also only one case with a full timeline, Task B.

### 8.1 The first live flag, the same evening

*Added at 20:55 the same day, from the drive's telemetry and the worker's tree.*

At 19:10 the lead started the next worker, to fix the two faults that were left. Its brief now carried
the two rules as a stop rule: "If 60 minutes pass after your first code change with no further code
change, or you have copied a placeholder (an elided/`<...>` argument) into a tool call twice, STOP and
write the report with what you did and where you are -- do not loop."

| Time | What happened |
|---|---|
| 19:12 to 19:13 | Nine code changes, all in one file. |
| 20:06 | Three test runs give 74 of 93, up from 72. The change worked. |
| 20:13 | 60 minutes without another code change. The first rule fires. The worker does not stop. |
| 20:13 to 20:42 | It keeps probing. It builds copies of the test's setup and runs them. It finds that one of the failing checks needs a part that belongs to a later task: the program answers that the part is "not built yet". Then it starts the next probe its brief asks for. |
| 20:37 | The supervisor's hourly check sees the flag and reads the worker's recent calls. It judges that the worker is investigating, not going in circles. It does nothing to the worker, and writes a note for the lead. |
| 20:42 | The worker changes code again, in a second file, 89 minutes after its previous change. |

**How the supervisor told the two apart.** A worker going in circles repeats itself: Task B proved the
same fault four times, wrote the same script four times and copied 11 placeholders, and its context was
compacted 13 times. This worker asked a new question at each step and found something it did not know
before. It copied no placeholder, and its context was compacted once.

**What it shows.**

- **A rule in a brief cannot count minutes for a model.** The worker did not stop. Some of its tool
  outputs carried the time of day. The record does not say whether the model failed to notice how long it
  had been, or noticed and judged that it was making progress. Either way, a rule that depends on elapsed
  time has to be checked by the program that runs the model, not written into the model's instructions.
- **The rule flagged a worker that was working.** This was the fourth kind in §8, silent and then fine,
  and it turned up within an hour of the rule being written. A stop at 20:13 would have prevented the
  second fix. That is why the design's default action is to report, and why someone reads the worker's
  calls before acting on a flag.
- **One case is not a rate.** Two flags so far, one true (Task B, measured afterwards) and one false (this
  one). Neither number is enough to set a threshold.

## 9. What changes

- **A design, not yet built.** The supervisor wrote a design for the team's agents:
  - the worker tells its parent, after every tool call, how many code changes, copied placeholders and
    compactions it has had;
  - the parent shows these and raises a flag by the two rules above;
  - the lead chooses, per batch, what a flag does: report only, ask the worker to wrap up, or stop it.

  The default is to report only, because of the fourth kind in §8.
- **Its place in the queue.** The operator placed it right after two smaller repairs.
- **At once, with no code.** The lead added the two rules to its briefs the same evening: a worker that
  goes 60 minutes after its first code change without another, or copies a placeholder twice, stops and
  reports. On its first live case the worker did not stop, and it was right not to (§8.1).
- **The label and the record.** The ending line should name a loop stop as one, and the pool should record
  whether each worker's result was merged. Both are in the design.

## 10. A short list to keep

1. Before you trust a failing test, show that it can pass.
2. Test a helper where it is used, not only what it contains.
3. "I did not find it" needs a second search before it becomes "it does not exist".
4. Watch for change, not for activity.
5. When two programs disagree, fix the one that is wrong. Do not teach the right one to accept the fault.
6. If you wait for something, make sure someone can still see it.
7. A rule about time belongs in the program that can see the clock, not in the instructions of the one that
   cannot.
8. A flag is a reason to look, not a reason to stop.

## 11. Try it yourself

All four exercises are small shell scripts. Work in an empty directory:

```sh
mkdir worker-exercises
cd worker-exercises
```

If anything goes wrong, close the terminal and delete the directory. Nothing here needs `sudo`.

### Exercise 1: a check that can never pass

This is the first fault in the test script, in small.

1. Save this as `check4.sh`:

```sh
#!/bin/sh
# A state line has five words: an id, a state, a date, a time and a time zone.
line='deadbeef red-setup 2026-11-11 11:34:56 +0100'

# The helper as it was first written: it wants exactly four words.
ok_as_written() {
    set -- $1
    [ $# -eq 4 ] || return 1
    return 0
}

# The helper as it should be: five words.
ok_fixed() {
    set -- $1
    [ $# -eq 5 ] || return 1
    return 0
}

ok_as_written "$line" && echo "as written: PASS" || echo "as written: FAIL"
ok_fixed "$line" && echo "fixed: PASS" || echo "fixed: FAIL"
```

2. Run it:

```sh
sh check4.sh
```

   You should see:

```
as written: FAIL
fixed: PASS
```

**What to notice:** before the program existed, the first helper said FAIL, and everybody expected FAIL.
It would have said FAIL for ever. One real line, fed in once, would have shown it.

### Exercise 2: quotes inside a variable are not quotes

This is the second fault.

1. Save this as `words.sh`:

```sh
#!/bin/sh
FLAGS='--name "two words" --count 1'

set -- $FLAGS
echo "from a variable: $# words"
for w in "$@"; do echo "  [$w]"; done

set -- --name "two words" --count 1
echo "written out: $# words"
for w in "$@"; do echo "  [$w]"; done
```

2. Run it:

```sh
sh words.sh
```

   You should see:

```
from a variable: 5 words
  [--name]
  ["two]
  [words"]
  [--count]
  [1]
written out: 4 words
  [--name]
  [two words]
  [--count]
  [1]
```

**What to notice:** the shell splits a variable at spaces and leaves the quote characters in the words.
In the real script, 12 intended words arrived as 46.

### Exercise 3: a wrapper that is never called

This is the fault in the fix.

1. Save this as `wrapper.sh`:

```sh
#!/bin/sh
run() { echo "run got $# words: $*"; }
with_flags() { run "$@" --build true; }

echo "the wrapper passed as a word:"
run job with_flags
echo "the wrapper called:"
with_flags job
```

2. Run it:

```sh
sh wrapper.sh
```

   You should see:

```
the wrapper passed as a word:
run got 2 words: job with_flags
the wrapper called:
run got 3 words: job --build true
```

**What to notice:** the function is correct. A test of the function alone passes. The mistake is at the
place of use, and only a run through that place shows it.

### Exercise 4: text where a file name belongs

This is one of the faults found in the scratch copy.

1. Save this as `textfile.sh`:

```sh
#!/bin/sh
note='the gate is RED today'

echo "the text given to grep as a file name:"
grep -q RED "$note" && echo "  found" || echo "  not found"

echo "the text given to grep on its input:"
printf '%s\n' "$note" | grep -q RED && echo "  found" || echo "  not found"
```

2. Run it:

```sh
sh textfile.sh
```

   You should see:

```
the text given to grep as a file name:
grep: the gate is RED today: No such file or directory
  not found
the text given to grep on its input:
  found
```

**What to notice:** `grep` did say what was wrong, on its error output. The real script had 29 checks
like this, and its error output had 20 such lines. Read the error output of a test run, even when the
counts look as expected.

## 12. Glossary

| Word | Meaning |
|---|---|
| lead, worker | An agent that hands out tasks; an agent that does one task in its own copy of the tree. |
| brief | The task text the lead writes for a worker. |
| `spawn_parallel` | The tool that starts workers and returns when all of them have ended. |
| supervisor | A separate session that reads the team's records and writes notes to the lead. |
| context window | The longest conversation a model can read at once. |
| compaction | jichi shortening a conversation that is nearly full. |
| placeholder | The short note jichi leaves where it removed an old tool call's arguments. |
| loop stop | A safeguard that ends a worker that repeats itself. |
| red-first | Writing a test before the code, and watching it fail. |
| positive control | An input a check must accept, used to show that the check can pass. |
| telemetry | The record jichi keeps of every model call and tool call. |
| progress | A change that brings the task nearer its end; here, a change to the source code. |

## 13. Sources

- **Telemetry:** the run records of four drives, 2026-10-17 to 2026-11-11; for the story, run 0037 of the
  harness-port drive.
- **The team's commits** on the drive's branch: bd60fb7d (the merge at 67 of 93), d5a26152 (the two fix
  briefs), 71d20b9d (70 of 93), 6aa15a61 (72 of 93) and f7d1a748 (the lead's log entry).
- **The workers' reports:** `reports/2026-11-11-M1159-B.md`, `-Bfix-drv.md` and `-Bfix-c.md` in the drive's
  internal records.
- **For §8.1:** the drive's telemetry of run 0037 (the worker of batch 7, from 19:10), the file times in its
  worktree, its brief as stored with the batch, and the supervisor's note SV:2037.
- **The supervisor's notes to the lead:** SV:0841, SV:1338, SV:1449, SV:1740, SV:1825, SV:1838 and SV:1909.
- **The supervision log** for 2026-11-11.
- **The design and its scripts:** `docs/internal/2026-11-11-lead-worker-supervision.md`,
  `docs/internal/2026-11-11-lead-worker-supervision-decisions.md` and
  `docs/internal/2026-11-11-worker-supervision/` (`scan.py` and `report.py` print the numbers of §8). The
  register row is `docs/DEFERRED.md` def-377.
- **The loop safeguards:** `src/chat/jc_agent.c` and `include/jc_toolloop.h` at 53a1ef4e.
- **The worker pool:** `src/tools/jc_tool_parallel.c` at 53a1ef4e.
