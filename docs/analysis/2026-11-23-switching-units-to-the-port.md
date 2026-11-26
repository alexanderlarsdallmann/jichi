# Switching host units to the port: a worked example, and the two checks that missed

*An analysis for self-learners, junior developers and agents. On the night of 2026-11-23 the supervisor of the
harness-port drive switched one host unit from a shell script to its C89 port, and moved the shadow of a second
unit onto a new build. Both changes went in. On the way, the supervisor made two mistakes of its own: one
was caught before it could do harm, and one failed a shadow tick, harmlessly, before it was fixed. This page tells
both changes in order, step by step, with what each check could and could not see. Section 4 is a checklist for an
agent doing the same work. Times are local, UTC+1.*

---

## 0. The situation

**The drive.** A team of jichi agents develops jichi itself. Outside the team, on the workstation, a set of shell
scripts does the steps the team may not do itself: push its branch, gate a ready batch with the full `make ci`,
land it on master, re-pin the team's binaries to the new master. The drive's task is to port those scripts into
`jichi-harness`, jichi's own C89 program, one unit at a time.

**The rule for switching** (the drive's PLAN, section 6). After a batch lands, its port runs beside the script it
replaces, on the same host, on real events. A unit switches from the script to the port only when both agree on
real events. Until then the script is authoritative.

**Two ways to run a port beside a script.**
- A **replay**: after the script has done a real step, run the port on the same inputs in a scratch folder and
  compare everything both produced. Used for steps that happen once per landing, such as the re-pin.
- A **shadow**: run the port on its own schedule against a copy of the real state, pushing to a local stand-in
  for the real remote, and compare the results tick by tick. Used for steps that run all day, such as the push.

## 1. The re-pin unit: from two replays to the switch

### 1.1 The evidence

After each landing, the re-pin builds jichi and jichi-harness at the new master in a fresh clone, keeps a copy of
the running pair, swaps the new pair into the team's `bin/` by rename, and runs three doctors in the drive's
container. Four replays compared the port with the script:

| Replay | Real re-pin | Result |
|---|---|---|
| a | 0e301f54 -> 3a2dddec (2026-11-22) | **Disagreed.** The port's copies of the binaries had no execute bit (mode 0664; the script's are 0775), so all three doctors failed with "Permission denied". The port copied files with `fopen(..., "wb")`, which does not carry a file's mode. |
| b | the same | Agreed, with the doctors pointed at the script's executable copies: the port's other half was right. |
| c (1) | 3a2dddec -> 558ecac8 (2026-11-22 15:28) | **Agreed**, with the fix for the execute bit landed: every say line byte-identical, binaries and backup byte-identical, every copy executable. |
| c (2) | 558ecac8 -> a0a8b4ae (2026-11-23 01:36) | **Agreed** the same way. A one-line change to the port's output was caught by the comparison (a positive control). |

Two agreements on real events met the rule. The operator gave the word: "You may switch the re-pin to the port, if
you recommend it."

**What made the supervisor recommend it.** Beyond the two agreements, it read the one behaviour the script does not
have: the port stops any child process after 600 seconds. The source names the reason: the slowest child, a cold
build, was measured at 2 to 3 minutes. A bound three times the measured worst case, named in the code, is a fence,
not an arbitrary cap. It also read that every refusal (not master, no green gate record, already current) comes
before the port clones, builds, copies or swaps anything; a refusal writes only one line, to the re-pin log.

### 1.2 Testing the live wiring without changing anything

A replay runs the port in a scratch folder. The switch runs it on the real folders, with a new configuration file
and a new command line. Nothing so far had tested those.

The supervisor used a refusal as the test. It ran the exact live command on the current master, a0a8b4ae, which
was already pinned. The port must then read the real push repository, the real gate record and the real pin, and
refuse before writing anything. It redirected the one file a refusal writes, the re-pin log, to a scratch file,
and took a snapshot of everything the command could touch (the pins, the build folder, the backups, the team's
`bin/`, the current-pin link, the real re-pin log) before and after.

The port answered `repin: refused: pins/current is a0a8b4ae already`, the script's own words, and the two
snapshots were identical.

### 1.3 The mistake in the supervisor's own edit

The switch replaces one line of the gate's script with the port's command. The supervisor wrote the new block by
copying the shape of the doctor commands from the old re-pin script. That script defines `D`, the drive's folder.
The gate's script defines no `D`, and it runs under `set -u`, which stops the script on any unset variable.

So the first draft of the new block would have stopped the gate with "D: unbound variable", on the first real
re-pin, right after a green landing. The drive would have stayed on the old binaries with a gate error to explain.

**How it was caught.** Not by `sh -n`: a syntax check passed the defective draft, because using an unset variable
is not a syntax error. It was caught by listing the variables the gate's script defines before installing anything,
and finding no `D` among them.

