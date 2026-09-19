# The FreeBSD row, six failures, and four probes I built that were broken

**2026-09-18 (M665).** The FreeBSD row was re-run because retiring the Pi Zero row
had left it at a coverage debt of **102** — it last recorded 201 smoke drivers on
2026-08-17 against 303 in the tree today. The first re-run put it at **296 of 303** and found six failing
drivers. **Five were harness defects, not platform ones**; with those fixed the
row ends at **302 of 303 drivers, 1,727 checks** — coverage debt **102 → 1**.
The one driver left is measured and **not** diagnosed, and this page says so.

That is the ordinary half. The half worth reading is that **every probe I wrote by
hand during the investigation was broken, four times in a row, and two lint
extractions I wrote the same afternoon were vacuous against the very defects they
were written for.** None of that was found by reasoning. All of it was found by
perturbation — by reverting the thing each check guards and watching whether the
check noticed.

---

## 1. The rig could not report its own denominator

The first run failed and the log had **no driver count**. `scripts/tier-v-bsd.sh`
captured `smoke: OK (N drivers, M checks)` on the success branch and, on the
failure branch, only the failing checks. So a row that *failed* came back with no
count — and its coverage debt stayed uncomputable, which is the exact gap
`PLATFORM_RETEST.md` already names for the Pi 400.

A partly-green row is precisely the one whose count a reader wants. The number is
free at the time and unrecoverable afterwards. Fixed: both branches now capture it.
That one line is what turned this session's result from "FreeBSD still fails" into
**296 of 303, debt 102 → 7**.

---

## 2. The six failures

| driver | cause | status |
|---|---|---|
| `ctx_estimate_lint` | `grep -rc` on a **single named file**. BSD grep's `-r` implies `-H`, so the count came back `src/chat/jc_compact.c:2` and the comparison against `2` failed. `-r` was doing nothing on GNU grep. **Two checks, same line shape.** | fixed, verified on guest |
| `cppcheck_lint` ch.1 | Its universe is enumerated by `git ls-files`, and `scripts/_rig_ship.sh` ships a tar that **deliberately excludes `.git`**. Every header looked untracked. This failed on FreeBSD, illumos and both Pi rows *at once*. | fixed (skips off a checkout), verified |
| `cppcheck_lint` ch.3 | Ran bare `make`; FreeBSD's `make` is **bmake**, which cannot parse this Makefile. It reported `rc=2` for a target that prints `cpp-check: OK` under `gmake`. | fixed via new `smoke_make`, verified |
| `man_page_lint` ch.7 | `man --warnings` is man-db's option; FreeBSD's man is mandoc-based and answers `Illegal option --`. The check reported a defect in the page **having never read it**. | fixed (picks a renderer), verified |
| `peer_reap_grace` ch.2 | See §3. Not jichi, and not the reap. | fixed, verified |
| `install_no_build` | Bare `make` at one call site, and `"${MAKE:-make}"` at three more — whose fallback is bare `make` exactly when a driver runs **standalone**, which is what the rig does to classify a failure. Its own header said `${MAKE}, not make (M661)`. | fixed, verified on guest (and it closes the illumos failure too) |
| `setup_keyfile` ch.22 | A **~150-column** line in the pre-prompt discard notice, against the project's own 76-column wizard rule. Unwrapped on every platform; the flush path is simply not reached in that driver on Linux. | fixed in `src/tui/jc_term.c`, verified |
| `parallel_abort` ch.1 | Only one child reaches the mock. See §6. | **measured, not diagnosed** |

### What mandoc found that GNU man hid

Switching renderers paid for itself on the first run. `mandoc -T lint` reported two
things `man --warnings` accepted silently, both real:

- `.B \\ (trailing backslash)` — a literal backslash in roff is `\e`; `\\` is an
  undefined escape and was being printed literally.
- `.TH JICHI 1 "2026\-08\-22"` — roff-escaped hyphens inside the date make it
  unparseable as a date.

Both fixed in `man/jichi.1`. A second renderer is a second opinion, and this is the
third time a non-Linux row has been the one to give it.

---

## 3. `peer_reap_grace`: the driver accused the fix it exists to defend

The failure read:

```
not ok 2 - took 50s -- that is the deadline ending it, which is what a
           blocking waitpid on a TERM-trapping child looks like
```

That sentence names a cause. The check could not see a cause; it could see a
duration. **The explanation was written into the driver in advance and printed as
though it had been measured.**

What is actually true, measured with the tier's own fixture:

