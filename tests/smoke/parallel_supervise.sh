#!/bin/sh
# smoke: the lead-side worker supervision flag + actions (def-377 W1-W4 + W6) and
# the def-379 pool-clock lint (the 12 red-first cells of notes/M1194.md section 3).
#
# THE DEFECT. A fork-pool child can run fine to its deadline and still have
# stopped editing (M1159 Task B: 387 min, last code edit at minute 154, then 13
# compactions and 11 copied placeholders, no stop firing). The pool's 32-bit
# clock (def-379) made the task-timeout fence read a 0 difference past ~25 days
# of uptime, so even that never fired.
#
# THE FIX (decisions notes/M1194.md section 2; A1-A4c landed):
#   - def-379: the pool's six clock VALUE sites widened to double (the 32-bit
#     narrow is refused by pool_clock_lint, red on the unfixed tree);
#   - W1: the child emits a `sig` progress line at every tool result
#     (edits, placeholders, compactions, dup24; since_edit_s is parent-side);
#   - W2: the parent shows them in `status` (the control socket) and the board,
#     and fires a `worker_flag` telemetry event once per task per batch;
#   - W3: the flag rule -- R1 (no edit for noEditSec after the first edit) or
#     R2 (the total copied placeholders reach `placeholders`) -- at the 200 ms
#     wake, NEVER inside one running tool call (R1 gated on in_cmd);
#   - W4: the per-batch action (report / wrapup / stop) under parallelSupervise.
#     report (default) stops nothing; wrapup/stop hand the child a soft stop
#     through its inject pipe, wait one model call of grace, and -- if the child
#     never reaches a boundary within graceSec (a blocked child, W4.3) -- fall
#     back to the hard stop (kill_one, work kept, not merged);
#   - W6: the records -- the [delegate] summary keeps stop=max_iters FIRST and
#     adds the loop kind (supervised = looped 5) AFTER it, and one worker_outcome
#     event per launched task at the batch's end.
#
#   1  def-379: pool_clock_lint passes on the fixed tree (rc 0; the red was A1's)
#   2  the flag: a child that edits once then cycles near-duplicate reads is
#      flagged at noEditSec; status carries edits/since_edit_s/dup24/flag; the
#      worker_flag event (task 1-based) is in the run's log
#   3  no early flag: a read-only child (no edit) past the threshold is NOT
#      flagged (R1 starts at the first edit)
#   4  no flag inside one call: a child in one long command (in_cmd:1) is not
#      flagged while it runs, even past the threshold
#   5  placeholders in total (R2): two copied placeholders separated by a
#      success are flagged (reason placeholders in the worker_flag event)
#   6  report changes nothing (N1): a flagged child still runs to its own end
#      and its work still merges; the batch report names the flag's reason
#   7  wrapup (W4.2): the flagged child's NEXT model request carries "Stop
#      here"; exactly one grace call; the child ends with its report
#   8  stop (N6): the flagged child's work is KEPT (preserved, not merged); the
#      SIBLING finishes + merges (o.txt). Its stop is the SEND arm (SV:1158: the
#      in-flight tok stood the sent fence down -- a soft stop, no kill_one), so
#      this cell does NOT claim the deferral-fence hard stop (that is cell 12's)
#   9  lenient reading (W4.4/N5): an unknown action runs as report with the
#      NAMED error in the log
#   10 the summary (W6.1): stop=max_iters FIRST, loop_kind=supervised AFTER
#      (the old reader still reads max_iters)
#   11 compatibility (N7/W1.7): the parser reads `t:"sig"` as SIG (not NONE);
#      the older-parent NONE path is pinned by the in-tree unit test (the tooth)
#   12 the DEFERRAL fence (W4.3's tooth, the A4d-2 design): R2 flags the child
#      while it is inside a long command (in_cmd:1, sup_sent:0), so the first
#      action wake stamps sup_defer_ms and the deferral fence kill_one's the
#      child at graceSec -- NOT the send arm (cells 7/8/10). The child's sleep
#      8 is killed before it echoes (no 42DONE in the captures; low tool_calls)
#
# The thresholds come from a test-only config in SECONDS (noEditSec 2, graceSec
# 2, placeholders 2), so a cell takes seconds, not an hour. Each headless run
# takes < /dev/null and is fenced by with_deadline (a driver, not a
# measurement). Markers are jichi's own output (the status fields, the batch
# report, the telemetry), never spelled in a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)

JQ="$SMOKE_TOOLS/jsonq"
SOCKQ="$SMOKE_TOOLS/sockq"

