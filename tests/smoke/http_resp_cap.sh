#!/bin/sh
# smoke: the collected-response ceiling (C-06 / M1092). Three headless
# scenarios; A and B against a 50 MiB body-file, both of which, WITHOUT the
# ceiling, buffer the whole response into memory:
#
#   A (the default ceiling): web_search sets no max_bytes, so it relies on
#     jc_http_perform's 32 MiB default (JC_HTTP_DEFAULT_RESP_BYTES). The 50 MiB
#     reply must ABORT the transfer -- and the tool must NAME jichi's own
#     ceiling ("exceeded the 33554432-byte limit (the transfer was cut)",
#     present ONLY when the collect ceiling cut it), so the model does not
#     retry the same search against a generic "request failed" -- rather than
#     buffer to OOM.
#   B (the pass-down): generate_audio with audioGenMaxBytes=4. The configured
#     cap is passed down as req.max_bytes, so the 50 MiB reply aborts AT the
#     cap and the tool reports the "transfer was cut" cap message (the
#     overflow-flag path, distinct from the post-hoc "is N bytes, over the cap"
#     backstop) -- and no file is written.
#   C (the envelope-bound transfer cap, M1092-b): generate_image with
#     imageGenMaxBytes=1000 and a 900-byte image. The DECODED image is under
#     the cap, but its b64_json envelope (~1228 bytes) is OVER it. M1092 capped
#     the POST leg's transfer at the decoded cap, so a legal image was cut in
#     transit and refused (this scenario is red at M1092); the fix caps that
#     leg's transfer at the ENVELOPE's size bound and the image is written
#     byte-identical.
#
# THE MEMORY TEETH (checks 4 and 8) assert on the KERNEL'S HIGH-WATER MARK.
# Each of the BASELINE, A and B turns runs in the FOREGROUND under the
# tests/tools/rsshwm wrapper (SV:0302 R2, M1092-c, 2026-10-30): rsshwm
# fork/exec's jichi, wait4()s it and prints the child's
# rusage.ru_maxrss (in KB, "maxrss_kb N") -- the peak the process actually
# reached, not an instant sampled on a schedule. The BASELINE turn (same
# config shape as A -- one mock model, no search block, no backend queried;
# the model answers "SEARCH_DONE" to the same single -p) measures everything
# except the over-limit transfer; check 4 asserts A's high-water mark grew by
# LESS THAN 40 MiB over it, check 8 the same for B.
# WHY THE SAMPLER WAS REPLACED: this driver used a 50 ms /proc/<pid>/statm
# background sampler, and it MISSES the transient peak -- with
# JC_HTTP_DEFAULT_RESP_BYTES raised to 1 GiB the driver was green 3/3 at
# ~8.6 MB growth while the kernel's own high-water mark said 63,552 KB (vs
# 45,312 KB under the real ceiling; /usr/bin/time -f %M, the supervisor's
# measurement, SV:0302 R2). A check that cannot go red proves nothing; the
# high-water mark separates the two and goes red (growth ~51 MiB > 40 MiB).
# (New driver, C-06 / M1092.)
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
cd "$ws"  # jichi's workspace is its cwd; everything else uses absolute paths
mkdir -p "$tmp/capmA" "$tmp/capmB" "$tmp/capmBase" "$tmp/caps" "$tmp/capa"

# rsshwm (M1092-c): the high-water wrapper. It is in the smoke-tools group
# (TT_BIN); if it is not built here yet, build it once (the run.sh header
# guard precedent, which exits before the first driver).
RSSHWM="$SMOKE_TOOLS/rsshwm"
if [ ! -x "$RSSHWM" ]; then
    make -C "$SMOKE_ROOT" smoke-tools >/dev/null 2>&1
fi

# hwm_kb FILE -- the maxrss_kb number rsshwm printed in FILE (jichi's output
# and the wrapper's line share the file; the line is unique to rsshwm).
# Prints nothing when no valid line was recorded: a check that read nothing
# must not pass (the M1092-b sampler lesson, carried to the wrapper).
hwm_kb() {
    sed -n 's/^maxrss_kb \([0-9][0-9]*\)$/\1/p' "$1" 2>/dev/null | head -1
}

# A 50 MiB sparse body: disk cost ~0, the mock streams ~50 MiB over loopback.
# A null body: it needs no particular content -- only its SIZE matters.
if command -v truncate >/dev/null 2>&1; then
    truncate -s 50M "$tmp/big.bin"
else
    dd if=/dev/zero of="$tmp/big.bin" bs=1M count=50 2>/dev/null
fi

