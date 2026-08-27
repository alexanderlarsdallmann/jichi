#!/bin/sh
# smoke: the self-hosting dev pack still loads, and still claims what its README
# says it claims (M513).
#
# WHAT THIS EXISTS FOR. examples/self-hosting/ is a workflow -- a compiled jichi
# reviewing jichi's own source -- documented in a 329-line README with measured
# findings. Until now the only thing checked about it was that its JSON parses
# (examples_lint, M382). Nothing checked that the pack LOADS: that the five agent
# profiles and four slash commands are discovered, that the agents the commands
# delegate to exist, or that the read-only agents are still read-only. Those are
# the README's load-bearing claims, and the README is dated: a rename in
# `.jichi/` discovery, or one dropped `readonly:` line, would leave a page that
# reads authoritative and instructions that do not work.
#
# ENTIRELY OFFLINE. `agents` and `commands` are listing subcommands: no key, no
# model call, no network. The configs here name a loopback endpoint or a gateway
# this tier must never dial, and nothing below dials anything -- which is also
# why this can run on the phone rows.
#
# NOT CHECKED, stated rather than implied (the M305 rule): whether a model is a
# good enough reviewer. That is the pack's own subject, it needs a model, and the
# README's "What good looks like" is the human rubric for it. This driver holds
# the harness, not the judgement.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
PACK="$SMOKE_ROOT/examples/self-hosting"
JQ="$SMOKE_TOOLS/jsonq"

# --- 1: the pack is all there -----------------------------------------------
# The floor. Every check below is over these files; if the layout moved, say so
# once, loudly, instead of passing over an empty set (the M295 lesson).
ncfg=$(ls "$PACK"/config.*.json 2>/dev/null | grep -c .)
nag=$(ls "$PACK"/agents/*.md 2>/dev/null | grep -c .)
ncmd=$(ls "$PACK"/commands/*.md 2>/dev/null | grep -c .)
if [ "$ncfg" -ge 3 ] && [ "$nag" -ge 5 ] && [ "$ncmd" -ge 4 ]; then
    t_ok "the pack holds $ncfg configs, $nag agents, $ncmd commands"
else
    t_fail "pack thinner than documented: $ncfg configs (>=3), $nag agents (>=5), $ncmd commands (>=4)"
fi

# --- the workspace a reader is told to build ---------------------------------
# Exactly the README's step 1: copy the assets into a checkout's .jichi/.
ws="$tmp/ws"
mkdir -p "$ws/.jichi/agents" "$ws/.jichi/commands"
cp "$PACK"/agents/*.md "$ws/.jichi/agents/" 2>/dev/null
cp "$PACK"/commands/*.md "$ws/.jichi/commands/" 2>/dev/null

LOCAL="$PACK/config.jichi-dev-local.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$LOCAL" agents) > "$tmp/agents.txt" 2>&1
(cd "$ws" && with_deadline 60 "$BIN" --config "$LOCAL" commands) > "$tmp/cmds.txt" 2>&1

# --- 2: every agent file is discovered --------------------------------------
missing=""
for f in "$PACK"/agents/*.md; do
    n=$(basename "$f" .md)
    grep -q "$n" "$tmp/agents.txt" || missing="$missing $n"
done
if [ -z "$missing" ]; then
    t_ok "all $nag agent profiles are discovered by \`agents\`"
else
    t_fail "agent profile(s) not listed:$missing -- $(head_bytes 120 < "$tmp/agents.txt")"
fi

# --- 3: every command file is discovered ------------------------------------
missing=""
for f in "$PACK"/commands/*.md; do
    n=$(basename "$f" .md)
    grep -q "/$n" "$tmp/cmds.txt" || missing="$missing /$n"
done
if [ -z "$missing" ]; then
    t_ok "all $ncmd slash commands are discovered by \`commands\`"
else
    t_fail "command(s) not listed:$missing -- $(head_bytes 120 < "$tmp/cmds.txt")"
fi

# --- 4: a command may not delegate to an agent that does not exist ----------
# The commands are one line of frontmatter each; the delegation is the whole
# mechanism, and a renamed agent file breaks it silently at run time -- with a
# model attached, i.e. at the worst moment.
# TWO delegation forms, because the pack uses both and a check that knew only
# one would have covered 3 of 4 commands -- with the flagship /review-diff, which
# delegates in PROSE ("Spawn the `c89-reviewer` agent"), in the uncovered half.
bad=""
ndeleg=0
for f in "$PACK"/commands/*.md; do
    for a in $(grep -ohE '^agent: *[a-z0-9-]+' "$f" 2>/dev/null | sed 's/^agent: *//'
               grep -ohE '`[a-z0-9-]+` agent' "$f" 2>/dev/null | tr -d '`' | sed 's/ agent$//'); do
        ndeleg=$((ndeleg + 1))
        [ -f "$PACK/agents/$a.md" ] || bad="$bad $(basename "$f")->$a"
    done
