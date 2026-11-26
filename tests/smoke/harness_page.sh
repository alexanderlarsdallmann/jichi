#!/bin/sh
# smoke: `jichi-harness --config <f> page <flags>` -- the page verb smoke driver
# (M1213, batch H12; the C89 port of the 2026-10-27 hardening-host drive's
# page-watch.sh, 33 cells; the park/restart wait is re-ported from
# park-resume.sh (wait while the unit is active, then the gate decides),
# and cells 26-33 are the b22-corrected hang cells and SV:0033 refusals
# (no drive.loopUnit or drive.containerName on the gate-pass path).
# Cells 34-38 (this run): the --park arm and the would arm.
# The assertions below ARE the port's contract (notes/M1213.md, the b-row
# behaviour list), written red-first.
#
# WHAT IS CHECKED (the 40 cells, one per b-row; each cell's contract: the exact
# byte on stdout/stderr plus the rc, on the stream named by the b-row):
#   1  b6   armed line: `armed: page after 1800s of continuous failure...`
#   2  b7   AFTER-1: no page (now-first=1799 < 1800)
#   3  b7   AFTER: page fires (now-first=1800), state file written
#   4  b8   once per stretch (second run with state present: NO page)
#   5  b7   FRESH+1: no page (now-lastf=1808 > 600)
#   6  b12  the page verb does NOT consult unit state (inactive: still pages)
#   7  b13  page-watch idle with inactive unit (background loop)
#   8  b10  hook absent-form: say line only, NO refusal
#   9  b10  hook failure is logged, never fatal
#  10  b14  gateway 401 = reachable (--probe)
#  11  b14  gateway down (000)
#  12  b15  no key file
#  13  b15  empty key file
#  14  b17  redaction (canary prefix in probe output)
#  15  b18  doctor-rc non-zero = not ready
#  16  b19  --probe form: rc is the model half's
#  17  b20  park armed line (background loop)
#  18  b22  n1 != n0+1 refusal
#  19  b21+b22 loop-end success gate: full park resumes
#  20  b23  --parked armed arm
#  21  b23  --parked refusal
#  22  b24  2-consecutive reset (READY, NOT-ready, READY, READY)
#  23  b27  STOP-already-moved double-check
#  24  b28  unit-run refusal
#  25  b30  restart = park gate + restart WITHOUT ready loop
#  26  b22-corrected hang cell -- failed unit (the is-failed arm refuses)
#  27  b22-corrected hang cell -- unknown state
#  28  b22-corrected hang cell -- erroring state command
#  29  SV:0033 page without drive.loopUnit -- rc 1 + the named refusal line
#  30  SV:0033 page-watch without drive.loopUnit -- refused before arming
#  31  SV:0033 park without drive.loopUnit -- rc 1 + the named refusal line
#  32  SV:0033 restart without drive.loopUnit -- rc 1 + the named refusal line
#  33  SV:0033 containerName refusal on the gate-pass path -- restart, rc 1 + the named line, no unit armed
#  34  --park  drive dir STOP created, CWD empty, park audit lines printed
#  35  --park would arm  no STOP, would: lines printed
#  36  page-watch idles  armed+inactive, no PAGE, still running after 5s
#  37  page-verb doctor-long  130-byte preamble + ok --live = READY
#  38  probe model-alone  gateway 000 ignored, model probe alone = rc 0
#  39  b8   once per stretch at last_ok=0 (no call ever succeeded): page, then NO page
#  40  b8   the same in page-watch: one PAGE line over ~4 s of iterations
#
# RED-FIRST (2026-11-25, the operator: "Fix the page port defect for 0.15.0,
# now."): cells 39 and 40 are RED on the commit that adds them. The independent
# review of d56465e2 found that a state of "0" skipped the once-per-stretch
# comparison (jc_harness_page.c, the page verb and page-watch), so a stretch with
# last_ok=0 paged on every run; page-watch.sh:47 pages once.
#
# FIXTURE: a drive dir D with bin/ (running pair), pins/<s8> + current symlink,
# pin-build/, backups/, runs/0098/telemetry.jsonl + runs/0099/telemetry.jsonl
# (the stretch fixture bytes from test_pagestretch.c), harness.log (cells append
# the STOP lines), conf/ (jichi-config the model-probe stand-in ignores), key
# (CANARY, never a real key), and shimmable stand-ins under $tmp/shim.
. "$(dirname "$0")/_smoke.sh"

t_plan 42
smoke_home
export TZ=UTC
tmp=$(smoke_tmp)
_mk=$(smoke_make)
(cd "$SMOKE_ROOT" && $_mk jichi-harness -s) >/dev/null 2>&1
"$BIN" --version > "$tmp/binv" 2>/dev/null
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

G=/usr/bin/grep
[ -x "$G" ] || G=grep

unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

# Stand-ins (written once; token-substituted so no shell state leaks)
SHIM="$tmp/shim"
mkdir -p "$SHIM"

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

# containerGone.sh: rc is the check (like the real `podman container exists`):
# exit 0 when the flag file is absent or holds "gone" (container gone), exit 1
# while the container "exists".
printf '#!/bin/sh\n[ ! -f "%s" ] || grep -qF gone "%s" && exit 0\nexit 1\n' "$tmp/gone" "$tmp/gone" > "$SHIM/containerGone.sh"

# unitRun.sh: appends line to logfile, exit 0
printf '#!/bin/sh\necho "unit armed by stand-in" >> "%s"\nexit 0\n' "$tmp/unitrun.log" > "$SHIM/unitRun.sh"

# pidList.sh: prints fixed line
printf '#!/bin/sh\nprintf "loop pid 4242 started Mon Nov 23 07:00:00 2026; exe -> %s/jichi-harness\\n"\n' "$ROOT" > "$SHIM/pidList.sh"

# pageCommand.sh: for hook cells; absent -> exit 0; FAIL -> prints to stderr, exit 3
cat > "$SHIM/pageCommand.sh" << 'EOM'
#!/bin/sh
flag="$1"
[ ! -f "$flag" ] && exit 0
line=$(cat "$flag")
if [ "$line" = "FAIL" ]; then
    printf 'hook failed: the stand-in refuses\n' >&2
    exit 3
