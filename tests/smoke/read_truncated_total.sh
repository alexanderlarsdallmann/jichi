#!/bin/sh
# smoke: when read_file caps its output, it reports the FILE's line count -- not
# the capped buffer's -- and says how much it did not return (M594).
#
# THE DEFECT, from an operator's interrupted session. Asked to explain a concept
# from a 12,509-line rulebook, the model searched, found the section, and read
# past the cap. jichi answered:
#
#     (no lines in range; file has 4027 lines)... [output truncated]
#
# Both halves in one breath, and the first is false: 4,027 is the number of lines
# in the 256 KB that were RETURNED. The file has 12,509. A model told the file
# ends at 4,027 stops looking for line 11,715 -- it is not a range error, it is a
# denial. This one did not believe it: the next thing it did was
# `wc -l`, which said 12509, and the operator interrupted a session that looked
# like it was going nowhere. It was going somewhere -- it was checking jichi.
#
# The true count costs nothing. read_file reads the WHOLE file (only the output is
# capped), so the bytes are already in memory; M594 counts them.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A file that is comfortably past the default 256 KB cap: 20,000 lines of 40
# bytes is ~800 KB, so roughly the first 6,500 lines are returnable.
# 9,000 lines x 36 bytes = ~324 KB: comfortably past the 256 KB cap and a third
# of the original fixture. The first version used 20,000 lines (~720 KB) and
# failed IN-SUITE ONLY -- passing standalone, which the tier's own retry
# classified as "cross-driver load/resource effects". A driver that needs a
# quiet machine is a flaky driver.
awk 'BEGIN{for(i=1;i<=9000;i++) printf "line %05d ........................\n", i}' \
    > "$ws/big.txt"
real=$(wc -l < "$ws/big.txt" | tr -d ' ')

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"big.txt","offset":8500,"limit":20}
rule
  text DONE
EOF
# The model must survive one large read under suite load; the default deadline
# is for drivers that exchange a few hundred bytes.
MM_DEADLINE=${MM_DEADLINE:-240}
export MM_DEADLINE
mm_start "$tmp/replies.mm" "$tmp/cap" 2
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false,
"logging":{"level":"metrics","path":"$tmp/telem.jsonl"}}
EOF
out=$(cd "$ws" && "$BIN" --config "$tmp/config.json" --no-session -p "read it" 2>&1)
mm_stop

# The tool's text reaches the model, not the terminal, so read it off the wire
# capture the mock model recorded.
body=$(cat "$tmp/cap"/* 2>/dev/null)

if printf '%s' "$body" | grep -q 'no lines in range'; then
    t_ok "the out-of-range read produced the range message (the fixture reached the cap)"
else
    t_fail "no range message in the request capture -- this driver measured nothing.
   $(printf '%s' "$body" | head_bytes 200)"
fi

# 1) the real total must appear ...
if printf '%s' "$body" | grep -q "the file has $real lines"; then
    t_ok "the message reports the FILE's line count ($real)"
else
    t_fail "the file's true line count ($real) is not in the message:
   $(printf '%s' "$body" | grep -o 'no lines in range[^\"]*' | head_bytes 200)"
fi

# 2) ... and the false form must not.
if printf '%s' "$body" | grep -qE '\(no lines in range; file has [0-9]+ lines?\)'; then
    t_fail "the old wording is still emitted for a TRUNCATED read -- it states the
   capped buffer's count as the file's, which is the defect itself."
else
    t_ok "the capped buffer's count is not presented as the file's"
fi

# 3) the reader is told what it CAN read, which is what makes this actionable.
#    Scoped to the RANGE message: an earlier version of this check looked for
#    the word anywhere in the request, and passed while the range message had
#    lost it -- the truncation notice below names the same knob, so the check was
#    satisfied by a different sentence than the one it is about.
rangemsg=$(printf '%s' "$body" | grep -o 'no lines in range[^)]*)' | head -1)
if printf '%s' "$rangemsg" | grep -q 'readMaxBytes'; then
    t_ok "the range message itself names readMaxBytes, so the limit can be acted on"
else
    t_fail "the RANGE message does not name the knob -- a reader who hits this
   cannot act on it: $rangemsg"
fi

# 4) the truncation notice says how much was returned of how much exists
if printf '%s' "$body" | grep -qE 'read lines 1-[0-9]+ of [0-9]+'; then
    t_ok "the truncation notice states the window and the whole"
else
    t_fail "the truncation notice does not say how much is missing, so a second
   read cannot be aimed: $(printf '%s' "$body" | grep -o 'output truncated[^\"]*' | head_bytes 160)"
fi

# 5) CONTROL: a SMALL file must keep the plain wording. Without this, checks 1-4
#    are satisfied by a tool that has simply forgotten how to answer simply.
printf 'one\ntwo\nthree\n' > "$ws/small.txt"
cat > "$tmp/replies2.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"small.txt","offset":900,"limit":5}
rule
  text DONE
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2" 2
cat > "$tmp/config2.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF
(cd "$ws" && "$BIN" --config "$tmp/config2.json" --no-session -p "read it" >/dev/null 2>&1)
mm_stop
body2=$(cat "$tmp/cap2"/* 2>/dev/null)
if printf '%s' "$body2" | grep -q '(no lines in range; file has 3 lines)'; then
    t_ok "an untruncated read keeps the plain, correct wording"
else
    t_fail "the small-file message changed; it was already true and should not have:
   $(printf '%s' "$body2" | grep -o 'no lines in range[^\"]*' | head_bytes 160)"
fi

t_done
