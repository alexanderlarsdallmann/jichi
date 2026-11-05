#!/bin/sh
# smoke: a resumed attempt carries the predecessor's own COMMITS, and `attempts show`
# shows them (M856, the demos drive's N30).
#
# THE DEFECT. M816's `"from":"<ref>"` cherry-picked the kept commit alone, i.e. its
# diff against its PARENT, on the belief that the parent is the base the worktree
# was created from. But M803 commits a kept state on top of the worktree's HEAD, so
# when the child committed as it went -- which the drive's worker template asks for,
# so an overflow loses nothing -- the parent is the child's last commit, and the pick
# carried only what was left uncommitted. An empty residue picked cleanly. Run 0022
# resumed two kept states that way: both successors started from the bare base, were
# told the work was "ALREADY APPLIED", and one wrote a substitute test that passed and
# was merged, then retracted. `jichi attempts show` had the same belief: it showed a
# diffstat against the parent, so a kept state whose work was all committed looked
# empty. from_resume.sh covers only a child that does not commit.
#
# THE FIX. The kept state's end record names its base (M808's `base:` line); the
# resume replays the whole range <base>..<ref>, `attempts show` diffs against that
# base, and the resume note names how many files it applied -- or says nothing was.
#
#   1  instrument: run 1's child committed its file and was kept, not merged
#   2  `jichi attempts show <ref>` lists the committed file (red: only the residue)
#   3  the resumed child's starting tree has the committed file (its own shell reads
#      it, and prints its content)
#   4  the resume note names the applied files, not a blanket "applied"
#   5  the resumed task's merge brings the committed file into the live tree
#   6  a kept state whose commits cancel out (a file added, then removed) is resumed
#      with a note that says nothing was applied -- never "ALREADY APPLIED"
#
# Markers: the committed file's content is written by arithmetic (C-$((40+2)) is
# C-42), so "C-42" appears in a request only when a shell READ it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true$1}
EOF
}

# --- run 1: the child COMMITS a file, then a verify:false gate keeps it ----------------
cat > "$tmp/r1.mm" <<'EOF'
wire openai
rule
  match "LEAD1"
  match "\"role\":\"tool\""
  text LEAD1DONE
rule
  match "LEAD1"
  tool spawn_parallel {"tasks":[{"task":"TASK_C commit as you go","write":true}]}
rule
  match "TASK_C"
  match "\"role\":\"tool\""
  text C1DONE
rule
  match "TASK_C"
  tool run_terminal_command {"command":"printf 'C-%s\\n' $((40+2)) > committed.txt && git add committed.txt && git -c user.name=c -c user.email=c@c commit -q -m 'child: committed step'"}
EOF
mm_start_unbounded "$tmp/r1.mm" "$tmp/cap1"
cfg ',"parallelVerify":true,"verify":"false"' > "$tmp/cfg1.json"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg1.json" -q --auto \
    -p "LEAD1 run one writer" < /dev/null > "$tmp/out1.txt" 2>&1 )