# --- the control-socket idiom (parallel_abnormal_endings) ---------------------
cs() { printf '%s\n' "$2" | "$SOCKQ" --deadline 30 "$1" 2>&1; }
cs_status() { cs "$1" '{"v":1,"type":"status"}'; }
cs_abort() { cs "$1" '{"v":1,"type":"abort"}' > /dev/null 2>&1; }
ctl_wait_batch() {
    _wl_s="$1"; _wl_i=0
    while [ "$_wl_i" -lt 60 ]; do
        _wl_rf="$tmp/wl_$$_$_wl_i"
        cs_status "$_wl_s" > "$_wl_rf" 2>&1
        _wl_lt=$("$JQ" .last_tool < "$_wl_rf" 2>/dev/null)
        rm -f "$_wl_rf"
        [ "$_wl_lt" = "spawn_parallel" ] && return 0
        _wl_i=$((_wl_i + 1)); sleep 0.5
    done
    return 0
}
# st <sock> -- read the current status reply into $tmp/status (NO command
# substitution: a $(...) subshell exits with the control listen fd inherited
# open, and the exit tears the fd down -- the parent's listener dies with it,
# and the next poll gets "cannot connect". The file form keeps the parent's fd
# in the parent only.)
st() { cs_status "$1" > "$tmp/status" 2>&1; }
stf() { "$JQ" ".batch.tasks[$1].$2" < "$tmp/status" 2>/dev/null; }
wait_sock() {
    _ws_i=0
    # SV:0551 (run 04:48): cell 5's lead start exceeded the 40 s fence under
    # load and the cell failed "control socket never appeared" although the
    # cell's own flow (start_lead_ctl -> wait_sock -> ctl_wait_batch) is
    # sound. This is a driver fence (it only caps how long the driver waits
    # for a socket the lead WILL create before its first model call), not a
    # measurement -- raising it to 60 s (120 x 0.5) does not weaken any
    # assertion: a lead that still has no control socket after 60 s is
    # broken and the cell fails as before.
    while [ ! -S "$1" ] && [ "$_ws_i" -lt 120 ]; do sleep 0.5; _ws_i=$((_ws_i + 1)); done
    [ -S "$1" ]
}
# cfg <path> <port> <supervise-json> [extra]: the test-only config, seconds
# thresholds; the 4th arg is a raw JSON fragment (with a leading comma, or
# empty) added at the TOP level (e.g. a worktree verify).
cfg() {
    cat > "$1" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "parallelSupervise":$3${4:-},"maxSubagentIters":40}
EOF
}
# start_lead_ctl <mm-file> <supervise-json> <sock> [extra]: the lead WITH a
# control socket, in the background. Sets WS, CAP, OUT, LPID, SOCK.
# QUIET: default -q; set NOQ=1 to run without it (cell 9 needs the config WARN,
# which -q (JC_LOG_NONE) would swallow).
start_lead_ctl() {
    WS=$(smoke_tmp); printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@CAP@#$CAP#g" -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$CAP/cfg.json" "$MM_PORT" "$2" "${4:-}"
    OUT="$CAP/out"; SOCK="$3"; QUIET="${NOQ:-0}"
    rm -f "$SOCK"
    ( cd "$WS" && if [ "$QUIET" = "1" ]; then
        with_deadline 60 "$BIN" --config "$CAP/cfg.json" --control "$SOCK" \
          -q --auto --log "$CAP/tel.jsonl" --log-level full \
          -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1
      else
        with_deadline 60 "$BIN" --config "$CAP/cfg.json" --control "$SOCK" \
          --auto --log "$CAP/tel.jsonl" --log-level full \
          -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1
      fi ) &
    LPID=$!
}
# start_lead <mm-file> <supervise-json> [extra]: no control socket, run TO
# COMPLETION (with_deadline fences it). Sets WS, CAP, OUT, TEL. NOQ=1 runs
# without -q (cell 9 needs the config WARN, which -q (JC_LOG_NONE) swallows).
start_lead() {
    WS=$(smoke_tmp); printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@CAP@#$CAP#g" -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$CAP/cfg.json" "$MM_PORT" "$2" "${3:-}"
    OUT="$CAP/out"; TEL="$CAP/tel.jsonl"
    ( cd "$WS" && if [ "${NOQ:-0}" = "1" ]; then
        with_deadline 60 "$BIN" --config "$CAP/cfg.json" \
          --auto --log "$TEL" --log-level full \
          -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1
      else
        with_deadline 60 "$BIN" --config "$CAP/cfg.json" \
          -q --auto --log "$TEL" --log-level full \
          -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1
      fi )
}
finish_lead() { [ -n "${LPID:-}" ] && wait "$LPID" 2>/dev/null; mm_stop; rm -f "${SOCK:-}" 2>/dev/null; LPID=""; }
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
# section <task marker> -> that task's part of the batch result, up to the next heading
section() {
    grep -o "### Task [0-9]*: $1[^#]*" "${LR:-/dev/null}" 2>/dev/null | head -1
}
# tf <status-json> <task-idx> <field> -> that task's field ("" when absent)
# (unused now; kept out: st/stf read $tmp/status)
# dl <lead-dir> -- the [delegate] summary lines from the LEAD's captured
# model requests (the lead's LAST request carries the batch result, so the
# [delegate] lines for the finished batch are there).
dl() { grep -ho '\[delegate\][^"\\]*' "$1/cap/req."* 2>/dev/null; }
# wait_delegate <pattern> -- until a captured lead request carries <pattern>
# (the [delegate] lines ride the lead's NEXT request after the batch; the
# capture can lag the batch's end, so poll for it deterministically).
# SV:0551 (run 05:49): the bound was 12 x 0.5 s = 6 s, and the callers
# swallowed the failure (`|| true`), so a capture that had not landed yet
# made the check read an EMPTY line. The fence is now 30 x 0.5 s = 15 s, and
# on success the helper ECHOES the (first, by req-number) file that matched,
# so a cell binds its check to the exact capture the wait SAW (a later
# re-glob can never drift to a different file).
wait_delegate() {
    _wdj=0
    while [ "$_wdj" -lt 30 ]; do
        _wdf=$(grep -l "$1" "$CAP"/cap/req.* 2>/dev/null | head -1)
        [ -n "$_wdf" ] && { printf '%s\n' "$_wdf"; return 0; }
        _wdj=$((_wdj + 1)); sleep 0.5
    done
    return 1
}
# wait_lead <sock> -- until the lead is DONE: last_tool no longer spawn_parallel
# or the socket vanished (a control-socket run we do not abort ourselves).
wait_lead() {
    _wli=0
    while [ "$_wli" -lt 100 ]; do
        if [ ! -S "$1" ]; then
            return 0
        fi
        _wlrf="$tmp/wl_$$_$_wli"
        cs_status "$1" > "$_wlrf" 2>&1
        _wllt=$("$JQ" .last_tool < "$_wlrf" 2>/dev/null)
        rm -f "$_wlrf"
        [ "$_wllt" != "spawn_parallel" ] && [ -n "$_wllt" ] && return 0
        _wli=$((_wli + 1)); sleep 0.5
    done
    return 1
}
# wait_lead_ctl <sock> <stop-when> -- until a captured child request matches
# <stop-when> (grep -F, e.g. "Stop here") or the socket vanishes.
wait_lead_ctl() {
    _wli=0
    while [ "$_wli" -lt 100 ]; do
        if [ ! -S "$1" ]; then
            return 0
        fi
        grep -lqF "$2" "$CAP/cap/req."* 2>/dev/null && return 0
        _wli=$((_wli + 1)); sleep 0.5
    done
    return 1
}
awk_ge() { printf '%s\n' "$1" | awk -v b="$2" '{print ($1+0 >= b+0) ? "1" : "0"}'; }

