#!/bin/sh
# smoke: a killed spawn_parallel child names the knob that killed it (M325).
#
# Measured: in one workload 6 of 10 spawn_parallel calls failed -- three to this
# watchdog, three to fork() -- and neither message said anything actionable.
# "sub-agent timed out" sends an operator looking for a hung child; the cause was
# a 300-second default on a project whose SUCCESSFUL parallel calls took
# 300-462 seconds. So the message now carries the limit and the config key.
#
# Exercised for real rather than unit-tested: the watchdog lives in the select
# loop around a fork pool, and the thing worth checking is what an operator
# actually reads when it fires.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The child is told to run a command that outlives the 1-second watchdog. The
# parent's rule fires first (count 1), then every later request is a subagent's.
cat > "$tmp/p.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"sleep for a while"}]}
rule
  match "focused sub-agent"
  tool run_terminal_command {"command":"sleep 30"}
rule
  match "\"role\":\"tool\""
  text PARENT_DONE
EOF
mm_start_unbounded "$tmp/p.mm" "$tmp/cap"
write_config "$tmp/c.json" "$MM_PORT" \
    '"parallelTaskTimeout":1,"maxParallelAgents":1,"toolProfile":"full"'

out=$( (cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/c.json" --auto \
        --no-lite --no-session --output jsonl -p "run them in parallel" \
        < /dev/null) 2>"$tmp/err" )
mm_stop

# The call must complete rather than hang: the watchdog exists so one wedged
# child cannot hold the swarm, and that is the property under test first.
if [ -n "$out" ]; then
    t_ok "the parent call returned (the watchdog reaped the child)"
else
    t_fail "no output -- the wedged child was not reaped"
fi

# The message names the LIMIT and the KNOB. Not merely "timed out": that was the
# old text, and it is what sent an operator hunting a hung child instead of a
# setting.
res=$(printf '%s\n' "$out" | sed -n 's/.*"type":"tool_result"\(.*\)/\1/p')
case "$res" in
    *parallelTaskTimeout*)
        t_ok "the timeout names parallelTaskTimeout" ;;
    *"timed out"*)
        t_fail "still the bare message: an operator cannot find the knob: $res" ;;
    *)
        t_fail "no timeout reported at all: $(printf '%s' "$res" | head_bytes 200)" ;;
esac

case "$res" in
    *"after 1s"*) t_ok "it states the limit that actually fired" ;;
    *)            t_fail "the limit value is missing: $res" ;;
esac

t_done
