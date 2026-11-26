# Which check caught which defect: notes to catchers.tsv

Written 2026-11-22 for the case studies of the four jichi drives on jichi. `catchers.tsv` has one row per
defect found and confirmed during a drive, 217 rows (ids C001 to C217, sorted by drive, then by date). These
notes give the counts, the record drift that lints caught, eight teaching examples, and what the table cannot
tell you.

## 1. How to read the table

**The four drives** (column `drive`):

| label | the drive | records read up to |
|---|---|---|
| hardening-1 | the first hardening drive, 2026-10-17 to 10-23 (shell-script harness) | its end |
| harness | the drive that built `jichi-harness`, 2026-10-23 to 10-27 | its end, plus one record defect found on 2026-11-22 |
| hardening-2 | the hardening drive under `jichi-harness`, 2026-10-27 to its pause on 11-05 | the pause |
| harness-port | the drive porting the host scripts into `jichi-harness`, from 2026-11-05 | 2026-11-22 17:22 (still running) |

A row belongs to the drive **during which the defect was found**, not the one that wrote it. Example: the
missing NULL in `gzip`'s argument list was written in the harness drive and found in the harness-port drive's
platform retest, so it is a harness-port row (C159).

**What counts as a defect.** A real fault in jichi's code, its tests, its build, the gate or harness scripts, or
its records, with a fix or a confirmed reproduction in the sources. Not counted:
- the 111 findings of the 2026-10-17 hardening review: they were the drives' input, not catches;
- lint reds that only showed a count or a banner out of date (section 3 counts them separately);
- claims that were later withdrawn;
- the supervisor's and the lead's process slips (typed times, wrong ids, a cap on a run): they are mistakes in
  how work was done, not faults in the project.

**Columns.** `defect` is under 70 characters. `missed_by` names the checks that ran before and did not see it,
or says "not recorded". `fixed_in` is a milestone or commit, or says that the sources read do not name one.

**The `caught_by` classes, as assigned here.** A row gets the instrument that first showed the defect.
- **red-first test**: a test written before its fix went red for a reason nobody had planned, and that reason
  was a second defect.
- **fast gate**: the team's own checks before READY (`make WERROR=1 test`, the drivers it touched, the
  keep-going smoke).
- **make ci: ...**: the stage of the full gate that went red. The gate's pre-gate sweep runs some of `make ci`'s
  stages early (lints, the `-O2` unit build, the mutant sweep); its catches are filed under the stage it ran.
  "release/32-bit/other stage" also covers the mutant sweep, the M729 unwritable-TMPDIR stage, the `-O2` and
  boundary stages and the e2e tier.
- **independent review**: someone who did not write the code read it. 38 rows: 19 by the drives' reviewer
  model reading each batch, 9 by the lead's own reviewer sub-agent in a fresh context (C026, C051, C056, C060,
  C069, C193, C197, C199, C212), 7 by the supervisor's design review of 2026-11-11 (C141-C147) and 3 by the
  review of the harness reading course (C175-C177).
- **supervisor re-check**: the supervising agent found it on purpose: by re-running a claimed test without its
  fix, verifying by effect, measuring, reading source, or running a sanitizer or census of its own.
- **shadow / switch-over replay**: a port running beside the script it replaces, or a replay of a real event
  through both, before the switch.
- **first real use**: the fault showed itself while the code did its real job (a run of the drive, a push, a
  re-pin), not in a check.
- **platform retest**: a run on another operating system, C library, compiler or architecture.
- **the operator**: the human maintainer noticed it.
- **other** (13 rows): the gate's commit-ledger stage (missing commit trailers, 5 rows), agents writing the
  harness reading course (4), the case-study re-derivation of 2026-11-22 (3), and the drive's own `-O2` pinned
  build (1).

**Source abbreviations** (column `source`; a number after a colon is a line, a number after a space is a section):

| abbreviation | file |
|---|---|
| H1F, H1R | `docs/internal/2026-11-22-drive-case-studies/hardening-2026-10-17/facts.md`, `docs/case-studies/hardening-2026-10-17/README.md` |
| HAF, HAR | `docs/internal/2026-11-22-drive-case-studies/harness-2026-10-23/facts.md`, `docs/case-studies/harness-2026-10-23/README.md` |
| H2F, H2R | `docs/internal/2026-11-22-drive-case-studies/hardening-2026-10-27/facts.md`, `docs/case-studies/hardening-2026-10-27/README.md` |
| HPF | `docs/internal/2026-11-22-drive-case-studies/harness-port-2026-11/facts.md` |
| HPR, HPD, HPP | `docs/case-studies/harness-port-2026-11/README.md`, `DATA_SHEET.md`, `PLATFORM_RETESTS.md` |
| APPX | `docs/internal/2026-11-15-harness-port-case-study/README.md` |
| FIND | `docs/internal/drive/2026-10-17-hardening/FINDINGS.md` |
| RS19 | `docs/internal/reviews/2026-10-19-supervision/review-summary-2026-10-19.md` |
| ROAD | `docs/ROADMAP.md` |
| LOG4 | `docs/internal/drive/2026-11-05-harness-port/LOG.md` |
| ANDR, ANPR | `docs/analysis/2026-11-11-reviewing-our-own-decisions.md`, `docs/analysis/2026-11-12-platform-retest.md` |
| AN5, AN3 | `docs/analysis/2026-11-13-five-harness-port-defects.md`, `docs/analysis/2026-11-13-three-defects-the-review-found.md` |
| SL1, SL2, SL3, SL4 | the supervisor's log of each drive, in order (kept on the drives' host) |
| DG3, DG4 | the gate's verdict digest of the second hardening and the harness-port drive (kept on the host) |

