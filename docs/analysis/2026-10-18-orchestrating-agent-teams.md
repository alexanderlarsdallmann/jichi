# Orchestrating a team of agents — one day of jichi fixing itself, measured

*A record of the first day of the 2026-10-18 hardening drive, in which a team of jichi agents fixed
findings of the [2026-10-17 hardening review](2026-10-17-hardening-00-overview.md) in jichi's own
source, under a supervisor. It reports what the day measured about how the team was organised, what
went wrong and why, how another open-source agent (qwen-code) handles the same problems, and what was
changed. Written by Claude Opus 5.5, the drive's supervisor, on the operator's request: "document this
honestly, and comprehensively for self-learners, too." The supervisor's own mistakes are in it, in
section 4.*

---

## Who this is for, and how to use it

**For** a self-learner or junior developer who wants to see what happens when several AI coding
agents work on one code base at once — not in a demonstration, but over hours, on a real C program,
with every change gated by its tests. You need no knowledge of jichi. You should know what a git
commit and a branch are; the page explains worktrees, merges, "red-first" tests and the rest as it
goes, and the glossary at the end collects them.

**After reading this page you will know:**

- how a lead agent, its workers and a supervisor divide the work, and where that division blocks;
- how to measure whether parallel workers are really used (here: about a fifth of the time);
- eleven concrete failures from the day, each with the general lesson it teaches;
- how qwen-code, a larger agent, solves the same problems, and where jichi is already ahead;
- which changes were adopted, which were filed for later, and why;
- three exercises you can run in a minute to see the central traps for yourself.
- what the afternoon added (section 9): nine more lessons, a commit rule measured anew, and an honest
  answer to whether wider waves helped.
- what the night and the second morning added (section 10): seven gates, five of them red for five
  different reasons, twelve more lessons, and the first master move.

**How far to trust a statement.** Each claim carries a mark, as in the hardening review:
**[M]** measured — something was run and its output recorded; **[V]** verified by reading the code
path end to end; **[R]** read, but not traced to the end. For qwen-code, **[checked]** means the
supervisor re-read the cited lines; **[read]** means a research agent read them and the supervisor did
not re-check. The raw numbers are in the project's internal records (`docs/internal/2026-10-18-orchestration/`),
and the scripts, the research reports and the supervision log beside the other review records
(`docs/internal/reviews/2026-10-18-orchestration/`).

---

## 1. The setup, in plain words

A **drive** is a long, unattended session in which jichi works on a code base — here, on jichi
itself — while a supervisor watches. On 2026-10-18 the team worked through "batch 0" of a plan of
twelve batches (0 to 11), the first group of findings to fix.

```
 operator (a person)            decides: start, stop, what reaches master
     |
 supervisor (Claude Opus 5.5)   checks every 20 min; re-runs each claimed test; sends notes
     |                          (never edits the team's files)
 harness (shell scripts)        starts the lead again when a run ends; pushes; records
     |
 lead (jichi, model qwen3.8-27b)        plans, dispatches, verifies, commits
     |   spawn_parallel  (the lead WAITS here until every worker is done)
     +-- worker 1 (jichi, qwen3-coder-next)  in its own git worktree
     +-- worker 2 ...                        in its own git worktree
     +-- ... up to 6 at once
```

- **The lead** is a jichi process running the larger free model, `jlu/qwen3.8-27b`. It reads the
  plan, writes a task text for each piece of work, hands the tasks to workers, checks what comes back,
  and commits.
- **Workers** are jichi sub-agents running the free coding model `jlu/qwen3-coder-next`. A worker
  that may edit gets its own **git worktree**: a second checkout of the same repository in another
  directory, so workers cannot step on each other's files while they work. Read-only review tasks run
  on `jlu/qwen3.8-27b`. Both models are free; the drive's configuration names no other (the telemetry
  confirms it: 596 lead calls, 4,354 worker calls on the coding model, 132 on the larger one, by
  11:00) [M].
- **`spawn_parallel`** is the jichi tool that runs the workers. When they are all done it **merges**
  each worker's changes back into the lead's tree. If two workers changed the same file, the first
  one's change wins and the other's work is *kept* as a git reference, not lost, so it can be
  recovered. While the workers run, **the lead waits**: that one fact shapes most of this page.
- **The rules every fix follows** (from the plan): confirm the finding on the current code; write a
  short design note; write a test that **fails first** ("red-first"); write the fix and watch the test
  pass; run the fast gate (the unit suite at `-Werror`); write the learner-facing documentation; record
  it; commit — one commit per finding.
