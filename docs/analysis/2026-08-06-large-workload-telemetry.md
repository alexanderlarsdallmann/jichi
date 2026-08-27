# A billion input tokens: what a large unattended workload shows (M321)

**Date:** 2026-08-06 · **Source:** one operator's `full`-tier telemetry log from a private
third-party project — 36 MB, **34,216 events**, 315 turns, 31 sessions, 16,075 model calls,
13,783 tool calls, **1.08 billion input tokens**. The project is not named here and nothing
project-specific (paths, file names, code, task content) appears in this document; every
figure below is a jichi-internal aggregate.

This is the largest workload jichi has been measured against — roughly 35× the 31 MB set that
informed M219/M285. Its value is that scale exposes failure modes a short run cannot.

---

## Provenance first, because it changed three conclusions

The log reports `jichi 0.9.0` and nothing finer. Per **M290**, a log outlives the code it
describes, and this project has twice reported a fixed defect as live by forgetting that.

Four tool names in the log never once succeeded: `todoedit` (14 calls), `run_shell_command`
(6), `create_file` (1), `glob` (46). All four are in jichi's alias tables. So either the
aliases are broken, or the log predates them.

**I tested the current build instead of inferring.** A mock model was scripted to call all
four by their invented names:

| Invented name | Current build |
|---|---|
| `todoedit` | **works** (aliased to `todowrite`) |
| `run_shell_command` | **works** (aliased to `run_terminal_command`) |
| `create_file` | **works** (aliased to `write_file`) |
| `glob` | **still fails** |

Three of the four were already fixed; the log predates M219's alias work. Had I trusted the
log, this document would have filed three phantom bugs. Only `glob` is live.

## Finding 1 — 6.5 hours lost to a 10-second timeout nobody could see

**2,402 model calls (15% of all calls) failed**, and the log's only description of them is
`"status": 0, "result": "error"`. No HTTP status, no transport detail. The retry path
classified all 2,366 as `transient`.

They are not random. Their latency distribution is:

```
p10 = 10002 ms      p50 = 10003 ms      p90 = 10004 ms      max = 10004 ms
```

Every one lands inside 2 milliseconds of **ten seconds** — that is not flakiness, that is
`JC_HTTP_CONNECT_TIMEOUT_DEFAULT` (10 s) firing deterministically. Meanwhile the *successful*
calls have **p90 = 9,942 ms**: this workload lives right at the edge of that limit.

**The operator had already tried to fix it, and fixed the wrong knob.** Their config sets
`timeouts: { stall: 90 }` — a sensible response to calls that seem to hang. But the wall was
`connect`, which they left at the default, because nothing told them which timeout fired.
`curl_easy_strerror(CURLE_OPERATION_TIMEDOUT)` says only *"Timeout was reached"*, and even
that goes to **stderr**, not into the event — so the artifact you analyse afterwards has
nothing.

**Cost: 6.5 hours of wall clock in connect timeouts, plus 0.5 h of retry backoff.** Roughly
seven hours, on a workload that otherwise completed 297 of 315 turns.

**It is environmental, not a jichi bug.** The failure rate is bimodal: 20 of 23 substantial
sessions are affected (worst: 35%), but two large sessions — **3,968 and 1,674 calls — are at
0.0%**, and 26 of 76 busy hours are entirely clean. A shared inference endpoint saturating at
its accept queue explains both halves; a client defect would not spare 5,600 consecutive
calls.

**So the defect is the diagnosis, not the timeout.** The fix is to make a transport failure
name itself: record the curl error and *which phase* timed out on the telemetry event, and
have the message name the knob to change.

## Finding 2 — mid-turn compaction ran 1,038 times and still went over budget

Of 1,061 compactions, **1,038 were mid-turn** (M76) and only 23 between turns: single turns
whose own tool churn overflows the window, repeatedly.

Per-call real input tokens: **p50 = 85,960, p90 = 104,692, p99 = 151,327, max = 203,609.**

- **426 calls (3.1%) exceeded the configured `contextLimit` of 128,000.**
- **148 calls (1.1%) exceeded the model's declared `contextLength` of 150,000** — the largest
  by **1.36×**.

No HTTP 400 was ever returned, so the declared window was conservative and no turn died of
it. But `contextLimit` is the operator's stated budget and it was silently exceeded.

**Why compaction could not hold the line.** Mid-turn elision's lever is *the oldest large
tool-result content*. This history has almost no large results: tool output is **p50 = 321 B,
p90 = 2,246 B, p99 = 14,255 B, and exactly one result above 100 KB** in 13,783 calls. The
window is filled by *thousands of small* results — and there is nothing large to elide. **70%
of the over-budget calls happened within six events of a compaction that had just run.**
Compaction fired, did what it could, and the request went out over budget anyway.

**And the event does not say so.** A mid-turn `compact` event records `elided`, `dup`, `age` —
never the before/after estimate, the target, or whether the target was reached. From the log
you cannot distinguish "compaction worked" from "compaction ran out of things to shrink and
gave up 36% over the declared window".

## Finding 3 — `glob` is a capability gap, not a naming accident *(closed: M324)*

`glob` was called **46 times, never successfully** — the most-invented name in the corpus.
It is deliberately hint-only: `jc_tool_canonical_name`'s comment explains that a transparent
alias is added only when the argument schema is compatible, and `glob`'s `pattern` does not
fit `list_files`' `path`. That reasoning is sound.