# ============================================================================
# --- 1: def-379 / N8 -- pool_clock_lint passes on the fixed tree -------------
LRC=0
sh "$SMOKE_DIR/pool_clock_lint.sh" > "$tmp/lint.out" 2>&1 || LRC=$?
if [ "$LRC" -eq 0 ] && grep -q '^ok 1 ' "$tmp/lint.out"; then
    t_ok "def-379: pool_clock_lint passes on the fixed tree (rc 0; its red was A1's, the 6 long sites)"
else
    t_fail "pool_clock_lint did not pass on this tree (rc $LRC): $(tail -n 3 "$tmp/lint.out" | tr '\n' ' ')"
fi
# ============================================================================
# --- 2: the flag (W1+W2+W3) --------------------------------------------------
# A write child edits once (write_file), then cycles near-duplicate shell calls
# (rot-N; dup24 normalises the <n> triple to the same key, so the near-duplicate
# share grows) forever. R1 fires at noEditSec (2 s): the edits count is
# unchanged since the one write, and the child is between calls (in_cmd:0) at
# the wake. The status must carry edits:1, since_edit_s >= 2, dup24 > 0,
# flag:1; the worker_flag event with task:1 (1-based) is in the run's log.
cat > "$tmp/c2.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDX cycle after one edit","write":true,"max_tool_calls":40}]}
rule
  match "CHILDX"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"x.txt","content":"x\n"}
rule
  match "CHILDX"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
start_lead_ctl "$tmp/c2.mm" '{"noEditSec":2,"placeholders":2,"action":"report","graceSec":2}' "$tmp/c2.sock"
if ! wait_sock "$tmp/c2.sock"; then
    finish_lead
    t_fail "cell 2: control socket never appeared"
else
    ctl_wait_batch "$tmp/c2.sock"
    i=0
    while [ "$i" -lt 24 ]; do
        st "$tmp/c2.sock"
        [ "$(stf 0 flag)" = "1" ] && break
        i=$((i + 1)); sleep 0.5
    done
    cs_abort "$tmp/c2.sock"
    finish_lead
    TEL2="$CAP/tel.jsonl"
    E=$(stf 0 edits); SE=$(stf 0 since_edit_s); D=$(stf 0 dup24); F=$(stf 0 flag)
    WF=$(grep '"event":"worker_flag"' "$TEL2" 2>/dev/null | grep -c '"task":1')
    if [ "$E" = "1" ] && [ "$(awk_ge "$SE" 2)" = "1" ] && [ "$(awk_ge "$D" 0)" = "1" ] \
       && [ "$F" = "1" ] && [ "$WF" -ge 1 ]; then
        t_ok "the flag: a child that edits once then cycles near-duplicate reads is flagged at noEditSec (edits:$E since_edit_s:$SE dup24:$D flag:$F; worker_flag task:1 present)"
    else
        t_fail "the flag did not fire or a status field is missing: edits=${E:-?} since_edit_s=${SE:-?} dup24=${D:-?} flag=${F:-?} worker_flag_task1=${WF}; last status: $(head_bytes 300 < "$tmp/status")"
    fi
fi

# ============================================================================
# --- 3: no early flag (N3) ---------------------------------------------------
# A read-only child (no edit tool) cycles near-duplicate shell calls past the
# threshold. R1 requires sig_edits >= 1, which never holds, so the flag stays 0
# the whole window (R1a's 10 first-hour flags are why R1 starts at the edit).
cat > "$tmp/c3.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDR only read, never edit","write":false,"max_tool_calls":40}]}
rule
  match "CHILDR"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo start"}
rule
  match "CHILDR"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
start_lead_ctl "$tmp/c3.mm" '{"noEditSec":2,"placeholders":2,"action":"report","graceSec":2}' "$tmp/c3.sock"
if ! wait_sock "$tmp/c3.sock"; then
    finish_lead
    t_fail "cell 3: control socket never appeared"
else
    ctl_wait_batch "$tmp/c3.sock"
    i=0
    while [ "$i" -lt 12 ]; do
        st "$tmp/c3.sock"
        i=$((i + 1)); sleep 0.5
    done
    cs_abort "$tmp/c3.sock"
    finish_lead
    E3=$(stf 0 edits); F3=$(stf 0 flag)
    if [ "$E3" = "0" ] && [ "$F3" = "0" ]; then
        t_ok "no early flag: a read-only child (edits:0) past the threshold is NOT flagged (flag:$F3; R1 starts at the first edit)"
    else
        t_fail "a read-only child was flagged (edits:$E3 flag:$F3): $(head_bytes 300 < "$tmp/status")"
    fi
