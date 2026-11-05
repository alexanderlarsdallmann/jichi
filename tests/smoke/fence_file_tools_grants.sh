#!/bin/sh
# smoke: a write child's file tools reach what its shell fence grants (M887, the demos
# drive's N49 (a)).
#
# THE DEFECT. A write child's shell may write where shellFenceAllow grants -- the
# drive grants /tmp, and its probes are told to write their findings to
# /tmp/<name>.tsv. Its write_file and read_file were refused there: "refused by safety
# fence (path outside workspace)", 2 times in run 0100 and 7 in run 0101, one probe
# three times with the same arguments. Then the probes used the shell -- or, through a
# relative path, put the scratch in the worktree, where the batch merge carried it into
# the live tree.
#
# THE FIX (the operator's decision, 2026-10-03: "Yes, in write children"). In a
# parallel write child, the file tools also reach the child's own temp dir and, with
# the shell fence on, the shellFenceAllow directories, for reads and writes. The top
# level is unchanged. (The temp dir's path is the child's own, so tests/test_app.c
# holds that grant; a mock cannot name it.)
#
#   1  a write child's write_file into a shellFenceAllow directory lands
#   2  its read_file there returns the file
#   3  CONTROL: a write_file outside the worktree and outside the grants is still
#      refused
#   4  CONTROL: at the top level, write_file into the granted directory is still
#      refused -- the grant is the write child's
#
# The markers come from jichi's effects (the files on disk) and from the child's own
# next request (which carries its tool results), never from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- a write child runs in a worktree"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
GRANT=$(smoke_tmp)        # the shellFenceAllow directory
OUTSIDE=$(smoke_tmp)      # neither the worktree nor a grant
marker="SEED-$((70 + 7))"
printf '%s\n' "$marker" > "$GRANT/seed.tsv"

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "shellFence":true,"shellFenceAllow":["$GRANT"]}
EOF
}

# run_lead <mm-file> <prompt> -> WS, CAP
run_lead() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg > "$CAP/cfg.json"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "$2" < /dev/null > "$CAP/out" 2>&1 )
    mm_stop
}

# One reply, three calls: write into the grant, read from it, write outside it.
cat > "$tmp/calls.sse" <<EOF
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"write_file","arguments":"{\\"path\\":\\"$GRANT/probe.tsv\\",\\"content\\":\\"probe\\\\n\\"}"}},{"index":1,"id":"c2","type":"function","function":{"name":"read_file","arguments":"{\\"path\\":\\"$GRANT/seed.tsv\\"}"}},{"index":2,"id":"c3","type":"function","function":{"name":"write_file","arguments":"{\\"path\\":\\"$OUTSIDE/x.tsv\\",\\"content\\":\\"x\\\\n\\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
cat > "$tmp/child.mm" <<'EOF'
wire openai
rule
  match "LEADG"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADG"
  tool spawn_parallel {"tasks":[{"task":"CHILDG write the probe's findings","write":true}]}
rule
  match "CHILDG"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDG"
  sse-file @TMP@/calls.sse
EOF
run_lead "$tmp/child.mm" "LEADG run the batch"
# the child's own next request: its task, its tool results, and not the lead's batch result
CR=""
for f in "$CAP"/cap/req.*; do
    if grep -q 'CHILDG' "$f" && grep -q '"role":"tool"' "$f" &&
       ! grep -q '## Parallel agents' "$f"; then
        CR=$f
        break
    fi
done

if [ -f "$GRANT/probe.tsv" ]; then
    t_ok "a write child's write_file into a shellFenceAllow directory lands"
else
    t_fail "write_file into the grant did not land: $(grep -o 'refused by safety fence[^"]*' "${CR:-/dev/null}" 2>/dev/null | head -n 1)"
fi
if [ -n "$CR" ] && grep -q "$marker" "$CR"; then
    t_ok "its read_file there returns the file"
else
    t_fail "read_file in the grant did not return the file: $(grep -o 'refused by safety fence[^"]*' "${CR:-/dev/null}" 2>/dev/null | head -n 1)"
fi
if [ ! -f "$OUTSIDE/x.tsv" ] && [ -n "$CR" ] && grep -q 'refused by safety fence' "$CR"; then
    t_ok "CONTROL: a write outside the worktree and the grants is still refused"
else
    t_fail "CONTROL: the write outside the grants was not refused: $(ls "$OUTSIDE")"
fi

# --- 4: CONTROL -- at the top level the grant does not apply ----------------------------
cat > "$tmp/top.sse" <<EOF
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"write_file","arguments":"{\\"path\\":\\"$GRANT/top.tsv\\",\\"content\\":\\"top\\\\n\\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
cat > "$tmp/top.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  match "\"role\":\"tool\""
  text LEAD-DONE
rule
  match "LEADT"
  sse-file @TMP@/top.sse
EOF
run_lead "$tmp/top.mm" "LEADT write it yourself"
if [ ! -f "$GRANT/top.tsv" ] && grep -l 'refused by safety fence' "$CAP"/cap/req.* > /dev/null 2>&1; then
    t_ok "CONTROL: at the top level the shellFenceAllow directory is still refused"
else
    t_fail "CONTROL: the top level wrote into the grant: $(ls "$GRANT")"
fi

t_done
