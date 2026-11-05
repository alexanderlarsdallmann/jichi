# jichi gap analysis: compaction (data flow)

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey), HEAD `1b71247d`,
C89, one dependency, Apache-2.0. Method: find the one thing the area is
definitely lacking; verify every load-bearing claim against source; flag
inferences; make the appendix reproducible.*

## TL;DR

The one thing lacking: **a durable, raw record of the conversation that
auto-compaction throws away.** Mid-turn tool-output elision is lossy-but-
recoverable (M339/M348: the FULL original is written to a 0600 preservation
store and the elision marker is a claim ticket naming the path). Auto-
compaction of the older prefix is lossy-but-**not** recoverable: the raw
messages are dropped, only the LLM summary remains (persisted into the saved
session, `COMPACTION.md:76`), no archive of the dropped bytes exists, and no
test verifies the summary's fidelity. The two lossy operations sit in the same
file with opposite recovery semantics, and the asymmetry is nowhere recorded.

## 1. What the area claims

`COMPACTION.md` (948 lines) documents a mature, self-correcting mechanism:

- **Byte-estimate trigger** (`bytes/4` + per-message overhead,
  `jc_compact.c:26,42,60`), firing at `0.8 × contextLimit`.
- **Per-model calibration (M77)**: the *system already knows the heuristic is
  wrong*. `jc_calib_observe` compares each API call's real `prompt_tokens` to
  the byte estimate and blends `real/estimate` into a running window-capped
  ratio, clamped `[0.5, 8.0]` (`jc_calib.c`, `jc_calib.h:36-40`), persisted to
  `~/.jichi.d/calibration.json`. Documented ratios range 0.69× to 2.53× — "any
  fixed correction would be wrong for most of this list, and wrong in both
  directions."
- **Mid-turn elision (M348)**: for a marathon turn's oversized *tool results*,
  the middle is dropped — but "The marker is a claim ticket: each lossy
  elision writes the FULL original content to the M339 preservation store
  (`~/.jichi.d/tool-output/`, 0600, cleaned at teardown — tickets are
  **turn-lived**)".
- **Auto-compaction**: the older prefix is *summarized by a model* and the raw
  messages are dropped; "The summary persists with the session, so a
  `--resume`d session stays compact."

## 2. The crux: two lossy operations, one file, opposite recovery

Both live in `src/chat/jc_compact.c`:

- **Tool-result elision** (`:773-811`): lossy, **recoverable** — the full bytes
  are spooled to `~/.jichi.d/tool-output/` and the marker names the path. The
  rationale ("Before M348 the marker was a receipt with no ticket, and the
  model that still needed the bytes re-ran the original call at full price")
  shows the recovery gap was found, measured, and fixed.
- **Prefix summarization** (`jc_compact_run`, `:420` onward): lossy,
  **not recoverable**. The prompt to the summarizer (`:420`) is an *instruction
  on what to keep* ("preserves: the user's goals and explicit requests, key
  decisions, …") — a lossy abstraction by design. The original messages are
  gone from jichi's only durable record (the session file), which now holds
  the summary instead of the history.

`grep` confirms it: no `preserve`/`store`/`archive`/`M339`/`claim`/ticket path
exists anywhere in the summarization branch of `jc_compact.c`. The M348
machinery is invoked by the elision helper, not by compaction. The
asymmetry — one lossy write is ticketed, the other is not — is not named in
`COMPACTION.md`, `DEFERRED.md`, or any test.

## 3. Why this is "definitely lacking"

1. **The summary is the only surviving record, and nothing verifies it.**
   `SUMMARIZE_SYS` (`jc_compact.c:416-422`) is a lossy abstraction — "Write a
   concise but complete summary" — and the dropped messages are not archived
   anywhere (no `preserve`/spool/ticket call in the compaction branch; the
   M348 machinery is invoked only from the elision helper, `:773-815`). No
   smoke driver plants facts in history, compacts, and asserts the summary
   recalls them (the fidelity grep over `compaction.sh`/`compact_*.sh` is
   empty). Calibration measures *how much* the estimate is off; it cannot
   measure *what the summary dropped*.
2. **The recovery gap was already found and fixed — for the sibling
   operation.** M348 exists because the ticketless marker made "the model
   re-ran the original call at full price". The same economic logic applies,
   scaled, to compaction: after a compact, a detail the summary lost can only
   be recovered by re-doing the work (re-reading files, re-running commands) —
   and the raw bytes of the conversation itself are unrecoverable in principle,
   not just in practice.
3. **The turn-lived store makes the asymmetry worse, not better.** M339
   tickets are "cleaned at teardown". Even if compaction used them, the
   summary would outlive its own raw record: the session (weeks-lived) holds
   the summary; the bytes it replaced die at process exit. So the durable
   record gets *more* lossy over time, while the session claims to "stay
   compact" forever.

## 4. Alternatives considered (why the lacking thing is this one)

- *The tokenizer* — lacking in principle (byte heuristic), but the M77
  calibration is exactly the honest fix: self-learned per model, persisted,
  bounded, documented with measured ratios. Not the gap.
- *Mid-turn target reach (M323)* — "when compaction cannot reach its target"
  is documented, including a recorded decision on what it does instead. Not
  the gap.
- *Summary fidelity* — real (item 1), but unverifiable *because* of the raw-
  record gap: a fidelity check needs the dropped original to diff against.
  The archive is the root; the fidelity test is a consequence.
- *Calibration cold start* — `ratio 1.0 when uncalibrated` is documented as
  intentional ("pre-M77 behaviour is exactly preserved until data arrives").
  A stated, bounded limit, not a hole.

## 5. The register's honesty about the rest

- `COMPACTION.md:460` — "The estimate is now scaled by a measured per-model
  … calibration" (M77/M286, with the corrected-basis warning and the
  "two samples told you the wrong sign" caution).
- `COMPACTION.md:520-528` — the M348 claim ticket, including the failure
  mode it fixed and the "Preservation failure falls back to the old
  ticketless marker, byte for byte — the model is never given a path that is
  not there."
- `COMPACTION.md:76` — "The summary persists with the session" — the doc
  *states* the compaction outcome plainly; what it does not state is that the
  replaced bytes are unrecoverable, or that no fidelity check exists.
- `DEFERRED.md` — no row for "archive the prefix compaction summarizes" or
  "verify summary fidelity" (grep: no compaction row with those words).

## 6. Implications

- **The long-session guarantee is only as good as the summarizer's recall.**
  "Auto-compaction keeps a session running indefinitely" — but indefinitely
  means each successive summary is built on the *previous* summary, not the
  raw history. Errors compound: a detail dropped at compact #1 is not there
  to be dropped at #2, it is gone. Nothing bounds or measures that
  compounding, and no raw checkpoint exists to re-summarize from.
- **The session file's meaning changes silently.** Before compaction it is a
  faithful transcript; after, it is a summary plus a verbatim tail. A
  `--resume` (analysis 14), a `jichi context` report (which reads the *saved*
  session), and any export all see the post-compact form with no marker that
  the prefix was ever larger — the "summary persists with the session" line is
  the only disclosure.
- **It is the one lossy operation that also discards its own ground truth.**
  Every other lossy write in jichi (elision, tool-output dumps, the discarded-
  state pin) keeps the original somewhere a claim ticket or a ref can reach.
  Compaction is the exception, and it is the operation that touches the most
  content (the whole old prefix).

## 7. Recommendation

1. **Archive the prefix before the summary replaces it.** In
   `jc_compact_run`, before dropping `[0, cut)`, write the raw transcript to a
   session-lived store (not the turn-lived M339 dir) — e.g.
   `~/.jichi.d/sessions/<id>.compacted-N.jsonl` — and record the path in the
   summary marker. This is the direct analogue of the M348 ticket, scoped to
   compaction, and it turns the fidelity question (item 1) from unverifiable
   to diffable.
2. **A fidelity probe, mock-model only.** Plant a small set of facts (a path,
   an identifier, a decision, a "do not do X" constraint) across enough
   messages to trigger compaction, compact, and assert the summary names them.
   With the archive from step 1 the test can also assert the archive is
   complete (byte-equal to the dropped prefix). Red before, green after.
3. **One sentence in `COMPACTION.md` beside "Lossy but bounded"**: auto-
   compaction's loss is *not* recoverable by a ticket today (until step 1),
   and a resumed session holds the summary, not the history — so the durable
   record is strictly coarser than the live one was.
4. **A DEFERRED row for the current state**, in the register's form: the
   asymmetry was observed (M348 fixed the sibling), compaction's prefix has no
   archive, no fidelity check exists.

Rejected: *a real tokenizer* — the register's M77 calibration is the honest,
measured, per-model fix and a BPE table is scope creep against it; *summarize
incrementally from the archive* — step 1 is the prerequisite, not the fix.

## 8. Limitations and honest counterarguments

1. **The summary prompt is deliberately thorough** (`SUMMARIZE_SYS` lists what
   to keep, and says "Do not omit details the agent would need to continue
   correctly"). That reduces, not eliminates, the risk: it is an instruction,
   and the model still decides what "details" are. The gap is that nothing
   *measures* the outcome.
2. **A session that keeps running indefinitely may not need the raw prefix
   back.** If the work moved on, re-summarizing an old raw transcript is
   rarely useful. The archive is cheap insurance for the tail of the
   distribution (the one run where a decision at message 40 mattered at
   message 400). This is a frequency argument, not a dismissal — it is the
   same argument M348 made for its ticket.
3. **I have not driven a compaction on a large real history.** The fidelity
   claim is structural (no archive, no check) from the source; the
   *magnitude* of what a summary loses is not measured here. The
   recommendation's probe is what would measure it.
4. **The M339 store is turn-lived, which is correct for elision** (the model
   re-reads within the turn) and wrong for compaction (the summary outlives
   the turn by the session's whole life). Step 1's fix is the *lifetime*, not
   the absence of a store.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the byte heuristic + calibration (the estimator is honestly known-wrong)
grep -n "BYTES_PER_TOKEN\|msg_tokens" src/chat/jc_compact.c | head
grep -n "JC_CALIB_MIN\|JC_CALIB_MAX\|JC_CALIB_WINDOW" include/jc_calib.h

# the M348 claim ticket — lives in the ELISION helper only
sed -n '773,815p' src/chat/jc_compact.c
grep -n "preserve\|M339\|M348\|spool\|ticket" src/chat/jc_compact.c

# the summarizer — a lossy abstraction, no archive call in its branch
sed -n '416,422p' src/chat/jc_compact.c

# the docs: ticket for elision (recoverable), summary for compaction (persisted)
grep -n "claim ticket\|turn-lived\|summary persists" docs/COMPACTION.md
sed -n '520,528p' docs/COMPACTION.md      # elision: lossy but recoverable
sed -n '74,76p' docs/COMPACTION.md        # compaction: "stays compact"

# the calibration honesty (measured ratios, "wrong sign" caution)
sed -n '779,796p' docs/COMPACTION.md

# the silence: no archive/fidelity row, no fidelity test
grep -n -i "compact" docs/DEFERRED.md | grep -iE "archive|fidelity|raw|recover"
grep -rn -i "fidelity\|recall\|accurate" tests/smoke/compaction.sh tests/smoke/compact_*.sh
```

**Verdict line for the register this could seed:** *jichi's compaction is the
one lossy write with no ticket. The sibling operation — mid-turn tool-result
elision — found exactly this gap (M348) and fixed it: the full original is
spooled to a 0600 store and the marker names the path, because "the model
re-ran the original call at full price." Auto-compaction of the older prefix
instead hands the raw messages to a model ("Write a concise but complete
summary", `SUMMARIZE_SYS`, `jc_compact.c:416`) and drops them: no archive in
the compaction branch (the M339/M348 machinery is invoked only from the
elision helper, `:773-815`), the saved session holds the summary not the
history (`COMPACTION.md:76`), the M339 store is turn-lived while the summary
is session-lived, and no test checks what the summary kept. The estimator
that decides *when* it fires is honestly calibrated per model (M77, clamped
[0.5,8.0], "two samples told you the wrong sign") — the system knows its
numbers are off and fixes that; it does not yet know its summaries are lossy
and has no way to check. Revisit when: the prefix is archived before the
summary replaces it, a mock-model fidelity probe asserts the summary recalls
planted facts, `COMPACTION.md` states that compaction's loss is not
ticket-recoverable, and the turn-lived/store-vs-session-lived/summary split is
named.*
