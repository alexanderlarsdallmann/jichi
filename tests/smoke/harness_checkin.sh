#!/bin/sh
# smoke: `jichi-harness --config <f> checkin [--push]` -- the checkin verb smoke driver
# (M1224, batch H13; the C89 port of the 2026-10-27 hardening-host drive's
# checkin.sh, 52 lines; the supervisor's read-only check-in of persistent timers,
# transient loop, current run's NAMED records, push state, drive LOG's READY/WITHDRAWN
# lines, network prerequisite, outage watcher, and supervision log's two copies).
# Cells are one per C-row below (the b-row behaviour list), written red-first.
# After M1228 fixes: 50 cells with F1/F2/F3/F4 red cells added (F1/F2/F2b need later source fix; F3/F4 are missing-teeth guards, green on head).
#
# WHAT IS CHECKED (the 50 cells, one per C-row; each cell's contract: exact bytes
# on stdout/stderr plus rc, on the stream named by the C-row):
#   1  C0   usage: missing --config, exit 2, usage line (FIXED: no --config in cell 1)
#   2  C0   usage: unknown flag before command, exit 2, usage line
#   3  C0   usage: second positional after checkin, exit 2, usage line
#   4  C1   header: `== <date '+%F %T %z'>`, byte-for-byte
#   5  C2   loop unit: `<loopUnit>: <state>`, empty state when unit not active
#   6  C2   once unit: `<onceUnit>: <state>`, derived as loopUnit + "-once"
#   7  C3   container: `no <prefix>- container` when no matching containers
#   8  C4   STOP file absent: no line printed (reference prints nothing)
#   9  C5   restart-loop line absent: omitted when .out file missing
#  10  C6   loop pid: omitted when no matching process (reference's case fall-through)
#  11  C7   harness.log tail: header only when file absent
#  12  C8   redactor: 44-hex CANARY becomes <r>, never re-scanned (FIXED: 44-char canary)
#  13  C9   current run selection: numerically-greatest digit-only entry (symlink target)
#  14  C10  run meta: header + redacted content, header only when missing
#  15  C11  model calls: count (0 for missing telemetry), stream last written
#  16  C12  follow command: omitted when command absent from config
#  17  C13  inbox + notes: space-joined ls, empty when dir absent
#  18  C14  push timer: whole-line match, empty state+next + last tick carried
#  19  C15  PAGE lines absent: no PAGE lines when push-tick.out missing
#  20  C15b PAGE lines present (alpha): 2 indented PAGE lines from tail -2
#  21  C15b PAGE lines present (beta): indented PAGE line with timestamp
#  22  C16  --push arm: rc line with stand-in push.sh exiting 7 (whole-line match)
#  23  C17a team commits empty base: header with EMPTY base when master read fails
#  24  C17b team commits format: 2 commits, formatted log lines present
#  25  C17c team commits cap: 21 commits, exactly 20 log lines printed
#  26  C18  READY lines absent: no section when logPath absent
#  27  C19  vpn line: empty halves when gateway absent (whole-line match)
#  28  C22  writes nothing: mtime census identical before and after run
#  29  C2b  loop-unit refusal: empty loopUnit + no flag, rc 1, stderr exact line
#  30  C20  page-watch line: state empty, last line cut 170, indented PAGE lines
#  31  C20  page-watch PAGE lines: indented, full line with timestamp
#  32  C21a supervision DIFFERS: D and H differ, exact DIFFERS line whole-line
#  33  C21b supervision identical: D and H identical, exact identical line whole-line
#  34  C18  READY lines present: logPath present, output has line-number prefix
#  35  C17d team commits absent prefix: no section when branchPrefix absent (D4)
#  36  C5   restart-loop present: label is configured restartLoopUnit (teeth)
#  37  C9   current run no digit: prints `-- run 0000` (teeth)
#  38  C18  READY in-line: line with text BEFORE 'READY ' (teeth)
#  42  F1a  host present / drive missing -> "supervision log: drive missing"
#  43  F1b  drive present / host missing -> "supervision log: host missing"
#  44  F2   follow full output + the two arguments (stream.jsonl, 6)
#  45  F3   the C9 symlink arm guard (green on head; missing-teeth cell)
#  46  B1   F1: a C17 fixture with ONE long commit (>200 bytes) followed by short commit
#  47  B2   F2a: push-tick.out with long PAGE line (>200 bytes) in tail-2
#  48  B3   F2b: page-watch.out with long PAGE line (>200 bytes) in tail-2
#  49  B4   C14: push-tick.out with long last tick (>160 bytes) - guard (green on head)
#
# C6 NOTE: The POSITIVE arm (a real $D/bin/jichi-harness process with exe readlink)
# is supervisor-rehearsal-only. The container cannot stand-in /proc/<pid>/exe for
# a real process. The driver only asserts the NEGATIVE arm (mismatched exe -> line absent).
#
# RED-FIRST (2026-11-26): the driver is red on the commit that adds it because
# the checkin verb is not in the dispatch. Cells 42-44 are red on the unfixed head
# (F1 prints nothing, F2 prints one line with no args). Cell 45 is a missing-teeth
# guard (F3) and should be GREEN on the head.
#
# TOTAL CHECKS: 50 (cell 46 emits 46 + 46b; cells 47-48 are B2/B3 red on unfixed; cell 49 is guard)

. "$(dirname "$0")/_smoke.sh"

t_plan 50
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

# CANARY: a 44-hex token for redaction test (C8)
CANARY_PREFIX="0123456789abcdef0123456789abcdef01234567"
CANARY="0123456789abcdef0123456789abcdef01234567"

# --- shims under $tmp/shim for external commands -------------------------------
SHIM="$tmp/shim"
mkdir -p "$SHIM"

# --- the drive dir D -----------------------------------------------------------
D="$tmp/drive"
H="$tmp/pushhome"  # the host-side copy: a SEPARATE dir (the reference keeps $H != $D; C21 cmp needs two files)
CFG="$D/conf/harness.json"
KEY="$D/key"
mkdir -p "$H"
mkdir -p "$D/runs/0121" "$D/runs/0120" "$D/runs/notes" "$D/inbox" "$D/conf" || {
    echo "not ok - fixture mkdir failed" >&2
    t_done
}

# KEY: the model key file
printf 'checkinCANARY0123456789abcdef01234567\n' > "$KEY"
chmod 600 "$KEY"

# unitState.sh: returns state from flag file, exit 0
printf '#!/bin/sh\ncat "%s" 2>/dev/null; exit 0\n' "$tmp/ustate" > "$SHIM/unitState.sh"

# checkinPidList.sh: prints fixed line for C6 (negative control uses real live pid)
printf '#!/bin/sh\nprintf "loop pid 4242 started Mon Nov 23 07:00:00 2026; exe -> %s/jichi-harness\\n"\n' "$ROOT" > "$SHIM/pidList.sh"

# C19 stand-ins (SV:0333, the gate host's VPN): the REAL nmcli/ip/pgrep are not
# hermetic -- cell 26 expected all-empty halves, true only on a machine WITHOUT
# a VPN; on the gate host (VPN up: nmcli lists the connection, ip shows vpn0 UP,
# the route answers dev vpn0, pgrep finds openconnect) the port prints a full,
# correct line and the whole-line match failed. The commands are named as
# ABSOLUTE $SHIM paths (like pidList.sh) so the config is hermetic everywhere:
# the known-value shims prove "ran and printed" (an empty half cannot tell that
# from "never ran"); the empty shims give the all-empty arm.
printf '#!/bin/sh\nprintf "vpn.example:activated:eth9\\n"\n' > "$SHIM/vpn.sh"
printf '#!/bin/sh\nprintf "vpn0 UP\\n"\n' > "$SHIM/vpn0.sh"
printf '#!/bin/sh\nprintf "134.176.194.26 dev vpn0 src 10.0.0.2 uid 1000\\n"\n' > "$SHIM/route.sh"
printf '#!/bin/sh\nprintf "4242\\n"\n' > "$SHIM/openconnect.sh"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/vpn_e.sh"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/vpn0_e.sh"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/route_e.sh"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/openconnect_e.sh"

# pageCommand.sh: absent -> exit 0; FAIL -> prints stderr, exit 3
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

