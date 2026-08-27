#!/bin/sh
# smoke: the journal says WHERE the verifier came from, and an empty --verify is
# a deliberate opt-out (M503, the second-zigodot-campaign row).
#
# THE DEFECT THIS EXISTS FOR. An `--auto` run with no `--verify` inherits the
# project's `testCommand` as its completion gate. That is a good default -- it is
# what M331's "the gate must agree with itself" wants -- but it happened
# INVISIBLY. Measured on a test-first authoring run ("add a panicking stub +
# failing gate tests; red IS success") whose config set `testCommand`: the run
# got that command as its verifier without anyone asking, fix-forward then fed
# "the verify failed, fix it" back at a model whose brief said the suite must end
# red, and the result was 39 tool calls of thrash, 1.56M tokens, a deadline
# exhausted, nothing delivered -- and a claimed success over an empty diff.
#
# The journal recorded `verify: <cmd>` either way, so a supervisor could not tell
# an operator's choice from a config inheritance. Now `verify_source` says which.
#
# The row also asked for "a no-verify opt-out flag (or an empty --verify meaning
# no gate)". Checked before building one: an empty --verify ALREADY disarms the
# gate -- it was simply undocumented. Check 2 pins that behaviour so the
# now-documented opt-out cannot regress.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text DONE
EOF
mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
"lowResource":false,
"testCommand":"true"}
EOF

start_field() {   # start_field <journal> <field>
    sed -n 's/.*"event":"start"//p' "$1" | head -1 \
        | sed -n 's/.*"'"$2"'":"\([^"]*\)".*/\1/p'
}

run_it() {   # run_it <journal> [extra args...]
    _j="$1"; shift
    (cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" --auto \
        --no-session --journal "$_j" "$@" -p "hi" < /dev/null > /dev/null 2>&1)
}

# ---- 1. inherited from the config, and SAID to be ---------------------------
run_it "$tmp/j1.jsonl"
if [ "$(start_field "$tmp/j1.jsonl" verify)" = "true" ] &&
   [ "$(start_field "$tmp/j1.jsonl" verify_source)" = "config" ]; then
    t_ok "an inherited testCommand is recorded as verify_source=config"
else
    t_fail "the inheritance is invisible in the journal: verify=\
'$(start_field "$tmp/j1.jsonl" verify)' source=\
'$(start_field "$tmp/j1.jsonl" verify_source)'"
fi

# ---- 2. the documented opt-out really disarms it ----------------------------
run_it "$tmp/j2.jsonl" --verify ""
if [ -z "$(start_field "$tmp/j2.jsonl" verify)" ]; then
    t_ok "an empty --verify disarms the inherited gate (the documented opt-out)"
else
    t_fail "--verify '' did not disarm the gate: verify=\
'$(start_field "$tmp/j2.jsonl" verify)'"
fi

# ---- 3. ...and reads as a DECISION, not an absence -------------------------
# An operator who deliberately removed the gate must not look like one who never
# had a gate. That distinction is the whole point of the field.
if [ "$(start_field "$tmp/j2.jsonl" verify_source)" = "flag" ]; then
    t_ok "the disarm is attributed to the operator (verify_source=flag)"
else
    t_fail "a deliberate disarm is indistinguishable from having no gate: \
source='$(start_field "$tmp/j2.jsonl" verify_source)'"
fi

# ---- 4. an explicit verifier is attributed too ------------------------------
run_it "$tmp/j3.jsonl" --verify "echo CHOSEN"
if [ "$(start_field "$tmp/j3.jsonl" verify)" = "echo CHOSEN" ] &&
   [ "$(start_field "$tmp/j3.jsonl" verify_source)" = "flag" ]; then
    t_ok "an explicit --verify is recorded as the operator's choice"
else
    t_fail "explicit verifier misattributed: verify=\
'$(start_field "$tmp/j3.jsonl" verify)' source=\
'$(start_field "$tmp/j3.jsonl" verify_source)'"
fi

mm_stop
t_done
