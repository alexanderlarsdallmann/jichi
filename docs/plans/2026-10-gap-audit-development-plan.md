# Plan: the 2026-10-05 gap audit — development and register discipline

*A plan, written on 2026-10-06 off master `79ec7e84`. The operator approved the
53-document self-audit (`docs/analysis/2026-10-05-JICHI-*.md`, merged as
`22d7886f`) and asked for a design-and-development plan, to work on a branch,
committing and pushing frequently, and to start with C1. Nothing in this page is
a milestone until the operator reviews it; where an item is already tracked, the
page says so and does not re-open it. Paths are relative to the repository root.*

**Status updated 2026-10-08: Wave 0 (the register lints, M903-M908) is landed and
merged to master; the M902 gate red and the Wave 1 items (M899-M901) landed
previously. Wave 1 item 3 (def-317, §9) landed as M909 on
`plan/2026-10-08-def317-handoff` (the R1 claim ticket, the R2 copy-back teeth,
the R6 report wiring); only the R3 measurement preceded it (M901, on master),
and def-317's skip-args-when-on-disk policy remains deferred. The rest of the
waves are recorded, not started. `make ci` is green on the merged head
(15012 checks, 0 failures in the final unit run). The operator's
decisions D1-D5 are recorded in §7. A `Status` line here is what the plan-status
lint (Wave 0, §4) will read, so it is kept current as waves land.

## 0. What the audit found, in one paragraph

The audit's two structural findings set the order of work:

- **The hot paths are measured; the cold paths are not.** Mitigations reached the
  TUI, the watched shell, and the parallel children, but not the audit sink, the
  headless output, the plain `popen` path, the args-elision pass, the workflow
  merge, or the generated `run.sh`. The 2026-10-04 security audit (G1–G8) is the
  tree-level statement of the same fact.
- **The register's deepest gap is witnesses, not code.** The UX study, the
  curriculum delivery, deck 07, and the multi-machine drive are all designed and
  never run.

Seven cheap doc fixes and three deferrals (def-315/316/317/318/319) already landed
as `82f19fc0`. The remaining ~40 findings are grouped below into five waves.
**Every wave item needs its "untracked" greps re-run against the current head
before it starts**, because the audit was read-only at `1b71247d` and master has
moved since (M897/M898, the DECISIONS anchor renumbering `79ec7e84`, the seven
closed doc fixes). A stale "no such test exists" grep is the difference between a
real gap and a duplicated one.

## 1. The waves

### Wave 0 — Drift re-verification and register integrity (docs + lints only)

Re-run each audit document's "how to prove" commands against the current head.
Cross-check the live-drift trio (man-page env parity, `init --list` bench list vs
the 13 bench directories, the two `docs/reading/` prose fixes) against any
in-flight branch before acting, per the audit's own reading order. Then land the
lint family def-319 names, each red on its named file today:

- `plan_status_lint.sh` — 4 stale "not built" headers in `docs/plans/` (M92, M93,
  M94-M95, files-and-structures) vs ROADMAP-done milestones.
- `decisions_anchor_lint` — anchor uniqueness + link resolution (partially fixed
  by `79ec7e84`; the lint keeps it).
- `reading_refs_lint` extended past `docs/reading/` to top-level `docs/*.md` (the
  GATE_INTEGRITY anchors rot is exactly what its narrow scan missed).
- a man-page/env-var parity lint and a bench-inventory parity check (the examples
  and man-page findings share this root cause: three inventories, no cross-check).

Each follows `docs/TESTING_RUNBOOK.md`; `tests/teeth.sh` proves each red.

### Wave 1 — Context economy (the audit's own wound)

Where the audit session itself broke (large-heredoc-write workload; a write's
arguments elided and re-sent 8+ times):

