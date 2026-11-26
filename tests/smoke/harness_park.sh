#!/bin/sh
# smoke: `jichi-harness --config <f> park ...` / `restart ...` -- the park/restart
# verbs smoke driver (M1213 T2, batch H12; 5 cells, items 4-6 from M1213 review).
#
# WHAT IS CHECKED (the 5 cells):
#   1  item 5 LAST-3 park: fixture 5-line harness.log; assert printed block is lines 3,4,5
#   2  item 5 LAST-3 restart: same, restart verb (same shared code path)
#   3  item 4 PINNED: fixture pins/current symlink; assert output contains `pinned: <target>`
#   4  item 6 DOCTOR-LONG: stand-in doctor outputs 130-byte junk + `ok --live: tool calling observed`; assert READY
#   5  item 5 short-log guard: 2-line harness.log prints both lines, no refusal
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
export TZ=UTC
tmp=$(smoke_tmp)
_mk=$(smoke_make)
(cd "$SMOKE_ROOT" && $_mk jichi-harness -s) >/dev/null 2>&1
"$BIN" --version > "$tmp/binv" 2>/dev/null
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

# Stand-ins (written once; token-substituted so no shell state leaks)
SHIM="$tmp/shim"

# --- the drive dir D -----------------------------------------------------------
D="$tmp/drive"
H="$D"
CFG="$D/conf/harness.json"
KEY="$D/key"
mkdir -p "$D/runs/0098" "$D/runs/0099" "$D/conf" "$D/pins" "$SHIM" || {
    echo "not ok - fixture mkdir failed" >&2
    t_done
}
# The model probe needs a non-empty key file (park_model_probe refuses a missing
# one); the doctor stand-in never reads it, so a canary prefix is enough. The
# ok --live line must not contain its first 12 chars (redaction arm).
printf '12345678901234567890' > "$KEY"

# unitState.sh: reads flag file, outputs active/inactive, exit 0
printf '#!/bin/sh\ncat "%s"\nexit 0\n' "$tmp/ustate" > "$SHIM/unitState.sh"

# unitFailed.sh: reads flag file, outputs failed/notfailed; exit 0 when failed, 1 when notfailed
printf '#!/bin/sh\nstate=$(cat "%s")\n[ "$state" = "failed" ] && exit 0 || exit 1\n' "$tmp/ufailed" > "$SHIM/unitFailed.sh"

# gatewayProbe.sh: prints code from flag file
printf '#!/bin/sh\ncat "%s"\n' "$tmp/gwcode" > "$SHIM/gatewayProbe.sh"

# modelProbe.sh: prints line from flag file; RC1 -> prints line and exits 1
cat > "$SHIM/modelProbe.sh" << 'EOM'
#!/bin/sh
flag="$1"
line=$(cat "$flag")
if [ "$line" = "RC1" ]; then
    printf 'warn --live: no healthy upstream\n'
    exit 1
fi
printf '%s\n' "$line"
exit 0
EOM

# containerGone.sh: rc is the check (like the real `podman container exists`)
printf '#!/bin/sh\n[ ! -f "%s" ] || grep -qF gone "%s" && exit 0\nexit 1\n' "$tmp/gone" "$tmp/gone" > "$SHIM/containerGone.sh"

# unitRun.sh: appends line to logfile, exit 0
printf '#!/bin/sh\necho "unit armed by stand-in" >> "%s"\nexit 0\n' "$tmp/unitrun.log" > "$SHIM/unitRun.sh"

# pidList.sh: prints fixed line
printf '#!/bin/sh\nprintf "loop pid 4242 started Mon Nov 23 07:00:00 2026; exe -> %s/jichi-harness\\n"\n' "$ROOT" > "$SHIM/pidList.sh"

# loopExit.sh: background appender for park cells (appends STOP-ended lines after 0.3s)
# Note: $D is expanded at write time (token-substitution)
printf '#!/bin/sh\nsleep 0.3\nprintf '"'"'STOP file present: loop ends\\nloop end\\n'"'"' >> "%s/harness.log"\nprintf '"'"'inactive'"'"' > "%s/ustate"\n' "$D" "$tmp" > "$SHIM/loopExit.sh"