mm_stop

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
ref=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-verify-red/**' \
      --format='%(refname)' 2>/dev/null | head -1)
if [ -n "$ref" ] && [ ! -f "$ws/committed.txt" ] &&
   git --git-dir "$shadow" log --format=%s "$ref" 2>/dev/null | grep -q 'child: committed step'; then
    t_ok "instrument: run 1's child committed its file and was kept, not merged"
else
    t_fail "instrument: no kept ref with the child's commit (ref=${ref:-none})"
    t_fail "attempts show: skipped -- no ref"
    t_fail "resume tree: skipped -- no ref"
    t_fail "resume note: skipped -- no ref"
    t_fail "resume merge: skipped -- no ref"
    t_fail "empty resume: skipped -- no ref"
    t_done
fi

# --- 2: `attempts show` sees the committed file ------------------------------------
# Read it back with the SAME config run 1 wrote it with. jichi honours
# `snapshots` on the read side: on a <1 GB machine the lite profile is the
# DEFAULT (snapshots off), so a bare `attempts show` on such a guest reads
# "no preserved state" for a state that was preserved by an explicit
# `snapshots:true` run -- a write/read inconsistency in THIS driver, found by
# the V2f (492 MB Debian 9) stamp 2026-10-16, green on the 32 GB host. The
# sibling attempts_show.sh reads back with the capture config for the same
# reason.
show=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cfg1.json" attempts show "$ref" < /dev/null 2>&1)
if printf '%s\n' "$show" | grep -E -q '^ *committed\.txt +\| '; then
    t_ok "attempts show lists the committed file (against the base, not the parent)"
else
    t_fail "attempts show does not list the committed file (red before M856: only the
 residue): $(printf '%s' "$show" | head_bytes 300)"
fi

# --- 3-5: resume it -------------------------------------------------------------------
cat > "$tmp/r2.mm.in" <<'EOF'
wire openai
rule
  match "LEAD2"
  match "\"role\":\"tool\""
  text LEAD2DONE
rule
  match "LEAD2"
  tool spawn_parallel {"tasks":[{"task":"TASK_S continue the work","write":true,"from":"@REF@"}]}
rule
  match "TASK_S"
  match "\"role\":\"tool\""
  text S2DONE
rule
  match "TASK_S"
  tool run_terminal_command {"command":"cat committed.txt"}
EOF
sed "s#@REF@#$ref#g" "$tmp/r2.mm.in" > "$tmp/r2.mm"
mm_start_unbounded "$tmp/r2.mm" "$tmp/cap2"
cfg "" > "$tmp/cfg2.json"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg2.json" -q --auto \
    -p "LEAD2 resume it" < /dev/null > "$tmp/out2.txt" 2>&1 )
mm_stop
FU=$(grep -l 'TASK_S' "$tmp"/cap2/req.* 2>/dev/null | xargs grep -l '"role":"tool"' 2>/dev/null |
     xargs grep -L 'Parallel agents' 2>/dev/null | head -1)
if [ -n "$FU" ] && grep -q 'C-42' "$FU"; then
    t_ok "the resumed child's starting tree has the committed file"
else
    t_fail "the resumed child's tree lacks the committed file (red before M856: the pick
 carried only the residue): $(tail -c 300 "${FU:-/dev/null}" 2>/dev/null)"
fi
FIRST=$(grep -l 'TASK_S' "$tmp"/cap2/req.* 2>/dev/null | xargs grep -L '"role":"tool"' 2>/dev/null | head -1)
if [ -n "$FIRST" ] && grep -q '1 file(s) -- are ALREADY APPLIED' "$FIRST"; then
    t_ok "the resume note names how many files it applied"
else
    t_fail "the resume note does not name the applied files: $(grep -o '\[resume\][^\\]*' "${FIRST:-/dev/null}" 2>/dev/null | head -1)"
fi
if grep -q 'C-42' "$ws/committed.txt" 2>/dev/null; then
    t_ok "the resumed task's merge brings the committed file into the live tree"
else
    t_fail "the live tree lacks the committed file after the resumed task merged: $(ls "$ws")"
fi

# --- 6: commits that cancel out -- the note must not claim an application ---------
before=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/**' --format='%(refname)' 2>/dev/null)
cat > "$tmp/r3.mm" <<'EOF'
wire openai
rule
  match "LEAD3"
  match "\"role\":\"tool\""
  text LEAD3DONE
rule
  match "LEAD3"
  tool spawn_parallel {"tasks":[{"task":"TASK_N commit, then revert","write":true}]}
rule
  match "TASK_N"
  match "\"role\":\"tool\""
  text N3DONE
rule
  match "TASK_N"
  tool run_terminal_command {"command":"printf 'tmp\\n' > undo.txt && git add undo.txt && git -c user.name=c -c user.email=c@c commit -q -m add && git rm -q undo.txt && git -c user.name=c -c user.email=c@c commit -q -m remove"}
EOF
mm_start_unbounded "$tmp/r3.mm" "$tmp/cap3"
cfg ',"parallelVerify":true,"verify":"false"' > "$tmp/cfg3.json"   # this mock's port
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg3.json" -q --auto \
    -p "LEAD3 run one writer" < /dev/null > "$tmp/out3.txt" 2>&1 )
mm_stop
ref3=""
for r in $(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/**' --format='%(refname)' 2>/dev/null); do
    case "$before" in *"$r"*) ;; *) ref3="$r" ;; esac
done
cat > "$tmp/r4.mm.in" <<'EOF'
wire openai
rule
  match "LEAD4"
  match "\"role\":\"tool\""
  text LEAD4DONE
rule
  match "LEAD4"
  tool spawn_parallel {"tasks":[{"task":"TASK_E continue","write":true,"from":"@REF@"}]}
rule
  match "TASK_E"
  text E4DONE
EOF
sed "s#@REF@#$ref3#g" "$tmp/r4.mm.in" > "$tmp/r4.mm"
mm_start_unbounded "$tmp/r4.mm" "$tmp/cap4"
cfg "" > "$tmp/cfg4.json"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg4.json" -q --auto \
    -p "LEAD4 resume it" < /dev/null > "$tmp/out4.txt" 2>&1 )
mm_stop
FIRST4=$(grep -l 'TASK_E' "$tmp"/cap4/req.* 2>/dev/null | xargs grep -L 'Parallel agents' 2>/dev/null | head -1)
if [ -n "$ref3" ] && [ -n "$FIRST4" ] &&
   grep -q 'changed no files against the base it started from, so nothing' "$FIRST4" &&
   ! grep -q 'ALREADY APPLIED' "$FIRST4"; then
    t_ok "a kept state whose commits cancel out is resumed with a note that nothing was applied"
else
    t_fail "the empty resume's note is wrong, or no state was kept (ref3=${ref3:-none}):
 $(grep -o '\[resume\][^\\]*' "${FIRST4:-/dev/null}" 2>/dev/null | head -1)"
fi

t_done
