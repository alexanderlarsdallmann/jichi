#!/bin/sh
# smoke: D-04/M979 - a failed in-place write reports the restore attempt.
#
# Requires a FAULT=1 binary (`make clean && make FAULT=1`); SKIPS otherwise.
# JICHI_FAULT_WRITE_AFTER=0 makes EVERY write fault (monotone threshold 0),
# which is the ENOSPC production case: the primary write truncates the file
# and fails, and the D-04 restore -- re-entering the same fault site -- fails
# too. So the driver asserts the reachable property: the tool reports an error
# naming the restore attempt ("restore also failed"), which pre-D-04 was a
# plain "could not write" -- proving the restore path RAN -- and there is no
# false success (the replaced content did not land). The positive "restore
# lands the original bytes" case (a transient primary failure the disk
# recovers from) needs a fault knob the monotone model cannot express; it is
# check 4, armed with the one-shot JICHI_FAULT_WRITE_ONCE_AT (the M269
# NET_MID_AT pattern), which faults only the primary write.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- a far more reliable probe
# than inferring it from injected behaviour.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Create original file
printf 'original content\n' > "$ws/target.txt"

# Mock wire: successful write that should be restored
cat > "$tmp/u.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"target.txt","content":"replaced content\n"}
rule
  text write done
EOF
mm_start "$tmp/u.mm" "$tmp/cap" 4

cat > "$tmp/c.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

cat > "$tmp/restore.pd" <<'EOF'
expect "] " 15
delay 300
send "replace the content of target.txt\r"
expect "] " 30
delay 400
send "/exit\r"
waitexit 10
assertexit 0
EOF

# Run with fault injection
(cd "$ws" && env JICHI_FAULT_WRITE_AFTER=0 "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/restore.log" "$tmp/restore.pd" -- \
    "$BIN" --config "$tmp/c.json" --no-route); rc=$?
mm_stop

# --- 1: the flow ran end to end ------------------------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "write_file completed"
else
    t_fail "PTY script rc=$rc (see transcript above)"
fi

# --- 2: write_file should report the D-04 restore attempt ----------------------
# Under the monotone fault the restore also fails, so the message is
# "restore also failed" (pre-D-04 a failed write said plain "could not write").
if grep -q "restore also failed" "$tmp/restore.log" 2>/dev/null; then
    t_ok "write_file reported the restore attempt (restore also failed)"
else
    t_fail "write_file did not report the D-04 restore attempt (check restore.log)"
fi

# --- 3: no false success -- the replaced content must not have landed ---------
if grep -q "replaced content" "$ws/target.txt" 2>/dev/null; then
    t_fail "write_file falsely reported success (replaced content landed)"
else
    t_ok "no false success (replaced content did not land)"
fi

# --- 4: the POSITIVE case -- a transient primary failure the disk recovers from
# JICHI_FAULT_WRITE_ONCE_AT=0 faults only the FIRST in-place write (the primary),
# so the D-04 restore -- the SECOND write -- lands the original bytes. (docs/
# DEFERRED.md's D-04 row named this property as unexpressible under the
# monotone AFTER knob; the one-shot knob closed it.)
ws4=$(smoke_tmp)
printf 'original content\n' > "$ws4/target.txt"
# A FRESH mockmodel on a NEW port: the arm-1 c.json points at the stopped one,
# so regenerate it against the new $MM_PORT.
mm_start "$tmp/u.mm" "$tmp/cap4" 4
cat > "$tmp/c4.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
cat > "$tmp/restore4.pd" <<'EOF'
expect "] " 15
delay 300
send "replace the content of target.txt\r"
expect "] " 30
delay 400
send "/exit\r"
waitexit 10
assertexit 0
EOF
(cd "$ws4" && env JICHI_FAULT_WRITE_ONCE_AT=0 "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/restore4.log" "$tmp/restore4.pd" -- \
    "$BIN" --config "$tmp/c4.json" --no-route); rc4=$?
mm_stop
if [ "$rc4" -eq 0 ] && grep -q "original restored" "$tmp/restore4.log" 2>/dev/null \
   && grep -q "original content" "$ws4/target.txt" 2>/dev/null; then
    t_ok "one-shot fault: original restored and bytes intact"
else
    t_fail "one-shot fault: rc=$rc4, restored=$(grep -c 'original restored' "$tmp/restore4.log" 2>/dev/null), content=$(cat "$ws4/target.txt" 2>/dev/null)"
fi

t_done
