#!/bin/sh
# M2 (the 2026-10-16 plan; drive-readiness handoff of 2026-10-16, in docs/internal/, section 8.3): jichi's git children must not trust the USER's repo
# config.
#
# THE HOLE, measured 2026-10-16 in a throwaway repo. jichi runs plain
# `git` against the repository it is pointed at, and those children inherit the
# process environment. A repository whose .git/config names
# core.fsmonitor=<script> therefore makes `git status` EXECUTE that script --
# a malicious repo (or a drive team that writes /work/.git/config) gets process
# execution with jichi's environment every time an agent reads its status.
# The same config key carries ext-diff, alias.*.command and (for a commit) the
# hooks path: more program-execution surfaces.
#
# A SECOND HOLE, found the same day while reviewing the fix: a repo that plants
# commit.gpgsign=true plus gpg.program=<script> makes `git commit` EXECUTE that
# script (measured: the raw commit fires it, and dropping
# `-c commit.gpgsign=false` from the neutral set fires it again).
# `-c log.showSignature=false` does NOT stop it -- showSignature only gates
# signature DISPLAY in log/show/blame, not the commit signing decision.
#
# THE FIX. Every git child jichi spawns carries, before anything else,
# `-c core.fsmonitor= -c log.showSignature=false -c commit.gpgsign=false
# -c core.hooksPath=/dev/null`; the read-only git_diff additionally carries the
# diff-only `--no-ext-diff --no-textconv`. The empty/forced values OVERRIDE the
# planted config (command-line config beats file config), so the scripts are
# never exec'd. This is a FENCE, not a display choice.
#
# WHAT THIS ASSERTS (red-first: without the fix, check 8 fires -- a planted
# gpg.program is executed by jichi's own git_commit).
#   1. the control: a PLANTED fsmonitor, driven by a bare `git`, actually
#      touches its marker -- so the test can fail, it is not vacuous;
#   2. the live jichi run calls git_status in the SAME planted repo and reaches
#      a final answer (the mock tool round trip is intact);
#   3. that same run leaves the fsmonitor marker ABSENT -- the fix wins over
#      the config;
#   4-5. the source carries the neutralisation at the right places (the tool's
#      git_capture, the snapshot's run_argv_env);
#   6. the COMMIT control: the planted gpg.program fires under a raw `git
#      commit` -- the instrument can detect it;
#   7-8. jichi's OWN git_commit in the same planted repo: reaches a final
#      answer (the round trip is intact) and leaves the gpg marker ABSENT --
#      the check pair that goes red when the neutralisation is dropped;
#   9. the COMMIT revert: a raw commit with everything EXCEPT
#      `-c commit.gpgsign=false` fires again -- that one flag is held
#      load-bearing;
#   10. git_diff carries the diff-only flags.
#
# A note on what the live check covers: it exercises the USER-repo path (the
# git_status tool, which reads the repo it is pointed at). The snapshot
# machinery reads jichi's own shadow repo (a different config, not attacker-
# controllable), so its neutralisation is asserted in the source (check 5)
# rather than by a second live run.
. "$(dirname "$0")/_smoke.sh"

# git is a hard dependency of the product's git tools; no git, nothing to
# neutralise -- a whole-driver skip, not a weakened pass. Decided BEFORE t_plan
# (smoke_lint check 20): a skip after the plan emits a second TAP plan.
command -v git >/dev/null 2>&1 || t_skip "git not available"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
marker="$ws/fsm_fired"
gpg_marker="$ws/gpg_fired"

# --- a real repo with a planted fsmonitor ---------------------------------
git -C "$ws" init -q
git -C "$ws" config user.email "t@t"
git -C "$ws" config user.name "t"
echo "hello" > "$ws/f.txt"
git -C "$ws" add f.txt
git -C "$ws" commit -qm init
# The planted fsmonitor: a script that records it ran. Written by the test,
# inside the smoke tmp tree, touching ONLY its own marker (path-fence clean).
cat > "$ws/fsm.sh" <<EOF
#!/bin/sh
touch "$marker"
EOF
chmod +x "$ws/fsm.sh"
git -C "$ws" config core.fsmonitor "$ws/fsm.sh"

# --- a planted gpg.program: the COMMIT-path program-execution surface ------
# commit.gpgsign=true + gpg.program=<script> makes `git commit` EXECUTE the
# script (measured). -c log.showSignature=false does NOT stop it (display-only);
# only -c commit.gpgsign=false does.
cat > "$ws/gpg.sh" <<EOF
#!/bin/sh
touch "$gpg_marker"
exit 0
EOF
chmod +x "$ws/gpg.sh"
git -C "$ws" config commit.gpgsign true
git -C "$ws" config gpg.program "$ws/gpg.sh"

