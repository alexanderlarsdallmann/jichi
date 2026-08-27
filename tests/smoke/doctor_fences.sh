#!/bin/sh
# smoke: `doctor` lints the `tools:` fences on agent profiles and skills (M285).
#
# A profile's `tools:` is an ENFORCED allow-list for a subagent (and a skill's is,
# with restrict-tools). Two ways an entry can be dead, both silent:
#
#   1. it names no tool that can ever match. Note a fence is exact strcmp
#      (jc_tool_allowed) while a CALL resolves aliases (jc_tool_canonical_name),
#      so `todo_write` works as a call and is dead in a fence -- an asymmetry
#      nobody spots by reading.
#   2. it names a real tool the resolved tool profile never advertises. This is
#      what made format_file fail 0/3 in the zigodot dogfood project while its
#      profiles declared the LSP navigation tools; finding it took reading 31 MB
#      of telemetry.
#
# Both are WARNINGS, not failures: unlike M284's unresolvable model selector
# (which aborts the subagent), a dead fence entry leaves a degraded-but-working
# profile. So the assertions here are textual plus a problem-count check proving
# the warning does NOT raise the exit code.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
tmp=$(smoke_tmp)
smoke_home

cfg="$tmp/c.json"
cat > "$cfg" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"tools":[{"name":"my_custom_tool","shell":"echo hi"}],
"toolProfile":"full","snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"maxRetries":0}
EOF

ws="$tmp/ws"
mkdir -p "$ws/.jichi/agents" "$ws/.jichi/skills/probe"

doctor_run() {
    ( cd "$ws" && "$BIN" --config "$1" doctor ) < /dev/null > "$tmp/out" 2>&1
    _dr_n=$(sed -n 's/.*, \([0-9][0-9]*\) problem.*/\1/p' "$tmp/out")
    [ -n "$_dr_n" ] || _dr_n=0
    printf '%s\n' "$_dr_n"
}

# --- a clean fence says so ---------------------------------------------------
cat > "$ws/.jichi/agents/clean.md" <<'EOF'
---
description: fences itself to real tools only
tools:
  - read_file
  - search_code
  - my_custom_tool
  - someserver__remote_thing
---
Body.
EOF
base=$(doctor_run "$cfg")
if grep -q "asset tool fences resolve" "$tmp/out"; then
    t_ok "a clean fence reports resolving (baseline $base problem(s))"
else
    t_fail "a clean fence was not reported as resolving"
fi
# The three non-builtins above must NOT be flagged: a config-declared user tool,
# and an MCP-namespaced name doctor cannot confirm without connecting.
if ! grep -q "my_custom_tool" "$tmp/out" && \
   ! grep -q "someserver__remote_thing" "$tmp/out"; then
    t_ok "user tools and MCP-namespaced names are not false-flagged"
else
    t_fail "a user tool or an MCP-namespaced name was wrongly flagged"
fi

# --- a foreign vocabulary is caught, with the real name suggested -----------
cat > "$ws/.jichi/agents/foreign.md" <<'EOF'
---
description: fences itself to another agent's tool vocabulary
tools:
  - read_file
  - grep
  - todo_write
---
Body.
EOF
n=$(doctor_run "$cfg")
if grep -q "asset tool fence names no such tool" "$tmp/out"; then
    t_ok "a fence entry that can never match is reported"
else
    t_fail "a dead fence entry was not reported"
fi
if grep -q "'grep' (use 'search_code')" "$tmp/out"; then
    t_ok "a hint-only guess suggests the real tool"
else
    t_fail "no suggestion offered for 'grep'"
fi
# The alias case is the subtle one: todo_write resolves as a CALL but not here.
if grep -q "'todo_write' (use 'todowrite')" "$tmp/out"; then
    t_ok "a silent alias is flagged with its canonical name"
else
    t_fail "the alias 'todo_write' was not flagged, or lacked its canonical name"
fi
if [ "$n" -eq "$base" ]; then
    t_ok "a dead fence entry is a warning, not a problem ($n)"
else
    t_fail "a dead fence entry changed the problem count ($base -> $n)"
fi
rm -f "$ws/.jichi/agents/foreign.md"

# --- tools the core profile never advertises --------------------------------
# Same assets, but toolProfile core: the LSP/git entries become unreachable.
cat > "$ws/.jichi/agents/navigator.md" <<'EOF'
---
description: wants LSP navigation
tools:
  - read_file
  - find_definition
  - find_references
---
Body.
EOF
cat > "$ws/.jichi/skills/probe/SKILL.md" <<'EOF'
---
name: probe
description: a skill fencing itself to a non-core tool
restrict-tools: true
tools:
  - read_file
  - git_diff
---
Body.
EOF
cat > "$tmp/core.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"toolProfile":"core","snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"maxRetries":0}
EOF
doctor_run "$tmp/core.json" > /dev/null
if grep -q "unreachable under the core profile" "$tmp/out"; then
    t_ok "non-core fenced tools are reported under toolProfile core"
else
    t_fail "non-core fenced tools were not reported under core"
fi
# Skills are linted too, not just agent profiles.
if grep -q "skill probe" "$tmp/out"; then
    t_ok "skill fences are linted alongside agent profiles"
else
    t_fail "a skill's fence was not linted"
fi

t_done
