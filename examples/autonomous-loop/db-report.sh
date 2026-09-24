#!/bin/sh
# db-report.sh -- the "access a database" reporting channel for a jichi loop.
#
# Invoked by jichi as the user-defined tool `db_report`. The DSN / connection is
# FIXED by the operator (env below), never taken from a model argument. The
# model supplies only two typed fields -- a short status and a numeric count.
#
# WHAT THIS COMMENT USED TO PROMISE, AND WHY IT WAS FALSE (M740). It said both
# values were "bound as parameters ... so a model value cannot alter the
# statement". For sqlite that was not true: the sqlite3 shell's `.param set`
# EVALUATES its value as an SQL expression when it can, and stores the text only
# if that fails. Measured 2026-09-24, sqlite3 3.46.1: a status of `(SELECT 6*7)`
# was stored as 42; `(SELECT writefile(char(112), char(104,105)))` created a file
# named `p` containing "hi"; `readfile('/etc/hostname')` read the host's name
# into the table. A model-chosen status could write files. So now, two walls,
# each enough alone: STATUS must match a short whitelist (a status label needs
# no parentheses or quotes), and sqlite3 runs with -safe, which refuses
# writefile(), readfile(), .shell and ATTACH whatever the SQL says.
#
# Two backends are shown; pick one at deploy time.
set -eu

STATUS="${JICHI_ARG_STATUS:-unknown}"
COUNT="${JICHI_ARG_COUNT:-0}"

# Reject anything that isn't a plain non-negative integer for COUNT.
case "$COUNT" in
  ''|*[!0-9]*) echo "db-report: COUNT must be a non-negative integer" >&2; exit 2 ;;
esac

# A status is a short label: letters, digits, space and . _ : + - only, 1-64 bytes.
# Anything else -- a parenthesis, a quote, a semicolon -- is refused, not cleaned.
case "$STATUS" in
  ''|*[!A-Za-z0-9\ ._:+-]*)
    echo "db-report: STATUS must be a short label (letters, digits, space, . _ : + -)" >&2; exit 2 ;;
esac
if [ "${#STATUS}" -gt 64 ]; then
  echo "db-report: STATUS is longer than 64 characters" >&2; exit 2
fi

BACKEND="${JICHI_DB_BACKEND:-sqlite}"

case "$BACKEND" in
  sqlite)
    DB="${JICHI_DB_PATH:-$HOME/.jichi.d/reports/loop.db}"
    mkdir -p "$(dirname "$DB")"
    # -batch: no interactive prompts. -safe: no file or shell access from SQL,
    # the second wall behind the whitelist above.
    sqlite3 -safe -batch "$DB" \
      "CREATE TABLE IF NOT EXISTS report(ts TEXT, status TEXT, count INTEGER);" \
      ".param set :s '$STATUS'" \
      ".param set :c $COUNT" \
      "INSERT INTO report VALUES(datetime('now'), :s, :c);"
    echo "inserted into sqlite $DB"
    ;;
  postgres)
    # DSN is operator-fixed; STATUS/COUNT bound as $1/$2 (never string-concatenated).
    : "${JICHI_PG_DSN:?set JICHI_PG_DSN to the connection string}"
    PGPASSWORD="${JICHI_PG_PASSWORD:-}" psql "$JICHI_PG_DSN" \
      -v ON_ERROR_STOP=1 --no-psqlrc \
      -c "CREATE TABLE IF NOT EXISTS report(ts timestamptz default now(), status text, count int);" \
      -c "INSERT INTO report(status,count) VALUES(\$1,\$2);" \
      -- "$STATUS" "$COUNT"
    echo "inserted into postgres"
    ;;
  *)
    echo "db-report: unknown JICHI_DB_BACKEND=$BACKEND" >&2; exit 2 ;;
esac
