#!/bin/sh
# smoke: a red worktree verify's excerpt shows the failing lines first, and the whole output
# is kept for the lead (M862, the demos drive's N43).
#
# THE CASE. Run 0090, 06:44:19: the batch result gave the lead the last 500 bytes of a red
# verify -- zig's build summary ("1075/1078 tests passed (3 failed)") and the failed build
# command, about 250 bytes of paths. Zig prints the failing tests before its summary, so
# their names and the line "expected 47, found 40" were not in the excerpt. The lead saw
# "3 failed", attributed the red to the worker's stash, and sent the same fix to the next
# worker as "verified".
#
# THE FIX (DECISIONS M862; N43's (a) and (c)):
#   - the lines before the tail that carry a failure marker come first, at most 12, each
#     cut at 200 bytes, with a count of those left out; then the 500-byte tail as before;
#   - the whole output is kept as a claim ticket in the lead's store, and the result
#     names its file (the lead is not fenced, so N41 does not apply).
#
#   1  a failing test's line printed before a long summary is in the batch result
#   2  and so is the "expected 47, found 40" line
#   3  the result names the whole output's file, and the lead can read it: a line that is
#      in neither the failing lines nor the tail reaches the lead through it
#   4  a line already in the tail is not shown twice
#   5  thirty failing lines show twelve, and say how many more there are
#
# Markers are printed by the verify script by arithmetic ($((6*7)) and the like), so no
# check finds its marker in a command or a config; the checks read the requests jichi sent.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = the verify command
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "parallelVerify":true,"verify":"$1"}
EOF
}
run() {  # <mm> <verify command> -> WS, CAP, OUT
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    cp "$1" "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1 )
    mm_stop
}
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }

# A red verify in zig's order: the failures, a line no excerpt shows, then a summary
# longer than the 500-byte tail.
cat > "$tmp/red.sh" <<'EOF'
echo "error: 'demo.test.case$((6*7))' failed:"
echo "expected $((40+7)), found $((5*8))"
echo "middle-$((3*111))-only-in-the-whole-output"
i=0
while [ "$i" -lt 20 ]; do
    echo "summary line $i: build step ok, cached, MaxRSS:41M ......."
    i=$((i + 1))
done
exit 1
EOF
cat > "$tmp/red.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "middle-333-only"
  text LEAD-DONE
rule
  match "LEADX"
  match "## Parallel agents"
  tool run_terminal_command {"command":"cat \"$HOME\"/.jichi.d/tool-output/*/verify-*.txt"}
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDV write a file","write":true}]}
rule
  match "CHILDV"
  match "\"role\":\"tool\""
  text CHILDV-DONE
rule
  match "CHILDV"
  tool write_file {"path":"v.txt","content":"v\n"}
EOF
run "$tmp/red.mm" "sh $tmp/red.sh"
LR=$(lead_result)
if [ -n "$LR" ] && grep -q -F "error: 'demo.test.case42' failed:" "$LR"; then
    t_ok "a failing test's line printed before a long summary is in the batch result"
else
    t_fail "the failing test's line is not in the batch result (red before M862: the tail only)"
fi
if [ -n "$LR" ] && grep -q -F 'expected 47, found 40' "$LR"; then
    t_ok "and so is the expected/found line"
else
    t_fail "the expected/found line is not in the batch result"
fi
# The middle line is in neither the failing lines nor the tail, so it reaches the lead only
# through the file the result names.
named=$(grep -c 'the whole output ([0-9]* bytes): ' "${LR:-/dev/null}" 2>/dev/null)
inres=$(grep -c -F 'middle-333-only' "${LR:-/dev/null}" 2>/dev/null)
read=$(grep -l -F 'middle-333-only' "$CAP"/cap/req.* 2>/dev/null | wc -l | tr -d ' ')
if [ "${named:-0}" -ge 1 ] && [ "${inres:-1}" = "0" ] && [ "$read" -ge 1 ]; then
    t_ok "the result names the whole output's file, and the lead reads what no excerpt showed"
else
    t_fail "the whole output: named=$named, in the result=$inres, read by the lead=$read"
fi

# --- 4: a short output: its failing line is in the tail, shown once ------------------
cat > "$tmp/short.sh" <<'EOF'
echo "FAIL short-case-$((2*21))"
exit 1
EOF
cat > "$tmp/short.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDV write a file","write":true}]}
rule
  match "CHILDV"
  match "\"role\":\"tool\""
  text CHILDV-DONE
rule
  match "CHILDV"
  tool write_file {"path":"v.txt","content":"v\n"}
EOF
run "$tmp/short.mm" "sh $tmp/short.sh"
LR=$(lead_result)
n=$(grep -o -F 'FAIL short-case-42' "${LR:-/dev/null}" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "1" ] && ! grep -q 'failing lines (before the tail)' "$LR"; then
    t_ok "a failing line already in the tail is shown once"
else
    t_fail "a short red verify's line appears $n time(s) in the result"
fi

# --- 5: thirty failing lines --------------------------------------------------------------
cat > "$tmp/many.sh" <<'EOF'
i=0
while [ "$i" -lt 30 ]; do
    echo "FAIL case-$((i + 100))"
    i=$((i + 1))
done
i=0
while [ "$i" -lt 20 ]; do
    echo "summary line $i: build step ok, cached, MaxRSS:41M ......."
    i=$((i + 1))
done
exit 1
EOF
run "$tmp/short.mm" "sh $tmp/many.sh"
LR=$(lead_result)
shown=$(grep -o 'FAIL case-1[0-9][0-9]' "${LR:-/dev/null}" 2>/dev/null | sort -u | wc -l | tr -d ' ')
if [ "$shown" = "12" ] && grep -q -F '(18 more such line(s) before the tail)' "$LR"; then
    t_ok "thirty failing lines show twelve, and say how many more"
else
    t_fail "thirty failing lines: $shown shown; $(grep -o '([0-9]* more such[^)]*)' "${LR:-/dev/null}" 2>/dev/null)"
fi

t_done
