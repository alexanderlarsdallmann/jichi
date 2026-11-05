#!/bin/sh
# smoke: a command that succeeds and prints nothing is said to have succeeded, and its
# third silent success in a turn says what the silence means (M895, the demos drive's
# N54).
#
# THE DEFECT. The shell tool answered a silent success with "(no output)" and then
# "[exit status: 0]", and the workers took that as something missing: six of them, in
# two days, re-ran a passing `zig build test` looking for output -- nine times in run
# 0106 alone. They spelled it differently each time (bare, `| tail`, `| grep`), so
# neither M829 nor M870 counted a repeat, and a line the team's lead added to the
# workers' profile did not stop it.
#
# THE FIX (the operator's decision, 2026-10-03: "(a) and (b)"). (a) A command that
# exits 0 and prints nothing reads "(no output: the command succeeded and printed
# nothing)"; one that fails and prints nothing still reads "(no output)". (b) Silent
# successes are counted per turn, by M806's normalised command, so `cmd`, `cmd | tail`
# and `cmd 2>&1` count as one; from the third, the result says it has succeeded
# silently N times, that exit 0 with no output means it passed, and that running it
# again on an unchanged tree will not show more.
#
#   1  a silent success says it succeeded
#   2  CONTROL: a silent failure still reads "(no output)" and its exit status
#   3  the third silent success of one command, spelled three ways, carries the note
#   4  the first two do not
#   5  CONTROL: two different commands, each silent twice, carry no note
#   6  a spawned write child's third silent success carries the note too
#
# The markers are jichi's own words in the requests it sends, never a command the mock
# sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- case C's child runs in a worktree"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
OKTXT='(no output: the command succeeded and printed nothing)'

# run_case NAME MOCK [EXTRA_JSON] -- one headless run in a fresh workspace
run_case() {
    _n="$1"; _mm="$2"; _x="${3:-}"
    _ws=$(smoke_tmp)
    printf 'seed\n' > "$_ws/seed.txt"
    mm_start_unbounded "$_mm" "$tmp/$_n.cap"
    cat > "$tmp/$_n.json" <<EOF
{"toolProfile":"full","lowResource":false,"selfReview":false,"models":[{"name":"m",
 "provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
 "roles":["chat"]}],"snapshots":true,"repoMap":false,"references":false,"maxRetries":0$_x}
EOF
    (cd "$_ws" && with_deadline 120 "$BIN" --config "$tmp/$_n.json" -q --auto --no-session \
        -p "LEAD$_n run the checks" < /dev/null) > "$tmp/$_n.out" 2>&1
    mm_stop
}

# --- case A: one command, silent three times, spelled three ways; then a silent failure
cat > "$tmp/a.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"true"}
rule
  count 2
  tool run_terminal_command {"command":"true | tail -n 5"}
rule
  count 3
  tool run_terminal_command {"command":"true 2>&1"}
rule
  count 4
  tool run_terminal_command {"command":"false"}
rule
  text DONE
EOF
run_case a "$tmp/a.mm"

if grep -q -F "$OKTXT" "$tmp/a.cap/req.2" 2>/dev/null; then
    t_ok "a silent success says it succeeded"
else
    t_fail "request 2 lacks the silent-success words: $(grep -o 'no output[^"]*' "$tmp/a.cap/req.2" 2>/dev/null | head_bytes 120)"
fi
if grep -q -F '(no output)\n[exit status: 1]' "$tmp/a.cap/req.5" 2>/dev/null; then
    t_ok "CONTROL: a silent failure still reads (no output) and its exit status"
else
    t_fail "request 5 lacks the silent failure's plain words: $(grep -o 'no output[^"]*' "$tmp/a.cap/req.5" 2>/dev/null | tail -n 1 | head_bytes 120)"
fi
if grep -q 'succeeded silently 3 times in this turn' "$tmp/a.cap/req.4" 2>/dev/null; then
    t_ok "the third silent success of one command, spelled three ways, carries the note"
else
    t_fail "request 4 carries no note: $(grep -o 'no output[^"]*' "$tmp/a.cap/req.4" 2>/dev/null | tail -n 1 | head_bytes 200)"
fi
if [ -s "$tmp/a.cap/req.3" ] && ! grep -q 'succeeded silently' "$tmp/a.cap/req.3"; then
    t_ok "the first two silent successes carry no note"
else
    t_fail "request 3 already carries a note, or is missing"
fi

# --- case B: CONTROL -- two different silent commands, twice each ----------------------
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"true"}
rule
  count 2
  tool run_terminal_command {"command":"test -d ."}
rule
  count 3
  tool run_terminal_command {"command":"true"}
rule
  count 4
  tool run_terminal_command {"command":"test -d ."}
rule
  text DONE
EOF
run_case b "$tmp/b.mm"
if [ -s "$tmp/b.cap/req.5" ] && ! grep -q 'succeeded silently' "$tmp/b.cap/req.5"; then
    t_ok "CONTROL: two different commands, each silent twice, carry no note"
else
    t_fail "case B: $(grep -o 'succeeded silently[^.]*' "$tmp/b.cap/req.5" 2>/dev/null | head_bytes 120)"
fi

# --- case C: a spawned write child runs one silent command until it is told ------------
cat > "$tmp/c.mm" <<'EOF'
wire openai
rule
  match "LEADc"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADc"
  tool spawn_parallel {"tasks":[{"task":"CHILDS run the suite","write":true}]}
rule
  match "CHILDS"
  match "succeeded silently 3 times"
  text CHILD-DONE
rule
  match "CHILDS"
  tool run_terminal_command {"command":"true"}
EOF
run_case c "$tmp/c.mm" ',"shellFence":false'
if grep -l "CHILDS" "$tmp/c.cap"/req.* 2>/dev/null | xargs grep -l 'succeeded silently 3 times' 2>/dev/null | grep -q .; then
    t_ok "a spawned write child's third silent success carries the note too"
else
    t_fail "no child request carries the note: $(grep -l CHILDS "$tmp/c.cap"/req.* 2>/dev/null | wc -l) child requests"
fi

t_done
