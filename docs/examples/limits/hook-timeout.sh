#!/bin/sh
# hook-timeout.sh JICHI MOCKMODEL -- show by effect what a hook's `timeout` of "absent" and of 0 mean (2026-11-21, the
# supervisor, for the limits census and its learner page). One SessionStart hook sleeps 31 s, then prints "late". Three
# runs of `jichi -p` against a local mock model (no network, no key) differ only in that hook's timeout:
#   A  no "timeout" key        B  "timeout": 0        C  "timeout": 60 (the positive control: the hook can finish)
# Each run's wall time, jichi's own telemetry line for the hook (if any) and its stderr warning are printed.
# Run it from the top of a jichi checkout, after `make jichi smoke-tools`:
#   sh docs/examples/limits/hook-timeout.sh ./jichi tests/tools/mockmodel < /dev/null
# Everything happens in a throwaway directory under /tmp, which is removed at the end. No caps on the runs themselves.
# The page that explains it: docs/LIMITS.md, section 5.
set -u
J=${1:?usage: hook-timeout.sh JICHI MOCKMODEL}; MM=${2:?usage: hook-timeout.sh JICHI MOCKMODEL}
# Absolute paths: each run starts inside its own throwaway workspace, so a relative ./jichi would not be found there.
J=$(cd "$(dirname "$J")" && pwd)/$(basename "$J") || exit 1; MM=$(cd "$(dirname "$MM")" && pwd)/$(basename "$MM") || exit 1
W=$(mktemp -d /tmp/jhook.XXXXXX) || exit 1
printf 'wire openai\nrule\n  text hello\n' > "$W/mm.script"; mkdir -p "$W/cap"
"$MM" --script "$W/mm.script" --capture "$W/cap" --port-file "$W/port" > "$W/mm.out" 2>&1 < /dev/null &
mm=$!
n=0; until [ -s "$W/port" ] || [ $n -ge 100 ]; do sleep 0.1; n=$((n + 1)); done
[ -s "$W/port" ] || { echo "mockmodel did not start: $(cat "$W/mm.out")"; rm -rf "$W"; exit 1; }
port=$(cat "$W/port")
for case in A B C; do
    case $case in
        A) t='' ; label='no "timeout" key' ;;
        B) t=', "timeout": 0' ; label='"timeout": 0' ;;
        C) t=', "timeout": 60' ; label='"timeout": 60' ;;
    esac
    mkdir -p "$W/$case/home" "$W/$case/ws"
    cat > "$W/$case/config.json" <<EOF
{ "models": [ { "name": "mock", "provider": "openai", "model": "mock/m", "apiBase": "http://127.0.0.1:$port/v1",
                "apiKeyEnv": "MOCK_KEY", "roles": ["chat"], "contextLength": 32768, "maxTokens": 1024,
                "inputCostPer1M": 0, "outputCostPer1M": 0 } ],
  "hooksEnabled": true,
  "hooks": { "SessionStart": [ { "commands": [ { "shell": "sleep 31; echo late"$t } ] } ] } }
EOF
    s=$(date +%s)
    ( cd "$W/$case/ws" && env HOME="$W/$case/home" MOCK_KEY=x "$J" --config "$W/$case/config.json" \
        --log "$W/$case/telemetry.jsonl" --log-level full -p "say hello" < /dev/null > "$W/$case/out.txt" 2> "$W/$case/err.txt" )
    rc=$?; e=$(( $(date +%s) - s ))
    hook=$(/usr/bin/grep -h '"hook":' "$W/$case/telemetry.jsonl" 2>/dev/null | head -n 1 | cut -c1-220)
    echo "$case ($label) rc=$rc wall=${e}s hook-telemetry: ${hook:-none}"
    /usr/bin/grep -h -i 'hook' "$W/$case/err.txt" | head -n 2 | sed 's/^/    stderr: /'
done
kill $mm 2>/dev/null; wait $mm 2>/dev/null
rm -rf "$W"
