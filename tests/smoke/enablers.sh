#!/bin/sh
# smoke: the M165 web-front-end enablers. One real mock turn persists a
# session; then: `ls --output json` lists it with the machine fields;
# `export --output json` projects the transcript with the assistant's
# tool_call (name + arguments as a PARSED OBJECT, asserted by pathing
# into it with jsonq) and the tool result linked by tool_call_id; jsonl
# carries NO heartbeat without --heartbeat, and `--heartbeat 1` against a
# slow model (mockmodel `delay 2500`) emits parseable heartbeat events.
# (Port of tests/e2e/enablers.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
echo "hello" > "$ws/note.txt"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text done
rule
  tool read_file {"path":"note.txt"}
EOF

# --- seed: one real turn, session persisted ---------------------------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --auto -p "read the note" < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "the seed turn completed and persisted a session"
else
    t_fail "seed run rc=$rc"
fi

# --- ls --output json ---------------------------------------------------------
(cd "$ws" && "$BIN" --config "$tmp/config.json" ls --all --output json \
    < /dev/null > "$tmp/ls.json" 2>/dev/null)
sid=$("$JQ" '.sessions[0].id' "$tmp/ls.json" 2>/dev/null)
nmsgs=$("$JQ" '.sessions[0].nmsgs' "$tmp/ls.json" 2>/dev/null)
wsf=$("$JQ" '.sessions[0].workspace' "$tmp/ls.json" 2>/dev/null)
if [ "$("$JQ" '.v' "$tmp/ls.json" 2>/dev/null)" = "1" ] \
   && [ "${#sid}" -ge 8 ] && [ "${nmsgs:-0}" -ge 2 ] && [ -n "$wsf" ]; then
    t_ok "ls json: v=1, id/nmsgs/workspace present"
else
    t_fail "ls json wrong: id='$sid' nmsgs='$nmsgs' ws='$wsf'"
fi

# --- export --output json -----------------------------------------------------
(cd "$ws" && "$BIN" --config "$tmp/config.json" export "$sid" \
    --output json < /dev/null > "$tmp/ex.json" 2>/dev/null)
tc_i=-1; tool_i=-1; i=0
roles=""
while [ $i -lt 32 ]; do
    role=$("$JQ" ".messages[$i].role" "$tmp/ex.json" 2>/dev/null) || break
    roles="$roles $role"
    if [ "$role" = "assistant" ] \
       && "$JQ" -q ".messages[$i].tool_calls[0]" "$tmp/ex.json"; then
        tc_i=$i
    fi
    if [ "$role" = "tool" ] && [ $tool_i -lt 0 ]; then
        tool_i=$i
    fi
    i=$((i + 1))
done
case "$roles" in
    *user*assistant*) ok_roles=1 ;;
    *) ok_roles=0 ;;
esac
if [ "$("$JQ" '.v' "$tmp/ex.json" 2>/dev/null)" = "1" ] \
   && [ $ok_roles -eq 1 ] && [ $tool_i -ge 0 ]; then
    t_ok "export json: v=1 with user/assistant/tool messages"
else
    t_fail "export json roles wrong:$roles"
fi

if [ $tc_i -ge 0 ] \
   && [ "$("$JQ" ".messages[$tc_i].tool_calls[0].name" "$tmp/ex.json")" \
        = "read_file" ] \
   && "$JQ" -q -t object ".messages[$tc_i].tool_calls[0].arguments" \
        "$tmp/ex.json" \
   && [ "$("$JQ" ".messages[$tc_i].tool_calls[0].arguments.path" \
        "$tmp/ex.json")" = "note.txt" ]; then
    t_ok "export json: tool_call carries name + PARSED arguments object"
else
    t_fail "tool_call projection wrong (msg index $tc_i)"
fi

tcid=$("$JQ" ".messages[$tool_i].tool_call_id" "$tmp/ex.json" 2>/dev/null)
if [ -n "$tcid" ] && [ "$tcid" != "null" ]; then
    t_ok "export json: the tool result is linked by tool_call_id"
else
    t_fail "tool result lacks tool_call_id"
fi

# --- heartbeat gating: none without the flag ---------------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --output jsonl -p "read the note" \
    < /dev/null > "$tmp/nohb.jsonl" 2>/dev/null)
if ! grep -q '"type":"heartbeat"' "$tmp/nohb.jsonl"; then
    t_ok "no heartbeat event without --heartbeat (contract intact)"
else
    t_fail "a heartbeat appeared without the flag"
fi
mm_stop

# --- heartbeat on a slow model ------------------------------------------------
mkdir -p "$tmp/slow"
cat > "$tmp/slow/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  delay 2500
  text done
rule
  delay 2500
  tool read_file {"path":"note.txt"}
EOF
mm_start "$tmp/slow/replies.mm" "$tmp/slow"
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    --no-session --output jsonl --heartbeat 1 -p "read the note" \
    < /dev/null > "$tmp/hb.jsonl" 2>/dev/null)
mm_stop
grep '"type":"heartbeat"' "$tmp/hb.jsonl" > "$tmp/hb.lines" || :
if [ -s "$tmp/hb.lines" ]; then
    t_ok "--heartbeat 1 emitted $(grep -c . "$tmp/hb.lines") event(s)"
else
    t_fail "no heartbeat events on a slow model"
fi
if head -1 "$tmp/hb.lines" | "$JQ" -q '.elapsed' 2>/dev/null; then
    t_ok "heartbeat events parse and carry elapsed"
else
    t_fail "heartbeat event shape wrong"
fi

t_done
