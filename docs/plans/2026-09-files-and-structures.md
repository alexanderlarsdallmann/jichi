# Files and data structures — the plan

**Status:** planned, not built. **Written:** 2026-09-16, after M636d measured the
gap. **Operator brief:** *"The courses must explain honestly, and comprehensively
the design decisions for jichi, and still teach alternatives for other
use-cases."* That sentence is the design constraint, not a preamble — every
section below is answerable to it.

## 1. The gap, as measured

From [`analysis/2026-09-16-language-teaching-coverage.md`](../analysis/2026-09-16-language-teaching-coverage.md)
§3b, counted over 147 markdown files plus the fixture trees:

| | Taught in the curriculum | Present in jichi's own `src/` |
|---|---|---|
| Reading/writing a file | **0 of 79 tasks, 0 of 16 reading chapters** | 26 files (`fopen`/`fread`/`fwrite`), 17 POSIX `read`/`write`, 5 `stat`, 4 atomic `rename`, 9 setting `0600`, **0 `fsync`** |
| Hash table | never | **none** |
| Linked list | never | **none** |
| Tree | never | **none** — `qsort` in 4 `.c` files, `bsearch` in 2 |
| Growable array | task 52 | `jc_vec`, the one general container |
| Arena | task 54 | `jc_mem`, three arenas by lifetime |

The five assignment files that touch file I/O hand it to the learner as
scaffolding — task 22's benchmark candidates, task 25's `wordtool` — and never
make it the subject.

## 2. Structure — a **sibling course**, not a longer one

**Recommended, and a change from the first sketch.** The operator chose "extend
the C systems course (51–54)". Building it that way was examined and is worse:

- **The numbers are not there.** Tasks run 00–75 contiguously with no gaps and no
  letter suffixes; **55 is Zig**. Inserting means renumbering 21 tasks across
  INDEX, the graders, `tests/e2e/curriculum_graders.py`, the i18n trees and every
  cross-reference — a large blast radius for cosmetic contiguity. Appending gives
  `51–54 + 76–80`, which reads as an apology every time it is written.
- **It would make the existing course untrue twice over.** Its own description is
  "heap ownership, a growable array, bounded writes, and an allocator" — all
  memory. A nine-task course holding memory, files *and* three data structures has
  no honest one-line description, and every other systems course is four tasks.
- **A sibling fixes the M636d overstatement by construction.** Today's heading
  claims "manual memory & **data structures**" while the plural rests on one task.
  Split, each title becomes true without softening anything: **"C: manual memory"**
  (51–54) and **"C: files & structures"** (76–80). That is better than editing the
  claim down, because the claim becomes *earned*.

**Rejected alternative:** renumber so the new course is 55–59. Costs 21 renames
and every stale link in the tree, buys contiguity nobody reads. Not done.

So: a new course, **tasks 76–80**, listed under its own INDEX heading in the
systems family beside C / Zig / C++ / Rust, and one word deleted from the old
course's title.

## 3. The teaching doctrine — three moves, every time

This is what the operator's sentence demands, made mechanical. Every chapter and
every task brief carries the same three moves, in this order:

1. **What jichi does, and why** — with the code, the milestone and the cost.
   *Not* "jichi uses arenas because arenas are good", but "jichi resets
   `tool_scratch` before every tool call because a 200-iteration turn otherwise
   holds every file it ever read, and M197/M198/M199 are what that cost."
2. **When that decision is wrong** — stated as plainly as the decision. This is
   the half a project bibliography normally omits, and the half that makes the
   first move believable.
3. **What you would reach for instead** — the alternative, built or measured, not
   merely named.

A chapter or task that cannot fill all three is not ready. The register is
[`APPROACH.md`](../APPROACH.md)'s: a claim carries its evidence.

**The material is unusually good for this because jichi's choices are arguable
and some are admitted defects:**

- **Zero `fsync` in 97k lines.** jichi never forces durability. Fine for a session
  file a user can re-create; fatal if you are writing a database. The course says
  both.
- **The path fence's check-then-open window is still open** — recorded at
  [`HARDENING.md`](../HARDENING.md) §"All of it is fixed as of M472, except…". A
  real, documented, unfixed TOCTOU in jichi's own file handling, taught as a live
  defect rather than a hypothetical. Nothing else in the curriculum can do that.