```
# INSTRUMENT: jichi own exit took 0s
# INSTRUMENT: with_deadline returned after 50s      <- same command, same fixture
```

jichi exits **immediately**; `jc_worker_reap_grace` works on FreeBSD exactly as
designed. The 50 seconds belong entirely to the wrapper. FreeBSD's `timeout(1)`
acquires **reaper** status (`procctl(PROC_REAP_ACQUIRE)`) so it can kill a whole
tree — so when jichi reaps the deaf MCP server, the server's own child (the
`sleep 120` that makes it linger) is reparented **to `timeout`**, and `timeout`
waits for it. The wrapper was timing the fixture's grandchild.

Confirmed independently before any of that: at 6 s, 14 s and 26 s there was **no
`jichi` process at all** — only `timeout` in `sigsusp` with the orphaned `sleep`
as its child.

**And the same behaviour bit one level up.** With check 2 fixed the driver still
failed *in suite*, while the harness's own captured output showed `1..3` and three
`ok` lines. The smoke runner wraps **every** driver in `timeout` (the deadline
wrapper in `tests/smoke/_smoke.sh`), and the deaf mock's orphaned `sleep 120` held
*that* open to the per-driver limit — a green driver reported red because of a
grandchild it left behind. The mock now lingers by blocking on a read from a fifo
inside its own shell, forking nothing at all: **3 s, rc=0** under the runner's own
wrapper, where it previously ran to the limit. The general hazard is worth stating
plainly: **on FreeBSD, a driver that leaves an orphaned descendant can fail the
tier with every check passing.**

The fix is to time the subject rather than the wrapper: run jichi as the driver's
own child and `wait` for it, since a shell waits for its own children only, and
keep the 45-second bound as a watchdog that kills rather than as the thing being
timed. Re-proven red (45 s, watchdog kill) with `jc_worker_reap_grace` reverted to
a blocking `waitpid`, and green (0–1 s) restored. The failure message no longer
asserts a location it cannot observe.

---

## 4. The part worth the page: four hand-built probes, all broken

Investigating §3 I wrote four probes. **Every one was wrong, and each was wrong in
a way that produced a plausible, publishable-looking number.**

| # | what I built | what was broken | what it would have "shown" |
|---|---|---|---|
| 1 | a script rebuilding the deaf-MCP fixture by hand | sourced `_smoke.sh` outside a driver, so `$SMOKE_TOOLS` was empty and the mock's `JQ` became `//tests/tools/jsonq` | jichi blocked in `select()` for 121 s — a hang that was **my mock failing to answer** |
| 2 | feeding that mock an `initialize` line directly | same fixture | "the mock never answers on FreeBSD" |
| 3 | the same, with `SMOKE_TOOLS` exported to fix #1 | `. tests/smoke/_smoke.sh` **overwrites** it after the export, so the export did nothing | identical broken number, now with a fix that felt applied |
| 4 | an instrumented copy of the real driver | a `'"'"'` shell escape written **literally** into the file by the generating script — unbalanced quotes | a syntax error, which at least announced itself |

Probe 1 is the dangerous one. It produced a coherent story — *jichi hangs on
FreeBSD waiting for an MCP response* — supported by a real kernel stack, and the
story was entirely an artefact of my own fixture. Had I stopped there I would have
filed a jichi defect against a platform where jichi was behaving perfectly.

**What worked was never a probe.** Both valid measurements came from the tier's own
machinery:

- running the **real driver** and sampling `ps`/`procstat` beside it, which is how
  the absent `jichi` process and the reparented `sleep` were seen;
- an instrumented **copy of the real driver**, which keeps `_smoke.sh`, `smoke_tmp`,
  `smoke_write_mock_mcp` and `$BIN` exactly as the failing run had them and adds
  only a `date` on either side of the subject.

> **The rule this session earned: do not rebuild a failing test's fixture. Instrument
> the failing test.** A fixture is a program; a copy of it written from memory under
> time pressure is an *unreviewed, untested* program, and the number it prints is
> indistinguishable from the real one. The tier's fixtures are proven by every other
> driver that uses them. A hand-built one is proven by nothing.

This is not a new lesson here, which is the uncomfortable part. `docs/ANECDOTES.md`
#81 is *"Nine guesses, nine measurements already available"*; `TEST_INTEGRITY.md`
has a whole section on auditing the universe; `peer_line_cap.sh`'s own header
records that its first fixture deadlocked and reported 45 s "as though it had
measured the shutdown path. It had measured nothing of the kind." I read that
sentence while fixing this file and then did it again, twice.

