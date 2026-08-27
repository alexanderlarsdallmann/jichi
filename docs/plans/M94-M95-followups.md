# M94 + M92 stretches + M95 — the noted unbuilt candidates

**Status:** planned (committed before building). Grounded in the M92/M93 work and
the 2026-07-06 telemetry/learn pass. Four small, independent follow-ups; one
candidate is deliberately **not** built (rationale below).

## M94 — eager superseded-read elision

M93 added `jc_compact_trim_superseded_reads` (drop a `read_file` result whose path
is read again later; zero information loss) but only runs it **inside**
`jc_compact_midturn`, which acts only above 80% of the budget. On a cacheless
backend the ~13× context ramp happens *well before* 80%, so duplicate reads are
re-billed every turn until the threshold trips.

**Change:** run the superseded pass **eagerly** — every tool round, regardless of
budget pressure — since it is zero-loss (the newest read of each file is always
kept). `jc_compact_midturn` runs `jc_compact_trim_superseded_reads(hist, 0, ...)`
**before** the 80% high-water check (`budget_tokens = 0` ⇒ it never early-stops ⇒
elides *all* superseded reads), then does the existing age-based pass only when
over the high-water. Duplicates never accumulate.

**Why not the memory-noted "read-time cache" (return a stub instead of the file):**
it risks dangling references — a stub "unchanged since your read above" points at a
copy that a later compaction may elide, and it can hide content the model expects
to re-see. Eager elision gets the same benefit (duplicates never linger) with
none of that risk, and reuses the M93 machinery. So M94 = eager, not read-time.

- **Logging:** the eager path is quiet (no `on_status`; runs every turn) to avoid a
  per-turn "compacting" banner; the pressure-triggered age-based path keeps its
  existing banner. Both still return their elision counts.
- **Test:** extend `tests/test_compact.c` — a history under 80% budget still has
  its superseded reads dropped by `jc_compact_midturn` (exercised via the trim fn
  directly, already covered; add a midturn-level assertion if a known limit is set).

## M92-S1 — headless `--output json` `work_kept`

`jc_agentjson_result` gains an `int work_kept` param, emitted as a bool. Set at the
main.c call site to `(app->env == NULL) ? 1 : !app->env->rolled_back`. An automation
reading `stop_reason:"budget"` can now tell whether its work survived (kept) or was
reverted — the M92 distinction, exposed to the JSON schema (additive;
`stop_reason` values unchanged). Unit-tested in `tests/test_agentjson.c`.

## M92-S2 — `learn analyze` outcome context line

`run_learn_analyze` prints, after the findings, a one-line outcome summary when the
log carries envelope outcomes:
`Autonomy outcomes: N completed, N budget-exhausted (work kept), N reverted, N verify-failed — budget stops that kept green work are NOT failures.`
This reaches the `/learn` mentor (which consumes the analyze stdout), steering it
away from drafting a false "runs keep hitting budget = a problem" lesson.

## M95 — mentor: propose only net-new (dedup-gap root cause)

The `learn apply` dedup is exact-line, so a **reworded** restatement of an
already-remembered note evades it and bloats memory (observed on 2026-07-06: the
mentor re-listed existing lessons reworded). Root-cause fix in the default
`mentor` scaffold prompt (`FILE_MENTOR` in `jc_scaffold.c`): instruct it to put
**only genuinely NEW** lessons under `## Memory notes` — never restate/reword an
existing remembered note — and to use `## Corrections` for a note that is now
false. (The `## Corrections` guidance already exists; this adds the "don't restate"
half.) Scaffold-only text; the scaffold parse test covers it.

## Not built — fuzzy dedup in `jc_memory_add`

Considered and rejected: normalizing whitespace/case won't catch rewording (the
actual failure), and a similarity-threshold dedup risks dropping a genuinely
*distinct* note (a false-positive that silently loses a real lesson). The M95
prompt fix addresses the root cause (don't generate the duplicate) more safely than
filtering it after the fact.

## Verification

Each is small and independent. `make WERROR=1` + `make test` + one full `make ci`
over the combined tree; separate logical commits (compaction / agentjson+cli /
scaffold). No live run required — all exercised by unit + scaffold tests.
