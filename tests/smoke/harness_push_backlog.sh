#!/bin/sh
# smoke: `jichi-harness push backlog --config <cfg> TIP...` -- push a refused
# backlog of the TEAM branch in steps, oldest tip first, from the host-only
# push.git (M1147, batch H7; the C89 port of the 2026-10-27 hardening-host
# drive's push-backlog.sh, behaviour B3). Each TIP is a commit on
# push.git's refs/heads/<prefix>/main; each step pushes it as a
# FAST-FORWARD of the remote's <prefix>/main (the team push machinery with
# the tip's refset, key scan AND size check both on, never --force),
# re-fetches after the step, and stops at the first refusal: the over-limit
# case is SPLIT, not raised -- a TIP whose delta is over 20 MiB is refused
# by the size check, the earlier valid step has already landed, `done` is
# NOT printed, and the command exits 1.
#
# M1147 (H7). Design note (the contract, behaviour list B3):
#   docs/internal/drive/2026-11-05-harness-port/notes/M1147.md
# Ported reference (read line by line; this driver holds B3):
#   docs/internal/drive/2026-10-27-hardening-host/push-backlog.sh
#
# THE RED THIS IS HELD BY. On the tree before this batch the top-level verb
# `push` does not exist in jichi-harness: every check that runs a push
# subcommand gets `unknown command 'push'` and exit 2. All six cells run the
# verb and are RED for exactly that reason (rc 2 instead of 0/1/2, and the
# push.log lines they assert -- `backlog: ...` -- are absent).
#
# WHAT IS CHECKED -- 10 cells, one t_ok/t_fail each:
#   1. usage: `push backlog` with no TIP -> exit 2, a `usage:` line on
#      stderr (the port: `usage: push backlog TIP...`).
#   2. a TIP not on push.git's local main -> exit 1, a
#      `backlog: <t> is not on push.git's <prefix>/main` line on push.log,
#      nothing pushed (the remote's main unchanged).
#   3. a TIP that is NOT a fast-forward of the remote's main (the remote
#      holds a diverged line ahead of it) -> exit 1, a
#      `backlog: <t> is not a fast-forward of the remote's <prefix>/main`
#      line, nothing pushed.
#   4. the happy step: a TIP ahead of the remote lands -- exit 0, the
#      remote's main moves to the TIP, the re-fetch happens (push.git's
#      tracking ref refs/remotes/origin/<prefix>/main is updated to the
#      TIP), push.log ends the step with `backlog: the remote's ... is
#      <short>` and the run with `backlog: done`.
#   5. the over-limit step: two steps in one run -- the first TIP is
#      valid and LANDS (the remote's main moves to it), the second TIP's
#      delta carries a 21 MiB blob: the size check refuses it -> exit 1,
#      the `backlog: the push of <t> failed` line, a `stop`, and NO
#      `backlog: done` -- the split, not the raise (the operator's choice
#      in push-backlog.sh's header: stepwise pushes with both checks on).
#   6. a key in the step's delta refuses: a second tip whose added blob
#      carries the canary token -> the key scan refuses -> exit 1, a
#      `backlog: the push of <t> failed` line, stop, no `done`, the remote
#      unchanged.
#   7. the REALISTIC two-TIP pin (SV:0238): local main at X; TIPs t1 < t2
#      < X (all on the local main, t1 older, t2 a child of t1); the remote
#      (the stored-URL standin) at the BASE (behind t1). `push backlog
#      <t1> <t2>`: BOTH steps land (exit 0, `backlog: done`, the remote's
#      main ends at t2), and push.git's local main is STILL at X -- the
#      port never moves it. GREEN on the current code: it pins the
#      reference's operator invariant (the local main sits at the running
#      tip; push.sh's own mirror fetch keeps it there), and a backlog
#      entirely in the local main's past completes. The reviewer's P1
#      scenario (a local main BEHIND the tips) is a TIP push.git does not
#      hold, and check 1 refusing it is the reference's correct behaviour.
#   8. the DESCENDANT-TIP refusal (SV:0339.2, the kept-but-rejected P1 fix's
#      tooth): push.git's local <prefix>/main at X; a TIP c that is a CHILD
#      of X but lives on ANOTHER ref in push.git (the tip of the side branch
#      refs/heads/<prefix>/work/a, never on main); the remote at X (behind c).
#      `push backlog c` must be REFUSED exit 1 with the
#      `backlog: <c> is not on push.git's <prefix>/main: stop` line -- the
#      reference's check 1 asks that a TIP be ON the local main, and a
#      DESCENDANT of it is not on it. The refusal is BEFORE any push:
#      push.git's local main is STILL at X (the port never moves it) and the
#      remote is UNTOUCHED (c did not land). GREEN on the current code: the
#      tooth guards against the kept-but-rejected P1 fix (67c0fe8 in
#      /tmp/kept-b12, the helper backlog_local_main_advance), which advances
#      the local main to the NEWEST backlog tip before the stepwise loop --
#      on a single-TIP run like this one the newest IS c, so that fix moves
#      local main onto c, check 1 passes, and c LANDS; this cell is where the
#      fix goes not-ok (red-first proven in the M1147 descendant-tip-cell
#      report).
#   9. the 9-TIP happy run (M1151 defect b, the silent under-push by
#      effect): 9 ascending empty commits on the team's main, the remote +
#      push.git's tracking ref seeded at the base, `push backlog <t1> ... <t9>`
#      (oldest first). The reference push-backlog.sh's `for t in "$@"` is
#      unbounded -- every TIP is a step. Assert: exit 0, the remote's main
#      ends at t9 (NOT t8 -- the pre-fix `tips[8]` cap drops the 9th positional,
#      pushes 8 steps, and still prints `backlog: done` and exits 0, a silent
#      under-push), push.log carries NINE `backlog: push` lines and
#      `backlog: done`.
#   10. the 10-TIP run (M1151 defect b, the `push_extra[8]` cap by effect):
#      same shape, 10 ascending tips, `push backlog <t1> ... <t10>`. Assert:
#      exit 0, the remote ends at t10, TEN `backlog: push` lines, `done`.
#      RED on the unfixed tree: the 10th positional fills main's
#      `push_extra[8]` and is refused LOUDLY -- `jichi-harness: unexpected
#      argument '<t10>' after command 'push'`, exit 2, nothing pushed.
#
# THE FIXTURE. Under $SMOKE_TMP: an init-built rehearsal (src ->
# `init rehearsal`, the team/mirror/standin/recs layout; mirror fetch
# populates the mirror) so the team push machinery runs against real local
# repos -- no network, no podman. The host's push.git is the reference's
# shape: a BARE repo (a --bare clone of the mirror, so the team's objects
# and refs/heads are present) plus the tracking ref
# refs/remotes/origin/<prefix>/main seeded from standin.git (the remote's
# copy). The config's host block names pushHome (the dir holding push.git,
# recs-push.git, push.log, push-tick.{out,state}, push.lock) and image.
# The red tree's loader parses point-wise and ignores the unknown host
# block, which is why the red lands on `unknown command 'push'`, not on a
# config refusal. modelKeyFile carries a random hex canary; gitTokenFile
# carries a DISTINCT fake 40-hex canary (never a real key -- a leaked
# canary can be attributed to this driver by its name).
#
# Runs the tree's binary by path ($HBIN, the third_program idiom), never a
# PATH-resolved jichi-harness, and -- for smoke_lint check 5 -- also runs
# the tree's "$BIN" once. No timeout wrapper (local repos); git children
# carry -c user.name/-c user.email (M2-neutral).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 10
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: the fixture git children are neutralised, but $BIN must
# be exercised so the lint sees the driver validate the just-built tree.
"$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# Neutralise ambient harness loader settings (env wins over the config) and
# the operator's ambient git config for the fixture repos.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

