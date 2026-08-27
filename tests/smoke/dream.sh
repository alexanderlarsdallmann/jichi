#!/bin/sh
# smoke: sleep-consolidation (M102) -- `dream <telemetry>` writes a
# propose-only, dated reflection under $HOME/.jichi.d/dreams/ and never
# touches the workspace. Offline: no model.
# (Port of tests/e2e/dream.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9

cat > "$ws/tl.jsonl" <<'EOF'
{"type":"turn","ws":"/x"}
{"type":"tool_call","tool":"edit_file","ok":true,"ws":"/x"}
EOF

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    dream tl.jsonl < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "dream exits 0"
else
    t_fail "dream rc=$rc: $(head_bytes 200 "$tmp/err")"
fi

draft=""
for f in "$HOME/.jichi.d/dreams/dream-"*.md; do
    [ -f "$f" ] && draft="$f" && break
done
if [ -n "$draft" ]; then
    t_ok "dream wrote a draft under ~/.jichi.d/dreams"
else
    t_fail "no dream-*.md under ~/.jichi.d/dreams"
fi

if [ -n "$draft" ] && grep -q "Propose-only" "$draft" \
   && grep -q "Suggested next actions" "$draft"; then
    t_ok "draft carries its propose-only framing"
else
    t_fail "draft missing the propose-only framing"
fi

# --- 4 (M611, S3): a dream identical to the last one is NOT recorded again ------
# THE DAEMON NOISE THIS FIXES. --idle-dream runs run_dream once per idle stretch;
# over unchanging telemetry every run produced a byte-identical, freshly-dated
# draft. A dream now records a DELTA or nothing. Re-run over the SAME log: no new
# file, and it says so. Born red: before M611 this wrote a second dated draft.
ndreams() { ls "$HOME/.jichi.d/dreams/dream-"*.md 2>/dev/null | grep -c . ; }
n1=$(ndreams)
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    dream tl.jsonl < /dev/null > /dev/null 2>"$tmp/err2"); rc=$?
n2=$(ndreams)
if [ "$rc" -eq 0 ] && [ "$n2" -eq "$n1" ] && grep -q 'unchanged' "$tmp/err2"; then
    t_ok "a re-run over identical telemetry writes no new draft (still $n2) and says 'unchanged'"
else
    t_fail "dedup failed: dreams $n1 -> $n2 (rc=$rc); err=$(head_bytes 120 "$tmp/err2")"
fi

# --- 5 (M611, S3): a CHANGED reflection IS recorded ------------------------------
# The delta must still land. A different telemetry log yields a different analysis
# body, so a new dated draft appears.
cat > "$ws/tl2.jsonl" <<'EOF'
{"type":"turn","ws":"/y"}
{"type":"tool_call","tool":"edit_file","ok":false,"error":"boom","ws":"/y"}
{"type":"tool_call","tool":"edit_file","ok":false,"error":"boom","ws":"/y"}
{"type":"tool_call","tool":"edit_file","ok":false,"error":"boom","ws":"/y"}
EOF
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    dream tl2.jsonl < /dev/null > /dev/null 2>"$tmp/err3")
n3=$(ndreams)
if [ "$n3" -gt "$n2" ]; then
    t_ok "a changed reflection is recorded ($n2 -> $n3)"
else
    t_fail "a changed reflection was not recorded (stuck at $n3)"
fi

# --- 6 (M611, S1): a dream never clobbers an existing dream -----------------------
# The stamp is whole seconds and jc_write_file truncates, so two dreams in one
# second used to overwrite a propose-only draft. Seed a sentinel dream stamped
# with the CURRENT second and the newest mtime, then produce a DIFFERENT dream:
# the sentinel's bytes must survive (a new suffixed/《dated》file carries the new one).
sec=$(date +%s)
seed="$HOME/.jichi.d/dreams/dream-$sec.md"
printf 'SENTINEL_DREAM_KEEPME
' > "$seed"
touch "$seed"   # newest mtime, so the dedup compares against it (content differs)
cat > "$ws/tl3.jsonl" <<'EOF'
{"type":"turn","ws":"/z"}
{"type":"tool_call","tool":"run_terminal_command","ok":false,"error":"nope","ws":"/z"}
{"type":"tool_call","tool":"run_terminal_command","ok":false,"error":"nope","ws":"/z"}
EOF
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    dream tl3.jsonl < /dev/null > /dev/null 2>/dev/null)
if grep -q 'SENTINEL_DREAM_KEEPME' "$seed" 2>/dev/null; then
    t_ok "an existing dream is not clobbered by a same-second write"
else
    t_fail "the sentinel dream was overwritten -- a propose-only draft was lost"
fi

t_done
