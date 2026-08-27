#!/bin/sh
# smoke: an armed envelope states its limits at takeoff (M355).
#
# M347's fuel gauge rings at ~80% -- but nobody briefed the pilot on the tank
# size at takeoff: the AUTO prompt said only "bounded by an iteration budget",
# no numbers, and finding 14's budget deaths were sized in calls the model
# could have paced against from call 1 had it known the cap. M355 states the
# armed limits (and only the armed ones) in the system prompt, plus the fact
# that the 80% bell exists.
#
# Two runs, M310-paired: with --max-tool-calls the first request's system
# prompt must carry the plan naming 5; without any envelope the same prompt
# must not mention it.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/f.mm" <<'EOF'
wire openai
rule
  text PLAN_DONE
EOF
mm_start "$tmp/f.mm" "$tmp/cap1" 2
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session --max-tool-calls 5 -p "go" < /dev/null) >/dev/null 2>&1
mm_stop

# --- 1: the bounded run is told its bounds, numbers included -------------------
if grep -q "This run is bounded" "$tmp/cap1/req.1" 2>/dev/null && \
   grep -q "tool calls: 5" "$tmp/cap1/req.1" 2>/dev/null; then
    t_ok "the flight plan names the 5-call cap at takeoff"
else
    t_fail "no flight plan in the bounded run's first request"
fi

# --- 2: ...and mentions only ARMED budgets --------------------------------------
if grep -q "token budget" "$tmp/cap1/req.1" 2>/dev/null; then
    t_fail "an unarmed budget was named"
else
    t_ok "unarmed budgets stay unmentioned"
fi

cat > "$tmp/g.mm" <<'EOF'
wire openai
rule
  text NOPLAN_DONE
EOF
mm_start "$tmp/g.mm" "$tmp/cap2" 2
write_config "$tmp/c2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c2.json" --auto --no-lite \
    --no-session -p "go" < /dev/null) >/dev/null 2>&1
mm_stop

# --- 3: no envelope, no plan (the pair; artifact must exist) --------------------
if [ -s "$tmp/cap2/req.1" ] && \
   ! grep -q "This run is bounded" "$tmp/cap2/req.1"; then
    t_ok "an unbounded run's prompt carries no flight plan"
else
    t_fail "request missing, or a plan appeared without an envelope"
fi

t_done