# tool_msg DIR -- the last "role":"tool" content string in the captured chat
# requests (the tool result the model saw), like audiogen.sh's M500 half.
tool_msg() {
    _tm_msg=""
    for _tm_f in "$1"/req.*; do
        [ -f "$_tm_f" ] || continue
        _tm_m=$(sed -n 's/.*"role":"tool"[^}]*"content":"\([^"]*\)".*/\1/p' "$_tm_f")
        [ -n "$_tm_m" ] && _tm_msg="$_tm_m"
    done
    printf '%s' "$_tm_msg"
}

# =============================================================================
# BASELINE -- the memory reference turn (runs FIRST, before A)
# =============================================================================
# Same config shape as scenario A: one mock model server, NO search block,
# and NO backend queried. The model answers "SEARCH_DONE" to the same single
# -p. Its high-water mark is everything the driver's process does EXCEPT the
# over-limit transfer; checks 4 and 8 measure the transfer's peak ON TOP of
# it. Foreground, no timeout wrapper (the caps-off rule: it finishes in ~1 s).
cat > "$tmp/model_base.mm" <<'EOF'
wire openai
rule
  count 1
  text SEARCH_DONE
EOF

mm_start "$tmp/model_base.mm" "$tmp/capmBase"
BASE_PORT=$MM_PORT

cat > "$tmp/config_base.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$BASE_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

$RSSHWM "$BIN" --config "$tmp/config_base.json" -q --no-session --auto \
    -p "search the web" < /dev/null > "$tmp/out_base" 2>&1
BASE_RC=$?
mm_stop

BASE_KB=$(hwm_kb "$tmp/out_base")

# =============================================================================
# SCENARIO A -- the default ceiling (web_search, no max_bytes set)
# =============================================================================
cat > "$tmp/search.mm" <<EOF
wire openai
rule
  delay 500
  status 200
  body-file $tmp/big.bin
EOF

cat > "$tmp/modelA.mm" <<'EOF'
wire openai
rule
  count 1
  tool web_search {"query":"hello"}
rule
  text SEARCH_DONE
EOF

mm_start "$tmp/search.mm" "$tmp/caps"
S_PORT=$MM_PORT; S_PID=$MM_PID
mm_start "$tmp/modelA.mm" "$tmp/capmA"

cat > "$tmp/configA.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "search":{"url":"http://127.0.0.1:$S_PORT/search","provider":"tavily","maxResults":5}}
EOF

# The turn runs in the FOREGROUND under rsshwm (no timeout wrapper, no
# backgrounded sampler -- the caps-off rule; the loopback transfer finishes on
# its own and the ceiling cuts it faster). rsshwm exits 0 only when jichi
# exited cleanly: a non-zero child exit (rc 1, "exit N" printed) or a signal
# (rc 2, "signal N" printed) is a failed turn. The mock's own --deadline is
# the ultimate backstop.
$RSSHWM "$BIN" --config "$tmp/configA.json" -q --no-session --auto \
    -p "search the web" < /dev/null > "$tmp/outA" 2>&1
A_RC=$?
A_KB=$(hwm_kb "$tmp/outA")

kill "$S_PID" 2>/dev/null
wait "$S_PID" 2>/dev/null
mm_stop

if [ -f "$tmp/caps/req.1" ] && grep -q "hello" "$tmp/caps/req.1"; then
    t_ok "A: the search backend was queried with the model's query"
else
    t_fail "A: the search backend was never queried"
fi
A_MSG=$(tool_msg "$tmp/capmA")
# The ceiling must be NAMED (33554432 = JC_HTTP_DEFAULT_RESP_BYTES): a generic
# "web search request failed" would look like a network blip and invite a
# retry of the same oversized search (M1090-b/G2; red at M1092, which said
# "web search request failed").
if printf '%s' "$A_MSG" | grep -q 'exceeded the 33554432-byte limit (the transfer was cut)'; then
    t_ok "A: the over-ceiling reply named jichi's own limit (the ceiling aborted it)"
else
    t_fail "A: the 50 MiB reply was not named via the default ceiling (tool saw: $(printf '%s' "$A_MSG" | head_bytes 80))"
fi
if [ "$A_RC" -eq 0 ] && grep -q "SEARCH_DONE" "$tmp/outA"; then
    t_ok "A: the turn still completed"
else
    t_fail "A: the turn did not complete (rsshwm rc $A_RC): $(head_bytes 120 "$tmp/outA")"
