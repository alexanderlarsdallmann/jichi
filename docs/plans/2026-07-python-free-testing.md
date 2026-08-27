# M209+ — testing without Python: tiers, the smoke suite, and the port plan

**Status: COMPLETE (M209 and B1–B6, M210–M217).** Every portable e2e
driver is ported — HTTP socket, AF_UNIX, stdio JSON-RPC, the PTY line
editor, the spawn_parallel fork pool, the autonomous-loop supervisor, the
session-arena footprint, and the learner-flow assignment ladder. The
smoke tier is **71 drivers / 312 checks**, all Python-free. `make e2e`
now **skips loudly** (exit 0, `elisp-compile` pattern) when python3 is
absent, and python3 is **optional-recommended** — `make check-target`
(= `test` + `smoke`) is a full build gate on any POSIX box.

What deliberately stays Python under `make e2e` (the residual, D14):
`redraw` (its DEC-deferred-wrap VT emulator is real software with its own
correctness burden), `stress` + `web_bridge` (they gate Python *products*
— testing them without Python tests a fiction), `curriculum_graders`
(needs a C compiler at test time), the model-gated live checks
(`headless_model`, `acp_terminal`), and `rig_lint` (it lints the Python
rig, and shrinks with it). `tests/bench/` + `tests/measure/` remain
measurements, never gates.

**D4 was corrected in M216:** the parallel drivers were assumed to need
concurrent accept; verified 9/9 that they do not, so mockmodel stays
single-threaded sequential (ANECDOTES #28).

This document records the design decisions and their alternatives so the
milestones' reasoning outlives them.

**B1 outcome (M210):** 18 Python drivers retired (the offline subprocess
set + `arena_lint`/`docs_flags` as awk/grep ports + the three M209 twins
extended to fully supersede their originals); e2e shrank 72 → 54 drivers;
the R3 one-driver-one-tier rule is now a `smoke_lint.sh` check (it caught
its first duplicate — `toolcalling_none` — the day it landed). Deferred
from B1 with reasons: `mcp.py` (its MOCK is imported by the
`mcp_prompt`/`mcp_ref` socket drivers; moves with them) and
`curriculum_graders.py` (needs cc at test time; its own wave).

## Problem

`make` and `make test` are Python-free, but the entire e2e layer (73 drivers
+ `_e2e.py`, ~11k lines, stdlib-only Python ≥3.7) funnels through
`tests/e2e/run.sh`, which hard-fails without `python3` — so `make ci`
requires Python with no fallback, and a box without Python (old systems; the
≤64 MB uClibc tiers docs/LOW_MEMORY.md promises) can build jichi but never
validate the build beyond the unit suite. LOW_MEMORY's own enforcement
citations (`arena_lint.py`, `soak.py`) could never run on the targets they
describe — a live docs tension until M209.

## Shape of the answer (tiered)

- **M209 (done):** a POSIX-sh **smoke tier** (`tests/smoke/`, `make smoke`)
  that validates a build on any POSIX box with no python3 anywhere, backed by
  three **test-only C89 helpers** (`tests/tools/`): `mockmodel` (scripted
  HTTP+SSE mock model server with request capture), `ptydrive` (expect-lite
  PTY driver), `jsonq` (dot-path JSON assertions linking `src/json/cJSON.c`).
  `make check-target` = `test` + `smoke` is the on-target validation answer.
  `make ci` runs smoke so the tier cannot rot; e2e keeps its python3
  requirement (its error now points at `make smoke`).
- **B1 (M210, planned):** port the ~10 subprocess-only offline e2e drivers +
  the portable lints (`arena_lint` → awk over the same regexes; `docs_flags`
  → `describe --output json` via jsonq diffed against the docs). Add a
  cross-check that every check lives in exactly one tier (R3).
- **B2 (M211+, planned):** the 45 socket drivers, in mockmodel-feature waves:
  plain text/marker → `count` state machines → **`wire anthropic`** →
  complex outgoing-body asserts (`jsonq --http-body` over captured `req.N`)
  → `sse-file` for exotica. Each wave extends `mm_core` tests-first.
- **B3 (planned):** control/AF_UNIX drivers (`control.py`, `daemon.py`,
  `acp_cancel.py`) — needs one small AF_UNIX line client (`sockq`, a fourth
  helper; a server and a client in one binary is a smell).
- **B4 (planned):** PTY drivers via ptydrive (`tab`, `ghost`, `paste`,
  `editor`; `typed.py`'s inter-keystroke timing via `delay`).
- **End state:** `make ci` = unit matrix + smoke (then containing the ported
  e2e) + a residual Python target that degrades loudly like `elisp-compile`;
  python3 becomes optional-recommended for the residual drivers only.

## Design decisions (decision — alternatives — rationale)

**D1 — Tiered, not big-bang.** Rejected: a full port now (~11k lines, one
giant landing); graceful-degradation only (old systems silently test less);
lowering the Python floor to 2.7/3.4 (mechanical — the corpus has zero
f-strings — but only helps boxes that have *some* Python, and buys permanent
straddle-maintenance). Each milestone lands green and independently useful.

**D2 — POSIX sh + C helpers, not a C e2e harness, not "smarter sh".** C89
process orchestration is 5–10× the verbosity of sh; the 17 TAP-emitting
assignment graders prove sh + grep/awk carries the assertion layer. Pure sh
can't do the three hard things (scriptable HTTP/SSE mock, PTY control with
timing, structural JSON asserts); `nc` is non-portable across
implementations, `expect`/`socat` would be new deps. sh owns orchestration +
assertions; C89 owns the three capabilities sh lacks — the product's own
pure-core/IO-shell split, applied to the rig.

**D3 — Three helpers, not one Swiss-army binary.** Disjoint dependency
footprints (cJSON+`-lm` / pty APIs / sockets), disjoint failure modes, each
readable in one sitting. Test-only: built by `make smoke-tools`, never
installed.

**D4 — mockmodel is single-threaded, sequential accept.** jichi issues one
model call at a time and every non-stall mock reply is `Connection: close` +
`Content-Length` (verified in the Python mocks), so threads would add
complexity with zero coverage gain. Concurrent accepts are deferred until a
ported parallel-agent driver actually needs them; the reply-table format is
unaffected either way.

**D5 — Request completion is owned by an incremental parser; capture is to
files.** The rig's most expensive lesson (ANECDOTES #18 / M201: twelve
drivers silently truncating requests on the first socket timeout, failing
body asserts as "wrong answers") is made structural: mockmodel's recv loop
can only feed bytes into `mm_http_feed`; only the parser, which knows
Content-Length, declares COMPLETE. Capture-to-`req.N` replaces in-process
`marker in req` asserts and leaves the evidence on disk for the M201
classification retry.

