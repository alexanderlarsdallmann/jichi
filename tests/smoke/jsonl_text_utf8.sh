#!/bin/sh
# smoke: --output jsonl streamed `text` lines are well-formed UTF-8 (M1065).
#
# THE DEFECT. A provider may fragment a multi-byte character across two SSE
# `content` deltas: delta 1 carries the lead byte (E2), delta 2 carries the
# continuation bytes (86 92). The headless jsonl emitter (hl_text, src/main.c)
# wrote each provider fragment VERBATIM as a separate `text` line, so a single
# U+2192 (E2 86 92, the arrow) landed as one `text` delta whose payload ends in
# a lone E2 and the next whose payload BEGINS with the orphan continuation
# bytes 86 92. Each line is then raw-invalid UTF-8 on a surface docs/EMBEDDING.md
# lists as STABLE and instructs consumers to parse; a strict reader (Python text
# mode) rejects the whole line, so the event is lost. Run 0077's stream.jsonl
# carried the signature (lines 1403-1405; 5 of 2537 lines).
#
# The fix holds the incomplete trailing sequence in a <=3-byte carry on hl_ctx
# and reassembles it with the next delta, so the whole character lands in ONE
# `text` delta.
#
# WHY AN END-TO-END DRIVER AS WELL AS THE UNIT TEST. jc_utf8_stream_prefix pins
# the pure helper's boundary behaviour. It cannot show that hl_text CALLS it --
# the wiring is the other half, and the wiring is what shipped broken. (The same
# M439/M536 lesson jsonl_utf8.sh states about its own twin.)
#
# THE CHECKS INSPECT BYTES, not JSON parsing. jsonq (cJSON) is LENIENT about an
# invalid byte inside a string -- it happily re-parses and re-prints the lone E2
# or the orphan 86 92 -- so a "does the line parse" check cannot see the defect.
# Checks 2-4 extract each text payload's RAW BYTES (od -An -tx1) and assert the
# UTF-8 property directly.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# --- the fixture: ONE 3-byte char (U+2192, E2 86 92) split across two deltas --
# delta 1 = the bytes `ab` + E2 ; delta 2 = the bytes 86 92 + `cd`. Written with
# printf so the lead and the continuations are RAW bytes, not escapes. A
# provider fragment puts E2 in one content delta and 86 92 in the next --
# exactly the split the finding measured in run 0077.
{
  printf 'data: {"choices":[{"delta":{"role":"assistant","content":"ab\342"}}]}\n\n'
  printf 'data: {"choices":[{"delta":{"content":"\206\222cd"}}]}\n\n'
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/body.sse"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $tmp/body.sse
EOF
mm_start "$tmp/replies.mm" "$tmp/cap" 6
# Default write_config: apiKey "x" is UNregistered as a secret, so
# jc_redact_maxlen() == 0 and hl_text takes the DIRECT jsonl path -- the one the
# finding is about.
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --output jsonl -p "arrow" < /dev/null \
    > "$tmp/stream.jsonl" 2>/dev/null); rc=$?
mm_stop

# One line per text delta, raw (cJSON prints the payload bytes verbatim).
$G '"type":"text"' "$tmp/stream.jsonl" > "$tmp/texts.jsonl" 2>/dev/null || true
n_text=$(wc -l < "$tmp/texts.jsonl" | tr -d ' ')

# --- 1: instrument -- the run completed and actually streamed text -----------
# A zero-delta run would make checks 2-4 hold vacuously, so this is the
# extraction floor.
if [ "$rc" -eq 0 ] && [ "$n_text" -ge 1 ]; then
    t_ok "the run completed (rc=0) and streamed $n_text text delta(s)"
else
    t_fail "run rc=$rc, text deltas=$n_text -- the scenario did not stream"
fi

