#!/bin/sh
# smoke: STATE-THE-REACH (M387). When an edit scope is armed, the boundary is
# made legible on both surfaces: the MODEL is told in the system prompt that the
# scope fences the file tools and the shell reaches past it (detected
# afterward), and the OPERATOR is told the same by `doctor`. Both are two-sided
# -- absent when no edit scope is configured, so the cached prefix does not
# churn on unscoped runs and doctor stays quiet.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text REACH_TURN_DONE
EOF

# --- prompt half: the note is in the first request only with an edit scope ---
mm_start "$tmp/replies.mm" "$tmp/cap1" 1
write_config "$tmp/config.json" "$MM_PORT"
( cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
      -q --no-session --edit-scope 'src/**' -p "hi" < /dev/null ) >/dev/null 2>&1
mm_stop
if grep -q "detected afterward" "$tmp/cap1/req.1"; then
    t_ok "edit scope armed: the shell-reach note is in the system prompt"
else
    t_fail "reach note missing from req.1"
fi

# M431: and the note NAMES the scope it refers to. It opened "The edit scope
# above fences the file tools" while nothing above named it, so the only way to
# learn the writable paths was to violate the fence and read the refusal (one run
# guessed 177 times before M333 put the list in that refusal). Two-sided via
# check 2 below, which requires the whole paragraph absent without a scope.
# -F: the glob is not a regex here.
if grep -q "only paths you may write" "$tmp/cap1/req.1" &&
   grep -qF 'src/**' "$tmp/cap1/req.1"; then
    t_ok "the note names the writable globs (src/**), not just 'the scope above'"
else
    t_fail "the scope globs are not in the system prompt: $(grep -o 'edit-scope[^"]\{0,90\}' "$tmp/cap1/req.1" 2>/dev/null | head -1)"
fi

mm_start "$tmp/replies.mm" "$tmp/cap2" 1
write_config "$tmp/config2.json" "$MM_PORT"
( cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config2.json" \
      -q --no-session -p "hi" < /dev/null ) >/dev/null 2>&1
mm_stop
if ! grep -q "detected afterward" "$tmp/cap2/req.1"; then
    t_ok "no edit scope: the note is absent (no prefix churn)"
else
    t_fail "reach note present without an edit scope"
fi

# --- doctor half -------------------------------------------------------------
cat > "$tmp/es.json" <<'EOF'
{"lowResource":false,"editScope":["src/**"],"models":[
 {"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1"}]}
EOF
"$BIN" --config "$tmp/es.json" doctor < /dev/null > "$tmp/doc" 2>&1
if grep -qi "edit scope: fences the file tools" "$tmp/doc"; then
    t_ok "doctor states the edit scope fences the file tools only"
else
    t_fail "doctor missing the STATE-THE-REACH line"
fi

cat > "$tmp/noes.json" <<'EOF'
{"lowResource":false,"models":[
 {"name":"m","provider":"openai","model":"x","apiBase":"http://127.0.0.1:1/v1"}]}
EOF
"$BIN" --config "$tmp/noes.json" doctor < /dev/null > "$tmp/doc2" 2>&1
if ! grep -qi "edit scope: fences" "$tmp/doc2"; then
    t_ok "doctor omits the line when no edit scope is configured"
else
    t_fail "doctor showed the line without an edit scope"
fi

t_done
