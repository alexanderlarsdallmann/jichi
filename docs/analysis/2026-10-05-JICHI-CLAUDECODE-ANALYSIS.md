# JICHI CLAUDE-CODE STUDY — what the sibling agent's own telemetry says

*Standalone analysis (not one of the doc-surfaces): read-only profiling of the
Claude Code session logs from which jichi was developed, 2026-10-05. All numbers
[measured] from ~/.claude/projects/-home-alex-development-journey/*.jsonl (24
files, ~364 MB) + ~/.claude/history.jsonl, referenced by 8-char session id only.
No log content is reproduced beyond 100-140-char steer snippets and no
credential material is printed (the token-in-URL push pattern is COUNTED, not
shown). Proprietary tool: mechanism names are inferred from entry shapes.*

## 1. Inventory

| session | span | cost | API time | compaction | human steers |
|---|---|---|---|---|---|
| 992eb478 (160 MB) | 2026-09-24 → 2026-10-04 (10 days) | $2,563.35 | 52.3 h | 18 summaries | 122 (median 90 chars) |
| 8e2418bf (96 MB)  | 2026-09-15 → 2026-09-23 (8 days)  | $2,537.30 | 30.9 h | 11 summaries | — |

Both sessions developed jichi (top touched paths in 992eb478: tests/smoke ×2,503,
docs ×1,642, include ×792, src/chat ×631, ROADMAP.md ×572). ~$5.1k total over
~18 days, 4-5 assistant turns per minute of wall-time averaged over the spans.
Model mix 992eb478 [measured]: opus-5.5 ×15,585, opus-4.8 ×8,016, fable-5
×322 — a mid-session tier switch (the /model command appears in the log),
jichi's routing concern from the operator's own seat.
history.jsonl: 1,056 prompts, 2026-08-14 → 2026-10-04. Top prefixes:
"Thank you for" ×122, "/status" ×52, "Thank you! Please," ×49, "/resume" ×38.

## 2. Context behavior (the part jichi can act on)

- **Token reminders**: `<total_tokens>N tokens left` injected ~9,900 times in
  992eb478 (per-turn, 9,499 distinct values), pool 15,000,000, lowest observed
  13,100,888 — i.e. the biggest session never dropped below ~87% of its pool.
  [inference] 15M is a usage pool, not a per-request window (a single request
  could not hold 15M tokens); the reminder is the *budget* analogue of jichi's
  [context] note, and it fired far more often than jichi's 80% high-water —
  every turn, not on pressure.
- **Compaction exists but is light**: 18 `isCompactSummary` user entries in
  10 days (one every ~14 h of session time). The system did NOT rely on
  compaction to stay under water — it relied on two other behaviors below.
- **External scratchpad as working memory** [measured]: the dominant Bash
  patterns after the cd-prefix are `SP=/tmp/claude-1000/.../scratchpad` ×948,
  `S=/tmp/.../scratchpad` ×631, `cat > /tmp/...` ×51. Large artifacts (plans,
  diffs, analysis outputs) are written to a per-session temp dir and read back
  in pieces instead of living in context. This is *exactly* the discipline this
  audit's handoff forced on itself ("write durable results now, small chunks"),
  adopted organically by the sibling tool.
- **/clear cadence**: 1 explicit /clear in 992eb478; 0 in 8e2418bf — but
  38 "/resume" prompts in history: the operator's reset discipline is
  *session-level* (end + resume on a new day), not mid-session clearing.
- **Bash-heavy, Read-light**: 8,005 Bash vs 597 Read vs 457 Write in 992eb478.
  Aggregation was done with `grep -c`, `python3 - <<PYEOF` heredocs (75+73+42)
  and `wc -l` rather than re-reading files — jichi's AGENTS.md "batch shell
  work / search before reading" advice was practiced, not just written.

## 3. Multi-agent and multi-machine behavior

- **Inter-session messaging**: 17 user turns are "Another Claude session sent
  a message" — subagent hand-backs, min 3 KB, **median 19 KB, max 31 KB,
  total 301 KB** (14 over 10 KB) injected into the main context. [inference]
  This is the sibling tool's version of jichi's spawn_subagent return path —
  and it shows the same hazard jichi's own M218 analysis documented: a nested
  agent's final answer, uncapped, is a large single tool-result in the parent.
  Claude Code's mitigation appears to be the scratchpad habit (big outputs
  written to /tmp first, referenced, not inlined) — but 14/17 hand-backs
  contradicted that.
