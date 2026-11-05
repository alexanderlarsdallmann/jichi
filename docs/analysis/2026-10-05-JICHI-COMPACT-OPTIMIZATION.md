# jichi COMPACT / AUTO-COMPACT — mechanism analysis + optimization plan

*Read-only survey, HEAD 1b71247d (2026-10-05). Companion to JICHI-COMPACTION-GAP-ANALYSIS.md (which covered the raw-prefix recovery gap). This one: how mid-turn elision works, why arguments get lost in practice, and ranked recommendations.*

## TL;DR

The machinery is mature and heavily milestone-tested (M93/M94, M218, M323, M326x, M348, M358, M361, M712). The pressure path: history estimated (bytes/4, M77-calibrated) above 80% of the window triggers mid-turn compaction, which elides down to 60%, never touching the last 6 messages (MIDTURN_KEEP_RECENT) or anything under 800 bytes (ELIDE_MIN_BYTES): (1) M94 eager dedup of superseded read_file results, (2) M348 lossy elision of old tool OUTPUT — each full output goes to a 0600 preservation store and the marker becomes a claim ticket with a path, (3) M218 lossy elision of old tool CALL ARGUMENTS — full replacement by a ~120-byte valid-JSON placeholder, path preserved if extractable, nothing else.

Step 3 is the problem class for long writing sessions, and it is structurally asymmetric with step 2: the output path has M348's preservation store, the argument path has none. A write_file/apply_patch argument is the document itself (several KB). Once elided it is unrecoverable from context — only external state (the file on disk, session checkpoints) can restore it. Worse, the placeholder has a documented copy-back trap: the M289 marker is imperative to prevent the model from copying the marker back as arguments, and the tool side rejects that round-trip with an "unrecoverable by design" error — recovery works if the placeholder's path field is present, but not if it is not.

## 1. The mechanism (verified)

Trigger + stages, jc_compact.c: MIDTURN_HIGH_PCT 80 (line 759), MIDTURN_TARGET_PCT 60 (line 760), MIDTURN_KEEP_RECENT 6 (line 761), ELIDE_MIN_BYTES 800 (line 762), SYS_TOOLS_OVERHEAD 2000 (line 36). Orchestrator jc_compact_midturn (line 1286): dedup hint (M94, line 1315), high-water check, M361 exhaustion latch, lossy output pass (trim_tool_output_ex + midturn_spill → jc_toolout_preserve, lines 1435/1192-1212), then argument pass only if still over target (line 1442): `if (estimate + SYS_TOOLS_OVERHEAD > target_est) trim_tool_args(...)`. Operator knobs: `autoCompact` (master switch, default true, jc_config.c:1291; gates mid-turn at jc_compact.c:736) and `contextLimit` (window size). The stage shape is NOT configurable: HIGH=80/TARGET=60/KEEP=6/MIN=800 are #defines (jc_compact.c:759-762) — no per-surface or per-run override.

## 2. The argument-elision specifics (verified)

