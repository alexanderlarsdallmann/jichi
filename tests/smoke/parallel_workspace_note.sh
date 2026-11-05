#!/bin/sh
# M820 (N6a, threadwork's weekend drive): a spawn_parallel WRITE child edits in its
# own isolated worktree, but its SHELL is not confined there (O9). On the drive a
# worker's task named "/work" -- the parent's live workspace -- eight times and made
# 53 write-class shell commands on it. The write child's system message now tells it
# plainly where it is ("## Your workspace ... your shell starts there ... do not cd
# into [the parent's workspace]"), the note the drive's team put in its profiles.
#
# The child calls the same mock endpoint as the lead, so its request -- system
# message and all -- is captured. We assert the note reaches a WRITE child's prompt,
# and NOT a read-only run's (it is gated on the worktree). The phrase is distinct
# from the tool description's own "isolated git worktree", which the LEAD carries.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"

# --- A write task: the child's prompt must carry the workspace note. ---
cat > "$tmp/w.mm" <<'EOF'
wire openai
rule
  match "LEADW"
  match "\"role\":\"tool\""
  text LEADWDONE
rule
  match "LEADW"
  tool spawn_parallel {"tasks":[{"task":"WRITETASK adjust reg.txt","write":true}]}
rule
  match "WRITETASK"
  text CHILDW-DONE
EOF
mm_start_unbounded "$tmp/w.mm" "$tmp/capW"
cat > "$tmp/cfgw.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfgw.json" -q --auto \
    -p "LEADW run one writer" < /dev/null > "$tmp/outw.txt" 2>&1 )
mm_stop

if grep -rq 'your shell starts' "$tmp/capW" 2>/dev/null; then
    t_ok "the write child's prompt carries the worktree/shell note"
else
    t_fail "no workspace note in any captured child request (outw: $(head_bytes 160 "$tmp/outw.txt"))"
fi

# --- A read-only run: the note is gated on the worktree, so it must be absent. ---
cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  match "LEADR"
  match "\"role\":\"tool\""
  text LEADRDONE
rule
  match "LEADR"
  tool spawn_parallel {"tasks":[{"task":"READTASK look at reg.txt"}]}
rule
  match "READTASK"
  text CHILDR-DONE
EOF
mm_start_unbounded "$tmp/r.mm" "$tmp/capR"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfgw.json" -q --auto \
    -p "LEADR run one reader" < /dev/null > "$tmp/outr.txt" 2>&1 )
mm_stop

if grep -rq 'your shell starts' "$tmp/capR" 2>/dev/null; then
    t_fail "a read-only child got the write-only worktree note"
else
    t_ok "a read-only child does not get the worktree note (write-gated)"
fi
t_done
