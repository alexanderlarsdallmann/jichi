#!/bin/sh
# http-report.sh -- the "make an HTTP request" reporting channel for a jichi loop.
#
# Invoked by jichi as the user-defined tool `http_report`. Built-in fetch_url is
# GET-only and SSRF-guarded; an arbitrary POST needs a user-defined tool, and a
# user-defined tool is NOT SSRF-guarded -- so the hardening lives HERE:
#
#   * The endpoint URL is FIXED by the operator ($JICHI_REPORT_URL), never taken
#     from a model argument -- the model cannot redirect the request to an
#     internal address (169.254.169.254, a metadata service, localhost, ...).
#   * The bearer token is read from the environment, not logged.
#   * The model supplies only the JSON payload body (a typed 'summary' field).
#   * --max-time bounds the call; --fail turns an HTTP error into exit != 0.
#
# jichi scrubs known provider API keys from this child's environment
# (jc_proc_scrub_secret_env), so OPENAI_API_KEY etc. are NOT visible here; only
# the credentials you set for THIS endpoint are.
set -eu

: "${JICHI_REPORT_URL:?set JICHI_REPORT_URL to the fixed reporting endpoint}"
TOKEN="${JICHI_REPORT_TOKEN:-}"
SUMMARY="${JICHI_ARG_SUMMARY:-}"

# Build the JSON body with jq if present (safe escaping); else a minimal
# hand-escape of double-quotes and backslashes. Payload is data, not URL.
if command -v jq >/dev/null 2>&1; then
  BODY="$(printf '%s' "$SUMMARY" | jq -Rs '{summary: .}')"
else
  esc=$(printf '%s' "$SUMMARY" | sed 's/\\/\\\\/g; s/"/\\"/g')
  BODY="{\"summary\":\"$esc\"}"
fi

set -- --fail --show-error --silent --max-time 20 \
       -H 'Content-Type: application/json' \
       -X POST --data "$BODY"
if [ -n "$TOKEN" ]; then
  set -- "$@" -H "Authorization: Bearer $TOKEN"
fi

curl "$@" "$JICHI_REPORT_URL"
echo "posted to $JICHI_REPORT_URL"
