# The intra-turn bound: a per-tool-call arena (M199)

*2026-07-29. Third and last of the memory-lifetime series:
`2026-07-28-memory-soak.md` (M180, asked the question and missed it),
`2026-07-29-session-arena.md` (M197, reproduced and fixed it),
`../proposals/2026-07-robustness-edge-cases.md` (M198, generalised it). This one
closes the item M198 deferred, and records the audit that changed its design.*

## The gap M197 left

M197 moved per-tool-call file reads off `app->arena` (created once, freed only at
process exit) onto `app->scratch`, which is reset at each top-level turn. That
bounds retention to **one turn**. It is not enough, because a turn is not small:

- `maxToolIters` defaults to 25, but `run_agent_loop` forces it to **at least
  200** whenever an envelope or verify gate is active (`src/chat/jc_agent.c`).
- So a read-heavy `--auto` turn re-reading a 250 KB file 200 times peaks ~50 MB
  above baseline, *inside a single turn*, and per-turn scratch cannot help by
  construction — it is reset after the peak has already happened.

The M197 measurements could not see this: `soak.py`'s profiles do one tool call
per turn and report a per-turn **slope**. A slope of zero says nothing about the
peak. That is the same shape of blind spot as M180's 13-byte fixture, one level
up: the harness measured the right quantity for the wrong failure mode.

## The audit, which changed the design

M198's deferral said this "needs an audit that nothing else allocated during a
tool call outlives it". The audit's answer was **no** — several things do — so the
proposed fix (reset an arena after each tool call) would have been a
use-after-free. Recorded here because the classification is reusable reference:

| Allocated during a tool call | Lifetime | Where |
|---|---|---|
| a file's bytes while formatting/matching/uploading | **per call** | `read_file`, `edit_file`, `apply_patch`, `list_files`, LSP reads + refactors, imagegen source, transcribe upload, memory's transient reads, `spawn_parallel`'s change list |
| `spawn_subagent`'s seed task and tool fence | **must survive a nested agent run** | `src/tools/jc_tool_subagent.c:181,191` (on scratch, deliberately) |
| the memory notes (`app->memory`) | session | `src/chat/jc_memory.c` |
| todo item content | session (until the next revision) | `src/tools/jc_tool_todo.c` |
| read-before-edit paths (`app->read_files`) | session | `src/chat/jc_app.c:121` |
| background process registry | session | `jc_bg` |
| the envelope's `green_commit` / `baseline_commit` | run | `src/chat/jc_agent.c` |
| a dynamic tool's registration record | session | `jc_tool_mcp.c`, `jc_tool_imagegen.c` |

Two further facts the audit had to establish before anything could be reset:

- **Tool results are `malloc`-owned** (`tu_ok_owned`, `tu_ok_copy` →
  `jc_strdup`), not arena-allocated. So no arena reset around a tool call can
  corrupt the result that is about to be appended to history.
- **`app->read_files` stores paths, not content**, so it is small and correctly
  session-lived; it does not need moving.

Conclusion: not a reset of an existing arena, but a **third** arena that only
per-call transients use.

## The fix

`app->tool_scratch` + `jc_app_tool_scratch(app)`, reset immediately before every
`jc_tool_execute` at **every** agent depth, and also at the top-level turn
boundary (callers outside the tool loop use it too — `jc_memory_load` from the
TUI's `/memory` would otherwise accumulate until the next tool call, or forever
in a session that never runs one). It falls back to scratch, then to
`app->arena`, so subcommands and tests never receive NULL.

Fourteen sites moved, including two that the original list missed and that matter
most per call: the **transcribe upload (up to 25 MB)** and the **imagegen source
image (up to 5 MB)**.

### The invariant, and the one way to break it

> No tool may hold tool-scratch data across a **nested agent run**, because that
> run's own tool calls reset the arena.

Today's users all consume their bytes inside a single call and none of them spawn.
`spawn_parallel` is safe for a different reason worth stating: its children are
`fork`ed processes with copy-on-write memory, so they cannot touch the parent's
arena at all. A future tool that both reads onto this arena *and* starts an
in-process nested run must use `jc_app_scratch` instead. This is stated in
`include/jc_app.h` where a new caller will actually read it, and enforced only by
review — a lint cannot see it.

