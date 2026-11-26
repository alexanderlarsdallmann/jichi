#!/bin/sh
# smoke: jichi on a system WITHOUT git -- what still works, what switches off,
# and what says so.
#
# WHY THIS EXISTS. Editing never needs git: read_file, write_file, edit_file,
# apply_patch and the shell tool are jichi's own code on plain files. git is
# needed by four things only: the eight git tools (registered only when
# `git rev-parse --is-inside-work-tree` succeeds, jc_tool.c), the snapshot
# store behind checkpoints, /undo and /rewind (a shadow git repo; the manager
# disables itself when `git --version` fails, jc_snapshot.c), spawn_parallel's
# write worktrees (taken from that store; without it a write task runs
# READ-ONLY, jc_tool_parallel.c), and the dirty-tree check before a dispatch.
# Until this driver nothing tested that set: the GNU/Hurd row (M767) ran once
# without git and found only that eight drivers failed instead of skipping
# (M773 added their guards), and every rig since M660 installs git on purpose.
#
# HOW. A PATH farm: a directory of symlinks to every executable on $PATH except
# `git` and `git-*` (M773 verified its guards the same way). jichi runs with
# PATH set to the farm, so its own `git` lookups fail exactly as on a system
# without git, while sh, the coreutils and every other tool are there.
#
# EACH CHECK HAS A CONTROL. The same run with git on PATH must show the
# opposite, so a check cannot pass because the fixture is broken (a turn that
# never ran also creates no checkpoint). Both workspaces are git repositories
# (`git init` by the HOST git before the runs), so the one difference between
# the halves is whether jichi can find git. On a host that has no git at all
# the farm IS the host, the no-git half is the real thing, and the controls
# are skipped with a reason in the message.
#
# Checks:
#   1  the instrument: the farm hides git and keeps sh/cat; the host has git
#   2  doctor FAILs "snapshots enabled but git is not on PATH" (control: OK)
#   3  an editing turn still edits: write_file + edit_file land, rc 0
#   4  the eight git tools are not offered to the model (control: offered)
#   5  no checkpoint store is created for the turn (control: one is)
#   6  `jichi checkpoints` says "snapshots unavailable here", exit 1
#      (control: it lists the turn's checkpoint)
#   7  a spawn_parallel WRITE task runs read-only and the lead is told so
#      ("(snapshots unavailable; ran read-only)"; control: no such note)
#   8  that task's write does not reach the workspace (control: it merges)
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

if command -v git >/dev/null 2>&1; then HAVE_GIT=1; else HAVE_GIT=0; fi
CTRL_SKIP="control skipped: no git on this host, so the farm is the host"

# --- the PATH farm --------------------------------------------------------------
# First entry on PATH wins, as in a real lookup; directories and non-executables
# are skipped. A dangling link counts as taken (test -h), so a later duplicate
# cannot replace it.
farm="$tmp/farm"
mkdir -p "$farm"
NL=0
_pf_ifs=$IFS
IFS=:
for _pf_d in $PATH; do
    IFS=$_pf_ifs
    [ -n "$_pf_d" ] && [ -d "$_pf_d" ] || continue
    for _pf_f in "$_pf_d"/*; do
        _pf_n=${_pf_f##*/}
        case $_pf_n in git|git-*) continue ;; esac
        if [ -e "$farm/$_pf_n" ] || [ -h "$farm/$_pf_n" ]; then continue; fi
        [ -f "$_pf_f" ] && [ -x "$_pf_f" ] || continue
        ln -s "$_pf_f" "$farm/$_pf_n" 2>/dev/null && NL=$((NL + 1))
    done
done
IFS=$_pf_ifs

# ng = no git (the farm), g = git (the host PATH). Separate HOMEs, so each half
# has its own ~/.jichi.d and its own checkpoint store.
hng=$(smoke_tmp); hg=$(smoke_tmp)
wng=$(smoke_tmp); wg=$(smoke_tmp)
if [ "$HAVE_GIT" -eq 1 ]; then
    (cd "$wng" && git init -q) >/dev/null 2>&1
    (cd "$wg" && git init -q) >/dev/null 2>&1
