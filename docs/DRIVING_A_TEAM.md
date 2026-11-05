# Driving a team of jichi agents: tasks, agent definitions, skills and supervision

*This page is for anyone who sets a **team** of jichi agents to work on a project:
- a self-learner or a junior developer trying it for the first time;
- an experienced user planning a long unattended drive;
- another agent that drives a jichi lead from outside, as a supervisor.*

*A team here means one lead jichi, and the workers, subagents and reviewers it starts. You should
know what [`spawn_parallel`](PARALLEL.md), [`spawn_subagent`](SUBAGENTS.md), agent profiles
([SUBAGENTS.md](SUBAGENTS.md) § Named profiles), [skills](SKILLS.md), [memory](MEMORY.md), [the
board](BOARD.md) and [`jichi control`](CONTROL.md) are. You do not need to have used them.*

*The evidence is one drive: 248 runs, 62.8 hours of work, from Friday 2026-09-25 18:01 to
Monday 2026-09-28 10:00.*
- *The team worked on **zigodot**, a reimplementation of the Godot engine in Zig, in a private
  repository.*
- *The goal: Godot's own 2D pong demo, playable in an X11 window.*
- *It ran unattended in a container, on free local models, and a supervising agent checked on
  it every five minutes.*
- *The complete record is [the weekend drive, complete](analysis/2026-09-28-the-weekend-drive-complete.md).*
- *The first day, and the rules it produced for finish lines, gates and prompts, are in [what a
  jichi team must be told](analysis/2026-09-26-what-a-jichi-team-must-be-told.md). This page does
  not repeat them; it links to them.*

*Each rule below says:*
- *what happened;*
- *why it happens;*
- *what jichi does about it **today** (master at M815), which is not always what the drive's
  pinned build did;*
- *what you write in a task, a profile, a skill or a note.*

*A single-run companion is [DRIVING.md](DRIVING.md): briefs, gates that cannot lie, and caps.*

---

## 0. The answer first

1. **Pick the lead for judgement, and the workers for code** (§1).
2. **Say where the worker works: its own worktree, by relative paths.** Never name the live tree
   in a task (§2.1).
