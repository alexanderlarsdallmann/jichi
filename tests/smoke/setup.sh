#!/bin/sh
# smoke: the `setup` wizard, non-interactive flag mode (M48/M52/M53/M183) --
# produces a valid secret-free config, scaffolds the preset's assets, emits
# an executable start-script; a second run merges instead of clobbering;
# the developer preset auto-detects the language pack; the rewrite journey
# emits referenceRoots. (Port of tests/e2e/setup.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 16
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --list names the presets
ws0=$(smoke_tmp)
(cd "$ws0" && "$BIN" setup --list < /dev/null > "$tmp/list" 2>&1)
missing=""
for role in developer technical-writer tester reviewer generic devops \
            support data; do
    grep -q "$role" "$tmp/list" || missing="$missing $role"
done
if [ -z "$missing" ]; then
    t_ok "setup --list names the presets"
else
    t_fail "presets missing:$missing"
fi

# flag-mode run: config + assets + script + guidance
ws=$(smoke_tmp)
(cd "$ws" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out" 2>&1); rc=$?
cfgp="$ws/local/config.json"
if [ $rc -eq 0 ] && [ -f "$cfgp" ] && "$JQ" -q '.' "$cfgp"; then
    t_ok "setup writes a valid local/config.json"
else
    t_fail "setup rc=$rc or config invalid/missing"
fi

if ! grep -q '"apiKey"' "$cfgp" \
   && [ "$("$JQ" '.models[0].provider' "$cfgp")" = "openai" ] \
   && [ "$("$JQ" '.models[0].model' "$cfgp")" = "gpt-4o" ] \
   && [ "$("$JQ" '.models[0].apiKeyEnv' "$cfgp")" = "OPENAI_API_KEY" ] \
   && "$JQ" '.models[0].roles' "$cfgp" | grep -q "chat"; then
    t_ok "config: apiKeyEnv only (no literal key), model + roles right"
else
    t_fail "config content wrong: $(head_bytes 200 "$cfgp")"
fi

if [ -f "$ws/.jichi/agents/docs-writer-beginner.md" ] \
   && [ -x "$ws/run.sh" ]; then
    t_ok "docs-pack assets scaffolded + executable start-script"
else
    t_fail "assets or run.sh missing/not executable"
fi

if grep -q "Next steps" "$tmp/out" && grep -q "Validation:" "$tmp/out" \
   && grep -q "config: valid JSON" "$tmp/out"; then
    t_ok "next-steps guidance + validation checklist printed"
else
    t_fail "guidance/validation missing from output"
fi

# hand-edit, then a second run merges (M53): the edit is kept
sed 's/^{/{"myCustomKey":123,"references":false,/' "$cfgp" > "$cfgp.new" \
    && mv "$cfgp.new" "$cfgp"
(cd "$ws" && with_deadline 30 "$BIN" setup --preset technical-writer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out2" 2>&1)
if grep -q "merged" "$tmp/out2" \
   && [ "$("$JQ" '.myCustomKey' "$cfgp")" = "123" ] \
   && [ "$("$JQ" '.references' "$cfgp")" = "false" ]; then
    t_ok "second run merges, keeping hand-edited keys"
else
    t_fail "merge clobbered or did not report 'merged'"
fi
if grep -q "exists; --force" "$tmp/out2"; then
    t_ok "the existing start-script is reported, not rewritten"
else
    t_fail "second run rewrote the start script"
fi

# M52: developer preset auto-detects the language pack (python here)
ws2=$(smoke_tmp)
mkdir -p "$ws2/src"
: > "$ws2/src/app.py"
: > "$ws2/main.py"
(cd "$ws2" && with_deadline 30 "$BIN" setup --preset developer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out3" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "python-cli" "$tmp/out3"; then
    t_ok "language auto-detection picked python-cli (M52)"
else
    t_fail "auto-detect rc=$rc: $(head_bytes 150 "$tmp/out3")"
fi
if [ -f "$ws2/.jichi/skills/pytest-triage/SKILL.md" ]; then
    t_ok "python-cli pack assets scaffolded by auto-detect"
else
    t_fail "pytest-triage skill missing"
fi

# M357: the tester preset arms measurement end-to-end -- the config carries
# logging:metrics, test.sh's EXEC line carries the bounded envelope, and the
# exit summary names the pricing keys (shown exactly when telemetry is on,
# because that is when $0 costs look like data).
ws4=$(smoke_tmp)
(cd "$ws4" && with_deadline 30 "$BIN" setup --preset tester \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out5" 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$("$JQ" '.logging.level' "$ws4/local/config.json")" = "metrics" ]; then
    t_ok "tester preset: config carries logging=metrics (M357)"
else
    t_fail "tester rc=$rc; logging: $("$JQ" '.logging' "$ws4/local/config.json")"
fi
if grep -q '^  --budget-tokens 400k --verify-every 8' "$ws4/test.sh"; then
    t_ok "test.sh exec line is budget-bounded (M357)"
else
    t_fail "test.sh lacks the bounded exec row: $(grep -c budget "$ws4/test.sh")"
fi
if grep -q 'inputCostPer1M' "$tmp/out5"; then
    t_ok "exit summary names the pricing keys when telemetry is on (M357)"
else
    t_fail "pricing hint missing from setup output"
fi

# M183: the rewrite journey emits referenceRoots + the port-auditor
ws3=$(smoke_tmp)
(cd "$ws3" && with_deadline 30 "$BIN" setup --preset rewrite \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --reference-root /tmp/old-tree --config-target local \
    < /dev/null > "$tmp/out4" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q '"referenceRoots"' "$ws3/local/config.json" \
   && grep -q "/tmp/old-tree" "$ws3/local/config.json" \
   && [ -f "$ws3/.jichi/agents/port-auditor.md" ]; then
    t_ok "rewrite journey: referenceRoots + port-auditor (M183)"
else
    t_fail "rewrite journey rc=$rc; referenceRoots/port-auditor missing"
fi

# M488: --api-base on the headless path. The interactive wizard prompted for the
# endpoint and model_obj() had always written it, but the flag form had none -- so
# a script following setup's OWN printed guidance got a config pointing at the
# provider's cloud. It bit precisely the presets that exist for locally hosted
# models, which by definition are not at that address.
#
# Two-sided: the flag must put the endpoint in the config, AND omitting it must
# still produce a working config with no apiBase (the provider default), or this
# check would pass on a build that hardcoded one.
ws4=$(smoke_tmp)
(cd "$ws4" && with_deadline 30 "$BIN" setup --preset developer \
    --provider openai --model my-local-model --key-env MY_KEY \
    --api-base http://127.0.0.1:1234/v1 --config-target local \
    < /dev/null > "$tmp/out6" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q '"apiBase": "http://127.0.0.1:1234/v1"' "$ws4/local/config.json"; then
    t_ok "--api-base reaches the generated config (M488)"
else
    t_fail "setup --api-base rc=$rc; apiBase missing from the generated config: $(grep -c apiBase "$ws4/local/config.json" 2>/dev/null)"
fi

ws5=$(smoke_tmp)
(cd "$ws5" && with_deadline 30 "$BIN" setup --preset developer \
    --provider openai --model gpt-4o --key-env OPENAI_API_KEY \
    --config-target local < /dev/null > "$tmp/out7" 2>&1); rc=$?
if [ $rc -eq 0 ] && ! grep -q 'apiBase' "$ws5/local/config.json"; then
    t_ok "omitting --api-base leaves the provider default (no apiBase key)"
else
    t_fail "without --api-base the config still names an endpoint -- rc=$rc"
fi

# The printed non-TTY guidance must NAME the flag. The defect was not only the
# missing flag: setup told a script exactly which flags to pass, and that list
# was the thing that produced the wrong config.
if grep -q -- '--api-base' "$tmp/out8" 2>/dev/null || \
   (cd "$(smoke_tmp)" && with_deadline 20 "$BIN" setup --preset developer \
      < /dev/null 2>&1 | grep -q -- '--api-base'); then
    t_ok "the non-TTY guidance names --api-base"
else
    t_fail "setup's not-a-TTY message still omits --api-base, so a script copying it lands on the cloud"
fi

t_done
