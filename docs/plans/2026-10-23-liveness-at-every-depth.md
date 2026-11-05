# Liveness at every depth (batch 14, item (e)) — design note

*The finding: a heartbeat row at least every `--heartbeat` interval while
`spawn_parallel` (and an in-process `spawn_subagent`) blocks, naming the batch
and its live children — batch 14 item (e) of
`docs/internal/drive/2026-10-17-hardening/PLAN.md`. Milestone **M1047**
(claimed from the drive's M960-onwards range; M1046 was the last used,
run 0074). Written 2026-10-23 (run 0075).*

**Status:** design settled for M1047 (one core step; no `-b` planned unless
the gate or the supervisor measures otherwise).

## The gap, measured on 03f83679 (run 0075)

The drive's lead runs with `--heartbeat 60` (jsonl). Two blocking paths leave
the lead's jsonl silent for the entire duration of the block:

### Leg A — `spawn_parallel` wait loop (the measured `stale:0074` gap)

`run_pool` (src/tools/jc_tool_parallel.c:1303) enters a `select()` loop
(200 ms timeout) that blocks until every child has finished. The only
heartbeat it emits is `jc_app_batch_progress(app, done, ntasks)` at line 1490,
which fires **only when a child's pipe goes EOF** (i.e., the child exits).
While children are alive and streaming — the normal state for a multi-minute
batch — the lead's jsonl receives **zero** heartbeat rows.

Reproduction (mockmodel `drip 900` on a 5-event SSE body, ~3.6 s child stream):

```
{"v":1,"type":"heartbeat","elapsed":0.000712358,"rss_kb":13924}
{"v":1,"type":"heartbeat","elapsed":4.50794,"batch":1,"total":1}
{"v":1,"type":"heartbeat","elapsed":4.50856,"rss_kb":14312}
A: total heartbeats=3  batch-heartbeats=1
```

The first row is the lead's own model call (pre-batch). The second is the
single **finish** tick — the child exited at t≈4.5 s. The third is the
lead's post-batch model call. During the ~4.5 s of child streaming, the lead
emitted **no** heartbeat. The supervisor's `stale` watchdog (no jsonl write
for 17 min) raised `stale:0074` at 11:40 while both children were alive and
streaming (supervisor 11:56 note).

The existing throttle in `jc_app_batch_progress` (src/chat/jc_app.c:1588,
`batch_hb_last_ms`) limits emission to once per `heartbeat_secs` — but the
function is never **called** during the idle wait, so the throttle is
irrelevant.

### Leg B — in-process `spawn_subagent`

`subagent_run` (src/tools/jc_tool_subagent.c:467) sets:

```c
const struct jc_agent_callbacks *cb_fwd =
    app->stream_subagents ? app->cb : NULL;
```

Headless leaves `stream_subagents = 0` (only the TUI sets it,
src/tui/jc_tui.c:3318), so `cb_fwd = NULL`. The subagent's model calls then
have no `on_progress` (src/chat/jc_agent.c:340: `if (cb != NULL &&
cb->on_progress != NULL)` is false), so the lead's `hl_progress` heartbeat
never fires during the subagent's model calls.

Reproduction (same drip, in-process `spawn_subagent`):

```
{"v":1,"type":"heartbeat","elapsed":0.000863321,"rss_kb":14212}
{"v":1,"type":"tool_result","name":"spawn_subagent",...}
{"v":1,"type":"heartbeat","elapsed":4.51276,"rss_kb":14392}
{"v":1,"type":"text","delta":"B-DONE"}
B: total heartbeats=2
```

The first row is the lead's pre-subagent model call. The second is the
lead's post-subagent model call. During the ~4.5 s of in-process subagent
model calls, the lead emitted **no** heartbeat.

## The fix

Two independent changes, each in one file:

### 1. Leg A: liveness tick in the `run_pool` wait loop

Add a call to `jc_app_batch_progress(app, done, ntasks, live)` after the
`select()` returns and the control poll, **before** the timeout check
(src/tools/jc_tool_parallel.c:1418, after line 1418). The existing throttle
in `jc_app_batch_progress` (at most once per `heartbeat_secs`) ensures this
fires at the correct cadence even though `select()` wakes every 200 ms.

The existing finish-tick at line 1490 is kept (it carries the same throttle,
so a finish and a liveness tick in the same interval emit only one row).

To name "its live children" per the pin, extend the signature:

- `jc_app_batch_progress(app, done, total)` → `jc_app_batch_progress(app, done, total, live)`
- `on_batch_progress` callback: `(void *user, int done, int total)` → `(void *user, int done, int total, int live)`
- `hl_batch_progress`: add `cJSON_AddNumberToObject(o, "live", (double)live)`

The `live` variable is already in scope in `run_pool` (declared line 1308).

