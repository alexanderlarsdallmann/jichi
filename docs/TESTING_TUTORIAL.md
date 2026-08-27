# Writing tests, running them, and reading the result — a tutorial

A learner's guide to the one skill that lets you trust an agent's work and your
own: write a test, run it, and read what it actually tells you. It is written to
sit above jichi's existing testing material — the curriculum modules, the
reference pages, the war stories — and route you into each at the right moment.
The through-line: **a test is a claim about behaviour, and a green test is only
worth what the claim was worth.** Most of the ways testing goes wrong are ways
the claim was empty while the checkmark was green.

## 1. Why this is the load-bearing skill

An AI agent is confidently wrong often enough that you cannot ship what it writes
on its say-so. A test is how you stop trusting the model — or yourself — and
start trusting the toolchain. The compiler and the test runner are tireless,
incorruptible reviewers; the whole discipline is learning to put a real question
to them and read the answer honestly.

This is also what lets you use a *cheaper, faster model* safely: gated work can
run on a small model because the gate, not the model, is the thing you trust (see
[CHOOSING_A_MODEL.md](CHOOSING_A_MODEL.md) §3 on the verification asymmetry, and
[AUTONOMY.md](AUTONOMY.md) on `--verify` and rollback). No gate, no cheap model.

## 2. The loop: write → run → read

### Write

Test **behaviour, not vocabulary**. The failure mode this avoids is the test that
matches a string the code happens to contain rather than the thing the string was
supposed to mean — [TEST_INTEGRITY.md](TEST_INTEGRITY.md) failure mode 9, and a
real jichi bug (M380: a message-catalog sweep asserted "every language complete"
while asserting through a fallback that hid every hole).

Think in **input classes, not test cases**: the empty input, the one-element
input, the too-big input, the malformed input — each class is a question the code
must have an answer to. Curriculum module 05,
[`curriculum/05-write-the-check-yourself.md`](curriculum/05-write-the-check-yourself.md),
teaches exactly this and is the required gate for that stage.

### Run

In jichi, `run_tests` (the tool) and `jichi test` (the subcommand) run your
configured `testCommand` (or `verify`) and parse the output. The parser handles
JUnit-XML, TAP, and a generic `file:line` + failure-marker scan — see
[TESTING.md](TESTING.md) for the formats and the caps. Under `--auto`, the same
parsed failures feed the fix-forward loop: the agent is handed the *parsed*
failure, not raw log noise.

### Read

**Read the failure, not the exit code.** A TAP line like `not ok 2 - hello.txt is
restored` carries three facts — it failed, which test, and what the test was
about — and all three matter. Curriculum module 03,
[`curriculum/03-tests-are-the-truth.md`](curriculum/03-tests-are-the-truth.md),
dissects a real failure line into its parts. The exit code alone is the least
informative thing a test run produces.

## 3. The traps — where green lies

Testing's hard part is not writing tests; it is noticing when a green result is
hollow. jichi's own suites have failed *while green* often enough that the
project keeps a doctrine page,
[TEST_INTEGRITY.md](TEST_INTEGRITY.md), cataloguing nine failure modes with the
incidents that taught them. The ones you will meet first:

- **The hollow gate** — a test that cannot fail (no assertion, or an assertion
  that is always true). If you never saw it red, you never saw it work.
- **The green that proves nothing** — a gate that ran a disjoint subset, or zero
  tests, and reported success ([GATE_INTEGRITY.md](GATE_INTEGRITY.md); jichi's
  M86 "verify passed while running nothing").