PFX=drive/x-test
TOKEN_CANARY="b2e8a4d7f1c693580e2b4a7d1f8c3e65a0d9b2e8"
KEYCANARY=$(od -An -N20 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n'); [ -n "$KEYCANARY" ] || KEYCANARY="canarykey$$_"

# A fixture failure emits one t_fail per cell so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 10 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

if [ ! -x "$HBIN" ]; then
    fixture_fail "the harness binary $HBIN is not built (run `make jichi-harness` first)"
fi

# --- the fixture: rehearsal + the host's push.git (bare, the reference shape)
SRC="$tmp/src"
git init -q "$SRC" || fixture_fail "init src"
git -C "$SRC" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: plan base" || fixture_fail "src commit"
git -C "$SRC" branch "$PFX/plan" || fixture_fail "src plan branch"

printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' \
    > "$tmp/jichi" || fixture_fail "fake jichi"
chmod 755 "$tmp/jichi"

printf '%s\n' "$KEYCANARY" > "$tmp/kfile"; chmod 600 "$tmp/kfile"
printf '%s\n' "$TOKEN_CANARY" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

PH="$tmp/pushhome"; mkdir -p "$PH" || fixture_fail "pushHome"

cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"modelKeyFile":"$tmp/kfile","gitTokenFile":"$tmp/tokfile",
"drive":{"branchPrefix":"$PFX","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"host":{"pushHome":"$PH","image":"jichi-drive:test"}}
EOF

"$HBIN" init --config "$tmp/cfg.json" rehearsal "$tmp/dir" \
    --from "$SRC" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i" 2> "$tmp/i.err" || \
    fixture_fail "init rehearsal failed: $(tr '\n' ' ' < "$tmp/i.err" | head_bytes 200)"
git -C "$tmp/dir/team" branch -q "$PFX/work/a" "$PFX/main" \
    || fixture_fail "team work branch"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/f" 2> "$tmp/f.err" || \
    fixture_fail "mirror fetch failed: $(tr '\n' ' ' < "$tmp/f.err" | head_bytes 200)"

# The remote (standin.git) is seeded with the base main, so push.git's
# tracking ref has a real remote to be ahead of.
git -C "$tmp/dir/mirror.git" push -q "$tmp/dir/standin.git" \
    "refs/heads/$PFX/main:refs/heads/$PFX/main" \
    < /dev/null > "$tmp/seed" 2>&1 || fixture_fail "seed standin: $(tr '\n' ' ' < "$tmp/seed" | head_bytes 160)"

# push.git: a BARE clone of the mirror (the team's objects + refs/heads),
# the tracking ref refs/remotes/origin/<prefix>/main from the standin.
# `init rehearsal` (M1147 B1.1) already laid push.git out as an EMPTY bare
# repo under the host push home, so clone to .new and swap it in -- the same
# idiom the per-cell re-clones below use.
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" \
    || fixture_fail "bare clone push.git"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" \
    || fixture_fail "bare clone push.git swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" \
    "$(git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$PFX/main")" \
    || fixture_fail "tracking ref seed"

LOG="$PH/push.log"
remote_main() { git -C "$tmp/dir/standin.git" rev-parse "refs/heads/$PFX/main" 2>/dev/null; }
track_main() { git -C "$PH/push.git" rev-parse "refs/remotes/origin/$PFX/main" 2>/dev/null; }
fetch_mirror() { "$HBIN" mirror fetch --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/fm" 2>&1; }
# runlog MARK -- the push.log lines of THIS run only (push.log is appended
# across cells, so a `done` from an earlier run must not leak into a later
# cell's count; MARK is a line the driver stamps into push.log just before
# the run).
runlog() {
    grep -v "^$" "$LOG" 2>/dev/null | awk -v m="$1" 'seen {print} $0 ~ m {seen=1}'
}

# --- 1: usage -- `push backlog` with no TIP ----------------------------------
"$HBIN" push backlog --config "$tmp/cfg.json" \
    < /dev/null > "$tmp/b1" 2> "$tmp/b1.err"
rc1=$?
if [ "$rc1" -ne 2 ]; then
    t_fail "1: \`push backlog\` with no TIP exited $rc1 (want 2) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b1.err" | head_bytes 200)"
elif ! grep -qF "usage:" "$tmp/b1.err"; then
    t_fail "1: the no-TIP refusal has no \`usage:\` line on stderr (want \`usage: push backlog TIP...\`): $(tr '\n' ' ' < "$tmp/b1.err" | head_bytes 200)"
else
    t_ok "1: usage -- \`push backlog\` with no TIP is exit 2 with a \`usage:\` line on stderr"
fi

# --- 2: a TIP not on push.git's local main ------------------------------------
# An external commit (no common history with the local main): the reference's
# step-1 refusal, BEFORE any push.
side="$tmp/side"
git init -q "$side" || fixture_fail "c2 init side"
git -C "$side" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "an external commit" || fixture_fail "c2 side commit"
ext=$(git -C "$side" rev-parse HEAD) || fixture_fail "c2 side rev-parse"
rm_before2=$(remote_main)
"$HBIN" push backlog --config "$tmp/cfg.json" "$ext" \
    < /dev/null > "$tmp/b2" 2> "$tmp/b2.err"
rc2=$?
rm_after2=$(remote_main)
if [ "$rc2" -ne 1 ]; then
    t_fail "2: a TIP not on push.git's local main was not refused (exit $rc2, want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b2.err" | head_bytes 200)"