fi
# Check 4 -- the memory tooth: the transfer's HIGH-WATER growth over the
# baseline turn must stay under 40 MiB (the 50 MiB body, the ceiling's margin).
# ru_maxrss is the kernel's own peak: the 50 ms statm sampler (M1092) missed
# the transient loopback peak, so with the ceiling raised to 1 GiB this
# driver was green 3/3 while the high-water mark said 63,552 KB vs 45,312 KB
# (SV:0302 R2). Perturb the ceiling back to 1 GiB and this check goes red
# (growth ~51 MiB > 40 MiB).
case "$BASE_KB" in
    '' ) t_fail "A: the baseline turn recorded no high-water mark (rsshwm rc $BASE_RC) -- a check that read nothing must not pass" ;;
    *  ) if [ -z "$A_KB" ]; then
             t_fail "A: the turn recorded no high-water mark (rsshwm rc $A_RC) -- a check that read nothing must not pass"
         else
             A_GROWTH=$(( A_KB - BASE_KB ))
             if [ "$A_GROWTH" -lt 40960 ]; then
                 t_ok "A: high-water growth stayed under 40 MiB (baseline ${BASE_KB} KB, turn ${A_KB} KB, growth ${A_GROWTH} KB), well under the 50 MiB body"
             else
                 t_fail "A: high-water growth reached the 50 MiB body (baseline ${BASE_KB} KB, turn ${A_KB} KB, growth ${A_GROWTH} KB) -- the default ceiling did not bound it"
             fi
         fi ;;
esac

# =============================================================================
# SCENARIO B -- the pass-down (generate_audio, audioGenMaxBytes=4)
# =============================================================================
cat > "$tmp/tts.mm" <<EOF
wire openai
rule
  delay 500
  status 200
  body-file $tmp/big.bin
EOF

cat > "$tmp/modelB.mm" <<'EOF'
wire openai
rule
  count 1
  tool generate_audio {"text":"hello","path":"big.mp3"}
rule
  text AUDIO_DONE
EOF

mm_start "$tmp/tts.mm" "$tmp/capa"
T_PORT=$MM_PORT; T_PID=$MM_PID
mm_start "$tmp/modelB.mm" "$tmp/capmB"

cat > "$tmp/configB.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"tts","provider":"openai","model":"mock-tts",
   "apiBase":"http://127.0.0.1:$T_PORT/v1","apiKey":"x","roles":["audio"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "audioGenMaxBytes":4}
EOF

$RSSHWM "$BIN" --config "$tmp/configB.json" -q --no-session --auto \
    -p "say hello" < /dev/null > "$tmp/outB" 2>&1
B_RC=$?
B_KB=$(hwm_kb "$tmp/outB")

kill "$T_PID" 2>/dev/null
wait "$T_PID" 2>/dev/null
mm_stop

if [ -f "$tmp/capa/req.1" ] && grep -q "hello" "$tmp/capa/req.1"; then
    t_ok "B: the TTS backend was queried"
else
    t_fail "B: the TTS backend was never queried"
fi
B_MSG=$(tool_msg "$tmp/capmB")
# "was cut" is the overflow-flag wording (C-06 leg 2): the transfer aborted AT
# the cap. The pre-fix post-hoc check says "is N bytes, over the 4-byte cap"
# instead -- a green on that wording would not be red-first.
if [ ! -e "$ws/big.mp3" ] && printf '%s' "$B_MSG" | grep -q 'transfer was cut'; then
    t_ok "B: the over-cap reply named the cap via the overflow flag and wrote no file"
else
    t_fail "B: the 4-byte cap was not named via the overflow flag (file: $([ -e "$ws/big.mp3" ] && echo yes || echo no); tool saw: $(printf '%s' "$B_MSG" | head_bytes 80))"
fi
if [ "$B_RC" -eq 0 ] && grep -q "AUDIO_DONE" "$tmp/outB"; then
    t_ok "B: the turn still completed"
else
    t_fail "B: the turn did not complete (rsshwm rc $B_RC): $(head_bytes 120 "$tmp/outB")"
fi
# Check 8 -- B's memory tooth, identical shape to check 4 (B vs the SAME
# baseline; the growth is the over-cap transfer's peak over a turn that never
# queried a backend).
case "$BASE_KB" in
    '' ) t_fail "B: the baseline turn recorded no high-water mark (rsshwm rc $BASE_RC) -- a check that read nothing must not pass" ;;
    *  ) if [ -z "$B_KB" ]; then
             t_fail "B: the turn recorded no high-water mark (rsshwm rc $B_RC) -- a check that read nothing must not pass"
         else
             # SV:0559 R4: B's threshold is 8 MiB, not 40. B's configured cap
             # is 4 bytes, so the pass-down (jc_audiogen.c's req.max_bytes =
             # max_bytes) costs ~nothing (measured 192 KB); WITHOUT it the
             # 32 MiB default ceiling cuts the transfer and 32,256 KB is
             # buffered -- under 40 MiB, green, so the audio pass-down had
             # no tooth. 8 MiB bites: 32,256 KB > 8192 red, 192 KB green.
             B_GROWTH=$(( B_KB - BASE_KB ))
             if [ "$B_GROWTH" -lt 8192 ]; then
                 t_ok "B: high-water growth stayed under 8 MiB (baseline ${BASE_KB} KB, turn ${B_KB} KB, growth ${B_GROWTH} KB) -- the audio cap was passed down to the transfer"
             else
                 t_fail "B: high-water growth reached the default 32 MiB ceiling (baseline ${BASE_KB} KB, turn ${B_KB} KB, growth ${B_GROWTH} KB) -- the configured audio cap was not passed down as the transfer's bound"
             fi
         fi ;;
