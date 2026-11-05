#!/bin/sh
# smoke: two processes editing one board keep each other's cards (M761).
#
# THE DEFECT, from the operator's twelve questions (plan section 10, "what is
# broken, first"): two processes lost each other's board updates. A session loads
# .jichi/board.json once, when it starts; every edit was applied to that copy and
# the whole file written back. So a run that added a card overwrote whatever
# another process had written since it started -- a `jichi board add` in another
# shell, or a second run -- and both handed out the same card id.
#
# Two shapes, because they fail for different reasons:
#   1-3  a run and a shell, one after the other. No timing window at all: the
#        run's copy is simply old. Runs on every build.
#   4    an edit that cannot be saved is not reported as made.
#   5-6  two shells at the same instant. Reloading first is not enough when both
#        reload before either saves; the edit must also hold a lock. The window is
#        microseconds, so a FAULT=1 build holds it open (JICHI_FAULT_BOARD_PAUSE_MS
#        sleeps between the reload and the save); on any other build these skip.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- a run adds a card while a shell adds one -------------------------------------------
# The run's first tool is `sleep 3`; the shell adds its card during it; then the
# run adds its own. It does NOT list first: a list reads the file (check 3), and a
# run that listed before adding would refresh its copy that way, which would let
# checks 1-2 pass without the edit's own reload -- the first version of this
# driver did exactly that, and the perturbation that removes the reload found it.
# Rules are first-match, so they read from the last reply back.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "added card"
  text BOARD_DONE
rule
  match "\"role\":\"tool\""
  tool board {"action":"add","title":"from the run"}
rule
  tool run_terminal_command {"command":"sleep 3"}
EOF
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT" '"board":true'
(cd "$ws" && "$BIN" board add "already there" < /dev/null > /dev/null 2>&1)
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto --budget-tokens 100k \
    --deadline 2m --no-session -p "run the task" \
    < /dev/null > "$tmp/run.out" 2> "$tmp/run.err") &
run_pid=$!
sleep 1
(cd "$ws" && "$BIN" board add "from the shell" < /dev/null > "$tmp/shell.out" 2>&1)
wait "$run_pid"; rc=$?
mm_stop

titles=$(grep '"title"' "$ws/.jichi/board.json" 2>/dev/null | sed 's/.*"title": *"\([^"]*\)".*/\1/' | tr '\n' '|')
# 1: all three cards survive -- the run's save kept the shell's card
case "$titles" in
    *"already there"*) c1a=1 ;; *) c1a=0 ;;
esac
case "$titles" in
    *"from the shell"*) c1b=1 ;; *) c1b=0 ;;
esac
case "$titles" in
    *"from the run"*) c1c=1 ;; *) c1c=0 ;;
esac
if [ "$rc" -eq 0 ] && [ "$c1a" -eq 1 ] && [ "$c1b" -eq 1 ] && [ "$c1c" -eq 1 ]; then
    t_ok "a run's board edit keeps the card another process added while it ran"
else
    t_fail "the board after a run and a shell both added (rc=$rc): $titles"
fi
# 2: ...under three different ids
nids=$(grep '"id"' "$ws/.jichi/board.json" 2>/dev/null | sed 's/[^0-9]//g' | sort -u | wc -l | tr -d ' ')
if [ "$nids" -eq 3 ]; then
    t_ok "the three cards have three different ids"
else
    t_fail "$nids distinct id(s) for the cards: $(grep '"id"' "$ws/.jichi/board.json" 2>/dev/null | tr -d ' \n')"
fi

# --- a run lists the board after a shell added a card ------------------------------------
# A second run, which only lists: its list must read the file, not the copy the
# session loaded at its start.
wsl=$(smoke_tmp)
cat > "$tmp/replies2.mm" <<'EOF'
wire openai
rule
  match "## TODO"
  text LIST_DONE
rule
  match "\"role\":\"tool\""
  tool board {"action":"list"}
