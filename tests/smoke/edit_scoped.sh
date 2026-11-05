#!/bin/sh
# smoke: the D6 scoped edit check (M975, batch 0c) -- the trip, the detection,
# and the two over-eagerness guards (the supervisor's 2026-10-18 15:56 note).
#
# THE CASE (docs/internal/drive/2026-10-17-hardening/D6-DECISION.md). A lead
# agent supervises worker batches that MERGE files into its tree. The operator
# decided (2026-10-16) that, from the Nth (default 2) edit by the TOP-LEVEL
# agent of a WORKER-OWNED path in the same turn, the file tools return a tool
# ERROR (the trip), and that after each tool call the tree is checked so a
# worker-owned path changed BY ANY MEANS (the shell included) is reported in
# the next tool result plus a telemetry event (the detection). The two guards:
# a path no child touched, and a child's kept-but-UNMERGED path, are never
# refused -- without them an implementation that refuses every 2nd edit
# passes the trip check.
#
# THE INSTRUMENT. Same headless pattern as parallel_heartbeat.sh, sequenced by
# the mockmodel's `count N` (the request number, 1-based; the parent and its
# children share one counter). Every lead/worker action is FORCED by a rule, so
# the numbering is deterministic (validated in scratch: arm 1's sequence is
# 1 lead-spawn, 2 worker-shell, 3 worker-done, 4 lead-read, 5 lead-edit1,
# 6 lead-edit2, 7 lead-done, 8 self-review). A trailing LEADDONE tail absorbs
# the one-shot self-review (fires only when the snapshot diff is non-empty) and
# any extra round-trip, so an arm's assertion never depends on the exact total.
# The lead's tool results are greppable in the mock's CAPTURES
# (the request that follows carries the result as a "tool" message); the jsonl
# sink is written with --log (the events are telemetry, not stream lines).
#
#   1  default N=2, reg.txt worker-merged: the 1st edit lands, the 2nd is
#      REFUSED (a tool error naming the path + "worker-owned path"), reg.txt
#      never carries the 2nd edit, and a scoped_edit (trip) event is written.
#   2  N=0: the config key scopedEditMax=0 turns the trip off -- the 2nd edit
#      lands and no refusal / no event appears. (guard: teeth after the fix)
#   3  un-owned: the lead writes+edits its OWN file, no worker touched it --
#      BOTH edits land, NO refusal. (guard: the 2026-10-16 measurement's
#      "the lead's own LOG.md" case the literal tripwire would have refused)
#   4  detection: a shell `sed -i` over the merged path is reported in the next
#      tool result ("changed outside the file tools") and as a scoped_edit
#      (detected) event.
#   5  kept/unmerged: a worker MOVES an assertion in a test file (the M865
#      hold keeps it, not merged), the lead edits that file twice -- BOTH land,
#      NO refusal. (guard: ownership (a) is merged paths only, until def-309)
#
# Checks 2 and 5 also carry UNIT teeth in tests/test_scopededit.c (an un-owned
# path is never refused over many edits; the TRIP THRESHOLD allows the first
# N-1 edits and refuses from the Nth, at N=1/2/3). The file is auto-discovered
# via tests/test_*.c and registered in tests/test_main.c.

. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

freshws() {
    rm -rf "$1"; mkdir -p "$1"
    ( cd "$1" && git init -q . && git config user.email t@t && \
        git config user.name t && git add -A . 2>/dev/null && \
        git commit -qm init >/dev/null 2>&1 )
}
mkcfg() { # mkcfg <cfgfile> <extra-json-before-closing-brace>
    printf '{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:%s/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true%s}\n' \
        "$MM_PORT" "$2" > "$1"
}
runlead() { # runlead <ws> <cfg> <jsonl> <tel>  (tel = the --log telemetry sink;
# the scoped_edit events go to the TELEMETRY file, not the --output stream, so
# the arms grep $tel -- as parallel_test_edit_hold.sh greps its --log file)
    ( cd "$1" && with_deadline 120 "$BIN" --config "$2" -q --no-session --auto \
        --log "$4" -p "LEADPROMPT run a writer" < /dev/null > "$3" 2> /dev/null )
}

