#!/bin/sh
# smoke: ~/.jichi.d -- the state root that holds FULL CONTENT COPIES of the
# workspace (shadow-repo objects, worktrees, the index cache) -- is created
# owner-only, and doctor warns (without fixing) when an EXISTING root is
# group/other-accessible (M1096, finding D-06).
#
# THE DEFECT. Fresh HOME, umask 022, one mock run with snapshots on (the
# lead's confirmation on head b34dafde): ~/.jichi.d 755, checkpoints/ 755,
# checkpoints/<key>/ 755 with objects/* 444 and index 644. On a shared host
# with a traversable home, a project kept under a 0700 directory -- or a 0600
# .env in a non-git workspace -- is copied into zlib objects another local
# user can read. M132/M488 made sessions/telemetry/audit 0600/0700 but not the
# store that holds the content itself.
#
# THE FIX HAS TWO TEETH (M1096, on head b34dafde): (1) the root is created
# 0700 by main, so every child is unreachable whatever its own mode; (2) a
# pre-existing permissive root is NAMED by doctor -- a posture warning, never
# a silent chmod, which is what checks 4 and 5 pin down.
#
# M1096-b (SV:1618) ADDS THE THIRD TOOTH: on an EXISTING install (a root made
# by a pre-fix build, i.e. 755), a FIXED RUN must tighten the root back to
# 0700 at startup. That is the residual the supervisor measured by effect:
# after a run under umask 022 the root, checkpoints/ and every old
# checkpoints/<key> stay r-xr-x, so the old objects/HEAD/index files (28
# measured) remain readable by other users. Check 7 is red-first for exactly
# that (a run leaves the permissive root r-xr-x); check 8 pins that doctor's
# warning is NOT a side effect of the startup tighten (re-chmod 755 after the
# run, run doctor, assert the line + remedy); check 9 re-pins the original
# contract (doctor warns, it does not fix).
#
# Baselines (green on BOTH sides; a driver red here is broken, not
# prophetic): 1, 5, 6 and 8. Teeth, red-first: 2 (root 755) and 3 (checkpoint
# base 755) -- both red at M1096's own 635e52cf, and check 4 (no doctor line,
# red at 635e52cf). Check 7 is the M1096-b tooth (red at 2861df0b: a run leaves
# the permissive root r-xr-x).
. "$(dirname "$0")/_smoke.sh"

command -v git > /dev/null 2>&1 || t_skip "git is required for snapshots"

t_plan 9
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

mkdir -p "$ws/src"
printf 'int main(void){return 0;}\n' > "$ws/src/a.c"
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t && git add -A && git commit -qm base) \
    > /dev/null 2>&1

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"src/b.txt","content":"hello\n"}
rule
  text DONE
EOF
# One mock, no --max-requests, serves BOTH headless runs: the fresh-HOME run
# (checks 1-3) and check 7's existing-install run. mm_stop stays until check 7
# (doctor in checks 4-6 makes no model call, so the mock is idle between them),
# keeping the config's apiBase valid so check 7 is a real agent run under the
# same mock. mockmodel's rule matcher is stateless per request, so run 2 is
# replayed identically (write_file, then DONE).
mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
"lowResource":false,"maxRetries":0}
EOF

# The run executes under umask 022 -- the common shared-host umask the finding
# measures -- in the same subshell as the run, so the umask belongs to jichi,
# not to the driver.
( umask 022; cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "do it" \
    < /dev/null > "$tmp/out" 2> "$tmp/err" ) || true

cp="$HOME/.jichi.d/checkpoints"
ckey=""
if [ -d "$cp" ]; then
    ckey=$(ls "$cp" 2>/dev/null | head -1)
fi

# ---- 1. the case is live: a checkpoint with real objects was written -------
if [ -n "$ckey" ] && [ -d "$cp/$ckey/objects" ]; then
    t_ok "the run created a checkpoint ($cp/$ckey with objects/)"
else
    t_fail "no checkpoint with an objects/ dir under $cp -- the scenario did \
not run: $(head_bytes 200 "$tmp/err")"
fi

# ---- 2-3. the teeth: owner-only root, owner-only checkpoint base -----------
# Portable mode read (the parallel_transcript idiom): `ls -ld | cut -c5-10`
# yields the GROUP/OTHER permission bits -- owner-only means '------'.
# stat -c is banned by posix_utils_lint.
fence=$(smoke_can_fence_owner)
root_perm=""; key_perm=""
[ -d "$HOME/.jichi.d" ] && root_perm=$(ls -ld "$HOME/.jichi.d" 2>/dev/null | cut -c5-10)
[ -n "$ckey" ] && [ -d "$cp/$ckey" ] \
    && key_perm=$(ls -ld "$cp/$ckey" 2>/dev/null | cut -c5-10)

if [ "$fence" = yes ]; then
    if [ "$root_perm" = "------" ]; then
        t_ok "the state root ~/.jichi.d is owner-only (group/other: ------)"
    else
        t_fail "state root is not owner-only: group/other '$root_perm' (want \
------) -- a shared-host umask 022 run leaves it world-traversable (D-06)"
    fi
    if [ "$key_perm" = "------" ]; then
        t_ok "the checkpoint base checkpoints/$ckey is owner-only (group/other: ------)"
    else
        t_fail "checkpoint base is not owner-only: group/other '$key_perm' \
(want ------)"
    fi
else
    t_skip_one "root mode check (this host cannot fence its owner -- the \
chmod guarantee is unverifiable here)"
    t_skip_one "checkpoint-base mode check (same host fence as above)"
