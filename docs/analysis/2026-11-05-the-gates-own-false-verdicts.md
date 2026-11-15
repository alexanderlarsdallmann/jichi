# The gate's own false verdicts: four in one day, all from the supervisor's scripts

*Analysis, 2026-11-05, written at the operator's request: "Please, document this honestly, and comprehensively for
self-learners, and agents, too." It was written by the hardening drive's supervisor, a Claude Code session. The
mistakes it describes are that supervisor's own. Every time below is local time (CET, +0100). The times come from
the gate's state file, its watch log, its digest and the supervision log. Every number comes from those records or
from a re-run named beside it.*

## 1. In one paragraph

A drive of jichi agents works on jichi's own source. A gate on the drive's host decides whether the lead agent's work
may become `master`. On 2026-11-05 that gate gave four false verdicts in under seven hours: two false REDs and two false
withdrawals. All four came from small shell scripts the supervisor had written that night to run the gate without a
supervisor. None came from the code under test. Each false verdict stopped a batch the gate should have run, and the
delays added up to about two hours. Twice a false RED sent the lead agent off to "fix" a tree that had nothing wrong
with it. Two of the four came from fixes for the other two. Every fix had passed its tests. The tests used a few real
records and cases the supervisor had invented. None of them first collected every shape of sentence and output the
drive actually writes. This page tells the four in order, then what they have in common, then the rules that follow
for agents and for anyone who writes a gate.

## 2. The setup: what the gate does

The drive's lead agent works on a team branch. When a batch of work is finished, it writes one line in its log:

```
BATCH 12c READY at 9ada7cd7
```

A watcher runs every ten minutes on the host. It reads the **last** such line, then decides:

1. Has this commit already got a verdict in the gate's state file? Then do nothing.
2. Has a later sentence in the log **withdrawn** it? Then write "withdrawn" and do nothing. The lead's instructions
   say: "To withdraw a READY before its gate starts, write a line that names the commit and the word WITHDRAWN."
3. Otherwise start the gate on exactly that commit.

The gate runs, in order:

- a ledger check of every commit's author, committer and trailers;
- a **pre-gate sweep**: about 100 lint and documentation drivers, the unit suite under sanitizers and at `-O2`, two
  drivers that need a container, and `smoke-mutant`. That last check swaps the jichi binary for one that does nothing
  and expects every **changed** test driver to notice.
- a replay of each test-only commit, to see it go red before its fix;
- the full `make ci`;
- then a fast-forward of `master`, and a re-pin of the binary the next run uses.

Each verdict is written to the state file, to the lead's inbox (and into the running lead's session), and to a
digest the operator reads.

These scripts were new. They were written between about 01:00 and 03:30 that night, after the supervisor's session
had stopped for 16.5 hours the day before and nothing left the drive's container in that time. The point of them
was that the drive should no longer depend on that session. The main path was rehearsed on a real head before its
first use. The rare branches were not: the empty mutant, the withdrawal.

## 3. Timeline

| time | event |
|---|---|
| 07:12 | the gate starts on 832c7930 (batch 12b, part 2: a record and one comment) |
| 07:14:58 | **false RED 1**: "smoke-mutant missing or not clean". The mutant had swept nothing, because no driver changed |
| 07:47:41 | the supervisor withdraws the RED in a dated note to the lead; the lead had planned new work to give the gate a driver |
| 07:50 | fix 1: a zero-driver mutant is clean when git, counting a second time, finds no changed driver |
| 07:52 - 08:44:25 | 832c7930 re-gated, green, landed |
| 11:1x | the lead writes `BATCH 12c READY at b8409f7d` |
| 11:32:12 | **false withdrawal 1**: a later line names b8409f7d and says that a *different* commit "is WITHDRAWN" |
| 11:48 | fix 2: a withdrawing sentence must name no other `READY at <sha>` |
| 13:2x | the lead writes `BATCH 12c READY at 9ada7cd7`, the final candidate for release 0.13.0 |
| 13:32:12 | **false withdrawal 2**: the lead named the older commits as "submissions at", not "READY at", and fix 2 missed them |
| 13:47 | fix 3: a withdrawing sentence must name no other commit at all; a withdrawal is now also written to the digest |
| 13:52:12 | the gate starts on 9ada7cd7 |
| 13:55:09 | **false RED 2**: "it swept no driver, but git counts 1 changed smoke driver(s)". Fix 1 counted the wrong set |
| 14:47 | fix 4: the second count uses the mutant's own rule for what a driver is; the RED is withdrawn in a note to the lead |
| 14:52:12 | the gate starts again on 9ada7cd7; its sweep is green at 14:55:10 and `make ci` begins |