**D6 — Declarative reply table, not a programmable mock.** Nearly all 45
Python mocks reduce to: match (request count and/or body substring) → canned
action (text/tool/status/stall). A line-oriented table covers that, parses in
a pure unit-tested core, and keeps mock behavior reviewable beside the
driver. `sse-file` is the verbatim escape hatch, so the format grows
demand-driven (tests-first), never speculatively. `wire anthropic` is
parsed-but-rejected: named headroom, not speculative code.

**D7 — ptydrive matches plain substrings over the accumulated transcript; no
regex, no VT emulation.** POSIX `regcomp` varies across the old systems this
tier targets and the PTY drivers overwhelmingly wait for literal markers.
Whole-transcript matching makes split-across-read patterns trivially correct
(unit-pinned). VT interpretation stays out: `redraw.py`'s DEC-deferred-wrap
emulator is real software with its own correctness burden — it stays Python
(D14).

**D8 — jsonq links the in-tree cJSON.** Rejected: a second hand-rolled JSON
scanner (guaranteed drift) and grep/sed over JSON text (wrong for structure).
"Harness identical to production", applied to JSON: the tier asserts through
the parser jichi ships. Policy: grep for markers, jsonq for structure.

**D9 — TAP as the driver protocol.** Precedent (assignment graders emit it
with grep/awk) and consumer (`jc_testparse` parses it) already in-tree. TAP
gives a mechanical denominator at both levels: `t_done` fails on
emitted≠plan, and `run.sh` sums plans vs oks suite-wide.

**D10 — The runner clones the e2e runner's idioms.** M198 suite-wide isolated
HOME + trap; M201 failure-classification retry (never retry-to-green);
per-driver logs and limits; `JC_SMOKE_*` env mirroring `JC_E2E_*`. Each idiom
was paid for by a documented incident; the tier inherits them on day one.

**D11 — Old-system tolerances, explicit.** No fractional `sleep` (not
POSIX). No hard dependency on `timeout(1)`: helpers self-watchdog via
`alarm()` (`--deadline`); `_smoke.sh`'s `with_deadline` falls back to a
pure-sh watchdog. Strict POSIX sh (no `local`, no `[[`), enforced by
`smoke_lint.sh` rather than convention. `mktemp -d` remains an accepted
baseline (already load-bearing in the e2e runner).

