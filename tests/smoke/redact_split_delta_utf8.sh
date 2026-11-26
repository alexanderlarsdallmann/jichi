#!/bin/sh
# M1193 (def-284): the redact-path emit cut splits a multi-byte character on
# --output jsonl. RED driver (the fix is NOT implemented yet, so C3 and C5 fail
# here; C1, C2 and C4 are green -- the note's red-first proof shape).
#
# THE DEFECT. hl_text_flush (src/main.c ~1838) holds back the last
# jc_redact_maxlen() bytes of the REDACTED buffer between two text deltas:
# safe = rlen - hold, not final. The cut is a raw byte offset into red. M1065's
# clamp moves the cut to a character boundary, but it computes that boundary in
# the RAW buffer's coordinates (text_hold) and uses it to cut the REDACTED buffer
# (red). Those are different coordinate systems whenever a redaction occurs in
# the current buffer, and the clamp is a no-op even in the no-redaction case
# (rlen == len: the raw boundary is the buffer's END, so `bsafe < safe` never
# fires). The defect: a mid-character cut is emitted, each jsonl line is
# raw-invalid UTF-8, and a strict reader (the surface's own contract) rejects the
# whole line.
#
# THE CHECKS INSPECT BYTES, not JSON parsing. jsonq (cJSON) is LENIENT about an
# invalid byte inside a string -- it happily re-parses and re-prints the lone
# E2 or the orphan 86/92 -- so a "does the line parse" check cannot see the
# defect. The strict-reader check is by RAW BYTES (od hex + a POSIX case test),
# exactly as jsonl_text_utf8.sh's checks 2-4 do. No python3: this tier is
# Python-free.
#
# THE FIXTURE (C3, measured on the current head, run 0057, 12:3x +0100 -- the
# note confirms it SPLITs, this is what the driver uses):
#   canary = CANARYSECRETzq7x0123456789 (26 bytes, hold = jc_redact_maxlen() = 26)
#   registered at config load (the mock's apiKey).
#   SSE body, three content deltas + finish + [DONE]:
#     delta1 = answer: CANARYSECRET        (20 raw bytes)
#     delta2 = zq7x0123456789 done         (19 raw bytes)
#     delta3 = " \342\206\222 " + 24 q's   (space, E2 86 92 arrow, space, 24 q's)
#   Raw buffer after all 3 deltas = 20 + 19 + 29 = 68 bytes.
#   Redacted buffer = 68 - 23 = 45 bytes (the canary 26 -> ***, saves 23).
#   rlen = 45, safe = rlen - hold = 45 - 26 = 19.
#   The red buffer is: "answer: *** done " (17 bytes) + E2(18) 86(19) 92(20)
#   + " " (21) + 24 q's (22..45). The arrow sits at red offsets 18-20; the cut
#   at 19 lands BETWEEN 86 (byte 19) and 92 (byte 20) -- INSIDE the arrow.
#   Measured output (unfixed):
#     line 1 ends   ...20 e2 86 0a  (payload = "answer: *** done " + E2 86)
#     line 2 begins 92 20 71 71 ... (payload = 92 + space + 24 q's)
#
#   N = 24 q's is the measured count that splits (the note's N = 24/25 family).
#   Re-derived here, not by hand from the note: raw = 68, red = 45, safe = 19,
#   arrow at red 18-20, cut 19 between 86 and 92. (23 q's would put safe = 18,
#   the cut between E2 and 86 -- also a split; 25 q's puts safe = 20, the cut
#   between 92 and the following space -- NO split. 24 is used.)
#
# C5 (the no-redaction-registered case, def-284's second face):
#   Same canary apiKey (hold = 26), but the model's text contains NO canary.
#   SSE body:
#     delta1 = \342\206\222 + 24 q's    (the arrow FIRST, no leading space)
#     delta2 = finish
#   Raw buffer = 3 + 24 = 27 bytes, rlen = 27 (no redaction happened),
#   safe = 27 - 26 = 1. The arrow sits at red offsets 0-2; the cut at 1 lands
#   BETWEEN E2 (byte 0) and 86 (byte 1) -- INSIDE the arrow. bsafe =
#   jc_utf8_stream_prefix(raw, 27) = 27 (the whole buffer is well-formed), so
#   27 > rlen? no; 27 < safe? no -- the clamp NEVER FIRES (rlen == len: the raw
#   boundary is the buffer's end). Measured output (unfixed):
#     line 1 = e2            (payload = a lone E2 lead byte)
#     line 2 = 86 92 71 71 .. (payload = 86 92 + 24 q's)
#
# C2 (the no-secret control, M1065's guard, the DIRECT utf8_tail path):
#   apiKey "x" (unregistered, jc_redact_maxlen() == 0) with the same arrow
#   fixture, the arrow split across TWO SSE content deltas (E2 in one, 86 92 in
#   the next -- exactly the provider fragmentation the M1065 finding measured).
#   GREEN on the current tree: the direct path is fixed. It is the guard that
#   the fix does not touch that path.

. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

canary="CANARYSECRETzq7x0123456789"
Q24=$(printf 'q%.0s' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24)

# --- helpers ------------------------------------------------------------------
# extract_delta_hex STREAM OUT -- for every `"type":"text"` line in STREAM, the
# payload's raw bytes as od hex tokens, one delta per line, in stream order.
# The jsonl_text_utf8.sh idiom: payload=$(... | $JQ .delta), then od. Command
# substitution strips jsonq's trailing newline, so od sees ONLY the payload's
# bytes. Echoes the number of text lines.
extract_delta_hex() {
    : > "$2"
    $G '"type":"text"' "$1" > "$2.texts" 2>/dev/null || true
    n=$(wc -l < "$2.texts" | tr -d ' ')
    i=0
    while [ "$i" -lt "$n" ]; do
        line=$(sed -n "$((i + 1))p" "$2.texts")
        payload=$(printf '%s' "$line" | $JQ .delta 2>/dev/null)
        printf '%s' "$payload" | od -An -tx1 | tr -s ' \n' ' ' \
            | sed 's/^ //;s/ $//' >> "$2"
        printf '\n' >> "$2"
        i=$((i + 1))
    done
    printf '%s' "$n"
}

# count_end_incomplete HEX -- number of payload lines that END on the lead byte
# 0xE2 with the 3-byte arrow incomplete: last token e2 (a lone lead), or last
# two tokens "e2 86" (the third byte 92 missing, on the next line). The
# fixture's only multi-byte character is the 3-byte arrow, so e2 is the whole
# lead-byte class that matters here.
count_end_incomplete() {
    _ci=0
    while IFS= read -r hexline; do
        [ -n "$hexline" ] || continue
        last=$(printf '%s' "$hexline" | awk '{print $NF}')
        last2=$(printf '%s' "$hexline" | awk '{print $(NF-1), $NF}')
        if [ "$last" = "e2" ] || [ "$last2" = "e2 86" ]; then
            _ci=$((_ci + 1))
        fi
    done < "$1"
    printf '%s' "$_ci"
}

# count_begin_orphan HEX -- number of payload lines that BEGIN with an orphan
# continuation byte 0x80-0xBF (the jsonl_text_utf8.sh check-2 case). A
# well-formed UTF-8 stream never opens on a continuation byte; the second half
# of a character landing FIRST is the tell-tale.
count_begin_orphan() {
    _co=0
    while IFS= read -r hexline; do
        [ -n "$hexline" ] || continue
        first=$(printf '%s' "$hexline" | cut -d ' ' -f1)
        case "$first" in
            8?|9?|a?|b?) _co=$((_co + 1)) ;;
        esac
    done < "$1"
    printf '%s' "$_co"
}