- **Reading a gate through a pipe** — `make test | grep -c fail` discards the
  exit code and can miss the runner's own lines. This one bit *this tutorial's
  author* mid-session (building the teeth tool, below, the first demo read exit
  codes through a `tail` and got the pipeline's).
- **Retrying to green destroys the evidence** — a flaky failure re-run until it
  passes is a bug you threw away ([ANECDOTES.md](ANECDOTES.md) #49).

## 4. Prove the teeth

The antidote to the hollow gate is one habit: **a new test must be shown to fail
without its fix.** Revert the fix (or break the invariant), run, confirm the
failure, restore. A test never observed failing has never been observed working.

jichi scripts this ritual: `tests/teeth.sh <file> <sed-expression> <command…>`
perturbs the guarded file, runs the check expecting it to go red, and restores
under a trap — refusing a perturbation that changes nothing (a no-op cannot prove
teeth). It is the mechanical form of the discipline; use it, and read
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) §"Prove the teeth" for why.

## 5. Prefer a lint to an audit

When you find yourself *checking by hand* that some rule holds across many files —
every config key documented, every tool name real, every example JSON valid — you
have found a lint waiting to be written. An audit finds what it knew to look for,
once; a lint finds it forever. This is jichi's most-repeated testing lesson
(TEST_INTEGRITY.md §"Prefer a lint to an audit"), and the `tests/smoke/*_lint.sh`
family is the worked corpus — each one born from an audit that missed something a
lint would have caught. When you write one, put a *floor* under its extraction
(so a moved file fails loudly instead of silently checking nothing) and prove it
two-sided (an invented input must NOT pass).

## 6. Your lint is a claim now — audit its universe

Section 5 told you to write the lint and put a floor under it. You can do both,
correctly, and still have a check that quietly examines the wrong set of things.
This is the trap you meet *after* you are good at tests, and it outlives all the
others, because **the check is green the entire time it is wrong.**

The question a passing suite cannot answer is not *does my check pass?* It is:

> **What is in the set my check examines, and is that the set I meant?**

Four examples, all from this repository, all found in one week by asking exactly
that. Every one had been green for months.

**1. A reader function nobody thought about.** A lint claimed to cover "every
top-level config key jichi parses". Its extraction listed the functions that read
a *scalar* — `jc_json_get_bool`, `_num`, `_str` — because those were the ones in
the file when it was written. Container keys (`models`, `hooks`, `permissions`,
`routing`, `tools`, …) are read with a different function, so the lint covered
**71 of 94** keys and reported success on all of them. See for yourself:

```sh
# in the jichi checkout -- the keys the lint saw, then the keys that exist
grep -ohE 'jc_json_(get_(bool|int|long|num|str|double)|dup_str)\(root, "[A-Za-z]+"' \
    src/config/jc_config.c | grep -oE '"[A-Za-z]+"' | sort -u | wc -l
grep -ohE '[A-Za-z_]+\(root, "[A-Za-z]+"' \
    src/config/jc_config.c | grep -oE '"[A-Za-z]+"' | sort -u | wc -l
```

**71** and **94**. The second command is the interesting one, and notice what
makes it work: it does not name the reader functions at all. It asks *what does
this file read off `root`* — a different route to the same set, which is move 2
below.

(The first draft of those two commands used `grep -c`, which counts **lines**,
and printed 62 and 28 — two numbers that are neither the truth nor each other.
Running the command you are about to publish is not a formality.)

**2. A trailing parenthesis.** Another lint enumerated the keyboard chords the
line editor handles with `grep -ohE 'ch == [0-9]+\)'`. That pattern requires the
comparison to *end* its condition — so `ch == 127 || ch == 8` contributed only
the 8, and byte 127 (the DEL most terminals actually send for Backspace) was
invisible. Count the difference:

```sh
# in the jichi checkout
grep -ohE 'ch == [0-9]+\)' src/tui/jc_term.c | grep -oE '[0-9]+' | sort -un | wc -l
grep -ohE 'ch == [0-9]+'    src/tui/jc_term.c | grep -oE '[0-9]+' | sort -un | wc -l
```

The lint's own byte→name table had carried an entry for 127 all along, which no
input could ever reach. **The intent was right and the extraction could not
deliver it** — the most common way this defect looks from the inside.

**3. A corpus that stopped at our own front door.** A lint holds the project's
shell scripts to POSIX — no GNU-only `\b`, no GNU `\|` alternation — because
those constructs match *nothing* on a BSD or macOS. It scanned `tests/` and
`scripts/`: the code the project runs. It did not scan the 79 shell scripts under
`docs/` — which is where **every graded assignment's `test.sh` lives**, the code
the project asks *you* to run. There were 24 defects in there, and two of them
matter to you personally: `53-never-call-sprintf` and
`61-cpp-own-your-memory` used `\b(sprintf|strcpy|…)` and `\b(new|delete|…)`,
which on a non-GNU `grep` match nothing at all — so those traps **passed a
solution that did the forbidden thing.** Eight capstone graders had the mirror
defect and *failed a correct answer*.

If you are working on macOS or a BSD, that is not a hypothetical about someone
else's machine. It is the grader that told you your right answer was wrong.

**4. The lint that should not be built.** The same week, a plan to widen a
different check across the whole documentation tree was **dropped after
measuring**: of 110 fenced code blocks outside the guides, exactly **1** quoted
source verbatim. Machinery for a population of one is not rigour, it is cost.
Measuring the population before building the gate is the same discipline as
measuring before optimising.

### The four moves

1. **Name the universe in one sentence, in the lint's own header.** "Every
   top-level key read straight off `root`." Now it is a claim someone can check —
   including you, six months later, which is the real audience.
2. **Enumerate it a second way and diff.** Not the same regex again: a *different
   route* to the same set. Every string comparison in the file, not the two
   variable names you remembered. This is the whole trick, and it is ten minutes.
3. **Floor it at today's exact count**, not a round number below. A floor of 18
   under 21 items leaves room for three to vanish in silence; a floor of 7 under
   7 fields fails the moment the extraction breaks.
4. **Prove it red.** Perturb the thing it guards and watch it fail, then restore
   (`tests/teeth.sh` does this for you — §4).

### And check your instruments while you are in there

Three ways a *measurement* lied during that same week, each costing real time:

- **Your shell may not be the gate's shell.** A hand-check of one lint's pattern
  reported that a key was documented nowhere; it is documented at length. The
  bare `grep` in that session was a wrapper, and it read the pattern differently
  from the `/usr/bin/grep` the test tier gets. **Verify a gate's pattern with the
  gate's own tool** — `sh -c '…'`, or an absolute path. Look at yours now:

```sh
# anywhere
command -v grep            # a bare name, not a path, means a function or alias
sh -c 'command -v grep'    # what a POSIX-sh test driver actually gets
```

- **Change one variable at a time.** A comparison that moved two things at once
  (how a file was extracted *and* which cache it used) had to be thrown away and
  redone. A measurement whose conditions moved twice is not a measurement.
- **A shared build cache is a variable.** Comparing an old and a new version of
  the same program filled a compiler's shared cache with the *old* build's
  artifacts, and the next run of the real gate served them — a passing solution
  reported as failing, with the cause hidden in a file path inside an error
  message. When you compare two versions of one thing, give each its own cache.

The maintainer-facing version of all of this, with the incident records, is
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) §"Audit the universe, not the result"; the
two measured write-ups are
[`analysis/2026-08-21-the-lint-universe-sweep.md`](analysis/2026-08-21-the-lint-universe-sweep.md)
and
[`analysis/2026-08-21-what-the-docs-quote.md`](analysis/2026-08-21-what-the-docs-quote.md).

