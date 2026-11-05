#!/bin/sh
# F-J4 (measured, M925, 2026-10-10 -- the zigodot drive's 2026-10-08 finding,
# recorded as "still inferred" until now): is a forked spawn_parallel
# worker's FULL saved answer reachable by the lead?
#
# THE MECHANISM (the branch the measurement reads). run_pool() FORKS each
# worker (jc_tool_parallel.c: pid = fork()), and every subagent hand-back
# crosses M911's single cap (def-318): jc_subagent_answer_into hands the
# parent the opening 3796 bytes (JC_SUBAGENT_ANSWER_CAP - 300) plus a note
# naming a spill file holding the rest. The spill is written by the CHILD, so
# it lands in the child's pid directory:
# ~/.jichi.d/tool-output/<child-pid>/subagent-1.txt. M339's fence exemption
# keys on the READER's own pid (jc_toolout_dir = $HOME/.jichi.d/tool-output/
# $(getpid())). So the lead's read_file on the worker's spill, with the path
# fence ON (the production posture), must be refused: the worker's answer is
# out of the lead's reach, cut at the cap.
#
# THE MEASUREMENT (fences ON, no caps -- a reproduction is a measurement).
# A mock worker answers with a 5600-byte text (over the cap). A pathFence-on
# lead (1) receives the spawn result and (2) is told by the mock to
# read_file the exact path the M911 note named, extracted from the lead's
# own captured request. The verdict is the tool result the lead receives.
# Python-free: the smoke tier runs where Python does not (M209).
#
# This driver OBSERVES the verdict and reports it; it does not prescribe a
# fix. A REACHABLE spill (refusal absent) FALSIFIES the finding, and the
# honest response is a design decision, not a green -- the falsification arm
# was exercised (pathFence:false reads the full spill) before this shape.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# A 5600-byte single-line answer: "W4 " + 5574 x's + " FJ4-MARKER-TAIL-5600".
# Single line on purpose: it needs no JSON escaping inside the sse-file, and
# the x run is the discriminator (see check 1).
XN=5574
X=$(awk -v n="$XN" 'BEGIN { s = ""; for (i = 0; i < n; i++) s = s "x"; print s }')
ANS="W4 $X FJ4-MARKER-TAIL-5600"
ANS_LEN=$(printf '%s' "$ANS" | wc -c)
if [ "$ANS_LEN" -le 4096 ]; then
    echo "not ok - answer is $ANS_LEN bytes; it must exceed the 4096 cap" >&2
    t_done
    exit 1
fi

# The worker's reply: the whole answer in ONE sse-file (body size is
# unconstrained; only the script's lines are capped at 1024). The answer is
# a 5598-byte single line: "W4 " + 5574 x's + " " + the 20-byte marker.
{
    printf 'data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"%s"},"finish_reason":"stop"}]}\n\n' "$ANS"
    printf 'data: [DONE]\n\n'
} > "$tmp/w.sse"

# The mock script. Rule order is priority (mockmodel matches first-in-order,
# and every match line within a rule must hold): the worker's request
# carries "W4" and no tool result; the lead's first request carries "LEAD"
# and no tool result; the lead's second carries both.
cat > "$tmp/fj4.mm" <<'EOF'
wire openai
rule
  match "W4"
  sse-file @TMP@/w.sse
rule
  match "LEAD"
  match "\"role\":\"tool\""
  text LEAD-DONE
rule
  match "LEAD"
  tool spawn_parallel {"tasks":[{"task":"W4 answer the whole list"}]}
EOF