# Per-delta byte stream: for every text line, the payload's raw bytes as od hex
# tokens, one delta's tokens on its own line, deltas in stream order. This is
# what checks 2-4 read, so the byte extraction lives in exactly one place.
i=0
: > "$tmp/delta_hex.txt"
while [ "$i" -lt "$n_text" ]; do
    line=$(sed -n "$((i + 1))p" "$tmp/texts.jsonl")
    # Command substitution strips jsonq's trailing newline, so od sees ONLY the
    # payload's bytes (a stray 0a would otherwise split the reassembled stream
    # and defeat check 4's contiguous-sequence match).
    payload=$(printf '%s' "$line" | $JQ .delta 2>/dev/null)
    printf '%s' "$payload" | od -An -tx1 | tr -s ' \n' ' ' \
        | sed 's/^ //;s/ $//' >> "$tmp/delta_hex.txt"
    printf '\n' >> "$tmp/delta_hex.txt"
    i=$((i + 1))
done

# --- 2: NO text payload begins with an orphan continuation byte (0x80-0xBF) --
# The second half of the character landing FIRST is the tell-tale: a well-formed
# UTF-8 stream never opens on a continuation byte (0x80-0xBF); a valid
# multi-byte sequence always opens on its LEAD byte (0xC0-0xF4). Checked on the
# RAW BYTES, because jsonq re-parses the invalid byte happily and a parse-only
# check would pass with the defect present. RED on the unfixed tree
# (delta 2 = 86 92 cd, whose first byte 86 is an orphan continuation).
bad_cont=0
while IFS= read -r hexline; do
    [ -n "$hexline" ] || continue
    first=$(printf '%s' "$hexline" | cut -d ' ' -f1)
    case "$first" in
        8?|9?|a?|b?) bad_cont=$((bad_cont + 1)) ;;
    esac
done < "$tmp/delta_hex.txt"
if [ "$bad_cont" -eq 0 ]; then
    t_ok "no text delta begins with an orphan continuation byte (0x80-0xBF)"
else
    t_fail "$bad_cont text delta(s) begin with a continuation byte -- the second half of a character landed first"
fi

# --- 3: the character lands in ONE delta (the E2 also carries 86 92) ---------
# On the fixed tree the carry reassembles E2 with its continuations, so the
# delta whose payload contains E2 also contains 86 and 92. On the unfixed tree
# E2 sits in one delta and 86 92 in the next, so no single delta has all three.
# RED on the unfixed tree.
e2_lines=0
e2_with_all=0
while IFS= read -r hexline; do
    [ -n "$hexline" ] || continue
    has_e2=0; has_86=0; has_92=0
    # test membership of each 2-char hex token in this delta's token list
    for tok in $hexline; do
        [ "$tok" = e2 ] && has_e2=1
        [ "$tok" = 86 ] && has_86=1
        [ "$tok" = 92 ] && has_92=1
    done
    if [ "$has_e2" -eq 1 ]; then
        e2_lines=$((e2_lines + 1))
        if [ "$has_86" -eq 1 ] && [ "$has_92" -eq 1 ]; then
            e2_with_all=$((e2_with_all + 1))
        fi
    fi
done < "$tmp/delta_hex.txt"
if [ "$e2_lines" -ge 1 ] && [ "$e2_with_all" -ge 1 ]; then
    t_ok "the E2 lead byte shares a delta with its continuations 86 92 (one delta)"
else
    t_fail "the E2 sits in a delta by itself (e2_lines=$e2_lines, e2_with_all=$e2_with_all) -- the character was split across two lines"
fi

# --- 4: reassembling every text delta carries the whole sequence in order ----
# The content must not be lost: concatenating all deltas contains ab E2 86 92 cd.
ok=1
# the reassembled bytes, in stream order, as one hex-token list. `ab` is 61 62,
# the arrow e2 86 92, `cd` is 63 64.
all=$(tr '\n' ' ' < "$tmp/delta_hex.txt" | tr -s ' ')
# the exact arrow sequence e2 86 92 must appear in the reassembled stream, in
# that order, with ab immediately before the lead byte and cd immediately after
# the final byte -- the whole character, not lost and not reordered.
case " $all " in
    *"61 62 e2 86 92 63 64"*) : ;;
    *) ok=0 ;;
esac
if [ "$ok" -eq 1 ]; then
    t_ok "the reassembled deltas carry the whole arrow (ab e2 86 92 cd) in order"
else
    t_fail "content lost or reordered: reassembled bytes = $(printf '%s' "$all" | head_bytes 120)"
fi

t_done
