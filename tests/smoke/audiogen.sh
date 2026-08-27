#!/bin/sh
# smoke: the generate_audio (TTS) tool (M32). The mock TTS backend answers
# /v1/audio/speech with RAW BINARY bytes (NULs + 0xff included, via
# mockmodel's body-file): the exact bytes must land at the workspace path
# (binary-safe end to end); an oversized response against a 4-byte
# audioGenMaxBytes is rejected with no file.
# (Port of tests/e2e/audiogen.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# ID3 \0 \0 \xff \xfb hello-audio  (deliberately binary-hostile)
printf 'ID3\000\000\377\373hello-audio' > "$tmp/audio.bin"

cat > "$tmp/tts.mm" <<EOF
wire openai
rule
  status 200
  body-file $tmp/audio.bin
EOF

run_case() { # run_case CAPTAG OUTNAME EXTRA_TOP [TTS_SCRIPT]
    _d="$tmp/$1"; mkdir -p "$_d/capm" "$_d/capa"
    _tts="${4:-$tmp/tts.mm}"
    cat > "$_d/model.mm" <<EOF
wire openai
rule
  count 1
  tool generate_audio {"text":"hello","path":"$2"}
rule
  text AUDIO_DONE
EOF
    mm_start "$_tts" "$_d/capa"
    A_PORT=$MM_PORT; A_PID=$MM_PID
    mm_start "$_d/model.mm" "$_d/capm"
    cat > "$_d/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"tts","provider":"openai","model":"mock-tts",
   "apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","roles":["audio"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0${3:+,$3}}
EOF
    (cd "$ws" && with_deadline 60 "$BIN" --config "$_d/config.json" \
        -q --no-session --auto -p "say hello" \
        < /dev/null > "$_d/out" 2>&1)
    kill "$A_PID" 2>/dev/null
    wait "$A_PID" 2>/dev/null
    mm_stop
}

# 1. happy path: the exact binary bytes land at out.mp3
run_case gen out.mp3 ''
if [ -f "$tmp/gen/capa/req.1" ] && grep -q "AUDIO_DONE" "$tmp/gen/out"; then
    t_ok "the TTS backend was queried and the turn completed"
else
    t_fail "backend not queried or turn incomplete"
fi
if [ -f "$ws/out.mp3" ]; then
    t_ok "generate_audio wrote the file"
else
    t_fail "no out.mp3"
fi
if cmp -s "$ws/out.mp3" "$tmp/audio.bin"; then
    t_ok "the bytes are exact (binary-safe, NULs intact)"
else
    t_fail "written bytes differ from the audio bytes"
fi

# 2. oversized vs a 4-byte cap: rejected, no file
run_case cap big.mp3 '"audioGenMaxBytes":4'
if [ ! -e "$ws/big.mp3" ]; then
    t_ok "an oversized audio response is rejected (no file)"
else
    t_fail "big.mp3 exists despite audioGenMaxBytes=4"
fi

# ---- 3. A FAILING BACKEND NAMES THE STATUS (M500) --------------------------
# THE DEFECT: every failure -- 500, 401, unreachable host -- reported the same
# sentence, "error: audio generation request failed". Measured against a real
# gateway whose TTS answers HTTP 500, a model then retried THREE argument
# variations (a different path, .wav vs .mp3, an explicit voice), spent 428
# output tokens and 27 seconds, and finished by advising the operator to fix an
# API key that was correct. The status was in the LOG the whole time.
#
# The tool result travels in the NEXT request body, so this reads the chat
# mock's capture rather than stdout (the index_coverage.sh lesson).
cat > "$tmp/tts500.mm" <<'EOS'
wire openai
rule
  status 500
  body {"error":{"message":"Internal server error"}}
EOS
run_case fail nope.mp3 '' "$tmp/tts500.mm"
_msg=$(for _f in "$tmp/fail/capm"/req.*; do
           [ -f "$_f" ] || continue
           sed -n 's/.*"role":"tool"[^}]*"content":"\([^"]*\)".*/\1/p' "$_f"
       done)
if printf '%s' "$_msg" | grep -q 'HTTP 500' &&
   printf '%s' "$_msg" | grep -q 'audio/speech'; then
    t_ok "a failing TTS backend is reported with its status and endpoint"
else
    t_fail "the failure did not name the status -- a model can only respond by \
retrying the arguments: $(printf '%s' "$_msg" | head_bytes 200)"
fi
# The anti-retry half, which is the whole value: 5xx must say the arguments are
# not the cause. A message that names the status and stops there still invites
# one more attempt with a different extension.
if printf '%s' "$_msg" | grep -q 'SERVER failure' && [ ! -e "$ws/nope.mp3" ]; then
    t_ok "and it tells the model the arguments are not the cause (no file written)"
else
    t_fail "no anti-retry guidance in the failure, or a file was written anyway: \
$(printf '%s' "$_msg" | head_bytes 200)"
fi

t_done
