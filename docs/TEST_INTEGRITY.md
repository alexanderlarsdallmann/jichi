# Testing the tests

*Who watches the watchmen? A test suite is software. It is written under the same
pressure, by the same people, with the same blind spots as the code it judges —
and unlike that code, nothing is watching it. This document is the record of how
this project's test suites have actually failed, and the practices adopted in
response. Every incident below happened here and is traceable to a milestone.*

Companion: [TESTING.md](TESTING.md) is about jichi *parsing* test output (a
product feature). This document is about whether our own suites can be believed.
zigodot has a sibling document, `docs/TEST_INTEGRITY.md`, with the same spine and
its own evidence.

---

## The premise

A passing test suite asserts two things at once:

1. the product behaves as specified, **and**
2. the suite is capable of noticing if it did not.

Only the first is usually checked. The second is assumed, and it is the one that
has repeatedly been false here. Every failure mode below produced **green**.

---

## The failure modes, with incidents

### 1. The instrument is broken — it reports failures that never happened

**M201.** Sixteen of 72 e2e drivers carried a private HTTP request reader whose
`Content-Length` body loop broke on the first socket timeout, silently returning a
**truncated** request. The mock then evaluated `marker in req` against a partial
body, picked the wrong canned reply, and the driver reported *a product regression
that never happened*. Two long-standing "product" failures — `prose_nudge`'s
`NO_NUDGE` and `constraints_scope`'s "loaded but not enforced" — were retroactively
explained by this one class.

The lesson is not "read carefully". It is that **a broken instrument does not
announce itself as broken**; it announces a bug in the thing being measured. When
a test fails, the first question is not "what did the product do wrong" but "is
this instrument sound".

**M326x, and this one shipped for three milestones.** `jc_compact_midturn` emits
`short` — "the pass could not reach its target, so the request went out over the
configured contextLimit". It was written as `!reached`, without consulting the
`pressed` field that says whether the high-water trigger had fired at all. An
unpressured pass takes an early return that leaves `reached` at 0, so **every
routine zero-loss dedup reported an emergency**. In a 36,925-event workload all
19 such events were false and none were real: a metric built specifically to
detect a failure mode reported it with a 100% false-positive rate, and the
summarizer printed the false claim in prose. I read those 19 as evidence of the
thrash I was investigating *before* checking what produced them — which is the
failure mode consuming itself.

The lesson is the sharpened form of this section: when an instrument reports the
defect you went looking for, that agreement is a reason to check the instrument,
not a reason to believe it.

**M481, and this one was red for months on one platform only.**
`sessions_footprint` and `turn_scratch` read a gauge out of a PTY transcript with a
pipeline ending in `grep -o '[0-9]*'`. That pattern is zero-or-more, so it matches
the empty string; GNU grep skips empty matches and prints the digits, while
OpenBSD's `grep version 0.9` prints **nothing and exits 0**. Both drivers reported
`could not read the /context arena gauge (before='' after='')` — an instrument
failure wearing the exact words of a product failure, since a missing gauge is
precisely what that message is for. M479 proved the product innocent (the emitter
has no platform guard, and `ptydrive`'s own `expect` saw the string twice) and
still could not name the cause.

What named it was **a second platform that passes**: NetBSD runs both drivers
green, because it is a BSD whose userland ships *GNU* grep. One probe on three
machines then took a months-old mystery to a single line. The general practice is
below — *diagnose by difference* — and the specific reason this survived the M466
GNU-ism sweep is that the sweep looked for non-POSIX **flags**, and nothing about
`grep -o` is non-POSIX. Only the emptiness of the pattern is.

### 2. The audit finds only what it knows to look for

Also **M201**, and the strongest argument in this file. Twelve of the sixteen bad
readers were found by grepping for a named function. **Four were found only by a
lint** (`ask`, `docs`, `mcp_prompt`, `mcp_ref`) — their loops were inline in the
connection handler, so no name matched.

> *"That is the argument for a lint over an audit: the audit found what it knew to
> look for."*

An audit is a snapshot of one person's hypothesis. A lint is an invariant that
holds for code not yet written. Prefer the lint. Two now run, one per tier:
`tests/e2e/rig_lint.py` enforces five properties across the residual Python
drivers (no private request readers, no truncating body loops, `SO_REUSEADDR` on
every listener, every driver can fail, no orphans), and
`tests/smoke/smoke_lint.sh` enforces ten-plus properties across the smoke
tier's **236 drivers** (`ls tests/smoke/*.sh | wc -l`, 2026-08-22; this line said
"six across 90" until M531 — an uncounted number in the file that argues for
counting, found by an outside reader)
(including "every driver can actually fail" and "one driver, one tier").
A third, `tests/smoke/docs_counts_lint.sh`, extends the idea to prose: the
curriculum's advertised task/trap counts and the scaffold-pack count must equal
the measured ones, after an audit found they had drifted because each milestone
incremented the previous claim instead of recounting (M259).