### 2. Leg B: forward the lead's `on_progress` into the in-process subagent

In `subagent_run` (src/tools/jc_tool_subagent.c:467), when
`app->stream_subagents == 0`, instead of setting `cb_fwd = NULL`, construct
a minimal callback struct with **only** `on_progress` and `user` from the
lead's `app->cb` (when non-NULL and `on_progress` non-NULL):

```c
struct jc_agent_callbacks sub_hb_cb;
memset(&sub_hb_cb, 0, sizeof(sub_hb_cb));
if (app->cb != NULL && app->cb->on_progress != NULL) {
    sub_hb_cb.on_progress = app->cb->on_progress;
    sub_hb_cb.user = app->cb->user;
}
const struct jc_agent_callbacks *cb_fwd;
if (app->stream_subagents) {
    cb_fwd = app->cb;  /* TUI: full streaming as before */
} else if (sub_hb_cb.on_progress != NULL) {
    cb_fwd = &sub_hb_cb;  /* headless: heartbeat only */
} else {
    cb_fwd = NULL;
}
```

This is safe because:
- `sub_hb_cb` is a local in `subagent_run`, alive for the synchronous
  `jc_agent_run_subagent` call.
- The subagent's model calls are sequential with the lead's (the lead blocks
  in `jc_tool_execute` → `subagent_run`), so the shared `hb_last_ms` throttle
  in `hl_ctx` is correct: at most one heartbeat per interval across both.
- No other callbacks are forwarded (no text, no tool events, no status), so
  the subagent stays silent except for heartbeats — the headless
  `stream_subagents = 0` contract is preserved for content.

## What is out of scope

- **Forked `spawn_parallel` children's own heartbeats.** Each child runs in
  its own process with its own jsonl (or none). The child's `hl_progress`
  already fires during its model calls. The gap is only the **lead's**
  silence while it waits.
- **The TUI's `--progress` (non-jsonl) path.** The TUI already shows a
  spinner via `on_progress`; the batch progress is shown via `on_status`.
  This fix is for the headless `--output jsonl --heartbeat <secs>` path only.
- **`jc_app_batch_progress`'s `agent_depth != 0` guard.** The guard at
  src/chat/jc_app.c:1584 is correct: nested batches (a child inside a child
  starting its own `spawn_parallel`) run in their own process and have their
  own jsonl. The lead's top-level pool is at depth 0. No change needed.
- **The `describe` event for `heartbeat`.** Adding `live` to the field list
  is a one-line change in src/main.c:9099; the `describe_fields_lint`
  extracts from the emitters, so it will pick up the new field automatically
  (the lint's ground truth is the `cJSON_Add*ToObject` calls in
  `hl_batch_progress`).

## Red-first checks

A new driver `tests/smoke/parallel_liveness.sh` (or an extension of the
existing `parallel_heartbeat.sh`) with:

1. **Leg A — periodic liveness during the wait:** a batch with one child
   that streams for ~5 s (drip 1000, 5 events). With `--heartbeat 1`, expect
   ≥ 3 heartbeat rows with `batch`/`total`/`live` fields **before** the
   child finishes (i.e., while `live >= 1`). Red on the current head: only
   1 batch heartbeat (the finish tick, `live` field absent).

2. **Leg A — `live` field present:** the batch heartbeat carries a `live`
   field (number of children still running). Red on the current head: no
   `live` field in the heartbeat JSON.

3. **Leg B — in-process subagent heartbeat:** a `spawn_subagent` with a
   child that streams for ~5 s (drip 1000, 5 events). With `--heartbeat 1`,
   expect ≥ 2 heartbeat rows during the subagent window (between the
   `tool_call` for `spawn_subagent` and the `tool_result`). Red on the
   current head: 0 heartbeat rows in that window.

4. **No regression — finish tick still fires:** the existing `batch`
   heartbeat on child finish is still emitted (check 1 of the existing
   `parallel_heartbeat.sh` still passes).

## Files owned

- `src/tools/jc_tool_parallel.c` — the liveness tick in `run_pool`
- `src/tools/jc_tool_subagent.c` — the `sub_hb_cb` in `subagent_run`
- `src/chat/jc_app.c` — `jc_app_batch_progress` signature + doc comment
- `include/jc_app.h` — `on_batch_progress` callback signature
- `src/main.c` — `hl_batch_progress` (add `live` field), `describe` event
- `tests/smoke/parallel_liveness.sh` — new driver (or extend
  `parallel_heartbeat.sh`)
- `tests/smoke/run.sh` — register the new driver name
- `docs/plans/2026-10-23-liveness-at-every-depth.md` — this note

## Open questions for the operator

None. The pin is unambiguous: a heartbeat row at least every interval while
blocking, naming the batch and its live children. Both legs are covered.
