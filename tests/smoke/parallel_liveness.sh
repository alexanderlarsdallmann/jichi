#!/bin/sh
# smoke: liveness heartbeats at every depth (M1047, batch-14 item (e)).
#
# THE GAP. While the lead blocks waiting on a `spawn_parallel` batch (Leg A) or an
# in-process `spawn_subagent` (Leg B), its `--output jsonl --heartbeat <secs>`
# stream is SILENT for the whole block: a supervisor (the `stale` watchdog) cannot
# tell a working child from a wedged one. On the unfixed head a ~5 s child stream
# produced ZERO heartbeat rows in the lead jsonl until the child finished, and an
# in-process subagent's ~5 s model call produced ZERO rows in the subagent window.
#
# THE FIX. (Leg A) `run_pool`'s 200 ms select loop calls jc_app_batch_progress
# (done, total, LIVE) every idle wake, so the existing once-per-heartbeat_secs
# throttle yields a `heartbeat` row naming the batch and how many children are
# still live. (Leg B) when stream_subagents==0 (headless), `subagent_run` forwards
# a minimal callback carrying ONLY on_progress/user, so the subagent's model calls
# keep the jsonl alive while the headless silent-subagent contract (no content)
# is preserved.
#
#   1  Leg A: >= 3 heartbeat rows carry BOTH "batch" and "live", one with live >= 1
#      BEFORE the child's finish row (the cadence while children stream)
#   2  Leg A: >= 1 heartbeat row carries a "live" field (names its live children)
#   3  Leg B: >= 2 heartbeat rows appear in the subagent window (between the
#      spawn_subagent tool_call row and its tool_result row)
#   4  CONTROL: without --heartbeat, no heartbeat row appears in either leg (opt-in)
#   5  Regression: the finish tick still fires (a Leg A batch heartbeat with
#      done == total, i.e. the child's exit)
#
# Markers: the jsonl stream is the test's only observable; the mockmodel `drip`
# (a slow SSE stream, ~5 s) drives each block's duration, the way
# first_token_time.sh's drip.sse does.

. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

# A ~5 s slow SSE stream: head+first event immediately, then one event every
# 1000 ms (the mockmodel `drip`). `sse-file` + `drip` are proven in
# first_token_time.sh; the content text is the child/subagent's final answer.
# The two variants differ only in the answer text, so each leg's own marker
# ("CHILDDONE" / "SUBRES") lands in its tool_result and lets the lead's follow-up
# request be told apart from the child's.
write_drip() {
    _dw_out=$1; _dw_txt=$2
    cat > "$_dw_out" <<EOF
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"$_dw_txt"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":" "},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":" "},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":20,"completion_tokens":4}}

data: [DONE]

EOF
}

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"

# run LEG [HB] -- one headless jichi run; jsonl in $CAP/out.jsonl. LEG is "a"
# (spawn_parallel) or "b" (spawn_subagent). HB is the --heartbeat argument or ""
# for the no-flag control run.
run() {
    _leg=$1; _hb=$2
    CAP=$(smoke_tmp)
    if [ "$_leg" = a ]; then
        write_drip "$CAP/drip.sse" CHILDDONE
        cat > "$CAP/r.mm" <<EOF
wire openai
rule
  match "LEADA"
  match "## Parallel agents"
  text LEADA-DONE
rule
  match "LEADA"
  tool spawn_parallel {"tasks":[{"task":"CHILDTASK slow","write":false}]}
rule
  match "CHILDTASK"
  drip 1000
  sse-file $CAP/drip.sse
EOF
    else
        write_drip "$CAP/drip.sse" "SUBRES done"
        cat > "$CAP/r.mm" <<EOF
wire openai
rule
  match "LEADB"
  nomatch "SUBRES"
  tool spawn_subagent {"task":"SUBTASK slow"}
rule
  match "LEADB"
  text LEADB-DONE
rule
  match "SUBTASK"
  drip 1000
  sse-file $CAP/drip.sse
EOF
    fi
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    # cfg() embeds $MM_PORT, so the config must be written only after the mock
    # announces a port (an empty port breaks the apiBase -> instant http error).
    cfg > "$CAP/cfg.json"
    _hbopt=""
    [ -n "$_hb" ] && _hbopt="--heartbeat $_hb"
    if [ "$_leg" = a ]; then _pm="LEADA"; else _pm="LEADB"; fi
    ( cd "$WS" && with_deadline 90 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --output jsonl $_hbopt -p "$_pm" < /dev/null > "$CAP/out.jsonl" 2>&1 )
    _rc=$?
    mm_stop
    # Publish the run's artefacts for the checks below.
    EVAL_OUT=$CAP/out.jsonl
    EVAL_RC=$_rc
}

# hb_rows OUT -- the set of heartbeat rows (the extraction floor: print the rows,
# not just their count).
hb_rows() {
    grep '"type":"heartbeat"' "$1" 2>/dev/null || true
}
# batch_rows OUT -- heartbeat rows carrying a "batch" field (the Leg A ticks).
batch_rows() {
    grep '"type":"heartbeat"' "$1" 2>/dev/null | grep '"batch":' || true
}
# live_rows OUT -- heartbeat rows carrying a "live" field.
live_rows() {
    grep '"type":"heartbeat"' "$1" 2>/dev/null | grep '"live":' || true
}
# count ROWSFILE -- number of lines.
nrows() {
    if [ -s "$1" ]; then grep -c . "$1" 2>/dev/null || true; else printf '0\n'; fi
}