fi

# --- 1: the instrument ----------------------------------------------------------
FARM_GIT=$(env PATH="$farm" sh -c 'command -v git' 2>/dev/null)
FARM_SH=$(env PATH="$farm" sh -c 'command -v cat' 2>/dev/null)
if [ -z "$FARM_GIT" ] && [ -n "$FARM_SH" ] && [ "$NL" -gt 0 ]; then
    if [ "$HAVE_GIT" -eq 1 ]; then
        t_ok "the farm hides git ($NL links, cat at $FARM_SH, no git); the host PATH has git at $(command -v git)"
    else
        t_ok "the farm hides git ($NL links, cat at $FARM_SH); $CTRL_SKIP"
    fi
else
    t_fail "the farm is not the instrument: git='$FARM_GIT' cat='$FARM_SH' links=$NL"
fi

# --- the configs ------------------------------------------------------------------
# snapshots ON (the default outside the low-resource profile); selfReview off, so
# an edit adds no review call and the mock's request numbering is the turn's.
cfg_edit() {
    cat > "$1" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0}
EOF
}

# --- 2: doctor --------------------------------------------------------------------
# No model is needed for this line; the port is closed, so a probe of it is
# refused at once. The verdict is read from the line, never from doctor's exit
# code (it exits non-zero when it FINDS something, M660).
cfg_edit "$tmp/doc.json" 9
DNG=$(cd "$wng" && with_deadline 60 env HOME="$hng" PATH="$farm" "$BIN" \
      --config "$tmp/doc.json" doctor < /dev/null 2>&1)
DG=$(cd "$wg" && with_deadline 60 env HOME="$hg" "$BIN" \
      --config "$tmp/doc.json" doctor < /dev/null 2>&1)
D_NG=0; D_G=0
printf '%s\n' "$DNG" | grep -q 'snapshots enabled but git is not on PATH' && D_NG=1
printf '%s\n' "$DG" | grep -q 'snapshots enabled (git found)' && \
    ! printf '%s\n' "$DG" | grep -q 'git is not on PATH' && D_G=1
if [ "$D_NG" -eq 1 ] && { [ "$D_G" -eq 1 ] || [ "$HAVE_GIT" -eq 0 ]; }; then
    if [ "$HAVE_GIT" -eq 1 ]; then
        t_ok "doctor without git: 'snapshots enabled but git is not on PATH'; with git: 'snapshots enabled (git found)'"
    else
        t_ok "doctor without git: 'snapshots enabled but git is not on PATH'; $CTRL_SKIP"
    fi
else
    t_fail "doctor: no-git line=$D_NG control=$D_G; no-git: $(printf '%s\n' "$DNG" | grep -Ei 'git|snapshot' | head -n 4 | tr '\n' '|')"
fi

# --- the editing turn, once per half ---------------------------------------------
cat > "$tmp/edit.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"new.txt","content":"NEW BY THE AGENT\n"}
rule
  count 2
  tool read_file {"path":"a.txt"}
rule
  count 3
  tool edit_file {"path":"a.txt","old_string":"OLD","new_string":"EDITED"}
rule
  text EDIT_TURN_DONE
EOF
# The read before the edit is what a real model does, and edit_file refuses
# without it ("read the file before editing it") -- the driver's first draft
# skipped it and measured that refusal instead of the edit.
# run_edit WS HOME PATH CAPDIR OUT -> rc
run_edit() {
    printf 'OLD line\n' > "$1/a.txt"
    mm_start "$tmp/edit.mm" "$4"
    cfg_edit "$4.json" "$MM_PORT"
    (cd "$1" && with_deadline 60 env HOME="$2" PATH="$3" "$BIN" \
        --config "$4.json" --no-session --auto --no-stdin \
        -p "make the two edits" < /dev/null > "$5" 2>&1)
    _re_rc=$?
    mm_stop
    return $_re_rc
}
run_edit "$wng" "$hng" "$farm" "$tmp/cap-ng" "$tmp/out-ng"; RC_NG=$?
run_edit "$wg" "$hg" "$PATH" "$tmp/cap-g" "$tmp/out-g"; RC_G=$?

