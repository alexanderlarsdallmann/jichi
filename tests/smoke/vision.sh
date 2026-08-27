#!/bin/sh
# smoke: vision input (M29). With a vision-capable model, --image attaches
# an image_url data-URI block to the request (asserted in the captured
# body); with a non-vision model the image is DROPPED with a warning and
# no data URI is sent. The workspace is empty so no rules text can fake
# the "image_url" token.
# (Port of tests/e2e/vision.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# a fake PNG: the magic bytes + filler (\211 = 0x89, \032 = 0x1a)
printf '\211PNG\r\n\032\nFAKEIMAGEDATA' > "$tmp/img.png"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text VISION_OK
EOF

# --- vision model: the image block must be in the request -------------------
mkdir -p "$tmp/cap1"
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT" '' '"vision":true'
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session --image "$tmp/img.png" -p "describe the image" \
      < /dev/null 2>/dev/null); rc=$?
mm_stop
case "$out" in
    *VISION_OK*) t_ok "the vision turn completed" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep -q '"image_url"' "$tmp/cap1/req.1" 2>/dev/null \
   && grep -q "data:image/png;base64," "$tmp/cap1/req.1"; then
    t_ok "the request carries the image_url data-URI block"
else
    t_fail "no image block in the captured request"
fi

# --- non-vision model: dropped with a warning, no data URI sent --------------
mkdir -p "$tmp/cap2"
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --no-session --image "$tmp/img.png" -p "describe the image" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop
if ! grep -q "data:image/png;base64," "$tmp/cap2/req.1" 2>/dev/null; then
    t_ok "no image block reached the non-vision model"
else
    t_fail "a non-vision model received the image block"
fi
if grep -q "not vision-capable" "$tmp/err"; then
    t_ok "the drop warning was printed"
else
    t_fail "no 'not vision-capable' warning: $(tail -c 150 "$tmp/err")"
fi

t_done
