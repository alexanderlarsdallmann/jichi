#!/bin/sh
# smoke: a parallel write child's `git stash` is kept, named, and never popped by another
# batch (M855, the demos drive's N34, and what measuring it found).
#
# THE DEFECT, AS REPORTED (N34). The drive's children ran `git stash` 57 times in seven
# runs. A child's kept state is its worktree, committed (M803) -- and a stashed change is
# not in the worktree. Run 0070: a worker stashed its change to test the base, was
# stopped, and its kept ref was empty.
#
# WHAT MEASURING IT FOUND (git 2.43, 2026-10-02). A write child's worktree is a LINKED
# worktree of the shadow repo, and linked worktrees share one `refs/stash`: worktree 1
# stashed A, worktree 2 stashed B, and worktree 1's `git stash pop` applied B. So the
# stash is one stack for every child of a batch, and an entry left in it outlives the
# batch: a later batch's child that runs `git stash pop` applies it.
#
# THE FIX (DECISIONS M855):
#   - before a batch with write tasks starts, and after it ends, every entry on the shadow
#     repo's stash stack is pinned as a kept ref and the stack is emptied;
#   - the batch result names the pinned entries (a stash cannot be traced to one child);
#   - a write child that runs `git stash` (its shell or the git_stash tool) is told, in
#     the tool result, that the stash is shared and not kept, and to commit instead.
#
#   1  a child's stashed change is kept under a parallel-stash ref, holding the change
#   2  the batch result names it
#   3  the stash stack is empty after the batch
#   4  the child is told the stash is shared and not kept
#   5  an entry left from before a batch is set aside at its start: a child's
#      `git stash pop` does not apply it, and it never reaches the live tree
#   6  and the batch result names the entry it set aside
#   7  with preservation off (preserveDiscarded: false) the stack is left alone: an
#      entry jichi cannot keep is never deleted
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

run_batch() {  # $1 = mm file, $2 = extra config; the workspace is $WS
    CAP=$(smoke_tmp)
    cp "$1" "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "LEADX run the batch" < /dev/null > "$CAP/out" 2>&1 )
    mm_stop
}
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
child_followup() { grep -l "$1" "$CAP"/cap/req.* 2>/dev/null |
                   xargs grep -l '"role":"tool"' 2>/dev/null |
                   xargs grep -L '## Parallel agents' 2>/dev/null | head -1; }
