#!/bin/sh
# db-report.sh -- the "access a database" reporting channel for a jichi loop.
#
# Invoked by jichi as the user-defined tool `db_report`. The DSN / connection is
# FIXED by the operator (env below), never taken from a model argument. The
# model supplies only two typed fields -- a short status and a numeric count --
# and they are bound as *parameters*, never spliced into SQL text, so a model
# value cannot alter the statement (no SQL injection).
#
# Two backends are shown; pick one at deploy time.
set -eu

STATUS="${JICHI_ARG_STATUS:-unknown}"
COUNT="${JICHI_ARG_COUNT:-0}"

# Reject anything that isn't a plain non-negative integer for COUNT. Belt and
# braces on top of the parameter binding below.
case "$COUNT" in
  ''|*[!0-9]*) echo "db-report: COUNT must be a non-negative integer" >&2; exit 2 ;;
esac

BACKEND="${JICHI_DB_BACKEND:-sqlite}"

case "$BACKEND" in
  sqlite)
    DB="${JICHI_DB_PATH:-$HOME/.jichi.d/reports/loop.db}"
    mkdir -p "$(dirname "$DB")"
    # -batch: no interactive prompts. Bind via .param so STATUS is data.
    sqlite3 -batch "$DB" \
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
