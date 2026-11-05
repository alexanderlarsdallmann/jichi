#!/bin/sh
# M1118 (ch4, finding C-04): the spill a too-large tool output is preserved to
# must be written through the same secret scrub as the session and the request
# dump, and its store directory must be owner-only where jichi created it.
# Before the fix the spill held a registered secret verbatim in a 0755
# directory: on a shared host another local user could read what a command
# printed. (R06 #15 recorded the 0755 directory; the missing redaction is C-04's
# half. The red-on-head checks are 2 and 3.)
#
# HOW IT READS. jichi's normal exit removes the run's spill store, so the
# instrument SIGKILLs the run mid-turn (the crash case the finding names: the
# file "survives a crash until the age sweep") and inspects what was left on
# disk under the driver's smoke HOME. The kill must come AFTER the tool
# result is in the jsonl stream and BEFORE cleanup: a marker file the second
# tool writes is that timing point -- it appears once jichi has returned from
# the tool (result emitted), and the mock's third reply then delays so the run
# hangs, giving a window in which the run has NOT exited. The spill FILE is
# located from the tool result's "COMPLETE output is at" line (parsed out of
# the --output jsonl preview). The MODE of the directory it lives in is read
# the portable idiom of state_dir_private (ls -ld, group/other chars, cut
# -c5-10) -- stat -c is banned by posix_utils_lint; on a host without a
# working mode fence (root ignores the bits, MSYS2) this driver's mode check
# is the only one that can go host-limited.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
CANARY=CANARYSECRETzq7x0123456789
tmp=$(smoke_tmp); ws=$(smoke_tmp)
mark="$tmp/marker"
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"printf '%0200d' 0; echo $CANARY"}
rule
  count 2
  tool run_terminal_command {"command":"printf marker > $mark"}
rule
  count 3
  delay 20000
  text DONE
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
# runMaxBytes 64: the ~240-byte output of the first tool is over the display
# cap, so the remainder is preserved to the spill store (M339). apiKey is the
# canary, so it is registered for redaction like a real provider key.
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "runMaxBytes":64}
EOF
# exec replaces the subshell with jichi, so $JPID IS jichi's pid (the
# parallel_abort idiom). The kill below must reach that exact process, the
# one that owns the store: a bare `"$BIN" &` inside a subshell would hand us
# the subshell's pid, orphan jichi on the kill, and let jichi clean its
# store up before the checks ran. Bounded by the mockmodel's self-watchdog
# (120 s) plus the explicit kill and mm_stop.
( cd "$ws" && exec "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    --output jsonl -p "go" < /dev/null > "$tmp/stream.jsonl" 2>/dev/null ) &
JPID=$!
# Poll for the marker: it appears when jichi has returned from the second
# tool, so the tool results (with the spill path) are already in the stream
# and the run has not yet exited. The bound is well under the mutant-sweep's
# MUTANT_TIMEOUT (120 s): in a real run the marker lands inside the third
# reply's 20 s delay, but against a binary that never does the run must still
# END (with not-ok lines on stdout) before the sweep's clock stops it --
# a driver killed mid-poll prints no checks and the sweep reads that as
# "stays green" (M1118-b, the two docs drivers' gate red).
i=0
while [ ! -f "$mark" ] && [ "$i" -lt 90 ]; do
    sleep 1; i=$((i + 1))
done
# SIGKILL the run so the spill survives (a normal exit would remove it).
kill -9 "$JPID" 2>/dev/null
wait "$JPID" 2>/dev/null
mm_stop
# Locate the spill file and parse the path the tool result names.
spill=$(find "$HOME/.jichi.d/tool-output" -name '*.txt' -type f 2>/dev/null | head -1)
claimed=$(grep 'COMPLETE output is at' "$tmp/stream.jsonl" 2>/dev/null \
    | sed -n 's/.*COMPLETE output is at \([^"]*\) -- read.*/\1/p' | head -1)
echo "# spill found on disk: ${spill:-none}; claimed by the tool result: ${claimed:-none}"
# check 1: a spill file exists under the smoke HOME
[ -n "$spill" ] && t_ok "a spill file exists under the smoke HOME ($spill)" \
    || t_fail "no spill file under $HOME/.jichi.d/tool-output (stream: $(tr '\n' ' ' < "$tmp/stream.jsonl" | dd bs=1024 count=1 2>/dev/null))"
# check 2 (RED-ON-HEAD): the spill holds the redaction marker, not the canary
if [ -n "$spill" ]; then
    has_star=$(grep -c '\*\*\*' "$spill" 2>/dev/null)
    has_canary=$(grep -c "$CANARY" "$spill" 2>/dev/null)
else
    has_star=0; has_canary=absent
fi
echo "# spill: '***' lines=$has_star canary lines=$has_canary"
[ -n "$spill" ] && [ "$has_star" -ge 1 ] && [ "$has_canary" = 0 ] \
    && t_ok "the spill is redacted (marker present, the canary absent)" \
    || t_fail "the spill is not redacted: canary appears $has_canary line(s), marker $has_star (want: marker >= 1, canary 0)"
# check 3 (RED-ON-HEAD): the store directory is owner-only where jichi made it
if [ -n "$spill" ]; then
    sdir=$(dirname "$spill")
    dperm=$(ls -ld "$sdir" 2>/dev/null | cut -c5-10)
else
    sdir=none; dperm=none
fi
echo "# store dir: $sdir group/other='$dperm' (want '------')"
[ -n "$spill" ] && [ "$dperm" = '------' ] \
    && t_ok "the store directory is owner-only (group/other: ------)" \
    || t_fail "the store directory is group/other-readable: '$dperm' (want '------')"
# check 4: the model read back what it got -- the tool result carries the path
[ -n "$claimed" ] && [ "$claimed" = "$spill" ] \
    && t_ok "the tool result carries the spill path (the instrument has something to read)" \
    || t_fail "the tool result names '${claimed:-no path}' but the spill on disk is '${spill:-none}'"
t_done
