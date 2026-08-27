#!/bin/sh
# capture.sh -- replay one recorded jichi run and write down what it did.
#
#   usage:  sh docs/reading/traces/capture.sh TRACE [OUTDIR]
#
# TRACE   a directory beside this script (tool-round, plain-turn, ...), or a
#         path to one anywhere on disk.
# OUTDIR  where the artifacts go. Default: a fresh temp dir, whose path is
#         printed on the last line so you can `ls` it.
#
# WHY THIS EXISTS. The chapters in this series quote a run: its event stream,
# the bytes it put on the wire, the file it left on disk. Prose about a run
# rots faster than prose about code, because it commits to values as well as
# to control flow -- so the run is not described here, it is REPLAYED. Where
# a model is involved it is tests/tools/mockmodel with a scripted reply table,
# so the whole trace is offline, keyless, deterministic, and yours to re-take:
#
#   make && make smoke-tools                       # in the jichi checkout
#   sh docs/reading/traces/capture.sh tool-round   # in the jichi checkout
#
# The artifacts committed under TRACE/expected/ came out of this script, and
# tests/smoke/reading_trace.sh re-takes every trace on `make smoke` and diffs.
# A chapter that drifts from the code therefore fails the build, which is the
# house rule (prefer a lint to an audit, docs/TEST_INTEGRITY.md).
#
# WHAT A TRACE DEFINES (TRACE/trace.sh, sourced -- see the shipped ones):
#
#   PROMPT         the user's sentence, when the run takes one
#   MAX_REQUESTS   round trips the mock will serve; request N+1 finds nothing
#                  listening, so an unexpected extra one cannot pass unseen
#   NEEDS_MODEL    0 for a run that never talks to a model (default 1)
#   STDOUT_NAME    artifact name for stdout (default stdout.jsonl)
#   REPO_MAP       true to inject the workspace's repository map into the
#                  system prompt (default false, as in chapters 1-2)
#   seed_workspace()  runs INSIDE the throwaway workspace, before jichi starts
#   run_trace()       the invocation itself, using $BIN and $CONFIG. Each trace
#                     spells its own out, because the command IS part of what
#                     the chapter is explaining.
#
# DELIBERATELY SELF-CONTAINED: it does not source tests/smoke/_smoke.sh, so a
# reader can follow it top to bottom without learning a test harness first.
# It does copy that tier's determinism rules -- a pinned tool profile,
# lowResource off -- because without them the artifact would depend on how
# much RAM your machine has (docs/CONSTRAINTS.md, smoke_lint checks 7-8).
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)

trace=${1:-}
if [ -z "$trace" ]; then
    echo "usage: sh $0 TRACE [OUTDIR]" >&2
    exit 2
fi
# A bare name is a trace beside this script; a path with a trace.sh in it is
# taken as given, so an exercise can copy a trace to /tmp, edit it there, and
# leave the repository untouched.
if [ -f "$trace/trace.sh" ]; then
    tdir=$(cd "$trace" && pwd)
else
    tdir="$here/$trace"
fi
if [ ! -f "$tdir/trace.sh" ]; then
    echo "capture: no such trace: $trace (want $tdir/trace.sh)" >&2
    exit 2
fi

BIN=${JC_TRACE_BIN:-$root/jichi}
MM="$root/tests/tools/mockmodel"
JQ="$root/tests/tools/jsonq"
if [ ! -x "$BIN" ]; then
    echo "capture: missing $BIN -- run 'make' first" >&2
    exit 2
fi

out=${2:-}
if [ -z "$out" ]; then
    out=$(mktemp -d "${TMPDIR:-/tmp}/jichi_trace.XXXXXX")
fi
mkdir -p "$out"

