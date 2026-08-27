#!/bin/sh
# smoke: an API key pasted where a VARIABLE NAME belongs is refused, and the
# refusal never repeats the key (M326e).
#
# The reported bug: `setup`'s prompt read "API key env var", so a user holding
# an sk-... pasted it. Nothing validated it, and the key was then echoed as
# typed, written into local/config.json as "apiKeyEnv", printed again by the
# validation block and a third time under the next-steps line
# "# your API key (never written to config)" -- beside a generated config
# comment promising secrets were never stored there. `doctor` then reported
# "no API key ... set apiKey/apiKeyEnv", which is the one thing the user
# believed they had done.
#
# Both halves are pinned here because they fail independently: setup must
# refuse the input, and doctor must diagnose a config that already contains it
# (hand-written, or produced by an older build).
#
# THE SECRET-NOT-ECHOED CHECKS ARE THE POINT. A diagnostic that names the
# mistake by quoting it hands the key to a terminal, a scrollback, and a bug
# report -- so "it errored" is not enough; it must error quietly.
. "$(dirname "$0")/_smoke.sh"

t_plan 13
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

KEY='sk-live-SMOKESECRET123'

# --- 1-4: setup refuses a key-shaped --key-env ------------------------------
(cd "$ws" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model gpt-4o --key-env "$KEY" < /dev/null \
    > "$tmp/s.out" 2> "$tmp/s.err")
rc=$?

if [ "$rc" -ne 0 ]; then
    t_ok "setup refuses a key-shaped --key-env (exit $rc)"
else
    t_fail "setup accepted a pasted key as a variable name"
fi

if ! grep -q "SMOKESECRET" "$tmp/s.out" "$tmp/s.err"; then
    t_ok "the refusal does not echo the key"
else
    t_fail "the key was printed back: $(grep -h SMOKESECRET "$tmp/s.out" "$tmp/s.err" | head -1)"
fi

if [ ! -f "$ws/local/config.json" ]; then
    t_ok "no config was written from the refused answer"
else
    t_fail "a config was written anyway: $(grep -c apiKeyEnv "$ws/local/config.json")"
fi

# The message has to say what to do, not merely that something is wrong.
if grep -q 'NAME of an environment variable' "$tmp/s.err" &&
   grep -q 'JICHI_API_KEY' "$tmp/s.err"; then
    t_ok "the refusal names the fix and an example variable"
else
    t_fail "unhelpful refusal: $(head_bytes 160 "$tmp/s.err")"
fi

# --- 5: a real variable name is still accepted ------------------------------
ws2=$(smoke_tmp)
(cd "$ws2" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model gpt-4o --key-env JICHI_API_KEY < /dev/null \
    > "$tmp/ok.out" 2>&1)
rc=$?
if [ "$rc" -eq 0 ] && [ -f "$ws2/local/config.json" ] &&
   grep -q '"apiKeyEnv": *"JICHI_API_KEY"' "$ws2/local/config.json"; then
    t_ok "a valid variable name still produces a config"
else
    t_fail "valid --key-env broke: rc=$rc $(head_bytes 160 "$tmp/ok.out")"
fi

# --- 6-8: doctor diagnoses a config that already carries the mistake --------
cat > "$tmp/bad.json" <<EOF
{"models":[{"name":"work","provider":"openai","model":"gpt-4o",
"apiBase":"https://example.invalid/v1","apiKeyEnv":"$KEY"}],
"lowResource":false}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/bad.json" doctor \
    < /dev/null > "$tmp/d.out" 2>&1)
rc=$?

if grep -q 'apiKeyEnv is not a usable environment-variable name' "$tmp/d.out"; then
    t_ok "doctor names the real defect, not just 'no API key'"
else
    t_fail "doctor did not diagnose it: $(grep -i 'api key' "$tmp/d.out" | head -1)"
fi

# FAIL, not WARN: getenv can never resolve such a name, so the run cannot work.
if [ "$rc" -eq 1 ] && grep -q 'problem' "$tmp/d.out"; then
    t_ok "it is a FAIL (doctor exits 1)"
else
    t_fail "expected a failing doctor, got rc=$rc"
fi

