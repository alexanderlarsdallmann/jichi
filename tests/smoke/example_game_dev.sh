#!/bin/sh
# smoke: the examples/game-dev scaffold slice stays valid.
#
# The ninth pilot domain-scaffold (docs/proposals/2026-08-domain-scaffolds.md)
# lives in examples/ until exercised, then graduates to a compiled-in `init
# game-dev` pack. This driver keeps it from rotting: it stages the assets as a real
# .jichi/ project and asserts jichi's own asset-frontmatter validator (jc_assetval,
# the engine behind `doctor`) passes them, and that the example config is valid
# JSON. (A game engine is not needed to validate a scaffold.) A negative check
# proves the validator has teeth.
. "$(dirname "$0")/_smoke.sh"

JQ="$SMOKE_TOOLS/jsonq"
EX="$SMOKE_ROOT/examples/game-dev"

t_plan 5

# 1. the expected assets are all present (2 agents, 4 commands, 3 skills).
nag=$(ls "$EX/agents"/*.md 2>/dev/null | grep -c .)
ncmd=$(ls "$EX/commands"/*.md 2>/dev/null | grep -c .)
nsk=$(ls "$EX/skills"/*/SKILL.md 2>/dev/null | grep -c .)
if [ "$nag" -eq 2 ] && [ "$ncmd" -eq 4 ] && [ "$nsk" -eq 3 ]; then
    t_ok "assets present (2 agents, 4 commands, 3 skills)"
else
    t_fail "asset count off (agents=$nag commands=$ncmd skills=$nsk)"
fi

# 2. the example config is valid JSON with the documented shape.
if [ "$("$JQ" '.models[0].roles[0]' "$EX/config.example.json" 2>/dev/null)" = "chat" ]; then
    t_ok "config.example.json is valid JSON with a chat model"
else
    t_fail "config.example.json is not the expected valid JSON"
fi

# stage the assets as a real .jichi/ project.
ws=$(smoke_tmp)
mkdir -p "$ws/.jichi"
cp -r "$EX/agents" "$ws/.jichi/agents"
cp -r "$EX/commands" "$ws/.jichi/commands"
cp -r "$EX/skills" "$ws/.jichi/skills"
cp "$EX/AGENTS.md" "$ws/"

# 3. jichi's own validator passes the staged assets.
out=$(cd "$ws" && "$BIN" doctor 2>&1)
if printf '%s\n' "$out" | grep -qi 'asset frontmatter valid'; then
    t_ok "doctor: asset frontmatter valid"
else
    t_fail "doctor did not report the assets valid"
    printf '%s\n' "$out" | grep -i asset | sed 's/^/# /'
fi

# 4. doctor sees the project assets (non-zero count).
if printf '%s\n' "$out" | grep -qi 'project assets'; then
    t_ok "doctor: project assets discovered"
else
    t_fail "doctor did not discover the project assets"
fi

# 5. teeth: a broken asset (an unknown frontmatter key) must be REJECTED, so the
#    green result above means something.
bad=$(smoke_tmp)
mkdir -p "$bad/.jichi/agents"
printf -- '---\ndescription: broken\nbogus_key: nope\n---\nbody\n' \
    > "$bad/.jichi/agents/broken.md"
badout=$(cd "$bad" && "$BIN" doctor 2>&1)
if printf '%s\n' "$badout" | grep -qi 'asset frontmatter valid'; then
    t_fail "validator did NOT catch an unknown frontmatter key -- no teeth"
else
    t_ok "validator rejects a broken asset (has teeth)"
fi

t_done
