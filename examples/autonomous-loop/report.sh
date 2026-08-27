#!/bin/sh
# report.sh -- the "write a file" reporting channel for an autonomous jichi loop.
#
# Invoked by jichi as the user-defined tool `report_status` (see
# config.autonomous.json). jichi passes the tool arguments two ways:
#   * the whole arguments object as JSON on stdin, and
#   * each scalar argument as $JICHI_ARG_<NAME> (e.g. JICHI_ARG_MESSAGE).
# Arguments never reach this script on the command line, so a model-supplied
# value can't inject shell words or extra flags (see docs/AUTONOMOUS_LOOPS.md
# and src/tools/jc_tool_user.c: user_tool_run).
#
# The destination is fixed by the OPERATOR here, not by the model: the model
# only supplies the message text. That is the key hardening property -- the
# model chooses WHAT to report, never WHERE.
set -eu

# Fixed destination (override at deploy time; never take it from a tool arg).
DEST="${JICHI_REPORT_FILE:-$HOME/.jichi.d/reports/status.log}"
mkdir -p "$(dirname "$DEST")"

# Message comes only from the typed argument; default keeps the tool total.
MSG="${JICHI_ARG_MESSAGE:-}"

# Timestamp without spawning date-format surprises; ISO-8601 UTC.
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

printf '%s\t%s\n' "$TS" "$MSG" >>"$DEST"
echo "reported to $DEST"