## 2. The counts

| caught_by | all | hardening-1 | harness | hardening-2 | harness-port |
|---|---|---|---|---|---|
| red-first test | 2 | 0 | 0 | 0 | 2 |
| fast gate (unit/drivers run by the team) | 2 | 0 | 0 | 1 | 1 |
| make ci: compilers/warnings | 4 | 0 | 1 | 1 | 2 |
| make ci: sanitizers/valgrind/leaks | 5 | 3 | 0 | 1 | 1 |
| make ci: unit or smoke tier | 17 | 2 | 8 | 3 | 4 |
| make ci: release/32-bit/other stage | 14 | 2 | 3 | 3 | 6 |
| independent review | 38 | 5 | 12 | 1 | 20 |
| supervisor re-check | 87 | 30 | 12 | 24 | 21 |
| shadow / switch-over replay | 6 | 0 | 0 | 0 | 6 |
| first real use | 15 | 6 | 3 | 5 | 1 |
| platform retest | 13 | 0 | 0 | 0 | 13 |
| the operator | 1 | 1 | 0 | 0 | 0 |
| other | 13 | 1 | 1 | 1 | 10 |
| **total** | **217** | **50** | **40** | **40** | **87** |

The four `make ci` classes together caught 40 defects. By area: product code 98, tests 58, gate or harness
scripts 31, records 19, build and toolchain 11. 24 of the 31 gate or harness script defects were found by the
supervisor, most of them in its own scripts.

## 3. Record drift caught by lints (not rows)

**Definition.** A red gate run whose every failing check is one of the four record-currency lints:
`docs_counts_lint` (a count or banner), `milestone_currency_lint` (the newest milestone), `changelog_coverage_lint`
(the changelog behind the roadmap) and `i18n_tracks_lint` (a translation still showing the old number).

| drive | red gate runs that were record drift only | red gate runs in all |
|---|---|---|
| hardening-1 | 24 | 86 (red or invalid) |
| harness | 11 | 48 |
| hardening-2 | 1 (2a4d4f33) | 8 `make ci` reds; none of the automated gate's verdicts was drift |
| harness-port | 2 (6d23be29, 6e5468fa) | 22 (15 to 11-15, 7 since) |

How they were counted: hardening-1 from `CS/hardening-2026-10-17/gatereds.out` (the failing drivers of each red
log); harness from `CS/harness-2026-10-23/numbers.out` section 17; hardening-2 from H2F:284-293; harness-port
from APPX:84-98 and HPF:320-328. Two cautions:
- In hardening-1, the count lint itself could not read a count of 1000 or more (C038). Some of its reds on
  2026-10-21 were that defect, not drift; the logs read do not separate them.
- Reds that the team's own checks or the pre-gate sweep caught before a gate ran are not in these numbers.

Lint-only reds that failed a **rule** lint (portability, POSIX utilities, smoke rules, snapshot, licence) are not
drift. The sources describe most of them only by the lint's name, so they are rows only where a source names
the defect: 28 such red gate runs in hardening-1 and 12 in the harness drive have no row.

## 4. Eight examples, one per kind of check

**1. A sanitizer saw a leak no other stage could (C011, make ci: sanitizers).** Batch 0 of the first hardening
drive added a test case that created two small apps and never freed them. The code was right and every test
passed; only memory was lost. The lead's READY check ran the full smoke tier, which runs no sanitizer, and the
supervisor's own sanitizer runs were older than the case. `make ci`'s clang build with LeakSanitizer reported
16,480 bytes lost in 6 allocations. A test can pass and still be wrong in a way only a memory tool can see.

**2. A race that passed seven times (C014, make ci: unit tier, fault-injection build).** The background-command
spool was capped by reading its end and rewriting the file while the command could still write. Batch 0's own
gates passed the test seven times. At batch 1's gate the machine was busy (three workers were building), the
window was wide enough, and the last line of output vanished. The supervisor then proved the cause with an
instrument, a pause inside the window, instead of re-running and hoping. A green run of a racy test proves only
that the race was not hit that time.

