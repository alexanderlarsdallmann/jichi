#!/bin/sh
# smoke: a write child that repeats a refused call, or copies a placeholder, is stopped;
# and every loop stop is named as one, not as the iteration cap (M869, the demos
# drive's N27; DEFERRED def-305).
#
# THE CASE. Run 0015's third worker on card 116 sent M218's placeholder as arguments
# from call 551 on: jichi refused each, and the worker repeated the same refused call
# about once a second, 742 times, until N24's overflow ended it after 19 minutes. M432's
# failure detector only warned (every third repeat), and M829's stop counted only a
# SUCCESSFUL call repeated with the same result. And when M829's stop did end a child,
# the batch result called it "stopped at its iteration cap" (def-305), which points a
# lead at the wrong remedy.
#
# THE FIX (DECISIONS M869; N27's (a) and (b)):
#   - (a) a child whose same refused call reaches M829's threshold (9) is ended, its
#     work kept, as M829 ends a success loop;
#   - (b) three copied placeholders in a row end it sooner;
#   - every loop stop is named for what it was, in the ending and the delegate report.
#
#   1  a child repeating the same refused call is stopped, and the ending says so
#   2  its delegate report says it repeated itself (not "re-delegate a NARROWER task")
#   3  a child copying a placeholder three times in a row is stopped at three
#   4  M829's success loop is named as one, not as the iteration cap (def-305)
#   5  CONTROL: a child refused twice that then succeeds is not stopped
#   6  telemetry records the three kinds (loop_stop)
#   7  a child cycling through four calls with nothing new is stopped (M870, N21)
#   8  CONTROL: the same rotation with one result changing each time is not
#   9  CONTROL: a child that edits and re-runs the same failing test, round after round,
#      is not stopped as a loop: a change starts the count again (M869, amended -- its
#      first build counted the failures across the whole turn)
#
# Markers: the endings, the report lines and the events are jichi's own output.
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
CAP=$(smoke_tmp)
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDR read a file that is not there","write":true},{"task":"CHILDP copy a placeholder","write":true},{"task":"CHILDS check the same thing","write":true},{"task":"CHILDC recover","write":true},{"task":"CHILDN rotate","write":true},{"task":"CHILDF rotate with something new","write":true},{"task":"CHILDE edit and test","write":true}]}
rule
  match "CHILDR"
  match "\"role\":\"tool\""
  tool read_file {"path":"missing-r.txt"}
rule
  match "CHILDR"
  tool write_file {"path":"r.txt","content":"r\n"}
rule
  match "CHILDP"
  match "\"role\":\"tool\""
  tool run_terminal_command {"elided":"PLACEHOLDER, not arguments: 1779 bytes of arguments were dropped"}
rule
  match "CHILDP"
  tool write_file {"path":"p.txt","content":"p\n"}
rule
  match "CHILDS"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo same-output"}
rule
  match "CHILDS"
  tool write_file {"path":"s.txt","content":"s\n"}
rule
  match "CHILDC"
  match "C-RECOVERED"
  text C-DONE
rule
  match "CHILDC"
  match "could not open"
  match "missing-c.txt"
  match "c.txt"
  tool run_terminal_command {"command":"echo C-RECOVERED-$((40+2))"}
rule
  match "CHILDC"
  match "\"role\":\"tool\""
  tool read_file {"path":"missing-c.txt"}
rule
  match "CHILDC"
  tool write_file {"path":"c.txt","content":"c\n"}
rule
  match "CHILDN"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$(( (n + 1) % 4 )); echo $n > .rot; echo rot-$n"}
rule
  match "CHILDN"
  tool write_file {"path":"n.txt","content":"n\n"}
rule
  match "CHILDF"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n + 1)); echo $n > .rot; echo rot-$((n % 4)); [ $((n % 4)) = 2 ] && echo fresh-$n; true"}
rule
  match "CHILDF"
  tool write_file {"path":"f.txt","content":"f\n"}
rule
  match "CHILDE"
  tool write_file {"path":"e.txt","content":"e\n"}
  tool run_terminal_command {"command":"test -f nope-e.txt"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "maxSubagentIters":40}