## Measurements

New `soak.py --profile reads` (many `read_file` calls in **one** turn, count via
`SOAK_READS_PER_TURN`), because the existing profiles measure the per-turn slope
and are structurally blind to the peak. 40 reads of a 1 MB file per turn, 10
turns, `maxToolIters: 200`:

| | peak RSS | first | last | slope |
|---|---|---|---|---|
| per-turn scratch only (pre-M199) | **55,140 KB** | 55,140 | 50,320 | −536 KB/turn |
| per-call arena (M199) | **15,424 KB** | 15,424 | 15,424 | 0.0 KB/turn |

The 39,716 KB difference is almost exactly the 40 × 1 MB of intra-turn reads now
reclaimed. Note the *slope* was ≈0 in both cases — M197 had already fixed the
cross-turn part — so **slope alone would have reported "no problem"**. Only the
peak shows it. The pre-M199 row was produced by disabling the per-call reset and
nothing else, which is the same discipline as verifying a gate can fail, applied
to a measurement.

The negative slope in the unfixed row is glibc returning memory between turns;
ignore it. Peak is the number that OOMs a constrained machine.

## Residual sites, and why they had to go first

Both are the same ownership smell, and both blocked an honest lint:

- `jc_memory_load` strdup'd the whole notes file onto `app->arena` and is called
  on **every** `remember` and every correction — N updates retained N copies
  (bounded by `JC_MEMORY_MAX`, but for no reason). Now malloc-owned via a new
  `jc_memory_refresh(app)` that frees the previous copy, so exactly one is live.
  Centralised deliberately: three call sites assigned this, and a fourth would
  have forgotten to free.
- `todowrite` replaced the whole list per revision, strdup'ing each item's
  content onto `app->arena` — a model revising a 10-item list 200 times retained
  2000 strings. Content is malloc-owned and freed on the replace and in
  `jc_todo_free`.

The ownership change surfaced **four test-side leaks** under ASan (the tests call
`jc_memory_load` directly and now own the result) — worth noting as the expected
cost of moving from arena to malloc: the compiler cannot find those for you, the
sanitizer can.

## The lint

`tests/smoke/arena_lint.sh` (ported from `tests/e2e/arena_lint.py` at M217,
widened at M218): inside `src/tools/` and `src/lsp/`, `app->arena` may
only allocate a dynamic tool's registration record. Three design details that
make it survive contact:

- **Comment-aware.** The fixed sites deliberately explain the old misuse in prose;
  a lint that flagged its own documentation would be deleted within a week.
- **Allowlist keyed by (file, exact source line)**, not by file. A file-level
  allowlist rots immediately — a new bad use in an already-listed file would pass.
- **Actionable failure message** naming both alternatives and pointing at
  `include/jc_app.h`, because the whole point is that the correct arena is not
  obvious.

Verified to have teeth: reintroducing one `app->arena` in `jc_tool_read.c`
produces `src/tools/jc_tool_read.c:85`. It would have caught all four sites
(M197's two mechanisms and M199's two residuals) at commit time.

## An honest non-result: the `prose_nudge` flake

Flagged in M198 as a pre-existing flake. Still unexplained, and I was **wrong
twice** about it — recorded because the pattern is the transferable part.

1. **Hypothesis: its hand-rolled HTTP reader truncated bodies.** `prose_nudge.py`
   had a local `recv_request` with a 2 s socket timeout that broke out of the body
   loop on the first timeout — precisely the naive pattern ANECDOTES #18
   documents, and `_e2e.recv_http_request` exists to replace. The story was
   plausible and mechanically specific: a truncated read means no valid response,
   so jichi waits and the outer timeout fires. **Disproved:** the timeouts
   reproduced identically with the shared reader (0/5 back-to-back). I had
   already written "5/5 back-to-back" into a code comment *before* measuring it,
   and had to correct it. The swap was kept anyway — the naive pattern is
   known-bad on its own merits — with a comment saying explicitly that it did not
   fix the timeout.
2. **Hypothesis: piping the driver's output mattered** (every failing run had gone
   through `| tail | cut`). **Disproved:** 3/3 pass with the pipe, 3/3 without.

3. **Hypothesis: CPU load.** **Disproved:** 6/6 pass under four `yes` loops on a
   3-core host.
