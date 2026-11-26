# What to teach next: extending the courses, the bibliography and the assignments

*Written 2026-11-23 by the host supervisor (Claude), on the operator's request: "Of the topics of software
development, programming, project management and the focus of self-learners which do you recommend extending with
reading courses, bibliography, exercises, and assignments? Please, document your honest grounded decisions,
including alternatives, and implications." Measured on the harness-port drive's team head f54735c3.*

> **Who this is for.** The operator, who decides what is built; whoever writes the next course, task or bibliography
> section; and self-learners and junior developers who want to know why the material covers what it covers. If you
> only want to start learning, read [`CURRICULUM.md`](../CURRICULUM.md) instead.
>
> **What this page is.** Four recommendations, each with the alternatives considered and what it costs, and the
> operator's decision on them. **Decided on 2026-11-23:** R1, R2 and R3, in that order; R4, the self-learner shelf, is
> left out. The decision is recorded in [`DECISIONS.md`](../DECISIONS.md#dec-2026-11-23-what-to-teach-next); each
> piece of work gets a card when it starts.
>
> **What you need first.** Nothing. Section 1 says what exists, with the commands that counted it.

## 1. What exists now

All counts were taken at f54735c3. The commands are in [section 7](#7-how-the-inventory-was-measured).

**Graded assignments: 90**, each with a `verify:` grader proven red-first in CI. By stage:

| Area | Stages (graded tasks) | Total |
|---|---|---|
| The craft core | shu (9), ha (5), ri (4) | 18 |
| Programming: languages | Racket, Guile, Elixir, Haskell, Clojure, Zig, C++, Rust, Python (4 each) | 36 |
| Programming: C and systems | c-systems (4), c-io (5), memory (3), migration (4) | 16 |
| Project process | process (9): requirements, use cases, design, documentation, session notes, kanban, scheduling, decisions, a stale register | 9 |
| Extras | extras (8): portability, a third compiler, C standards, reading a real project, works-on-my-machine, the signed byte, reading a turn, grounded discourse | 8 |
| First steps with jichi | plain (3) | 3 |

**Reading guides: six.** Five on jichi's own source: Annai (10 chapters and an appendix, the first tour), Fukabori
(12, the design in depth), Tsuiseki (4, traces of real turns), Ireko (7, the JSON layer) and Tazuna (10, the
harness). The sixth, Kiroku (one page), teaches reading the project's record rather than its code. The plan
[`plans/2026-11-17-reading-courses.md`](../plans/2026-11-17-reading-courses.md) lists six more source courses, each
waiting until its code is stable: portability, the terminal, code search, the protocol clients, running other
programs, and the fences.

**Tutorials: fourteen**, among them testing, architecture, state machines, UML, use cases, pseudocode, domain
modelling, interfaces and orchestration.

**The bibliography: 194 entries in fourteen sections** (the page's own figure, held by `bibliography_lint`). Most
sections are about the craft (50 entries) and the languages; there are sections on interfaces (18 entries) and on
human-computer interaction.

**Written, but not in the tree at f54735c3.** The object-oriented and logic-programming tutorials, with two new
bibliography sections and 14 example programs, were handed over on 2026-11-21 and never applied
([`ANECDOTES.md`](../ANECDOTES.md) 131). They were re-cut on 2026-11-23 and land with this page; the bibliography then
holds 228 entries in sixteen sections.

### What the tree itself says it does not teach

[`CURRICULUM.md`](../CURRICULUM.md), "What this course does not teach", counts **zero** graded tasks on concurrency,
networking and protocols, security, persistence, user-interface work, and algorithms as a subject. It calls these
"absences of scope, not oversights", and says of one of them: "For concurrency and distributed work specifically, the
honest answer is that this course does not cover them and a dedicated one should."

### What the bibliography has, and lacks, per area

I searched it for the standard works of each area (the search is a signal, not the answer; I read every hit).

- **Project management:** three works. *The Mythical Man-Month* (Brooks), *Writing Effective Use Cases* (Cockburn),
  *Release It!* (Nygard). Nothing on estimation, risk, queues and flow, or requirements as a discipline.
- **Security:** the SEI CERT C Coding Standard and Seacord's *Effective C*, both read as C references. Nothing on
  threat modelling or security engineering as a field.
- **Concurrency:** Stevens and Rago's *Advanced Programming in the UNIX Environment* and Kerrisk's *The Linux
  Programming Interface* (processes, signals, pipes), and *C++ Concurrency in Action* (threads). Nothing on
  concurrency as a subject (ordering, races, coordination).
- **How people learn:** nothing. The one Ericsson entry is the think-aloud protocol book, read for usability testing.

## 2. How I decided

Four questions, asked of every candidate topic:

1. **Is it a real gap?** The tree's own "does not teach" list, a bibliography shelf that is empty, or an area with
   few tasks.
2. **Is it grounded in this tree?** A course on jichi's own code or records can be checked: `reading_quotes_lint` and
   `reading_refs_lint` turn a course red when the code it quotes changes, and a grader can be proven red-first on a
   real defect. A course on material jichi does not have would be, in the curriculum's own words, "teaching a second
   subject, less well".
3. **Does a learner get something here that the standard literature does not give?** Real defects with their red
   tests, measured data, a project's honest record of its failures.
4. **What does it cost to build, and to keep true?** Each graded task needs a spec, a grader, a reference solution,
   a red-first proof in CI, and updated counts (`docs_counts_lint` holds the task and trap figures on several
   pages).
   A bibliography entry needs its ISBN or URL checked and dated.

## 3. The recommendations, in order (R1 to R3 decided; R4 left out)

### R1. Project management: running work with agents (first)

**What:** extend the process track with three to five graded tasks, and add a bibliography section on estimation,
risk and flow.

The tasks, each built from this project's own drive records (the case studies in
[`case-studies/`](../case-studies/) publish them, with machines described generically):

- **Estimate from data, then check.** The learner gets a drive's measured history: how long each batch took from its
  first commit to its landing, how long the gate takes (about 70 minutes on the host), how often a batch needed a
  second try (about half of recent batches). They estimate when a named batch lands and write down the basis. The
  actual landing is in the record. This goes one step past task 73, which checks that milestones carry estimates and
  a retrospective: here the estimate is made from a reference class of past batches.
- **A handover is done when its files are in the tree.** The learner gets an inbox of handovers and the history that
  followed. They must find which handovers landed and which were acknowledged and lost. The fixture is real: of the
  ten documentation handovers in this drive up to 2026-11-23, eight landed and two were answered "acknowledged,
  queued" or "land with the docs batch" and never applied ([`ANECDOTES.md`](../ANECDOTES.md) 131 tells both).
- **A trigger nobody watched.** A plan says "do X as soon as A, B, C and D have landed". The learner gets the
  landing record and must say when X became due, and what would have made someone notice. The fixture is real too: a
  release was planned for "as soon as" four items landed. Three landed over 17 hours, the fourth only in part, and
  nobody asked whether the release was due. Deciding whether a half-done precondition counts is part of the task.
- Optionally, **a queue**: why "queued at the tail" turns "soon" into "after everything else", read with a flow model.

**Why first:**
- It is the thinnest of the four areas: 9 graded tasks and three books. Kiroku already teaches how to read
  the record; these tasks ask the learner to act on it.
- The material is jichi's own and measured, and few learners can get it elsewhere: what actually goes wrong when one
  person runs a team of agents.
- The lessons recur. In the project's internal lessons ledger (not in the public tree), 23 of 174 lessons are
  addressed to a supervisor or a lead, and the same handover failure happened twice in one drive.

**The bibliography section (candidates, to be checked before they are entered):**
- Steve McConnell, *Software Estimation: Demystifying the Black Art* (2006).
- Tom DeMarco and Timothy Lister, *Waltzing with Bears: Managing Risk on Software Projects* (2003), and *Peopleware*.
- Donald G. Reinertsen, *The Principles of Product Development Flow* (2009): queues, batch size, cost of delay.
- Karl Wiegers and Joy Beatty, *Software Requirements* (3rd edition, 2013).
- Daniel Kahneman, *Thinking, Fast and Slow* (2011), for the planning fallacy and the outside view.

**Alternatives considered:**
- **A general project-management course** (roles, ceremonies, a methodology). Rejected: it is taught everywhere, and
  jichi has nothing of its own to ground it in.
- **Bibliography only.** Cheaper, but the gap is in practice, not reading: the failures above were made by people and
  agents who knew the rule.

**Implications:**
- A grader can check the method (a basis written down, a comparison with the actual), never whether an estimate was
  good. Task 73 already says so honestly ("the floor vs. the judgment"); the new tasks must say it too.
- Fixtures come from drive records. They must name no host, network or person, as the case studies already do.

### R2. Software development: security, from jichi's own fixed defects (second)

**What:** four to six graded tasks and a security bibliography section. A security reading course follows later.

The tasks, each a frozen copy of code before a fix, with the red test that proved the defect:
- **The check that failed open.** Five such checks were fixed in M1208: an oversized redirect line that skipped a
  counter, a command hidden behind more than 64 tokens, a 33rd secret dropped from redaction, a push listing cut at
  its capture cap and sent half-scanned, a 17th directory that escaped a limit. Each fix had a red-first cell. The
  learner writes the red test first, then the fix.
- **The memory error a sanitizer finds.** M1210's three: a heap overflow in a truncator, a use-after-free, a stack
  overflow from a missing NUL. The learner runs the SAN build, reads the report, fixes with lifetimes only.
- **A threat model of one tool.** Who can write each input of one jichi tool (the user, a config file, a model, a
  web page), what the fence is, and what happens when it fails. [`LIMITS.md`](../LIMITS.md)'s "fence or cap" and the
  project's rule on who wrote a JSON value are the instruments.

**The bibliography section (candidates, to be checked):**
- Ross Anderson, *Security Engineering* (3rd edition, 2020).
- Adam Shostack, *Threat Modeling: Designing for Security* (2014).
- The CERT C standard and *Effective C* are already in the C section; the new section points to them.

**Why second:** the gap is total (0 tasks), the material is real and already proven (every defect above has its red
test), and a C programmer who learns to write the red test for a fail-open check has learned something most courses
do not teach.

**Alternatives considered:**
- **Wait for the "fences" reading course** (planned after the prompt-injection work, PI-1 to PI-7). Rejected for the
  tasks: a task that carries its own copy of the code it grades does not change when jichi's code does. The reading
  course should still wait, because it quotes live code.
- **An exploitation course.** Rejected: the project teaches finding, testing and fixing its own defects. Attacking
  systems is a different subject with different obligations.

**Implications:** every task is defensive. The "does not teach" table in CURRICULUM.md must change when the first task
lands, and so must the counts that `docs_counts_lint` holds.

### R3. Programming: concurrency through processes (third)

**What:** a concurrency track built on jichi's model, processes rather than threads: fork, pipes, signals, process
groups, and what goes wrong between them. Four to six graded tasks, then the "running other programs" reading course
when its code settles.

The project's own races are the fixtures:
- a fork and `setpgid` race, fixed in the M24-M29 quality pass;
- the lease race (M760);
- a backgrounded subshell whose `$!` names the wrong process on some shells (`smoke_lint` check 15 exists for it).

**Why third, not first:** the curriculum itself says a dedicated course should exist, and jichi has real material.
But it is the largest of the four to build well, the reading course waits for the def-269 design and PI-4, and the
literature for processes is already on the shelf (Stevens and Rago, Kerrisk).

**Bibliography candidates:** Allen B. Downey, *The Little Book of Semaphores* (free); Leslie Lamport, "Time, Clocks,
and the Ordering of Events in a Distributed System" (1978); Herlihy and Shavit, *The Art of Multiprocessor
Programming*, read for contrast with threads.

**Alternatives considered:**
- **A threads course.** Rejected: jichi uses no threads (C89, POSIX processes; `include/jc_lease.h` says it "has no
  lock of any kind"). A threads course would have nothing in this tree to ground it.
- **Leave it to the Elixir track,** which treats processes as the unit of failure. Rejected: Elixir's processes hide
  the operating system, and the races above live in the operating system.

## 4. Not recommended now, and not taken

- **R4, the self-learner shelf: left out by the operator on 2026-11-23.** It was a bibliography section on how people
  learn programming (Hermans, *The Programmer's Brain*; Brown, Roediger and McDaniel, *Make It Stick*; Ericsson and
  Pool, *Peak*; Pólya, *How to Solve It*; Hoover and Oshineye, *Apprenticeship Patterns*), with two or three short
  exercises (predict, then check, with `/predict`; explain a change back; read the code before asking), and a full
  course only after the learner study ([`LEARNER_STUDY.md`](../LEARNER_STUDY.md)) has run. What leaving it out means:
  the bibliography keeps no section on learning itself, and the learner study stays the first step for any course
  addressed to how self-learners work.
- **Persistence and databases.** jichi stores sessions as files; there is no database to ground a course in. Task 77
  (replace a file without losing it) already teaches the part jichi has.
- **Algorithms as a graded track.** Decided on 2026-09-25: "a reading page first, a graded track later, once the
  page has been read". [`ALGORITHMS.md`](../ALGORITHMS.md) is that page, four lessons measured on this tree. Nothing
  says yet that a learner has read it; the learner study would.
- **Graded user-interface work.** The interface tutorial and the HCI section of the bibliography exist. Grading UI
  work needs users, which is the learner study again.
- **Networking and protocols as a new track.** Ireko covers the JSON layer; the protocol-clients reading course is
  planned once PI-2 and PI-5 land. A separate track would duplicate it.
- **More language tracks.** Nine exist, and the OOP and logic tutorials are written. The bibliography's own rule is
  that a section is added "when there was a reason, never alphabetically".
- **LLM and agent internals.** The bibliography leaves them out on purpose: "it dates in months". R1 teaches the
  practice from this project's records, not the literature.

## 5. The order, and what each step costs

1. **Land what is already written:** the OOP and logic-programming tutorials (handed over 2026-11-21, never applied).
   Done with this page: re-cut on 2026-11-23, every published command re-run in the tree.
2. **The bibliography sections for R1 to R3.** Cheap, and each entry is checked and dated as the page requires.
3. **R1's tasks** (project management). Medium: the fixtures exist in the drive records.
4. **R2's tasks** (security). Medium: the fixtures exist as fixed commits with red cells.
5. **R3's tasks** (concurrency), then its reading course when def-269 and PI-4 land. Large.

**For every task:** a spec, a grader, a reference solution, a red-first proof in CI, the counts in CURRICULUM.md and
the README (`docs_counts_lint`), and the "does not teach" table when a whole area stops being absent. The curriculum
and the bibliography have no translations, so no translation lags behind.

## 6. What I did not check

- **The bibliography candidates are not verified.** No ISBN, edition or URL above was checked; each is checked and
  dated before it enters [`BIBLIOGRAPHY.md`](../BIBLIOGRAPHY.md), as its rules say.
- **The keyword search over the bibliography** finds titles and authors, not coverage; I read every hit, but a book
  that teaches estimation under another title would have been missed.
- **The ledger figure** (23 of 174 lessons addressed to a supervisor or a lead) is a pattern match on one field.
- **The size estimates** (three to five tasks, four to six) are judgment, not measurement.

## 7. How the inventory was measured

From the top of a checkout of f54735c3:

```sh
sh -c 'for f in docs/assignments/*.md; do grep -q "^verify:" "$f" && grep -m1 "^stage:" "$f"; done | sort | uniq -c | sort -rn'
sh -c 'ls docs/reading/*.md | wc -l'
sh -c 'for k in "Software Estimation" "Waltzing with Bears" "Security Engineering" "Threat Modeling" "Programmer.s Brain" "Make It Stick"; do printf "%s: " "$k"; grep -c -i "$k" docs/BIBLIOGRAPHY.md; done'
```

The first prints the 90 graded tasks by stage (shu 9, process 9, extras 8, ha 5, c-io 5, four each for the nine
languages and for c-systems, migration and ri, three each for memory and plain). The second prints 50: the
chapter pages, one index page per guide, and Kiroku. The third prints 0 for each of six works this page proposes
to add (and exits 1, because `grep -c` does when it finds nothing).

## Read further

- [`CURRICULUM.md`](../CURRICULUM.md): the course, and what it does not teach.
- [`BIBLIOGRAPHY.md`](../BIBLIOGRAPHY.md): the reading, and its rules for an entry.
- [`plans/2026-11-17-reading-courses.md`](../plans/2026-11-17-reading-courses.md): the next reading courses.
- [`LEARNER_STUDY.md`](../LEARNER_STUDY.md): the study that should come before a course addressed to how learners work.
- [`ANECDOTES.md`](../ANECDOTES.md) 131: the two lost handovers, which R1's second task is built from.
- [`case-studies/`](../case-studies/): the drive records R1's fixtures come from.
