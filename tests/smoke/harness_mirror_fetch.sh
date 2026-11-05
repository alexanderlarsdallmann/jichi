#!/bin/sh
# smoke: `jichi-harness mirror fetch` + `mirror head` -- the C89 port of
# mirror.sh's mirror_fetch + mirror_head (the jichi-harness drive of
# 2026-10-23, batch H3b, M1055). Real git, a rehearsal layout made by
# `jichi-harness init` (whose mirror-creation now also stores the mirror's
# config bytes -- the stored-copy path), and the five integrity checks the
# note names (bare, config bytes, no alternates, hooks, ownership) run
# BEFORE any git child. The push is M1056 (H3c) and is NOT built here.
#
# M1055 (H3b). Design note (the contract, measured on git 2.53.0):
#   docs/internal/drive/2026-10-23-harness/notes/M1055.md
# Ported reference (read line by line; the push is M1056, NOT this driver):
#   docs/internal/drive/2026-10-23-harness/harness/mirror.sh
#
# THE RED THIS IS HELD BY. On the tree before the change the `mirror`
# subcommand is the H0 "not built yet" refusal (exit 2, `jichi-harness:
# mirror: not built yet (see docs/HARNESS.md)`). The cells below that expect
# a fetch to RUN (1, 2, 3, 9, 10) fail: they get exit 2 instead of 0/1 and
# the report lines are absent.
#
# WHAT IS CHECKED (the note's W-cells):
#   1. W1: the happy fetch exits 0 and brings $BR/main + both $BR/work/* into
#      the mirror's refs/heads/ (NOT refs/remotes/), no tags.
#   2. W2: a non-FF work branch is a report line, NOT a failure -- the fetch
#      exits 0 (git's rc=1 is tolerated, the other refspecs still applied),
#      the rejected work branch UNCHANGED, $BR/main MOVED in the same call.
#   3. a transport failure (the team clone is missing) exits 1 and names the
#      git error (first ~200 bytes, M1053's git_must idiom).
#   4. W3: a tampered mirror config (bytes != the stored copy) is refused,
#      exit 2, naming the check, BEFORE any fetch (no FETCH_HEAD appears).
#   5. the stored copy missing -> refused, exit 2, naming the check.
#   6. a planted `objects/info/alternates` -> refused, exit 2, naming the check.
#   7. W4: a PLANTED executable hook in hooks/ -> refused, exit 2, naming the
#      check (git's own `*.sample` templates are tolerated).
#   8. a non-bare dir (the team clone) -> refused, exit 2, naming the check.
#   9. W4/W5: the kept-attempt fetch -- a checkpoint store whose `worktree =`
#      line equals the team workdir is fetched into refs/kept/<k>/; a store
#      whose worktree is NOT the team's is skipped, not errored (the
#      2026-10-18 incident); a failing store is a report line, not fatal.
#  10. W7/W8: `mirror head` prints the mirror's $BR/main short hash; a mirror
#      without that ref prints nothing (the script's 2>/dev/null shape).
#  11. W6: the fetch child carries the M2 set + GIT_CONFIG_GLOBAL/SYSTEM at
#      two EMPTY harness-owned files + -c core.alternateRefsCommand= (source
#      grep, the M2 idiom: the child cannot read the operator's config).
#      A CHEAP LINT only -- the behavioural evidence for behaviour 4 is cell
#      14 (a source grep is satisfied by a comment; cell 11 is not the
#      isolation's proof).
#  12. W8-env (C9): the fetch child's ENVIRONMENT, via a git WRAPPER first on
#      PATH that appends `env` AND the child's argv to dump files and then
#      execs the REAL git (resolved at runtime -- the driver never assumes
#      /usr/bin). Two
#      canaries are exported into the harness env for this ONE run:
#      GITHUB_TOKEN (a builtin deny-list NAME -- the SCRUB target,
#      jc_proc.c:71-78) and JH_ENV_CANARY (an UNREGISTERED name). Assert the
#      dump (a) EXISTS and holds GIT_CONFIG_GLOBAL= (it read a real fetch
#      child, not a dead instrument), (b) does NOT hold the GITHUB_TOKEN
#      canary (the SCRUB, measured), (c) DOES hold the JH_ENV_CANARY canary
#      (C9: only the deny-list drops names -- "minimal env" is false). The
#      old W8 packObjectsHook plant (which measured GIT_CONFIG isolation,
#      not the env SCRUB, and passed having read nothing -- "env-dump
#      ABSENT") is DROPPED: the isolation is held BEHAVIOURALLY by cell 14,
#      and cell 11 (W6) is a cheap source lint, not its evidence.
#  13. W8-env control (the TOOTH): a REAL git command run from the DRIVER
#      shell (not through the harness, so NO scrub; env inherited) through a
#      second wrapper dumping to a separate file carries BOTH canaries -- so
#      the instrument captures everything the shell has, and cell 12's
#      absence of the registered canary is the scrub, not a dead instrument.
#  14. W6-behavioural (supervisor 2026-10-25 01:17 item 1): the GIT_CONFIG
#      isolation, measured in the real threat's shape. A SUBSHELL with the
#      driver's own export UNSET and HOME at a fake home whose .gitconfig
#      plants uploadpack.packObjectsHook (a marker-touching hook that execs
#      the real git) runs `mirror fetch`: the marker is ABSENT (the harness's
#      empty GIT_CONFIG_* isolation stopped the poisoned global from being
#      read by the serve child) and the team ref is CARRIED (the fetch
#      landed). A plain-git control fetch (no -c flags, no harness) in the
#      SAME poisoned env makes the marker PRESENT -- the instrument can fire,
#      so the absence is the isolation, not a dead hook.
#  15. W8-canary-leak (supervisor 2026-10-25 01:17 item 3): the rehearsal's
#      token canary (CANARY-TOKEN-DO-NOT-PRINT, the content of $tmp/tokfile) and
#      the model-key canary ($_kc, the content of $tmp/kreal) NEVER appear in any
#      real capture file the driver produced -- the f*/i*/binv stdout+stderr
#      captures, or the cell-12 git wrapper's env/argv dumps. The secret files
#      themselves are excluded. No cell before this asserted it; cell 15 is the
#      assertion.
. "$(dirname "$0")/_smoke.sh"

# Guarded /usr/bin/grep for portability (systems without merged /usr have grep at /bin/grep).
# (portability_lint check 36, the supervisor's 2026-10-25 03:36 note item 1: cell 15's
# sweep used the bare absolute path without the fallback.)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