---

## 5. Two lint extractions that were vacuous, and how that was caught

Two lints written **the same afternoon**, each as the durable form of a fix, each
green on a clean tree, each blind to the exact defect it existed for:

**`posix_utils_lint` check 20** — bans `grep -r` pointed at a single named file.
Its line predicate matched the literal word `grep`. Both real call sites spell it
`"$G"`. Reintroducing the actual defect changed nothing: the check stayed green.

**`portability_lint` check 14** — pins README's never-compiled prose against
`PLATFORMS.md`. Markdown is hard-wrapped, and the stale sentence had `never been`
on one line and `compiled` on the next, so a line-based grep could not see it.
Restoring the operator's real sentence: still green.

Both were floored at **0 violations**, and that is why the count could not help:
**zero findings and a broken extraction look identical.** The only thing that
separated them was reverting the defect and watching the check fail to notice —
`CLAUDE.md`'s "perturb per CHECK", applied to a lint rather than to a driver.

A third, smaller instance in the same file: my new check's
`grep -c . 2>/dev/null || echo 0` printed **two** zeros on empty input, because
`grep -c` prints `0` *and* exits 1, so `[ "0\n0" -eq 0 ]` is a syntax error rather
than a pass. And the em dash I wrote as `\xe2\x80\x94` was caught on FreeBSD by
this tier's *own* check 12, which bans GNU hex escapes in a `sed` pattern.

---

## 6. What is measured and not diagnosed

Stated separately because naming a cluster is not the same as naming a cause.

**`parallel_abort` check 1.** Reproducible, not flaky. The mock records the parent's
turn plus one 6,220-byte `TASK_` request; on Linux the same run records the parent
plus **two**. So one child of the fork pool does not reach the mock on FreeBSD.
Whether it never spawned, or its request was lost, or the abort cut it, is **not
established** — and because check 1 is the anti-vacuity guard, checks 2 and 3 of
that driver pass there without measuring anything.

**`install_no_build` turned out to BE the bare-`make` family** and is fixed, which
is worth recording because the reason it looked otherwise was its own header: it
says *"`${MAKE}`, not `make` (M661)"* while one line still ran bare `make` and
three more used `"${MAKE:-make}"`, whose fallback is bare `make` exactly when a
driver is run standalone — which is what the rig does to classify a failure. **A
rule stated in a header and applied at some sites is not a rule**, and reading the
header instead of the code is how I nearly parked it a second time.

**The bare-`make` family.** A tier-wide count stands at install 15, clean 11, test 9,
ci 7, info 6, smoke 4. `smoke_make` now exists in `_smoke.sh` and **one** site is
converted — the one that failed. Converting the rest blind, across sites whose
behaviour on four kernels has not been measured, is how a portability fix becomes a
portability bug.

**README's platform count.** The sentence "Nineteen of them" is pinned by nothing.
It was not changed, because a replacement number would be a guess.

---

## 7. What changed

- `scripts/tier-v-bsd.sh` — the driver count is captured on the failing branch too.
- `tests/smoke/ctx_estimate_lint.sh` — two `grep -rc` on named files de-recursed.
- `tests/smoke/cppcheck_lint.sh` — check 1 skips off a git checkout; check 3 uses
  `smoke_make`.
- `tests/smoke/man_page_lint.sh` — check 7 selects `man --warnings`, else
  `mandoc -T lint` (STYLE excluded), else skips the render half.
- `tests/smoke/peer_reap_grace.sh` — check 2 times jichi itself and polls rather
  than backgrounding a watchdog subshell (`smoke_lint` check 15 refused that: on
  OpenBSD ksh `$!` after a subshell is the subshell); the deaf mock lingers on a
  fifo read instead of forking `sleep`; the failure message no longer asserts a
  cause it cannot observe.
- `tests/smoke/install_no_build.sh` — four make invocations converted to
  `smoke_make`, including three `"${MAKE:-make}"` whose fallback is bare `make`.
- `src/tui/jc_term.c` — the pre-prompt discard notice wrapped to 76 columns.
- `tests/smoke/_smoke.sh` — `smoke_make`.
- `tests/smoke/posix_utils_lint.sh` — check 20, `grep -r` at a named file.
- `tests/smoke/portability_lint.sh` — check 14, README prose vs PLATFORMS.md.
- `man/jichi.1` — `\e`, and an unescaped `.TH` date.
- `README.md` — illumos is no longer described as never compiled.