### 3. A test that cannot fail

`rig_lint`'s fourth property: **every driver must be *able* to fail.** One that can
only reach `ok()` is decoration. This is worth enforcing mechanically because a
decorative test is indistinguishable from a real one in a green run — it is only
distinguishable in a *red* one, which by construction never comes.

### 4. The oracle is wrong

**ANECDOTES #20.** A bench grader validated by an ad-hoc snippet — which used
different YAML unescaping than the runner — scored **five correct edits as
failures**. The grader was the thing under test and nobody was testing it.

`tests/bench/check_graders.py` now proves every grader **two-sided** (a reference
solution must pass, a known-bad must fail) and does so **through the runner's own
parser**, not a re-implementation. A validator that parses differently from the
system it validates is testing a fiction.

### 5. Green that proves nothing

**M86.** A verify can pass while running *nothing*: a dogfooded `zig build test`
gate silently ran a disjoint subset, so whole subsystems were never compiled while
the gate stayed green. `jc_env_verify_sanity` now compares each green verify's
observed test count against the run's high-water mark and warns on `no_tests`
(green but zero tests) or `fewer_tests` (fewer than an earlier green).

A green with no denominator is not evidence. **Always know how many tests ran.**

**M482: three checks that could not fail, on a tier nothing built.** `faults.sh`
drove the allocation injector at three fixed budgets and asserted only "did not
hang, did not die on a signal" — passing for any exit code in 0..123. Both halves
were hollow. `ls --all` makes **two** counted allocations, so at 200/800/3000
nothing was injected: the output is byte-identical to no injection, **and to
misspelling the environment variable's name**. And the passing range contained the
defect the file's own header names ("surviving an allocation failure by silently
dropping sessions from the listing with exit 0 is the DEFECT").

The defect was real and present: a failed session enumeration printed
`(no saved sessions)` and exited 0, and `ls --output json` — a *stable* interface a
supervisor parses — returned a well-formed `{"v":1,"sessions":[]}`. The function
being called carried M198's comment saying that this exact symptom was what
`skipped` had been added to remove; M198 fixed the per-file half and left the
whole-call half inside the same `||`.

None of it had run since, because **no stage of `make ci` ever built `FAULT=1`**,
and the three drivers skip without it. That is the orphan lint one level up: they
were correctly named in `run.sh`, so "listed" was true and "reachable" was not.
Two cheap questions come out of it — *what builds the prerequisite of every
conditional test?*, and *does misspelling the variable change the result?* — plus
`make smoke-faults` in the gate and `smoke_lint` check 16 to keep it there.
Full account: [`analysis/2026-08-18-the-tier-no-gate-built.md`](analysis/2026-08-18-the-tier-no-gate-built.md).

### 6. Retrying to green destroys the evidence

**M201.** When three drivers flaked in-suite but passed standalone, `run.sh` was
given failure *classification* — capture the failing output, re-run once
standalone, label the result **in-suite-only** or **also-alone** — and
deliberately **not** retry-to-green. The suite still fails either way.

A retry converts a reproducible signal into an intermittent one and throws away
the only run that had information in it.

### 7. Testing the wrong artifact

**M205, and three more times in one session.** `make test` rebuilds `run_tests`
but **not** `./jichi`. A fix can be unit-green while every driven verification
runs a binary from minutes earlier. It cost real time on M205, and then twice more
in the M207/M208 session — to the person who had written the M205 note.

> `strings ./jichi | grep '<the new message>'`

Two seconds, and it belongs to the build step, not to the debugging that follows.
The hazard arrives the moment a tool is both built and installed.

### 8. Reading a gate through a pipe

**M185.** A lint piped through `tail -1` masked a red gate: the exit code was
discarded by the pipeline. **Watch exit codes directly.** `cmd | tail -1` reports
`tail`'s status, not `cmd`'s.

### 9. The assertion matched, but not the thing it named

**M293 and M296, twice in one session.** Both were new PTY drivers, both passed on
the first run, and both stayed green when the code they existed to test was
deleted — because the string they grepped for was also produced by something else
in the same transcript.

- **M293** asserted that `/learn apply` refreshes the live session's memory. It
  passed with `jc_memory_refresh` removed, because the fixture's draft contained a
  `## Corrections` section and `jc_memory_correct` refreshes `app->memory` *itself*.
  The driver was exercising the machinery it shared a workspace with. Fixed by
  splitting the fixture: a memory-notes-**only** draft is the one that isolates the
  new refresh, since `jc_memory_add` is the function that does not do it.
- **M296** asserted that the reply header names the model id. It passed with the
  header reverted to the tier alone, because the driver ran `/status` later in the
  same session and `/status` prints the same `name (id)` pair. Fixed by anchoring
  the grep on the header's own `" - chat - <time>"` tail.