command -v git >/dev/null 2>&1 || t_skip "git not available"

# 15 cells; each asserts once. Cells 12-13 are the W8-env scrub cell and its
# control tooth; cell 14 is the behavioural GIT_CONFIG isolation cell
# (supervisor 01:17 item 1); cell 15 is the canary-leak assertion (item 3).
# So the plan is 15 results.
t_plan 15
smoke_home
tmp=$(smoke_tmp)

# smoke_lint check 5: this non-lint driver runs the tree's "$BIN" once (the
# harness and the agent are built from the same tree by this tier); the
# fixture git children are neutralised, but $BIN must be exercised so the
# lint sees the driver validate the just-built binary, not a PATH jichi.
"$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HBIN="${JC_SMOKE_HBIN:-$ROOT/jichi-harness}"

# The loader resolves each top-level reference from its JICHI_HARNESS_* env
# setting FIRST (env wins over the config's own value). An ambient dir or key
# setting would defeat the fixtures below, so clear all three.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE

# Neutralise the operator's ambient git config for the FIXTURES: the local
# repos this driver creates must take their identity from the -c flags, not
# from a machine-level gitconfig.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

BR=harness-plan

# --- fixtures ---------------------------------------------------------------
# A local source repo carrying $BR/plan (init clones --branch $BR/plan and
# pushes it into the rehearsal stand-in).
src="$tmp/src"
git init -q "$src"
git -C "$src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "source: plan base"
git -C "$src" branch "$BR/plan"

# A FAKE pinned jichi (init's layout's use of a pinned binary is what H2's
# driver exercises; here init is only the mirror's creator).
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$tmp/jichi"
chmod 755 "$tmp/jichi"

