#!/bin/sh
# smoke: sound I/O tools (M163b). play_audio drives the configured player
# (which logs $JICHI_AUDIO_FILE -- must be the ABSOLUTE path), record_audio
# creates the target file through the mock recorder (an oversize duration
# is clamped jichi-side), the path fence blocks an out-of-workspace path
# BEFORE the player runs, and without a `sound` config neither tool is
# advertised to the model.
# (Port of tests/e2e/sound.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
playlog="$tmp/played.log"
export PLAYLOG="$playlog"

rec="$tmp/mock-record.sh"
printf '#!/bin/sh\nfor a in "$@"; do :; done\nprintf "RIFFmock" > "$a"\n' \
    > "$rec"
chmod +x "$rec"
printf 'RIFFxxxx' > "$ws/hello.wav"

SOUND_CFG="\"sound\":{\"play\":{\"shell\":\"printf '%s\\\\n' \\\"\$JICHI_AUDIO_FILE\\\" >> \\\"\$PLAYLOG\\\"\"},\"record\":{\"command\":\"$rec\"},\"recordMaxSeconds\":5}"

# run_case CAP TOOLNAME ARGS_JSON EXTRA_TOP
run_case() {
    _cap="$tmp/$1"; mkdir -p "$_cap"
    if [ -n "$2" ]; then
        cat > "$_cap/replies.mm" <<EOF
wire openai
rule
  count 1
  tool $2 $3
rule
  text done
EOF
    else
        cat > "$_cap/replies.mm" <<'EOF'
wire openai
rule
  text done
EOF
    fi
    mm_start "$_cap/replies.mm" "$_cap"
    write_config "$_cap/config.json" "$MM_PORT" "$4"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$_cap/config.json" \
        -q --no-session --auto -p "do it" < /dev/null > /dev/null 2>&1)
    mm_stop
}

# play_audio: the player receives the ABSOLUTE audio path
run_case A play_audio '{"path":"hello.wav"}' "$SOUND_CFG"
logged=$(cat "$playlog" 2>/dev/null)
case "$logged" in
    /*hello.wav) t_ok "the player received the absolute audio path" ;;
    *) t_fail "player log: '$logged'" ;;
esac

# record_audio: oversize duration clamped, file created via the recorder
run_case B record_audio '{"seconds":999,"path":"take.wav"}' "$SOUND_CFG"
if [ -f "$ws/take.wav" ]; then
    t_ok "record_audio created the file (duration clamped jichi-side)"
else
    t_fail "no take.wav after record_audio"
fi

# fence: an out-of-workspace path is refused BEFORE the player runs
lines_before=$(grep -c . "$playlog" 2>/dev/null || echo 0)
run_case C play_audio '{"path":"/etc/hostname"}' "$SOUND_CFG"
lines_after=$(grep -c . "$playlog" 2>/dev/null || echo 0)
if [ "$lines_after" -eq "$lines_before" ]; then
    t_ok "the fence blocked an out-of-workspace path pre-player"
else
    t_fail "the player ran for /etc/hostname"
fi

# registration: no sound config -> the tools are not advertised
run_case D '' '' ''
if ! grep -q "play_audio" "$tmp/D/req.1" 2>/dev/null \
   && ! grep -q "record_audio" "$tmp/D/req.1" 2>/dev/null; then
    t_ok "sound tools are gated on the sound config"
else
    t_fail "sound tools advertised without a sound config"
fi

t_done
