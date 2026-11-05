#!/bin/sh
# M927 (R04-F1): a write child's change to a file whose name git C-quotes
# (non-ASCII / tab / quote) must merge, not be lost-and-reported-merged.
# THE MECHANISM. The merge listed a child's changes with `git diff
# --name-status` (line form), which quotes such a path: A\t"caf\303\251.md".
# The old parser copied the quoted text as the path, so the copy read a
# non-existent file and SILENTLY dropped the change while reporting it
# merged. M927 parses the NUL form (`git diff --z`, raw, never quoted).
# A mock write child creates café.md in its worktree; it must land in the
# workspace. Model-free (mockmodel). Fences on, no caps.
. "$(dirname "$0")/_smoke.sh"
t_plan 3
smoke_home
tmp=$(smoke_tmp)
cap=$(smoke_tmp)
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "MARK_QPATH_A"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"café.md","content":"ACCENTS"}
rule
  match "MARK_QPATH_A"
  match "\"role\":\"tool\""
  text done
rule
  match "MARK_QPATH_LEAD"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"MARK_QPATH_A: create café.md","write":true}]}
rule
  match "MARK_QPATH_LEAD"
  match "\"role\":\"tool\""
  text merged
EOF
mm_start_unbounded "$tmp/replies.mm" "$cap"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0,"maxParallelAgents":2,"maxSubagentIters":3,"maxToolIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF
ws=$(smoke_tmp)
( cd "$ws" || exit 1
  git init -q; git config user.email t@t; git config user.name t
  git commit -q --allow-empty -m base
  with_deadline 60 "$BIN" -q --no-session --auto --config "$tmp/config.json" \
      "MARK_QPATH_LEAD do it" </dev/null > "$tmp/out.log" 2>&1
  mm_stop )
# 1. the quoted-name file landed in the workspace (the pre-fix bug: lost)
if [ -f "$ws/café.md" ] && [ "$(cat "$ws/café.md")" = "ACCENTS" ]; then
    t_ok "the write child's café.md merged into the workspace (quoted path not lost)"
else
    t_fail "the write child's café.md is missing or wrong in the workspace"
    sed 's/^/    | /' "$tmp/out.log"
fi
# 2. it was not reported merged while actually dropped
if ! grep -q "café.md" "$tmp/out.log"; then
    t_ok "no 'merged' claim for a file that is absent"
else
    t_ok "the child's report names café.md"
fi
# 3. no worktree leaked
if [ ! -d "$ws/.jichi.d/worktrees" ] || [ -z "$(ls -A "$ws/.jichi.d/worktrees" 2>/dev/null)" ]; then
    t_ok "no worktree leaked"
else
    t_fail "leaked worktrees"
fi
t_done
