#!/bin/sh
# smoke: resuming a conversation reports workspace drift to the MODEL (M350).
#
# --continue restores the conversation; the workspace may have moved while it
# slept (a human edit, a git pull, a CLI undo -- M349's recorded residual
# gap). The restored history is full of tool results describing files as they
# WERE; M350 compares the believed files' mtimes against the session file's
# own last save and injects one [resume] note naming what changed, saved
# immediately so a second resume detects nothing twice.
#
# Three runs: run 1 reads f.txt and saves the session; f.txt is then changed
# on disk; run 2 (--continue) must carry the note; run 3 (--continue, nothing
# touched) must NOT inject again -- the note from run 2 stays in the history
# exactly once (idempotence via the run-2 save).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'first version\n' > "$ws/f.txt"

cat > "$tmp/r1.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"f.txt"}
rule
  text RUN1_OK
EOF
mm_start "$tmp/r1.mm" "$tmp/cap1" 3
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    -p "study f.txt" < /dev/null) >/dev/null 2>&1
mm_stop

# The drift test is STRICTLY-newer mtime than the session's save; a same-second
# edit would tie, so give the clock room before changing the file.
sleep 2
printf 'second version\n' >> "$ws/f.txt"

cat > "$tmp/r2.mm" <<'EOF'
wire openai
rule
  text RUN2_OK
EOF
mm_start "$tmp/r2.mm" "$tmp/cap2" 2
write_config "$tmp/c2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c2.json" --auto --no-lite \
    --continue -p "next step" < /dev/null) >/dev/null 2>"$tmp/err2"
mm_stop

# --- 1: the resumed request tells the model what moved -------------------------
if grep -q "\[resume\] since this conversation last ran" "$tmp/cap2/req.1" \
        2>/dev/null && grep -q "f.txt" "$tmp/cap2/req.1" 2>/dev/null; then
    t_ok "run 2's request names the drifted file"
else
    t_fail "no [resume] note in run 2: $(head_bytes 200 "$tmp/cap2/req.1" 2>/dev/null)"
fi

# --- 2: ...and the human is told a note was left -------------------------------
if grep -q "noted for the model" "$tmp/err2"; then
    t_ok "run 2's stderr says the note was left"
else
    t_fail "no stderr notice in run 2: $(head_bytes 200 "$tmp/err2")"
fi

cat > "$tmp/r3.mm" <<'EOF'
wire openai
rule
  text RUN3_OK
EOF
mm_start "$tmp/r3.mm" "$tmp/cap3" 2
write_config "$tmp/c3.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c3.json" --auto --no-lite \
    --continue -p "one more" < /dev/null) >/dev/null 2>"$tmp/err3"
mm_stop

# --- 3: an unchanged workspace injects nothing (pair of check 2) ---------------
if grep -q "noted for the model" "$tmp/err3"; then
    t_fail "run 3 injected again with nothing changed"
else
    t_ok "run 3 stays quiet -- nothing moved since run 2's save"
fi

# --- 4: the history carries run 2's note exactly once ---------------------------
n=$(grep -o "\[resume\] since this conversation last ran" "$tmp/cap3/req.1" \
        2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "exactly one [resume] note in the resumed history"
else
    t_fail "expected 1 note in run 3's request, found '$n'"
fi

# --- 5: run 1 itself carried no note (a fresh session has no 'last ran') -------
if [ -s "$tmp/cap1/req.1" ] && \
   ! grep -q "\[resume\]" "$tmp/cap1/req.1"; then
    t_ok "a fresh session starts without a drift note"
else
    t_fail "run 1's first request is missing or carries a note"
fi

t_done
