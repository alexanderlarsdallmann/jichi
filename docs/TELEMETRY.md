# Telemetry / event logging

jichi writes a structured, append-only **JSONL event log** of its own
behavior — model latency, tokens, cost, tool usage, routing, compaction, errors,
and (opt-in) the prompts/responses themselves — so you can analyze runs offline
to refine, extend, and optimize jichi. **The `metrics` tier is on by default
since M599**; it records numbers and names, never a prompt, a response or a line
of your code. The `full` tier, which does, stays opt-in.

> **Why on by default** (the operator's decision, 2026-08-27): *"jichi's learner
> must learn from jichi's own development, and dogfooding. Telemetry should be
> on by default, otherwise a learner forgets."* Measured that day: three logs on
> the machine, all from other projects — `learn analyze --workspace .` on jichi
> itself had nothing to read, because the default was off and nobody's config
> said otherwise. The learning loop (`LEARNING.md`) is fed by this file; with it
> off, the loop is a design.

## Turning it off, up, or elsewhere

Per run, on the command line:

```sh
jichi --log-level metrics -p "..."     # structured metrics, no content
jichi --log-level full    -p "..."     # also prompts/responses/tool I/O
jichi --log /path/to/run.jsonl -p ...  # explicit path (implies metrics)
jichi --log -            -p "..."      # force-disable
jichi --log-level off    -p "..."      # the same, by tier
```

Or in config (a project's `local/config.json` or `~/.jichi`):

```json
{ "logging": { "level": "metrics", "path": null } }
```

`level` is `off` · `metrics` (default) · `full`. CLI overrides config.

## Where it's written

Default: `~/.jichi.d/telemetry/<workspace>-<key>.jsonl` — **one file per
workspace, appended across runs** (M599), and **outside** any project workspace
on purpose. (A snapshot rollback runs `git reset --hard`; files inside the
workspace get reverted, so observability must live outside it — see
[`ANECDOTES.md`](ANECDOTES.md).) `<workspace>` is the directory's basename
(anything outside `[A-Za-z0-9._-]` becomes `_`) and `<key>` is the same
per-workspace hash the checkpoint store and the run lease use, so two projects
called `app` get two files. Before M599 the default was one `<run-id>.jsonl` per
run — and `learn analyze` reads *one* log, so the learner had a one-run memory.

The readers (`telemetry`, `learn analyze`, `dream`, `improve`, `doctor`'s
tool-use check) prefer **this workspace's own file** and fall back to the newest
`.jsonl` in the directory only when it does not exist — so a run writes where the
reader reads (the M533 rule), and `jichi telemetry` in project A never summarises
project B because B ran more recently. Every event still carries `ws`, and
`--workspace` still filters. `tests/smoke/telemetry_default.sh` pins all three
properties: default on, one appended file per workspace, reader preference.

**Retention is still yours.** Nothing prunes `~/.jichi.d/telemetry/`; a metrics
file grows by a few hundred bytes per event (measured: 828 events in 341 KB). The
`full` tier is the one that grows fast. `DEFERRED.md` carries the retention row.

## Tiers

| Tier | Captures |
| --- | --- |
| `metrics` | structured numbers + metadata only — no prompt/response/tool content. Small, privacy-safe; ideal for performance/cost/reliability analysis. |
| `full` | everything in `metrics`, **plus** bounded content: the user prompt, the assistant response, and tool args + output. Richer (eval datasets, quality analysis) but the files contain your code — keep them private. |

`full`-tier content fields are truncated to ~16 KB each on a UTF-8 boundary, with
a `...[+N B]` marker.

## Event schema

One JSON object per line. **Common fields on every event:**

> **"Every" became true at M583.** This table has said *every event* since M420,
> and it was **wrong**: `depth`, `turn` and `run` were stamped by a `static`
> helper inside `src/chat/jc_agent.c`, so nine emitters in four other files —
> `prefix_churn`, `hook`, `retrieve`, `test_edit`, `args_truncated` and four
> `args_repair` variants — carried none of the three. M420's join between
> behaviour and outcome was partial by exactly those events. The stamping helper
> is now shared (`jc_app_telem_begin`, declared in `include/jc_app.h`) and
> `telemetry_events_lint.sh` check 9 fails the build if an app-sourced event
> reaches `jc_eventlog_begin` directly again. A documented claim that nothing
> checks is a claim about intent, not about the program.


| Field | Meaning |
| --- | --- |
| `v` | **event-schema** version (currently `1`) — bumped when the field layout changes, *not* when jichi is released |
| `jichi` | the **build** that wrote the event (`JC_VERSION`), M290. Distinct from `v`, and the field that makes a log self-describing: see below |
| `ts` | wall-clock seconds (double) |
| `sid` | run id (correlates a file's events) |
| `ws` | canonical workspace root (M56; lets a shared log be filtered per project) |
| `seq` | per-run monotonic event counter (orders events) |
| `depth` | agent depth (0 = top level, ≥1 = subagent) |
| `turn` | top-level turn index |
| `run` | **M420** — the envelope's run id, **present only during a bounded run** (`--auto`/budgeted). It is the join key into that run's journal (`~/.jichi.d/runs/<run>.jsonl`), so *behaviour* (tokens, latency, tool ok-rates, cache) can finally be correlated with *outcome* (budget, verify, rollback, goalposts). Absent means "not a bounded run", which is itself information — an empty string would be a lie every reader had to special-case. Before M420 the two sinks shared no key and had to be matched by mtime by hand; see [the seams proposal](proposals/2026-08-observability-seams.md) S1. |
| `event` | the event type (below) |

> **What the reader prints, and what it deliberately does not (M584, seams D6).**
> Until M584 the summariser read **10** of the emitted event types and silently
> dropped the rest — `args_truncated`, `constraint`, `constraint_exempt`,
> `history_check`, `hook`, `kinetic`, `prefix_churn`, `privileged`, `retrieve`
> were written to disk on every run and displayed by no command. *A signal
> nobody looks at is the same as no signal*, and it costs disk to keep the
> illusion. All of them now have a reader line, and
> `telemetry_events_lint.sh` check 10 fails the build if a new event type is
> added without one.
>
> **Each line prints only when its count is non-zero**, so a log that never
> exercised a feature stays quiet rather than growing a row of zeros — a zero
> here would read as "measured and clean" when the truth is usually "the feature
> was off". Measured on this project's own corpus (42,652 events, one workload):
> of the nine, only `hook` (15) and `privileged` (2) had ever fired. The other
> six had never occurred, because the features behind them are off by default
> (auto-context), need a violation (`history_check`) or need hardware
> (`kinetic`). So the six new readers are **not** evidenced as valuable; what is
> evidenced is that the tenth event type cannot now be born unreadable.

> **A nameless tool call is counted from `tool_call`, not from a new event
> (M585).** A model can emit a tool call whose `name` never arrives; jichi used
> to answer `unknown tool ''`, which invites it to correct a name it never sent,
> and the measured result was **bursts** — seven such calls across three
> sessions, every one in a run of two or three inside a single turn. The
> diagnosis now says *malformed call* and names the corrective action, and the
> reader prints `Tool calls with NO NAME: N, in M burst(s)`. It is counted from
> the empty `name` the event has always carried, deliberately **not** a new
> event type — so every log already on disk answers the question retroactively,
> where a new event would have started from zero.

**Event types and their extra fields:**

| `event` | Fields |
| --- | --- |
| `turn_start` | `mode`, `model`; *(full)* `prompt` |
| `turn_end` | `result` (ok/aborted/error); `rss_kb` (the process's resident set, M180 — the session's memory curve lives in the log); under `--auto`: `outcome`, `tokens_used`, `tool_calls` |
| `model_call` | `model` (config name), `model_id` (the **wire** model id, M289 — the summarizer groups by this so a config rename does not split a model's history), `attempt`, `status` (HTTP), `latency_ms`, `ok`; on success `in_tok`, `out_tok`, `cache_read_in`, `cache_write_in`, `cost_usd`; *(full)* `response`; on failure `result` (`aborted` / `timeout` / `error` — `timeout` is a stalled/frozen model, M22c). `cache_read_in` is the input tokens served from a prompt-cache hit, `cache_write_in` the tokens written to the cache — Anthropic reports both, OpenAI-compatible backends only `cache_read_in` (M31a). On success also `sys_tok`, `tools_tok`, `hist_tok` (M192) — the **estimated** composition of this call's prompt: system prompt, serialized tool schemas, and history. They use the same byte/4 heuristic as the compaction trigger, so attribution and compaction decisions can never disagree; against the real `in_tok` their sum gives the M77 calibration ratio per call. See "Where did the input go" below |
| `model_retry` | `attempt`, `status`, `backoff_ms`, `reason` (`timeout` / `transient`) |
| `test_edit` | `tool` (`edit_file`/`apply_patch`) and `path` — M88's moved-goalpost heuristic fired: a **test assertion was modified** during an autonomous run. Mirrored here (M417) from the envelope's `test_assertion_edit` journal event so the *offline* readers see it: `telemetry` renders a loud `Goalposts:` line, and `learn analyze` raises a `test_edit` insight at threshold **one** — a single gate edit is the difference between a grade and a gamed grade. The measured case that earned it: ten warnings on one run, verdict PASS, evidence deleted (see [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §10 and `attempt`'s TAINTED verdict). |
| `args_repair` | `tool`, `ok`, and `kind` — `"json"` for an M148 syntax repair (trailing comma, unclosed brace, Python literal), `"unwrap"` for an M172 self-named argument wrapper (`{"edit_file": {…}}` instead of `{…}`), or `"elided_placeholder"` (M289) when the model sent back jichi's own argument-elision marker as arguments. The last is always `ok:false` — it is **unrepairable by design**, since the real arguments were dropped from context on purpose — and is counted so the rate stays visible instead of hiding among generic shape failures; see [COMPACTION.md](COMPACTION.md). Both are argument repairs, so both count toward the `Self-correction` totals; the `kind` field is there so you can disaggregate a *syntax* problem from a *shape* one — they call for different fixes. Since M353 a **successful** repair is also announced to the *model* on the repaired call's own result (one bracketed note asking for strictly valid JSON) — before that, only this event and an INFO log ever knew, so the model ran on silently fixed arguments and kept the habit |
| `tool_call` | `name`, `ok`, `duration_ms`, `output_bytes`, `args` (summary); `exit` — the command's own exit status, present **only** for tools that run one (`run_terminal_command`, `run_tests`), M168; *(full)* `args_full`, `output` |
| `route` | `to` (model), `reason` (`turn-start` / `verify_fail` / `tool_error` / `stall` / `context`) |
| `compact` | (history was summarized this turn); mid-turn: `phase:"midturn"`, `elided`, and the M192 split `dup` / `age` — `dup` counts elisions from the **zero-loss** superseded-read pass (M93/M94), `age` from the lossy age-based fallback. `dup + age == elided`. Before the split, one merged count could not say which mechanism was doing the work, so the dedup's effectiveness was unmeasurable |
| `args_truncated` | `tool` — the call's arguments were cut off at the model's output-token ceiling (M334); the paired error result names the ceiling rather than blaming the JSON |
| `constraint` | `tool`, `reason` — a user-set hard constraint refused a violating tool call mechanically (M110) |
| `constraint_exempt` | `tool`, `why` — an **inferred** read-only did *not* refuse a write, because an explicit `--edit-scope`/`editScope` names that exact path (M459). Emitted only when the exemption changed the outcome, so an ordinary in-scope write is silent. A constraint quietly *not* applied is as opaque as one quietly applied, which is why this exists at all |
| `hook` | `hook`, `tool`, `outcome`, `timeout_s`, `code` — a lifecycle hook **failed** (M25/M326v). `outcome` is a bounded classifier: `start_failed` (the process never started) · `timeout` (killed at its limit) · **`not_runnable`** (exit 126/127 — the command was missing or not executable, so *the check did not run*, M584) · `nonzero_exit` (it ran and complained). `code` carries the exit status when there was one. A clean hook, a deliberate `exit 2` block, and a hook answering with the JSON contract write **nothing**: this is a failure log, not a trace |
| `kinetic` | `subject`, `decision` — the kinetic gate ruled on a hardware-actuating call (M163) |
| `nudge` | `phase` (`fired`/`recovered`) — the prose-narrated-tool-call nudge (M147): fired when a call was narrated instead of invoked, recovered when the retry produced a native call |
| `privileged` | `launcher`, `decision` — the privileged-command gate ruled on a sudo-class launch (M152/M153); the always-on audit log holds the full record |
| `retrieve` | `blocks`, `tokens` — auto-context retrieval attached passages to the user turn (M61) |
| `history_check` | `violations`, `first` — the wire-shape validator found the history contract broken (M364); warned once per run |
| `prefix_churn` | `streak` — the system prompt's hash changed on that many consecutive turns (M365): the cached prefix is being re-billed every call |

### Reading a tool's ok-rate (M168)

`ok` on a `tool_call` answers "did this call succeed", and for most tools that is
the whole story. For the two tools that **run a command** it is not:
`run_terminal_command` and `run_tests` set `is_error` from the command's exit
status, so a perfectly working tool that ran a red build or a red test is recorded
`ok:false` — indistinguishable, in the raw field, from a tool that genuinely broke
(command not found, a timeout, a fence denial, a refused permission).

That distinction matters because **running a red gate is the agent doing its job**.
In a fix-forward loop it runs the gate, sees it fail, and fixes it; a low ok-rate
there is the signature of correct behaviour, not of an unreliable tool. Measured on
30 MB of real dogfood telemetry from the zigodot program: 199 of 294 apparent
shell/test failures were red gates, which made `run_tests` look like a
73%-reliable tool when its **tool-level** success rate was 97%.

So the event now also carries `exit` (only when a command was run), and the
summarizer splits the two:

```
  run_tests                calls=309 ok=228/309 (73%)  dur_ms mean=2622.4 ...
                             of which 73 were red commands (non-zero exit), not
                             tool failures -> tool-level ok=301/309 (97%)
```

Judge the **tool** by the second line and the **work** by the first. The extra
line appears only when some red command occurred, so a tool that runs nothing —
and every log written before M168, which carries no `exit` field — renders exactly
as before.

**Design decision — do not redefine `ok`.** Flipping `ok` to mean "the tool
worked" would have been cleaner in isolation and would have silently changed the
meaning of every historical log, including the 30 MB this finding came from.
Adding a field and deriving the second rate keeps old data readable and new data
honest.

### Where did the input go (M192)

On a backend without prompt caching, essentially the whole bill is input: the same
30 MB dogfood log shows **273.8 M input tokens against 1.005 M output — 99.63 %** —
and 43 % of its bounded runs ended on a budget rather than on the task. Nothing in the
log said *what* those input tokens were, so every explanation ("the history grows",
"it re-reads files") was an argument rather than a measurement.

`model_call` now carries the estimated split, and the summarizer renders it per model:

```
  jlu/qwen3-coder-next     calls=2 err=0  in=3163 out=91  cost=$0.0000  lat_ms ...
                             input/call (est): sys=205 (17%) tools=898 (77%) history=70 (6%)
                             est vs real: 1.35x (byte/4 under-estimates; M77 calibration target)
```

Read it as three different *kinds* of cost:

- **`history`** is the only part that grows within a turn. A high share here points at
  compaction and read discipline (see the `Compaction reclaim` line, and M93/M94).
- **`sys` + `tools`** are a fixed per-call toll, paid identically on every call. They
  do not respond to compaction at all — the levers are `repoMapLimit`, instruction-file
  size, and `toolProfile` (M74 advertises the lean core set). The run above is a
  reminder not to assume: on a short prompt the *tool schemas* were 77 % of it.
- **`est vs real`** is the byte/4 heuristic's error against the server's own count,
  i.e. what M77 calibrates. It is normally >1 (the estimate runs optimistic).

The three fields are estimates from the same helpers the compaction trigger uses
(`jc_compact_estimate_text` / `_tokens`), deliberately — a separate estimator could
disagree with the decisions actually being made. Both lines render only for logs whose
events carry the fields, so pre-M192 reports are unchanged.

## The `telemetry` subcommand

A built-in offline summarizer (no network):

```sh
jichi telemetry            # summarize the newest log under the default dir
jichi telemetry run.jsonl  # summarize a specific file
jichi telemetry --workspace .   # only this project's events (M56)
jichi telemetry --since 7d      # only the last week's events (M286)
```

`--workspace <path>` filters to events whose `ws` matches the given root
(canonicalized to match the stamped value); events without a `ws` are skipped.
Because the telemetry/run dirs are shared across all projects, this is how you
get per-project metrics from a mixed log.

Without a filter, the summary ends with a **By workspace** breakdown (M59) —
each `ws`-stamped root with its event/turn counts (plus an `(unattributed)` row
for pre-`ws` events) — so a shared log makes plain which projects it mixes. The
section is omitted when `--workspace` is set (the report is already scoped) or
when no event carried a `ws` stamp.

### Which build wrote this? (M290)

Every event carries `jichi` — the build that produced it — and the summarizer
states it:

```
jichi: 0.9.0
events=2106 turns=34 ...
```

When a log spans **more than one** build, that becomes a warning, because every
rate below it then mixes eras:

```
jichi: 2 BUILDS in this log -- every rate below mixes them; window with --since to read one era
  0.8.4        11902 event(s)
  0.9.0        2526 event(s)
```

**Why this exists.** A log outlives the code it describes, and reading one era's
numbers as current is a mistake this project made **twice in one session**:
`run_tests` at 75% and `format_file` at 0/3 were both written up as live defects,
and both had been fixed weeks before the log was read (M168 and `6c6dc89`). The
only thing that caught either was having the git history open to date-check commits
against milestones — a correction path a user does not have, and neither does the
[`/learn` mentor](LEARNING.md), which reads this same summary and turns it into
durable memory notes. A lesson learned from pre-fix data is permanently wrong.

Note the trap this replaces: `"v":1` was the only version-shaped field in the log,
it is the *event schema*, and it looks enough like a build number that nobody asks.

`jichi` is on **every** event rather than in a one-time header, for the same reason
`ws` is: the reader filters (by workspace, by `--since`), and logs get concatenated,
split and shared — a header is lost by exactly the operations that make the version
matter. The cost is ~13 bytes/event, 0.5% on a 34 MB log.

The other sinks carry it too — the run journal on its `start` record (surfaced by
`runs`, which warns when the listed rows span builds), the privileged audit log per
entry, and each session file. See [OBSERVABILITY.md](OBSERVABILITY.md).

### Model rows group by wire id (M289)

A `model_call` event carries both the model's **config name** (`model`) and its
**wire id** (`model_id`). The summarizer groups by the wire id and displays the
most recently seen config name. Renaming a model in the config used to split its
history into two reader rows — one real rename showed 777 calls under the new name
and 4585 under the old, each with its own `est vs real` ratio computed on a
fraction of the data — while `calibration.json`, which keys by wire id, correctly
kept one entry. Two instruments, one model, two answers. A pre-M289 log carries no
`model_id`, so it keys by name exactly as before and old reports are unchanged.

### `--since <dur>`: a log outlives the code it describes (M286)

`--since 7d` / `--since 36h` windows the summary to events at or after the cutoff
(the same duration parser as `runs --since` and `audit --since`). An event with no
`ts` is excluded, for the same reason `--workspace` skips an unattributed event: a
window that admitted undatable events would defeat its own purpose. When a window
is in force the report says so on a `window:` line — an unlabelled partial summary
is exactly the kind of number that gets quoted as though it covered everything.

**Why you want this.** A long-lived log spans your own fixes. One project's 34 MB
log ran six weeks and crossed M168 (which taught this reader to separate a red gate
from a broken tool), M192 (input attribution) and M219 (tool-name aliases) — so the
single aggregate ok-rate it printed mixed events from both sides of each fix. Read
whole, it showed `run_terminal_command` at 87% tool-level and `run_tests` at 75%,
and those numbers were written up as live defects. Read with `--since 1d`, the same
log showed **99%** and no `run_tests` failures at all: every one of them predated
M168. The aggregate was not wrong, it was answering a question about six weeks ago.

Window first when you are diagnosing *current* behaviour; read whole when you want
the historical trend. This matters beyond a human reading a report — the
[`/learn` mentor](LEARNING.md) reads the same summary and writes durable lessons
from it.

Example output:

```
Telemetry: ~/.jichi.d/telemetry/<id>.jsonl

events=7 turns=1 retries=0 routes=0 compacts=0 errors=0 (timeouts=0)
Outcomes: completed=41  budget_exhausted: kept=39 reverted=0  verify_failed=0

Models:
  jlu/qwen3-coder-next     calls=3 err=0  in=47994 out=52  cost=$0.0000  lat_ms mean=888 max=992

Tools:
  run_terminal_command     calls=2 ok=2/2 (100%)  dur_ms mean=0.9 max=1.0  out=38 B

Sessions (timeline):
  a9c68fbc calls=12  in=0.27M out=5.2k cost=$0.0000  peak_in=32k  tools=30/32 (93%)  compact=0
  523bb527 calls=94  in=6.76M out=30.5k cost=$0.0000  peak_in=117k  tools=89/90 (98%)  compact=0
```

The **Tools** section counts one row per tool, keyed by **the tool that ran** —
not by the name the model typed. jichi resolves a set of transparent aliases
(`todo_write` → `todowrite`, `create_file` → `write_file`, `glob` → `list_files`,
and others in `jc_tool.c`), and the log deliberately records the **raw** name a
model sent, because a message must say what was asked (M532). Until M591 the
reader keyed on that raw name, so an aliased call opened a *second* row for a
tool that already had one — a real workspace log showed `todo_write calls=1`
beside `todowrite calls=2`, and every statistic on that tool was computed on a
third of its calls. The grouping is done at read time, so it also repairs logs
already on disk.

When an alias was used, a short section names each spelling and its count:

```
Names the model reached for (resolved by alias):
  todo_write               -> todowrite            calls=1
```

It appears only when an alias was actually resolved. A model that keeps reaching
for a name that is not the tool's own is saying something about the tool list it
was given — that the name should be taught, renamed, or promoted from alias to
canonical.

The **Outcomes** line (M92) appears when any turn ran under the autonomy envelope
(`--auto` / `--verify` / a budget). It counts terminal outcomes from `turn_end`
events, splitting `budget_exhausted` into **kept** (green partial work banked at
the budget stop — the normal end state under M80) vs **reverted** (rolled back to
green because the verifier was red at exit). This is what stops a shelf of
successful budget-sized increments from reading as failures; see docs/AUTONOMY.md
(Outcome clarity).

Each session line also carries **`cache=NN%`**, its own prompt-cache hit-rate
(M592), and that is the number to read — **not** the aggregate in the Models
block. A log outlives the settings it was recorded under. One 2026-08-25 drive on
a single model summarised as `hit-rate=8.0%`; its five sessions were 0%, 0%, 0%,
0% and 92.4%, because the deployment's prefix caching was switched on between the
fourth and the fifth. 8.0% describes no session that ever ran, and the "before"
is not recoverable from anything else the summary prints. The hazard is worse
than the equivalent one for tool ok-rates (which is why `--since` exists): a
server setting changes with no trace in this repository at all, so a reader
cannot even date it from the tree. `cache=` is omitted below 2,000 tokens of
input for that session, because a hit-rate over a hundred tokens is noise.

The **Sessions (timeline)** section (M82) lists one line per `sid` in first-seen
order — a run/phase-level view of a multi-run log: model calls, in/out tokens,
cost, tool ok-rate, mid-turn `compact`ions, and **`peak_in`** (the largest
single-call input). `peak_in` is the key line when the backend has no prompt cache
(every turn re-sends its growing prefix uncached): it shows how close each run came
to the context window and which runs therefore compacted. Use it to spot the
expensive runs, the token ramp, and features that needed multiple passes.

## A short mid-turn compaction says so (M323)

`compact` events with `phase: "midturn"` now carry the whole decision — `before`, `after`,
`target`, `limit` (calibrated real tokens, the units the trigger compares) and **`short`**, a
bool that is true when the pass ran under pressure and could not get under its target. The
event is emitted **whenever the trigger fired**, including when nothing was elided — which used
to produce no event at all, and is the case worth seeing.

```
Compaction SHORT: 12 mid-turn pass(es) could not reach the target -- requests went out
  over the configured contextLimit. The lever is LARGE tool results; a history of many
  small ones leaves nothing to elide.
```

Why: a 34,216-event workload ran 1,038 mid-turn compactions and still sent 3.1% of its calls
over the configured limit, invisibly. See
[COMPACTION.md](COMPACTION.md#when-mid-turn-compaction-cannot-reach-its-target-m323) for what to
do about it, and [the analysis](analysis/2026-08-06-large-workload-telemetry.md) for the
measurement. Pre-M323 logs have no `short` field and the line is omitted.

## Transport failures name their cause (M321)

A failed `model_call` that never reached HTTP carries `"status": 0` — no HTTP code to group
by. Such events now also carry **`transport`**, a short diagnosis, and the summary line
reports them:

```
Transport failures: 2402 model call(s) never reached HTTP -- 2402 could not CONNECT
  (no request sent; raise timeouts.connect)
```

**Why this exists.** A 34,216-event log from a large unattended workload had 2,402 such
failures — 15% of all model calls — recorded as nothing but `status: 0`. Their latencies sat
inside 2 ms of exactly 10 s: the default connect timeout, firing deterministically. The
operator had raised `timeouts.stall`, the knob they had heard of, and lost **6.5 hours of wall
clock** to the one they had not. `curl_easy_strerror` cannot distinguish them — for both it
says *"Timeout was reached"* — but jichi can, because it already asks `CURLINFO_CONNECT_TIME`.

A **connect**-phase failure is the one worth acting on: the request was never sent, so no
tokens were spent — only time. Details:
[the analysis](analysis/2026-08-06-large-workload-telemetry.md).

Older logs have no `transport` field and the line is simply omitted, so existing reports are
unchanged.

## Joined to the tool registry: paid-for vs called (M314)

The log knows what was **called**; the tool registry knows what each definition **costs**.
`jichi context tools` joins them — a `calls` column beside the token column, and a footer
totalling what the never-called tools cost on every model call. It reads the newest log
under `~/.jichi.d/telemetry/`, filtered to the current workspace.

`jc_telemetry` stays pure and registry-unaware: the join happens in the reporting layer by
tool name, so nothing here changes. See
[COMPACTION.md](COMPACTION.md#paid-for-vs-called-m314) for the report, and for the four
ways a zero in that column can mislead (no log, a foreign workspace's log, a one-turn log,
and a tool the model was never *able* to call) — each of which the report states rather
than papers over.

## Analysis recipes

The format is plain JSONL, so `jq`, `pandas`, DuckDB, etc. all work directly.

**Total cost of a run** (`jq`):

```sh
jq -s '[.[] | select(.event=="model_call") | .cost_usd // 0] | add' run.jsonl
```

**Mean latency per model:**

```sh
jq -s 'map(select(.event=="model_call"))
       | group_by(.model)
       | map({model: .[0].model,
              calls: length,
              mean_ms: (map(.latency_ms) | add / length)})' run.jsonl
```

**Tool success rate:**

```sh
jq -s 'map(select(.event=="tool_call"))
       | group_by(.name)
       | map({tool: .[0].name, calls: length,
              ok: (map(select(.ok)) | length)})' run.jsonl
```

**Load into pandas:**

```python
import json, pandas as pd, glob, os
path = max(glob.glob(os.path.expanduser("~/.jichi.d/telemetry/*.jsonl")),
          key=os.path.getmtime)
df = pd.DataFrame(json.loads(l) for l in open(path))
calls = df[df.event == "model_call"]
print(calls.groupby("model")[["latency_ms", "in_tok", "out_tok", "cost_usd"]].agg(
      ["count", "mean", "sum"]))
```

## Privacy & safety

- API keys and request headers are **never** logged.
- `metrics` carries no prompt/response/tool content — safe to share.
- `full` files contain your prompts, code, and tool output; they live under
  `$HOME`, are never committed, and should be treated as sensitive.

See [`ROADMAP.md`](ROADMAP.md) (M21) for the design and `jc_eventlog`/`jc_telemetry`
in the source. Telemetry is one of jichi's three observability sinks — the run
journals (`runs`) and the privileged-command audit (`audit`) are the others;
[OBSERVABILITY.md](OBSERVABILITY.md) maps all three.
