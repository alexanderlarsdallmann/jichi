#!/bin/sh
# probe-models.sh -- ask an OpenAI-compatible server what it can actually serve.
#
#   sh scripts/probe-models.sh [BASE_URL] [MODEL_ID ...]
#
# BASE_URL defaults to http://127.0.0.1:1234/v1 (LM Studio's default). With no
# model ids it probes every id the server advertises. Set JICHI_API_KEY if the
# endpoint needs one (never pass a key on the command line; it lands in your
# shell history and in `ps`).
#
# WHAT IT ANSWERS, and why the question needs asking. `jichi doctor --live`
# classifies tool calling as native / text / none in one request, which is the
# check that decides whether a model can do agent work at all. But when it says
# **probe failed** it has told you it could not get an answer -- not what the
# answer is -- and on 2026-08-21 that ambiguity cost real time: four of eight
# models a local server advertised turned out to FAIL TO LOAD, while the two that
# loaded emitted perfectly correct tool calls as PROSE the server never converted
# into the `tool_calls` field. Those are two different faults with two different
# repairs, and one probe verdict covered both
# (docs/analysis/2026-08-21-self-hosting-first-review.md).
#
# So this walks the list and separates them:
#
#   LOADS    the server answered a chat request at all
#   TOOLS    native  -- tool_calls[] came back populated: usable by jichi
#            prose   -- the call arrived as text; the model is right and the
#                       server is not translating. jichi cannot execute it
#                       (it will notice and nudge -- M147 -- but not run it)
#            none    -- no tool call attempted
#   SECS     wall clock, which on a JIT-loading server is mostly load time
#
# COSTS AND CAUTIONS, because this is not free on a small machine:
#   - Probing every id LOADS every id, one after another, and on a memory-bound
#     box each load evicts the last. Name specific models to avoid that.
#   - One completion per model, max_tokens 600. That number is measured, not
#     chosen: at 32 a prose call was truncated before its `arguments` key and
#     the classifier said `none`; at 96 a REASONING model spent the whole budget
#     in `reasoning_content` and never reached its tool call, so a natively
#     capable model was reported as `prose`. A request budget is a measurement
#     instrument, and too small a one fabricates results. On a priced endpoint that is
#     real money, so this project's rule applies: local or free models only
#     unless the operator granted otherwise for a named run.
#   - Read-only: it never writes to the server or the tree.
#
# Dependencies: curl and a POSIX shell. Deliberately not jq or python3 -- this is
# a diagnostic you want to run on the machine that is broken, and the
# classification is coarse enough for grep to be honest about it.
set -e

