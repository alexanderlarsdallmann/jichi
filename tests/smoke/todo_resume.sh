#!/bin/sh
# smoke: the task list survives a resume (M606).
#
# `todowrite` is the tool the models lean on most -- 348 of ~408 tool events in
# the local telemetry -- and until M606 it wrote the least durable state in the
# process: a list on jc_app that no codec saved, no resume restored and no
# session switch cleared. After `--continue` the history said "in-progress: write
# the codec" while `todoread` answered "(todo list is empty)"; the model's belief
# and the tool's state disagreed, silently (the M350 drift shape, for the plan
# instead of the files).
#
# Two runs. Run 1 writes a two-item list and saves the session. Run 2 resumes it
# and calls todoread; the mock answers RUN2_EMPTY when the tool result carries
# the empty-list marker and RUN2_LIST otherwise. Run 1's history cannot contain
# that marker (its list was never empty), so the marker can only come from run
# 2's own todoread -- the check needs no JSON surgery on the request body.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/r1.mm" <<'MM'
wire openai
rule
  count 1
  tool todowrite {"todos":[{"content":"write the codec","status":"in_progress"},{"content":"prove it red","status":"pending"}]}
rule
  text RUN1_OK
MM
mm_start "$tmp/r1.mm" "$tmp/cap1" 2
write_config "$tmp/c.json" "$MM_PORT"
out1=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    -p "plan the work" < /dev/null 2>"$tmp/err1"); rc1=$?
mm_stop

# --- 1: run 1 wrote the list and finished ------------------------------------------
if [ "$rc1" -eq 0 ] && grep -q 'write the codec' "$tmp/cap1/req.2" 2>/dev/null; then
    t_ok "run 1 wrote a two-item list (todowrite result reached request 2)"
else
    t_fail "run 1 rc=$rc1; request 2: $(head_bytes 200 "$tmp/cap1/req.2" 2>/dev/null)"
fi

# --- 2: the session file carries the list ---------------------------------------------
sess=$(ls "$HOME/.jichi.d/sessions/"*.json 2>/dev/null | head -n 1)
if [ -n "$sess" ] && grep -q '"todos"' "$sess" && grep -q 'write the codec' "$sess" \
        && grep -q 'in_progress' "$sess"; then
    t_ok "the session file carries a todos array with the item and its state"
else
    t_fail "no todos in the session file: $(head_bytes 160 "${sess:-/dev/null}")"
fi

cat > "$tmp/r2.mm" <<'MM'
wire openai
rule
  count 1
  tool todoread {}
rule
  match "(todo list is empty)"
  text RUN2_EMPTY
rule
  text RUN2_LIST
MM
mm_start "$tmp/r2.mm" "$tmp/cap2" 2
write_config "$tmp/c2.json" "$MM_PORT"
out2=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c2.json" --auto --no-lite \
    --continue -p "what is left?" < /dev/null 2>"$tmp/err2"); rc2=$?
mm_stop

# --- 3: the resumed run's todoread renders the restored list -------------------------
case "$out2" in
    *RUN2_LIST*) t_ok "after --continue, todoread renders the list (rc=$rc2)" ;;
    *RUN2_EMPTY*) t_fail "after --continue, todoread answered '(todo list is empty)' \
while the history holds a two-item list" ;;
    *) t_fail "run 2 rc=$rc2: $(printf '%s' "$out2" | head_bytes 160) / \
$(head_bytes 200 "$tmp/err2")" ;;
esac

# --- 4: the restored item keeps its state, not just its text ---------------------------
# The todoread result is the LAST tool message of request 2; the in-progress item
# renders as "in-progress  write the codec" there. Run 1's history renders the same
# line once (its own todowrite result), so the restored list adds a second copy.
n=$(grep -c 'in-progress  write the codec' "$tmp/cap2/req.2" 2>/dev/null || echo 0)
if [ "$n" -ge 1 ] && [ "$(awk 'BEGIN{c=0} {s=$0; while ((i=index(s,"in-progress  write the codec"))>0) {c++; s=substr(s,i+1)}} END{print c}' "$tmp/cap2/req.2" 2>/dev/null)" -ge 2 ]; then
    t_ok "the restored item is still in-progress (rendered twice in request 2: history + todoread)"
else
    t_fail "restored state missing: 'in-progress  write the codec' occurs \
$(awk 'BEGIN{c=0} {s=$0; while ((i=index(s,"in-progress  write the codec"))>0) {c++; s=substr(s,i+1)}} END{print c}' "$tmp/cap2/req.2" 2>/dev/null) time(s) in request 2 (need 2)"
fi

t_done