# first_offender HEX -- echoes "kind <tokens>" for the first violating line: a
# line that begins on an orphan continuation byte, or ends on an incomplete
# e2 / e2 86 tail. Used to name the defect in the t_fail message.
first_offender() {
    while IFS= read -r hexline; do
        [ -n "$hexline" ] || continue
        first=$(printf '%s' "$hexline" | cut -d ' ' -f1)
        last=$(printf '%s' "$hexline" | awk '{print $NF}')
        last2=$(printf '%s' "$hexline" | awk '{print $(NF-1), $NF}')
        case "$first" in 8?|9?|a?|b?) printf '%s' "begins $first ($hexline)"; return ;; esac
        if [ "$last" = "e2" ]; then printf '%s' "ends $last ($hexline)"; return; fi
        if [ "$last2" = "e2 86" ]; then printf '%s' "ends $last2 ($hexline)"; return; fi
    done < "$1"
}

# =============================================================================
# RUN A: the no-secret control (apiKey "x", M1065's DIRECT utf8_tail path).
# The arrow is split across two SSE content deltas (E2 in one, 86 92 in the
# next) -- the provider fragmentation the M1065 finding measured.
# =============================================================================
{
  printf 'data: {"choices":[{"delta":{"role":"assistant","content":"ab\342"}}]}\n\n'
  printf 'data: {"choices":[{"delta":{"content":"\206\222cd %s"}}]}\n\n' "$Q24"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/bodyA.sse"

cat > "$tmp/repliesA.mm" <<EOF
wire openai
rule
  sse-file $tmp/bodyA.sse
EOF
mm_start "$tmp/repliesA.mm" "$tmp/capA" 6
write_config "$tmp/configA.json" "$MM_PORT"

( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configA.json" \
    --no-session --output jsonl -p "go" < /dev/null \
    > "$tmp/streamA.jsonl" 2>/dev/null ); rcA=$?
mm_stop

nA=$(extract_delta_hex "$tmp/streamA.jsonl" "$tmp/hexA.txt")

# =============================================================================
# RUN B: the canary apiKey (hold=26) with the canary split across deltas and
# the arrow at the redact cut -- the measured def-284 fixture.
# =============================================================================
{
  printf 'data: {"choices":[{"delta":{"role":"assistant","content":"answer: CANARYSECRET"}}]}\n\n'
  printf 'data: {"choices":[{"delta":{"content":"zq7x0123456789 done"}}]}\n\n'
  printf 'data: {"choices":[{"delta":{"content":" \342\206\222 %s"}}]}\n\n' "$Q24"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/bodyB.sse"

cat > "$tmp/repliesB.mm" <<EOF
wire openai
rule
  sse-file $tmp/bodyB.sse
EOF
mm_start "$tmp/repliesB.mm" "$tmp/capB" 6

cat > "$tmp/configB.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$canary","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configB.json" \
    --no-session --output jsonl -p "go" < /dev/null \
    > "$tmp/streamB.jsonl" 2>/dev/null ); rcB=$?
mm_stop

nB=$(extract_delta_hex "$tmp/streamB.jsonl" "$tmp/hexB.txt")

# =============================================================================
# RUN C: the canary apiKey is registered (hold=26) but ABSENT from the text,
# with the arrow at rlen - hold -- the no-redaction second face.
# =============================================================================
{
  printf 'data: {"choices":[{"delta":{"role":"assistant","content":"\342\206\222%s"}}]}\n\n' "$Q24"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/bodyC.sse"

cat > "$tmp/repliesC.mm" <<EOF
wire openai
rule
  sse-file $tmp/bodyC.sse
EOF
mm_start "$tmp/repliesC.mm" "$tmp/capC" 6

cat > "$tmp/configC.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$canary","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configC.json" \
    --no-session --output jsonl -p "go" < /dev/null \
    > "$tmp/streamC.jsonl" 2>/dev/null ); rcC=$?
mm_stop

nC=$(extract_delta_hex "$tmp/streamC.jsonl" "$tmp/hexC.txt")

# =============================================================================
# C1: instrument -- the canary run completed and actually streamed text -------
# A zero-delta run would make C3 vacuous, so this is the extraction floor.
# =============================================================================
if [ "$rcB" -eq 0 ] && [ "$nB" -ge 1 ]; then
    t_ok "the run completed (rc=0) and streamed $nB text delta(s)"
else
    t_fail "canary run rc=$rcB, text deltas=$nB -- the scenario did not stream"
fi

# --- C2: the no-secret control (M1065 must not regress) ------------------------
# The direct utf8_tail path reassembles E2 with its continuations, so no payload
# ends on a lone/incomplete lead byte and none begins on an orphan continuation.
endA=$(count_end_incomplete "$tmp/hexA.txt")
begA=$(count_begin_orphan "$tmp/hexA.txt")
if [ "$rcA" -eq 0 ] && [ "$nA" -ge 1 ] && [ "$endA" -eq 0 ] && [ "$begA" -eq 0 ]; then
    t_ok "no-secret control (M1065 direct path): $nA deltas, 0 end-incomplete, 0 begin-orphan"
else
    t_fail "no-secret control regressed: rc=$rcA, $nA deltas, $endA end-incomplete, $begA begin-orphan; first: $(first_offender "$tmp/hexA.txt" | head_bytes 60)"
fi

# --- C3: THE defect (the redact path) -----------------------------------------
# No payload may end on an incomplete arrow lead (e2 / e2 86) or begin on an
# orphan continuation (8?|9?|a?|b?). RED on the unfixed tree: the measured cut
# at safe = 19 splits the arrow (line 1 ends e2 86, line 2 begins 92). GREEN
# with the fix: 0 and 0.
endB=$(count_end_incomplete "$tmp/hexB.txt")
begB=$(count_begin_orphan "$tmp/hexB.txt")
if [ "$endB" -eq 0 ] && [ "$begB" -eq 0 ]; then
    t_ok "redact path: 0 payloads end incomplete, 0 begin orphan (the emit cut did not split the arrow)"
else
    t_fail "redact path splits a multi-byte character: $endB payload(s) end incomplete, $begB begin orphan; first: $(first_offender "$tmp/hexB.txt" | head_bytes 60)"
fi

# --- C4: content not lost -----------------------------------------------------
# The fix holds the split half, it must not DROP it: reassembling every text
# payload in order carries the whole arrow e2 86 92, and the 24 q's (24 x 71)
# follow it in a run. GREEN even on the unfixed tree (the note: "the
# reassembled stream carries the whole arrow (no content lost) -- the defect is
# the per-line invalidity, not loss").
allB=$(tr '\n' ' ' < "$tmp/hexB.txt" | tr -s ' ')
ok4=1
case " $allB " in
    *"e2 86 92"*) : ;;
    *) ok4=0 ;;
