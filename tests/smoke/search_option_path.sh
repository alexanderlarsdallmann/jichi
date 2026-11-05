#!/bin/sh
# smoke: D-31 search_code must refuse a path that begins with `-` (M982).
#
# A tool argument that reaches an external program where it can be read as an
# option, while the fence judges it as a path. The fix: refuse a leading `-`,
# insert `--` before the path so grep parses it as an operand on every platform.
#
# The test runs two arms:
#   1. search_code with a dash-leading path (e.g. `--version`) must return the
#      tool error (not "(no matches)" and NOT grep's output).
#   2. search_code with a normal path still works, and a path given as `./sub`
#      still finds the match.
#
# D-36 (M1110) adds two more legs on the same mock-driven shape:
#   3. search_code whose `path` is a PRESENT FIFO (named pipe) must be refused
#      promptly by the tool instead of hanging in grep's open() (rc 124 under
#      the deadline pre-fix). The 30 s deadline is the instrument that turns
#      the hang into a red.
#   4. anti-vacuous control: a regular file still matches (the guard does not
#      over-block).
#
# M1110-b (D-36 follow-up) adds one more leg:
#   5. a SYMLINK that resolves to a regular file must still be searched (stat
#      follows the link; lstat would over-refuse it -- the M1110-b regression,
#      the house M992/M1020/M1108 semantics).

. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

freshws() {
    rm -rf "$1"; mkdir -p "$1"
    ( cd "$1" && git init -q . && git config user.email t@t && \
        git config user.name t && git add -A . 2>/dev/null && \
        git commit -qm init >/dev/null 2>&1 )
}
mkcfg() { # mkcfg <cfgfile> <extra-json-before-closing-brace>
    printf '{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:%s/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0%s}\n' \
        "$MM_PORT" "$2" > "$1"
}
runauto() { # runauto <ws> <cfg> <jsonl>
    ( cd "$1" && with_deadline 120 "$BIN" --config "$2" -q --no-session --auto \
        -p "AUTOPROMPT" < /dev/null > "$3" 2> /dev/null )
}
runauto30() { # runauto30 <ws> <cfg> <jsonl>
    # D-36 (M1110): a copy of runauto whose with_deadline is 30 instead of 120.
    # The 30 s is the INSTRUMENT that turns D-36's hang into a red: pre-fix the
    # FIFO tool call blocks in grep's open() forever, so the run reaches the
    # deadline and returns rc>=124 -- a timeout is the bug, hence a failure
    # (mirrors rules_fifo_skip.sh's `if [ $rc -ge 124 ]; then t_fail ...`).
    # Used only by legs 3, 4 and 5; legs 1 and 2 keep runauto (120 s).
    ( cd "$1" && with_deadline 30 "$BIN" --config "$2" -q --no-session --auto \
        -p "AUTOPROMPT" < /dev/null > "$3" 2> /dev/null )
}

# --- 1: dash-leading path must be refused --------------------------------------
ws="$tmp/ws1"; freshws "$ws"
a1="$tmp/a1"; rm -rf "$a1"; mkdir -p "$a1/cap"
cat > "$a1/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"x","path":"--version"}
rule
  count 2
  text TASKDONE
EOF
mm_start_unbounded "$a1/r.mm" "$a1/cap"
mkcfg "$a1/cfg.json" ""
runauto "$ws" "$a1/cfg.json" "$a1/out.jsonl"
mm_stop

# The tool should have returned an error with the dash message, NOT "(no matches)"
dash_err=$(grep -h "dash" "$a1"/cap/req.* 2>/dev/null | grep -c . || true)
no_match=$(grep -h "(no matches)" "$a1"/cap/req.* 2>/dev/null | grep -c . || true)
{ [ "${dash_err:-0}" -ge 1 ] && [ "${no_match:-0}" -eq 0 ]; } \
    && t_ok "1 dash path refused with error (not grep output)" \
    || t_fail "1 dash path: error=$dash_err, no_matches=$no_match (red before D-31: grep is called, errors or finds nothing)"

# --- 2: normal path still works, `./` prefix is accepted ----------------------
ws="$tmp/ws2"; freshws "$ws"
printf 'test content\n' > "$ws/file.txt"
a2="$tmp/a2"; rm -rf "$a2"; mkdir -p "$a2/cap"
cat > "$a2/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"test","path":"."}
rule
  count 2
  text TASKDONE
EOF
mm_start_unbounded "$a2/r.mm" "$a2/cap"
mkcfg "$a2/cfg.json" ""
runauto "$ws" "$a2/cfg.json" "$a2/out.jsonl"
mm_stop

# The search should have found the match
found=$(grep -h "file.txt" "$a2"/cap/req.* 2>/dev/null | grep -c . || true)
{ [ "${found:-0}" -ge 1 ]; } \
    && t_ok "2 normal path finds match" \
    || t_fail "2 normal path: found=$found (red before D-31: the `--` separator is missing)"

# --- 3 (D-36, M1110): a PRESENT FIFO as the search path is refused, not hung --
ws3="$tmp/ws3"; freshws "$ws3"
mkfifo "$ws3/pipe"
a3="$tmp/a3"; rm -rf "$a3"; mkdir -p "$a3/cap"
cat > "$a3/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"x","path":"pipe"}
rule
  count 2
  text TASKDONE
EOF
mm_start_unbounded "$a3/r.mm" "$a3/cap"
mkcfg "$a3/cfg.json" ""
runauto30 "$ws3" "$a3/cfg.json" "$a3/out.jsonl"
rc3=$?
mm_stop