esac

# =============================================================================
# SCENARIO C -- the envelope-bound transfer cap (M1092-b, red at M1092)
# =============================================================================
# imageGenMaxBytes=1000, a 900-byte image: the DECODED image is UNDER the cap,
# but the b64_json envelope (1200 b64 chars + framing = 1228 bytes) is OVER it.
# M1092 passed the decoded cap down as the POST leg's transfer max_bytes, so
# the legal image was cut in transit and refused with a false "exceeded the
# cap" message; the fix caps that leg at the ENVELOPE's size bound
# (jc_base64_encoded_len(cap) + ENVELOPE_JSON_SLACK) and the image lands.
head_bytes 900 /dev/zero > "$tmp/imgc_img.bin"
# GNU base64's -w0 (no wrap) is not portable to the BSD smoke rows; the
# tree-idiom `base64 | tr -d '\n'` is Linux-identical (900 bytes -> 1200
# b64 chars, no wrap to drop) and a wrapped b64 inside the JSON string would
# be invalid JSON anyway.
IMG900_B64=$(base64 < "$tmp/imgc_img.bin" | tr -d '\n')
printf '{"data":[{"b64_json":"%s"}]}' "$IMG900_B64" > "$tmp/imgc_body.json"

cat > "$tmp/imgc.mm" <<EOF
wire openai
rule
  status 200
  body-file $tmp/imgc_body.json
EOF

cat > "$tmp/modelC.mm" <<'EOF'
wire openai
rule
  count 1
  tool generate_image {"prompt":"a small image","path":"out_c.png"}
rule
  text IMAGE_DONE
EOF

mm_start "$tmp/imgc.mm" "$tmp/capic"
I_PORT=$MM_PORT; I_PID=$MM_PID
mm_start "$tmp/modelC.mm" "$tmp/capimC"

cat > "$tmp/configC.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"img","provider":"openai","model":"mock-image",
   "apiBase":"http://127.0.0.1:$I_PORT/v1","apiKey":"x","roles":["image"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0,"imageGenMaxBytes":1000}
EOF

rm -f "$ws/out_c.png"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configC.json" \
    -q --no-session --auto -p "make an image" \
    < /dev/null > "$tmp/outC" 2>&1)
kill "$I_PID" 2>/dev/null
wait "$I_PID" 2>/dev/null
mm_stop

C_MSG=$(tool_msg "$tmp/capimC")
if [ -f "$tmp/capic/req.1" ] && grep -q "IMAGE_DONE" "$tmp/outC"; then
    t_ok "C: the image backend was queried and the turn completed"
else
    t_fail "C: backend not queried or turn incomplete (tool saw: $(printf '%s' "$C_MSG" | head_bytes 80))"
fi
if [ -f "$ws/out_c.png" ] && cmp -s "$ws/out_c.png" "$tmp/imgc_img.bin"; then
    t_ok "C: the under-cap image (b64 envelope OVER the cap) was written byte-identical"
else
    t_fail "C: the legal 900 B image was refused or corrupted (file: $([ -e "$ws/out_c.png" ] && echo yes || echo no); tool saw: $(printf '%s' "$C_MSG" | head_bytes 80))"
fi
if [ -f "$ws/out_c.png" ] && [ "$(wc -c < "$ws/out_c.png")" -eq 900 ]; then
    t_ok "C: the decoded size is 900 bytes, under the 1000-byte cap"
else
    t_fail "C: decoded size wrong (got: $([ -f "$ws/out_c.png" ] && wc -c < "$ws/out_c.png" || echo none))"
fi
if printf '%s' "$C_MSG" | grep -q 'Saved generated image (900 bytes)'; then
    t_ok "C: the tool reported the saved image size"
else
    t_fail "C: the tool did not report the saved image (saw: $(printf '%s' "$C_MSG" | head_bytes 80))"
fi

t_done