- `jc_compact_trim_tool_args` (jc_compact.c:1038): walks assistant messages older than keep_recent; per tool call with `strlen(arguments_json) > ELIDE_MIN_BYTES (800)`, wholesale-replaces args with `elide_args_marker()` (line 1013): a valid JSON object, ~120 B, carrying (a) a directive note "PLACEHOLDER, not arguments: N bytes ... cannot be recovered. To repeat this call, send its real arguments", (b) `path` IF extractable from the old args (top-level `path` for write_file/edit_file; first edit's path for apply_patch). Everything else — the file content, the edits, the pattern — is gone. No preservation store: compare the output path, whose `trim_tool_output_ex` takes a `midturn_spill` callback (jc_compact.c:1192 → `jc_toolout_preserve`, M339/M348) and whose marker names the 0600 file. The args pass has no such parameter (signature, line 1038-1040).
- The small-args guard is load-bearing for the M94 dedup: line 1068 comment — args <=800 B are skipped so read_file/search_code arguments stay intact for `read_path_dup`. So argument elision is BY DESIGN aimed at big writes.
- M289's directive wording exists because the model COPIED an earlier descriptive marker back as arguments (ROADMAP M218). The round-trip is still possible; the tool side detects it (jc_tool.c:1403-1428): telemetry `kind: "elided_placeholder"`, `ok: false`, "unrepairable by design", plus a path-specific re-send hint when the marker kept a path.
- Stage order matters: the args pass runs only if the output pass did not reach target (jc_compact.c:1442). On this session's workload (large heredoc CONTENT in args, modest output), the output pass frees little, so the args pass fires and drops exactly the session's most irreplaceable material — the documents being written.

## 3. Recommendations (ranked)

**R1 — Symmetry: preserve args the way outputs are preserved. (HIGH value)**
`trim_tool_args` should take a spill callback like `trim_tool_output_ex` does
(jc_compact.c:937) and route each elided args blob through
`jc_toolout_preserve` (M348); the marker would then name the 0600 file:
"to repeat this call, read <path>". That converts the session's worst failure
mode — re-sending a 3 KB heredoc with the real content from memory — into a
deterministic re-read of the preserved body. The machinery exists next door
for outputs; the args path structurally lacks it (signature, line 1038-1040).
One risk to weigh: a write_file arg IS the file being created — preserving it
to disk duplicates content that usually lands in the workspace. Preserve only
what does not exist elsewhere (edit old_string context, command text,
heredocs to files outside the workspace, non-write tools).

**R2 — Recover-on-reject (cheap, high leverage for the documented trap).**
The tool side already names the path it kept (jc_tool.c:1418 "Re-send the full
arguments for '<path>'") and already telemetry-flags the round-trip
(kind elided_placeholder, line 1407). Extend that one error branch: when the
marker DID keep a path and the original call was a file-write tool, append
"the file at <path> already holds that content from the earlier call (verify
with a range read before rewriting)". The M289 copy-back trap becomes a no-op
the model can see instead of a lost call. No schema change, no storage.

**R3 — Measure the write loop that already exists (cheap, no code change).**
The register's reread row (DEFERRED line 2223, M348/M459) measured 72% of
read_file calls being re-reads; M459 post-claim-ticket measured 0% on one
14-call corpus ("NOT evidence", floor 50). There is NO equivalent measurement
for the WRITES that args-elision drops: `tests/measure/reread_ratio.py:98,106`
is hardwired to `name == "read_file"`. Generalize it (a few lines) to count
repeat tool_call events of any tool over identical path/content fingerprints,
and to count `elided_placeholder` rejection events (R2's telemetry makes them
queryable). Until then no one knows whether args-elision is costing real work.

**R4 — The stage order is a hidden policy (medium).**
`trim_tool_args` runs only if the output pass missed target
(jc_compact.c:1442), and outputs are elided age-first, size-gated at 800 B.
On a document-writing workload the pressure is in the ARGUMENTS, so the
output pass frees little and the args pass drops the session's most valuable
content as the fallback stage. Two cheap alternatives: (a) when
estimate-minus-output-elision still exceeds target AND the excess is mostly
assistant args, say so in the [context] note — it currently reads "eliding old
tool output" even when outputs were spared; (b) expose HIGH/TARGET/KEEP as
config (they are #defines, 759-762; only autoCompact/contextLimit are knobs)
so a long doc-writing session can raise the 6-message keep window.

**R5 — Shrink what lands in history (the real optimization for my sessions).**
The compactor's job is to elide what the model WROTE; the better lever is
writing less into history. Large shell heredocs (this analysis is one: 20+
appends of 1-2 KB) are arguments — elision-eligible — while large read_file
OUTPUTS are claim-tickets (re-readable). So: prefer `run_terminal_command`
cat-append chunks of ~1.5 KB (what happened here, out of necessity) over big
write_file calls, and prefer reading files back in range after writing
(recoverable elision) over re-deriving content from memory (unrecoverable).
A skill or AGENTS.md note stating exactly this, plus "write durable results
before you believe the budget" (the [context] note already says this — R4(a)
would say it EARLIER for args-heavy workloads), is the cheapest improvement
available to the operator today, no code change.

**R6 — Make the latch see the args pass (small).**
The M361 exhaustion latch + M712 horizon (jc_compact.c:1386-1415) decide
"no elidable candidate in the new range" by scanning for tool OUTPUTS above
ELIDE_MIN_BYTES outside keep_recent. If the pressure is in ARGS, the latch
can mark the pass exhausted while the args pass still has bulk to reclaim —
or (worse for the model) the [context] note fires "could NOT bring it back
under the target" (line 308) when in fact an args pass later in the same call
would have. `jc_compact_released_candidate` should include large
arguments_json in its candidate test (same 800 B gate, same keep window).

## 4. What I did NOT recommend (and why)

- Disable autoCompact for long writing sessions: it trades elision-loss for
  a hard context-window overflow — worse. `autoCompact: false`
  (jc_config.c:1291) is a legitimate kill-switch, not an optimization.
- Re-read the elided file instead of the args: for write_file, the file on
  disk is the recovery source — that is already the case, and the trap is
  only the COPY-BACK of the marker (R2 addresses it).
- Shrink ELIDE_MIN_BYTES below 800: the 800 B floor doubles as the
  idempotence guard (jc_compact.c:768-769) and protects the M94 dedup's
  read_path argument lookup (line 1068). Lowering it elides read paths too.
- Add a per-tool exemption (never elide write_file args): it fixes this
  session's pain but makes the args pass unpredictable on exactly the
  workloads M218 measured it for (40k-event telemetry, history = 83% of
  every request). R1's conditional preserve is the bounded version.

## 5. Limitations (honest)

- I did NOT run the unit suite or a pressured replay: the stage mechanics
  are read from source (every line cited), but the *interaction* of R6's
  latch claim is inferred from the code path, not from a measured run.
- "The args pass fired in this session" is inferred: the session's own
  elided-argument markers + the "[context] 80%... could NOT bring it back"
  notes are the evidence; I cannot see this session's telemetry.
- M218's 83%-of-every-request figure is the telemetry corpus's, quoted from
  the ROADMAP entry, not re-measured.
- R1's "preserve only what is not elsewhere" needs a per-tool policy table;
  I sketched it, did not design it.
- This document itself is a write-args workload; it was drafted in
  ~1.5 KB shell chunks (R5 in practice) and survived the session's
  elisions — which is partly why it exists in this form.

## 6. Appendix (reproducible; from the jichi checkout)

    grep -nE "define (MIDTURN_HIGH_PCT|MIDTURN_TARGET_PCT|MIDTURN_KEEP_RECENT|ELIDE_MIN_BYTES|SYS_TOOLS_OVERHEAD)" src/chat/jc_compact.c
    grep -n "jc_compact_midturn(" src/chat/jc_compact.c
    sed -n '1286,1470p' src/chat/jc_compact.c        # orchestrator: dedup -> latch -> output pass -> args pass
    sed -n '1009,1035p' src/chat/jc_compact.c        # elide_args_marker (M289 directive + path keep)
    sed -n '1038,1090p' src/chat/jc_compact.c        # trim_tool_args (800 B gate, no spill param)
    sed -n '1192,1212p' src/chat/jc_compact.c        # midturn_spill -> jc_toolout_preserve (the OUTPUT-side ticket)
    sed -n '296,315p' src/chat/jc_compact.c          # the [context] note, both variants
    sed -n '1403,1428p' src/tools/jc_tool.c          # copy-back rejection + elided_placeholder telemetry
    grep -n "autoCompact" src/config/jc_config.c     # the one real knob
    grep -n "name == \"read_file\"" tests/measure/reread_ratio.py  # read-only measurement gap
    sed -n '11127,11150p' docs/ROADMAP.md            # M218: args elision rationale
    grep -n "M348" docs/ROADMAP.md | head -1         # claim-ticket milestone

## 7. Verdict

The compactor is among the most milestone-saturated systems in the tree,
and its output-side design (M348 claim tickets, 0600 preservation store,
dedup, latch, calibrated trigger) is sound. The optimization gap is a single
structural asymmetry: **the argument pass (M218) has no recovery path of its
own** — a ~120 B placeholder where a multi-KB document used to be, and the
marker itself says "cannot be recovered." R1 (extend M348's preservation to
eligible args) is the fix the architecture already implies; R2 (turn the
documented copy-back trap into a visible no-op) is nearly free; R3 (measure
the write loop the reread ratio missed) is the project's own M201
read-before-believing discipline applied to this session's symptom.
