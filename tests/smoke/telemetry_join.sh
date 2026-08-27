#!/bin/sh
# smoke: a bounded run's telemetry can be JOINED to its journal (M420).
#
# THE DEFECT THIS EXISTS FOR. A telemetry event carried `sid`/`ws`/`turn`/`depth`;
# a journal event carried `run`. Neither carried the other's key, so BEHAVIOUR
# (tokens, latency, tool ok-rates, cache) and OUTCOME (budget, verify, rollback,
# goalposts) were recorded separately and could never be correlated. Writing the
# 2026-08-13 campaign analysis, attributing a 34x apply_patch loop to the run that
# caused it required matching session files BY MTIME. See
# docs/proposals/2026-08-observability-seams.md S1.
#
# This driver does not check that a field is PRESENT -- it performs the join, which
# is the only thing worth asserting: a key that exists and does not match is worse
# than no key, because it invites a reader to trust it.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"out.txt","content":"joined\n"}
rule
  text JOIN_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 2
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto --verify "true" \
      --journal "$tmp/run.jsonl" \
      --log "$tmp/telem.jsonl" --log-level metrics \
      -p "Write the file. Do not run any build commands." < /dev/null 2>&1); rc=$?
mm_stop

# --- 1: both sinks were written ---------------------------------------------
if [ -s "$tmp/run.jsonl" ] && [ -s "$tmp/telem.jsonl" ]; then
    t_ok "the run wrote both a journal and a telemetry log"
else
    t_fail "rc=$rc journal=$(wc -c < "$tmp/run.jsonl" 2>/dev/null) telem=$(wc -c < "$tmp/telem.jsonl" 2>/dev/null); out=$(printf '%s' "$out" | head_bytes 100)"
fi

# --- 2: THE JOIN -- the journal's run id appears in telemetry ----------------
# jsonq reads one object at a time, so pull the ids with the tier's own tools:
# the journal's `start` line is the first line, and every telemetry event that
# went through telem() during the run must carry the same `run`.
# Select the start record BY NAME, not by position. `start` is not reliably the
# first line: an inferred constraint (M110) is journaled BEFORE it, and that record
# carries no `ws` -- so a `head -1` here would fail check 4 for a reason having
# nothing to do with the join. Measured on a real run whose journal began
# `constraint`. Position is not ground truth (TEST_INTEGRITY).
grep '"event":"start"' "$tmp/run.jsonl" | head -1 > "$tmp/start.json"
jrun=$("$SMOKE_TOOLS/jsonq" '.run' "$tmp/start.json" 2>/dev/null)
trun=$(grep -o '"run":"[^"]*"' "$tmp/telem.jsonl" | head -1 | sed 's/.*:"//;s/"$//')
if [ -n "$jrun" ] && [ "$jrun" = "$trun" ]; then
    t_ok "the join holds: journal run=$jrun is the run stamped on telemetry"
else
    t_fail "journal run='$jrun' vs telemetry run='$trun' -- the key does not join"
fi

# --- 3: every telem() event in the run carries it ----------------------------
# Not just one: a partial stamping would join some behaviour and silently drop
# the rest, which reads as "this run made three model calls" when it made ten.
nev=$(grep -c '"event":' "$tmp/telem.jsonl" || true)
nrun=$(grep -c '"run":"' "$tmp/telem.jsonl" || true)
if [ "$nev" -ge 4 ] && [ "$nev" -eq "$nrun" ]; then
    t_ok "all $nev telemetry events carry the run id (no partial stamping)"
else
    t_fail "$nrun of $nev telemetry events carry a run id"
fi

# --- 4: the journal names its workspace -------------------------------------
# `runs` had no workspace column at all: on a machine driving three projects a
# row could not say whose run it was.
jws=$("$SMOKE_TOOLS/jsonq" '.ws' "$tmp/start.json" 2>/dev/null)
case "$jws" in
    "$ws"*) t_ok "the journal's start event names the workspace" ;;
    *)      t_fail "start.ws='$jws' (want '$ws')" ;;
