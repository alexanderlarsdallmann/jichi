# Robustness: the edge cases development use cannot reach (M198)

*2026-07-29. Follows `docs/analysis/2026-07-29-session-arena.md` (M197), whose
root cause was not a coding error so much as a **measurement** error: the harness
that should have caught it measured a 13-byte fixture, and the leak checkers that
should have caught it are structurally blind to lifetime bugs.*

## Why this document exists

M197 found two real defects that had survived four milestones of green CI. Both
were invisible for structural reasons, not for lack of care. That prompts a
different question from "what else is broken?", namely:

> **What classes of defect can our current way of working not see?**

Development use is a narrow, benign, single-threaded slice of the input space. It
runs one instance, on a healthy filesystem, with plenty of memory, at default
settings, driven by a human at a keyboard. Every one of those is a coverage
boundary, and each boundary has defects sitting behind it. This document
enumerates seven such boundaries, records the design decision for each, and fixes
them in order of demonstrated value.

The organizing principle — and the reason the list is what it is:

| Development use is… | …so this goes untested |
|---|---|
| a healthy state directory | degenerate/hostile state jichi did not write (**#1**) |
| a human at a keyboard | the programmatic input path (**#2**) |
| measured by small fixtures | anything proportional to input size (**#3**) |
| memory-rich | error and exhaustion paths (**#4**) |
| default configuration | flag combinations, especially `--lite` (**#5**) |
| one instance | concurrent access to shared state (**#6**) |
| well inside every limit | boundary behaviour at each cap (**#7**) |

Two of these were confirmed as live defects by direct experiment before this
document was written; they are marked **verified** below and lead the
implementation order.

---

## #1 Degenerate state in the directories jichi owns — **verified**

### Evidence

Five degenerate session stores, each probed against `ls --all`:

| store contains | observed |
|---|---|
| a directory named `x.json` | skipped cleanly ✅ |
| a zero-byte `.json` | skipped cleanly ✅ |
| truncated JSON | skipped cleanly ✅ |
| **a FIFO named `*.json`** | **hangs forever** (rc=124 at a 10 s timeout) ❌ |
| **`sessionId` ≠ filename stem** | **listed, unresumable by either name** ❌ |

The FIFO hang: `jc_read_file` (`src/platform/jc_platform_posix.c:97`) `fopen`s
without any file-type check, so `fopen`/`fread` on a FIFO with no writer blocks
indefinitely. `/sessions`, `/resume`, `ls`, and `--continue` startup all wedge
with no output, no diagnostic, and no timeout. There is no `S_ISREG` anywhere in
`src/`.

The id mismatch: `jc_session_list` takes `meta.id` from the file's `sessionId`
field (`jc_session.c:362`) while `session_path()` (`:23`) rebuilds the path from
the id. When they disagree the entry is listed but neither name resolves —
verified: `export <stem>` → "no session matching", `export <in-file-id>` → "no
session matching".

### Design decision

**Distinguish scanned paths from named paths.** This is the load-bearing
decision, and the reason a blanket fix is wrong.

- A **named** path is one the user or config supplied (`--config`, `read_file`,
  `@path`). Honouring a FIFO there is arguably correct — `--config <(jq …)`
  resolves to `/dev/fd/63`, a pipe, and is a legitimate shell idiom that a
  blanket `S_ISREG` in `jc_read_file` would break.
- A **scanned** path is one jichi discovered by walking a directory it owns
  (the session store, the index walk, the repo-map walk, skills/commands/agents/
  output-style discovery). Here a non-regular entry is unambiguously garbage and
  must be skipped, because the user never asked for it and cannot see why the
  tool hung.

Therefore:

1. New platform predicate `int jc_is_regular_file(const char *path)` (`stat` +
   `S_ISREG`), called by scanning readers **before** the read. Applied first to
   `jc_session_list`; then the other walks.
2. `jc_read_file` additionally rejects a **directory** with `JC_ERR_IO`. Today
   `fopen` on a directory succeeds on Linux, `ftell` reports a size, `fread`
   fails, and the function returns `JC_OK` with an **empty string** — a silent
   wrong answer, not a hang. Rejecting a directory cannot break a pipe, so this
   one is safe to put at the chokepoint.
3. Session identity comes from the **filename stem**, not the file's `sessionId`
   field, because the stem is what `session_path()` can find again.
   `jc_session_list` sets `meta.id` from the stem, and `jc_session_load_by_id`
   forces `s->id` to the id it was asked for, so load→save round-trips to the
   same file. A disagreement is logged once, not silently repaired.

**Rejected alternatives.** *`S_ISREG` inside `jc_read_file`*: breaks
`--config <(…)` and any future deliberate pipe read. *Filtering inside
`jc_list_dir`*: that function also backs the `list_files` tool, where showing
FIFOs and directories is correct. *Skipping id-mismatched sessions*: hides a file
the user can see on disk, trading one confusion for another. *Trusting the
in-file id*: leaves the unreachable state exactly as it is.

**Not auto-deleting** stale `*.json.tmp<pid>` files (crash leftovers from
`jc_write_file_atomic`). They already fail the `.json` suffix test so they are
inert; deleting files in a user's home on startup is a worse risk than the litter.
`doctor` reports a count instead.

### Tests

`tests/e2e/degenerate_store.py` — a table-driven driver, one case per hostile
store, asserting *bounded time* and a sane exit for each. The FIFO case is the
point: it must fail in seconds, not hang. Unit coverage for
`jc_is_regular_file` and the directory rejection in `tests/test_platform.c`.

---

## #4 Silent degradation under resource exhaustion

### Evidence

There is no fault-injection harness (`make fuzz`'s 18 targets are *input*
fuzzing). The codebase is rich in `if (… != JC_OK) continue;`. Observed
consequence under `ulimit -v`: allocation fails → `jc_read_file` returns
`JC_ERR_OOM` → `jc_session_list` skips the file (`jc_session.c:357`) → **the TUI
prints a silently truncated session list**, with no warning, no diagnostic, and a
success exit code. The user's sessions appear to have vanished.

The interesting question is not "does it crash" but **"what does the user see
when it fails?"**

### Design decision

**A compile-time-gated fault injector, not `LD_PRELOAD`.** New `FAULT=1` Makefile
knob (sibling of `SAN=1`/`SIZE=1`) compiling in `src/util/jc_fault.c`, configured
at runtime by `JICHI_FAULT_ALLOC_AFTER` / `JICHI_FAULT_READ_AFTER` /
`JICHI_FAULT_WRITE_AFTER` (fail the Nth call onward). Inert and zero-cost in
default builds — the calls compile to nothing when `FAULT` is unset.

**Rationale over `LD_PRELOAD`:** an interposer is glibc-specific and cannot work
with the static-musl build this project supports (M190); it cannot distinguish
jichi's allocations from libcurl's or cJSON's, so a targeted test is impossible;
and it needs a separate build artifact. A compiled-in counter at jichi's own
chokepoints (`block_new`, `jc_read_file`, `jc_write_file_atomic`) is portable
C89, targets precisely the layer under test, and is trivially auditable.

**Not shipped in release builds**, by construction — the knob is compile-time. A
runtime-only env gate would put fault injection in every user's binary for no
benefit.

**Then make the degradation visible.** Injection without reporting only proves
we survive; the defect is the silence. `jc_session_list` gains a skipped-count
out-param; `/sessions` and `ls` report `(N session(s) unreadable)`. This is the
generalizable half: an audit of the top error paths asking *is the loss visible?*

### Tests

`tests/e2e/faults.py` (gated on a `FAULT=1` binary, skipped otherwise, so
`make ci` is unaffected) walking the top error paths and asserting each produces
a *diagnostic*, not just a survival. Plus a unit test that a skipped session is
counted.

---

## #2 The programmatic input path differs from the human one — **partially covered**

### Evidence

`jc_term.c:780` treats a newline as a *pasted-row commit* rather than a submit
whenever more input is already buffered (`input_pending`, M156's burst-paste
fallback). The submit path is therefore selected by **inter-keystroke timing**:
a human always takes the typed branch; a fast driver always takes the paste
branch. This cost real time during M197 — a batching harness silently glued
`/sessions` and `/context` into one logical line.

`tests/e2e/paste.py` already covers both paste routes (bracketed `ESC[200~` and
the burst fallback) — better than initially assumed. What nothing asserts is the
**typed** path: that N newlines typed with human-scale gaps produce N separate
submissions. That is the most common real path and the only one with no test.

### Design decision

Add `tests/e2e/typed.py` as the deliberate complement to `paste.py`: write the
same multi-line input with inter-keystroke delays above the `input_pending`
window and assert it arrives as **separate turns**, where `paste.py` asserts one
combined turn. Together they pin both sides of a timing-dependent branch, and any
future change to the heuristic breaks exactly one of them.

**Rejected:** making the heuristic timing-independent. It is doing real work for
users on terminals without bracketed paste, and the M156 design is sound; the gap
is test coverage, not behaviour.

Document the divergence in `docs/TUI_RENDER.md` so the next person writing a PTY
driver reads it before losing an afternoon: **send one command per write and wait
for a completion marker; output silence does not mean done.**

---

## #3 Fixture proportionality

### Evidence

`soak.py`'s read fixture was 13 bytes against a defect proportional to file size,
so 200 turns retained 2.6 KB and the harness reported "no leak". One line changed
the measured slope 34×.

### Design decision

This is a **technique**, not a patch, and it resists automation — a lint cannot
know what a measurement is proportional to. So: a documented, repeatable audit
step, plus knobs where a constant currently hides a size-proportional effect.

Recorded in `docs/TESTING.md` as a checklist item for every new harness:

> For each quantity a harness measures, name what it scales with, then check the
> fixture is within an order of magnitude of a realistic value. If the fixture is
> small for speed, expose it as a flag with the small value as the default
> (`soak.py --fixture-bytes`), so the blind spot is a knob instead of a constant.

Applied: `soak.py --fixture-bytes` (done in M197). To audit: the fixtures in
`tests/e2e/fixtures/`, `tests/bench/`, and the per-test temp workspaces.

---

## #5 Flag combinations, especially `--lite` end to end

### Evidence

`lowResource` is unit-tested at config resolution (`tests/test_config.c:296-332`)
but **no e2e test runs with `--lite`**, and it shifts ~10 defaults at once
(snapshots/repoMap/references/markdown off, `maxParallelAgents` 1, subagent depth
0, smaller `contextLimit`/iters/retries, smaller per-tool caps). The tri-states
compound it: `pathFence` (−1/0/1), `promptCache` (−1/0/1), `toolProfile`
(auto/core/full), and a `contextLimit` below 12000 silently switching the
toolset. Development sits at one corner of that cube.

### Design decision

**Make the existing suite parameterisable rather than writing a second suite.**
`_e2e.py`'s `_argv()` honours a new `JC_E2E_EXTRA` (whitespace-split, appended to
every invocation), and `tests/e2e/run.sh` grows a `--lite` mode that sets it. The
full suite then runs twice with one code path.

**Rejected:** a hand-written combinatorial matrix. The cross-product is large,
most cells are uninteresting, and the valuable signal is "does the whole suite
still pass with the resource-lean defaults" — which the existing tests already
express. A few tests legitimately assert on features `--lite` disables; those get
an explicit skip guard rather than a weakened assertion.

---

## #6 Concurrency on shared state

### Evidence

Two instances sharing `~/.continue/sessions` and `~/.jichi.d/` is a *documented*
deployment (`docs/AUTONOMOUS_LOOPS.md`, `examples/stress/`). But `jc_file_mtime`
is second-granularity and `qsort` is not stable, so **"most recent session" is
nondeterministic** when two saves land in the same second — affecting bare
`/resume`, `--continue`, and `jc_session_load_recent_scoped`.

### Design decision

**Make the ordering total.** `meta_cmp_desc` breaks an mtime tie by `id`
(ascending), so the sort is deterministic regardless of qsort's stability or the
input order. Cheap, pure, unit-testable, and it removes a class of "it picked the
wrong session" report that would otherwise be unreproducible.

Session *writes* are already atomic (`jc_write_file_atomic`, M146), so
save/save and save/read races are safe; the remaining exposure is ordering and
`/sessions clear` deleting a file another instance is about to rewrite — which is
benign (the rewrite recreates it) and is left documented rather than locked.

**Rejected:** file locking on the store. It would serialise unrelated instances
for a problem whose only symptom is tie ordering, and cross-platform advisory
locking is a much larger commitment than the defect warrants.

### Tests

A unit test that two metas with equal mtime sort deterministically by id, and an
e2e that runs two instances concurrently against one store and asserts both
survive and the listing is consistent.

---

## #7 A boundary table for every cap

### Evidence

The 64 MiB `JC_READ_FILE_MAX` boundary was probed in M197: a 63 MB session loads
(retaining 64,512 KB pre-fix), a 65 MB session retains **0 KB** — it is silently
**invisible** to both `/sessions` and `/resume`, with "no session matching" as the
only symptom. There are ~12 comparable caps (`readMaxBytes`, `runMaxBytes`,
`fetchMaxBytes`, `searchMaxBytes`, `gitMaxBytes`, `imageGenMaxBytes`,
`audioGenMaxBytes`, `transcribeMaxBytes`, `JC_MEMORY_MAX`, `JC_GLOSSARY_MAX`,
`repoMapLimit`, `snapshotLimit`), each with under/at/over cases.

### Design decision

Two separate questions per cap, and the second is the one that matters:

1. Is the boundary arithmetic right (off-by-one at exactly the cap)?
2. **Is the truncation visible to whoever needs to know?** M191 established that
   a truncation is a correctness boundary when the output feeds a strict parser;
   M197 adds that an invisible skip is a correctness boundary when the user is
   the parser.

Implement as a table-driven unit test over the pure caps (`jc_config_cap`,
memory, glossary) plus targeted e2e for the tool caps, and fix visibility where
it is missing — starting with the over-cap session, which the #4 skipped-count
reporting already covers.

---

## Other findings folded in

- **Bare `/resume` resumes the session you are already in.** `jc_tui.c:2658`
  saves the live session (setting its mtime to now) *before*
  `jc_session_load_recent_scoped` picks the newest — which is therefore always
  the current one, making the "(no earlier session…)" branch at `:2668`
  unreachable. Decision: exclude the live session id from the recent-scoped
  lookup, so bare `/resume` means "the previous one" as users expect.
- **`tests/e2e/_e2e.py` has no `HOME` isolation**, so a full `run.sh` deposits
  ~8 sessions in the developer's real store (`workspaceDirectory` of
  `/tmp/jichi_e2e_*`). Decision: add an opt-in `isolated_home()` helper and use
  it in the TUI-spawning drivers, rather than changing the default — some tests
  legitimately exercise global config discovery under the real `HOME`.
- **Duplicate session aliases**: unverified. The M197 probe used `export @same`,
  which does not appear to accept alias syntax at all, so it proved nothing about
  whether `jc_session_resolve_alias` reports `-2` (ambiguous) correctly. Needs
  checking through the TUI before any claim is made.
- **Filenames containing newlines** would corrupt the line-oriented output of
  `list_files` and `search_code`. Untested; belongs to #1's family.
- **Session-store rotation.** The M197 retention is fixed, but `/sessions` still
  reads and cJSON-parses every file, so a 480 MB store costs ~3 s per
  invocation. Decision: a `doctor` warning above a threshold, not automatic
  deletion — the same reasoning as the `.tmp` litter.

## Cross-cutting: two techniques worth institutionalising

**Footprint assertions as a gate class.** `test_session_footprint` (M197) asserts
a *shape* — cost independent of content size — not a byte count, which is why it
survives refactors. The template fits any repeatable operation: run it N times,
assert bounded growth. Worth generalising once a second instance appears.

**A lint for the arena class.** `app->arena` in a per-call code path is the smell
behind both M197 mechanisms and both residual ones (`jc_memory.c:102`,
`jc_tool_todo.c:151`). A `tests/e2e/` source lint flagging new `app->arena` uses
outside startup/registration would have caught all four at commit time. Design:
an allowlist of known-legitimate sites (config, rules, repo map, memory notes,
tool registration) plus a failure message pointing at `jc_app_scratch`. Deferred
until the residual sites are fixed, so the allowlist starts honest.

## Implementation order

Ranked by demonstrated value per unit of work, which is also the order below.
**#1 and #4 close verified defects**; the rest close coverage gaps.

| Phase | Item | Status |
|---|---|---|
| 1 | #1 degenerate state (FIFO hang, id mismatch, directory read) | **done** |
| 2 | #4 fault injection + visible degradation | **done** |
| 3 | #2 typed-input path | **done** |
| 4 | #6 total ordering + concurrency | **done** |
| 5 | #7 cap boundary table | **done** |
| 6 | #5 `--lite` end to end | **done** |
| 7 | #3 fixture-proportionality checklist | **done** |
| 8 | Other findings: bare `/resume`, `_e2e` HOME, alias ambiguity, doctor lints | **done** |

All eight landed. Verification for the set: 9073 unit checks (9075 with
`FAULT=1`) on gcc and clang under `WERROR=1`, `SAN=1` clean, valgrind 0 errors,
`cpp-check`, `fuzz` 18 targets, and the e2e suite green both default and `--lite`.

Each phase lands with its own tests and is verified against the full gate set
(`WERROR=1` on gcc and clang, `SAN=1`, valgrind, `cpp-check`, `fuzz`, e2e) before
the next begins.

## Outcomes, including where this document was wrong

Recorded after implementation, because three of the predictions above did not
survive contact.

- **#1 confirmed both defects and fixed them.** `tests/e2e/degenerate_store.py`
  covers ten hostile stores; reverting the `jc_is_regular_file` guard reproduces
  `fifo: HUNG (rc=124)`, so the gate has teeth. Seven of the ten cases were
  already handled correctly before the change — worth stating, since the value of
  the driver is now mostly as a regression fence.
- **#4's own test was wrong twice before it was right.** The first probe for "is
  this a FAULT build?" inferred it from injected behaviour, but `--version` does
  not allocate enough to fail; the fix was to make a FAULT build *announce itself*
  in `--version`, which an operator should see anyway. Then the write-failure case
  turned out to be untestable through the e2e surface at all: both callers of
  `jc_write_file_atomic` are reachable only after a model call, so driving it
  through `-p` against the dead-port fixture measured the **connect timeout** and
  reported a "write-fault HUNG" that was nothing of the kind. That contract now
  lives in the unit suite under `#ifdef JC_FAULT`, where it needs no network.
- **#2 was overstated in the first draft.** `paste.py` already covered *both*
  paste routes; the genuine gap was only the typed path, which `typed.py` now
  pins. The claim "the e2e tests may be exercising the paste branch while users
  hit the typed branch" was half right — they were, but deliberately.
- **#7's fixture had the very off-by-one the file exists to catch.** The first
  version of `write_session_of_size` produced a file exactly *at* the read cap
  instead of over it, silently converting the over-cap case into an at-cap case
  and making three assertions fail for the wrong reason.
- **Duplicate aliases: no defect.** Flagged unverified above, and now checked at
  the API where the contract lives: `jc_session_resolve_alias` correctly returns
  `-2` for two sessions sharing an alias. The earlier probe used
  `export @alias`, which does not accept alias syntax and therefore proved
  nothing. Pinned by a test so it stays true.
- **The `_e2e` HOME fix had to be wider than proposed.** This document argued for
  an opt-in helper used by the TUI-spawning drivers. That was insufficient:
  *headless* drivers persist sessions too, so 23 artifacts still appeared after
  isolating the five PTY drivers. The mechanism is now a **suite-wide** private
  `HOME` in `run.sh` (opt out with `JC_E2E_KEEP_HOME=1`), which also removes a
  determinism hazard nobody had noticed — every driver was previously reading the
  developer's real global config, glossary and memory, so a test could pass or
  fail depending on whose machine it ran on. Measured store delta after a full
  run: **0** (was ~23).
- **A pre-existing flake, reported not papered over.** `prose_nudge` failed once
  during a full-suite run under memory pressure and passed standalone against both
  a real and an isolated `HOME`. It is timing-sensitive, unrelated to these
  changes, and left alone rather than given a longer timeout on a guess.
  **M199 follow-up: still unexplained, and two hypotheses were disproved.** Its
  hand-rolled 2 s HTTP reader (ANECDOTES #18's naive pattern) was replaced with
  the shared robust one — a good change on its own merits, but the timeouts
  reproduced identically — and output piping turned out to be irrelevant too. The
  failures cluster right after heavy build activity; 6/6 pass on a quiet machine.
  The timeout stays as it is.

Deferred deliberately, with reasons, rather than half-done:

- ~~**The arena lint**~~ — **done in M199**, once the two residual re-strdup sites
  (`jc_memory.c:102`, `jc_tool_todo.c:151`) were fixed so its allowlist could
  start honest. `tests/e2e/arena_lint.py` now permits `app->arena` in
  `src/tools/`/`src/lsp/` only for a dynamic tool's registration record; the
  allowlist is keyed by (file, exact line) so a new use in an allowlisted file is
  still caught, and the scan is comment-aware because the fixed sites explain the
  old misuse in prose. Verified to have teeth.
- **Filenames containing newlines** corrupting line-oriented `list_files` /
  `search_code` output — real, in #1's family, untested, and a bigger change than
  a guard (it is an output-format question, not a read question).
- **Concurrent-instance e2e.** The ordering defect it would have caught is fixed
  and unit-tested (`test_bounds.c`); a two-process driver on a 4.9 GB host with
  swap already half used is more likely to produce flakes than findings.
