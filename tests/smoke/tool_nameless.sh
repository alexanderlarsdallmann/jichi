#!/bin/sh
# smoke: tool calls that arrive with NO NAME are counted, and bursts are counted
# as bursts (M585).
#
# THE DEFECT. A model can emit a tool call whose name never arrives. jichi
# answered `error: unknown tool ''`, which invites it to correct a name it never
# sent -- so it re-sent the identical empty call. Measured on a real workload:
# seven nameless calls across three sessions, every one inside a burst of two or
# three within a single turn. Three mistakes cost seven round-trips.
#
# WHY THE READER IS TESTED SEPARATELY FROM THE DIAGNOSIS. The message is a pure
# function of the call and is unit-tested (tests/test_tool.c, test_tool_nameless).
# What cannot be unit-tested is whether an OFFLINE reader ever surfaces the
# pattern -- which is the half that was missing for the eight orphan events of
# M584, one milestone earlier. This driver feeds the shipped summariser a
# hand-written log, so it needs no model, no network and no mock.
#
# BURSTS, and why they are the number that matters: a burst is a run of
# consecutive nameless calls inside one (sid, turn). Seven calls in three bursts
# is three mistakes; seven calls in seven bursts would be seven. The boundary is
# exercised in BOTH directions below -- a new turn in the same session, and a new
# session -- because a counter keyed on only one of them would still print 3 for
# some fixture and be wrong.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

# Written literally rather than generated: a fixture a reader can check by eye is
# worth more here than one that shares a bug with the code under test.
cat > "$tmp/t.jsonl" <<'EOF'
{"v":1,"ts":1786000001,"sid":"aaaa","ws":"/w","seq":1,"event":"turn_start","turn":3}
{"v":1,"ts":1786000002,"sid":"aaaa","ws":"/w","seq":2,"event":"tool_call","turn":3,"name":"read_file","ok":true,"duration_ms":0.2,"output_bytes":10}
{"v":1,"ts":1786000003,"sid":"aaaa","ws":"/w","seq":3,"event":"tool_call","turn":3,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000004,"sid":"aaaa","ws":"/w","seq":4,"event":"tool_call","turn":3,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000005,"sid":"bbbb","ws":"/w","seq":5,"event":"tool_call","turn":9,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000006,"sid":"bbbb","ws":"/w","seq":6,"event":"tool_call","turn":9,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000007,"sid":"bbbb","ws":"/w","seq":7,"event":"tool_call","turn":9,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000008,"sid":"bbbb","ws":"/w","seq":8,"event":"tool_call","turn":44,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000009,"sid":"bbbb","ws":"/w","seq":9,"event":"tool_call","turn":44,"name":"","ok":false,"duration_ms":0.0,"output_bytes":0}
{"v":1,"ts":1786000010,"sid":"bbbb","ws":"/w","seq":10,"event":"tool_call","turn":44,"name":"write_file","ok":true,"duration_ms":1.0,"output_bytes":5}
EOF

rep=$("$BIN" telemetry "$tmp/t.jsonl" 2>&1)

# --- 1: the denominator ------------------------------------------------------
# Every check below reads one line out of the reader's output. If the reader
# failed to parse the fixture at all it would print no Tools section, and the
# absence assertions would hold over nothing.
case "$rep" in
    *"read_file"*|*"write_file"*)
        t_ok "the reader parsed the fixture (the named calls are present)" ;;
    *)
        t_fail "the reader produced no tool rows, so nothing below is measured.
   Output was:
$(printf '%s' "$rep" | head_bytes 300)" ;;
esac

# --- 2: nameless calls are counted at all ------------------------------------
line=$(printf '%s\n' "$rep" | grep 'NO NAME')
if [ -n "$line" ]; then
    t_ok "the reader reports tool calls that arrived with no name"
else
    t_fail "seven nameless calls in the log and the reader said nothing. The
   \`tool_call\` event has carried the empty name all along -- what was missing
   was a reader, which is exactly how eight event types went unread until M584."
fi

# --- 3: the count is right ---------------------------------------------------
n=$(printf '%s\n' "$line" | sed -n 's/.*NO NAME: \([0-9][0-9]*\).*/\1/p')
if [ "$n" = "7" ]; then
    t_ok "all 7 nameless calls are counted"
else
    t_fail "counted '${n:-nothing}' nameless calls, want 7 -- the fixture holds
   exactly seven, written out one per line so the expected number can be read
   off the file by eye."
fi

# --- 4: bursts, not calls ----------------------------------------------------
# 7 calls but only 3 mistakes: aaaa/turn3, bbbb/turn9, bbbb/turn44. A counter
# that keyed only on the session id would say 2; only on the turn, 3 by luck
# here -- so the fixture changes the turn WITHIN a session on purpose.
b=$(printf '%s\n' "$line" | sed -n 's/.* in \([0-9][0-9]*\) burst.*/\1/p')
if [ "$b" = "3" ]; then
    t_ok "the 7 calls are reported as 3 bursts (3 mistakes, not 7)"
else
    t_fail "reported '${b:-nothing}' bursts, want 3. The log holds three runs of
   consecutive nameless calls -- two in session aaaa turn 3, three in bbbb turn
   9, two in bbbb turn 44. A counter keyed on the session alone gives 2; one
   keyed on the turn alone happens to give 3 here and is still wrong."
fi

# --- 5: control -- a clean log says nothing -----------------------------------
# Without this the line could be unconditional and checks 2-4 would still pass.
grep -v '"name":""' "$tmp/t.jsonl" > "$tmp/clean.jsonl"
clean=$("$BIN" telemetry "$tmp/clean.jsonl" 2>&1)
if printf '%s\n' "$clean" | grep -q 'NO NAME'; then
    t_fail "a log with no nameless calls still printed the line -- it must appear
   only when the count is non-zero, or every clean report grows a row of zeros
   that reads as a measurement."
else
    t_ok "control: a log with no nameless calls does not print the line"
fi

t_done