## 4. The four false verdicts

### 4.1 07:14: an empty sweep read as a dirty one

**What the gate said:** RED. "The pre-gate sweep: missing or not clean: smoke-mutant. Fix it, then write a new BATCH
READY line."

**What was true:** the batch changed a record and one comment. No test driver changed, so the mutant sweep had
nothing to do. It said so, correctly, and exited 0:

```
smoke-mutant rc=0 (MUTANT_BASE=77d2406f): mutant: no changed drivers to sweep (0 excluded)
```

**The cause:** the verdict script accepted only one line as clean, `mutant: OK`. Its author had not thought of the
case where there is nothing to sweep, so "nothing to sweep" fell into the else-branch, and the else-branch said "not
clean". This is the classifier mistake jichi's own rules warn about: *a classifier's else-branch must not be a
finding*.

**What it cost:** about 40 minutes of gate time. The lead (run 0176) found the cause correctly. Then it planned to
build a parked work item, D-11, so that the sweep would have a driver to check: new work, done to satisfy a gate,
and not needed by the code. The supervisor stopped that with a dated note. It withdrew the RED by name, said there
was nothing to fix, said not to start the item, and said not to write a new READY line.

**The fix:** a zero-driver line counts as clean only if a second, independent count agrees: git, in the gate's own
repository, finds no changed test driver in the batch. Any git error, an empty batch, or an excluded driver stays
red.

**How it was tested:** on eight cases. The real 832c7930 output came out green, as did the real 77d2406f output. Six
came out red: a batch that changes `stream_error.sh`, a base that is not a commit, an excluded driver, exit code 1,
an empty batch, and a rehearsal output with real lint failures.

**Why the test missed what came next:** see 4.4. None of the eight cases changed a driver that does not run the
binary.

### 4.2 11:32: a sentence about another commit withdrew this one

**What the gate said:** nothing, which is the problem. The watcher wrote `b8409f7d withdrawn` to its state file and
a line to its own log. It told neither the lead nor the operator.

**What was true:** nobody had withdrawn b8409f7d. The lead had written, in each of its pause checks:

> `BATCH 12c READY at b8409f7d` is the log's last READY line (line 9963). The earlier `BATCH 12c READY at 307de990`
> (line 9926) is WITHDRAWN (...). Per SV:0850, ... b8409f7d carries all of 12c's record ...

**The cause:** the watcher's withdrawal check was one regular expression over each later **line**:
`<commit>.*WITHDRAWN|WITHDRAWN.*<commit>`. A line that names this commit anywhere and the word WITHDRAWN anywhere
matched, even when the word was about another commit.

**What it cost:** no time in the end, by luck: b8409f7d needed one more commit anyway (§6). Had it been the final
commit, the drive would have waited for a verdict that never came, until a supervisor's check-in noticed.

**The fix (11:48):** read **sentences**, not lines, and let a sentence withdraw the commit only if it names no
*other* `READY at <sha>`.

**How it was tested:** on six cases. The real log came out "gated", where the old check said "withdrawn". A genuine
withdrawal of 307de990 from the real log came out "withdrawn". So did an explicit withdrawal, a withdrawal that also
names its base commit, and a withdrawal that names its successor. A sentence withdrawing another commit came out
"gated". All passed.

**Why the test missed what came next:** two cases were the real log and four were invented sentences. Every
invented sentence that named another commit named it as `READY at <sha>`, the form the fix looked for. Nobody had
collected the other ways the lead names a commit.

### 4.3 13:32: the fix of 4.2 was too narrow

**What the gate said:** again, `9ada7cd7 withdrawn`, and again only in the watcher's own files.

**What was true:** 9ada7cd7 was the final candidate for 0.13.0. The lead had written:

> The earlier submissions at 307de990 (WITHDRAWN, run 0188) and b8409f7d (...) will not be gated; the gate runs on
> 9ada7cd7 per the READY line.

**The cause:** the lead named the older commits as "submissions at", not "READY at". So the 11:48 rule saw only one
commit in the sentence, 9ada7cd7, together with the word WITHDRAWN.

**What it cost:** 20 minutes. The supervisor's hourly check-in at 13:45 found it. The gate started at 13:52.

