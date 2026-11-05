#!/bin/sh
# smoke: `"constraintsAutoAdopt": false` turns off reading rules out of an --auto
# request, and only a config the operator chose can turn it off (M874: the demos
# drive's N45 (c), the operator's decision of 2026-10-02, narrow-only).
#
# WHY. A drive's lead prompt is assembled from several documents -- the standing
# prompt, the last run's handoff, a supervisor's notes -- and any "do not <verb>" in
# them became a session rule the model was told to follow (N45). M872 skips what is
# quoted or asked; this key lets a harness that assembles its prompt turn the
# inference off. A rule the operator WROTE (.jichi/constraints.md) still binds.
#
# THE CHECKS: by default "Do not commit." is read (1); `--config` with the key false
# reads nothing, and the request carries no advisory block (2); the user's own
# ~/.jichi with it false does the same (3); an untrusted project config with it
# false is not applied -- the rule is read, and the notice names the key (4);
# trusted, it applies (5); with the key off, a rule the operator wrote is still in
# the request as a limit (6); `doctor` (7) and `brief-check` (8) say the switch is off.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
PROMPT="Do not commit. Then say C_DONE."

cat > "$tmp/c.mm" <<'EOF'
wire openai
rule
  text C_DONE
EOF

user_cfg() { # user_cfg <port> [extra-json] -> the user's own ~/.jichi
    cat > "$HOME/.jichi" <<EOF
{"lowResource":false,"models":[{"name":"mine","provider":"openai","model":"mine",
"apiBase":"http://127.0.0.1:$1/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
"maxRetries":0$2}
EOF
}

run_c() { # run_c <ws> <extra-json-for-~/.jichi> -> $CAP/req.1, $CAP/out, $ERR
    _ws=$1; _extra=$2
    CAP=$(smoke_tmp)
    mm_start "$tmp/c.mm" "$CAP"
    user_cfg "$MM_PORT" "$_extra"
    ERR="$CAP/err"
    (cd "$_ws" && unset JC_CONFIG && with_deadline 60 "$BIN" --no-session --auto \
        -p "$PROMPT" < /dev/null > "$CAP/out" 2> "$ERR")
    mm_stop
}

inferred() { grep -q '\[constraint\] inferred from your request' "$1"; }

# --- 1: the default reads the rule ---------------------------------------------------
WS1=$(smoke_tmp)
run_c "$WS1" ""
if grep -q 'C_DONE' "$CAP/out" && inferred "$ERR"; then
    t_ok "by default an --auto run reads \"Do not commit.\" as a rule"
else
    t_fail "default: out=$(head_bytes 80 "$CAP/out"); err: $(grep constraint "$ERR" | head_bytes 200)"
fi

# --- 2: --config, the key false -------------------------------------------------------
CAP=$(smoke_tmp)
mm_start "$tmp/c.mm" "$CAP"
write_config "$tmp/off.json" "$MM_PORT" '"constraintsAutoAdopt":false'
(cd "$WS1" && with_deadline 60 "$BIN" --config "$tmp/off.json" --no-session --auto \
    -p "$PROMPT" < /dev/null > "$CAP/out" 2> "$CAP/err")
mm_stop
if grep -q 'C_DONE' "$CAP/out" && ! inferred "$CAP/err" && [ -f "$CAP/req.1" ] &&
   ! grep -q 'Inferred from the request' "$CAP/req.1"; then
    t_ok "--config with constraintsAutoAdopt false reads no rule, and the request has no advisory block"
else
    t_fail "--config off: out=$(head_bytes 80 "$CAP/out"); err: $(grep constraint "$CAP/err" | head_bytes 200)"
fi

# --- 3: the user's own ~/.jichi, the key false -----------------------------------------
run_c "$WS1" ',"constraintsAutoAdopt":false'
if grep -q 'C_DONE' "$CAP/out" && ! inferred "$ERR"; then
    t_ok "the user's own config with the key false reads no rule"
else
    t_fail "user config off: err: $(grep constraint "$ERR" | head_bytes 200)"
fi

# --- 4-5: a project config: not applied untrusted, applied once trusted -----------------
WS2=$(smoke_tmp); mkdir -p "$WS2/.jichi"
printf '%s\n' '{"constraintsAutoAdopt":false}' > "$WS2/.jichi/config.json"
run_c "$WS2" ""
if inferred "$ERR" && grep 'is not trusted' "$ERR" | grep -q 'constraintsAutoAdopt'; then
    t_ok "an untrusted project config cannot turn it off, and the notice names the key"
else
    t_fail "untrusted project: err: $(grep -E 'constraint|trusted' "$ERR" | head_bytes 300)"
fi
(cd "$WS2" && unset JC_CONFIG && "$BIN" trust < /dev/null > "$tmp/trust.out" 2>&1)
run_c "$WS2" ""
if grep -q '^trusted ' "$tmp/trust.out" && grep -q 'C_DONE' "$CAP/out" && ! inferred "$ERR"; then
    t_ok "trusted, the project config turns it off"
else
    t_fail "trusted project: trust=$(head_bytes 120 "$tmp/trust.out"); err: $(grep constraint "$ERR" | head_bytes 200)"
fi

# --- 6: a rule the operator wrote still binds with the key off --------------------------
WS3=$(smoke_tmp); mkdir -p "$WS3/.jichi"
printf '%s\n' 'deny-cmd push' > "$WS3/.jichi/constraints.md"
run_c "$WS3" ',"constraintsAutoAdopt":false'
if [ -f "$CAP/req.1" ] && grep -q 'Active constraints (ENFORCED' "$CAP/req.1" &&
   grep -q 'do not push' "$CAP/req.1" && ! inferred "$ERR"; then
    t_ok "with the key off, an authored rule is still sent as a limit"
else
    t_fail "authored with key off: $(grep -o 'Active constraints[^#]*' "$CAP/req.1" 2>/dev/null | head_bytes 200)"
fi

# --- 7-8: doctor and brief-check name the switch ----------------------------------------
out=$("$BIN" --config "$tmp/off.json" doctor < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'constraint inference off (constraintsAutoAdopt)'; then
    t_ok "doctor names constraintsAutoAdopt when it is off"
else
    t_fail "doctor: $(printf '%s' "$out" | grep -i constraint | head -3)"
fi
printf '%s\n' 'Fix the parser. Do not commit.' > "$tmp/brief.md"
out=$(cd "$WS1" && "$BIN" --config "$tmp/off.json" brief-check "$tmp/brief.md" < /dev/null 2>/dev/null)
if printf '%s' "$out" | grep -q 'constraintsAutoAdopt is off in this config'; then
    t_ok "brief-check says an --auto run under this config adopts none of its findings"
else
    t_fail "brief-check: $(printf '%s' "$out" | grep -A3 'constraints inferred' | head -5)"
fi

t_done
