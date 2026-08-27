#!/bin/sh
# smoke: the transcribe_audio tool (M33). The mock STT backend answers
# /v1/audio/transcriptions with {"text": ...}; the tool must upload the
# workspace clip as a multipart body (asserted in the captured request:
# a multipart boundary + the clip's RIFF bytes) and the transcript must
# be fed back to the model, with the turn completing.
# (Port of tests/e2e/transcribe.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capm" "$tmp/capt"
printf 'RIFF\000\000\000\000WAVEfmt ' > "$ws/clip.wav"

cat > "$tmp/stt.mm" <<'EOF'
wire openai
rule
  status 200
  body {"text":"the quick brown fox"}
EOF
cat > "$tmp/model.mm" <<'EOF'
wire openai
rule
  count 1
  tool transcribe_audio {"path":"clip.wav"}
rule
  text TRANSCRIBE_DONE
EOF

mm_start "$tmp/stt.mm" "$tmp/capt"
T_PORT=$MM_PORT; T_PID=$MM_PID
mm_start "$tmp/model.mm" "$tmp/capm"

cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"stt","provider":"openai","model":"whisper-1",
   "apiBase":"http://127.0.0.1:$T_PORT/v1","apiKey":"x",
   "roles":["transcribe"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "transcribe the clip" < /dev/null); rc=$?
kill "$T_PID" 2>/dev/null
wait "$T_PID" 2>/dev/null
mm_stop

if [ -f "$tmp/capt/req.1" ]; then
    t_ok "the transcription backend was queried"
else
    t_fail "no request reached the STT backend (rc=$rc)"
fi
if grep -q "multipart/form-data" "$tmp/capt/req.1" 2>/dev/null \
   && grep -q "WAVEfmt" "$tmp/capt/req.1" 2>/dev/null; then
    t_ok "the clip went up as a multipart upload with its bytes"
else
    t_fail "multipart body or clip bytes missing from the upload"
fi
if grep -q "the quick brown fox" "$tmp/capm/req.2" 2>/dev/null; then
    t_ok "the transcript was fed back to the model"
else
    t_fail "transcript missing from the second model request"
fi
case "$out" in
    *TRANSCRIBE_DONE*) t_ok "the turn completed" ;;
    *) t_fail "turn incomplete: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# ---- 5. a failing STT backend names its status (M500) ----------------------
# Same defect as generate_audio's, and found the same way: "error: transcription
# request failed" was the message for a 500, a 401 and an unreachable host
# alike, so a model's only available response was to retry the arguments. The
# 401 case is chosen here deliberately -- it is the one where the operator, not
# the model, must act, and the old message could not say so.
tmp2=$(smoke_tmp); ws2=$(smoke_tmp)
mkdir -p "$tmp2/capm" "$tmp2/capt"
printf 'RIFF\000\000\000\000WAVEfmt ' > "$ws2/clip.wav"
cat > "$tmp2/stt.mm" <<'EOS'
wire openai
rule
  status 401
  body {"error":{"message":"invalid api key"}}
EOS
cat > "$tmp2/model.mm" <<'EOS'
wire openai
rule
  count 1
  tool transcribe_audio {"path":"clip.wav"}
rule
  text DONE
EOS
mm_start "$tmp2/stt.mm" "$tmp2/capt"
T2_PORT=$MM_PORT; T2_PID=$MM_PID
mm_start "$tmp2/model.mm" "$tmp2/capm"
cat > "$tmp2/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"stt","provider":"openai","model":"whisper-1",
   "apiBase":"http://127.0.0.1:$T2_PORT/v1","apiKey":"x",
   "roles":["transcribe"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp2/config.json" \
    -q --no-session --auto -p "transcribe the clip" < /dev/null > /dev/null 2>&1)
kill "$T2_PID" 2>/dev/null
wait "$T2_PID" 2>/dev/null
mm_stop
_m=$(for _f in "$tmp2/capm"/req.*; do
         [ -f "$_f" ] || continue
         sed -n 's/.*"role":"tool"[^}]*"content":"\([^"]*\)".*/\1/p' "$_f"
     done)
if printf '%s' "$_m" | grep -q 'HTTP 401' &&
   printf '%s' "$_m" | grep -q 'API key'; then
    t_ok "a rejected key is reported as a key problem, not a failed request"
else
    t_fail "the 401 was not named, so the operator is not told to fix the key: \
$(printf '%s' "$_m" | head_bytes 200)"
fi

t_done