done
if [ "$ndeleg" -lt 5 ]; then
    t_fail "only $ndeleg agent delegations found (floor 5) -- the extraction broke, and an empty set passes every check"
elif [ -z "$bad" ]; then
    t_ok "all $ndeleg delegated agents exist (frontmatter and prose forms)"
else
    t_fail "command(s) naming a missing agent:$bad"
fi

# --- 5: the read-only agents are still read-only ----------------------------
# The README's safety claim, and the reason the review slice is "safe against
# any model". Asserted through the BINARY's own view (the `readonly` marker in
# `agents` output), not by grepping the frontmatter this driver could misread.
ro_bad=""
for n in c89-reviewer arena-auditor committer; do
    grep -A 1 "  $n - " "$tmp/agents.txt" | grep -q 'readonly' || ro_bad="$ro_bad $n"
done
if [ -z "$ro_bad" ]; then
    t_ok "the three review agents are reported readonly by the binary"
else
    t_fail "agent(s) no longer readonly -- the review slice is not safe against any model:$ro_bad"
fi

# --- 6: the write slice's fence is what the README documents -----------------
# A positive allow-list that excludes src/ and include/ is the whole safety
# argument of the write slice. If it ever grows a src/ entry, that is a decision
# somebody must make on purpose, not a diff that slips through.
W="$PACK/config.jichi-dev-write.json"
scope=$("$JQ" .editScope "$W" 2>/dev/null)
verify=$("$JQ" .verify "$W" 2>/dev/null)
revert=$("$JQ" .revertOutOfScope "$W" 2>/dev/null)
case "$scope" in
    *src/*|*include/*) t_fail "the write slice's editScope now reaches core code: $scope" ;;
    *tests/*|*docs/*)
        if [ -n "$verify" ] && [ "$revert" = "true" ]; then
            t_ok "the write slice is fenced to $scope with verify '$verify' and revertOutOfScope"
        else
            t_fail "write slice missing verify/revertOutOfScope (verify='$verify' revert='$revert')"
        fi ;;
    *) t_fail "the write slice's editScope is unreadable or empty: '$scope'" ;;
esac

# --- 7: the local config is runnable by a reader with no gateway -------------
# The point of that file: no key, a loopback endpoint, and a declared context
# window (an undeclared one is the commonest mid-turn 400 -- docs/COMPACTION.md).
key=$("$JQ" .models[0].apiKey "$LOCAL" 2>/dev/null)
keyenv=$("$JQ" .models[0].apiKeyEnv "$LOCAL" 2>/dev/null)
base=$("$JQ" .models[0].apiBase "$LOCAL" 2>/dev/null)
ctx=$("$JQ" .models[0].contextLength "$LOCAL" 2>/dev/null)
if [ -z "$key" ] && [ -z "$keyenv" ] && [ -n "$ctx" ]; then
    case "$base" in
        http://127.0.0.1:*|http://localhost:*)
            t_ok "the local config is keyless, loopback ($base), contextLength $ctx" ;;
        *) t_fail "the local config's endpoint is not loopback: $base" ;;
    esac
else
    t_fail "local config: key='$key' keyEnv='$keyenv' contextLength='$ctx' -- it must need no key and declare a window"
fi

# --- 8: the fence may admit only what the verify runs ----------------------
# M517. The rule, stated in the config's own comment: a path the edit tools may
# write must either be run by the verify command or be unable to affect a gate.
# It was broken in both directions -- `tests/**` admitted 231 smoke drivers and
# `docs/**` admitted 74 assignment graders, against a verify of `make test`,
# which runs the unit suite and neither of those. A weakened gate passed its own
# verify. This check holds the pair CONSISTENT rather than holding either one
# constant: widen the scope to a gate directory and the verify must widen too.
scope=$("$JQ" .editScope "$W" 2>/dev/null)
verify=$("$JQ" .verify "$W" 2>/dev/null)
bad=""
case "$scope" in
    *tests/smoke*|*tests/e2e*|*'tests/**'*)
        case "$verify" in
            *smoke*|*ci*) ;;
            *) bad="$bad scope-admits-smoke/e2e-but-verify-is('$verify')" ;;
        esac ;;
esac
case "$scope" in
    *'docs/**'*|*docs/assignments*)
        case "$verify" in
            *grade*|*curriculum*|*ci*) ;;
            *) bad="$bad scope-admits-assignment-graders-but-verify-is('$verify')" ;;
        esac ;;
esac
if [ -z "$bad" ]; then
    t_ok "the write fence admits nothing its verify does not run ($verify)"
else
    t_fail "fence/verify inconsistent:$bad -- widen both or neither
 (examples/self-hosting/config.jichi-dev-write.json's _editscope_comment)"
fi

t_done
