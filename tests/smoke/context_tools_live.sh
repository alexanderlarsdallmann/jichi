#!/bin/sh
# smoke: `context tools` advertises the same set a real turn does (M325b).
#
# The reporting surfaces built only jc_tool_register_builtins, so they listed the
# unconditional tools and nothing else -- 16 where a live session in a git repo
# had 26. The report under-stated the very cost it exists to measure, and every
# conditional tool added since M41 had widened the gap silently.
#
# The differential checks are the point: capture what a real turn puts on the
# wire, then compare it against what the report claims, BOTH DIRECTIONS.
#
# Duplication needs its own check, and finding that out is the lesson here. While
# this milestone was being written main() registered ten tools TWICE (a bare
# jc_tool_registry_register is a vec push with no duplicate check). Every set and
# count comparison stayed GREEN through it -- `sort -u` collapses duplicates on
# both sides, and the report builds its own registry once -- so the last check
# compares the RAW entry count against the distinct one.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A git repo, so the eight git tools are in play -- they were the biggest single
# block missing from the report.
(cd "$ws" && git init -q . 2>/dev/null)
: > "$ws/a.txt"

cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  text LIVE_OK
EOF
mm_start "$tmp/r.mm" "$tmp/cap" 1
write_config "$tmp/c.json" "$MM_PORT" '"toolProfile":"full"'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --no-lite \
    --no-session -q -p "hi" < /dev/null) >/dev/null 2>&1
mm_stop

req="$tmp/cap/req.1"
if [ ! -s "$req" ]; then
    t_fail "no request captured -- nothing below can mean anything"
    t_done
fi

grep -o '"name":"[a-z_0-9]*"' "$req" | sed 's/"name":"//;s/"//' | sort -u > "$tmp/wire"
nwire=$(grep -c . "$tmp/wire")
if [ "$nwire" -ge 20 ]; then
    t_ok "a live turn advertised $nwire tools (git repo, full profile)"
else
    t_fail "only $nwire tools on the wire -- fixture is not exercising the gap"
fi

(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/c.json" --no-lite \
    context tools < /dev/null) 2>/dev/null > "$tmp/report"
# The row has two shapes: without telemetry `bytes pct% pct% [*] name`, and --
# since M599 turned metrics on by default, so this driver's own live turn now
# leaves a log the report joins -- `bytes pct% pct% [*] calls name`. An
# extraction that knew one shape read the other as 0 rows and failed a count it
# was not measuring (shape 2 in docs/reading/KIROKU.md, found by the tier).
sed -n 's/^ *[0-9]* *[0-9]*% *[0-9]*% *\**  *[0-9]*  *\([a-z_0-9]*\)$/\1/p' "$tmp/report" \
    | sort -u > "$tmp/rep"
nrep=$(grep -c . "$tmp/rep")

# Direction 1: nothing on the wire may be missing from the report.
miss=$(comm -23 "$tmp/wire" "$tmp/rep" | tr '\n' ' ')
if [ -z "$miss" ]; then
    t_ok "every tool a turn sends appears in the report"
else
    t_fail "the report omits tools a turn sends: $miss"
fi

# Direction 2: nothing in the report may be absent from the wire. This is what
# catches an over-eager registrar -- and duplicates, since `sort -u` on both
# sides hides those but the count comparison below does not.
extra=$(comm -13 "$tmp/wire" "$tmp/rep" | tr '\n' ' ')
if [ -z "$extra" ]; then
    t_ok "the report claims no tool a turn does not send"
else
    t_fail "the report lists tools that are not advertised: $extra"
fi

# The counts must agree across all three views.
hdr=$(sed -n 's/^Tool definitions: \([0-9]*\) advertised.*/\1/p' "$tmp/report")
if [ "$hdr" = "$nrep" ] && [ "$nrep" = "$nwire" ]; then
    t_ok "counts agree: $hdr advertised, $nrep listed, $nwire on the wire"
else
    t_fail "count mismatch: header=$hdr listed=$nrep wire=$nwire"
fi

# NO TOOL MAY BE ADVERTISED TWICE. This needs the RAW wire count, not the unique
# one: jc_tool_registry_register is a vec push with no duplicate check, and while
# this milestone was being written main() registered ten tools twice. Every check
# above stayed green through it -- `sort -u` collapses duplicates on both sides,
# and the report's own registry was built once -- so the three counts still
# agreed while the request carried 36 entries for 26 tools. An earlier draft of
# this driver claimed the count check would catch that. It did not; this does.
# `tr -d` and `-eq`, both deliberately (M459, found by the FreeBSD row).
# BSD `wc -l` pads its output with leading blanks where GNU's does not, so this
# captured "      26" while nwire (from `grep -c`) was "26" -- and the STRING
# comparison below failed while the failure message printed both as 26, a check
# contradicting its own numbers. Strip the blanks, and compare numerically so a
# stray one cannot do it again.
raw=$(grep -o '"name":"[a-z_0-9]*"' "$req" | wc -l | tr -d '[:space:]')
if [ "$raw" -eq "$nwire" ]; then
    t_ok "no tool is advertised twice ($raw entries, $nwire distinct)"
else
    t_fail "$raw tool entries for $nwire distinct names -- duplicate registration"
fi

t_done
