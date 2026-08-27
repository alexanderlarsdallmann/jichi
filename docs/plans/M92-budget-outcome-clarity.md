# M92 — budget-outcome clarity: distinguish "work kept" from "rolled back"

**Status:** planned (this doc committed before implementation, per the loop).
**Grounded in:** the deeper telemetry-mining pass over the zigodot GDScript-codegen
increments (28 runs). See docs/ANECDOTES.md #6–7.

## Problem (the finding)

Of 28 `--auto` increments, **23 ended with outcome `budget_exhausted`** — yet
**every one banked green work** (the M80 `budget_exit` verify was exit 0; only the
2 earliest, largest runs rolled back). `budget_exhausted` is therefore the *normal,
successful* terminal state for a deliberately budget-sized increment, not a
failure.

But the terminal **disposition** — was the work *kept* or *reverted*? — is
orthogonal to the outcome enum and is **recorded nowhere as a field**. It is only
inferable by correlating a separate `rollback` journal event. Consequences:

- An operator scanning outcomes reads "23 budget_exhausted" as 23 failures. (I did,
  until I cross-checked the rollback count — n=1, but that's exactly the confusion.)
- The offline summarizers make it worse: `jc_telemetry` and `jc_insights`
  (`learn analyze`) **don't parse envelope outcomes at all** — the distinction is
  invisible where an operator would look for it.
- An automation reading the headless `--output json` `stop_reason` gets `"budget"`
  and cannot tell whether its work survived.

The data to fix this is *present* (M80 already runs the exit verify and journals a
`rollback` event); only the **field** and the **summarization** are missing.

## Design

The disposition is orthogonal to `enum jc_env_outcome`, so we add a **boolean**,
not a new enum value. This keeps the `main.c` exit-code mapping
(`outcome == JC_ENV_BUDGET_EXHAUSTED` → exit 2; `JC_ENV_VERIFY_FAILED` → exit 1)
and the headless `stop_reason` schema untouched.

### Core (committed scope)

1. **State — `struct jc_envelope` (jc_envelope.h):** add `int rolled_back;`
   (default 0, set in `jc_env_init`). Single source of truth.

2. **Set it — `env_rollback_and_finish` (jc_agent.c):** set `e->rolled_back = 1`
   **only when `jc_snapshot_restore_commit` actually succeeds** (a real revert
   happened — not merely when rollback was *considered*). Covers both the
   verify-failed and the M80 red-budget-exit rollback paths, since both funnel
   through this one function.

3. **Pure helper (the testable core) — jc_envelope.{h,c}:**
   `const char *jc_env_disposition_name(enum jc_env_outcome o, int rolled_back)`
   returning a stable, human-facing label composing outcome × disposition:
   - `RUNNING` → `"running"`
   - `OK` → `"completed"`
   - `VERIFY_FAILED` → `rolled_back ? "verify_failed (rolled back)" : "verify_failed (kept)"`
   - `BUDGET_EXHAUSTED` → `rolled_back ? "budget_exhausted (rolled back)" : "budget_exhausted (work kept)"`

   Pure; unit-tested over the full outcome × {0,1} matrix. `jc_env_outcome_name`
   (the bare enum name, used in the journal `outcome` field and exit-code logic)
   is left unchanged for back-compat.

4. **Journal `end` event (jc_agent.c):** add
   `cJSON_AddBoolToObject(o, "rolled_back", app->env->rolled_back)` alongside the
   existing `outcome` — so a post-hoc journal reader (e.g. the mining script) reads
   one field instead of correlating a separate `rollback` event.

5. **Telemetry `turn_end` event (jc_agent.c):** add the same `rolled_back` bool
   next to the existing `outcome`, making the eventlog self-describing.

6. **Summarizer — `jc_telemetry.c` (the operator-facing fix):** parse `turn_end`
   events (currently ignored) into an **outcome breakdown** on
   `jc_telemetry_summary`, splitting `budget_exhausted` by `rolled_back`. Render a
   new line, e.g.:

   ```
   Outcomes: completed=5  budget_exhausted: kept=21 reverted=2  verify_failed=0
   ```

   This is the line that stops the misread. Unit-tested via the existing synthetic
   `turn_end` string-feed harness in `tests/test_telemetry.c`.

7. **Docs:** ROADMAP.md M92 done entry; a line in docs/AUTONOMY.md near the M80
   budget-rollback note; a line in docs/TELEMETRY.md for the outcome breakdown.

8. **Tests:** `tests/test_envelope.c` (the `jc_env_disposition_name` matrix) +
   `tests/test_telemetry.c` (synthetic `turn_end` outcome breakdown). `make ci`.

### Stretch (ask before including)

- **S1 — headless `--output json` `work_kept` field:** add a boolean `work_kept`
  to `jc_agentjson_result` so an automation driving jichi learns whether its work
  survived a budget stop. Touches the (already long) result-builder signature and
  the M63 JSON schema (additive — `stop_reason` stays `"budget"`).
- **S2 — `learn analyze` advisory (jc_insights.c):** emit
  "N budget-exhausted runs kept green work (not failures)" so the report doesn't
  count successful bank-and-commit runs as problems. `jc_insights` currently reads
  no outcomes, so this is net-new tabulation there.

## Non-goals

- No change to the `enum jc_env_outcome`, exit codes, or `stop_reason` values
  (schema stability).
- No change to *when* a rollback happens (M80's `jc_env_budget_rollback_decision`
  is unchanged) — M92 only makes the existing decision **observable**.

## Verification

- Pure `jc_env_disposition_name` unit-tested over the outcome × disposition matrix.
- Summarizer outcome breakdown unit-tested with synthetic `turn_end` lines
  (kept vs reverted).
- `make WERROR=1` + `make test` + full `make ci` green.
- Manual: re-run `telemetry ~/.jichi.d/telemetry/zigodot-full.jsonl` and
  confirm the new Outcomes line reads the 86-turn log correctly (though most turns
  there are interactive, not `--auto`, so the budget rows may be sparse — the
  gdscg-* *journals* are the real fixtures, exercised by the unit test).