work=$(mktemp -d "${TMPDIR:-/tmp}/jichi_trace_run.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
cap="$work/cap"; ws="$work/ws"; hm="$work/home"
mkdir -p "$cap" "$ws" "$hm"
CONFIG="$work/config.json"
export BIN CONFIG

# --- the trace's own definition ---------------------------------------------
PROMPT=""; MAX_REQUESTS=""; NEEDS_MODEL=1; STDOUT_NAME="stdout.jsonl"
REPO_MAP=false
. "$tdir/trace.sh"
(cd "$ws" && seed_workspace)

# --- the model, if this trace has one ---------------------------------------
mmpid=""
port=1     # a port nothing listens on: see the NEEDS_MODEL=0 note below
if [ "$NEEDS_MODEL" -eq 1 ]; then
    for p in "$MM" "$JQ"; do
        if [ ! -x "$p" ]; then
            echo "capture: missing $p -- run 'make smoke-tools' first" >&2
            exit 2
        fi
    done
    "$MM" --script "$tdir/replies.mm" --capture "$cap" --port-file "$cap/.port" \
          --deadline 60 --max-requests "$MAX_REQUESTS" >/dev/null &
    mmpid=$!
    i=0
    while [ ! -s "$cap/.port" ]; do
        kill -0 "$mmpid" 2>/dev/null || { echo "capture: mock model died" >&2; exit 1; }
        i=$((i + 1))
        if [ "$i" -gt 10 ]; then echo "capture: mock model never announced a port" >&2; exit 1; fi
        sleep 1
    done
    port=$(cat "$cap/.port")
fi

# The config is written either way. For a NEEDS_MODEL=0 trace its endpoint is
# port 1, where nothing listens -- so if that run ever dialled the model it
# would fail loudly instead of passing quietly. The absence of an error in
# those artifacts is the evidence that no request was made.
cat > "$CONFIG" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$port/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":$REPO_MAP,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG

# --- the run -----------------------------------------------------------------
# run_trace comes from the trace; HOME is redirected so a real ~/.jichi cannot
# reach the artifact, and stdin is closed because an open stdin on a non-TTY is
# read as prompt context.
rc=0
(cd "$ws" && HOME="$hm" && export HOME && run_trace < /dev/null) \
    > "$work/stdout" 2> "$work/stderr.txt" || rc=$?
if [ -n "$mmpid" ]; then
    kill "$mmpid" 2>/dev/null || true
    wait "$mmpid" 2>/dev/null || true
fi
printf '%s\n' "$rc" > "$work/exit_status"

# --- normalization -----------------------------------------------------------
# Seven things differ between two honest runs of the same fixture. Each is
# replaced by a <NAME> placeholder, and each replacement uses the value
# OBSERVED IN THIS RUN rather than one computed here -- a run that starts
# before midnight and finishes after it would otherwise be "normalized"
# against a date it never sent.
#
#   <WORKSPACE>  the throwaway workspace (named in the system prompt)
#   <RUNDIR>     the directory holding the config and the mock's capture
#   <DATE>       today, which the system prompt states
#   <VERSION>    the User-Agent's version stamp
#   <PORT>       the mock model's port, picked by the kernel
#   <CACHE_KEY>  prompt_cache_key: one UUID per session
#   <LEN>        Content-Length: it counts the bytes of the un-normalized
#                body, so shipping the real number beside a normalized body
#                would be a number that does not add up.
#
# THE LIST IS MEASURED, NOT REASONED. It was five entries until the first
# stability run -- two captures from directories of different lengths, diffed
# -- came back with one line changed and prompt_cache_key in it. Re-take the
# trace twice into two directories and diff them before adding a field here;
# a normalizer written from the source alone would have shipped that UUID into
# the committed artifact and made every later capture fail against it.
ws_real=$(cd "$ws" && pwd -P)
work_real=$(cd "$work" && pwd -P)
date_obs=$(sed -n "s/.*Today's date: \([0-9][0-9-]*\).*/\1/p" "$work/stdout" \
           "$cap/req.1" 2>/dev/null | head -1)
ver_obs=$(sed -n 's|.*jichi/\([0-9][0-9.]*\).*|\1|p' "$cap/req.1" 2>/dev/null | head -1)
esc() { printf '%s' "$1" | sed 's/[].[^$*\\/]/\\&/g'; }
sed_ws=$(esc "$ws"); sed_wsr=$(esc "$ws_real")
sed_wk=$(esc "$work"); sed_wkr=$(esc "$work_real")

norm() {
    sed -e "s/$sed_ws/<WORKSPACE>/g" \
        -e "s/$sed_wsr/<WORKSPACE>/g" \
        -e "s/$sed_wk/<RUNDIR>/g" \
        -e "s/$sed_wkr/<RUNDIR>/g" \
        -e "${date_obs:+s/Today's date: $(esc "$date_obs")/Today's date: <DATE>/g}" \
        -e "${ver_obs:+s|jichi/$(esc "$ver_obs")|jichi/<VERSION>|g}" \
        -e "s|127\.0\.0\.1:$port|127.0.0.1:<PORT>|g" \
        -e 's|"prompt_cache_key":"[0-9a-f-]*"|"prompt_cache_key":"<CACHE_KEY>"|g' \
        -e "s|^Content-Length: [0-9]*|Content-Length: <LEN>|"
}

# The request body: everything after the first empty line of the HTTP head.
http_body() { awk 'b { print; next } /^\r?$/ { b = 1 }' "$1"; }

norm < "$work/stdout"      > "$out/$STDOUT_NAME"
norm < "$work/stderr.txt"  > "$out/stderr.txt"
cp "$work/exit_status" "$out/exit_status"

nreq=0
while [ -f "$cap/req.$((nreq + 1))" ]; do
    nreq=$((nreq + 1))
    norm < "$cap/req.$nreq" > "$out/req.$nreq"
    http_body "$cap/req.$nreq" | norm > "$work/body.$nreq"
done
if [ "$NEEDS_MODEL" -eq 1 ] && [ "$nreq" -eq 0 ]; then
    # No request at all from a run that was supposed to make one: a build too
    # old for a flag used here, a port nobody listened on, a jichi that died in
    # argument parsing. Say so, rather than writing an artifact set that looks
    # complete.
    echo "capture: the run sent no request -- nothing to normalize." >&2
    echo "capture: jichi exited $rc; its stderr follows:" >&2
    head -20 "$work/stderr.txt" >&2
    exit 1
fi

# What the workspace looks like afterwards: the half of the run that is not
# on the wire at all.
( cd "$ws" && for f in *; do
    [ -f "$f" ] || continue
    printf '== %s ==\n' "$f"
    cat "$f"
  done ) > "$out/workspace.after"

# --- the shape file ----------------------------------------------------------
# A projection of the request bodies, not a second copy of them: how many
# messages went out, in which roles, and how big each one's content is. Sizes
# are measured on the NORMALIZED body, so they are the same on your machine as
# in the committed copy -- which also means the absolute numbers are a few
# bytes off what the socket saw (<WORKSPACE> is shorter than your path). The
# growth lines at the bottom are exact either way: the system message is
# byte-identical across the requests of one run, so it cancels.
#
# A trace with no model has no requests and therefore no shape file at all.
if [ "$nreq" -gt 0 ]; then
{
    printf '# generated by docs/reading/traces/capture.sh -- structure, not content\n'
    printf '# content= is the DECODED length the model reads; body= is wire bytes.\n'
    n=0
    while [ "$n" -lt "$nreq" ]; do
        n=$((n + 1))
        b="$work/body.$n"
        bytes=$(wc -c < "$b" | tr -d ' ')
        nt=0
        while "$JQ" -q ".tools[$nt].function.name" "$b" 2>/dev/null; do nt=$((nt + 1)); done
        printf 'req.%s body=%s messages=' "$n" "$bytes"
        m=0
        while "$JQ" -q ".messages[$m].role" "$b" 2>/dev/null; do m=$((m + 1)); done
        printf '%s tools=%s\n' "$m" "$nt"
        m=0
        while role=$("$JQ" ".messages[$m].role" "$b" 2>/dev/null); do
            # jsonq prints a string raw plus one newline, so -1 is the decoded
            # length. A message with no string content -- an assistant turn that
            # is nothing but a tool call -- reports '-' rather than the 4 bytes
            # of the JSON null it actually carries.
            if "$JQ" -q -t string ".messages[$m].content" "$b" 2>/dev/null; then
                clen=$("$JQ" ".messages[$m].content" "$b" | wc -c | tr -d ' ')
                clen=$((clen - 1))
            else
                clen='-'
            fi
            extra=""
            tn=$("$JQ" ".messages[$m].tool_calls[0].function.name" "$b" 2>/dev/null || printf '')
            if [ -n "$tn" ]; then
                extra=" tool_call=$tn id=$("$JQ" ".messages[$m].tool_calls[0].id" "$b")"
            fi
            tcid=$("$JQ" ".messages[$m].tool_call_id" "$b" 2>/dev/null || printf '')
            if [ -n "$tcid" ]; then extra=" for=$tcid"; fi
            printf '  [%s] %-9s content=%s%s\n' "$m" "$role" "$clen" "$extra"
            m=$((m + 1))
        done
    done
    n=1
    while [ "$n" -lt "$nreq" ]; do
        a=$(wc -c < "$work/body.$n" | tr -d ' ')
        z=$(wc -c < "$work/body.$((n + 1))" | tr -d ' ')
        printf 'growth req.%s->req.%s = +%s bytes\n' "$n" "$((n + 1))" "$((z - a))"
        n=$((n + 1))
    done
} > "$out/shape"
fi

printf '%s\n' "$out"
