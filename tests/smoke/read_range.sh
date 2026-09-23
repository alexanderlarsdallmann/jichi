#!/bin/sh
# smoke: telemetry records the line range a read EXECUTED (M715, plan D6 item 3).
#
# THE GAP. A metrics-tier `tool_call` event summarises its arguments as `args`,
# and for read_file that summary is the path alone. It has to stay that way: it
# is the same string the TUI prints and a screen reader speaks
# (jc_tool_arg_summary, M571), so it cannot grow a range without changing what
# an operator hears. With only the path, paging through a file (offset 1, 201,
# 401 ...) and reading the same lines twice were the same event, and ordinary
# telemetry could not tell paging from re-reading -- tests/measure/
# success_repeats.py had to leave read_file out of its metrics-tier count.
#
# REPORTED BY THE TOOL, NOT PARSED BESIDE IT. jc_tool_execute repairs malformed
# arguments (M148) and unwraps a self-named wrapper (M172) before the tool sees
# them, so a second parser at the telemetry site would read a different object
# from the one that ran -- CLAUDE.md's "a preview must read every argument
# exactly as the executor will". read_file puts the range it read into its
# result (has_range / range_offset / range_limit, as exit_status carries a
# command's exit), and the event copies those two numbers. Check 3 is the one a
# beside-the-call parser fails: the range is inside the wrapper.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
printf 'l1\nl2\nl3\nl4\nl5\nl6\n' > "$ws/f.txt"

# One call per request, in order; `count N` matches the N-th request only.
cat > "$tmp/reads.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"f.txt","offset":3,"limit":2}
rule
  count 2
  tool read_file {"path":"f.txt","offset":"3","limit":"2.0"}
rule
  count 3
  tool read_file {"read_file":{"path":"f.txt","offset":5,"limit":1}}
rule
  count 4
  tool read_file {"path":"f.txt"}
rule
  count 5
  tool list_files {"path":"."}
rule
  text done
EOF
mm_start "$tmp/reads.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q --no-session \
    --log "$tmp/telem.jsonl" --log-level metrics -p "read it" < /dev/null) \
    > /dev/null 2>&1
mm_stop

grep '"event":"tool_call"' "$tmp/telem.jsonl" 2>/dev/null > "$tmp/calls"
ev() { sed -n "${1}p" "$tmp/calls"; }
field() { printf '%s' "$1" | "$JQ" ".$2" 2>/dev/null; }
range_is() {   # EVENT OFFSET LIMIT -- a read_file event carrying exactly this range
    [ "$(field "$1" name)" = "read_file" ] &&
    [ "$(field "$1" offset)" = "$2" ] && [ "$(field "$1" limit)" = "$3" ]
}

# --- 1: a paged read records its range ------------------------------------------
if range_is "$(ev 1)" 3 2; then
    t_ok "a paged read's tool_call event carries offset 3, limit 2"
else
    t_fail "no offset 3 / limit 2 on the paged read ($(wc -l < "$tmp/calls") tool_call events): $(ev 1 | head_bytes 240)"
fi

# --- 2: string-typed numbers are recorded as the executor read them (M168) ------
if range_is "$(ev 2)" 3 2; then
    t_ok "\"offset\":\"3\",\"limit\":\"2.0\" is recorded as 3 and 2, as the tool read it"
else
    t_fail "string-typed range not recorded as the executor's 3/2: $(ev 2 | head_bytes 240)"
fi

# --- 3: a self-named wrapper -- the range that RAN, after the unwrap -------------
if grep '"event":"args_repair"' "$tmp/telem.jsonl" 2>/dev/null | grep -q '"unwrap"' &&
   range_is "$(ev 3)" 5 1; then
    t_ok "a wrapped call records the unwrapped range the tool executed (5, 1)"
else
    t_fail "the wrapped call's executed range is not recorded (or no unwrap ran): $(ev 3 | head_bytes 240)"
fi

# --- 4: no range given -- the defaults the tool applied --------------------------
if range_is "$(ev 4)" 1 0; then
    t_ok "a whole-file read records the executed defaults: offset 1, limit 0 (to the end)"
else
    t_fail "a read with no range does not record offset 1 / limit 0: $(ev 4 | head_bytes 240)"
fi

# --- 5: a tool that has no range records none ------------------------------------
e5=$(ev 5)
if [ "$(field "$e5" name)" = "list_files" ] &&
   ! printf '%s' "$e5" | grep -q '"offset"' && ! printf '%s' "$e5" | grep -q '"limit"'; then
    t_ok "list_files' event carries no offset or limit"
else
    t_fail "a rangeless tool's event carries range fields (or is missing): $(printf '%s' "$e5" | head_bytes 240)"
fi

# --- 6: what an operator sees and hears is unchanged -- the summary is the path --
if [ "$(field "$(ev 1)" args)" = "f.txt" ]; then
    t_ok "the read's args summary is still the path alone (TUI and speech unchanged)"
else
    t_fail "the args summary changed: $(field "$(ev 1)" args | head_bytes 120)"
fi

t_done