rule
  tool run_terminal_command {"command":"sleep 3"}
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" '"board":true'
(cd "$wsl" && "$BIN" board add "already there" < /dev/null > /dev/null 2>&1)
(cd "$wsl" && exec "$BIN" --config "$tmp/config2.json" --auto --budget-tokens 100k \
    --deadline 2m --no-session -p "run the task" \
    < /dev/null > "$tmp/run2.out" 2> "$tmp/run2.err") &
run_pid=$!
sleep 1
(cd "$wsl" && "$BIN" board add "from the shell" < /dev/null > /dev/null 2>&1)
wait "$run_pid"
mm_stop
# 3: the list's result -- the request carrying the board -- names the shell's card
listed=0
for f in "$tmp"/cap2/req.*; do
    if grep -q '## TODO' "$f" && grep -q 'from the shell' "$f"; then
        listed=1
    fi
done
if [ "$listed" -eq 1 ]; then
    t_ok "the board tool's list shows a card another process added after the session began"
else
    t_fail "no list result named the shell's card: $(ls "$tmp/cap2" | tr '\n' ' ')"
fi

# --- an edit into a board that cannot be read ---------------------------------------------
# M533 refuses to save over a board.json that exists and cannot be parsed, so the
# damaged file survives for a human. The tool still answered "added card [1]",
# and the model was told an edit was made that was on no disk.
ws3=$(smoke_tmp)
mkdir -p "$ws3/.jichi"
printf '{ not json\n' > "$ws3/.jichi/board.json"
cat > "$tmp/replies3.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text DAMAGED_DONE
rule
  tool board {"action":"add","title":"into a damaged board"}
EOF
mm_start "$tmp/replies3.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT" '"board":true'
(cd "$ws3" && exec "$BIN" --config "$tmp/config3.json" --auto --budget-tokens 100k \
    --deadline 2m --no-session -p "add a card" < /dev/null > "$tmp/d.out" 2> "$tmp/d.err")
mm_stop
# 4: the model is told the board was not changed, and the file is untouched
told=0
for f in "$tmp"/cap3/req.*; do
    grep -q 'the board was not changed' "$f" && told=1
done
if [ "$told" -eq 1 ] && [ "$(cat "$ws3/.jichi/board.json")" = "{ not json" ]; then
    t_ok "an add into an unreadable board is reported as not made, and the file is untouched"
else
    t_fail "damaged board: told=$told, file now: $(head_bytes 80 "$ws3/.jichi/board.json")"
fi

# --- two shells at the same instant ---------------------------------------------------------
if "$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1"; then
    ws2=$(smoke_tmp)
    (cd "$ws2" && JICHI_FAULT_BOARD_PAUSE_MS=1500 exec "$BIN" board add "first" \
        < /dev/null > "$tmp/a.out" 2>&1) &
    pa=$!
    (cd "$ws2" && JICHI_FAULT_BOARD_PAUSE_MS=1500 exec "$BIN" board add "second" \
        < /dev/null > "$tmp/b.out" 2>&1) &
    pb=$!
    wait "$pa"; wait "$pb"
    t2=$(grep '"title"' "$ws2/.jichi/board.json" 2>/dev/null | sed 's/.*"title": *"\([^"]*\)".*/\1/' | tr '\n' '|')
    # 5: both cards survive two edits made at once
    case "$t2" in
        *first*second*|*second*first*)
            t_ok "two shells adding at the same instant both keep their card" ;;
        *)
            t_fail "two edits at once left: $t2" ;;
    esac
    # 6: ...and each shell was told a different id
    ida=$(sed -n 's/^added card \[\([0-9]*\)\].*/\1/p' "$tmp/a.out")
    idb=$(sed -n 's/^added card \[\([0-9]*\)\].*/\1/p' "$tmp/b.out")
    if [ -n "$ida" ] && [ -n "$idb" ] && [ "$ida" != "$idb" ]; then
        t_ok "each shell was told a different card id ($ida, $idb)"
    else
        t_fail "the two shells were told ids '$ida' and '$idb'"
    fi
else
    t_skip_one "needs a FAULT=1 binary (make clean && make FAULT=1) to hold two edits' window open"
    t_skip_one "needs a FAULT=1 binary (make clean && make FAULT=1) to hold two edits' window open"
fi

t_done