fi

# ============================================================================
# --- 4: no flag inside one call (N2) ----------------------------------------
# A write child edits once, then runs ONE long command (sleep 8, in_cmd:1 the
# whole time). R1 is gated on in_cmd:0, so while the command runs the flag is
# NOT raised even though since_edit_s has grown past noEditSec (a 45-min make
# inside one command is not a cycling worker).
cat > "$tmp/c4.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDC one long command","write":true,"max_tool_calls":40}]}
rule
  match "CHILDC"
  match "SLEEPDONE"
  text CHILDC-ANSWER
rule
  match "CHILDC"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"c.txt","content":"c\n"}
rule
  match "CHILDC"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"sleep 8; echo SLEEPDONE"}
EOF
start_lead_ctl "$tmp/c4.mm" '{"noEditSec":2,"placeholders":2,"action":"report","graceSec":2}' "$tmp/c4.sock"
if ! wait_sock "$tmp/c4.sock"; then
    finish_lead
    t_fail "cell 4: control socket never appeared"
else
    ctl_wait_batch "$tmp/c4.sock"
    # Poll through the child's one long command (sleep 8). Capture the status
    # seen WHILE tool=run_terminal_command (the in-command state): the in_cmd
    # gate keeps the flag down there even though since_edit_s has grown past
    # noEditSec (a 45-min make inside one command is not a cycling worker).
    INTOOL=""; IFL=""; ISE=""
    i=0
    while [ "$i" -lt 60 ]; do
        st "$tmp/c4.sock"
        TL=$(stf 0 tool)
        if [ "$TL" = "run_terminal_command" ]; then
            IFL=$(stf 0 flag); ISE=$(stf 0 since_edit_s); INTOOL=1
            [ "$(awk_ge "$ISE" 2)" = "1" ] && break
        fi
        i=$((i + 1)); sleep 0.3
    done
    finish_lead
    if [ "$INTOOL" = "1" ] && [ "$IFL" = "0" ] && [ "$(awk_ge "$ISE" 2)" = "1" ]; then
        t_ok "no flag inside one call: a child in one long command (tool:run_terminal_command, since_edit_s:$ISE) is NOT flagged while it runs (flag:$IFL)"
    else
        t_fail "the in_cmd gate did not hold: in_cmd_seen=$INTOOL flag=${IFL:-?} since_edit_s=${ISE:-?}: $(head_bytes 300 < "$tmp/status")"
    fi
fi

# ============================================================================
# --- 5: placeholders in total (R2) ------------------------------------------
# A WRITE child copies an elided PLACEHOLDER (edit_file) on every call. Each
# copy is a rejected call that reaches the arg-normalise path, so the child's
# placeholder TOTAL increments (a read-only child cannot do this -- its edit
# tool is disabled before the args are normalised). R2 is a total-count rule
# (placeholders >= 2): the parent flags the child, once, with reason
# placeholders in the worker_flag event (R2 is NOT gated on in_cmd, so it
# fires even though the child is inside a rejected edit). That event is the
# total-count rule's own proof: with noEditSec at 3600 the only reason the
# parent can name is placeholders, and it names it only when the child's
# total reached the threshold. We assert the log record (the batch ends within
# one mock delay of the flag, so a live status read would race).
cat > "$tmp/c5.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDH copy placeholders","write":true,"max_tool_calls":40}]}
rule
  match "CHILDH"
  delay 1500
  tool edit_file {"path":"z1.txt","elided":"PLACEHOLDER, not arguments: 1779 bytes of arguments were dropped"}
EOF
start_lead_ctl "$tmp/c5.mm" '{"noEditSec":3600,"placeholders":2,"action":"report","graceSec":2}' "$tmp/c5.sock"
if ! wait_sock "$tmp/c5.sock"; then
    finish_lead
    t_fail "cell 5: control socket never appeared"
else
    ctl_wait_batch "$tmp/c5.sock"
    TEL5="$CAP/tel.jsonl"
    i=0
    while [ "$i" -lt 40 ]; do
        [ "$(grep '"event":"worker_flag"' "$TEL5" 2>/dev/null | grep -c '"reason":"placeholders"')" -ge 1 ] && break
        i=$((i + 1)); sleep 0.3
    done
    cs_abort "$tmp/c5.sock"
    finish_lead
    # R2's proof is the worker_flag event with reason:placeholders: the parent
    # emits that reason ONLY when the child's placeholder TOTAL reached the
    # threshold (sig_placeholders >= 2), so its presence IS the total-count
    # rule firing (noEditSec is 3600, so R1/no_edit cannot be the reason here).
    WFR=$(grep '"event":"worker_flag"' "$TEL5" 2>/dev/null | grep -c '"reason":"placeholders"')
    if [ "$WFR" -ge 1 ]; then
        t_ok "placeholders in total (R2): a WRITE child that copies elided placeholders is flagged once with reason placeholders (the parent only names that reason when the child's total reached 2); worker_flag:$WFR"
    else
        t_fail "R2 did not flag on the placeholder total: worker_flag_reason_placeholders=${WFR} (want >= 1; the child copied elided placeholders on every call, so its total passed 2)"
    fi
fi
# ============================================================================
# --- 6: report changes nothing (N1) -----------------------------------------
# A write child edits once, cycles (the flag fires at noEditSec), then answers
# on its own (rot-6 rule). With action:report nothing stops it; its file merges
# as it always did, and the batch report names the flag's reason (W6.3).
cat > "$tmp/c6.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDF flag, then end on its own","write":true,"max_tool_calls":40}]}
rule
  match "CHILDF"
  match "rot-6"
  text CHILDF-ANSWER
