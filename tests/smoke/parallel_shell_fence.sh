#!/bin/sh
# M831 (O9): the Landlock shell fence on a spawn_parallel WRITE child.
#
# A write child edits in its own isolated worktree, and its FILE tools are path-
# fenced to it (M24/M820). Its SHELL is not: run_terminal_command can write anywhere
# the process can, and a lexical guard on the command TEXT is not a fence -- an
# absolute path, a `cd` out of the worktree, a variable holding the path, and a
# script written then run all reach the live tree the same way. M831 applies a
# Landlock write fence in the forked child before its agent loop, confining writes
# (child and every command it starts) to the worktree, the shadow repo, /dev/null and
# the directories in shellFenceAllow. M852: until then this header also said "the temp
# dir" -- no temp dir was granted, and a fenced child's `cc -c` and `mktemp` failed
# (measured). Since M852 the child's own $TMPDIR lives inside the shadow repo, and
# shell_fence_temp_and_setup.sh holds what a fenced child can do and how a fence set
# up wrong is reported.
#
# This driver DRIVES a real write child (via mockmodel) whose one shell command
# attempts all four escape routes AND a legitimate in-worktree write, then it checks
# the effect on disk: fenced, every escape file is ABSENT from the live workspace and
# the in-worktree write still merged back; UNFENCED (shellFence: false), the same
# command lands the escape in the live tree -- which is the instrument proof that the
# refusals are real and not vacuous (TESTING_RUNBOOK: perturb per check, prove it
# can fire). Skips whole where this kernel has no Landlock: there the fence genuinely
# cannot apply, so the escape-refusal premise does not hold (doctor reports it, and
# the child says so at run time -- never silently).
. "$(dirname "$0")/_smoke.sh"

# Precondition: the shell fence must be AVAILABLE here, or the whole driver is moot.
if ! "$BIN" doctor 2>&1 | grep -q 'shell fence: available'; then
    t_skip "no Landlock on this kernel (shell fence unavailable) -- doctor says so"
fi

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"

# The one shell command the write child runs. @WS@ is the live workspace (the parent
# tree, NOT the child's worktree); every write to it must be refused when fenced.
# Route 1 absolute path; 2 cd into the live tree then relative write; 3 a variable
# holding the path; 4 a script written in the worktree (allowed) that writes outside.
# Then the legitimate write: reg.txt (relative -> the worktree, must merge back) and
# /dev/null (granted). Each escape is `; `-separated so one refusal does not abort
# the rest, and stderr is dropped so a refusal is quiet.
CMD="echo abs > @WS@/esc_abs.txt 2>/dev/null; cd @WS@ && echo cd > esc_cd.txt 2>/dev/null; cd - >/dev/null; P=@WS@/esc_var.txt; echo var > \$P 2>/dev/null; echo 'echo scr > @WS@/esc_script.txt' > s.sh; sh s.sh 2>/dev/null; echo inside > reg.txt; : > /dev/null; echo FENCE-CMD-RAN"

mm_from_cmd() {  # $1 = expanded command -> writes an .mm to stdout
    cat <<EOF
wire openai
rule
  match "LEADFENCE"
  match "\"role\":\"tool\""
  text LEADFENCEDONE
rule
  match "LEADFENCE"
  tool spawn_parallel {"tasks":[{"task":"ESCAPE probe the fence","write":true}]}
rule
  match "ESCAPE"
  match "\"role\":\"tool\""
  text CHILD-FENCE-DONE
rule
  match "ESCAPE"
  tool run_terminal_command {"command":"$1"}
EOF
}

cfg() {  # $1 = shellFence value (true/false) -> writes a config to stdout
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":$1}
EOF
}

run_child_with() {  # $1 = shellFence value -> runs one write child, resets $ws first
    rm -f "$ws"/esc_*.txt
    printf 'base\n' > "$ws/reg.txt"
    cmd=$(printf '%s' "$CMD" | sed -e "s#@WS@#$ws#g")
    mm_from_cmd "$cmd" > "$tmp/f.mm"
    # $MM_PORT is only known after mm_start; write the config inside the port scope.
    mm_start_unbounded "$tmp/f.mm" "$tmp/cap"
    cfg "$1" > "$tmp/cfg.json"
    ( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
        -p "LEADFENCE run one writer" < /dev/null > "$tmp/out.$1.txt" 2>&1 )
    mm_stop
}

# ---- Fenced (default posture; shellFence:true): every escape must be REFUSED. ----
run_child_with true

for r in abs cd var script; do
    if [ -e "$ws/esc_$r.txt" ]; then
        t_fail "fenced: escape '$r' WROTE $ws/esc_$r.txt (fence did not hold)"
    else
        t_ok "fenced: escape '$r' refused (no $ws/esc_$r.txt)"
    fi
done

# The legitimate in-worktree write must still reach the live tree via merge-back:
# proves the fence did not break the child's own worktree writes.
if grep -q inside "$ws/reg.txt" 2>/dev/null; then
    t_ok "fenced: the in-worktree write merged back (reg.txt has 'inside')"
else
    t_fail "fenced: in-worktree write lost (reg.txt: $(head_bytes 80 "$ws/reg.txt"); out: $(head_bytes 200 "$tmp/out.true.txt"))"
fi

# ---- Unfenced (shellFence:false): the SAME command now LANDS an escape. This is
# the instrument proof -- without the fence the write reaches the live tree, so the
# refusals above are real. (One route suffices as the positive control.) ----
run_child_with false

if [ -e "$ws/esc_abs.txt" ]; then
    t_ok "unfenced: escape 'abs' LANDED in the live tree (instrument fires)"
else
    t_fail "unfenced: escape 'abs' did not land even with shellFence:false -- the check cannot distinguish fenced from unfenced (out: $(head_bytes 200 "$tmp/out.false.txt"))"
fi

t_done