WS=$(smoke_tmp)
( cd "$WS" && git init -q 2>/dev/null && git config user.email f@f && git config user.name f && git commit -q --allow-empty -m init )
CAP=$(smoke_tmp)
( cd "$WS" || exit 1
  sed -e "s#@TMP@#$tmp#g" "$tmp/fj4.mm" > "$CAP/r.mm"
  mm_start_unbounded "$CAP/r.mm" "$CAP"
  cat > cfg.json <<EOF
{"toolProfile":"full","lowResource":false,"pathFence":true,
 "models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF
  with_deadline 90 "$BIN" --config cfg.json -q --no-session --auto \
      "LEAD do it" </dev/null > "$tmp/lead.out" 2>&1
  mm_stop )
[ -s "$tmp/lead.out" ] || { echo "not ok - the lead run produced no output" >&2; t_done; exit 1; }

REQS=$(ls "$CAP"/req.* 2>/dev/null | wc -l)
if [ "$REQS" -eq 0 ]; then
    t_fail "the mock received no requests; the run errored before the pool ($(head_bytes 200 "$tmp/lead.out"))"
fi

# 1. the lead's captured context holds the answer's OPENING and the M911
#    note, and NOT the tail. The opening's discriminator is the x run, not
#    the "W4" prefix: the spawn call's task argument ("W4 answer the whole
#    list") carries the same prefix, and a grep on it would pass for the
#    wrong reason.
HEAD=0; NOTE=0; TAIL=0
for r in "$CAP"/req.*; do
    [ -f "$r" ] || continue
    grep -q 'W4 xxxxxx' "$r" && HEAD=1
    grep -q 'carries the opening only' "$r" && NOTE=1
    grep -q 'FJ4-MARKER-TAIL-5600' "$r" && TAIL=1
done
[ "$HEAD" = 1 ] && [ "$NOTE" = 1 ] && [ "$TAIL" = 0 ] \
    && t_ok "M911 hand-back: the lead got the opening + the note; the tail is only in the spill" \
    || t_fail "lead context: head=$HEAD note=$NOTE tail_in_lead=$TAIL reqs=$REQS"

# 2. the note names a readable spill, in the child's pid dir. The home is
#    fresh, so the ONLY pid dir that may exist is the worker's: the lead's
#    own answers are small and never spill. Exactly one dir under the root
#    makes child-pid-ness provable rather than assumed.
SPILL=$(for r in "$CAP"/req.*; do grep -o '[^ "]*/tool-output/[0-9]*/[^ "\\]*' "$r" 2>/dev/null; done | sort -u | head -1)
NPIDS=$(ls "$HOME/.jichi.d/tool-output" 2>/dev/null | wc -l)
[ -n "$SPILL" ] && [ -f "$SPILL" ] && [ "$NPIDS" = 1 ] \
    && t_ok "the M911 note names a readable spill in the child's pid dir (the only one, since the lead never spills): $SPILL" \
    || t_fail "the M911 note names no readable spill file: '${SPILL:-none}' (pid dirs: $NPIDS)"

# 3. the controlled read: a pathFence-on lead reads the worker's spill. The
#    mock names the exact path; the verdict is the tool result the lead
#    receives. The result reaches the lead's next request as a "tool"
#    message, so the CAPTURED BODY is the evidence -- not the TTY: the
#    run-summary footer prints "(N refused by a fence)" even when N=0, and a
#    TTY grep misfired on it during this measurement's own debugging.
if [ -n "$SPILL" ] && [ -f "$SPILL" ]; then
    cat > "$tmp/read.mm" <<EOF
wire openai
rule
  match "RLEAD"
  match "\\"role\\":\\"tool\\""
  text READ-DONE
rule
  match "RLEAD"
  tool read_file {"path":"$SPILL"}
EOF
    RCAP=$(smoke_tmp)
    ( cd "$WS" || exit 1
      sed -e "s#@TMP@#$tmp#g" "$tmp/read.mm" > "$RCAP/rr.mm"
      mm_start_unbounded "$RCAP/rr.mm" "$RCAP"
      cat > rcfg.json <<EOF
{"toolProfile":"full","lowResource":false,"pathFence":true,
 "models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
      "$BIN" --config rcfg.json --no-session --auto \
          "RLEAD read the file" </dev/null > "$tmp/read.out" 2>&1
      mm_stop )
    # The refusal text is model-facing (jc_tool_read.c: "error: refused by
    # safety fence (path outside workspace and any referenceRoots)"); the
    # reachable text is the answer's own tail marker, which can appear in a
    # captured body only if the read returned the file's content.
    REFUSED=0; REACHABLE=0
    for r in "$RCAP"/req.*; do
        [ -f "$r" ] || continue
        grep -q 'refused by safety fence' "$r" && REFUSED=1
        grep -q 'FJ4-MARKER-TAIL-5600' "$r" && REACHABLE=1
    done
    if [ "$REFUSED" = 1 ] && [ "$REACHABLE" = 0 ]; then
        SPILLPID=$(printf '%s' "$SPILL" | sed 's/.*tool-output\///;s/\/.*//')
        t_ok "F-J4 CONFIRMED: the pathFence-on lead's read of the worker's spill (child pid dir $SPILLPID) is refused -- the full answer is out of reach, cut at the M911 cap."
    elif [ "$REACHABLE" = 1 ]; then
        t_fail "F-J4 FALSIFIED: the fence-on lead READ the worker's full spill (tail marker in the captured tool result) -- the answer is in reach; the finding is wrong, the design must be revisited."
    else
        t_fail "cannot determine the verdict; refused=$REFUSED reachable=$REACHABLE; read.out: $(head_bytes 160 "$tmp/read.out")"
    fi
fi

t_done
