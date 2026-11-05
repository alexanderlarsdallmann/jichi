#!/bin/sh
# smoke: a write task's ROOT report (REPORT.md / REPORT-*.md) is kept OUT of the
# file-level merge (def-332, M1044): both workers' reports are stored redacted
# with the run's records (~/.jichi.d/attempts/<run>/report-t<N>.md, beside the
# child transcripts), named in the batch result, and neither merged -- so a
# shared report name no longer drops the second writer's evidence as a code
# conflict or forces a clean task into a Kept parallel-conflict state.
#
#   1  both code files merge (a.txt / b.txt) -- the code path is intact
#   2  NO REPORT.md in the live workspace (neither report merged)
#   3  REDACTION PIN: task 1's stored report (report-t1.md) carries *** and NOT
#      the 18-byte canary apiKey the config registered with the redactor
#   4  TRUNCATION PIN: task 2's stored report (report-t2.md, ~9030 bytes, longer
#      than the old 8192-byte redaction buffer) ENDS with its TAILMARK -- the
#      tail was not cut
#   5  the batch result names both: "## Reports" with the stored paths
#   6  nothing is dropped or Kept for the report: no "REPORT.md (task" line,
#      no parallel-conflict Kept
#
# ROUTING (each child does exactly THREE calls; the canary/TAILMARK sit where
# any truncation would drop them otherwise: at the START and the END):
#   TASK_A (the REDACTION pin): 1st call writes a.txt; 2nd (a tool result is
#     present, no REPORT.md yet) writes REPORT.md with the canary in its FIRST
#     bytes; 3rd (the tool result names REPORT.md) falls to the final text.
#   TASK_B (the TRUNCATION pin): 1st call writes b.txt; 2nd (a tool result is
#     present, no REPORT.md yet) run_terminal_command builds REPORT.md longer
#     than 8191 bytes with TAILMARK at the END (its final `echo REPORT.md` goes
#     to stdout, so the 3rd call's routing sees it and stops); 3rd falls to the
#     final text. The 9000 filler bytes are `dd bs=9000 count=1 if=/dev/zero`
#     (NOT `head -c`): posix_utils_lint bans the GNU-only `head -c` in the
#     shell sources, and dd is byte-identical here (the tier's own head_bytes
#     is built on dd).
#   lead final: "## Parallel agents" + the task markers  -> final text
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "TASK_A"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"a.txt","content":"AA\n"}
rule
  match "TASK_A"
  match "\"role\":\"tool\""
  nomatch "REPORT.md"
  nomatch "## Parallel agents"
  tool write_file {"path":"REPORT.md","content":"canarysecretXYZ123 evidence A\n"}
rule
  match "TASK_B"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"b.txt","content":"BB\n"}
rule
  match "TASK_B"
  match "\"role\":\"tool\""
  nomatch "REPORT.md"
  nomatch "## Parallel agents"
  tool run_terminal_command {"command":"echo reportB-head > REPORT.md; dd bs=9000 count=1 if=/dev/zero 2>/dev/null | tr '\\0' x >> REPORT.md; echo TAILMARK >> REPORT.md; echo REPORT.md"}
rule
  nomatch "TASK_A"
  nomatch "TASK_B"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"TASK_A: create a.txt","write":true},{"task":"TASK_B: create b.txt","write":true}]}
rule
  text all done
EOF

mm_start_unbounded "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"canarysecretXYZ123","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "parallelVerify":false,"maxRetries":0,"maxParallelAgents":2,"maxSubagentIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF

# a git workspace with one committed seed
( cd "$ws" && git init -q && git config user.email t@t \
    && git config user.name t \
    && printf 'seed\n' > seed.txt && git add -A && git commit -qm init )

(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --no-stdin -p "delegate two writes" \
    < /dev/null > "$tmp/out" 2>&1)
mm_stop

LR=$(grep -l '## Parallel agents' "$tmp"/req.* 2>/dev/null | tail -1)

if [ -f "$ws/a.txt" ] && [ "$(cat "$ws/a.txt")" = "AA" \
   ] && [ -f "$ws/b.txt" ] && [ "$(cat "$ws/b.txt")" = "BB" ]; then
    t_ok "both code files merged (a.txt=AA, b.txt=BB) -- the code path is intact"
else
    t_fail "a code file missing/wrong: a=[$(cat "$ws/a.txt" 2>/dev/null)] b=[$(cat "$ws/b.txt" 2>/dev/null)] (ls: $(ls "$ws" | tr '\n' ' '))"
fi
if [ ! -e "$ws/REPORT.md" ]; then
    t_ok "no REPORT.md in the live workspace (neither report was merged)"
else
    t_fail "REPORT.md was merged into the live tree: $(head_bytes 120 "$ws/REPORT.md")"
fi
R1=$(find "$HOME/.jichi.d/attempts" -name report-t1.md 2>/dev/null | head -n 1)
if [ -n "$R1" ] && grep -q '\*\*\*' "$R1" \
   && ! grep -q 'canarysecretXYZ123' "$R1"; then
    t_ok "task 1's stored report is redacted: it carries *** and NOT the registered canary apiKey"
else
    t_fail "the redaction pin is not held: report-t1.md missing ($R1) or the canary was not scrubbed (canary present: $(grep -c 'canarysecretXYZ123' "${R1:-/dev/null}" 2>/dev/null), *** present: $(grep -c '\*\*\*' "${R1:-/dev/null}" 2>/dev/null)); $(head_bytes 200 "${R1:-/dev/null}")"
fi
R2=$(find "$HOME/.jichi.d/attempts" -name report-t2.md 2>/dev/null | head -n 1)
if [ -n "$R2" ] && tail -c 40 "$R2" | grep -q 'TAILMARK'; then
    t_ok "task 2's stored report is NOT truncated: the ~9030-byte report ends with its TAILMARK"
else
    t_fail "the truncation pin is not held: report-t2.md missing ($R2) or its TAILMARK was cut off (size: $(wc -c < "$R2" 2>/dev/null), tail: $(tail -c 40 "$R2" 2>/dev/null | tr -d '\0' | tr '\n' ' '))"
fi
if [ -n "$LR" ] && grep -q '## Reports' "$LR" \
   && grep -q 'report-t1.md' "$LR" && grep -q 'report-t2.md' "$LR" \
   && grep -q 'kept out of the merge' "$LR"; then
    t_ok "the batch result names both stored reports (## Reports + report-t1.md + report-t2.md)"
else
    t_fail "the result does not name the stored reports: $(grep -o '## Reports[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
if [ -n "$LR" ] && ! grep -q 'REPORT.md (task' "$LR" \
   && ! grep -q 'parallel-conflict' "$LR"; then
    t_ok "nothing was dropped or Kept for the report (no 'REPORT.md (task' line, no parallel-conflict Kept)"
else
    t_fail "the report still surfaced as merge fallout: $(grep -o 'REPORT.md (task[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 200) $(grep -o 'parallel-conflict[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 200)"
fi

t_done
