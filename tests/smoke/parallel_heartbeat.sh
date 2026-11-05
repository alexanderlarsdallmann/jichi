#!/bin/sh
# smoke: spawn_parallel batch emits heartbeat events with "batch" and "total" fields
# when --heartbeat is set (M165, D4-C).
#
# THE CASE. A batch of subagents runs without emitting any heartbeat events to the
# jsonl stream, so a supervisor cannot tell if the batch is progressing or wedged.
# The fix adds a periodic heartbeat event from the batch wait loop each time a task
# finishes, emitting "batch: k of m tasks done" at most once per heartbeat interval.
#
# THE FIX. The parallel tool's wait loop calls jc_app_batch_progress when a task
# finishes. This emits a `heartbeat` event with "batch" and "total" fields when
# jsonl is on and heartbeat_secs > 0.
#
#   1  a batch with --heartbeat 1 emits at least one heartbeat event during the wait
#   2  at least one heartbeat event carries "batch":k and "total":m fields
#   3  without --heartbeat, no heartbeat event appears (the flag is opt-in)
#
# Markers: the jsonl stream is the test's only observable; the mockmodel's delay
# drives the batch's duration.

. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
CAP=$(smoke_tmp)

# A mockmodel that takes >= 3 seconds to respond, so we're guaranteed to see
# at least one heartbeat if the interval is 1 second.
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEAD"
  match "## Parallel agents"
  delay 3500
  text LEAD-DONE
rule
  match "LEAD"
  tool spawn_parallel {"tasks":[{"task":"TASK a long-running task","write":false}]}
rule
  match "TASK"
  delay 3000
  tool list_files {"path":"."}
EOF

mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

# --- 1-2: with --heartbeat 1, a heartbeat event appears during the batch ---
cd "$WS"
with_deadline 90 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --output jsonl --heartbeat 1 -p "LEAD" < /dev/null > "$CAP/out.jsonl" 2>&1
rc=$?
mm_stop

# Check for at least one heartbeat event
hb_count=$(grep -c '"type":"heartbeat"' "$CAP/out.jsonl" 2>/dev/null || echo 0)
if [ "$hb_count" -gt 0 ]; then
    t_ok "batch with --heartbeat 1 emitted $hb_count heartbeat event(s)"
else
    t_fail "no heartbeat event in the jsonl stream (red before M165 batch fix)"
fi

# Extract and verify the batch fields
if [ "$hb_count" -gt 0 ]; then
    # Check if ANY heartbeat event has batch and total fields
    hb_with_batch=$(grep '"type":"heartbeat"' "$CAP/out.jsonl" | grep '"batch":' | grep '"total":' | head -1)
    if [ -n "$hb_with_batch" ]; then
        t_ok "at least one heartbeat event carries batch and total fields"
    else
        t_fail "no heartbeat event has batch/total fields"
    fi
else
    t_fail "no heartbeat event to check (red before M165 batch fix)"
fi

# --- 3: without --heartbeat, no heartbeat event appears ---
CAP=$(smoke_tmp)
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEAD"
  match "## Parallel agents"
  delay 3500
  text LEAD-DONE
rule
  match "LEAD"
  tool spawn_parallel {"tasks":[{"task":"TASK a long-running task","write":false}]}
rule
  match "TASK"
  delay 3000
  tool list_files {"path":"."}
EOF

mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cd "$WS"
with_deadline 90 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --output jsonl -p "LEAD" < /dev/null > "$CAP/out.jsonl" 2>&1
rc=$?
mm_stop

hb_count=$(grep -c '"type":"heartbeat"' "$CAP/out.jsonl" 2>/dev/null || true)
if [ -z "$hb_count" ]; then
    hb_count=0
fi
if [ "$hb_count" -eq 0 ]; then
    t_ok "no heartbeat event without --heartbeat (the flag is opt-in)"
else
    t_fail "heartbeat appeared without the flag: $hb_count event(s)"
fi

t_done
