# Plan: the data seams, first slice — the join, and the two quality signals

**Status:** done (M420). §§1-5 written *before* the code and left unedited; §6 records what
actually happened, including the deviations.
**Date:** 2026-08-13
**Implements:** [`proposals/2026-08-observability-seams.md`](../proposals/2026-08-observability-seams.md)
— seams **S1** and **S2**, decisions **D1** and **D6** (partial).
**Deliberately not in this slice:** D2 (`history.jsonl`), D3 (aggregate-by-default),
D5 (retention), the rest of D6, D7's failure class. §5 says why.

## 1. Why these two, and why now

The proposal measured seven seams. Two of them are additive, small, and useful the
day they land; the rest either change a shipped default, add a new file with a new
schema, or delete data. So this slice takes only the additive pair:

- **S1/D1 — the join.** A telemetry event carries `sid`/`ws`/`turn`/`depth`; a
  journal event carries `run`. Neither carries the other's key, so *behaviour* and
  *outcome* are recorded separately and cannot be correlated. Writing the
  2026-08-13 campaign analysis, attributing a 34× `apply_patch` loop to the run
  that caused it required matching session files **by mtime**.
- **S2/D6 — the two quality signals.** `verify_stuck` (M89: the run is thrashing on
  one error) and `test_assertion_edit` (M88: a gate may have been moved) are
  written to the journal and read by **nobody**. A run can be stuck for ten retries
  or have moved a goalpost and its `runs` row looks ordinary.

The second one is the reason to do this now rather than later: yesterday's campaign
produced a run whose goalpost warning fired **ten times** and still reported PASS.
M410 fixed the *verdict*; the supervisor's *table* still cannot see it.

## 2. What gets built

### 2a. `run` on telemetry events (D1)

`telem()` in `src/chat/jc_agent.c` already stamps `depth` and `turn` because it has
`app`. It also has `app->env`, so:

```c
if (app->env != NULL && app->env->run_id != NULL) {
    cJSON_AddStringToObject(o, "run", app->env->run_id);
}
```

Conditional on purpose: a turn with no envelope has no run id, and an empty-string
field would be a lie a reader has to special-case. **Absent means "not a bounded
run"** — which is itself information.

### 2b. `ws` on the journal's `start` event (D1)

The `start` event is written in `jc_agent.c` (not in `jc_envelope.c`), so `app->root`
is in hand and **the envelope struct does not change**. One line beside the existing
`verify`/`edit_scope` fields.

This is not for the join — telemetry already carries `ws`. It is for the reader: a
journal that cannot say which project it belongs to is unusable on a machine driving
several, and `runs` currently has no workspace column at all.

### 2c. `stuck=N` and `goalposts=N` in `runs` (D6)

`jc_runsview.c` gains two counters on `struct jc_run_summary`, incremented from the
`verify_stuck` and `test_assertion_edit` events it currently ignores, and renders
them in the NOTES column and in `--output json`. The column already carries
`steered=N` (M161), so the shape and the precedent both exist — no new surface.

Wording matters here and is decided: `goalposts=N` reads as an **accusation of the
run**, which is the point. A green run wearing `goalposts=3` should stop a
supervisor.

## 3. Critical files

| File | Change |
|---|---|
| `src/chat/jc_agent.c` | `telem()` +`run`; the `start` journal event +`ws` |
| `include/jc_runsview.h` | two `long` fields + the JSON contract comment |
| `src/util/jc_runsview.c` | two event branches, NOTES rendering, JSON fields |
| `tests/test_runsview.c` | born-red unit test for both counters + JSON |
| `tests/smoke/*` | born-red: `run` present in an enveloped run's telemetry, `ws` on `start` |
| `docs/TELEMETRY.md`, `docs/OBSERVABILITY.md` | the new field and the two note keys |

## 4. Verification

- `make test` — the new unit checks must be **born red** (assert the fields before
  the counters exist; watch the compile fail, then the assertion fail).
- `make smoke` — full tier; the new driver proven with `tests/teeth.sh`.
- **End-to-end, on real data:** run a small enveloped mock turn, then confirm the
  join actually works — `run` in telemetry, the same id in the journal, and
  `runs --output json` carrying the new keys. A field that exists but does not
  join is worth nothing.
- `telemetry_events_lint.sh` and `notice_tags_lint.sh` stay green (no new event
  names; the vocabularies are unchanged — this slice adds *fields*, not events).

## 5. What is deliberately left out, and why

- **`history.jsonl` (D2).** Held on purpose: once telemetry carries `run`, it may
  turn out that `runs` + a joined telemetry read answers "is this improving?"
  without a new file, schema, lint and version. Building the roll-up first would
  commit us to maintaining it before we know it is needed. **Do the join, ask the
  trend question against it, then decide.**
- **Aggregate-by-default (D3).** A documented behaviour change to a shipped reader;
  it needs its own milestone and a CHANGELOG **Changed** entry, not a drive-by.
- **Retention (D5).** 572 KB after a week is not pressure, and the corpus that made
  yesterday's loop measurable was six days old — a naive window would have eaten
  the evidence.
- **The remaining six unread events and D7's failure class.** Each wants its own
  reader decision; batching them would make one commit that nobody can review.

## 6. What actually happened

Shipped as **M420**. The plan above is unedited; this section records where it was
right, where it was thin, and one thing it got wrong.

**Both fields went in exactly as designed** — `telem()` gained a conditional `run`,
the `start` event gained `ws` from `app->root`, and the envelope struct did not
change. §2a's guess that `app->env->run_id` would be in hand at `telem()` was
correct. Two lines of real logic.

**The plan under-specified the reader work, and it grew.** §2c said "two counters";
the honest total was two counters **plus a `ws[512]` field on the summary struct**,
because a `ws` written to the journal that `runs` cannot read is a field written for
nobody. That was implied by §2b's motivation and absent from §2c's file list — a
plan that names a writer must name its reader in the same breath.

**The parse for `ws` deviated deliberately.** The plan implied reading it from the
`start` event; the code captures the first non-empty `ws` on *any* event (the same
shape as M290's `jichi` capture just above it), so a journal that later stamps the
field elsewhere still yields it. Copying the neighbouring pattern beat inventing a
narrower one.

**One design choice made during the work, not before it: only-when-non-zero.** Both
new NOTES keys are printed only when they fire, and the reason is written where the
code is — *an always-present `goalposts=0` would train a reader to skip the column
the one time it matters.* The unit test asserts the absence as well as the presence,
because that is the half a future "make the output uniform" refactor would break.

**Born red at three levels, in order.** The unit test failed to *compile* (three
missing struct members) before it could fail an assertion; the join driver, run
against a perturbed binary, reported `journal run='44637f60…' vs telemetry run=''`
— the exact pre-M420 state — and `0 of 5 telemetry events carry a run id`; dropping
the `ws` stamp turned check 4 red on its own.

**The end-to-end proof §4 demanded found nothing wrong, and was still the point.**
`telemetry_join.sh` does not assert that a field is *present* — it performs the
join, compares the journal's `run` against telemetry's, and additionally requires
**every** event to carry it. Partial stamping would join some behaviour and silently
drop the rest, which reads as "this run made three model calls" when it made ten.
That check is the one I would have skipped if I had only asserted presence.

**What this slice does not settle**, restated now that the join exists: whether
`history.jsonl` (D2) is needed. The next honest step is to *use* the join — ask a
real trend question against `runs --output json` plus a joined telemetry read — and
let the awkwardness (or absence of it) decide. That is deliberately not a promise
to build it.
