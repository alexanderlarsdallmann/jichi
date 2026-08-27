#!/bin/sh
# smoke: the generate_image tool (M32). A mock image backend answers
# /v1/images/generations with a b64_json PNG: (1) the decoded bytes land
# at the workspace path; (2) an 8-byte image against imageGenMaxBytes=4
# is rejected with no file; (3) an edit sends the `source` image as RAW
# base64 in ref_images (never a data: URI -- the API base64-decodes the
# entry directly) and `model` selects the image model.
# (Port of tests/e2e/imagegen.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# base64 of the 8 PNG magic bytes \x89 P N G \r \n \x1a \n
PNG_B64="iVBORw0KGgo="
printf '\211PNG\r\n\032\n' > "$tmp/magic.png"

cat > "$tmp/img.mm" <<EOF
wire openai
rule
  status 200
  body {"data":[{"b64_json":"$PNG_B64"}]}
EOF

# run_case CAPTAG TOOL_ARGS_JSON EXTRA_TOP -> capm/capi under $tmp/<tag>
run_case() {
    _d="$tmp/$1"; mkdir -p "$_d/capm" "$_d/capi"
    cat > "$_d/model.mm" <<EOF
wire openai
rule
  count 1
  tool generate_image $2
rule
  text IMAGE_DONE
EOF
    mm_start "$tmp/img.mm" "$_d/capi"
    I_PORT=$MM_PORT; I_PID=$MM_PID
    mm_start "$_d/model.mm" "$_d/capm"
    cat > "$_d/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"img","provider":"openai","model":"mock-image",
   "apiBase":"http://127.0.0.1:$I_PORT/v1","apiKey":"x","roles":["image"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0${3:+,$3}}
EOF
    (cd "$ws" && with_deadline 60 "$BIN" --config "$_d/config.json" \
        -q --no-session --auto -p "make an image" \
        < /dev/null > "$_d/out" 2>&1)
    kill "$I_PID" 2>/dev/null
    wait "$I_PID" 2>/dev/null
    mm_stop
}

# 1. happy path: decoded bytes land at out.png
run_case gen '{"prompt":"a red cube","path":"out.png"}' ''
if [ -f "$tmp/gen/capi/req.1" ] && grep -q "IMAGE_DONE" "$tmp/gen/out"; then
    t_ok "the image backend was queried and the turn completed"
else
    t_fail "backend not queried or turn incomplete"
fi
if cmp -s "$ws/out.png" "$tmp/magic.png"; then
    t_ok "the decoded PNG bytes were written verbatim"
else
    t_fail "out.png missing or wrong bytes"
fi

# 2. oversized response vs a 4-byte cap: rejected, no file
run_case cap '{"prompt":"a red cube","path":"big.png"}' \
    '"imageGenMaxBytes":4'
if [ ! -e "$ws/big.png" ]; then
    t_ok "an oversized image is rejected (no file written)"
else
    t_fail "big.png exists despite imageGenMaxBytes=4"
fi

# 3. edit: source as RAW base64 in ref_images + model selection
printf '\211PNG\r\n\032\n' > "$ws/src.png"
run_case edit '{"prompt":"make it blue","path":"edited.png","source":"src.png","model":"img"}' ''
req="$tmp/edit/capi/req.1"
if grep -q '"ref_images"' "$req" 2>/dev/null \
   && grep -q "$PNG_B64" "$req" 2>/dev/null; then
    t_ok "the source image went as ref_images raw base64"
else
    t_fail "ref_images/base64 missing from the backend request"
fi
if ! grep -q "data:" "$req" 2>/dev/null; then
    t_ok "no data: URI in the backend request"
else
    t_fail "the source was wrongly sent as a data: URI"
fi
if [ -f "$ws/edited.png" ]; then
    t_ok "the edited image was written"
else
    t_fail "edited.png missing"
fi

t_done
