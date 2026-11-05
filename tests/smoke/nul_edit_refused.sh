#!/bin/sh
# smoke: D-01/M978 - edit_file refuses files with NUL bytes.
#
# Flow: a mock model reads a file with a NUL byte, then tries to edit it.
# The edit should fail with a NUL refusal message, and the file's content
# should be unchanged (mtime/size preserved).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Create a file with a NUL byte (before the run - model can't see the tail),
# and keep a copy to compare bytes against afterwards (no stat: BSD and GNU
# spell the mtime probe differently, and byte-identity is the stronger claim).
printf 'abc\0def' > "$ws/nul.txt"
cp "$ws/nul.txt" "$tmp/nul.orig"

# Mock wire: read_file sees the file (satisfies the guard), edit_file should refuse
cat > "$tmp/u.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"nul.txt"}
rule
  count 2
  tool edit_file {"path":"nul.txt","old_string":"abc","new_string":"XYZ"}
EOF
mm_start "$tmp/u.mm" "$tmp/cap" 4

cat > "$tmp/c.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

cat > "$tmp/nul.pd" <<'EOF'
expect "] " 15
delay 300
send "read the file and replace abc with XYZ\r"
# The model reads first
expect "] " 30
delay 400
# The edit should be refused with NUL message
send "\r"
expect "] " 30
delay 400
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/nul.log" "$tmp/nul.pd" -- \
    "$BIN" --config "$tmp/c.json" --no-route); rc=$?
mm_stop

# --- 1: the flow ran end to end ------------------------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "read_file + edit_file completed"
else
    t_fail "PTY script rc=$rc (see transcript above)"
fi

# --- 2: edit_file should refuse with NUL message -------------------------------
# The phrase must be one ONLY the edit refusal prints: read_file's D-01 marker
# also says "NUL bytes" in the same log (supervisor 04:45: with edit_file's
# guard disabled, a grep for "NUL bytes" stays green because the read marker
# matches -- the tooth must name the edit's own words, "readable as").
if grep -q "readable as" "$tmp/nul.log" 2>/dev/null; then
    t_ok "edit_file refused due to NUL bytes"
else
    t_fail "edit_file did not refuse NUL file (check nul.log)"
fi

# --- 3: file should be unchanged, byte for byte --------------------------------
if cmp -s "$tmp/nul.orig" "$ws/nul.txt"; then
    t_ok "file unchanged after failed edit"
else
    t_fail "file was modified (no longer byte-identical to the pre-run copy)"
fi

t_done