- **Two-machine development**: steers reference "the other machine
  (threadwork)" pushing findings to a branch; the handoff-file convention
  (this audit's own HANDOFF-*.md) is the same pattern: a durable markdown file
  is the inter-agent/inter-machine message bus.
- **Steering style** [measured, 3 longest real steers shown]: the operator
  gives short corrections with pasted context (median 90 chars; longest real
  steer 1,006 chars containing a 3315-id paste), defers to the agent's
  honesty ("Thank you for your honesty..."), and scopes explicitly ("You may
  have to run make ci, and commit, and push"). 54 AskUserQuestion, 34 Monitor,
  4 TaskStop in 992eb478 — the agent was allowed to ask, watch, and be stopped.

## 4. Commit/hygiene patterns

- 81 `git add -A` invocations in 992eb478 (bulk staging, then commit -F a
  message file — the same discipline this audit uses; inline heredoc commit
  messages fail under the context budget, verified in-session by jichi).
- Token-in-URL push: 62 Bash invocations beginning
  `T=$(cat .../keys/...)` — the credential is read into a shell var,
  interpolated, and unset. jichi's own push hygiene (this audit's commits) is
  the *identical* pattern, i.e. the operator ported it by habit. [inference]
  The pattern's residue: 62 command lines each containing the token
  substitution exist in this log on disk — the reason jichi's SECURITY-AUDIT
  (analysis 1) flagged args_full telemetry and why redaction-before-disk
  (M802) matters for any tool that journals commands.

## 5. What jichi can take from this (ranked)

**C1 — A per-turn budget reminder is cheaper than an on-pressure note.**
Claude Code injected a `<total_tokens>N left` line ~9,900 times (every turn),
and its biggest session never dropped below ~87% of pool. jichi's [context]
note fires only past the 80% high-water and, as this very audit hit, can arrive
*after* an argument is already elided. A low-cost standing line (pool +
percent) in the system prefix — the M300-style "say it once, in the cached
prefix" pattern — would give the model continuous water-level awareness instead
of a red alert at the edge. [inference, from the contrast]

**C2 — Cap nested-agent hand-backs.** 14 of 17 subagent returns were >10 KB
(median 19 KB) and landed verbatim in the parent's context. jichi's
spawn_subagent has no documented cap on the returned final answer; the M218
elision (this audit's #43) is the *consequence* handler. A size cap (or
scratchpad-to-disk-and-reference) at the spawn boundary is the *cause*
handler, and it mirrors the sibling tool's own dominant habit (301 KB of
hand-backs vs 4,665 scratchpad touches — the scratchpad won). [measured sizes;
inference that jichi lacks the cap — verified against the spawn docs in #43]

**C3 — The scratchpad is a feature jichi should name.** The sibling's most
repeated behavior (per-session /tmp dir for large artifacts, read back in
pieces) is jichi's unadvertised `run_in_background` + temp-file idiom. jichi's
AGENTS.md describes it ("write durable results now") but no tool or command
*provides* a per-run scratchpad path. A `/scratch` (or the repo-map emitting
the run's temp dir) would turn an operator-taught habit into a first-class
surface. [inference]

**C4 — Two-machine handoff files are the inter-agent message bus.** Both the
sibling tool (threadwork pushing findings to a branch) and this audit
(HANDOFF-*.md) use a durable markdown file as the cross-agent/cross-session
state. jichi has `memory.md` (agent-written facts) but no *operator-curated
handoff* convention; the DEFERRED/register pattern is the natural home for a
"working state you resume later" file type. [inference]

**C5 — Token residue is a jichi-relevant security fact, not just a hygiene
note.** 254 push lines in one session, 318 lines carrying the key-file
reference, all on disk in a plain JSONL. jichi's M802 (redact secrets before
the session store) and the SECURITY-AUDIT's args_full finding exist for
exactly this; the sibling tool has no equivalent redaction in its JSONL. If
jichi ever journals raw commands, the redact-before-write ordering is the
load-bearing step. [measured counts; the redaction-absence is [inference] from
the entry shapes]

## 6. Limits

- Proprietary tool: "compaction", "token pool", "subagent hand-back" are
  inferred from JSONL entry *shapes* (isCompactSummary, total_tokens_reminder,
  agent-message), not documented semantics; the 15M figure is read as a usage
  pool, not asserted as a context window.
- Two sessions profiled deeply (992eb478, 8e2418bf); the 22 small files (<300
  KB, mostly /resume stubs) are counted, not read.
- No credential or code content is reproduced; the token-in-URL pattern is
  count-only. Findings reference session ids, never full transcripts.
