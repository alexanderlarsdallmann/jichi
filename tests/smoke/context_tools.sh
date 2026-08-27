#!/bin/sh
# smoke: `context tools` -- per-tool definition sizes (M313).
#
# Six checks, of which the two that matter are the agreement ones. M310, M311
# and M312 were each a case of two descriptions of the same thing disagreeing,
# so a NEW report on a thing that already has one earns its own pinning:
#
#   1. the view lists tools, largest first (a sorted-descending token column).
#   2. every advertised tool appears (count matches the summary line's).
#   3. the stated total EQUALS the total `context` prints -- the same number,
#      not merely a close one. The per-tool lines sum slightly lower because
#      array framing belongs to no tool, and the header says so, which is why
#      this compares the headers rather than the sums.
#   4. the `core` footer's count matches what `--tool-profile core` actually
#      advertises, so the "what would core save me" claim is checked against
#      the fence rather than trusted.
#   5. under `core` the view lists the fenced set, not the full one (M310).
#   6. it is read-only: no model call, so it works against an unreachable
#      endpoint. (The config points at port 9 deliberately.)
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# No mockmodel at all: these reports must not touch the network. Port 9
# (discard) is unreachable by design, so a report that tried would hang or
# fail rather than pass quietly.
cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"contextLimit":32000,"maxRetries":0}
EOF

jc() {
    (cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
        --no-lite "$@" < /dev/null 2>/dev/null)
}

full=$(jc context tools --tool-profile full)
if [ -z "$full" ]; then
    t_fail "context tools produced no output"
    t_done
fi

# --- 1: sorted descending -----------------------------------------------------
toks=$(printf '%s\n' "$full" | sed -n 's/^ *\([0-9][0-9]*\)  *[0-9][0-9]*%.*/\1/p')
ntoks=$(printf '%s\n' "$toks" | grep -c .)
unsorted=$(printf '%s\n' "$toks" | awk 'NR>1 && $1 > prev {bad++} {prev=$1} END {print bad+0}')
if [ "$ntoks" -ge 5 ] && [ "$unsorted" = "0" ]; then
    t_ok "$ntoks tools listed, largest first"
else
    t_fail "listing not sorted descending ($ntoks rows, $unsorted inversions)"
fi

# --- 2: every advertised tool has a row ---------------------------------------
claimed=$(printf '%s\n' "$full" | sed -n 's/^Tool definitions: \([0-9]*\) advertised.*/\1/p')
if [ -n "$claimed" ] && [ "$claimed" = "$ntoks" ]; then
    t_ok "all $claimed advertised tools have a row"
else
    t_fail "header claims $claimed advertised, $ntoks rows printed"
fi

# --- 3: the two reports state the SAME total ----------------------------------
view_tot=$(printf '%s\n' "$full" | sed -n 's/^Tool definitions:.*~\([0-9]*\) tokens.*/\1/p')
rep_tot=$(jc context --tool-profile full | \
          sed -n 's/^  tool definitions *~\([0-9]*\).*/\1/p')
if [ -n "$view_tot" ] && [ "$view_tot" = "$rep_tot" ]; then
    t_ok "context and context tools state the same total (~$view_tot)"
else
    t_fail "totals disagree: context says ~$rep_tot, context tools says ~$view_tot"
fi

# --- 4: the core footer agrees with the actual fence --------------------------
footer_n=$(printf '%s\n' "$full" | \
           sed -n 's/^\* = kept by the core profile: \([0-9]*\) of.*/\1/p')
core_out=$(jc context tools --tool-profile core)
core_n=$(printf '%s\n' "$core_out" | \
         sed -n 's/^Tool definitions: \([0-9]*\) advertised.*/\1/p')
if [ -n "$footer_n" ] && [ "$footer_n" = "$core_n" ]; then
    t_ok "the core footer's count ($footer_n) matches what core advertises"
else
    t_fail "footer claims core keeps $footer_n, core advertises $core_n"
fi

# --- 5: under core, the fenced set is what is listed (M310) -------------------
if printf '%s\n' "$core_out" | grep -q "core profile" && \
   [ -n "$core_n" ] && [ -n "$claimed" ] && [ "$core_n" -lt "$claimed" ]; then
    t_ok "core lists the fenced set ($core_n of $claimed) and names the profile"
else
    t_fail "core view did not narrow ($core_n vs $claimed) or did not say so"
fi

# --- 6: no model call --------------------------------------------------------
# Reached here at all against an unreachable endpoint, and every `jc` call ran
# under a 30s deadline, so neither report waited on the network.
case "$full" in
    *"spawn_subagent"*|*"read_file"*)
        t_ok "read-only: the report renders with no reachable model" ;;
    *)
        t_fail "no tool names in the output -- did it need a model?" ;;
esac

t_done
