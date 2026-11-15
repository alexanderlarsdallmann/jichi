# The hang that was a loop: two hours of waiting, one wrong diagnosis, and what 251 runs say

*Analysis, 2026-11-11, written at the operator's request: "Document this honestly, and comprehensively for
self-learners, and junior developers, please." It was written by the harness-port drive's supervisor, a Claude Code
session. Some of the mistakes below are that supervisor's own, and they are told as such. Every time is local time
(CET, +0100) unless it says UTC. The times come from the drive's telemetry, the team's commits, the worker reports,
and the supervision log. The numbers come from those records or from the scripts named in §9. The exercises in §11
were run on Linux exactly as printed, before this page was written.*

## 1. Who this is for, and how to read it

This page is for you if you write or run programs that start other programs: a test script, a build, an agent that
uses a shell. You need a shell and a little C. Every idea is explained when it first appears, and the glossary (§12)
repeats the short definitions.

- **For the story:** §2 to §5.
- **For the ideas:** §6.
- **For the mistakes and what they teach:** §7 to §10.
- **To see the effects on your own machine:** §11 takes about ten minutes.

## 2. In one paragraph

A team of jichi agents was porting a test stage, `redfirst`, from shell into C. The new stage never finished. A
worker agent ran it in the foreground of its shell tool, so the worker could do nothing else until jichi's two-hour
command limit ended the call. The supervisor (the author of this page) looked at the stuck process with `ps`. It saw
no child process, and told the team and the operator that the stage was waiting for a child that had already
gone: "a reap hang". That was wrong. Another worker measured it properly an hour later. The stage was starting the
same child process again and again, about 25 times a second, because a loop never moved past the word it had just
read. The bug sat in the same file as a twin bug found earlier that evening. The supervisor withdrew its claim in
writing. In between, the operator asked for a study of all the drive telemetry: how often a foreground command could
not finish, and why. That study (§9) turned one bad evening into six design proposals for jichi, two of which were
queued at once.

## 3. The setting