# The worker-appends-to-reg.txt child, shared by arms 1, 2 and 4 (they differ
# only in the LEAD continuation and the config). count 2 = the child's shell,
# count 3 = the child's done.
WORKER_REG='rule
  count 3
  text TASKDONE
rule
  count 2
  tool run_terminal_command {"command":"printf '"'"'merged-by-worker\\n'"'"' >> reg.txt && echo did-w"}'

# --- 1: default N=2, the 2nd edit of the merged path is refused --------------
ws="$tmp/ws1"; freshws "$ws"; printf 'base\n' > "$ws/reg.txt"; ( cd "$ws" && git add reg.txt && git commit -qm seed )
a1="$tmp/a1"; rm -rf "$a1"; mkdir -p "$a1/cap"
cat > "$a1/r.mm" <<EOF
wire openai
rule
  count 4
  tool read_file {"path":"reg.txt"}
rule
  count 5
  tool edit_file {"path":"reg.txt","old_string":"base","new_string":"LEAD-E1"}
rule
  count 6
  tool edit_file {"path":"reg.txt","old_string":"LEAD-E1","new_string":"LEAD-E2"}
rule
  count 7
  text LEADDONE
rule
  count 8
  text LEADDONE
rule
  count 9
  text LEADDONE
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"WKRTASK append a line to reg.txt","write":true}]}
$WORKER_REG
EOF
mm_start_unbounded "$a1/r.mm" "$a1/cap"
mkcfg "$a1/cfg.json" ""
runlead "$ws" "$a1/cfg.json" "$a1/out.jsonl" "$a1/tel.jsonl"
mm_stop
e1=$(grep -c "LEAD-E1" "$ws/reg.txt" 2>/dev/null || true)
e2=$(grep -c "LEAD-E2" "$ws/reg.txt" 2>/dev/null || true)
ref1=$(grep -h "worker-owned path" "$a1"/cap/req.* 2>/dev/null | grep -c . || true)
ev1=$(grep -c '"scoped_edit"' "$a1/tel.jsonl" 2>/dev/null || true)
{ [ "${e1:-0}" -ge 1 ] && [ "${e2:-0}" -eq 0 ] && [ "${ref1:-0}" -ge 1 ] && [ "${ev1:-0}" -ge 1 ]; } \
    && t_ok "1 default N=2: 1st edit landed, 2nd refused (event=$ev1)" \
    || t_fail "1 default N=2: LEAD-E1 on disk=$e1, LEAD-E2 on disk=$e2, refusal=$ref1, events=$ev1 (red before M975: the 2nd edit silently lands)"

# --- 2: N=0 turns the trip off ------------------------------------------------
ws="$tmp/ws2"; freshws "$ws"; printf 'base\n' > "$ws/reg.txt"; ( cd "$ws" && git add reg.txt && git commit -qm seed )
a2="$tmp/a2"; rm -rf "$a2"; mkdir -p "$a2/cap"
cp "$a1/r.mm" "$a2/r.mm"
mm_start_unbounded "$a2/r.mm" "$a2/cap"
mkcfg "$a2/cfg.json" ',"scopedEditMax":0'
runlead "$ws" "$a2/cfg.json" "$a2/out.jsonl" "$a2/tel.jsonl"
mm_stop
e2b=$(grep -c "LEAD-E2" "$ws/reg.txt" 2>/dev/null || true)
refb=$(grep -h "worker-owned path" "$a2"/cap/req.* 2>/dev/null | grep -c . || true)
evb=$(grep -c '"scoped_edit"' "$a2/tel.jsonl" 2>/dev/null || true)
{ [ "${e2b:-0}" -ge 1 ] && [ "${refb:-0}" -eq 0 ] && [ "${evb:-0}" -eq 0 ]; } \
    && t_ok "2 N=0: the 2nd edit lands, no refusal, no event (guard)" \
    || t_fail "2 N=0: LEAD-E2 on disk=$e2b, refusal=$refb, events=$evb (want the edit to land with no refusal)"

