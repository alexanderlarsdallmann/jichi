#!/bin/sh
# smoke: D-16 (M1100) -- a PRESENT-but-unreadable .jichi/memory.md must be
# refused, never silently replaced. The 64 MiB read cap (JC_READ_FILE_MAX) is
# the measured trigger: jc_read_file returns JC_ERR_TOOBIG for a file OVER the
# cap, and the old jc_memory_add collapsed EVERY read error to existing=NULL
# ("missing file is fine") and then WROTE a fresh file -- replacing the whole
# prior content. The lead's LOG-3 run 0106 confirmed it: a 67109888-byte
# memory.md became 8 bytes ("- hello\n"), rc=0. The M533 board rule (refuse to
# overwrite what you could not read) is the precedent this driver pins for the
# file tools.
#
# The dedupe behaviour (a note already in the file is not re-appended) is
# UNIT-covered in tests/test_memory.c (test_add_and_tool), so it is not a
# check here; Leg C keeps the append half of the normal-path regression.
#
#   LEG A (the finding): over-cap memory.md (67109888 B = 64 MiB + 1 KiB) ->
#         remember must be REFUSED, the file must be byte-for-byte untouched.
#   LEG B (the boundary): EXACTLY at the cap (67108864 B) reads fine ->
#         remember must APPEND (this pins that the fix refuses only OVER the
#         cap, per the lead's run-0106 correction).
#   LEG C (normal append): small memory.md with one note -> a NEW note
#         appends (dedupe is unit-covered).
#
# t_plan 5. RED-FIRST: on the unfixed head, checks 1, 2 and 3 go red (the
# over-cap file is replaced); checks 4 and 5 stay green (boundary + normal
# append already work).

. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

# write_config's fixed model list is fine here (remember is a core/full tool).

# 64 MiB + 1 KiB: 65537 x 1024 zeros -- exactly the lead's run-0106 repro size.
OVER_BYTES=67109888
# Exactly the cap (reads fine): 65536 x 1024 zeros.
AT_BYTES=67108864

# --- LEG A: the finding (over-cap file must be refused, untouched) ----------
wsA=$(smoke_tmp)
mkdir -p "$wsA/.jichi"
dd if=/dev/zero of="$wsA/.jichi/memory.md" bs=1024 count=65537 2>/dev/null
# sanity: the fixture is really over the cap
szA0=$(wc -c < "$wsA/.jichi/memory.md" | tr -d ' ')

cat > "$tmp/repliesA.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"hello"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesA.mm" "$tmp/capA"
write_config "$tmp/configA.json" "$MM_PORT"
( cd "$wsA" && with_deadline 120 "$BIN" --config "$tmp/configA.json" \
    -q --no-session --auto --no-stdin -p "REMEMBER one thing" \
    < /dev/null > /dev/null 2>&1 )
mm_stop

# CHECK 1: memory.md is STILL over the cap after the run (pre-fix: 8 bytes).
szA=$(wc -c < "$wsA/.jichi/memory.md" 2>/dev/null | tr -d ' ')
if [ "$szA" = "$OVER_BYTES" ]; then
    t_ok "Leg A: over-cap memory.md is still $OVER_BYTES bytes (refused, not replaced)"
else
    t_fail "Leg A: over-cap memory.md was REPLACED (now $szA bytes, expected $OVER_BYTES)"
fi

# CHECK 2: its first 16 bytes are still zeros (pre-fix: starts '- hello').
first16=$(head_bytes 16 < "$wsA/.jichi/memory.md" | od -An -tx1 | tr -d ' \n')
if [ "$first16" = "00000000000000000000000000000000" ]; then
    t_ok "Leg A: first 16 bytes of memory.md are still zeros (content preserved)"
else
    t_fail "Leg A: first 16 bytes changed (now 0x$first16) -- file was rewritten"
fi

# CHECK 3: the tool RESULT the model received names the refusal (pre-fix: the
# success text "Noted; saved..." appears instead).
if grep -q 'could not be read' "$tmp/capA/req.2" 2>/dev/null; then
    t_ok "Leg A: remember refused with 'could not be read' (model saw the refusal)"
else
    t_fail "Leg A: refusal not in the model's tool result (req.2): $(head_bytes 200 < "$tmp/capA/req.2" 2>/dev/null)"
fi

# --- LEG B: the boundary (exactly at the cap reads fine -> APPENDS) ---------
# The lead's run-0106 correction: an exactly-at-cap file (67108864 B) does NOT
# trip the cap (the trigger is OVER JC_READ_FILE_MAX), so remember must append.
# This check is GREEN pre-fix and stays GREEN post-fix -- it pins that the fix
# refuses only the over-cap case, not the at-cap case.
wsB=$(smoke_tmp)
mkdir -p "$wsB/.jichi"
dd if=/dev/zero of="$wsB/.jichi/memory.md" bs=1024 count=65536 2>/dev/null

cat > "$tmp/repliesB.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"hello"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesB.mm" "$tmp/capB"
write_config "$tmp/configB.json" "$MM_PORT"
( cd "$wsB" && with_deadline 120 "$BIN" --config "$tmp/configB.json" \
    -q --no-session --auto --no-stdin -p "REMEMBER one thing" \
    < /dev/null > /dev/null 2>&1 )
mm_stop

# CHECK 4: the at-cap file GREW (appended) and now contains the note. The append
# of "- hello\n" to a file that does not end in a newline also inserts one, so
# the size grows by 9 (1 newline + "- hello" + newline) and the note is present.
szB=$(wc -c < "$wsB/.jichi/memory.md" 2>/dev/null | tr -d ' ')
if [ "$szB" -gt "$AT_BYTES" ] && grep -q 'hello' "$wsB/.jichi/memory.md"; then
    t_ok "Leg B: at-cap memory.md APPENDED (now $szB bytes, note present) -- fix refuses only OVER the cap"
else
    t_fail "Leg B: at-cap memory.md was not appended (size=$szB, note absent) -- the boundary regressed"
fi

# --- LEG C: normal append (small file, new note appends) --------------------
# Dedupe (an already-present note is not re-added) is unit-covered in
# tests/test_memory.c::test_add_and_tool; here we keep the append half.
wsC=$(smoke_tmp)
mkdir -p "$wsC/.jichi"
printf -- '- first note\n' > "$wsC/.jichi/memory.md"

cat > "$tmp/repliesC.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"second note"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesC.mm" "$tmp/capC"
write_config "$tmp/configC.json" "$MM_PORT"
( cd "$wsC" && with_deadline 120 "$BIN" --config "$tmp/configC.json" \
    -q --no-session --auto --no-stdin -p "REMEMBER one thing" \
    < /dev/null > /dev/null 2>&1 )
mm_stop

# CHECK 5: the new note appended and the old one is preserved.
if grep -q 'second note' "$wsC/.jichi/memory.md" && grep -q 'first note' "$wsC/.jichi/memory.md"; then
    t_ok "Leg C: small memory.md appended the new note (old note preserved)"
else
    t_fail "Leg C: append regressed: $(head_bytes 200 < "$wsC/.jichi/memory.md" 2>/dev/null)"
fi

t_done