- **One container.** `jc_vec` and nothing else, because C89 has no generics and a
  type-safe map is either `void *` or macros; because arena lifetimes remove the
  need for removal; and because *N is small* — the tool registry, the model list,
  the repo map are dozens to hundreds. That last reason is the one a learner must
  **measure rather than believe**, which is why task 78 is a measurement.
- **`qsort` + `bsearch` are the tree jichi did not write.** A sorted array answers
  ordered queries until insertion churn dominates.

## 4. Reading — two docs, siblings of `C_STANDARDS.md`

The operator's standing preference is the reading-guide genre over graded sets
built on speculation. These carry the argument; the tasks carry the proof.

- **`docs/FILE_HANDLING.md`** — jichi's file decisions end to end: bounded reads
  (32 KB for `@`-refs, 5 MB for images, and why a cap is a design decision and not
  laziness), atomic replace via temp + `rename` (4 sites, plus the Makefile's own
  `cmp -s || mv -f` stamp recipe), `0600` on secret-bearing sinks (M132, i.e. it
  was wrong first), the descriptor fence (M472), the open TOCTOU, and **the fsync
  jichi does not call**. Alternatives: `mmap` for random access over a large file,
  `O_APPEND` for logs, `fsync`/`fdatasync` when durability is the requirement,
  `O_EXCL` for exclusive creation.
- **`docs/DATA_STRUCTURES.md`** — why one container; what arenas change about the
  question; hash table, linked list and tree each through the three moves of §3;
  and the complexity-versus-cache argument stated honestly, i.e. that asymptotics
  decide it only past an N the reader should measure.

Both are **reading, not graded**, with "something to do" per section, following
`docs/reading/`'s pattern — and both must carry **Appendix A: a read-only twin**,
so a reader with no compiler is still served.

## 5. The five tasks

Every one is graded under **AddressSanitizer**, like the rest of the systems
family, and every grader must be **two-sided**: it provably rejects the untouched
fixture and accepts a reference solution
(`tests/e2e/curriculum_graders.py` enforces this on every change).

### 76 — The file that wasn't there *(2 pts, file I/O: reading)*

**Fixture.** A loader that `fopen`s a data file and trusts everything: no NULL
check, a single `fread` whose return value is ignored, an unbounded read into a
fixed buffer.

**The learner must** report a missing file actionably (not `assert`, not silence),
survive a **short read** (`fread` returning less than asked without being an
error), and bound the read so a 2 GB file cannot be pulled into memory.

**Grader.** Three fixtures — absent file, truncated file, file over the cap —
asserting exit status and that the message names the path. Two-sided: the
untouched loader segfaults under ASan on the absent file.

**jichi tie / honesty.** `@`-refs are capped at 32 KB and images at 5 MB. The
course says why a cap is a decision (a model context is finite) and when it is
wrong (a tool that must process a whole file cannot cap it — it must stream).

### 77 — Replace it without losing it *(3 pts, file I/O: writing)*

**Fixture.** A state saver using `fopen(path, "w")`, which **truncates before it
writes** — so an interrupted save destroys the old content and leaves nothing.

**The learner must** write to a temp file in the *same directory*, `rename()` it
into place, and set `0600` where the content holds a secret.

**Grader.** Uses the existing **`FAULT=1` fault-injection tier** (`make
smoke-faults`) to fail the write midway and assert the original file survives
byte-for-byte; asserts the mode bits; asserts no temp file is left behind.
Two-sided: the untouched saver loses the file.

**jichi tie / honesty.** Four files do exactly this, and so does the Makefile's
`$(STAMP)` recipe. Then the honest part: **jichi never calls `fsync`**, so a
power cut can still lose the rename. The course explains why that is an accepted
trade for a session file, and why the same code in a database would be a bug —
and has the learner add the `fsync` and measure what it costs.

### 78 — The scan that was fast enough *(4 pts, hash table, measured)*

**Fixture.** A lookup over a contiguous `jc_vec`-shaped array by linear scan.

**The learner must** build a hash table — including collisions and a stated
position on deletion — **and then measure both across N** on their own machine,
recording the crossover.

**Grader.** Correctness of the table (including a collision-forcing key set), plus
a recorded measurement with an N, a method and a machine — the measurement
hygiene task 22 already teaches (*slope lies — keep the peak*) reused rather than
reinvented. Two-sided.