BASE=${1:-http://127.0.0.1:1234/v1}
[ $# -gt 0 ] && shift
TIMEOUT=${PROBE_TIMEOUT:-180}

command -v curl >/dev/null 2>&1 || { echo "probe-models: needs curl" >&2; exit 2; }

auth=""
if [ -n "${JICHI_API_KEY:-}" ]; then
    auth="Authorization: Bearer $JICHI_API_KEY"
fi

# One curl invocation shape, so the key handling has a single home.
post() {   # post PATH BODY_FILE
    if [ -n "$auth" ]; then
        curl -s --max-time "$TIMEOUT" -H "Content-Type: application/json" \
             -H "$auth" -X POST --data-binary @"$2" "$BASE$1"
    else
        curl -s --max-time "$TIMEOUT" -H "Content-Type: application/json" \
             -X POST --data-binary @"$2" "$BASE$1"
    fi
}
get() {    # get PATH
    if [ -n "$auth" ]; then
        curl -s --max-time 30 -H "$auth" "$BASE$1"
    else
        curl -s --max-time 30 "$BASE$1"
    fi
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/probe-models.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

# --- the model list ----------------------------------------------------------
if [ $# -gt 0 ]; then
    for m in "$@"; do printf '%s\n' "$m"; done > "$tmp/ids"
else
    get /models > "$tmp/models.json" || true
    # One id per line. The list is what the server ADVERTISES, which is not the
    # same as what it can serve -- the whole point of what follows.
    sed 's/}/}\n/g' "$tmp/models.json" \
      | sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
      | sort -u > "$tmp/ids"
fi
n=$(grep -c . "$tmp/ids" 2>/dev/null || true)
[ -n "$n" ] || n=0
if [ "$n" -eq 0 ]; then
    echo "probe-models: $BASE advertised no models (is the server up?)" >&2
    exit 1
fi
echo "probe-models: $BASE -- $n model(s), ${TIMEOUT}s budget each"
echo
printf '%-40s %-6s %-7s %5s  %s\n' MODEL LOADS TOOLS SECS NOTE
printf '%-40s %-6s %-7s %5s  %s\n' "----------------------------------------" "-----" "------" "----" "----"

while IFS= read -r id; do
    [ -n "$id" ] || continue
    # A one-tool request whose obvious answer is a tool call. If the server
    # translates tool calls at all, this is where it shows.
    cat > "$tmp/body.json" <<BODY
{"model":"$id",
 "messages":[{"role":"user","content":"Read the file notes.txt. Use the read_file tool."}],
 "tools":[{"type":"function","function":{"name":"read_file",
   "description":"Read a file at a path",
   "parameters":{"type":"object","properties":{"path":{"type":"string"}},
   "required":["path"]}}}],
 "tool_choice":"auto","max_tokens":600,"stream":false}
BODY
    t0=$(date +%s)
    rc=0
    post /chat/completions "$tmp/body.json" > "$tmp/out.json" 2>/dev/null || rc=$?
    t1=$(date +%s)
    secs=$((t1 - t0))

    loads=no; tools='-'; note=''
    if [ ! -s "$tmp/out.json" ]; then
        # curl 28 is its own timeout, 7 is connection refused. Saying "timeout or
        # refused" would repeat the exact ambiguity this script exists to remove:
        # a model that is merely SLOW TO LOAD is not a broken model. Measured
        # 2026-08-21: gemma-4-12b-qat timed out at 120s and loaded fine at 300s.
        case "$rc" in
            28) note="no reply within ${TIMEOUT}s -- may just be a slow load; retry with PROBE_TIMEOUT=600" ;;
            7)  note='connection refused (is the server up on this port?)' ;;
            *)  note="curl exited $rc" ;;
        esac
    elif grep -q '"error"' "$tmp/out.json"; then
        # The message is the server's own, and is the thing worth reading: a load
        # failure and an unknown model say different things here.
        note=$(sed -n 's/.*"message"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
               "$tmp/out.json" | head -1 | cut -c1-58)
        [ -n "$note" ] || note='error reply (no message field)'
    else
        loads=yes
        # A prose call arrives INSIDE a JSON string, so its quotes are escaped:
        # the bytes are \"name\", not "name". Grepping the raw reply for '"name"'
        # therefore matched nothing and reported `none` for a model that was
        # emitting a correct call every time -- a classifier fooled by one layer
        # of encoding. Strip backslashes once and classify off that.
        # Flatten NEWLINES FIRST. LM Studio pretty-prints its JSON, so the
        # native shape arrives as `"tool_calls": [` / newline / `{`, and grep is
        # line-based: [[:space:]] never matches across a line, so the native
        # branch below could not fire at all against that server -- and the
        # prose branch then matched the "name"/"arguments" keys INSIDE the real
        # tool_calls array and reported the exact opposite of the truth. The
        # HRZ gateway returns compact one-line JSON, so the same code answered
        # correctly there: one instrument, two servers, one silent inversion.
        tr '\n' ' ' < "$tmp/out.json" > "$tmp/one.json"
        tr -d '\\' < "$tmp/one.json" > "$tmp/flat.json"
        if grep -q '"tool_calls"[[:space:]]*:[[:space:]]*\[[[:space:]]*{' "$tmp/one.json"; then
            tools=native
        elif grep -q '"name"' "$tmp/flat.json" && grep -q '"arguments"' "$tmp/flat.json"; then
            tools=prose
            note='call is in content, not tool_calls -- jichi cannot execute it'
        else
            tools=none
            note='no tool call attempted'
        fi
    fi
    printf '%-40s %-6s %-7s %5s  %s\n' "$(printf '%s' "$id" | cut -c1-40)" \
           "$loads" "$tools" "$secs" "$note"
done < "$tmp/ids"

echo
echo "native = usable by jichi.  prose = repair the server, not the model."
echo "Cross-check one model with the tool jichi itself uses:"
echo "  ./jichi --config <your-config> doctor --live"