# --- 3: an un-owned file (the lead's own) is never refused ---------------------
ws="$tmp/ws3"; freshws "$ws"
a3="$tmp/a3"; rm -rf "$a3"; mkdir -p "$a3/cap"
cat > "$a3/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"mine.txt","content":"zero\n"}
rule
  count 2
  tool read_file {"path":"mine.txt"}
rule
  count 3
  tool edit_file {"path":"mine.txt","old_string":"zero","new_string":"one"}
rule
  count 4
  tool edit_file {"path":"mine.txt","old_string":"one","new_string":"two"}
rule
  count 5
  text LEADDONE
rule
  count 6
  text LEADDONE
rule
  count 7
  text LEADDONE
EOF
mm_start_unbounded "$a3/r.mm" "$a3/cap"
mkcfg "$a3/cfg.json" ""
runlead "$ws" "$a3/cfg.json" "$a3/out.jsonl" "$a3/tel.jsonl"
mm_stop
m3=$(grep -c "two" "$ws/mine.txt" 2>/dev/null || true)
ref3=$(grep -h "worker-owned path" "$a3"/cap/req.* 2>/dev/null | grep -c . || true)
{ [ "${m3:-0}" -ge 1 ] && [ "${ref3:-0}" -eq 0 ]; } \
    && t_ok "3 un-owned file: both lead edits land, no refusal (guard)" \
    || t_fail "3 un-owned: mine.txt on disk=$(cat "$ws/mine.txt" 2>/dev/null | tr '\n' ' '), refusal=$ref3 (an over-eager check refuses the lead's own file)"

# --- 4: a shell sed over the merged path is reported ---------------------------
ws="$tmp/ws4"; freshws "$ws"; printf 'base\n' > "$ws/reg.txt"; ( cd "$ws" && git add reg.txt && git commit -qm seed )
a4="$tmp/a4"; rm -rf "$a4"; mkdir -p "$a4/cap"
cat > "$a4/r.mm" <<EOF
wire openai
rule
  count 4
  tool run_terminal_command {"command":"sed 's/worker/worker2/' reg.txt > reg.tmp && mv reg.tmp reg.txt && echo did-sed"}
rule
  count 5
  tool read_file {"path":"reg.txt"}
rule
  count 6
  text LEADDONE
rule
  count 7
  text LEADDONE
rule
  count 8
  text LEADDONE
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"WKRTASK append a line to reg.txt","write":true}]}
$WORKER_REG
EOF
mm_start_unbounded "$a4/r.mm" "$a4/cap"
mkcfg "$a4/cfg.json" ""
runlead "$ws" "$a4/cfg.json" "$a4/out.jsonl" "$a4/tel.jsonl"
mm_stop
det=$(grep -h "changed outside the file tools" "$a4"/cap/req.* 2>/dev/null | grep -c . || true)
dev=$(grep '"scoped_edit"' "$a4/tel.jsonl" 2>/dev/null | grep -c "detected" || true)
{ [ "${det:-0}" -ge 1 ] && [ "${dev:-0}" -ge 1 ]; } \
    && t_ok "4 detection: the shell sed over the merged path is reported (event=$dev)" \
    || t_fail "4 detection: notice=$det, detected events=$dev (red before M975: the sed is invisible)"

# --- 5: a kept, UNMERGED child's path (the M865 hold) is never refused ---------
# The worker moves an assertion in pin_test.zig (line 1) -> the M865 hold KEEPS
# it (not merged), so the file is not worker-owned. The lead edits a DIFFERENT
# line (line 2, the worker never touched it) twice -- both must land with NO
# refusal.
ws="$tmp/ws5"; freshws "$ws"
printf 'assert(47)\ntwo-line\n' > "$ws/pin_test.zig"; ( cd "$ws" && git add pin_test.zig && git commit -qm seed )
a5="$tmp/a5"; rm -rf "$a5"; mkdir -p "$a5/cap"
cat > "$a5/r.mm" <<'EOF'
wire openai
rule
  count 2
  tool read_file {"path":"pin_test.zig"}
