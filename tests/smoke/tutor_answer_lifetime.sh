#!/bin/sh
# M926 (R04-F2): /tutor must not free() the tutor's hand-back.
#
# THE DEFECT. The TUI's /tutor ran the assignment-helper sub-agent, printed
# its answer, and then free(answer). But the hand-back is ARENA-allocated on
# the per-turn scratch (jc_subagent_answer_into, M911 -- every answer_out site
# is jc_arena_strdup), not a malloc: free() on an arena-interior pointer is
# undefined and glibc aborts it ("free(): invalid pointer" / "corrupted").
# The stale contract comment that said "malloc-d (caller frees)" invited it.
#
# THE TOOTH (deterministic, no ASan). Drive /tutor on a real PTY with a mock
# provider. The answer is printed to the pty BEFORE the free, so:
#   - fixed  -> the answer marker reaches the pty AND the session stays alive
#               (/exit is clean, exit 0).
#   - revert -> the free() aborts the process; the run ends non-zero (and/or
#               the pty carries a heap-corruption banner).
# Model: mockmodel. Fences/caps per the measurement doctrine (this is a
# correctness run, not a timing run).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Mock: answer the tutor sub-agent's request (matched on the question marker).
cat > "$tmp/tutor.mm" <<'EOF'
wire openai
rule
  match "MARKTQ"
  text "TUTORANSWER-M926 this is the nudge"
EOF
cap=$(smoke_tmp)
mm_start_unbounded "$tmp/tutor.mm" "$cap"
write_config "$tmp/config.json" "$MM_PORT"

cat > "$tmp/tutor.pd" <<EOF
expect "] " 20
delay 400
send "/tutor why does free corrupt? MARKTQ\r"
expect "TUTORANSWER-M926" 25
delay 500
send "/exit\r"
waitexit 15
assertexit 0
EOF

if (cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$tmp/pty.log" \
        "$tmp/tutor.pd" -- "$BIN" --config "$tmp/config.json" --no-lite) \
        > "$tmp/drive.out" 2>&1; then
    t_ok "/tutor delivers the answer and the session stays alive (no free of arena memory)"
else
    t_fail "/tutor delivers the answer and the session stays alive (no free of arena memory)"
    sed 's/^/    | /' "$tmp/drive.out"
fi

# The answer text itself reached the terminal.
if smoke_bgrep "$tmp/pty.log" -q "TUTORANSWER-M926"; then
    t_ok "the tutor's answer is printed to the pty"
else
    t_fail "the tutor's answer is printed to the pty"
fi

# A heap abort leaves a banner; the fixed build must not show one.
if smoke_bgrep "$tmp/pty.log" -qE "free\(\): invalid pointer|munmap_chunk|corrupted size|Segmentation fault"; then
    t_fail "no heap-corruption abort after /tutor (free() was called on arena memory)"
else
    t_ok "no heap-corruption abort after /tutor"
fi

mm_stop
t_done