4. **Hypothesis: memory pressure**, which is what the original correlation
   suggested (the failures clustered right after build cycles on a host with swap
   half used). **Disproved:** 3/3 pass with available memory driven from 1204 MB
   down to 451 MB.

What is actually true: it failed twice — once inside a full `run.sh`, once as five
consecutive runs immediately after heavy build activity — and **could not be
reproduced deliberately by any means tried**. Its 60 s inner timeout was
deliberately **not** raised: that hides the signal without understanding it, and
this file would then have recorded a fix that wasn't one.

### The leading remaining hypothesis, for whoever picks this up

`prose_nudge.py`'s mock server is **single-threaded**: it does
`accept()` → `recv_request()` → `send_sse()` → `close()` inline in one loop.
`soak.py`'s mock, by contrast, spawns a thread per connection. So in
`prose_nudge` any connection that stalls blocks the *whole* server for the
reader's full 15 s deadline, while jichi's real request waits unanswered in the
listen backlog; with `maxRetries: 0` there is no second chance. That is a real
serialization hazard, present in this file and absent from its sibling.

**A datapoint that reframes this (M200).** A *second*, unrelated driver --
`constraints_scope` -- failed once inside a full `run.sh` during this milestone and
then passed 4/4 standalone, and the suite passed on re-run. So the pattern is not
"this driver is fragile" but **"something about running ~55 drivers in sequence
occasionally breaks one of them"**. That makes the single-threaded-server theory
below *less* likely, since the two drivers do not share that structure, and makes a
suite-level resource effect more likely. Sockets churn but do not exhaust
(TIME-WAIT went 20 → 52 across a full run, three orders of magnitude below any
limit). Whatever it is, it is intermittent, it is not a regression from any change
in M197--M200, and it should be chased at the suite level -- e.g. by having
`run.sh` record per-driver wall time and retry-on-failure *reporting* (not
retry-to-green), so the next occurrence arrives with data attached.

The single-threaded-server observation was **not** applied as a fix, on purpose. Nothing demonstrates it is the cause,
threading the handler adds a race on the shared `state["n"]` request counter that
decides which canned reply is sent, and shipping an unverified change here would
be the same error as raising the timeout — dressed up as engineering. The next
attempt should instead **capture where jichi is stuck**: run the driver in a loop
with the inner timeout raised only in a scratch copy, and when it hangs, read
`/proc/<pid>/wchan` and `ss -tnp` for the mock's port to see whether the request
was ever accepted. That distinguishes "jichi never sent it", "the server never
accepted it", and "the server accepted and never replied" — which is the fork in
the road that all four disproved hypotheses were guessing at.

A false alarm from the same session, worth the same honesty: five consecutive
timeouts mid-implementation looked like a regression I had introduced, so I
bisected against committed `HEAD` before continuing. It wasn't mine. Two minutes
well spent, and the reason to keep a known-good reference point.

## A new finding, and a correction to my own claim (M200)

M197 concluded that a large session store costs only **latency** now that the
retention is fixed (~3 s of parsing per listing). **That was wrong**, and the
harness that produced it was wrong in a third way.

`ls --all` on this developer's real store (243 files, 17 MB) peaks at **193 MB
RSS**, against 8.5 MB for `--version`. Yet a *synthetic* 243-file / 17 MB store
peaks at 9.3 MB. Four hypotheses, three disproved:

| tested | result |
|---|---|
| scales with store bytes? | **no** — 1.75 → 17.5 MB stores all peak ~9.3 MB |
| scales with file-size *variance*? | **no** — `--vary 1.0`, a 36 MB spread store, peaks 10.2 MB |
| driven by the single largest file? | **partly** — one real 2.25 MB / 78-message session alone peaks 27.9 MB, nowhere near 193 |
| driven by cJSON **node count**? | **yes** |

Controlled experiment, identical store bytes (17,010 KB) and file count (243),
varying only messages per file:

| messages/file | store | peak RSS |
|---|---|---|
| 2 (the M197 fixture) | 17,010 KB | **9,284 KB** |
| 200 | 17,010 KB | **16,896 KB** |
| 2000 | 17,010 KB | **237,824 KB** |