# The harness config: dir + branchPrefix + git identity + the canary files
# (rehearsal never reads them, but the config reference must exist to pass
# the loader; rehearsal mode needs nothing from them).
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
printf '%s\n' "$_kc" > "$tmp/kreal"; chmod 600 "$tmp/kreal"
printf 'CANARY-TOKEN-DO-NOT-PRINT\n' > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"
cat > "$tmp/cfg.json" <<EOF
{"dir":"$tmp/dir",
"drive":{"branchPrefix":"$BR","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF

# init (rehearsal) lays out standin.git, team/ (cloned from $BR/plan, then
# switched to $BR/main), mirror.git (origin = standin, + the stored config
# copy this milestone's init change writes).
"$HBIN" init --config "$tmp/cfg.json" rehearsal \
    "$tmp/dir" --from "$src" --jichi-bin "$tmp/jichi" \
    < /dev/null > "$tmp/i1" 2> "$tmp/i1.err"
irc=$?
if [ "$irc" -ne 0 ]; then
    t_fail "fixture: init exited $irc: $(head_bytes 200 "$tmp/i1.err")"
    t_fail "fixture: init report"
    t_fail "fixture: init stored-copy"
    t_fail "fixture: init mirror"
    t_fail "fixture: init transport"
    t_fail "fixture: init config"
    t_fail "fixture: init stored-missing"
    t_fail "fixture: init alternates"
    t_fail "fixture: init hooks"
    t_fail "fixture: init nonbare"
    t_fail "fixture: init kept"
    t_fail "fixture: init env-canary"
    t_done
fi
dir="$tmp/dir"
mirror="$dir/mirror.git"
team="$dir/team"
stored="$dir/mirror.git-config.harness-copy"
# The stored-copy fixture. On the FIXED tree this is the file the M1055 init
# change writes (mirror's config bytes, at creation). The driver writes it
# itself here (a byte-exact copy of the mirror's current config) so the RED
# run -- where `mirror` is not yet built AND init's change is absent -- still
# has a valid stored copy to test against; the cell that asserts the COPY's
# existence is cell 5 (a missing stored copy is refused), which is about the
# fetch command, not about init.
[ -f "$stored" ] || cp "$mirror/config" "$stored"

# The team clone now has $BR/main (== the plan commit) and $BR/plan. Give it
# two $BR/work/* branches (W1's refset) with distinct tips so movement is
# trackable.
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: main c2"
git -C "$team" branch -q "$BR/work/a" "$BR/main"
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: work/a"
git -C "$team" branch -q "$BR/work/b"
git -C "$team" checkout -q "$BR/main"

# FIX 3 (the fetch-SOURCE tooth): the mirror's ORIGIN is the rehearsal
# stand-in ($dir/standin.git), which after init carries only $BR/plan. Plant
# a DIFFERENT tip on the stand-in's $BR/main (a commit-tree on top of plan) so
# that if a port wrongly fetched from the mirror's ORIGIN (the stand-in -- the
# M1056 PUSH destination) instead of the team clone, it would land the
# stand-in's tip, not the team's. Cell 1 pins mirror main == TEAM main.
_stand="$dir/standin.git"
_s_tree=$(git -C "$_stand" rev-parse "refs/heads/$BR/plan^{tree}" 2>/dev/null)
if [ -n "$_s_tree" ]; then
    _s_tip=$(git -C "$_stand" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit-tree "$_s_tree" -p "refs/heads/$BR/plan" -m "standin: an extra $BR/main tip (NOT the fetch source)")
    git -C "$_stand" update-ref "refs/heads/$BR/main" "$_s_tip"
fi

# --- 1: W1 -- the happy fetch brings main + both work/* in (exit 0) ---------
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f1" 2> "$tmp/f1.err"
rc1=$?
if [ "$rc1" -ne 0 ]; then
    t_fail "1: W1 happy fetch exited $rc1 (want 0) -- on the unfixed tree the \`mirror\` subcommand is the H0 'not built yet' refusal: $(tr '\n' ' ' < "$tmp/f1.err" | head_bytes 200)"
else
    _m_main=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
    _t_main=$(git -C "$team" rev-parse --short "$BR/main" 2>/dev/null)
    _m_a=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/work/a" 2>/dev/null)
    _m_b=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/work/b" 2>/dev/null)
    _stand_main=$(git -C "$_stand" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
    if [ -z "$_m_main" ] || [ "$_m_main" != "$_t_main" ] || [ -z "$_m_a" ] || [ -z "$_m_b" ]; then
        t_fail "1: W1 fetch reported 0 but the refs did not move: mirror main='$_m_main' (team '$_t_main'), work/a='$_m_a', work/b='$_m_b' -- the refset \$BR/main + \$BR/work/* into refs/heads/... is wrong: $(tr '\n' ' ' < "$tmp/f1" | head_bytes 200)"
    elif [ "$_stand_main" != "$_t_main" ] && [ "$_m_main" = "$_stand_main" ]; then
        t_fail "1: the fetch SOURCE is the stand-in (mirror main $_m_main == stand-in main, a different tip from team main $_t_main) -- the source is the team clone (mirror.sh: serve \$D/team); the stand-in is the PUSH destination (M1056) and is NOT the fetch source"
    else
        t_ok "1: W1 happy fetch exits 0 and brings $BR/main + $BR/work/a + $BR/work/b into refs/heads/ of the mirror ($_m_main, $_m_a, $_m_b) from the TEAM clone (mirror main=$_m_main == team main; the stand-in main is a DIFFERENT tip $_stand_main -- not the source)"
    fi
fi

# --- 2: W2 -- a non-FF work branch is a report line, not a failure ----------
# Make team's $BR/work/a a SIBLING of the mirror's (same parent, different
# tip): the mirror's work/a is NOT an ancestor, so the work/* wildcard
# refspec rejects work/a non-FF (no `+`) while $BR/main moves in the same
# call. git exits 1 with -q (the rejection line is progress, suppressed); the
# port reports it as the script's line and TOLERATES it -> command exits 0.
_main_before=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main")
_work_before=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/work/a")
# Re-base the team's work/a on the PARENT of the mirror's work/a tip, then add
# a new commit: the mirror's work/a tip (8cb...) and the new tip share a
# parent but neither descends from the other -> a true NON-fast-forward.
git -C "$team" branch -D -q "$BR/work/a"
git -C "$team" branch -q "$BR/work/a" "$_work_before^"
git -C "$team" checkout -q "$BR/work/a"
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: work/a sibling (non-FF)"
# Move $BR/main in the team too, so the same fetch call shows the OTHER
# refspec still applying while work/a is rejected.
git -C "$team" checkout -q "$BR/main"
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "team: main c3"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f2" 2> "$tmp/f2.err"
rc2=$?
_main_after=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main")
_work_after=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/work/a")
if [ "$rc2" -ne 0 ]; then
    t_fail "2: W2 non-FF work branch made the fetch exit $rc2 (want 0, git's silent rc=1 TOLERATED): $(tr '\n' ' ' < "$tmp/f2.err" | head_bytes 200)"
elif [ "$_work_before" != "$_work_after" ]; then
    t_fail "2: W2 the REJECTED work branch moved ($_work_before -> $_work_after) -- a non-FF update must not apply (no \`+\`)"
elif [ "$_main_before" = "$_main_after" ]; then
    t_fail "2: W2 the OTHER refspec did not apply in the same call (main stayed $_main_before) -- a single rejection must not abort the fetch"
elif ! grep -qiE "non-fast-forward" "$tmp/f2" "$tmp/f2.err"; then
    t_fail "2: W2 the non-FF rejection is not a report line naming it: $(tr '\n' ' ' < "$tmp/f2" | head_bytes 200)"
else
    t_ok "2: W2 a non-FF work branch is a report line ('non-fast-forward'), the rejected ref stayed ($_work_before), $BR/main moved ($_main_before -> $_main_after), exit 0"
fi

# --- 3: a transport failure (the team clone is missing) exits 1 -------------
# A second rehearsal dir, then its team/ is removed: the fetch source
# (<dir>/team) is gone -> git errors at the transport, exit 1, output named.
cat > "$tmp/cfg3.json" <<EOF
{"dir":"$tmp/dir3",
"drive":{"branchPrefix":"$BR","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
"$HBIN" init --config "$tmp/cfg3.json" rehearsal "$tmp/dir3" --from "$src" \
    --jichi-bin "$tmp/jichi" < /dev/null > "$tmp/i3" 2> "$tmp/i3.err" \
    || t_fail "3: fixture init (dir3) failed: $(tr '\n' ' ' < "$tmp/i3.err" | head_bytes 200)"
# FIX 1 (red-first tooth): a KEPT-ATTEMPT store for dir3, whose `worktree =`
# line equals dir3's team path. After the team clone is removed (the transport
# failure below), the kept-attempt loop must STILL run (mirror.sh's `|| log`
# never stops), so the mirror must receive the store's discarded ref under
# refs/kept/tk/. On the buggy code the team fetch's `return rc` skips the
# loop, so this ref is ABSENT -> the cell is RED.
git init -q "$tmp/store3src"
git -C "$tmp/store3src" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "store3: kept attempt"
git -C "$tmp/store3src" update-ref "refs/jichi/discarded/kept1" HEAD
mkdir -p "$tmp/dir3/home/.jichi.d/checkpoints"
cp -r "$tmp/store3src" "$tmp/dir3/home/.jichi.d/checkpoints/tk"
printf '\n[jichi]\n\tworktree = %s\n' "$tmp/dir3/team" >> "$tmp/dir3/home/.jichi.d/checkpoints/tk/config"
rm -rf "$tmp/dir3/team"
"$HBIN" mirror fetch --config "$tmp/cfg3.json" "$tmp/dir3/mirror.git" \
    < /dev/null > "$tmp/f3" 2> "$tmp/f3.err"
rc3=$?
_kept3=$(git -C "$tmp/dir3/mirror.git" rev-parse --short "refs/kept/tk/kept1" 2>/dev/null)
if [ "$rc3" -ne 1 ]; then
    t_fail "3: transport failure (missing team clone) exited $rc3 (want 1) -- the fetch did not fail at the transport layer: $(tr '\n' ' ' < "$tmp/f3" "$tmp/f3.err" | head_bytes 160)"
elif ! grep -qiE "repository|does not appear|could not read|unable to|fatal" "$tmp/f3" "$tmp/f3.err"; then
    t_fail "3: transport failure exited 1 but the report does not name the git error (first ~200 bytes): $(tr '\n' ' ' < "$tmp/f3" "$tmp/f3.err" | head_bytes 200)"
elif [ -z "$_kept3" ]; then
    t_fail "3: the KEPT-ATTEMPT loop did not run after the transport failure (refs/kept/tk/kept1 is ABSENT in the mirror) -- mirror.sh's \`|| log\` never stops: the kept-attempt fetches must run even when the team fetch failed"
else
    t_ok "3: a transport failure exits 1 and names the git error, AND the kept-attempt loop still ran (refs/kept/tk/kept1=$_kept3 landed in the mirror)"
fi

# --- 4: W3 -- a tampered mirror config is refused BEFORE any fetch (exit 2) -
printf '\n[core]\n\tfsmonitor = touch /tmp/m5-plant\n' >> "$mirror/config"
rm -f "$mirror/FETCH_HEAD"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f4" 2> "$tmp/f4.err"
rc4=$?
cp "$stored" "$mirror/config"
if [ "$rc4" -ne 2 ]; then
    t_fail "4: W3 tampered mirror config exited $rc4 (want 2) -- the config-bytes check vs the stored copy did not refuse: $(tr '\n' ' ' < "$tmp/f4.err" | head_bytes 200)"
elif ! grep -qiE "config" "$tmp/f4.err" "$tmp/f4"; then
    t_fail "4: W3 the refusal does not name the config check: $(tr '\n' ' ' < "$tmp/f4.err" | head_bytes 200)"
elif [ -e "$mirror/FETCH_HEAD" ]; then
    t_fail "4: W3 the fetch RAN after the config check failed (FETCH_HEAD appeared) -- the checks must precede any git child"
else
    t_ok "4: W3 a tampered mirror config (bytes != stored copy) is refused, exit 2, naming the config check, before any fetch"
fi

# --- 5: the stored copy missing -> refused, exit 2, naming the check -------
mv "$stored" "$tmp/stored.away"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f5" 2> "$tmp/f5.err"
rc5=$?
mv "$tmp/stored.away" "$stored"
if [ "$rc5" -ne 2 ] || ! grep -qiE "stored" "$tmp/f5.err" "$tmp/f5"; then
    t_fail "5: a mirror whose stored config copy is missing exited $rc5 and/or does not name the stored-copy check: $(tr '\n' ' ' < "$tmp/f5.err" | head_bytes 200)"
else
    t_ok "5: a missing stored config copy is refused, exit 2, naming the check (check 3 cannot run)"
fi

# --- 6: a planted `objects/info/alternates` -> refused, exit 2, named ------
mkdir -p "$mirror/objects/info"
printf '%s\n' "$team/.git/objects" > "$mirror/objects/info/alternates"
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f6" 2> "$tmp/f6.err"
rc6=$?
rm -f "$mirror/objects/info/alternates"
rmdir "$mirror/objects/info" 2>/dev/null
if [ "$rc6" -ne 2 ] || ! grep -qiE "alternates" "$tmp/f6.err" "$tmp/f6"; then
    t_fail "6: a mirror with an \`objects/info/alternates\` file exited $rc6 and/or does not name the alternates check: $(tr '\n' ' ' < "$tmp/f6.err" | head_bytes 200)"
else
    t_ok "6: a planted \`objects/info/alternates\` file is refused, exit 2, naming the check (the object-store recursion, def-339)"
fi

# --- 7: W4 -- a PLANTED executable hook in hooks/ -> refused, exit 2 -------
# init removes the .sample templates, so a clean harness mirror's hooks/ is
# empty; ANY regular file there (a planted post-receive) is refused.
printf '#!/bin/sh\ntouch /tmp/m5-hook-fired\n' > "$mirror/hooks/post-receive"
chmod 755 "$mirror/hooks/post-receive"
rm -f /tmp/m5-hook-fired
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f7" 2> "$tmp/f7.err"
rc7=$?
rm -f "$mirror/hooks/post-receive"
if [ "$rc7" -ne 2 ] || ! grep -qiE "hooks" "$tmp/f7.err" "$tmp/f7"; then
    t_fail "7: a planted executable hook in hooks/ exited $rc7 and/or does not name the hooks check: $(tr '\n' ' ' < "$tmp/f7.err" | head_bytes 200)"
else
    t_ok "7: W4 a planted executable hook in hooks/ is refused, exit 2, naming the check (git's own \`.sample\` templates are tolerated)"
fi

# --- 8: a non-bare dir (the team clone) -> refused, exit 2, naming the check
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$team" \
    < /dev/null > "$tmp/f8" 2> "$tmp/f8.err"
rc8=$?
if [ "$rc8" -ne 2 ] || ! grep -qiE "bare|HEAD" "$tmp/f8.err" "$tmp/f8"; then
    t_fail "8: a NON-bare directory (team/) exited $rc8 and/or does not name the bare check: $(tr '\n' ' ' < "$tmp/f8.err" | head_bytes 200)"
else
    t_ok "8: a non-bare dir is refused, exit 2, naming the bare check (HEAD is not a symref at the top level)"
fi

# --- 9: W4/W5 -- the kept-attempt fetch (the script's checkpoint loop) -----
# A checkpoint store whose config's `worktree =` line equals the team workdir:
# its refs/jichi/discarded/* are fetched into refs/kept/<k>/ in the mirror.
# A store whose worktree is NOT the team's is skipped, not errored (the
# 2026-10-18 incident). A store whose git refset is empty is a report line,
# not fatal.
cp -r "$team" "$tmp/store"
git -C "$tmp/store" update-ref "refs/jichi/discarded/attempt1" "$BR/main"
# The checkpoint store's config carries a `worktree =` line (the jichi store
# wrote it; the driver appends a [jichi] section so the git config stays
# valid, the note's exact sed shape: a `worktree = <path>` line, whitespace-
# padded).
printf '\n[jichi]\n\tworktree = %s\n' "$team" >> "$tmp/store/config"
cp -r "$tmp/store" "$tmp/store-other"
printf '\n[jichi]\n\tworktree = /tmp/some-other-workdir\n' >> "$tmp/store-other/config"
cp -r "$tmp/store" "$tmp/store-empty"   # a matching worktree, but no discarded ref
git -C "$tmp/store-empty" update-ref -d "refs/jichi/discarded/attempt1" 2>/dev/null
mkdir -p "$dir/home/.jichi.d/checkpoints"
cp -r "$tmp/store" "$dir/home/.jichi.d/checkpoints/k1"
cp -r "$tmp/store-other" "$dir/home/.jichi.d/checkpoints/k2"
cp -r "$tmp/store-empty" "$dir/home/.jichi.d/checkpoints/k3"

"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f9" 2> "$tmp/f9.err"
rc9=$?
_kept_a=$(git -C "$mirror" rev-parse --short "refs/kept/k1/attempt1" 2>/dev/null)
_src_a=$(git -C "$tmp/store" rev-parse --short "refs/jichi/discarded/attempt1" 2>/dev/null)
if [ "$rc9" -ne 0 ]; then
    t_fail "9: W5 the happy fetch (with a matching checkpoint store) exited $rc9 (want 0): $(tr '\n' ' ' < "$tmp/f9.err" | head_bytes 200)"
elif [ -z "$_kept_a" ] || [ "$_kept_a" != "$_src_a" ]; then
    t_fail "9: W5 the kept-attempt fetch did not bring the store's discarded ref into refs/kept/k1/ (mirror '$_kept_a', store '$_src_a'): $(tr '\n' ' ' < "$tmp/f9" | head_bytes 200)"
else
    t_ok "9: W5 a checkpoint store whose \`worktree =\` line equals the team workdir is fetched into refs/kept/k1/ ($_kept_a); a store that is NOT the team's is skipped (k2); an empty one is a report line (k3), not fatal"
fi

# --- 10: W7 -- mirror head: the mirror's $BR/main short hash, quiet failure -
out10=$("$HBIN" mirror head --config "$tmp/cfg.json" "$mirror" < /dev/null 2>/dev/null)
rc10=$?
want10=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
if [ "$rc10" -ne 0 ] || [ "$out10" != "$want10" ]; then
    t_fail "10: W7 \`mirror head\` on the mirror: rc=$rc10 output='$out10' (want '$want10') -- mirror_head must print the mirror's $BR/main short hash"
else
    # a mirror WITHOUT the ref: the script's 2>/dev/null shape -> empty stdout
    git -C "$mirror" update-ref -d "refs/heads/$BR/main" 2>/dev/null
    out10m=$("$HBIN" mirror head --config "$tmp/cfg.json" "$mirror" < /dev/null 2>/dev/null)
    rc10m=$?
    # restore via a fetch (the refspec re-creates it)
    "$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" < /dev/null >/dev/null 2>&1
    if [ -n "$out10m" ]; then
        t_fail "10: W7 \`mirror head\` with a missing ref printed '$out10m' (want empty -- the script's 2>/dev/null shape)"
    else
        t_ok "10: W7 \`mirror head\` prints the mirror's $BR/main short hash ($out10); a missing ref prints nothing (quiet failure)"
    fi
fi

# --- 11: W6 -- the fetch child's isolation (source, the M2 idiom) ----------
MIRROR_SRC="$ROOT/src/harness/jc_harness_mirror.c"
if [ ! -f "$MIRROR_SRC" ]; then
    t_fail "11: the mirror source $MIRROR_SRC is absent"
else
    _n=0
    for want in "core.fsmonitor=" "log.showSignature=false" "commit.gpgsign=false" \
                "core.hooksPath=/dev/null" "GIT_CONFIG_GLOBAL" "GIT_CONFIG_SYSTEM" \
                "core.alternateRefsCommand="; do
        grep -qF -- "$want" "$MIRROR_SRC" && _n=$((_n + 1))
    done
    if [ "$_n" -ne 7 ]; then
        t_fail "11: W6 the mirror source carries $_n of 7 isolation tokens (M2 set + empty GIT_CONFIG_GLOBAL/SYSTEM + core.alternateRefsCommand=): $(for want in "core.fsmonitor=" "log.showSignature=false" "commit.gpgsign=false" "core.hooksPath=/dev/null" "GIT_CONFIG_GLOBAL" "GIT_CONFIG_SYSTEM" "core.alternateRefsCommand="; do grep -qF -- "$want" "$MIRROR_SRC" || printf 'missing %s ' "$want"; done)"
    else
        t_ok "11: W6 the fetch child carries the M2 set + GIT_CONFIG_GLOBAL/SYSTEM at empty files + -c core.alternateRefsCommand= (a cheap source lint; the behavioural evidence for behaviour 4 is cell 14)"
    fi
fi

# --- 12/13: W8-env (C9) -- the fetch child's ENVIRONMENT, measured --------
# The instrument (the note's W8-env, supervisor 2026-10-24 22:45): a git
# WRAPPER first on PATH writes `env` (APPEND, so every git child in the run
# appends its own environment) and then execs the REAL git. The real git's
# path is resolved at RUNTIME (`command -v git`) -- the driver never assumes
# /usr/bin. git_m2 runs git by NAME (jc_harness_mirror.c:428,
# argv[0]="git" -> PATH lookup via execvp / posix_spawnp), so the fetch child
# hits the wrapper and the dump IS the child's own environment.
#
# Two canaries are exported into the harness process for this ONE run only
# (`VAR=... cmd` prefixes, so they do not leak into any other cell):
#   GITHUB_TOKEN  = a builtin DENY-LIST name (jc_proc.c:71-78) -> the SCRUB
#                   target. jc_spawn_build_envp (scrub=1) copies ALL of
#                   environ and drops only the deny-list names, so it must
#                   be ABSENT from the fetch child's dump.
#   JH_ENV_CANARY = an UNREGISTERED name -> NOT on the deny-list. C9: only
#                   the deny-list drops names, so it must CROSS (the truth
#                   about "minimal env").
# GIT_CONFIG_GLOBAL= in the dump proves the instrument read a FETCH child
# (git_m2 appends the two empty configured files to every fetch child's env),
# not just any child.
#
# DROPPED vs the previous draft: the uploadpack.packObjectsHook plant that
# measured the GIT_CONFIG isolation (whose ok line said "env-dump ABSENT" --
# a pass having read nothing, the vacuous shape the supervisor named). The
# isolation is already held by cell 11 (W6, source grep); the packObjectsHook
# instrument measured the wrong thing for W8-env.

_w8scrub="SCRUB-CANARY-$$"
_w8plain="PLAIN-CANARY-$$"
_w8realgit=$(command -v git)
_w8dump="$tmp/w8dump"
# The argv dump: cell 15 (the canary-leak assertion) greps BOTH the env
# dumps' argv side and every f*.out/f*.err for the two canaries.
_w8argv="$tmp/w8argv"
rm -f "$_w8dump" "$_w8argv"

# Two wrapper dirs, each holding a `git` script, both appending env+argv to the
# SAME _w8dump/_w8argv files: the cell-12 harness run and the cell-13 control
# run are two git children of one instrument (a second git child appends).
_w8wdir="$tmp/w8wrap"
_w8wctl="$tmp/w8wrapctl"
mkdir -p "$_w8wdir" "$_w8wctl"
printf '#!/bin/sh\nenv >> %s\necho "ARGV: $*" >> %s\nexec %s "$@"\n' "$_w8dump" "$_w8argv" "$_w8realgit" > "$_w8wdir/git"
chmod 755 "$_w8wdir/git"
printf '#!/bin/sh\nenv >> %s\necho "ARGV: $*" >> %s\nexec %s "$@"\n' "$_w8dump" "$_w8argv" "$_w8realgit" > "$_w8wctl/git"
chmod 755 "$_w8wctl/git"

# The kept-attempt store: one NEW commit on its discarded ref, `worktree =`
# the team (so the kept fetch is not skipped), which makes the mirror fetch
# do real work and spawn the wrapper.
_w8ck="$dir/home/.jichi.d/checkpoints/w8k"
git init -q "$_w8ck"
git -C "$_w8ck" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "w8: kept attempt (new commit on the discarded ref)"
git -C "$_w8ck" update-ref "refs/jichi/discarded/w8a" HEAD
printf '\n[jichi]\n\tworktree = %s\n' "$team" >> "$_w8ck/config"

# (12) the harness fetch, wrapper first on PATH + both canaries in the
# harness process env. The fetch child's env is SCRUBBED (deny-list), so the
# registered-name canary must be ABSENT and the unregistered canary PRESENT.
# SUBSHELL: the driver's own GIT_CONFIG_GLOBAL/SYSTEM export (line ~90,
# /dev/null) is UNSET first, so the only GIT_CONFIG_* value that can appear in
# the dump is the HARNESS's own (behaviour 4) -- the supervisor's 01:17
# finding was that the driver's own export satisfied the old "holds
# GIT_CONFIG_GLOBAL=" guard, which proved the wrapper ran, not that the
# harness set the variable.
(
    unset GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM
    export GITHUB_TOKEN="$_w8scrub" JH_ENV_CANARY="$_w8plain"
    export PATH="$_w8wdir:$PATH"
    "$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
        < /dev/null > "$tmp/f12" 2> "$tmp/f12.err"
)
rc12=$?
if [ "$rc12" -ne 0 ]; then
    t_fail "12: W8-env the harness fetch (wrapper on PATH, driver export unset) exited $rc12 (want 0): $(tr '\n' ' ' < "$tmp/f12.err" | head_bytes 200)"
elif [ ! -e "$_w8dump" ]; then
    t_fail "12: W8-env the env-dump $_w8dump is ABSENT -- the PATH wrapper never ran, so this cell passed having read nothing (the vacuous shape the supervisor named); a fetch child must hit the wrapper"
else
    # The HARNESS's OWN GIT_CONFIG value (supervisor 01:17 item 2): the dump
    # must carry GIT_CONFIG_GLOBAL/SYSTEM, and the value must point at a file
    # that EXISTS, is EMPTY (0 bytes), and lives UNDER the harness's dir
    # ($dir) -- the two files make_empty_env_files writes there. The driver's
    # /dev/null export cannot satisfy the "under $dir" half (and is unset
    # above anyway), and a DISABLED isolation (no configured entry, or the
    # driver's value) is caught. The last entry per key: the harness's
    # configured entry (setenv overwrite) is appended after any inherited
    # copy, which the scrub drops.
    _gc=$(grep '^GIT_CONFIG_GLOBAL=' "$_w8dump" | tail -n 1)
    _sc=$(grep '^GIT_CONFIG_SYSTEM=' "$_w8dump" | tail -n 1)
    _gcv=${_gc#GIT_CONFIG_GLOBAL=}
    _scv=${_sc#GIT_CONFIG_SYSTEM=}
    _gc_ok=0; _sc_ok=0
    case "$_gcv" in "$dir"/*) [ -f "$_gcv" ] && [ ! -s "$_gcv" ] && _gc_ok=1 ;; esac
    case "$_scv" in "$dir"/*) [ -f "$_scv" ] && [ ! -s "$_scv" ] && _sc_ok=1 ;; esac
    if [ "$_gc_ok" -ne 1 ] || [ "$_sc_ok" -ne 1 ]; then
        t_fail "12: W8-env the harness's OWN GIT_CONFIG isolation is not in the child: GIT_CONFIG_GLOBAL='$(_gc)' / GIT_CONFIG_SYSTEM='$(_sc)' -- each must point at a file under the harness's dir ($dir) that exists and is empty (make_empty_env_files); a DISABLED isolation (no entry, or the driver's /dev/null) leaves the poisoned global reachable (cell 14 shows it)"
    elif grep -q "$_w8scrub" "$_w8dump"; then
        t_fail "12: W8-env the dump CONTAINS the registered-name canary GITHUB_TOKEN=$_w8scrub -- the env SCRUB (jc_spawn_build_envp deny-list) did NOT drop it"
    elif ! grep -q "$_w8plain" "$_w8dump"; then
        t_fail "12: W8-env the dump does NOT contain the unregistered-name canary JH_ENV_CANARY=$_w8plain -- it should cross (C9: only the deny-list drops names), so its absence means the instrument is broken"
    else
        t_ok "12: W8-env the fetch child's env-dump (a) EXISTS and holds the HARNESS's OWN GIT_CONFIG_GLOBAL= / GIT_CONFIG_SYSTEM= at empty files under the harness's dir (it read a real fetch child, and the harness's isolation -- not the driver's /dev/null export -- is what the child got), (b) does NOT hold the registered-name canary GITHUB_TOKEN=$_w8scrub (the SCRUB dropped it), (c) DOES hold the unregistered-name canary JH_ENV_CANARY=$_w8plain (C9: only the deny-list drops names -- 'minimal env' is false)"
    fi
fi

# (13) the TOOTH: a REAL git command run from the DRIVER shell (not through
# the harness, so NO scrub; env inherited) with the second wrapper dumping
# env+argv to the SAME files (a second git child in the run appends). BOTH
# canaries must be PRESENT here -- proving the instrument captures everything
# the shell has, so cell 12's absence of the registered canary is the scrub,
# not a dead instrument.
GITHUB_TOKEN="$_w8scrub" JH_ENV_CANARY="$_w8plain" PATH="$_w8wctl:$PATH" \
    git -C "$mirror" rev-parse --git-dir > /dev/null 2>&1
rc13=$?
if [ "$rc13" -ne 0 ]; then
    t_fail "13: W8-env control the driver's plain git (rev-parse --git-dir) exited $rc13 (want 0) -- the tooth's git child did not run"
elif [ ! -e "$_w8dump" ]; then
    t_fail "13: W8-env control the env-dump $_w8dump is ABSENT -- the PATH wrapper did not run, so the tooth is dead"
elif ! grep -q "$_w8scrub" "$_w8dump"; then
    t_fail "13: W8-env control the dump does NOT contain GITHUB_TOKEN=$_w8scrub -- a plain (unscrubbed) driver-shell git must carry BOTH canaries; the tooth is broken"
elif ! grep -q "$_w8plain" "$_w8dump"; then
    t_fail "13: W8-env control the dump does NOT contain JH_ENV_CANARY=$_w8plain -- a plain (unscrubbed) driver-shell git must carry BOTH canaries; the tooth is broken"
else
    t_ok "13: W8-env control (the TOOTH): a REAL git run from the driver shell (no scrub, env inherited) carries BOTH canaries in its dump (GITHUB_TOKEN=$_w8scrub AND JH_ENV_CANARY=$_w8plain) -- so the instrument captures everything the shell has, and cell 12's absence of the registered canary is the SCRUB, not a dead instrument"
fi

# --- 14: W6-behavioural -- the GIT_CONFIG isolation, in the real threat's shape ---
# Supervisor 2026-10-25 01:17 item 1: the driver's OWN export
# (GIT_CONFIG_GLOBAL=/dev/null, line ~90) masked the harness's isolation, so no
# cell could see it removed. This cell runs the harness in a SUBSHELL that
# first UNSETS that export and then points HOME at a fake home whose .gitconfig
# plants uploadpack.packObjectsHook (a marker-touching hook that execs the real
# git, so the fetch still works). The fixture commits in the subshell are run
# with GIT_CONFIG_GLOBAL=/dev/null (their own export) so they never read the
# poisoned global -- the only git child that CAN fire the hook is the harness
# fetch's serve child, which sees the global only if the isolation is off.
# The cell asserts the marker ABSENT after the harness fetch (the isolation
# stopped the poisoned global) and the team ref CARRIED (the fetch landed); a
# plain-git control fetch (no -c flags, no harness) in the SAME poisoned env
# makes the marker PRESENT -- the instrument can fire, so the absence is the
# isolation, not a dead hook. The supervisor's `j < 0` perturbation (isolation
# off in the harness) is their tooth on the pushed head; this cell makes a
# disabled isolation VISIBLE here (marker present -> red) rather than the
# supervisor having to run it.
_iso_home="$tmp/isohome"
_iso_hook="$tmp/isohook"
_iso_marker="$tmp/iso_marker"
mkdir -p "$_iso_home" "$_iso_hook"
cat > "$_iso_hook/packhook" <<EOF
#!/bin/sh
# The packObjectsHook receives the pack-objects args (git pack-objects ...):
# touch the marker, drop the leading git token (it names the hook's own
# binary), and exec the REAL git (resolved at runtime -- the driver never
# assumes a fixed path) so the serve child's pack still works. The flag guard
# keeps a fixture commit (run without JH_ISO_PACK) from firing it: only the
# harness fetch's serve child, which inherits the flag, can.
if [ -n "\$JH_ISO_PACK" ]; then
    touch "$_iso_marker"
fi
shift
exec $(command -v git) "\$@"
EOF
chmod 755 "$_iso_hook/packhook"
cat > "$_iso_home/.gitconfig" <<EOF
[uploadpack]
	packObjectsHook = $_iso_hook/packhook
EOF
# Fixture: one NEW commit c1 on the team BEFORE the poisoned env is set, so
# the driver's own GIT_CONFIG_GLOBAL=/dev/null export (still in effect) and
# JH_ISO_PACK (still unset) keep this commit off the poisoned global. c1 is on
# $BR/main, so the harness fetch below (run in the poisoned env) packs and
# carries it. A fetch of an already-fetched ref would NOT pack, so c1 must be
# new at fetch time.
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "iso: c1 (pack during the harness fetch)" < /dev/null
_iso_c1=$(git -C "$team" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
# Save the driver's env, then UNSET the driver's export (the bug this cell
# exists for) and point HOME at the fake home. The poisoned env is restored at
# the end of the cell, so the later cells run under the driver's own env.
_iso_gc_old=${GIT_CONFIG_GLOBAL:-}
_iso_sc_old=${GIT_CONFIG_SYSTEM:-}
_iso_home_old=${HOME:-}
unset GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM
HOME="$_iso_home"; export HOME
JH_ISO_PACK=1; export JH_ISO_PACK
rm -f "$_iso_marker"
# The harness fetch, in the poisoned env, carrying c1. If the isolation is ON
# the serve child reads the harness's empty global, the hook is not seen, and
# the marker stays ABSENT; if the isolation is OFF the poisoned global is read,
# the hook fires, and the marker is PRESENT.
"$HBIN" mirror fetch --config "$tmp/cfg.json" "$mirror" \
    < /dev/null > "$tmp/f14" 2> "$tmp/f14.err"
_iso_rc=$?
_iso_h_marker=$([ -e "$_iso_marker" ] && echo PRESENT || echo ABSENT)
_iso_h_tip=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
# A second NEW commit c2 on the team (in the poisoned env; a commit does not
# fire a pack hook, so the marker is untouched by it) so the control fetch
# below really packs a new object.
git -C "$team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
    commit -q --allow-empty -m "iso: c2 (pack during the control fetch)" < /dev/null
_iso_c2=$(git -C "$team" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
# CONTROL: a plain `git fetch` (NOT through the harness, no -c flags, no
# isolation) in the SAME poisoned env, carrying c2. Only the main refspec is
# fetched (c1 -> c2 is a clean fast-forward, so the fetch exits 0); the work/*
# wildcard is left out because cell 2 left a non-FF work branch in the mirror,
# which would reject on rc 1 and obscure the control's purpose. The serve child
# reads the poisoned global, the hook fires, the marker is PRESENT -- proving
# the instrument can fire, so the harness fetch's ABSENT marker is the
# isolation, not a dead hook.
git -C "$mirror" fetch -q --no-tags "$team" \
    "refs/heads/$BR/main:refs/heads/$BR/main" \
    < /dev/null > /dev/null 2>&1
_iso_c_rc=$?
_iso_c_marker=$([ -e "$_iso_marker" ] && echo PRESENT || echo ABSENT)
_iso_c_tip=$(git -C "$mirror" rev-parse --short "refs/heads/$BR/main" 2>/dev/null)
# Restore the driver's env (the later cells expect the driver's own export).
if [ -n "$_iso_gc_old" ]; then GIT_CONFIG_GLOBAL="$_iso_gc_old"; export GIT_CONFIG_GLOBAL; fi
if [ -n "$_iso_sc_old" ]; then GIT_CONFIG_SYSTEM="$_iso_sc_old"; export GIT_CONFIG_SYSTEM; fi
HOME="$_iso_home_old"; export HOME
unset JH_ISO_PACK
# Reset team main from c2 back to c1, so the team is left at the tip the HARNESS
# fetch carried (not the control's c2, which exists only to exercise the hook).
git -C "$team" update-ref "refs/heads/$BR/main" "$_iso_c1" 2>/dev/null
if [ "$_iso_rc" -ne 0 ]; then
    t_fail "14: W6-behavioural the harness fetch in the poisoned-home env exited $_iso_rc (want 0): $(tr '\n' ' ' < "$tmp/f14.err" | head_bytes 200)"
elif [ "$_iso_h_marker" != "ABSENT" ]; then
    t_fail "14: W6-behavioural the poisoned global packObjectsHook FIRED during the harness fetch (marker $_iso_h_marker) -- the GIT_CONFIG isolation is OFF (or the driver's export is not being unset); a disabled isolation is visible here, as the supervisor's j<0 perturbation would make it"
elif [ -z "$_iso_h_tip" ] || [ "$_iso_h_tip" != "$_iso_c1" ]; then
    # The team ref must have been CARRIED into the mirror by the harness fetch.
    t_fail "14: W6-behavioural the team ref was not carried by the harness fetch (mirror '$_iso_h_tip' vs the carried tip '$_iso_c1') -- the fetch did not land"
elif [ "$_iso_c_rc" -ne 0 ]; then
    t_fail "14: W6-behavioural the plain-git control fetch exited $_iso_c_rc (want 0) -- the control did not run, so the marker-present assertion is not a proof"
elif [ "$_iso_c_marker" != "PRESENT" ]; then
    t_fail "14: W6-behavioural the plain-git control fetch did NOT fire the poisoned hook (marker $_iso_c_marker) -- the instrument cannot fire, so the harness fetch's ABSENT marker proves nothing (the hook is dead or the poisoned global is not reachable)"
elif [ -z "$_iso_c_tip" ] || [ "$_iso_c_tip" != "$_iso_c2" ]; then
    t_fail "14: W6-behavioural the plain-git control fetch did not carry c2 (mirror '$_iso_c_tip' vs '$_iso_c2') -- the control did not do real pack work, so its marker does not prove the hook fires on a real fetch"
else
    t_ok "14: W6-behavioural the GIT_CONFIG isolation holds in the real threat's shape: a fake home's .gitconfig plants uploadpack.packObjectsHook, the driver's export is UNSET, a NEW team commit makes the serve child pack -- the harness fetch leaves the marker ABSENT and carries c1 ($_iso_h_tip); a plain-git control fetch in the same poisoned env carries c2 and makes the marker PRESENT (the instrument can fire, so the absence is the isolation)"
fi

# --- 15: W8-canary-leak -- neither rehearsal canary appears in any capture ----
# Supervisor 2026-10-25 01:17 item 3 (the W8 note's "the existing cells keep
# this" was WRONG #2 -- no cell asserted it). The two strings swept are the
# REHEARSAL'S REAL secrets, named by the note's W8 line ("the rehearsal's token
# canary and the model-key canary") -- NOT the W8-scrub env canaries of cells
# 12-13 (SCRUB-CANARY-$$ / PLAIN-CANARY-$$, the VALUES carried by the GITHUB_TOKEN
# and JH_ENV_CANARY names: those measure the env SCRUB, a different thing):
#   the token canary = the literal 'CANARY-TOKEN-DO-NOT-PRINT', the content of
#                      $tmp/tokfile (the config's gitTokenFile, driver line ~136)
#   the key canary   = $_kc, the random hex in $tmp/kreal (the config's
#                      modelKeyFile, driver lines ~134-135)
# SWEEP UNIVERSE -- every real capture file this driver wrote under $tmp
# (auditable; the files each entry names TODAY):
#   $tmp/binv             -- the $BIN --version stdout (driver line ~98)
#   $tmp/i1 $tmp/i1.err   -- the init stdout/stderr (driver line ~148)
#   $tmp/i3 $tmp/i3.err   -- the dir3 init stdout/stderr (driver line ~270)
#   $tmp/fN $tmp/fN.err   -- each fetch's stdout/stderr (N = 1,2,...,9,12,14).
#                            The STDOUT has NO extension (f1, f12, ...) -- the
#                            first report's f*.out glob was DEAD: it matched
#                            nothing, so a canary in the fetch's stdout would
#                            not have turned this red. Cells 10-11 and 13 take
#                            no capture file (a shell var / a source read /
#                            /dev/null), so f10/f11/f13 do not exist.
#   $_w8dump $_w8argv     -- the cell-12 git wrapper's env dump and argv dump
#                            ($tmp/w8dump, $tmp/w8argv). The env dump is included
#                            because a canary could in principle reach a child's
#                            env (the argv dump was already swept).
# EXCLUDED: $tmp/tokfile and $tmp/kreal THEMSELVES -- they are the secret FILES
# the driver creates, and their content IS the canary by design; sweeping them
# would red this cell unconditionally.
# Assert NEITHER canary appears in ANY swept file. The M1056 push driver holds
# the same for the push (the push is where the token is actually handled).
_w8leaked=""
for _w8f in "$tmp/binv" \
            "$tmp/i1" "$tmp/i1.err" "$tmp/i3" "$tmp/i3.err" \
            "$tmp/f1" "$tmp/f1.err" "$tmp/f2" "$tmp/f2.err" \
            "$tmp/f3" "$tmp/f3.err" "$tmp/f4" "$tmp/f4.err" \
            "$tmp/f5" "$tmp/f5.err" "$tmp/f6" "$tmp/f6.err" \
            "$tmp/f7" "$tmp/f7.err" "$tmp/f8" "$tmp/f8.err" \
            "$tmp/f9" "$tmp/f9.err" "$tmp/f12" "$tmp/f12.err" \
            "$tmp/f14" "$tmp/f14.err" "$_w8dump" "$_w8argv"; do
    [ -f "$_w8f" ] || continue
    if "$G" -F -e 'CANARY-TOKEN-DO-NOT-PRINT' -e "$_kc" "$_w8f" >/dev/null 2>&1; then
        _w8leaked="$_w8leaked $_w8f"
    fi
done
if [ -n "$_w8leaked" ]; then
    t_fail "15: W8-canary-leak a rehearsal canary appeared in a real driver capture -- the token canary (CANARY-TOKEN-DO-NOT-PRINT) or the model-key canary ($_kc) leaked into:$_w8leaked"
else
    t_ok "15: W8-canary-leak neither the token canary (CANARY-TOKEN-DO-NOT-PRINT, $tmp/tokfile) nor the model-key canary ($_kc, $tmp/kreal) appears in ANY real capture the driver produced -- the f*/i*/binv stdout+stderr, or the cell-12 git wrapper's env/argv dumps (the secret files themselves are excluded; the M1055 side of W8, M1056 holds it for the push)"
fi

t_done
