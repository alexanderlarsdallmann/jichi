#!/bin/sh
# smoke: a parallel child's stderr lines carry the UTC time and the child's own id, and
# each line is whole (M864, the demos drive's §11 (b)).
#
# THE CASE. Run 0090's stderr.log, 372 lines, mixed the lead and every worker with no
# agent id and no time on a line. The drive's supervisor attributed N41's 29 "could not
# preserve" warnings to workers only through a PID inside the paths they printed, and
# read a stale stderr tail as the gateway outage's current state, because the lines
# carried no time. A child's line was also written in three pieces (the tag, the
# message, the newline), so two children could interleave inside one line.
#
# THE FIX (DECISIONS M864; §11 (b), the operator's order of 2026-10-02):
#   - in a spawn_parallel child, every log line begins with the UTC time and the
#     child's id -- the string its telemetry rows carry as task_run;
#   - every log line is formatted first and written with one call;
#   - the lead's own lines are unchanged.
#
#   1  a child's warning carries "<UTC time> <its id> [jichi warn]"
#   2  the second batch's child carries its own, different id
#   3  each id is the task_run its telemetry rows carry
#   4  the time is the run's (UTC date of the run)
#   5  CONTROL: the lead's own warning is unchanged, "[jichi warn] ..." at the line start
#   6  a line longer than the 4 KB buffer (-v's request dump) is still written whole:
#      C89 has no va_copy, and the long path walks the arguments again from va_start
#
# Markers: the lines are jichi's own stderr; the ids are compared with jichi's own
# telemetry.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
CAP=$(smoke_tmp)
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "CHILD2"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  match "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILD2 keep checking","write":true}]}
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILD1 keep checking","write":true}]}
rule
  match "CHILD"
  tool run_terminal_command {"command":"echo checking-$$"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "maxSubagentIters":2,"continueCutReply":"whenever"}
EOF
TEL="$CAP/tel.jsonl"
d0=$(date -u +%Y-%m-%d)
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" --auto \
    --log "$TEL" --log-level full -p "LEADX two batches" < /dev/null > "$CAP/out" 2> "$CAP/err" )
d1=$(date -u +%Y-%m-%d)
mm_stop

pat='^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z [^ ]* \[jichi warn\] hit max tool iterations'
l1=$(grep -E "$pat" "$CAP/err" 2>/dev/null | grep -E -- '-b1-t1 ' | head -1)
l2=$(grep -E "$pat" "$CAP/err" 2>/dev/null | grep -E -- '-b2-t1 ' | head -1)
id1=$(printf '%s' "$l1" | cut -d' ' -f2)
id2=$(printf '%s' "$l2" | cut -d' ' -f2)
if [ -n "$l1" ]; then
    t_ok "a child's warning carries the UTC time and its id ($id1)"
else
    t_fail "no child warning with a time and an id (red before M864): $(grep 'hit max tool' "$CAP/err" | head -2)"
fi
if [ -n "$l2" ] && [ "$id1" != "$id2" ]; then
    t_ok "the second batch's child carries its own id ($id2)"
else
    t_fail "the second batch's child has no line of its own, or the same id: '$id1' '$id2'"
fi
if [ -n "$id1" ] && [ -n "$id2" ] &&
   grep -q -F "\"task_run\":\"$id1\"" "$TEL" && grep -q -F "\"task_run\":\"$id2\"" "$TEL"; then
    t_ok "each id is the task_run its telemetry rows carry"
else
    t_fail "the stderr ids are not the telemetry's task_run values"
fi
day=$(printf '%s' "$l1" | cut -c1-10)
if [ -n "$day" ] && { [ "$day" = "$d0" ] || [ "$day" = "$d1" ]; }; then
    t_ok "the time is the run's (UTC $day)"
else
    t_fail "the line's date '$day' is not the run's ($d0..$d1)"
fi
if grep -q '^\[jichi warn\] config: continueCutReply' "$CAP/err"; then
    t_ok "CONTROL: the lead's own warning is unchanged"
else
    t_fail "the lead's config warning changed: $(grep 'continueCutReply' "$CAP/err" | head -1)"
fi

# --- 6: a long line, whole -------------------------------------------------------------
CAP=$(smoke_tmp)
printf 'wire openai\nrule\n  match "LEADV"\n  text V-DONE\n' > "$CAP/r.mm"
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
printf '{"models":[{"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:%s/v1","apiKey":"x","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}\n' "$MM_PORT" > "$CAP/cfg.json"
( cd "$WS" && with_deadline 60 "$BIN" --config "$CAP/cfg.json" -v -p "LEADV hello" < /dev/null > "$CAP/out" 2> "$CAP/err" )
mm_stop
line=$(grep '^\[jichi debug\] request: ' "$CAP/err" 2>/dev/null | head -1)
case "$line" in
    *'"tool_choice":"auto"}') whole=1 ;;
    *) whole=0 ;;
esac
if [ "${#line}" -gt 4096 ] && [ "$whole" = 1 ]; then
    t_ok "a line longer than the buffer is written whole (${#line} characters)"
else
    t_fail "the long request line was cut: ${#line} characters, ends '$(printf '%s' "$line" | tail -c 30)'"
fi

t_done