if ! grep -q "SMOKESECRET" "$tmp/d.out"; then
    t_ok "doctor does not print the suspected key"
else
    t_fail "doctor echoed the key: $(grep SMOKESECRET "$tmp/d.out" | head -1)"
fi


# --- 9-13: --context-length <tokens|auto> ----------------------------------
# Deliberately OFFLINE checks only. `auto` resolves its prerequisites BEFORE it
# would issue a request, so both refusals are testable here; the successful
# probe needs a LiteLLM gateway and is verified by hand, not by this tier -- a
# smoke driver that needs the internet is a driver that fails on the platform
# rows this project exists to measure.
#
# The pairing is the point: a NUMBER must stay offline and exact (the mode for a
# script or an agent driving another jichi), and `auto` must refuse rather than
# quietly write nothing when it cannot even ask -- answering an explicit flag
# with silence is the M458 failure.
ws6=$(smoke_tmp)
(cd "$ws6" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model jlu/qwen3.8-27b --key-env JICHI_API_KEY \
    --context-length 196608 < /dev/null > "$tmp/cl.out" 2>&1)
rc=$?
if [ "$rc" -eq 0 ] &&
   grep -q '"contextLength": *196608' "$ws6/local/config.json"; then
    t_ok "--context-length <n> writes the per-model window, offline"
else
    t_fail "explicit --context-length failed: rc=$rc $(head_bytes 160 "$tmp/cl.out")"
fi

# No flag, no key: an unknown window must stay unknown rather than be guessed,
# because a guessed number is indistinguishable from a measured one later.
ws7=$(smoke_tmp)
(cd "$ws7" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model gpt-4o --key-env JICHI_API_KEY \
    < /dev/null > /dev/null 2>&1)
if ! grep -q 'contextLength' "$ws7/local/config.json"; then
    t_ok "no --context-length means no contextLength key (nothing is guessed)"
else
    t_fail "a window was invented: $(grep contextLength "$ws7/local/config.json")"
fi

# A malformed value is a usage error, not a silent zero.
ws8=$(smoke_tmp)
(cd "$ws8" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model m --context-length 12ab < /dev/null \
    > /dev/null 2> "$tmp/cl2.err")
rc=$?
if [ "$rc" -eq 2 ] && grep -q 'positive token count' "$tmp/cl2.err"; then
    t_ok "a malformed --context-length is refused with the accepted forms"
else
    t_fail "bad value not refused: rc=$rc $(head_bytes 160 "$tmp/cl2.err")"
fi

# `auto` with nothing to ask: refuse, and name what is missing. No request is
# issued, which is why this is safe in an offline tier.
ws9=$(smoke_tmp)
(cd "$ws9" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --provider openai --model m --key-env JICHI_API_KEY --context-length auto \
    < /dev/null > /dev/null 2> "$tmp/cl3.err")
rc=$?
if [ "$rc" -eq 2 ] && grep -q -- '--api-base' "$tmp/cl3.err"; then
    t_ok "--context-length auto refuses without an endpoint, and says so"
else
    t_fail "auto did not refuse: rc=$rc $(head_bytes 160 "$tmp/cl3.err")"
fi

# `auto` with an endpoint but no exported key: refuse, and NAME the variable --
# the failure is in the environment, so the message has to say which name it
# looked for. Still no request.
ws10=$(smoke_tmp)
# with_deadline is a shell FUNCTION, so env(1) cannot invoke it (rc 127).
# Unset in the subshell instead: same guarantee, and the deadline survives.
(cd "$ws10" && unset JC_SMOKE_ABSENT_KEY; with_deadline 60 "$BIN" setup \
    --non-interactive --preset generic --provider openai --model m \
    --api-base http://127.0.0.1:1/v1 --key-env JC_SMOKE_ABSENT_KEY \
    --context-length auto < /dev/null > /dev/null 2> "$tmp/cl4.err")
rc=$?
if [ "$rc" -eq 2 ] && grep -q 'JC_SMOKE_ABSENT_KEY' "$tmp/cl4.err"; then
    t_ok "auto refuses when the named key variable is unset, and names it"
else
    t_fail "auto ignored the missing key: rc=$rc $(head_bytes 200 "$tmp/cl4.err")"
fi

t_done