**Honesty.** This is the task that justifies jichi having no hash table, and it
must be able to **refute** it: if the learner's crossover comes out below jichi's
actual N, the honest conclusion is that jichi should have one. The grader checks
that a measurement was made and reported, **not** which way it came out.

### 79 — When a vector is wrong *(3 pts, linked list)*

**Fixture.** Code that holds a pointer into a vector across a push that
`realloc`s — a dangling pointer ASan catches immediately.

**The learner must** fix it twice: once the jichi way (hold an *index*, not a
pointer; or allocate from an arena that does not move), and once with a linked
list, whose nodes have stable addresses and O(1) splice.

**Grader.** ASan-clean under both fixes; asserts both exist, because the lesson is
the comparison. Two-sided.

**Honesty.** jichi has no list module because indices plus arenas are the cheaper
discipline for its shapes. The list trades cache locality for address stability —
and the course says which of those the learner's own use-case is buying.

### 80 — The order you didn't sort *(4 pts, tree)*

**Fixture.** A workload needing ordered iteration and range queries — which a hash
table cannot answer at all.

**The learner must** implement it twice: sorted array + `bsearch` (what jichi
does), and a BST; then measure insertion-heavy against query-heavy.

**Grader.** Correct ordered traversal and range results from both; a recorded
comparison. Two-sided.

**Honesty.** **`qsort` + `bsearch` is the tree jichi did not write** — 4 `.c`
files and 2. It is the right answer until insertion churn dominates, and the task
shows the learner where their own churn puts them.

*(Corrected 2026-09-16: this page first said "8 and 5". That count included `.o`
and `.d` build artifacts, because the grep restricted the directory and not the
file type — the same mistake as the `ring`/`st**ring**` substring error the
analysis page records, one level up. The source figures are 4 and 2.)*

## 6. What must move with it

| Artifact | Change |
|---|---|
| `docs/assignments/INDEX.md` | new course heading + table; 79 → **84** tasks |
| `docs/CURRICULUM.md` | new course in the systems family; **"manual memory & data structures" → "manual memory"** for 51–54; the §3b.2 scope section (the other DEFERRED row) |
| `tests/e2e/curriculum_graders.py` | 5 two-sided proofs + trap cases (58 → ~63): a hollow hash table, a measurement with no method, an atomic save that forgets the temp file's directory, a list fix that still holds the stale pointer |
| `tests/smoke/docs_counts_lint.sh` | counts are **recounted, never incremented** (M259) |
| `docs/README.md` | index rows for the two new docs (`docs_index_lint`) |
| `docs/DEFERRED.md` | close both M636d rows |
| `CHANGELOG.md`, `docs/ROADMAP.md` | the milestone entry |

## 7. Risks, and what would make this wrong

- **Task 78's measurement may not replicate.** On a small-N bench the crossover
  may be invisible. Mitigation: the grader requires a *recorded method*, not a
  particular result — and §3's third move means an unexpected result is a finding,
  not a failure.
- **Five tasks is the largest single addition to the graded set so far.** If the
  reading docs turn out to carry the argument well enough, 79 and 80 can ship as
  reading-with-something-to-do instead. Decide after 76–78 are built, not now.
- **The fault-injection grader (77) is the least certain piece.** `FAULT=1` is a
  separate build, and `make smoke-faults` is the only place it runs. If wiring a
  curriculum grader to it proves fragile, fall back to a wrapper that kills the
  process mid-write — and say in the brief that the simulation is coarser.
- **Discard criterion.** If building 76 and 77 shows the file-I/O material is
  better as reading than as grading — because the interesting failures need a
  filesystem the grader cannot arrange — ship the doc and drop the tasks. That
  outcome gets written down either way.

## 8. Sequence

1. `docs/FILE_HANDLING.md` and `docs/DATA_STRUCTURES.md` — the argument first, so
   the tasks have something to point at.
2. Tasks **76, 77** (files), each red-first per the
   [testing runbook](../TESTING_RUNBOOK.md).
3. Task **78** (hash table + measurement) — the keystone; build it before 79/80,
   since it is the one that could refute jichi's own choice.
4. Tasks **79, 80** (list, tree).
5. INDEX, CURRICULUM, counts, trap cases, the scope section, ROADMAP + CHANGELOG.
6. `make ci` alone and last.