# --- 1: the control fires (the test can fail) ------------------------------
rm -f "$marker"
git -C "$ws" status --short --branch >/dev/null 2>&1
if [ -e "$marker" ]; then
    t_ok "control: a bare git status in the planted repo fired the fsmonitor"
else
    t_fail "control did not fire -- the test cannot detect the hole; rerun"
fi

# --- 2: jichi's git_status in the same repo must NOT fire it ---------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool git_status {}
rule
  match "\"role\":\"tool\""
  text FSMON_OK
EOF
mm_start "$tmp/replies.mm" "$tmp" 2
write_config "$tmp/config.json" "$MM_PORT"
rm -f "$marker"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --auto --no-session -q -p "show the status" < /dev/null); rc=$?
mm_stop
case "$out" in
    *FSMON_OK*) t_ok "jichi's git_status ran to a final answer" ;;
    *) t_fail "git_status run did not reach the final answer: $(printf '%s' "$out" | head_bytes 160) (rc=$rc)" ;;
esac
if [ ! -e "$marker" ]; then
    t_ok "jichi's git_status did NOT fire the planted fsmonitor"
else
    t_fail "planted fsmonitor FIRED under jichi -- the neutralisation is absent"
fi

# --- 4-5: the source carries the fix at the right places ------------------
if grep -q 'core.fsmonitor=' "$SMOKE_ROOT/src/tools/jc_tool_git.c"; then
    t_ok "the git tool's git_capture neutralises core.fsmonitor"
else
    t_fail "git tool no longer carries core.fsmonitor="
fi
if grep -q 'core.fsmonitor=' "$SMOKE_ROOT/src/snapshot/jc_snapshot.c"; then
    t_ok "the snapshot's run_argv_env neutralises core.fsmonitor"
else
    t_fail "snapshot no longer carries core.fsmonitor="
fi

# --- 6: the COMMIT control fires (the gpg instrument is not vacuous) -------
rm -f "$gpg_marker"
git -C "$ws" commit --allow-empty -qm gpg-control >/dev/null 2>&1
if [ -e "$gpg_marker" ]; then
    t_ok "control: a bare git commit in the planted repo fired gpg.program"
else
    t_fail "control did not fire -- the gpg test cannot detect the hole"
fi

# --- 7: jichi's OWN git_commit must NOT fire it (the load-bearing proof) ---
# check 6 proves the instrument; check 8 proves the specific flag is what
# suppresses it; this one drives jichi's real commit_run -> git_capture path,
# so it is the check that goes RED when the flag is dropped from the source.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool git_commit {"message":"gpg-neutral"}
rule
  match "\"role\":\"tool\""
  text GPG_OK
EOF
mm_start "$tmp/replies.mm" "$tmp" 2
write_config "$tmp/config.json" "$MM_PORT"
# Stage a real change so jichi's `git commit -m ...` SUCCEEDS and writes a new
# commit object. Without this it would fail with "nothing to commit" and never
# reach the signing step, passing this check vacuously (a commit that does not
# write a commit cannot be signed). The mock replies after the tool result,
# either way.
echo "gpg" > "$ws/g.txt"
git -C "$ws" add g.txt
rm -f "$gpg_marker"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --auto --no-session -q -p "commit it" < /dev/null); rc=$?
mm_stop
case "$out" in
    *GPG_OK*) t_ok "jichi's git_commit ran to a final answer" ;;
    *) t_fail "git_commit run did not reach the final answer: $(printf '%s' "$out" | head_bytes 160) (rc=$rc)" ;;
esac
if [ ! -e "$gpg_marker" ]; then
    t_ok "jichi's git_commit did NOT fire the planted gpg.program"
else
    t_fail "planted gpg.program FIRED under jichi's git_commit -- the neutralisation is absent"
fi

# --- 8: dropping ONLY commit.gpgsign=false makes it fire again -------------
rm -f "$gpg_marker"
git -C "$ws" -c core.fsmonitor= -c log.showSignature=false \
    -c core.hooksPath=/dev/null \
    commit --allow-empty -qm gpg-revert >/dev/null 2>&1
if [ -e "$gpg_marker" ]; then
    t_ok "dropping commit.gpgsign=false fires it again (load-bearing)"
else
    t_fail "the flag was not load-bearing -- showSignature would not be, so something else suppressed it"
fi

# --- 9: the diff-only flags -------------------------------------------------
if grep -q -- '--no-ext-diff' "$SMOKE_ROOT/src/tools/jc_tool_git.c"; then
    t_ok "git_diff carries the diff-only --no-ext-diff neutralisation"
else
    t_fail "git_diff no longer carries --no-ext-diff"
fi

t_done
