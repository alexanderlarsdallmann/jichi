#!/bin/sh
# smoke: prompt-inferred constraints are session-scoped (M169). A
# constraint jichi GUESSED from prose must be enforced THIS run but leave
# no store behind (not even .jichi/), and the adoption notice must say so;
# an AUTHORED .jichi/constraints.md is announced, enforced, and survives
# byte-intact. The mock asks for the forbidden tool, so the refusal
# provably comes from jichi's gate.
# (Port of tests/e2e/constraints_scope.py, M211; the two runs share one
# mock, requests 1-2 and 3-4, so the count rules pick the tool turn.)
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
store="$ws/.jichi/constraints.md"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_tests {}
rule
  count 3
  tool run_tests {}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"testCommand":"true"'

# --- 1. inferred: enforced, but nothing persisted ---------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "Do not run tests. Then say DONE." \
    < /dev/null > /dev/null 2>"$tmp/err1")

if grep -i -q "constraint" "$tmp/err1" && grep -q "THIS SESSION" "$tmp/err1"
then
    t_ok "adoption notice on stderr says THIS SESSION"
else
    t_fail "notice missing/unscoped: $(tail -c 200 "$tmp/err1")"
fi
if [ ! -e "$store" ] && [ ! -d "$ws/.jichi" ]; then
    t_ok "nothing persisted (no store, no .jichi/)"
else
    t_fail "an inferred constraint left files behind"
fi
if grep -i -q "constraint" "$tmp/req.2" 2>/dev/null; then
    t_ok "the forbidden run_tests call was refused by the gate"
else
    t_fail "no refusal in the tool result fed back to the model"
fi

# --- 2. authored: honoured, announced, byte-intact ---------------------------
mkdir -p "$ws/.jichi"
printf '%s\n%s\n' \
  "# jichi constraints -- enforced every turn (M110). One per line." \
  "deny-cmd test" > "$store"
sum_before=$(cksum "$store")

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "Then say DONE." \
    < /dev/null > /dev/null 2>"$tmp/err2")
mm_stop

if grep -q "active from" "$tmp/err2"; then
    t_ok "an authored store is announced at startup"
else
    t_fail "no announcement: $(tail -c 200 "$tmp/err2")"
fi
if [ -f "$store" ]; then
    t_ok "the authored store survived the run"
else
    t_fail "the authored store vanished"
fi
if [ "$(cksum "$store")" = "$sum_before" ]; then
    t_ok "the authored store is byte-intact"
else
    t_fail "the authored store was rewritten"
fi
if grep -i -q "constraint" "$tmp/req.4" 2>/dev/null; then
    t_ok "the authored constraint was enforced"
else
    t_fail "no refusal on the authored-constraint run"
fi

t_done
