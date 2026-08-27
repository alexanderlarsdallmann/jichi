#!/bin/sh
# smoke: `doctor` classifies the model selectors used by project assets and the
# routing tiers (M284).
#
# Why a lint and not an audit: an agent profile's `model:`, a command's `model:`,
# and routing.fast/strong all resolve at USE time (index, then a name/id
# substring, then a role). A typo therefore surfaced as "error: no model matches
# 'x'" from inside a spawned subagent -- mid-run, in an --auto turn that then
# spends budget recovering -- and a typo'd routing tier surfaced not at all
# (jc_config_routing_resolve just returns 0 and the run quietly never escalates).
#
# No model is contacted: doctor's selector pass is pure resolution against the
# loaded config. The apiBase points at port 9 (discard) on purpose, so the
# unreachable-server FAIL is a CONSTANT here -- which is why every assertion
# below is differential (problem count vs the no-assets baseline) or textual,
# never a bare exit code.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
tmp=$(smoke_tmp)
smoke_home

cfg="$tmp/c.json"
cat > "$cfg" <<'EOF'
{"models":[
{"name":"fast","provider":"openai","model":"mock-gemma-4b",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x",
 "roles":["chat","summarize"]},
{"name":"strong","provider":"openai","model":"mock-gemma-31b",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x"}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,"maxRetries":0}
EOF

ws="$tmp/ws"
mkdir -p "$ws/.jichi/agents" "$ws/.jichi/commands"

# doctor_run -> stdout+stderr in $tmp/out; echoes the reported problem count.
doctor_run() {
    ( cd "$ws" && "$BIN" --config "$cfg" doctor ) < /dev/null > "$tmp/out" 2>&1
    _dr_n=$(sed -n 's/.*, \([0-9][0-9]*\) problem.*/\1/p' "$tmp/out")
    [ -n "$_dr_n" ] || _dr_n=0
    printf '%s\n' "$_dr_n"
}

# --- baseline: no assets, no routing block ---------------------------------
base=$(doctor_run)
if grep -q "model selectors resolve" "$tmp/out"; then
    t_ok "clean config reports selectors resolving (baseline $base problem(s))"
else
    t_fail "clean config did not report the selector check"
fi

# --- an unresolvable agent selector is a FAIL ------------------------------
cat > "$ws/.jichi/agents/typo.md" <<'EOF'
---
description: names a model that is not configured
model: gpt-5-mni
---
Body.
EOF
n=$(doctor_run)
if grep -q "unresolvable model selector" "$tmp/out"; then
    t_ok "typo'd agent selector is reported as unresolvable"
else
    t_fail "typo'd agent selector was not reported"
fi
if grep -q "agent typo: model 'gpt-5-mni'" "$tmp/out"; then
    t_ok "the finding names the profile and the selector"
else
    t_fail "the finding did not name the profile and selector"
fi
if [ "$n" -eq $((base + 1)) ]; then
    t_ok "problem count rose by exactly one ($base -> $n)"
else
    t_fail "problem count $n, expected $((base + 1))"
fi
rm -f "$ws/.jichi/agents/typo.md"

# --- an ambiguous selector is a WARN, not a FAIL ---------------------------
# "mock-gemma" substring-matches BOTH models; the first wins by position, which
# is resolvable but never what was meant.
cat > "$ws/.jichi/agents/ambig.md" <<'EOF'
---
description: substring-matches two configured models
model: mock-gemma
---
Body.
EOF
n=$(doctor_run)
if grep -q "wins by position" "$tmp/out"; then
    t_ok "ambiguous selector names the positional winner"
else
    t_fail "ambiguous selector was not reported"
fi
if [ "$n" -eq "$base" ]; then
    t_ok "ambiguity is a warning, not a problem ($n)"
else
    t_fail "ambiguity changed the problem count ($base -> $n)"
fi
rm -f "$ws/.jichi/agents/ambig.md"

# --- a role that no configured model declares ------------------------------
cat > "$ws/.jichi/agents/roleless.md" <<'EOF'
---
description: names a real role that no model holds
model: rerank
---
Body.
EOF
doctor_run > /dev/null
if grep -q "names a role no configured model declares" "$tmp/out"; then
    t_ok "unstaffed role selector is distinguished from a typo"
else
    t_fail "unstaffed role selector was not distinguished"
fi
rm -f "$ws/.jichi/agents/roleless.md"

# --- a routing tier: the silent-failure case -------------------------------
cat > "$tmp/c2.json" <<'EOF'
{"models":[
{"name":"fast","provider":"openai","model":"mock-gemma-4b",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]},
{"name":"strong","provider":"openai","model":"mock-gemma-31b",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x"}],
"routing":{"fast":"fast","strong":"stronk"},
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,"maxRetries":0}
EOF
( cd "$ws" && "$BIN" --config "$tmp/c2.json" doctor ) < /dev/null \
    > "$tmp/out" 2>&1
if grep -q "routing strong: model 'stronk'" "$tmp/out"; then
    t_ok "a typo'd routing tier is caught at config time"
else
    t_fail "a typo'd routing tier was not caught"
fi

t_done
