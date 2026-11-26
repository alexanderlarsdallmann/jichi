#!/bin/sh
# smoke: the workspace path-containment fence (M24). Under --auto the mock
# orders a write_file aimed OUTSIDE the workspace: with the fence ON
# (default under --auto) the file must not appear yet the turn still
# completes; with --no-path-fence the same write succeeds -- proving the
# fence, not some other guard, blocked it.
# (Port of tests/e2e/pathfence.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
other=$(smoke_tmp)
outside="$other/escape.txt"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"$outside","content":"pwned"}
rule
  text FENCE_DONE
EOF

# Case 1: fence ON (the --auto default) -> the outside write is denied
mkdir -p "$tmp/cap1"
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "write the file" < /dev/null); rc=$?
mm_stop
if [ ! -e "$outside" ]; then
    t_ok "fence ON: the out-of-root write was denied"
else
    t_fail "fence ON but the outside file was written"
fi
case "$out" in
    *FENCE_DONE*) t_ok "the turn still completed after the denial" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# Case 2: --no-path-fence -> the same write succeeds (the control)
mkdir -p "$tmp/cap2"
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
      -q --no-session --auto --no-path-fence -p "write the file" \
      < /dev/null); rc=$?
mm_stop
if [ -e "$outside" ]; then
    t_ok "--no-path-fence: the same write succeeded (the fence was the guard)"
else
    t_fail "write failed even with the fence off (rc=$rc)"
fi

# Case 3 (M383): search_code is a READ tool -- it must consult the same fence,
# or the model can grep file *contents* from outside the workspace (worse than
# list_files, which M324 fenced when it leaked only names). The mock orders a
# search of an outside directory holding a secret; with the fence ON the tool
# result -- carried back in the SECOND request body (mockmodel captures it) --
# must NOT contain the secret's content marker, and the turn still completes.
secret="$other/secret.txt"
printf '%s\n' 'LEAKED_CONTENT_XYZZY' > "$secret"
cat > "$tmp/search.mm" <<EOF
wire openai
rule
  count 1
  tool search_code {"pattern":"LEAKED","path":"$other"}
rule
  text SEARCH_FENCE_DONE
EOF
mkdir -p "$tmp/cap3"
mm_start "$tmp/search.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
      -q --no-session --auto -p "search outside" < /dev/null); rc=$?
mm_stop
if [ -f "$tmp/cap3/req.2" ] && ! grep -q "XYZZY" "$tmp/cap3/req.2"; then
    t_ok "search_code fence: outside file content did not leak into the result"
else
    t_fail "search_code leaked outside content (req.2): $(grep -o 'XYZZY' "$tmp/cap3/req.2" 2>/dev/null | head -1)"
fi
case "$out" in
    *SEARCH_FENCE_DONE*) t_ok "the turn completed after the search fence denial" ;;
    *) t_fail "search turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# --- 6 to 10: a value on pathFence that jichi cannot read (def-415) -------------
# THE DEFECT, measured on 2026-11-17 and again on 2026-11-26: `"pathFence": "ture"`
# was read as OFF, in every mode, where leaving the key out means "on under
# --auto"; doctor called it "path fence off" and `doctor --unattended` called it
# "explicitly off". A typo meant as `true` opened the fence. The operator's
# decision of 2026-11-17 (docs/DECISIONS.md): an unreadable value on a fence key
# closes the fence, in every mode, and jichi says so.
#   6  by effect, in a run WITHOUT --auto (where an absent key leaves the fence
#      off): with "ture" an outside search must not leak.
#   7  the load-time warning names the key and the value (not under -q,
#      which silences every warning).
#   8  doctor shows a problem that names pathFence, not "path fence off".
#   9  doctor --unattended fails it as unreadable, not as "explicitly off".
#  10  CONTROL: `false` is still off. Without it, a jichi that turned every
#      fence on would pass 6 to 9.
mkdir -p "$tmp/cap6"
mm_start "$tmp/search.mm" "$tmp/cap6"
write_config "$tmp/config6.json" "$MM_PORT" '"pathFence":"ture"'
out6=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config6.json" \
      -q --no-session -p "search outside" < /dev/null 2> "$tmp/err6"); rc6=$?
mm_stop
if [ -f "$tmp/cap6/req.2" ] && ! grep -q "XYZZY" "$tmp/cap6/req.2"; then
    t_ok "6 \"pathFence\": \"ture\" fences a run without --auto: the outside content did not leak"
else
    t_fail "6 \"pathFence\": \"ture\" left the fence OFF in a run without --auto (rc=$rc6): outside content in the result: $(grep -c 'XYZZY' "$tmp/cap6/req.2" 2>/dev/null) line(s); req.2 present: $([ -f "$tmp/cap6/req.2" ] && echo yes || echo no)"
fi
# The run above has -q, which silences every warning, this one too. The warning
# is read where a person meets it: on stderr of a command without -q.
"$BIN" --config "$tmp/config6.json" doctor < /dev/null > /dev/null 2> "$tmp/err7"
if grep 'pathFence' "$tmp/err7" 2>/dev/null | grep -q 'ture'; then
    t_ok "7 the load-time warning names the key and the value"
else
    t_fail "7 no warning that names pathFence and the unreadable value (stderr: $(head_bytes 160 "$tmp/err7" 2>/dev/null | tr '\n' '|'))"
fi
doc8=$("$BIN" --config "$tmp/config6.json" doctor < /dev/null 2>&1)
if printf '%s' "$doc8" | grep -i 'fence' | grep -q 'cannot read' && \
   ! printf '%s' "$doc8" | grep -q 'path fence off'; then
    t_ok "8 doctor shows the unreadable pathFence as a problem, and the fence as on"
else
    t_fail "8 doctor does not report the unreadable value: $(printf '%s' "$doc8" | grep -i 'fence' | head -2 | tr '\n' '|')"
fi
doc9=$("$BIN" --config "$tmp/config6.json" doctor --unattended < /dev/null 2>&1)
if printf '%s' "$doc9" | grep 'unattended' | grep -i 'fence' | grep -q 'cannot read' && \
   ! printf '%s' "$doc9" | grep -q 'explicitly off'; then
    t_ok "9 doctor --unattended fails it as an unreadable value, not as explicitly off"
else
    t_fail "9 doctor --unattended: $(printf '%s' "$doc9" | grep 'unattended' | grep -i 'fence' | head -1)"
fi
write_config "$tmp/config10.json" "$MM_PORT" '"pathFence":false'
doc10=$("$BIN" --config "$tmp/config10.json" doctor < /dev/null 2>&1)
if printf '%s' "$doc10" | grep -q 'path fence off'; then
    t_ok "10 CONTROL: \"pathFence\": false is still off"
else
    t_fail "10 CONTROL: \"pathFence\": false is no longer reported off: $(printf '%s' "$doc10" | grep -i 'fence' | head -1)"
fi

t_done
