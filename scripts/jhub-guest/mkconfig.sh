#!/bin/sh
# runs as root: a per-user jichi config pointing at that user's tunnelled mock.
# $1 = stud1's guest-side mock port   $2 = stud2's
#
# TWO mocks, on purpose. mockmodel serves requests SEQUENTIALLY (its own header
# says so: "jichi issues one model call at a time and every non-stall reply is
# Connection: close, so a single-threaded accept loop is exactly faithful").
# stud1's run stalls on purpose to hold its lease -- which also holds the mock's
# only accept slot, so stud2 never got served and its run looked inconclusive.
# That was the instrument, not jichi.
set -eu
p1="$1"; p2="$2"
i=1
for u in stud1 stud2; do
    if [ "$i" = 1 ]; then port="$p1"; else port="$p2"; fi
    cat > "/home/$u/config.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$port/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
"timeouts":{"stall":180,"request":300}}
CFG
    chown "$u:$u" "/home/$u/config.json"
    i=2
done
echo "configs written (stud1 -> $p1, stud2 -> $p2)"