An earlier revision of the M293 driver had a third variant: it counted occurrences
of a substring and found two with the fix working perfectly, because the tool's own
`corrected memory: removed "…"` line quotes the note it removed.

**The shared cause is that a PTY transcript is one flat log of every surface.** A
grep against it does not test the surface you had in mind; it tests whether the
string appears *anywhere the session printed*. So:

> **Anchor a transcript assertion on something unique to the surface under test** —
> a neighbouring field, a delimiter, the line's own shape — and prove it by deleting
> the code and watching *that specific check* go red.

The general form is worse than these two instances suggest: any output-matching
test is vulnerable whenever the same value legitimately appears twice. Counting
occurrences is not a fix, because the tool's diagnostics quote their own inputs.

**The mirror image, M310: asserting an absence.** A new driver checked that
`toolProfile: core` does *not* advertise the assignment tools, and passed on the first
run — over a captured request in which those tools had never been registered at all. Its
sibling check, that `full` *does* advertise them, was red: both configs passed
`assignments` as `write_config`'s **fourth** argument, where it lands inside the model
object and is silently ignored.

An absence check has strictly more ways to pass than a presence check: the feature was
off, the file is missing, the name was misspelled, the capture never happened. `grep -q`
returning non-zero proves none of them apart. So:

> **Pair an absence assertion with a presence one, and require the artifact to exist
> before its absence means anything.** The presence check is what tells you the negative
> was measured rather than merely unobserved.

The pairing is what found this one. Neither check would have been suspicious alone — the
green one looked like the feature working, and had it shipped alone it would have gone on
looking like that.

---

### A test that asserts its **environment** instead of the product (M450, M452)

Three checks reddened in one session for things that were true of the *host* rather than
of jichi. None was a product defect; all three cost a reader the same time a real red
costs, and one of them destroyed a whole run.

| The check | What it assumed | Where it broke |
|---|---|---|
| `jc_proc_group_rss_kb(getpgrp()) > 0` | this process has a process group | a Guix container, where every process has `pgrp 0` — so the function honoured its own `pgid <= 0 => 0` contract, *the contract the next line pins* |
| `accessible.sh` calling `t_skip` at check **6 of 8** | a `C.UTF-8` locale exists | any glibc < 2.35 — **including CentOS 7 and Debian 9, two of this project's own Tier V rows**. `t_skip` is a *whole-driver* verb: it silently abandoned checks 7 and 8, then failed the tier on a count |
| `doctor` exits 0 in `config_dir.sh`'s control | a model server is reachable | any machine with no network — i.e. exactly the machine the portable tier exists to validate. Written earlier the **same day** as the milestone that fixed it |

**The rule.** A check may assert what jichi does. It may not assert what the host happens
to provide — unless the check's subject *is* that provision, in which case it must be
guarded and say so. Ask of every new assertion: *would this red on a machine with no
network, no process groups, no UTF-8 locale, no `/tmp`?* If yes, and that is not the
thing under test, guard it.

**The corollary about skips.** A per-check skip and a per-driver skip are different verbs
and must not be confused. `t_skip` ends the driver; `t_skip_one` (added at M450) skips one
check and counts toward the plan. Using the first where the second was meant loses
coverage *silently* and reports a count mismatch instead of a cause — two bad outcomes
from one substitution.

### A null check followed by a dereference (M265, again at M452)

`JC_CHECK` **records and continues**, on purpose, so one red does not hide the next. The
price is that a check is not a guard:

```c
JC_CHECK(m0 != NULL);      /* records the failure ... */
JC_CHECK(m0->roles == X);  /* ... and then dereferences it anyway. SIGSEGV. */
```

M265 found this on git < 2.5. M452 found it again in `test_config.c`, on an Android
tablet with no `/tmp`: the fixture was never written, the config never loaded, and the
suite **aborted at the 4th of 123 test files** — reporting nothing about the other ~119.
An upstream environment failure was converted into total information loss.

> **Check, then _branch_. Never check, then dereference.**

The site found was fixed; a re-run **aborted again at a different site of the same
shape**, so the class is known to be wider than the instances seen. The audit and the
`/tmp` conversion behind it are planned in
[`plans/2026-08-test-tmpdir.md`](plans/2026-08-test-tmpdir.md).

**The verification has teeth by construction:** point `TMPDIR` at a non-existent
directory and run the suite. Before the fix it aborts early; after it, it must produce
honest reds and still reach the last test file. That reproduces the real failure rather
than approximating it.

---

## The operator's half: docs/SESSION_RUNBOOK.md

