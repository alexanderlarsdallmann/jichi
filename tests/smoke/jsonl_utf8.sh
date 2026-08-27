#!/bin/sh
# smoke: the jsonl tool_result preview is valid UTF-8 (M439).
#
# THE DEFECT. `tool_result.preview` was built with jc_snprintf into a 513-byte buffer.
# jc_snprintf is not UTF-8 aware, so a cut landing inside a multi-byte character put
# INVALID UTF-8 on a JSON line -- in `--output jsonl`, which docs/EMBEDDING.md lists as a
# **stable** interface and instructs consumers to parse. A strict reader is entitled to
# reject the whole line, so a truncated preview became a lost event.
#
# It was not a rare edge case either: 512 is not a multiple of 3, so for any tool result
# made of 3-byte characters and long enough to truncate, splitting one was the NORMAL
# outcome.
#
# WHY AN END-TO-END DRIVER AS WELL AS THE UNIT TEST. test_agentjson_preview pins the pure
# function's boundary behaviour. It cannot show that main.c's tool_result path CALLS it --
# the wiring is the other half, and the wiring is what shipped broken.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# A file of 3-byte characters, comfortably past the 512-byte preview cap. U+2026 (E2 80
# A6) is used because every one of its bytes is distinctive: a split leaves a lone E2 or
# an orphan 80/A6, both trivially detectable.
: > "$ws/wide.txt"
i=0
while [ "$i" -lt 40 ]; do
    printf '\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246' >> "$ws/wide.txt"
    i=$((i + 1))
done
bytes=$(wc -c < "$ws/wide.txt" | tr -d ' ')

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text UTF8_DONE
rule
  tool read_file {"path":"wide.txt"}
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --output jsonl \
    -p "read the wide file" < /dev/null > "$tmp/out.jsonl" 2>/dev/null); rc=$?
mm_stop

# --- 1: the fixture is actually big enough to truncate -------------------------
# The extraction floor. A file under 512 bytes would make every check below pass
# vacuously, since no cut would happen at all.
if [ "$rc" -eq 0 ] && [ "$bytes" -gt 600 ]; then
    t_ok "wrote a ${bytes}-byte multi-byte file and the run completed"
else
    t_fail "rc=$rc bytes=$bytes"
fi

# --- 2: the preview exists and is truncated -----------------------------------
line=$(grep '"type":"tool_result"' "$tmp/out.jsonl" | head -1)
printf '%s' "$line" > "$tmp/line.json"
prev=$("$JQ" '.preview' "$tmp/line.json" 2>/dev/null)
plen=$(printf '%s' "$prev" | wc -c | tr -d ' ')
if [ -n "$prev" ] && [ "$plen" -gt 400 ] && [ "$plen" -le 512 ]; then
    t_ok "the preview is present and truncated (${plen} bytes)"
else
    t_fail "preview length $plen -- expected a truncation under 512"
fi

# --- 3: the preview ends with a COMPLETE character ---------------------------
# The direct assertion, and the one the first cut of this driver got wrong. It checked
# "the last byte is not a continuation byte (0x80-0xBF)" and reported the product broken
# on a preview ending 0xA6 -- but 0xA6 is the correct FINAL byte of U+2026, and a valid
# UTF-8 string ending in any multi-byte character always ends in a continuation byte. The
# test was inverted, not the code.
#
# With a fixture made only of U+2026 (E2 80 A6), the precise property is that the tail is
# a whole sequence: a split cut ends "e2" or "e2 80" instead. Checked with od, which is
# POSIX, because the property is about bytes.
tail3=$(printf '%s' "$prev" | od -An -tx1 | tr -s ' ' '\n' | grep -v '^$' | tail -3 | tr '\n' ' ')
case "$tail3" in
    "e2 80 a6 ") t_ok "the preview ends with a complete U+2026 (e2 80 a6)" ;;
    *)           t_fail "the preview's last three bytes are '$tail3' -- a character was split" ;;
esac

# --- 4: no orphaned lead byte anywhere in the preview ------------------------
# Independent of check 3 and stronger than a length test. Every U+2026 contributes exactly
# one E2 and one A6, so the two counts are equal in any well-formed prefix; a cut that
# split a character leaves an extra E2 (or an E2 80 pair) with no matching A6.
#
# A length-modulo test was tried first and was ALSO wrong: read_file prepends a `cat -n`
# gutter, so the 511-byte preview is 7 ASCII bytes plus 504 = 168 whole characters, and
# "must be a multiple of 3" reported a stray byte that did not exist. Counting the bytes
# that must pair is gutter-independent.
ne2=$(printf '%s' "$prev" | od -An -tx1 | tr -s ' ' '\n' | grep -c '^e2$')
na6=$(printf '%s' "$prev" | od -An -tx1 | tr -s ' ' '\n' | grep -c '^a6$')
if [ "$ne2" -gt 100 ] && [ "$ne2" -eq "$na6" ]; then
    t_ok "every lead byte has its continuation ($ne2 characters, none split)"
else
    t_fail "e2=$ne2 a6=$na6 -- an orphaned lead byte means a split character"
fi

# --- 5: every jsonl line still parses ----------------------------------------
# Checked over ALL lines, because a stable-surface promise is about the stream, not one
# record. Stated plainly: this check passes with the fix REVERTED too, because cJSON --
# the parser jsonq links, and the one jichi ships -- is lenient about invalid UTF-8 in a
# string. It is here as a regression guard on the stream's shape, not as evidence for the
# UTF-8 property; checks 3 and 4 are the ones with teeth. A STRICTER consumer (Python's
# json with a UTF-8 decode, Go's encoding/json) is what actually rejects the line, and
# proving that would mean depending on such a consumer in a python-free tier.
bad=0
while IFS= read -r l; do
    [ -n "$l" ] || continue
    printf '%s' "$l" > "$tmp/one.json"
    "$JQ" -q '.type' "$tmp/one.json" >/dev/null 2>&1 || bad=$((bad + 1))
done < "$tmp/out.jsonl"
if [ "$bad" -eq 0 ]; then
    t_ok "every emitted jsonl line parses and carries a type"
else
    t_fail "$bad jsonl line(s) failed to parse"
fi

t_done