**The test that would have caught it, and now does.** The supervisor took the exact block out of the new file and
ran it in `sh` under `set -u`, with the variables the gate's script defines before it, on the refusal path of
section 1.2. The corrected block refused correctly and changed nothing. Then it ran the same test on the defective
version: it stopped at once with "D: unbound variable". A test is worth keeping only after it has been seen to fail.

### 1.4 Installing, and what is still owed

- The new script was written to a separate file, checked, given the old file's mode, and moved over the old one
  in one rename, at a moment when no gate service was running. A gate that starts later reads the new script; no
  gate can read half of it.
- The old script was kept beside it, and the new block carries its own rollback: the old line, commented out,
  with the instruction to restore it only while no gate runs.
- **Still owed:** the first real re-pin through the port happens at the next landing. It is checked by effect
  then: the binaries in `bin/` equal the new pin's, the current-pin link, the backup, the three doctors, and the
  re-pin text in the gate's note.

## 2. The push shadow: moved, failed, fixed

### 2.1 What was moved, and why

Since 2026-11-09 the push port has run as a shadow: every 20 minutes, two minutes after the real push, it pushes a
copy of the team's branches to a local stand-in remote, and a comparison checks that the stand-in's tips equal the
real remote's. On 2026-11-20 the supervisor found that the port lacked one behaviour of the script: refreshing the
host's push repository from the real remote before each push. The team built it as milestone M1211, and it
landed on master at a0a8b4ae.

The shadow tests only the build it runs. With the operator's word ("Please, move it, now"), the supervisor moved the
shadow onto a0a8b4ae at 01:39, keeping the old setting as a file for a one-step return.

### 2.2 What was checked before the move, and what was not

The supervisor checked **safety**: every remote the shadow uses is the local stand-in, so M1211's new fetches
could not reach the real server. That was true.

It did not check **fidelity**: whether the stand-in could answer the new calls the way the real server does.

### 2.3 The failure

The first tick on the new build, at 01:57:13, failed:

```
push: the fetch of the remote's refs failed (rc=128): nothing pushed
```

M1211's first step fetches the remote's `master` and the drive's branches into the push repository. It is fatal
by design: the script also stops when it cannot read the remote. The real server always has a `master`. The
stand-in held only the drive's three branches; asked for `master`, it had nothing, and git stopped with exit code
128. The port behaved correctly. The stand-in did not behave like the real peer.

**The cost.** One shadow tick pushed nothing. The real push, which the script still does, ran normally two
minutes earlier. The shadow's page went only to a local record file, by design.

### 2.4 The information was already in the record

The team's own test for M1211 had met the same need, and handled it: its fixture gives the stand-in a `master`
branch before the first tick, with a comment saying why: "b1 fetches refs/heads/master explicitly. The real remote
has a master". The drive's log said the same in its entry on the red-first commit ("the standin's master branch before the
warm-up"), and the supervisor had read that entry the day before. The
team made its fixture behave like the real peer; the supervisor's own shadow, older than M1211, did not, and nobody
re-checked it when the code under test gained a new call to that peer.

### 2.5 The fix, and its check

The shadow's sync step, which runs before every tick, now first copies the real remote's `master`, as the host's
push repository last fetched it, into the stand-in. The real push repository is only read, as the source of a
fetch; a fetch runs no hooks in the stand-in. The supervisor ran the exact new lines under `set -u` (the lesson of
section 1.3, one hour old): the stand-in's `master` became a0a8b4ae, equal to the real one.

The next tick, at 02:17, recovered: it pushed the team's branch and records, wrote M1211's new line `push: team main
is da9da2fa (push.git's copy)`, and the stand-in's tips equal the real remote's. The shadow's push repository showed
the refresh by effect: its copy of the remote's `master` at a0a8b4ae and its team branch at da9da2fa. The day of
agreeing ticks the push switch needs counts from this tick.

## 3. What these teach

### 3.1 A safety check is not a fidelity check

**What happened.** Before moving the shadow, the supervisor made sure the new code could not reach the real
server. It did not make sure the stand-in could answer the new code the way the real server would. The first
tick failed on that gap.

**The rule.** When the code under test gains a call to an outside peer, check the stand-in against that call
before the first run: list the new calls (here: fetch `master` and the drive branches), and ask the stand-in each
one, read-only (`git ls-remote` on the stand-in, asking for `refs/heads/master`, would have printed nothing).

**Why it matters to you.** A test double that is safe but unfaithful produces failures that are not real, or hides
ones that are. Both waste the time you meant to save.

### 3.2 A stand-in ages with the code it serves

**What happened.** The shadow's stand-in was built in November for a port that never fetched `master`. It was right
then. It became wrong the moment the port gained the script's behaviour, and nothing in the shadow said so until a
tick failed.

