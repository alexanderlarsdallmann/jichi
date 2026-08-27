# The project's own test tiers

*Contributor reference: what jichi's own test tiers are, what each holds, and
which incidents shaped them. The **rules** distilled from this live in
[`../CLAUDE.md`](../CLAUDE.md); the doctrine lives in
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) (why a test lies) and
[GATE_INTEGRITY.md](GATE_INTEGRITY.md) (why a gate lies).*

*This text was cut out of `CLAUDE.md` at M516, when the rules file was reduced to
what must reach a model on every request (77% of it could not —
`analysis/2026-08-21-self-hosting-first-review.md` §5). It landed in
[TESTING.md](TESTING.md), which documents the `run_tests` **tool**, and sat there
until M527 — two thirds of a page whose title and index entry described the other
third. It is its own page now, indexed for the audience that needs it.*

*The M516 note asked for a de-duplication pass against TEST_INTEGRITY.md and
GATE_INTEGRITY.md. **It ran at M527 and found almost nothing**: across those three
pages plus PLATFORMS.md there is not one identical sentence, and exactly one
paragraph pair scored above 0.30 Jaccard on content words — the audit-the-universe
material below, now a pointer instead of a retelling. Method:
sentence-level exact matching plus paragraph-level Jaccard over content words,
both directions, cross-file and within each file. The worry was reasonable and it
was wrong; recorded so nobody searches again.*

`tests/test_*.c` each expose a `void test_<name>(void)` declared in
`tests/jc_test.h` and called from `tests/test_main.c`. Tests must not require
network access (provider streaming is tested by feeding synthetic SSE events).
Add new tests by following that pattern and wiring them into `test_main.c`.

`tests/smoke/` (M209) is the **Python-free smoke tier**: POSIX-sh drivers
(TAP output, sourced `_smoke.sh` lib, listed in `tests/smoke/run.sh`, linted
by `smoke_lint.sh`) backed by four test-only C89 helpers in `tests/tools/`
(`mockmodel` — scripted HTTP/SSE mock model server with request capture;
`ptydrive` — expect-lite PTY driver; `jsonq` — dot-path JSON asserts via the
in-tree cJSON; `sockq` — one-shot AF_UNIX line client for the daemon/control
sockets). The M209–M217 port moved the whole portable e2e suite here (71
drivers): so **`make check-target` (= `test` + `smoke`) is a full build gate
on any POSIX box**, `make e2e` skips loudly without python3, and python3 is
optional-recommended. Only a permanently-Python residual stays under e2e
(`redraw`'s VT emulator, the `stress`/`web_bridge` example products,
`curriculum_graders` needing cc, the model-gated live checks, `rig_lint`).
One driver, one tier — enforced by `smoke_lint.sh`; when porting, delete the
Python original in the same commit. Design
+ decisions: `docs/plans/2026-07-python-free-testing.md`. Two hard-won driver
rules: run jichi with stdin closed (`< /dev/null`), and give PTY scripts
human-scale `delay`s between sends AND a settle `delay` after each
command-output `expect` before the next send — `expect` returns mid-render,
and the M156 burst-paste window merges
back-to-back writes into one logical line).

