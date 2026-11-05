# Inject delivery state (batch 14, item (d)) — design note

*The finding: `jichi control` reports whether a queued note was applied — batch 14 item (d)
of `docs/internal/drive/2026-10-17-hardening/PLAN.md`. Milestone **M1045** (claimed from the
drive's M960-onwards range; M1044 was the last used, run 0071). Written 2026-10-23 (run 0072).*

**Status:** design settled for M1045 (one core step; no `-b` planned unless the gate or the
supervisor measures otherwise). The 23:34 grant lets item (d) go ahead red-first.

## The gap, measured on e74f4522 (run 0072)

The control protocol already knows three delivery moments for an inject: it is **acked**
(server accepted the text into the queue), **queued** (sitting in the pending buffer), and
**applied** (folded into history as one `[operator]` user message at a boundary). The wire
reports only the first. From the outside:

1. **The ack overclaims while paused.** `serve_pending` answers an inject with
   `"note":"applied at next model call"` (src/chat/jc_control.c:378) on every accept — and
   accepts happen from `jc_control_boundary`'s pause loop, so a `pause`d run acks "applied
   at next model call" at the exact moment its next model call cannot happen (the loop is
   blocked). Measured: `pause` → `inject` → reply `{"ok":true,"note":"applied at next model
   call"}` while `status` says `"paused":true`.
2. **`status` has no delivery field at all.** `struct jc_control_status` carries
   `paused`, `last_tool`, counts of the run — but neither how much inject text is waiting
   nor how many folds have happened. Measured: the full status line
   `{"v":1,"ok":true,"mode":"auto","run":"…","elapsed":3,"tokens_used":25,
   "budget_tokens":100000,"tool_calls":1,"deadline_secs":120,"outcome":"running",
   "last_tool":"run_terminal_command","paused":true}` — no `queued_injects`, no
   `injects_applied` (and the run had been injected 1 s earlier).