1. **Byte-stability test (this sitting).** The M31d `test_prefix_stable` asserts
   byte-identical bodies for two calls on the *same* inputs. It does **not**
   assert that the system+tools prefix stays identical across a *growing* turn
   (turn 1 vs turn 2 with caching on) — which is the property the cache actually
   depends on, and the audit's PROMPTCACHE rec 1. Add it for both providers.
   This test is written **before** C1, because it is C1's falsifier: if C1
   churning the prefix is the bug, this test is the red that proves it.
2. **C1 (this sitting).** The standing water-level note. See §2 for the design
   and why the audit's literal wording ("in the system prefix") is adjusted.
3. **def-317 / compact-optimization R1+R2+R6.** Give the M218 args pass a spill
   callback so elided arguments get a 0600 claim ticket (M348 symmetry),
   recover-on-reject in the copy-back trap, and make the M361 exhaustion latch
   see args. R3 lands first (measure before the fix).
4. **def-318 / Claude-Code C2.** Cap (or spill-and-reference) subagent hand-backs
   at the spawn boundary — measured 301 KB on the sibling's own logs.
5. **Compaction prefix archive + fidelity probe.** Archive the dropped prefix to
   a session-lived store (the M348 analogue, scoped to compaction); a
   mock-model fidelity probe with planted facts; the COMPACTION.md honesty
   sentence; a DEFERRED row for the current unrecoverable state.
6. **Resume envelope warning.** A bounded `--auto` run's envelope is
   per-invocation and silently lost on `--continue`; warn at resume if the prior
   run was bounded (the journal already records `edit_scope`). Red-first driver
   meeting the envelope and session tests.
7. **Session build-tag warning.** Read `env.jichi` (M290 wrote it for exactly
   this) at load and warn when it differs from the running build.
8. **Claude-Code C3–C5.** The scratchpad surface, the operator handoff-file
   convention, and redact-before-disk ordering as load-bearing — recorded as
   items, not built, until 3/4/5 are measured.

### Wave 2 — Security cold paths (the security audit's suggested order)

G8 → G6 → G4 → G5, each one small change with a red-first test:

- **G8** `jc_ctrl_sanitize` at the headless/ACP/daemon JSONL sinks (model text is
  currently raw — OSC 52 reaches the client's renderer; the TUI was fixed, the
  cold paths were not).
- **G6** privileged-launcher detection: `timeout 5 sudo …` / `watch` / `xargs`
  bypasses; wrapper-list entries + a stated limitation.
- **G4** audit-log create via `open(O_NOFOLLOW|O_EXCL)` + `fdopen` (one
  portability review).
- **G5** `write_file` parent directories via `jc_mkdir_p_private`.
- The two things the security docs *claim but never prove*: the SSRF
  **DNS-rebinding / redirect-to-internal** tests (unit driving
  `opensocket_guard` with a fake `curl_sockaddr`, plus a 302-to-loopback smoke),
  and the **injection probe set** (offline, mock-model, ≥2 free `jlu/*` models;
  SECURITY.md amended to "refusal is unmeasured" until it runs).
