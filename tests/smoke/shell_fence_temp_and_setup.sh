#!/bin/sh
# smoke: a fenced write child can do real work, and a fence that is set up wrong
# says so (M852, the demos drive's N36; the operator's decisions of 2026-10-01).
#
# THE DEFECT. M831's Landlock fence confines a spawn_parallel write child's shell to
# its worktree, the shadow repo and /dev/null. It left /tmp out on purpose (world-
# writable, and a project can live there) but gave the child no temp dir of its own,
# so every tool that makes a temp file failed: measured, a fenced child's `cc -c`
# aborted with "Cannot create temporary file in /tmp: Permission denied", and
# `mktemp` failed. Nothing said the fence was the cause. M831's driver never ran a
# tool that needs a temp dir, and its header said "the temp dir" was allowed.
#
# THE FIX AND THE DECISIONS.
#   - Each write child gets a private $TMPDIR in its worktree's git admin dir
#     (`<shadow repo>/worktrees/<id>/tmp`): granted already, outside the worktree (so
#     never merged), removed with the worktree.
#   - A shell command whose output reads like a refused write gets a one-line hint,
#     and the batch result counts them per task.
#   - Case A: Landlock is present but the fence cannot be applied -> the write task
#     is REFUSED (M831 ran it unfenced). The reproducible trigger: a regular file
#     listed in shellFenceAllow (directory rights on a file are EINVAL).
#   - Case B: a shellFenceAllow entry that is not absolute or does not exist grants
#     nothing -- the fence still holds (measured) -- and the batch result now says so
#     once, at the top.
#
#   1  a fenced write child's `cc -c` compiles (red before M852: it aborted)
#   2  `mktemp` works, and its file is under the shadow repo, not in the worktree
#   3  the temp dir is gone after the batch
#   4  the temp file never reaches the live workspace (the merge does not carry it)
#   5  a command with no permission error gets no fence hint
#   6  case B: missing and relative entries are named once at the top of the result
#   7  case B: and the fence still holds -- an escape is still refused
#   8  case A: a file listed in shellFenceAllow refuses the write task, naming it,
#      and the child never calls the model
#   9  case A: a read-only task in the same batch still runs
#  10  the hint: a refused write gets the fence hint in the child's tool result
#  11  the hint: and the batch result counts it for that task
#
# Every marker a check greps is printed by the child's OUTPUT, never spelled in its
# command (a request carries both): CC-OK-$((40+2)) prints CC-OK-42, and the paths
# are matched from their leading slash. A first draft grepped plain markers and three
# checks passed on the command text alone.
. "$(dirname "$0")/_smoke.sh"

# Precondition, as in parallel_shell_fence.sh: without Landlock there is no fence.
probe_home=$(smoke_tmp)
HOME="$probe_home" "$BIN" doctor < /dev/null 2>&1 | grep -q 'shell fence: available' ||
    t_skip "no Landlock on this kernel (shell fence unavailable) -- doctor says so"

t_plan 11
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

# run_batch <mm-file> <extra-config> -> WS, CAP (captures), OUT
run_batch() {
    WS=$(smoke_tmp)
    printf 'int f(void) { return 1; }\n' > "$WS/x.c"
    CAP=$(smoke_tmp)
    sed -e "s#@WS@#$WS#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1 )
    mm_stop
}

# the lead's view: the follow-up request that carries the batch result
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
# a child's view: its follow-up request (it carries the tool result)
child_followup() { grep -l "$1" "$CAP"/cap/req.* 2>/dev/null |
                   xargs grep -l '"role":"tool"' 2>/dev/null |
                   xargs grep -L '## Parallel agents' 2>/dev/null | head -1; }

# --- 1-5: real work under the fence ----------------------------------------------
cat > "$tmp/work.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDW compile and make a temp file","write":true}]}
rule
  match "CHILDW"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDW"
  tool run_terminal_command {"command":"cc -c x.c -o x.o && echo CC-OK-$((40+2)); T=$(mktemp) && echo \"MK-OK:$T\"; echo \"TD:$TMPDIR\""}
EOF
run_batch "$tmp/work.mm" ""
CF=$(child_followup CHILDW)
if [ -n "$CF" ] && grep -q 'CC-OK-42' "$CF"; then
    t_ok "a fenced write child's cc -c compiles"
else
    t_fail "a fenced write child could not compile (red before M852: 'Cannot create
 temporary file'): $(tail -c 300 "${CF:-/dev/null}" 2>/dev/null)"