shadow() {  # the workspace's shadow repo: the one whose refs hold jichi's checkpoints
    for g in "$HOME"/.jichi.d/checkpoints/*; do
        [ -d "$g" ] && git --git-dir="$g" rev-parse -q --verify HEAD >/dev/null 2>&1 &&
            { echo "$g"; return; }
    done
}

# --- 1-4: a child stashes its change ------------------------------------------------
cat > "$tmp/stash.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDS change seed.txt, then stash it","write":true}]}
rule
  match "CHILDS"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDS"
  tool run_terminal_command {"command":"printf 'stashed-change\\n' > seed.txt && git -c user.name=c -c user.email=c@c stash"}
EOF
run_batch "$tmp/stash.mm"
G=$(shadow)
LR=$(lead_result)
REF=$(grep -o 'refs/jichi/discarded/parallel-stash/[0-9]*-[0-9]*' "${LR:-/dev/null}" 2>/dev/null | head -1)
if [ -n "$G" ] && [ -n "$REF" ] &&
   git --git-dir="$G" show "$REF:seed.txt" 2>/dev/null | grep -q 'stashed-change'; then
    t_ok "a child's stashed change is kept under a parallel-stash ref, holding the change"
else
    t_fail "no parallel-stash ref holds the stashed change (red before M855): shadow='$G' ref='$REF'"
fi
if [ -n "$LR" ] && grep -q 'stash entr' "$LR" && [ -n "$REF" ]; then
    t_ok "the batch result names the kept stash entry"
else
    t_fail "the batch result does not name the stash entry: $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi
# the store sentence on the stash section: "jichi's own attempt store under ... not in this workspace"
stash_store_hint=$(grep -h 'Kept stash entr' "$LR" 2>/dev/null | grep -c 'jichi.*attempt store.*not in this workspace')
[ "$stash_store_hint" -ge 1 ] \
    && t_ok "the Kept stash entries section says the ref lives in jichi's attempt store (not the workspace)" \
    || t_fail "the Kept stash entries section gives no store location hint: $(grep -h 'Kept stash entr' "$LR" 2>/dev/null | head -3)"
if [ -n "$G" ] && ! git --git-dir="$G" rev-parse -q --verify refs/stash >/dev/null 2>&1; then
    t_ok "the stash stack is empty after the batch"
else
    t_fail "the shadow repo's stash stack still holds entries after the batch (shadow='$G')"
fi
CF=$(child_followup CHILDS)
if [ -n "$CF" ] && grep -q 'the stash is shared with every other child' "$CF"; then
    t_ok "the child is told the stash is shared and not kept"
else
    t_fail "no stash hint in the child's tool result: $(tail -c 300 "${CF:-/dev/null}" 2>/dev/null)"
fi

# --- 5-6: an entry from before the batch is set aside --------------------------------
# Plant one, as an earlier batch (or a jichi before M855) would have left it: a linked
# worktree of the shadow repo stashes a change and is removed; the entry stays.
# The add uses NO `-q` and the remove is `rm -rf` + `worktree prune` -- the same
# version-independent idiom the product uses (jc_snapshot.c:929, 1542-1545). `worktree
# add -q` and `worktree remove` are git 2.17+ forms: on Debian 9's git 2.11 the `-q`
# exits 129 (so plant/ is never made and the seed write hits 'Directory nonexistent'),
# which stopped the whole V2f smoke tier at this driver (2026-10-14 re-test).
if [ -n "$G" ]; then
    git --git-dir="$G" worktree add --detach "$tmp/plant" HEAD >/dev/null 2>&1
    printf 'LEFTOVER\n' > "$tmp/plant/seed.txt"
    ( cd "$tmp/plant" && git -c user.name=p -c user.email=p@p stash -q ) >/dev/null 2>&1
    rm -rf "$tmp/plant"
    git --git-dir="$G" worktree prune >/dev/null 2>&1
fi
planted=$(git --git-dir="$G" rev-parse -q --verify refs/stash 2>/dev/null)
cat > "$tmp/pop.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDP pop the stash","write":true}]}
rule
  match "CHILDP"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDP"
  tool run_terminal_command {"command":"git stash pop; cat seed.txt"}
EOF
run_batch "$tmp/pop.mm"
LR=$(lead_result)
if [ -n "$planted" ] && grep -q '^seed$' "$WS/seed.txt" && ! grep -q 'LEFTOVER' "$WS/seed.txt"; then
    t_ok "an entry from before the batch is set aside: the child's pop does not apply it"
else
    t_fail "the earlier entry reached the live tree, or was never planted: planted='$planted'
 seed.txt=$(cat "$WS/seed.txt" 2>/dev/null)"
fi
if [ -n "$LR" ] && grep -q 'refs/jichi/discarded/parallel-stash-earlier/' "$LR"; then
    t_ok "the batch result names the entry it set aside"
else
    t_fail "the batch result does not name the earlier entry: $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 7: preservation off -- the stack is left alone ---------------------------------
WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
before=$(ls -d "$HOME"/.jichi.d/checkpoints/* 2>/dev/null)
run_batch "$tmp/stash.mm" ',"preserveDiscarded":false'
G7=""
for g in "$HOME"/.jichi.d/checkpoints/*; do
    case "$before" in *"$g"*) ;; *) G7="$g" ;; esac
done
LR=$(lead_result)
if [ -n "$G7" ] && git --git-dir="$G7" rev-parse -q --verify refs/stash >/dev/null 2>&1 &&
   ! grep -q 'Kept stash entries' "${LR:-/dev/null}" 2>/dev/null; then
    t_ok "with preservation off the stash stack is left alone (nothing kept, nothing deleted)"
else
    t_fail "with preservation off the stash entry was deleted, or was reported kept: shadow='$G7'"
fi

t_done