fi

# ---- 4. doctor NAMES a pre-existing permissive root (red-first) ------------
# Doctor observes, it does not fix. M1096-b tightens jichi's OWN root at
# startup (pinned by check 7), but doctor is EXCLUDED from that: it must read
# the RAW on-disk mode and name it, because a doctor that chmod'd its probe
# target before the check would never fire this line. So the driver chmods the
# (already-created, 0700) root back to 755 -- the shape of an install predating
# M1096, or a root a non-jichi creator / failed tighten left -- and demands the
# doctor warning plus the chmod 700 remedy.
chmod 755 "$HOME/.jichi.d" 2>/dev/null
with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/doc1" 2>&1
if grep -q 'state dir private' "$tmp/doc1" 2>/dev/null \
    && grep -q 'chmod 700' "$tmp/doc1" 2>/dev/null; then
    t_ok "doctor warns about the permissive pre-existing root (state dir private)"
else
    t_fail "doctor is silent about a group/other-accessible ~/.jichi.d -- no \
'state dir private' line or no 'chmod 700' remedy: $(head_bytes 200 "$tmp/doc1")"
fi

# ---- 5. pin: doctor warns, it does NOT fix ---------------------------------
# Green on both sides (the root stays 755 before the fix because nothing
# touches it, and after the fix because doctor must not chmod a pre-existing
# dir). A silent fix would be the regression this pins.
root_perm=$(ls -ld "$HOME/.jichi.d" 2>/dev/null | cut -c5-10)
if [ "$root_perm" = "r-xr-x" ]; then
    t_ok "doctor did not chmod the pre-existing root (still group/other r-xr-x)"
else
    t_fail "doctor changed the pre-existing root to '$root_perm' -- it must \
warn, not fix silently"
fi

# ---- 6. pin: a private root is not warned about ----------------------------
chmod 700 "$HOME/.jichi.d" 2>/dev/null
with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/doc2" 2>&1
if grep -q 'state dir private' "$tmp/doc2" 2>/dev/null; then
    t_fail "doctor warns 'state dir private' for an owner-only root -- false \
positive: $(grep 'state dir private' "$tmp/doc2" | head -2)"
else
    t_ok "a private root draws no warning"
fi

# ---- 7. M1096-b (SV:1618): the tooth. A run tightens jichi's OWN permissive
# state root back to owner-only. Check 6 left the root 700; restore the
# permissive shape of an EXISTING install (root made by a pre-fix build under
# umask 022), then run the agent headless one more time under umask 022. On
# head 2861df0b a run leaves the root r-xr-x -- RED; after the fix the
# startup tighten makes it owner-only. The mock from checks 1-3 is still
# running: the rule matcher is stateless per request, so the second run is
# replayed identically.
#
# NOTE: doctor does NOT tighten (it must keep naming permissive roots -- the
# pinned checks 4/5/8/9), so the tighten applies to the agent run.
chmod 755 "$HOME/.jichi.d" 2>/dev/null
( umask 022; cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "do it again" \
    < /dev/null > "$tmp/out2" 2> "$tmp/err2" ) || true
mm_stop
if [ "$fence" = yes ]; then
    root_perm=$(ls -ld "$HOME/.jichi.d" 2>/dev/null | cut -c5-10)
    if [ "$root_perm" = "------" ]; then
        t_ok "an agent run tightens jichi's own permissive state root back to owner-only (group/other: ------)"
    else
        t_fail "the run left ~/.jichi.d group/other '$root_perm' (want ------) -- an EXISTING install (a root made by a pre-fix build) stays world-traversable after a run: the old checkpoints and their objects/HEAD/index files remain readable by other users (D-06, SV:1618)"
    fi
else
    t_skip_one "run-tightens-root mode check (this host cannot fence its owner -- the chmod guarantee is unverifiable here)"
fi

# ---- 8. belt pin: doctor's warning is NOT a side effect of the startup
# tighten. Re-chmod 755 AFTER the check-7 run (which tightened the root on
# the fixed tree), run doctor, and demand the 'state dir private' line AND
# the 'chmod 700' remedy -- this is check 4's grep with a stated reason: it
# proves the warning branch survives a world in which the agent run fixes the
# root (on head the run does not fix it, so this is simply check 4's
# condition; both sides green). Doctor observes on-disk state; it does not
# chmod, so the root it sees here is exactly what we set it to.
chmod 755 "$HOME/.jichi.d" 2>/dev/null
with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/doc3" 2>&1
if grep -q 'state dir private' "$tmp/doc3" 2>/dev/null \
    && grep -q 'chmod 700' "$tmp/doc3" 2>/dev/null; then
    t_ok "doctor still names a permissive root after the run tightened one (state dir private + chmod 700 -- not a side effect of the startup tighten)"
else
    t_fail "doctor is silent about a permissive root it did NOT create -- the warning vanished once the agent run self-hardens: $(head_bytes 200 "$tmp/doc3")"
fi

# ---- 9. pin: doctor warns, it does not fix (the original M1096 contract,
# re-pinned under the new world where the agent run DOES fix) ---------------
root_perm=$(ls -ld "$HOME/.jichi.d" 2>/dev/null | cut -c5-10)
if [ "$root_perm" = "r-xr-x" ]; then
    t_ok "doctor did not chmod the permissive root (still group/other r-xr-x after the warning)"
else
    t_fail "doctor changed the permissive root to '$root_perm' -- it must warn, not fix (the fix is the agent run's job, pinned by check 7)"
fi

t_done