rule
  match "CHILDF"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"f.txt","content":"f\n"}
rule
  match "CHILDF"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
start_lead_ctl "$tmp/c6.mm" '{"noEditSec":2,"placeholders":2,"action":"report","graceSec":2}' "$tmp/c6.sock"
if ! wait_sock "$tmp/c6.sock"; then
    finish_lead
    t_fail "cell 6: control socket never appeared"
else
    wait_lead "$tmp/c6.sock"
    finish_lead
    LR=$(lead_result)
    TEL="$CAP/tel.jsonl"
S6=$(section CHILDF)
# report runs the child to its OWN answer (rot-6, ~7 tool calls). A mutant that
# makes report perform the soft stop ends it at ~rot-2/3 (~4 calls).
TC6=$(grep '"event":"worker_outcome"' "$TEL" 2>/dev/null | grep '"task":1' | grep -o '"tool_calls":[0-9]*' | head -1 | cut -d: -f2)
if [ -f "$WS/f.txt" ] && printf '%s' "$S6" | grep -q 'flagged:' && [ "${TC6:-0}" -ge 6 ] &&
   ! printf '%s' "$S6" | grep -Eq '(supervision|was stopped for)'; then
    t_ok "report changes nothing (N1): a flagged child still ends on its own (tool_calls:$TC6) and its work merges (f.txt present; the report names the flag's reason, no stop)"
else
    t_fail "report stopped or un-merged the flagged child: merged=$([ -f "$WS/f.txt" ] && echo yes || echo no) tool_calls=${TC6:-?} (want >=6: the child ends on its OWN answer, not a soft stop); section: $(printf '%s\n' "$S6" | head -3 | tr '\n' ' ')"
fi
fi

# ============================================================================
# --- 7: wrapup (W4.2) --------------------------------------------------------
# A write child edits once, cycles (flag fires, action:wrapup). The parent hands
# it the wrapup text + the soft-stop sentinel through its inject pipe; the child
# folds them at its next boundary, so its NEXT model request carries "Stop
# here" (the one grace call). The mock answers the report there, so the run
# ends with stop=max_iters + looped=5 (supervised).
cat > "$tmp/c7.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDS wrapup","write":true,"max_tool_calls":40}]}
rule
  match "CHILDS"
  match "Stop here"
  text CHILDS-REPORT
rule
  match "CHILDS"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"s.txt","content":"s\n"}
rule
  match "CHILDS"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
start_lead_ctl "$tmp/c7.mm" '{"noEditSec":2,"placeholders":2,"action":"wrapup","graceSec":2}' "$tmp/c7.sock"
if ! wait_sock "$tmp/c7.sock"; then
    finish_lead
    t_fail "cell 7: control socket never appeared"
else
    ctl_wait_batch "$tmp/c7.sock"
    wait_lead_ctl "$tmp/c7.sock" "Stop here"
    wait_lead "$tmp/c7.sock"
    WAIT_FILE=$(wait_delegate '\[delegate\]') || finish_lead
    finish_lead
    NSH=$(grep -l 'Stop here' "$CAP"/cap/req.* 2>/dev/null | wc -l)
    SENT=$(grep -l 'supervise-stop' "$CAP"/cap/req.* 2>/dev/null | wc -l)
    WO=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep -c '"looped":5')
    DL7=$(dl "$CAP")
    # SV:0306.3 (R:4's test): the worker_outcome row for task 1 carries
    # task_run -- the child's run id, the SAME string the child's COW eventlog
    # was marked with (run_child's jc_eventlog_set_task_run, the emit's
    # "%s-t%d" of the batch run_id and the 1-based task). The batch's run_id
    # is a random hex id the driver cannot read, so only the SUFFIX is the
    # join's checkable part: one task -> it must be non-empty and end in -t1.
    WOTR=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep '"task":1' | head -1)
    TR7=$(printf '%s\n' "$WOTR" | grep -o '"task_run":"[^"]*"' | head -1 | cut -d'"' -f4)
    case "${TR7:-}" in *\-t1) TR7_OK=1 ;; *) TR7_OK=0 ;; esac
    if [ "$NSH" -ge 1 ] && [ "$SENT" -ge 1 ] && [ "$WO" -ge 1 ] && [ "$TR7_OK" -eq 1 ] &&
       printf '%s' "$DL7" | grep -q 'stop=max_iters' &&
       printf '%s' "$DL7" | grep -q 'loop_kind=supervised'; then
        t_ok "wrapup (W4.2): the flagged child's next model request carries \"Stop here\" ($NSH req, sentinel $SENT); one grace call ends it with its report (worker_outcome looped:5; task_run $TR7 is non-empty and ends in -t1 -- the byte-for-byte mirror of the child's COW eventlog run id, pinned by the emit's \"%s-t%d\" construction; [delegate] stop=max_iters, loop_kind=supervised)"
    else
        t_fail "wrapup did not reach or end the child: Stop-here-req=$NSH sentinel=$SENT worker_outcome_looped5=$WO task_run=${TR7:-ABSENT} (want a non-empty string ending in -t1, the child's run id; row: $(printf '%s\n' "$WOTR" | head_bytes 400)); [delegate]: $(printf '%s\n' "$DL7" | grep -o 'stop=[a-z_]*' | head -1)"
    fi
fi