A 26× spread from structure alone. `jc_session_list` builds a full cJSON tree per
file to read four scalars and an array length; a tree is ~64 bytes of node per
JSON value plus a copy of every string, so cost tracks *values*, not bytes. The
trees are correctly deleted, but glibc does not return that memory to the OS
within the run, so the peak is cumulative.

**Why the harness missed it: the fixture had realistic bytes and unrealistic
structure.** Two messages and one big padding string per file — ~100× too few
nodes. This is the third distinct way a fixture has under-reported a real cost in
this series (M180: too small; M199: measured slope not peak; here: right size,
wrong shape). `mkstore.py` now takes `--messages` so the dimension is a knob
rather than an assumption, and `--vary` for size spread.

**This matters for the documented audience.** `docs/LOW_MEMORY.md`'s tiers
bottom out at ≤64 MB total RAM (a 32 MB floor was claimed at the time). A single `/sessions` on an ordinary real store would exceed
that many times over. It is transient, not retained — but a transient that large
is an OOM on the machines that guide is written for.

**Fixed in M202.** `jc_sessmeta_scan` (`src/session/jc_sessmeta.c`,
`include/jc_sessmeta.h`) reads the listing fields in one forward pass with **no
allocation and no tree**: it tracks string/escape state and brace depth, captures
the four top-level scalars, and counts the objects directly inside the `history`
array. Tracking string state is what makes the count trustworthy — a message whose
*content* contains the text `"role":` must not inflate it, and a substring count
would. It is deliberately **not** a JSON parser: it answers exactly the listing's
questions and **returns 0 when it cannot be sure**, so anything unusual (a foreign
writer, an unexpected shape, an illegal escape, a truncated file) still gets a real
`cJSON_Parse` rather than a half-read row.

| | peak RSS | wall |
|---|---|---|
| real store (243 files / 17 MB), before | 193,344 KB | 0.14–0.48 s |
| real store, after | **13,596 KB** | 0.11–0.17 s |
| synthetic 243 × 2000 messages, before | 237,824 KB | — |
| synthetic 243 × 2000 messages, after | **9,216 KB** | 0.12 s |

A 14× reduction on the real store and 26× on the pathological one, and the peak no
longer tracks message count at all. Correctness was checked the only way that
really counts for a parser replacement: `ls --all` output is **byte-identical**
across all 243 real sessions, before and after. The fallback was verified to engage
(a well-formed session with no `history` key still lists, via cJSON) and a
genuinely corrupt file is still counted and reported rather than crashing. 46 unit
checks in `tests/test_sessmeta.c` cover the shape jc_session_save writes,
pretty-printed input, escapes including `\uXXXX`, `"role":` inside content, braces
inside content, nested `toolCalls`, over-long values (truncated but still
trustworthy), and the whole failure contract.

**The superseded recommendation, for the record.** `jc_session_list` does not need a
parse tree: it wants `sessionId`, `title`, `alias`, `workspaceDirectory` — which
`jc_session_save` writes *before* `history` — and a message count that is
display-only. A targeted scan for those four fields plus a count of top-level
`"role":` occurrences, falling back to the current full parse when the scan does
not find them (foreign or older files), would cut both the peak and the latency.
It is deliberately left undone: hand-rolling JSON field extraction is exactly
where subtle bugs live, it needs its own fixtures and fuzzing, and it deserves a
milestone rather than being tacked onto this one. `doctor`'s existing store
warning keys off bytes, which remains a usable proxy — but the honest driver is
node count, and that is now recorded.

## Auditing the test rig itself (M201)

Asked to check whether the Python rig was flawed too. It was, in one class, and
worse than expected.

