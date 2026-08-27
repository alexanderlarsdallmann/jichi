#!/bin/sh
# smoke: the tool profile (M74) advertises what it says it advertises, and
# /context reports what is actually sent (M310).
#
# M74 has been shipping since long before this driver, with no smoke
# coverage of the one thing it does: which tool definitions leave the
# process. M310 measured the profile's cost and found the GAUGE wrong --
# jc_context_report sized the unfenced array, so under `core` it
# over-reported the single line a user consults it for. Fixing a gauge
# without pinning it is how it drifts back, so:
#
#   1. `full` advertises non-core tools (run_tests, todowrite).
#   2. `core` advertises the seven built-in essentials and NOT those.
#   3. `context` reports fewer tool tokens under core, and SAYS so.
#   4. `auto` resolves to core below JC_TOOL_PROFILE_AUTO_BELOW (12000)
#      -- the threshold is what makes this reachable without a flag.
#
# Assertions 1+2 read the captured request body, so they prove suppression
# at the wire, not in a report about the wire.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text PROFILE_OK
EOF

# --- 1/2: what actually goes on the wire -------------------------------------
for prof in full core; do
    mkdir -p "$tmp/cap-$prof"
    mm_start "$tmp/replies.mm" "$tmp/cap-$prof" 1
    write_config "$tmp/config-$prof.json" "$MM_PORT" '"contextLimit":32000'
    (cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config-$prof.json" \
        --tool-profile "$prof" --no-lite --no-session -q \
        -p "say something" < /dev/null) > "$tmp/out-$prof" 2>&1
    mm_stop
done

req_full="$tmp/cap-full/req.1"
req_core="$tmp/cap-core/req.1"

if [ -s "$req_full" ] && grep -q '"read_file"' "$req_full" && \
   grep -q '"run_terminal_command"' "$req_full"; then
    t_ok "full advertises the core essentials"
else
    t_fail "full request missing the core tools (or no capture)"
fi

# A non-core built-in that is always registered: run_tests needs no config,
# todowrite likewise. If BOTH vanish from `full`, the profile is not the
# reason -- so require them here, which also keeps assertion 2 meaningful.
if grep -q '"run_tests"' "$req_full" && grep -q '"todowrite"' "$req_full"; then
    t_ok "full also advertises non-core tools (run_tests, todowrite)"
else
    t_fail "full request lacks run_tests/todowrite -- registration changed?"
fi

if [ -s "$req_core" ] && grep -q '"read_file"' "$req_core" && \
   grep -q '"apply_patch"' "$req_core"; then
    t_ok "core still advertises the seven essentials"
else
    t_fail "core request missing an essential tool (or no capture)"
fi

if grep -q '"run_tests"' "$req_core" || grep -q '"todowrite"' "$req_core"; then
    t_fail "core advertised a non-core tool -- the fence is not applied"
else
    t_ok "core does NOT advertise run_tests/todowrite"
fi

# --- 3: the gauge agrees with the wire (M310) --------------------------------
ctx_full=$(cd "$ws" && "$BIN" --config "$tmp/config-full.json" \
           --tool-profile full --no-lite context < /dev/null 2>/dev/null | \
           grep 'tool definitions')
ctx_core=$(cd "$ws" && "$BIN" --config "$tmp/config-full.json" \
           --tool-profile core --no-lite context < /dev/null 2>/dev/null | \
           grep 'tool definitions')

n_full=$(printf '%s' "$ctx_full" | sed -n 's/.*~\([0-9]*\).*/\1/p')
n_core=$(printf '%s' "$ctx_core" | sed -n 's/.*~\([0-9]*\).*/\1/p')

if [ -n "$n_full" ] && [ -n "$n_core" ]; then
    t_ok "context reports a tool-definition size under both profiles"
else
    t_fail "could not parse the tool-definition line: [$ctx_full] [$ctx_core]"
fi

if [ -n "$n_core" ] && [ -n "$n_full" ] && [ "$n_core" -lt "$n_full" ]; then
    t_ok "context reports FEWER tool tokens under core ($n_core < $n_full)"
else
    t_fail "context reports the same size under core ($n_core vs $n_full) -- \
the report is sizing the unfenced array (M310 regression)"
fi

case "$ctx_core" in
    *"core profile"*)
        t_ok "the core report names the profile" ;;
    *)
        t_fail "core report does not say which profile: $ctx_core" ;;
esac

# --- 4: auto reaches core by itself under the threshold ----------------------
ctx_auto=$(cd "$ws" && "$BIN" --config "$tmp/config-full.json" \
           --tool-profile auto --no-lite --context-limit 8000 context \
           < /dev/null 2>/dev/null | grep 'tool definitions')
case "$ctx_auto" in
    *"core profile"*)
        t_ok "auto resolves to core below the 12000-token threshold" ;;
    *)
        t_fail "auto did not resolve to core at contextLimit 8000: $ctx_auto" ;;
esac

# --- 5: what core COSTS, pinned (M310) ---------------------------------------
# The seven essentials are built-ins, so `core` silently drops every
# conditional tool -- including the assignments feature's `hint` and
# `ask_for_help`, which are exactly the machinery `attempt` exists to
# exercise and whose use its own report counts ("N hints used"). M310
# measured the profile's cost with assignments OFF, so this is the
# mechanical proof of the capability half, and the reason core is a
# recommendation rather than a default for `attempt`.
for prof in full core; do
    mkdir -p "$tmp/capa-$prof"
    mm_start "$tmp/replies.mm" "$tmp/capa-$prof" 1
    write_config "$tmp/config-a-$prof.json" "$MM_PORT" \
        '"contextLimit":32000,"assignments":true'
    (cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config-a-$prof.json" \
        --tool-profile "$prof" --no-lite --no-session -q \
        -p "say something" < /dev/null) > /dev/null 2>&1
    mm_stop
done

if grep -q '"hint"' "$tmp/capa-full/req.1" 2>/dev/null; then
    t_ok "full advertises the assignment tools when assignments is on"
else
    t_fail "full lacks 'hint' with assignments:true -- registration changed?"
fi

# The capture MUST exist before absence means anything: a missing file makes
# `grep -q` fail, which would read as "core dropped it" no matter what the
# code did. This pair caught itself while being written -- the sibling
# assertion above went red because both configs put `assignments` in the
# MODEL object (write_config's 4th arg) where it is ignored, and this check
# sailed through green on an 18-tool request that never had `hint` in it.
if [ ! -s "$tmp/capa-core/req.1" ]; then
    t_fail "no core capture -- absence of 'hint' would prove nothing"
elif grep -q '"hint"' "$tmp/capa-core/req.1"; then
    t_fail "core advertised 'hint' -- the fence is not applied to conditionals"
else
    t_ok "core drops the assignment tools (the documented cost of core)"
fi

t_done