- The **memory-fence decision** (wrap with `jc_untrusted_wrap` vs. record
  "memory is first-party guidance") and the **project-rules trust-premise fix**
  (def-298's "the untrusted rule still applies" has no referent) land here as
  decisions + red-first tests.
- **def-315** (a SIGTERM to jichi does not stop a plain-`popen` command) is a
  milestone of its own in this wave: every command gets the watched path's group
  + abort check. It changes how every command runs, so it stands alone.
- **G7** the HARDENING.md links to `analysis/` (not `docs/analysis/`): doc fix.

### Wave 3 — Actuation and integrity of the merge/restore machinery

1. **Snapshot false green.** Check `git clean -fd`'s return in `restore_to`
   (jc_snapshot.c:705-716); a FAULT=1 driver; the envelope's rollback check then
   fires for real.
2. **Checkpoint `/undo` ignored-file report line** + red-first driver.
3. **`run_workflow` merge non-destruction.** Conflict-loser changes preserved to a
   ref (M803 pattern) or at minimum a per-file lost-path report; a two-steps-same
   path driver; a shared helper with `spawn_parallel`.
4. **`--edit-scope` implies `--strict-scope` under `--auto`** (or an explicit
   acknowledgment) — makes ANECDOTES #45 structurally impossible; a driver that
   arms scope, writes out-of-scope via the shell, and asserts refusal.
5. **Telemetry content redaction** (the FULL tier logs file contents verbatim):
   path masking, a canary-leak driver, `args_full` behind an explicit opt-in.
6. **Prompt-cache sentinel widened** to hash the tools array (rec 2) and emit a
   quiet one-time `prefix_reset` event.
7. **Setup `run.sh` generator**: the four launcher house rules; a subdirectory-run
   driver red today.
8. **Godot pack decision transcription** (decided 2026-10-01, zero C landed):
   `--check-only` verify + GDScript 4 guidance; def-294 reconciled.
9. **Notify "wants input" + rollback states**; **repo-map staleness note**
   (cache-stable one-liner); **daemon — decision (b)**: thread the client's
   resume id through `jc_daemon_build_prompt` (the NULL slot already exists),
   making the doc's warm-session bullet true while keeping W3's fork-per-request
   isolation, with the two-prompt continuity test either way.

### Wave 4 — Witnesses and live-surface tests (operator-arranged)

1. **Mock LSP stdio server** in CI (four def-rows become regression-protected) +
   an availability-gated real-server driver.
2. **MCP remote tool call**: the mock serves one `echo` tool; a headless turn; the
   approval gate both directions; one HTTP-transport smoke.
3. **Retrieval-quality micro-benchmark** for semantic search (recall@5/MRR on
   ground-truth queries against the free `jlu` embed endpoint) + an offline
   ranking-sanity driver.
4. **Driven-debt dial** for the platform matrix (PORTABILITY + PLATFORM
   converge): per-row `driven:` field, offline-debt-style banding, a T8 trigger,
   re-drive WSL2 first.
5. **Emacs revert-offer ERT test** (the stub gains an edit mode); a scheduled
   non-PR `JC_E2E_MODEL` job on a local model.
6. **W8**: the operator plays the pong once.
7. The JLU-arranged events: the **learner UX study** (protocol complete; settles
   def-42's five disagreements), the **6-session curriculum delivery** (first
   INSTRUCTOR §10 record), **deck 07 to a reviewer**.
8. **One recorded multi-machine drive** (SSH layer on the real fleet, jichi
   supervising, the divergent-fleet merge policy written first) and the **cost
   ceiling**: a `costUsd` budget the COST doc names as "never in dollars".
9. **Debugger**: at minimum the DEFERRED row with measured costs (the unrecorded
   absence) and the cheap crash path — a `crash_report` tool using addr2line,
   core-dump opt-in mirroring `JICHI_KEEP_DUMPABLE`. DAP only if the row's
   measurement says so.
10. **def-316 measurement** (how often drives' journals show recursive removals
    of granted directories) — decide note-vs-Landlock-after.

## 2. C1 — design note (the item this sitting starts with)

The audit's C1 says: *a low-cost standing line (pool + percent) in the system
prefix — the M300-style "say it once, in the cached prefix" pattern — would give
the model continuous water-level awareness instead of a red alert at the edge.*

The literal wording has a contradiction the audit did not flag, and the
byte-stability test (item 1) is what finds it:

- The **static** half — "your context window is ~N tokens" — already sits in the
  cached prefix (`jc_sysmsg_append_context_window`, M358, byte-stable within a
  model). That half of C1 is *done*; what is missing is the **dynamic** half.
- A **live percent** in the system prefix changes the prefix's bytes every turn.
  That is exactly the churn the M365 sentinel exists to catch ("the prompt hash
  moves on turns 2,3,4"), and it breaks the M31 cached prefix the prompt-cache
  depends on. The byte-stability test asserts the prefix bytes are identical
  across a growing turn; a live-percent-in-prefix would make it red.
- jichi already solves "dynamic, model-facing, cache-stable" with the M358
  `[context]` note: a user-role message at the tail (the M347 control-inject
  shape), from the pass's own calibrated numbers. Its only weakness is the
  **once-per-run latch** (`ctx_noticed`): the note arrives on the first pressed
  round and is silent after, so the model has a single water-level reading, not a
  standing one — the audit's actual complaint.

So C1 is: **remove the once-per-run latch; re-fire the note each mid-turn round
while the pass is over the high-water**, using the cache-stable tail vehicle. The
model gets a standing reading every round it matters (over 80%), the cached
prefix is untouched (byte-stability test stays green), and the M365 sentinel is
not fired by legitimate operation. The note's text gains the pool + percent in
that standing shape.

What is **not** built now, and is recorded as a DEFERRED row with its trigger:
the *unconditional* per-round standing note (a tail message injected on every
round whether pressed or not). Its cost — one extra message per round, possible
tool-call reshaping, and whether the tail breakpoint's cache still matches — is
not yet measured, and the register's doctrine is measure-before-change. The
pressed-only standing note (what C1 ships) is the version whose cost is bounded by
the existing M323 emission condition.

Falsifier for C1: a driver that arms a context limit, drives two pressed mid-turn
rounds, and asserts the note appears in **both** (red if the once-per-run latch is
restored); the byte-stability test must stay green (the prefix did not churn);
`make ci` green on the head.

## 3. def-317 R3 — the measurement that precedes the fix

The audit's R3 generalizes `tests/measure/reread_ratio.py` (hardwired to
`read_file`) to count **write re-sends** (repeat `write_file`/`apply_patch`
events over identical path/content fingerprints) and **`elided_placeholder`
rejection events**, so the cost of args-elision is countable before R1/R2 change
it. This sitting lands the generalized instrument and runs it on an available
corpus, reporting with the script's own 50-call floor (below it is "NOT
EVIDENCE", never a percentage). It does **not** land the R1 spill fix — that is a
later milestone gated on these numbers.

## 4. Register discipline (applies to every item, every wave)

- **Milestones.** Every code change is one ROADMAP milestone (the next free M###
  after M898), with design + implications + the red-first proof, in the register's
  form. No batching several findings into one milestone: a gate that reaches five
  things at once verifies none of them.
- **Red-first, always.** Each new test is watched fail before its fix
  (`tests/teeth.sh`). A test never observed failing has never been observed
  working.
- **DEFERRED rows for what is not done.** Every audit finding that a wave does
  *not* close gets a DEFERRED row with a revisit trigger and the rejected
  alternatives, so the register stays a finding, not a liability. The deferral
  is as honest as the fix.
- **No caps on measurement runs.** A reproduction is a measurement; `timeout` /
  `--deadline` / budget tokens stay off. Fences (`--edit-scope`, path fence,
  read-only, `--max-tool-calls`) stay on. `deadStream` (M-L2) stays on — it is a
  fence, not a cap.
- **Free models only.** All model work — the live re-drives, the retrieval
  benchmark, the injection probe — is on `jlu/*` on the HRZ gateway or a local LM
  Studio. No priced model, no stripped-prefix id. If a step would cost money, it
  stops and asks the operator for that one named run.
- **Commit attribution.** Per `docs/analysis/COMMIT-ATTRIBUTION.md`:
  `Co-Authored-By: jichi <version> <address>`, `Jichi-Build: <short-hash>
  (<branch>, <date>)`, `Jichi-Model: jlu/qwen3.8-27b`, as a trailer block after a
  blank line. The build hash is this working tree's HEAD at commit time.
- **Commit frequently, push after each green step.** Work is stored on the
  branch, not in a session. Push only when `make ci` is green on the exact head
  (or, for a docs-only commit, when the doc lints it names are green).
- **Work on the branch `plan/2026-10-06-gap-audit-development`.** This document
  and all Wave 1 work land there first; merge to master only after `make ci` is
  green on the exact head.

## 5. Ordering and dependencies

- Byte-stability test **before** C1 (it is C1's falsifier).
- R3 measurement **before** the R1 spill fix (measure the cost, then change it).
- Wave 2's G8/G6/G4/G5 are independent of each other and of Wave 1; they may
  interleave once Wave 1's context work is green.
- Wave 4's witness events need the code side of Waves 2–3 landed first, so the
  witnesses measure the fixed state, not the old one (def-42 settlements, the
  daemon doc, the platform rows).
- Nothing here blocks on a priced model or on hosted CI.

## 6. What this sitting does, in order

1. Create the branch (done), configure the authenticated push (done).
2. This plan document + register discipline (this commit).
3. Byte-stability test, red-first where it can be red, green on the head, commit +
   push.
4. C1 (remove the once-per-run latch, standing pressed-only note), red-first
   driver, `make ci` green, commit + push.
5. def-317 R3 generalized measurement, run on an available corpus with its floor,
   commit + push.
6. ROADMAP + DEFERRED rows for what landed and what is deferred.

## 7. Operator decisions, 2026-10-06

Recorded at the start of the sitting, quoted from the operator where given:

- **D1 — Work on a branch, commit and push frequently.** Branch
  `plan/2026-10-06-gap-audit-development`; push after each green step; merge to
  master only after `make ci` is green on the exact head.
- **D2 — Commit attribution.** Use the trailers in
  `docs/analysis/COMMIT-ATTRIBUTION.md`; the model string for this sitting is
  `jlu/qwen3.8-27b` (the operator named it).
- **D3 — This sitting's scope.** Plan document + register discipline, then the
  byte-stability test, then C1, then def-317 R3 measurement. The rest of the
  waves are recorded, not built.
- **D4 — Daemon: go with (b).** Thread the client's resume id through
  `jc_daemon_build_prompt` (the NULL slot already exists), making the
  warm-session doc bullet true while keeping W3's fork-per-request isolation,
  with the two-prompt continuity test. Recorded here for its Wave 3 milestone;
  not built this sitting.
- **D5 — C1 before the R1 fix, and C1 is pressed-only.** The standing note fires
  each mid-turn round while over the high-water (the once-per-run latch is
  removed); the unconditional per-round note is deferred with its trigger until
  measured (§2).

*Drafted by the plan mode of this session; no item here has a milestone number
yet. The next ROADMAP milestone to be taken is M899 (byte-stability test), M900
(C1), M901 (R3 measurement), unless the operator reorders.*

## 8. Wave 0 progress, 2026-10-07 (branch `plan/2026-10-07-register-lints`)

**Drift sweep (done, read-only, at `1db8e36d`):** all three live-drift trio
items are still red at the current head -- (1) man page names 0 of the 7
project env vars the code reads; (2) `init --list`'s printf (src/main.c:5175)
lists 12 benches, disk holds 13 (`slm-lab` missing); (3) both `docs/reading/`
prose defects stand (`fukabori-07:91` cites `jc_tool_subagent.c` for a function
defined in `jc_agent.c`; `fukabori-09:14` over-claims). No in-flight branch
conflicts: the three `man/jichi.1` branches are 278-348 commits behind master
and diff only `trust`/`untrust` command entries; m750's slm-lab diff is a base
artifact. GATE_INTEGRITY's line anchors were already fixed by `22d7886f`.

**Lints landed (commit `d65a6ef7`):**
- `plan_status_lint` (M903, GREEN 4/4): two rules, measured union = exactly the
  audit's four stale pages; the four headers now carry supersession notes
  naming the done milestone. Teeth: reverting M92's header makes check 2 red
  on M92 only. Rule B's window (20 chars after the dedated basename, excluding
  `experiment` lines) was tuned by measuring all seven not-built pages.
- `decisions_anchor_lint` (M904, RED 2/4 as intended): the M170 sweep left 9
  `<a id="dec-m?">` anchors (`dec-m?` and `dec-m?-2` duplicated) and 8 `[M?](#dec-
  m?)` links. Checks: id uniqueness, no `?` in ids or targets, link resolution.
  **Blocked on content work:** each of the 8 rows needs its real milestone.
  Partial mapping found: row 2064 "Prefer a lint to an audit" -> M259's house
  rule; row 2382 "Prove a gate is satisfiable" -> M329's session (line 18391).
  The rest have no unambiguous ROADMAP hit -- a wrong M### is worse than `?`,
  so this sits for a fresh context that can read each candidate entry fully.

**Landed this sitting (branch commits, M903-M907, all red-first with teeth):**
- M905: man page ENVIRONMENT now names all 9 `JICHI_*`/`JC_*` vars the code
  reads (measured, floor 9, extraction over every TU — `man_page_lint` check 8);
  teeth: deleting one var's entry goes red on that var.
- M906: `init --list` printf now lists all 13 benches (`slm-lab` was missing);
  `examples_lint` check 3 (floor 13, extraction by RUNNING the binary per the
  audit's spec, two directions); teeth: red on `slm-lab` against the pre-fix
  binary.
- M907: the two reading prose defects fixed (fukabori-07 anchor
  `jc_tool_subagent.c` -> `jc_agent.c` where `jc_subagent_can_spawn` is
  defined; fukabori-09 "before each mutating turn" -> the three actual
  `jc_snapshot_take` call sites). `reading_refs_lint` green.
- DECISIONS.md: 3 of 9 `M?` rows attributed (prefer-lint->M259,
  satisfiable->M330, force-deliverable->M330); 6 stay `M?` with a
  supersession note naming the evidence trail, and unique ids so the anchor
  lint's ambiguity check is green. `decisions_anchor_lint` 4/4.

**BLOCKED at the context limit — a fresh sitting must finish this, and it is a
whole-table job, not two rows:** `docs_counts_lint` checks 12+13 (PROJECT
TIMELINE). The page's own M865 methodology pins every universe and recounts
ALL rows in one pass, and it documents that a figure whose counting method
doesn't reproduce is CARRIED, not bumped. Measured so far (2026-10-07, this
head): ROADMAP `###` entries 890 (M1-M907); 1,707 commits; unit files 137;
smoke drivers 424; e2e modules 10; fuzz targets 23; unit checks 15,005 per
run (the green CI's final unit run reported 15,012 — a +7 this branch cannot
have caused: it touches no tests/*.c; not chased further here). NOT
reproduced: the Documentation row — "576
English markdown pages (628 files)" matches no universe tried (`docs/*.md`
excl i18n gives 634 English pages/200,913 lines, all files 686; the M865 pin
note says 564 pages reproduced at M832 with `docs/internal/` excluded, so the
definition moved or includes a further exclusion). The 90-assignments/75-traps
sub-figure is already marked carried in the page (90 `verify:` specs + 75
traps re-verified as current). The smoke-tier check total also needs a clean
`JC_SMOKE_KEEP_GOING=1 make smoke` run (the last one was killed at the
context limit; 1,105 ok-lines reached before it halted).

**DEFERRED, with the population measured (2026-10-07):** the
reading_refs_lint extension to top-level `docs/*.md`. The audit marked it
"Optional, structural"; its mandatory fix (GATE_INTEGRITY's live anchors)
landed at `22d7886f`, and that page now has zero line anchors. A scan of the
extension's proposed universe found 42 current offenders: 33 line-anchors
(21 in the chronological records -- ROADMAP 17, ANECDOTES 4, DEFERRED 6 --
historical evidence the DECISIONS house rule protects) plus not-found refs.
A gate firing 33 times on deliberate citations is the audit-wearing-a-lint's-
clothes rot the house rule rejects. Two live pointers were reworded to symbol
anchors on the way: RACKET_PARADIGM.md:151 (jc_rrf_fuse, drifted 65->68) and
FILE_HANDLING.md:121 (jc_write_file_atomic, drifted 300->336). A future
sitting can scope the extension to the non-record pages only, or record the
deferral in DEFERRED.md.

Then: `make ci` green on the exact
head, ROADMAP M903-M907 entries already appended, plan Status line, merge.

## 9. def-317 R1+R2+R6 handoff, 2026-10-08 (anchored, red-first design)

Wave 1 item 3 is scoped so the next sitting starts at implementation. All
anchors verified at `fff76fd5` (master).

**R1 — the claim ticket for elided ARGS (the wound fix).** The M348 ticket
pattern exists only for the result side: `jc_compact_trim_tool_output_ex`
(`include/jc_compact.h:178` typedef `jc_compact_spill_fn(ctx, text, len,
path_out, path_cap)`, `src/chat/jc_compact.c:965`) offers each elision to
`midturn_spill` with `app` as ctx. The ARGS side (`jc_compact.c:~1060-1078`,
`elide_args_marker` at :994) replaces `tc->arguments_json` with a ~100-byte
marker and calls no spiller. Add an `_ex`-style spill to the args path:
`elide_args_marker(old, len, spill, spill_ctx, ticket, cap)` offers the FULL
`arguments_json` (0600, same store the result tickets use), and the marker
naming the ticket is built only when the offer is accepted — byte-identical
to today's marker when spill is NULL or declines (the ticketless no-op
invariant M348 set). `app` flows in from the same call site as
`midturn_spill` (:1435-1440). The M289 copy-back trap (`src/tools/jc_tool.c:1407`)
then fires for a model that copies a ticket-bearing placeholder verbatim —
R2 needs no change IF the args marker embeds the same M289 directive note the
result marker does (`elide_args_marker` already reserves `note[256]` for it);
verify the trap's kind string path covers args elisions, red-test with a
planted placeholder copy-back of an args marker.

**R6 — the M361 latch sees args.** `struct jc_midturn_latch`
(`include/jc_compact.h:342`; use at `jc_compact.c:1377-1386`) skips the LOSSY
trims after a proved-exhausted pass. MEASURED at the handoff, no gap: the
latch check (`:1386`) is an EARLY `return dup` (`:1419`) and the args trim
(`jc_compact_trim_tool_args`, def `:1038`, call `:1443`) sits downstream of
it, so a latched pass never re-runs it; the latch SET condition (`:1465`)
already treats an args-only-elided pass as not exhausted. R6's real work is
report-side: the R1 ticket path must feed `mrep.preserved` (`:294`). Do NOT
write a latch-skip red test — it would be green from the start.

**Red-first order (teeth.sh):** (1) synthetic history with a >ELIDE_MIN_BYTES
write_file call, run the args pass, assert the ticket file exists 0600 and the
marker names it — red today (no ticket); (2) copy-back trap: model arg
containing the args marker verbatim → `elided_placeholder` journal kind —
red if uncovered. (The third candidate was the latch red test — dropped
after measurement: see R6, it is green by construction.) Then the fix,
then `make ci` on the exact head.

**Falsifier (from M901's measurement):** after the fix, a session that
previously showed an `elided_placeholder` rejection on `run_terminal_command`
/ `write_file` re-sends (M901: 112 rejections in the corpus) shows the ticket
path in the marker instead, and the re-send count on that session drops to the
idempotent floor (M901's 5%) — measured with `tests/measure/args_elision.py`,
whose 50-event floor applies.

**Branch:** `plan/2026-10-08-def317-handoff` (created at this handoff,
clean). Model: `jlu/qwen3.8-27b` (free) unless the operator names otherwise.
No caps; fences on; `deadStream` on.