**The rule.** When a milestone changes what a unit asks of the outside world, re-check every fixture, stand-in and
shadow that impersonates that world, not only the test the milestone adds.

**Why it matters to you.** Mocks and fixtures are code with no tests of their own. They drift silently.

### 3.3 A block moved between scripts carries the variables of the script it came from

**What happened.** A block copied from one script used a variable only that script defines. A syntax check passed
it; it would have failed on its first real run.

**The rule.** Before installing an edited script, run the exact edited block, taken from the new file, under the
target script's own options (`set -u`) and with the variables the target defines before it, on a path that
writes nothing or writes to scratch. Then run the same test on the defective version and watch it fail.

**Why it matters to you.** "It parses" is the weakest thing you can say about a script. Unset variables, wrong
paths and missing commands all parse.

### 3.4 Test live wiring through a refusal

**What happened.** The port's live command line and configuration were tested on the real folders without any
risk: the command was asked to do something it must refuse (re-pin to the version already pinned), and a snapshot
before and after showed it changed nothing.

**The rule.** When a command can refuse before it writes, use that refusal to test its wiring on the real
inputs. Redirect the one thing a refusal writes, and compare a snapshot of everything else.

**Why it matters to you.** A dry run tests the dry-run path. A refusal on the live inputs tests the reads, the
paths and the configuration the real run will use.

### 3.5 Switch at a quiet moment, in one rename, with the way back written down

**What happened.** The gate's script was replaced while no gate ran, by writing a new file and renaming it over the
old one, with the old file kept and the rollback written into the new one.

**The rule.** Never edit a script in place while something may run it. Write, check, rename. Keep the old version
and say, in the file, how to go back.

**Why it matters to you.** A shell reads a script while it runs. An in-place edit can hand it half of the old file
and half of the new one.

## 4. For an agent switching a unit from a script to its port

1. Confirm the switch rule is met: the port agreed with the script on real events, the number the plan asks for,
   with every defect a disagreement found fixed and re-checked on a later real event.
2. Read the port's behaviours that the script does not have (bounds, refusals, output format). For each, find the
   reason in the source; a bound needs a measurement behind it.
3. Write the live configuration and the exact live command. Test them on the real inputs through a refusal that
   writes nothing, with the one written file redirected and a snapshot of everything else compared.
4. Write the edit to a new file. List the variables the target script defines; run the exact edited block under
   the target's options on the refusal path; run the same test on a deliberately broken copy and see it fail.
5. Install only while nothing runs the target: same mode, one rename, the old file kept, the rollback written in.
6. Before a shadow runs new code: list every new call the code makes to the outside, and ask the stand-in each
   one, read-only. Make the stand-in answer as the real peer does before the first tick.
7. Read the first real run by effect, not by its exit code alone, and write down what is still owed.
8. Record each step with the time from `date`, your own mistakes included.

## 5. Timeline, 2026-11-23 (UTC+1)

| Time | What |
|---|---|
| 01:36:07 | The batch with M1211 lands on master at a0a8b4ae. 01:36:24 the script re-pins; doctors clean. |
| 01:36:52 - 01:37:09 | Replay c (2) of that re-pin: the port agrees. |
| 01:39 | The push shadow moves onto a0a8b4ae (the operator's word). |
| 01:44:28 | The re-pin port's live command, tested through the refusal: correct refusal, nothing changed. |
| before 01:45 | The first draft of the new gate block uses an unset `D`; found by listing the script's variables. |
| 01:45:22 | The exact corrected block, under `set -u`: correct refusal, nothing changed. The defective version fails. |
| 01:45:36 | The gate's re-pin step switched to the port (the operator's word). |
| 01:57:13 | The shadow's first tick on the new build fails: the stand-in has no `master`. |
| about 01:58 | The shadow's sync step now copies the real `master` into the stand-in; checked under `set -u`. |
| 02:17:23 | The next shadow tick recovers; the refresh is seen by effect. |

## Sources

- The switch rule: `docs/internal/drive/2026-11-05-harness-port/PLAN.md`, section 6.
- M1211's design and test: `docs/internal/drive/2026-11-05-harness-port/notes/M1211.md`;
  `tests/smoke/harness_push_tick.sh` (the stand-in's `master` before the warm-up tick).
- The port's code: `src/harness/jc_hp_tick.c` (the fetch of the remote's refs), `src/harness/jc_harness_repin.c`
  (the 600-second child bound and its measurement).
- The drive's case study: `docs/case-studies/harness-port-2026-11/` and `docs/case-studies/JICHI_DRIVES.md`.
- The replays, the gate's script and the shadow's scripts are host-side records; the supervisor's log of the drive
  holds each step with its time.
- The lessons ledger: `docs/internal/lessons/LEDGER.md`, entries 2026-11-23.01 to .03.