- **A drive** is a long unattended session in which jichi works on its own source code.
  - **A lead agent** (a 27-billion-parameter open-weight model on the institution's own server) plans the work and
    writes briefs.
  - **Worker agents** (a coding model) each do one task in their own copy of the repository, called a worktree.
  - **A supervisor** checks the drive once an hour, sends notes to the lead's inbox, and reports to **the
    operator**, the human who owns the project.
- **The task was porting.** The drive ports a set of shell scripts (the "host scripts" that gate and land the team's
  work) into `jichi-harness`, a C program that ships with jichi. Batch H9b ports the gate's stages.
- **The stage.** One stage is `redfirst`. For every commit in a range, it checks that a new test fails before its
  fix and passes after it. To do that it starts a child process per check: build, run the test driver, read the
  result. That makes it a program that starts many programs.
- **The tool.** The workers drive the code through jichi's shell tool, `run_terminal_command`. A model asks for a
  command; jichi runs it and returns its output and exit status. While the command runs, the model waits. That is a
  **foreground** call. The tool can also start a command **in the background** (`run_in_background: true`) and
  return at once with a handle the model can read later.

## 4. Timeline

| Time | What happened |
|---|---|
| 15:27:51 | The lead sends two workers out: T9, the `redfirst` stage, and a small test task. |
| 19:07-19:17 | T9's worker runs at the edge of its context window. jichi compacts its context 19 times in 11 minutes, and 11 of those passes free nothing. |
| 19:18:56 | The batch returns after 3 h 51 min. T9's own report says **not done**: the new tests R1-R9 fail, and "the stage's per-check replay child hangs on this host". Its brief said to stop on the third such failure, and it did. |
| 19:20:36 | The lead commits the small task. T9's code is merged into the team's working copy but **not committed**. The worker's worktree has been deleted, so the working copy is the only copy. |
| 19:39 | The supervisor's note SV:1939: commit it somewhere other than `main` before you probe it. |
| 19:43:39 | The lead commits everything on a side branch, `wip/t9-redfirst-hang`. |
| 19:19-20:03 | The lead investigates. A small C program copying the stage's start-and-collect pattern works fine. It finds **bug 1** (a string comparison that never matched a placeholder followed by more text) and fixes it. It finds **bug 2**: the loop over commits never advances its cursor, so it re-parses the same commit forever and starts children in a "201-pid spawn storm". It leaves the one-line fix for the next run. |
| 20:05:48 | A new run starts. At 20:09:29 a worker (T9-b) is sent to add the one line. The brief prescribes a commit subject ending "+ the driver R1-R9 now green". |
| 20:21 | T9-b adds the line. The storm stops: the stage now ends in under 60 ms. But it reports "0 red-first commits" where it should report 1. A **third** defect is now visible: an early `break` skips the last commit of every range. |
| 20:32:45 | T9-b runs the stage on a fresh test repository (a range of three commits), **in the foreground, with no time limit of its own**. It never finishes. |
| 20:37-20:38 | The supervisor looks at the process with `ps` and `/proc`: no child, sleeping in a short timed wait, 7-8 s of CPU in 5 minutes, one per-check output file open. The control socket says the worker is live and has been idle for 336 s. |
| 20:38 | The supervisor asks the operator whether it may kill the stuck process. Killing a drive process needs the operator's word. No answer that hour. The supervisor asks again at 21:37. |
| 22:32:45 | jichi's command limit (`runTimeout`, 7200 s) ends the call with exit 124. **The worker waited two hours.** |
| 22:38-23:00 | T9-b carries on. It runs the test driver under `timeout 90`. The cap kills the driver in test R2, and R3-R9 then fail on a missing config file, an artifact of the kill. It reports honestly: the one line was needed but is not enough. It also writes that a single `ps` snapshot shows only one or two children of a storm, and that only a count of distinct process ids measures it. |
| 23:07-23:09 | The lead commits T9-b under an honest subject, not the pre-written "now green". It writes in its log that the "reap hang" idea is **contradicted** by what it measured in the earlier run. It sends T9-c to remove the early `break`, with the instruction "if R1 then hangs, re-instrument and stop". |
| about 23:05-23:31 | The operator asks the supervisor what to do about the worker. The supervisor recommends a note to the lead saying that **"the reap hang is real"**, and asks for a standing permission to kill a hung foreground stage test. The operator: "send the note; kill standing for this drive". SV:2331 goes out at 23:32. |
| from 23:33 | At the operator's request, the supervisor studies the telemetry of all 251 drive runs (§9) and writes a design note. |
| 23:57, 23:59 | The operator queues two of the proposals (B3 and B1, §10) to be built right after batch H9b. |
| 23:09-00:27 | T9-c removes the `break`. R1's check now runs, and the stage hangs again. As instructed, the worker instruments the stage instead of guessing. Section 5 has the details. |
| 00:34:02 | The lead commits T9-c: 62 of 64 driver tests pass. It runs the driver **in the background** and reads the result there. |
| 00:38-00:40 | The supervisor reads T9-c's report and **withdraws** its claim: SV:0038 to the lead, and a dated correction in its design note. |

## 5. What the stage was really doing

T9-c added three counters to the stage, printed while it ran, and removed them before committing:

1. **The loop over commits** ran exactly once for the one-commit range: correct.
2. **The loop over test drivers** printed `b=gd dp=gd`, `b=gd dp=gd`, `b=gd dp=gd`, ... It read the driver name `gd`
   and never moved its cursor `dp` past it.
3. **The collector of finished children** (`waitpid`) counted **197 different child process ids in about 8
   seconds**, each collected cleanly the first time it was asked.

So nothing was waiting for a lost child. The stage was starting the same check again and again, about 25 times a
second, and collecting each one correctly. The fix was one line, `dp += blen;`. It is the same mistake as bug 2,
which T9-b fixed with `tok += cplen;`: a hand-written loop over the words of a string that reads a word and never
steps past it.

Why did it look like waiting? The children were short-lived: 197 in about 8 seconds is about one every 40 ms,
including the parent's own work between them.

- **What the supervisor saw:** it looked with `ps`, which takes one picture. Between two children there is no child,
  so the picture shows none. The parent spends almost all its time in a short timed wait (it polls for the child
  every 20 ms), so the picture shows it sleeping.
- **What the worker saw:** T9-b, in its own run of the stage, caught a child that had exited but was not yet
  collected, a "zombie".
- **Both pictures were true, and neither was the story.** §11, exercise 3, shows this on your own machine.

## 6. The ideas you need

### 6.1 Foreground, background, and why `&` does not always mean background

Your shell connects a command's output to a **pipe**: a one-way channel with a writing end and a reading end. The
program that runs the command (here jichi) reads the pipe until **end of file (EOF)**. EOF arrives only when **every**
process that holds the writing end has closed it. Exiting closes it.

When you write `( long_job > log 2>&1; echo done >> log ) &`, the shell starts a subshell in the background. The
commands inside send their output to `log`. But the subshell itself still holds the pipe as its own standard output.
So the reader gets no EOF until the subshell ends, which is when `long_job` ends. The command looks backgrounded
and holds the call anyway. jichi's shell tool reads to EOF (`src/chat/jc_app.c:810`), so it waits too.

- **To let go of the pipe:** redirect the subshell itself, `( ... ) > /dev/null 2>&1 &`.
- **In an agent:** use the tool's real background mode.

Exercise 1 in §11 shows both.

### 6.2 Processes, process groups, zombies and reaping

- **The table:** every running program is a process with a number, its pid. A process that starts another is the
  child's parent.
- **Zombies:** when a child exits, the kernel keeps a small record of it until the parent asks for its exit status
  with `wait` or `waitpid`. Until then the child is a **zombie** (state `Z` in `ps`, shown as `<defunct>`). Asking
  is called **reaping**.
- **Two ways to fail at it:** a parent that forgets to reap leaves zombies behind. A parent that waits for a child
  it will never get back waits forever. That second failure is what the supervisor believed it saw.
- **Process groups:** a program can put its children in a **process group** so it can signal all of them at once.
  jichi does this for a drive's shell commands, so that its time limit can stop the whole pipeline with one
  `kill(-pgid, SIGTERM)`. That is also why the time limit kills a job you meant to keep running in the background
  (§9, F2).

### 6.3 A loop that never advances

Parsing by hand in C often looks like this:

```c
while (p[0] != '\0') {
    size_t len = strcspn(p, " ");   /* length of the next word */
    use_word(p, len);
    /* p += len;   <- forget this line and p never moves */
    while (p[0] == ' ') {
        p++;
    }
}
```

The skip-spaces step at the end looks like progress, but it moves nothing when the word is still under the cursor.
The loop then handles the same word forever. If handling a word means starting a process, you get a **spawn storm**:
endless short-lived children.

- **Why it hid twice:** a storm in the commit loop masked the early `break` after it. Then the `break` masked the
  storm in the driver loop. Each fix uncovered the next bug.
- **How to catch it:** every time round the loop, write down which part of the string you have consumed. Better
  still, use a tokenizer function that returns the rest of the string, so the advance cannot be forgotten.

Exercise 4 is a small, safe version.

### 6.4 One snapshot cannot measure a rate

`ps` shows what exists at one instant. A process that lives for 5 ms out of every 40 ms appears in about one
picture in eight, and many tools miss it more often than that, because they read the process table over several
milliseconds.

- **To learn whether something is happening many times, count distinct events over an interval:** distinct pids
  seen in many samples, or better, a counter inside the program that produces the events. T9-c's counter in the
  collector saw every child. Sampling from outside sees only some.
- **This is not special to processes.** It is why one log line, one CPU reading or one screenshot of a dashboard
  cannot tell you a rate.

### 6.5 Caps and fences

jichi's rules separate two kinds of limit.

- **A fence** bounds what a run may touch: which files it may edit, which commands it may run.
- **A cap** bounds how long or how much: a timeout, a token budget, a `| head`.

On a run that measures or reproduces something, caps stay **off**, because a cap that fires does not just hide the
answer: it manufactures a different one. T9-b's `timeout 90` killed the test driver in the middle of test R2. The
driver's temporary directory was left half built, so R3 to R9 failed on "config file not found". Seven failures that
described the timeout, not the code. Exercise 2 shows the effect in one line.

## 7. What went wrong, by role

This section is the honest part. Each item says what was done, why it was natural to do, and what it cost.

### 7.1 The supervisor (the author)

- **It named a mechanism from snapshots.**
  - **What it saw:** at 20:37 it read the stuck process with `ps` and `/proc`: no child, a short timed wait, an
    output file open.
  - **What it concluded:** the stage was waiting for a child that had gone. It said so to the operator in its
    recommendation, wrote it to the lead as "the reap hang is real" (SV:2331), and wrote it into a design note.
  - **What it already knew:** T9-b's report had said that a snapshot cannot measure a storm. The lead had written
    that its own earlier measurement contradicted the reap idea. The supervisor read both and still argued from its
    picture.
  - **Why it was natural:** the picture was vivid and specific (pid, wait channel, file descriptor). Specific
    evidence feels like strong evidence, but it was one frame.
  - **The cost:** a wrong note that the lead had to read (it arrived after T9-c had already been sent, so it changed
    nothing in the code), a withdrawn note, a correction in the design note, and one argument in that note
    rewritten (B2, §10).
  - **What it does now:** describe a stuck process by what was observed ("no child in three snapshots"), and name a
    mechanism only from an instrument.
- **It waited for permission while the cost ran.** Killing a drive process needs the operator's word, and the
  supervisor asked rather than acted. That was the right rule, but the question sat unanswered for nearly two hours
  while the limit ran down. The fix was a standing permission, narrowly worded (§8), and a design for a proper
  control verb (B3, §10).
- **Smaller slips, caught by its own checks:**
  - One of its study scripts was first run through `| head`, which can end a measurement early. It was stopped and
    re-run without it.
  - A register row was over the documentation's line-length limit.
  - A design note used a shell option name that jichi's documentation lint read as an unknown jichi flag.

### 7.2 The workers

- **T9's worker ran out of room.** For its last 11 minutes it worked at the edge of its context window. jichi tried to
  compact the context 19 times, and 11 of those passes freed nothing. It still committed and reported honestly. That
  is a product gap more than a worker's mistake (§9, F5).
- **T9-b ran a command it suspected of hanging in the foreground,** with no limit and no background mode. That cost
  two hours.
- **T9-b and T9-c capped the test driver** (`timeout 90`, `timeout 120`, `timeout 300`). The first cap produced seven
  false failures. Both workers said in their reports which results came from the cap, which is what saved the
  analysis.

### 7.3 The lead

- **It wrote the expected outcome into the brief.** The subject it prescribed for T9-b ended "+ the driver R1-R9 now
  green". The worker used it as told, and had to explain in its report that the claim was not true. Prescribe the
  shape of a commit subject, never its verdict.
- **It left almost four hours of a worker's merged work uncommitted while starting a hunt in the same files.** When told, it moved the
  work to a side branch within four minutes.

### 7.4 jichi itself

Four product gaps made the evening worse. Each is now a proposal (§10):

- A shell `&` holds the call open.
- No control verb ends only the current command. The verb that reaches a worker is applied after its command ends,
  and the verb that stops a worker ends the whole worker.
- A fired limit reads like an ordinary result.
- Compaction keeps trying when it can free nothing.

## 8. What went right

- **The block was visible.** jichi's control socket answers `status` while a command runs. It showed the worker
  live, inside `run_terminal_command`, idle for 336 s, and rising.
- **Honest reports.** Every worker said what it had not done. T9-b wrote that its prescribed commit subject was not
  realized, and that only a distinct-pid count measures a storm. T9-c wrote that its result was 62 of 64, not the 64
  of 64 its brief expected.
- **The lead's experiment.** Before guessing, the lead wrote a small C program that copied the stage's
  start-and-collect pattern. It worked, which pointed away from the collector. The lead's next brief then said
  exactly what to do if the hang came back: instrument, do not guess.
- **T9-c's instruments.** Three counters, one per loop and one in the collector, each answering one question. The
  answer took eight seconds to collect.
- **Corrections in writing.** The supervisor's wrong claim was withdrawn by a dated note that names what it
  withdraws, and the same correction went into the design note.
- **A narrow permission.** The standing kill covers only a foreground `gate run` stage test that has shown the hang
  signs for about 15 minutes. Never a worker, the lead or the loop, and each kill is reported.
- **Habits changed within the night.** The lead's next long run used the background mode and read its output when it
  was ready.

## 9. The telemetry study: how often does a foreground command not finish?

jichi writes one line of telemetry per model call and per tool call. The operator asked whether this case was rare.
The study read every telemetry file of the six drives so far: 251 runs, 92,987 tool calls, 50,279 of them shell
commands.

**Method, so you can judge it:**

- **Read every file.** Each file was opened by its exact name, never by a wildcard, because the run directories also
  contain named pipes that a wildcard read would block on.
- **Count by a second route.** The main count was checked with `grep` over the same files (50,282 lines; one run was
  still writing).
- **Classify before counting.** A timeout leaves the note `[stopped: command timed out after Ns and was terminated]`.
  118 lines contain that text, but 52 of them only quote it (an agent reading jichi's own source, for example), so
  the count is 66, not 118.
- **State what the data cannot show.** Telemetry records a command, its duration and its exit status, not process
  states. So "hung or just slow" is read from the command and its output, never measured.

**What it found:**

| | Finding | Numbers |
|---|---|---|
| F1 | Commands ended by jichi's own time limit | 66 calls, 13.7 h of agent time. 56 hit a limit the model chose for that call; 10 hit the configured `runTimeout`. |
| | ... of which full test-tier runs cut short | 16, cut at 600-2,000 s. The same tier, when allowed to finish, took 1,510-2,470 s. |
| F2 | A shell `&` held the call anyway | 7 calls of 10 min or more (3.7 h). Two lost their background job when the time limit killed the process group. |
| F3 | Long legitimate waits | Full test runs of 25-40 min, and loops that sleep and poll, look the same as a hang until they end. |
| F4 | Commands wrapped in `timeout` by the model | 530; at least 27 fired, judged by exit code. Pipelines hide the exit code, so there may be more. |
| F5 | Context compactions that freed nothing | 285 of 1,416 (20%), up to 61 in a row. |
| F6 | A control verb that ends only the current command | none exists |

The full study, with the scripts that produced every number, is the design note
`docs/internal/2026-11-10-blocked-foreground-calls.md` (internal; it names machine paths).

**Lessons from the method:**

- A count you have not cross-checked is a guess with a number attached.
- A text match is not an event until you have read what matched.

## 10. What changes in jichi

The study ends in six proposals. Each one says what to measure before building it, and which alternatives were
rejected and why. The operator queued two of them to be built first after batch H9b, before anything else that was
waiting:

- **B3, `jichi control <socket> cancel-cmd [task]`.** It ends only the current command of the lead, or of one worker,
  and the agent keeps working. It reads a clear note, `[stopped: cancelled by the supervisor]`, as the command's
  result. For a worker, a signal sets a flag that its command loop checks.
- **B1, return when the shell exits.** If the shell has finished but a background process still holds the output
  pipe, jichi returns instead of waiting, and says which job is still running. The design leans to handing that job
  to the background registry, so no output is lost.

Four more are written up for later review:

- **B2: show what a silent command is doing.** In `status` and in telemetry: the age of the last output, the
  children, and, since the correction, **how many distinct children were started per interval**. B2 never kills.
  Many short children with no output is a strong hint of a storm, but a silent parallel test run looks similar.
- **B4: say whose limit fired.** "The 900 s limit you set", or "the configured limit". When the model's own
  `timeout` wrapper fired, say that the missing output is not a finding.
- **B5: stop compacting when it frees nothing.** A worker that runs out of room should end its task with a clear
  "context exhausted" and a short state report, instead of grinding on.
- **B6: describe the `&` trap where the model chooses.** One sentence in the shell tool's description.

The bugs in the stage itself are the team's work in batch H9b. At the time of writing, two tests (R6 and R8) still
fail on a separate defect.

## 11. Try it yourself

These ran on Linux (GNU coreutils and procps) exactly as printed. Run them in a scratch directory. None of them
writes outside it. Exact timings and counts will differ on your machine.

**Exercise 1: an `&` that holds the pipe.** Each line starts a background `sleep 3` and pipes the output to `cat`,
which plays the part of jichi's reader.

```
start=$(date +%s); sh -c '( sleep 3 ) & echo started' | cat; echo "waited $(( $(date +%s) - start )) s"
start=$(date +%s); sh -c '( sleep 3 ) > /dev/null 2>&1 & echo started' | cat; echo "waited $(( $(date +%s) - start )) s"
```

The first prints `started` and `waited 3 s`. The background subshell held the pipe. The second prints `waited 0 s`:
the subshell's own output went to `/dev/null`, so the pipe closed when `sh` exited.

**Exercise 2: a cap that manufactures a result.**

```
timeout 1 sh -c 'echo step 1; sleep 2; echo step 2'; echo "exit=$?"
```

It prints `step 1` and `exit=124`. Read without the exit code, the script seems to stop after step 1. It did not: the
cap stopped it. Exit 124 means "the timeout fired", and everything after that point is missing, not failed.

**Exercise 3: one snapshot against a count.** Save this as `storm.sh` and run `bash storm.sh`. It starts a parent
that runs 20 short children, one after another, then looks at it once and then 20 times.

```
sh -c 'i=0; while [ $i -lt 20 ]; do sleep 0.2; i=$((i+1)); done' &
storm=$!
sleep 1
echo "one snapshot:"; ps -o pid=,stat=,comm= --ppid $storm
seen=$(for n in $(seq 1 20); do ps -o pid= --ppid $storm; sleep 0.05; done | sort -u | wc -l)
echo "distinct children in 20 samples: $seen"
wait $storm
```

On the machine this page was written on, the single snapshot listed **no child**, five runs out of five. The 20
samples found 7 or 8 different children.

- **That is the supervisor's mistake in miniature.** "No child" was a true picture of a parent that was starting
  children all the time.
- **Shorter children vanish entirely.** With `sleep 0.01` instead of `sleep 0.2`, even 20 samples found none, on
  every try, probably because `ps` reads the process table over several milliseconds and misses processes that
  short.
- **The lesson:** count from inside the program that starts them when you can, as T9-c did.

**Exercise 4: the loop that never advances.** Save as `loop.c`. It is C89, and bounded so that it cannot hang.

```c
/* loop.c - a token loop that forgets to advance (the T9 bug class),
 * bounded so that it cannot hang. */
#include <stdio.h>
#include <string.h>

static void walk(const char *list, int advance)
{
    const char *p = list;
    int rounds = 0;
    while (p[0] != '\0') {
        size_t len = strcspn(p, " ");
        printf("  token: %.*s\n", (int)len, p);
        if (advance) {
            p += len;            /* the line T9 was missing */
        }
        while (p[0] == ' ') {
            p++;
        }
        if (++rounds == 5) {
            printf("  ... stopped after 5 rounds: the loop never moved\n");
            return;
        }
    }
}

int main(void)
{
    printf("without the advance:\n");
    walk("gd ge", 0);
    printf("with the advance:\n");
    walk("gd ge", 1);
    return 0;
}
```

```
cc -std=c89 -pedantic -Wall -Wextra -o loop loop.c && ./loop
```

Without the advance it prints `token: gd` five times and stops itself. With it, `gd` then `ge`.

- **Try this:** make each round start a process (for example `system("true")`) and watch it with exercise 3's
  sampler. You have built a small spawn storm.
- **Try this too:** add a counter, as T9-c did, and compare its number with what `ps` shows.

### A checklist for your own work

1. **A command that might hang:** start it in the background, and watch its output and its children. Do not wrap it
   in a timeout if you need its answer.
2. **A cap that fired:** read only the results before it fired. Everything after belongs to the cap.
3. **A process that looks stuck:** write down what you saw, not what you think it means. Count events over time, or
   add a counter inside the program, before you name a mechanism.
4. **A loop you wrote by hand over a string:** check that every path through the body moves the cursor forward.
5. **A brief or a ticket:** never write the expected result into the commit message in advance.
6. **Merged work you are about to debug:** commit it first, on a side branch if it is not finished.
7. **A claim you made that turns out wrong:** withdraw it in writing, say what it replaces, and put the correction
   where the claim was.

## 12. Glossary

| Word | Meaning |
|---|---|
| foreground call | A tool call the agent waits for; it can do nothing else meanwhile. |
| background mode | The shell tool starts the command and returns a handle at once; the output goes to a file to read later. |
| pipe, EOF | A one-way channel between processes; end of file arrives when every writer has closed it. |
| process group | A set of processes that can be signalled together; jichi gives each shell command its own. |
| zombie | A child that has exited but whose exit status its parent has not collected yet. |
| reap | Collect a child's exit status with `wait`/`waitpid`, which removes the zombie. |
| spawn storm | A loop that starts short-lived processes over and over, usually because it never makes progress. |
| snapshot | One reading at one instant, such as one `ps`; it cannot show a rate. |
| cap / fence | A limit on duration or amount / a limit on what a run may touch; caps stay off for measurements. |
| `runTimeout` | jichi's configured upper limit for a shell command, 7200 s on this drive. |
| compaction | jichi shortening an agent's context when it is nearly full; "unrelieved" means it freed nothing. |
| brief | The task text the lead writes for a worker. |

## 13. Sources

- **Telemetry:** run records 0033 and 0034 of the harness-port drive (2026-11-10): tool calls, durations, exit
  status, compactions, and the control socket's `status` replies.
- **The worker reports:** `reports/2026-11-10-M1157-T9.md`, `-T9-b.md` and `-T9c.md` in the drive's internal
  records.
- **The team's commits:** a566b2b5, 42be8383, b9edb2de, d069a559, cfaa8b13, b0d8c1df, 0f5845d6, 4292948b and
  172eae6f, on the drive's side branch `wip/t9-redfirst-hang`.
- **The supervisor's notes to the lead:** SV:1939, SV:2331 (section 1 withdrawn), SV:2357, SV:2359 and SV:0038.
- **The supervision log** for 2026-11-10 and 2026-11-11.
- **The design note and its scan scripts:** `docs/internal/2026-11-10-blocked-foreground-calls.md` and
  `docs/internal/2026-11-10-blocked-calls/`. The register row is `docs/DEFERRED.md` def-376.
- **The shell runner:** `src/chat/jc_app.c` (`run_command_watched`) at 53a1ef4e.
- **The rules on caps and fences:** `CLAUDE.md`, "Caps versus fences".
