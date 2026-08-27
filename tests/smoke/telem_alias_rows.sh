#!/bin/sh
# smoke: one tool, one row -- the reader groups tool_call events by the tool that
# RAN, not by the name the model happened to type (M591).
#
# THE DEFECT THIS EXISTS FOR. A real workspace log printed
#
#   todo_write               calls=1 ...
#   todowrite                calls=2 ...
#
# for one tool. `todo_write` is a transparent alias (jc_tool.c), so all three
# calls ran todowrite -- but the reader keyed its per-tool row on the raw wire
# name, so every statistic on that tool (ok-rate, latency, output bytes) was
# computed on a fraction of its calls, and nothing said so.
#
# M532 decided DELIBERATELY that the log records the raw name -- "a gate must
# decide on what will run; a message must say what was asked" -- and that
# decision is untouched here. The fix is in the reader, which is why it needs no
# new field and why it repairs the logs already on disk.
#
# The requested spelling is not discarded: it is reported as its own line,
# because a model that keeps reaching for `todo_write` is telling the maintainer
# what the tool list taught it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

log="$HOME/.jichi.d/telemetry/aliased.jsonl"
mkdir -p "$HOME/.jichi.d/telemetry"

# Three calls to one tool under two spellings, plus one unaliased tool as a
# control that the grouping does not smear everything together.
cat > "$log" <<'EOF'
{"event":"tool_call","ts":1000,"sid":"s1","turn":1,"name":"todowrite","ok":true,"duration_ms":1,"output_bytes":10}
{"event":"tool_call","ts":1001,"sid":"s1","turn":1,"name":"todo_write","ok":true,"duration_ms":1,"output_bytes":10}
{"event":"tool_call","ts":1002,"sid":"s1","turn":1,"name":"todoadd","ok":true,"duration_ms":1,"output_bytes":10}
{"event":"tool_call","ts":1003,"sid":"s1","turn":1,"name":"read_file","ok":true,"duration_ms":1,"output_bytes":10}
EOF

out="$tmp/out.txt"
"$BIN" telemetry > "$out" 2>&1

# 1. The fixture must actually have been read. Without this floor every check
#    below passes on an empty report, which is the shape of a test that cannot
#    fail.
if grep -q '^Tools:' "$out"; then
    t_ok "the report has a Tools section (the fixture was read)"
else
    t_fail "no Tools section -- the fixture was not read; every check below would pass on an empty report"
fi

# 2. All three spellings land in ONE row, under the tool that ran.
if grep -qE '^  todowrite +calls=3 ' "$out"; then
    t_ok "three spellings of one tool make one row with calls=3"
else
    t_fail "expected one todowrite row with calls=3; got: $(grep -E '^  todo' "$out" | tr '\n' '|')"
fi

# 3. And no row exists under a requested-but-not-canonical name.
if grep -qE '^  (todo_write|todoadd) +calls=' "$out"; then
    t_fail "a per-tool row still exists under an alias spelling: $(grep -E '^  todo' "$out" | tr '\n' '|')"
else
    t_ok "no per-tool row under an alias spelling"
fi

# 4. The unaliased tool is untouched -- grouping by canonical name must not
#    merge tools that are genuinely different.
if grep -qE '^  read_file +calls=1 ' "$out"; then
    t_ok "a tool called by its own name keeps its own row"
else
    t_fail "read_file's row is wrong or missing: $(grep -E '^  read_file' "$out" | tr '\n' '|')"
fi

# 5. Both requested spellings are reported, with their counts, so the signal is
#    not just swallowed.
if grep -qE 'todo_write +-> todowrite +calls=1' "$out" &&
   grep -qE 'todoadd +-> todowrite +calls=1' "$out"; then
    t_ok "each alias spelling is named with its own count"
else
    t_fail "the alias lines are wrong or missing: $(grep -A 3 'reached for' "$out" | tr '\n' '|')"
fi

# 6. CONTROL: a log where every call names its tool prints no alias section at
#    all. Without this, check 5 could be satisfied by a section that is always
#    printed, and the report would grow a line that means nothing.
log2="$HOME/.jichi.d/telemetry/clean.jsonl"
cat > "$log2" <<'EOF'
{"event":"tool_call","ts":2000,"sid":"s2","turn":1,"name":"read_file","ok":true,"duration_ms":1,"output_bytes":10}
EOF
out2="$tmp/out2.txt"
"$BIN" telemetry > "$out2" 2>&1
if grep -q 'reached for' "$out2"; then
    t_fail "an alias section was printed for a log with no aliased call -- the section means nothing"
else
    t_ok "a log with no aliased call prints no alias section"
fi

t_done