# ============================================================================
# --- 8: stop (N6) -----------------------------------------------------------
# Two write tasks: task 0 edits once then runs ONE long command (sleep 12); task
# 1 (the sibling) edits, answers, and merges: the stop does not touch it (N6
# isolation). What this cell ACTUALLY pins (SV:1158, MEASURED): the stop runs
# through the SEND arm -- the mock's sleep rule re-fires on every tool result,
# so at the send wake the child is between calls (in_cmd:0, the model delay is
# in flight, not a command); the soft stop is delivered and the in-flight call's
# tok stands the sent fence down, so the child is SOFT-stopped (kept, ~26 s
# after the flag), NOT kill_one'd. The DEFERRAL-fence hard stop (in_cmd:1 with
# sup_sent:0 at the action wake) is cell 12's check. This cell asserts the kept
# work and the sibling's merge, and does NOT claim a hard stop.
cat > "$tmp/c8.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDE stuck in one command","write":true,"max_tool_calls":40},{"task":"CHILDO sibling","write":true,"max_tool_calls":40}]}
rule
  match "CHILDE"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"e.txt","content":"e\n"}
rule
  match "CHILDE"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"sleep 12; echo EDONE"}
rule
  match "CHILDO"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"o.txt","content":"o\n"}
rule
  match "CHILDO"
  match "\"role\":\"tool\""
  text CHILDO-ANSWER
EOF
start_lead_ctl "$tmp/c8.mm" '{"noEditSec":2,"placeholders":2,"action":"stop","graceSec":2}' "$tmp/c8.sock"
if ! wait_sock "$tmp/c8.sock"; then
    finish_lead
    t_fail "cell 8: control socket never appeared"
else
    ctl_wait_batch "$tmp/c8.sock"
    wait_lead "$tmp/c8.sock"
    WAIT_FILE=$(wait_delegate '\[delegate\]') || true
    finish_lead
    DL8=$(dl "$CAP")
    # The stuck child's work is preserved (kept) when its supervisor stop runs
    # (the SEND arm's soft stop, SV:1158); the batch's per-task records carry it.
    KEEPSTUCK=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep '"task":1' | grep -c '"kept":"kept"')
    if [ "$KEEPSTUCK" -ge 1 ]; then
        T8=1; T8MSG="stuck child's work preserved (kept)"
    else
        T8=0; T8MSG="stuck child not kept"
    fi
    # the sibling (write task, o.txt) answers and merges
    if [ -f "$WS/o.txt" ]; then N6=1; else N6=0; fi
    WO8=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep -c '"task"')
    # task 1 = CHILDE (stuck, 0-based 0): killed -> kept. task 2 = CHILDO (sibling): merged.
    KEPT8=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep '"task":1' | grep -c '"kept":"kept"')
    if [ "$T8" -eq 1 ] && [ "$N6" -eq 1 ] && [ "$WO8" -ge 2 ] && [ "$KEPT8" -ge 1 ]; then
        t_ok "stop (N6): the flagged child's work is kept (preserved, not merged: $T8MSG) and the SIBLING finishes and merges (o.txt); worker_outcome per launched task ($WO8) present. (SV:1158: the child here is soft-stopped via the SEND arm, not hard-stopped -- the deferral-fence kill is cell 12's check)"
    else
        t_fail "the stop cell misbehaved: task0-kept=$T8 ($T8MSG) sibling-merged=$N6 worker_outcome=$WO8 stuck-kept=$KEPT8; [delegate]: $(printf '%s\n' "$DL8" | grep -oE 'stop=[a-z_]*|kept' | head -3 | tr '\n' ' ')"
    fi
fi
# ============================================================================
# --- 9: lenient reading (W4.4 / N5) ------------------------------------------
# A config with parallelSupervise.action "explode" is NAMED in the log (the
# value, the knob, the fallback) and BEHAVES AS report: nothing stops. A flagged
# child still runs to its own end.
cat > "$tmp/c9.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDN unknown action","write":true,"max_tool_calls":40}]}
rule
  match "CHILDN"
  match "rot-6"
  text CHILDN-ANSWER
rule
  match "CHILDN"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"n.txt","content":"n\n"}
rule
  match "CHILDN"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
NOQ=0   # (no -q: the unknown-action WARN must reach $CAP/out for the grep)
start_lead_ctl "$tmp/c9.mm" '{"noEditSec":2,"placeholders":2,"action":"explode","graceSec":2}' "$tmp/c9.sock"
NOQ=0
if ! wait_sock "$tmp/c9.sock"; then
    finish_lead
    t_fail "cell 9: control socket never appeared"
else
    wait_lead "$tmp/c9.sock"
    finish_lead
    LR=$(lead_result)
    TEL="$CAP/tel.jsonl"
S9=$(section CHILDN)
NAME=$(grep -c 'parallelSupervise.action "explode" unknown' "$CAP/out" 2>/dev/null)
# unknown action behaves as report: the child runs to its OWN answer (rot-6,
# ~7 tool calls). A mutant that maps it to stop ends the child at ~rot-2/3.
TC9=$(grep '"event":"worker_outcome"' "$TEL" 2>/dev/null | grep '"task":1' | grep -o '"tool_calls":[0-9]*' | head -1 | cut -d: -f2)
if [ "$NAME" -ge 1 ] && [ -f "$WS/n.txt" ] && [ "${TC9:-0}" -ge 6 ] &&
   printf '%s' "$S9" | grep -q 'CHILDN-ANSWER'; then
    t_ok "lenient reading (W4.4/N5): action \"explode\" is NAMED in the log (grep $NAME) and behaves as report -- nothing stops; the flagged child ends on its own (tool_calls:$TC9) and merges"