fi
MK=$(grep -o 'MK-OK:/[^\\" ]*' "${CF:-/dev/null}" 2>/dev/null | head -1 | sed 's/^MK-OK://')
TD=$(grep -o 'TD:/[^\\" ]*' "${CF:-/dev/null}" 2>/dev/null | head -1 | sed 's/^TD://')
case "$MK" in
    "$HOME"/.jichi.d/checkpoints/*/worktrees/*/tmp/*)
        t_ok "mktemp works, and its file is under the shadow repo's worktree admin dir" ;;
    *)  t_fail "mktemp failed, or its file is not in the private temp dir: '$MK' (TMPDIR='$TD')" ;;
esac
if [ -n "$TD" ] && [ ! -e "$TD" ]; then
    t_ok "the private temp dir is gone after the batch"
else
    t_fail "the private temp dir survived the batch, or was never set: '$TD'"
fi
mkbase=$(basename "${MK:-none}")
if [ -n "$MK" ] && ! find "$WS" -name "$mkbase" 2>/dev/null | grep -q .; then
    t_ok "the temp file never reached the live workspace"
else
    t_fail "the temp file '$mkbase' was merged into the live workspace (or mktemp failed)"
fi
if [ -n "$CF" ] && ! grep -q 'This shell is fenced (M831)' "$CF"; then
    t_ok "a command with no permission error gets no fence hint"
else
    t_fail "a successful command got the fence hint (or the child never ran)"
fi

# --- 6-7: case B -- entries that grant nothing are named once, the fence holds --
cat > "$tmp/caseb.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDB write outside","write":true}]}
rule
  match "CHILDB"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDB"
  tool run_terminal_command {"command":"echo b > @WS@/esc_b.txt 2>/dev/null; echo B-RAN-$((40+2))"}
EOF
run_batch "$tmp/caseb.mm" ',"shellFenceAllow":["relative/cache","/nonexistent-m852-dir"]'
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'shellFenceAllow: ' "$LR" &&
   grep -q 'relative/cache' "$LR" && grep -q 'is not an absolute path' "$LR" &&
   grep -q 'nonexistent-m852-dir' "$LR" && grep -q 'does not exist' "$LR"; then
    t_ok "case B: entries that grant nothing are named once, at the top of the result"
else
    t_fail "case B: the batch result does not name the entries that grant nothing:
 $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi
if [ ! -e "$WS/esc_b.txt" ] && grep -q 'B-RAN-42' "$CAP"/cap/req.* 2>/dev/null; then
    t_ok "case B: the fence still holds -- the escape was refused and the child ran"
else
    t_fail "case B: the escape landed, or the child never ran (esc_b=$([ -e "$WS/esc_b.txt" ] && echo landed || echo absent))"
fi

# --- 8-9: case A -- a file listed as a directory refuses the write task ----------
AFILE="$tmp/not-a-directory"
printf 'x\n' > "$AFILE"
cat > "$tmp/casea.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDA should never run","write":true},{"task":"READA read only"}]}
rule
  match "CHILDA"
  text CHILDA-RAN
rule
  match "READA"
  text READA-DONE
EOF
run_batch "$tmp/casea.mm" ",\"shellFenceAllow\":[\"$AFILE\"]"
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'refused: the shell fence could not be applied' "$LR" &&
   grep -q 'not-a-directory' "$LR" &&
   ! grep -l 'CHILDA should never run' "$CAP"/cap/req.* 2>/dev/null |
     xargs grep -L '## Parallel agents' 2>/dev/null | grep -q .; then
    t_ok "case A: a file listed in shellFenceAllow refuses the write task, naming it"
else
    t_fail "case A: the write task was not refused (or did not name the entry):
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi
if [ -n "$LR" ] && grep -q 'READA-DONE' "$LR"; then
    t_ok "case A: a read-only task in the same batch still runs"
else
    t_fail "case A: the read-only sibling did not run: $(head_bytes 300 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 10-11: the hint, and its count -------------------------------------------------
cat > "$tmp/hint.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDH write outside loudly","write":true}]}
rule
  match "CHILDH"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDH"
  tool run_terminal_command {"command":"echo h > @WS@/esc_h.txt"}
EOF
run_batch "$tmp/hint.mm" ""
CF=$(child_followup CHILDH)
if [ -n "$CF" ] && grep -q 'This shell is fenced (M831)' "$CF"; then
    t_ok "the hint: a refused write gets the fence hint in the child's tool result"
else
    t_fail "no fence hint after a refused write: $(tail -c 300 "${CF:-/dev/null}" 2>/dev/null)"
fi
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'shell command(s) in this task failed with a permission error under the shell fence' "$LR"; then
    t_ok "the hint: the batch result counts it for that task"
else
    t_fail "the batch result does not count the fence hint: $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi

t_done
