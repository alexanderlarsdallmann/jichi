# JICHI TYPE_AHEAD.md — gap analysis (analysis 45)

## TL;DR
*Read-only survey of docs/TYPE_AHEAD.md at HEAD 1b71247d (258 lines), cross-checked against src/tui/jc_tui.c and git history, 2026-10-05.*

*The type-ahead feature is one of the most carefully documented surfaces in the tree: every design decision D1–D11 is stated, and I verified all of them true against the code (the [operator] prefix, ISIG/OPOST kept, TUI_QUEUE_MAX=4096, Enter-only commit, the two smoke tests). The one real gap is a STALE KEYS TABLE: the doc was last touched 2026-08-02 (M258), but M753 (2026-09-25) added two behaviors the keys table omits.*

## The two M753 behaviors the keys table omits

1. **Esc now stops a running turn** (as Ctrl-C does), while type-ahead is
   held. The doc's keys table lists only Ctrl-C as the abort key and says
   arrows/function-keys are "ignored". M753 (`tui_esc_stop`, jc_tui.c:394,
   called from queue_poll at :415 and :509) makes a bare Esc (distinguished
   from an arrow/function sequence by a bounded wait, `TUI_ESC_WAIT_MS`)
   abort the model call and kill a running tool. A user who reads the table
   and holds Esc does not know the turn stops.

2. **Type-ahead OFF is now not silent.** Before M753, typing with the feature
   off simply did nothing (the terminal was held but nothing was collected or
   said). M753 added `JC_MSG_TYPEAHEAD_OFF` ("not kept (type-ahead is off;
   /typeahead on keeps it)"), printed once per turn (`c->ta_noticed`, jc_tui.c:
   435-438). The doc's off-by-default section does not mention this notice.

## Why the keys table is the right place for both

The table is the doc's contract with the user about what the terminal does
while a turn runs. M753 changed exactly that contract (two new key
behaviors) and the doc was not updated: last touch 2026-08-02 (M258 commit
0e501122), M753 landed 2026-09-25 (53ed13d4) and did not touch TYPE_AHEAD.md
(verified: git show 53ed13d4 --stat has no TYPE_AHEAD).

Note the sharper form: the table is not merely incomplete, it is FALSE for
one key. The "arrows, function keys | ignored" row covers Esc, and since
M753 a bare Esc is NOT ignored while a turn runs — it aborts the model call
and kills a running tool. A user who reads "ignored" will be surprised the
first time the turn stops.

## What checks out (the doc is 95% accurate)
Verified true against code: --type-ahead/--no-type-ahead (main.c:399,1529),
config typeAhead (jc_config.c:1312), /typeahead + the M258 immediate-enable
fix (jc_tui.c:3367-3370), Enter-only commit, the [operator] prefix
(jc_message.c:114), D5 ISIG/OPOST kept (jc_tui.c:531), D6 shared
jc_history_add_operator (jc_control.c:516, jc_agent.c:4431), D7 overflow
notice naming the dropped line + end-of-turn unsent print-back (jc_tui.c:
378-379, 563; JC_MSG_QUEUE_FULL/UNSENT in the M137 catalog), D11 idle tick in the command runner (jc_app.c:762, M258), and both smoke tests exist
(typeahead.sh, typeahead_live.sh).

## The corrections (ready to apply)
1. TYPE_AHEAD.md keys table: add a row "| **Esc** | M753: stops a running
   turn (as Ctrl-C); an arrow/function-key sequence is still ignored |".
2. Same table, the arrows row: clarify it no longer covers a bare Esc.
3. Off-by-default section: one line on the M753 "not kept (type-ahead is
   off)" notice.
Each a 1–3 line edit; all three are the M753 behavior, none needs code.

## Register connection
The DEFERRED M715-note staleness (analysis 39) and the plan-header staleness
(38) are the same class: a shipped milestone changed behavior, the
feature-facing doc did not follow. M753 is the third instance in a TUI-key
context. A lint cannot catch this cheaply (the doc's keys table is prose),
so the practical guard is the M711-style discipline: any commit that
changes key handling under the type-ahead hold should name TYPE_AHEAD.md
in its touched-files — as 53ed13d4's own summary did for the catalog, but
not for the doc.

## Limitations
- M753's exact key semantics (Esc disambiguation wait, held-Esc bound) are
  read from jc_tui.c; I did not PTY-drive them. The doc's own smoke test
  (typeahead_live.sh) covers the pre-M753 behavior.
- D-claims I marked verified were spot-checked at the named line, not all
  exercised. The feature is opt-in; my checks are static.

## Appendix (reproducible)
  git log -1 --format="%ad %h" -- docs/TYPE_AHEAD.md    # 2026-08-02 M258
  git show 53ed13d4 --stat | grep -c TYPE_AHEAD         # 0
  grep -n tui_esc_stop src/tui/jc_tui.c | head          # M753, called at :415,:509
  grep -n "TYPEAHEAD_OFF" src/tui/jc_tui.c              # :438
  grep -nE "Esc|Ctrl-C|arrows" docs/TYPE_AHEAD.md       # the stale table

## Verdict
TYPE_AHEAD.md is among the tree's best-documented features; its one gap is
a stale keys table (M753's Esc-abort and off-mode notice unrecorded; the
"ignored" row false for Esc since 2026-09-25). A 1–3 line doc fix; no code
change, no behavior gap.