# What a user sees at the moment of the edit: recorded, not asserted (SNAPSHOTS.md
# calls the no-git case one of the silent ones).
echo "# the no-git turn's output names git or snapshots on $(grep -Eci 'git|snapshot' "$tmp/out-ng") line(s)"

# --- 3: the edit lands without git ----------------------------------------------
# edited WS OUT RC -> 1 when the turn ended and both edits are on disk
edited() {
    [ "$3" -eq 0 ] && grep -q 'EDIT_TURN_DONE' "$2" &&
    [ "$(cat "$1/new.txt" 2>/dev/null)" = "NEW BY THE AGENT" ] &&
    [ "$(cat "$1/a.txt" 2>/dev/null)" = "EDITED line" ] && echo 1 && return
    echo 0
}
E_NG=$(edited "$wng" "$tmp/out-ng" "$RC_NG"); E_G=$(edited "$wg" "$tmp/out-g" "$RC_G")
if [ "$E_NG" -eq 1 ] && [ "$E_G" -eq 1 ]; then
    t_ok "without git an editing turn still edits (new.txt written, a.txt edited, rc 0), as it does with git"
else
    t_fail "edits: no-git=$E_NG (rc $RC_NG, new.txt='$(cat "$wng/new.txt" 2>/dev/null)' a.txt='$(cat "$wng/a.txt" 2>/dev/null)') git=$E_G (rc $RC_G); no-git out: $(tail -n 3 "$tmp/out-ng" | tr '\n' '|')"
fi

# --- 4: the git tools are not offered ----------------------------------------------
# The first request carries the tool list; count the eight names in it.
gtools() {
    _gt_n=0
    for _gt in git_status git_diff git_log git_blame git_add git_commit git_branch git_stash; do
        grep -q "\"$_gt\"" "$1" 2>/dev/null && _gt_n=$((_gt_n + 1))
    done
    echo "$_gt_n"
}
GT_NG=$(gtools "$tmp/cap-ng/req.1"); GT_G=$(gtools "$tmp/cap-g/req.1")
HAS_TOOLS_NG=0
grep -q '"write_file"' "$tmp/cap-ng/req.1" 2>/dev/null && HAS_TOOLS_NG=1
if [ "$HAS_TOOLS_NG" -eq 1 ] && [ "$GT_NG" -eq 0 ] &&
   { [ "$GT_G" -eq 8 ] || [ "$HAVE_GIT" -eq 0 ]; }; then
    t_ok "without git the model is offered 0 of the 8 git tools (write_file is offered); with git, $GT_G of 8"
else
    t_fail "git tools offered: no-git=$GT_NG (write_file offered: $HAS_TOOLS_NG) git=$GT_G (want 0 and 8)"
fi

# --- 5: no checkpoint store ----------------------------------------------------------
nstore() { ls -A "$1/.jichi.d/checkpoints" 2>/dev/null | grep -c .; }
CS_NG=$(nstore "$hng"); CS_G=$(nstore "$hg")
if [ "$RC_NG" -eq 0 ] && [ "$CS_NG" -eq 0 ] &&
   { { [ "$RC_G" -eq 0 ] && [ "$CS_G" -ge 1 ]; } || [ "$HAVE_GIT" -eq 0 ]; }; then
    t_ok "without git the turn leaves no checkpoint store; with git it leaves $CS_G"
else
    t_fail "checkpoint stores: no-git=$CS_NG (rc $RC_NG) git=$CS_G (rc $RC_G)"
fi

# --- 6: `jichi checkpoints` says so ---------------------------------------------------
CK_NG=$(cd "$wng" && with_deadline 60 env HOME="$hng" PATH="$farm" "$BIN" \
        --config "$tmp/cap-ng.json" checkpoints < /dev/null 2>&1); CKRC_NG=$?
CK_G=$(cd "$wg" && with_deadline 60 env HOME="$hg" "$BIN" \
       --config "$tmp/cap-g.json" checkpoints < /dev/null 2>&1); CKRC_G=$?
