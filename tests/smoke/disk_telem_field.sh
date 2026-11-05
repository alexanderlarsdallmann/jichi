#!/bin/sh
# M805 (P4a): every tool_call telemetry row must carry a free-disk measurement --
# disk_free_mb + disk_free_files for the filesystem the workspace lives on --
# sampled the moment the tool finished. This is the SIGNAL that the later disk
# guard (P4b) will threshold; before it can guard, the corpus has to record where
# the floors actually are. The field is a measurement, not a fence: nothing here
# refuses to run. A model makes one shell tool call; then read the tool_call row
# out of the telemetry log and assert the two numbers are present and real (a
# positive MiB free -- the ZFS-fragment bug in jc_disk_free_mb would report 0).
# The unknown-vs-full contract of jc_disk_space itself is covered by the unit
# test test_disk_space; here we prove the field reaches the wire.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
JQ="$SMOKE_TOOLS/jsonq"
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "\"role\":\"tool\""
  text DONE
rule
  tool run_terminal_command {"command":"echo hi"}
EOF
mm_start "$tmp/replies.mm" "$tmp/cap" 2
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q --auto \
    --log "$tmp/telemetry.jsonl" --log-level full -p "go" < /dev/null > "$tmp/out.txt" 2>&1 )
mm_stop

# Isolate the tool_call row(s). grep is enough to select; jsonq reads the number.
grep '"event":"tool_call"' "$tmp/telemetry.jsonl" 2>/dev/null > "$tmp/tc.jsonl"
mb=$("$JQ" -l '.disk_free_mb' "$tmp/tc.jsonl" 2>/dev/null | head -1)
files=$("$JQ" -l '.disk_free_files' "$tmp/tc.jsonl" 2>/dev/null | head -1)
# A positive MiB reading: awk so a float compares correctly, and an empty/absent
# value is treated as 0 (fails), never as a pass.
mb_pos=$(awk -v v="$mb" 'BEGIN{print (v+0 > 0) ? 1 : 0}')
files_ok=$(awk -v v="$files" 'BEGIN{print (v != "" && v+0 >= 0) ? 1 : 0}')
echo "# tool_call rows=$(grep -c . "$tmp/tc.jsonl"); disk_free_mb=$mb (positive=$mb_pos); disk_free_files=$files (ok=$files_ok)"
if [ -n "${E1_RES:-}" ]; then
    mkdir -p "$E1_RES"; cp -p "$tmp/telemetry.jsonl" "$E1_RES/" 2>/dev/null
fi

[ -s "$tmp/tc.jsonl" ] && t_ok "telemetry recorded a tool_call row (the instrument has something to read)" \
    || t_fail "telemetry has no tool_call row"
[ -n "$mb" ] && t_ok "the tool_call row carries disk_free_mb" \
    || t_fail "the tool_call row has no disk_free_mb field"
[ "$mb_pos" = 1 ] && t_ok "disk_free_mb is a positive measurement (${mb} MiB) -- not the ZFS-fragment 0" \
    || t_fail "disk_free_mb is not positive ($mb) -- measurement collapsed to 0"
[ "$files_ok" = 1 ] && t_ok "the tool_call row carries disk_free_files (${files})" \
    || t_fail "the tool_call row has no valid disk_free_files field ($files)"
t_done