elif ! grep -qF "is not on" "$LOG" 2>/dev/null; then
    t_fail "2: the refusal has no \`backlog: <t> is not on push.git's <prefix>/main\` line in push.log: $(tr '\n' '|' < "$LOG" 2>/dev/null | head_bytes 240)"
elif [ "$rm_before2" != "$rm_after2" ]; then
    t_fail "2: the not-on-main refusal CHANGED the remote's main ($rm_before2 -> $rm_after2) -- it must stop before any push"
else
    t_ok "2: a TIP not on push.git's local main is refused exit 1 with the \`is not on\` line in push.log, nothing pushed (the remote's main unchanged)"
fi

# --- 3: a TIP that is not a fast-forward of the remote's main ------------------
# A DIVERGED line: base + one commit, pushed straight to the remote (the
# operator's stand for a remote that advanced elsewhere); the TIP = base
# (on the local main, but the remote is no longer its ancestor).
base=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c3 base"
div="$tmp/div"
git init -q "$div" || fixture_fail "c3 init div"
git -C "$div" fetch -q "$tmp/dir/standin.git" "refs/heads/$PFX/main:refs/heads/diverged" 2>/dev/null || \
    fixture_fail "c3 fetch base: standin empty"
# git 2.53.0: a `git fetch` into a fresh repo does NOT move HEAD to the
# fetched branch (HEAD stays on the default branch), so the diverge commit
# below would land on `master` and `diverged` would still == base -- the remote
# never advances and cell 3 cannot exercise its not-a-FF refusal. Point HEAD at
# the fetched ref explicitly before the commit (the remote genuinely advances
# past base). The port's own assertion is unchanged.
git -C "$div" checkout -q diverged || fixture_fail "c3 checkout diverged"
git -C "$div" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "the remote's own line, ahead of the TIP" || fixture_fail "c3 div commit"
git -C "$div" push -q "$tmp/dir/standin.git" "diverged:refs/heads/$PFX/main" \
    < /dev/null > "$tmp/b3seed" 2>&1 || fixture_fail "c3 diverge push: $(tr '\n' ' ' < "$tmp/b3seed" | head_bytes 160)"
"$HBIN" push backlog --config "$tmp/cfg.json" "$base" \
    < /dev/null > "$tmp/b3" 2> "$tmp/b3.err"
rc3=$?
rm_after3=$(remote_main)
if [ "$rc3" -ne 1 ]; then
    t_fail "3: a TIP that is not a fast-forward of the remote's main was not refused (exit $rc3, want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b3.err" | head_bytes 200)"
elif ! grep -qF "is not a fast-forward" "$LOG" 2>/dev/null; then
    t_fail "3: the refusal has no \`backlog: <t> is not a fast-forward of the remote's <prefix>/main\` line in push.log: $(tr '\n' '|' < "$LOG" 2>/dev/null | head_bytes 240)"
elif [ "$rm_after3" = "$base" ]; then
    t_fail "3: the not-a-FF refusal PUSHED the TIP (the remote's main moved to $base) -- it must stop before any push"
else
    t_ok "3: a TIP that is not a fast-forward of the remote's main (the remote holds a diverged line ahead of it) is refused exit 1 with the \`is not a fast-forward\` line, nothing pushed"
fi
# Reset the remote to $base for cell 4 (the driver's own instrument, a direct
# push to the bare stand-in; a backwards move needs --force, which is the
# DRIVER's setup -- the port's own never---force is what the cells hold).
git -C "$div" push -q --force "$tmp/dir/standin.git" \
    "$base:refs/heads/$PFX/main" < /dev/null > "$tmp/b3reset" 2>&1 \
    || fixture_fail "c3 remote reset: $(tr '\n' ' ' < "$tmp/b3reset" | head_bytes 160)"