**The fix (13:47):** a sentence withdraws a commit only if it names that commit and **no other commit in any form**.
A commit here means a token of 7 to 40 hex digits, bounded on both sides, with at least one letter a-f, so that a
date such as 20261105 is not one. An **ambiguous** sentence withdraws nothing. The reason is the asymmetry. A missed
withdrawal costs one gate run on a commit the lead can still replace with a new READY line. A false withdrawal
stalls the drive in silence. A withdrawal is now also written to the operator's digest and shown on the desktop, so
it cannot pass unseen.

**How it was tested:** on ten cases, and then on the **population**: every sentence in the lead's log that contains
WITHDRAWN. Only three distinct sentences name exactly one commit, and all three are the genuine withdrawal of
307de990.

### 4.4 13:55: the fix of 4.1 counted the wrong set

**What the gate said:** RED. "smoke-mutant (it swept no driver, but git counts 1 changed smoke driver(s) in 14
changed file(s) from 832c7930 to 9ada7cd7)". And again: "Fix it, then write a new BATCH READY line."

**What was true:** the batch changed one file under `tests/smoke/`: `snapshot_lint.sh`, and only one of its
comments. That driver is a lint over the source tree. It never runs the jichi binary. The mutant sweep exists to
catch a driver that stays green while the binary does nothing, so it sweeps only drivers that run the binary. Its
selector is:

```sh
uses_bin() {   # does this driver INVOKE the binary, not merely mention it?
    sed -e 's|#.*$||' "$1" | grep -q '"\$BIN"'
}
```

So "no changed drivers to sweep" was the true answer again. The 07:50 second count disagreed with it, because the
second count took every changed `tests/smoke/*.sh` file: the **directory**, not the **selector**.

**What it cost:** about an hour of gate time (13:55 to 14:52), and most of the lead's run 0198. The lead read the
driver's history, its references to the binary and the lint that guards the drivers, looking for a fault in its own
batch. The supervisor saw the RED only at the 14:45 check-in.

**The fix (14:47):** the second count now applies the mutant's own rule. A driver counts if it exists at the commit,
is not the tier's runner (`run.sh`) or helper (`_smoke.sh`), and runs `"$BIN"` outside comments. A git error while
reading a driver is red, never a skip.

**How it was tested:** ten cases, all passed. The real 9ada7cd7 output came out green; production had said red. The
real 832c7930 and 77d2406f outputs came out green. A range that changes `stream_error.sh`, which runs the binary,
came out red when nothing was swept. So did an excluded driver, exit code 1, a base that is not a commit, an empty
batch, a failed mutant, and a missing sanitizer line.

**The part that hurts most:** the supervisor had already met this exact mistake at 03:29 the same morning. The
red-first replay script had counted `tests/smoke/run.sh`, the tier's runner, as a driver, and replayed the whole
smoke tier as one "driver". It was fixed by excluding the runner and the helper, as the tier itself does. Four hours
later the supervisor wrote the 07:50 second count by directory again.

## 5. What the four have in common

1. **They live in the branches written for the unusual case.** These are the empty sweep and the withdrawal. The
   main path (sweep, `make ci`, land) was rehearsed on a real head before its first use, and in use it never gave a
   false verdict. The branches for the unusual case were tested only when they first fired, in production.
2. **Every fix was tested on a few real records and on cases the supervisor invented.** The 07:50 fix had eight
   cases, three of them real gate outputs. The 11:48 fix had six, four of them invented. Each passed. The case
   that broke each fix was one the writer of the input actually produced: a comment-only change to a lint driver,
   and a sentence that said "submissions at". **The population was
   there to read**: the lead's log, and the batch's own diff. It was read only after the fix had failed.
3. **A re-count must copy the selector, not the directory.** A second count is only a second opinion on the same
   question if it asks the same question. "Which files changed under `tests/smoke/`?" is not "which drivers would the
   mutant sweep?" The same mistake appeared three times in one day: the red-first replay at 03:29, the 07:50 second
   count, and the 13:55 RED that came from it.
4. **A false verdict costs most when it is silent or says "Fix it".** The silent withdrawals stopped the drive until
   a person looked. The two REDs said "Fix it". So the lead did what a lead should do with a RED: it looked for a
   fault in its own batch. The first time it planned new work to give the gate something to check. The second time
   it spent most of a run. At 07:50 the supervisor wrote down, for the harness that will replace these scripts, that
   a RED caused by the gate's own tools needs its own verdict class: one that tells the lead to wait, not to fix.
   Nothing was changed in the running gate, and the same cost came back at 13:55.
