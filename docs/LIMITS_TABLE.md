# Limits: the complete census (2026-11-21, master 0e301f54)

> **Who this is for.** A reader looking up one limit: what it bounds, how to change it, what happens when it fires,
> and where its number came from. Read [`LIMITS.md`](LIMITS.md) first: it explains the fields, the method, the
> rule and what was checked by hand.
>
> Every entry is a READING of the source by one of five agents, not a measurement; the supervisor checked the sourced
> time limits and a sample by hand. An entry you find wrong is a defect in this page: report it. Entries: 507.

**Changed since the census (2026-11-23).** The entries below still describe 0e301f54, line numbers included. Seven
of them describe behaviour that has changed since; [`LIMITS.md`](LIMITS.md) sections 4.4 and 6 say how:
- fixed by M1208, now failing closed: `src/net/jc_http.c:1150` (the redirect guard's status line),
  `src/util/jc_priv.c:241` and `src/util/jc_kinetic.c:108` (the 64-token guards), `src/util/jc_log.c:20`
  (`JC_REDACT_MAX`), `src/harness/jc_harness_push.c:73` (`PUSH_OUT_CAP`), `src/tools/jc_tool_parallel.c:73`
  (`JC_PAR_NEWDIR_MAX`, the hold decision);
- fixed by M1203-i: `src/harness/jc_harness_repin.c:616` (the re-pin children's 30-second kill, now
  `RR_CHILD_DEADLINE`, 600 s).

**How to read an entry.** The heading names where the limit is defined (file:line) and the limit itself. Below it:
- **Value**, and its **kind**: a cap (it changes the result when it fires), a fence (it bounds the blast radius) or
  a structural limit (an internal size), as [`LIMITS.md`](LIMITS.md) section 1 defines them;
- **Bounds**: what it limits, in plain words;
- **Change it with**: the config key or flag, or "hard-coded";
- **When it fires**: named, silent, a kill, an error value, or unknown, with where that happens;
- **Source**: where the number came from (measured / incident / reasoned / none / unclear), then the evidence.

An `X` before a location, or `[route X]` after a name, marks a limit that neither candidate list had and a
classifying agent added. Two of the five agents marked such limits, one in each of these ways; the other three did
not mark theirs, so an unmarked entry may be one too. The entries are grouped by directory, in the census's
order.

This page was one table until 2026-11-23. The table cut long cells short; each entry now carries its text in full
from the census data, and the figures in [`LIMITS.md`](LIMITS.md) are unchanged (507 limits; 290 caps; 335 with no
source).

## `include`

### `include/jc_agent.h:178` — JC_DENY_STOP_AT [route X]

- **Value:** 3 refusals · **cap**
- **Bounds:** consecutive refused approvals in one turn before jichi stops asking and ends the turn
- **Change it with:** hard-coded
- **When it fires:** named: tool result "The user has refused N call(s) in a row this turn; at 3 the turn ends" +
  WARN "N consecutive refusals this turn -- stopping" (src/chat/jc_agent.c:4263, :4330-4333)
- **Source:** incident: include/jc_agent.h:171-177 (an operator's transcript ran to ten prompts for one rename)

### `include/jc_agent.h:180` — JC_SUBAGENT_MIN_ITERS

- **Value:** 4 iterations · **cap**
- **Bounds:** floor of the per-depth tapered subagent iteration budget
- **Change it with:** hard-coded (the base is maxSubagentIters)
- **When it fires:** silent
- **Source:** none

### `include/jc_app.h:55` — JC_IMAGE_MAX_BYTES

- **Value:** 5 MiB · **fence**
- **Bounds:** one image file attached to a message (before base64)
- **Change it with:** hard-coded
- **When it fires:** error-value: JC_ERR_TOOBIG (src/chat/jc_app.c:697-710)
- **Source:** none

### `include/jc_app.h:60` — JC_DESIGN_MAX

- **Value:** 16 KiB · **cap**
- **Bounds:** total --design/--spec text injected into the system prompt (head kept)
- **Change it with:** hard-coded (a dynamic fit can lower it, docs/DESIGN_INPUT.md:116)
- **When it fires:** named: "[design truncated to fit the context budget]" (src/chat/jc_app.c:1925)
- **Source:** none: include/jc_app.h:57-59 (why capped; why 16 KB not stated)

### `include/jc_app.h:65` — JC_DESIGN_MAX_DOCS

- **Value:** 8 docs · **structural**
- **Bounds:** entries in the design-doc dedupe table
- **Change it with:** hard-coded
- **When it fires:** silent: docs past 8 are still loaded but no longer de-duplicated (src/chat/jc_app.c:1860)
- **Source:** reasoned: include/jc_app.h:61-64 ("Eight is far past the sane case")

### `include/jc_bg.h:37` — JC_BG_MAX

- **Value:** 8 processes · **fence**
- **Bounds:** concurrent run_in_background processes per session
- **Change it with:** hard-coded
- **When it fires:** named: starting a 9th returns an error (docs/BACKGROUND.md:31)
- **Source:** none

### `include/jc_bg.h:38` — JC_BG_BUF_MAX

- **Value:** 256 KiB · **cap**
- **Bounds:** in-memory captured output per background process (the head)
- **Change it with:** hard-coded
- **When it fires:** named: truncation is flagged and the last 8 KiB are delivered at the end
- **Source:** none

### `include/jc_bg.h:43` — JC_BG_TAIL_MAX

- **Value:** 8 KiB · **cap**
- **Bounds:** tail ring of an over-cap background output
- **Change it with:** hard-coded
- **When it fires:** named: delivered on the final read, after the head
- **Source:** reasoned: include/jc_bg.h:39-42 ("8 KiB covers a summary line and its context generously")

### `include/jc_bg.h:54` — JC_BG_SPOOL_MAX

- **Value:** 512 KiB · **cap**
- **Bounds:** on-disk spool file of a background process, applied at reap
- **Change it with:** hard-coded
- **When it fires:** silent: at reap the file is rewritten to its last 512 KiB, then unlinked
- **Source:** reasoned: include/jc_bg.h:44-53 (2x JC_BG_BUF_MAX); cap-at-reap is the operator's decision,
  docs/DECISIONS.md:3657

### `include/jc_calib.h:36` — JC_CALIB_MIN

- **Value:** 0.5 · **structural**
- **Bounds:** lowest accepted token-estimate calibration sample (and stored ratio)
- **Change it with:** hard-coded
- **When it fires:** silent: the sample is discarded (src/util/jc_calib.c:100-101)
- **Source:** reasoned: include/jc_calib.h:34-35 ("almost certainly a bad sample")

### `include/jc_calib.h:37` — JC_CALIB_MAX

- **Value:** 8.0 · **structural**
- **Bounds:** highest accepted calibration sample (and stored ratio)
- **Change it with:** hard-coded
- **When it fires:** silent: the sample is discarded (src/util/jc_calib.c:100-101)
- **Source:** reasoned: include/jc_calib.h:34-35

### `include/jc_calib.h:40` — JC_CALIB_WINDOW

- **Value:** 20 samples · **structural**
- **Bounds:** averaging window of the calibration ratio
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: include/jc_calib.h:38-39 (why a window; not why 20)

### `include/jc_calib.h:42` — JC_CALIB_MAX_ENTRIES

- **Value:** 128 models · **structural**
- **Bounds:** models in ~/.jichi.d/calibration.json
- **Change it with:** hard-coded
- **When it fires:** silent: a 129th model is never calibrated (src/util/jc_calib.c:106-107)
- **Source:** none

### `include/jc_compact.h:31` — JC_COMPACT_DEFAULT_LIMIT

- **Value:** 32,000 tokens · **cap**
- **Bounds:** context budget assumed when neither contextLimit nor the model's contextLength is set; compaction
  triggers at 80% of it
- **Change it with:** contextLimit (top level), contextLength (per model), --context-limit
- **When it fires:** unknown: auto-compaction logs at DEBUG (src/chat/jc_compact.c:728); other surfaces not traced
- **Source:** none: src/chat/jc_compact.c:262-265 (calls it "a guess about" the model)

### `include/jc_compact.h:36` — JC_COMPACT_SUMMARIZER_DEFAULT

- **Value:** 8192 tokens · **structural**
- **Bounds:** context assumed for the summarize-role model when it declares none; history is chunked to fit
- **Change it with:** the summarize model's contextLength
- **When it fires:** silent: more, smaller summarization chunks
- **Source:** none: docs/ROADMAP.md:5664-5670 (M30: chunking fixed an HTTP 400 from an oversized summarize call;
  8192 not derived)

### `include/jc_confbench.h:49` — JC_CONFBENCH_MAX_ITEMS

- **Value:** 20 items · **structural**
- **Bounds:** items in the configuration bench report
- **Change it with:** hard-coded
- **When it fires:** silent: items past 20 are not added (src/util/jc_confbench.c:14)
- **Source:** none

### `include/jc_config.h:343` — JC_ROUTE_CONTEXT_PCT [route X]

- **Value:** 75 % · **structural**
- **Bounds:** fast-tier context use at which routing escalates to the strong model
- **Change it with:** routing.escalateOnContext (true = 75, or a number)
- **When it fires:** unknown (not traced)
- **Source:** reasoned: include/jc_config.h:339-342 (deliberately below the 80% compaction trigger)

### `include/jc_config.h:343` — routing.escalateOnContext (JC_ROUTE_CONTEXT_PCT)

- **Value:** 75 % of the fast tier's window · **cap**
- **Bounds:** context pressure at which routing moves a turn from the fast to the strong model
- **Change it with:** config routing.escalateOnContext (a number, or a bool: true = 75, false = 0)
- **When it fires:** unknown (escalation path not traced)
- **Source:** reasoned: include/jc_config.h:339-342 ("deliberately below the compaction trigger's 80%", M288)

### `include/jc_config.h:352` — JC_ROUTE_DEESCALATE_GAP [route X]

- **Value:** 20 points · **structural**
- **Bounds:** hysteresis: routing returns to the fast tier only below 55% (75 - 20)
- **Change it with:** hard-coded
- **When it fires:** unknown (not traced)
- **Source:** reasoned: include/jc_config.h:345-351 (avoids escalate/compact/de-escalate oscillation that discards
  the cached prefix)

### `include/jc_config.h:416` — JC_PAR_SUP_NO_EDIT_SEC

- **Value:** 3600 s · **cap**
- **Bounds:** seconds a spawn_parallel worker may go without a new edit (after its first) before it is flagged
- **Change it with:** parallelSupervise.noEditSec
- **When it fires:** named: flag on the status board, the status reply and a worker_flag telemetry event; the
  default action "report" stops nothing
- **Source:** measured: docs/PARALLEL.md:383 ("60 min: 7 of 563 stalled"); include/jc_config.h:413-415

### `include/jc_config.h:417` — JC_PAR_SUP_PLACEHOLDERS [route X]

- **Value:** 2 placeholders · **cap**
- **Bounds:** copied placeholders before a spawn_parallel worker is flagged
- **Change it with:** parallelSupervise.placeholders
- **When it fires:** named: flag on board, status and worker_flag telemetry; default action "report" stops nothing
- **Source:** measured: docs/PARALLEL.md:384 ("4 of 880 looped on placeholders")

### `include/jc_config.h:418` — JC_PAR_SUP_GRACE_SEC

- **Value:** 60 s · **fence**
- **Bounds:** grace between a flagged worker's soft stop and its hard stop
- **Change it with:** parallelSupervise.graceSec
- **When it fires:** kill (hard stop), only under action wrapup/stop (src/tools/jc_tool_parallel.c:2211-2217)
- **Source:** none: docs/PARALLEL.md:386 (its measurement column reads "—")

### `include/jc_config.h:818` — JC_TOOL_PROFILE_AUTO_BELOW [route X]

- **Value:** 12,000 tokens · **cap**
- **Bounds:** known effective context below which toolProfile "auto" advertises only the lean core tools
- **Change it with:** toolProfile (full/core/auto), --tool-profile
- **When it fires:** unknown: doctor reports the resolved profile (docs/ROADMAP.md:7144); not traced further
- **Source:** none: docs/ROADMAP.md:7132-7140 (on an 8192-token endpoint tool definitions were the binding floor;
  12000 not derived)

### `include/jc_config.h:828` — JC_DEFAULT_MAX_TOKENS

- **Value:** 4096 tokens · **cap**
- **Bounds:** max_tokens sent on every model request when neither maxTokens nor contextLength is known
- **Change it with:** maxTokens or contextLength (per model)
- **When it fires:** named: a reply cut at the length limit is warned about (tests/smoke/cut_reply_warnings.sh:83);
  a delegate is asked once to continue (M859)
- **Source:** none: docs/ROADMAP.md:7346-7353 (an omitted max_tokens caused HTTP 400 on every request, so one is
  always sent; 4096 itself not derived)

### `include/jc_constraint.h:74` — JC_CONSTRAINT_MAX

- **Value:** 64 constraints · **fence**
- **Bounds:** enforced constraints held per workspace (authored + inferred)
- **Change it with:** hard-coded
- **When it fires:** silent: a 65th constraint is not added (src/chat/jc_app.c:1709 returns 0) and constraints.md
  entries past 64 are not loaded (src/chat/jc_app.c:1654-1655)
- **Source:** none

### `include/jc_constraint.h:75` — JC_CONSTRAINT_SOURCE_MAX

- **Value:** 200 bytes · **structural**
- **Bounds:** quoted source sentence shown for an inferred constraint
- **Change it with:** hard-coded
- **When it fires:** named: "..." where cut (src/chat/jc_constraint.c:1984-1991)
- **Source:** none

### `include/jc_context.h:84` — JC_TOOLUSE_MIN_SESSIONS

- **Value:** 3 sessions · **structural**
- **Bounds:** distinct telemetry sessions before doctor advises on unused tools
- **Change it with:** hard-coded
- **When it fires:** named: below it doctor prints an OK line with the counts (src/main.c:16999-17000)
- **Source:** reasoned: include/jc_context.h:79-83 ("defensible rather than derived")

### `include/jc_context.h:85` — JC_TOOLUSE_MIN_CALLS

- **Value:** 20 calls · **structural**
- **Bounds:** recorded tool calls before doctor advises on unused tools
- **Change it with:** hard-coded
- **When it fires:** named: as above (src/main.c:16999-17000)
- **Source:** reasoned: include/jc_context.h:79-83; docs/DOCTOR.md:191 ("defensible round numbers, not derived
  ones")

### `include/jc_control.h:45` — JC_CONTROL_LINE_MAX

- **Value:** 16,384 bytes · **fence**
- **Bounds:** control-socket text: a command's journal text, the client's reply buffer, the lead's pending-inject
  cap (4x)
- **Change it with:** hard-coded
- **When it fires:** silent: journal text cut (src/chat/jc_control.c:268); the client stops reading a reply at 16
  KiB (src/main.c:18384)
- **Source:** none

### `include/jc_control.h:52` — JC_CONTROL_LINE_MAX_INJECT

- **Value:** 131,072 bytes · **fence**
- **Bounds:** one request line read from the control socket (any verb)
- **Change it with:** hard-coded
- **When it fires:** named: an over-long line is cut and refused as "not a JSON object" (src/chat/jc_control.c:392)
- **Source:** incident: include/jc_control.h:46-51 (harness driver cell 14 sends a ~70,000-byte inject-task line)

### `include/jc_control.h:58` — JC_CONTROL_QUEUE_MAX

- **Value:** 65,536 bytes · **cap**
- **Bounds:** total inject-task text queued for one pool task
- **Change it with:** hard-coded
- **When it fires:** named: "task %d's queue is full" (src/chat/jc_control.c:403)
- **Source:** reasoned: src/tools/jc_tool_parallel.c:2032 ("65536, the pipe capacity"); include/jc_control.h:53-57
  (mirrors the lead's M1045 cap)

### `include/jc_control.h:62` — JC_POOL_MAX_TASKS

- **Value:** 32 tasks · **cap**
- **Bounds:** tasks in one spawn_parallel batch (status snapshot size)
- **Change it with:** hard-coded
- **When it fires:** unknown: enforced by JC_PAR_MAX_TASKS (src/tools/jc_tool_parallel.c:63), outside this shard
- **Source:** none

### `include/jc_convert.h:49` — JC_IR_MAX_MODELS

- **Value:** 64 models · **cap**
- **Bounds:** models jichi-convert carries over from a foreign config
- **Change it with:** hard-coded
- **When it fires:** silent: jc_ir_new_model returns NULL and callers break with no warning
  (src/convert/jc_convert_core.c:46, jc_convert_continue.c:303-306)
- **Source:** none

### `include/jc_convert.h:50` — JC_IR_MAX_ROLES

- **Value:** 16 roles · **cap**
- **Bounds:** roles per converted model
- **Change it with:** hard-coded
- **When it fires:** silent (src/convert/jc_convert_core.c:88)
- **Source:** none

### `include/jc_convert.h:51` — JC_IR_MAX_MCP

- **Value:** 32 servers · **cap**
- **Bounds:** MCP servers carried over by jichi-convert
- **Change it with:** hard-coded
- **When it fires:** silent (src/convert/jc_convert_core.c:60; callers break)
- **Source:** none

### `include/jc_convert.h:52` — JC_IR_MAX_LSP

- **Value:** 16 servers · **cap**
- **Bounds:** LSP servers carried over by jichi-convert
- **Change it with:** hard-coded
- **When it fires:** silent (src/convert/jc_convert_core.c:74)
- **Source:** none

### `include/jc_convert.h:53` — JC_IR_MAX_DOCS

- **Value:** 32 docs · **cap**
- **Bounds:** docs entries carried over by jichi-convert
- **Change it with:** hard-coded
- **When it fires:** silent (src/convert/jc_convert_continue.c:96, :440)
- **Source:** none

### `include/jc_convert.h:54` — JC_IR_MAX_ASSETS

- **Value:** 256 assets · **cap**
- **Bounds:** files (rules, commands, agents) copied by jichi-convert
- **Change it with:** hard-coded
- **When it fires:** silent (src/convert/jc_convert_assets.c:20)
- **Source:** none

### `include/jc_convert.h:57` — JC_IR_MAX_LIST

- **Value:** 64 entries · **cap**
- **Bounds:** per-item string lists in a conversion: args, env, headers, instructions, permission entries, warnings
- **Change it with:** hard-coded
- **When it fires:** silent (e.g. src/convert/jc_convert_claude.c:72, :82)
- **Source:** none

### `include/jc_daemon.h:39` — JC_DAEMON_MAX_LINE

- **Value:** 1 MiB · **fence**
- **Bounds:** one request line a client sends the warm daemon
- **Change it with:** hard-coded
- **When it fires:** named: error {"code":"limit.line"} (src/main.c:17480, :17809)
- **Source:** reasoned: include/jc_daemon.h:27-38 ("a pasted file in a prompt fits comfortably; a runaway does not";
  M528 closed an unbounded read)

### `include/jc_delegreport.h:60` — JC_DELEG_TOOL_MAX

- **Value:** 64 bytes · **structural**
- **Bounds:** name of a delegate's last failing tool in its report
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `include/jc_delegreport.h:61` — JC_DELEG_MSG_MAX

- **Value:** 200 bytes · **structural**
- **Bounds:** failure message, files-changed list and run error in a delegate's report to the lead
- **Change it with:** hard-coded
- **When it fires:** silent: cut by jc_snprintf (e.g. src/tools/jc_tool_parallel.c:4682)
- **Source:** none

### `include/jc_diskguard.h:42` — JC_DISKGUARD_INTERVAL_MS

- **Value:** 10,000 ms · **structural**
- **Bounds:** how often the disk-space fence re-measures at a tool boundary
- **Change it with:** hard-coded (the floor itself is diskFloorMb)
- **When it fires:** silent: a fill between samples is seen at the next sample or a forced check
- **Source:** none: include/jc_diskguard.h:21-26 (why the check is throttled by time; not why 10 s)

### `include/jc_envelope.h:34` — JC_ENV_SIG_MAX

- **Value:** 160 bytes · **structural**
- **Bounds:** a verify-failure signature (first "error:" line) used to detect a run stuck on one error
- **Change it with:** hard-coded
- **When it fires:** silent: compared on the first 159 bytes
- **Source:** none

### `include/jc_envelope.h:90` — JC_ENV_SCOPE_JOURNAL_MAX

- **Value:** 10 globs · **cap**
- **Bounds:** edit-scope globs written into the run journal
- **Change it with:** hard-coded
- **When it fires:** silent: globs past 10 are omitted from the journal line (src/chat/jc_agent.c:6623-6624)
- **Source:** reasoned: include/jc_envelope.h:86-89 ("Ten is far above any real fence seen in this project")

### `include/jc_eventlog.h:46` — JC_EVENTLOG_CHILD_SEQ_STRIDE [route X]

- **Value:** 1,000,000 events · **structural**
- **Bounds:** event sequence band per spawn_parallel child
- **Change it with:** hard-coded
- **When it fires:** silent: a child past 1,000,000 events overlaps the next band
- **Source:** measured: include/jc_eventlog.h:41-45 (the whole weekend drive emitted ~50,000 events)

### `include/jc_eventlog.h:49` — JC_EVENTLOG_TEXT_MAX

- **Value:** 16 KiB · **cap**
- **Bounds:** each content field (prompt, response, tool I/O) in full-tier telemetry and in a parallel child's
  transcript
- **Change it with:** hard-coded (the tier is logging.level)
- **When it fires:** named: "...[+N B]" marker at a UTF-8 boundary (docs/ROADMAP.md:5384)
- **Source:** none

### `include/jc_fim.h:27` — JC_FIM_DEFAULT_BUDGET

- **Value:** 4000 bytes per side · **cap**
- **Bounds:** prefix and suffix context sent to the autocomplete model by `fim`
- **Change it with:** hard-coded
- **When it fires:** silent: the cursor-nearest 4000 bytes of each side are kept (src/main.c:11778-11785)
- **Source:** none

### `include/jc_glossary.h:27` — JC_GLOSSARY_MAX

- **Value:** 8 KiB · **cap**
- **Bounds:** combined global + project glossary injected into the prompt (tail kept)
- **Change it with:** hard-coded
- **When it fires:** silent: the tail is kept with no warning (src/chat/jc_glossary.c:109-112)
- **Source:** none

### `include/jc_harness_handoff.h:45` — JC_HARNESS_HANDOFF_OUT_CAP

- **Value:** 6144 bytes (6000-byte cut + marker) · **cap**
- **Bounds:** the "What the previous run left you" section in a harness run's prompt
- **Change it with:** hard-coded
- **When it fires:** named: cut back to the last newline before 6000 bytes, with a marker line
  (include/jc_harness_handoff.h:30-33)
- **Source:** none: include/jc_harness_handoff.h:30-33 (ports the reference handoff.py's "behaviour 10"; no
  rationale for 6000)

### `include/jc_http.h:289` — JC_SOCKWATCH_MAX

- **Value:** 8 sockets · **structural**
- **Bounds:** sockets one transfer's watch records (to tell a TLS-handshake timeout from a TCP one)
- **Change it with:** hard-coded
- **When it fires:** silent: a 9th socket is not recorded (src/net/jc_http.c:587)
- **Source:** none

### `include/jc_http.h:44` — JC_HTTP_STALL_LOW_SPEED

- **Value:** 1 byte/s · **cap**
- **Bounds:** throughput below which the stall window counts as silence (CURLOPT_LOW_SPEED_LIMIT)
- **Change it with:** hard-coded
- **When it fires:** named (with timeouts.stall): "connected, then the stream stalled (timeouts.stall)"
  (src/net/jc_http.c:480)
- **Source:** none: include/jc_http.h:36-43 (the "genuine progress resets the timer" premise was measured wrong: a
  prefilling server sends 0 bytes)

### `include/jc_http.h:45` — JC_HTTP_CONNECT_TIMEOUT_DEFAULT

- **Value:** 30 s · **fence**
- **Bounds:** TCP connect + TLS handshake of a model call and of every side HTTP call
- **Change it with:** timeouts.connect (global or per model), --timeout-connect; side calls take CLI > global > 30
  (src/net/net_util.c:86)
- **When it fires:** named: "could not connect within %lds (timeouts.connect)" (src/net/jc_http.c:460)
- **Source:** incident: commit fec3d643 (M326v) and docs/ROADMAP.md:18108-18111: at 10 s, 14% of 17,365 model calls
  failed at 10,003 ms; raised 10 -> 30

### `include/jc_http.h:46` — JC_HTTP_STALL_TIMEOUT_DEFAULT

- **Value:** 30 s · **cap**
- **Bounds:** seconds a streamed model response may stay under 1 byte/s, server prefill included
- **Change it with:** timeouts.stall (global or per model), --timeout-stall <s|off>; 0 turns it off
- **When it fires:** named: "connected, then the stream stalled (timeouts.stall)" (src/net/jc_http.c:480); doctor
  warns when a model's recorded latency exceeds it
- **Source:** none: docs/internal/2026-10-15-stall-default-handoff.md:10 ("chosen at M22a ... with no latency
  measurement behind the number"); include/jc_http.h:36-43 (measured afterwards: a ~12k-token prefill takes ~36 s
  and the default cut 4 of 4 first attempts)

### `include/jc_http.h:59` — JC_HTTP_DEFAULT_RESP_BYTES

- **Value:** 32 MiB · **fence**
- **Bounds:** body of every collected (non-streaming) HTTP response whose caller names no cap: embeddings, model
  lists, image/audio/transcription, web search, MCP-over-HTTP, compaction, one-shot
- **Change it with:** hard-coded (callers with their own key ride the same mechanism: audioGenMaxBytes,
  imageGenMaxBytes)
- **When it fires:** named where the caller reads req.overflow: web search names the limit
  (src/tools/jc_tool_websearch.c:210-219); other callers not traced
- **Source:** reasoned: include/jc_http.h:48-58 (reuses fetch_url's 32 MiB OOM backstop, "bounded for the 256 MB
  board"); hardening finding C-06, M1092

### `include/jc_inhist.h:37` — JC_INHIST_MAX

- **Value:** 500 entries · **cap**
- **Bounds:** TUI input-history lines recalled; the file is rewritten to the newest 500 once it passes 1,000
- **Change it with:** inputHistory: false turns the file off; the count is hard-coded
- **When it fires:** silent
- **Source:** none

### `include/jc_inhist.h:38` — JC_INHIST_LINE_MAX

- **Value:** 2048 bytes · **cap**
- **Bounds:** one input line saved to the history file
- **Change it with:** hard-coded
- **When it fires:** silent: a longer line is not written (src/util/jc_inhist.c:135)
- **Source:** none

### `include/jc_insights.h:72` — JC_INSIGHTS_RECENT_SEC

- **Value:** 259,200 s (3 days) · **cap**
- **Bounds:** recency window: a failing tool with no calls in the last 3 days of the log is not reported by learn
  analyze
- **Change it with:** hard-coded
- **When it fires:** silent: older failures are skipped
- **Source:** reasoned: include/jc_insights.h:68-71 ("Three days spans a normal gap between work sessions")

### `include/jc_log.h:46` — JC_REDACT_MIN

- **Value:** 6 bytes · **fence**
- **Bounds:** shortest registered secret the redactor scrubs from logs, dumps and transcripts
- **Change it with:** hard-coded
- **When it fires:** silent: a shorter secret is not registered and never redacted (src/util/jc_log.c:186-187)
- **Source:** reasoned: include/jc_log.h:44-45 ("too generic to safely scrub")

### `include/jc_lsp.h:53` — JC_LSP_MAX_BODY

- **Value:** 64 MiB · **fence**
- **Bounds:** one LSP message body (Content-Length) from a language server
- **Change it with:** hard-coded
- **When it fires:** silent: the frame is dropped and the reader resyncs, with no log
  (src/lsp/jc_lsp_proto.c:128-136)
- **Source:** reasoned: include/jc_lsp.h:42-45 ("far above any real LSP payload ... single-digit MB")

### `include/jc_lsp.h:63` — JC_LSP_MAX_HEADER

- **Value:** 64 KiB · **fence**
- **Bounds:** LSP header block before the blank line
- **Change it with:** hard-coded
- **When it fires:** silent: buffer dropped and the reader resyncs (src/lsp/jc_lsp_proto.c:111-116)
- **Source:** reasoned: include/jc_lsp.h:55-62 ("A real header block is two short lines; 64 KB is orders above
  that")

### `include/jc_memory.h:26` — JC_MEMORY_MAX

- **Value:** 8 KiB · **cap**
- **Bounds:** part of .jichi/memory.md injected into the system prompt (tail kept)
- **Change it with:** hard-coded
- **When it fires:** named: WARN "memory: .jichi/memory.md is N KB; only the most recent 8 KB is loaded ...
  (dropped: ...)" (src/chat/jc_memory.c:193-201)
- **Source:** none

### `include/jc_parallel.h:151` — JC_PAR_MAX_CALLS_CAP

- **Value:** 1,000,000 calls · **structural**
- **Bounds:** a task's model-supplied max_tool_calls
- **Change it with:** hard-coded
- **When it fires:** silent: larger values are clamped (src/tools/jc_parallel.c:282-284)
- **Source:** none

### `include/jc_parallel.h:202` — JC_PAR_VFAIL_LINES [route X]

- **Value:** 12 lines · **cap**
- **Bounds:** failing-verify lines shown to the lead for a write child
- **Change it with:** hard-coded
- **When it fires:** named: a count of the matching lines is given (include/jc_parallel.h:171)
- **Source:** none

### `include/jc_parallel.h:203` — JC_PAR_VFAIL_LINE_MAX

- **Value:** 200 bytes · **cap**
- **Bounds:** one line of a write child's failed-verify excerpt shown to the lead
- **Change it with:** hard-coded
- **When it fires:** named: a cut line ends in " ..." (src/tools/jc_parallel.c:436-437)
- **Source:** none

### `include/jc_parallel.h:67` — JC_PAR_PINNED_MAX

- **Value:** 600 bytes · **structural**
- **Bounds:** a spawn_parallel child's list of kept-before-discard refs in its done line
- **Change it with:** hard-coded
- **When it fires:** silent: refs that do not fit are skipped (src/tools/jc_tool_parallel.c:1021)
- **Source:** none

### `include/jc_path.h:27` — JC_PATH_MAX

- **Value:** 4096 bytes · **structural**
- **Bounds:** canonical path buffers across the tree (67 use sites)
- **Change it with:** hard-coded
- **When it fires:** unknown: realpath-based sites fail with an error; snprintf sites may truncate
- **Source:** none

### `include/jc_peercap.h:35` — JC_PEER_LINE_MAX

- **Value:** 8 MiB · **fence**
- **Bounds:** one newline-delimited message from an MCP stdio server or an ACP client
- **Change it with:** hard-coded
- **When it fires:** named: "mcp: server sent N bytes with no newline (cap N) -- refusing the message"
  (src/mcp/jc_mcp_stdio.c:255-261); the ACP twin at src/acp/jc_acp.c:187-192
- **Source:** reasoned: include/jc_peercap.h:16-18 ("a large tool result is a few hundred KB")

### `include/jc_platform.h:150` — JC_READ_FILE_MAX

- **Value:** 64 MiB · **fence**
- **Bounds:** one jc_read_file slurp (rules, memory, configs, sessions, any file read whole)
- **Change it with:** hard-coded
- **When it fires:** error-value: JC_ERR_TOOBIG; what the user sees depends on the caller (memory/rules writers
  refuse with a named error since M1100)
- **Source:** reasoned: include/jc_platform.h:147-149 ("Far above any plausible source file (M24)")

### `include/jc_prefix.h:42` — JC_PREFIX_CHURN_STREAK [route X]

- **Value:** 3 builds · **structural**
- **Bounds:** consecutive changed system-prompt builds before the once-per-session cache-churn warning
- **Change it with:** hard-coded
- **When it fires:** named: one warning per session
- **Source:** none

### `include/jc_proc.h:199` — JC_PROC_DEF_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** kill deadline of a child run through jc_proc_capture when the caller passes timeout <= 0 (hooks, user
  tools, harness gate/repin commands, git helpers)
- **Change it with:** per caller (hooks: the hook's "timeout"); otherwise hard-coded
- **When it fires:** kill: the process group is killed and -2 returned, the same value as an abort
  (src/util/jc_proc.c:779-790)
- **Source:** none

### `include/jc_relay.h:115` — JC_RELAY_BUDGET_MIN

- **Value:** 25 tool calls · **cap**
- **Bounds:** floor of the V2 relay leg-budget controller
- **Change it with:** hard-coded
- **When it fires:** unknown (not traced)
- **Source:** none: docs/proposals/2026-10-context-relay.md:782 (states B_min = 25; no derivation)

### `include/jc_relay.h:116` — JC_RELAY_BUDGET_HIGH_PCT

- **Value:** 80 % · **cap**
- **Bounds:** share of the context window at which the controller plans a relay leg to end
- **Change it with:** hard-coded
- **When it fires:** unknown (not traced)
- **Source:** reasoned: docs/proposals/2026-10-context-relay.md:735 ("the high-water"; the same 80% the compaction
  trigger uses; P2 at :99 measured leads sawtoothing at 60-80%)

### `include/jc_relay.h:117` — JC_RELAY_BUDGET_RESERVE

- **Value:** 4,000 tokens · **structural**
- **Bounds:** tokens held back for the handover call when a leg's budget is computed
- **Change it with:** hard-coded
- **When it fires:** unknown (not traced)
- **Source:** none: docs/proposals/2026-10-context-relay.md:735 (given as "e.g. 4,000 tokens")

### `include/jc_relay.h:156` — JC_RELAY_BATON_TOPN [route X]

- **Value:** 20 entries · **cap**
- **Bounds:** entries printed per top-N list in relay handover notes
- **Change it with:** hard-coded
- **When it fires:** unknown (not traced)
- **Source:** reasoned: include/jc_relay.h:151-155 ("20 keeps a ten-thousand-file run under the default 8 KB cap")

### `include/jc_repomap.h:26` — JC_REPOMAP_MAX

- **Value:** 12 KiB · **cap**
- **Bounds:** rendered repository map injected into the system prompt
- **Change it with:** repoMapLimit (0 = built-in)
- **When it fires:** named: "... (truncated; N more files)" (src/index/jc_repomap.c:814)
- **Source:** none

### `include/jc_repomap.h:27` — JC_REPOMAP_FILE_SYMS [route X]

- **Value:** 16 symbols · **cap**
- **Bounds:** symbols rendered per file in the repo map
- **Change it with:** hard-coded
- **When it fires:** named: ", ..." (src/index/jc_repomap.c:808)
- **Source:** none

### `include/jc_repomap.h:28` — JC_REPOMAP_SCAN_CAP

- **Value:** 64 symbols · **structural**
- **Bounds:** symbols the heuristic scanner extracts from one file
- **Change it with:** hard-coded
- **When it fires:** silent (src/index/jc_repomap.c:70)
- **Source:** none

### `include/jc_repomap.h:29` — JC_REPOMAP_MAX_FILES

- **Value:** 4000 files · **cap**
- **Bounds:** source files the repo-map walk collects
- **Change it with:** hard-coded
- **When it fires:** silent: the walk stops; the "N more files" count covers only collected files
  (src/index/jc_repomap.c:673-679)
- **Source:** none

### `include/jc_reread.h:36` — JC_READ_RECS_MAX

- **Value:** 256 records · **structural**
- **Bounds:** (path, range) read records kept per session for the re-read advisory
- **Change it with:** hard-coded
- **When it fires:** silent: new ranges are not recorded (src/chat/jc_app.c:288)
- **Source:** none: include/jc_reread.h:31-35 (fails toward a missed advisory, never a false one; why 256 not
  stated)

### `include/jc_selfheal.h:23` — JC_SELFHEAL_MAX_PATHS

- **Value:** 24 files · **structural**
- **Bounds:** distinct files the per-turn re-edit watch tracks
- **Change it with:** hard-coded
- **When it fires:** silent: a 25th file is not tracked (src/util/jc_selfheal.c:27)
- **Source:** none

### `include/jc_selfheal.h:24` — JC_SELFHEAL_PATH_MAX

- **Value:** 256 bytes · **structural**
- **Bounds:** path key stored per tracked file
- **Change it with:** hard-coded
- **When it fires:** silent: path cut to 255 bytes (src/util/jc_selfheal.c:30-31)
- **Source:** none

### `include/jc_selfheal.h:25` — JC_SELFHEAL_REDO_THRESHOLD

- **Value:** 3 edits · **structural**
- **Bounds:** edits of one file in a turn before a one-time "step back" nudge
- **Change it with:** hard-coded
- **When it fires:** named: a nudge appended to the tool result (src/chat/jc_agent.c:4997)
- **Source:** none: include/jc_selfheal.h:6-10 (zigodot dogfooding, docs/ANECDOTES.md #8/#9, motivates the watch; 3
  is not derived)

### `include/jc_sessmeta.h:41` — JC_SESSMETA_WS [route X]

- **Value:** 1024 bytes · **structural**
- **Bounds:** workspace path read by the fast session-listing scanner
- **Change it with:** hard-coded
- **When it fires:** silent: cut at 1023 bytes (put_byte, src/session/jc_sessmeta.c:11-19)
- **Source:** reasoned: include/jc_sessmeta.h:35-36 ("generous for what jc_session_save writes")

### `include/jc_setup.h:102` — JC_SETUP_MAX_LSP

- **Value:** 4 servers · **structural**
- **Bounds:** LSP servers the setup wizard writes
- **Change it with:** hard-coded
- **When it fires:** silent (src/setup/jc_setup.c:474)
- **Source:** none

### `include/jc_setup.h:103` — JC_SETUP_MAX_MCP

- **Value:** 4 servers · **structural**
- **Bounds:** MCP servers the setup wizard writes
- **Change it with:** hard-coded
- **When it fires:** silent (src/setup/jc_setup.c:489)
- **Source:** none

### `include/jc_setup.h:104` — JC_SETUP_MAX_DOCS

- **Value:** 4 entries · **structural**
- **Bounds:** docs entries the setup wizard writes
- **Change it with:** hard-coded
- **When it fires:** silent (src/setup/jc_setup.c:504)
- **Source:** none

### `include/jc_snapshot.h:29` — JC_SNAPSHOT_MAX_LIST

- **Value:** 200 checkpoints · **cap**
- **Bounds:** checkpoints held in the in-session undo stack and the CLI listing
- **Change it with:** hard-coded
- **When it fires:** silent: the oldest is dropped from the in-memory index; the shadow repo keeps the commits
  (src/snapshot/jc_snapshot.c:729-737)
- **Source:** none

### `include/jc_sound.h:32` — JC_SOUND_PLAY_TIMEOUT_DEFAULT

- **Value:** 120 s · **cap**
- **Bounds:** wall time of one play_audio player process
- **Change it with:** sound.playTimeoutSeconds (0 = 120)
- **When it fires:** kill (src/tools/jc_tool_sound.c:79-83)
- **Source:** none

### `include/jc_sound.h:33` — JC_SOUND_RECORD_DEFAULT

- **Value:** 60 s · **cap**
- **Bounds:** ceiling on a record_audio request when recordMaxSeconds is unset
- **Change it with:** sound.recordMaxSeconds
- **When it fires:** silent: the requested seconds are clamped (src/util/jc_sound.c:16; the tool schema says
  "clamped to the configured maximum", src/tools/jc_tool_sound.c:125)
- **Source:** none

### `include/jc_sound.h:34` — JC_SOUND_RECORD_CAP

- **Value:** 600 s · **fence**
- **Bounds:** hard ceiling on recordMaxSeconds itself
- **Change it with:** hard-coded
- **When it fires:** silent: a larger recordMaxSeconds is clamped to 600 (src/util/jc_sound.c:17)
- **Source:** none

### `include/jc_sse.h:38` — JC_SSE_FIELD_MAX

- **Value:** 4 MiB · **fence**
- **Bounds:** one SSE line, or one event's data payload, from a model server
- **Change it with:** hard-coded
- **When it fires:** silent: bytes past the cap are dropped and `overflowed` is set, but no code reads the flag
  (src/net/jc_sse.c:66-72, :111-114)
- **Source:** reasoned: include/jc_sse.h:34-37 ("A well-behaved server never approaches this")

### `include/jc_suggest.h:44` — JC_SUGGEST_SYS_CAP

- **Value:** 2048 bytes · **structural**
- **Bounds:** Ctrl-G suggestion system-prompt buffer
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: include/jc_suggest.h:42-43 ("a buffer size that always fits")

### `include/jc_telemetry.h:410` — JC_DOCTOR_LAT_MIN_CALLS

- **Value:** 5 calls · **structural**
- **Bounds:** recorded calls a model needs before doctor warns that its slowest call exceeds the stall timeout
- **Change it with:** hard-coded
- **When it fires:** silent: below 5 calls no warning (src/main.c:15175)
- **Source:** reasoned: docs/ROADMAP.md:34658-34660 ("One slow request is not a tail")

### `include/jc_testparse.h:33` — JC_TEST_MAX_FAILURES

- **Value:** 50 failures · **cap**
- **Bounds:** failures kept in a parsed test report
- **Change it with:** hard-coded
- **When it fires:** named: further failures are counted in `truncated` (src/util/jc_testparse.c:128-129)
- **Source:** none: include/jc_testparse.h:31-32 (why bounded; not why 50)

### `include/jc_testparse.h:34` — JC_TEST_MAX_MSG

- **Value:** 512 bytes · **structural**
- **Bounds:** one failure message in a parsed test report
- **Change it with:** hard-coded
- **When it fires:** silent: message cut to the buffer (copy_range, src/util/jc_testparse.c:126-137)
- **Source:** none

### `include/jc_tool.h:21` — JC_SILENT_NOTE_AT [route X]

- **Value:** 3 runs · **structural**
- **Bounds:** silent successes of one command in a turn before its result says so
- **Change it with:** hard-coded
- **When it fires:** named: note in the tool result
- **Source:** none: include/jc_tool.h:14-20 (six workers re-ran a passing zig build test; 3 not derived)

### `include/jc_toolcaps.h:24` — JC_CAP_READ_DEFAULT

- **Value:** 256 KiB · **cap**
- **Bounds:** read_file output shown to the model
- **Change it with:** readMaxBytes (0 = built-in; --lite makes it 64 KiB, src/config/jc_config.c:1477)
- **When it fires:** named: "... [output truncated] (read lines 1-N of M; readMaxBytes=262144)"
  (docs/TOOL_OUTPUT_COST.md:203-204)
- **Source:** reasoned: docs/TOOL_OUTPUT_COST.md:308-314 (a safety bound: "A 256 KB read cap is right for 'do not
  die'")

### `include/jc_toolcaps.h:25` — JC_CAP_RUN_DEFAULT

- **Value:** 64 KiB · **cap**
- **Bounds:** run_terminal_command / run_tests output shown to the model
- **Change it with:** runMaxBytes (--lite: 16 KiB)
- **When it fires:** named: truncation notice; the remainder up to 1 MiB goes to a spill file (M339)
- **Source:** reasoned: docs/TOOL_OUTPUT_COST.md:308-314

### `include/jc_toolcaps.h:26` — JC_CAP_FETCH_DEFAULT

- **Value:** 128 KiB · **cap**
- **Bounds:** fetch_url output shown to the model
- **Change it with:** fetchMaxBytes (--lite: 32 KiB)
- **When it fires:** named: truncation notice
- **Source:** reasoned: docs/TOOL_OUTPUT_COST.md:308-314

### `include/jc_toolcaps.h:27` — JC_CAP_SEARCH_DEFAULT

- **Value:** 64 KiB · **cap**
- **Bounds:** search_code output shown to the model
- **Change it with:** searchMaxBytes (--lite: 16 KiB)
- **When it fires:** named: truncation notice
- **Source:** reasoned: docs/TOOL_OUTPUT_COST.md:308-314

### `include/jc_toolcaps.h:28` — JC_CAP_GIT_DEFAULT

- **Value:** 32 KiB · **cap**
- **Bounds:** git_status/diff/log/blame output shown to the model
- **Change it with:** gitMaxBytes (--lite: 8 KiB)
- **When it fires:** named: truncation notice
- **Source:** reasoned: docs/TOOL_OUTPUT_COST.md:308-314

### `include/jc_toolloop.h:249` — JC_NOPROGRESS_MAX_ENTRIES

- **Value:** 32 calls · **structural**
- **Bounds:** distinct successful calls the no-progress detector remembers (least recently seen evicted)
- **Change it with:** hard-coded
- **When it fires:** silent: the least recently seen call is forgotten (src/util/jc_toolloop.c:647)
- **Source:** none

### `include/jc_toolloop.h:250` — JC_NOPROGRESS_STOP_AT [route X]

- **Value:** 9 calls · **cap**
- **Bounds:** identical successful calls (same arguments, same result) that END a depth>=1 child
- **Change it with:** hard-coded
- **When it fires:** named: the child ends and the repeat count is in its end record
- **Source:** reasoned: include/jc_toolloop.h:239-248 (the third warning point after notes at 3 and 6; a child once
  made one call 1,126 times)

### `include/jc_toolloop.h:254` — JC_PLACEHOLDER_STOP_AT [route X]

- **Value:** 3 calls · **cap**
- **Bounds:** copied-placeholder arguments in a row that END a depth>=1 child
- **Change it with:** hard-coded
- **When it fires:** named: a loop ending recorded for the child
- **Source:** none: include/jc_toolloop.h:251-253 (run 0015's worker copied the placeholder 742 times; 3 not
  derived)

### `include/jc_toolloop.h:255` — JC_NOPROGRESS_NOTE_AT [route X]

- **Value:** 3 calls · **structural**
- **Bounds:** identical successful calls before the "same result" note, repeated at each multiple
- **Change it with:** hard-coded
- **When it fires:** named: note appended to the newest result
- **Source:** none

### `include/jc_toolloop.h:269` — JC_NOVELTY_WINDOW

- **Value:** 24 calls · **cap**
- **Bounds:** window of the "nothing new" check: 24 stale calls in a row END a depth>=1 child (subagent or parallel
  worker)
- **Change it with:** hard-coded
- **When it fires:** named: the child ends with a loop ending recorded in its end record (src/chat/jc_agent.c:5311)
- **Source:** none: include/jc_toolloop.h:263-268 (run 0012's worker rotated four checks for 28 minutes; 24 not
  derived)

### `include/jc_toolloop.h:61` — JC_TOOLLOOP_MAX_ENTRIES

- **Value:** 24 pairs · **structural**
- **Bounds:** distinct (tool, key) pairs the failed-call loop detector tracks per turn
- **Change it with:** hard-coded
- **When it fires:** silent: new pairs are not tracked (src/util/jc_toolloop.c:407)
- **Source:** none: include/jc_toolloop.h:58-60 ("a bounded miss is better than an allocation"; why 24 not stated)

### `include/jc_toolloop.h:62` — JC_TOOLLOOP_TOOL_MAX

- **Value:** 40 bytes · **structural**
- **Bounds:** tool name kept per tracked pair
- **Change it with:** hard-coded
- **When it fires:** silent: cut (src/util/jc_toolloop.c:411)
- **Source:** none

### `include/jc_toolloop.h:63` — JC_TOOLLOOP_KEY_MAX

- **Value:** 160 bytes · **structural**
- **Bounds:** normalised shell-command key per tracked call
- **Change it with:** hard-coded
- **When it fires:** silent: cut to 159 bytes
- **Source:** none

### `include/jc_toolloop.h:69` — JC_TOOLLOOP_EXACT_AT [route X]

- **Value:** 3 calls · **structural**
- **Bounds:** identical failing (tool, arguments) calls in a turn before the loop note
- **Change it with:** hard-coded
- **When it fires:** named: a per-failure-class note appended to the tool result
- **Source:** measured: include/jc_toolloop.h:16-31 (fitted to the zigodot + chrtext corpora: fires on every
  measured loop, clear of the 2x tail of 78 turns)

### `include/jc_toolloop.h:70` — JC_TOOLLOOP_CLASS_AT [route X]

- **Value:** 4 calls · **structural**
- **Bounds:** failing calls of one tool with one failure class in a turn before the loop note
- **Change it with:** hard-coded
- **When it fires:** named: as above
- **Source:** measured: include/jc_toolloop.h:16-31

### `include/jc_toolout.h:35` — JC_TOOLOUT_SPILL_MAX

- **Value:** 1 MiB · **cap**
- **Bounds:** shell output captured per run_terminal_command call (what the spill file can preserve beyond the
  display cap)
- **Change it with:** hard-coded (the display cap is runMaxBytes)
- **When it fires:** named: "... [output truncated]" appended to the capture (include/jc_proc.h:202-203)
- **Source:** none: include/jc_toolout.h:32-34 (bounds one call's peak; why 1 MiB not stated)

### `include/jc_toolout.h:38` — JC_TOOLOUT_MAX_AGE_DAYS

- **Value:** 3 days · **cap**
- **Bounds:** age after which a crashed session's leftover spill directory is deleted
- **Change it with:** hard-coded
- **When it fires:** silent: removed by an age sweep (src/util/jc_toolout.c:251)
- **Source:** none

### `include/jc_transcribe.h:23` — JC_TRANSCRIBE_MAX_BYTES

- **Value:** 25 MiB · **fence**
- **Bounds:** audio file uploaded for transcription
- **Change it with:** transcribeMaxBytes (0 = built-in)
- **When it fires:** named: "error: audio file is N bytes, over the N-byte cap" (src/tools/jc_tool_transcribe.c:82)
- **Source:** reasoned: include/jc_transcribe.h:21 ("OpenAI's 25 MB limit")

### `include/jc_voice.h:46` — JC_VOICE_MAX_CHARS

- **Value:** 1200 chars · **cap**
- **Bounds:** text sent to TTS for one spoken reply
- **Change it with:** hard-coded
- **When it fires:** named: cut at a sentence boundary and a spoken note says so (include/jc_voice.h:43-45)
- **Source:** reasoned: include/jc_voice.h:43-45 ("with no barge-in, a five-minute monologue is a trap")

### `include/jc_workerpool.h:24` — JC_WORKER_TERM_GRACE_MS

- **Value:** 300 ms · **cap**
- **Bounds:** wait between SIGTERM and SIGKILL when reaping a daemon worker, an MCP stdio server or an LSP server
- **Change it with:** hard-coded
- **When it fires:** kill: SIGKILL, then a blocking reap
- **Source:** none

### `include/jc_workflow.h:31` — JC_WF_MAX_STAGES

- **Value:** 16 stages · **cap**
- **Bounds:** stages in one workflow spec
- **Change it with:** hard-coded
- **When it fires:** named: stderr "workflow: WARNING -- the spec exceeded built-in limits ... were DROPPED and will
  not run" (src/main.c:13695)
- **Source:** none

### `include/jc_workflow.h:32` — JC_WF_MAX_ITEMS

- **Value:** 64 items · **cap**
- **Bounds:** fan-out items in one map stage
- **Change it with:** hard-coded
- **When it fires:** named: the same WARNING (src/main.c:13695)
- **Source:** none

## `other`

### `X include/jc_platform.h:150` — JC_READ_FILE_MAX (enforced src/platform/jc_platform_posix.c:579)

- **Value:** 64 MiB · **fence**
- **Bounds:** one jc_read_file slurp
- **Change it with:** hard-coded
- **When it fires:** error-value: JC_ERR_TOOBIG
- **Source:** reasoned: include/jc_platform.h:147-149 ("Far above any plausible source file (M24)")

### `X src/harness/jc_harness_drive.c:204` — FETCH_LINE

- **Value:** 512 bytes · **structural**
- **Bounds:** one stamped mirror-fetch line in harness.log
- **Change it with:** hard-coded
- **When it fires:** named: "mirror fetch line truncated (over FETCH_LINE bytes)"
- **Source:** reasoned: log_line's stamped buffer is 512 (:204)

### `X src/harness/jc_harness_drive.c:86` — PAUSE_TOTAL_S

- **Value:** 600 s · **fence**
- **Bounds:** the back-off after QUICK_COUNT quick failures (no run starts)
- **Change it with:** hard-coded
- **When it fires:** named: "... waiting 10 minutes"
- **Source:** reasoned: docs/internal/drive/2026-10-23-harness/harness/loop.sh:19-20

### `X src/harness/jc_harness_drive.c:97` — QUICK_COUNT

- **Value:** 3 · **fence**
- **Bounds:** quick failures in a row that trigger the pause
- **Change it with:** hard-coded
- **When it fires:** named (same line as QUICK_LIMIT_S)
- **Source:** reasoned: docs/internal/drive/2026-10-23-harness/harness/loop.sh:19-20

### `X src/harness/jc_harness_handoff.c:1152` — hh_one_line literal caps (600 :1152, 140 :1193, 200 :1272, 110 :1323)

- **Value:** 600 / 140 / 200 / 110 code points · **cap**
- **Bounds:** last message, each todo, a batch preview, each task line in the handoff
- **Change it with:** hard-coded
- **When it fires:** named: ellipsis
- **Source:** none: - (reference handoff.py, no rationale)

### `X src/harness/jc_harness_handoff.c:58` — HANDOFF_TOP_FILES

- **Value:** 25 · **cap**
- **Bounds:** files listed in the handoff (most used first)
- **Change it with:** hard-coded
- **When it fires:** silent: paths past 25 are dropped with no count
- **Source:** none: - (reference handoff.py:24)

### `X src/harness/jc_harness_handoff.c:59` — HANDOFF_NOTE_CHARS

- **Value:** 500 code points · **cap**
- **Bounds:** the start of each manager note shown in the handoff
- **Change it with:** hard-coded
- **When it fires:** named: cut with an ellipsis (hh_one_line :690-698)
- **Source:** none: - (handoff.py:24-28 records why notes are shown at all -- run 0002/0009 -- not why 500)

### `X src/harness/jc_harness_mirror.c:69` — MIRROR_ERR_CUT

- **Value:** 200 bytes · **cap**
- **Bounds:** same, mirror fetch
- **Change it with:** hard-coded
- **When it fires:** silent cut of the diagnostic
- **Source:** none

### `X src/harness/jc_harness_notify.c:89` — NOTIFY_HOOK_ARGS

- **Value:** 16 words · **fence**
- **Bounds:** words in a notify hook command
- **Change it with:** hard-coded
- **When it fires:** named: "... (>=16 words); not run" (:807-810)
- **Source:** none

### `X src/harness/jc_harness_notify.c:91` — NOTIFY_INJ_CUT

- **Value:** 160 bytes · **cap**
- **Bounds:** the inject's captured reply in the note
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference cut -c1-160)

### `X src/harness/jc_harness_push.c:79` — PUSH_ERR_CUT

- **Value:** 200 bytes · **cap**
- **Bounds:** the excerpt of a failed git child's output in a message
- **Change it with:** hard-coded
- **When it fires:** silent cut of the diagnostic
- **Source:** none: - (M1053's git_must idiom)

### `X src/harness/jc_harness_run.c:111` — RUN_MAX_TOOL_CALLS

- **Value:** 50000 · **fence**
- **Bounds:** jichi's --max-tool-calls for the lead run
- **Change it with:** hard-coded in the port (the reference read ZD_MAX_TOOL_CALLS)
- **When it fires:** unknown (enforced by jichi, outside this shard)
- **Source:** none: - (run-lead.sh:33, no rationale; run.c:109 cites "the operator's 2026-09-25 23:15 decision: caps
  off, fences on")

### `X src/harness/jc_harness_run.c:112` — RUN_VERIFY_TIMEOUT

- **Value:** 30m (1800 s) · **cap**
- **Bounds:** jichi's --verify-timeout for the lead run (the testCommand verifier after the lead's work)
- **Change it with:** hard-coded in the port (the reference read ZD_VERIFY_TIMEOUT)
- **When it fires:** named: the lead sees a failed verification (run-lead.sh:37-38); the meta says "limits:
  max-tool-calls=50000 verify-timeout=30m" (:1273)
- **Source:** incident: docs/internal/drive/2026-10-23-harness/harness/run-lead.sh:34-38 (a looping test kept the
  verifier spinning; "A green suite takes seconds to minutes, so 30 minutes catches only a hang")

### `X src/harness/jc_hp_backlog.c:96` — BACKLOG_FILE_MB

- **Value:** 8 MiB · **fence**
- **Bounds:** per file version, the backlog push's size check
- **Change it with:** hard-coded
- **When it fires:** named refusal (backlog's size-check text, not traced)
- **Source:** measured: same as PUSH_MAX_FILE_MB

### `X src/harness/jc_hp_backlog.c:97` — BACKLOG_TOTAL_MB

- **Value:** 20 MiB · **fence**
- **Bounds:** per step, the backlog push's size check
- **Change it with:** hard-coded
- **When it fires:** named refusal (not traced)
- **Source:** measured: same as PUSH_MAX_TOTAL_MB

### `X src/harness/jc_hp_gateci.c:90` — GCI_LINE_CUT

- **Value:** 300 bytes · **cap**
- **Bounds:** per-line cut of the ci failure excerpt
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (comment cites SV:1238, no number rationale found)

### `X src/harness/jc_hp_gateci.c:91` — GCI_JOIN_CUT

- **Value:** 160 bytes · **cap**
- **Bounds:** the joined ci failure lines in the note
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference gate-run.sh:79 cut -c1-160)

### `X src/harness/jc_hp_gateref.c:657` — GREF_CLEAR_NODES

- **Value:** 262144 · **fence**
- **Bounds:** total nodes the clear may visit
- **Change it with:** hard-coded
- **When it fires:** never fires: the counter starts at 262144 and is INCREMENTED (:701) while the check is `*nodes
  <= 0` (:698)
- **Source:** none

### `X src/harness/jc_hp_gateref.c:658` — GREF_CLEAR_NAMES (with GREF_CLEAR_DEPTH 32 :656)

- **Value:** 256 entries per directory level · **fence**
- **Bounds:** the redfirst scratch clear
- **Change it with:** hard-coded
- **When it fires:** named: a level with more entries returns -1 ("fault, never a silent partial clear") -> setup
  INVALID
- **Source:** reasoned: a fresh scratch has <= 4 (:658)

### `X src/harness/jc_hp_gatesweep.c:990` — GSW_CLEAR_NODES

- **Value:** 262144 · **fence**
- **Bounds:** total nodes the clear may visit
- **Change it with:** hard-coded
- **When it fires:** never fires: counter incremented (:1038) against a `<= 0` check (:1035)
- **Source:** none

### `X src/harness/jc_hp_gatesweep.c:991` — GSW_CLEAR_NAMES (with GSW_CLEAR_DEPTH 32 :989)

- **Value:** 256 entries per directory level · **fence**
- **Bounds:** the sweep scratch clear (clones j/san/o2/ctr, tmp, records)
- **Change it with:** hard-coded
- **When it fires:** named: -1 -> "could not clear the stage scratch <dir>" (:1326-1327)
- **Source:** reasoned: a fresh scratch has <= 6 (:991)

### `X src/harness/jc_hp_records.c:101` — PUSH_ERR_CUT (records copy)

- **Value:** 200 bytes · **cap**
- **Bounds:** same, records push
- **Change it with:** hard-coded
- **When it fires:** silent cut of the diagnostic
- **Source:** none

### `X src/harness/jc_hp_tick.c:117` — TICK_PAGE_ARGS

- **Value:** 16 words · **fence**
- **Bounds:** words in host.pageCommand
- **Change it with:** hard-coded
- **When it fires:** named: "pageCommand is too long to run (>15 words); not run" (:358-361)
- **Source:** none

### `X src/harness/jc_hp_tick.c:646` — page repeat (fails == 1 || fails % 6 == 0)

- **Value:** every 6th consecutive failure · **cap**
- **Bounds:** how often `push tick` pages while the push keeps failing
- **Change it with:** hard-coded
- **When it fires:** silent: pages 2-5, 7-11, ... are suppressed; each TICK FAIL line is still logged
- **Source:** reasoned: docs/internal/drive/2026-10-27-hardening-host/push-tick.sh:9-10 ("every 6th consecutive
  failure (2 h at 20 min)")

### `X src/snapshot/jc_snapshot.c:504` — snapshot prune trigger (total > snapshotLimit * 2)

- **Value:** 2 x snapshotLimit · **cap**
- **Bounds:** checkpoint history before it is pruned back to snapshotLimit
- **Change it with:** config snapshotLimit (<= 0 = unlimited, :493-494)
- **When it fires:** named (INFO log): "snapshots: pruned history to <n> checkpoints" (:482)
- **Source:** reasoned: src/snapshot/jc_snapshot.c:485-486 (hysteresis)

## `src/acp`

### `src/acp/jc_acp.c:46` — ACP_RESULT_MAX

- **Value:** 8192 bytes · **cap**
- **Bounds:** tool-result text echoed to the ACP client (editor)
- **Change it with:** hard-coded
- **When it fires:** named: "\n... [truncated]" after a whole-codepoint prefix (:563-576)
- **Source:** none: - (docs/ROADMAP.md:30539-30547, M536, is about the UTF-8 cut, not the number)

## `src/chat`

### `src/chat/jc_agent.c:274` — model-call retry backoff

- **Value:** 500 ms, doubling, max 8000 ms · **cap**
- **Bounds:** sleep between retries of a transient model-call failure
- **Change it with:** hard-coded
- **When it fires:** named: "transient error (status N); retry i/M in Nms" (src/chat/jc_agent.c:791-794, 808-812)
- **Source:** none

### `src/chat/jc_agent.c:53` — JC_AGENT_MAX_ARG_PATHS

- **Value:** 32 paths · **fence**
- **Bounds:** declared paths in one tool call still considered for the explicit-scope exemption (M501)
- **Change it with:** hard-coded
- **When it fires:** silent: over 32 the constraint stays applied (the safe direction)
- **Source:** reasoned: src/chat/jc_agent.c:47-52

### `src/chat/jc_agent.c:5623` — relay hard trigger (rc.hard_pct)

- **Value:** 90 % of the effective window · **cap**
- **Bounds:** server prompt tokens at which the relay hands over (trigger E3)
- **Change it with:** hard-coded (relay is opt-in)
- **When it fires:** named: WARN at the handover and stop_reason "handover"
- **Source:** none: docs/proposals/2026-10-context-relay.md:351-352 states the default without a reason

### `src/chat/jc_agent.c:6556` — envelope iteration floor (opts.max_iters < 200 -> 200)

- **Value:** 200 rounds · **cap**
- **Bounds:** tool rounds of a turn run under the autonomy envelope (--auto, budgets, verify)
- **Change it with:** hard-coded (a maxToolIters above 200 is kept)
- **When it fires:** named: "hit max tool iterations (200)"
- **Source:** reasoned: src/chat/jc_agent.c:6553-6555 ("give the loop ample headroom so it isn't cut short by the
  plain iteration cap")

### `src/chat/jc_agent.c:7142` — JC_SUBAGENT_ANSWER_CAP

- **Value:** 4096 bytes (a head of 3796 B is kept) · **cap**
- **Bounds:** a subagent's or parallel child's final answer as handed back into the parent's context
- **Change it with:** hard-coded
- **When it fires:** named: head + "... [the complete answer is at <path> ... this note carries the opening only, N
  of M bytes]" (src/chat/jc_agent.c:7172-7175); full text in the 0600 preservation store
- **Source:** incident: src/chat/jc_agent.c:7125-7131 (2026-10-05 audit: 14 of 17 hand-backs over 10 KB, max 31 KB);
  docs/DEFERRED.md:1489-1494 (def-318)

### `src/chat/jc_agent.c:947` — JC_SILENT_KEYS

- **Value:** 16 distinct commands · **structural**
- **Bounds:** table of silent-success commands counted per turn (M895)
- **Change it with:** hard-coded
- **When it fires:** silent: the 17th distinct command is not counted (returns 0)
- **Source:** none

### `src/chat/jc_app.c:1559` — fallback reachability probe timeout (jc_net_reachable arg)

- **Value:** 2 s · **cap**
- **Bounds:** how long GET {apiBase}/models may take before a model that declares a fallback is treated as down
- **Change it with:** hard-coded
- **When it fires:** named: "[fallback] <model> unreachable -> <fallback>" (docs/MODELS.md:139-142); the 2 s is not
  named
- **Source:** none

### `src/chat/jc_app.c:47` — JC_SHELL_LINE_MAX

- **Value:** 8400 bytes (usable length is less: minus "exec 2>&1; " and the secret-unset prefix) · **structural**
- **Bounds:** longest shell command line jichi formats for the local shell
- **Change it with:** hard-coded
- **When it fires:** named: tool error "the command is N bytes, and the local shell runs at most M whole; it was NOT
  run" (src/tools/jc_tool_run.c:100-108)
- **Source:** none: - (the 8400 has no rationale; refusing instead of truncating is incident M721,
  docs/ROADMAP.md:43434-43441)

### `src/chat/jc_app.c:50` — JC_REACH_MAX (also re-#defined at src/chat/jc_agent.c:56)

- **Value:** 64 models · **structural**
- **Bounds:** configured models the fallback reachability cache covers
- **Change it with:** hard-coded
- **When it fires:** silent: with more than 64 models jc_app_effective_model returns the model unchanged, so
  fallback never happens (src/chat/jc_app.c:1587-1588)
- **Source:** none

### `src/chat/jc_app.c:772` — B1 held-pipe drain window

- **Value:** 200 ms of silence · **cap**
- **Bounds:** after the shell exits while a background job still holds its output pipe, how long jichi keeps reading
  before returning
- **Change it with:** hard-coded
- **When it fires:** named: the call returns the shell's status with the note "[the shell exited (status N) while a
  process it started still holds the command's output; ... background process N ...]" (src/chat/jc_app.c:863-869;
  :880-886 when no background slot is free)
- **Source:** none: - (the comment's "measured shape" is a job that writes nothing, not the 200 ms)

### `src/chat/jc_autocontext.c:120` — autoContextTopK default

- **Value:** 5 results per source · **cap**
- **Bounds:** retrieval results per source considered for auto-context
- **Change it with:** config autoContextTopK (0 = 5)
- **When it fires:** silent
- **Source:** none

### `src/chat/jc_autocontext.c:22` — autoContextMaxTokens default (DEFAULT_TOKENS)

- **Value:** 3000 tokens, then clamped to context limit / 3 and deflated by the calibration ratio · **cap**
- **Bounds:** size of the auto-RAG block injected into a user turn (opt-in autoContext)
- **Change it with:** config autoContextMaxTokens (0 = this default)
- **When it fires:** named to the model only: "... [truncated]" per section (src/chat/jc_autocontext.c:83)
- **Source:** none

### `src/chat/jc_bg.c:74` — stale background spool sweep age

- **Value:** 86400 s (24 h) · **cap**
- **Bounds:** age (mtime) after which a bg-N.out spool is unlinked at the next session's start
- **Change it with:** hard-coded
- **When it fires:** silent: unlink
- **Source:** reasoned: src/chat/jc_bg.c:65-68 ("24 h outlives any realistic run")

### `src/chat/jc_bg.c:884` — background kill grace

- **Value:** 10 x 50 ms = 500 ms (80 ms at jichi exit, jc_bg.c:933) · **cap**
- **Bounds:** SIGTERM -> SIGKILL window for a killed background command
- **Change it with:** hard-coded
- **When it fires:** kill: state KILLED
- **Source:** none

### `src/chat/jc_board.c:349` — JC_BOARD_LOCK_WAIT_MS

- **Value:** 5000 ms · **cap**
- **Bounds:** wait for another process's .jichi/board.lock before editing the board
- **Change it with:** hard-coded
- **When it fires:** named: WARN "another process has held <lock> for more than 5 s -- editing without it, so an
  edit made at this moment may be lost" (src/chat/jc_board.c:390-393)
- **Source:** reasoned: src/chat/jc_board.c:346-347 ("A board edit takes microseconds; a lock held longer is a
  process stopped inside one")

### `src/chat/jc_compact.c:39` — MIN_MESSAGES

- **Value:** 4 messages · **structural**
- **Bounds:** auto-compaction never folds a history shorter than this
- **Change it with:** hard-coded
- **When it fires:** silent: no compaction
- **Source:** none

### `src/chat/jc_compact.c:41` — RENDER_TRUNC

- **Value:** 4000 bytes · **cap**
- **Bounds:** each message's content in the transcript handed to the compaction summarizer
- **Change it with:** hard-coded
- **When it fires:** named to the summarizer only: "...[truncated]" (src/chat/jc_compact.c:141-142)
- **Source:** none

### `src/chat/jc_compact.c:444` — summarizer output tokens

- **Value:** summarizer context / 4, clamped to [256, 2048] tokens · **cap**
- **Bounds:** max_tokens of each compaction summary
- **Change it with:** hard-coded (follows the summarize model's contextLength, else JC_COMPACT_SUMMARIZER_DEFAULT
  8192, include/jc_compact.h:36)
- **When it fires:** silent: a longer summary is cut at max_tokens
- **Source:** none

### `src/chat/jc_compact.c:452` — JC_COMPACT_SUMMARY_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total time of each compaction summarizer call (non-streaming; per chunk and per halving retry)
- **Change it with:** hard-coded
- **When it fires:** named but without the cause: WARN "auto-compaction: summarization failed; keeping full history"
  (src/chat/jc_compact.c:713-714); the word timeout never appears
- **Source:** none: - (the M1093 report, docs/internal/drive/2026-10-17-hardening/reports/2026-10-30-m1093.md:25,
  says the per-class totals "keep their measured values" but no measurement of this 60 is written anywhere)

### `src/chat/jc_compact.c:538` — summarize_fit retries

- **Value:** 3 halvings · **cap**
- **Bounds:** retries of a context-overflow HTTP 400 from the summarizer, each with half the transcript
- **Change it with:** hard-coded
- **When it fires:** named but generic: "auto-compaction: summarization failed; keeping full history"
- **Source:** none

### `src/chat/jc_compact.c:751` — between-turn compaction trigger and kept tail

- **Value:** trigger at 80 % (est > limit*4/5); keep ~35 % (limit*7/20, jc_compact.c:703) · **cap**
- **Bounds:** when between-turn summarization runs and how much recent history stays verbatim
- **Change it with:** autoCompact false turns it off; the fractions are hard-coded
- **When it fires:** named: stderr "[compacted N earlier messages into a summary]" unless -q
  (src/chat/jc_compact.c:724-727)
- **Source:** none

### `src/chat/jc_compact.c:766` — MIDTURN_HIGH_PCT / MIDTURN_TARGET_PCT

- **Value:** 80 % / 60 % of the effective context limit · **cap**
- **Bounds:** when mid-turn elision of old tool output starts, and the level it elides down to
- **Change it with:** hard-coded
- **When it fires:** named: INFO "[compact] mid-turn: elided N old tool output(s)" and status "compacting
  (mid-turn): elided old tool output to fit context" (src/chat/jc_compact.c:1667-1675)
- **Source:** measured: docs/ROADMAP.md:18316-18360 (M326y: n = 593 pressured passes; both kept unchanged on
  evidence)

### `src/chat/jc_compact.c:768` — MIDTURN_KEEP_RECENT

- **Value:** 6 messages · **structural**
- **Bounds:** newest messages that mid-turn elision never touches
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/chat/jc_compact.c:769` — ELIDE_MIN_BYTES

- **Value:** 800 bytes · **cap**
- **Bounds:** tool results at or below this size are never elided mid-turn
- **Change it with:** hard-coded
- **When it fires:** silent: a result <= 800 B is skipped
- **Source:** measured: docs/ROADMAP.md:18333-18337 (M326y: 86 % of output bytes are above 800; a floor of 200 would
  reach 98 % for 2.2x the churn)

### `src/chat/jc_compact.c:770` — ELIDE_HEAD / ELIDE_TAIL

- **Value:** 400 / 200 bytes · **cap**
- **Bounds:** bytes of an elided tool result kept in the context (head and tail)
- **Change it with:** hard-coded
- **When it fires:** named: an elision marker with a claim ticket to the full text in ~/.jichi.d/tool-output
  (docs/COMPACTION.md:512-524)
- **Source:** none

### `src/chat/jc_constraint.c:147` — RUN_WORDS

- **Value:** 24 words · **structural**
- **Bounds:** words of one shell command the constraint classifier inspects
- **Change it with:** hard-coded
- **When it fires:** silent: later words are not classified
- **Source:** none

### `src/chat/jc_context.c:590` — JC_HIST_MAX_TOOLS

- **Value:** 48 distinct tools · **structural**
- **Bounds:** distinct tool names tallied in the "context history" report (M315)
- **Change it with:** hard-coded
- **When it fires:** silent: the 49th and later tools' tokens count toward the total but get no line, so the
  per-tool percentages no longer add up
- **Source:** none

### `src/chat/jc_control.c:200` — control request read deadline

- **Value:** 2 s (time(NULL), whole seconds) · **cap**
- **Bounds:** how long jichi waits for a control client's request line
- **Change it with:** hard-coded
- **When it fires:** error-value: a line not complete in time is parsed as read; a partial line answers "not a JSON
  object" (src/chat/jc_control.c:390-393)
- **Source:** none

### `src/chat/jc_control.c:41` — CONTROL_PENDING_MAX

- **Value:** 65536 bytes (4 x JC_CONTROL_LINE_MAX 16384) · **cap**
- **Bounds:** total steering text queued by control "inject" between model calls
- **Change it with:** hard-coded
- **When it fires:** named: reply "steering queue full" (src/chat/jc_control.c:427-429)
- **Source:** none

### `src/chat/jc_envelope.c:995` — verify fix-forward retries

- **Value:** 3 · **cap**
- **Bounds:** fix-forward attempts after a red verifier before rollback
- **Change it with:** --verify-retries <n> (0 = roll back at once)
- **When it fires:** named: the envelope outcome and rollback in the run footer and journal
- **Source:** none

### `src/chat/jc_hooks.c:26` — HOOK_MAX_OUTPUT

- **Value:** 32768 bytes · **cap**
- **Bounds:** a hook command's captured stdout + stderr
- **Change it with:** hard-coded
- **When it fires:** named: jc_proc_capture appends "\n... [output truncated]" (include/jc_proc.h:202-203)
- **Source:** none

### `src/chat/jc_oneshot.c:21` — JC_ONESHOT_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total time of a one-shot (non-streaming) side model call when the caller passes timeout <= 0
- **Change it with:** hard-coded (every caller passes its own value)
- **When it fires:** error-value: JC_ERR_TIMEOUT, the caller gets NULL / timed_out = 1
- **Source:** none: - (same M1093 "measured values" wording, no measurement written)

### `src/chat/jc_rules.c:16` — JC_RULES_MAX

- **Value:** 32768 bytes · **cap**
- **Bounds:** the combined rules block (global AGENTS.md, the AGENTS.md/CLAUDE.md walk, config instructions) in the
  system prompt
- **Change it with:** hard-coded
- **When it fires:** named to the MODEL only: "... [rules truncated]" in the prompt (src/chat/jc_rules.c:101); no
  log line, doctor check or telemetry tells the operator
- **Source:** none

### `src/chat/jc_rules.c:17` — JC_RULES_WALK_MAX

- **Value:** 64 directory levels · **structural**
- **Bounds:** how far the rules walk and the workspace-root search climb from cwd
- **Change it with:** hard-coded
- **When it fires:** silent: deeper ancestors are not read
- **Source:** reasoned: src/chat/jc_rules.c:17 ("guard against pathological directory chains")

### `src/chat/jc_sysmsg.c:29` — JC_SYSMSG_FIT_PCT

- **Value:** 45 % of the context window (at 4 bytes per token) · **cap**
- **Bounds:** share of the window that rules + repo map + design docs may take together in the system prompt
- **Change it with:** hard-coded (follows contextLimit / contextLength / the 32000 default)
- **When it fires:** named to the model only: "[... <section> truncated to fit the model context window ...]"
  (src/chat/jc_sysmsg.c:177-179)
- **Source:** none

### `src/chat/jc_sysmsg.c:97` — sysmsg fit limit clamp

- **Value:** 8,000,000 tokens · **structural**
- **Bounds:** overflow guard on the byte math of the system-prompt fit
- **Change it with:** hard-coded
- **When it fires:** silent (no real model reaches it)
- **Source:** reasoned: src/chat/jc_sysmsg.c:93-96

## `src/command`

### `src/command/jc_command.c:21` — JC_CMD_SHELL_MAX

- **Value:** 16384 bytes · **cap**
- **Bounds:** output of a !`cmd` shell expansion inside a custom slash command
- **Change it with:** hard-coded
- **When it fires:** named ("...[truncated]")
- **Source:** none

### `src/command/jc_command.c:22` — JC_CMD_FILE_MAX

- **Value:** 32768 bytes · **cap**
- **Bounds:** an @path file inlined by a custom slash command
- **Change it with:** hard-coded
- **When it fires:** silent (head kept, no marker)
- **Source:** none: docs/analysis/2026-09-29-scaffolding-packs-review.md:476 (R4-05 records the missing marker)

### `src/command/jc_refs.c:216` — REF_RSS_FETCH_MAX

- **Value:** 2097152 bytes · **cap**
- **Bounds:** raw feed bytes kept for @rss: before reducing to text
- **Change it with:** hard-coded
- **When it fires:** silent (body cut before parsing; tail items vanish)
- **Source:** none

### `src/command/jc_refs.c:220` — REF_RSS_FETCH_TIMEOUT_SECS

- **Value:** 30 s · **cap**
- **Bounds:** total wall-clock of the @rss: feed fetch
- **Change it with:** hard-coded (only the connect / deadStream fences read config, via jc_net_side_fences)
- **When it fires:** named, misleadingly: "(could not fetch feed: model stalled (timed out))" when connected,
  "(could not fetch feed: http error)" otherwise
- **Source:** none: M1093 report docs/internal/drive/2026-10-17-hardening/reports/2026-10-30-m1093.md:27 says
  "keeping the measured values" but records no measurement; finding C-09 asked for "off or a measured value"
  (docs/analysis/2026-10-17-hardening-04-network-and-secrets.md:480)

### `src/command/jc_refs.c:28` — REF_BLOCK_MAX

- **Value:** 32768 bytes · **cap**
- **Bounds:** one @-reference block in a user prompt (@file, tool-backed refs, @rss:, @problems)
- **Change it with:** hard-coded
- **When it fires:** named ("... [reference truncated]")
- **Source:** none

### `src/command/jc_refs.c:285` — REF_PROBLEMS_MAX_FILES

- **Value:** 16 files · **cap**
- **Bounds:** files of this session that @problems asks the language server about
- **Change it with:** hard-coded
- **When it fires:** silent (files past 16 are not mentioned)
- **Source:** none: jc_refs.c:283-284 motivates a bound ("each spawns/queries the LSP with a timeout"), not the
  number

### `src/command/jc_refs.c:29` — REF_TOTAL_MAX

- **Value:** 98304 bytes · **cap**
- **Bounds:** all @-references of one prompt together
- **Change it with:** hard-coded
- **When it fires:** named ("... [more references omitted]")
- **Source:** none

### `src/command/jc_refs.c:359` — jc_proc_capture(alias cmd, 65536, 15)

- **Value:** 15 s; 65536 bytes · **cap**
- **Bounds:** the "cmd" of an @ref alias that yields a secret (presence check only)
- **Change it with:** hard-coded
- **When it fires:** silent (a killed or failing command reads as "unset")
- **Source:** none

## `src/config`

### `src/config/jc_config.c:1358` — maxToolIters

- **Value:** 25 (--lite 12); <= 0 -> default · **cap**
- **Bounds:** model -> tool rounds in one top-level turn
- **Change it with:** config maxToolIters
- **When it fires:** named: WARN "hit max tool iterations (N)" (src/chat/jc_agent.c:6153); --output json stop_reason
  "max_iters" (M322)
- **Source:** none

### `src/config/jc_config.c:1364` — scopedEditMax

- **Value:** 2; negative -> 2; 0 = off · **fence**
- **Bounds:** edits by the lead of a worker-owned (merged) path in one turn; the Nth is refused
- **Change it with:** config scopedEditMax
- **When it fires:** error-value: the edit tool returns a tool error naming the file, the owning task and what to do
  instead (docs/ROADMAP.md:52643-52650)
- **Source:** reasoned: docs/ROADMAP.md:52614-52617, :52643-52647 (operator's decision 2026-10-16: N = 2 keeps one
  reconciling edit allowed)

### `src/config/jc_config.c:1368` — maxRetries

- **Value:** 4 (--lite 2); negative -> 0 · **cap**
- **Bounds:** retries of a transient model-call failure (429, 5xx, transport error, stall/connect timeout)
- **Change it with:** config maxRetries
- **When it fires:** named: retry WARNs, then the error
- **Source:** none: - ; measured against it: docs/internal/lessons/LEDGER.md:1187-1195 (2026-11-02: outages up to
  10.1 min killed 15 drive runs; the drive set maxRetries 120)

### `src/config/jc_config.c:1387` — relayNotesBytes

- **Value:** 8192 bytes; <= 0 -> 8192 · **cap**
- **Bounds:** the relay baton's notes (bites the model's account)
- **Change it with:** config relayNotesBytes
- **When it fires:** unknown (cut in the relay code, src/util, not traced)
- **Source:** none: src/config/jc_config.c:1379-1385 cites the proposal's "8 KB by default" with no measurement

### `src/config/jc_config.c:1404` — relayMaxLegs

- **Value:** 5, clamped to [1, 32] · **cap**
- **Bounds:** in-process relay legs per run (relayInProcess)
- **Change it with:** config relayMaxLegs
- **When it fires:** silent: a value above 32 becomes 32 with no message; the leg stop itself not traced
- **Source:** reasoned: src/config/jc_config.c:1398-1403 (the 32 ceiling is "a guard against a typo"); the 5 has no
  rationale

### `src/config/jc_config.c:1460` — repoMapLimit

- **Value:** 0 -> JC_REPOMAP_MAX 12 KB (include/jc_repomap.h:26) · **cap**
- **Bounds:** rendered repository map in the system prompt
- **Change it with:** config repoMapLimit
- **When it fires:** unknown (rendered in src/index, shard 5)
- **Source:** none

### `src/config/jc_config.c:1464` — contextLimit (top-level) > models[].contextLength > built-in

- **Value:** unset -> JC_COMPACT_DEFAULT_LIMIT 32000 tokens (include/jc_compact.h:31); --lite 16384 · **cap**
- **Bounds:** the context budget that compaction, mid-turn elision, the system-prompt fit, auto-context and the
  output ceiling are computed from
- **Change it with:** config contextLimit, models[].contextLength, --context-limit
- **When it fires:** named only under lite: WARN "lite caps the context budget at N tokens, but the active model
  declares contextLength M" (src/config/jc_config.c:2163-2169); the 32000 fallback is silent
- **Source:** none: - (jc_compact.c:264 calls 32000 "a guess")

### `src/config/jc_config.c:1471` — snapshotLimit

- **Value:** 100; 0 = unlimited · **cap**
- **Bounds:** checkpoints kept in the shadow git repository
- **Change it with:** config snapshotLimit
- **When it fires:** silent: the oldest checkpoints are pruned at startup once history passes 2 x the limit
- **Source:** none

### `src/config/jc_config.c:1477` — readMaxBytes / runMaxBytes / fetchMaxBytes / searchMaxBytes / gitMaxBytes

- **Value:** 0 -> built-in 256K / 64K / 128K / 64K / 32K bytes (include/jc_toolcaps.h:24-28); --lite 64K / 16K / 32K
  / 16K / 8K · **cap**
- **Bounds:** tool output shown to the model per call
- **Change it with:** config keys of the same names
- **When it fires:** named: a truncation or spill note (run_terminal_command spills the rest to
  ~/.jichi.d/tool-output, M339)
- **Source:** none

### `src/config/jc_config.c:1487` — imageGenMaxBytes / audioGenMaxBytes

- **Value:** 0 -> 16 MB (src/tools/jc_tool_imagegen.c:37) / 32 MB (src/tools/jc_tool_audiogen.c:24) · **cap**
- **Bounds:** a generated image or audio response
- **Change it with:** config keys of the same names
- **When it fires:** error-value: "rejected before anything is written" (docs/MEDIA_GEN.md:200-201)
- **Source:** none

### `src/config/jc_config.c:1491` — transcribeMaxBytes

- **Value:** 0 -> 25 MiB (JC_TRANSCRIBE_MAX_BYTES, include/jc_transcribe.h:23) · **cap**
- **Bounds:** audio file uploaded for transcription
- **Change it with:** config transcribeMaxBytes
- **When it fires:** unknown (enforced in src/tools/jc_tool_transcribe.c, shard 5)
- **Source:** none

### `src/config/jc_config.c:1517` — maxSubagentDepth

- **Value:** 2 (--lite 0); negative -> 0 · **fence**
- **Bounds:** nesting depth of spawn_subagent
- **Change it with:** config maxSubagentDepth
- **When it fires:** named: past the depth the spawn tools are not offered (jc_subagent_can_spawn,
  src/chat/jc_agent.c:7074)
- **Source:** none

### `src/config/jc_config.c:1521` — maxSubagentIters

- **Value:** unset or <= 0 -> the parsed maxToolIters (25); halved per depth, floor JC_SUBAGENT_MIN_ITERS 4
  (include/jc_agent.h:180) · **cap**
- **Bounds:** tool rounds of one subagent
- **Change it with:** config maxSubagentIters
- **When it fires:** named: "hit max tool iterations (N)" for the sub-agent
- **Source:** none

### `src/config/jc_config.c:1597` — memBudgetMb / --mem-budget

- **Value:** 0 = off · **cap**
- **Bounds:** resident memory (process-group RSS, sampled about every 500 ms) of a model-issued shell command
- **Change it with:** config memBudgetMb, --mem-budget <MB>
- **When it fires:** kill, named: status "[mem] KILLED: exceeded the N MB budget", output "[stopped: exceeded the
  memory budget]", exit 137; one WARN near 80 %
- **Source:** none

### `src/config/jc_config.c:1598` — runTimeout / --run-timeout / per-call "timeout" argument

- **Value:** 0 = no limit (default) · **cap**
- **Bounds:** wall clock of a model-issued shell command (run_terminal_command, and run_tests and @diff via
  jc_app_run_command)
- **Change it with:** per-call "timeout" arg > --run-timeout > config runTimeout (src/config/jc_config.c:2685-2694)
- **When it fires:** kill (SIGTERM then SIGKILL of the group), named: "[stopped: command timed out after Ns and was
  terminated]", exit 124 (src/chat/jc_app.c:1123-1132)
- **Source:** none

### `src/config/jc_config.c:1803` — parallelMetric.timeoutSecs

- **Value:** 0 -> the verifier's --verify-timeout, which itself defaults to none · **cap**
- **Bounds:** one metric measurement run in a worktree (parallelTestEdits "metric")
- **Change it with:** config parallelMetric.timeoutSecs
- **When it fires:** named: a measurement that fails is held (kept as parallel-metric)
- **Source:** none

### `src/config/jc_config.c:1888` — sound.playTimeoutSeconds

- **Value:** 0 -> 120 s (JC_SOUND_PLAY_TIMEOUT_DEFAULT, include/jc_sound.h:32) · **cap**
- **Bounds:** one play_audio player command
- **Change it with:** config sound.playTimeoutSeconds
- **When it fires:** kill (unknown message; sound_run in src/tools)
- **Source:** none

### `src/config/jc_config.c:1890` — sound.recordMaxSeconds

- **Value:** 0 -> 60 s, hard cap 600 s (include/jc_sound.h:33-34) · **cap**
- **Bounds:** length of one record_audio capture
- **Change it with:** config sound.recordMaxSeconds
- **When it fires:** silent: a longer request is clamped (jc_sound_clamp_seconds, src/util/jc_sound.c:14-21)
- **Source:** none

### `src/config/jc_config.c:1912` — maxParallelAgents

- **Value:** 0 = auto min(cpu, 8) (JC_PAR_CEILING, src/tools/jc_tool_parallel.c:64); --lite 1 · **fence**
- **Bounds:** concurrent spawn_parallel children
- **Change it with:** config maxParallelAgents
- **When it fires:** silent: extra tasks wait for a slot
- **Source:** none

### `src/config/jc_config.c:1919` — parallelTaskTimeout

- **Value:** 0/absent -> 300 s (JC_PAR_TASK_TIMEOUT_DEFAULT, src/tools/jc_tool_parallel.c:276) · **cap**
- **Bounds:** wall clock of one spawn_parallel child
- **Change it with:** config parallelTaskTimeout (> 0 only; no "no limit")
- **When it fires:** kill, named: "sub-agent timed out after Ns (parallelTaskTimeout)"; the child is reported
  failed, kept and never merged
- **Source:** none: - for the 300; an incident shows it too short: docs/PARALLEL.md:346-350,
  docs/CONFIG_TUTORIAL.md:1199-1202 (successful children took 300, 309, 362 and 462 s; the default killed about
  half)

### `src/config/jc_config.c:1940` — parallelSupervise.placeholders

- **Value:** 2 (JC_PAR_SUP_PLACEHOLDERS, include/jc_config.h:417) · **cap**
- **Bounds:** R2: copied-placeholder total that flags a worker
- **Change it with:** config parallelSupervise.placeholders
- **When it fires:** named: worker_flag (report-only by default)
- **Source:** measured: docs/PARALLEL.md:384 ("4 of 880 looped on placeholders")

### `src/config/jc_config.c:1971` — daemonWorkers

- **Value:** 0 = auto min(cpu, 4), clamped to JC_DAEMON_MAX_WORKERS (src/main.c) · **fence**
- **Bounds:** concurrent daemon turns
- **Change it with:** config daemonWorkers
- **When it fires:** silent: further clients wait
- **Source:** none

### `src/config/jc_config.c:1976` — daemonWorkerTimeout

- **Value:** 0/absent -> 300 s (literal at src/main.c:17752) · **cap**
- **Bounds:** wall clock of one daemon request: a whole agent turn in a forked worker
- **Change it with:** config daemonWorkerTimeout (> 0 only)
- **When it fires:** kill: SIGTERM then grace then SIGKILL; daemon stderr "daemon: worker N exceeded 300s ->
  killing" (src/main.c:17524-17528)
- **Source:** none

### `src/config/jc_config.c:2120` — timeouts.deadStream

- **Value:** 900 s; 0 = off; absent keeps 900 · **fence**
- **Bounds:** a connected model stream on which NOTHING arrives
- **Change it with:** config timeouts.deadStream (global block only; no CLI flag, no per-model tier)
- **When it fires:** named: the transfer is aborted as a timeout
- **Source:** measured: include/jc_config.h:696-702 ("default 900 (above the 540 s healthy max the drive
  measured)"); incident docs/MODELS.md:251-256 (a call ran 11,677 s after its stream died)

### `src/config/jc_config.c:2130` — diskFloorMb

- **Value:** 10 MiB; 0 = off · **fence**
- **Bounds:** free space on the workspace filesystem below which a run stops
- **Change it with:** config diskFloorMb
- **When it fires:** named: the run stops; an unmeasurable disk never stops it
- **Source:** none: include/jc_config.h:707-711 and docs/MODELS.md:326-328 say the default is "provisional until the
  telemetry corpus ... shows where a real floor sits"

### `src/config/jc_config.c:2131` — diskFloorFiles

- **Value:** 0 = off · **fence**
- **Bounds:** free inodes below which a run stops
- **Change it with:** config diskFloorFiles
- **When it fires:** named: the run stops
- **Source:** none

### `src/config/jc_config.c:544` — timeouts.stall

- **Value:** built-in 30 s (JC_HTTP_STALL_TIMEOUT_DEFAULT, include/jc_http.h:46); -1/absent = inherit; 0 = off ·
  **cap**
- **Bounds:** seconds a model stream may stay at ~0 bytes/s, including the whole prefill before the first token
- **Change it with:** config timeouts.stall (global or per model), --timeout-stall <s|off>
- **When it fires:** named: model_call result "timeout", retried as transient (maxRetries), then an error naming the
  knob (docs/MODELS.md:276-296)
- **Source:** reasoned: include/jc_http.h:32-34 ("Generous enough not to trip a slow-but-progressing local
  generation"); CONTRADICTED by measurement at include/jc_http.h:36-43 (a ~12k-token prefill takes ~36 s; the 30 s
  default cut 4 of 4 first attempts, 2026-10-15)

### `src/config/jc_config.c:545` — timeouts.request

- **Value:** built-in 0 (off) · **cap**
- **Bounds:** hard wall-clock cap on a whole model call
- **Change it with:** config timeouts.request, --timeout-request <s|off>
- **When it fires:** kill: CURLOPT_TIMEOUT, then retried as transient
- **Source:** none

### `src/config/jc_config.c:591` — models[].maxTokens -> jc_config_effective_max_tokens

- **Value:** 0 = auto: contextLength/5 clamped to [512, 16384]; with no window known JC_DEFAULT_MAX_TOKENS 4096
  (include/jc_config.h:828) · **cap**
- **Bounds:** output tokens requested for each model reply
- **Change it with:** config models[].maxTokens
- **When it fires:** named: a reply cut at the ceiling is marked truncated; a delegate is asked once to continue
  (continueCutReply, M859)
- **Source:** reasoned: src/config/jc_config.c:2536-2539 ("~1/5 of the window ... bounded so a huge context doesn't
  request an absurd cap"); always sending max_tokens is incident M84 (include/jc_config.h:830-834)

### `src/config/jc_config.c:953` — hooks.<Event>[].commands[].timeout (hook command timeout)

- **Value:** absent, 0 or negative -> 30 s (JC_PROC_DEF_TIMEOUT 30L, include/jc_proc.h:199) · **cap**
- **Bounds:** wall clock of one hook command; PreToolUse/PostToolUse hooks run on every matching tool call
- **Change it with:** config hook command "timeout" (only a value > 0 changes it; there is no way to say "no limit")
- **When it fires:** kill: process group SIGKILL (src/util/jc_proc.c:780-800); WARN "hook timed out and was killed"
  (src/chat/jc_hooks.c:258), hidden under -q; telemetry hook outcome "timeout" with timeout_s = the CONFIGURED value
- **Source:** none

## `src/harness`

### `src/harness/jc_harness_cred.c:36` — TOKEN_MIN_LEN

- **Value:** 12 bytes · **fence**
- **Bounds:** minimum trimmed token length the credential helper accepts
- **Change it with:** hard-coded
- **When it fires:** named: "could not be loaded" (:256)
- **Source:** none: - ("the reference's 'at least 12 bytes'", cred.c:34-35)

### `src/harness/jc_harness_doctor.c:46` — HDOC_MAX

- **Value:** 16 · **structural**
- **Bounds:** report lines of `jichi-harness doctor` (7 checks today)
- **Change it with:** hard-coded
- **When it fires:** silent: an index past 15 is clamped and overwrites the last slot (:60-61)
- **Source:** reasoned: src/harness/jc_harness_doctor.c:42-45 (the fixed seven checks)

### `src/harness/jc_harness_drive.c:1004` — git_recs timeout

- **Value:** 120 s · **cap**
- **Bounds:** the drive loop's git children on the records repo (add/status/commit :1264-1332) and the mirror
  rev-parse (:1537)
- **Change it with:** hard-coded
- **When it fires:** kill: "snapshot <run> (<kind>): the records commit failed: (no output)" (:1337-1341); timeout
  not named
- **Source:** none

### `src/harness/jc_harness_drive.c:205` — FETCH_DEADLINE_MS

- **Value:** 120000 ms (120 s) · **cap**
- **Bounds:** the drive loop's fork child that runs the in-process mirror fetch and logs it
- **Change it with:** hard-coded
- **When it fires:** kill: SIGKILL of the child (:341-350), then logged as a fetch failure (rc is the kill class)
- **Source:** none

### `src/harness/jc_harness_drive.c:74` — PATH_CAP (drive)

- **Value:** 4352 bytes · **structural**
- **Bounds:** drive loop path joins
- **Change it with:** hard-coded
- **When it fires:** unknown (not verified per join)
- **Source:** reasoned: src/harness/jc_harness_drive.c:72-73

### `src/harness/jc_harness_drive.c:81` — GIT_OUT_CAP (drive)

- **Value:** 256 KiB · **structural**
- **Bounds:** one drive git child's captured output
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_harness_drive.c:96` — QUICK_LIMIT_S

- **Value:** 120 s · **fence**
- **Bounds:** a failed run shorter than this counts as a quick failure
- **Change it with:** hard-coded
- **When it fires:** named: "three quick failures in a row: waiting 10 minutes" (:1906-1907)
- **Source:** reasoned: docs/internal/drive/2026-10-23-harness/harness/loop.sh:19-20 ("so a broken gateway is not
  hammered")

### `src/harness/jc_harness_handoff.c:57` — HANDOFF_MAX_BYTES

- **Value:** 6000 bytes · **cap**
- **Bounds:** the handoff section prepended to the next run's prompt
- **Change it with:** hard-coded
- **When it fires:** named: "- *(cut at 6000 bytes)*" marker, cut back to the last newline (hh_cut :1455-1500)
- **Source:** reasoned: src/harness/jc_harness_handoff.c:1441-1443 ("a section that eats the model's context to
  report on the previous run is worse than no section"); the number is the reference's (handoff.py:23) with no
  rationale

### `src/harness/jc_harness_handoff.c:60` — HANDOFF_MAX_NOTES

- **Value:** 4 · **cap**
- **Bounds:** manager notes shown in the handoff
- **Change it with:** hard-coded
- **When it fires:** named: "and N more" (:1381-1385)
- **Source:** none: - (reference handoff.py:29)

### `src/harness/jc_harness_handoff.c:61` — HANDOFF_MAX_TODOS

- **Value:** 15 · **cap**
- **Bounds:** todo items shown in the handoff
- **Change it with:** hard-coded
- **When it fires:** silent: items past 15 dropped, no count (:1180)
- **Source:** none: - (reference handoff.py:119 items[:15])

### `src/harness/jc_harness_handoff.c:62` — HANDOFF_MAX_TASKS

- **Value:** 8 · **cap**
- **Bounds:** spawn_parallel tasks shown in the handoff
- **Change it with:** hard-coded
- **When it fires:** silent: tasks past 8 dropped, no count (:1297)
- **Source:** none: - (reference handoff.py:143 tasks[:8])

### `src/harness/jc_harness_init.c:186` — git_run_env timeout

- **Value:** 30 s · **cap**
- **Bounds:** every git child of `init`, incl. `clone --no-local` of the source (:849) and the first fetch from
  origin into mirror.git (:962)
- **Change it with:** hard-coded
- **When it fires:** kill: "init: git <what> failed (rc=-2): ..." (git_must :211-212) or "mirror.git: the fetch from
  origin FAILED" (:979, not fatal); timeout not named
- **Source:** unclear: docs/reading/tazuna-02-init-laying-out-a-drive.md:236-237 (a local --no-local clone took ~3
  s; a network clone was not measured)

### `src/harness/jc_harness_init.c:236` — small-child timeout (od canary; also :296 build_stamp --version)

- **Value:** 10 s · **cap**
- **Bounds:** init's canary draw (`od -N12 /dev/urandom`) and the pinned binary's `--version --verbose`
- **Change it with:** hard-coded
- **When it fires:** kill: canary -> named refusal "could not draw the canary (od ... failed)" (:720-721); build
  stamp -> stamp left empty (unknown how reported)
- **Source:** none

### `src/harness/jc_harness_init.c:69` — GIT_OUT_CAP (init)

- **Value:** 256 KiB · **structural**
- **Bounds:** one init git child's captured output
- **Change it with:** hard-coded
- **When it fires:** silent (marker appended, not checked)
- **Source:** none: src/harness/jc_harness_init.c:67-68 ("256 KB is a floor, not a fit")

### `src/harness/jc_harness_init.c:72` — PATH_CAP (init)

- **Value:** 4352 bytes · **structural**
- **Bounds:** init's path joins
- **Change it with:** hard-coded
- **When it fires:** named where checked (e.g. "drive.branchPrefix is too long", :584)
- **Source:** reasoned: src/harness/jc_harness_init.c:71 ("JC_PATH_MAX + 256")

### `src/harness/jc_harness_land.c:52` — LL_CAP_PATH

- **Value:** 4096 bytes · **structural**
- **Bounds:** land path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: a path the cells name + slack (:52)

### `src/harness/jc_harness_land.c:53` — LL_CAP_LINE

- **Value:** 4096 bytes · **structural**
- **Bounds:** one captured git line / summary in land
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_harness_land.c:54` — LL_CAP_SHA

- **Value:** 128 bytes · **structural**
- **Bounds:** a 40-hex id
- **Change it with:** hard-coded
- **When it fires:** silent if exceeded
- **Source:** reasoned: comment :54

### `src/harness/jc_harness_land.c:55` — LL_CAP_SMALL

- **Value:** 512 bytes · **structural**
- **Bounds:** a short reading (m8, m12)
- **Change it with:** hard-coded
- **When it fires:** silent if exceeded
- **Source:** reasoned: comment :55

### `src/harness/jc_harness_mirror.c:56` — PATH_CAP (mirror)

- **Value:** 4352 bytes · **structural**
- **Bounds:** mirror fetch path joins
- **Change it with:** hard-coded
- **When it fires:** mixed: entry joins checked (:818, :829), inner joins unchecked (:189, :228, :271, :303, :706,
  :710) and would truncate silently near the cap
- **Source:** reasoned: src/harness/jc_harness_mirror.c:54-55

### `src/harness/jc_harness_mirror.c:61` — MIRROR_OUT_CAP

- **Value:** 256 KiB · **structural**
- **Bounds:** one mirror fetch / rev-parse child's output
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: src/harness/jc_harness_mirror.c:58-60 ("a floor, not a fit")

### `src/harness/jc_harness_mirror.c:65` — MIRROR_FETCH_TIMEOUT

- **Value:** 60 s · **cap**
- **Bounds:** each git child of `mirror fetch` (git_m2 :463), incl. the fetch of the team's branches
- **Change it with:** hard-coded
- **When it fires:** kill: "the team-branches fetch could not run (rc=-2): ..." (:553); the timeout is not named
- **Source:** reasoned: src/harness/jc_harness_mirror.c:63-64 ("generous but bounded; a local-path fetch over a
  drive clone is seconds" -- no number measured)

### `src/harness/jc_harness_notify.c:70` — NOTIFY_PATH_CAP

- **Value:** 4352 bytes · **structural**
- **Bounds:** notify path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_harness_notify.c:68-69

### `src/harness/jc_harness_notify.c:77` — NOTIFY_TEXT_CAP

- **Value:** 128 KiB · **fence**
- **Bounds:** a note's TEXT
- **Change it with:** hard-coded
- **When it fires:** named: "the TEXT is <n> bytes; it would not fit the 131072-byte note body ... refusing before
  any write" (:556-562)
- **Source:** reasoned: src/harness/jc_harness_notify.c:71-76 (keep the buffer heap-sized; "a truncated verdict is a
  wrong verdict")

### `src/harness/jc_harness_notify.c:87` — NOTIFY_ATTACH_CAP

- **Value:** 1 MiB · **fence**
- **Bounds:** a note's attached file
- **Change it with:** hard-coded
- **When it fires:** named refusal (exit 1)
- **Source:** reasoned: src/harness/jc_harness_notify.c:83-86

### `src/harness/jc_harness_push.c:66` — PATH_CAP (push)

- **Value:** 4352 bytes · **structural**
- **Bounds:** mirror push path joins
- **Change it with:** hard-coded
- **When it fires:** named where checked: "the mirror path is too long to build the stored-copy path" (:1860)
- **Source:** reasoned: src/harness/jc_harness_push.c:64-65

### `src/harness/jc_harness_push.c:73` — PUSH_OUT_CAP

- **Value:** 8 MiB · **cap**
- **Bounds:** the rev-list / cat-file --batch-check / ls-remote capture that lists what a push adds
- **Change it with:** hard-coded
- **When it fires:** silent: jc_proc_capture appends "[output truncated]" but no harness code looks for it (git grep
  finds none); the parser keeps only 40-hex lines (:2530-2543), so objects past the cap would be neither key-scanned
  nor sized (by reading, not exercised)
- **Source:** none: src/harness/jc_harness_push.c:68-72 ("A cap here is a floor for the driver's fixtures, not a
  measured fit")

### `src/harness/jc_harness_push.c:74` — PUSH_SMALL_CAP

- **Value:** 256 KiB · **structural**
- **Bounds:** get-url / for-each-ref / cat-file -s captures
- **Change it with:** hard-coded
- **When it fires:** silent (truncation marker not checked)
- **Source:** none: src/harness/jc_harness_push.c:68-72 ("not a measured fit")

### `src/harness/jc_harness_push.c:75` — PUSH_BLOB_CAP

- **Value:** 64 MiB · **cap**
- **Bounds:** one blob read raw for the key scan
- **Change it with:** hard-coded
- **When it fires:** silent: bytes past 64 MiB would not be scanned
- **Source:** reasoned: src/harness/jc_harness_push.c:68-70 ("a blob can be up to the per-file limit (and the
  reference's incident was 9.7 MB), so the blob capture cap is generous")

### `src/harness/jc_harness_push.c:76` — PUSH_TIMEOUT

- **Value:** 120 s · **cap**
- **Bounds:** every git child of `mirror push` (git_m2 :512) and of jc_hp_git_run (:2236), the shared runner used by
  land, repin, gate, gateverdict, gateledger, gatesweep, backlog, watchgate
- **Change it with:** hard-coded
- **When it fires:** kill: callers report "... failed (rc=-2)" or their refusal text; -2 not named as a timeout
- **Source:** reasoned: docs/internal/drive/2026-11-05-harness-port/reports/2026-11-06-M1147-a1.md:86-89 ("so a hung
  git child cannot wedge a host tick"; the reference push.sh / push-backlog.sh ran git with NO cap)

### `src/harness/jc_harness_push.c:82` — PUSH_MAX_FILE_MB

- **Value:** 8 MiB · **fence**
- **Bounds:** one file version a push may add to the remote (size check)
- **Change it with:** hard-coded (the reference read ZD_PUSH_MAX_FILE_MB)
- **When it fires:** named: "the size check: a file version is <n> MB -- over the limit ..." then "the push was
  refused: the commits stay in the mirror" (:1760-1770)
- **Source:** measured: docs/internal/drive/2026-10-23-harness/harness/pre-push-sizecheck.sh:23-33 (913 commits
  measured 2026-09-28: largest 1.5 MB, API dump 6.6 MB, 9.7 MB mistaken binaries; incident :4-7, a 313 MB copy)

### `src/harness/jc_harness_push.c:83` — PUSH_MAX_TOTAL_MB

- **Value:** 20 MiB · **fence**
- **Bounds:** all file versions one push adds
- **Change it with:** hard-coded (the reference read ZD_PUSH_MAX_TOTAL_MB)
- **When it fires:** named: "the size check: <n> file version(s), <m> MB in all -- over ..." (:1744-1755)
- **Source:** measured: docs/internal/drive/2026-10-23-harness/harness/pre-push-sizecheck.sh:23-35

### `src/harness/jc_harness_push.c:86` — SECRET_MIN_LEN

- **Value:** 12 bytes · **fence**
- **Bounds:** minimum trimmed length of the key/token the key scan searches for (a floor)
- **Change it with:** hard-coded
- **When it fires:** named: "the <label> could not be loaded: shorter than 12 bytes after trimming whitespace ...
  the push is refused" (:788-792)
- **Source:** none: - (reference pre-push-keyscan.sh:39, :47 uses 12 without rationale)

### `src/harness/jc_harness_push.c:95` — PUSH_MAX_TIPS

- **Value:** 24 · **fence**
- **Bounds:** branch tips one push carries through the key scan and size check
- **Change it with:** build flag only (-DPUSH_MAX_TIPS, #ifndef :94)
- **When it fires:** named: resolve_tips returns -2 and the callers refuse instead of dropping (push.c:1141-1148)
- **Source:** none: - (push.c:1141-1148 records the incident that the cap used to drop tips silently and a kept
  attempt carrying a secret went out after a clean scan; it grounds refuse-not-drop, not 24)

### `src/harness/jc_harness_push.c:98` — PUSH_MAX_EXCL

- **Value:** 48 · **fence**
- **Bounds:** exclusion tips (remote + tracking) for the push's object range
- **Change it with:** build flag only (-DPUSH_MAX_EXCL)
- **When it fires:** error-value: the add returns -1 past 48 (:989-990); caller handling not traced
- **Source:** none

### `src/harness/jc_harness_repin.c:616` — rr_run_cmd -> jc_proc_capture(..., 0L, NULL) -> JC_PROC_DEF_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** every repin shell child: the clone (:929), the build `make -j8 WERROR=1 jichi jichi-harness` (:967),
  both --version stamps (:999), the doctor (:1166), the live doctor that calls the model (:1289)
- **Change it with:** hard-coded
- **When it fires:** kill: rc -2 -> rr_run_cmd returns 0 -> "the clone failed" / "the build failed: <log>" (:930,
  :968-970); the timeout is not named
- **Source:** none

### `src/harness/jc_harness_repin.c:74` — RR_CAP_PATH

- **Value:** 4096 bytes · **structural**
- **Bounds:** repin path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: a path the cells name + slack (:74)

### `src/harness/jc_harness_repin.c:75` — RR_CAP_SHA

- **Value:** 128 bytes · **structural**
- **Bounds:** a 40-hex id
- **Change it with:** hard-coded
- **When it fires:** silent if exceeded (not reachable for a valid id)
- **Source:** reasoned: a full 40-hex id + slack (:75)

### `src/harness/jc_harness_repin.c:76` — RR_CAP_SMALL

- **Value:** 512 bytes · **structural**
- **Bounds:** a short reading (s8, m8)
- **Change it with:** hard-coded
- **When it fires:** silent if exceeded
- **Source:** reasoned: comment :76

### `src/harness/jc_harness_repin.c:77` — RR_CAP_LINE

- **Value:** 8192 bytes · **structural**
- **Bounds:** one captured line / summary / meta
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_harness_repin.c:78` — RR_CAP_CMD

- **Value:** 16384 bytes · **structural**
- **Bounds:** one shimmable command line (clone, build, doctor)
- **Change it with:** hard-coded
- **When it fires:** silent: e.g. clone_cmd/build_cmd are built with unchecked jc_snprintf (:922-924, :959-962), so
  an over-long configured command would run truncated (by reading)
- **Source:** none

### `src/harness/jc_harness_repin.c:79` — RR_CAP_KEY

- **Value:** 8192 bytes · **structural**
- **Bounds:** the model key read for the live doctor
- **Change it with:** hard-coded
- **When it fires:** silent: a longer key is cut to 8191 bytes (rr_read_key :556-558)
- **Source:** none

### `src/harness/jc_harness_repin.c:80` — RR_LIVE_CAP

- **Value:** 64 KiB · **structural**
- **Bounds:** the live doctor's combined output
- **Change it with:** hard-coded
- **When it fires:** silent: the capture's "[output truncated]" marker is lost when rr_run_cmd copies cap_out-1
  bytes (:627-633)
- **Source:** none

### `src/harness/jc_harness_run.c:103` — GIT_OUT_CAP (run)

- **Value:** 256 KiB · **structural**
- **Bounds:** the run's rev-parse capture
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: src/harness/jc_harness_run.c:101-102 ("a floor, not a fit")

### `src/harness/jc_harness_run.c:107` — VERSION_OUT_CAP

- **Value:** 4096 bytes · **structural**
- **Bounds:** `--version --verbose` capture
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** reasoned: src/harness/jc_harness_run.c:105-106 ("writes a handful of lines; 4 KB is a floor")

### `src/harness/jc_harness_run.c:477` — rev-parse HEAD timeout

- **Value:** 30 s · **cap**
- **Bounds:** the run's read of the team head
- **Change it with:** hard-coded
- **When it fires:** error-value: returns 0 (empty head) to the caller (:480-490)
- **Source:** none

### `src/harness/jc_harness_run.c:515` — --version capture timeout

- **Value:** 10 s · **cap**
- **Bounds:** the run's version/build stamp of the jichi binary
- **Change it with:** hard-coded
- **When it fires:** error-value (stamp NULL)
- **Source:** none

### `src/harness/jc_harness_run.c:99` — PATH_CAP (run)

- **Value:** 4352 bytes · **structural**
- **Bounds:** run path joins
- **Change it with:** hard-coded
- **When it fires:** unknown (not verified per join)
- **Source:** reasoned: src/harness/jc_harness_run.c:97-98

### `src/harness/jc_hp_backlog.c:93` — BACKLOG_PATH_CAP

- **Value:** 4352 bytes · **structural**
- **Bounds:** backlog path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: local mirror of jc_harness_push.c PATH_CAP (:93)

### `src/harness/jc_hp_backlog.c:94` — BACKLOG_SMALL_CAP

- **Value:** 256 KiB · **structural**
- **Bounds:** is-ancestor / rev-parse / cat-file -t / -s captures
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_backlog.c:95` — BACKLOG_BLOB_CAP

- **Value:** 64 MiB · **cap**
- **Bounds:** one blob read for the backlog key scan
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_backlog.c:98` — BACKLOG_SECRET_MIN

- **Value:** 12 bytes · **fence**
- **Bounds:** the same floor in the backlog push
- **Change it with:** hard-coded
- **When it fires:** named refusal (not traced)
- **Source:** none

### `src/harness/jc_hp_gate.c:520` — jc_capture_cmd -> jc_proc_capture(..., 0L, NULL) -> JC_PROC_DEF_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** the gate's land and repin steps (host.landRun / host.repinRun via gf_capture_cmd :573; calls at :2369,
  :2429) and `approve`'s land step (jc_harness_main.c:2578)
- **Change it with:** hard-coded
- **When it fires:** kill: rc -2 makes jc_capture_cmd return 0 and the caller says "could not run the land step" /
  "could not run the repin step" (gf_die :2370, :2430) although the child ran and was killed
- **Source:** none: - (comment :515-518 reasons only about the 16 KB output cap)

### `src/harness/jc_hp_gate.c:55` — GF_PATH_MAX

- **Value:** 4352 bytes · **structural**
- **Bounds:** gate homes, record paths, the stage command (3*GF_PATH_MAX + 8192)
- **Change it with:** hard-coded
- **When it fires:** error-value: an over-long command word returns 0 (gf_cmd_word), the caller promotes it
- **Source:** reasoned: src/harness/jc_hp_gate.c:52-54 ("the driver's paths are the mkdtemp shape (~160)")

### `src/harness/jc_hp_gate.c:56` — GF_TEXT_MAX

- **Value:** 2048 bytes · **structural**
- **Bounds:** one say / note field
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** reasoned: the joins are <= 640 (:56)

### `src/harness/jc_hp_gate.c:57` — GF_NOTE_MAX

- **Value:** 16384 bytes · **structural**
- **Bounds:** the whole note text
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** reasoned: well under the notify's 128KB TEXT cap (:57-58)

### `src/harness/jc_hp_gate.c:59` — GF_REC_MAX

- **Value:** 512 KiB · **cap**
- **Bounds:** a stage record read back for the verdict
- **Change it with:** hard-coded
- **When it fires:** silent: gf_read_file reads cap-1 bytes (:669) and the verdict takes the record's last line
  (gf_last_line), so a record over 512 KiB yields a mid-record line as the reason (by reading)
- **Source:** reasoned: the driver's records are small (:59-61)

### `src/harness/jc_hp_gate.c:62` — GF_LINE_MAX

- **Value:** 8192 bytes · **structural**
- **Bounds:** one line
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gate.c:770` — gf_git timeout

- **Value:** 120 s · **cap**
- **Bounds:** the gate orchestrator's own D8 setup git children (rev-parse of the READY commit and of master,
  :1516-1536)
- **Change it with:** hard-coded
- **When it fires:** kill: a killed rev-parse reads as "The commit is not in the host's push.git." (gf_red
  :1517-1519)
- **Source:** none

### `src/harness/jc_hp_gate.c:914` — jc_proc_capture(..., 0, 0L, NULL) in gf_stage_child -> JC_PROC_DEF_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** each gate stage child: `gate run --stage ledger|sweep|ci|redfirst|verdict` (incl. the whole make ci)
- **Change it with:** hard-coded (include/jc_proc.h:199; 0 selects it at src/util/jc_proc.c:780)
- **When it fires:** kill: process group SIGKILLed, rc -2 mapped to 1 (jc_hp_gate.c:1594-1595, :1726-1727,
  :1904-1905, :2106-2107) and reported as that stage's RED/INVALID with the record's last line; the word timeout
  never appears
- **Source:** none: - (the comments at :911-913 and the file header :30 say "NO timeout (D14: the ci's 48 minutes
  are the point)")

### `src/harness/jc_hp_gateci.c:87` — GCI_LINE_MAX

- **Value:** 256 KiB · **structural**
- **Bounds:** one captured line, the git command buffer, and the home/log path buffers (:912, :1116-1119)
- **Change it with:** hard-coded
- **When it fires:** error-value for the command (:919-921 returns -1)
- **Source:** none

### `src/harness/jc_hp_gateci.c:945` — gci_git_read / gci_child timeout (also :1047)

- **Value:** 600 s · **cap**
- **Bounds:** the ci stage's keyless git reads (rev-parse, merge-base) and its clone/checkout child (:1380)
- **Change it with:** hard-coded
- **When it fires:** kill: "ci: clone/checkout failed: INVALID" (:1381) or a "checkout mismatch: INVALID"; -2 not
  named as a timeout
- **Source:** none

### `src/harness/jc_hp_gateledger.c:47` — GL_CAP_LINE

- **Value:** 8192 bytes · **structural**
- **Bounds:** one trailer / log-format capture in the ledger
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gateledger.c:48` — GL_CAP_SHA

- **Value:** 96 bytes · **structural**
- **Bounds:** a 40-hex id
- **Change it with:** hard-coded
- **When it fires:** silent if exceeded
- **Source:** reasoned: comment :48

### `src/harness/jc_hp_gateref.c:63` — GREF_LINE_MAX

- **Value:** 8192 bytes · **structural**
- **Bounds:** one line in redfirst
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gateref.c:64` — GREF_CAP_TEXT

- **Value:** 1 MiB · **structural**
- **Bounds:** a command's combined output in redfirst (stack arrays :1205-1206, :1427)
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gateref.c:904` — gref_run timeout

- **Value:** 600 s · **cap**
- **Bounds:** the redfirst stage's git reads, its setup clone (:1071) and each per-commit checkout (:1481)
- **Change it with:** hard-coded
- **When it fires:** kill: "redfirst: setup: INVALID" or the CHECKOUT FAILED line; no timeout named
- **Source:** none

### `src/harness/jc_hp_gatesweep.c:796` — gsw_run timeout

- **Value:** 600 s · **cap**
- **Bounds:** each sweep child: the build (:1427), each smoke driver (:1505), make smoke-mutant (:1632), each SAN
  unit build (:1714), the container stage (:1781, :1838)
- **Change it with:** hard-coded
- **When it fires:** kill: a driver shows as RED rc=-2, the build as "build failed: INVALID", the container as
  "container stage did not run: INVALID"; no timeout named
- **Source:** none

### `src/harness/jc_hp_gatesweep.c:84` — GSW_LINE_MAX

- **Value:** 8192 bytes · **structural**
- **Bounds:** one captured-output line in the sweep
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gatesweep.c:85` — GSW_CAP_TEXT

- **Value:** 1 MiB · **structural**
- **Bounds:** a command's combined output (build log, mutant, unit, container)
- **Change it with:** hard-coded
- **When it fires:** silent: the capture marker is dropped by gsw_run's copy (:799-807)
- **Source:** none

### `src/harness/jc_hp_gatesweep.c:86` — GSW_CAP_DRIVER

- **Value:** 1 MiB · **cap**
- **Bounds:** one smoke driver's output (slot_drv :1199)
- **Change it with:** hard-coded
- **When it fires:** silent: the saved <driver>.txt and the "not ok" count see only the first 1 MiB; red still
  follows rc != 0 (:1511-1512)
- **Source:** none

### `src/harness/jc_hp_gatesweep.c:88` — GSW_NOTOK_MAX

- **Value:** 200 bytes · **cap**
- **Bounds:** a not-ok excerpt line
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference sweep.sh:23 cut -c1-200)

### `src/harness/jc_hp_gatesweep.c:89` — GSW_MUT_MAX

- **Value:** 150 bytes · **cap**
- **Bounds:** a mutant excerpt line
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference sweep.sh:35-36 cut -c1-150)

### `src/harness/jc_hp_gatesweep.c:90` — GSW_UNIT_MAX

- **Value:** 200 bytes · **cap**
- **Bounds:** a unit-failure excerpt line
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference sweep.sh:52 cut -c1-200)

### `src/harness/jc_hp_gateunits.c:38` — UNITS_TEXT_CAP (gate units)

- **Value:** 16384 bytes · **structural**
- **Bounds:** one generated unit file
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_gateunits.c:36-37 ("the template is ~1 KB")

### `src/harness/jc_hp_gateverdict.c:33` — GV_CAP_COUNT

- **Value:** 256 KiB · **structural**
- **Bounds:** a name-only git capture
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gateverdict.c:34` — GV_CAP_BODY

- **Value:** 64 KiB · **cap**
- **Bounds:** one smoke driver's body read by the verdict
- **Change it with:** hard-coded
- **When it fires:** silent: a body over 64 KiB is read in part (by reading)
- **Source:** none

### `src/harness/jc_hp_gateverdict.c:35` — GV_FLAG_MAX

- **Value:** 120 bytes · **cap**
- **Bounds:** a flagged line in the verdict
- **Change it with:** hard-coded
- **When it fires:** silent cut
- **Source:** none: - (reference :55 cut -c1-120)

### `src/harness/jc_hp_gateverdict.c:36` — GV_LINE_MAX

- **Value:** 8192 bytes · **structural**
- **Bounds:** one sweep.txt line
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** reasoned: the cuts are 120 (:36)

### `src/harness/jc_hp_gateverdict.c:37` — GV_CAP_PATH

- **Value:** 1024 bytes · **structural**
- **Bounds:** `<s>:<driver>` argument for cat-file / show
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: comment :37

### `src/harness/jc_hp_gateverdict.c:38` — GV_CAP_GERR

- **Value:** 1024 bytes · **structural**
- **Bounds:** the "could not read" driver list
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_gateverdict.c:39` — GV_CAP_WHY

- **Value:** 2048 bytes · **structural**
- **Bounds:** the red: reason line
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_records.c:105` — SECRET_MIN_LEN (records copy)

- **Value:** 12 bytes · **fence**
- **Bounds:** the same floor for the records push's key scan
- **Change it with:** hard-coded
- **When it fires:** named refusal
- **Source:** none: - (reference pre-push-keyscan.sh:39)

### `src/harness/jc_hp_records.c:88` — PATH_CAP (records)

- **Value:** 4352 bytes · **structural**
- **Bounds:** records push path joins
- **Change it with:** hard-coded
- **When it fires:** unknown (not verified per join)
- **Source:** reasoned: src/harness/jc_hp_records.c:86-87

### `src/harness/jc_hp_records.c:95` — PUSH_OUT_CAP (records copy)

- **Value:** 8 MiB · **cap**
- **Bounds:** rev-list / batch-check capture of the records push
- **Change it with:** hard-coded
- **When it fires:** silent (as push.c:73)
- **Source:** none: src/harness/jc_hp_records.c:90-94 ("not a measured fit")

### `src/harness/jc_hp_records.c:96` — PUSH_SMALL_CAP (records copy)

- **Value:** 256 KiB · **structural**
- **Bounds:** rev-parse capture
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: src/harness/jc_hp_records.c:90-94

### `src/harness/jc_hp_records.c:97` — PUSH_BLOB_CAP (records copy)

- **Value:** 64 MiB · **cap**
- **Bounds:** one records blob read raw for the key scan
- **Change it with:** hard-coded
- **When it fires:** silent: bytes past 64 MiB not scanned
- **Source:** reasoned: src/harness/jc_hp_records.c:90-93 ("generous")

### `src/harness/jc_hp_records.c:98` — PUSH_TIMEOUT (records copy)

- **Value:** 120 s · **cap**
- **Bounds:** git children of the records commit/push (git_sb :430)
- **Change it with:** hard-coded
- **When it fires:** kill: generic failure text, -2 not named
- **Source:** reasoned: same as jc_harness_push.c:76 (M1147-a1.md:86)

### `src/harness/jc_hp_rundrive.c:32` — PATH_CAP (run-drive units)

- **Value:** 4352 bytes · **structural**
- **Bounds:** path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_rundrive.c:30-31

### `src/harness/jc_hp_tick.c:114` — TICK_PATH_CAP

- **Value:** 4352 bytes · **structural**
- **Bounds:** push tick path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_tick.c:112-113

### `src/harness/jc_hp_tick.c:369` — pageCommand timeout

- **Value:** 30 s · **cap**
- **Bounds:** host.pageCommand run by `push tick` when the push keeps failing
- **Change it with:** hard-coded
- **When it fires:** kill: "pageCommand could not be run (rc=-2); the PAGE line was still written" (:372-374); the
  comment (:371) knows -2 is a timeout, the message does not say so
- **Source:** none

### `src/harness/jc_hp_units.c:35` — PATH_CAP (units)

- **Value:** 4352 bytes · **structural**
- **Bounds:** path joins (JC_PATH_MAX 4096 + 256)
- **Change it with:** hard-coded
- **When it fires:** error-value (joins checked with >= sizeof in this file)
- **Source:** reasoned: src/harness/jc_hp_units.c:33-34 ("give the joins room")

### `src/harness/jc_hp_units.c:39` — UNITS_TEXT_CAP (push units)

- **Value:** 16384 bytes · **structural**
- **Bounds:** one generated systemd unit file
- **Change it with:** hard-coded
- **When it fires:** error-value (generation checked against sizeof, :264)
- **Source:** reasoned: src/harness/jc_hp_units.c:37-38 ("the reference's are ~1 KB")

### `src/harness/jc_hp_watchgate.c:627` — wgate_run_list timeout

- **Value:** 120 s · **cap**
- **Bounds:** each shimmable command of `watch gate`: the notify hook (:930), systemctl is-active (:1200), the unit
  list (:1356), pgrep (:1427)
- **Change it with:** hard-coded
- **When it fires:** kill: "the notify hook failed ..." (:932) or the caller's generic text; -2 not named
- **Source:** none

### `src/harness/jc_hp_watchgate.c:67` — WGATE_PATH_CAP

- **Value:** 4352 bytes · **structural**
- **Bounds:** watch gate path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_watchgate.c:65-66

### `src/harness/jc_hp_watchgate.c:72` — WGATE_LOG_CAP

- **Value:** 1 MiB · **structural**
- **Bounds:** a shimmed command's captured output (unit list, pgrep pids)
- **Change it with:** hard-coded
- **When it fires:** silent (marker dropped by the copy :630-637)
- **Source:** none: - (the incident at src/harness/jc_hp_watchgate.c:68-71 is about the LOG this cap used to bound,
  not the 1 MiB)

### `src/harness/jc_hp_watchgate.c:74` — WGATE_SHA_CAP

- **Value:** 64 bytes · **structural**
- **Bounds:** a 7-40-hex commit token
- **Change it with:** hard-coded
- **When it fires:** error-value (unit name checked, :1195-1197)
- **Source:** reasoned: comment :73

### `src/harness/jc_hp_watchgate.c:77` — WGATE_BUSY_CAP

- **Value:** 4096 bytes · **structural**
- **Bounds:** the busy-set say line
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/harness/jc_hp_watchgate.c:78` — WGATE_CMD_CAP

- **Value:** 4096 bytes · **structural**
- **Bounds:** a shimmable command string
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** none

### `src/harness/jc_hp_watchgate.c:80` — WGATE_MAX_ARGV

- **Value:** 40 · **structural**
- **Bounds:** argv slots of a shimmable command + appended args
- **Change it with:** hard-coded
- **When it fires:** silent: arguments past 39 are dropped by the loop condition (:613-620) and the command runs
  anyway
- **Source:** none

### `src/harness/jc_hp_watchunits.c:44` — PATH_CAP (watch units)

- **Value:** 4352 bytes · **structural**
- **Bounds:** path joins
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_watchunits.c:42-43

### `src/harness/jc_hp_watchunits.c:47` — UNITS_TEXT_CAP (watch units)

- **Value:** 16384 bytes · **structural**
- **Bounds:** one generated unit file
- **Change it with:** hard-coded
- **When it fires:** unknown
- **Source:** reasoned: src/harness/jc_hp_watchunits.c:45-46

## `src/index`

### `src/index/jc_docs.c:32` — DOCS_URL_TTL

- **Value:** 86400 s · **cap**
- **Bounds:** age after which a cached documentation URL is re-fetched
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: jc_docs.c:31 ("(1 day)" restates the value)

### `src/index/jc_docs.c:34` — DOCS_FETCH_MAX

- **Value:** 2097152 bytes · **cap**
- **Bounds:** a fetched documentation page kept before stripping to text and indexing
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/index/jc_docs.c:37` — DOCS_FETCH_TIMEOUT_SECS

- **Value:** 30 s · **cap**
- **Bounds:** total wall-clock of a documentation-URL fetch
- **Change it with:** hard-coded (connect / deadStream fences from config)
- **When it fires:** error-value ("error: fetching <url> failed (model stalled (timed out))" once connected) unless
  a stale cache exists, which is then used silently
- **Source:** none: same M1093 "measured values" claim with no measurement (see REF_RSS_FETCH_TIMEOUT_SECS)

### `src/index/jc_index.c:49` — CHUNK_CHARS

- **Value:** 1500 chars · **structural**
- **Bounds:** size of one indexed chunk (lines are grouped until this is passed)
- **Change it with:** hard-coded
- **When it fires:** silent (shapes what a search hit contains)
- **Source:** none

### `src/index/jc_index.c:50` — MAX_FILE_BYTES

- **Value:** 1048576 bytes · **cap**
- **Bounds:** files larger than this are left out of the embedding index (codebase_search); also caps a PDF's
  extracted text when indexing docs
- **Change it with:** hard-coded
- **When it fires:** silent (the file is skipped; search simply never returns it)
- **Source:** none

### `src/index/jc_index.c:53` — PDF_MAX_FILE_BYTES

- **Value:** 33554432 bytes · **cap**
- **Bounds:** raw size of a PDF accepted for docs indexing
- **Change it with:** hard-coded
- **When it fires:** silent (skipped)
- **Source:** reasoned: jc_index.c:51-52 ("embedded images inflate file size", hence larger than MAX_FILE_BYTES)

### `src/index/jc_index.c:808` — jc_walk_ctx_init(root, 128, 500000)

- **Value:** depth 128; 500000 entries · **cap**
- **Bounds:** directory depth and entries the embedding-index walk visits
- **Change it with:** hard-coded
- **When it fires:** silent (jc_walk sets ctx->capped, which no caller reads)
- **Source:** none: bound added after D-27/D-29 (symlink loop grew memory to 1.94 GB,
  docs/internal/reviews/2026-10-17-hardening/D-files-persistence.md:812-825); the digits are not explained

### `src/index/jc_lexical.c:17` — MAX_QTERMS

- **Value:** 64 terms · **cap**
- **Bounds:** distinct query terms scored by the BM25-lite half of hybrid search
- **Change it with:** hard-coded
- **When it fires:** silent ("extra are ignored")
- **Source:** none

### `src/index/jc_lexical.c:18` — TOK_MAX

- **Value:** 64 bytes · **structural**
- **Bounds:** longest token kept by the lexical scorer (longer tokens are cut to 63 bytes)
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/index/jc_repomap.c:21` — RM_MAX_FILE_BYTES

- **Value:** 524288 bytes · **cap**
- **Bounds:** source files larger than this are left out of the repo map the model sees in its system message
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: docs/ROADMAP.md:5234 explains why it is NOT configurable, not why 512 KB

### `src/index/jc_repomap.c:760` — jc_walk_ctx_init(root, 128, JC_REPOMAP_MAX_FILES)

- **Value:** depth 128 · **cap**
- **Bounds:** directory depth of the repo-map walk
- **Change it with:** hard-coded
- **When it fires:** silent (ctx->capped is never read)
- **Source:** none: same D-29 incident as the index walk; the digits are not explained

### `src/index/jc_retrieve.c:16` — DEF_TOP_K

- **Value:** 5 results · **cap**
- **Bounds:** results returned by codebase_search / docs retrieval when the caller gives none
- **Change it with:** tool argument top_k (codebase_search), max_results (search_docs); config autoContextTopK for
  auto-context
- **When it fires:** silent (the best 5 are returned)
- **Source:** none: jc_retrieve.c:15 ("matches the historical jc_search defaults")

### `src/index/jc_retrieve.c:17` — DEF_CAND_FACTOR

- **Value:** 4 (x top_k) · **cap**
- **Bounds:** candidates fetched per ranking before fusion and rerank
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: jc_retrieve.c:15

### `src/index/jc_retrieve.c:18` — DEF_CAND_MIN

- **Value:** 20 candidates · **cap**
- **Bounds:** floor on candidates fetched before fusion and rerank
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none: jc_retrieve.c:15

### `src/index/jc_walk.c:25` — PATH_MAX (fallback)

- **Value:** 4096 bytes · **structural**
- **Bounds:** path buffers in the shared walker when the platform does not define PATH_MAX
- **Change it with:** hard-coded
- **When it fires:** silent (realpath into a too-small buffer would be the failure)
- **Source:** none

## `src/json`

### `src/json/cJSON.c:107` — JC_JSON_MAX_NODES

- **Value:** 1,000,000 nodes · **fence**
- **Bounds:** nodes one parse may allocate (each value is a 64-byte node; ~40x the input bytes)
- **Change it with:** hard-coded (test-only setter cJSON_jc_set_max_nodes)
- **When it fires:** named in the stream path: reply marked truncated + WARN naming JC_JSON_MAX_NODES
  (src/provider/jc_provider_openai.c:414, jc_provider_anthropic.c:395); error-value elsewhere
- **Source:** measured: src/json/cJSON.c:89-106 (a 1,000,000-element array = ~78 MB peak RSS; largest legitimate
  parse ~524,000 nodes); docs/ROADMAP.md:55075

### `src/json/cJSON.c:87` — JC_JSON_MAX_DEPTH

- **Value:** 256 levels · **fence**
- **Bounds:** nesting depth of any JSON jichi parses (model tool arguments, MCP/LSP/ACP replies, configs)
- **Change it with:** hard-coded
- **When it fires:** error-value: cJSON_Parse returns NULL, the same as malformed JSON (src/json/cJSON.c:603-606)
- **Source:** measured: src/json/cJSON.c:71-86 (SIGSEGV measured at ~75,000 levels on an 8 MB stack, ~2,000 at 128
  KB); docs/analysis/2026-08-17-source-hardening-audit.md §H4

## `src/lsp`

### `src/lsp/jc_lsp.c:35` — LSP_INIT_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** the language server's initialize handshake
- **Change it with:** hard-coded
- **When it fires:** named, generic: diagnostics say "[lsp <name>: unavailable]" for the rest of the session; the
  log says "initialize failed"
- **Source:** none

### `src/lsp/jc_lsp.c:36` — LSP_DIAG_TIMEOUT

- **Value:** 15 s · **cap**
- **Bounds:** wait for publishDiagnostics after opening/editing a file (post-edit diagnostics, @problems)
- **Change it with:** hard-coded
- **When it fires:** named, ambiguous: "[lsp <name>: no diagnostics received]" (does not say the wait timed out)
- **Source:** none

### `src/lsp/jc_lsp.c:42` — LSP_DIAG_GRACE

- **Value:** 2.0 s · **cap**
- **Bounds:** how long to keep reading after the first diagnostics batch (a server may re-publish)
- **Change it with:** hard-coded
- **When it fires:** silent (a later batch is missed and the file can read clean)
- **Source:** measured: jc_lsp.c:37-41 + docs/ROADMAP.md:51769 (M938): the review's clangd reproduction had the real
  set 0.6 s behind the empty one; the mock proves it at 1.4 s; grown 0.4 -> 2.0

### `src/lsp/jc_lsp.c:43` — LSP_NAV_TIMEOUT

- **Value:** 10 s · **cap**
- **Bounds:** definition / references / symbols / rename / format / code-action round trip
- **Change it with:** hard-coded
- **When it fires:** named, generic ("[lsp: no response from server]")
- **Source:** none

### `src/lsp/jc_lsp.c:44` — LSP_CA_DIAG_TIMEOUT

- **Value:** 3 s · **cap**
- **Bounds:** diagnostics wait before asking for code actions
- **Change it with:** hard-coded
- **When it fires:** silent (code actions are requested with an empty diagnostics context)
- **Source:** none

## `src/main`

### `src/main.c:10458` — JC_GC_KEEP_DEFAULT

- **Value:** 20 refs · **cap**
- **Bounds:** discarded checkpoint refs kept per repository by `checkpoints gc` regardless of age
- **Change it with:** --keep
- **When it fires:** named (reports kept/expired counts; removes only with --yes)
- **Source:** none: docs/proposals/2026-08-work-preservation.md:417 argues the policy shape (age plus count), not 20

### `src/main.c:10459` — JC_GC_KEEP_DAYS_DEFAULT

- **Value:** 7 days · **cap**
- **Bounds:** age within which discarded checkpoint refs are kept by `checkpoints gc`
- **Change it with:** --older-than
- **When it fires:** named (reported; removed only with --yes)
- **Source:** none: docs/proposals/2026-08-work-preservation.md:417 (shape, not 7)

### `src/main.c:10472` — gc_dir_kb: jc_proc_capture(du -sk, 4096, 20)

- **Value:** 20 s · **cap**
- **Bounds:** `du` sizing one checkpoint store
- **Change it with:** hard-coded
- **When it fires:** silent (size unknown, -1)
- **Source:** none

### `src/main.c:10514` — gc for-each-ref: jc_proc_capture(1 MiB, 30)

- **Value:** 30 s; 1048576 bytes · **cap**
- **Bounds:** listing a repo's discarded refs for retention
- **Change it with:** hard-coded
- **When it fires:** silent (returns early: the repo counts no expired refs)
- **Source:** none

### `src/main.c:10557` — gc update-ref -d: jc_proc_capture(0, 30)

- **Value:** 30 s · **cap**
- **Bounds:** deleting one expired ref
- **Change it with:** hard-coded
- **When it fires:** silent ((void) result)
- **Source:** none

### `src/main.c:10573` — gc git gc --prune=now: jc_proc_capture(0, 300)

- **Value:** 300 s · **cap**
- **Bounds:** packing a repo after expired refs are deleted
- **Change it with:** hard-coded
- **When it fires:** silent ((void) result; space not reclaimed)
- **Source:** none

### `src/main.c:10610` — gc_remove_repo: jc_proc_capture(rm -rf, 0, 120)

- **Value:** 120 s · **cap**
- **Bounds:** removing one orphaned checkpoint repository
- **Change it with:** hard-coded
- **When it fires:** silent (not counted as removed; may leave it half-deleted)
- **Source:** none

### `src/main.c:10694` — gc core.worktree: jc_proc_capture(4096, 10)

- **Value:** 10 s · **cap**
- **Bounds:** reading where a shadow repo's work tree is
- **Change it with:** hard-coded
- **When it fires:** silent (the repo classifies as unreachable, so it is never removed)
- **Source:** none

### `src/main.c:11691` — MAIN_HELPER_COMPLETE_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total of the `complete` and `fim` one-shot helper calls
- **Change it with:** hard-coded (connect / deadStream fences from config)
- **When it fires:** named, misleading ("error: completion failed (HTTP <status>)", no mention of a timeout)
- **Source:** none: same M1093 "measured values" claim with no measurement

### `src/main.c:12685` — improve_run_verify: jc_proc_capture(sh -c verify, 262144, 600)

- **Value:** 600 s; 262144 bytes · **cap**
- **Bounds:** an assignment spec's verify command during `improve`
- **Change it with:** hard-coded
- **When it fires:** silent (rc -2 is scored as FAIL; no "timed out" text)
- **Source:** none

### `src/main.c:12729` — improve --attempt cap

- **Value:** 5 specs · **cap**
- **Bounds:** failing specs one `improve --attempt` run attempts
- **Change it with:** hard-coded
- **When it fires:** named ("- <spec>: FAIL (not attempted; cap 5)")
- **Source:** none

### `src/main.c:12873` — spec setup: jc_proc_capture(sh -c setup, 65536, 120)

- **Value:** 120 s · **cap**
- **Bounds:** an assignment spec's setup script before an attempt (improve --attempt, attempt)
- **Change it with:** hard-coded
- **When it fires:** silent (result ignored; the attempt runs on a half-set-up tree)
- **Source:** none

### `src/main.c:12931` — rmdir worktree base: jc_proc_capture(0, 20)

- **Value:** 20 s · **cap**
- **Bounds:** removing the empty per-run worktree base dir (improve --attempt, attempt)
- **Change it with:** hard-coded
- **When it fires:** silent (result ignored)
- **Source:** none

### `src/main.c:13897` — workflow verify stage: jc_proc_capture(sh -c cmd, 262144, 600)

- **Value:** 600 s · **cap**
- **Bounds:** a workflow `verify` stage's command
- **Change it with:** hard-coded
- **When it fires:** named, indirect ("verify `cmd`: FAIL (exit -2)" goes into the context a later synthesize stage
  reads)
- **Source:** none

### `src/main.c:14023` — workflow synthesize: jc_oneshot_ex(..., 180, ...)

- **Value:** 180 s · **cap**
- **Bounds:** the one model call of a workflow `synthesize` stage
- **Change it with:** hard-coded
- **When it fires:** named ("synthesize produced no output (model timed out); keeping the stage inputs")
- **Source:** none

### `src/main.c:17496` — JC_DAEMON_MAX_WORKERS

- **Value:** 64 workers · **cap**
- **Bounds:** concurrent forked daemon workers; the auto size is min(cpu, 4) (src/main.c:17748)
- **Change it with:** config daemonWorkers (clamped to 64)
- **When it fires:** named, indirect (the start line prints the clamped "N worker(s)"; nothing says it was clamped)
- **Source:** none

### `src/main.c:17752` — daemon worker watchdog (daemonWorkerTimeout default)

- **Value:** 300 s · **cap**
- **Bounds:** one daemon request's whole agent turn
- **Change it with:** config daemonWorkerTimeout; 0 or absent = 300 s (cannot be switched off)
- **When it fires:** kill: named on the daemon's stderr ("daemon: worker N exceeded 300s -> killing"); the client
  sees only EOF
- **Source:** none

### `src/main.c:18374` — control client reply wait

- **Value:** 300 s · **cap**
- **Bounds:** how long `jichi control` waits for the run's one-line reply
- **Change it with:** env JC_CONTROL_CLIENT_SECS (>= 1)
- **When it fires:** named ("no reply within the wait ... the command was delivered and is queued" [exit 3])
- **Source:** reasoned: src/main.c:18369-18372 ("served at the run's next tool boundary, which behind a slow model
  call can be minutes away")

### `src/main.c:19945` — packages recommend: jc_oneshot(..., 60, ...)

- **Value:** 60 s; 30 file names · **cap**
- **Bounds:** the recommendation call of `packages recommend` (and the 30 top-level names it sends, :19937)
- **Change it with:** hard-coded
- **When it fires:** named, generic ("(recommendation unavailable; catalog:)")
- **Source:** none

### `src/main.c:4829` — worktree prune: jc_proc_capture(rm -rf, 0, 20)

- **Value:** 20 s · **cap**
- **Bounds:** deleting one stale attempt worktree
- **Change it with:** hard-coded
- **When it fires:** named, generic ("warn: could not delete worktree <id>")
- **Source:** none

### `src/main.c:5936` — JC_RUNS_DEFAULT_LIMIT

- **Value:** 20 rows · **cap**
- **Bounds:** rows `jichi runs` lists
- **Change it with:** --all
- **When it fires:** named ("(N more; use --all)"; JSON has shown/total)
- **Source:** none

### `src/main.c:5990` — PS_MAX

- **Value:** 64 runs · **structural**
- **Bounds:** live runs `jichi ps` can list
- **Change it with:** hard-coded
- **When it fires:** silent (ps_row_for returns NULL past 64)
- **Source:** none

### `src/main.c:7787` — setup_detect_lang budget

- **Value:** 2000 entries; depth 4 · **cap**
- **Bounds:** entries the setup wizard scans to guess a project's language (depth bound at src/main.c:7738)
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/main.c:819` — JC_MAX_POS

- **Value:** 64 args · **structural**
- **Bounds:** positional command-line arguments kept
- **Change it with:** hard-coded
- **When it fires:** silent (arguments past 64 are dropped)
- **Source:** none

### `src/main.c:8858` — setup --advisor / --onboard: jc_oneshot_ex(..., 240, NULL, ...)

- **Value:** 240 s · **cap**
- **Bounds:** the advisor call, and each of onboarding's two calls (src/main.c:8978, :8996)
- **Change it with:** hard-coded
- **When it fires:** named ("advisor: the model timed out (240s)..."; onboard: "returned no analysis (timed out)")
- **Source:** reasoned: commit 5deab7f7 2026-07-07 ("Raise the advisor's jc_oneshot timeout 120s -> 240s; ... some
  self-hosted models are slow to first byte"); no measurement

## `src/mcp`

### `src/mcp/jc_mcp.c:28` — JC_MCP_FQNAME_CAP

- **Value:** 65 bytes (64 + NUL) · **structural**
- **Bounds:** registered name of an MCP tool, "<server>__<tool>"
- **Change it with:** hard-coded
- **When it fires:** silent: name cut at 64 bytes (jc_mcp_tool_fqname, src/mcp/jc_mcp_proto.c:589-609)
- **Source:** reasoned: src/mcp/jc_mcp.c:28 comment "provider tool-name limit (64) + NUL"

### `src/mcp/jc_mcp_http.c:32` — MCP_HTTP_TIMEOUT_SECS

- **Value:** 120 s · **cap**
- **Bounds:** total wall time of one MCP-over-HTTP request (initialize, tools/list, tools/call)
- **Change it with:** hard-coded
- **When it fires:** named: tool result "error: MCP tool call to '<server>' failed (<status>)"
  (src/mcp/jc_mcp.c:133)
- **Source:** none

### `src/mcp/jc_mcp_stdio.c:284` — stdio request skip guard [route X]

- **Value:** 10,000 messages · **fence**
- **Bounds:** messages (notifications, stray ids) a stdio MCP request skips while waiting for its own reply
- **Change it with:** hard-coded
- **When it fires:** error-value: JC_ERR_IO with no log line, then "MCP tool call ... failed"
- **Source:** none

### `src/mcp/jc_mcp_stdio.c:35` — MCP_IO_TIMEOUT_SECS

- **Value:** 120 s · **cap**
- **Bounds:** wait for ONE newline-terminated message from a stdio MCP server
- **Change it with:** hard-coded
- **When it fires:** named: log "mcp: timed out waiting for response" (src/mcp/jc_mcp_stdio.c:219), then the tool
  result "error: MCP tool call ... failed"
- **Source:** none

## `src/net`

### `src/net/jc_audiogen.c:19` — JC_NET_AUDIOGEN_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total time of one speech-generation call
- **Change it with:** hard-coded
- **When it fires:** named but misattributed after connect (by reading)
- **Source:** unclear: literal from 7ad50f86 2026-06-25 M32 without rationale; M1093 report names no measurement

### `src/net/jc_embed.c:14` — JC_EMBED_BATCH

- **Value:** 64 texts · **structural**
- **Bounds:** texts per embeddings request
- **Change it with:** hard-coded
- **When it fires:** silent (batching only)
- **Source:** none

### `src/net/jc_embed.c:172` — error-body log cap (literal 2048 at five sites)

- **Value:** 2048 B · **cap**
- **Bounds:** bytes of a failed side call's response body written to the log
- **Change it with:** hard-coded
- **When it fires:** silent: first 2048 bytes kept, no marker (net_util.c:173-191)
- **Source:** reasoned: jc_embed.c:169-171 "so it cannot straddle jc_logf's redaction buffer"

### `src/net/jc_http.c:1108` — header_collect line buffer

- **Value:** 1024 B · **structural**
- **Bounds:** one response header line kept for callers that ask for headers
- **Change it with:** hard-coded
- **When it fires:** silent: a header of 1024 bytes or more is dropped (jc_http.c:1112-1114)
- **Source:** none

### `src/net/jc_http.c:1150` — redirect_hop_header line buffer

- **Value:** 64 B · **structural**
- **Bounds:** one status line read by the 3xx hop counter of the redirect guard
- **Change it with:** hard-coded
- **When it fires:** silent: a status line of 64 bytes or more is ignored, so its 3xx is not counted
  (jc_http.c:1158-1160, 1186-1189)
- **Source:** none

### `src/net/jc_http.c:1507` — CURLOPT_MAXREDIRS

- **Value:** 5 hops · **fence**
- **Bounds:** redirects followed by any request that follows redirects
- **Change it with:** hard-coded
- **When it fires:** named: curl's own error text (CURLE_TOO_MANY_REDIRECTS) through the generic branch of
  jc_http_describe_failure (jc_http.c:486-487)
- **Source:** none: jc_http.c:1493-1497 grounds the hop cap's EXISTENCE in an incident (M472: credentialed requests
  followed unlimited redirects); 5 is not explained

### `src/net/jc_http.c:777` — RD_SET_CAP

- **Value:** 16 entries · **structural**
- **Bounds:** (family, address, port) keys the redirect guard records at hop 0 of one transfer
- **Change it with:** hard-coded
- **When it fires:** silent: a 17th hop-0 address is not recorded (jc_http.c:834-842); a later self-redirect to it
  is then judged by the private-address classifier alone
- **Source:** none

### `src/net/jc_imagegen.c:21` — JC_NET_IMAGEGEN_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total time of one image-generation side call, per leg (the POST and the URL fetch each get 60 s)
- **Change it with:** hard-coded
- **When it fires:** named but misattributed (by reading): side calls run with stall 0, so jc_http_describe_failure
  (jc_http.c:425-488) never names the total cap; a reply still arriving when 60 s run out is reported as "connected,
  then the stream went dead -- nothing arrived (timeouts.deadStream)" (jc_http.c:474-477)
- **Source:** unclear: the literal came with 7ad50f86 2026-06-25 "M32: media generation tools (generate_image /
  generate_audio)" without rationale; 712beb40 2026-10-30 M1093 named it, and its report
  (docs/internal/drive/2026-10-17-hardening/reports/2026-10-30-m1093.md:27) says "keeping the measured values" but
  names no measurement

### `src/net/jc_imagegen.c:26` — ENVELOPE_JSON_SLACK

- **Value:** 2048 B · **structural**
- **Bounds:** JSON framing allowance added to the b64 image transfer cap
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** reasoned: jc_imagegen.c:23-25 "a few hundred bytes. 2048 is generous framing slack"

### `src/net/jc_transcribe.c:20` — JC_NET_TRANSCRIBE_TIMEOUT_SECS

- **Value:** 120 s · **cap**
- **Bounds:** total time of one transcription call, upload included
- **Change it with:** hard-coded
- **When it fires:** named but misattributed after connect (by reading), as for the image cap
- **Source:** unclear: literal from 3e9adcd2 2026-06-25 "M33: audio input / transcription (transcribe_audio)"
  without rationale; M1093 report "keeping the measured values" names none

### `src/net/net_util.c:128` — dead-stream fence for side calls without a config (literal 900L)

- **Value:** 900 s · **fence**
- **Bounds:** a connected side call that receives nothing
- **Change it with:** timeouts.deadStream where a config is passed; every net_util, image, audio and transcribe site
  passes NULL, so for them hard-coded
- **When it fires:** named: "connected, then the stream went dead -- nothing arrived (timeouts.deadStream)"
  (jc_http.c:474-477)
- **Source:** measured: docs/ROADMAP.md:46864-46867 (M-L2: healthy calls max 540 s over 12,464; one dead call ran
  11,677 s)

### `src/net/net_util.c:21` — JC_NET_POST_TIMEOUT_SECS

- **Value:** 60 s · **cap**
- **Bounds:** total time of one embeddings or rerank POST (jc_net_post_json)
- **Change it with:** hard-coded (jc_net_post_json passes no config, net_util.c:164, so CLI/global timeouts never
  reach it)
- **When it fires:** named but misattributed after connect (by reading): "connected, then the stream went dead --
  nothing arrived (timeouts.deadStream)" (jc_http.c:474-477)
- **Source:** unclear: net_util.c:16-20 says "The value is measured-working" but names no measurement; literal from
  0dd6feb1 2026-06-18 "Add embedding/rerank models and semantic codebase search"; named by 712beb40 2026-10-30 M1093

### `src/net/net_util.c:22` — JC_NET_REACHABLE_TIMEOUT_SECS

- **Value:** 2 s · **cap**
- **Bounds:** total time of the liveness probe GET /models when the caller passes <= 0
- **Change it with:** caller argument; no config key
- **When it fires:** never fires as written: the total is raised to the connect fence, 30 s by default
  (net_util.c:234-236); logging is muted for the probe (net_util.c:241-246)
- **Source:** none: net_util.c:224-233 (M1144 H6): the measured incident (doctor's false FAIL 2026-11-05, TLS
  handshakes of 9.9/11.7/10.7 s against a 3 s total) made the argument a floor; it grounds the override, not the 2

### `src/net/net_util.c:23` — JC_NET_MODELS_TIMEOUT_SECS

- **Value:** 5 s · **cap**
- **Bounds:** total time of the model listing GET /models when the caller passes <= 0
- **Change it with:** caller argument; no config key
- **When it fires:** named but misattributed (by reading): when this total fires during a slow TLS handshake the
  message names the connect fence instead: "TCP connected, but the TLS handshake did not finish within 30s
  (timeouts.connect covers it)" (jc_http.c:436-447)
- **Source:** unclear: literal from 4fd70f90 2026-07-07 "setup: LLM-assisted advisor, config subcommand, one-shot +
  model listing"; net_util.c:19 "measured-working" names no measurement

### `src/net/net_util.c:24` — JC_NET_MODEL_LISTED_TIMEOUT_SECS

- **Value:** 6 s · **cap**
- **Bounds:** total time of the "is this model id listed" GET when the caller passes <= 0
- **Change it with:** caller argument; no config key
- **When it fires:** named but misattributed during TLS (by reading), as for JC_NET_MODELS_TIMEOUT_SECS
- **Source:** unclear: function from c0739f02 2026-09-21 "M689: two seams that only driving could find";
  net_util.c:19 "measured-working" names no measurement

### `src/net/net_util.c:25` — JC_NET_MODEL_LIMITS_TIMEOUT_SECS

- **Value:** 6 s · **cap**
- **Bounds:** total time of the context-window lookup (model_info) when the caller passes <= 0
- **Change it with:** caller argument; no config key
- **When it fires:** named but misattributed during TLS (by reading); the caller gets an error value and the context
  window stays unknown
- **Source:** unclear: literal from 0a199c3d 2026-08-19 "net,doctor,config: read the context window the gateway
  publishes, and stop capping one in silence"; "measured-working" names no measurement

## `src/provider`

### `src/provider/prov_internal.h:16` — JC_PROV_MAX_CALLS

- **Value:** 32 slots · **cap**
- **Bounds:** tool calls one streamed reply can carry (a slot per stream index)
- **Change it with:** hard-coded
- **When it fires:** silent: a call whose index is >= 32 is dropped with `continue` (jc_provider_openai.c:272-274;
  jc_provider_anthropic.c:409, 435, 508)
- **Source:** none

### `src/provider/prov_internal.h:17` — JC_PROV_ID_LEN

- **Value:** 96 B · **structural**
- **Bounds:** tool-call id kept per slot
- **Change it with:** hard-coded
- **When it fires:** silent: a longer id is cut to 95 bytes by jc_snprintf (jc_provider_anthropic.c:413,
  jc_provider_openai.c:513)
- **Source:** none

### `src/provider/prov_internal.h:25` — JC_PROV_SB_KEEP_TEXT

- **Value:** 64 KiB · **structural**
- **Bounds:** text buffer capacity kept between streams; above it the buffer is released
- **Change it with:** hard-coded
- **When it fires:** silent (memory only)
- **Source:** reasoned: prov_internal.h:18-24 (M218: one 4 MB message parked its high-water for the session)

### `src/provider/prov_internal.h:26` — JC_PROV_SB_KEEP_ARGS

- **Value:** 8 KiB · **structural**
- **Bounds:** per-call argument buffer capacity kept between streams
- **Change it with:** hard-coded
- **When it fires:** silent (memory only)
- **Source:** reasoned: prov_internal.h:18-24 (M218)

### `src/provider/prov_internal.h:34` — JC_PROV_MAX_TEXT

- **Value:** 4 MiB · **cap**
- **Bounds:** running total of assistant text accumulated from one stream
- **Change it with:** hard-coded (jc_prov_set_max_text is test-only)
- **When it fires:** named: the model is told "[jichi] Your last reply was cut at jichi's per-reply size limit,
  part-way through..." (jc_agent.c:2973-2976) and the log says "[cut] the reply hit jichi's per-reply size cap"
- **Source:** reasoned: prov_internal.h:28-33 "4 MiB of assistant text mirrors JC_SSE_FIELD_MAX";
  docs/ROADMAP.md:55079 (M1090, finding A-07)

### `src/provider/prov_internal.h:35` — JC_PROV_MAX_ARGS

- **Value:** 1 MiB · **cap**
- **Bounds:** argument bytes of one tool call in one stream
- **Change it with:** hard-coded (jc_prov_set_max_args is test-only)
- **When it fires:** named: tool result "error: the arguments to `<tool>` were cut at jichi's per-call size limit,
  so they are incomplete..." (jc_tool.c:2043-2049)
- **Source:** reasoned: prov_internal.h:28-33 "1 MiB of arguments per tool call is generous for any legit single
  call (file contents travel through read_file, not inline args)"

## `src/session`

### `src/session/jc_session.c:1127` — session alias length [route X]

- **Value:** 64 chars · **fence**
- **Bounds:** length of a session alias
- **Change it with:** hard-coded
- **When it fires:** error-value: alias refused as invalid (jc_session_alias_valid)
- **Source:** none

### `src/session/jc_session.c:1628` — resume-drift path list [route X]

- **Value:** 128 paths · **cap**
- **Bounds:** files listed in a resume-drift report (M350)
- **Change it with:** hard-coded
- **When it fires:** silent: paths past 128 are not listed
- **Source:** none

### `src/session/jc_session.c:173` — session auto-title length [route X]

- **Value:** 60 bytes · **structural**
- **Bounds:** title taken from the first user message
- **Change it with:** hard-coded
- **When it fires:** silent: byte cut, can split a UTF-8 character
- **Source:** none

## `src/setup`

### `src/setup/jc_setup.c:230` — context_limit (small-local preset)

- **Value:** 6000 tokens · **cap**
- **Bounds:** the context window the wizard writes for a small local model
- **Change it with:** config contextLimit (written by the wizard only when unset)
- **When it fires:** unknown (enforced outside this shard)
- **Source:** reasoned: src/setup/jc_setup.c:220-224 ("~half a typical 8k local window because the byte estimate
  under-counts (M77 calibration then tightens it)")

### `src/setup/jc_setup.c:242` — max_parallel (lean host)

- **Value:** 1 · **fence**
- **Bounds:** parallel agents the wizard writes for a small host
- **Change it with:** config maxParallelAgents
- **When it fires:** unknown (enforced outside this shard)
- **Source:** reasoned: src/setup/jc_setup.c:233-237 (each agent is a forked process with its own libcurl handle and
  arena)

## `src/snapshot`

### `src/snapshot/jc_snapshot.c:1278` — JC_FP_FILE_MAX

- **Value:** 8 MiB · **cap**
- **Bounds:** files hashed by content in the worktree fingerprint
- **Change it with:** hard-coded
- **When it fires:** silent: a larger file counts by size and mtime (:1341-1345), so a same-size same-mtime change
  is invisible
- **Source:** none

### `src/snapshot/jc_snapshot.c:35` — JC_SNAPSHOT_MAX_FILES

- **Value:** 20000 files · **cap**
- **Bounds:** files in a non-git workspace before snapshots (undo) turn themselves off
- **Change it with:** hard-coded
- **When it fires:** named (WARN log): "snapshots: workspace has >20000 files and no git; disabled (set
  \"snapshots\": false to silence)" (:543-545)
- **Source:** reasoned: src/snapshot/jc_snapshot.c:31-33 ("a `git add -A` would be too heavy")

## `src/tools`

### `src/tools/jc_tool.c:1358` — JC_REFUSAL_LIST_MAX

- **Value:** 10 names · **cap**
- **Bounds:** tool names listed in a fence refusal
- **Change it with:** hard-coded
- **When it fires:** named ("+N more")
- **Source:** reasoned: jc_tool.c:1355-1357 ("Enough to show the whole core profile (8 tools)")

### `src/tools/jc_tool.c:787` — TC_SCAN_MAX

- **Value:** 16384 bytes · **cap**
- **Bounds:** how much of a final answer is scanned for a tool call written as prose (the M147 nudge)
- **Change it with:** hard-coded
- **When it fires:** silent (a prose call after 16 KB is accepted as the final answer, no nudge)
- **Source:** reasoned: jc_tool.c:787 ("a narrated call sits early anyway")

### `src/tools/jc_tool.c:883` — TC_RECOVER_MAX

- **Value:** 16 calls · **cap**
- **Bounds:** tool calls recovered from one reply's markdown (M949)
- **Change it with:** hard-coded
- **When it fires:** silent (calls past 16 are dropped)
- **Source:** none: jc_tool.c:883 ("hard bound"); docs/ROADMAP.md:52019 says "sixteen per reply is the hard bound"
  without a reason

### `src/tools/jc_tool_ask.c:26` — ASK_MAX_OPTIONS

- **Value:** 16 options · **cap**
- **Bounds:** answer options shown for one ask_user question
- **Change it with:** hard-coded
- **When it fires:** silent (options past 16 are dropped)
- **Source:** none

### `src/tools/jc_tool_ask.c:29` — ASK_JOURNAL_QMAX

- **Value:** 240 bytes · **cap**
- **Bounds:** question text as written to the run journal
- **Change it with:** hard-coded
- **When it fires:** silent (UTF-8-safe cut in the record only)
- **Source:** none

### `src/tools/jc_tool_audiogen.c:24` — AUDIOGEN_MAX_BYTES

- **Value:** 33554432 bytes · **cap**
- **Bounds:** generated speech audio (also the transfer bound)
- **Change it with:** config audioGenMaxBytes (0 = this default)
- **When it fires:** error-value ("error: generated audio exceeded the N-byte cap (the transfer was cut at the cap,
  no file written)")
- **Source:** none

### `src/tools/jc_tool_check_model.c:166` — check_model probe req.timeout_secs

- **Value:** 60 s · **cap**
- **Bounds:** the check_model tool's live probe request
- **Change it with:** hard-coded
- **When it fires:** named, indirect ("fail: the probe request did not complete -- N ms"; N near 60000 is the only
  sign)
- **Source:** none: jc_tool_check_model.c:166 says only "the doctor's own probe timeout"

### `src/tools/jc_tool_docs.c:21` — DOCS_DEF_RESULTS

- **Value:** 5 results · **cap**
- **Bounds:** search_docs results when max_results is not given
- **Change it with:** tool argument max_results (no upper clamp)
- **When it fires:** silent
- **Source:** none

### `src/tools/jc_tool_fetch.c:28` — FETCH_TIMEOUT_SECS

- **Value:** 30 s · **cap**
- **Bounds:** total wall-clock of a fetch_url request
- **Change it with:** hard-coded (connect / deadStream fences from config)
- **When it fires:** error-value, misleading: "error: request failed (model stalled (timed out))" for a slow web
  page once connected, "(http error)" before
- **Source:** none: same M1093 "measured values" claim with no measurement

### `src/tools/jc_tool_fetch.c:89` — fetch_url req.max_bytes

- **Value:** 33554432 bytes · **cap**
- **Bounds:** download size before fetch_url aborts the transfer (the display cap is fetchMaxBytes, shard 2)
- **Change it with:** hard-coded
- **When it fires:** error-value, generic ("error: request failed (...)"; req.overflow is not read here, unlike
  web_search)
- **Source:** reasoned: jc_tool_fetch.c:87-88 ("OOM backstop well above the display cap")

### `src/tools/jc_tool_git.c:233` — jc_git_clamp_max

- **Value:** default 20, max 100 commits · **cap**
- **Bounds:** commits git_log returns
- **Change it with:** tool argument max (clamped 1..100)
- **When it fires:** silent (a larger max is clamped to 100)
- **Source:** none

### `src/tools/jc_tool_git.c:470` — git_add path cap (n < 23) and git_capture arg cap (k < 31)

- **Value:** 21 paths, then 20 args (= 18 paths) · **cap**
- **Bounds:** paths one git_add call stages
- **Change it with:** hard-coded
- **When it fires:** silent (extra paths dropped; the tool still answers "Staged changes.")
- **Source:** none

### `src/tools/jc_tool_imagegen.c:37` — IMAGEGEN_MAX_BYTES

- **Value:** 16777216 bytes · **cap**
- **Bounds:** a generated image (the POST leg's transfer bound is the base64 envelope of this)
- **Change it with:** config imageGenMaxBytes (0 = this default)
- **When it fires:** error-value ("error: image response was cut at the N-byte size limit" / "generated image is N
  bytes, over the N-byte cap")
- **Source:** none

### `src/tools/jc_tool_ls.c:29` — LS_MAX_RESULTS

- **Value:** 1000 matches · **cap**
- **Bounds:** paths list_files returns for a pattern
- **Change it with:** hard-coded
- **When it fires:** named ("[... truncated at 1000 matches; narrow the pattern ...]")
- **Source:** reasoned: jc_tool_ls.c:18-28 ("Past this the answer is noise anyway")

### `src/tools/jc_tool_ls.c:30` — LS_MAX_ENTRIES

- **Value:** 200000 entries · **cap**
- **Bounds:** entries list_files scans for a pattern
- **Change it with:** hard-coded
- **When it fires:** named ("[... stopped after scanning 200000 entries; the results above are incomplete ...]")
- **Source:** reasoned: jc_tool_ls.c:24-25 ("A pattern that matches nothing must still terminate on a huge tree")

### `src/tools/jc_tool_ls.c:31` — LS_MAX_DEPTH

- **Value:** 32 levels · **cap**
- **Bounds:** directory depth list_files descends for a pattern
- **Change it with:** hard-coded
- **When it fires:** silent (deeper directories are skipped with no note)
- **Source:** reasoned: jc_tool_ls.c:26-27 (symlink loops)

### `src/tools/jc_tool_parallel.c:276` — JC_PAR_TASK_TIMEOUT_DEFAULT

- **Value:** 300 s · **cap**
- **Bounds:** one spawn_parallel child's whole run (per-child watchdog)
- **Change it with:** config parallelTaskTimeout; 0 or absent = 300 s (cannot be switched off)
- **When it fires:** kill; named to the model ("sub-agent timed out after 300s (parallelTaskTimeout) -- raise it if
  the task legitimately takes longer")
- **Source:** none: the number is unexplained (jc_tool_parallel.c:276); a measurement CONTRADICTS it: M325 found
  successful children at 300-462 s and the default killing about half of them (docs/ROADMAP.md:17122-17140), yet the
  default stayed 300

### `src/tools/jc_tool_parallel.c:277` — JC_PAR_TERM_GRACE_MS

- **Value:** 300 ms · **cap**
- **Bounds:** SIGTERM -> SIGKILL grace for a stopped child
- **Change it with:** hard-coded
- **When it fires:** silent (escalates to SIGKILL)
- **Source:** reasoned: jc_tool_parallel.c:1732-1735 ("a short grace window" so a child that traps SIGTERM cannot
  wedge the parent)

### `src/tools/jc_tool_parallel.c:412` — JC_DUP_RING

- **Value:** 24 calls · **structural**
- **Bounds:** window of a child's recent calls used for the near-duplicate share (dup24) reported to the supervisor
- **Change it with:** hard-coded
- **When it fires:** silent (a signal, not a stop)
- **Source:** none

### `src/tools/jc_tool_parallel.c:413` — JC_DUP_TOOL_MAX / JC_DUP_KEY_MAX / JC_DUP_NORM_MAX

- **Value:** 48 / 160 / 256 bytes · **structural**
- **Bounds:** tool name, normalised args/result key, and work buffer of the near-duplicate ring
- **Change it with:** hard-coded
- **When it fires:** silent (keys truncated before comparison)
- **Source:** none

### `src/tools/jc_tool_parallel.c:63` — JC_PAR_MAX_TASKS

- **Value:** 32 tasks · **cap**
- **Bounds:** tasks one spawn_parallel call runs
- **Change it with:** hard-coded
- **When it fires:** named ("(note: only the first 32 tasks were run)")
- **Source:** none

### `src/tools/jc_tool_parallel.c:64` — JC_PAR_CEILING

- **Value:** 8 children · **cap**
- **Bounds:** automatic concurrency: min(CPU cores, 8) live children
- **Change it with:** config maxParallelAgents (a set value bypasses the ceiling)
- **When it fires:** silent (extra tasks queue)
- **Source:** none

### `src/tools/jc_tool_parallel.c:65` — JC_PAR_ANSWER_CAP

- **Value:** 16384 bytes · **cap**
- **Bounds:** one child's answer in the spawn_parallel result
- **Change it with:** hard-coded
- **When it fires:** named ("... [answer truncated]")
- **Source:** none

### `src/tools/jc_tool_parallel.c:73` — JC_PAR_NEWDIR_MAX

- **Value:** 16 dirs · **cap**
- **Bounds:** new directories of a write child named in the result
- **Change it with:** hard-coded
- **When it fires:** named ("; and more")
- **Source:** none

### `src/tools/jc_tool_parallel.c:74` — JC_PAR_NEWDIR_HOLD_FILES

- **Value:** 100 files · **fence**
- **Bounds:** a write child's new directory with more files than this is held out of the merge
- **Change it with:** hard-coded
- **When it fires:** named ("... HELD")
- **Source:** incident: jc_tool_parallel.c:66-72: run 0102 merged a 189-file 15 MB scratch dir; run 0085 a 963-file
  copy; operator decision 2026-10-03. The 100 itself is not derived

### `src/tools/jc_tool_parallel.c:75` — JC_PAR_NEWDIR_HOLD_BYTES

- **Value:** 1048576 bytes · **fence**
- **Bounds:** a write child's new directory larger than this is held out of the merge
- **Change it with:** hard-coded
- **When it fires:** named ("... HELD")
- **Source:** incident: as JC_PAR_NEWDIR_HOLD_FILES

### `src/tools/jc_tool_search.c:269` — search_code context clamp

- **Value:** 10 lines · **cap**
- **Bounds:** context lines around each search_code match
- **Change it with:** tool argument context (clamped to 10)
- **When it fires:** silent
- **Source:** reasoned: jc_tool_search.c:270 ("keep the output bounded")

### `src/tools/jc_tool_sound.c:166` — record_audio grace (timeout = secs + 5)

- **Value:** seconds + 5 s · **cap**
- **Bounds:** kill deadline of the recorder command
- **Change it with:** recording length via tool argument seconds (clamped to recordMaxSeconds); the 5 s is
  hard-coded
- **When it fires:** silent (a jichi-stopped recorder is expected; result OK if a file exists)
- **Source:** reasoned: jc_tool_sound.c:164-165 ("a small grace so a well-behaved -d N can exit on its own")

### `src/tools/jc_tool_sound.c:182` — transcribe cap literal in record_audio

- **Value:** 26214400 bytes · **cap**
- **Bounds:** size above which record_audio warns that transcription will refuse the file
- **Change it with:** config transcribeMaxBytes (0 = this literal)
- **When it fires:** named ("note: larger than the transcribe cap")
- **Source:** none

### `src/tools/jc_tool_subagent.c:100` — model list in the spawn_subagent schema

- **Value:** 12 models · **cap**
- **Bounds:** configured models named in the tool description the model reads
- **Change it with:** hard-coded
- **When it fires:** named (", ...")
- **Source:** none

### `src/tools/jc_tool_test.c:18` — TEST_MAX_OUTPUT

- **Value:** 65536 bytes · **cap**
- **Bounds:** output captured from run_tests' command
- **Change it with:** config runMaxBytes (0 = this default)
- **When it fires:** named ("... [output truncated]")
- **Source:** none

### `src/tools/jc_tool_test.c:19` — TEST_RAW_TAIL

- **Value:** 4096 bytes · **cap**
- **Bounds:** raw output shown to the model after the parsed test summary
- **Change it with:** hard-coded
- **When it fires:** named (header says "--- raw output (tail) ---")
- **Source:** reasoned: jc_tool_test.c:6-8 ("a bounded raw tail ... so the model gets the signal without re-reading
  build chatter")

### `src/tools/jc_tool_user.c:140` — user tool timeout (tools[].timeout -> jc_proc_capture)

- **Value:** 30 s when absent or 0 (JC_PROC_DEF_TIMEOUT) · **cap**
- **Bounds:** one run of a user-defined tool
- **Change it with:** config tools[].timeout (> 0); 0 or absent = 30 s, so it cannot be switched off
- **When it fires:** kill; named to the model ("[tool timed out and was killed]")
- **Source:** none: the 30 is JC_PROC_DEF_TIMEOUT (include/jc_proc.h:199, the trigger of this census)

### `src/tools/jc_tool_user.c:34` — USER_TOOL_MAX_OUTPUT

- **Value:** 32768 bytes · **cap**
- **Bounds:** output of a user-defined tool (config tools[])
- **Change it with:** hard-coded
- **When it fires:** named ("... [output truncated]", from jc_proc_capture)
- **Source:** none

### `src/tools/jc_tool_websearch.c:24` — WEB_SEARCH_MAX_BYTES

- **Value:** 16384 bytes · **cap**
- **Bounds:** formatted web_search result shown to the model
- **Change it with:** config searchMaxBytes (0 = this default) -- the SAME key that caps search_code (default 64 KB)
- **When it fires:** named ("... [truncated]")
- **Source:** none

### `src/tools/jc_tool_websearch.c:25` — WEB_SEARCH_DEF_RESULTS

- **Value:** 5 results · **cap**
- **Bounds:** web_search results when neither the tool argument nor config give one
- **Change it with:** tool argument max_results; config search.maxResults
- **When it fires:** silent
- **Source:** none

### `src/tools/jc_tool_websearch.c:26` — WEB_SNIPPET_MAX

- **Value:** 320 bytes · **cap**
- **Bounds:** one result's snippet
- **Change it with:** hard-coded
- **When it fires:** named ("...")
- **Source:** none

### `src/tools/jc_tool_websearch.c:29` — WEB_SEARCH_TIMEOUT_SECS

- **Value:** 30 s · **cap**
- **Bounds:** total wall-clock of a web_search request
- **Change it with:** hard-coded (connect / deadStream fences from config)
- **When it fires:** error-value, generic ("error: web search request failed")
- **Source:** none: same M1093 "measured values" claim with no measurement

## `src/tui`

### `src/tui/jc_tui.c:1365` — tool result preview

- **Value:** 360 bytes (240 head + 100 tail) · **cap**
- **Bounds:** a tool result as printed to the human in the TUI
- **Change it with:** hard-coded
- **When it fires:** named ("... (N bytes) ...")
- **Source:** reasoned: jc_tui.c:1361-1363 (head + tail so a trailing error is not hidden)

### `src/tui/jc_tui.c:1597` — UNRECOGNISED_MAX

- **Value:** 3 keypresses · **fence**
- **Bounds:** unrecognised keys an approval prompt absorbs before it denies
- **Change it with:** hard-coded
- **When it fires:** named (prints the denial; the model gets a denied tool call)
- **Source:** reasoned: jc_tui.c:1594-1596 ("Small on purpose: it exists to tolerate a fumble")

### `src/tui/jc_tui.c:1627` — VIEW_VALUE_MAX

- **Value:** 1200 bytes · **cap**
- **Bounds:** one argument value shown by the approval prompt's view key
- **Change it with:** hard-coded
- **When it fires:** named ("(N bytes in total)")
- **Source:** none: jc_tui.c:1619-1621 motivates a bound (a 200 KB write_file "would otherwise read for an hour"),
  not 1200

### `src/tui/jc_tui.c:257` — TUI_QUEUE_MAX

- **Value:** 4096 bytes (also keys per poll) · **cap**
- **Bounds:** type-ahead held while a turn runs: committed lines in total, the line being typed, and keys drained
  per poll
- **Change it with:** hard-coded
- **When it fires:** named for a committed line ("queue full, line dropped"); silent for keys past 4096 on the line
  being typed (jc_tui.c:483)
- **Source:** none: jc_tui.c:254-256 ("Bounds, not policy"; no magnitude given)

### `src/tui/jc_tui.c:258` — TUI_QUEUE_ECHO_MAX

- **Value:** 56 bytes · **structural**
- **Bounds:** tail of the typed line echoed beside the working indicator
- **Change it with:** hard-coded
- **When it fires:** silent (display tail only; the text is kept)
- **Source:** reasoned: jc_tui.c:254-256 ("the tail ... that fits beside the working indicator")

### `src/tui/jc_tui.c:267` — TUI_ESC_WAIT_MS

- **Value:** 100 ms · **cap**
- **Bounds:** wait after a bare ESC for the rest of an escape sequence before reading it as the Esc key (which stops
  the turn)
- **Change it with:** hard-coded
- **When it fires:** silent (by reading: a sequence whose later bytes arrive after 100 ms is read as Esc = stop)
- **Source:** reasoned: jc_tui.c:259-266 (vim's ttimeoutlen default is also 100 ms; a terminal sends a sequence as
  one burst). The 1.2 s figure there measured the EARLIER design, not this value

### `src/tui/jc_tui.c:3046` — tui_suggest req.timeout_secs

- **Value:** 12 s · **cap**
- **Bounds:** the inline ghost-text suggestion call (Ctrl-G)
- **Change it with:** hard-coded
- **When it fires:** silent (no suggestion appears)
- **Source:** reasoned: jc_tui.c:3042-3045 ("the short cap that keeps the editor responsive")

### `src/tui/jc_tui.c:3134` — tui_advise req.timeout_secs

- **Value:** 20 s · **cap**
- **Bounds:** the /advice prompt-clarity call (Ctrl-Q)
- **Change it with:** hard-coded
- **When it fires:** silent (no advice line appears)
- **Source:** reasoned: jc_tui.c:3134 ("a judgement call, not a completion: allow more")

### `src/tui/jc_tui.c:4378` — /listen seconds

- **Value:** 1-300 s, default 5 · **cap**
- **Bounds:** recording window of /listen
- **Change it with:** the /listen argument
- **When it fires:** named ("usage: /listen [seconds]  (1-300, default 5)")
- **Source:** none

### `src/tui/jc_tui.c:661` — ACC_FLUSH_CAP

- **Value:** 4096 bytes · **structural**
- **Bounds:** accessible mode's line buffer: text held until a newline before it is written for a screen reader
- **Change it with:** hard-coded
- **When it fires:** silent (flushes mid-line; nothing lost)
- **Source:** reasoned: jc_tui.c:657-660 ("deliberately large, because cutting mid-sentence is the thing being
  fixed")

## `src/util`

### `src/util/jc_audit.c:22` — JC_AUDIT_CMD_MAX

- **Value:** 65536 B · **cap**
- **Bounds:** command text written per audit-log line
- **Change it with:** hard-coded
- **When it fires:** named: suffix "...[+N B]" (jc_eventlog.c:296-298)
- **Source:** reasoned: jc_audit.c:20-21 "Generous cap: forensics want the whole command line, unlike the eventlog's
  16 KB content cap."

### `src/util/jc_cacheaudit.c:12` — JC_CACHEAUDIT_MIN_VOLUME

- **Value:** 2000 tokens · **structural**
- **Bounds:** threshold: input tokens before the cache audit gives a verdict
- **Change it with:** hard-coded
- **When it fires:** named: "insufficient data" (jc_cacheaudit.c:45)
- **Source:** reasoned: jc_cacheaudit.c:11 "Below this many input tokens there isn't enough signal to judge
  caching."

### `src/util/jc_diff.c:20` — DIFF_LCS_CELL_CAP

- **Value:** 4,000,000 cells · **cap**
- **Bounds:** LCS table for the changed middle of one diff (int per cell, ~16 MB)
- **Change it with:** hard-coded
- **When it fires:** silent: falls back to all deletions then all additions, a correct but unminimal diff
  (jc_diff.c:137-148)
- **Source:** none

### `src/util/jc_diff.c:21` — DIFF_DEFAULT_MAX_LINES

- **Value:** 400 lines · **cap**
- **Bounds:** lines of a rendered unified diff when the caller passes <= 0
- **Change it with:** caller argument (hard-coded at each call site)
- **When it fires:** named: "... (diff truncated)" (jc_diff.c:299)
- **Source:** none

### `src/util/jc_dispatch.c:127` — dirty-tree probe git deadline (literal 30, two calls)

- **Value:** 30 s · **cap**
- **Bounds:** `git status --porcelain` (jc_dispatch.c:127) and the fallback `git rev-parse --is-inside-work-tree`
  (jc_dispatch.c:156) in the dispatch probe
- **Change it with:** hard-coded
- **When it fires:** kill, then a verdict: a timed-out status (-2) falls to rev-parse and reads UNKNOWN; but a
  rev-parse that itself times out or fails to spawn reads CLEAN (jc_dispatch.c:156-158), by reading
- **Source:** reasoned: jc_dispatch.c:125-126 "30 s caps a stuck git (the driver's own deadline is looser)"

### `src/util/jc_dispatch.c:39` — JD_PATH_BUF

- **Value:** 512 B · **structural**
- **Bounds:** one dirty path copied into the named-paths display
- **Change it with:** hard-coded
- **When it fires:** silent: path cut, count stays exact
- **Source:** reasoned: jc_dispatch.c:37-38

### `src/util/jc_dispatch.c:42` — JD_OUT_CAP

- **Value:** 65536 B · **cap**
- **Bounds:** captured `git status --porcelain` output in the dirty-tree probe
- **Change it with:** hard-coded
- **When it fires:** silent: output cut and "... [output truncated]" appended (jc_proc.c:870); parse_porcelain
  (jc_dispatch.c:69-78) then counts that marker line as one more path, by reading
- **Source:** reasoned: jc_dispatch.c:40-41 "A dirty workspace's porcelain is at most a handful of lines per file;
  64 KiB covers any realistic dispatch base"

### `src/util/jc_gradecore.c:111` — assignment verify deadline and output cap (literals)

- **Value:** 600 s; 262144 B · **cap**
- **Bounds:** the assignment's `verify` shell command
- **Change it with:** hard-coded
- **When it fires:** error-value: verify_exit -2, scored as not passed; shown as "verify: <cmd> (exit -2)"
  (jc_tui.c:2698) and verify_exit -2 in JSON (main.c:12426); never says "timed out". Output past 256 KiB gets "...
  [output truncated]" (jc_proc.c:870) before jc_testparse reads it
- **Source:** none

### `src/util/jc_gradecore.c:96` — assignment setup deadline and output cap (literals)

- **Value:** 120 s; 65536 B · **cap**
- **Bounds:** the assignment's `setup` shell command, run before grading
- **Change it with:** hard-coded
- **When it fires:** silent: exit code and output are discarded, grading goes on after a killed setup
  (jc_gradecore.c:89-97)
- **Source:** none

### `src/util/jc_inhist.c:89` — secret-token minimum length (literal 16, two sites)

- **Value:** 16 chars · **fence**
- **Bounds:** shortest key-prefixed or Bearer token the input-history filter treats as a secret (:89, :100)
- **Change it with:** hard-coded
- **When it fires:** silent: a shorter token is not recognised and the line is saved to ~/.jichi.d/input_history
- **Source:** none

### `src/util/jc_insights.c:15` — TOOL_MIN_CALLS

- **Value:** 3 calls · **structural**
- **Bounds:** threshold: calls a tool needs before `learn analyze` may flag it as failing often
- **Change it with:** hard-coded
- **When it fires:** silent: no finding below it
- **Source:** none: jc_insights.c:13-14 states the rule, not why 3

### `src/util/jc_insights.c:16` — TOOL_OK_FLOOR_PCT

- **Value:** 60 % · **structural**
- **Bounds:** threshold: ok-rate below which a tool counts as failing often
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:17` — MODEL_TIMEOUT_MIN

- **Value:** 2 timeouts · **structural**
- **Bounds:** threshold: timeouts before a model is flagged as stalling
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:18` — RETRY_MIN

- **Value:** 3 retries · **structural**
- **Bounds:** threshold: model retries before a retry-storm finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:19` — ROUTE_MIN

- **Value:** 3 routes · **structural**
- **Bounds:** threshold: routing escalations before a finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:20` — COMPACT_MIN

- **Value:** 3 compactions · **structural**
- **Bounds:** threshold: compactions before a heavy-compaction finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:21` — JC_INSIGHT_REDO_MIN

- **Value:** 3 edits · **structural**
- **Bounds:** threshold: edits of one file in one session before a redo-loop finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:22` — ERROR_MIN

- **Value:** 3 errors · **structural**
- **Bounds:** threshold: failed model calls (not timeout/retry) before a finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:23` — VERIFY_FAIL_MIN

- **Value:** 2 runs · **structural**
- **Bounds:** threshold: autonomous runs failing the verify gate before a finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_insights.c:24` — BUDGET_REVERT_MIN

- **Value:** 2 runs · **structural**
- **Bounds:** threshold: runs rolled back for hitting the budget before a finding
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_jsonrepair.c:14` — JR_MAX_DEPTH

- **Value:** 64 open containers · **structural**
- **Bounds:** nesting the JSON-repair pass will track
- **Change it with:** hard-coded
- **When it fires:** silent: the repair gives up (returns NULL), the broken JSON stays broken
  (jc_jsonrepair.c:111-114)
- **Source:** reasoned: docs/reading/ireko-07-when-the-json-is-broken.md:316-322 (protects the stack array; the
  parser's bound is 256; real payloads nest under ~12)

### `src/util/jc_kinetic.c:108` — kinetic-command matcher guard (literal 64)

- **Value:** 64 tokens · **fence**
- **Bounds:** words skipped while resolving a segment's leading command
- **Change it with:** hard-coded
- **When it fires:** silent and fails OPEN: returns NULL, read as not kinetic
- **Source:** none: jc_kinetic.c:108 says only "pathological input"

### `src/util/jc_learn.c:518` — redo-scan session limit (literal 10)

- **Value:** 10 sessions · **cap**
- **Bounds:** recent sessions scanned for redo loops by `learn analyze`
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_lease.c:198` — JC_LEASE_LOCK_WAIT_MS

- **Value:** 5000 ms · **cap**
- **Bounds:** wait for a workspace lease's .lock before going on without it
- **Change it with:** hard-coded
- **When it fires:** named (log WARN): "lease: another process has held <lock> for more than 5 s -- <action> without
  it, so a run starting at this moment may not be seen" (jc_lease.c:208-211)
- **Source:** reasoned: jc_lease.c:196-197 "Long enough for any take (microseconds, or a FAULT pause); short enough
  that a holder stopped inside one costs the next run seconds, not the run."

### `src/util/jc_log.c:20` — JC_REDACT_MAX

- **Value:** 32 secrets · **fence**
- **Bounds:** distinct secrets the log-redaction registry can hold
- **Change it with:** hard-coded
- **When it fires:** silent and fails OPEN: a 33rd secret is not registered, so it is NOT redacted from logs
  (jc_log.c:194-196)
- **Source:** none: raised 8 -> 32 in 4fe616b5 2026-07-14 "feat(security): hardening + autonomy/learning extensions
  (M130-M134)" with no stated reason; first defined in c5d489e4 2026-06-24 M24-M27

### `src/util/jc_mem.c:11` — JC_ARENA_DEFAULT_BLOCK

- **Value:** 8192 B · **structural**
- **Bounds:** default arena block size
- **Change it with:** hard-coded
- **When it fires:** silent (internal)
- **Source:** none

### `src/util/jc_memtrim.c:18` — M_MMAP_THRESHOLD

- **Value:** 128 KiB · **structural**
- **Bounds:** allocations served by mmap instead of brk
- **Change it with:** hard-coded
- **When it fires:** silent (memory only)
- **Source:** reasoned: jc_memtrim.c:13-17 "128 KB is glibc's own default STARTING threshold -- we only disable the
  ratchet"

### `src/util/jc_memtrim.c:20` — M_TRIM_THRESHOLD

- **Value:** 256 KiB · **structural**
- **Bounds:** free top-of-heap before brk is trimmed
- **Change it with:** hard-coded
- **When it fires:** silent (memory only)
- **Source:** none: jc_memtrim.c:19 states, does not explain

### `src/util/jc_notify.c:17` — NOTIFY_OUT_CAP

- **Value:** 4096 B · **structural**
- **Bounds:** captured output of the notify command, which is discarded
- **Change it with:** hard-coded
- **When it fires:** silent (no effect: output discarded)
- **Source:** reasoned: jc_notify.c:15-16

### `src/util/jc_notify.c:18` — NOTIFY_TIMEOUT

- **Value:** 10 s · **cap**
- **Bounds:** kill deadline of the notify / --notify command
- **Change it with:** hard-coded
- **When it fires:** kill, silent: return value ignored (jc_notify.c:62-63)
- **Source:** reasoned: jc_notify.c:15-16 "a notifier should be quick and must never wedge the front-end"

### `src/util/jc_patch.c:34` — PATCH_MATCH_LINES_MAX

- **Value:** 8 line numbers · **cap**
- **Bounds:** line numbers listed in the "old_string matches more than once" hint
- **Change it with:** hard-coded
- **When it fires:** named: ", ..." after the eighth (jc_patch.c:76-78)
- **Source:** none: include/jc_patch.h:83-89 grounds the HINT (11 of 14 failed edits over four drives were
  non-unique matches), not the 8

### `src/util/jc_patch.c:379` — PATCH_HINT_MAX_TOKENS

- **Value:** 24 tokens · **structural**
- **Bounds:** identifier tokens of old_string used to score the near-match hint
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_patch.c:380` — PATCH_HINT_CTX

- **Value:** 2 lines · **structural**
- **Bounds:** context lines each side of the best near-match line
- **Change it with:** hard-coded
- **When it fires:** silent
- **Source:** none

### `src/util/jc_patch.c:381` — PATCH_HINT_MAX_COL

- **Value:** 160 B · **cap**
- **Bounds:** bytes of each excerpt line shown in the near-match hint
- **Change it with:** hard-coded
- **When it fires:** named: " ..." (jc_patch.c:497-499)
- **Source:** none

### `src/util/jc_path.c:152` — JC_RESOLVE_MAX_HOPS

- **Value:** 40 hops · **fence**
- **Bounds:** symlink hops when resolving a write target that does not exist yet
- **Change it with:** hard-coded
- **When it fires:** error-value: JC_ERR_NOTFOUND, fail closed, "a symlink cycle, or a hostile chain"
  (jc_path.c:338)
- **Source:** reasoned: jc_path.c:147-150 "Linux's own MAXSYMLINKS is 40; a legitimate write target is one or two
  hops away"

### `src/util/jc_path.c:26` — PATH_MAX fallback

- **Value:** 4096 B · **structural**
- **Bounds:** path buffer size where <limits.h> defines no PATH_MAX
- **Change it with:** compile-time only
- **When it fires:** unknown
- **Source:** none

### `src/util/jc_pdf.c:12` — JC_PDF_TIMEOUT

- **Value:** 30 s · **cap**
- **Bounds:** one pdftotext (or pdfCommand) run per PDF read, @-reference or index pass
- **Change it with:** hard-coded (pdfCommand changes the program, not the time)
- **When it fires:** kill, then error-value: read_file answers "error: could not extract text from '<path>'"
  (jc_tool_read.c:89-90) and never says it timed out; an @file.pdf reference is skipped silently
  (jc_refs.c:496-502), by reading
- **Source:** none

### `src/util/jc_priv.c:241` — privileged-launcher detector token guard (literal 64, three sites)

- **Value:** 64 tokens · **fence**
- **Bounds:** assignments, wrapper words and options scanned per command segment before giving up
- **Change it with:** hard-coded
- **When it fires:** silent and fails OPEN: returns JC_PRIV_NONE (jc_priv.c:241; :159 and :182 in skip_wrapper_args)
- **Source:** none: jc_priv.c:241 says only "pathological input"

### `src/util/jc_proc.c:431` — JC_POPEN_MAX

- **Value:** 8 streams · **fence**
- **Bounds:** jc_proc_popen streams alive at once
- **Change it with:** hard-coded
- **When it fires:** error-value: jc_proc_popen_pid returns NULL (jc_proc.c:450-452); jc_app.c:1345-1346 turns that
  into JC_ERR_IO, with nothing naming the table
- **Source:** reasoned: jc_proc.c:427-430 "a small fixed table with linear scan ... a caller somehow holding eight
  live streams gets a refusal, never an untracked child"

### `src/util/jc_proc.c:557` — jc_proc_reap_bounded "no deadline" stand-in

- **Value:** 1e9 ms (~11.6 days) · **cap**
- **Bounds:** reap wait when the caller passes deadline_ms <= 0
- **Change it with:** hard-coded
- **When it fires:** kill: SIGTERM, ~1 s grace, SIGKILL; returns -2
- **Source:** none: jc_proc.c:555-557 "a far-future bound" states, does not justify

### `src/util/jc_proc.c:583` — SIGTERM grace in jc_proc_reap_bounded

- **Value:** ~1 s (50 x 20 ms) · **cap**
- **Bounds:** time a deadlined or aborted child gets to exit after SIGTERM
- **Change it with:** hard-coded
- **When it fires:** kill: SIGKILL to the group (jc_proc.c:597-600, 628-630)
- **Source:** none: jc_proc.c:583 "Grace period ~1s." states, does not explain

### `src/util/jc_proc.c:683` — close-fds / cloexec sweep ceiling (literal 4096L, two sites)

- **Value:** 4096 descriptors · **structural**
- **Bounds:** highest descriptor swept by jc_proc_parent_cloexec_all (:683) and jc_proc_child_close_fds (:707)
- **Change it with:** hard-coded
- **When it fires:** silent: a descriptor above 4096 is not swept (CLOEXEC at creation is the real fix)
- **Source:** reasoned: jc_proc.c:699-705 "a million close() calls on every spawn is a measurable cost for no
  benefit. 4096 covers any descriptor table jichi can actually produce" (no figure given)

### `src/util/jc_proc.c:780` — JC_PROC_DEF_TIMEOUT (applied inside jc_proc_capture; defined include/jc_proc.h:199)

- **Value:** 30 s · **cap**
- **Bounds:** kill deadline of every jc_proc_capture child whose caller passes timeout <= 0
- **Change it with:** hard-coded (no caller can ask jc_proc_capture for no deadline)
- **When it fires:** kill: SIGKILL to the child's process group (jc_proc.c:787-800), return -2 (jc_proc.c:880-885);
  jc_proc_capture itself prints nothing, each caller decides what to say
- **Source:** none: include/jc_proc.h:198 states the number, gives no reason; it descends from USER_TOOL_DEF_TIMEOUT
  30L (1fc89547 2026-06-22 "Add user-defined tools") and HOOK_DEF_TIMEOUT 30L (c5d489e4 2026-06-24 M24-M27), neither
  commented; merged into the shared default by 1370dd9b 2026-06-25 "refactor: shared jc_proc_capture (fixes hook
  stdin deadlock), wire log redaction"

### `src/util/jc_progress.c:430` — prediction line cap (literal 8192)

- **Value:** 8192 B · **cap**
- **Bounds:** longest line read by jc_progress_predict_scan
- **Change it with:** hard-coded
- **When it fires:** silent: a longer line is skipped
- **Source:** none

### `src/util/jc_progress.c:79` — learner progress line cap (literal 4096, two sites)

- **Value:** 4096 B · **cap**
- **Bounds:** longest progress-record line read by jc_progress_scan (:79) and jc_progress_hints_scan (:325)
- **Change it with:** hard-coded
- **When it fires:** silent: a longer line is skipped
- **Source:** none

### `src/util/jc_promptcache.c:52` — minimum cacheable prompt (literals 1024, 4096)

- **Value:** 1024 tokens (sonnet); 4096 (haiku, opus) · **structural**
- **Bounds:** threshold: prompt size below which the cache warning says caching cannot work
- **Change it with:** hard-coded
- **When it fires:** named: "the cacheable prefix (tools + system) is ~N tokens and this model will not cache a
  block under M..." (main.c:16964-16972)
- **Source:** reasoned: jc_promptcache.c:40-49 (vendor minimums, deliberately conservative after M340)

### `src/util/jc_queryrewrite.c:112` — query-rewrite one-shot deadline (literal 30)

- **Value:** 30 s · **cap**
- **Bounds:** the model call that expands a retrieval query
- **Change it with:** hard-coded
- **When it fires:** silent: rewrite_call returns NULL and jc_queryrewrite returns NULL (jc_queryrewrite.c:147-152),
  so retrieval runs on the raw query
- **Source:** none

### `src/util/jc_relay.c:71` — unrelieved-streak hand-over threshold (literal 2)

- **Value:** 2 rounds · **cap**
- **Bounds:** pressured compaction rounds that relieved nothing before the relay hands the session over
- **Change it with:** hard-coded
- **When it fires:** named: hand-over reason "unrelieved" (jc_relay.c:72, :192)
- **Source:** reasoned: jc_relay.c:61-65 "two in a row means elision is DONE relieving this history"

### `src/util/jc_rewind.c:10` — JC_REWIND_CLEAN_CAP

- **Value:** 256 B · **structural**
- **Bounds:** cleaned user message compared with a checkpoint label
- **Change it with:** hard-coded
- **When it fires:** silent: comparison is over the shorter length, a prefix match (jc_rewind.c:60-63)
- **Source:** none

### `src/util/jc_rss.c:13` — RSS_TAG_BUF

- **Value:** 24 B · **structural**
- **Bounds:** a tag name plus "</" and NUL in the feed parser
- **Change it with:** hard-coded
- **When it fires:** error-value: field() returns 0 for a longer tag (jc_rss.c:88)
- **Source:** reasoned: jc_rss.c:79-87 (no tag jichi passes exceeds 11 characters)

### `src/util/jc_rss.c:14` — RSS_MAX_ITEMS

- **Value:** 40 items · **cap**
- **Bounds:** items rendered from one RSS or Atom feed
- **Change it with:** hard-coded
- **When it fires:** silent: the loop stops, no "more items" note (jc_rss.c:231)
- **Source:** none

### `src/util/jc_rss.c:15` — RSS_DESC_MAX

- **Value:** 1200 B · **cap**
- **Bounds:** description snippet per feed item
- **Change it with:** hard-coded
- **When it fires:** named: bare " ..." marker, no byte count (jc_rss.c:168-170)
- **Source:** none

### `src/util/jc_rss.c:186` — feed-detection scan window (literal 4096)

- **Value:** 4096 B · **cap**
- **Bounds:** bytes searched for an <rss, <feed, <rdf, <item or <entry tag
- **Change it with:** hard-coded
- **When it fires:** silent: a feed whose first such tag comes after 4 KiB is not recognised as a feed
- **Source:** reasoned: jc_rss.c:186 "only look near the top"

### `src/util/jc_sound.c:62` — SOUND_OUT_CAP

- **Value:** 4096 B · **cap**
- **Bounds:** captured output of a play or record command
- **Change it with:** hard-coded
- **When it fires:** named: "... [output truncated]" (jc_proc.c:870)
- **Source:** none

### `src/util/jc_sound.c:63` — SOUND_ARGV_CAP

- **Value:** 64 slots · **cap**
- **Bounds:** argv of an argv-form play or record command
- **Change it with:** hard-coded
- **When it fires:** silent: configured args past 60 are dropped (jc_sound.c:108-110)
- **Source:** none

### `src/util/jc_telemetry.c:182` — build versions tracked in a telemetry summary (literal 32)

- **Value:** 32 versions · **cap**
- **Bounds:** distinct jichi builds summarised from one telemetry log
- **Change it with:** hard-coded
- **When it fires:** silent: builds past the 32nd are not listed
- **Source:** reasoned: jc_telemetry.c:182 "a log spanning 32 builds is enough"

### `src/util/jc_toolout.c:226` — tool-output store cleanup rm -rf deadline (literal 30L, two calls)

- **Value:** 30 s · **cap**
- **Bounds:** `rm -rf` of this session's tool-output directory (:226) and of each directory past
  JC_TOOLOUT_MAX_AGE_DAYS (:257)
- **Change it with:** hard-coded
- **When it fires:** kill, silent: result cast to (void); a half-deleted directory stays until the next sweep
- **Source:** none

### `src/util/jc_voice.c:255` — voice playback default deadline (literal 120)

- **Value:** 120 s · **cap**
- **Bounds:** the voice-mode playback command
- **Change it with:** sound.playTimeoutSeconds (config)
- **When it fires:** kill, then a log line "voice: playback command exited -2" (jc_voice.c:265)
- **Source:** none

## Read further

- [`LIMITS.md`](LIMITS.md): what a default limit is, five questions to ask of one, the rule, and what this census
  found.
- [`DEFERRED.md`](DEFERRED.md#def-421), def-421 to def-423: the measurements still owed for the time limits.
