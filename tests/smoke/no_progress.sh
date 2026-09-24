#!/bin/sh
# smoke: the same call, answered the same way, is told at three (plan D1, M733).
#
# THE GAP THIS CLOSES. M432 tells a model a FAILING call is looping; a call that
# SUCCEEDS with the same answer every time was invisible. M687 made 200 successful
# tool calls with 0 errors and had the answer by the sixteenth. On the 2026-09-23
# drive one turn ran the same search_code 35 times in a row and was answered
# "(no matches)" each time -- truthfully. The threshold is M731's fit on that drive:
# every futile repeat reached three, no productive turn passed two.
#
# THE CHECKS are the plan's (docs/plans/2026-09-after-m712.md §2), less the stop,
# which is not built: the note at the third identical call and not the second, once;
# an edit between the repeats starts the count again; a different result each time
# is not a repeat; and the finding reaches the journal and the `runs` table. One more
# guards the integration's own input: the registry calls run_terminal_command
# mutating, and a shell command repeated with the same output must still be counted.
#
# The note travels in the tool result inside the NEXT request, so the captured
# requests are the ground truth for "the model was told" (the M429 lesson).
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
NOTE="returned the same result"

run_mm() {   # SCRIPT CAPDIR JOURNAL -> runs jichi in a fresh workspace
    _ws=$(smoke_tmp)
    printf 'hello\n' > "$_ws/a.txt"
    mm_start "$1" "$2" 12
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$_ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
        -q --no-session --auto --budget-tokens 200k --journal "$3" \
        -p "look for it" < /dev/null) > /dev/null 2>&1
    mm_stop
}
last_req() {  # CAPDIR -> the highest-numbered captured request
    ls "$1"/req.* 2>/dev/null | sort -t. -k2 -n | tail -1
}

# --- A: four identical searches, each answering "(no matches)" -----------------
cat > "$tmp/same.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 2
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 3
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 4
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  text SAME_DONE
EOF
mkdir -p "$tmp/ja"
run_mm "$tmp/same.mm" "$tmp/capA" "$tmp/ja/j.jsonl"

# 1: request 4 carries results 1-3, and the third is the first told.
if [ -s "$tmp/capA/req.4" ] && grep -q "$NOTE 3 times" "$tmp/capA/req.4"; then
    t_ok "the model is told at the third identical call"
else
    t_fail "no note in request 4 of $(ls "$tmp"/capA/req.* 2>/dev/null | wc -l | tr -d ' ')"
fi

# 2: request 3 carries results 1-2 and must be clean: two is the productive tail.
if [ -s "$tmp/capA/req.3" ] && ! grep -q "$NOTE" "$tmp/capA/req.3"; then
    t_ok "silent at two (request 3 is clean)"
else
    t_fail "fired too early -- request 3 already carries the note"
fi

# 3: the fourth identical call is not told again.
last=$(last_req "$tmp/capA")
n=$(grep -o "$NOTE" "$last" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "told once in the whole turn, not once per repeat"
else
    t_fail "expected 1 note in $last, found $n"
fi

# 4: the operator is told too (M422's lesson).
if grep '"event":"no_progress"' "$tmp/ja/j.jsonl" 2>/dev/null | grep -q '"repeat":3'; then
    t_ok "the journal carries a no_progress event at repeat 3"
else
    t_fail "no no_progress event: $(grep -o '"event":"[a-z_]*"' "$tmp/ja/j.jsonl" 2>/dev/null | sort -u | tr '\n' ' ')"
fi

# 5: ...and it reaches the table a supervisor reads.
if "$BIN" runs "$tmp/ja" --output json < /dev/null 2>/dev/null | grep -q '"no_progress":1'; then
    t_ok "runs --output json reports no_progress for the run"
else
    t_fail "runs has no no_progress: $("$BIN" runs "$tmp/ja" --output json < /dev/null 2>&1 | head_bytes 200)"
fi

# --- B: an edit between the repeats starts the count again ---------------------
# Two searches, an edit that succeeds, two more: the same answer four times, but
# never three since the tree last changed. The read before the edit is the edit's
# own precondition ("read the file before editing it"); read_file does not touch
# the watch.
cat > "$tmp/edit.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 2
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 3
  tool read_file {"path":"a.txt"}
rule
  count 4
  tool edit_file {"path":"a.txt","old_string":"hello","new_string":"hullo"}
rule
  count 5
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  count 6
  tool search_code {"pattern":"NOT_IN_THIS_TREE_ZQX"}
rule
  text EDIT_DONE
EOF
run_mm "$tmp/edit.mm" "$tmp/capB" "$tmp/jB.jsonl"
# The edit must really have landed -- an edit that failed resets nothing, and this
# check would then pass for the wrong reason. $_ws is run_mm's workspace.
if grep -q hullo "$_ws/a.txt" 2>/dev/null && [ -s "$tmp/capB/req.7" ] &&
   ! grep -q "$NOTE" "$tmp/capB/req.7"; then
    t_ok "an edit between the repeats starts the count again (no note)"
else
    t_fail "a.txt: $(head_bytes 40 < "$_ws/a.txt" 2>/dev/null); notes in request 7: $(grep -o "$NOTE" "$tmp/capB/req.7" 2>/dev/null | wc -l | tr -d ' ')"
fi

# --- C: the same question, a different answer each time ------------------------
cat > "$tmp/diff.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"wc -c < c.txt; printf x >> c.txt"}
rule
  count 2
  tool run_terminal_command {"command":"wc -c < c.txt; printf x >> c.txt"}
rule
  count 3
  tool run_terminal_command {"command":"wc -c < c.txt; printf x >> c.txt"}
rule
  count 4
  tool run_terminal_command {"command":"wc -c < c.txt; printf x >> c.txt"}
rule
  text DIFF_DONE
EOF
run_mm "$tmp/diff.mm" "$tmp/capC" "$tmp/jC.jsonl"
if [ -s "$tmp/capC/req.5" ] && ! grep -q "$NOTE" "$tmp/capC/req.5"; then
    t_ok "a different result each time is not a repeat (no note)"
else
    t_fail "the note fired on changing results: $(grep -o "$NOTE [0-9]* times" "$tmp/capC/req.5" 2>/dev/null | head -1)"
fi

# --- D: a shell command answering the same way IS counted ----------------------
# The registry calls run_terminal_command mutating; were its flag read as "reset",
# the loop the corpora measured most (the same command, the same output) would go
# untold.
cat > "$tmp/shell.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo SAME_EACH_TIME"}
rule
  count 2
  tool run_terminal_command {"command":"echo SAME_EACH_TIME"}
rule
  count 3
  tool run_terminal_command {"command":"echo SAME_EACH_TIME"}
rule
  text SHELL_DONE
EOF
run_mm "$tmp/shell.mm" "$tmp/capD" "$tmp/jD.jsonl"
if [ -s "$tmp/capD/req.4" ] && grep -q "$NOTE 3 times" "$tmp/capD/req.4"; then
    t_ok "a shell command repeated with the same output is told at three"
else
    t_fail "no note for three identical shell calls in $(ls "$tmp"/capD/req.* 2>/dev/null | wc -l | tr -d ' ') requests"
fi

t_done