5. **A fix of a fix is the riskiest change of all.** It is written quickly, under pressure, and against the case
   that just failed. Two of the four false verdicts (4.3 and 4.4) came from earlier fixes that had passed their
   tests.

They also had one thing in their favour. Each script failed **closed**: a doubt became a RED or a withdrawal, never
a landing. Nothing wrong reached `master`. The cost was time and an agent's attention, and no landing was wrong.

## 6. The supervisor's other mistakes that day

- **01:40, the watcher's busy check counted the watcher itself.** It waited while any unit named `jhd-gate-*` ran,
  and the watcher's own timer and service match that name. Every READY would have waited forever. A dry run passed
  only because the timer did not exist yet. Found and fixed at 01:58, before any READY arrived.
- **02:30, the watcher saw a pushed commit 20 minutes late.** The push script read the remote's branches before the
  push, not after it. Fixed. The fix was not yet shown to fire on a real push, and the log says so.
- **09:47, a note that did not name the facts.** The supervisor asked the lead to correct a README stamp that lists
  the unit suite's builds. The note did not name the nine builds. The lead wrote a plausible list that was wrong
  (sanitizers as two builds; a leak checker that does not run the unit suite). The batch was withdrawn before its
  gate, and the next note named all nine. If a note asks for a fact, it should state that fact.
- **The pre-publication check missed four of twelve sites.** Before the 0.13.0 cut, the operator asked for the
  shipped pages to be read for a class of passages a standing rule of the operator's keeps out of public text (the
  release record calls them "gateway attributions"). The first pass searched line by line for keywords and found
  eight sites. A second pass found four more that the first had missed. It looked for the two kinds of words
  together in one **paragraph**, over every file. Then the supervisor's note described the
  class in plain words, the lead copied that description into the public release record, and a further note was
  needed to replace it with the class's name. **A note to an agent is a draft of the record**: write it in words the
  record may carry.
- **Reading through `| head`.** A `git diff --stat | head -30` cut the list of changed files, and two changed pages
  looked unchanged. A full re-read caught it. jichi's first rule says a `| head` is a cap. That holds for reading a
  result too, not only for measuring.
- **A wait that ended early.** A background wait for a gate's verdict matched the line systemd prints when it starts
  a unit. It should have matched the verdict line the gate writes. Watch for a signal from the program that produces
  it.

## 7. The team's side

- The lead diagnosed the 07:14 RED correctly, but its first plan was work to satisfy the gate. That is the natural
  answer to "Fix it", which is why the gate must not say "Fix it" when the fault is its own (§5.4).
- The 12b record landed one batch late. A READY line can never be inside the commit it names, so a record the lead
  wrote **beside** the READY line arrived only with the next batch. The rule since then: every record edit goes into
  the commit the READY line names.
- Removing one gateway attribution, the lead rewrote a summary line and stated a sequence that the page's own
  measurements contradict: a setting was described as on from a session where it had been measured on for only two
  of four deployments. Reading the rewrite against the measurements beside it caught it (see the release record,
  M1143-b).
- The lead acted on every note. It changed nothing when told to change nothing, and it never wrote a READY line it
  had been told not to write.

## 8. What worked

- **Withdraw, fix, re-gate, in that order.** Each false verdict was withdrawn in a dated note. The note named the
  verdict it superseded and told the lead what not to do: change nothing, write no new READY line. The gate's tool
  was fixed. The commit was re-gated **as it stood**, by one line in the state file (`<sha> regate <time>`).
- **Tests on the real records.** Every fix that held was tested on real gate outputs and the real log, as well as
  invented cases. From 13:47 on it was also tested on the whole population of the lead's WITHDRAWN sentences.
- **Failing closed.** See the end of §5.
- **An independent reviewer that was measured first.** At 04:13 the gate's advisory review, run on a model that had
  first been tested on a range of commits with two known flaws, rated a batch-12 fix WRONG. The supervisor reproduced
  it by effect: an error was retried 3 times where it should have been sent once. Because of that, 0.13.0 was not cut
  from batch 12, and batch 12b fixed it.

## 9. Rules for agents

Each rule names the incident it comes from.