# --- 4: the happy step (a TIP ahead of the remote lands, the re-fetch) --------
# The local main + tracking ref advance together: the mirror gains a commit,
# the driver re-clones push.git from the mirror (the operator's own step),
# and pushes the TIP. The remote's main moves to the TIP, the re-fetch
# updates push.git's tracking ref to the TIP, and the run ends `backlog:
# done`, exit 0.
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: the backlog tip" || fixture_fail "c4 commit"
fetch_mirror || fixture_fail "c4 mirror fetch"
tip4=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c4 rev-parse"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c4 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c4 re-clone swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c4 tracking seed"
printf 'SMOKE-RUN-4\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" "$tip4" \
    < /dev/null > "$tmp/b4" 2> "$tmp/b4.err"
rc4=$?
rm_after4=$(remote_main)
tr_after4=$(track_main)
if [ "$rc4" -ne 0 ]; then
    t_fail "4: the happy backlog step (a TIP ahead of the remote) exited $rc4 (want 0) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b4.err" | head_bytes 200)"
elif [ "$rm_after4" != "$tip4" ]; then
    t_fail "4: the step exited 0 but the remote's main did not move to the TIP (remote=$rm_after4 tip=$tip4)"
elif [ "$tr_after4" != "$tip4" ]; then
    t_fail "4: the re-fetch did not happen: push.git's tracking ref is $tr_after4, not the TIP $tip4 (the step must re-fetch after the push)"
elif ! runlog SMOKE-RUN-4 | grep -qF "the remote's" ; then
    t_fail "4: push.log has no \`backlog: the remote's <prefix>/main is <short>\` line for the step: $(runlog SMOKE-RUN-4 | head_bytes 240)"
elif ! runlog SMOKE-RUN-4 | grep -qF "backlog: done"; then
    t_fail "4: push.log has no \`backlog: done\` line at the end of the run: $(runlog SMOKE-RUN-4 | head_bytes 240)"
else
    t_ok "4: the happy step -- a TIP ahead of the remote lands (exit 0, the remote's main moves to the TIP), the re-fetch updates push.git's tracking ref, and push.log carries the \`the remote's ...\` line and \`backlog: done\`"
fi

# --- 5: the over-limit step (the split, not the raise) ------------------------
# Two steps in one run: the FIRST tip (a small commit) is valid and LANDS;
# the SECOND tip's delta carries a 21 MiB blob -> the size check refuses that
# step -> exit 1, the \`backlog: the push of <t> failed\` line, a stop, and
# NO \`backlog: done\`. The first tip already landed: the split is the point
# (the operator's choice over a raised limit).
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: the valid step" || fixture_fail "c5 commit1"
fetch_mirror || fixture_fail "c5 fetch1"
tip5a=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c5 rev-parse a"
dd if=/dev/urandom of="$tmp/dir/team/over.bin" bs=1048576 count=21 2>/dev/null \
    || fixture_fail "c5 dd"
git -C "$tmp/dir/team" add over.bin
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: the over-limit step (a 21 MiB blob)" || fixture_fail "c5 commit2"
fetch_mirror || fixture_fail "c5 fetch2"
tip5b=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c5 rev-parse b"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c5 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c5 re-clone swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c5 tracking seed"
printf 'SMOKE-RUN-5\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" "$tip5a" "$tip5b" \
    < /dev/null > "$tmp/b5" 2> "$tmp/b5.err"
rc5=$?
rm_after5=$(remote_main)
if [ "$rc5" -ne 1 ]; then
    t_fail "5: the over-limit two-step run did not exit 1 (exit $rc5, want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b5.err" | head_bytes 200)"
elif ! runlog SMOKE-RUN-5 | grep -qF "the push of" || ! runlog SMOKE-RUN-5 | grep -qF "stop"; then
    t_fail "5: the refused step has no \`backlog: the push of <t> failed ...\` + \`stop\` lines in push.log: $(runlog SMOKE-RUN-5 | head_bytes 240)"
elif runlog SMOKE-RUN-5 | grep -qF "backlog: done"; then
    t_fail "5: the run printed \`backlog: done\` although the second step was refused -- a run that stops at the first refusal never finishes: $(runlog SMOKE-RUN-5 | head_bytes 240)"
elif [ "$rm_after5" != "$tip5a" ]; then
    t_fail "5: the SPLIT did not hold: the remote's main is $rm_after5 -- the first (valid) step must have LANDED at $tip5a even though the second was refused"
else
    t_ok "5: the over-limit step -- the first TIP lands (the remote's main is the first tip), the second TIP's 21 MiB delta is refused by the size check (exit 1, the \`the push of <t> failed\` line, a \`stop\`), and NO \`backlog: done\`: the split, not the raise"
fi

# --- 6: a key in the step's delta refuses (canary, exit 1, stop) --------------
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: a clean step" || fixture_fail "c6 commit0"
fetch_mirror || fixture_fail "c6 fetch0"
printf 'secret=%s\n' "$TOKEN_CANARY" > "$tmp/dir/team/leak-backlog.txt"
git -C "$tmp/dir/team" add leak-backlog.txt
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q -m "team: a canary-bearing step" || fixture_fail "c6 commit1"
fetch_mirror || fixture_fail "c6 fetch1"
tip6=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c6 rev-parse"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c6 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c6 re-clone swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c6 tracking seed"
rm_before6=$(remote_main)
printf 'SMOKE-RUN-6\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" "$tip6" \
    < /dev/null > "$tmp/b6" 2> "$tmp/b6.err"
rc6=$?
rm_after6=$(remote_main)
# The canary must not leak into push.log (the secret rule: paths, not content).
if [ "$rc6" -ne 1 ]; then
    t_fail "6: a step whose delta carries the canary token was not refused (exit $rc6, want 1) -- on the red tree the \`push\` verb does not exist: $(tr '\n' ' ' < "$tmp/b6.err" | head_bytes 200)"
