# A wedged run, and what the log said next — telemetry pass, 2026-07-28

**Trigger:** the long-running jichi instance driving zigodot began returning HTTP 500
on every request and never recovered — including on fresh turns:

```
[jichi error] provider returned HTTP 500: {"error":{"message":
  "Router.acompletion() missing 1 required positional argument: 'messages'", ...}}
```

**Source:** `~/.jichi.d/telemetry/zigodot-full.jsonl` — 30 MB, 10 621 events,
143 turns, 4 471 successful model calls, 273.8 M input tokens, `--log-level full`
(so `args_full` and bounded tool output are on record). The workload is the zigodot
program; it was not advanced here.

One defect found and fixed (M191, the wedge itself). One gap fixed and the pass's
central blind spot instrumented (M192). Two findings confirmed already-fixed. One
metric declared un-judgeable from this log's vintage rather than reported as a defect
— and one finding of my own **corrected** after measuring its ceiling (see #2).

## Summary of findings

| # | Finding | Status |
|---|---|---|
| 1 | A 400-byte elision cut inside an em-dash made every later request unparseable | **fixed (M191)** |
| 2 | Superseded-read elision is blind to the same file under two path spellings | **fixed (M192)** — smaller than first reported, see the correction |
| 3 | 99.63 % of tokens are input, 82 % of reads are repeats — and nothing attributed them | **instrumented (M192)** |
| 4 | Budget exhaustion is the *majority* autonomous outcome (55 of 129 runs) | operator + #2/#3 |
| 5 | `format_file` failed 3/3: "the server returned malformed formatting edits" | small, open |
| 6 | `todo_write` failed 28 of 36 calls on argument shape | candidate for M148 coercion |
| 7 | Hallucinated tool names (`grep`, `glob`, `todoedit`) | **already fixed (M91)** — verified |
| 8 | `run_tests` / `run_terminal_command` ok-rates | **not judgeable from this log** |

## 1. The wedge (fixed, M191)

Full write-up: docs/ANECDOTES.md #22. In brief, the two fields that turned a
suspected gateway outage into a one-line local defect:

- `"latency_ms": 47.6` (then 51, 42, 41, 62) on a ~400 KB body — rejected *before*
  being read.
- the call immediately before it succeeded at `"in_tok": 100154` — so the failing
  body was **smaller** than the one that had just worked. Not size, not overflow.

And the event between them: `{"event":"compact","phase":"midturn","elided":6}`.
`elide_tool_msg` kept 400 head bytes / 200 tail bytes of a tool result; byte 400
fell inside an em-dash in a zigodot design document, keeping only `\xe2`. The
request body was then ill-formed UTF-8, which the gateway's `body.decode()`
rejected — and because the byte now lived in the history, so was every later turn.

Reading the *foreign* error literally is what located it: `Router.acompletion(self,
model, messages, ...)` named exactly one missing argument, so `model` was present
and `messages` was not — a combination no jichi code path produces, since
`oa_build_request` adds them unconditionally and adjacently. LiteLLM's
`_read_request_body` swallows every exception, returns `{}`, and the handler then
fills `model` in from its own configuration. "Only `messages` missing" is the
signature of *a body that failed to parse*, wearing a server-side default as a
disguise.

13 of that session's 101 tool outputs contained multi-byte characters — em-dashes,
arrows, `§`, `✅`, `’` — all from the project's own markdown. The only open question
was which cut would land badly first.

## 2. Superseded-read elision cannot see the same file twice (fixed in M192)

> **Corrected 2026-07-28.** This finding was first written as "~200 reads of the two
> hottest files are invisible to the mechanism", which overstated it by an order of
> magnitude. M94 dedups *within* each spelling, so each spelling retains exactly one
> full copy — the waste is one redundant resident copy per alternate spelling, not one
> per read. Measured properly: **338 distinct path strings normalise to 334 distinct
> files; 4 files are spelled two ways; ~94 KB of redundant full copies held
> resident.** Still worth fixing and nearly free, but a small correctness item rather
> than the milestone thesis it was billed as. It became part of M192, whose actual
> thesis is the instrumentation below. The lesson: quantify the remedy's *ceiling*
> before ranking it, not just the symptom's size.

M93 elides a `read_file` result when the same path is read again later. Its match was
a raw string compare (`jc_compact.c`, `jc_compact_trim_superseded_reads`):

```c
if (paths[j] != NULL && strcmp(paths[j], paths[i]) == 0) {
```

The log shows the model reading the same files under **both** spellings:

| path as passed | reads |
|---|---|
| `/home/…/zigodot/src/gdscript/vm.zig` | 232 |
| `src/gdscript/vm.zig` | 91 |
| `/home/…/zigodot/src/gdscript/codegen.zig` | 229 |
| `src/gdscript/codegen.zig` | 107 |

An absolute read never supersedes a relative one, so the two groups dedup separately
and each keeps a full copy. These are the largest files in the workload (M168 recorded
single `read_file` results of 169–172 KB), so two copies instead of one is ~94 KB —
about 24 k tokens resident, re-billed on every later call in the turn on a cacheless
backend.

**Fixed (M192).** `read_path_dup` now normalises through the new pure
`jc_path_normalize` (`src/util/jc_path.c`) before comparing. Deliberately *not*
`jc_path_resolve`: that calls `realpath()`, and this pass is pure, runs after every
tool round, and is unit-tested offline. `..` is refused rather than collapsed —
`a/link/../b` is not `a/b` when `link` is a symlink, and a false identity match would
elide a read that was never superseded, i.e. lose information. A refused path keeps
its raw spelling, so the worst case is the old missed dedup.

The `compact` event also gained `dup`/`age`, which is how #3 below became answerable
at all.

## 3. What the tokens actually went on

```
model calls ok=4471   in=273.8M   out=1.005M   -> input share 99.63%
read_file  1843 calls over 338 distinct paths  -> 82% repeat reads, 65 MB returned
```

Two independent measurements of the same problem. On a backend without prompt
caching, a token read into the history is re-billed on **every** later call in the
turn, so an 82 % repeat-read rate is not 82 % waste — it is that, compounded by
position. This is the same finding as M93 (which measured 84 %) and M168.

But note what this log **cannot** say: whether M93/M94 already neutralises most of
that 82 %. The dedup runs every round and its effect was reported only as a `jc_logf`
line to stderr, while the `compact` telemetry event merged it with the lossy
age-based pass under one `elided` count. Nor does anything attribute a call's input to
system prompt vs tool schemas vs history. **That absence, not the dedup gap, is what
made M192 an instrumentation milestone** — `model_call` now carries
`sys_tok`/`tools_tok`/`hist_tok` and `compact` carries `dup`/`age`, and the summarizer
renders both. First observation from the new fields, on a two-call smoke run: the tool
schemas were **77 %** of the estimated input, against 17 % system prompt and 6 %
history — a fixed per-call toll nobody had measured.

`cost=$0.0000` throughout: the HRZ models carry no `inputCostPer1M`, so cost
reporting is inert on this workload. Setting even approximate pricing would make
`/cost`, the telemetry summary and the budget numbers meaningful.

## 4. Budget exhaustion is the normal outcome, not the exception

```
Outcomes: completed=74   budget_exhausted: kept=48 reverted=7   verify_failed=0
```

55 of 129 bounded runs (43 %) ended on a budget, not on the task. M80 means 48 of
those kept their work, which is the design working as intended — but a budget stop
is still an interrupted task. The obvious reading is that re-sent read output is the
dominant consumer — but that is exactly the inference this log cannot support, and
believing it is what led to over-ranking #2. M192's attribution fields exist to settle
it on the next drive rather than by argument; the two-call smoke run already hints the
answer is less obvious than "history" (tool schemas were 77 % of that prompt).

`verify_failed=0` with `routes=0` is consistent, not suspicious: the config sets
`escalateOnVerify: true` and `escalateOnError: false`, and the verifier never went
red, so tiered routing correctly never fired.

## 5–6. Two small ones

- **`format_file` 0/3**, all `error: the server returned malformed formatting edits`
  — `zls`'s `textDocument/formatting` reply is not being parsed. Three calls is thin
  evidence; worth one reproduction against `zls` before deciding whether the fault
  is ours or the server's.
- **`todo_write` 28 failures / 36 calls**, all `error: 'todos' must be an array` — and
  all **one** shape: `todos` arrived as a JSON *string* containing the array,
  `{"todos": "[{\"content\": …}]"}`. The model stringified a nested structure.

  This is not `jc_jsonrepair`'s remit (M148), which repairs *syntactically* broken
  almost-JSON after a parse failure; here the JSON parses and is semantically the wrong
  shape. The right precedent is **M172's self-named-wrapper unwrap**
  (`jc_tool_unwrap_self_named`, applied in `jc_tool_execute`, `src/tools/jc_tool.c:669`),
  which solves the same *class* — valid JSON of the wrong shape — and is already
  counted as an `args_repair` with `kind:"unwrap"`. A sibling pass in that same slot,
  `kind:"unstring"`, would consult the tool's own `schema()`/`schema_ctx()`: where a
  declared parameter's type is `array` or `object` and the received value is a string
  that parses as that type, substitute the parsed value. Safe because the substitution
  is validated by parse and the tool's own validation still runs; conservative because
  it fires only on a declared type mismatch. Generalises to `apply_patch.edits`,
  `spawn_parallel.tasks`, `git_add.paths` — and `apply_patch`'s single failure in this
  log (`'path', 'old_string', and 'new_string' are required`) may be the same shape.

## 7. Confirmed already fixed

Hallucinated tool names — `grep` (7), `glob` (6), `todoedit` (5), all
`error: unknown tool`. Every occurrence is dated **2026-06-30 to 2026-07-10**, and
there are **none after July 10**; `grep` and `glob` are both in M91's
`jc_tool_semantic_alias` table today. Reported here only because the raw ok-rate
still shows 0 % for those names and would otherwise read as an open defect.

## 8. Deliberately not judged

`run_tests` (ok 228/309) and `run_terminal_command` (ok 1092/1311) look alarming,
and the summary prints a red-vs-broken split for the latter — but only 33 of 1 311
`run_terminal_command` events and **0** of 309 `run_tests` events carry the `exit`
field at all, because M168a added it on 2026-07-28. Those rates therefore conflate
"the tests were red" with "the tool broke", and this log cannot separate them.
Re-measure after a fresh drive rather than acting on them.

## Method note

Both of the decisive clues for #1 (`latency_ms`, and `in_tok` on the *preceding*
call) and all of the evidence for #2 (`args_full` on `read_file`) exist only because
this project runs at `--log-level full`. That is now three consecutive findings
(#21, #22, and #2 above) that the metrics tier alone would not have supported. The
standing recommendation holds: run long autonomous drives at `full`.