3. **Give every task an end the worker can see: its report.** A call budget ("stop after about
   150 calls") is a rule a worker cannot keep, because it cannot see its own count (§2.2).
4. **Bound every command that can run a program: a window, an engine, a server.** Programs that
   never exit cost this drive 7.2 hours in 13 shell calls (§2.3).
5. **One owner per file across parallel tasks, and the lead owns the shared files.** When two
   workers edit one file, the merge keeps the first and drops the second (§2.4).
6. **Name the command whose output carries the number the worker must read** (§2.5).
7. **Keep the worker's report out of the tree** (§2.6).
8. **A worker inherits neither the project rules nor the memory. Whatever it must know is in its
   task or its agent profile, so put the rules every task needs into the profile** (§3).
9. **Put procedures into skills, the lead's standing rules into the rules file, and only recent
   lessons into memory.** jichi injects only the newest 8 KB of the memory file (§4, §5).
10. **As the lead: commit a merge before you touch it, and run a red check in a separate
    worktree.** Twice in one night a lead threw away merged work it had not committed (§6).
11. **As a supervisor: facts first, then a question, and no answer.** Know what reaches a lead
    during a batch, and what does not (§7).
12. **Let jichi's fences do the fencing,** and know what M800–M815 added: preserved attempts, the
    dead-stream fence, the disk fence, all on by default (§8, §9).

---

## 1. The shape of a team, and who should lead

A team in this drive had four kinds of member:

| Member | How it starts | Where it works | What it was for |
|---|---|---|---|
| **lead** | the harness starts it, once per run | the live tree | plan, delegate, review, merge, commit, log |
| **worker** | `spawn_parallel` with `write: true` | **its own git worktree**, merged back at the end | implement one card |
| **subagent** | `spawn_subagent` | **the live tree** (it has no worktree of its own) | one scoped task, synchronously |
| **reviewer** | `spawn_subagent` with `readonly: true`, often on another model | the live tree, read-only | check a diff before or after a commit |

**The lead's model decided more than anything else.** The drive had two phases:

| Phase | Runs | Wall time | Median run | Commits | What it produced |
|---|---|---|---|---|---|
| A: a coding model as the lead | 182 | 16.1 h | 1.1 min | 280 | a gate or the goal claimed 10 times without evidence; no delegation at all |
| B: a slower, stronger model as the lead, coders as workers | 66 | 47.9 h | 27.3 min | 194 | every verified rung of the ladder, and a second project (an editor) |

The comparison is not controlled. Within the same hour as the switch, the operator also relaxed
the finish line and the supervisor tightened the standing prompt, and phase B had three times the
time. But the difference in kind is clear from the records:
- **Phase A's lead** wrote code quickly, and declared victory quickly. In the first 183 runs,
  14,935 tool calls, no lead delegated at all.
- **Phase B's lead** parked detours with notes, wrote specs, ran a check red before the code,
  delegated coding to faster models, re-ran every gate itself and read the counts.

**What you do:**
- **Choose the lead for plan-following and honest review, and the workers for coding.** In jichi
  that is `--model` for the lead (it pins the run and turns routing off; [ROUTING.md](ROUTING.md))
  and a `model` per task in `spawn_parallel`, or per profile.
- **Reviewers on a different model from the writer.** In phase B, the read-only reviewers on two
  other models found real defects, and the lead fixed them:
  - a test whose name promised an engine run it never made;
  - a section-ordering bug in `.tres` output.

  A second model had already paid off in phase A, where nothing was delegated: a double free the
  fast model committed was found when jichi's routing (`escalateOnVerify`) moved a failing turn
  to `jlu/qwen3.8-27b` ([the day-one analysis](analysis/2026-09-26-the-weekend-drive-day-one.md)).
  An earlier version of this list credited that double free to the phase-B reviewers.
- **Do not rely on a lead to delegate or to review itself because a prompt asks it to.** Phase A
  was asked to, 183 times, and did not (the day-one page, §6). If a reviewer matters, make it
  structural: an agent profile, a skill, or `escalateOnVerify` routing.

## 2. The task text: the contract you hand a worker

A worker sees its task text, its agent profile and the few sections jichi appends (§3), and
nothing else: not the project's rules, not the memory, not the lead's history, not the board, and
not what the last worker learnt. Every failure in this section was a task text that assumed more.

### 2.1 Say where the worker works

**What happened.** A lead wrote a task that named the live tree as its target eight times:
- "copy the demo into `/work/tests/fixtures/…`";
- "move the cache into `/work/…`".

The worker was a write child with its own worktree. Its first command was `cd /work`. jichi's
file fence refused its two `write_file` calls to `/work`, because they were outside its
workspace. The worker then did the same writes **through the shell**: 53 commands, straight
into the live tree, past the pool's merge.

**Why it happens.** A `spawn_parallel` write child's workspace is its worktree. Its **file
tools** are fenced to that workspace, and its **shell** is not: a command can `cd` anywhere the
user can write. The fence refused exactly what it should have. The task told the worker to do
the thing the fence refuses, so the worker found another way.

**What jichi does today.** The file fence refuses a write outside the workspace, as it did here.
Shell confinement is not built, so a task that points at the live tree still gets there.

**What you write:**
- In the task, relative paths only: "your worktree is your working directory; every path in this
  task is relative to it". Never an absolute path to the live tree.
- In the profile, once for every task (§3): "if a file tool refuses a write, stop and report the
  refusal; do not do the write another way".
- Keep a clear line between write tasks, which get their own worktree (`spawn_parallel`, `write:
  true`), and subagents, which work **in the live tree** (§1). Give a subagent write access only
  when you want its changes in the live tree immediately.

### 2.2 Give the task an end the worker can see

**What happened.**
- A worker finished its card and its tests passed. Then it re-ran the same test command, whose
  only output on success was the one word it echoed afterwards, **341 times in 9 minutes**.
  Every result was the same.
- Half an hour later, another worker on the next card did the same **1,126 times in 28
  minutes**, although it had already read its passing count.
- jichi's no-progress warning ("returned the same result 3x this turn") fired **once** each time.
- Both loops ended only when jichi's mid-turn compaction elided the repeated results from the
  worker's context. After that, each worker ran a few different checks and gave its answer.

**Why it happens.** Both tasks said "past about 150 calls, stop and report" and "do not claim
green without the count". The lead's own diagnosis, written into the team's memory afterwards,
is the right one:
- **A worker cannot see its own call counter,** so a call budget is a rule it cannot keep.
- **"Do not claim green without the count" governs what the report says, not when to stop.**

So the worker had no visible end, and a passing command is always safe to run again. In a model
that has started repeating itself, it is the most likely next call.

**What jichi does today:**
- The success-twin detector (M733) notes an identical call with an identical result **once per
  call per turn**, and there is no stop.
- A worker's whole task is one turn, so it is told once and then never again. M806 made the
  *failure* detector warn again at each multiple of its threshold, and taught it to see a command
  cut off at its own time limit. It did not change the success twin.

**What you write.** An end the worker can observe, and the loop named as forbidden:

```markdown
When the gate is green, your turn is OVER: give your report as your final answer and make no
further tool calls. A green build is not a task to repeat. Running it again changes nothing and
burns the budget.
```

The drive's lead adopted this. Most later task texts ended with an explicit stop, in several
wordings ("your turn is over…", "then STOP", "your final answer is the report"), and a few kept a
call budget next to it. The livelock came back only once, briefly: 8 identical runs, then the
report.

### 2.3 Bound every command that can run a program

**What happened.** 13 shell calls in this drive took ten minutes or more, **7.2 hours** in total:

| Kind | Calls | Total | What it was |
|---|---|---|---|
| Godot runs | 2 | 4.7 h | a scene run with no end, and a "check-only" run given a project instead of a script, which ran the game |
| X11/Xvfb window checks | 8 | 2.0 h | a check that waited on a window that had closed, or on a background process its cleanup never stopped |
| the project's own "build and run" step | 3 | 0.5 h | it compiles the game and then **plays it until the window is closed**; the arguments after `--` were not passed on |

**Why it happens.** A model reads "build and run" as "build", and a check as something that
ends. A program with a window, an engine loop or a server does not end by itself. Its output
does not say so, and the command simply never returns.

**What jichi does today:**
- `runTimeout` (config) bounds any shell command that has no `timeout` of its own. This drive
  set it to 900 s after five hangs, and a four-hour hang became a fifteen-minute one.
- The per-call `timeout` argument of `run_terminal_command` kills the command's whole process
  group.
- Since M806, the loop detector sees a command that keeps running to its own time limit.

**What you write:**
- In the profile: "wrap every command that can start a program in `timeout N`, and run a program
  by its binary with its frame or time limit, not through a build step that runs it".
- Put the right way to run your project's program into a skill (§4) or the rules file, not into
  memory (§5).
- Set `runTimeout` for every unattended run.

### 2.4 One owner per file, and the lead owns the shared files

**What happened.** Two workers each added a new test file, and each registered it in the same
root file that tells the build which tests to run. The pool merged the first worker's version of
that root file and dropped the second's, reporting "conflicts skipped (first writer kept):
`src/root.zig` (task 2)". So the second worker's test was in the tree but was never compiled or
run. It was a hollow gate until the lead read the report and added the line by hand.

**Why it happens.** `spawn_parallel` merges **one file at a time**, and a file touched by more
than one child keeps the first child's version ([PARALLEL.md](PARALLEL.md) § Merge). It never
merges lines. That is the right rule for code, and it makes "a list every task appends to" a
guaranteed conflict.

**What jichi does today:**
- The conflict is reported in the result.
- Since M803, the losing child's worktree is **preserved** under `refs/jichi/discarded/…`.
- Since M809, the result opens with a *Kept* section that names it. So nothing is lost, but the
  line is still not in the tree until someone adds it.

**What you write:**
- In each task: "Files you own: …". Disjoint sets.
- For a shared registry (a test root, a build file, an index, a changelog): **either one task
  owns it, or no task does and the lead adds the lines after the merge.** Say which.
- After a merge, as the lead: read the merge summary before anything else, and check that the
  test count grew by the number of tests the workers added.

**What is next (built, the scoped edit check).** The one-owner-per-file rule is enforced in two
layers. The first layer is the task-text instruction ("Files you own: …", disjoint sets). The
second layer is built: from the lead's second (configurable, N; 0 = off) edit of a file a worker
of the same run has just merged, the file tools return a tool error naming the file and the
owning task, with a hint to use a follow-up task or `jichi attempts show` / `recover` for
dropped work. The first edit is allowed, so a one-line fix after a merge costs no round. Because
the shell can write any file, jichi also checks after each tool call whether a merged worker
file changed by any means (sed, git checkout) and reports it in the next tool result with a
notice naming the path. A shell write is reported once; subsequent changes to the same path are
reported again after the next tool call.

### 2.5 Name the command whose output carries the number

A worker told "the suite count must be baseline + 1; read the number" needs a command that prints
the number. Bare `zig build test` prints nothing on success, and the build's summary option
prints the count. The drive's lead ran the summary form itself and first told its workers only
"`zig build test`". This was not the cause of the livelock in §2.2: a worker that had read the
count kept looping anyway. But it is the cheapest ambiguity to remove. **Write the exact command
and the exact line of output to read.**

### 2.6 Keep the worker's report out of the tree

**What happened.** The stop rule in §2.2 first said "write `REPORT.md` in your worktree root".
The pool copies new files into the live tree, and a lead that commits with `git add -A` right
after a merge commits them too. A worker's `REPORT.md` was **committed three times**, twice that
way and once by a subagent on purpose, and **removed five times**. A stale report in the
repository also meant that every new worktree started with a file named `REPORT.md` already
present, next to a rule that said "your turn is over the moment `REPORT.md` exists".

**What you write:**
- "Give your report as your final answer." The pool returns it to the lead, and since M807 a write
  child's transcript is kept as well.
- If a file is needed, give it a path outside the tree, or `.gitignore` it.

### 2.7 Small tasks, pre-diagnosed

- **Small.** Two runs and one worker died when the gateway refused a request past the model's
  context window. Each time, jichi's warning had said the history was "many small tool results
  rather than a few large ones, so there is little left to elide". A long task made of many small
  tool calls cannot be compacted. The fix that worked was smaller tasks (the day-one page, §10,
  finding 7).
- **Pre-diagnosed.** In phase B the lead probed the problem first:
  - where the round trip first differs;
  - which construct breaks it;
  - which line of Godot's own source answers it.

  Its task then said "the root cause is already proven; do not re-diagnose it; the fix is here".
  Several such tasks went from dispatch to commit within ten minutes.

### 2.8 Phrase boundaries as what to do

In `--auto`, jichi infers advisory constraints from the request. A task that says what *not* to
do can turn into a rule that refuses the wrong thing ([DRIVING.md](DRIVING.md) §7.1). Run
`jichi brief-check` on a task text before you use it; it shows the inferred rules without a model
call.

### 2.9 A task-text template

```markdown
<Card N>: <one sentence: the deliverable>.

Where you work: your own git worktree is your working directory. Every path below is relative to
it. You own these files, and only these: <list>. <The shared file> is the lead's; leave it
unchanged.

The problem, already diagnosed by the lead: <the root cause, with file:line and the evidence>.
The fix: <what to change>. Do not re-diagnose it.

Gate, run from your worktree, each command bounded:
1. `timeout 600 <build and test command that prints the count>`: read the line "<N>/<N> tests
   passed". It must be <baseline + k>.
2. <the second check, with its expected output>.
Show the new test red before the fix: <how>.

When both are green, your turn is OVER: give your report as your final answer (the claim, the
commands you ran, their counts, the files you changed) and make no further tool calls. A green
build is not a task to repeat.

If the task cannot be done as asked, say so in the report, with what you tried and what you saw.
That is an acceptable outcome.
```

## 3. Agent definitions: the rules every task inherits

**A child does not inherit its parent's context.** [SUBAGENTS.md](SUBAGENTS.md) says it plainly:
"no project rules, no memory, no glossary, no repo map". A `spawn_parallel` child runs through
the same path, so a lesson the lead keeps in `AGENTS.md` or in `.jichi/memory.md` never reaches a
worker. What a worker sees is:
- its task text;
- its agent profile's body;
- the sections jichi appends at any depth: the environment, the untrusted-content rule, the active
  constraints, and the edit-scope globs.

A worker can still `read_file` the rules if a task tells it to.

An agent profile (`.jichi/agents/<name>.md`; [SUBAGENTS.md](SUBAGENTS.md) § Named profiles) is
read with every task that names it. That makes it the right place for everything that is true
of **every** task: where the worker works, how it runs programs, what it does when refused, and
how it reports. It is also the place where one fix reaches every future task, where a fix in one
task text reaches one worker.

**What happened.** After §2.1's incident the lead added a "Where you work" block to both of its
worker profiles. This is its text, lightly shortened:

```markdown
Where you work:
- Your files live in YOUR worktree, addressed by RELATIVE paths -- never the live tree. A write
  child gets its own git worktree; its file tools refuse the live tree, but its shell can write
  there freely -- nothing fences the shell.
- If a file tool refuses a write, STOP and report the refusal. Re-check that you used a relative
  path; never route around it through the shell.
- A check that passes on the live tree while you are in a worktree is hollow evidence.
```

From then on, no `spawn_parallel` worker's call touched the live tree. The supervisor checked
the telemetry for it at every check-in. The two write *subagents* that came later worked in the
live tree by design (§1).

**What belongs in a profile:**
- where the worker works (above);
- how it runs programs: bounded, by their binary (§2.3);
- how it ends and what the report contains (§2.2);
- what it must never do in this project (in positive form, §2.8);
- its model: coders for implementers, a different model for reviewers;
- `readonly: true` for reviewers, so a review can never write.

**What does not belong in a profile:** facts about one card (they go in the task), and lessons
that are still being learnt (they go in memory until they are sure, §5).

**Read the envelope's warnings, and then read the diff.** jichi's goalpost warning ("edited a
test assertion … verify this fixes the code, not the goalpost") fired 303 times in this drive.
The kinds seen, without a count of each:
- A worker writing a **new** test, which is what its task asked.
- An honest repair of a wrong check (the day-one page, §10, finding 8), and later an honest
  repair of a wrong expected colour in an existing window check.
- An edit to an agent profile whose file **name** contains "test" (`weekend-test-writer.md`).
- A worker that changed what a data structure holds and loosened another card's committed test
  to match. It reverted both before it finished.

The warning is a pointer. It tells you where to look, and only a reading of the diff tells you
what happened.

## 4. Skills: procedures the team loads when it needs them

A skill ([SKILLS.md](SKILLS.md)) is a folder with a `SKILL.md`, loaded on demand. It suits a
**procedure**: how to run a window check under Xvfb and capture it, how to generate a fixture's
import cache, how to run the reference engine headless and bounded, the exact
`spawn_parallel` shape this team uses.

What this drive shows about skills is modest, and it is stated as such:
- **In the first 183 runs the lead called `load_skill` 7 times in 14,935 tool calls.** A skill
  the lead does not load does nothing. Name the skill in the task or the profile when it applies
  ("use the `x11-check` skill for any window check").
- **A skill's `allowed-tools` is advisory for the top-level agent,** and enforced only in a
  sub-agent through `restrict-tools` ([SKILLS.md](SKILLS.md)). Do not rely on a skill to fence
  the lead.
- **The lessons that cost this drive the most were procedures:** running a GUI program bounded
  (§2.3), a red check in a separate worktree (§6), generating an engine's import cache. The team
  kept the first of them in memory, where it fell out of the window (§5). A skill, or a profile
  for the workers, would have kept it where it is read.

## 5. Memory, rules, prompts and logs: which layer carries what

| Layer | Where | Who reads it, and when | Budget | Use it for |
|---|---|---|---|---|
| project rules | `AGENTS.md` or `CLAUDE.md` | the lead, at every run's start; **not** its children | 32 KB | the lead's standing rules |
| agent profile | `.jichi/agents/<name>.md` | every task that names it | per task | how a kind of worker behaves (§3) |
| skill | `.jichi/skills/<name>/SKILL.md` | whoever loads it | on demand | procedures (§4) |
| standing prompt | the harness's prompt file | the lead, every run | the prompt | the finish line, the order of work, the deadline |
| memory | `.jichi/memory.md` | the lead, every run, **newest 8 KB only**; not its children | 8 KB | recent, dated lessons, not yet promoted to a rule |
| log and gates table | the project's own files | whoever the prompt tells to read them | the context | what happened, and what is true now |

**The memory file keeps its newest 8 KB, and a team keeps its most important lessons at the
top.** That combination lost lessons twice ([MEMORY.md](MEMORY.md) § Design decisions: "only the
last 8 KB is injected").
- By Sunday night the team's memory file was 26 KB. The lesson that cost the most, "this build
  step runs the game until the window closes; compile with the plain build and run the binary
  with its frame limit", was written twice, at bytes 7,820 and 15,369. The loaded window began at
  byte 17,827, so neither copy reached the prompt. The next lead ran the build step and lost ten
  minutes to it, at least the fourth time the drive paid for this one mistake.
- After a question from the supervisor, the lead pruned the file to 6,248 bytes and put the
  costliest lessons **first**. Fourteen runs later, new notes had brought it to 9,475 bytes, and
  the first twenty lines were out of the window again. That is where the pruned file kept its
  most important lessons. At the drive's end the file was 9,046 bytes, and the loaded part began
  in the middle of that same lesson ("…step does not exist; the real one is `run-pong`").
- jichi warned every time ("memory: .jichi/memory.md is 25 KB; only the most recent 8 KB is
  loaded"), 205 times in all, and the `remember` tool's result said so too. Since **M825** that
  warning also **names the headings it drops** ("dropped: …"), so the loss is visible at a glance
  rather than a byte count you have to reason about.

**What you do:**
- Promote a lesson out of memory once it is sure:
  - to the rules file if every run of the lead needs it;
  - to a profile if a kind of worker needs it (a worker never sees memory);
  - to a skill if it is a procedure.
- If you prune memory, remember which end survives. Anything that must load either goes last, or
  goes out of memory altogether.
- Treat the memory warning as an instruction, not as noise.

The standing prompt is code, and a finding that changes the plan changes it in the same hour.
The log repeats itself. Both are on the day-one page, §4 and §5.

## 6. The lead's own discipline: merged work, red checks, commits

**What happened, twice in one night.** A batch had merged into the live tree, uncommitted, and the
lead then wanted to show that the new test fails without the fix:
- **The first lead** appended a scratch test to the shared root file, ran it, and cleaned up with
  `git checkout -- src/root.zig`. That also threw away the merged, uncommitted line that
  registered the worker's test. The new test would no longer have run, and the lead had not
  noticed.
- **The second lead** replaced a merged file with its committed version (`git show
  HEAD:src/editor/scene_writer.zig > src/editor/scene_writer.zig`) to run the red check. It then
  ran `git checkout src/editor/scene_writer.zig`, and its own message said "restoring from the
  working tree". A checkout restores from the **index**, which here meant the last commit, so the
  worker's merged change was gone.

**How each was found:**
- **The first** was caught by a question from the supervisor, and the line was restored.
- **The second** the lead noticed itself, when the build broke. It rewrote the change from what it
  had read of the diff. A question from the supervisor then pointed at a case the rewrite no
  longer handled: a scene without an id lost its header line. The lead fixed that with a new test.

**What jichi does today.** Since M803 a worker's worktree is preserved when the pool removes it.
It is kept under `refs/jichi/discarded/<reason>/…`, readable with `jichi attempts show <ref>`
(M815). That covers work lost **in the pool**. It cannot cover work the lead destroys **in the
live tree** after a merge. That is git's business, and the lead's.

**What you write, in the lead's standing prompt or rules.** The rule the drive's lead wrote into
its memory, and kept to afterwards: "commit merged worker work immediately; RED checks in a
detached worktree, never in /work". Written out, with the commands it then used:

```markdown
Commit merged worker work immediately, before any other edit to the tree.
Run a red check in a separate worktree, never in the live tree:
    git worktree add /tmp/red HEAD      (break the fix there, run the test, see it fail)
    git worktree remove /tmp/red        (then delete it)
```

Add two lines the drive shows are needed as well:
- "Never use `git checkout -- <file>`, or redirect `git show` into a file, in a tree that holds
  uncommitted work."
- "Stage the files you mean (`git add <paths>`), not `git add -A` right after a merge."

Two more habits the drive's reviews relied on:
- **After every merge, check that the test count grew** by the number of tests the workers
  added. An unregistered test file never runs. A count that did not grow is how the lead caught
  one of the four hollow tests the drive found.
  - jichi's envelope makes the same check when its verifier runs, if the verifier prints a count
    (M692): "a test file was edited, and the observed test count did not grow … The new test
    likely never runs".
  - A bare `zig build test` prints no count, and then the check cannot be made. Configure the
    verifier with the build's summary option.
- **Read the reviewer's findings before the commit, and fix what it accepts.** When the lead
  skipped a review, it said so in the log.

## 7. Supervising a team: for a person, or an agent driving the lead

### 7.1 What reaches a lead, and when

| You send | While the lead is between calls | During a long shell command | During a `spawn_parallel` batch |
|---|---|---|---|
| `status` | answered | answered within about a second (M801) | answered (M801) |
| `inject` | applied at the next model call | queued to the next boundary | queued until **the batch returns** |
| `pause` | pauses at the boundary | deferred to the boundary | "launch no new task": the running children continue (M804) |
| `abort` | at the boundary | deferred to the boundary | served in the batch: children are killed, then **preserved** (M804) |
| a note in the harness's inbox | the **next run** reads it at start (this drive's harness put it into the next prompt) | same | same |

Nothing reaches a **worker** during a batch. Write what a worker must know into its task or its
profile before the batch starts.

**The client's own limit.** `jichi control … inject` waits up to 300 s for its reply (lower it with
`JC_CONTROL_CLIENT_SECS` when you would rather poll than block). Twice in this drive, an inject sent
during a batch outlived that and printed a "no reply within the wait … delivered and is queued"
error. Both injects landed after the batch returned. Do not re-send: that doubled one earlier inject.
Since **M823** the client makes this scriptable — that queued case **exits 3**, distinct from the **1**
a failed connect returns — so a supervisor can wait-and-re-check on 3 and treat 1 as a gone run. Or
watch the stream for the lead's status line `operator steering injected`.

### 7.2 What to say

The pattern that worked every time:
- the **facts first**, each checkable, with the command or the telemetry that shows it;
- then **a question**;
- **no answer**.

For example:

```text
Facts: (1) After the batch, `git status` showed src/root.zig modified: the worker's
registration of its test. (2) Each of your probe commands since then ends in
`git checkout -- src/root.zig`. (3) The registration is now gone, and the suite no
longer runs the new test.
Question: Is the worker's test still wired in, and how many tests does the suite count
now? Before you commit, how will you make sure a probe cannot discard uncommitted work?
```

All five questions of the drive's last night were acted on in the same run, within minutes of
landing. Three of them became lasting rules, written by the team itself:
- the report-and-stop rule (§2.2);
- the commit-first and red-check rule (§6);
- the profiles' "Where you work" (§3).

On the first day, a note that carried its evidence and a command to reproduce it was acted on,
and a question the team could answer by assertion got an assertion back (the day-one page, §7).

### 7.3 What to watch

| You see | It usually means | What to do |
|---|---|---|
| the same call with an identical result, dozens of times | a finished worker with no visible end (§2.2) | save its worktree if it matters; fix the next task text |
| a worker's commands naming the live tree | a task that pointed it there (§2.1) | check the telemetry for writes, and fix the task and the profile |
| "refused by safety fence", then shell writes to the same place | a worker routing around the fence | the same, urgently |
| a command with no `timeout` that runs a program, long after it should have ended | §2.3 | let `runTimeout` end it if it will soon. Stop it yourself only if nothing will |
| "conflicts skipped (first writer kept)" | a shared file (§2.4) | check that the lead re-adds what was dropped |
| "edited a test assertion" on an **existing** test | a goalpost, or an honest fix | read the diff (§3) |
| the memory warning | lessons are no longer loaded (§5) | ask what must load, and where it should live |
| a *Kept* section in the batch result | a child's work was preserved, not merged | make sure the lead reads it (`jichi attempts show`) |

### 7.4 When to act, and when not to

- **Let a bound end what it will end soon.** A worker's command two minutes from its 900 s limit
  will end on its own. Killing it early hands the model an unexplained failure to debug.
- **Stop a clearly hung process when nothing will end it,** with the operator's permission.
  Record every stop.
- **Save before a risky moment, read-only.** A copy of a worktree's diff and its untracked files
  costs seconds. This drive made 13 such copies on its last night and needed none of them, which
  is the right outcome for insurance. Since M803, jichi preserves a pool's worktrees itself.
- **Record every step-in:** what you saw, what you sent and why, and what came of it. The drive's
  supervision log is how this page could be written at all.
- **Use tools that cannot block you.** A supervisor waiting on a permission prompt is not
  supervising. That happened for five hours on the first night (the day-one page, §8).

### 7.5 The end of a drive

At the deadline the harness sends the lead SIGINT.
- **On the drive's pinned build,** a lead inside a batch would have killed its children and
  removed their worktrees, losing the unmerged work.
- **Since M803 and M804,** an interrupted batch is preserved.

Either way, tell the team the end time in the standing prompt ("no new batch after 09:15; merge
and commit by 09:45"). From 09:23 on, this drive's lead only verified and closed. The last run
ended clean, with both of the harness's deadline saves finding nothing left to save.

## 8. Long unattended drives: the harness

Most of this is on the day-one page (§9, the configuration table) and in
[CONTAINER_TUTORIAL.md](CONTAINER_TUTORIAL.md) and [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md).
What the whole drive adds:

- **Caps off, fences on,** and the difference is now in jichi itself. The dead-stream fence
  (`timeouts.deadStream`, default 900 s, M810) stays on when the stall cap is off. In this drive
  one call ran **11,677 s** with its stream dead; healthy calls took at most 540 s over 12,464 of
  them. The disk fence (`diskFloorMb`, default 10 MB, M813) stops a run before a full disk corrupts its
  records; set the floor higher for a long drive.
- **Set `"continueCutReply": "headless"` for a supervised headless lead.** A lead that runs
  unattended has nobody else who can say "continue" to it, and its final reply can still be cut
  at the output ceiling -- the drive measured this: a 25-minute lead ended on a cut reply, one
  memory note committed, and nothing asked for the rest. Since M965 the default for a headless
  top-level run when nothing is set is "headless" semantics (a supervised headless lead gets the
  continuation by default); set "delegates" to restore the old default, "off" to disable. The
  remedy is in the binary and the drive's config did not use it, which is why this page now
  names it (D1, the zigodot drive 2026-10-11; `continueCutReply` in [CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md)).
- **Make sure no process in the container holds the key except jichi, and scan every push for
  it.** jichi keeps its own copy out of the model's reach, but it cannot protect a copy held by
  another process. This drive found one, removed it on its second day, and scanned every push
  from then on. The scans found no key. Install the key-scan as a `pre-push` hook **executable**
  (`install -m 755`, not `cp`): git silently **skips a non-executable hook** and prints only a
  *hint*, so a scan that fails closed when it runs fails open when it does not — make the harness
  refuse to start unless the hook is executable. See [SECURITY.md](../SECURITY.md) and
  [CONTAINER_TUTORIAL.md](CONTAINER_TUTORIAL.md).
- **Read what the run did through the container, not with git on the host.** The workspace is
  mounted read-write, `.git` included, so a model can leave a hook (`pre-commit`, `post-checkout`)
  or a config entry (`core.fsmonitor`, `core.sshCommand`, `diff.external`) in it — no malice needed,
  a formatter hook is enough. `git help git` §SECURITY: *"it is not safe to run Git commands in a
  … working tree … when that .git directory itself comes from an untrusted source"*, and a rootless
  `--userns=keep-id` container writes files you own, so `safe.directory` does not save you. After a
  run, read the diff **inside** the container (`podman exec … git -C /work diff`) or by fetching the
  branch into a bare mirror through a contained `--upload-pack` — never `git diff`/`git log`/`git
  status` on the host in that workspace. The same holds for jichi's shadow repo and `jichi attempts`
  against a HOME the container wrote.
- **Push the records off the machine, often.** A model's shell once deleted a drive's data
  ([ANECDOTES.md](ANECDOTES.md) #108). This drive's records went to another server every 20
  minutes.

## 9. What jichi does for you now (M800–M827)

| The drive's failure | What jichi does since | How you use it | What it does not cover |
|---|---|---|---|
| a lead unreachable for 12.9 h inside batches | `status` mid-call (M801); `abort` and `pause` in a batch (M804) | `jichi control <socket> status` | `inject` still waits for the batch |
| a dead stream ran 11,677 s | the dead-stream fence (M810) | on by default; `timeouts.deadStream` | a slow but live stream (that is a cap's job) |
| a stopped or conflicting worker's work deleted | preserved (M803), with an end record (M808), a transcript (M807), a *Kept* line (M809) | `jichi attempts show` (M815) | the lead's edits (§6) |
| a dead run's worktrees orphaned, or a same-pid run colliding with them | a run-id base, a lock, a manifest (M811); salvage (M812) | automatic; `jichi doctor` names orphans (M814) | — |
| a disk that could fill during a drive | the disk fence (M813) | `diskFloorMb`; `jichi doctor` shows it | — |
| child telemetry named the lead's model | children are named, with their own model and seq band (M800) | per-child analysis now works | — |
| a secret a tool printed reached the stream and the session | both sinks redact registered secrets (M802, M802b) | automatic | a secret jichi never registered |
| a check cut off at its own time limit, repeated | the loop detector sees it, and warns again as it persists (M806) | automatic | an identical **success** repeated (§2.2) |

## 10. A checklist

**Before the drive**
- [ ] The lead's model chosen for judgement, the workers' for code, the reviewers' different from
      the writers'.
- [ ] Each worker profile has "Where you work", the refusal rule, bounded programs, and the
      report-and-stop rule.
- [ ] The procedures are in skills, the lead's standing rules in the rules file, the workers' in
      their profiles, and memory under 8 KB.
- [ ] `runTimeout` set, and caps off, fences on.
- [ ] The key does not reach any process except jichi; records pushed off the machine.
- [ ] The deadline written into the standing prompt.

**Every task text**
- [ ] Relative paths only, and the files the worker owns listed; shared files named as the lead's.
- [ ] Every command that can start a program bounded.
- [ ] The exact gate commands, and the output line to read.
- [ ] The end: "give your report as your final answer; make no further tool calls".
- [ ] An escape hatch for "this cannot be done as asked".
- [ ] `jichi brief-check` run on it.

**After every batch, as the lead**
- [ ] The merge summary and the *Kept* section read first.
- [ ] Committed before any other edit.
- [ ] The test count grew by what the workers added.
- [ ] A red check, if any, run in a separate worktree.

**While supervising**
- [ ] Facts, then a question, then no answer; one inject, not re-sent.
- [ ] The §7.3 signatures watched; every step-in recorded.

## 11. Something to do

Each exercise has a **read-only twin** for a reader without the tools.

**1. Audit a task text.** Here is a task text in the shape the drive started with. Find the six
problems that §2 names:

```markdown
Card 12: add the lights demo to the editor fixtures. Copy it from /ref/demos/lights to
/work/tests/fixtures/lights and add a test in /work/src/editor/lights_test.zig; register
it in src/root.zig. Run `zig build test` and make sure the count went up. Check that the
demo loads by running the engine on it. Past about 150 calls, stop and write REPORT.md.
```

*Read-only twin: the same exercise.* The six problems:
- the live-tree paths (§2.1);
- a call budget as the end (§2.2);
- an unbounded engine run (§2.3);
- a shared registry with no owner (§2.4);
- a command that does not print the count (§2.5);
- a report file in the tree (§2.6).

**2. Watch jichi's own guards fire, on the mock model.** These smoke drivers reproduce the drive's
failures offline and show the fix. They use no network and no real model. Run them in a
terminal, from the root of a jichi checkout, after `make` and `make smoke-tools`:

```sh
sh tests/smoke/tool_loop_atbound.sh         # M806: the loop detector and a command cut off at its limit
sh tests/smoke/parallel_preserve.sh         # M803: a conflict loser's worktree is preserved
sh tests/smoke/parallel_abort_preserves.sh  # M804: an abort mid-batch keeps both children's work
sh tests/smoke/parallel_reach.sh            # M801: status answered during a long call
```

*Read-only twin:* open each driver and read its header. Then predict which check goes red if the
fix is removed. Each driver names its red-first proof.

**3. Measure your memory file.** In a terminal, in your project's directory, see how much of it
jichi loads:

```sh
wc -c .jichi/memory.md
tail -c 8192 .jichi/memory.md | head -n 3
```

The second command shows where the loaded part begins. Is a lesson you care about above that
line? *Read-only twin:* take any 10 KB text file and do the same arithmetic.

**4. Write the profile.** Write a worker profile with §3's "Where you work" block, §2.2's end, and
§2.3's rule for programs. Then run `jichi brief-check` on it, and read which advisory rules jichi
infers from it. *Read-only twin:* read [AUTONOMY.md](AUTONOMY.md) § `brief-check` and predict them.

## 12. What this page cannot tell you

- **One project and one operator.** The drive used free local models, the pinned build of
  2026-09-25 (M770), and one supervising agent.
- **The shape of the findings is likely to transfer;** the numbers are not. The findings are
  livelocks without a visible end, unbounded programs, first-writer merges, a tail-kept memory,
  and destructive checkouts in a tree with uncommitted work.
- **The two phases are not a controlled comparison** of two lead models (§1).
- **What happened in skills is thin evidence** (§4).
- **Some of jichi has changed since the drive** (§9), and this page says what jichi does today
  as of M815. The drive's own build behaved as the "failure" column says.

## Read further

- [The weekend drive, complete](analysis/2026-09-28-the-weekend-drive-complete.md): the record
  and its numbers (primary).
- [What a jichi team must be told](analysis/2026-09-26-what-a-jichi-team-must-be-told.md) and
  [the weekend drive, day one](analysis/2026-09-26-the-weekend-drive-day-one.md): finish lines,
  gates, prompts, and the first 17 hours (primary).
- [DRIVING.md](DRIVING.md): one driven run, its brief, its gate and its caps.
- [PARALLEL.md](PARALLEL.md), [SUBAGENTS.md](SUBAGENTS.md), [SKILLS.md](SKILLS.md),
  [MEMORY.md](MEMORY.md), [CONTROL.md](CONTROL.md): the mechanisms this page leans on.
- [SNAPSHOTS.md](SNAPSHOTS.md) and [AUTONOMY.md](AUTONOMY.md): preserved attempts, the envelope
  and its warnings.
- [ANECDOTES.md](ANECDOTES.md) #112–#118: the drive's war stories.