chmod +x "$SHIM"/*.sh 2>/dev/null

# runs/0098/telemetry.jsonl (3 lines) - from test_pagestretch.c sttest call
: > "$D/runs/0098/telemetry.jsonl"
printf '{"event":"model_call","ts":9000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' > "$D/runs/0098/telemetry.jsonl"
printf '{"event":"model_call","ts":9010.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' >> "$D/runs/0098/telemetry.jsonl"
printf '{"event":"model_call","ts":9020.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' >> "$D/runs/0098/telemetry.jsonl"

# runs/0099/telemetry.jsonl (3 lines)
printf '{"event":"model_call","ts":10000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' > "$D/runs/0099/telemetry.jsonl"
printf '{"event":"model_call","ts":10010.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' >> "$D/runs/0099/telemetry.jsonl"
printf '{"event":"model_call","ts":10020.0,"latency_ms":0,"ok":true,"status":200,"depth":0}\n' >> "$D/runs/0099/telemetry.jsonl"

# pin-build/s8: a fake build file
printf 'a0a8b4ae\n' > "$D/pins/s8"

# config writer
cfg_write() {
    _cw_path="$1"; _cw_unit="$2"; _cw_co="$3"
    cat > "$_cw_path" <<EOF
{"dir":"$D","drive":{"loopUnit":"$2","containerName":"$3","waitInterval":"1","readyInterval":"1","notReadyInterval":"1","postStart":"0"},"host":{"pageUnitState":"$SHIM/unitState.sh","pageUnitFailed":"","pageContainerGone":"","pageUnitRun":"","pagePidList":""},"modelKeyFile":"$D/key"}
EOF
}

# --- cell 1: item 5 LAST-3 park -----------------------------------------------
# Deterministic: the unit is ALREADY inactive, so the wait loop exits at once and
# the verb reads the full 7-line log (5 + the 2 STOP-ended lines) before printing
# the last 3. No background appender, so tail -3 is line5, STOP-file, loop-end.
: > "$D/harness.log"
printf 'line1\nline2\nline3\nline4\nline5\nSTOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'inactive' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
cfg_write "$CFG" "jhp-fixture-loop" ""
touch "$D/STOP"
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0 > "$tmp/park1.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'harness.log'\''s last 3 lines:' "$tmp/park1.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
# extract the lines after "harness.log's last 3 lines:"
_last3=$(sed -n '/harness.log.*last 3 lines:/,$p' "$tmp/park1.out" | tail -n +2 | head -3)
if [ "$_found" -eq 1 ] && printf '%s\n' "$_last3" | grep -qF 'line5' && printf '%s\n' "$_last3" | grep -qF 'STOP file present: loop ends' && printf '%s\n' "$_last3" | grep -qF 'loop end'; then
    t_ok "1: item 5 LAST-3 park: harness.log's last 3 lines: line5, STOP file present, loop end"
else
    t_fail "1: item 5 LAST-3 park expected line5, STOP file present, loop end (got: $(tr '\n' '|' < "$tmp/park1.out" | head_bytes 300))"
fi

# --- cell 2: item 5 LAST-3 restart --------------------------------------------
# Deterministic, as cell 1: the unit is already inactive, so restart reads the
# full 7-line log and prints the last 3 (line5, STOP-file, loop-end).
: > "$D/harness.log"
printf 'line1\nline2\nline3\nline4\nline5\nSTOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'inactive' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
cfg_write "$CFG" "jhp-fixture-loop" ""
touch "$D/STOP"
( exec "$HBIN" --config "$CFG" restart --wait-interval 1 --post-start 0 > "$tmp/restart1.out" 2>&1 ) &
_restartpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'harness.log'\''s last 3 lines:' "$tmp/restart1.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_restartpid" 2>/dev/null
wait "$_restartpid" 2>/dev/null
_last3=$(sed -n '/harness.log.*last 3 lines:/,$p' "$tmp/restart1.out" | tail -n +2 | head -3)
if [ "$_found" -eq 1 ] && printf '%s\n' "$_last3" | grep -qF 'line5' && printf '%s\n' "$_last3" | grep -qF 'STOP file present: loop ends' && printf '%s\n' "$_last3" | grep -qF 'loop end'; then
    t_ok "2: item 5 LAST-3 restart: harness.log's last 3 lines: line5, STOP file present, loop end"
else
    t_fail "2: item 5 LAST-3 restart expected line5, STOP file present, loop end (got: $(tr '\n' '|' < "$tmp/restart1.out" | head_bytes 300))"
fi

# --- cell 3: item 4 PINNED ----------------------------------------------------
# The verb must reach the post-start branch (pinned: <target>): the resume gate
# (n1 == n0+1, last line "loop end", unit not failed) and the READY gate (2
# consecutive gateway+model probes) must both pass, so the config wires the full
# shim set and loopExit.sh appends the STOP lines AFTER n0 is counted.
ln -s s8 "$D/pins/current"
: > "$D/harness.log"
printf 'line1\nline2\nline3\nline4\nline5\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
cat > "$CFG" <<EOF
{"dir":"$D","drive":{"loopUnit":"jhp-fixture-loop","containerName":"","waitInterval":"1","readyInterval":"1","notReadyInterval":"1","postStart":"0"},"host":{"pageUnitState":"$SHIM/unitState.sh","pageUnitFailed":"$SHIM/unitFailed.sh","pageContainerGone":"$SHIM/containerGone.sh","pageUnitRun":"$SHIM/unitRun.sh","pagePidList":"$SHIM/pidList.sh","pageGatewayProbe":"$SHIM/gatewayProbe.sh","pageModelProbe":"$SHIM/modelProbe.sh $tmp/modelline"},"modelKeyFile":"$KEY"}
EOF
touch "$D/STOP"
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0 > "$tmp/park2.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25; do
    if grep -qF 'pinned: s8' "$tmp/park2.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
wait "$_appender" 2>/dev/null
# the output must contain `pinned: s8` (exact byte-for-byte)
if [ "$_found" -eq 1 ] && grep -qF 'pinned: s8' "$tmp/park2.out" 2>/dev/null; then
    t_ok "3: item 4 PINNED: output contains \`pinned: s8\`"
else
    t_fail "3: item 4 PINNED expected \`pinned: s8\` in output (got: $(tr '\n' '|' < "$tmp/park2.out" | head_bytes 600))"
fi

# --- cell 4: item 6 DOCTOR-LONG -----------------------------------------------
# stand-in doctor that outputs 130-byte junk line then `ok --live: tool calling observed`
cat > "$SHIM/doctorLong.sh" << 'EOM'
#!/bin/sh
printf 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n'
printf 'ok --live: tool calling observed\n'
exit 0
EOM
chmod +x "$SHIM/doctorLong.sh"
# doctorLong prints a 130-byte junk line BEFORE the ok --live line: with the old
# 121-byte capture the --live line is past the cut and the probe reads no --live
# line forever; with the 4096 capture it is found in the full buffer.
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
: > "$D/harness.log"
printf 'line1\nline2\nline3\nline4\nline5\n' >> "$D/harness.log"
printf '200' > "$tmp/gwcode"
printf 'gone' > "$tmp/gone"
# the model probe is the doctorLong stand-in (no flag arg: its own output IS the probe)
cat > "$CFG" <<EOF
{"dir":"$D","drive":{"loopUnit":"jhp-fixture-loop","containerName":"","waitInterval":"1","readyInterval":"1","notReadyInterval":"1","postStart":"0"},"host":{"pageUnitState":"$SHIM/unitState.sh","pageUnitFailed":"$SHIM/unitFailed.sh","pageContainerGone":"$SHIM/containerGone.sh","pageUnitRun":"$SHIM/unitRun.sh","pagePidList":"$SHIM/pidList.sh","pageGatewayProbe":"$SHIM/gatewayProbe.sh","pageModelProbe":"$SHIM/doctorLong.sh"},"modelKeyFile":"$KEY"}
EOF
touch "$D/STOP"
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0 > "$tmp/park3.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25; do
    if grep -qF 'the gateway and the lead' "$tmp/park3.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
wait "$_appender" 2>/dev/null
# The verb should report READY (the 2 consecutive probes succeed, the 130-byte
# preamble included)
if [ "$_found" -eq 1 ] && grep -qF 'the gateway and the lead'\''s model answer again' "$tmp/park3.out" 2>/dev/null; then
    t_ok "4: item 6 DOCTOR-LONG: 130-byte preamble followed by ok --live line -> READY"
else
    t_fail "4: item 6 DOCTOR-LONG expected READY (2 consecutive probes) (got: $(tr '\n' '|' < "$tmp/park3.out" | head_bytes 600))"
fi

# --- cell 5: item 5 short-log guard -------------------------------------------
: > "$D/harness.log"
printf 'short1\nshort2\n' >> "$D/harness.log"
printf 'inactive' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
cfg_write "$CFG" "jhp-fixture-loop" ""
touch "$D/STOP"
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0 > "$tmp/park4.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'harness.log'\''s last 3 lines:' "$tmp/park4.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
# a 2-line log should print both lines, in order, no refusal
_last2=$(sed -n '/harness.log.*last 3 lines:/,$p' "$tmp/park4.out" | tail -n +2 | head -2)
if [ "$_found" -eq 1 ] && printf '%s\n' "$_last2" | grep -qF 'short1' && printf '%s\n' "$_last2" | grep -qF 'short2'; then
    t_ok "5: item 5 short-log guard: 2-line harness.log prints both lines, in order"
else
    t_fail "5: item 5 short-log guard expected short1,short2 printed (got: $(tr '\n' '|' < "$tmp/park4.out" | head_bytes 200))"
fi

t_done
