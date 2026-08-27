#!/bin/sh
# smoke: `attempt` refuses what it cannot grade, and runs `setup` where it
# grades (M615, seams B2 of docs/analysis/2026-08-27-the-teaching-seams.md).
#
# TWO CONFLATIONS, one driver:
#   1. A verify whose PROGRAM does not resolve from here was scored FAIL --
#      exit 1, and --record wrote passed:false into the learner's progress
#      file. `grade` has refused this since M502 ("This is NOT a grade");
#      `attempt` had no cannot-run class at all.
#   2. `grade` runs the spec's `setup` before verify; `attempt` never ran it,
#      so a setup-dependent spec graded differently per surface. setup now
#      runs INSIDE the worktree, before the turn -- and must not touch the
#      real workspace.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws/docs/assignments"

# A catch-all mock: pre-M615 the cannot-run spec still reached the model.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text NOTHING_TO_DO
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"assignments":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

# --- 1-3: cannot-run is a refusal, not a grade -------------------------------
cat > "$ws/docs/assignments/broken.md" <<'EOF'
---
title: broken harness
verify: "sh ./missing/grader.sh"
points: 1
---
The grader is not reachable from this directory.
EOF
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      attempt docs/assignments/broken.md --record < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ]; then
    t_ok "attempt exits 2 on an unrunnable verify (a refusal, not a grade)"
else
    t_fail "attempt rc=$rc (want 2): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)"
fi
case "$out" in
    *"NOT a grade"*) t_ok "the refusal says so in M502's words" ;;
    *) t_fail "no refusal wording: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac
if [ ! -f "$ws/.jichi/progress.jsonl" ]; then
    t_ok "--record wrote nothing for the refusal"
else
    t_fail "a non-grade was recorded: $(head_bytes 120 "$ws/.jichi/progress.jsonl")"
fi

# --- 4-5: setup runs in the worktree, before the turn ------------------------
# The gate passes iff setup created the marker; the scripted model does
# nothing. PASS therefore proves setup ran -- and it ran in the SANDBOX:
# the real workspace must stay markerless.
cat > "$ws/docs/assignments/withsetup.md" <<'EOF'
---
title: setup makes the marker
verify: "[ -f marker ]"
setup: "touch marker"
points: 1
---
The setup command creates the marker; verify only checks it.
EOF
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      attempt docs/assignments/withsetup.md < /dev/null 2>&1); rc=$?
mm_stop
case "$out" in
    *"-- PASS"*) t_ok "setup ran in the worktree (verify green, model did nothing)" ;;
    *) t_fail "no PASS (rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)" ;;
esac
if [ ! -f "$ws/marker" ]; then
    t_ok "the real workspace stayed markerless (setup ran in the sandbox only)"
else
    t_fail "setup leaked into the workspace"
fi
t_done