esac
# The 24 q's are 24 x 71 tokens; they must all survive (none dropped). Count
# the 71 tokens in the reassembled stream -- there are exactly 24 (the q's are
# the only source of 0x71 in this fixture).
n71=0
for tok in $allB; do
    [ "$tok" = "71" ] && n71=$((n71 + 1))
done
[ "$n71" -eq 24 ] || ok4=0
if [ "$ok4" -eq 1 ]; then
    t_ok "reassembled deltas carry the whole arrow (e2 86 92) and all 24 q's (content not dropped)"
else
    t_fail "content lost or reordered: n71=$n71 (want 24); reassembled bytes = $(printf '%s' "$allB" | head_bytes 120)"
fi

# --- C5: the registered-but-absent case (no redaction, rlen == len) ------------
# Same per-line byte assertions as C3. RED on the unfixed tree: with no
# redaction the raw boundary is the buffer's end and the M1065 clamp never
# fires, so the cut at safe = 1 lands inside the arrow (line 1 ends e2, line 2
# begins 86 92).
endC=$(count_end_incomplete "$tmp/hexC.txt")
begC=$(count_begin_orphan "$tmp/hexC.txt")
if [ "$endC" -eq 0 ] && [ "$begC" -eq 0 ]; then
    t_ok "registered-but-absent: 0 payloads end incomplete, 0 begin orphan (the no-redaction cut did not split the arrow)"
else
    t_fail "registered-but-absent splits a multi-byte character: $endC payload(s) end incomplete, $begC begin orphan; first: $(first_offender "$tmp/hexC.txt" | head_bytes 60)"
fi

t_done