EOF
TEL="$CAP/tel.jsonl"
( cd "$WS" && with_deadline 180 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --log "$TEL" --log-level full -p "LEADX run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1)
section() {  # <task marker> -> that task's part of the batch result, JSON-escaped,
             # up to the next heading
    grep -o "### Task [0-9]*: $1[^#]*" "${LR:-/dev/null}" 2>/dev/null | head -1
}

RS=$(section CHILDR)
if printf '%s' "$RS" | grep -q -F 'was stopped for repeating the same refused call after'; then
    t_ok "a child repeating the same refused call is stopped, and the ending says so"
else
    t_fail "the refused-call loop was not stopped or not named (red before M869): $(printf '%s\n' "$RS" | grep -o 'This task [^.]*' | head -1)"
fi
if printf '%s' "$RS" | grep -q -F 'repeated the same call without progress' &&
   ! printf '%s' "$RS" | grep -q 'NARROWER'; then
    t_ok "its delegate report says it repeated itself, not to narrow the task"
else
    t_fail "the report's advice: $(printf '%s\n' "$RS" | grep -o '\[delegate\] [^.]*' | tail -1)"
fi
PS=$(section CHILDP)
if printf '%s' "$PS" | grep -q -F 'was stopped for copying placeholders instead of arguments after 4 tool call(s)'; then
    t_ok "a child copying a placeholder three times in a row is stopped at three"
else
    t_fail "the placeholder loop: $(printf '%s\n' "$PS" | grep -o 'This task [^.]*' | head -1)"
fi
SS=$(section CHILDS)
if printf '%s' "$SS" | grep -q -F 'was stopped for repeating the same successful call' &&
   ! printf '%s' "$SS" | grep -q -F 'iteration cap'; then
    t_ok "M829's success loop is named as one, not as the iteration cap (def-305)"
else
    t_fail "the success loop's ending: $(printf '%s\n' "$SS" | grep -o 'This task [^.]*' | head -1)"
fi
CS=$(section CHILDC)
if printf '%s' "$CS" | grep -q 'C-DONE' && ! printf '%s' "$CS" | grep -q 'was stopped for'; then
    t_ok "CONTROL: a child refused twice that then succeeds is not stopped"
else
    t_fail "the recovering child was stopped, or did not finish: $(printf '%s' "$CS" | head -3)"
fi
k1=$(grep '"event":"loop_stop"' "$TEL" 2>/dev/null | grep -c '"kind":"refused"')
k2=$(grep '"event":"loop_stop"' "$TEL" 2>/dev/null | grep -c '"kind":"placeholder"')
k3=$(grep '"event":"loop_stop"' "$TEL" 2>/dev/null | grep -c '"kind":"same_result"')
if [ "$k1" = 1 ] && [ "$k2" = 1 ] && [ "$k3" = 1 ]; then
    t_ok "telemetry records the three kinds (loop_stop)"
else
    t_fail "loop_stop events: refused=$k1 placeholder=$k2 same_result=$k3"
fi

# --- 7-8: a rotation (M870, N21) -------------------------------------------------------
NS=$(section CHILDN)
if printf '%s' "$NS" | grep -q -F 'was stopped for cycling through the same calls with nothing new'; then
    t_ok "a child cycling through four calls with nothing new is stopped (M870)"
else
    t_fail "the rotation was not stopped (red before M870): $(printf '%s\n' "$NS" | grep -o 'This task [^.]*' | head -1)"
fi
FS=$(section CHILDF)
if printf '%s' "$FS" | grep -q -F 'iteration cap' &&
   ! printf '%s' "$FS" | grep -q -F 'cycling through'; then
    t_ok "CONTROL: a rotation with one result changing each time is not stopped as one"
else
    t_fail "the fresh rotation: $(printf '%s\n' "$FS" | grep -o 'This task [^.]*' | head -1)"
fi
ES=$(section CHILDE)
if printf '%s' "$ES" | grep -q -F 'iteration cap' &&
   ! printf '%s' "$ES" | grep -q -F 'was stopped for repeating'; then
    t_ok "CONTROL: edit-and-test rounds with the same failing test are not a loop"
else
    t_fail "edit-and-test rounds were stopped as a loop (red with M869's first build): $(printf '%s\n' "$ES" | grep -o 'This task [^.]*' | head -1)"
fi

t_done