**Sixteen of 72 drivers each carried a private copy of a request reader whose
Content-Length body loop broke on the first socket timeout**, silently returning a
**truncated** request. `_e2e.recv_http_request` exists precisely to replace that
pattern (ANECDOTES #18) and only twelve drivers used it.

The failure mode is what makes this bad. A truncated body does not look like a
timeout. The mock evaluates `marker in req` against a partial request, picks the
wrong canned reply, and the driver reports a **product regression that never
happened**. Both suite failures observed before this audit had exactly that
signature:

- `prose_nudge`: `FAIL: the prose tool call was not nudged ... NO_NUDGE` — its
  mock tests `b"did not invoke it" in req`.
- `constraints_scope`: `FAIL: the authored constraint was loaded but not enforced`.

That retroactively explains both, and explains why M199's swap of *one* driver's
reader did not stop the timeouts I was reproducing: those were a different,
load-induced symptom. Twelve were found by grepping for a named reader function;
the other **four were found only by the lint**, because their loops were inline in
the connection handler rather than in a function with a matching name.

All sixteen now delegate. `tests/e2e/rig_lint.py` enforces four properties of the
rig: no private request readers, no Content-Length loop that can break early (in
any spelling), `SO_REUSEADDR` on every AF_INET listener, and — a check worth
having on principle — every driver must be *able* to fail, since one that only
calls `ok()` is decoration. All 72 pass.

**A third in-suite-only flake, and what was ruled out.** `pathfence` then failed
once inside a full run and passed standalone, joining `prose_nudge` and
`constraints_scope`. One suspect was my own M198 change (a suite-wide shared
`HOME`, which could have let `~/.jichi.d/calibration.json` leak token-estimate
state between drivers and shift context decisions). **Disproved:** the shared HOME
stays empty, because every driver sets its own internally. Not the cause.

Rather than guess a fifth time, `run.sh` now **classifies its own failures**: on a
failure it prints the captured output, re-runs that driver once standalone, and
labels the result *in-suite-only* or *also-alone*. This is deliberately not
retry-to-green — the suite still fails either way. It means the next occurrence
arrives with the two things every previous one lacked: the failing output (which
distinguishes a truncation-style wrong answer from a timeout) and whether the
driver is broken in isolation. Smoke-tested against a deliberately failing driver.

## Recommendations

Ranked, with the reasoning rather than just the verdict.

1. **Treat "which arena?" as a design question, not a detail.** Three lifetimes
   now exist and the shortest that outlives the data is always right. The lint
   covers the tool/LSP layer; `src/main.c` and `src/tui/jc_tui.c` still hold 38
   and 26 `app->arena` uses respectively, unaudited. Most are genuinely
   session-lived (startup wiring), but that is an assumption, not a measurement —
   extending the lint there, with an allowlist built by reading each one, is the
   obvious next increment.
2. **Measure peaks, not only slopes.** A zero slope reported "no problem" for a
   50 MB intra-turn peak. Any harness that samples once per unit of work is blind
   to what happens inside that unit; say in its docstring which of the two it
   measures.
3. **Prefer `malloc` + free over an arena for anything replaced wholesale.**
   `app->memory` and the todo list were both "replace the whole value" data on an
   append-only allocator, which cannot express that. An arena is the wrong tool
   whenever the word "replace" appears.
4. **Keep the fault injector in the toolbox.** `FAULT=1` exists now and is inert
   by default; the alloc/read/write sites are the three that matter most, but
   `JICHI_FAULT_*` could grow a network site cheaply, which would let the retry
   ladder be tested without a flaky server.
5. **Do not fix a flake you cannot explain.** Two plausible causes for
   `prose_nudge` were disproved by measurement. Raising the timeout would have
   converted an honest unknown into a false "fixed" and lost the only signal.
6. **Still open, deliberately:** filenames containing newlines corrupting the
   line-oriented output of `list_files`/`search_code` (an output-format question,
   not a read guard); session-store rotation (retention is fixed but a 480 MB
   store still costs ~3 s of parsing per listing); and a concurrent-instance e2e
   (the ordering defect it would catch is fixed and unit-tested, and two processes
   on this host would produce flakes rather than findings).

## Reproducing

```sh
make
export JC_SOAK_BIN=$PWD/jichi

# the intra-turn peak (the M199 measurement)
SOAK_READS_PER_TURN=40 python3 tests/measure/soak.py \
    --turns 10 --profile reads --fixture-bytes 1048576

# the pre-M199 comparison: disable ONLY the per-call reset in
# run_agent_loop (src/chat/jc_agent.c), rebuild, re-run the above

# the lint, and proof it bites
sh tests/smoke/arena_lint.sh   # tests/e2e/arena_lint.py before the M217 port
#   then put one `app->arena` back in src/tools/jc_tool_read.c and re-run

# M197 must stay flat
python3 tests/measure/session_scan.py --variant sessions --calls 8 \
    --files 250 --bytes 71680
```
