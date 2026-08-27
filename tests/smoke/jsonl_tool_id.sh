#!/bin/sh
# smoke: the jsonl stream carries the provider's tool-call id (M442).
#
# THE DEFECT. `tool_call` and `tool_result` events carried the tool NAME and nothing
# else identifying the call. A round with two calls to the SAME tool therefore produced
# two indistinguishable `tool_call` events and two indistinguishable `tool_result`
# events, and a supervisor building a timeline had to pair them by order -- which is
# precisely the assumption a reordered or concurrent result set breaks.
#
# The id already existed: it is the provider's `tool_calls[].id`, which the agent loop
# holds as `call_id` and puts in the history so the model can pair them. Only the
# machine surface could not see it.
#
# WHY THE FIXTURE USES ONE TOOL TWICE. Two DIFFERENT tools can be paired by name, so a
# driver using read_file and list_files would pass without any id at all. The defect
# only exists when the names collide, so the fixture makes them collide.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Two read_file calls in ONE round, on different paths.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text IDS_DONE
rule
  tool read_file {"path":"a.txt"}
  tool read_file {"path":"b.txt"}
EOF

echo "alpha" > "$ws/a.txt"
echo "beta"  > "$ws/b.txt"

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --output jsonl \
    -p "read both" < /dev/null > "$tmp/out.jsonl" 2>/dev/null); rc=$?
mm_stop

# --- 1: the fixture produced two same-named calls in one round ------------------
# The extraction floor. With one call, or with two differently-named ones, every check
# below would pass without proving anything about pairing.
ncall=$(grep -c '"type":"tool_call"' "$tmp/out.jsonl" 2>/dev/null)
nres=$(grep -c '"type":"tool_result"' "$tmp/out.jsonl" 2>/dev/null)
nread=$(grep '"type":"tool_call"' "$tmp/out.jsonl" | grep -c '"name":"read_file"')
if [ "$rc" -eq 0 ] && [ "$ncall" = "2" ] && [ "$nres" = "2" ] && [ "$nread" = "2" ]; then
    t_ok "two same-named calls and two results in one round"
else
    t_fail "rc=$rc calls=$ncall results=$nres read_file=$nread"
fi

# --- 2: every tool_call event carries an id -----------------------------------
missing=0
i=1
for l in $(grep -n '"type":"tool_call"' "$tmp/out.jsonl" | cut -d: -f1); do
    sed -n "${l}p" "$tmp/out.jsonl" > "$tmp/one.json"
    "$JQ" -q '.id' "$tmp/one.json" >/dev/null 2>&1 || missing=$((missing + 1))
    i=$((i + 1))
done
if [ "$missing" -eq 0 ]; then
    t_ok "every tool_call event carries an id"
else
    t_fail "$missing tool_call event(s) have no id"
fi

# --- 3: the two ids are DISTINCT ----------------------------------------------
# The property that makes pairing possible. Two calls sharing an id would satisfy
# check 2 and leave the defect exactly where it was.
ids=$(grep '"type":"tool_call"' "$tmp/out.jsonl" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
nids=$(printf '%s\n' $ids | grep -c .)
nuniq=$(printf '%s\n' $ids | sort -u | grep -c .)
if [ "$nids" = "2" ] && [ "$nuniq" = "2" ]; then
    t_ok "the two calls have distinct ids ($(printf '%s ' $ids))"
else
    t_fail "$nids id(s), $nuniq distinct -- pairing is still by order"
fi

# --- 4: each result's id matches a call's -------------------------------------
# The pairing itself, end to end. An id on the call alone would be decoration.
rids=$(grep '"type":"tool_result"' "$tmp/out.jsonl" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
unmatched=0
for r in $rids; do
    printf '%s\n' $ids | grep -qx "$r" || unmatched=$((unmatched + 1))
done
nrids=$(printf '%s\n' $rids | grep -c .)
if [ "$nrids" = "2" ] && [ "$unmatched" -eq 0 ]; then
    t_ok "both results carry an id that matches a call"
else
    t_fail "result ids=$nrids unmatched=$unmatched"
fi

# --- 5: the ids are the PROVIDER's, not invented locally ----------------------
# The load-bearing check. A locally-minted uuid would satisfy checks 2-4 while being
# useless for the thing a supervisor actually wants: correlating the jsonl stream with
# the request bodies (or a provider-side log). So the id must appear in the captured
# request too -- the mock sends c1/c2, and jichi must echo those, not replace them.
bad=0
for id in $ids; do
    grep -l "\"id\":\"$id\"" "$tmp"/cap/req.* >/dev/null 2>&1 || bad=$((bad + 1))
done
if [ "$bad" -eq 0 ]; then
    t_ok "the ids are the ones on the wire, not locally minted"
else
    t_fail "$bad id(s) appear in the jsonl but in no request body"
fi

# --- 6: no id field when the provider sends none ------------------------------
# An empty string would read as "an id that happens to be blank". The M431c rule for
# run_start applies: emit the field when it means something, omit it otherwise. Checked
# on the `text` event, which has no call to identify.
if ! grep '"type":"text"' "$tmp/out.jsonl" | grep -q '"id":'; then
    t_ok "events with no call to identify carry no id field"
else
    t_fail "an id leaked onto a non-tool event"
fi

t_done