**D12 — `tests/smoke/` naming; helpers in `tests/tools/`.** The tier
validates a *build*; when B-waves port coverage here the name stays honest.
Helpers live outside the suite dir because later tiers (and possibly
`tests/measure/`) will share them.

**D13 — ci gains smoke; e2e keeps its hard python3 requirement.** Leaving
smoke out of ci lets it rot (the exact TEST_INTEGRITY failure: gates nobody
runs go green-stale); degrading e2e now would silently weaken the strongest
gate while its coverage is still Python-only. Graceful e2e degradation is
deferred to the end state, when the sh tier actually contains the ported
coverage (R7).

**D14 — Never ported (named, per "state what a green means").**
`web_bridge.py` + `stress.py` gate Python *products* — testing them without
Python tests a fiction. `redraw.py` keeps its VT emulator (D7).
`rig_lint.py` lints Python and shrinks with its corpus. `tests/bench/` +
`tests/measure/` are measurements, never gates. Consequence, stated honestly:
python3 ends optional-recommended, not eliminated.

**D15 — Instruments get teeth.** The helpers are instruments, and this
project's history is instruments lying (the bench grader that scored five
correct edits as failures). Pure cores are unit-tested in `run_tests`; the
HTTP-feed test was observed RED (18 failures) against a naive break-at-head
parser before landing; every sh driver was observed failing under one
deliberate break (12/12, recorded in the ROADMAP M209 entry); `smoke_lint.sh`
is the tier's `rig_lint.py`.

**D16 — Milestone-A driver selection = the contracts an old/small box
depends on.** Build sanity (flags/doctor/init), the headless round trip
(basic/tool/toolcalling:none), the machine-readable surface (json/jsonl —
what supervisors parse), the bounded-failure contracts (stall timeout;
SIGTERM 143 / SIGINT 130 — what supervisors trust), persistence
(sessions/export), one PTY sanity check. Dev-machine lints are deliberately
excluded: they gate development, not a deployment (→ B1).

## Recommendations

- **R1 — Which tier where.** Dev/CI: `make ci` (everything). Any POSIX box,
  including Python-less and ≤64 MB targets: `make check-target`. The small
  low-resource reference box should run both `check-target` and the Python
  e2e — it is where the in-suite-only flake reproduces, and smoke gives it a
  second, cheaper signal to cross-reference.
- **R2 — Port order follows mockmodel features,** not driver alphabetical
  order; every wave shippable, `mm_core` extended tests-first.
- **R3 — One driver, one tier.** Delete the Python original in the same
  commit as its port; add a cross-check (B1) that no check exists twice —
  duplicated coverage drifts, and drift makes one copy a lie.
- **R4 — Resist helper feature creep.** mockmodel is not a general HTTP
  mock, ptydrive is not expect, jsonq is not jq. New helper capability
  requires a new unit test and a lint/teeth entry in the same commit.
- **R5 — Timing bounds encode the contract, not the dev box.** Wall-clock
  drivers (`stall`, later `typed`) get generous *upper* bounds; a smoke tier
  that flakes on the very machines it exists for is worse than none.
- **R6 — Smoke is not a substitute gate** until the B-waves land: smoke
  validates a build, e2e validates the product (so says CONTRIBUTING's tier
  table).
- **R7 — Revisit e2e graceful degradation only at the end state**, then flip
  it to the `elisp-compile` pattern (loud skip) in one commit.

## What M209's first live run taught (kept for the B-waves)

Two harness subtleties surfaced immediately, both now documented in
`tui_basic.sh`: an `expect` string must not appear in the startup banner
("Type /exit to quit" made `expect "/exit"` fire instantly), and
back-to-back `send`s land inside the M156 burst-paste window and merge into
ONE logical line — PTY scripts need human-scale `delay`s between sends,
which is exactly the timing surface `typed.py` exists to pin. Also: any sh
driver running jichi headless MUST close stdin (`< /dev/null`) — an open
non-TTY stdin is read as prompt context and blocks the run forever.

And the first full `make ci` caught a real 2-byte leak in `mm_core`'s
parse-error paths (an error exit freed the committed rules but not the
in-progress rule's strdup'd fields; fixed by routing every exit through
`mm_parse_fail`). Instruments get the same gates as the product — and the
gates bit on day one.
