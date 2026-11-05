#!/bin/sh
# smoke: what `spawn_subagent` does with the `model` argument a model actually
# writes (M745).
#
# THE MEASURED FAILURES. On the 2026-09-24 zigodot drive two parents passed a
# `model` their config did not have. One passed "default" -- the tool's own
# description says the model "Defaults to the current model". The other passed
# "summarize", the description's own example of a role, and no configured model
# declared it. Both were refused with a bare `error: no model matches '...'`, and
# each parent spent a round trip guessing again. Now:
#   1. a miss names the configured models, so the next guess is not a guess;
#   2. a real role no model declares runs on the current model, and says so;
#   3. "default" is the current model.
# Each check reads the parent's NEXT request, where the tool result is what the
# model sees.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/s.mm" <<'EOF'
wire openai
rule
  match "TOP miss"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP miss"
  tool spawn_subagent {"task":"CHILD miss","model":"no-such-model"}
rule
  match "TOP role"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP role"
  tool spawn_subagent {"task":"CHILD role","model":"summarize"}
rule
  match "TOP deflt"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP deflt"
  tool spawn_subagent {"task":"CHILD deflt","model":"default"}
rule
  match "CHILD"
  text CHILDANSWER
EOF

# The parent's requests that carry a tool result, for prompt P.
tool_turn() {
    for _f in "$1"/req.*; do
        [ -f "$_f" ] || continue
        grep -q "$2" "$_f" && grep -q '"role":"tool"' "$_f" && printf '%s\n' "$_f"
    done
}

run_one() {  # run_one PROMPT CAPDIR
    mm_start "$tmp/s.mm" "$2" 9
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        -q --no-session --auto -p "$1" < /dev/null > "$2.out" 2>&1)
    mm_stop
}

run_one "TOP miss" "$tmp/miss"
f=$(tool_turn "$tmp/miss" "TOP miss" | head -1)
if [ -n "$f" ] && smoke_bgrep "$f" -q "no model matches 'no-such-model'. The configured models are: 1 'm' (mock)"; then
    t_ok "a model that matches nothing is refused with the configured models named"
else
    t_fail "the refusal does not list the models: $(smoke_bgrep "${f:-/dev/null}" -o 'no model matches[^"]*' | head -1)"
fi

# O1 teeth (supervisor 11:58, 4th attempt): both checks read the FIRST request
# (req.1), which carries no tool call and no refusal -- so a phrase found there
# can only be in the spawn_subagent DEFINITION, not in M745's refusal text (which
# the tool-RESULT request also carries). A definition carries "description"; a
# call carries "arguments", so the count literal names a definition only; and a
# one-line greedy pattern (the old bug) cannot collapse two definitions into one
# match.
f1="$tmp/miss/req.1"
n=$(smoke_bgrep "$f1" -o '"name":"spawn_subagent","description"' | wc -l)
n=$(echo "$n")
if [ "$n" = 1 ]; then
    t_ok "spawn_subagent is defined exactly once (dynamic replaces the static form)"
else
    t_fail "spawn_subagent is defined $n time(s) in req.1 (expected 1)"
fi
if [ -f "$f1" ] && smoke_bgrep "$f1" -q 'The configured models are:'; then
    t_ok "spawn_subagent's definition lists the configured models"
else
    t_fail "req.1's spawn_subagent definition does not list the configured models"
fi

run_one "TOP role" "$tmp/role"
f=$(tool_turn "$tmp/role" "TOP role" | head -1)
if [ -n "$f" ] && smoke_bgrep "$f" -q "CHILDANSWER"; then
    t_ok "a role no model declares still runs the child (on the current model)"
else
    t_fail "the summarize child did not run: $(smoke_bgrep "${f:-/dev/null}" -o 'error: [^"]*' | head -1)"
fi
if [ -n "$f" ] && smoke_bgrep "$f" -q "no configured model declares the role 'summarize'"; then
    t_ok "...and the parent is told which model it ran on"
else
    t_fail "no note about the missing summarize role"
fi

run_one "TOP deflt" "$tmp/deflt"
f=$(tool_turn "$tmp/deflt" "TOP deflt" | head -1)
if [ -n "$f" ] && smoke_bgrep "$f" -q "CHILDANSWER"; then
    t_ok "\"model\": \"default\" is the current model"
else
    t_fail "the default child did not run: $(smoke_bgrep "${f:-/dev/null}" -o 'error: [^"]*' | head -1)"
fi

t_done