But the *measurement* says something the reasoning missed: alongside 46 `glob` calls sit
**7,761 `run_terminal_command` calls — 56% of every tool call in the workload.** The model has
no first-class way to say *"list the files matching this pattern"*, so it invents one, and when
refused it shells out. `list_files` lists a directory; `search_code` searches content; neither
answers the question.

**Closed in M324.** `list_files` gained an optional `pattern` (`*` within a segment, `**`
across, `?` one character) using the same pure `jc_glob_match` the edit-scope fence uses — so
the schema objection was *fixed* rather than argued with, and `glob` graduated from hint-only to
a transparent alias. `fd` and `find_files` came with it; `find`/`ls`/`dir` stayed hints, because
their arguments are shell flags rather than a pattern. Verified end-to-end: a
`glob {"pattern": "**/*.c"}` call now returns matching paths instead of `unknown tool 'glob'`.

Two things came out of implementing it that the analysis had not predicted. **`list_files`
consulted no path fence at all** — tolerable while it returned one level of names, not once it
could walk recursively, so the fence and the feature shipped together. And the walk needed a
**depth cap**: `jc_is_dir` follows symlinks, so a link to `..` would have descended forever, and
nothing else in the design would have stopped it.

## Finding 4 — the smaller reliability notes *(spawn_parallel investigated: M325)*

| Observation | Numbers |
|---|---|
| `spawn_parallel` ok-rate | **4/10 (40%)**, mean duration **274 s**, max 462 s |
| `spawn_subagent` ok-rate | 22/28 (78%), mean **437 s**, max **33 minutes** |
| longest single shell command | **32 minutes** (`run_terminal_command`, max 1,921,250 ms) |
| `edit_file` | 1,465/1,612 (90%) — 147 failures |
| `read_file` | 1,820/1,917 (94%) — 97 failures |
| argument repair (M148) | 239/274 (87%) succeeded — the mechanism is earning its place |
| calibration (M77) | est vs real **1.16×** and **1.21×** — working as designed |
| privileged-command gate | 2 events, both recorded |

`spawn_parallel` at 40% over ten calls is thin evidence but the worst ratio in the set, and
each failure costs minutes.

**Investigated in M325, and this sentence was wrong: the log *could* say why.** The `full` tier
records each tool call's `output`, so the six failures were classifiable without a synthetic
workload — I had asserted otherwise from memory rather than checking. Reading them through
jichi's own error strings (never the project's task text):

| Failures | Cause |
|---|---|
| **3** | `sub-agent timed out` — the per-child watchdog at `parallelTaskTimeout`, **default 300 s** |
| **3** | `fork failed` — the child process could not be created at all |

**The timeout default was mismatched to this workload, and the numbers show it plainly.** The
*successful* `spawn_parallel` calls took **300, 309, 362 and 462 seconds**. Children legitimately
needed minutes, so a 300-second cap was killing roughly half of them. The three timeouts sit at
308, 312, 321 and 364 s — clustered just past the limit, the same signature as M321's connect
timeout.

Both messages were unactionable: `sub-agent timed out` named neither the limit nor the knob, and
`fork failed` carried no `errno` — so EAGAIN (process limit → lower `maxParallelAgents`) was
indistinguishable from ENOMEM (memory → run fewer children). Both now say which.

**And the ok-rate itself is misleading**, which is worth recording next to the 40%: telemetry logs
one `tool_call` per *call*, marked failed if **any** child failed. "4/10 ok" can mean six calls
that each lost one child of four, not six calls that achieved nothing. `PARALLEL.md` now says to
read the result text rather than the ratio.

## What this licenses, ranked

1. **Make a transport failure name itself** (Finding 1). The curl error and the timed-out
   phase belong on the `model_call` event, and the operator-facing message should name the
   knob. Seven hours were lost to a knob the operator did not know existed. *This is the fix
   with the best ratio of effort to harm avoided.*
2. **Make a short compaction say so** (Finding 2). Record before/after/target on the mid-turn
   `compact` event and warn when the target is not reached. It is the difference between a
   silently-exceeded budget and a diagnosable one.
3. **Close the `glob` gap** (Finding 3) by giving `list_files` an optional pattern, which makes
   the alias schema-compatible and retires 46 failures plus some share of 7,761 shell calls.
4. **Configuration guidance** (below).
5. **Look at `spawn_parallel`** (Finding 4) with a workload that exercises it.

## Configuration guidance this workload argues for

Written for the docs, not as jichi changes:

- **On a shared or remote endpoint, raise `timeouts.connect`.** The default 10 s is fine for a
  healthy endpoint and lethal for a saturated one; the symptom is a cluster of failures at
  *exactly* the timeout value, which is the signature to teach.
- **`contextLimit` is a target, not a guarantee.** With a history of many small tool results,
  mid-turn compaction may not be able to reach it. Set the model's `contextLength` honestly and
  leave headroom rather than assuming the limit will be enforced.
- **A `summarize` model with an 8,192-token window will be called a lot** on a workload like
  this (1,038 mid-turn compactions). M30 chunks the prefix to fit, so it works — but the
  round-trip count is worth knowing before choosing a tiny summarizer.
- **`run_terminal_command` at 56% of all tool calls** suggests reaching for the shell where a
  first-class tool would be cheaper and safer to gate. Worth reviewing per project.