Everything below is about how a *suite* can be green and wrong. There is a
second half, and it is the same disease in the operator's hands: a proof run
against a stale binary, a monitor reading a dead run's artifact, a `pkill -f`
pattern matching its own shell, a check that passes on empty input. Those are
collected in [SESSION_RUNBOOK.md](SESSION_RUNBOOK.md) with a fixed order of
execution and `scripts/preflight.sh` to enforce what can be enforced, because
each rule there was written after breaking it — several of them twice.

## The practices

Each is cheap, mechanical, and adopted because something above went wrong.

### Assert behaviour, not vocabulary

A gate required the string `checkAllAllocationFailures` to appear in a file, meaning "prove
ownership under allocation failure". A driven run satisfied it with a test whose **title**
contained that word and whose body used the ordinary testing allocator:

```zig
test "Action and UndoRedo: checkAllAllocationFailures" {
    const allocator = std.testing.allocator;   // not the failing harness
    ...
}
```

The check passed and the defect it existed to prevent shipped — an ownership leak, 3
allocations and 0 frees, proven the moment a real harness call replaced the decorative one.

`grep` cannot distinguish a call from a name, a comment, or a string. So:

- Grep for the **call**: `std\.testing\.checkAllAllocationFailures\(` — with the paren.
- Better, where it is possible: make the check **construct its own fixture** and assert an
  outcome. The same gate's decisive check built its own consumer of the new API, ran it, and
  required a value to come back after `undo`. No test the run authored could satisfy that.

This is [the prefer-a-lint rule](#prefer-a-lint-to-an-audit) applied one level down: prefer a
lint that checks *behaviour* to a lint that checks *vocabulary*. A vocabulary check is
especially weak when the thing reading it is optimising to pass it — a model naming a test
after the token the gate wants is not deceit, it is gradient descent.

Full incident: [ANECDOTES.md](ANECDOTES.md) #43. For gates on driven runs specifically, see
[DRIVING.md](DRIVING.md) §6.

### Prove the teeth

**A new test must be shown to fail without its fix.** Revert the fix (or break the
invariant), run, confirm the expected number of failures, restore. Record the
count in the milestone entry.

This is standard here — M206's *"verified to have teeth (guard reverted → 2
failures)"*, M207's *"the old `strstr` chain produces exactly 6 failures, one per
scoped case"*. A test never observed failing has never been observed working.

The ritual is scripted since M381: `tests/teeth.sh <file> <sed-expr> <command…>`
perturbs, expects red, and restores under a trap — refusing a perturbation that
changes nothing (VACUOUS), because a no-op that "proves" teeth is this
document's failure mode 3 wearing a lab coat.

### Diagnose by difference

When a check fails on one platform and passes on another, the difference *is* the
diagnosis, and it is almost always cheaper than re-reading the failing side. Two
platforms that agree tell you nothing; two that disagree localise the cause to
whatever differs between them.

This is worth stating because it inverts the instinct. The two drivers above were
read, re-read and narrowed across three sessions on the platform where they failed.
The thing that solved them was bringing up a platform where they **pass** — and
choosing NetBSD had been justified on entirely unrelated grounds (it has procfs, so
it can run `child_fds.sh`). Its grep being GNU rather than BSD was not the reason
to build that row; it was the reason the row paid for itself the same day.

The corollary matters for how a matrix is read: a sibling platform is only a useful
control if it differs in the suspect component. Two BSDs are not two samples of
"BSD userland" — had NetBSD shipped a BSD grep, it would have failed the same two
drivers and taught nothing new.

### Some defects only a reader can find

The counterweight to the rule below, and the reason it is not the whole story.
M392's documentation review found two defects no lint in this repository could
have caught: a `doctor` sample output that no build can produce (it showed a FAIL
as an ignorable warning, for a condition that cannot fire), and a missing caveat
that git-ignored files are outside the checkpoint net. Both are **prose that is
internally coherent and simply untrue of the program** — a checker sees
well-formed markdown, and a lint over prose would pass exactly the defects that
matter. So documentation gets a *repeating human pass* with a written rubric
([DOC_REVIEW.md](DOC_REVIEW.md)), and the lintable slices of it — command shapes,
documented defaults — get lints (M393, M394). Knowing which is which is the skill.

### Prefer a lint to an audit

If you found a bug by reading, ask what invariant it violated and whether that
invariant is checkable. Existing lints:

| lint | invariant |
|---|---|
| `tests/e2e/rig_lint.py` | no private request readers; no early-breaking `Content-Length` loop *in any spelling*; `SO_REUSEADDR` on every listener; **every driver can fail**; **no orphans** — every driver is named in `run.sh` (M213, after a port dropped a driver from the list without deleting it, silently ending its coverage) |
| `tests/smoke/arena_lint.sh` | `src/tools/` and `src/lsp/` use the tool or turn arena, never the session arena |
| `tests/smoke/docs_flags.sh` | documented flags and real flags agree |
| `tests/bench/check_graders.py` | every grader is two-sided, via the runner's own parser |
| `tests/smoke/smoke_lint.sh` | every smoke driver sources `_smoke.sh`, can fail, uses `"$BIN"`, and is strict POSIX sh with no python3/nc/curl (M209) |
| `tests/smoke/tool_names_lint.sh` | `JC_ALL_TOOL_NAMES` equals the tool names `src/tools/` defines (M285) |
| `tests/smoke/builtin_cmds_lint.sh` | every built-in slash command is in `jc_assetval`'s `BUILTIN_CMDS[]`, so `doctor` can warn about shadowing (M262) |
| `tests/smoke/slash_commands_lint.sh` | every `/command` **and subcommand** mentioned in a source string resolves to something a user can run (M295) |
| `tests/smoke/docs_counts_lint.sh` | the curriculum's task/trap counts, the scaffold-pack count, **and the ROADMAP release banner** — its figures, its agreement with `CURRICULUM.md`, and its `latest milestone` against the file's newest entry (M259, extended M326t) |
| `tests/smoke/tool_caps_lint.sh` | the output caps `TOOL_OUTPUT_COST.md` advertises equal the `#define`s in `src/tools/` and the `--lite` fallbacks in `jc_config.c`, in both directions; every cap key it recommends is one jichi parses (M326z) |
| `tests/smoke/compact_pressed.sh` | a mid-turn compaction never claims to have fallen short unless it was under pressure (`short` ⇒ `pressed`); every compaction event reports which it was (M326x) |
| `tests/smoke/portability_lint.sh` | the clock_gettime probe, the `LDLIBS` slot that consumes it, and the source guard it switches all exist together (each is inert alone); `INSTALL.md` states the minimum libcurl/glibc, and its libcurl versions are decoded from the `LIBCURL_VERSION_NUM` guards (M326u) |
| `tests/smoke/project_records_lint.sh` | every command `PROJECT_RECORDS.md` teaches produces the output it claims, against fixtures extracted from the page; every example block is classified; nothing taught is non-POSIX (M326s) |
| `tests/smoke/smoke_lint.sh` (check 16, M482) | every driver that requires a `FAULT=1` build is named in the Makefile gate. The orphan check one level up: `faults`, `faults_net` and `faults_net_midstream` were listed in `run.sh` and skipped in every run on every platform, because nothing built the binary they need |
| `tests/smoke/posix_utils_lint.sh` | no GNU-only utility flag or regex construct in code that runs on a target — `head -c`, `grep -P`, the colour flag, grep's GNU-only include/exclude file filters, `sed -i` without a suffix, `stat -c`, `xargs -r`, `sort -V`, BRE `\|`, `\b`, `\xNN`, and (M481) a **`grep -o` pattern that can match the empty string**, which OpenBSD's grep answers with no output and exit 0. Each entry was verified against the real utility's usage string, not assumed; two checks are planted-positive self-tests |
| `tests/smoke/org_mode_lint.sh` | every org claim in `ORG_MODE.md` is re-measured on Emacs, with the expected values scraped from the page's **visible prose** rather than held in the test (M326s) |

M295 is the worked example of making one of these trustworthy, because its whole
risk was false positives — a lint that cries wolf gets ignored wholesale, so it
would have been worse than the audit it replaced. Three things made it quiet:

- **Narrow on facts, not on English.** The plan proposed an "instruction cue" list
  (`run`, `type`, `via`, …). What actually worked were properties of C and of
  paths: the `/` must *open* a token; the token must not continue as a path; the
  containing string must contain a space (a message is prose, `"/v1"` is a token).
  A cue list would have been a guess about how sentences are written.
- **No exception list.** Two prose collisions survived the rules — a glossary
  defining "command" as "a /slash shortcut", and "the /learn command" in `--help`.
  Both were **reworded** rather than excepted. An exception list is where a lint
  goes to die: every entry is a place the invariant no longer holds, recorded as
  though it did.
- **Fail loudly when the ground truth shrinks.** The lint compares mentions against
  commands extracted from the dispatch code. If a dispatch shape changes, that
  universe silently empties — and then the lint either flags everything or, if the
  candidate scan breaks the same way, *passes while checking nothing*. Floors on
  both extractions turn that into a failure, which is failure mode 3 above applied
  to a lint's own inputs.

It also demonstrates the payoff of comparing lists rather than reading them: building
the ground truth surfaced three unrelated defects (`/route` missing from Tab
completion; then, once added, `route` missing from `BUILTIN_CMDS[]`; and `learn
corrections` missing from `--help`), none of which the lint's actual check is about.