3. **The run-level `steered` flag is not the same fact.** `runs` sets `steered` from
   journal events (M161), readable only after the run ends; it cannot distinguish
   "acked, still queued" from "folded, in history". A supervisor's `jichi ps` + `control
   status` polling loop has nothing to tell it either.
4. **The client's exit-3 path conflates delivered with applied.** `run_control` says "the
   command was delivered and is queued for the next tool boundary" on no-reply (src/main.c
   ~17000) — a true statement, but "delivered" there means the socket write succeeded,
   and with item (d) the supervisor can now check the queue itself instead of trusting a
   prose claim.

The finding is item (d)'s own words: `jichi control` **reports whether a queued note was
applied**. The note is already in the transcript (the fold is observable in request N) —
what is missing is the *channel's* report of it.

## The shape

Two integers in `struct jc_control_status`, **always emitted** (zero is a meaningful
state, unlike the other fields' omit-when-zero convention — a supervisor polling from
cold must observe the baseline):

```
"injected": <int>,        // total inject commands this run accepted into the queue
"queued_injects": <int>,  // lines still waiting in the pending buffer (0 = none)
```

- `injected` is incremented in the `JC_CTL_INJECT` accept path (serve_pending), next to the
  existing `journal_cmd(app, "inject", …)` — the journal is the record, the counter the
  live view.
- `queued_injects` is **derived, not stored**: the pending buffer already keeps one line
  per inject (separators appended before each non-first line); `queued_injects =
  jc_sb_line_count(&c->pending)` (a small helper on `struct jc_sb` counting `'\n'` + one
  non-empty tail, or the equivalent inline in jc_control.c). A stored counter that a
  future change must remember to decrement is the class of bookkeeping bug the drive has
  already caught (the pending buffer is the single source of truth; the fold clears it).
- The ack note stays `"applied at next model call"` — it is true at accept time for a
  non-paused run, and the *observable* correction for the paused case is the status pair
  (`paused:true` + `queued_injects:1`), not a new note string. Changing the note would
  break the existing smoke assertion in `tests/smoke/control.sh` (run 1) and the M159
  e2e history without adding information the pair does not add. If the supervisor reads
  that as an overclaim worth its own pin, that is a `-b`.
- `injects_applied` (a third counter, incremented at the fold site in
  `jc_control_boundary`) was considered and rejected for the first pass: it duplicates
  `injected - queued_injects` while the run is alive, and after the run it is the
  journal's `{"cmd":"inject"}` events again — two writers for one fact (the M1042
  "several writers" class, applied to bookkeeping). The invariant `injected =
  applied + queued` is what a supervisor computes; the channel reports the two terms it
  owns. Revisit if a real consumer needs the third.

**Protocol version stays `JC_CONTROL_PROTO_V` 1:** additive fields, and every documented
consumer (the `jichi control` client prints the line, `jichi ps` polls, the smoke tier
reads by field) tolerates extras — the same argument `mode` (M304) and `deadline_credit`
(M162) made when they landed.

### Where the fold is (the single write site)

`jc_control_boundary` is the ONLY place pending text enters history (the TUI
`take_input` path and the envelope budget notice use `jc_history_add_operator` directly
and never touch `c->pending`; `jc_control_poll` passes `hist=NULL` by construction).
So `queued_injects` falling to 0 after a fold is pinned by the one code path that
causes it.

## Red-first (the driver, `tests/smoke/control.sh` t_plan 12 → 15)

Run 6: a fresh run; during the `sleep 3` window `inject` (expect the existing ack),
`status` → **`queued_injects` must be 1 and `injected` 1** (this is the red line: on the
unfixed tree the status line has neither field; jsonq reads empty). Then `abort` (the
run was never resumed, so the queued note stays queued — the folded-state check would be
a false green from the next run's mock). Assertions:

14. the `inject` ack still says `applied at next model call` (unchanged behaviour)
15. status after the inject: `injected == 1` and `queued_injects == 1` (RED on head)
16. baseline: status taken BEFORE any inject shows `injected == 0` and
    `queued_injects == 0` (proves the fields are emitted, not omitted-when-zero)

Plus a unit leg in `tests/test_control.c` (red first, with the driver): build a status
with `injected=3, queued_injects=1` and assert the wire carries both; build one with
zeros and assert the wire carries `0` (the always-emit contract).

## Not goals

- No new verb (`delivery` / `steering` read is `status`'s job; a verb would be a second
  writer for the same counters).
- No note-string change for the paused ack (rejected above; the pair is the pin).
- No `injects_applied` third counter (rejected above).
- Nothing in `run_control`'s client path beyond printing (it already prints the whole
  line); the exit-3 prose is not touched in M1045.
- No e2e (python) change: the smoke tier is where the control channel is tested (M214).

## M1045-b (the supervisor's 2026-10-23 05:59 close): notes, not lines

The M1045 "derived, not stored" shape had a seam the gate did not exercise. `queued_injects`
was computed by `pending_line_count` (src/chat/jc_control.c), which counts `'\n'` bytes plus a
non-empty tail in `c->pending`. But each accepted inject appends `cmd.text` **whole** to the
buffer (the `JC_CTL_INJECT` case), and the fold in `jc_control_boundary` turns the *entire*
buffer into ONE `[operator]` message. So a single inject whose note contains a newline byte — a
JSON `\n` escape decodes to a real newline before the append — reads as more than one line,
even though it is one note and one fold. The line count was the wrong quantity for the field's
name: `queued_injects` was silently "queued lines in the buffer" instead of "queued notes".

**Measured (the supervisor's probe, 2026-10-23 05:59).** Baseline status:
`"injected":0,"queued_injects":0`. After one two-line note, `status` read
`"injected":1,"queued_injects":2` — one note, two lines. The smoke check 15
(`status after the inject: injected=1 and queued_injects=1`) went red on a multi-line inject
for exactly this reason; the shipped `jichi control inject` client cannot carry a newline (it
joins single-line positional args), so only a raw-socket caller reaches the discrepancy, and
that is precisely the supervisor's own probe.

**The fix: a pending-NOTES counter owned by the buffer's own lifecycle.** `struct jc_control`
gains `long pending_notes;` (after `injected`; zeroed for free by `jc_control_open`'s existing
`memset`). It is set at the two places that own the buffer, and nowhere else:

- the `JC_CTL_INJECT` accept path, beside (not in place of) the existing `c->injected++`:
  `c->pending_notes++;` — the note is now queued;
- the fold in `jc_control_boundary` (the `if (c->pending.len > 0 && hist != NULL)` block, after
  the `jc_sb_free`/`jc_sb_init` of both buffers): `c->pending_notes = 0;` — the fold is the one
  place the buffer is emptied.

`status_reply` then reads `s.queued_injects = c->pending_notes;`, and the `pending_line_count`
static helper and its comment are **deleted** (its line-count was the defect).

**Why a counter, and why it is not a second writer of `injected`.** M1045 chose "derived, not
stored" because a stored counter a future change must remember to decrement is the bookkeeping
bug the drive has already caught. That instinct was right *against `injected`* — `injected`
keeps its semantics unchanged: total accepts, incremented on accept, never decremented.
`pending_notes` is different: it is the live size of the queue, and it is owned by the buffer's
own lifecycle, not derived from the buffer's *contents*. The two writers are exactly the two
sites that already free/re-init `c->pending` — the accept appends, the fold frees — so a future
change to how the buffer is emptied cannot desynchronize the count, and no site outside the
buffer's lifecycle touches it. It is a second writer of `pending`'s size, not of `injected`.

**The new driver leg** (`tests/smoke/control.sh`, t_plan 15 → 16). Run 6 gains a second inject
after the existing first-inject status check: a note whose text carries a real newline, sent
raw through `sockq` (the shipped `ctl` client cannot carry one). It asserts `injected == 2`
AND `queued_injects == 2` — on the unfixed tree the second status reads `queued_injects == 3`
(1 line from the first note + 2 from the second), the red. The leg is the teeth; the wire
contract (`queued_injects` always emitted, the value a caller stores) is unchanged, so
`tests/test_control.c` is untouched.

## The follow-ups this step names, not builds

- **M1045-b** (only if measured needed): a note-string or client-side hint for the
  paused-accept overclaim; or `injects_applied` for a consumer that wants the fold count
  without computing it.
- The B1 non-blocking pool (item (b)) will add *worker*-depth control; delivery state
  there would be a per-pool matter, out of scope for the top-level `struct jc_control`.

## Verification plan (lead)

Fast gate: `make WERROR=1 test` (unit count goes up by the new checks; re-measure on the
merged head, not the worktree), `control.sh` 15/15, neighbours `daemon.sh` /
`control_*.sh` untouched. The `docs` pages (CONTROL.md table row, the protocol block)
land with the lead's record commit, with `docs_counts_lint` / `notice_tags_lint` green
(no new notice tags; the status fields need no NOTICES.md row). ROADMAP entry + the
banner's newest-milestone check (`docs_counts_lint` reads the `**M…**` banner) in the
same record commit.