fi
exit 0
EOM

chmod +x "$SHIM"/*.sh 2>/dev/null

# --- the drive dir D -----------------------------------------------------------
D="$tmp/drive"
H="$D"
CFG="$D/conf/harness.json"
KEY="$D/key"
mkdir -p "$D/runs/0098" "$D/runs/0099" "$D/runs/0100" "$D/conf" "$D/pins" "$SHIM" || {
    echo "not ok - fixture mkdir failed" >&2
    t_done
}

# loopExit.sh: background appender used in park cells (F6)
# Note: $D and $tmp are expanded at write time (token-substitution)
printf '#!/bin/sh\nsleep 0.3\nprintf '"'"'STOP file present: loop ends\\nloop end\\n'"'"' >> "%s/harness.log"\nprintf '"'"'inactive'"'"' > "%s/ustate"\n' "$D" "$tmp" > "$SHIM/loopExit.sh"
chmod +x "$SHIM/loopExit.sh" 2>/dev/null

# runs/0098/telemetry.jsonl (3 lines) - from test_pagestretch.c sttest call
# Every line carries "event":"model_call" and "latency_ms":0
cat > "$D/runs/0098/telemetry.jsonl" << 'EOT'
{"event":"model_call","ts":9000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
EOT

# runs/0099/telemetry.jsonl (1 line) - last_ok=1000
cat > "$D/runs/0099/telemetry.jsonl" << 'EOT'
{"event":"model_call","ts":1000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
EOT

# runs/0100/telemetry.jsonl (2 lines) - fails at ts=1298 (503/connect depth 0) and ts=2500 (503/stall depth 2)
cat > "$D/runs/0100/telemetry.jsonl" << 'EOT'
{"event":"model_call","ts":1298.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"connect","depth":0}
{"event":"model_call","ts":2500.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"stall","depth":2}
EOT

# harness.log: start EMPTY; cells that need STOP lines write them
: > "$D/harness.log"

# pins/current: printf 'ab12cd34\n'
printf 'ab12cd34\n' > "$D/pins/current"

# conf/jichi-config.json
printf '{"lowResource":false}\n' > "$D/conf/jichi-config.json"

# KEY: the CANARY + newline, chmod 600
printf 'pageCANARY0123456789abcdef01234567\n' > "$KEY"
chmod 600 "$KEY"

# Per-cell config builder
cfg_write() {
    _cfg_ust="$1"
    _cfg_ufail="$2"
    _cfg_gwcode="$3"
    _cfg_modelline="$4"
    _cfg_hook="$5"
    # The 4th parameter is the model-line file name (without .out suffix).
    # Empty => use default $tmp/modelline; non-empty => use $tmp/${_cfg_modelline}.out.
    _cfg_modelfile="$tmp/modelline"
    if [ -n "$_cfg_modelline" ]; then
        _cfg_modelfile="$tmp/${_cfg_modelline}.out"
    fi
    _cfg_base='{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $_cfg_modelfile"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}'
    if [ -z "$_cfg_hook" ]; then
        echo "$_cfg_base" > "$CFG"
    else
        # Inject pageCommand INTO the single "host" object, not after the root
        # Use | as delimiter to avoid conflicts with the path separators
        echo "$_cfg_base" | sed "s|\"pagePidList\":\"$SHIM/pidList.sh\"|\"pagePidList\":\"$SHIM/pidList.sh\",\"pageCommand\":\"$SHIM/pageCommand.sh $tmp/hookflag\"|" > "$CFG"
    fi
}

# Helper: run a harness verb
run_harness() {
    "$HBIN" --config "$CFG" "$@" < /dev/null > "$tmp/oh" 2>&1
}

# --- 1 (b6): armed (page) ------------------------------------------------------
printf 'active' > "$tmp/ustate"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'armed: page after 1800s of continuous failure (latest failure within 600s); loop unit jdw-test-loop active' "$tmp/oh"; then
    t_ok "1: b6 armed (page) -- rc 0 + armed line on stdout"
else
    t_fail "1: b6 expected rc 0 + armed line (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 2 (b7): AFTER-1: NO page --------------------------------------------------
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3097 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && ! grep -qF 'PAGE:' "$tmp/oh" && [ ! -e "$D/page.state" ]; then
    t_ok "2: b7 AFTER-1 NO page -- rc 0, NO PAGE line, state absent"
else
    t_fail "2: b7 expected rc 0 + no PAGE + no state (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160); state: $([ -e "$D/page.state" ] && echo present || echo absent))"
fi

# --- 3 (b7): AFTER: page fires -------------------------------------------------
rm -f "$D/page.state"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'PAGE: model calls failing for 30 min since 00:21 (run 0100, 2 failed: 503/connect:1,503/stall:1); parking the drive' "$tmp/oh" && [ -e "$D/page.state" ] && [ "$(cat "$D/page.state")" = "1000" ]; then
    t_ok "3: b7 AFTER page fires -- rc 0 + PAGE line, state=1000"
else
    t_fail "3: b7 expected rc 0 + PAGE line + state=1000 (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160); state: $([ -e "$D/page.state" ] && cat "$D/page.state" || echo absent))"
fi

# --- 4 (b8): once per stretch --------------------------------------------------
# The once-per-stretch contract (green state):
#   FIRST run (fresh $D/page.state) pages -- assert the PAGE line + state=1000
#   SECOND run (state holds last_ok) does NOT re-page -- assert NO PAGE line, rc 0
rm -f "$D/page.state"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
first_rc=$?
first_paged=$(grep -cF 'PAGE: model calls failing for 30 min since 00:21 (run 0100, 2 failed: 503/connect:1,503/stall:1); parking the drive' "$tmp/oh")
state=$(cat "$D/page.state" 2>/dev/null || echo absent)
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
second_rc=$?
second_paged=$(grep -cF 'PAGE: model calls failing for 30 min since 00:21 (run 0100, 2 failed: 503/connect:1,503/stall:1); parking the drive' "$tmp/oh")
# Contract: first run pages (PAGE line, state=1000), second does not (no PAGE line, rc 0)
if [ "$first_rc" -eq 0 ] && [ "$first_paged" -eq 1 ] && [ "$state" = "1000" ] && [ "$second_rc" -eq 0 ] && [ "$second_paged" -eq 0 ]; then
    t_ok "4: b8 once per stretch -- first run pages (PAGE line, state=1000); second run: no re-page, state holds last_ok"
else
    t_fail "4: b8 expected green-state contract (first: PAGE+state=1000; second: no PAGE, rc 0) got first_rc=$first_rc first_paged=$first_paged state=$state second_rc=$second_rc second_paged=$second_paged"
fi

# --- 5 (b7): FRESH+1: NO page --------------------------------------------------
rm -f "$D/page.state"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && ! grep -qF 'PAGE:' "$tmp/oh" && [ ! -e "$D/page.state" ]; then
    t_ok "5: b7 FRESH+1 NO page -- rc 0, NO PAGE line, state absent"
else
    t_fail "5: b7 expected rc 0 + no PAGE + no state (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160); state: $([ -e "$D/page.state" ] && echo present || echo absent))"
fi

# --- 6 (b12): unit state ignored by page verb ----------------------------------
printf 'inactive' > "$tmp/ustate"
rm -f "$D/page.state"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'PAGE: model calls failing for 30 min since 00:21 (run 0100, 2 failed: 503/connect:1,503/stall:1); parking the drive' "$tmp/oh"; then
    t_ok "6: b12 page ignores unit state -- rc 0 + PAGE line (unit inactive)"
else
    t_fail "6: b12 expected rc 0 + PAGE line with inactive unit (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 7 (b13): page-watch idle with inactive unit -------------------------------
printf 'inactive' > "$tmp/ustate"
rm -f "$tmp/pw.out"
cfg_write "" "" "" "" ""
( exec "$HBIN" --config "$CFG" page-watch --interval 1 --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 1800 --fresh-s 600 > "$tmp/pw.out" 2>&1 ) &
pwpid=$!
i=0
while [ $i -lt 50 ] && ! grep -qF 'armed:' "$tmp/pw.out" 2>/dev/null; do
    sleep 0.1
    i=$((i + 1))
done
kill "$pwpid" 2>/dev/null
wait "$pwpid"
if grep -qF 'armed: page after 1800s of continuous failure (latest failure within 600s); loop unit jdw-test-loop inactive' "$tmp/pw.out" && ! grep -qF 'PAGE:' "$tmp/pw.out"; then
    t_ok "7: b13 page-watch idle -- armed line with inactive, NO PAGE line"
else
    t_fail "7: b13 expected armed+inactive, no PAGE (got: $(tr '\n' '|' < "$tmp/pw.out" | head_bytes 160))"
fi

# --- 8 (b10): hook absent-form -------------------------------------------------
rm -f "$D/page.state"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'PAGE:' "$tmp/oh" && ! grep -qF 'hook' "$tmp/oh" && ! grep -qF 'hook' "$tmp/eh"; then
    t_ok "8: b10 hook absent-form -- PAGE line, NO hook refusal"
else
    t_fail "8: b10 expected PAGE + no hook (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 9 (b10): hook failure logged, never fatal ---------------------------------
rm -f "$D/page.state"
printf 'FAIL' > "$tmp/hookflag"
cfg_write "" "" "" "" "hook"
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3098 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'PAGE:' "$tmp/oh" && { grep -qF 'hook failed' "$tmp/oh" || grep -qF 'hook failed' "$tmp/eh"; }; then
    t_ok "9: b10 hook failure logged -- PAGE line, rc 0, hook failed logged"
else
    t_fail "9: b10 expected PAGE + rc 0 + hook failed (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi
rm -f "$tmp/hookflag"

# --- 10 (b14): gateway 401 = reachable ----------------------------------------
printf '401' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'ready' "$tmp/oh"; then
    t_ok "10: b14 gateway 401 reachable -- rc 0, ready on stdout"
else
    t_fail "10: b14 expected rc 0 + ready (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 11 (b14): gateway down ----------------------------------------------------
printf '000' > "$tmp/gwcode"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 1 ] && ! grep -qF 'ready' "$tmp/oh"; then
    t_ok "11: b14 gateway down -- rc 1, NO ready"
else
    t_fail "11: b14 expected rc 1 + no ready (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 12 (b15): no key file -----------------------------------------------------
rm -f "$KEY"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'no readable key file' "$tmp/oh"; then
    t_ok "12: b15 no key file -- rc 1, no readable key file"
else
    t_fail "12: b15 expected rc 1 + no readable key file (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 13 (b15): empty key file --------------------------------------------------
: > "$KEY"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'the key file is empty' "$tmp/oh"; then
    t_ok "13: b15 empty key file -- rc 1, the key file is empty"
else
    t_fail "13: b15 expected rc 1 + key file empty (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# Restore key
printf 'pageCANARY0123456789abcdef01234567\n' > "$KEY"

# --- 14 (b17): redaction -------------------------------------------------------
printf 'ok --live: tool calling observed key=pageCANARY01 and more' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 1 ] && grep -qF "(a --live line, redacted: it held the key's prefix)" "$tmp/oh"; then
    # CENSUS: neither pageCANARY01 nor full canary in output files; E1: a redacted
    # line cannot be ready, so NO ready line may appear either.
    if ! grep -qF 'pageCANARY01' "$tmp/oh" "$tmp/eh" 2>/dev/null && ! grep -qF 'pageCANARY0123456789abcdef01234567' "$tmp/oh" "$tmp/eh" 2>/dev/null && ! grep -qF 'ready' "$tmp/oh"; then
        t_ok "14: b17 redaction -- rc 1, (redacted) in stdout, no canary leak, no ready line"
    else
        t_fail "14: b17 expected no canary + no ready line (got: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
    fi
else
    t_fail "14: b17 expected rc 1 + (redacted) (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 15 (b18): doctor-rc non-zero = not ready ----------------------------------
printf 'RC1' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'doctor rc=1' "$tmp/oh"; then
    t_ok "15: b18 doctor-rc non-zero -- rc 1, doctor rc=1"
else
    t_fail "15: b18 expected rc 1 + doctor rc=1 (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 16 (b19): --probe form: rc is model half's --------------------------------
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'ready' "$tmp/oh"; then
    t_ok "16: b19 --probe rc is model half's -- rc 0, ready"
else
    t_fail "16: b19 expected rc 0 + ready (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 17 (b20): park armed line -------------------------------------------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
cfg_write "" "" "" "" ""
rm -f "$tmp/park.out"
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 > "$tmp/park.out" 2>&1 ) &
parkpid=$!
i=0
while [ $i -lt 50 ] && ! grep -qF 'armed:' "$tmp/park.out" 2>/dev/null; do
    sleep 0.1
    i=$((i + 1))
done
kill "$parkpid" 2>/dev/null
wait "$parkpid"
if grep -qF 'armed: unit jdw-test-loop active; STOP-ended lines so far: 1; waiting for the loop to end' "$tmp/park.out"; then
    t_ok "17: b20 park armed -- armed line with active unit, 1 STOP line"
else
    t_fail "17: b20 expected armed line (got: $(tr '\n' '|' < "$tmp/park.out" | head_bytes 160))"
fi

# --- 18 (b22): n1 != n0+1 refusal ----------------------------------------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'inactive' > "$tmp/ustate"
cfg_write "" "" "" "" ""
run_harness park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1
RC=$?
if [ "$RC" -eq 1 ]; then
    combined=$(cat "$tmp/oh" "$tmp/eh" 2>/dev/null | tr '\n' '|')
    if [ "$(printf '%s' "$combined" | $G -cF 'NOT resuming')" -eq 1 ] && [ "$(printf '%s' "$combined" | $G -cF 'STOP-ended lines now: 1')" -eq 1 ] && [ "$(printf '%s' "$combined" | $G -cF 'harness.log')" -eq 1 ]; then
        t_ok "18: b22 n1 != n0+1 refusal -- rc 1, NOT resuming + lines info"
    else
        t_fail "18: b22 expected rc 1 + NOT resuming + lines (got: $(printf '%s' "$combined" | head_bytes 160))"
    fi
else
    t_fail "18: b22 expected rc 1 (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 19 (b21+b22): loop-end success gate ---------------------------------------
# Use the GATE-PASS SNIPPET: pre-seed harness.log with 1 STOP-ended line + loop end
# ustate=active, ufailed=notfailed, a background appender that after 0.3s adds the 2nd STOP line + loop end and sets ustate inactive
: > "$D/harness.log"
: > "$tmp/unitrun.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
touch "$D/STOP"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness park --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
if [ "$_parkrc" -eq 0 ] && grep -qF 'STOP moved to' "$tmp/oh" && grep -qF 'unit armed by stand-in' "$tmp/unitrun.log" && grep -qF 'loop pid 4242 started' "$tmp/oh"; then
    t_ok "19: b21+b22 loop-end success -- STOP moved, unit armed, pid line"
else
    t_fail "19: b21+b22 expected STOP moved + unit armed (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 20 (b23): --parked armed arm ----------------------------------------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'inactive' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
touch "$D/STOP"
cfg_write "" "" "" "" ""
run_harness park --parked
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'armed (--parked): unit jdw-test-loop inactive and not failed, STOP present, harness.log ends with the STOP lines' "$tmp/oh"; then
    t_ok "20: b23 --parked armed -- rc 0, armed (--parked) line"
else
    t_fail "20: b23 expected rc 0 + armed (--parked) (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 21 (b23): --parked refusal ------------------------------------------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
touch "$D/STOP"
cfg_write "" "" "" "" ""
run_harness park --parked
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'NOT resuming (--parked): the loop is not parked on STOP (unit active); harness.log ends:' "$tmp/oh"; then
    t_ok "21: b23 --parked refusal -- rc 1, NOT resuming (--parked) line"
else
    t_fail "21: b23 expected rc 1 + NOT resuming (--parked) (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 22 (b24): 2-consecutive reset ---------------------------------------------
# Use the GATE-PASS SNIPPET (its own harness.log + appender, so the gate passes)
: > "$D/harness.log"
: > "$tmp/unitrun.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
rm -f "$tmp/mprobe_count"
printf '0' > "$tmp/mprobe_count"
TMPMPC="$tmp/mprobe_count"
printf '#!/bin/sh\nflag="$1"\ncount=$(cat "%s" 2>/dev/null || echo 0)\ncase $count in\n    1) printf '"'"'warn --live: no healthy upstream\\n'"'"' ;;\n    *) printf '"'"'ok --live: tool calling observed\\n'"'"' ;;\nesac\necho $((count + 1)) > "%s"\nexit 0\n' "$TMPMPC" "$TMPMPC" > "$tmp/modelProbe2.sh"
chmod +x "$tmp/modelProbe2.sh"
# Write complete config inline (F5: no sed -i)
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$tmp/modelProbe2.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}' > "$CFG"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness park --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
if [ "$_parkrc" -eq 0 ] && [ -f "$tmp/mprobe_count" ] && [ "$(cat "$tmp/mprobe_count" 2>/dev/null)" = "4" ] && grep -qF 'unit armed by stand-in' "$tmp/unitrun.log"; then
    t_ok "22: b24 2-consecutive reset -- 4 probes (READY, NOT-ready, READY, READY), unit armed"
else
    t_fail "22: b24 expected 4 probes + unit armed (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160); count: $(cat "$tmp/mprobe_count" 2>/dev/null || echo absent))"
fi

# --- 23 (b27): STOP-already-moved double-check ---------------------------------
# Use the GATE-PASS SNIPPET (gate passes), gwcode=200, model ok, container gone
# Create RO host dir to make STOP move fail deterministically
RO="$D/rohost"
mkdir -p "$RO"
chmod 555 "$RO"
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
touch "$D/STOP"
cat > "$tmp/unitRun4.sh" << 'EOR'
#!/bin/sh
echo "unit armed by stand-in" >> "$tmp/unitrun.log"
sleep 1
touch "$D/STOP"
exit 0
EOR
chmod +x "$tmp/unitRun4.sh"
# cfg_write variant that sets host.dir to RO
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$RO"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$tmp/unitRun4.sh"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}' > "$CFG"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness park --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
# D6: DROP the stale [ "$RC" -eq 1 ] check, keep only grep + park's own rc
if grep -qF 'STOP is still there' "$tmp/oh" && [ "$_parkrc" -eq 1 ]; then
    t_ok "23: b27 STOP-already-moved -- rc 1, STOP is still there"
else
    t_fail "23: b27 expected rc 1 + STOP is still there (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi
# Reset RO dir perms (or leave it; tmp discarded)
# chmod 755 "$RO" 2>/dev/null

# --- 24 (b28): unit-run refusal ------------------------------------------------
# Use the GATE-PASS SNIPPET (gate passes), gwcode=200, model ok, container gone
# host.dir writable (drive==host) so STOP move SUCCEEDS
# unitRun stand-in exits 1
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
touch "$D/STOP"
cat > "$tmp/unitRun5.sh" << 'EOR'
#!/bin/sh
echo "unit armed by stand-in" >> "$tmp/unitrun.log"
exit 1
EOR
chmod +x "$tmp/unitRun5.sh"
# Restore drive==host
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$tmp/unitRun5.sh"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}' > "$CFG"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness park --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
# D6: DROP the stale [ "$RC" -eq 1 ] check, keep only grep + park's own rc
if grep -qF 'the unit start failed (rc=1)' "$tmp/oh" && [ "$_parkrc" -eq 1 ]; then
    t_ok "24: b28 unit-run refusal -- rc 1, the unit start failed (rc=1)"
else
    t_fail "24: b28 expected rc 1 + unit start failed (rc=1) (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 25 (b30): restart = park gate + restart WITHOUT ready loop ----------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
touch "$D/STOP"
rm -f "$tmp/mprobe_count"
: > "$tmp/unitrun.log"
# Restore the WORKING SHIM unitRun into pageUnitRun (cell 24 left it exit-1)
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}' > "$CFG"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness restart --wait-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
if [ "$_parkrc" -eq 0 ] && grep -qF 'unit armed by stand-in' "$tmp/unitrun.log" && [ ! -f "$tmp/mprobe_count" ]; then
    t_ok "25: b30 restart = park gate + restart WITHOUT ready loop -- rc 0, unit armed, no probes"
else
    t_fail "25: b30 expected rc 0 + unit armed + no probes (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 26 (b22-corrected): hang cell -- failed unit ------------------------------
# The re-ported wait (park-resume.sh:77) ends on ANY non-active state, then the
# gate refuses. This cell isolates the GATE's is-failed arm: the loop really
# ended for the STOP file (the appender makes n1 == n0+1, last line "loop end"),
# the unit is inactive and NOT failed-by-state -- only the is-failed command
# says failed. If the is-failed arm were dead (finding 1) park would resume.
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'failed' > "$tmp/ufailed"
cfg_write "" "" "" "" ""
touch "$D/STOP"
# the loop "ends" 0.3s in: append the 2nd STOP-ended line + loop end (n1 = n0+1)
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 --ready-interval 1 --not-ready-interval 1 --post-start 0 > "$tmp/park.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'NOT resuming' "$tmp/park.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
wait "$_appender" 2>/dev/null
if [ "$_found" -eq 1 ] && grep -qF 'The supervisor decides' "$tmp/park.out" && ! grep -qF 'STOP moved to' "$tmp/park.out"; then
    t_ok "26: b22-corrected failed unit -- the loop ended for STOP but the unit is failed, so the is-failed arm refuses (no STOP move)"
else
    t_fail "26: failed unit expected the is-failed refusal within 10 s, no resume (got: $(tr '\n' '|' < "$tmp/park.out" | head_bytes 160))"
fi

# --- 27 (b22-corrected): hang cell -- unknown state ----------------------------
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'unknown' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
cfg_write "" "" "" "" ""
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 > "$tmp/park.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'NOT resuming' "$tmp/park.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
if [ "$_found" -eq 1 ] && grep -qF 'The supervisor decides' "$tmp/park.out"; then
    t_ok "27: b22-corrected unknown state -- refusal within one interval, NOT resuming + supervisor decides"
else
    t_fail "27: unknown state expected NOT resuming + supervisor within 10 s (got: $(tr '\n' '|' < "$tmp/park.out" | head_bytes 160))"
fi

# --- 28 (b22-corrected): hang cell -- erroring state command -------------------
# A state command that errors (exit 2, prints nothing) is a non-active state:
# the wait ends, the gate refuses. It must not wait.
cat > "$tmp/unitStateErr.sh" << 'EOS'
#!/bin/sh
exit 2
EOS
chmod +x "$tmp/unitStateErr.sh"
: > "$D/harness.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'unknown' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$tmp/unitStateErr.sh"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b","containerName":"jhd-hardening"}}' > "$CFG"
( exec "$HBIN" --config "$CFG" park --interval 1 --wait-interval 1 > "$tmp/park.out" 2>&1 ) &
_parkpid=$!
_found=0
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if grep -qF 'NOT resuming' "$tmp/park.out" 2>/dev/null; then _found=1; break; fi
    sleep 1
done
kill "$_parkpid" 2>/dev/null
wait "$_parkpid" 2>/dev/null
if [ "$_found" -eq 1 ] && grep -qF 'The supervisor decides' "$tmp/park.out"; then
    t_ok "28: b22-corrected erroring state command -- refusal within one interval, NOT resuming + supervisor decides"
else
    t_fail "28: erroring state command expected NOT resuming + supervisor within 10 s (got: $(tr '\n' '|' < "$tmp/park.out" | head_bytes 160))"
fi

# --- 29-32 (SV:0033): the config names no drive.loopUnit / containerName -------
# cfg_write_nounit: cell 1's config with loopUnit and containerName deleted.
cfg_write_nounit() {
    printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageContainerGone":"'"$SHIM/containerGone.sh $tmp/gone"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models"},"drive":{"leadModel":"qwen27b"}}' > "$CFG"
}

# --- 29 (SV:0033): page refuses without drive.loopUnit -------------------------
printf 'active' > "$tmp/ustate"
cfg_write_nounit
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 1800 --fresh-s 600
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'the config names no drive.loopUnit' "$tmp/oh"; then
    t_ok "29: SV:0033 page without drive.loopUnit -- rc 1 + the named refusal line"
else
    t_fail "29: page expected rc 1 + no drive.loopUnit (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 30 (SV:0033): page-watch refuses before arming ----------------------------
printf 'active' > "$tmp/ustate"
cfg_write_nounit
( exec "$HBIN" --config "$CFG" page-watch --interval 1 --state "$D/page.state" --out "$D/page.out" > "$D/pw.out" 2>&1 ) &
_pwpid=$!
for _i in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "$_pwpid" 2>/dev/null; then break; fi
    sleep 1
done
if kill -0 "$_pwpid" 2>/dev/null; then
    kill "$_pwpid" 2>/dev/null
fi
wait "$_pwpid" 2>/dev/null
if grep -qF 'the config names no drive.loopUnit' "$D/pw.out" && ! grep -qF 'armed' "$D/pw.out"; then
    t_ok "30: SV:0033 page-watch without drive.loopUnit -- refused before arming"
else
    t_fail "30: page-watch expected refusal before arming (got: $(tr '\n' '|' < "$D/pw.out" | head_bytes 160))"
fi

# --- 31 (SV:0033): park refuses without drive.loopUnit -------------------------
cfg_write_nounit
run_harness park --interval 1
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'the config names no drive.loopUnit' "$tmp/oh"; then
    t_ok "31: SV:0033 park without drive.loopUnit -- rc 1 + the named refusal line"
else
    t_fail "31: park expected rc 1 + no drive.loopUnit (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 32 (SV:0033): restart refuses without drive.loopUnit ----------------------
cfg_write_nounit
run_harness restart --wait-interval 1
RC=$?
if [ "$RC" -eq 1 ] && grep -qF 'the config names no drive.loopUnit' "$tmp/oh"; then
    t_ok "32: SV:0033 restart without drive.loopUnit -- rc 1 + the named refusal line"
else
    t_fail "32: restart expected rc 1 + no drive.loopUnit (got rc=$RC: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160))"
fi

# --- 33 (SV:0033): containerName refusal on the gate-pass path ------------------
# Use the GATE-PASS SNIPPET (its own harness.log + appender), gwcode=200, model ok, container gone
# The config names NEITHER host.pageContainerGone NOR drive.containerName
# So the restart gate passes but refuses on the containerName check BEFORE starting the unit
: > "$D/harness.log"
: > "$tmp/unitrun.log"
printf 'STOP file present: loop ends\nloop end\n' >> "$D/harness.log"
printf 'active' > "$tmp/ustate"
printf 'notfailed' > "$tmp/ufailed"
printf '200' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
printf 'gone' > "$tmp/gone"
touch "$D/STOP"
# Inline config: copy cell 25's config, but delete host.pageContainerGone and drive.containerName
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitState":"'"$SHIM/unitState.sh $tmp/ustate"'","pageUnitFailed":"'"$SHIM/unitFailed.sh $tmp/ufailed"'","pageGatewayProbe":"'"$SHIM/gatewayProbe.sh $tmp/gwcode"'","pageModelProbe":"'"$SHIM/modelProbe.sh $tmp/modelline"'","pageUnitRun":"'"$SHIM/unitRun.sh $tmp/unitrun.log"'","pagePidList":"'"$SHIM/pidList.sh"'","pageGateway":"https://api.hrz.uni-giessen.de/v1/models","containerName":"jhd-hardening"},"drive":{"loopUnit":"jdw-test-loop","leadModel":"qwen27b"}}' > "$CFG"
# the loop "exits" 0.3s in: append the 2nd STOP-ended line + loop end, go inactive
( exec "$SHIM/loopExit.sh" ) &
_appender=$!
run_harness restart --wait-interval 1 --post-start 0
_parkrc=$?
wait "$_appender" 2>/dev/null
# Assert: rc 1 AND the named line in $tmp/oh AND NO 'unit armed by stand-in' in $tmp/unitrun.log
if [ "$_parkrc" -eq 1 ] && grep -qF 'the config names no drive.containerName' "$tmp/oh" && ! grep -qF 'unit armed by stand-in' "$tmp/unitrun.log"; then
    t_ok "33: SV:0033 containerName refusal on the gate-pass path -- restart, rc 1 + the named line, no unit armed"
else
    t_fail "33: SV:0033 containerName refusal expected rc 1 + the named line + no unit armed (got rc=$_parkrc: $(tr '\n' '|' < "$tmp/oh" | head_bytes 160); unitrun: $(cat "$tmp/unitrun.log" 2>/dev/null || echo absent))"
fi

# --- 34 (item 3 --park): drive dir + different CWD; run page verb with --park (bounded idiom)
# Assert <drive>/STOP exists AND no STOP in CWD. Pin the park arm's two lines.
# Fix: use --fresh-s 700 (not 600) so now-lastf=658 <= 700 and the page condition fires.
rm -f "$D/STOP" "$tmp/STOP" "$D/page.state" 2>/dev/null || true
mkdir -p "$D"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
# Run the page verb with --park in the driver's temp dir (CWD)
# This should create <drive>/STOP, not $tmp/STOP
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 60 --fresh-s 700 --park
RC=$?
if [ "$RC" -eq 0 ] && [ -f "$D/STOP" ] && [ ! -f "$tmp/STOP" ]; then
    # Check the park lines are printed (they should be the park command and touch)
    if grep -qF 'park --interval 60' "$tmp/oh" && grep -qF 'touch "' "$tmp/oh"; then
        t_ok "34: item 3 --park -- drive dir STOP created, CWD empty, park lines printed"
    else
        t_fail "34: item 3 expected STOP in drive, CWD empty, and park lines (got rc=$RC: $(tr '\n' '|' < \"$tmp/oh\" | head_bytes 160))"
    fi
else
    t_fail "34: item 3 expected STOP in drive, CWD empty (got rc=$RC: $(tr '\n' '|' < \"$tmp/oh\" | head_bytes 160); drive STOP: $(test -f \"$D/STOP\" && echo present || echo absent); tmp STOP: $(test -f \"$tmp/STOP\" && echo present || echo absent))"
fi

# --- 35 (item 4 --park would arm, no --park): same as 34 but without --park
# Assert the same park audit lines but NO STOP created.
rm -f "$D/STOP" "$tmp/STOP" "$D/page.state" 2>/dev/null || true
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness page --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 60 --fresh-s 700
RC=$?
if [ "$RC" -eq 0 ] && [ ! -f "$D/STOP" ] && [ ! -f "$tmp/STOP" ]; then
    # Check the would: lines are printed (they should be the park command and would: touch)
    if grep -qF 'park --interval 60' "$tmp/oh" && grep -qF 'would: touch "' "$tmp/oh"; then
        t_ok "35: item 4 --park would arm -- no STOP created, would: lines printed"
    else
        t_fail "35: item 4 expected no STOP, would: lines (got rc=$RC: $(tr '\n' '|' < \"$tmp/oh\" | head_bytes 160))"
    fi
else
    t_fail "35: item 4 expected no STOP (got rc=$RC: $(tr '\n' '|' < \"$tmp/oh\" | head_bytes 160); drive STOP: $(test -f \"$D/STOP\" && echo present || echo absent); tmp STOP: $(test -f \"$tmp/STOP\" && echo present || echo absent))"
fi

# --- 36 (item 8 page-watch idles): unitState stand-in that answers inactive FOREVER
# page-watch must print its armed line and KEEP RUNNING (bounded by driver: poll ~5s, then kill)
# Assert process was alive after poll and printed no PAGE and did not exit on its own
printf 'inactive' > "$tmp/ustate"
rm -f "$tmp/pw35.out"
cfg_write "" "" "" "" ""
( exec "$HBIN" --config "$CFG" page-watch --interval 1 --state "$D/page.state" --out "$D/page.out" --now 3158 --after-s 1800 --fresh-s 600 > "$tmp/pw35.out" 2>&1 ) &
pwpid=$!
# Poll for 5 seconds, checking if process is still alive
_i=0
while [ $_i -lt 50 ] && kill -0 "$pwpid" 2>/dev/null; do
    sleep 0.1
    _i=$((_i + 1))
done
# Process should still be alive after 5 seconds
if kill -0 "$pwpid" 2>/dev/null; then
    kill "$pwpid" 2>/dev/null
    wait "$pwpid" 2>/dev/null
    # Assert: armed line present, no PAGE line
    if grep -qF 'armed: page after 1800s of continuous failure' "$tmp/pw35.out" && ! grep -qF 'PAGE:' "$tmp/pw35.out"; then
        t_ok "36: item 8 page-watch idles -- armed line, no PAGE, still running after 5s"
    else
        t_fail "36: item 8 expected armed+no PAGE, still alive (got: $(tr '\\n' '|' < \"$tmp/pw35.out\" | head_bytes 160))"
    fi
else
    wait "$pwpid" 2>/dev/null
    t_fail "36: item 8 expected process alive after 5s, got dead (got: $(tr '\\n' '|' < \"$tmp/pw35.out\" | head_bytes 160))"
fi

# --- 36 (item 6 page-verb doctor-long): stand-in doctor whose output is 130-byte junk THEN ok --live
# The page verb must report it READY (today: 121-byte capture misses it)
: > "$tmp/doctor36.out"
# 130 bytes of junk + the --live line
_junk="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
printf '%s\nok --live: tool calling observed\n' "$_junk" > "$tmp/doctor36.out"
cfg_write "" "" "" "doctor36" ""
run_harness probe --model qwen27b --probe
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'ready' "$tmp/oh"; then
    t_ok "37: item 6 page-verb doctor-long -- 130-byte preamble + ok --live = READY"
else
    t_fail "37: item 6 expected READY with 130-byte preamble (got rc=$RC: $(tr '\\n' '|' < \"$tmp/oh\" | head_bytes 160))"
fi

# --- 37 (item 7 probe model-alone): config with gateway probe -> 000 and model probe -> ok --live
# probe --model-probe must be rc 0 reporting the model line (today: returns 1 on gateway 000 first)
printf '000' > "$tmp/gwcode"
printf 'ok --live: tool calling observed' > "$tmp/modelline"
cfg_write "" "" "" "" ""
run_harness probe --model qwen27b --model-probe
RC=$?
if [ "$RC" -eq 0 ] && grep -qF 'doctor rc=0: ok --live: tool calling observed' "$tmp/oh"; then
    t_ok "38: item 7 probe model-alone -- gateway 000 ignored, model probe alone = rc 0"
else
    t_fail "38: item 7 expected rc 0 + model output with gateway 000 (got rc=$RC: $(tr '\\n' '|' < \"$tmp/oh\" | head_bytes 160))"
fi

# --- 39, 40 (b8 at last_ok=0): once per stretch when NO call ever succeeded -----
# The 2026-11-25 independent review of d56465e2 (M1213 WRONG): a stretch with no
# successful model call has last_ok=0; the first page writes "0" to the state,
# and the port skipped its comparison whenever the state held "0", so it paged on
# EVERY run of that stretch. page-watch.sh:47 compares the state with last_ok
# and pages once. A second drive dir D0 with one run of two failed calls and no
# ok call anywhere (the cell-3 failures: ts 1298 connect, 2500 stall), and a copy
# of the config whose root "dir" names D0 (host.dir stays D).
D0="$tmp/drive0"
mkdir -p "$D0/runs/0101"
cat > "$D0/runs/0101/telemetry.jsonl" << 'EOT'
{"event":"model_call","ts":1298.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"connect","depth":0}
{"event":"model_call","ts":2500.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"stall","depth":2}
EOT
printf 'active' > "$tmp/ustate"
cfg_write "" "" "" "" ""
CFG0="$tmp/harness0.json"
sed 's|"dir":"'"$D"'"|"dir":"'"$D0"'"|' "$CFG" > "$CFG0"

# 39: the page verb, twice on that stretch: one PAGE line in all.
rm -f "$D0/page.state"
"$HBIN" --config "$CFG0" page --state "$D0/page.state" --out "$D0/page.out" --now 3098 --after-s 1800 --fresh-s 600 \
    < /dev/null > "$tmp/oh39a" 2>&1; rc39a=$?
st39=$(cat "$D0/page.state" 2>/dev/null || echo absent)
"$HBIN" --config "$CFG0" page --state "$D0/page.state" --out "$D0/page.out" --now 3098 --after-s 1800 --fresh-s 600 \
    < /dev/null > "$tmp/oh39b" 2>&1; rc39b=$?
p39a=$($G -c 'PAGE:' "$tmp/oh39a"); p39b=$($G -c 'PAGE:' "$tmp/oh39b")
if [ "$rc39a" -eq 0 ] && [ "$p39a" -eq 1 ] && [ "$st39" = "0" ] && [ "$rc39b" -eq 0 ] && [ "$p39b" -eq 0 ]; then
    t_ok "39: b8 at last_ok=0 -- the first page writes state 0, the second run of the same stretch does not re-page"
else
    t_fail "39: b8 at last_ok=0 expected one PAGE then none (got first rc=$rc39a pages=$p39a state=$st39; second rc=$rc39b \
pages=$p39b: $(tr '\n' '|' < "$tmp/oh39b" | head_bytes 160))"
fi

# 40: page-watch on that stretch for ~4 s at --interval 1 (several iterations):
# one PAGE line in all. Its once-per-stretch check is a second site in the code.
rm -f "$D0/page.state" "$tmp/pw40.out"
( exec "$HBIN" --config "$CFG0" page-watch --interval 1 --state "$D0/page.state" --out "$D0/page.out" --now 3098 \
    --after-s 1800 --fresh-s 600 > "$tmp/pw40.out" 2>&1 ) &
pw40=$!
_i=0
while [ $_i -lt 40 ] && kill -0 "$pw40" 2>/dev/null; do
    sleep 0.1
    _i=$((_i + 1))
done
kill "$pw40" 2>/dev/null
wait "$pw40" 2>/dev/null
p40=$($G -c 'PAGE:' "$tmp/pw40.out")
if [ "$p40" -eq 1 ]; then
    t_ok "40: b8 at last_ok=0 in page-watch -- one PAGE line over ~4 s of iterations, not one per iteration"
else
    t_fail "40: b8 at last_ok=0 in page-watch expected exactly 1 PAGE line over ~4 s, got $p40: \
$(tr '\n' '|' < "$tmp/pw40.out" | head_bytes 200)"
fi

# --- 41, 42 (the clock): without --now, page and page-watch judge at the real time
# The 0.15.0 release audit (2026-11-25): --now defaulted to 0 and nothing read the
# clock in its place, so `now - first` was negative, the AFTER test never passed,
# and neither verb paged unless the caller passed --now -- which every cell above
# does, and which a unit running the verb unattended does not. A drive dir D1 whose
# one run holds two failed calls stamped from the real clock: the stretch opened
# 1900 s ago and its last failure was 60 s ago (AFTER 1800, FRESH 600 => page).
# D2 is the same stretch opened only 60 s ago (=> no page), so a fix that pages
# always is red too.
_t=$(date +%s)
D1="$tmp/drive1"; D2="$tmp/drive2"
mkdir -p "$D1/runs/0101" "$D2/runs/0101"
printf '{"event":"model_call","ts":%s.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"connect","depth":0}\n{"event":"model_call","ts":%s.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"stall","depth":2}\n' \
    "$((_t - 1900))" "$((_t - 60))" > "$D1/runs/0101/telemetry.jsonl"
printf '{"event":"model_call","ts":%s.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"connect","depth":0}\n{"event":"model_call","ts":%s.0,"latency_ms":0,"ok":false,"status":503,"fail_class":"stall","depth":2}\n' \
    "$((_t - 60))" "$((_t - 30))" > "$D2/runs/0101/telemetry.jsonl"
CFG1="$tmp/harness1.json"; CFG2="$tmp/harness2.json"
sed 's|"dir":"'"$D"'"|"dir":"'"$D1"'"|' "$CFG" > "$CFG1"
sed 's|"dir":"'"$D"'"|"dir":"'"$D2"'"|' "$CFG" > "$CFG2"

# 41: the page verb without --now: the old stretch pages once, the young one does not.
rm -f "$D1/page.state" "$D2/page.state"
"$HBIN" --config "$CFG1" page --state "$D1/page.state" --out "$D1/page.out" --after-s 1800 --fresh-s 600 \
    < /dev/null > "$tmp/oh41a" 2>&1; rc41a=$?
"$HBIN" --config "$CFG2" page --state "$D2/page.state" --out "$D2/page.out" --after-s 1800 --fresh-s 600 \
    < /dev/null > "$tmp/oh41b" 2>&1; rc41b=$?
p41a=$($G -c 'PAGE:' "$tmp/oh41a"); p41b=$($G -c 'PAGE:' "$tmp/oh41b")
if [ "$rc41a" -eq 0 ] && [ "$p41a" -eq 1 ] && [ "$rc41b" -eq 0 ] && [ "$p41b" -eq 0 ]; then
    t_ok "41: page without --now judges at the real time -- a stretch open 1900 s pages once, one open 60 s does not"
else
    t_fail "41: page without --now expected one PAGE for the 1900 s stretch and none for the 60 s one (got rc=$rc41a pages=$p41a; \
rc=$rc41b pages=$p41b: $(tr '\n' '|' < "$tmp/oh41a" | head_bytes 160))"
fi

# 42: page-watch without --now on the old stretch for ~3 s at --interval 1: one PAGE.
rm -f "$D1/page.state" "$tmp/pw42.out"
( exec "$HBIN" --config "$CFG1" page-watch --interval 1 --state "$D1/page.state" --out "$D1/page.out" \
    --after-s 1800 --fresh-s 600 > "$tmp/pw42.out" 2>&1 ) &
pw42=$!
_i=0
while [ $_i -lt 30 ] && kill -0 "$pw42" 2>/dev/null; do
    sleep 0.1
    _i=$((_i + 1))
done
kill "$pw42" 2>/dev/null
wait "$pw42" 2>/dev/null
p42=$($G -c 'PAGE:' "$tmp/pw42.out")
if [ "$p42" -eq 1 ]; then
    t_ok "42: page-watch without --now judges at the real time -- one PAGE line over ~3 s on a stretch open 1900 s"
else
    t_fail "42: page-watch without --now expected exactly 1 PAGE line over ~3 s, got $p42: \
$(tr '\n' '|' < "$tmp/pw42.out" | head_bytes 200)"
fi

t_done