The M209 smoke tier also moved the request-reader lesson from lint to
*structure*: `tests/tools/mm_core.c`'s incremental HTTP parser is the only
place that can declare a request complete (on Content-Length), so the mock's
recv loop cannot re-grow the truncation bug in any spelling — and unlike the
tier's short sh lib (`tests/smoke/_smoke.sh`, which is in the "read carefully,
deliberately unwatched core" class with `_e2e.py`), the parser IS watched:
`tests/test_ttools.c` feeds it byte-by-byte and was observed red (18 failures)
against a naive break-at-head implementation before landing.

"In any spelling" is the important phrase: `rig_lint` rejects the *shape*, not a
known list of names, which is why it caught four cases the audit could not.

### Audit the universe, not the result

A check that passes tells you its **universe** is clean. It does not tell you the
universe is the one the check claims. Three milestones running found a green lint
covering less than its own header said:

| Milestone | The check | Claimed | Covered |
|---|---|---|---|
| M508 | `reading_refs_lint` anchor count | "the chapters point at code N times", per index page | one count summed over all three series |
| M510 | `config_keys_lint` | "TOP-LEVEL keys read straight off `root`" | 71 of 94 — every container key (`models`, `hooks`, `routing`, …) was read with a shape the extraction did not match |
| M511 | `subcommands_lint` | "TWO LEVELS" of dispatch, in capitals | level 2 matched two variable names; a third shape carried five verbs |
| M511 | `keys_lint` | "every control chord the line editor handles" | `ch == N\)` — a chord written `ch == N \|\|` was invisible, and the map's own `127) Backspace` entry was unreachable |
| M511 | `posix_utils_lint` | POSIX-only shell constructs | `tests/` and `scripts/` — not the 79 scripts under `docs/` that **learners** run, where 24 defects were sitting |

The shape is identical every time: **the extraction was pinned to an incidental
detail of how the code happened to be written** — a reader function, a variable
name, a trailing parenthesis, a directory. None was wrong when written. Each
became wrong when the code grew a second way of doing the same thing, and the
check kept passing over the smaller set.

Two defences, one cheap and one not:

- **Floor every extraction at the count when you wrote it**, not a round number
  below it. `session_fields_lint` floors 7 fields at 7 — the tightest in the tier,
  and the one that would fail loudest. `keys_lint` floored 18 under 21 bytes,
  which left room for three chords to disappear in silence.
- **Send a learner to [TESTING_TUTORIAL.md](TESTING_TUTORIAL.md) §6**, not here.
  This page is the incident register; that one teaches the skill from the same
  four examples, with commands to run.
- **Sweep periodically**, asking each check *what is in your universe?* rather
  than *do you pass?* One afternoon over 40 lints found 4 gaps and 24 live
  defects (`docs/analysis/2026-08-21-the-lint-universe-sweep.md`). The question
  a passing suite cannot answer is the one worth asking on purpose.

### State what a green means

A measurement's docstring should say what it does **not** prove.
`tests/bench/run_bench.py` is a measurement, not a gate, and lives outside
`make ci` deliberately. zigodot's `tests/demo_sweep.py` states that "ran clean"
means *parsed, compiled and executed without raising* — **not** "behaves as in
Godot" — because its runner has no scene tree. An unqualified number gets quoted
later as if it meant more than it did.

### Keep the harness identical to production

A harness that configures the system differently from the real entry point tests a
system nobody ships. See zigodot's document for the case that makes this concrete:
its e2e harness ran the VM **without a ClassDB** while both real runners wired one,
so class reflection silently degraded and a feature could pass the gate and fail in
the real runner.

For jichi the equivalent question is: does the e2e driver drive the *same* binary,
config precedence and arena lifetimes as a real run? Where it cannot (a mock
server), the divergence should be named in the driver.

### The tiers you run by habit are not the gate

M519, found by accident. `make ci` was run to completion for the first time in an
unknown number of milestones and failed at its **ASan/UBSan stage** before
reaching any new check: 4,224 bytes leaked in 3 allocations, from a config test
that freed its arena but not the heap vectors each `jc_config_load_json` call
allocates. A detached worktree at the previous commit reproduced it identically,
and the test dates from **M503** — roughly sixteen milestones during which
`make test` and `make smoke` were green every single time.

Nothing was wrong with the gate. It was simply not the thing being run: a session
builds, runs the unit suite, runs the smoke tier, and reads three green summaries
that between them cover every stage *except* the sanitizers, valgrind, the
curl-free link and the FAULT build. **A gate stage that only CI runs is only as
reliable as the last time CI ran**, and this project's CI is a make target on a
developer's machine.

Two practices follow:

- **Run the whole gate before a commit that claims a milestone**, not the tiers
  that answer the question you were working on. The tiers tell you your change
  works; the gate tells you the tree is shippable, and those are different claims.
