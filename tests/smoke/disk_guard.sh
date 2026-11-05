#!/bin/sh
# M813 (P4b): the disk-space FENCE stops a run before a filling disk can corrupt the
# records. It is a fence (on always, ungated by an envelope), throttled by time, and
# UNKNOWN never stops (that half is unit-tested in test_diskguard). Here, end to end:
# with an impossibly high floor the guard trips on the first iteration and the run
# stops before it does its work; with a modest floor the same run completes normally.
# Before M813 the high floor was ignored and the run completed -- the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text ALLDONE
rule
  tool run_terminal_command {"command":"echo hi"}
EOF

run_with_floor() {
    floor="$1"; out="$2"
    mm_start "$tmp/replies.mm" "$tmp/cap.$floor" 3
    cat > "$tmp/cfg.$floor.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,"diskFloorMb":$floor}
EOF
    ( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cfg.$floor.json" -q --no-session --auto \
        -p "do the thing" < /dev/null > "$out" 2>&1 )
    mm_stop
}

# --- control: a modest 1 MiB floor does not trip on a filesystem with room ------
run_with_floor 1 "$tmp/ok.txt"
grep -q ALLDONE "$tmp/ok.txt" 2>/dev/null \
    && t_ok "control: with a 1 MiB floor the run completes normally (the disk has room)" \
    || t_fail "control: the run did not complete even with a low floor -- $(tr '\n' ' ' < "$tmp/ok.txt" | head_bytes 160)"

# --- the fence: an impossibly high floor stops the run --------------------------
run_with_floor 999999999 "$tmp/stop.txt"
echo "# high-floor run output: $(tr '\n' ' ' < "$tmp/stop.txt" | head_bytes 200)"
! grep -q ALLDONE "$tmp/stop.txt" 2>/dev/null \
    && t_ok "the fence stopped the run before it finished (no final answer)" \
    || t_fail "the fence did NOT stop the run -- it reached ALLDONE despite an impossible floor"
grep -qi 'stopping' "$tmp/stop.txt" 2>/dev/null \
    && t_ok "the run says it is stopping (the operator is told)" \
    || t_fail "no 'stopping' message -- the halt was silent"
grep -qi 'disk floor' "$tmp/stop.txt" 2>/dev/null \
    && t_ok "the message names the disk floor (not a Ctrl-C or a stall)" \
    || t_fail "the message does not name the disk floor: $(tr '\n' ' ' < "$tmp/stop.txt" | head_bytes 160)"
t_done
