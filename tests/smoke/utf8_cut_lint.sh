#!/bin/sh
# smoke lint: no new byte-counted `%.Ns` cut of a string in src/ (M889, the demos
# drive's N51).
#
# WHY THIS EXISTS. `%.70s` keeps 70 BYTES, and a multi-byte character can straddle
# the cut. spawn_parallel printed each task's title that way; run 0104's title had an
# em dash starting at its 70th byte, one byte of it was kept, and the telemetry line
# that carried the batch result was not valid UTF-8. M889 cut that title with
# jc_utf8_trunc_len and made the event log's sink repair any line, but 27 such
# conversions remain in src/, most of them over ASCII (a commit hash's 12 hex digits,
# a ref, a session id), some over text a model or a user wrote. This lint does not
# judge which is which. It holds the count per file, so that a new one is a decision:
# cut a string that may hold UTF-8 with jc_utf8_trunc_len and `%.*s`.
#
# CHECKED      -- (1) the scan reads string literals at all: it finds today's 27 in
#                 src/*.c, the same files as the table below; (2) no file has more
#                 than its number (a new byte cut, printed); (3) no file has fewer (a
#                 fixed one: lower its number) and no file outside the table has any.
# UNIVERSE     -- `%.<digits>s` inside a double-quoted segment of a line of
#                 src/**/*.c. A comment that quotes one, in backticks, is not counted.
# NOT CHECKED  -- whether a counted conversion's argument can hold non-ASCII text; a
#                 string literal that spans an escaped quote (`\"`) splits wrongly.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)

# Today's numbers. Lower one when you fix a conversion; never raise one.
cat > "$tmp/table" <<'EOF'
src/chat/jc_agent.c 2
src/main.c 7
src/snapshot/jc_snapshot.c 3
src/tools/jc_tool_parallel.c 10
src/tui/jc_tui.c 5
EOF

( cd "$SMOKE_ROOT" && find src -name '*.c' | sort ) > "$tmp/files"
if [ ! -s "$tmp/files" ]; then
    t_fail "no src/*.c found under $SMOKE_ROOT -- the scan reads nothing"
    t_done
fi
( cd "$SMOKE_ROOT" && while IFS= read -r f; do
    awk -v F="$f" '{
        n = split($0, seg, "\"")
        c = 0
        for (i = 2; i <= n; i += 2) {
            s = seg[i]
            while (match(s, /%\.[0-9]+s/)) {
                c++
                s = substr(s, RSTART + RLENGTH)
            }
        }
        if (c > 0) print F ":" FNR ": " c
    }' "$f"
done < "$tmp/files" ) > "$tmp/hits"
awk -F: '{ n[$1] += $3 } END { for (f in n) print f, n[f] }' "$tmp/hits" | sort > "$tmp/counts"

# --- 1: the scan reads string literals: today's total, in the table's files ---------
total=$(awk '{ s += $2 } END { print s + 0 }' "$tmp/counts")
want=$(awk '{ s += $2 } END { print s + 0 }' "$tmp/table")
if [ "$total" -ge 1 ] && [ "$total" -le "$want" ]; then
    t_ok "the scan finds $total byte-counted cut(s) in src/ (the table holds $want)"
else
    t_fail "the scan finds $total, the table holds $want: $(head -n 5 "$tmp/hits" | tr '\n' ' ')"
fi
# --- 2: no file has more than its number ------------------------------------------------
over=""
while read -r f n; do
    w=$(awk -v F="$f" '$1 == F { print $2 }' "$tmp/table")
    [ -n "$w" ] || w=0
    [ "$n" -le "$w" ] || over="$over $f($n>$w)"
done < "$tmp/counts"
if [ -z "$over" ]; then
    t_ok "no file has gained a byte-counted cut"
else
    t_fail "a new byte-counted cut:$over -- use jc_utf8_trunc_len and %.*s: $(awk -F: '{print $1":"$2}' "$tmp/hits" | tr '\n' ' ' | head_bytes 400)"
fi
# --- 3: no number is stale --------------------------------------------------------------
under=""
while read -r f w; do
    n=$(awk -v F="$f" '$1 == F { print $2 }' "$tmp/counts")
    [ -n "$n" ] || n=0
    [ "$n" -ge "$w" ] || under="$under $f($n<$w)"
done < "$tmp/table"
if [ -z "$under" ]; then
    t_ok "every number in the table is today's count"
else
    t_fail "fewer cuts than the table says:$under -- lower its number"
fi

t_done