- **A stage's silence is not evidence.** No check reported "the sanitizer stage
  has not run since M503", because absence of a run produces absence of output.
  This is the same shape as `docs/GATE_INTEGRITY.md`'s FAULT-build finding (M482:
  three drivers that skipped on every binary the gate built, and therefore ran
  nowhere) — the second instance of it, which makes it a pattern rather than an
  incident.

### A classifier's else-branch must not be a finding

M519, and it cost an hour. `scripts/probe-models.sh` asked "does this model emit
native tool calls?" as:

```sh
if grep -q '"tool_calls"[[:space:]]*:[[:space:]]*\[[[:space:]]*{' out.json; then
    tools=native
elif grep -q '"name"' flat.json && grep -q '"arguments"' flat.json; then
    tools=prose        # <-- a POSITIVE CLAIM in the fallback
```

LM Studio pretty-prints its JSON, so `[` and `{` land on different lines, `grep`
matches within a line, and the `native` branch **could not fire against that
server at all**. The fallback then matched `"name"` and `"arguments"` *inside the
real `tool_calls` array* and reported `prose` — the exact opposite of the truth —
for five natively-capable models in a row.

Three properties, each generalizable:

- **The else-branch asserted.** Had it said `unknown`, five `unknown`s would have
  sent a reader to the bytes immediately. A classifier that cannot distinguish
  "the pattern did not match" from "the thing is absent" converts every matching
  bug into a finding, and findings get published.
