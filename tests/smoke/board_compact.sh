#!/bin/sh
# smoke: the board tool returns what changed, not the whole board (M899, the demos
# drive's N37).
#
# THE DEFECT. Every `board` action -- list, add, move, remove, set_phase -- ended
# with the whole board's render. The drive's lead kept 185 cards, so each call
# returned about 30,000 tokens; its history ran at 120,000 to 136,000 tokens, above
# the compactor's target, until the team archived 152 done cards by hand.
#
# THE FIX. add, move, remove and set_phase return the changed card (when there is
# one) and one line of counts. `list` gives TODO and DOING in full and DONE as a
# count; `list` with "all": true gives everything. `/board` and `jichi board` are for
# people and keep the whole render.
#
# The board is built through `jichi board` itself -- 200 cards, 150 of them done,
# each title about 120 bytes -- so the fixture is the file jichi writes.
#
#   1  move on a 200-card board returns under 2 KB
#   2  move returns the moved card, in its new column, and the counts
#   3  list without "all" counts the done cards and shows none of their titles
#   4  list with "all": true shows the done cards
#   5  add returns the new card and the counts, under 2 KB
#   6  remove and set_phase each return under 2 KB
#   7  CONTROL: `jichi board` still prints the done cards for people
#   8  CONTROL: list without "all" still shows every todo and doing card
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- the board: 200 cards through the real command -----------------------------------
pad="the longer description a lead writes when it files a card for a worker to pick up"
i=1
while [ "$i" -le 200 ]; do
    if [ "$i" -le 150 ]; then
        t="DONEMARK-$i $pad"
    elif [ "$i" -le 190 ]; then
        t="TODOMARK-$i $pad"
    else
        t="DOINGMARK-$i $pad"
    fi
    (cd "$ws" && "$BIN" board add "$t" implementation < /dev/null > /dev/null 2>&1)
    i=$((i + 1))
done
i=1
while [ "$i" -le 150 ]; do
    (cd "$ws" && "$BIN" board done "$i" < /dev/null > /dev/null 2>&1)
    i=$((i + 1))
done
i=191
while [ "$i" -le 200 ]; do
    (cd "$ws" && "$BIN" board move "$i" doing < /dev/null > /dev/null 2>&1)
    i=$((i + 1))
done
(cd "$ws" && "$BIN" board < /dev/null > "$tmp/cli.out" 2>&1)
full=$(wc -c < "$tmp/cli.out" | tr -d ' ')

# --- one run: move, list, list all, add, remove, set_phase ---------------------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool board {"action":"move","id":160,"state":"doing"}
rule
  count 2
  tool board {"action":"list"}
rule
  count 3
  tool board {"action":"list","all":true}
rule
  count 4
  tool board {"action":"add","title":"ADDMARK a card filed during the run"}
rule
  count 5
  tool board {"action":"remove","id":201}
rule
  count 6
  tool board {"action":"set_phase","phase":"testing"}
rule
  text BOARD_DONE
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT" '"board":true'
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" --auto --no-session \
    -p "tidy the board" < /dev/null > "$tmp/run.out" 2> "$tmp/run.err")
mm_stop

# last_tool REQ -- the content of the last tool message in a captured request (the
# capture holds the HTTP head; the body follows the first blank line)
last_tool() {
    tr -d '\r' < "$1" | awk 'b { print } /^$/ { b = 1 }' > "$1.body"
    set -- "$1.body"
    _i=0; _c=""
    while "$JQ" -q ".messages[$_i]" "$1" 2>/dev/null; do
        if [ "$("$JQ" ".messages[$_i].role" "$1" 2>/dev/null)" = "tool" ]; then
            _c=$("$JQ" ".messages[$_i].content" "$1" 2>/dev/null)
        fi
        _i=$((_i + 1))
    done
    printf '%s' "$_c"
}
for n in 2 3 4 5 6 7; do
    last_tool "$tmp/cap/req.$n" > "$tmp/res.$n"
done
sz() { wc -c < "$1" | tr -d ' '; }

# 1-2: move
if [ -s "$tmp/res.2" ] && [ "$(sz "$tmp/res.2")" -lt 2048 ]; then
    t_ok "move on a 200-card board returns under 2 KB ($(sz "$tmp/res.2") bytes; the whole board is $full)"
else
    t_fail "move returned $(sz "$tmp/res.2") bytes (the whole board is $full)"
fi
if grep -q 'TODOMARK-160 .*(doing' "$tmp/res.2" && grep -q '^Board: 39 todo, 11 doing, 150 done' "$tmp/res.2"; then
    t_ok "move returns the moved card, in its new column, and the counts"
else
    t_fail "move's result: $(head_bytes 200 < "$tmp/res.2")"
fi

# 3-4: list
if ! grep -q 'DONEMARK' "$tmp/res.3" && grep -q '150 cards' "$tmp/res.3"; then
    t_ok "list without \"all\" counts the done cards and shows none of their titles"
else
    t_fail "list: $(grep -c DONEMARK "$tmp/res.3") done titles shown, $(sz "$tmp/res.3") bytes"
fi
if [ "$(grep -c 'DONEMARK' "$tmp/res.4")" -eq 150 ]; then
    t_ok "list with \"all\": true shows the done cards"
else
    t_fail "list all: $(grep -c DONEMARK "$tmp/res.4") of 150 done titles"
fi

# 5-6: add, remove, set_phase
if grep -q '^added card \[201\]' "$tmp/res.5" && grep -q 'ADDMARK' "$tmp/res.5" \
   && grep -q '^Board: ' "$tmp/res.5" && [ "$(sz "$tmp/res.5")" -lt 2048 ]; then
    t_ok "add returns the new card and the counts, under 2 KB"
else
    t_fail "add's result ($(sz "$tmp/res.5") bytes): $(head_bytes 200 < "$tmp/res.5")"
fi
if grep -q '^removed card \[201\]' "$tmp/res.6" && [ "$(sz "$tmp/res.6")" -lt 2048 ] \
   && grep -q 'active phase: testing' "$tmp/res.7" && [ "$(sz "$tmp/res.7")" -lt 2048 ]; then
    t_ok "remove and set_phase each return under 2 KB"
else
    t_fail "remove $(sz "$tmp/res.6") bytes, set_phase $(sz "$tmp/res.7") bytes: $(head_bytes 160 < "$tmp/res.7")"
fi

# 7-8: controls
if [ "$(grep -c 'DONEMARK' "$tmp/cli.out")" -eq 150 ]; then
    t_ok "CONTROL: \`jichi board\` still prints the done cards for people"
else
    t_fail "jichi board printed $(grep -c DONEMARK "$tmp/cli.out") of 150 done cards"
fi
if [ "$(grep -c 'TODOMARK' "$tmp/res.3")" -eq 40 ] && [ "$(grep -c 'DOINGMARK' "$tmp/res.3")" -eq 10 ]; then
    t_ok "CONTROL: list without \"all\" still shows every todo and doing card"
else
    t_fail "list showed $(grep -c TODOMARK "$tmp/res.3") of 40 todo-or-moved and $(grep -c DOINGMARK "$tmp/res.3") of 10 doing"
fi

t_done