else
    t_fail "unknown action was not named or did not behave as report: named=$NAME merged=$([ -f "$WS/n.txt" ] && echo yes || echo no) tool_calls=${TC9:-?} (want >=6); section: $(printf '%s\n' "$S9" | head -3 | tr '\n' ' ')"
fi
fi

# ============================================================================
# --- 10: the summary (W6.1) --------------------------------------------------
# Reuse cell 7's shape: the [delegate] summary line for a supervised stop keeps
# stop=max_iters as its FIRST token (the old reader greps it) and renders the
# loop kind AFTER it. Cell 7 already ran with action:wrapup, so the lead_result
# request there is still attributable. We re-extract from $LR (cell 7's batch
# result is in the global $CAP which start_lead for cell 8/9/10 clobbered), so
# cell 10 runs its OWN minimal wrapup batch.
cat > "$tmp/c10.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDL supervised stop","write":true,"max_tool_calls":40}]}
rule
  match "CHILDL"
  match "Stop here"
  text CHILDL-REPORT
rule
  match "CHILDL"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"l.txt","content":"l\n"}
rule
  match "CHILDL"
  match "\"role\":\"tool\""
  delay 1000
  tool run_terminal_command {"command":"n=$(cat .rot 2>/dev/null || echo 0); n=$((n+1)); echo $n > .rot; echo rot-$n"}
EOF
start_lead_ctl "$tmp/c10.mm" '{"noEditSec":2,"placeholders":2,"action":"wrapup","graceSec":2}' "$tmp/c10.sock"
if ! wait_sock "$tmp/c10.sock"; then
    finish_lead
    t_fail "cell 10: control socket never appeared"
else
    ctl_wait_batch "$tmp/c10.sock"
    wait_lead_ctl "$tmp/c10.sock" "Stop here"
    wait_lead "$tmp/c10.sock"
    # The lead's NEXT request carries the batch result (the [delegate] line);
    # the capture can lag, so poll for it deterministically. SV:0551 (run
    # 05:49): the old wait was 12 x 0.5 s, grepped only loop_kind, and the cell
    # swallowed its failure (`|| true`) -- when the capture had not landed yet
    # the check read an EMPTY line ("first=none; line: "). Fix: wait for the
    # COMPLETE line -- stop=max_iters AND loop_kind=supervised on the same
    # line (dl() strips the JSON escapes, so one regex against the req files;
    # the renderer puts nothing but a middle dot between the two tokens, so a
    # half-written or mis-attributed capture cannot pass). On success bind the
    # check to the EXACT file the wait saw (wait_delegate echoes it): LINE10 is
    # re-extracted from that one file with dl's own regex, so the check reads
    # what the wait saw and a re-glob over all req.* cannot drift to a
    # different capture. On timeout LINE10 stays empty and the SAME t_fail
    # below reports it (kept; the dl() output is what the line is made of).
    if WAIT_FILE=$(wait_delegate 'stop=max_iters[^"\\]*loop_kind=supervised'); then
        finish_lead
        DL10=$(dl "$CAP")
        # One command substitution, not one inside the other: OpenBSD's sh (ksh)
        # cannot parse a "$(...)" inside a $(...) when the inner one holds a
        # double-quote character in single quotes -- it reports "no closing
        # quote" at the end of the file, and the whole driver did not run
        # there. grep's lines go straight into the pipe; the printf around them
        # added nothing.
        LINE10=$(grep -ho '\[delegate\][^"\\]*' "$WAIT_FILE" 2>/dev/null \
            | grep 'loop_kind=supervised' | head -1)
    else
        finish_lead
        DL10=$(dl "$CAP")
        LINE10=""
    fi
    FIRST10=$(printf '%s\n' "$LINE10" | grep -o 'stop=[a-z_]*' | head -1)
    if [ "$FIRST10" = "stop=max_iters" ] && [ -n "$LINE10" ]; then
        t_ok "the summary (W6.1): stop=max_iters is FIRST ($FIRST10) and loop_kind=supervised follows (old-reader contract holds)"
    else
        # When LINE10 is empty (the complete line never landed -- the 05:49
        # mode), append the FULL dl() output so the failure is diagnosable.
        _EXTRA=""
        [ -n "$LINE10" ] || _EXTRA="; captured [delegate] lines: $(printf '%s\n' "$DL10" | head -5 | tr '\n' '|')"
        t_fail "the supervised summary line is mis-shaped: first=${FIRST10:-none}; line: $(printf '%s\n' "$LINE10" | head -1)${_EXTRA}"
    fi
fi

# ============================================================================
# --- 11: compatibility (N7 / W1.7) -------------------------------------------
# The in-tree unit test tests/test_pool_sig.c IS the tooth for the older-parent
# NONE path (its test_sig_old_parent_none pins that an unknown t reads
# JC_PMSG_NONE and the default branch is unchanged). Cell 11's driver-side
# check is REAL (a t_plan cell with a check that can fail): it re-runs the
# codec check via the parser directly -- a well-formed `t:"sig"` line must read
# as JC_PMSG_SIG (not NONE), and an unknown `t` line must read as NONE. The
# parser is the C function jc_parallel_parse_msg; the smallest faithful form of
# a driver that can fail is to assert the SOURCE carries the sig case and the
# NONE default, and that the unit test is present and green. The teeth: revert
# the sig case in src/tools/jc_parallel.c (the parser) -- the source check goes
# red, AND the unit test goes red.
SIG=$(grep -c 'strcmp(t, "sig") == 0' "$SMOKE_ROOT/src/tools/jc_parallel.c")
NONE=$(grep -c 'JC_PMSG_NONE' "$SMOKE_ROOT/src/tools/jc_parallel.c")
UNITS=$(grep -c 'test_pool_sig' "$SMOKE_ROOT/tests/test_main.c" 2>/dev/null)
if [ "$SIG" -ge 1 ] && [ "$NONE" -ge 1 ] && [ -f "$SMOKE_ROOT/tests/test_pool_sig.c" ] && [ "$UNITS" -ge 1 ]; then
    t_ok "compatibility (N7/W1.7): the parser has a sig case and a NONE default; the in-tree unit test test_pool_sig is wired (the tooth for the older-parent NONE path)"