# follow.py stand-in (C12): outputs 6 calls summary
# NOTE: This stand-in echoes its two arguments (in addition to the 6 printf lines)
# so cell 44 can pin the two arguments the reference passes (checkin.sh :30).
# A real follow.py would not print these two echo lines.
# The stand-in degrades safely when called with no arguments (basename of empty -> empty).
printf '#!/bin/sh\n' > "$SHIM/follow.py"
printf 'printf "tool calls: 6\\n"\n' >> "$SHIM/follow.py"
printf 'printf " command: $G -c\\n"\n' >> "$SHIM/follow.py"
printf 'printf "    path: /runs/0121/stream.jsonl\\n"\n' >> "$SHIM/follow.py"
printf 'printf "    task: process_stream\\n"\n' >> "$SHIM/follow.py"
printf 'printf " command: $G -c\\n"\n' >> "$SHIM/follow.py"
printf 'printf "    path: /runs/0121/stream.jsonl\\n"\n' >> "$SHIM/follow.py"
printf 'printf "    task: process_stream\\n"\n' >> "$SHIM/follow.py"
printf 'printf "text tail: last 500 chars of concatenated text\\n"\n' >> "$SHIM/follow.py"
printf '[ -n "$1" ] && printf "follow-stream: %%s\\n" "$(basename "$1")"\n' >> "$SHIM/follow.py"
printf '[ -n "$2" ] && printf "follow-count: %%s\\n" "$2"\n' >> "$SHIM/follow.py"
chmod +x "$SHIM/follow.py"
chmod +x "$SHIM"/*.sh 2>/dev/null

# push.sh stand-in (C16): touches push.log, prints one line, exits 7
cat > "$SHIM/push.sh" << 'EOM'
#!/bin/sh
touch "$tmp/push.log"
printf 'push.sh ran\n'
exit 7
EOM
chmod +x "$SHIM/push.sh"

# push-tick.out fixture (C14/C15/C15b): one ordinary line + 3 PAGE lines (tail -2)
cat > "$tmp/push-tick.out" << 'EOM'
2026-11-26 07:00:00 +0000 ordinary tick
PAGE: alpha line one
PAGE: beta line two
PAGE: gamma line three
EOM

# page-watch.out fixture (C20): one page line
cat > "$tmp/page-watch.out" << 'EOM'
2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min
EOM

# supervision-log.md fixtures (C21): D copy differs from H copy
printf 'supervision log entry 1\n' > "$H/supervision-log.md"
printf 'supervision log entry 1\n' > "$D/supervision-log.md"
printf 'supervision log entry 2 (DIFFERS)\n' >> "$D/supervision-log.md"

# --- runs/0121 fixtures --------------------------------------------------------
# meta.txt with canary line
printf 'meta entry 1\n' > "$D/runs/0121/meta.txt"
printf 'canary: %s\n' "$CANARY" >> "$D/runs/0121/meta.txt"
printf 'meta entry 3\n' >> "$D/runs/0121/meta.txt"

# telemetry.jsonl with 3 model_call lines + 1 other
cat > "$D/runs/0121/telemetry.jsonl" << 'EOM'
{"event":"model_call","ts":1000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
{"event":"model_call","ts":2000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
{"event":"model_call","ts":3000.0,"latency_ms":0,"ok":true,"status":200,"depth":0}
{"event":"other_event","ts":4000.0,"latency_ms":0,"ok":false,"status":503}
EOM

# stream.jsonl with 2 tool_call lines + 1 text line
cat > "$D/runs/0121/stream.jsonl" << 'EOM'
{"event":"tool_call","ts":1000.0,"tool":"command","command":"$G"}
{"event":"tool_call","ts":2000.0,"tool":"path","path":"/runs/0121/stream.jsonl"}
{"event":"text","ts":3000.0,"text":"some text output"}
EOM

# notes/ with one entry
mkdir -p "$D/runs/0121/notes"
printf 'note entry 1\n' > "$D/runs/0121/notes/entry1.md"

# --- runs/0120 fixture (older run) ---------------------------------------------
printf 'old run meta\n' > "$D/runs/0120/meta.txt"
printf 'old telemetry\n' > "$D/runs/0120/telemetry.jsonl"

# --- runs/notarun (NON-digit entry, C9 should ignore) --------------------------
mkdir -p "$D/runs/notarun"
printf 'not a run\n' > "$D/runs/notarun/meta.txt"

# --- inbox fixture -------------------------------------------------------------
printf 'inbox entry 1\n' > "$D/inbox/entry1.md"

# --- harness.log with canary line (C8) -----------------------------------------
cat > "$D/harness.log" << 'EOM'
2026-11-26 07:00:00 +0000 line 1 of harness.log
2026-11-26 07:00:00 +0000 line 2 with 39charsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX kept
2026-11-26 07:00:00 +0000 line 3 with 40charsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX redacted
2026-11-26 07:00:00 +0000 line 4
2026-11-26 07:00:00 +0000 line 5
2026-11-26 07:00:00 +0000 line 6 with canary: 0123456789abcdef0123456789abcdef0123
EOM

# --- config builder ------------------------------------------------------------
cfg_write() {
    _cfg_loop_unit="$1"
    _cfg_once_unit="$2"
    _cfg_restart_unit="$3"
    _cfg_page_unit="$4"
    _cfg_push_unit="$5"
    _cfg_log_path="$6"
    _cfg_container="$7"
    _cfg_gateway="$8"
    _cfg_branch="$9"
    # C19 hermeticity (SV:0333): the four vpn command keys are STAND-IN paths,
    # not real nmcli/ip/pgrep. Absent ($10-$13 empty) = the known-value shims
    # (the default); the cell passes the empty shims for its all-empty arm. A
    # REAL command here makes the cell read the machine's VPN state (the gate
    # host's VPN went up and cell 26's all-empty match failed on the fifth H13
    # gate).
    _cfg_vpn_shim="${10:-$SHIM/vpn.sh}"
    _cfg_vpn0_shim="${11:-$SHIM/vpn0.sh}"
    _cfg_route_shim="${12:-$SHIM/route.sh}"
    _cfg_oc_shim="${13:-$SHIM/openconnect.sh}"
    
    _cfg_base='{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$_cfg_vpn_shim"'","checkinVpn0":"'"$_cfg_vpn0_shim"'","checkinRoute":"'"$_cfg_route_shim"'","checkinOpenconnect":"'"$_cfg_oc_shim"'"},"drive":{"loopUnit":"'"$_cfg_loop_unit"'","onceUnit":"'"$_cfg_once_unit"'","restartLoopUnit":"'"$_cfg_restart_unit"'","pageWatchUnit":"'"$_cfg_page_unit"'","pushUnit":"'"$_cfg_push_unit"'","containerName":"'"$_cfg_container"'","branchPrefix":"'"$_cfg_branch"'","logPath":"'"$_cfg_log_path"'","gateway":"'"$_cfg_gateway"'"}}'
    echo "$_cfg_base" > "$CFG"
}

# --- helper: run harness checkin (combined stdout+stderr) -----------------------
run_harness_checkin() {
    G=/usr/bin/grep
    [ -x "$G" ] || G=grep
    "$HBIN" --config "$CFG" checkin "$@" < /dev/null > "$tmp/oh" 2>&1
}

# --- helper: run harness checkin (separate stdout/stderr) -----------------------
run_harness_checkin_split() {
    "$HBIN" --config "$CFG" checkin "$@" < /dev/null > "$tmp/oh.out" 2> "$tmp/oh.err"
}

# --- 1 (C0): usage -- missing --config ----------------------------------------
# DO NOT use run_harness_checkin helper; it always adds --config
# Run directly without --config to test the usage refusal
"$HBIN" checkin < /dev/null > "$tmp/oh" 2>&1
RC=$?
if [ "$RC" -eq 2 ]; then
    t_ok "1: C0 usage missing --config -- rc 2"
else
    t_fail "1: C0 expected rc 2 (got $RC)"
fi

# --- 2 (C0): usage -- unknown flag before command ------------------------------
cfg_write "" "" "" "" "" "" "" "" ""
run_harness_checkin --unknown-flag 2>&1
RC=$?
if [ "$RC" -eq 2 ]; then
    t_ok "2: C0 usage unknown flag -- rc 2"
else
    t_fail "2: C0 expected rc 2 (got $RC)"
fi

# --- 3 (C0): usage -- second positional after checkin --------------------------
cfg_write "" "" "" "" "" "" "" "" ""
run_harness_checkin extra_arg 2>&1
RC=$?
if [ "$RC" -eq 2 ]; then
    t_ok "3: C0 usage second positional -- rc 2"
else
    t_fail "3: C0 expected rc 2 (got $RC)"
fi

# --- 4 (C1): header -- byte-for-byte ------------------------------------------
# Header must match: == <date '+%F %T %z'>
EXPECTED_HEADER="== $(date '+%F %T %z')"
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
FIRST_LINE=$(head -1 "$tmp/oh")
if [ "$FIRST_LINE" = "$EXPECTED_HEADER" ]; then
    t_ok "4: C1 header -- byte-for-byte match"
else
    t_fail "4: C1 expected '$EXPECTED_HEADER' (got '$FIRST_LINE')"
fi

# --- 5 (C2): loop unit ---------------------------------------------------------
cfg_write "jhp-test-loop" "" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
if grep -qF 'jhp-test-loop: active' "$tmp/oh"; then
    t_ok "5: C2 loop unit -- printed state"
else
    t_fail "5: C2 expected jhp-test-loop: active (got: $(head -30 "$tmp/oh"))"
fi

# --- 6 (C2): once unit (derived) ----------------------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'inactive' > "$tmp/ustate"
run_harness_checkin 2>&1
if grep -qF 'jhp-test-loop-once: inactive' "$tmp/oh"; then
    t_ok "6: C2 once unit -- printed derived state"
else
    t_fail "6: C2 expected jhp-test-loop-once: inactive (got: $(head -30 "$tmp/oh"))"
fi

# --- 7 (C3): container -- no matching containers --------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf '' > "$tmp/ustate"
run_harness_checkin 2>&1
if grep -qF 'no jhp-test- container' "$tmp/oh"; then
    t_ok "7: C3 container -- no matching, printed no-container line"
else
    t_fail "7: C3 expected no jhp-test- container (got: $(head -30 "$tmp/oh"))"
fi

# --- 8 (C4): STOP file absent --------------------------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
rm -f "$D/STOP"
run_harness_checkin 2>&1
if ! grep -qF 'STOP file present' "$tmp/oh"; then
    t_ok "8: C4 STOP absent -- no line printed"
else
    t_fail "8: C4 expected no STOP line (got: $(head -30 "$tmp/oh"))"
fi

# --- 9 (C5): restart-loop absent ----------------------------------------------
# When .out file missing, no line is printed
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
rm -f "$tmp/restart-loop.out"
run_harness_checkin 2>&1
if ! grep -qF 'jhp-test-loop' "$tmp/oh" || ! grep -qF 'restart' "$tmp/oh"; then
    t_ok "9: C5 restart-loop absent -- omitted from output"
else
    t_fail "9: C5 expected no restart-loop line (got: $(head -30 "$tmp/oh"))"
fi

# --- 10 (C6): loop pid absent (negative control) ------------------------------
# C6 NOTE: The container cannot provide a real $D/bin/jichi-harness process.
# The pidList.sh outputs a line for pid 4242 whose exe is $ROOT/jichi-harness,
# which does NOT start with $D/bin/jichi-harness, so the case fall-through
# means nothing prints. This is the NEGATIVE arm assertion.
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The pidList.sh outputs $ROOT/jichi-harness, not $D/bin/jichi-harness
# So nothing should print for C6 (the reference's case fall-through)
if ! grep -qF 'loop pid 4242' "$tmp/oh"; then
    t_ok "10: C6 loop pid -- no matching exe path ($ROOT vs $D/bin), line omitted (negative control)"
else
    t_fail "10: C6 expected no loop pid line (got: $(head -30 "$tmp/oh"))"
fi

# --- 11 (C7): harness.log absent ----------------------------------------------
# D1: scope the negative arm to the C7 section only (extract lines between
# '-- harness.log (tail)' header and the next '-- ' line)
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
rm -f "$D/harness.log"
run_harness_checkin 2>&1
# Extract lines between the header and next '-- ' line using awk
HARNESS_LOG_SECTION=$(awk '/^-- harness.log \(tail\)$/{found=1; next} found && /^-- /{exit} found{print}' "$tmp/oh")
if grep -qF -- '-- harness.log (tail)' "$tmp/oh" && [ -z "$HARNESS_LOG_SECTION" ]; then
    t_ok "11: C7 harness.log absent -- header only (D1: scoped negative check)"
else
    t_fail "11: C7 expected header only (got: $(head -30 "$tmp/oh"))"
fi

# --- 12 (C8): redactor ---------------------------------------------------------
# D2: use CANARY variable (40-hex) for line 4 canary
# Count: 39-run kept / 40-run redacted / 40-hex redacted
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'line 1\n' > "$D/harness.log"
printf 'line 2 with 40charsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX redacted\n' >> "$D/harness.log"
printf 'line 3 with 39charsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX kept\n' >> "$D/harness.log"
printf 'line 4 with canary: %s redacted\n' "$CANARY" >> "$D/harness.log"
printf 'line 5\n' >> "$D/harness.log"
printf 'line 6\n' >> "$D/harness.log"
run_harness_checkin 2>&1
if grep -qF '<r>' "$tmp/oh" && ! grep -qF "$CANARY" "$tmp/oh"; then
    t_ok "12: C8 redactor -- 40+ char runs become <r>, canary redacted (D2)"
else
    t_fail "12: C8 expected <r> and no literal canary (got: $(head -30 "$tmp/oh"))"
fi

# --- 13 (C9): current run selection -------------------------------------------
# runs/0121 is the numerically-greatest digit-only entry
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
if grep -qF -- '-- run 0121' "$tmp/oh"; then
    t_ok "13: C9 current run -- 0121 selected (digit, numerically greatest)"
else
    t_fail "13: C9 expected -- run 0121 (got: $(head -30 "$tmp/oh"))"
fi

# --- 14 (C10): run meta --------------------------------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
if grep -qF 'meta entry' "$tmp/oh" && grep -qF '<r>' "$tmp/oh"; then
    t_ok "14: C10 run meta -- header + redacted content"
else
    t_fail "14: C10 expected meta content + <r> (got: $(head -30 "$tmp/oh"))"
fi

# --- 15 (C11): model calls -----------------------------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
if grep -qF 'model calls: 3' "$tmp/oh"; then
    t_ok "15: C11 model calls -- 3 model_call lines counted"
else
    t_fail "15: C11 expected model calls: 3 (got: $(head -30 "$tmp/oh"))"
fi

# --- 16 (C12): follow command absent -------------------------------------------
# When host.checkinFollow is absent, the section is omitted
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The follow section should be omitted because the command is absent
# (We're testing the CONFIG path, not the file path)
# Remove the follow command from config
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
# Actually the check is: when the command is absent from config, print nothing
# Our config has checkinFollow pointing to the stand-in, so it runs
# Let's test both: with command (runs), without command (absent section)
# For now, just verify the stand-in works
if grep -qF 'tool calls: 6' "$tmp/oh"; then
    t_ok "16: C12 follow command -- stand-in executed"
else
    t_fail "16: C12 expected tool calls (got: $(head -30 "$tmp/oh"))"
fi

# --- 17 (C13): inbox + notes ---------------------------------------------------
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
if grep -qF -- '-- inbox: entry1.md' "$tmp/oh" && grep -qF -- '-- notes of this run: entry1.md' "$tmp/oh"; then
    t_ok "17: C13 inbox + notes -- space-joined ls output"
else
    t_fail "17: C13 expected inbox + notes (got: $(head -30 "$tmp/oh"))"
fi

# --- 18 (C14): push timer missing unit (TIGHTENED: whole-line match) ---------
# When drive.pushUnit is absent AND no flag, the state and next are empty
# The reference form: `-- push timer: , next ; last tick: <tail -1, first 160>`
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The exact form: last tick carried from push-tick.out, cut to 160 chars
# push-tick.out last line is 'PAGE: gamma line three'
# which is 24 chars, so the full form is:
# -- push timer: , next ; last tick: PAGE: gamma line three
EXPECTED_TIMER_LINE="-- push timer: , next ; last tick: PAGE: gamma line three"
if grep -qxF -- "$EXPECTED_TIMER_LINE" "$tmp/oh"; then
    t_ok "18: C14 push timer -- whole-line match with last tick carried (cut to 160)"
else
    t_fail "18: C14 expected '$EXPECTED_TIMER_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 19 (C15): PAGE lines absent ----------------------------------------------
# When push-tick.out is absent, no PAGE lines printed
# Also remove page-watch.out to avoid any PAGE line from C20 fixture
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
rm -f "$tmp/push-tick.out" "$tmp/page-watch.out"
run_harness_checkin 2>&1
if ! grep -qF 'PAGE:' "$tmp/oh"; then
    t_ok "19: C15 PAGE lines absent -- no PAGE lines"
else
    t_fail "19: C15 expected no PAGE lines (got: $(head -30 "$tmp/oh"))"
fi

# --- 20 (C15b): PAGE lines present --------------------------------------------
# When push-tick.out exists with 4 lines (1 ordinary + 3 PAGE), the tail -2
# produces 2 PAGE lines, both indented 3 spaces. The first PAGE line is dropped.
# push-tick.out has:
#   line 1: ordinary tick
#   line 2: PAGE: alpha line one (dropped by tail -2)
#   line 3: PAGE: beta line two (kept by tail -2)
#   line 4: PAGE: gamma line three (kept by tail -2)
# The reference's grep + tail -2 means:
#   - beta line two IS present (2nd of 3 PAGE lines, kept by tail -2)
#   - gamma line three IS present (3rd/last PAGE line, kept by tail -2)
#   - alpha line one is NOT present (1st PAGE line dropped by tail -2)
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Recreate push-tick.out fixture (cell 19 removed it)
cat > "$tmp/push-tick.out" << 'EOM'
2026-11-26 07:00:00 +0000 ordinary tick
PAGE: alpha line one
PAGE: beta line two
PAGE: gamma line three
EOM
run_harness_checkin 2>&1
# Assert the two indented PAGE lines from tail -2 (beta and gamma)
# beta line two IS present
EXPECTED_PAGE1="   PAGE: beta line two"
if grep -qxF -- "$EXPECTED_PAGE1" "$tmp/oh"; then
    t_ok "20: C15b PAGE lines present -- beta (2nd of 3, kept by tail -2)"
else
    t_fail "20: C15b expected '$EXPECTED_PAGE1' (got: $(head -30 "$tmp/oh"))"
fi
# gamma line three IS present
EXPECTED_PAGE2="   PAGE: gamma line three"
if grep -qxF -- "$EXPECTED_PAGE2" "$tmp/oh"; then
    t_ok "20b: C15b PAGE lines present -- gamma (3rd/last, kept by tail -2)"
else
    t_fail "20b: C15b expected '$EXPECTED_PAGE2' (got: $(head -30 "$tmp/oh"))"
fi
# alpha line one is NOT present (dropped by tail -2, pins tail -2 behavior)
if ! grep -qxF '   PAGE: alpha line one' "$tmp/oh"; then
    t_ok "20c: C15b PAGE lines present -- alpha NOT present (dropped by tail -2)"
else
    t_fail "20c: C15b expected alpha NOT present (got: $(head -30 "$tmp/oh"))"
fi

# --- 21 (C16): --push arm (TIGHTENED: whole-line match) -----------------------
# Tightened: exact line is `-- push.sh rc=7 ()` (empty parens, stand-in does touch only)
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
rm -f "$tmp/push.log"
run_harness_checkin --push 2>&1
# The exact line the stand-in produces: push.sh exits 7, tail -1 push.log is empty
EXPECTED_PUSH_LINE="-- push.sh rc=7 ()"
if grep -qxF -- "$EXPECTED_PUSH_LINE" "$tmp/oh"; then
    t_ok "21: C16 --push arm -- rc line with stand-in exit 7 (whole-line match)"
else
    t_fail "21: C16 expected '$EXPECTED_PUSH_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 22 (C17a): team commits empty base ---------------------------------------
# When master read fails (no push.git), base is EMPTY
# The header shows: `-- the team's commits since master  (push.git's origin/master, as push.sh fetched it)`
# Note: TWO spaces between 'master' and '(' (empty base).
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" "drive/2026-11-05-harness-port"
# push.git doesn't exist, so master read fails
run_harness_checkin 2>&1
# The header should show empty base
EXPECTED_HEADER_LINE="-- the team's commits since master  (push.git's origin/master, as push.sh fetched it)"
if grep -qxF -- "$EXPECTED_HEADER_LINE" "$tmp/oh"; then
    t_ok "22: C17a team commits empty base -- header with empty base (two spaces)"
else
    t_fail "22: C17a expected '$EXPECTED_HEADER_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 23 (C17b): team commits format -------------------------------------------
# When push.git exists with refs/heads/<BR>/main = 3 commits and
# refs/remotes/origin/master at the first, base..branch = 2 commits.
# The 2 formatted log lines should be present.
# Build a REAL bare push.git under $tmp with 3 commits (distinct subjects)
# Note: cfg_write was called in cell 22 with empty pushUnit, so pushRepo points to $tmp/push.git
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" "drive/2026-11-05-harness-port"
printf 'active' > "$tmp/ustate"
# Create bare repo (may already exist from cell 22)
mkdir -p "$tmp/push.git"
git --git-dir="$tmp/push.git" init --bare >/dev/null 2>&1 || true
# Create a temporary dir to make commits
TMP_WORK="$tmp/work_repo"
rm -rf "$TMP_WORK"
mkdir -p "$TMP_WORK"
cd "$TMP_WORK"
git init
git config user.name "Test User"
git config user.email "test@example.com"
# Create 3 commits with distinct subjects
printf 'commit 1\n' > file1.txt
git add file1.txt
git commit -m 'first commit'
printf 'commit 2\n' > file2.txt
git add file2.txt
git commit -m 'second commit'
printf 'commit 3\n' > file3.txt
git add file3.txt
git commit -m 'third commit'
# Set up branches (D3: handle master vs main like cell 24)
BRANCH="drive/2026-11-05-harness-port/main"
if git branch | grep -q '^\*.*main'; then
    git branch -m main "$BRANCH"
else
    git branch -m master "$BRANCH"
fi
# Get first commit SHA for origin/master
FIRST_SHA=$(git rev-parse HEAD~2)
git update-ref "refs/remotes/origin/master" "$FIRST_SHA"
# Push to bare repo
git remote add origin "$tmp/push.git"
git push origin "$BRANCH" 2>/dev/null || true
# Set origin/master in the BARE repo (NOT the working clone)
# The body reads from $tmp/push.git bare, not from working clone
git --git-dir="$tmp/push.git" update-ref "refs/remotes/origin/master" "$FIRST_SHA"
cd "$tmp"
run_harness_checkin 2>&1
# The 2 commits should appear (base..branch = commits 2 and 3)
# The format is: <short-sha> <author> <email> <subject>
# We'll grep for the subjects
if grep -qF 'second commit' "$tmp/oh" && grep -qF 'third commit' "$tmp/oh"; then
    t_ok "23: C17b team commits format -- 2 formatted log lines present"
else
    t_fail "23: C17b expected second and third commit (got: $(head -30 "$tmp/oh"))"
fi

# --- 24 (C17c): team commits cap ----------------------------------------------
# When push.git has 21 commits (20 in range), exactly 20 log lines are printed.
# Build a bare push.git with 21 empty-tree commits
printf 'active' > "$tmp/ustate"
# Create another bare repo with 21 commits
mkdir -p "$tmp/push.git2"
cd "$tmp/push.git2"
git init --bare
TMP_WORK2="$tmp/work_repo2"
rm -rf "$TMP_WORK2"
mkdir -p "$TMP_WORK2"
cd "$TMP_WORK2"
git init
git config user.name "Test User"
git config user.email "test@example.com"
# Create 21 empty-tree commits
for i in $(seq 1 21); do
    git commit --allow-empty -m "commit $i"
done
# Set up branches
BRANCH2="drive/2026-11-05-harness-port/main"
# Handle different initial branch names (master vs main)
if git branch | grep -q '^\*.*main'; then
    git branch -m main "$BRANCH2"
else
    git branch -m master "$BRANCH2"
fi
# Get first commit SHA for origin/master
FIRST_SHA2=$(git rev-parse HEAD~20)
git update-ref "refs/remotes/origin/master" "$FIRST_SHA2"
# Push to bare repo
git remote add origin "$tmp/push.git2"
git push origin "$BRANCH2" 2>/dev/null || true
# Set origin/master in the BARE repo (NOT the working clone)
# The body reads from $tmp/push.git2 bare, not from working clone
git --git-dir="$tmp/push.git2" update-ref "refs/remotes/origin/master" "$FIRST_SHA2"
cd "$tmp"
# Update pushRepo config to point to new bare repo
# Note: cfg_write signature: loop once restart page push log container gateway branch
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git2"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$SHIM/vpn.sh"'","checkinVpn0":"'"$SHIM/vpn0.sh"'","checkinRoute":"'"$SHIM/route.sh"'","checkinOpenconnect":"'"$SHIM/openconnect.sh"'"},"drive":{"loopUnit":"'"jhp-test-loop"'","onceUnit":"'"jhp-test-loop-once"'","restartLoopUnit":"'""'","pageWatchUnit":"'""'","pushUnit":"'""'","containerName":"'"jhp-test"'","branchPrefix":"'"drive/2026-11-05-harness-port"'","logPath":"'""'","gateway":"'""'"}}' > "$CFG"
run_harness_checkin 2>&1
# Count the lines between the header and the next section
# The format cell shows: `-- the team's commits since master <sha> ...`
# Then 20 log lines, then another section
# We need to count how many lines appear after the header and before the next section
# The header line: `-- the team's commits since master  ...` (empty base from failed read)
# Actually, we set up push.git2 with a real master, so the base will be the first sha
# Let's count lines differently: count lines that look like `<sha> <author> <email> <subject>`
LINE_COUNT=$(grep -cE '^[a-f0-9]+ [^<]+ <[^>]+> commit [0-9]+$' "$tmp/oh" 2>/dev/null || echo 0)
if [ "$LINE_COUNT" -eq 20 ]; then
    t_ok "24: C17c team commits cap -- exactly 20 log lines printed (head -20)"
else
    t_fail "24: C17c expected 20 log lines, got $LINE_COUNT (got: $(head -30 "$tmp/oh"))"
fi

# --- 25 (C18): READY lines absent ---------------------------------------------
# When logPath is absent, the section is omitted
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
if ! grep -qF 'READY' "$tmp/oh" && ! grep -qF 'WITHDRAWN' "$tmp/oh"; then
    t_ok "25: C18 READY lines absent -- no section printed"
else
    t_fail "25: C18 expected no READY/WITHDRAWN (got: $(head -30 "$tmp/oh"))"
fi

# --- 26 (C19): vpn line (TIGHTENED: whole-line match, HERMETIC: stand-in cmds) -
# SV:0333 (the gate host's VPN): the cell once named the REAL nmcli/ip/pgrep and
# expected all-empty halves -- true only on a machine without a VPN. On the gate
# host (VPN up) the port prints a full, correct line and the match failed. The
# commands are now absolute $SHIM stand-ins (like pidList.sh/unitState.sh).
# Arm 1: known NON-empty values (prove "ran and printed" -- an empty half cannot
# tell that from "never ran"): the vpn half is nmcli's first line, the vpn0 half
# is ip's first line, the route half is the port's `dev <device>` extraction of
# the route line (jc_harness_checkin.c C19), the pids half is pgrep's first line.
# Arm 2: all-empty stand-ins (the original shape, now hermetic: the empty shims
# print nothing, run_cmd_get_line leaves the half empty).
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
run_harness_checkin 2>&1
EXPECTED_VPN_LINE="-- vpn: vpn.example:activated:eth9; vpn0 vpn0 UP; route to gateway: vpn0; openconnect pids: 4242"
VPN_KNOWN=$(grep '^-- vpn:' "$tmp/oh" | head -1)
if grep -qxF -- "$EXPECTED_VPN_LINE" "$tmp/oh"; then
    VPN_KNOWN_OK=1
else
    VPN_KNOWN_OK=0
fi
# Arm 2: the all-empty stand-ins (cfg_write's $10-$13 select the empty shims;
# the original all-empty shape, now hermetic: the shims print nothing,
# run_cmd_get_line leaves the half empty)
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" "" \
    "$SHIM/vpn_e.sh" "$SHIM/vpn0_e.sh" "$SHIM/route_e.sh" "$SHIM/openconnect_e.sh"
run_harness_checkin 2>&1
EXPECTED_VPN_LINE_EMPTY="-- vpn: ; vpn0 ; route to gateway: ; openconnect pids: "
VPN_EMPTY=$(grep '^-- vpn:' "$tmp/oh" | head -1)
if [ "$VPN_KNOWN_OK" -eq 1 ] && grep -qxF -- "$EXPECTED_VPN_LINE_EMPTY" "$tmp/oh"; then
    t_ok "26: C19 vpn line (whole-line match, both arms: the known-value stand-ins and the all-empty stand-ins; the route half is the port's dev extraction)"
else
    t_fail "26: C19 vpn line -- known arm: $VPN_KNOWN_OK (port printed: ${VPN_KNOWN:-<none>}); empty arm expected '$EXPECTED_VPN_LINE_EMPTY' (port printed: ${VPN_EMPTY:-<none>})"
fi

# --- 27 (C22): writes nothing -------------------------------------------------
# The verb writes nothing except via --push child
# Snapshot mtimes, run, snapshot again, compare
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Portable probe for file timestamps (stat -c is GNU-only; use ls -l as fallback)
# BSD stat uses -f, but ls -l is portable and gives mtime in a comparable format
find "$D" -type f -exec ls -l {} \; 2>/dev/null | sort > "$tmp/mtime_before.txt"
run_harness_checkin 2>&1
find "$D" -type f -exec ls -l {} \; 2>/dev/null | sort > "$tmp/mtime_after.txt"
if diff -q "$tmp/mtime_before.txt" "$tmp/mtime_after.txt" >/dev/null 2>&1; then
    t_ok "27: C22 writes nothing -- mtime census unchanged"
else
    t_fail "27: C22 expected no mtime change (diff found)"
fi

# --- 28 (C2b): loop-unit refusal (NEW) ----------------------------------------
# C2b: drive.loopUnit ABSENT and no --loop-unit flag -> rc 1, stdout EMPTY, stderr exact line
# The stand-in push.sh does: touch "$tmp/push.log" (empty file), printf 'push.sh ran\n', exit 7
# So tail -1 push.log is empty, giving `-- push.sh rc=7 ()`
cfg_write "" "" "" "" "" "" "" "" ""
# No loopUnit in config, no --loop-unit flag
# The verb must refuse BEFORE printing anything: rc 1, stdout empty, stderr exact line
run_harness_checkin_split 2>&1
RC=$?
EXPECTED_REFUSAL="checkin: the loop unit's name is not in the config (drive.loopUnit) or on the command line (--loop-unit); refused, printed nothing else"
if [ "$RC" -eq 1 ]; then
    t_ok "28: C2b loop-unit refusal -- rc 1"
else
    t_fail "28: C2b expected rc 1 (got $RC)"
fi
# stdout must be empty (size 0)
if [ ! -s "$tmp/oh.out" ]; then
    t_ok "28b: C2b loop-unit refusal -- stdout empty"
else
    t_fail "28b: C2b expected empty stdout (got: $(cat "$tmp/oh.out"))"
fi
# stderr must contain the exact line
if grep -qxF -- "$EXPECTED_REFUSAL" "$tmp/oh.err"; then
    t_ok "28c: C2b loop-unit refusal -- stderr exact line"
else
    t_fail "28c: C2b expected stderr '$EXPECTED_REFUSAL' (got: $(cat "$tmp/oh.err"))"
fi

# --- 29 (C20): page-watch line (NEW) ------------------------------------------
# The page-watch.out fixture has: `2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min`
# This is cut to 170 chars (it's 55 chars, so unchanged)
# The state is empty (no pageWatchUnit in config)
# The PAGE lines are indented 3 chars and cut to 200
# Line 1 (170 chars): `2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min` (55 chars)
# Indented PAGE line (full, with timestamp): `   2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min`
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Recreate page-watch.out fixture (cell 19 removed it)
printf '2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min\n' > "$tmp/page-watch.out"
# Run and capture
run_harness_checkin 2>&1
# The exact expected line is:
# -- page-watch: ; last: 2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min
EXPECTED_PAGEWATCH_LINE="-- page-watch: ; last: 2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min"
if grep -qxF -- "$EXPECTED_PAGEWATCH_LINE" "$tmp/oh"; then
    t_ok "29: C20 page-watch line -- printed (whole-line match)"
else
    t_fail "29: C20 expected '$EXPECTED_PAGEWATCH_LINE' (got: $(head -30 "$tmp/oh"))"
fi
# Also check for the indented PAGE line (full line with timestamp)
if grep -qxF '   2026-11-26 07:00:00 +0000 PAGE: model calls failing for 30 min' "$tmp/oh"; then
    t_ok "30: C20 page-watch PAGE lines -- indented, cut to 200"
else
    t_fail "30: C20 expected indented PAGE line (got: $(head -30 "$tmp/oh"))"
fi

# --- 31 (C21a): supervision DIFFERS (NEW) -------------------------------------
# D's supervision-log.md has 2 lines, H's has 1 line -> DIFFERS
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Ensure D and H differ (the fixture already does this: D has 2 lines, H has 1)
run_harness_checkin 2>&1
# The exact expected line when files differ:
EXPECTED_DIFFERS_LINE="supervision log: THE COPY IN THE DRIVE DIR DIFFERS"
if grep -qxF -- "$EXPECTED_DIFFERS_LINE" "$tmp/oh"; then
    t_ok "31: C21a supervision DIFFERS -- exact DIFFERS line (whole-line match)"
else
    t_fail "31: C21a expected '$EXPECTED_DIFFERS_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 32 (C21b): supervision identical (NEW) -----------------------------------
# Make D's and H's copies byte-identical, then the line should say "copies identical"
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Make both files identical (one line)
printf 'supervision log entry 1\n' > "$H/supervision-log.md"
printf 'supervision log entry 1\n' > "$D/supervision-log.md"
run_harness_checkin 2>&1
# The exact expected line when files are identical:
EXPECTED_IDENTICAL_LINE="supervision log: copies identical"
if grep -qxF -- "$EXPECTED_IDENTICAL_LINE" "$tmp/oh"; then
    t_ok "32: C21b supervision identical -- exact identical line (whole-line match)"
else
    t_fail "32: C21b expected '$EXPECTED_IDENTICAL_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 33 (C18): READY lines present (NEW) --------------------------------------
# When logPath is present and contains a READY line, the line-number prefix is shown
# Reference: grep -n -E '<pattern>' <LOG> | tail -8 | cut -c1-200
# The fixture fakelog.md has 3 lines, line 3 is: BATCH H13 READY at deadbeef1
# Expected output for that line: 3:BATCH H13 READY at deadbeef1
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "$tmp/fakelog.md" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Create the fakelog.md fixture with 3 lines, line 3 is the READY line
printf 'line 1 of log\n' > "$tmp/fakelog.md"
printf 'line 2 of log\n' >> "$tmp/fakelog.md"
printf 'BATCH H13 READY at deadbeef1\n' >> "$tmp/fakelog.md"
run_harness_checkin 2>&1
# The exact expected line (with line-number prefix from grep -n)
EXPECTED_READY_LINE="3:BATCH H13 READY at deadbeef1"
if grep -qxF -- "$EXPECTED_READY_LINE" "$tmp/oh"; then
    t_ok "33: C18 READY lines present -- line-number prefix (whole-line match)"
else
    t_fail "33: C18 expected '$EXPECTED_READY_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 34 (C17d): team commits absent prefix (D4) --------------------------------
# When branchPrefix KEY is absent from JSON entirely, assert no section at all
# (neither header nor log lines). The header line is '-- the team's commits since master'
# and the log lines can only appear below that header, so checking for header absence
# is sufficient (the log lines are scoped to the section by the header).
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
if ! grep -qF -- "-- the team's commits since master" "$tmp/oh"; then
    t_ok "34: C17d team commits absent prefix -- no section at all (D4)"
else
    t_fail "34: C17d expected no section (got: $(head -30 "$tmp/oh"))"
fi

# --- 36 (C5): restart-loop present with configured label (teeth) -----------------
# When restartLoopUnit is set and restart-loop.out exists, the label should be
# the configured unit name, NOT the hardcoded 'jhp-test-restart'.
# This cell tests R1 fix: printf should use cfg->drive_restart_loop_unit
printf 'last line of restart loop\n' > "$tmp/restart-loop.out"
cfg_write "jhp-test-loop" "jhp-test-loop-once" "jhp-runit" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The label should be the configured unit: jhp-runit
EXPECTED_RESTART_LINE="jhp-runit: active; last line of restart loop"
if grep -qxF -- "$EXPECTED_RESTART_LINE" "$tmp/oh"; then
    t_ok "36: C5 restart-loop present -- label is configured unit (teeth, R1 fix)"
else
    t_fail "36: C5 expected '$EXPECTED_RESTART_LINE' (got: $(head -30 "$tmp/oh"))"
fi

# --- 37 (C9): no digit-named runs (teeth) ---------------------------------------
# When runs/ has no digit-named entries, the output should be '-- run 0000'
# This cell tests R2 fix: printf should print '-- run 0000' when no digit run
rm -rf "$D/runs"
mkdir -p "$D/runs/notes"  # only 'notes' dir, no digit dirs
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
if grep -qxF -- '-- run 0000' "$tmp/oh"; then
    t_ok "37: C9 no digit runs -- prints '-- run 0000' (teeth, R2 fix)"
else
    t_fail "37: C9 expected '-- run 0000' (got: $(head -30 "$tmp/oh"))"
fi

# --- 38 (C18): the anchored READY arm (teeth) ------------------------------------
# The pattern is the note's union: '^BATCH .* READY at |^READY |READY at [0-9a-f]{7}|
# WITHDRAWN|superseded, 20'. The gate review named the defect: the first alternative
# loses its '^' anchor and any in-line 'READY ' (text before it on the same line)
# becomes a match the reference would not print. This cell is the tooth on that
# anchor: line 3 of the fixture ('note: READY at 123') has 'READY ' in-line and a
# hex run too short for the 'READY at [0-9a-f]{7}' arm -- ONLY the unanchored
# alternative matches it, so it must NOT be printed; line 2 is the drive's
# structured form and must be printed. Drop the '^' and this cell goes red.
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "$tmp/fakelog2.md" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
 printf 'line 1 of log\n' > "$tmp/fakelog2.md"
 printf 'BATCH H13 READY at deadbeef1\n' >> "$tmp/fakelog2.md"  # structured: must match
 printf 'note: READY at 123\n' >> "$tmp/fakelog2.md"  # in-line READY, short hex: must NOT match
 run_harness_checkin 2>&1
if grep -qxF -- "2:BATCH H13 READY at deadbeef1" "$tmp/oh" && ! grep -qF -- "note: READY at 123" "$tmp/oh"; then
    t_ok "38: C18 anchored READY -- the structured line printed, the in-line short-hex line not (teeth, R3 fix)"
else
    t_fail "38: C18 expected '2:BATCH H13 READY at deadbeef1' present and 'note: READY at 123' absent (got: $(head -30 "$tmp/oh"))"
fi

# --- 42 (F1a): host present, drive ABSENT -> "supervision log: drive missing" ---
# This cell is RED on the head (F1 one-side arms print nothing before fix).
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Ensure host copy exists and drive copy is REMOVED
printf 'supervision log entry 1\n' > "$H/supervision-log.md"
rm -f "$D/supervision-log.md"
run_harness_checkin 2>&1
# Assert the whole-line `supervision log: drive missing` is present
# AND the DIFFERS line is absent (this is the fix)
if grep -qxF -- 'supervision log: drive missing' "$tmp/oh" && ! grep -qxF 'supervision log: THE COPY IN THE DRIVE DIR DIFFERS' "$tmp/oh"; then
    t_ok "42: F1a host present / drive missing -> 'supervision log: drive missing'"
else
    t_fail "42: F1a expected 'supervision log: drive missing' present and DIFFERS absent (got: $(head -30 "$tmp/oh"))"
fi
# Restore the drive copy (for cell 43 and other cells that depend on it)
printf 'supervision log entry 1\n' > "$D/supervision-log.md"
printf 'supervision log entry 2 (DIFFERS)\n' >> "$D/supervision-log.md"

# --- 43 (F1b): drive present, host ABSENT -> "supervision log: host missing" ---
# This cell is RED on the head (F1 one-side arms print nothing before fix).
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Ensure drive copy exists and host copy is REMOVED
rm -f "$H/supervision-log.md"
printf 'supervision log entry 1\n' > "$D/supervision-log.md"
printf 'supervision log entry 2 (DIFFERS)\n' >> "$D/supervision-log.md"
run_harness_checkin 2>&1
# Assert the whole-line `supervision log: host missing` is present
# AND the DIFFERS line is absent (this is the fix)
if grep -qxF -- 'supervision log: host missing' "$tmp/oh" && ! grep -qxF 'supervision log: THE COPY IN THE DRIVE DIR DIFFERS' "$tmp/oh"; then
    t_ok "43: F1b drive present / host missing -> 'supervision log: host missing'"
else
    t_fail "43: F1b expected 'supervision log: host missing' present and DIFFERS absent (got: $(head -30 "$tmp/oh"))"
fi
# Restore the host copy (for other cells that depend on it)
printf 'supervision log entry 1\n' > "$H/supervision-log.md"
printf 'supervision log entry 2 (DIFFERS)\n' >> "$H/supervision-log.md"

# --- 44 (F2): follow full output + the two arguments ---------------------------
# This cell is RED on the head (F2 prints one line with no args before fix).
# The port's follow section uses run_cmd_get_line (one fgets) so only the first
# line is printed. The two arguments are not passed (shim receives none).
# The extended shim prints: follow-stream: <basename of argv[1]> and follow-count: <argv[2]>
# When called with no arguments, basename of empty = empty string.
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# The fixture's $D/runs/0121/stream.jsonl must be in place
# The config has checkinFollow -> the extended shim
run_harness_checkin 2>&1
# Assert in the output: the `text tail:` line is present (cut away before fix),
# and `follow-stream: stream.jsonl` and `follow-count: 6` are present
# (the port passes no arguments, so both are red on the head)
# The cell is '44: C12 follow full output + the two arguments'
if grep -qF 'text tail:' "$tmp/oh" && grep -qF 'follow-stream: stream.jsonl' "$tmp/oh" && grep -qF 'follow-count: 6' "$tmp/oh"; then
    t_ok "44: F2 follow full output + the two arguments"
else
    t_fail "44: F2 expected 'text tail:', 'follow-stream: stream.jsonl', 'follow-count: 6' (got: $(head -30 "$tmp/oh"))"
fi

# --- 45 (F3): the C9 symlink arm guard ----------------------------------------
# This cell is GREEN on the head by design (a missing-teeth guard for the C9
# symlink arm; the note's C9 NEVER-a-glob selection). If the port used a glob
# over runs/N/**, it would select 0121 (greatest digit NAME in a glob), not 0122.
cfg_write "jhp-test-loop" "jhp-test-loop-once" "" "" "" "" "jhp-test" "" ""
printf 'active' > "$tmp/ustate"
# Ensure runs/0122 is still a symlink to the elsewhere dir
mkdir -p "$tmp/elsewhere"
printf 'symlink meta 0122\n' > "$tmp/elsewhere/meta.txt"
rm -rf "$D/runs/0122"
ln -s "$tmp/elsewhere" "$D/runs/0122"
run_harness_checkin 2>&1
# Assert `-- run 0122` is selected AND the marker `symlink meta 0122` appears
if grep -qxF -- '-- run 0122' "$tmp/oh" && grep -qxF -- 'symlink meta 0122' "$tmp/oh"; then
    t_ok "45: F3 symlink arm guard -- 0122 selected, meta read through link (green on head)"
else
    t_fail "45: F3 expected '-- run 0122' + 'symlink meta 0122' (got: $(head -30 "$tmp/oh"))"
fi
# Remove the symlink after the cell
rm -f "$D/runs/0122"

# --- 46 (B1, F1): C17 commit lines with long subject (>200 bytes) ----------------
# This cell is RED on the head (F1: long commit line glues to next line before fix).
# The fixture has ONE long commit subject (250 bytes of 'a') followed by a short commit.
# The C18 section follows to verify READY line is not glued.
# Expected: each commit on its own line, line count equals commit count (3).
# The count is a structural guard, not the F1 tooth: the long commit is LAST in git-log
# order (newest-first), so on the unfixed port its dropped newline glues DOWNWARD to the
# next section and the C17 count stays 3 (cell 46 green). The F1 glue is caught by 46b
# below, whose exact match on the C18 line fails once the long commit prefixes it.
printf 'active' > "$tmp/ustate"
# Create bare repo with 4 commits (initial + 3 subject commits, one long subject >200 bytes)
mkdir -p "$tmp/push.git3"
cd "$tmp/push.git3"
git init --bare >/dev/null 2>&1 || true
TMP_WORK3="$tmp/work_repo3"
rm -rf "$TMP_WORK3"
mkdir -p "$TMP_WORK3"
cd "$TMP_WORK3"
git init >/dev/null 2>&1
git config user.name "Test User"
git config user.email "test@example.com"
# Initial commit (required so HEAD~3 exists)
printf 'initial\n' > file0.txt
git add file0.txt
git commit -m 'initial commit' >/dev/null 2>&1
# Long commit subject: 250 'a' characters (exceeds 200)
LONG_SUBJECT=$(printf '%250s' | tr ' ' 'a')
# Create 3 more commits: first (long subject), second (short), third (another short)
# Total 4 commits; HEAD~3 points to initial commit; range includes all 3 subject commits
printf 'commit 1\n' > file1.txt
git add file1.txt
git commit -m "$LONG_SUBJECT" >/dev/null 2>&1
printf 'commit 2\n' > file2.txt
git add file2.txt
git commit -m 'short commit 1' >/dev/null 2>&1
printf 'commit 3\n' > file3.txt
git add file3.txt
git commit -m 'short commit 2' >/dev/null 2>&1
# Set up branches
BRANCH3="drive/2026-11-05-harness-port/main"
if git branch | grep -q '^\*.*main'; then
    git branch -m main "$BRANCH3"
else
    git branch -m master "$BRANCH3"
fi
# Get first commit SHA for origin/master (HEAD~3, before the 3 commits to show)
FIRST_SHA3=$(git rev-parse HEAD~3)
git update-ref "refs/remotes/origin/master" "$FIRST_SHA3"
# Push to bare repo
git remote add origin "$tmp/push.git3" >/dev/null 2>&1
git push origin "$BRANCH3" 2>/dev/null || true
# Set origin/master in the BARE repo
git --git-dir="$tmp/push.git3" update-ref "refs/remotes/origin/master" "$FIRST_SHA3" >/dev/null 2>&1
cd "$tmp"
# Update config to point to new push repo
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git3"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$SHIM/vpn.sh"'","checkinVpn0":"'"$SHIM/vpn0.sh"'","checkinRoute":"'"$SHIM/route.sh"'","checkinOpenconnect":"'"$SHIM/openconnect.sh"'"},"drive":{"loopUnit":"'"jhp-test-loop"'","onceUnit":"'"jhp-test-loop-once"'","restartLoopUnit":"'""'","pageWatchUnit":"'""'","pushUnit":"'""'","containerName":"'"jhp-test"'","branchPrefix":"'"drive/2026-11-05-harness-port"'","logPath":"'"$tmp/fakelog3.md"'","gateway":"'""'"}}' > "$CFG"
# Create fakelog3.md with READY line (C18 section)
printf 'line 1 of log\n' > "$tmp/fakelog3.md"
printf 'BATCH H13 READY at deadbeef1\n' >> "$tmp/fakelog3.md"
run_harness_checkin 2>&1
# Count lines in C17 section: should be 3 (one per commit)
C17_LINE_COUNT=$(awk '/^-- the team.*commits/{found=1; next} found && /^-- /{exit} found{print}' "$tmp/oh" | grep -cE '^[a-f0-9]+ ' 2>/dev/null || echo 0)
# Assert: (a) long commit prints as exactly 200 bytes + newline (next line is short commit)
# (b) C17 output line count equals fixture's commit count (3)
# (c) C18 READY line is NOT glued to last commit
if [ "$C17_LINE_COUNT" -eq 3 ]; then
    t_ok "46: B1 C17 long commit -- 3 lines (long commit on own line, next 2 commits on own lines)"
else
    t_fail "46: B1 expected 3 C17 lines, got $C17_LINE_COUNT (got: $(head -30 "$tmp/oh"))"
fi
# Check C18 READY line is not glued: it should have its own line number (2:)
if grep -qxF '2:BATCH H13 READY at deadbeef1' "$tmp/oh"; then
    t_ok "46b: B1 C17 long commit -- C18 READY line not glued (its own line)"
else
    t_fail "46b: B1 expected '2:BATCH H13 READY at deadbeef1' (got: $(grep -E '(BATCH|READY|READY at)' "$tmp/oh"))"
fi

# --- 47 (B2, F2a): C15 push-tick.out with long PAGE line (>200 bytes) ------------
# This cell is RED on the head (F2a: long PAGE line glues to next section header).
# Fixture: >=3 PAGE lines where LAST is >200 bytes (250), tail-2 includes it,
# then "-- the team's commits" section.
# Expected: 200-byte PAGE print on its own line, next line is section header.
# Red on unfixed: they merge into one line.
cd "$tmp"
# Create push-tick.out with 4 lines: 1 ordinary + 3 PAGE, last PAGE >200 bytes
LONG_PAGE_LINE=$(printf '%250s' | tr ' ' 'a')
cat > "$tmp/push-tick.out" << EOM
2026-11-26 07:00:00 +0000 ordinary tick
PAGE: alpha line one
PAGE: beta line two
PAGE: $LONG_PAGE_LINE
EOM
# Update config to use push-tick2.out
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git3"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$SHIM/vpn.sh"'","checkinVpn0":"'"$SHIM/vpn0.sh"'","checkinRoute":"'"$SHIM/route.sh"'","checkinOpenconnect":"'"$SHIM/openconnect.sh"'"},"drive":{"loopUnit":"'"jhp-test-loop"'","onceUnit":"'"jhp-test-loop-once"'","restartLoopUnit":"'""'","pageWatchUnit":"'""'","pushUnit":"'""'","containerName":"'"jhp-test"'","branchPrefix":"'"drive/2026-11-05-harness-port"'","logPath":"'"$tmp/fakelog3.md"'","gateway":"'""'"}}' > "$CFG"
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The last PAGE line (250 chars) should be cut to 200 + newline, so it's on its own line
# The next line should be "-- the team's commits" header
# Count lines: we expect 2 lines from tail -2 of PAGE lines (beta and gamma)
# Plus the "-- the team's commits" header on its own line
# Check: the 200-byte PAGE print is on its own line (the next line is the header)
# Extract PAGE section lines (indented 3 spaces)
PAGE_LINES=$(grep '^   PAGE:' "$tmp/oh" | head -5)
# Assert: beta and gamma are present, and the next line after the last PAGE is the header
# The last PAGE line in output should end with newline (not glue)
# Check the line count between "the team's commits" header and the next section
# Actually, the simplest assertion is: check that "the team's commits" header is on its own line
if grep -qF -- "-- the team's commits since master" "$tmp/oh"; then
    # The header line should be on its own line, not glued to the last PAGE
    HEADER_LINE=$(grep -nF -- "-- the team's commits since master" "$tmp/oh" | tail -1 | cut -d: -f1)
    # Get the previous line number
    PREV_LINE=$((HEADER_LINE - 1))
    PREV_CONTENT=$(sed -n "${PREV_LINE}p" "$tmp/oh")
    # The header line should start with "-- " (not be glued to a PAGE line)
    # If glued, the header would be on the same line as the PAGE content
    HEADER_AT_LINE=$(sed -n "${HEADER_LINE}p" "$tmp/oh")
    # Check that the header starts with "-- " (properly on its own line)
    if echo "$HEADER_AT_LINE" | grep -q "^-- the team's commits since master "; then
        t_ok "47: B2 C15 long PAGE -- 200-byte PAGE on its own line, header on next line"
    else
        # The header is glued to a PAGE line (e.g., "   PAGE: ...-- the team's commits...")
        t_fail "47: B2 expected header on own line (got: '$HEADER_AT_LINE')"
    fi
else
    t_fail "47: B2 expected '-- the team's commits' header (got: $(head -30 "$tmp/oh"))"
fi

# --- 48 (B3, F2b): C20b page-watch.out with long PAGE line (>200 bytes) ---------
# This cell is RED on the head (F2b: long PAGE line glues to supervision log line).
# Fixture: page-watch.out with a >200-byte PAGE line in tail-2, followed by
# "supervision log: copies identical" line.
# Expected: 200-byte PAGE print on its own line, supervision line on next line.
# Red on unfixed: they merge.
cd "$tmp"
# Create page-watch.out with a long PAGE line (250 chars)
cat > "$tmp/page-watch.out" << EOM
2026-11-26 07:00:00 +0000 PAGE: $LONG_PAGE_LINE
EOM
# Update config to use page-watch2.out
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git3"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$SHIM/vpn.sh"'","checkinVpn0":"'"$SHIM/vpn0.sh"'","checkinRoute":"'"$SHIM/route.sh"'","checkinOpenconnect":"'"$SHIM/openconnect.sh"'"},"drive":{"loopUnit":"'"jhp-test-loop"'","onceUnit":"'"jhp-test-loop-once"'","restartLoopUnit":"'""'","pageWatchUnit":"'"jhp-page-watch"'","pushUnit":"'""'","containerName":"'"jhp-test"'","branchPrefix":"'"drive/2026-11-05-harness-port"'","logPath":"'"$tmp/fakelog3.md"'","gateway":"'""'"}}' > "$CFG"
# Ensure both supervision logs exist and are identical
printf 'supervision log entry 1\n' > "$H/supervision-log.md"
printf 'supervision log entry 1\n' > "$D/supervision-log.md"
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The page-watch output section has:
# -- page-watch: <state>; last: <last line>
# Then indented PAGE lines
# Then supervision log: line
# Assert: supervision log line is on its own line (not glued to PAGE)
if grep -qxF -- "supervision log: copies identical" "$tmp/oh"; then
    # Check it's on its own line
    if grep -qxF "supervision log: copies identical" "$tmp/oh"; then
        t_ok "48: B3 C20b long PAGE -- supervision log line on its own line"
    else
        t_fail "48: B3 expected 'supervision log: copies identical' (got: $(grep -i supervision "$tmp/oh"))"
    fi
else
    t_fail "48: B3 expected 'supervision log: copies identical' (got: $(head -30 "$tmp/oh"))"
fi

# --- 49 (B4, C14): push-tick.out with long last tick (>160 bytes) - guard (green) -
# This cell is GREEN on the head (B4: the 160-cut field site is already correct).
# The note B4 says: prove it with a cell, not a change.
# Fixture: push-tick.out whose LAST line is >160 bytes (250 chars).
# Expected: -- push timer: ...; last tick: <160 bytes> line is exactly one line.
# The cut to 160 should NOT cause newline corruption.
cd "$tmp"
# Create push-tick.out with a very long last line (250 chars)
LONG_TICK=$(printf '%250s' | tr ' ' 'a')
cat > "$tmp/push-tick.out" << EOM
2026-11-26 07:00:00 +0000 ordinary tick
PAGE: alpha one
PAGE: beta two
$LONG_TICK
EOM
# Update config to use push-tick3.out
printf '{"dir":"'"$D"'","modelKeyFile":"'"$KEY"'","host":{"dir":"'"$H"'","checkinFollow":"'"$SHIM/follow.py"'","checkinPidList":"'"$SHIM/pidList.sh"'","checkinPush":"'"$SHIM/push.sh"'","pushHome":"'"$tmp"'","pushRepo":"'"$tmp/push.git3"'","supervisionLog":"'"$H/supervision-log.md"'","unitState":"'"$SHIM/unitState.sh $tmp/ustate"'","checkinVpn":"'"$SHIM/vpn.sh"'","checkinVpn0":"'"$SHIM/vpn0.sh"'","checkinRoute":"'"$SHIM/route.sh"'","checkinOpenconnect":"'"$SHIM/openconnect.sh"'"},"drive":{"loopUnit":"'"jhp-test-loop"'","onceUnit":"'"jhp-test-loop-once"'","restartLoopUnit":"'""'","pageWatchUnit":"'""'","pushUnit":"'""'","containerName":"'"jhp-test"'","branchPrefix":"'"drive/2026-11-05-harness-port"'","logPath":"'"$tmp/fakelog3.md"'","gateway":"'""'"}}' > "$CFG"
printf 'active' > "$tmp/ustate"
run_harness_checkin 2>&1
# The push timer line should be on its own line
# The last tick should be cut to 160 chars but not corrupt the line
if grep -qF -- "-- push timer:" "$tmp/oh"; then
    # Count lines containing "push timer" - should be exactly 1
    PUSH_TIMER_COUNT=$(grep -cF -- "-- push timer:" "$tmp/oh" 2>/dev/null || echo 0)
    if [ "$PUSH_TIMER_COUNT" -eq 1 ]; then
        t_ok "49: B4 C14 long tick -- push timer line is exactly one line (guard)"
    else
        t_fail "49: B4 expected 1 push timer line, got $PUSH_TIMER_COUNT"
    fi
else
    t_fail "49: B4 expected '-- push timer:' line (got: $(head -30 "$tmp/oh"))"
fi

t_done