rule
  count 3
  tool edit_file {"path":"pin_test.zig","old_string":"assert(47)","new_string":"assert(43)"}
rule
  count 4
  text TASKDONE
rule
  count 5
  tool read_file {"path":"pin_test.zig"}
rule
  count 6
  tool edit_file {"path":"pin_test.zig","old_string":"two-line","new_string":"two-line-A"}
rule
  count 7
  tool edit_file {"path":"pin_test.zig","old_string":"two-line-A","new_string":"two-line-B"}
rule
  count 8
  text LEADDONE
rule
  count 9
  text LEADDONE
rule
  count 10
  text LEADDONE
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"WKRTASK move the pin","write":true}]}
EOF
mm_start_unbounded "$a5/r.mm" "$a5/cap"
mkcfg "$a5/cfg.json" ""
runlead "$ws" "$a5/cfg.json" "$a5/out.jsonl" "$a5/tel.jsonl"
mm_stop
b5=$(grep -c "two-line-B" "$ws/pin_test.zig" 2>/dev/null || true)
kept5=$(grep -h "not merged" "$a5"/cap/req.* 2>/dev/null | grep -c . || true)
ref5=$(grep -h "worker-owned path" "$a5"/cap/req.* 2>/dev/null | grep -c . || true)
{ [ "${kept5:-0}" -ge 1 ] && [ "${b5:-0}" -ge 1 ] && [ "${ref5:-0}" -eq 0 ]; } \
    && t_ok "5 kept/unmerged child path: the lead edits it freely (guard)" \
    || t_fail "5 kept/unmerged: held=$kept5, two-line-B on disk=$b5, refusal=$ref5 (ownership (a) is merged paths only -- a kept path is not owned)"

# --- 6: same as arm 1 but lead uses ./reg.txt (dot spelling) in both edits ----
ws="$tmp/ws6"; freshws "$ws"; printf 'base\n' > "$ws/reg.txt"; ( cd "$ws" && git add reg.txt && git commit -qm seed )
a6="$tmp/a6"; rm -rf "$a6"; mkdir -p "$a6/cap"
cat > "$a6/r.mm" <<EOF
wire openai
rule
  count 4
  tool read_file {"path":"./reg.txt"}
rule
  count 5
  tool edit_file {"path":"./reg.txt","old_string":"base","new_string":"LEAD-E1"}
rule
  count 6
  tool edit_file {"path":"./reg.txt","old_string":"LEAD-E1","new_string":"LEAD-E2"}
rule
  count 7
  text LEADDONE
rule
  count 8
  text LEADDONE
rule
  count 9
  text LEADDONE
rule
  count 1
  tool spawn_parallel {"tasks":[{"task":"WKRTASK append a line to reg.txt","write":true}]}
$WORKER_REG
EOF
mm_start_unbounded "$a6/r.mm" "$a6/cap"
mkcfg "$a6/cfg.json" ""
runlead "$ws" "$a6/cfg.json" "$a6/out.jsonl" "$a6/tel.jsonl"
mm_stop
e1=$(grep -c "LEAD-E1" "$ws/reg.txt" 2>/dev/null || true)
e2=$(grep -c "LEAD-E2" "$ws/reg.txt" 2>/dev/null || true)
ref1=$(grep -h "worker-owned path" "$a6"/cap/req.* 2>/dev/null | grep -c . || true)
ev1=$(grep -c '"scoped_edit"' "$a6/tel.jsonl" 2>/dev/null || true)
{ [ "${e1:-0}" -ge 1 ] && [ "${e2:-0}" -eq 0 ] && [ "${ref1:-0}" -ge 1 ] && [ "${ev1:-0}" -ge 1 ]; } \
    && t_ok "6 dot spelling: 1st edit landed, 2nd refused (event=$ev1)" \
    || t_fail "6 dot spelling: LEAD-E1 on disk=$e1, LEAD-E2 on disk=$e2, refusal=$ref1, events=$ev1 (red before F6: the dot spelling is not matched to the owned entry)"

t_done