esac

# --- 5: the reader sees ONE run in a directory holding BOTH sinks (M421) -----
# This driver has always written both logs into $tmp -- which is what an operator
# does for a campaign, and doubly so now that the join invites reading them
# together. `runs <dir>` globs *.jsonl and both sinks key on "event", so before
# M421 every run rendered TWICE: once real, once all zeroes. M420 made the
# phantom worse before better, by stamping the REAL run id on it (pre-M420 it
# wore the filename, visibly not a run) -- and `constraint` is one of three event
# names both vocabularies use, so the phantom could carry telemetry's count in a
# column an operator acts on.
#
# The pure parser and the vocabulary separation are covered by tests/test_runsview.c
# and telemetry_events_lint.sh. This asserts it at the SURFACE where the defect
# actually appeared: a row an operator reads.
nrows=$("$BIN" runs "$tmp" 2>/dev/null \
        | grep -cE '^[0-9a-f]{8}-[0-9a-f]{4}-' || true)
if [ "$nrows" -eq 1 ]; then
    t_ok "runs reports exactly 1 run for a directory holding both sinks"
else
    t_fail "runs printed $nrows rows (want 1) -- a telemetry log is being read as a journal"
fi

# --- 6+7: the events that BYPASSED the stamp until M583 ------------------------
# WHY A SECOND ARM, AND WHAT IT PROVES. Check 3 above already asserts that every
# telemetry event carries the run id, and it has been GREEN throughout -- green
# because the fixture above never provokes the nine emitters that bypassed
# telem(). The assertion was right; the coverage was missing, which is M563's
# lesson: an instrument that never covers the combination is indistinguishable
# from no instrument.
#
# args_repair is the one a fixture can provoke deterministically: a tool call
# whose arguments are malformed JSON (a trailing comma) goes through jichi's
# conservative repair path (M148), which emits it. Before M583 that event
# carried no depth, no turn and no run.
cat > "$tmp/repair.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"out2.txt","content":"repaired",}
rule
  text REPAIR_DONE
EOF

mm_start "$tmp/repair.mm" "$tmp/cap2" 2
cat > "$tmp/config2.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
out2=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
       -q --no-session --auto --verify "true" \
       --journal "$tmp/run2.jsonl" \
       --log "$tmp/telem2.jsonl" --log-level metrics \
       -p "Write the file." < /dev/null 2>&1); rc2=$?
mm_stop

nrep=$(grep -c '"event":"args_repair"' "$tmp/telem2.jsonl" 2>/dev/null || true)
if [ "${nrep:-0}" -ge 1 ]; then
    t_ok "the fixture provokes args_repair ($nrep), an emitter that bypassed the stamp"
else
    t_fail "no args_repair event was emitted, so check 7 below would assert over
   nothing -- the denominator this arm exists to supply. rc2=$rc2, events:
$(grep -oE '"event":"[a-z_]+"' "$tmp/telem2.jsonl" 2>/dev/null | sort | uniq -c | head)"
fi

# Every one of them must carry all three keys. Asserted per FIELD, not merely
# per event: a partial stamp joins some behaviour and silently drops the rest.
bad=0
for k in run turn depth; do
    n=$(grep '"event":"args_repair"' "$tmp/telem2.jsonl" 2>/dev/null \
        | grep -c "\"$k\":" || true)
    [ "${n:-0}" -eq "${nrep:-0}" ] || bad=1
done
if [ "$bad" -eq 0 ] && [ "${nrep:-0}" -ge 1 ]; then
    t_ok "args_repair carries run, turn and depth -- the D4 seam is closed"
else
    t_fail "an args_repair event is missing one of run/turn/depth. Nine emitters
   in four files used to reach jc_eventlog_begin() directly and carried none of
   them, so M420's join was partial by exactly these events. Sample:
$(grep '"event":"args_repair"' "$tmp/telem2.jsonl" 2>/dev/null | head -1 | head_bytes 300)"
fi

t_done
