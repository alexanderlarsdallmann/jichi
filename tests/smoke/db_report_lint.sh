#!/bin/sh
# smoke lint: the autonomous-loop example's database tool treats a model's value as data (M740).
#
# THE DEFECT. examples/autonomous-loop/db-report.sh is this project's example of "a
# database as a user-defined tool", and its comment -- and docs/AUTONOMOUS_LOOPS.md --
# said the model's STATUS was bound as a parameter, so "a hostile value is inert data".
# For the sqlite backend it was not: the sqlite3 shell's `.param set` EVALUATES its value
# as an SQL expression when it can. Measured 2026-09-24, sqlite3 3.46.1: `(SELECT 6*7)`
# was stored as 42, and `(SELECT writefile(char(112), char(104,105)))` created a file.
# The tool is mutating, so under --auto a model could write files through a field meant
# for a short label. Found while writing docs/SQLITE.md, whose subject is exactly this.
#
# THE CHECKS run the script itself, as jichi would, against a database in this driver's
# temp dir: a plain label is stored as text (the instrument -- the tool works at all); an
# expression is refused and nothing is stored; a writefile() payload is refused and writes
# nothing; and the second wall ON ITS OWN -- the script's own sqlite3 flags, with the
# whitelist bypassed -- still refuses writefile() inside `.param set`.
# Needs sqlite3; runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

command -v sqlite3 > /dev/null 2>&1 ||
    t_skip "no sqlite3 on this host -- the example's sqlite backend cannot be exercised"
t_plan 4
tmp=$(smoke_tmp)
SCRIPT="$SMOKE_ROOT/examples/autonomous-loop/db-report.sh"
DB="$tmp/loop.db"

run_tool() {  # STATUS -> the tool's exit code; runs from $tmp so a relative writefile lands there
    (cd "$tmp" && JICHI_DB_PATH="$DB" JICHI_DB_BACKEND=sqlite \
        JICHI_ARG_STATUS="$1" JICHI_ARG_COUNT=1 sh "$SCRIPT") > "$tmp/out" 2>&1
}
rows() { sqlite3 "$DB" "SELECT count(*) FROM report;" 2> /dev/null || echo 0; }

# --- 1: the instrument -- a plain label is stored, as text --------------------
run_tool "tests failed: 3"; _rc=$?
_got=$(sqlite3 "$DB" "SELECT typeof(status) || '|' || status FROM report;" 2> /dev/null)
if [ "$_rc" -eq 0 ] && [ "$_got" = "text|tests failed: 3" ]; then
    t_ok "a plain status label is stored as text (so the refusals below mean something)"
else
    t_fail "the tool did not store a plain label (exit $_rc, row '$_got'):
$(cat "$tmp/out")"
fi

# --- 2: an SQL expression is refused, and nothing is stored -------------------
_before=$(rows)
run_tool "(SELECT 6*7)"; _rc=$?
_after=$(rows)
if [ "$_rc" -ne 0 ] && [ "$_before" = "$_after" ]; then
    t_ok "an SQL expression as the status is refused and stores nothing"
else
    t_fail "an SQL expression as the status was accepted (exit $_rc, rows $_before -> $_after).
Before M740 \`(SELECT 6*7)\` was stored as 42: the sqlite3 shell's .param set
evaluates its value as SQL. Validate the label before it reaches sqlite3."
fi

# --- 3: a writefile() payload is refused and writes no file --------------------
rm -f "$tmp/p"
run_tool "(SELECT writefile(char(112), char(104,105)))"; _rc=$?
if [ "$_rc" -ne 0 ] && [ ! -e "$tmp/p" ]; then
    t_ok "a writefile() payload as the status is refused and writes no file"
else
    t_fail "a writefile() payload as the status reached the file system (exit $_rc,
file p $( [ -e "$tmp/p" ] && echo EXISTS || echo absent)). A field meant for a label
must not be able to write files."
fi

# --- 4: the second wall alone -- the script's sqlite3 flags refuse writefile() -------
# Read the flags from the script rather than restating them, so this checks what
# ships: with the whitelist out of the way, is -safe still there, and does it hold?
_flags=$(sed -n 's/^ *sqlite3 \(-[^"$]*\)"\$DB".*/\1/p' "$SCRIPT" | head -n 1)
rm -f "$tmp/q"
# shellcheck disable=SC2086 -- the flags are deliberately word-split
(cd "$tmp" && sqlite3 $_flags "$DB" \
    ".param set :s '(SELECT writefile(char(113), char(104,105)))'") > /dev/null 2>&1
case " $_flags " in
    *" -safe "*) _safe=1 ;;
    *) _safe=0 ;;
esac
if [ "$_safe" -eq 1 ] && [ ! -e "$tmp/q" ]; then
    t_ok "the script's own sqlite3 flags ($_flags) refuse writefile() even past the whitelist"
else
    t_fail "the second wall is missing: the script runs sqlite3 with '${_flags:-no flags found}',
and writefile() $( [ -e "$tmp/q" ] && echo 'WROTE a file' || echo 'wrote nothing') inside .param set.
Keep -safe on the sqlite3 call: the whitelist is one wall, and one wall is a single mistake away."
fi

t_done
