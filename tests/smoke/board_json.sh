#!/bin/sh
# smoke: `jichi board --output json` is a read-only view of the board for a
# supervisor or a script (M900; plan 2026-10-03, step 2.2).
#
# THE GAP. `jichi board` printed a render for people and nothing else; the control
# socket's status has no board, and telemetry shows board calls only as tool calls.
# A supervisor that wanted the team's board had to parse columns of text, or keep a
# second list beside it. Plan §2's advice to a supervisor -- read the team's board,
# do not keep your own -- had nothing to read.
#
# THE FIX. `jichi board [list] --output json` prints one line: "phase" (null when
# none), "counts" ({todo, doing, done}) and "cards" (each with id, title, state, and
# phase and note when set). Read-only: the edits keep their text output and their
# lock. A board that exists and cannot be read exits 1 and prints nothing on stdout:
# an empty board there would be indistinguishable from "no cards yet".
#
#   1  the view parses and its counts match the board
#   2  each card carries its id, title and state, and its phase when set
#   3  "phase" is null with none set, and the active phase once set
#   4  `board list --output json` prints the same view
#   5  an unreadable board exits 1 with nothing on stdout
#   6  CONTROL: no board at all is exit 0, zero counts and no cards
#   7  CONTROL: without --output, `jichi board` still prints the columns
#   8  the view does not write the board file
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

b() { (cd "$ws" && "$BIN" board "$@" < /dev/null); }

b add "JSONMARK one" design > /dev/null 2>&1
b add "JSONMARK two" > /dev/null 2>&1
b add "JSONMARK three" testing > /dev/null 2>&1
b done 1 > /dev/null 2>&1
b move 2 doing > /dev/null 2>&1
b --output json > "$tmp/v1.json" 2> "$tmp/v1.err"; rc1=$?

# 1: parses, counts
if [ "$rc1" -eq 0 ] \
   && [ "$("$JQ" '.counts.todo' "$tmp/v1.json" 2>/dev/null)" = "1" ] \
   && [ "$("$JQ" '.counts.doing' "$tmp/v1.json" 2>/dev/null)" = "1" ] \
   && [ "$("$JQ" '.counts.done' "$tmp/v1.json" 2>/dev/null)" = "1" ]; then
    t_ok "the view parses and its counts match the board"
else
    t_fail "rc=$rc1, view: $(head_bytes 200 < "$tmp/v1.json")"
fi

# 2: the cards (the file's order: by id)
if [ "$("$JQ" '.cards[0].id' "$tmp/v1.json" 2>/dev/null)" = "1" ] \
   && [ "$("$JQ" '.cards[0].title' "$tmp/v1.json" 2>/dev/null)" = "JSONMARK one" ] \
   && [ "$("$JQ" '.cards[0].state' "$tmp/v1.json" 2>/dev/null)" = "done" ] \
   && [ "$("$JQ" '.cards[0].phase' "$tmp/v1.json" 2>/dev/null)" = "design" ] \
   && [ "$("$JQ" '.cards[1].state' "$tmp/v1.json" 2>/dev/null)" = "doing" ] \
   && ! "$JQ" -q '.cards[1].phase' "$tmp/v1.json" 2>/dev/null \
   && [ "$("$JQ" '.cards[2].state' "$tmp/v1.json" 2>/dev/null)" = "todo" ]; then
    t_ok "each card carries its id, title and state, and its phase when set"
else
    t_fail "cards: $(head_bytes 300 < "$tmp/v1.json")"
fi

# 3: phase null, then set
null1=$("$JQ" -t null '.phase' "$tmp/v1.json" > /dev/null 2>&1 && echo yes)
b phase testing > /dev/null 2>&1
b --output json > "$tmp/v2.json" 2>/dev/null
if [ "$null1" = "yes" ] && [ "$("$JQ" '.phase' "$tmp/v2.json" 2>/dev/null)" = "testing" ]; then
    t_ok "\"phase\" is null with none set, and the active phase once set"
else
    t_fail "phase before: null=${null1:-no}; after: $("$JQ" '.phase' "$tmp/v2.json" 2>&1)"
fi

# 4: list form
b list --output json > "$tmp/v3.json" 2>/dev/null
if [ -s "$tmp/v3.json" ] && cmp -s "$tmp/v2.json" "$tmp/v3.json"; then
    t_ok "\`board list --output json\` prints the same view"
else
    t_fail "list form differs: $(head_bytes 120 < "$tmp/v3.json")"
fi

# 8 (taken before 5 breaks the file): the view writes nothing. The file is put on
# one line first, so a save -- which pretty-prints -- would change its bytes.
tr -d '\n' < "$ws/.jichi/board.json" > "$tmp/board.one" && cp "$tmp/board.one" "$ws/.jichi/board.json"
sum1=$(cksum < "$ws/.jichi/board.json")
b --output json > /dev/null 2>&1
b list --output json > /dev/null 2>&1
sum2=$(cksum < "$ws/.jichi/board.json")

# 5: unreadable
cp "$ws/.jichi/board.json" "$tmp/board.good"
printf '{"cards":[{"id":1,' > "$ws/.jichi/board.json"
b --output json > "$tmp/v5.out" 2> "$tmp/v5.err"; rc5=$?
if [ "$rc5" -eq 1 ] && [ ! -s "$tmp/v5.out" ] && grep -q 'could not be read' "$tmp/v5.err"; then
    t_ok "an unreadable board exits 1 with nothing on stdout"
else
    t_fail "unreadable: rc=$rc5, stdout $(wc -c < "$tmp/v5.out" | tr -d ' ') bytes: $(head_bytes 120 < "$tmp/v5.out")"
fi
cp "$tmp/board.good" "$ws/.jichi/board.json"

# 6: no board
ws2=$(smoke_tmp)
(cd "$ws2" && "$BIN" board --output json < /dev/null > "$tmp/v6.json" 2>/dev/null); rc6=$?
if [ "$rc6" -eq 0 ] && [ "$("$JQ" '.counts.todo' "$tmp/v6.json" 2>/dev/null)" = "0" ] \
   && [ "$("$JQ" '.cards' "$tmp/v6.json" 2>/dev/null)" = "[]" ]; then
    t_ok "CONTROL: no board at all is exit 0, zero counts and no cards"
else
    t_fail "no board: rc=$rc6, $(head_bytes 120 < "$tmp/v6.json")"
fi

# 7: text unchanged
b > "$tmp/text.out" 2>/dev/null
if grep -q '^## TODO' "$tmp/text.out" && grep -q 'JSONMARK one' "$tmp/text.out"; then
    t_ok "CONTROL: without --output, \`jichi board\` still prints the columns"
else
    t_fail "text: $(head_bytes 120 < "$tmp/text.out")"
fi

if [ -n "$sum1" ] && [ "$sum1" = "$sum2" ]; then
    t_ok "the view does not write the board file"
else
    t_fail "board.json changed under the view: $sum1 -> $sum2"
fi

t_done