1. **Test a classifier on the writer's real output before you trust it.** If a script reads what an agent or a tool
   writes, first collect every real instance of the shape it decides on (every WITHDRAWN sentence, every mutant
   summary line), and run the check over all of them. Invented cases come second. (4.2, 4.3)
2. **A re-count asks the tool's question, with the tool's selector.** Call the tool's own selector, or copy it with
   a comment naming its source. Never use the directory it happens to look in. (4.4; 03:29)
3. **Every branch of a verdict script is rehearsed before it can fire,** the rare ones most of all: the empty input,
   the withdrawal, the timeout. (4.1)
4. **The else-branch says "unknown", not "red".** A line the script does not recognise is a question for a person
   or a second count, not a finding. (4.1)
5. **Ambiguous input does nothing.** When the input does not clearly say "withdraw", do not withdraw. Choose the
   default whose failure is loud and cheap. (4.3)
6. **No silent verdicts.** Every verdict that stops work reaches the agent that is waiting and the operator, in the
   same tick. (4.2, 4.3)
7. **The gate's own fault has its own class.** A RED that comes from the gate's tools must not say "Fix it". Say
   "INVALID: the gate's fault; change nothing and wait". If you can only write the lesson down, write it into the
   running gate as well, not only into the plan for its successor. (4.1, 4.4)
8. **A fix of a fix gets the full treatment.** Real records, the population, and the case that first failed. (4.3,
   4.4)
9. **A note to an agent is a draft of the record.** State the facts you ask for, and use words the public record may
   carry. (§6)
10. **Read whole results.** No `| head` between you and a list you are judging. (§6)

## 10. For self-learners: checking your own gate, step by step

Each step is something you can do with a script you already have.

1. **List the verdicts your gate can give**, including the quiet ones: "skipped", "withdrawn", "nothing to do".
2. **For each verdict, find the code that gives it,** and ask: what input reaches this branch? If the answer is
   "anything I did not think of", that branch is a finding machine. Make it print "unknown" instead.
3. **Collect real inputs.** Save the real outputs of the tools your gate reads and the real lines your agents write.
   Keep them in a directory of fixtures.
4. **Run every verdict branch over the real inputs** and read the result for each one. Do not read only the count.
5. **Where your gate counts something a tool also counts,** compare the two counts on the same real input. If they
   disagree, find out why before you trust either.
6. **Make every stopping verdict loud:** a line in a file a person reads, a desktop notice, or a message in the
   agent's inbox.
7. **When your gate is wrong,** withdraw the verdict in writing, name what it supersedes, fix the tool, add the
   failing input to your fixtures, and re-run the gate on the same commit. Do not ask the agent to change its work
   to suit your gate.

If you get stuck at step 2 because the script is too long to follow, that is a finding too: split it until each
verdict has one short function.

## 11. What the harness that replaces these scripts must do

The scripts are the reference for jichi-harness's watcher, gate and landing, which are planned and not built. From
this day:

- **A structured withdrawal line**, `BATCH <n> WITHDRAWN at <commit>`, read like the READY line, instead of
  free-text sentences.
- **An INVALID verdict class** for faults in the gate's own tools, delivered with "change nothing, wait".
- **Every verdict delivered**, withdrawal included, to the lead and to the operator.
- **Selectors shared, not copied**: the gate asks the test tier which drivers the mutant would sweep (for example a
  listing mode of the sweep script) instead of keeping its own copy of the rule.
- **A fixture set of real records** for every verdict branch, run on every change to the gate.
- **A re-gate verb** for the supervisor, recorded with its reason.

## 12. Sources

- The gate's state file, watch log and digest for 2026-11-05, and the gate directories for 832c7930 and 9ada7cd7:
  `run.log`, `sweep.txt`.
- The supervision log, entries 07:50, 11:48, 13:48 and 14:48 of 2026-11-05 (and 01:58, 02:30, 03:29 for §6).
- The lead's log, `docs/internal/drive/2026-10-17-hardening/LOG-3.md`, lines 9926-10115.
- The supervisor's notes to the lead: SV:0747, SV:0850, SV:0947, SV:0954, SV:1041, SV:1147, SV:1246, SV:1446.
- The release record: ROADMAP M1143, M1143-b and M1143-c.
- `scripts/mutant-sweep.sh` at 9ada7cd7, the `uses_bin()` selector.
- The host scripts after these fixes, with their test cases, are versioned with the drive's internal records.