## 7. Running it in jichi, concretely

```sh
jichi test 'make test'          # run a command NOW — no config needed
jichi test                      # the same, using the configured testCommand/verify
jichi --auto -p "add a failing test for the empty-input case, watch it fail, then fix it"
jichi --auto --verify 'make test' -p "implement X"   # the gate drives the loop
```

Three rules those four lines encode, each of which will otherwise cost you a
confusing failure:

- **`jichi test` takes the command as an argument.** With no argument and no
  config it exits with `no test command given and none configured` — which reads
  like a broken install and is really just a missing argument.
- **Put every flag *before* `-p`.** `-p` takes the prompt as its argument and
  refuses a flag-shaped one (exit 2, with a message saying so).
- **A headless run starts in chat mode, which is read-only.** With no human to
  answer an approval prompt, any tool that would ask is *refused* — so `-p` alone
  can read and explain but cannot edit. `--auto` is what lets it write, and
  [AUTONOMY.md](AUTONOMY.md) is what bounds it once it can.

To make `jichi test` work without an argument, set `testCommand` in your config —
that is `~/.jichi` for all projects, or `./local/config.json` for this one
([CONFIG_TUTORIAL.md](CONFIG_TUTORIAL.md) walks through both):

```json
{ "testCommand": "make test" }
```

## 8. Extra curriculum — the reading track

In reading order:

1. [`curriculum/03-tests-are-the-truth.md`](curriculum/03-tests-are-the-truth.md)
   — read the failure, not the exit code; red before green.
2. [`curriculum/05-write-the-check-yourself.md`](curriculum/05-write-the-check-yourself.md)
   — input classes, grading the grader, the hollow gate.
3. [TESTING.md](TESTING.md) — jichi's structured test integration: the parser,
   `run_tests`, `testCommand` vs `verify`, the fix-forward loop.
4. [TEST_INTEGRITY.md](TEST_INTEGRITY.md) — the nine failure modes and the
   practices, the doctrine page every lesson above points at.
5. [GATE_INTEGRITY.md](GATE_INTEGRITY.md) — when a gate passes while running the
   wrong thing (or nothing), and the STATE-THE-REACH / DECLARE-THE-GATE fixes.
6. [ANECDOTES.md](ANECDOTES.md) — the war stories, each *symptom → dead ends →
   root cause → lesson*: #17 (the hollow gate), #19/#20 (the grader that lied,
   and naming the request before the model), #49 (re-running destroyed the
   evidence), #50 (a bizarre reply that was a prompt-delivery bug, not a model
   weakness). These are the reason the doctrine pages exist.

External concepts worth reading up on (search these; prefer primary sources):
*test-driven development* and *red-green-refactor*; *equivalence partitioning*
and *boundary-value analysis* (the input-classes idea, named); *mutation
testing* (proving teeth, mechanized at scale); *property-based testing* (assert an
invariant over generated inputs rather than one case); *flaky tests* and why
re-running to green is a bug, not a fix.
