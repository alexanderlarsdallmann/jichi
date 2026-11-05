#!/bin/sh
# smoke: `attempt --agent` applies the profile's `tools:` and `model:` (M848,
# DEFERRED def-287, decision 2b of the 2026-09-30 profile rule).
#
# THE DEFECT. `attempt --agent` ran the profile through the command path, which
# applies the persona, the style and a read-only tightening -- and ignores
# `tools:` and `model:` ("a subagent-only concern"). So the learner tiers the
# assignments pack ships differed by persona alone: learner-senior, whose tools:
# list leaves out the help tools, was offered `hint` like every other tier, and
# ASSIGNMENTS.md's "differ by tool allow-list + persona" was false.
#
# THE RULE (decided 2026-09-30): in a run of its own -- a subtask, a subagent,
# `attempt` -- a profile's `tools:` is a fence that can only narrow; `attempt`
# and subagents also take its `model:`; an explicit --model wins over the
# profile's. The main turn is untouched (subtask_follows_posture.sh check 5).
#
#   1  control: with assignments on and no --agent, `hint` IS offered -- so
#      check 2's absence is the fence, not a turn that never had the tool.
#   2  the decision's own test: `attempt --agent learner-senior` (the SHIPPED
#      profile, scaffolded by `init assignments`) is not offered `hint`.
#   3  a profile's `model:` picks the model the attempt calls.
#   4  an explicit --model beats the profile's, and the run says so.
#   5  a profile naming an unconfigured model says so and keeps the current one
#      -- never a silent fallback.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- jichi snapshots shell out to it"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text DONE
EOF

# Not write_config: that helper pins "snapshots":false, and attempt refuses to
# run without snapshots (attempt_tainted.sh). Two models, so `model:` can move.
write_cfg() {
    cat > "$1" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
{"name":"m2","provider":"openai","model":"mock2",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
"snapshots":true,"assignments":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
}

# One workspace per case: the shipped assignments pack plus a one-line spec and
# two hand-written profiles that pin a model.
make_ws() {
    _w=$(smoke_tmp)
    (cd "$_w" && "$BIN" --config "$tmp/boot.json" init assignments \
        < /dev/null > /dev/null 2>&1)
    mkdir -p "$_w/docs/assignments" "$_w/.jichi/agents"
    cat > "$_w/docs/assignments/one.md" <<'EOF'
---
title: say done
verify: "true"
points: 1
---
Reply DONE.
EOF
    cat > "$_w/.jichi/agents/pinned.md" <<'EOF'
---
description: pins the second model
model: m2
---
You solve assignments.
EOF
    cat > "$_w/.jichi/agents/ghost.md" <<'EOF'
---
description: names a model that is not configured
model: no-such-model
---
You solve assignments.
EOF
    echo "$_w"
}

# attempt_case <extra args...> -> CAP (captures), ERR (stderr)
attempt_case() {
    WS=$(make_ws)
    CAP=$(smoke_tmp)
    mm_start "$tmp/replies.mm" "$CAP"
    write_cfg "$CAP/config.json"
    ERR="$CAP/err"
    (cd "$WS" && with_deadline 90 "$BIN" --config "$CAP/config.json" "$@" \
        attempt docs/assignments/one.md < /dev/null > /dev/null 2>"$ERR")
    mm_stop
}

# A config for `init`, which calls no model.
MM_PORT=1
write_cfg "$tmp/boot.json"

# --- 1: control -- hint is offered without a profile --------------------------
attempt_case
if grep -q '"name":"hint"' "$CAP/req.1" 2>/dev/null; then
    t_ok "control: with assignments on and no --agent, hint is offered"
else
    t_fail "control failed: hint was not offered without a profile, so check 2
 would prove nothing: $(head_bytes 200 "$ERR")"
fi

# --- 2: the decision's test -- learner-senior is not offered hint -------------
attempt_case --agent learner-senior
if [ -f "$WS/.jichi/agents/learner-senior.md" ] &&
   grep -q '"name":"read_file"' "$CAP/req.1" 2>/dev/null &&
   ! grep -q '"name":"hint"' "$CAP/req.1" 2>/dev/null; then
    t_ok "attempt --agent learner-senior is fenced to its tools: list (no hint)"
else
    t_fail "learner-senior was offered hint, or the run offered no tools at all
 (profile=$([ -f "$WS/.jichi/agents/learner-senior.md" ] && echo present || echo missing),
  hint=$(grep -c '"name":"hint"' "$CAP/req.1" 2>/dev/null))"
fi

# --- 3: the profile's model: picks the model ----------------------------------
attempt_case --agent pinned
if grep -q '"model":"mock2"' "$CAP/req.1" 2>/dev/null; then
    t_ok "a profile's model: picks the model the attempt calls"
else
    t_fail "the profile's model: was ignored: $(grep -o '"model":"[^"]*"' "$CAP/req.1" 2>/dev/null | head -1)"
fi

# --- 4: an explicit --model wins, and says so ---------------------------------
attempt_case --model m --agent pinned
if grep -q '"model":"mock"' "$CAP/req.1" 2>/dev/null &&
   grep -q 'overrides the profile' "$ERR"; then
    t_ok "an explicit --model beats the profile's model:, and the run says so"
else
    t_fail "--model did not win, or said nothing: $(grep -o '"model":"[^"]*"' "$CAP/req.1" 2>/dev/null | head -1) $(head_bytes 200 "$ERR")"
fi

# --- 5: an unconfigured profile model is named, not silently dropped ----------
attempt_case --agent ghost
if grep -q '"model":"mock"' "$CAP/req.1" 2>/dev/null &&
   grep -q 'not configured' "$ERR"; then
    t_ok "a profile naming an unconfigured model says so and keeps the current one"
else
    t_fail "an unconfigured profile model fell back silently: $(head_bytes 200 "$ERR")"
fi

t_done