CKL_G=$(printf '%s\n' "$CK_G" | grep -c '^ *[0-9][0-9]*  ')
if [ "$CKRC_NG" -eq 1 ] &&
   printf '%s\n' "$CK_NG" | grep -q 'snapshots unavailable here' &&
   { { [ "$CKRC_G" -eq 0 ] && [ "$CKL_G" -ge 1 ]; } || [ "$HAVE_GIT" -eq 0 ]; }; then
    t_ok "without git 'jichi checkpoints' exits 1: snapshots unavailable here; with git it lists $CKL_G checkpoint(s)"
else
    t_fail "checkpoints: no-git rc=$CKRC_NG '$(printf '%s' "$CK_NG" | head -n 1)'; git rc=$CKRC_G lines=$CKL_G"
fi

# --- the parallel write task, once per half -------------------------------------------
cat > "$tmp/par.mm" <<'EOF'
wire openai
rule
  match "TASK_P"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"p.txt","content":"PPP"}
rule
  nomatch "TASK_P"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"TASK_P: create p.txt","write":true}]}
rule
  text all done
EOF
cfg_par() {
    cat > "$1" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0,"maxParallelAgents":1,"maxSubagentIters":3,"maxToolIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF
}
# run_par WS HOME PATH CAPDIR
run_par() {
    if [ "$HAVE_GIT" -eq 1 ]; then
        (cd "$1" && printf 'seed\n' > seed.txt && git add -A &&
         git -c user.email=t@t -c user.name=t commit -qm init) >/dev/null 2>&1
    fi
    mm_start_unbounded "$tmp/par.mm" "$4"
    cfg_par "$4.json" "$MM_PORT"
    (cd "$1" && with_deadline 120 env HOME="$2" PATH="$3" "$BIN" \
        --config "$4.json" -q --no-session --auto --no-stdin \
        -p "delegate one write" < /dev/null > "$4.out" 2>&1)
    _rp_rc=$?
    mm_stop
    return $_rp_rc
}
png=$(smoke_tmp); pg=$(smoke_tmp)
hpng=$(smoke_tmp); hpg=$(smoke_tmp)
[ "$HAVE_GIT" -eq 1 ] && (cd "$png" && git init -q) >/dev/null 2>&1
[ "$HAVE_GIT" -eq 1 ] && (cd "$pg" && git init -q) >/dev/null 2>&1
run_par "$png" "$hpng" "$farm" "$tmp/pcap-ng"; PRC_NG=$?
run_par "$pg" "$hpg" "$PATH" "$tmp/pcap-g"; PRC_G=$?
RO_NG=$(cat "$tmp/pcap-ng"/req.* 2>/dev/null | grep -c 'snapshots unavailable; ran read-only')
RO_G=$(cat "$tmp/pcap-g"/req.* 2>/dev/null | grep -c 'snapshots unavailable; ran read-only')
DONE_NG=$(cat "$tmp/pcap-ng"/req.* 2>/dev/null | grep -c 'TASK_P')

# --- 7: the write task runs read-only, and the lead is told ------------------------------
if [ "$PRC_NG" -eq 0 ] && [ "$RO_NG" -ge 1 ] &&
   { [ "$RO_G" -eq 0 ] || [ "$HAVE_GIT" -eq 0 ]; }; then
    t_ok "without git the write task runs read-only and the lead's request says so ($RO_NG request(s)); with git, no such note"
else
    t_fail "read-only note: no-git=$RO_NG (rc $PRC_NG, TASK_P seen in $DONE_NG request(s)) git=$RO_G (rc $PRC_G)"
fi

# --- 8: and its write does not land ----------------------------------------------------------
if [ ! -e "$png/p.txt" ] &&
   { [ "$(cat "$pg/p.txt" 2>/dev/null)" = "PPP" ] || [ "$HAVE_GIT" -eq 0 ]; }; then
    t_ok "without git the task's p.txt never reaches the workspace; with git it merges (PPP)"
else
    t_fail "p.txt: no-git present=$([ -e "$png/p.txt" ] && echo yes || echo no) git='$(cat "$pg/p.txt" 2>/dev/null)'"
fi

t_done
