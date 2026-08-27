#!/bin/sh
# smoke: doctor's "paying for tools you never call" check (M316).
#
# This is ADVICE, so the checks are about the bar it clears rather than the
# arithmetic (which tests/smoke/context_tools_use.sh already pins):
#
#   1. no telemetry -> an OK line saying the check is quiet and why. Silence
#      would be indistinguishable from a pass.
#   2. THIN evidence (below the session/call thresholds) -> still quiet, and
#      it prints the numbers so the user can see how far short they are. This
#      is the check that matters: advising off one session is the failure mode
#      the whole design exists to avoid.
#   3. ENOUGH evidence with unused tools -> a WARN naming the count and the
#      per-call cost, and pointing at the LEVER (toolProfile) rather than at
#      individual tools.
#   4. it never FAILs: doctor still exits 0. Nothing here is broken.
#   5. --unattended does NOT escalate it (that flag is for posture problems;
#      token efficiency is not one).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"contextLimit":32000,"maxRetries":0}
EOF

doc() {
    (cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
        --no-lite doctor "$@" < /dev/null 2>&1)
}

wsc=$(cd "$ws" && pwd -P)

# writelog SESSIONS CALLS_PER_SESSION -- a log with that many distinct sids,
# each making that many read_file calls. Only read_file is ever called, so
# every other advertised tool is unused.
writelog() {
    _ns=$1; _nc=$2
    mkdir -p "$HOME/.jichi.d/telemetry"
    : > "$HOME/.jichi.d/telemetry/probe.jsonl"
    _s=1
    while [ "$_s" -le "$_ns" ]; do
        printf '{"v":1,"ts":178600000%s,"sid":"s%s","ws":"%s","seq":1,"event":"turn_start","depth":0,"turn":1}\n' \
            "$_s" "$_s" "$wsc" >> "$HOME/.jichi.d/telemetry/probe.jsonl"
        _c=1
        while [ "$_c" -le "$_nc" ]; do
            printf '{"v":1,"ts":178600000%s,"sid":"s%s","ws":"%s","seq":%s,"event":"tool_call","depth":0,"turn":1,"name":"read_file","ok":true,"duration_ms":1.0,"output_bytes":10}\n' \
                "$_s" "$_s" "$wsc" "$((_c + 1))" \
                >> "$HOME/.jichi.d/telemetry/probe.jsonl"
            _c=$((_c + 1))
        done
        _s=$((_s + 1))
    done
}

# --- 1: no telemetry ---------------------------------------------------------
rm -rf "$HOME/.jichi.d/telemetry"
if doc | grep -q "tool use: no telemetry for this project"; then
    t_ok "no telemetry: the check says it is quiet, and why"
else
    t_fail "no-telemetry case is silent (indistinguishable from a pass)"
fi

# --- 2: thin evidence -- one session, few calls ------------------------------
writelog 1 3
thin=$(doc)
if printf '%s\n' "$thin" | grep -q "not enough telemetry to judge" && \
   ! printf '%s\n' "$thin" | grep -q "paying for tools you never call"; then
    t_ok "one session, 3 calls: quiet, and does NOT advise"
else
    t_fail "advised (or went silent) on one session of evidence"
fi
if printf '%s\n' "$thin" | grep -q "1 session(s), 3 tool call(s)"; then
    t_ok "the shortfall is printed, not just asserted"
else
    t_fail "thin-evidence line does not show the numbers"
fi

# --- 3: enough evidence ------------------------------------------------------
writelog 4 8
full=$(doc)
if printf '%s\n' "$full" | grep -q "paying for tools you never call" && \
   printf '%s\n' "$full" | grep -q "toolProfile" && \
   printf '%s\n' "$full" | grep -qE "never called across 4 sessions"; then
    t_ok "4 sessions, 32 calls: warns, names the lever, states the evidence"
else
    t_fail "no warning with sufficient evidence: $(printf '%s\n' "$full" | \
            grep -i 'tool use' | head -1)"
fi

# --- 4: never a FAIL ---------------------------------------------------------
(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" --no-lite \
    doctor < /dev/null) > "$tmp/d.out" 2>&1
rc=$?
# The fixture's unreachable endpoint is a pre-existing FAIL on some configs, so
# assert on THIS check's severity rather than on the exit code: jc_doctor_render
# leads a WARN with "!" and a FAIL with "x"/"U+2717", so the marker on this line
# is the whole question. (rc is captured above and reported on failure only,
# since it belongs to the whole checklist rather than to this check.)
line=$(grep "paying for tools you never call" "$tmp/d.out" | head -1)
case "$line" in
    "! "*)  t_ok "reported as a warning, not a failure" ;;
    "x "*|"✗ "*) t_fail "rendered as a FAILURE: $line" ;;
    *)      t_fail "unexpected severity marker (rc=$rc): $line" ;;
esac

# --- 5: --unattended does not escalate it -----------------------------------
un=$(doc --unattended)
if printf '%s\n' "$un" | grep -q "paying for tools you never call" && \
   ! printf '%s\n' "$un" | grep -qE "^(x|✗) paying for tools"; then
    t_ok "--unattended leaves it a warning (that flag is for posture)"
else
    t_fail "--unattended escalated a token-efficiency warning"
fi

t_done