**3. One compiler is not enough (C134, C136, C150, C151, make ci: compilers and the release stage).** In the
harness-port drive four gate reds were compiler errors that only one stage's flags showed: the fortified `-O2`
build twice, the C++ build, and clang. The team's fast gate used one compiler, so each cost a gate attempt, and a
green attempt took an hour. Compile new code with every compiler and flag set the gate uses before asking for a
gate.

**4. A reviewer saw what a green gate could not (C120, independent review).** A fix made jichi retry a broken
stream. `make ci` was green and the batch landed. A reviewer model in a fresh context rated the fix WRONG: it also
retried an error meaning "this request is too long", so one call became four, and on a priced model each costs
money. The test tier's fixture allowed zero retries, so no test could see any retry at all. A review reads the
condition, not only the outcome of the tests you wrote.

**5. The supervisor re-ran the claim on the real binary (C044, supervisor re-check).** A change turned off
secret redaction completely when no search key was set. The team's redaction drivers passed, because `make test`
does not rebuild `jichi`: the drivers ran a binary built hours before the change. The supervisor re-ran them on
the pushed commit, and three went red. A test result is evidence only for the binary it ran. In the same spirit,
the supervisor's sanitizer pass in the harness drive found a write past the end of an array (C078) that every
green gate had missed, because the gate's sanitizer stage ran the new program only as `--version`.

**6. A port that agreed for days was still wrong (C170 and C216, shadow / switch-over replay).** The C port of
the gate watcher ran beside the shell script and agreed on 524 of 524 ticks. A replay of one moment no real event
had reached, a gate already running, showed the port would start a second `make ci`: `systemctl` indents its
lines by two spaces, and the port read the empty first word as the unit name. Its test's stand-in printed the
names flush left, as no real `systemctl` does. Later, the repin port's copies of the binaries compared equal with
`cmp` and still lost their exec bit (C216): `cmp` reads contents, not file modes. Replay the branches real
events have not reached, and compare the property that matters.

**7. Another machine found what x86-64 hid (C159, C160, platform retest).** An argument list for `gzip` had no
NULL at its end; on x86-64 the next slot happened to hold NULL, so every gate passed, and on an aarch64 board
`gzip` failed and a note vanished without a word. A `va_list` passed by value to a helper in the C89 `snprintf`
fallback read the wrong argument on every non-x86-64 board; no x86-64 binary even contained the fallback. Three
gates on one kind of machine are one sample.

**8. The first real use met the real peer (C091, first real use).** The credential helper's tests wrote the
remote's path with a leading slash. Real git sends it without one. On the first real drive under
`jichi-harness`, the helper refused its own configured remote, and every push to the real server would have
failed. Build a fixture from the real program's output, not from memory.

Two classes have only one or two rows and are worth a line each. **The operator** noticed that drive commits carried
a placeholder author (217 of them) and a no-reply trailer (208), set by the harness image and the supervisor's
prompt; no check existed for it (C029; a lint, M999, came after). **A red-first test** for a push refusal went red for a reason its author had
not planned, and the reason was a second defect: the product freed the capture that would have named the cause
(C213).

## 5. What the counts cannot tell you

- **The records are mostly the supervisor's.** The supervision logs are the richest source, so the supervisor's
  catches are over-represented: 87 of 217 rows. The table measures what the records describe, not how often each
  check fires.
- **The fast gate's catches are almost invisible.** The team fixed what its own checks caught before it pushed,
  and nobody wrote those down. Only 2 rows remain. A low count here does not mean the fast gate was useless.
- **Gate reds on intermediate heads** (the CI follower gated every pushed head) are rows only where a source
  names the defect; see section 3 for the lint reds without rows. The 12 single-object compile failures and the
  unit-target reds among hardening-1's follower reds are not described in the sources read.
- **Some classes are a judgement**, stated in section 1: the lead's reviewer sub-agent is filed under
  independent review; the ledger stage, the reading course and the case-study re-derivation are "other".

**Found but not classified, so not in the table:**
- An orphaned grader that kept one core at 100 % for 3.6 days (L-28, FIND:324-347): the records say the operator
  killed it on 2026-10-20 at about 00:00, but not who noticed it first.
- A SIGINT before the run's pid was recorded was lost (HAF:307): "reviews + supervisor", and the log does not
  say which saw it first.
- The UTF-8 character split by redaction (def-284, SL1:4488-4498; again as M1065): it was in the register before
  the drives began.
- The credential helper's userinfo and percent-escape handling (M1066), and the watcher ignoring a re-READY of a
  finished head (HPF:345-347): found by reading only, with no fix or reproduction in the sources.
- The hook bypass through duplicate keys (M1113-b) and the six fail-open redaction sinks (C-19): consequences of
  hardening-review findings, so part of the drives' input.
- The leaks card of 2026-11-21 (24 drivers, about 20 stacks, SL4:10229-10231): not described defect by defect.
- An empty `~/.jichi` file in the drive's home and a reviewer killed by SIGKILL (HPF:562-563, 312): causes not
  established.
