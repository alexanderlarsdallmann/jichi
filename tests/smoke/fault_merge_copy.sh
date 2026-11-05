#!/bin/sh
# M927 (R04-F1): a failed merge copy is a CONFLICT, not a silent drop.
# FAULT-gated (JICHI_FAULT_COPY_AFTER=0), SKIPs on a normal binary.
# Pre-M927 a failed copy was recorded NOWHERE: the task was marked merged
# and its worktree -- the only copy of the change -- destroyed. Now the
# result says "copy failed: not merged" and the worktree is preserved.
. "$(dirname "$0")/_smoke.sh"
# Guard before t_plan: the plan is only emitted when the driver actually runs
# (a FAULT=1 binary) -- t_skip emits the sole 1..0 plan itself.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"
t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
cap=$(smoke_tmp)
( cd "$ws" || exit 1
  git init -q 2>/dev/null && git config user.email f@f && git config user.name f && git commit -q --allow-empty -m init
  cat > replies.mm <<'MM'
wire openai
rule
  match "CA"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"alpha.txt","content":"AAA"}
rule
  match "CA"
  match "\"role\":\"tool\""
  text CA-DONE
rule
  nomatch "CA"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"CA: create alpha.txt","write":true}]}
rule
  match "\"role\":\"tool\""
  text lead-done
MM
  mm_start_unbounded "$ws/replies.mm" "$cap"
  cat > config.json <<CFG
{"toolProfile":"full","lowResource":false,
 "models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "preserveDiscarded":true}
CFG
  export JICHI_FAULT_COPY_AFTER=0
  with_deadline 120 "$BIN" --config config.json \
      -q --no-session --auto "spawn the child" </dev/null > "$tmp/out" 2>&1
  mm_stop )
if [ -f "$ws/alpha.txt" ]; then
    t_fail "the failed copy must not leave the file in the workspace"
else
    t_ok "the failed copy leaves no file (not silently applied)"
fi
if grep -l "copy failed: not merged" "$cap"/req.* >/dev/null 2>&1; then
    t_ok "the result names the failed copy, and the worktree is preserved"
else
    t_fail "no 'copy failed: not merged' in the result -- the failed copy was silently reported as merged (or lost)"
    sed 's/^/    | /' "$cap"/req.* 2>/dev/null | head -8
fi
t_done