- **The supervisor** never edits the team's files. It reads the team's pushed commits from a mirror,
  re-runs each claimed test in a fresh clone (with the network off), and sends notes: an **inject**
  (a message the running lead reads at its next model call) or an **inbox** note (folded into the
  next run's prompt).

## 2. What the day measured

### 2.1 Throughput

From 04:07 to 11:40 the lead ran four times (each run ends when the lead decides it is done; the
harness then starts the next). By 11:40 the drive line held 27 commits: 23 of the team's fixes and
records, 2 of the team's unverified snapshots ("WIP"), and 2 of the supervisor's, made just before the
start. Seven fixes were verified by the supervisor on the pushed commits by then, and an eighth (O1)
at 12:16 — each test re-run, and each test shown to fail with its fix removed or disabled [M]:

| Finding | What it fixed | The test that fails without the fix |
|---|---|---|
| L-20 | a background command's output repeated up to 8 KiB at a size edge | 3 unit checks |
| L-21 + L-19 | jichi read "Never end a run with a dirty tree: commit the work…" as "do not commit", and "Never run `make ci` here" as "do not run build commands" | 3 unit checks |
| M968 | the same misreading of "Never run make ci here." written without backticks | 1 unit check |
| H9 | an injected note showed its sender twice | driver check 12 |
| D3 | a headless lead was not asked again after an empty answer | driver check 3 |
| M961 | the shell fence blocked background commands' output files | driver checks 1, 2, 3, 5 |
| M962 | a background command's output file on disk had no size cap | a unit check |
| O1 (12:16) | one `spawn_subagent` per request, listing the configured models | 5 unit checks; driver checks 2 and 3 |

The first three runs lasted 65 to 92 minutes and used 13.6 to 20.4 million tokens each; the fourth
had used 33.6 million after three hours and forty-five minutes [M]. All of it on free, locally hosted models.

### 2.2 How busy were the workers?

**The question.** Six worker slots were available. Were they used?

**The method.** jichi writes a telemetry line for every model call and every tool call, and each line
names the worker task (`task_run`) and the time. Divide each run into minutes; a worker is *busy* in a
minute if it made at least one call in that minute; count the busy workers per minute. [M]

| Run | Minutes | Minutes with **no** worker busy | Most at once | Worker-minutes used / available at 6 |
|---|---|---|---|---|
| 1 | 68 | 44 | 2 | 31 / 408 (8 %) |
| 2 | 92 | 48 | 6 | 155 / 552 (28 %) |
| 3 | 65 | 32 | 4 | 76 / 390 (19 %) |
| 4 (to 11:00) | 189 | 71 | 5 | 231 / 1,134 (20 %) |

**So: about a fifth of the capacity, and in roughly half of all minutes no worker ran at all.**

**What the measurement cannot see.** A worker that is inside a long command (a 2-minute test suite)
makes no calls during it and counts as idle, so the numbers are a slight undercount. A minute in which
one task ends and the next starts can show one more worker than there are slots (run 2 showed "7" in
two minutes; the table says 6).

**Why.** Three causes, in order of size:

1. **The lead waits while the workers work, and the workers wait while the lead checks.**
   `spawn_parallel` blocks: the lead can do nothing until every worker in the batch is done. Then the
   lead verifies — re-runs tests, removes a fix to see its test fail, checks the exact commit — and
   during all of that no worker runs. The empty minutes are those verification stretches. They are
   also where the day's most important catches came from (section 3).
2. **Batch 0's work was concentrated in a few files.** After the second run lost five of eight tasks
   to merge conflicts (section 3.1), the lead gave every file to exactly one task. With most of
   batch 0 in the same three or four files, that left room for one to three tasks at a time.
3. **Rework.** One finding (O1) took three worker attempts; another (M961) two or three. Each retry is
   a small batch with one task.

**The lesson.** "Six workers" is a capacity, not a speed. Utilisation depends on whether the person
(or agent) coordinating can keep working while the workers do — and on whether the work can be split
into pieces that do not touch the same files.

## 3. Eleven things that went wrong, and what each teaches

Each case says what happened, how it was found, and the general lesson. None of them is unusual; they
are the ordinary failures of parallel work, and seeing them in a real record is the point.

### 3.1 Shared files in one batch

**What happened.** In the second run the lead sent eight tasks at once. Three of them were told to
edit the same source file, three the same test registry (`tests/smoke/run.sh`, the list of test
drivers), two the same header — and **every** task text asked the worker to write a report file named
`REPORT.md` at the top of the tree. Result: five of eight tasks were kept unmerged as conflicts [M].
In the next batch, with the overlap reduced but not gone, the same task (D4-C) was kept unmerged
again, over its edit to `run.sh`.

**How it was found.** The supervisor compared the "files you own" lines of the task texts: the
conflicting files were exactly the ones named twice.

**The lesson.** Parallel work needs **disjoint write sets**: every file is owned by exactly one task
in a batch. Files that *everyone* touches — a registry, a list, a counter in the documentation, a
report — are the usual offenders; give them to one task per batch (or have the coordinator update them
after the merge). A scratch file every task writes is a guaranteed conflict.

### 3.2 A debugging edit that changed behaviour

**What happened.** A worker fixing the shell fence (M961) left six debugging log calls in the code —
one with `%zu`, a C99 format the project's C89 rules forbid — and, while adding them, wrapped an
existing block in `if (ev == NULL) { log } else { ... }`. `ev` is the telemetry record, which is NULL
when telemetry is switched off. Inside the `else` sat the line that reported "the fence was applied".
So with telemetry off, every fenced task would have been **refused** although its fence had applied
[V].

**How it was found.** The supervisor diffed the worker's file against the last commit and read the
changed block as behaviour, not as text: "what happens when `ev` is NULL?" The function that creates
`ev` returns NULL exactly when telemetry is off (`jc_app.c:550`).

**The lesson.** Debugging edits are code. A change "only for logging" can move other statements into
a branch. Read a diff by asking, for each changed condition, **what runs now in each case** — and
remove debug output before anything is committed.

### 3.3 A test that could never fail

**What happened.** A worker added a check to a test driver:

```sh
if [ -n "$f" ] && smoke_bgrep "$f" -q '"name":"spawn_subagent".*"The configured models are: 1' | tail -1; then
    t_ok "spawn_subagent's schema includes the configured model list"
```

(`tests/smoke/subagent_model_select.sh` on the drive line at `0a112fcd`.)

In a shell, the exit status of a pipeline is the status of its **last** command — here `tail`, which
succeeds whether or not the search found anything. The check passed whatever the program did [V].
The supervisor proved it by putting back the code from *before* the fix: the driver still passed, 5
of 5 [M].

**And again.** The fix replaced it with two checks, and the supervisor tried both against the two
ways the code could be wrong — the duplicate put back, and the model list taken away. Both checks
stayed green [M]. The first counted matches of `"type":"function","function":{.*"name":"spawn_subagent"`
with `grep -o`; the request body is a single line of JSON, and the greedy `.*` swallows everything
between the first tool and the last mention, so the count is 1 however many duplicates there are. The
second looked for the sentence "The configured models are:" — which the same request also carries in
an earlier tool result, the error message the driver's first check provokes on purpose. Each check
could find its evidence somewhere other than where it meant to look. The third version (12:10)
counts a string only a tool *definition* carries, in the first request, before any tool result exists;
the supervisor put each defect back and saw each check fail by its own message — "spawn_subagent is
defined 2 time(s) in req.1 (expected 1)" and "req.1's spawn_subagent definition does not list the
configured models" [M]. It took four attempts in all.

**The lesson.** A test that has never been seen to fail has never been seen to work. Before trusting
a check, **remove the thing it guards and watch it go red** — and do it again after every rewrite of
the check. Make the pattern match only the thing you mean (a definition carries `"description"` next
to its name; a call carries `"arguments"`), and look where only that thing can be. Exercise 1
(section 7) shows the `| tail` trap in four lines.

### 3.4 The recorded output that blessed a defect

**What happened.** The O1 fix made a jichi tool (`spawn_subagent`) list the configured model names in
its description. To do that, the worker registered a second, dynamic version of the tool — but the
original stayed registered too, so every request to the model now carried **two tools with the same
name** [M]. jichi keeps recorded copies of real requests ("traces", used by the tutorials and checked
by a driver); the worker re-recorded them, and the new recordings — with the duplicate — became the
"expected" output. The commit message read: "prose back to 20 tools". The correct count, on master,
was 19 [M].

**How it was found.** By counting: the tool name appeared twice in the new recording and once in the
old; master's recordings said 19 tools, the new ones 20.

**The lesson.** A recorded ("golden") output records **what the program does, not what is right**.
When a re-recording changes a number, explain the difference before committing it: here the "+1" was
the bug. (Some model providers reject a request with two tools of one name; others accept it and the
model sees two descriptions of one tool.)

### 3.5 Commits that did not build

**What happened.** The M968 commit message said "make WERROR=1 test → 15438 checks, 0 failures". The
supervisor's clean clone of that exact commit **did not compile**: a file that came in with an earlier
unverified snapshot did not build under gcc 15 [M]. The lead had run the tests in its working tree,
which held other, uncommitted changes that happened to fix the build.

**The lesson.** "Green in my working tree" is not "green on this commit". When the tree holds
anything uncommitted, check a commit on **the commit itself** — `git stash -u`, or a clean
`git worktree add` of it — and quote that run. The lead adopted this rule the same morning and used
it for every later commit.

### 3.6 The test binary that was not rebuilt

**What happened.** To prove M962's test, the lead disabled the fix and ran the tests — and they
**passed**. It looked like the test was useless. Then the lead noticed the test program's date:
`make -j8` builds jichi but not the test runner, so it had been running a binary from before its
edits. With `make test`, which rebuilds the runner, the test failed as it should [M].

**The lesson.** Know what your build command rebuilds. A "perturb and re-run" check is only as good
as the binary it runs; check the binary's timestamp, or use the target that builds what you test.

### 3.7 A revert that does not compile is not a red

**What happened.** To re-check M962, the supervisor put back the source file from before the fix.
The tests did not fail — they **did not link**: the newer test code called a function the old file
did not have [M]. A failure, but not the failure that matters.

**The lesson.** "Red" must mean *an assertion failed*, not *the build broke*. When removing a fix
breaks compilation, perturb inside the function instead — here, one added `return;` that skipped the
cap — and look for the specific failing check (`tests/test_bg.c:296: cap_verified`). qwen-code makes
the same rule explicit (section 5). Exercise 3 shows the difference.

### 3.8 The flaky test that looked like lost data

**What happened.** M962's committed test failed once, in a worker's test run: the head of a background
command's output (the marker `MARKHEAD`) was missing from what the test read. Two stories fit: a
**test race**, or **real lost output**. The supervisor read the code: `drain()` copies new bytes into
the delivered buffer *before* it shrinks the file, so the head is always delivered on the first read.
The failing path was the test's own loop — the file grew past the cap again before the second check,
the loop went round, and a *later* read (new bytes only) was the one checked [V]. A test race; the fix
collects every read.

**The lesson.** When a test fails intermittently, **read the code path** before believing either "the
product is broken" or "the test is flaky". Here the reading also found something real (next case).

### 3.9 A limit the plan itself built in

**What happened.** The same reading showed that M962's cap holds only when jichi reads the file, and
jichi reads it between tool calls. During one long model call — minutes — a background command that
writes fast can still grow its file without bound [V]. The supervisor's own plan had specified "cap
the spool file in `drain()`", so the gap came from the plan, not from the team.

**The lesson.** A bound enforced by the reader holds only as often as the reader reads. A bound that
must hold regardless belongs where the writing happens (for a child process, a file-size limit set
before it starts, `RLIMIT_FSIZE`) — and the record should state the bound honestly. Filed as a
follow-up.

### 3.10 A global rule to fix one tool

**What happened.** The second attempt at the duplicate tool (3.4) changed the tool registry so that
registering a name that already exists **replaces** the old entry — for every tool, silently [V]. The
supervisor checked the other callers: user-defined tools already skip a taken name with a warning, and
MCP tools are named `<server>__<tool>`, which no built-in matches, so nothing could replace a fenced
built-in tool today. Still, a rule for every registration had been written to fix one, and any future
duplicate would vanish without a word.

**The lesson.** Fix the one case, not the class, unless the class is what is broken. A narrower shape
— an explicit "replace" for the one intended override, and a warning on any other duplicate — keeps
the next mistake visible. The lead re-dispatched it that way, and it landed at 11:41: `register()`
now warns and keeps the first entry, a separate `replace()` serves the one intended override, the
recordings are back at 19 tools, and the new unit test fails five checks when `replace()` is made to
append again [M]. Its driver checks got their teeth only in the next attempt (case 3.3, "And
again").

### 3.11 The commit clock and a blocked lead

**What happened.** The operator's rule is: commit regularly; flag any stretch over 60 minutes without
a commit. In the fourth run the lead went past it — 91 minutes, from 10:06 to 11:37 — while it waited
on two batches in a row and then fixed a build the second one broke, with verified work sitting
uncommitted [M]. The supervisor's notes could not reach it: a lead blocked in
`spawn_parallel` reads injected notes only when the batch returns.

**The lesson.** This is cause 1 of section 2.2 again, from another side: a coordinator that blocks
cannot react — not to its workers, not to its supervisor, not to its own clock. Commit verified work
**before** dispatching the next batch.

## 4. The supervisor's own mistakes

An honest record includes the mistakes of the one writing it.

1. **The standing prompt contradicted the rules it enforced.** The lead's prompt, carried over from an
   earlier drive's harness, said: "Bound every command with the run_terminal_command `timeout`
   argument". The project's rules say the opposite for any run that measures or reproduces something —
   a time limit that fires does not just hide the answer, it produces a different one. At 08:56 the
   supervisor flagged the lead for wrapping a test run in `timeout 600`, while the supervisor's own
   prompt told it to put a time limit on every command. Found at 11:20 while editing the prompt;
   corrected the same minute, with a dated note naming the earlier note it replaces.
2. **The plan's own wording set the cap's gap** (case 3.9).
3. **A hand-typed time was wrong.** One note said "09:21"; it was sent at 09:19. Small, but a record
   that is wrong about small things is trusted less about big ones; times are now taken from the
   clock, not typed.
4. **A check script that hid its own failure.** The first re-check of M968 failed in seconds and
   printed only "exit 2" — no build log. The script was changed to print the compiler errors, and the
   re-run named the real cause (case 3.5).
5. **A judgement that cost a task.** At 07:15 the supervisor saw that the next batch still gave
   `run.sh` to two tasks, and decided not to interrupt (the lead could not read a note until the batch
   ended anyway). The task was lost to that conflict. The note would not have arrived in time — which
   is case 3.11's lesson, for the supervisor.
6. **Earlier the same day,** while planning batch 0, the supervisor stated that two branches had
   "never been merged"; their files were on master, copied in by a later commit. Merged means **the
   content is there**, not that the branch is an ancestor. Corrected in the record the same morning.

## 5. How qwen-code solves the same problems

qwen-code is an open-source coding agent (github.com/QwenLM/qwen-code). Two research agents read
it at commit `ac92a0f699` (2026-10-15) — reading only, nothing run — and the supervisor re-checked the
central claims. Paths are relative to `packages/core/src/` unless they start otherwise.

| Problem (this page) | qwen-code's answer | Evidence |
|---|---|---|
| The blocked lead (2.2, 3.11) | Sub-agents run **in the background by default**; the parent keeps working and gets a completion notification in a later turn. "While background agents run, continue meaningful non-overlapping work." | `tools/agent/agent.ts:1018-1026` [checked] |
| Why they changed it | A review "ran fifteen foreground agents sequentially and exhausted its six-hour budget"; a model "returning one call per response turns that instruction into serial execution". | `docs/design/review-parallel-dispatch.md:6-11` [checked] |
| Waiting for a whole batch | Workflow `pipeline()` chains the stages per item "with NO barrier between stages, so item A can be in stage 3 while item B is still in stage 1". | `agents/runtime/workflow-orchestrator.ts:2924-2930` [checked] |
| Correcting a worker | A message queue the running agent "drains ... between tool rounds". | `agents/background-tasks.ts:1728-1732` [checked] |
| Retrying the same failed call | A run that hit its limit says so, because otherwise the parent "retries the same call verbatim". | `tools/agent/agent.ts:1500-1504` [checked] |
| Briefing a worker (3.1, 3.2) | "Give every task an objective, scope, completion condition, and required evidence." "Past roughly three, the leader spends more of its turn reconciling reports than doing the work." | `skills/bundled/coordinate/SKILL.md:45-46` [checked] |
| Fixes that introduce defects (3.2, 3.10) | After a fix, one agent reads **only the applied hunks** and names the assumption each introduces and whether anything pins it. | `skills/bundled/review/DESIGN.md:927-933` [checked] |
| Tests that cannot fail (3.3, 3.7) | A command reports a test as unreachable or inert; "`gated` requires a real assertion failure" — a revert that breaks the build is inconclusive. | `packages/cli/src/commands/review/test-efficacy.ts:12-34` [checked] |
| Patching forever (O1's three attempts) | After many rounds, an advisory that the *approach* may be wrong: the review "could say where an approach leaks, but never that a different approach would retire all of the leaks". | `docs/design/2026-08-17-review-approach-signal.md:15-29` [checked] |
| A worker started from stale code | Refused, fail-closed: a worktree agent will not start while the parent has uncommitted changes. | `tools/agent/agent.ts:3373-3401` [checked] |
| Overlapping write sets (3.1) | Prompt text only ("give concurrent agents disjoint write scopes"); no runtime check was found. | `tools/agent/agent.ts:1018` [checked]; the absence [read] |

**Where jichi is already ahead.** jichi does not refuse a dirty parent: `spawn_parallel` first
snapshots the lead's whole working tree, uncommitted changes included, and starts every worker from
that snapshot (`src/tools/jc_tool_parallel.c:2203-2207`) [V]. And jichi merges a worker's work only
after a green test run inside its worktree, keeping a failed attempt as a recoverable reference — the
first attempt at O1's fix broke six unit tests and never reached the lead's tree [M].

**What does not transfer.** qwen-code's *arena* runs 2 to 5 models on the same task and lets a person
pick the winner — useful for choosing between approaches, but it multiplies the cost, and the drive's
problem is not choosing. Its *forked* agents share the parent's prompt cache; a team on two different
models cannot share one.

## 6. What changed, and what was filed

**Adopted the same day** (the operator's decision; to be recorded as D16 in the drive's decisions, and
in the lead's standing prompt under "Working with workers" from the next run on):

1. **The brief.** Every worker task names its objective, the files it owns ("edit no other file"),
   what is out of scope, when it is done, and the evidence to quote: the failing line before, the green
   count after. No debug output; no file outside the list.
2. **The hunk audit.** After a worker's fix and before its commit, a fresh reviewer on the larger
   model reads only the changed hunks and names, for each, the assumption it introduces and what pins
   it.
3. **The test proof by a probe task**, not by the lead: a worker task with its own worktree whose
   changes are never merged (`write: true`, `merge: false` — a read-only task has no shell). It
   removes the fix, rebuilds, and quotes the failing assertion; a removal that breaks the build is
   reported as inconclusive.
4. **A design note at the third attempt** on one finding, before the next dispatch.
5. **Wider waves** once the independent findings start: 4 to 5 tasks as usual, 6 at most, each file
   owned by one task.
6. **The timeout rule, corrected** (section 4.1): no time limit on a run that measures or reproduces
   something. The drive keeps one 30-minute limit per command as a backstop against a command that
   hangs forever; a run it ends counts as an **unknown** result, never as a pass or a fail (the
   operator's decision).

**Filed for after batch 11** (D17; batch 12 of the plan, each "design note first"):

| | Proposal for jichi | Addresses |
|---|---|---|
| B1 | A non-blocking `spawn_parallel`: workers in the background, completion notes delivered between the lead's tool rounds | 2.2 cause 1, 3.11 |
| B2 | Per-task stages with no barrier: implement → test → review chained per task | 2.2 cause 1 |
| B3 | A declared file list per task: overlap refused at dispatch, writes outside the list kept, not merged | 3.1, 3.2 |
| B4 | A message queue to correct a running worker instead of relaunching it | 2.2 cause 3 |
| B5 | Typed "ran out of budget" results naming the limit | rework |
| B6 | Clearing old tool results from the lead's context | the lead's context filled |

B3 is the one where jichi could do better than qwen-code, which leaves disjoint write sets to prompt
text.

## 7. Three exercises

Each takes a minute in any POSIX shell with git 2.38 or newer. Each command block was run in exactly
this form before it was published; the outputs shown are from that run.

### Exercise 1 — a pipeline's exit status (case 3.3)

```sh
cd "$(mktemp -d)"
printf 'alpha\nbeta\n' > list.txt
if grep -q gamma list.txt | tail -1; then echo "check passed"; else echo "check failed"; fi
if grep -q gamma list.txt; then echo "check passed"; else echo "check failed"; fi
```

Output:

```
check passed
check failed
```

"gamma" is not in the file. The first check passes anyway: the `if` tests `tail`'s status, and `tail`
succeeded. **Try** the first check again with `set -o pipefail`, which makes a pipeline fail when any
of its commands fails:

```sh
bash -c 'cd "$(mktemp -d)"; printf "alpha\nbeta\n" > list.txt; set -o pipefail; if grep -q gamma list.txt | tail -1; then echo "check passed"; else echo "check failed"; fi'
```

It prints `check failed`. Run it in bash: POSIX added `pipefail` only in 2024, and not every `sh` has
caught up — Ubuntu 24.04's `/bin/sh` (dash 0.5.12) answers `set: Illegal option -o pipefail`.

### Exercise 2 — see a merge conflict before merging (cases 3.1, 5)

```sh
cd "$(mktemp -d)" && git init -q demo && cd demo
git config user.name learner && git config user.email learner@example.org
printf 'one\n' > f.txt && git add f.txt && git commit -qm base
git worktree add -q ../a -b a && git worktree add -q ../b -b b
(cd ../a && printf 'from a\n' > f.txt && git commit -qam a)
(cd ../b && printf 'from b\n' > f.txt && git commit -qam b)
git merge-tree --write-tree --name-only a b; echo "exit=$?"
git status --short; echo "the working tree above is untouched"
```

Output (the first line, a tree id, differs on every machine):

```
447faa133aa10efb458e5a5694b421c857c14291
f.txt

Auto-merging f.txt
CONFLICT (content): Merge conflict in f.txt
exit=1
the working tree above is untouched
```

Two worktrees ("workers") edited the same file. `git merge-tree --write-tree` computes the merge
**without touching any working tree** and names the conflicting file; exit status 1 means "conflict".
A coordinator can run this before merging — or, better, give each file to one worker so it never
happens.

### Exercise 3 — red for the right reason (cases 3.3, 3.7)

```sh
cd "$(mktemp -d)"
printf 'add() { echo $(($1 + $2)); }\n' > add.sh
cat > test.sh <<'T'
. ./add.sh
if [ "$(add 2 3)" = 5 ]; then echo "ok - add 2 3 = 5"; else echo "not ok - add 2 3 = $(add 2 3)"; exit 1; fi
T
sh test.sh; echo "exit=$?"
sed 's/+ \$2/- $2/' add.sh > add.new && mv add.new add.sh
sh test.sh; echo "exit=$?"
printf '' > add.sh
sh test.sh; echo "exit=$?"
```

Output:

```
ok - add 2 3 = 5
exit=0
not ok - add 2 3 = -1
exit=1
test.sh: 2: add: not found
test.sh: 2: add: not found
not ok - add 2 3 = 
exit=1
```

Run 1 is green. Run 2 **perturbs** the code (a `+` becomes a `-`) and the test fails *by its
assertion*: "add 2 3 = -1" — proof that the test checks the sum. Run 3 deletes the function: the test
fails too, but because `add` no longer exists. That red proves only that the test calls `add`, not
that it checks what `add` returns — the shell's version of "a revert that does not compile". (The
`not found` line appears twice because the test calls `add` twice.)

## 8. Glossary

- **Batch** — a group of findings fixed together; master moves once per batch, after the full gate.
- **Drive** — a long, unattended session of agents working on a code base, under a supervisor.
- **Fast gate / full gate** — the unit suite at `-Werror` (minutes) / `make ci`: two compilers,
  sanitizers, valgrind, every test tier (30 to 45 minutes).
- **Golden output / trace** — a recorded copy of real output that a test compares against.
- **Inject** — a message delivered to a running jichi at its next model call.
- **Lead** — the agent that plans, dispatches, verifies and commits.
- **Merge conflict** — two changes to the same lines that git cannot combine on its own.
- **Red-first ("teeth")** — a new test must be seen failing before the fix, by its assertion, and
  passing after.
- **Vacuous check** — a check that passes whatever the program does.
- **Worker** — an agent that does one task in its own worktree.
- **Worktree** — an extra checkout of the same git repository in another directory, with its own
  branch; changes are shared through commits, not through the files.
- **WIP snapshot** — a commit of unverified work, labelled as such, so that nothing is lost when a run
  ends.

## 9. The afternoon, 12:15 to 18:45 (added at 18:50)

*Sections 1 to 8 were written at midday. The drive went on; this section records the afternoon the same
way: what happened, how it was found, and what it teaches. The marks mean what they meant above.*

### 9.1 What happened

Three more runs: run 5 (12:12 to 15:05, 17.6 million tokens, 1,604 tool calls), run 6 (15:05 to
15:31) and run 7 (from 15:32, still running at 18:45). By 18:25 the drive line held 55 commits [M]. The
lead closed the rest of batch 0's small items, then proved every file the two morning snapshots had
brought in, and spent most of the afternoon on the last item, D6 -- the scoped edit check that
refuses the lead's own second edit of a file a worker owns, and reports any change to such a file made
by other means.

### 9.2 Nine more lessons

**Pay a snapshot's debt file by file.** The two "WIP" commits of the morning (3.11, 3.5) had brought in
code that no commit had proven on its own. The lead mapped every one of their files to a finding and a
proof -- a commit with its failing line, a probe's quoted failing assertion, or a revert [M]. For one
group (D5) removing the fix broke the build; the lead recorded that, honestly, as *inconclusive* (case
3.7), and a probe then made a clean red by changing one character inside the function: without the
notice's closing parenthesis, `tests/test_memory.c:326` fails [M, re-run by the supervisor]. A
snapshot commit is a debt; the ledger is how it is paid.

**A time limit inside a test is a cap too.** A background-output test failed once, and was first
recorded as a flake. Reading it found the mechanism: its helper waited at most 2 seconds (100 polls of
20 ms) for a command that writes 30,000 lines; on a busy machine the command outlasts that, the helper
gives up, and the last line -- delivered only when the command exits -- is missing [V]. The test never
checked that the command had exited, so a slow machine produced a wrong answer instead of an honest
"too slow". The fix (M974) waits for "exited" under a generous bound and asserts it **first**; with the
bound cut to one poll, the "exited" check is the first to fail [M]. The lesson is case 3.7's and the
project's caps rule at once: a limit that fires must say that it fired.

**Test a permission check as the user it guards against.** A new test forced an error by making a
directory read-only. Root ignores directory permissions, so under root -- many containers, many CI
runners -- the test would fail for the wrong reason. The lead guarded it (`geteuid() != 0`, the
project's existing pattern), and the supervisor measured both sides as root in a container: before the
guard, `tests/test_bg.c:364: id < 0` fails; after it, the suite passes [M].

**One commit, one finding -- by explicit paths.** After a five-task batch merged, `git add -A` put all
five fixes and a worker's scratch report into one commit titled for one of them [M]. Nothing was lost,
but the record no longer said what the commit held. The repair: a commit removing the scratch file, and
a log table naming every piece of the big commit with its finding and proof. The rule since: after a
merge, commit by explicit paths.

**A plan step's inputs belong in the workspace before the step.** D6 had been decided by the operator two
days earlier; the decision lived only on a branch that the plan never brought into the drive's line, and
the team's container cannot reach the server. The lead searched for it, call after call. The supervisor
copied the decision section in, word for word, with its source -- the gap was in the supervisor's plan,
not in the team.

**Count the guards against doing too much.** The decision lists five checks that must fail first. The
lead's first test had three, all of them "is it refused when it should be". The two missing ones were
"is it **not** refused when it should not be" -- a file no worker touched, such as the lead's own log,
and a worker's attempt that was kept but never merged. Without them, an implementation that refuses
every second edit passes. The lead added both. A test suite that only checks that a fence closes cannot
tell a fence from a wall.

**"Gates green" is a claim; the exact commit is the measurement.** The lead reported the first version of
D6 as green. A clean build of that exact commit with warnings as errors did not compile (a C89 rule,
a declaration after a statement), and a lint failed because the new telemetry event had no reader [M].
The lead reproduced the failure, wrote "my claim was wrong", and fixed it. A third lint failure that the
supervisor attributed to D6 was in fact older -- see 9.4.

**Read the code for what no test exercises.** The third version passed every gate. Reading it found two
defects no test reached [V]: when two watched files change in one step, the code calls `free()` on
memory it did not allocate with `malloc` (undefined behaviour); and after every tool call it re-reads
every watched file into memory that lives for the lead's whole turn -- a lead's turn is a whole run of
about a thousand tool calls, so that memory grows with every call. The first needs a test that changes
two files at once under the address sanitizer; the second needs a footprint check, because a leak checker
sees nothing wrong with memory that is still reachable. Both were sent to the lead with those tests.

**Running the program finds what reading the program does not.** Two more jichi bugs came from watching
the drive itself, both measured [M]. *L-23:* a worker's long answer is saved to a file in the worker's
own process directory; the lead is handed that path, and the lead's path fence -- which exempts only the
lead's own directory -- refuses it (the lead was process 2, the worker process 459). *L-24:* while the lead
waits on an in-process sub-agent, its output stream writes no heartbeat (none in 22 minutes, against four
during one parallel batch) and its control socket answers no one, so a watch saw a live run as stale and
the supervisor's notes waited until the sub-agent returned.

### 9.3 The commit rule, measured anew

The operator's rule was "commit and push regularly"; the supervisor had made it "flag 60 minutes without
a commit". The afternoon showed that this measures the wrong thing. One 91-minute stretch held verified
work uncommitted -- a real risk; a later 69-minute stretch had a clean tree, because the lead was only
proving tests, whose probes never merge -- no risk at all [M]. The operator approved a new measure: the
**age of uncommitted work** (more than 30 minutes is flagged), and a run that ends with uncommitted work;
a clean tree is never late. The watch records when it first sees the tree dirty at a given commit,
because a file's modification time cannot tell: a file edited again looks fresh. With it came a
practice -- commit what is verified before every dispatch, since a lead waiting on its workers can
neither commit nor read a note.

The new measure found a conflict within the day: the practice "audit a fix before committing it" held D6's
first stage uncommitted for 43 minutes behind an in-process review. The operator changed the order (D18):
commit on a green gate of the exact commit, then audit that commit, and fix findings in follow-up
commits. Verified work no longer waits behind a reviewer.

### 9.4 The supervisor's afternoon mistakes

1. **The 60-minute rule** measured the clock instead of the risk (9.3).
2. **The plan gap** that left D6's decision outside the workspace (9.2).
3. **A misattributed lint failure.** The supervisor listed a failing arena lint among D6's faults. It was
   older: the morning's O1 fix had registered a tool on the session arena, and the supervisor's own O1
   check (12:16) had not run that lint. The lead found the true source; it was a registration that
   lives as long as the session, matching two existing exceptions, and was allow-listed for that reason.
   Corrected in the record with the time of the correction.
4. **A secret-handling slip.** Checking a commit for key values, the supervisor wrote the lines of one
   key file into a scratch file for a moment (deleted at once, never printed). The project's rule is that
   a token is never written to a file; a pipe would have done the same job.

### 9.5 Did wider waves raise the workers' use?

Not yet, and the numbers say why [M]: worker use was 14 % of six slots in run 5, 2 % in run 6 and 8 % in
run 7 so far -- no better than the morning. The practice of wider waves ran once at full width (six tasks:
five probes and an audit, about 400 worker calls in five minutes), and then D6 took the afternoon: one
item, worked one attempt at a time, with in-process reviews that block the lead. Wider waves help only
when there are independent pieces of work; batch 0 ended with one piece. The morning's expectation for
batches 1 to 9, which are mostly independent findings, is still a prediction, and the measurement will
come with them.

### 9.6 Where batch 0 stands at 18:45

Everything but D6 is closed and verified. D6 passes every gate at its third attempt; it still owes its
unit-test file and the two defects of 9.2. Then the lead runs the whole smoke tier and writes
`BATCH 0 READY`; the supervisor runs the full gate (`make ci`) on that exact commit and asks the operator
before master moves.

## 10. The night and the second morning: two batches through the gate (added 2026-10-19)

*Section 9 ended at 18:45 on the first day with batch 0 nearly ready. This section follows the drive
through the night and the next morning, as far as the first master move. The marks mean what they meant
above: [M] measured, [V] verified by reading the source.*

### 10.1 What happened

The gate is `make ci`: about thirty minutes of gcc and clang builds at `-Werror`, the address and
undefined-behaviour sanitizers, valgrind, the unit suite six times over, the fault-injection build, the
smoke tier of more than 460 shell drivers, a mutant sweep and the end-to-end tier. The supervisor runs it on
exactly the commit the lead declares ready, in a fresh clone of that commit, with no time limit [M; the
logs are in `docs/internal/reviews/2026-10-19-supervision/gate-logs/`]:

| Commit | When (+0200) | Result | What it found |
|---|---|---|---|
| `3cb67597` | 10-18 23:36-23:39 | red | batch 0's first READY: a leak under the sanitizer in one test case |
| `1580c5ef` | 10-19 00:16-00:19 | red | a check that makes the temporary directory unwritable crashed the suite: `fputs(NULL)` behind a guard that did not stop |
| `4f869ece` | 10-19 00:56-01:27 | green | batch 0 passes, but the mutant stage swept 0 drivers (10.2 f) |
| `14ea29ea` | 10-19 06:56-07:01 | red | batch 1's gate: a background command's last line was lost under load -- a race in **batch 0's** code (10.2 a) |
| `907552f6` | 10-19 08:20-08:35 | red | the code green; two documentation counts stale (the milestone banners, the number of smoke drivers) |
| `5ab3ef6e` | 10-19 09:01-09:33 | red | the smoke tier green (467 drivers); the mutant sweep found one test that cannot fail (10.2 k) |
| `113e5f7f` | 10-19 10:20-10:54 | **green** | every stage: 467 drivers and 2,730 checks; the mutant sweep reached all 18 changed drivers |

Between the gates the team fixed batch 1: four findings about losing a user's data. An undo deleted
files that were ignored at the time of the undo (D-02). A file holding a NUL byte was cut at that byte
when the model edited it (D-01). A failed write left the user's file empty instead of putting it back
(D-04). And `setup` replaced a configuration with comments in it, without a backup (D-22). From
185733ea to 113e5f7f the drive line holds **131 commits** changing 114 files [M]. The commit trailers name the
model that wrote each one: 100 by the lead (qwen3.8-27b), 25 by workers (qwen3-coder-next). The other
six carry no trailer: four team commits made without one, and two set-up commits under the operator's
name (the lead prompt and a note on it).

### 10.2 Twelve more lessons

**a. A gate's failure belongs to the code, not to the batch under test.** Batch 1's gate failed on a test
of background-command output. Batch 1 had not touched that code. Batch 0 had, and batch 0's own gate
had passed the same test seven times. The cause was a *race*: two processes acting on one file, where
the result depends on which acts first. A race can pass a hundred times and fail on the hundred and
first, when the machine is busier. Here three workers of the next batch were running at the same time,
which they had not been at batch 0's gate. "Green" means "did not fail this time". So when a later gate fails, the first question is "which
code?", not "which batch?".

**b. A symptom that comes and goes is settled with an instrument, not with another run.** Re-running
the gate until it passes would have proved nothing; the race would still be there. The supervisor added
two instruments instead [M]. **A:** one line of output inside the rewrite, nothing else changed. It
showed the file being rewritten while the command was still writing to it, which the code's own comment
said could not matter. **B:** the same line, plus a one-second pause inside the dangerous window, plus a
command that writes its last lines slowly. B reproduced the gate's exact failure, every time.

**c. Count what the instrument reached.** The supervisor's first attempt at B paused but left the command
unchanged, and it passed. That pass meant nothing: in that run the command had finished writing before
the rewrite ran, so the pause sat in a window no byte crossed. Instrument A was what showed this. It
counted the rewrites and the state of the command at each one. An instrument that did not reach the
thing it measures produces a green that is not evidence.

**d. A pause meant to widen a window must sit inside the window.** The team's own fix added a
fault-injection pause for its test. The first version placed it *before* the size read, so the bytes
written during the pause were counted and kept. On the old code that test could never have gone red.
The bytes are lost between the rewrite's *read* and its *truncate*, and that is where the pause belongs.
Moved there, the test fails with the fix removed and passes with it [M].

**e. A test that passes without its fix is not a test of the fix.** Three cases, all measured by removing
the fix and running the tests [M]:
- `nul_edit_refused` check 2 looked for the words "NUL bytes" in the session log. The *read* step had
  already printed those words, so the check passed with the edit refusal removed. It now looks for words
  only the refusal prints.
- `apply_patch`'s NUL refusal had no test at all: with it removed, 15,649 checks passed. It has one now.
- A `--` separator added to a `grep` command is never reached. An earlier check refuses every path that
  starts with a dash, so no such path ever gets to `grep`. That one is harmless and is recorded as
  defence in depth.

**f. A sweep that swept nothing.** The mutant stage replaces the program with a hollow one and checks
that every changed test notices. On the first green gate it reported "no changed drivers to sweep". It
compares against the commit's own parent, and that commit changed only documentation. Run again against
the batch's base (`MUTANT_BASE`), it swept 13 drivers, all of which noticed [M]. A stage that reads
nothing reports success.

**g. The fault model decides which tests you can write.** jichi's write-failure injector is *monotone*:
after N calls, every call fails. For D-04 that means the restore -- a second write -- fails too, so "the
original is put back" could not be tested. The lead's first answer was to test only "restore also
failed" and defer the rest. That was honest, but the finding's own test said "the original must
survive". A *one-shot* knob (fail exactly the n-th write) took a few dozen lines. It made the real property
testable for both `write_file` and `edit_file` [M].

**h. A record that others will act on must claim exactly what was measured.** The lead's note for the
next drive said that every commit "at or above" 4f869ece was gated. Only 4f869ece was. The note also
counted a batch-1 fix as batch 0's. Both would have led the next drive to rely on unchecked code.
Corrected before it was used.

**i. A ceiling that only falls.** One documentation lint counts, page by page, the table rows longer than
200 characters, and its header says the count may only go down. To admit a long new row, the lead raised
the count. Raising the ceiling to admit one row is the same move as loosening any gate: a lint that is
adjusted to pass no longer checks anything. The row was shortened instead.

**j. A design trade-off belongs to the person who owns the decision.** The race could be fixed three
ways:
- **Rewrite the file only after the command has exited.** Simple and correct. But a long-running,
  chatty command's file then grows without limit until it ends.
- **A small relay process as the only writer of the file.** Bounded and correct. More code.
- **Linux hole-punching, with the first way elsewhere.** Bounded on Linux.

The operator had decided earlier that the file is capped, so the first way changes that decision. The
choice went to the operator. The operator accepted the first way as an interim and asked that the trade-off
be written down plainly (`docs/DECISIONS.md`, `docs/DEFERRED.md`).

**k. A test that chooses its own program cannot be checked by swapping the program.** The mutant
sweep hands every test a hollow jichi through one variable that the shared test library reads. One new
test set that variable itself, to the real jichi, so the sweep could not reach it. With the hollow
binary it stayed green on all five checks; with its one line deleted, all five noticed [M]. The line
had come from an earlier repair: a path that pointed at the wrong place was re-pointed, when it should
have been deleted. The fix came with a lint: only the two shared files may set that variable, and the
lint fails on a copy of the old line [M].

**l. A gate that stops at the first failure hides the second.** `make smoke` stops at the first red
driver. To see whether anything else was red behind a failing documentation lint, the supervisor ran
the whole tier once more with keep-going on (466 of 467 drivers passed). Only then was a one-file fix
known to be enough.

### 10.3 The supervisor's mistakes

1. **A verdict not passed on.** The batch-0 gate was green at 01:27. The supervisor wrote that in the
   supervision log and asked the operator, but never told the lead. Four hours later the lead's log read
   "no verdict yet". A result recorded in one place is not a result delivered.
2. **Two runs in one file.** Three verification runs started in the same second, and their output file
   was named by commit and second, so they wrote into one file. Discarded and re-run, and the pid is now
   in the name.
3. **The widening that reached nothing** (10.2 c), and **an instrument that did not compile**: an
   escaped `&&` inside single quotes became an invalid C escape.
4. **Times from memory.** Three gate times typed into a README from memory; two were wrong. They now
   come from each log's recorded start and end.
5. **A secret scan that read nothing.** Before pushing these records, the supervisor scanned them for the
   project's keys. Its positive control -- a copy of a key file, which must match -- did not match. The
   key files end without a newline, so joining them glued each key onto the next file's first line. With
   one key per line: 0 hits in 43 files, 3 of 3 controls matched. Without the control, "0 hits" would have
   been believed.

6. **A grep that read a pipe.** To see whether a run's prompt held a note, the supervisor ran `grep`
   over every file of the run's directory. Two of those "files" are named pipes, through which jichi
   hands its journal and telemetry to the harness. The grep opened one and waited on it for two
   minutes. A second reader on a pipe competes for its bytes. The check afterwards found that nothing
   was lost (71 tool calls in the stream, 71 records in the journal) [M]. The rule since: name the
   files to read; never read a run directory with a wildcard.
7. **Hand-typed times, again.** Two log entries and a note carried times typed by hand, and they were
   off by up to six minutes. One of them was evidence: "the files were written before the grep began".
   That claim was false, and it was corrected in a dated entry. Times now come from `date`.

8. **A key in a scratch file.** To make the secret scan's positive control, the supervisor copied a key
   file, and once a line holding the gateway key, into scratch files. They were deleted within seconds
   and never printed. But the project's rule is that a key is never written to a file, whatever the
   file is for. The control needs no file: `grep -cF -f <(patterns) <(cat keyfile)` gives 1. This is
   the same slip as 9.4 item 4, in a new form. That is why the rule is about *files*, not about intent.
9. **A backtick that ran.** A log entry written with an unquoted heredoc, used to get the time from
   `date`, held text in backticks. The shell ran it as a command. It was harmless, but it blanked the
   words. The safe form keeps the two apart: the heading via `printf` with `$(date)`, the body in a
   quoted heredoc.
10. **A test checked at the last moment.** Writing the recommendation for master, the supervisor found
    one fix in the range (L-25, the run tool's timeout description) whose test it had never seen fail.
    It ran that check before the move (4 checks red without the fix). Without the written
    recommendation, which lists every fix with its check, the gap would have reached master unseen.

### 10.4 Where things stand

By 10:57 on 2026-10-19 the supervisor had moved master from 185733ea to **113e5f7f**, a fast-forward of
131 commits. That holds batches 0 and 1 and the first two fixes of batch 2. Before the move, every test
that a fix in that range claimed was seen failing without its fix, one of them only just before the
move. The operator had agreed beforehand that master may move after each batch's green gate. The supervisor
writes its recommendation into the supervision log first, then moves.

Seven gates ran over the two batches (the table in 10.1), and five were red. Each red was a
different kind of problem: in batch 0 a leak and a crash, then a race, two stale counts and a test that
could not fail. The code itself was green
at every gate after the race was fixed; what failed were the checks that only a whole gate on the exact
commit runs. That is the argument for running it every time.

Batch 2 continues from 113e5f7f: the edit-scope fence (B-04 and A-01), then the rest of the plan.

## Read further

- [Hardening review 0](2026-10-17-hardening-00-overview.md) — the findings the team was fixing.
- [`docs/TESTING_RUNBOOK.md`](../TESTING_RUNBOOK.md) — the project's ten steps for adding a test,
  each named after the incident that produced it.
- [`docs/DRIVING_A_TEAM.md`](../DRIVING_A_TEAM.md) — how to run a jichi team yourself.
- `docs/internal/2026-10-18-orchestration/` and `docs/internal/reviews/2026-10-18-orchestration/` — the
  raw numbers, the scripts that produced them, the two qwen-code research reports, and the supervision
  log of the day (internal records; not part of a public snapshot).
- `docs/internal/reviews/2026-10-19-supervision/` — every gate log of section 10 (the red ones too), the
  instruments that settled the race, and the supervision log through the master move.
