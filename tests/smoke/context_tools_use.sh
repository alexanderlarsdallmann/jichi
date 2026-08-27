#!/bin/sh
# smoke: `context tools` joined to telemetry -- paid-for vs used (M314).
#
# The join answers "you pay N tokens for a tool you called zero times". The
# risk is not the arithmetic, it is the ways a zero can lie, so most of these
# checks are about that:
#
#   1. NO LOG -> a stated absence, and NO calls column. Telemetry is off by
#      default, so a column of zeroes would tell most users they use none of
#      their tools. This is the check that matters.
#   2. a log for THIS workspace -> the column appears and the counts match.
#   3. a tool absent from the log reads 0 and is counted in the unused total.
#   4. the evidence base (turns, tool calls) is printed, so a one-turn log can
#      be discounted by the reader rather than believed.
#   5. a log for a DIFFERENT workspace is NOT this workspace's evidence -- it
#      must fall back to the stated-absence case, not report zeroes.
#   6. the report never advises: no "remove", "disable" or "delete".
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"contextLimit":32000,"maxRetries":0}
EOF

jc() {
    (cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
        --no-lite context tools < /dev/null 2>/dev/null)
}

# --- 1: no log at all ---------------------------------------------------------
rm -rf "$HOME/.jichi.d/telemetry"
none=$(jc)
if printf '%s\n' "$none" | grep -q "no telemetry log for this workspace" && \
   ! printf '%s\n' "$none" | grep -q "core  calls"; then
    t_ok "no log: a stated absence, and no calls column"
else
    t_fail "no-log case did not state the absence, or printed a calls column"
fi

# The canonical workspace root as jichi stamps it (the driver's $ws may be a
# symlinked /tmp on some systems, and the ws filter compares canonical paths).
wsc=$(cd "$ws" && pwd -P)

# A synthetic log: two tools called, everything else absent. Real event shape,
# minus the fields this report does not read.
mklog() {
    mkdir -p "$HOME/.jichi.d/telemetry"
    cat > "$HOME/.jichi.d/telemetry/probe.jsonl" <<EOF
{"v":1,"ts":1786000000,"sid":"s1","ws":"$1","seq":1,"event":"turn_start","depth":0,"turn":1}
{"v":1,"ts":1786000001,"sid":"s1","ws":"$1","seq":2,"event":"tool_call","depth":0,"turn":1,"name":"read_file","ok":true,"duration_ms":1.0,"output_bytes":10}
{"v":1,"ts":1786000002,"sid":"s1","ws":"$1","seq":3,"event":"tool_call","depth":0,"turn":1,"name":"read_file","ok":true,"duration_ms":1.0,"output_bytes":10}
{"v":1,"ts":1786000003,"sid":"s1","ws":"$1","seq":4,"event":"tool_call","depth":0,"turn":1,"name":"read_file","ok":true,"duration_ms":1.0,"output_bytes":10}
{"v":1,"ts":1786000004,"sid":"s1","ws":"$1","seq":5,"event":"tool_call","depth":0,"turn":1,"name":"write_file","ok":true,"duration_ms":1.0,"output_bytes":10}
EOF
}

# --- 2 + 3 + 4: this workspace ------------------------------------------------
mklog "$wsc"
mine=$(jc)

rf=$(printf '%s\n' "$mine" | sed -n 's/^ *[0-9]*  *[0-9]*%  *[0-9]*%  *\**  *\([0-9]*\)  *read_file$/\1/p')
wf=$(printf '%s\n' "$mine" | sed -n 's/^ *[0-9]*  *[0-9]*%  *[0-9]*%  *\**  *\([0-9]*\)  *write_file$/\1/p')
if [ "$rf" = "3" ] && [ "$wf" = "1" ]; then
    t_ok "calls column joined from the log (read_file 3, write_file 1)"
else
    t_fail "call counts wrong: read_file '$rf' (want 3), write_file '$wf' (want 1)"
fi

ls_calls=$(printf '%s\n' "$mine" | sed -n 's/^ *[0-9]*  *[0-9]*%  *[0-9]*%  *\**  *\([0-9]*\)  *list_files$/\1/p')
unused=$(printf '%s\n' "$mine" | \
         sed -n 's/^\([0-9]*\) advertised tools were never called.*/\1/p')
if [ "$ls_calls" = "0" ] && [ -n "$unused" ] && [ "$unused" -ge 2 ]; then
    t_ok "a tool absent from the log reads 0 and is counted ($unused unused)"
else
    t_fail "absent tool read '$ls_calls' (want 0), unused count '$unused'"
fi

if printf '%s\n' "$mine" | grep -q "1 turn, 4 tool calls"; then
    t_ok "the evidence base is stated (1 turn, 4 tool calls)"
else
    t_fail "evidence base missing or wrong: $(printf '%s\n' "$mine" | grep '^Use:')"
fi

# --- 5: another project's log is not evidence here ----------------------------
mklog "/some/other/project"
other=$(jc)
if printf '%s\n' "$other" | grep -q "no telemetry log for this workspace" && \
   ! printf '%s\n' "$other" | grep -q "core  calls"; then
    t_ok "a foreign workspace's log is treated as no data, not as zeroes"
else
    t_fail "a log for another workspace was counted as this one's evidence"
fi

# --- 6: it describes, it does not advise -------------------------------------
mklog "$wsc"
advice=$(jc | grep -icE "remove |disable |delete |you should" || true)
if [ "$advice" = "0" ]; then
    t_ok "the report describes without advising (advice is doctor's job)"
else
    t_fail "the report gave $advice line(s) of advice"
fi

t_done