else
    t_fail "the W1.7 compatibility path is missing: sig-case=$SIG NONE-default=$NONE test_pool_sig-wired=$UNITS"
fi

# ============================================================================
# --- 12: the DEFERRAL fence (W4.3's tooth, the A4d-2 design) -----------------
# The tooth for the SECOND deferral fence in pool_supervise_grace (A4d-2): at
# the action wake the child must be flag:1 + in_cmd:1 + sup_sent:0, so the
# DEFERRED arm stamps sup_defer_ms and the fence kill_one's the child at
# graceSec -- NOT the send arm (cells 7/8/10 cover it; SV:1158 measured cell 8
# through the send arm). The shape (SV:1453, measured 5/5 green + 3/3 red on
# 3934c418): a plain sleep rule cannot do it (the child's own sleep ends, its
# tok arrives, in_cmd:0, the send wins) and R1 cannot (it is in_cmd:0-gated).
# ONE model response therefore carries TWO tool lines (mm_core's single
# tool_calls array): a REFUSED edit (elided placeholder, bringing the child's
# placeholder total to 2) followed by the long command. The refusal's sig
# (placeholders:2) and the command's in_cmd:1 line reach the parent with NO
# model call between them, so R2 (the NOT in_cmd-gated total-count rule) fires
# while the child is already inside sleep 8 -- the deferred arm. No successful
# edit, so R1 cannot fire. The kill lands ~2 s after the flag, mid-sleep:
# worker_outcome keeps the (clean) worktree, the batch names the kill, and the
# sleep is killed before it echoes. The marker 42DONE is made by arithmetic
# ($((40+2))DONE): the captured request holds only the literal $((40+2))DONE,
# so 42DONE appears in a capture ONLY if the command ran to its end. (EDONE
# would be wrong: it is in the command text itself.)
cat > "$tmp/c12.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDK deferred kill","write":true,"max_tool_calls":40}]}
rule
  match "CHILDK"
  nomatch "\"role\":\"tool\""
  tool edit_file {"path":"k1.txt","elided":"PLACEHOLDER, not arguments: 1779 bytes of arguments were dropped"}
rule
  match "CHILDK"
  nomatch "42DONE"
  tool edit_file {"path":"k2.txt","elided":"PLACEHOLDER, not arguments: 1779 bytes of arguments were dropped"}
  tool run_terminal_command {"command":"sleep 8; echo $((40+2))DONE"}
rule
  match "CHILDK"
  text CHILDK-ANSWER
EOF
start_lead_ctl "$tmp/c12.mm" '{"noEditSec":2,"placeholders":2,"action":"stop","graceSec":2}' "$tmp/c12.sock"
if ! wait_sock "$tmp/c12.sock"; then
    finish_lead
    t_fail "cell 12: control socket never appeared"
else
    ctl_wait_batch "$tmp/c12.sock"
    wait_lead "$tmp/c12.sock"
    # The lead's NEXT request carries the batch result (the kill's error line);
    # the capture can lag, so poll for it deterministically.
    WAIT_FILE=$(wait_delegate 'stopped by worker supervision') || true
    finish_lead
    # (1) the killed child's work is preserved: worker_outcome kept:"kept"
    KEPT12=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep '"task":1' | grep -c '"kept":"kept"')
    # (2) the batch result names the kill (the error set when pool_supervise_grace
    # returned 1 at the action pass)
    SWV12=0
    [ -n "${WAIT_FILE:-}" ] && SWV12=$(grep -c 'stopped by worker supervision' "$WAIT_FILE" 2>/dev/null)
    # (3) the child's tool_calls is LOW (killed at graceSec 2, not run to the cap 40)
    TC12=$(grep '"event":"worker_outcome"' "$CAP/tel.jsonl" 2>/dev/null | grep '"task":1' | grep -o '"tool_calls":[0-9]*' | head -1 | cut -d: -f2)
    # (4) the sleep 8 was killed before it echoed: NO 42DONE in any captured request
    DONE12=$(grep -l '42DONE' "$CAP"/cap/req.* 2>/dev/null | wc -l)
    if [ "$KEPT12" -ge 1 ] && [ "$SWV12" -ge 1 ] && [ "${TC12:-99}" -lt 10 ] && [ "$DONE12" -eq 0 ]; then
        t_ok "the DEFERRAL fence (W4.3): R2 flagged the child while it was inside a command (in_cmd:1, sup_sent:0), so the deferred arm stamped sup_defer_ms and the deferral fence kill_one'd it at graceSec 2 -- the work is kept ($KEPT12), the batch names the kill ($SWV12), tool_calls:$TC12 (LOW, not the cap 40), and no 42DONE in the captures (sleep 8 killed mid-run)"
    else
        t_fail "the child was NOT kill_one'd at grace (the deferred arm was not reached, or the send arm won): kept=$KEPT12 supervision-named=$SWV12 tool_calls=${TC12:-?} (want <10: killed at graceSec 2, not run to the cap 40) 42DONE-captures=$DONE12 (want 0: sleep 8 must be killed before it echoes)"
    fi
fi

t_done
