#!/bin/sh
# smoke: `context history` -- where a saved session's history went (M315).
#
# Six checks. The two that carry weight are the accounting ones, because that
# is the invariant M312/M313/M314 each had to establish for their own line:
#
#   1. it reads a SAVED session (the subcommand has no live conversation) and
#      names which one.
#   2. the role block sums EXACTLY to the stated total -- no remainder. The
#      per-message term is compaction's own, so this cannot drift from the
#      history line it explains.
#   3. tool results are attributed BY NAME, resolved through tool_call_id.
#   4. the per-tool block states its own base, since it covers only the tool
#      results -- a percentage that changes denominator between blocks
#      silently is how a report stops being trusted.
#   5. the largest-messages block prints a locatable index.
#   6. no saved session is an error with a message, not an empty report.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'a line about the timeout setting\nanother line\n' > "$ws/note.txt"

# --- 6 first: no session at all ----------------------------------------------
cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"contextLimit":32000,"maxRetries":0}
EOF
none=$( (cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
        --no-lite context history < /dev/null) 2>&1 )
if printf '%s\n' "$none" | grep -q "no saved session"; then
    t_ok "no saved session: an error with a message, not an empty report"
else
    t_fail "expected a no-session error, got: $(printf '%s' "$none" | head_bytes 100)"
fi

# --- a real session with two tool calls of different sizes -------------------
# `count N` selects the Nth request (1-based), which is how the two calls are
# ordered. read_file returns the note (bigger); search_code returns one match
# (smaller), so the per-tool ordering is a fact about the data, not the sort.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"note.txt"}
rule
  count 2
  tool search_code {"pattern":"timeout"}
rule
  match "\"role\":\"tool\""
  text HISTORY_DONE
rule
  status 500
  body {"error":"unexpected request"}
EOF

mm_start "$tmp/replies.mm" "$tmp" 3
write_config "$tmp/config-live.json" "$MM_PORT" '"contextLimit":32000'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config-live.json" \
    --auto --no-lite -q -p "read the note then search it" < /dev/null) \
    > "$tmp/run.out" 2>&1
mm_stop

rep=$( (cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
       --no-lite context history < /dev/null) 2>/dev/null )

if printf '%s\n' "$rep" | grep -q "^History: session "; then
    t_ok "reads the saved session and names it"
else
    t_fail "no session header: $(printf '%s' "$rep" | head_bytes 160)"
fi

# --- 2: the role block sums exactly to the stated total ----------------------
stated=$(printf '%s\n' "$rep" | sed -n 's/^History:.*~\([0-9]*\) tokens$/\1/p')
rolesum=$(printf '%s\n' "$rep" | \
          sed -n '/^  by role$/,/^$/p' | \
          sed -n 's/^    [a-z][a-z ]*~\([0-9]*\) .*/\1/p' | \
          awk '{s += $1} END {printf "%d", s+0}')
if [ -n "$stated" ] && [ "$rolesum" = "$stated" ]; then
    t_ok "the role block sums exactly to the total ($rolesum = $stated)"
else
    t_fail "role block sums to $rolesum, total says $stated -- unexplained remainder"
fi

# --- 3: attribution by tool name --------------------------------------------
if printf '%s\n' "$rep" | grep -q "read_file" && \
   printf '%s\n' "$rep" | grep -q "search_code" && \
   ! printf '%s\n' "$rep" | grep -q "(unknown)"; then
    t_ok "tool results attributed by name (read_file, search_code)"
else
    t_fail "tool attribution failed: $(printf '%s\n' "$rep" | \
            sed -n '/tool output/,/^$/p' | tr '\n' ' ')"
fi

# --- 4: the per-tool block states its own base ------------------------------
if printf '%s\n' "$rep" | grep -q "of ~[0-9]* tokens of tool results"; then
    t_ok "the per-tool block states its own percentage base"
else
    t_fail "per-tool block does not state its base"
fi

# --- 5: a locatable index on the largest messages ---------------------------
if printf '%s\n' "$rep" | grep -qE "\(message [0-9]+\)"; then
    t_ok "the largest messages carry a locatable index"
else
    t_fail "no message index printed"
fi

t_done