elif ! runlog SMOKE-RUN-6 | grep -qF "the push of" || ! runlog SMOKE-RUN-6 | grep -qF "stop"; then
    t_fail "6: the key-refused step has no \`backlog: the push of <t> failed ...\` + \`stop\` lines in push.log: $(runlog SMOKE-RUN-6 | head_bytes 240)"
elif runlog SMOKE-RUN-6 | grep -qF "backlog: done"; then
    t_fail "6: the run printed \`backlog: done\` although the step was refused by the key scan"
elif [ "$rm_before6" != "$rm_after6" ]; then
    t_fail "6: the key-refused step CHANGED the remote's main ($rm_before6 -> $rm_after6) -- the refusal must fire before any push"
elif runlog SMOKE-RUN-6 | grep -qF "$TOKEN_CANARY"; then
    t_fail "6: the refusal PRINTED the canary content into push.log (only the path may be named, the secret rule)"
else
    t_ok "6: a key in the step's delta refuses -- the canary-bearing TIP is refused exit 1 by the key scan (the \`the push of <t> failed\` line, a \`stop\`), no \`done\`, the remote unchanged, and the canary never printed"
fi

# --- 7: the REALISTIC two-TIP pin (SV:0238: both tips in the local main's past) ---
# The reference's operator invariant, pinned: local main at X; tips t1 <
# t2 < X (both on the local main, t1 the older, t2 a child of t1); the
# remote (the stored-URL standin) at the BASE (behind t1). Both tips are
# genuine fast-forwards of the remote, and BOTH pass check 1 (ancestor of
# the local main, which holds for every tip of a real backlog -- push.sh's
# own step-2 mirror fetch keeps the local main at the running tip), so
# `push backlog <t1> <t2>` must land BOTH steps: the remote's main ends at
# t2, exit 0, `backlog: done`, and push.git's local main is STILL at X
# (the port never moves it -- the operator's job, not the port's). This
# cell is GREEN on the current code: the reviewer's P1 "local main behind
# the tips" is a TIP push.git does not hold, and check 1 refusing it is
# correct (a fix that made such a run complete would make check 1 vacuous
# AND rewind the local main in the normal case).
base7=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c7 base"
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: t1 of the two-TIP pin" || fixture_fail "c7 commit1"
fetch_mirror || fixture_fail "c7 fetch1"
t7a=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c7 rev-parse a"
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: t2 of the two-TIP pin (a child of t1)" || fixture_fail "c7 commit2"
fetch_mirror || fixture_fail "c7 fetch2"
t7b=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c7 rev-parse b"
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: X, the local main past both tips" || fixture_fail "c7 commit3"
fetch_mirror || fixture_fail "c7 fetch3"
X7=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c7 rev-parse X"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c7 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c7 re-clone swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c7 tracking seed"
# The remote back to the BASE (behind t1): the DRIVER's own instrument, a
# backwards move to the bare stand-in, so --force is the SETUP's, never the
# port's (the port's never---force is what the cells hold, as in cell 3).
git -C "$tmp/dir/mirror.git" push -q --force "$tmp/dir/standin.git" \
    "$base7:refs/heads/$PFX/main" < /dev/null > "$tmp/c7seed" 2>&1 \
    || fixture_fail "c7 remote reset: $(tr '\n' ' ' < "$tmp/c7seed" | head_bytes 160)"
# Shape sanity (a fixture error, not an assertion on the port): local main
# at X, t1 < t2 < X, t1 a child of t2's parent line.
[ "$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main")" = "$X7" ] || fixture_fail "c7 shape X"
git -C "$PH/push.git" merge-base --is-ancestor "$t7a" "refs/heads/$PFX/main" || fixture_fail "c7 shape t1-on-main"
git -C "$PH/push.git" merge-base --is-ancestor "$t7b" "refs/heads/$PFX/main" || fixture_fail "c7 shape t2-on-main"
git -C "$PH/push.git" merge-base --is-ancestor "$t7a" "$t7b" || fixture_fail "c7 shape t1< t2"
[ "$(remote_main)" = "$base7" ] || fixture_fail "c7 shape remote-at-base"
local_main_before7=$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main")
rm_before7=$(remote_main)
printf 'SMOKE-RUN-7\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" "$t7a" "$t7b" \
    < /dev/null > "$tmp/b7" 2> "$tmp/b7.err"
rc7=$?
rm_after7=$(remote_main)
local_main_after7=$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main" 2>/dev/null)
tr_after7=$(track_main)
if [ "$rc7" -ne 0 ]; then
    t_fail "7: the realistic two-TIP pin (both tips in the local main's past, the remote behind both) did not complete (exit $rc7, want 0): $(tr '\n' ' ' < "$tmp/b7.err" | head_bytes 200)"
elif [ "$rm_after7" != "$t7b" ]; then
    t_fail "7: the two-TIP run exited 0 but the remote's main is $rm_after7 -- the second step must have landed at t2 ($t7b)"
elif [ "$tr_after7" != "$t7b" ]; then
    t_fail "7: the re-fetch after the last step left push.git's tracking ref at $tr_after7, not the landed t2 ($t7b)"
elif [ "$local_main_after7" != "$local_main_before7" ] || [ "$local_main_before7" != "$X7" ]; then
    t_fail "7: push.git's local main MOVED in the run ($local_main_before7 -> $local_main_after7, want it still at X $X7) -- the port never moves the local main"
elif ! runlog SMOKE-RUN-7 | grep -qF "backlog: done"; then
    t_fail "7: the run completed but push.log has no \`backlog: done\`: $(runlog SMOKE-RUN-7 | head_bytes 240)"