# req.2 is the round-2 request carrying the tool result. m3 counts the D-36
# refusal marker ("not a regular file"), d3 counts the D-31 dash message (the
# refusal must not be misrouted through the dash arm), m3t is the marker line
# for the diagnostic.
m3=$(grep -h "not a regular file" "$a3"/cap/req.2 2>/dev/null | grep -c . || true)
d3=$(grep -h "dash" "$a3"/cap/req.2 2>/dev/null | grep -c . || true)
m3t=$(grep -h "not a regular file" "$a3"/cap/req.2 2>/dev/null | sed -n 's/^\(.\{120\}\).*/\1/p' | head -n 1 || true)
# D-36's correction (the first attempt's leg 3 was hollow -- it printed `ok 3`
# on the UNFIXED binary, treating the rc=124 hang as a pass). The hang is the
# bug: rc>=124 is a FAILURE here, mirroring rules_fifo_skip.sh exactly.
if [ "$rc3" -ge 124 ]; then
    t_fail "3 FIFO path: command timed out (the tool call hung in grep's open, rc=$rc3)"
elif [ "$rc3" -eq 0 ] && [ "${m3:-0}" -ge 1 ] && [ "${d3:-0}" -eq 0 ]; then
    t_ok "3 FIFO path refused promptly (marker in round-2 request, not the dash message)"
else
    t_fail "3 FIFO path: rc=$rc3, refusal_marker=${m3:-0}, dash_marker=${d3:-0} (run ended but the refusal is not in req.2; marker_line=$m3t)"
fi

# --- 4 (D-36, M1110): anti-vacuous control -- a regular file still matches ----
ws4="$tmp/ws4"; freshws "$ws4"
printf 'pipe-control-match\n' > "$ws4/f.txt"
a4="$tmp/a4"; rm -rf "$a4"; mkdir -p "$a4/cap"
cat > "$a4/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"pipe-control-match","path":"."}
rule
  count 2
  text TASKDONE
EOF
mm_start_unbounded "$a4/r.mm" "$a4/cap"
mkcfg "$a4/cfg.json" ""
runauto30 "$ws4" "$a4/cfg.json" "$a4/out.jsonl"
rc4=$?
mm_stop

found4=$(grep -h "f.txt" "$a4"/cap/req.2 2>/dev/null | grep -c . || true)
if [ "$rc4" -eq 0 ] && [ "${found4:-0}" -ge 1 ]; then
    t_ok "4 regular file still matches (the guard does not over-block)"
else
    t_fail "4 regular file: rc=$rc4, found=${found4:-0} (the D-36 guard over-blocks a regular file)"
fi

# --- 5 (M1110-b, D-36): a SYMLINK to a regular file is still searched --------
# M1110-d: the found-key must read grep's RESULT, not the call's own pattern
# argument. req.2 is the round-2 request, which carries the assistant's own
# tool call (pattern + path) echoed in its arguments, so grepping req.2 for a
# marker that IS the pattern argument passes even when grep found nothing.
# The old leg keyed found5 on SYMCTRL-1110B == the pattern -- vacuous (the
# supervisor measured: ok 5 with the marker removed from real.txt). Decouple
# the link NAME from its CONTENT: the link is sym1110d.txt (name carries no
# marker), the content is a line `SYM1110DPAT SYM1110DRES`, the pattern is
# SYM1110DPAT, and found5 greps req.2 for SYM1110DRES -- a result-only marker
# present only in grep's output line, never in the call's arguments.
ws5="$tmp/ws5"; freshws "$ws5"
printf 'SYM1110DPAT SYM1110DRES\n' > "$ws5/real.txt"
ln -s real.txt "$ws5/sym1110d.txt"
a5="$tmp/a5"; rm -rf "$a5"; mkdir -p "$a5/cap"
cat > "$a5/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool search_code {"pattern":"SYM1110DPAT","path":"sym1110d.txt"}
rule
  count 2
  text TASKDONE
EOF
mm_start_unbounded "$a5/r.mm" "$a5/cap"
mkcfg "$a5/cfg.json" ""
runauto30 "$ws5" "$a5/cfg.json" "$a5/out.jsonl"
rc5=$?
mm_stop

# found5 reads the RESULT-only marker (SYM1110DRES): it appears only in
# grep's output line (sym1110d.txt:1:SYM1110DPAT SYM1110DRES), never in the
# call's pattern/path arguments, so it proves the target was searched THROUGH
# the link. The content marker must differ from the search pattern -- that is
# what makes found5 read the result, not the argument (M1110-d).
# "not a regular file" would be the lstat-guard over-refusal (the M1110-b
# regression).
found5=$(grep -h "SYM1110DRES" "$a5"/cap/req.2 2>/dev/null | grep -c . || true)
refuse5=$(grep -h "not a regular file" "$a5"/cap/req.2 2>/dev/null | grep -c . || true)
if [ "$rc5" -eq 0 ] && [ "${found5:-0}" -ge 1 ] && [ "${refuse5:-0}" -eq 0 ]; then
    t_ok "5 symlink-to-regular-file path still searched (stat follows, M992/M1108 semantics)"
else
    t_fail "5 symlink path: rc=$rc5, found=${found5:-0}, refused=${refuse5:-0} (the guard over-refuses a link to a regular file)"
fi

rm -rf "$tmp"

t_done
