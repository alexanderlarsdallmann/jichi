# JICHI OBSERVABILITY.md — gap analysis (analysis 51)

## TL;DR

*Read-only survey of docs/OBSERVABILITY.md at HEAD 1b71247d (389 lines, last touched 2026-10-04 by M898), every table row and mechanism cross-checked against code and lints, 2026-10-05.*

*INVERSE finding: the most lint-guarded document in the tree, and accurate in every claim I checked. No gap. The value of the analysis is the contrast — this doc is outside the drift class that #44–#50 all fell into, and the reason is mechanical, not virtuous: two standing lints (M366's two-way event-vocabulary registry, M158's docs↔flags) make drift fail the build instead of rot silently.*

## 1. The claims, all verified true

- **Three sinks + their defaults.** Telemetry (`metrics` ON by default since M599 — the operator's 2026-08-27 decision, jc_config.c:1129); run journal (on whenever a run is env'd); privileged audit (ALWAYS on — `privileged_audit = 1` default, jc_config.c:1028, key `privilegedAudit` at :1598). The doc says exactly this.
- **"All four sinks record the build" (M290).** All four verified:
  - telemetry: `jc_eventlog.c:149` stamps every event.
  - run journal: `jc_envelope.c:1218` + `jc_runsview.c:102` stamp the `start` record.
  - audit: `jc_audit.c:57` per entry (append-only across runs, so per-entry is the only form that works).
  - session: `jc_session.c:326` — `cJSON_AddStringToObject(env, "jichi", JC_VERSION)`, re-stamped on every save, so the value is "the newest jichi that touched it" (the comment at :323-325 says precisely that, and the doc's "last saved it" phrasing matches).
- **The readers use it.** `telemetry` warns when a log spans builds (jc_telemetry.c:172-181, capped at 32 — a log spanning 32 builds is treated as one era, a reasonable bound); `runs` prints a not-comparable footer and carries `jichi` per row in `--output json` (main.c:4700, 4801; jc_runsview.c:301).
- **The motivating incident.** "run_tests at 75% and format_file at 0/3, both written up as live defects and all fixed weeks earlier (M168 and 6c6dc89)" — matches the M290 ROADMAP entry verbatim in substance.
- **`doctor --unattended` (M158b).** Real flag: main.c:1164 `strcmp(a, "--unattended")`, the profile is JC_DOC_FAIL-vs-JC_DOC_WARN at :11886.
- **The docs↔flags lint (M158).** `tests/smoke/docs_flags.sh` exists and runs green: "all 149 parsed flags are documented somewhere a user reads".

## 2. Why this doc does not rot (the mechanical reason)

`telemetry_events_lint.sh` (M366) is the guard, and it is *two-way and self-checking*:
- every emitted telemetry event has a doc row, and every doc row is emitted (ghost detection);
- every emitted journal event has a row in OBSERVABILITY.md "Journal event reference", and vice versa;
- **anti-breakage floors**: extraction must find ≥14 telemetry and ≥18 journal names in *both* source and doc, or the lint FAILS (a regex change that silently stops matching is itself a red build).

Last run: ok 1-10, including "all 29 emitted telemetry events are read by a summariser (45 arms)". So the event tables — the part most likely to rot as events are added every milestone — are build-protected. Combined with M158's flag lint, OBSERVABILITY.md is the one top-level doc where the *structure that rotted #47-#50* (a doc outlives the code it indexes) is mechanically prevented.

This is the honest counterweight to the round's pattern: #47 TEST_INTEGRITY, #48 GATE_INTEGRITY, #49 HARDENING, #50 DECISIONS all rotted because no lint reached them; OBSERVABILITY has not rotted *because* a lint reaches it. The tree's doctrine ("prefer the lint to the audit") is visibly doing its job here and absent elsewhere.

## 3. What I did NOT verify

- The prose quality of the journal field descriptions (each row's "extra fields" text was read from emit sites at M366; I verified the row *sets* match, not every field annotation).
- The Recommendations section's forward-looking text (it is advice, not claim).

## 4. Verdict

No gap. The document is accurate in all 8 checked claim-groups, and — uniquely among the top-level design docs surveyed this round — its rot-prone sections are covered by a standing, self-checking, two-way lint. If the register wanted a *recommendation* out of this analysis, it is the inverse of the others: **extend the M366/M158 pattern** (doc-indexed-thing vs code-emitted-thing, with anti-breakage floors) to the surfaces that did rot — DECISIONS.md's anchors, GATE_INTEGRITY.md's line refs, HARDENING.md's section-7 status, TEST_INTEGRITY.md's property counts. That is a generalizable lint family; the doc here is its proof-of-work.
