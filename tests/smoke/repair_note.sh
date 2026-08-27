#!/bin/sh
# smoke: a repaired tool call tells its author (M353).
#
# M148 conservatively repairs malformed tool-call JSON (trailing commas,
# Python literals, quote swaps) and the call then runs. The UNREPAIRABLE
# path always taught -- it echoes the tool's expected schema -- but a
# successful repair told the operator alone (INFO log + args_repair
# telemetry), so the model ran on arguments subtly different from what it
# sent, saw a clean result, learned "my JSON was fine", and kept the habit.
# M353 rides one note on the repaired call's own result.
#
# mockmodel embeds the rule's argument text VERBATIM (no re-serialization),
# so a trailing comma travels the real SSE wire into the real repair path.
# The valid second call is the pair -- asserted as a note-count of exactly
# one in the third request (the first call's note legitimately lives in the
# history; the M350/M351 count-not-absence pattern).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'WIRE_CONTENT_842\n' > "$ws/f.txt"

cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"f.txt",}
rule
  count 2
  tool read_file {"path":"f.txt"}
rule
  text REPAIR_DONE
EOF
mm_start "$tmp/r.mm" "$tmp/cap" 5
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session -p "read it twice" < /dev/null) >/dev/null 2>&1
rc=$?
mm_stop

# --- 1: the broken call still ran (repair worked end to end on the wire) ------
if grep -q "WIRE_CONTENT_842" "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "the trailing-comma call was repaired and the read succeeded"
else
    t_fail "no file content in request 2: $(head_bytes 200 "$tmp/cap/req.2" 2>/dev/null)"
fi

# --- 2: ...and its result says so ----------------------------------------------
if grep -q "note: the arguments you sent" "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "the repaired call's result carries the note"
else
    t_fail "no repair note in request 2"
fi

# --- 3: the VALID call adds no second note (count, not absence) ----------------
n=$(grep -o "note: the arguments you sent" "$tmp/cap/req.3" 2>/dev/null \
    | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "strictly valid arguments ride clean -- exactly one note in history"
else
    t_fail "expected 1 note in request 3, found '$n'"
fi

# --- 4: advisory only ------------------------------------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "the run landed clean (exit 0)"
else
    t_fail "run exited $rc"
fi

t_done