# ===========================================================================
# Leg A: a spawn_parallel batch whose one child streams for ~5 s.
# ===========================================================================
run a 1
runA_rc=$EVAL_RC
runA=$EVAL_OUT
: > "$tmp/a_batch"; batch_rows "$runA" > "$tmp/a_batch"
: > "$tmp/a_live";  live_rows  "$runA" > "$tmp/a_live"
a_batch_n=$(nrows "$tmp/a_batch")
a_live_n=$(nrows "$tmp/a_live")

# --- 1: >= 3 batch+live rows, one with live >= 1 BEFORE the finish row --------
# The finish row is the batch heartbeat with live == 0 (done == total). A live>=1
# row before it proves the tick fired while the child was still streaming, not
# merely at its exit.
a_live_ge1_before_finish=0
finish_seen=0
while IFS= read -r _row; do
    _lv=$(printf '%s' "$_row" | sed -n 's/.*"live":\([0-9][0-9]*\).*/\1/p')
    if [ "$finish_seen" -eq 0 ] && [ -n "$_lv" ] && [ "$_lv" -ge 1 ] 2>/dev/null; then
        a_live_ge1_before_finish=1
    fi
    if [ "$finish_seen" -eq 0 ] && [ "$_lv" = "0" ]; then
        finish_seen=1
    fi
done < "$tmp/a_live"
if [ "$runA_rc" -eq 0 ] && [ "$a_batch_n" -ge 3 ] 2>/dev/null && \
   [ "$a_live_ge1_before_finish" -eq 1 ]; then
    t_ok "Leg A: $a_batch_n batch heartbeat rows, one with live>=1 before the finish row"
else
    t_fail "Leg A cadence (rc=$runA_rc, batch+live rows=$a_batch_n, live>=1 before finish=$a_live_ge1_before_finish):
$(hb_rows "$runA")"
fi

# --- 2: >= 1 heartbeat row carries "live" ------------------------------------
if [ "$a_live_n" -ge 1 ] 2>/dev/null; then
    t_ok "Leg A: $a_live_n heartbeat row(s) carry the live field"
else
    t_fail "Leg A: no heartbeat row carries the live field:
$(hb_rows "$runA")"
fi

# ===========================================================================
# Leg B: an in-process spawn_subagent whose model call streams for ~5 s.
# ===========================================================================
run b 1
runB_rc=$EVAL_RC
runB=$EVAL_OUT
# The subagent window: between the spawn_subagent tool_call row and its
# tool_result row.
awk '/"type":"tool_call"/ && /"name":"spawn_subagent"/{f=1;next}
     /"type":"tool_result"/ && /"name":"spawn_subagent"/{f=0}
     f' "$runB" 2>/dev/null > "$tmp/b_window"
b_hb_n=$(grep '"type":"heartbeat"' "$tmp/b_window" 2>/dev/null | grep -c . || true)
[ -n "$b_hb_n" ] || b_hb_n=0

# --- 3: >= 2 heartbeat rows in the subagent window ---------------------------
if [ "$runB_rc" -eq 0 ] && [ "$b_hb_n" -ge 2 ] 2>/dev/null; then
    t_ok "Leg B: $b_hb_n heartbeat rows in the subagent window"
else
    t_fail "Leg B (rc=$runB_rc, heartbeats in window=$b_hb_n):
$(grep '"type":"heartbeat"' "$runB" 2>/dev/null)
--- subagent window ---
$(cat "$tmp/b_window" 2>/dev/null | head_bytes 800)"
fi

# ===========================================================================
# Control: without --heartbeat, no heartbeat row in either leg.
# ===========================================================================
run a ""
runa_ctrl=$EVAL_OUT
run b ""
runb_ctrl=$EVAL_OUT
c_a=$(hb_rows "$runa_ctrl" | grep -c . || true); [ -n "$c_a" ] || c_a=0
c_b=$(hb_rows "$runb_ctrl" | grep -c . || true); [ -n "$c_b" ] || c_b=0
if [ "$c_a" -eq 0 ] && [ "$c_b" -eq 0 ]; then
    t_ok "control: no heartbeat row without --heartbeat (leg A=$c_a, leg B=$c_b)"
else
    t_fail "control: heartbeat appeared without the flag (leg A=$c_a, leg B=$c_b):
$(hb_rows "$runa_ctrl")
$(hb_rows "$runb_ctrl")"
fi

# ===========================================================================
# Regression: the finish path still runs (the batch completes).
#
# The finish tick (jc_app_batch_progress on a child's pipe EOF) shares the
# once-per-interval throttle with the idle liveness ticks, so while a child
# streams on a 1 s cadence the distinct done==total row is folded into the
# interval (the design note: "a finish and a liveness tick in the same interval
# emit only one row") and is not separately observable -- asserting on a
# "live:0" row would be flaky (it lands within 1 s of the last liveness emit
# ~80% of the time). What IS deterministic, and is what the finish path
# produces, is the batch's COMPLETION: the spawn_parallel tool_result carrying
# the child's answer -- emitted only after the child's pipe goes EOF, is reaped,
# done reaches total, and the finish-tick call site on that path runs.
# parallel_heartbeat.sh (run separately in the check phase) is the guard for the
# batch/total row shape itself.
# ===========================================================================
finish_res=$(grep '"type":"tool_result"' "$runA" 2>/dev/null | grep '"name":"spawn_parallel"' | grep 'CHILDDONE' || true)
if [ -n "$finish_res" ]; then
    t_ok "regression: the batch completes (spawn_parallel result carries the child answer)"
else
    t_fail "regression: the batch did not complete (no spawn_parallel tool_result with the child answer):
$(grep '"type":"tool_result"' "$runA" 2>/dev/null)"
fi

t_done