**A new test must be shown to fail without its fix** (revert the guard, run,
confirm the failure count, restore) — a test never observed failing has never been
observed working. The **documentation-coverage** lints close the loop the numeric audits left open:
`tests/smoke/config_keys_lint.sh` (M305) fails the build when a top-level config key
jichi parses is documented nowhere — it found `systemPrompt` (free text appended to
the system prompt *and* every subagent's), `timeFormat` and `numberFormat` — and when
a `docs/` page is linked from nowhere. Its scope is stated: top-level keys only,
nested objects excluded explicitly. Plain-register entry points live at
`docs/PLAIN_LANGUAGE.md` and `docs/i18n/de/EINFACHE_SPRACHE.md` (German is the
original; both are separate pages, never simplifications of the dense ones).

`docs/TEST_INTEGRITY.md` records how this project's own suites
have failed while green (M201's sixteen truncating request readers reporting
regressions that never happened; M86's verify that passed while running nothing;
the bench grader that scored five correct edits as failures; M293/M296's two PTY
fixtures that matched a string printed by a *different* surface in the same
transcript) and the practices
adopted in response — chiefly **prefer a lint to an audit**, because "the audit
found what it knew to look for". The lint inventory lives there;
`tests/smoke/slash_commands_lint.sh` (M295 — every `/command` and subcommand named
in a source string must resolve) is the worked example of making one trustworthy:
narrow on facts about C and paths rather than guesses about English, **reword prose
collisions instead of keeping an exception list**, and put a floor under the
ground-truth extraction so a changed dispatch shape fails loudly instead of leaving
the lint checking nothing.

`tests/bench/` (M166/M167f) is the **live small-model bench** — a measurement, not
a gate, and deliberately outside `make ci` because it needs a running model. 11
`jc_assign` specs over fixed fixtures (21 points), each graded by its own `verify`
and each with a reference solution in `check_graders.py`, which proves every
grader two-sided **through the runner's own parser** (a grader validated by an
ad-hoc snippet with different YAML unescaping once scored five correct edits as
failures — docs/ANECDOTES.md #20);
`run_bench.py` runs one bounded headless `--auto` turn per task in a throwaway
workspace *and* `HOME`, `report.py` renders the measurement table, and
`schema_probe.py` replays a captured request body while varying the advertised
tool array. Run it on changes under `src/provider/`, `src/chat/jc_sysmsg.c`, or
the tool schemas: a small local model catches malformed-request defects that a
tolerant frontier model silently absorbs (see docs/ANECDOTES.md #19). Procedure:
docs/BENCH_LOCAL_GPU.md; results: docs/analysis/.

### Auditing a check, and not trusting your own instruments

Three milestones in a row (M508, M510, M511) found a **green lint covering less
than its own header claimed**, and the sweep that followed found four such gaps
and 24 live defects. **The four cases, their shared cause and the incident
register are in [TEST_INTEGRITY.md](TEST_INTEGRITY.md) §"Audit the universe" —
this page does not retell them.** (That retelling was the single duplication the
M527 pass found across these four pages, which is why it is a pointer now.)

What belongs here is the part a contributor acts on rather than reads. When you
write or touch a lint in this repository:

- **State its universe in one sentence in its header**, and treat that sentence
  as the thing under test. If the header says "top-level keys read straight off
  `root`", the extraction owes you all of them.
- **Enumerate the universe a second way, by a different route, and diff.** Not
  the same regex again. `[A-Za-z_]+\(root, "` finds keys without naming a single
  reader function; every string-comparison target in `main.c` finds dispatch
  shapes you did not remember. This is ten minutes and it is where all four gaps
  came from.
- **Floor the extraction at today's exact count, not a round number below it.**
  A floor of 18 under 21 items leaves room for three to disappear in silence.
- **Measure the population before building the gate.** A plan to widen the quote
  lint across `docs/` was dropped after counting: 1 verbatim quote in 110 code
  blocks. Machinery for a population of one is cost, not rigour (M510).
- **Scan what we ask other people to run, not only what we run.** The 24 defects
  were in graded assignment scripts, including two safety traps that *passed* a
  violating solution on any non-GNU `grep`. A learner failed by a broken grader
  has no way to know it was the grader.

And distrust the measurement itself — three lied in one week:

- **Verify a gate's pattern with the gate's own tool.** Inside an agent session
  bare `grep` here is a **shell-function shim**, and it read a lint's own
  `(^|[^A-Za-z0-9_])key(…)` pattern as matching **0** lines of a corpus where
  `/usr/bin/grep` matches **1,443**. That produced a nearly-published claim that
  a working lint was broken. The smoke tier is unaffected — every driver is
  `#!/bin/sh`, and `sh` resolves to `/usr/bin/grep` — which is exactly why a
  hand-check must use `sh -c '…'` or an absolute path.
- **Change one variable at a time.** A comparison that moved both the extraction
  method and the build cache had to be discarded and redone. A measurement whose
  conditions moved twice is not a measurement (the ANECDOTES #63 lesson, again).
- **A shared build cache is a variable.** Comparing an old and a new grader
  compiled each task's *pristine* fixture, and zig's shared cache then served
  those artifacts to the real gate — a correct capstone reported as failing, the
  cause visible only as a stray `/tmp/` path inside an error message.
  `tests/e2e/curriculum_graders.py` now creates its own zig cache.
- **Run every command you publish.** Three exercises in the M509 reading
  chapters were wrong until executed, and the first draft of the M511 tutorial's
  own example used `grep -c` (counting lines) and printed two numbers that were
  neither the truth nor each other.

Learner-facing version, and the one to send anybody else to:
`docs/TESTING_TUTORIAL.md` §6. Doctrine and incident records:
`docs/TEST_INTEGRITY.md` §"Audit the universe, not the result". Measured
write-ups: `docs/analysis/2026-08-21-the-lint-universe-sweep.md` and
`docs/analysis/2026-08-21-what-the-docs-quote.md`.

**Do not add an `AGENTS.md` to this repository.** `src/chat/jc_rules.c:add_dir_rules`
tries `AGENTS.md` first and **returns** if it exists — `CLAUDE.md` is only the
fallback. An `AGENTS.md` here, even a one-line pointer, would silently shadow
this entire file for every jichi run in this checkout, including jichi's own
dogfooding. Rules for this repository go in `CLAUDE.md`; `AGENTS.md` is the name
jichi *scaffolds into other people's projects* (`jichi setup`), which is why the
loader knows it.