else
    t_ok "7: the realistic two-TIP pin (SV:0238) -- both tips are in the local main's past and the remote is behind both: BOTH steps land (exit 0, the remote's main ends at t2, the re-fetch to t2, \`backlog: done\`), and push.git's local main is STILL at X -- the port never moves it; the reference's operator invariant, held"
fi

# --- 8: the DESCENDANT-TIP refusal (SV:0339.2; the kept-but-rejected P1 tooth) ---
# The reference's check 1 (is the TIP ON push.git's local <prefix>/main?)
# refuses a DESCENDANT of the local main that lives on ANOTHER ref: a TIP
# that the local main can reach is not "on" it. The shape: the local main at
# X; a commit c a CHILD of X that is the tip of the side branch
# refs/heads/<prefix>/work/a (a work branch, so the mirror fetch's work
# wildcard carries it into push.git's store); the remote (the stored-URL
# standin) at X (behind c). `push backlog c` is refused exit 1 BEFORE any
# push, with the `backlog: <c> is not on push.git's <prefix>/main: stop`
# line; push.git's local main is STILL at X (the port never moves it) and
# the remote is UNTOUCHED (c did not land).
#
# WHY THIS CELL IS A TOOTH. The kept-but-rejected P1 fix (67c0fe8 in
# /tmp/kept-b12) adds backlog_local_main_advance: before the stepwise loop
# the local main is advanced, forward-only, to the NEWEST backlog tip. On a
# single-TIP run the newest tip IS the argument -- here c, a descendant of X
# the local main can reach -- so the fix moves push.git's local main ONTO c,
# check 1 passes, and c LANDS on the remote. This cell is exactly where that
# fix goes not-ok (the red-first: cell 8 ok on the current code, not-ok with
# 67c0fe8 applied, ok again after the restore -- the M1147 descendant-tip-cell
# report). The port in this tree never moves local main, so the cell is
# GREEN: check 1's refusal stands.
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: X8, the local main (the side ref c's parent)" || fixture_fail "c8 commit X"
fetch_mirror || fixture_fail "c8 fetch X"
X8=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c8 rev-parse X"
# c: a CHILD of X8, on the SIDE branch (work/a), never on the local main. The
# driver's own instrument (the team repo + the mirror's work wildcard + the
# re-clone). work/a was cut at the base main (the fixture's `branch -q
# work/a main`), still far BEHIND X8, so it is first moved forward,
# --ff-only (a forward-only move; the driver's setup, as the remote seeds in
# cells 3/7), so c's parent IS X8 (the shape the cell needs).
git -C "$tmp/dir/team" checkout -q "$PFX/work/a" || fixture_fail "c8 checkout side"
git -C "$tmp/dir/team" merge -q --ff-only "$PFX/main" || fixture_fail "c8 side ff main"
git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: c, a child of X8 on the side branch (not on main)" || fixture_fail "c8 commit c"
C8=$(git -C "$tmp/dir/team" rev-parse HEAD) || fixture_fail "c8 rev-parse c"
git -C "$tmp/dir/team" checkout -q "$PFX/main" || fixture_fail "c8 checkout back"
fetch_mirror || fixture_fail "c8 fetch c"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c8 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c8 re-clone swap"
# The remote (the standin) at X8 (behind c): a FORWARD FF push from the
# mirror (the remote is at t7b from cell 7, an ancestor of X8) -- the
# driver's own instrument, as in cells 3/7.
git -C "$tmp/dir/mirror.git" push -q "$tmp/dir/standin.git" \
    "$X8:refs/heads/$PFX/main" < /dev/null > "$tmp/c8seed" 2>&1 \
    || fixture_fail "c8 remote seed: $(tr '\n' ' ' < "$tmp/c8seed" | head_bytes 160)"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$X8" || fixture_fail "c8 tracking seed"
# Shape sanity (a fixture error, not an assertion on the port): local main at
# X8; c a CHILD of X8 (X8 reaches c); c NOT on the local main (c does not
# reach X8 -- the shape check 1 refuses); the remote at X8.
[ "$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main")" = "$X8" ] || fixture_fail "c8 shape X"
git -C "$PH/push.git" merge-base --is-ancestor "$X8" "$C8" || fixture_fail "c8 shape c-child-of-X"
if git -C "$PH/push.git" merge-base --is-ancestor "$C8" "refs/heads/$PFX/main"; then
    fixture_fail "c8 shape: the side ref c IS on the local main (want it OFF -- the descendant shape)"
fi
[ "$(remote_main)" = "$X8" ] || fixture_fail "c8 shape remote-at-X"
local_main_before8=$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main")
rm_before8=$(remote_main)
printf 'SMOKE-RUN-8\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" "$C8" \
    < /dev/null > "$tmp/b8" 2> "$tmp/b8.err"
rc8=$?
rm_after8=$(remote_main)
local_main_after8=$(git -C "$PH/push.git" rev-parse "refs/heads/$PFX/main" 2>/dev/null)
if [ "$rc8" -ne 1 ]; then
    t_fail "8: a DESCENDANT TIP (c a child of the local main X8, on the side branch work/a) was not refused (exit $rc8, want 1) -- the reference's check 1 asks that a TIP be ON the local main; a descendant not on it is refused: $(runlog SMOKE-RUN-8 | head_bytes 300)"
elif ! runlog SMOKE-RUN-8 | grep -qF "is not on push.git's $PFX/main: stop"; then
    t_fail "8: the descendant-TIP refusal has no \`backlog: <c> is not on push.git's <prefix>/main: stop\` line in push.log: $(runlog SMOKE-RUN-8 | head_bytes 300)"
elif [ "$local_main_after8" != "$local_main_before8" ] || [ "$local_main_before8" != "$X8" ]; then
    t_fail "8: push.git's local main MOVED in the run ($local_main_before8 -> $local_main_after8, want it still at X8 $X8) -- the refusal fires before any push, and the port never moves the local main"
elif [ "$rm_before8" != "$rm_after8" ]; then
    t_fail "8: the descendant-TIP refusal CHANGED the remote's main ($rm_before8 -> $rm_after8) -- c must NOT land (the refusal is before any push)"
else
    t_ok "8: the descendant-TIP refusal (SV:0339.2) -- a TIP c that is a CHILD of push.git's local main X8 but lives on the side branch (work/a, not on main), the remote at X8: refused exit 1 with the \`is not on push.git's <prefix>/main: stop\` line, push.git's local main STILL at X8 (the port never moves it), the remote UNTOUCHED (c did not land); the tooth the kept-but-rejected P1 fix (67c0fe8, the local-main advance) would have broken"
fi

# --- 9: the 9-TIP happy run (M1151 defect b: the silent under-push, by effect) ---
# The reference's `for t in "$@"` is UNBOUNDED: every TIP the operator passes
# is a step, oldest first, `backlog: done` only after ALL steps. The port
# pre-M1151(b) capped the TIP list at 8 (tips[8]): the 9th positional was
# copied nowhere, dropped, no warning, yet the run printed `backlog: done`
# and exited 0 -- a SILENT UNDER-PUSH. This cell is that effect: 9 ascending
# empty commits on the team's main, the remote + push.git's tracking ref at
# the base (behind tip 1 -- the remote sat at main~9 before the commits, so
# no remote move is needed), all 9 passed oldest-first. RED on the unfixed
# tree: only 8 steps run, the remote ends at tip8 (NOT tip9), push.log
# carries eight `backlog: push` lines, yet `done` prints and exit 0.
for k in 1 2 3 4 5 6 7 8 9; do
    git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team: t9$k, a step of the nine-TIP run" || fixture_fail "c9 commit $k"
    fetch_mirror || fixture_fail "c9 fetch $k"
done
base9=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~9") || fixture_fail "c9 base"
tip9_1=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~8") || fixture_fail "c9 tip9_1"
tip9_2=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~7") || fixture_fail "c9 tip9_2"
tip9_3=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~6") || fixture_fail "c9 tip9_3"
tip9_4=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~5") || fixture_fail "c9 tip9_4"
tip9_5=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~4") || fixture_fail "c9 tip9_5"
tip9_6=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~3") || fixture_fail "c9 tip9_6"
tip9_7=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~2") || fixture_fail "c9 tip9_7"
tip9_8=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~1") || fixture_fail "c9 tip9_8"
tip9_9=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c9 tip9_9"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c9 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c9 re-clone swap"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c9 tracking seed"
# Shape sanity (a fixture error, not an assertion on the port): the remote +
# tracking ref at the base (behind tip 1), the tips ascending, all on
# push.git's local main.
[ "$(remote_main)" = "$base9" ] || fixture_fail "c9 shape remote-at-base"
git -C "$PH/push.git" merge-base --is-ancestor "$base9" "$tip9_1" || fixture_fail "c9 shape base < tip1"
for t in "$tip9_1" "$tip9_4" "$tip9_8" "$tip9_9"; do
    git -C "$PH/push.git" merge-base --is-ancestor "$t" "refs/heads/$PFX/main" || fixture_fail "c9 shape tip-on-main"
done
git -C "$PH/push.git" merge-base --is-ancestor "$tip9_1" "$tip9_9" || fixture_fail "c9 shape tips ascending"
printf 'SMOKE-RUN-9\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" \
    "$tip9_1" "$tip9_2" "$tip9_3" "$tip9_4" "$tip9_5" "$tip9_6" "$tip9_7" "$tip9_8" "$tip9_9" \
    < /dev/null > "$tmp/b9" 2> "$tmp/b9.err"
rc9=$?
rm_after9=$(remote_main)
npush9=$(runlog SMOKE-RUN-9 | grep -cF "backlog: push")
if [ "$rc9" -ne 0 ]; then
    t_fail "9: the 9-TIP run did not complete (exit $rc9, want 0): $(tr '\n' ' ' < "$tmp/b9.err" | head_bytes 200)"
elif [ "$rm_after9" != "$tip9_9" ]; then
    t_fail "9: the 9-TIP run exited 0 but the remote's main is $rm_after9 -- EVERY tip must land; the 9th step must put the remote at tip9 ($tip9_9), not the 8th ($tip9_8) (the pre-M1151(b) cap silently dropped the 9th TIP, pushed 8 steps, and still printed done with exit 0 -- the silent under-push)"
elif [ "$npush9" != "9" ]; then
    t_fail "9: push.log carries $npush9 \`backlog: push\` lines for the run (want nine -- one step per TIP, oldest first): $(runlog SMOKE-RUN-9 | head_bytes 240)"
elif ! runlog SMOKE-RUN-9 | grep -qF "backlog: done"; then
    t_fail "9: the 9-step run has no \`backlog: done\` (it must print only after ALL nine steps): $(runlog SMOKE-RUN-9 | head_bytes 240)"
else
    t_ok "9: the 9-TIP happy run (M1151 defect b) -- 9 ascending tips passed oldest-first: exit 0, the remote's main ends at tip9 (NOT tip8), push.log carries NINE \`backlog: push\` lines and \`backlog: done\` -- the reference's unbounded \`for t in \"\$@\"\` held: every TIP is a step, no silent under-push"
fi

# --- 10: the 10-TIP run (M1151 defect b: the push_extra[8] cap, by effect) ---
# Same shape, one more tip: 10 ascending empty commits on the team's main,
# the remote + push.git's tracking ref seeded at the base (behind tip 1;
# cell 9 left the remote at tip9_8, so the driver force-pushes it back --
# the driver's own instrument, as cells 3/7 do), all 10 passed oldest-first.
# RED on the unfixed tree: the
# 10th positional overflowed jc_harness_main.c's `push_extra[8]` and was
# refused LOUDLY before any step -- `jichi-harness: unexpected argument
# '<t10>' after command 'push'`, exit 2, nothing pushed (a cap the reference
# does not have, and the wrong one to hit: it blocked the operator from
# passing 9 or more TIPs at all). The tips[8] cap of cell 9 would,
# separately, drop the 9th and 10th steps of such a run.
for k in 1 2 3 4 5 6 7 8 9 10; do
    git -C "$tmp/dir/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team: t10$k, a step of the ten-TIP run" || fixture_fail "c10 commit $k"
    fetch_mirror || fixture_fail "c10 fetch $k"
done
base10=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~10") || fixture_fail "c10 base"
tip10_1=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~9") || fixture_fail "c10 tip10_1"
tip10_2=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~8") || fixture_fail "c10 tip10_2"
tip10_3=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~7") || fixture_fail "c10 tip10_3"
tip10_4=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~6") || fixture_fail "c10 tip10_4"
tip10_5=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~5") || fixture_fail "c10 tip10_5"
tip10_6=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~4") || fixture_fail "c10 tip10_6"
tip10_7=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~3") || fixture_fail "c10 tip10_7"
tip10_8=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~2") || fixture_fail "c10 tip10_8"
tip10_9=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main~1") || fixture_fail "c10 tip10_9"
tip10_10=$(git -C "$tmp/dir/mirror.git" rev-parse "refs/heads/$PFX/main") || fixture_fail "c10 tip10_10"
git clone -q --bare "$tmp/dir/mirror.git" "$PH/push.git.new" || fixture_fail "c10 re-clone"
rm -rf "$PH/push.git"; mv "$PH/push.git.new" "$PH/push.git" || fixture_fail "c10 re-clone swap"
# The remote (the standin) back to $base10 (behind tip10_1): cell 9's run left
# it at tip9_8 (the silent under-push on the unfixed tree, or tip9_9 on a
# fixed one) -- either way ahead of base10, so a backwards move: the
# DRIVER's own instrument, a --force push to the bare stand-in, as cells
# 3/7 do (the port's never---force is what the cells hold).
git -C "$tmp/dir/mirror.git" push -q --force "$tmp/dir/standin.git" \
    "$base10:refs/heads/$PFX/main" < /dev/null > "$tmp/c10seed" 2>&1 \
    || fixture_fail "c10 remote reset: $(tr '\n' ' ' < "$tmp/c10seed" | head_bytes 160)"
git -C "$PH/push.git" update-ref "refs/remotes/origin/$PFX/main" "$(remote_main)" || fixture_fail "c10 tracking seed"
# Shape sanity (a fixture error, not an assertion on the port): the remote +
# tracking ref at the base (behind tip 1), the tips ascending, all on
# push.git's local main.
[ "$(remote_main)" = "$base10" ] || fixture_fail "c10 shape remote-at-base"
git -C "$PH/push.git" merge-base --is-ancestor "$base10" "$tip10_1" || fixture_fail "c10 shape base < tip1"
for t in "$tip10_1" "$tip10_5" "$tip10_9" "$tip10_10"; do
    git -C "$PH/push.git" merge-base --is-ancestor "$t" "refs/heads/$PFX/main" || fixture_fail "c10 shape tip-on-main"
done
git -C "$PH/push.git" merge-base --is-ancestor "$tip10_1" "$tip10_10" || fixture_fail "c10 shape tips ascending"
printf 'SMOKE-RUN-10\n' >> "$LOG"
"$HBIN" push backlog --config "$tmp/cfg.json" \
    "$tip10_1" "$tip10_2" "$tip10_3" "$tip10_4" "$tip10_5" \
    "$tip10_6" "$tip10_7" "$tip10_8" "$tip10_9" "$tip10_10" \
    < /dev/null > "$tmp/b10" 2> "$tmp/b10.err"
rc10=$?
rm_after10=$(remote_main)
npush10=$(runlog SMOKE-RUN-10 | grep -cF "backlog: push")
if [ "$rc10" -ne 0 ]; then
    t_fail "10: the 10-TIP run did not complete (exit $rc10, want 0) -- the pre-M1151(b) tree refused the 10th positional LOUDLY (\`unexpected argument\` after command 'push', exit 2, nothing pushed); NO positional may be dropped or refused as unexpected: $(tr '\n' ' ' < "$tmp/b10.err" | head_bytes 200)"
elif [ "$rm_after10" != "$tip10_10" ]; then
    t_fail "10: the 10-TIP run exited 0 but the remote's main is $rm_after10 -- EVERY tip must land; the 10th step must put the remote at tip10 ($tip10_10)"
elif [ "$npush10" != "10" ]; then
    t_fail "10: push.log carries $npush10 \`backlog: push\` lines for the run (want ten -- one step per TIP, oldest first): $(runlog SMOKE-RUN-10 | head_bytes 240)"
elif ! runlog SMOKE-RUN-10 | grep -qF "backlog: done"; then
    t_fail "10: the 10-step run has no \`backlog: done\` (it must print only after ALL ten steps): $(runlog SMOKE-RUN-10 | head_bytes 240)"
else
    t_ok "10: the 10-TIP run (M1151 defect b) -- 10 ascending tips passed oldest-first: exit 0, the remote's main ends at tip10, push.log carries TEN \`backlog: push\` lines and \`backlog: done\`: no positional is dropped (the tips[8] cap) or refused as \`unexpected argument\` (the push_extra[8] cap)"
fi

t_done