- **It was validated on one input shape.** The same script answered *correctly*
  against the HRZ gateway, whose JSON is single-line. Every correct answer it had
  ever given came from a server whose formatting suited a line-based pattern.
  **A tool validated on one input shape is not validated** — this is the
  test-corpus rule (M511's portability sweep) applied to an instrument.
- **It disagreed with the product and the product was right.** jichi's
  `doctor --live` had reported the true verdict from the first minute. When your
  own instrument and the thing under test disagree, the instrument is the newer,
  less-exercised code.

The house rule already covers the mechanism — *verify a gate's pattern with the
gate's own tool*, written after a `grep` shim read 1,443 matches as 0 — and this
is its second instance in three milestones, against a different tool, in the same
direction: **a plausible negative.** So the practice is stated as a shape, not an
anecdote: **run the pattern against a recorded real response, and require it to
match; then require the fallback to be a non-claim.** `config_bool_lint.sh` does
the first half by asking the binary for the effect rather than grepping for the
cause.

### Never let a suite's own state leak between cases

M198's suite-wide shared `HOME` was a *leading suspect* for three flakes — a
`calibration.json` could have carried token-estimate state between drivers and
shifted their context decisions. It was **disproved** (every driver sets its own
HOME), but the investigation is the point: shared mutable state between test cases
turns one case's success into another's precondition.

---

## Recommendations (not yet implemented)

1. ~~**A teeth-check helper.**~~ **Built (M381), in the sed-perturbation form:**
   `tests/teeth.sh <file> <sed-expr> <command…>` perturbs the guarded file, runs
   the check expecting nonzero, and restores under a trap — with two refusals
   the manual ritual lacked: a perturbation that changes **nothing** is rejected
   as VACUOUS (a no-op proves no teeth), and an interrupt cannot leave the
   perturbation in the tree. Exit codes: 0 teeth-OK / 1 TOOTHLESS / 2
   vacuous-or-usage; all three demonstrated on `refs_lint.sh` before first use.
   The original `make teeth TEST=<name>` revert-a-patch-hunk form was **not**
   built: it needs named-patch bookkeeping that nothing else maintains, and one
   day of M373–M380 work used the sed form six times — the ritual the helper
   should mechanize is the one actually practiced.
2. ~~**A harness-parity lint.**~~ **Built (M271), both tiers.** `rig_lint.py`
   check 6 rejects any driver naming the binary as a literal (`_e2e.BIN` is the
   only sanctioned spelling); `smoke_lint.sh` check 5 gained the *absence* side
   its message had always claimed — presence of `"$BIN"` never proved a driver
   did not ALSO call a PATH-resolved `jichi`. Failure mode 7 is now an invariant
   rather than a habit.

   Two notes from building it. The smoke half's first draft flagged three
   drivers, and all three were **false positives**: prose naming the client ("the
   shipped `jichi control` client") matched because backtick command
   substitution was in the command-position set. This tier uses `$( )`
   throughout, so backticks came out and comments are stripped first — a lint
   that cries wolf on comments gets disabled. And both halves were proven with a
   throwaway canary driver that calls a bare `jichi` *alongside* a correct
   `"$BIN"` call, since the interesting failure is the mixed one, not the
   wholly-wrong one.
3. ~~**Count the checks per module.**~~ **Its intent is built (M390); its stated
   mechanism is rejected.** The failure this recommendation was about — an
   accidentally-unreferenced test file going dark while the one growing total hides
   it — is now a lint, not a report: `tests/smoke/unit_orphans_lint.sh` requires
   every column-0 `void test_*(void)` definition to be called by the runner, every
   call to be defined, and every `jc_test.h` declaration to have a definition. The
   toolchain cannot do this for you: a non-static function nobody calls is legal C.
   The smoke tier has had the same check for its drivers since M271; the unit tier
   simply lacked it. Born green (146/146/146 clean), so its teeth were proven by
   perturbation — a renamed definition, and a planted dead declaration.

   **Rejected: a per-file check-count baseline.** A per-file count is only
   *information* until something compares it against a committed baseline, and that
   baseline must be edited on every test addition — the rotting-numbers problem
   M259 fixed and M307 answered with bounds. It would trade a silent gap for a
   maintenance burden that decays into the same silence.

   **The maintenance-free remainder, deferred (DEFERRED.md):** assert that every
   test function contributes **at least one check**, so a stub, a commented-out
   body, or an early return cannot pass as a test. That is failure mode 3 at
   function granularity, and it needs no baseline — only a per-function delta on
   `jc_test_checks`, which in turn wants the runner's 146 hand-written
   `printf(name); test_name();` pairs to become a `{name, fn}` table.
4. **Periodically re-run the M201 question.** It was asked once, and the answer was
   "yes, in one class, and the class was larger and worse than assumed". It has not
   been asked since about the *unit* suite (only the e2e rig). **What it needs and
   what it would mean** (recorded M390): the unit runner takes no arguments, so
   there is no way to run one test alone — the same `{name, fn}` table above is the
   prerequisite. Then run each of the 146 alone and compare with the in-suite
   result, **both directions**: a test that fails only in-suite (state leaked into
   it) and one that passes only in-suite (it depends on a predecessor's setup).
   Candidate shared state is real — the `jc_msg` language global, registered
   redaction secrets, log level, locale, cwd, static caches. Expect a **zero**:
   `test_msg` already ends with `jc_msg_set_lang(JC_MSGL_EN)` and the comment
   "leave the process-wide default as tests found it", so the discipline exists in
   at least one place. A clean zero is a result worth recording, like M343's 0/21.

   **Any change to the runner must prove the instrument first** — failure mode 1 is
   "the instrument is broken", and M201's incident was exactly that. The teeth for a
   table refactor: the total check count identical across the change (11,305 →
   11,305) and a planted failure still reported with the right name and count.

---

---

## The regress, and where it actually stops

The question does not close. `rig_lint.py` watches 72 drivers; its own source
says so:

```python
# _e2e.py is the ONE place a reader may live; rig_lint does not test itself.
EXEMPT = {"_e2e.py", "rig_lint.py"}
```

That exemption is correct — a lint cannot meaningfully lint itself — and it is
also the honest edge of the regress. Something is always unwatched. The practical
question is not "how do we watch everything" but **"what is unwatched, and is it
small and simple enough to read in one sitting?"**

`_e2e.py`'s `recv_http_request` is the load-bearing exemption: sixteen drivers
went wrong precisely by *not* using it, and the whole rig now depends on it being
right. So the standard for that file is different from the standard for a driver
— it is the one place where careful reading, rather than a lint, is the control.
Keep such files few, short, and named.

The regress stops at three things, in this order:

1. **Reality.** A test suite is a proxy; the product meeting a real workload is
   not. The zigodot demo sweep exists because a rigorous corpus still missed the
   top real blocker twice. Dogfooding is the same instinct: jichi builds zigodot,
   and two 1.5M-token failures (M207) were found by *use*, not by tests.
2. **Falsifiability.** A test proven to fail without its fix has been observed
   working once. That single observation is worth more than any amount of review.
3. **A short, named, deliberately-unwatched core.** Not zero — small and stated.

## The short version

- A test suite is software with no test suite. Assume it has bugs.
- When a test fails, suspect the instrument before the product.
- A green with no denominator is not evidence.
- If you found it by reading, encode it as a lint — the audit finds only what it
  knew to look for.
- A test never seen failing has never been seen working.
- Never retry to green; classify the failure instead.
- Verify you are testing the artifact you just built.
- Anchor a transcript assertion on something unique to the surface under test — a
  PTY log holds every command's output, so a bare grep tests the log, not the code.
- Check whether a gate is RUN before checking whether it is correct. jichi's
  multi-toolchain gate was correct and unexercised; five classes of breakage
  accumulated across three toolchains, the oldest over 33 milestones (#47).
- An incremental gate does not cover what it does not rebuild; a configuration-
  specific gate does not cover configurations nobody builds; and a gate that
  cries wolf does not get run at all.
- Grep for the call, not the token. A check that an identifier "appears in the file"
  is satisfied by a comment, a string, or a test name chosen to satisfy it.
- The strongest check is one the thing under test cannot author: have the gate build
  its own fixture and assert an outcome.
