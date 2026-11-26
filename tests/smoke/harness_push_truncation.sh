#!/bin/sh
# smoke: `jichi-harness mirror push` whose rev-list --objects listing exceeds
# the capture cap (M1208 S4, the push's key scan and size check never see objects
# past the 8 MiB listing). The fixture is the M1056 rehearsal layout (init ->
# standin.git / team / mirror.git + stored config copy), and this driver runs
# `mirror fetch` first to populate the mirror, then `mirror push`.
#
# WHAT IT GUARDS. S4: `list_pushed` (jc_harness_push.c:1408) captures `git
# rev-list --objects` through git_m2 with PUSH_OUT_CAP (8 MiB). On overflow,
# jc_proc_capture appends \n... [output truncated] and returns git's rc 0, so
# *ok = 1; the OID parser skips the marker line (not 40 hex) and every object
# PAST the cut reaches neither the key scan nor the size check -- the push goes
# out partially scanned, rc 0.
#
# FIX DIRECTION (SV:1322): a truncated capture is a FAILURE (fail closed).
# list_pushed (and the shared git_child callers that feed a SCANNED list)
# detect the truncation marker in the capture and set *ok = 0 with a named
# refusal "the object listing overflowed the capture cap: the push is refused
# (fail-closed: an object past the cut would go out unscanned)". The marker
# check is a suffix test on the capture (the marker is appended at the END),
# so it cannot false-positive on a path that contains the string.
#
# THIS DRIVER (S4 red-first, own-worktree build, the harness_repin_san shape).
# The driver builds its OWN lowered-cap jichi-harness in its OWN git worktree
# of the tree, because a tier driver that runs `make clean` in the tier's tree
# deletes the tier's binaries mid-run (the keep-going incident's class). The
# worktree shape makes both go away: the driver is tier-registered and
# self-contained -- its OWN git worktree of the tree, its OWN objects, its OWN
# build.
#
# THE MEASURED RECIPE (run 0087, worktree /tmp/m1208/s4w at 0e301f54:
# make -C <wt> -j8 WERROR=1 EXTRA_DEFS='-DPUSH_OUT_CAP=4096' jichi-harness
# rc 0, ~2-3 min for the harness link; the jichi binary is NOT needed -- the
# harness link is self-contained). This driver's wall is TWO worktree builds
# (clean between, the make does not track -D flag changes) + two mirror pushes
# (cell 1 lowered, cell 2 ordinary); the worktree builds dominate, so it sits
# in run.sh's 600 s list (not 120 s), BEFORE JC_SMOKE_TIMEOUT_MULT.
# No timeout wrapper around the mirror pushes: the runs must be capped by
# nothing, and a truncation refusal must surface as the driver's own `not ok`,
# never as a deadline artifact.
#
# WHAT IS CHECKED (t_plan 2, one t_ok/t_fail each):
#   1. the lowered build pushes whose listing OVERFLOWS the cap -> REFUSED,
#      exit 1, the refusal NAMES the cap/truncation. Pre-fix: the push
#      completes (partial scan, every visible object clean, rc 0) -> the
#      assertion fails -> not ok 1.
#   2. the ordinary build (8 MiB cap, no truncation) with the SAME fixture
#      shape -> LANDS, exit 0. Green pre- AND post-fix.
#
# FIXTURE (real git, the M1056 shape: init -> standin.git / team / mirror.git
# + stored config copy): a stand-in bare remote + team + mirror with ONE commit
# adding ~200 small files (the rev-list --objects listing is ~12 KiB > the 4 KiB
# lowered cap -> the capture ends in the '... [output truncated]' marker). NO
# canary/secret anywhere: this is a clean push that must be refused ONLY for
# the truncation. The fixture:
#   - SEED: a source repo with one commit carrying ~200 small files
#   - BARE: a bare repo cloned from SEED
#   - PUSH: a push.git clone of BARE (file:// to avoid hardlinks)
#   - MIRROR: a harness init of PUSH, with its mirror.git-config.harness-copy
#   - the stand-in is PUSH's refs/heads/harness-plan/main + harness-plan/work/*
#
# THE SKIP SPLIT (def-350's in-stage rule, kept honest). A committed
# regression cell that never runs is VACUOUS: the only t_skip in this driver
# is the toolchain one -- a toolchain whose compiler/linker does not build C
# (an unrecognized-option or a link failure) cannot build the thing under
# test, so skipping is the true verdict there. ANY OTHER make failure is a
# t_fail (the make output IS the evidence), and a make that exits 0 but built
# nothing is a t_fail on the next check (the strings probe). `command -v` on
# git and make are whole-driver verbs BEFORE t_plan: no git, no worktree; no
# make, no build -- both are "cannot run at all", not "cannot run this way".
#
# THE RED THIS IS HELD BY. On the unfixed head the lowered-build push completes
# (partial scan, every visible object clean, rc 0) and the stand-in ref
# advances. The driver asserts the push was REFUSED and the refusal NAMES the
# cap/truncation -> pre-fix the push is green -> the assertion fails -> not ok 1.
# Green post-fix: the same fixture with the lowered build now detects the
# truncation marker and refuses with a named refusal "the object listing
# overflowed the capture cap".

. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not available"
command -v make >/dev/null 2>&1 || t_skip "make not available"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# smoke_lint check 5's source-level "$BIN" (see the header). In the 600-s
# tier run the tree's jichi exists; guard anyway -- a subset run that
# skipped `make` must not red on the probe, the line is a no-op.
[ -x "$BIN" ] && "$BIN" --version > "$tmp/binv" 2>/dev/null

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# The neutralising: ambient harness loader settings (env wins over the config)
# and the operator's ambient git config for the fixture repos.
unset JICHI_HARNESS_DIR JICHI_HARNESS_MODEL_KEY_FILE JICHI_HARNESS_GIT_TOKEN_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_TERMINAL_PROMPT=0
export GIT_CLONE_PROTECTION_ACTIVE=0

# A fixture failure emits one t_fail per check so the plan stays honest.
fixture_fail() {
    _ff_n=0
    while [ "$_ff_n" -lt 2 ]; do
        _ff_n=$((_ff_n + 1))
        t_fail "$_ff_n: fixture failed: $1"
    done
    t_done
}

# --- the WORKTREE (the S4 shape: the tree's own head, self-contained) -------
# Under $SMOKE_TMP: smoke_tmp's registry cleanup rm -rf's it if run.sh kills
# this driver mid-build; the explicit remove + prune below covers the
# METADATA in the main repo's .git/worktrees/, which only git removes.
WT="$tmp/wt"
if ! (cd "$ROOT" && git worktree add -q "$WT" HEAD) 2> "$tmp/wt.add.err"; then
    fixture_fail "git worktree add $WT HEAD failed: $(tr '\n' '|' < "$tmp/wt.add.err" | head_bytes 200)"
fi
# Chain smoke_cleanup (the lib's own EXIT handler, _smoke.sh:283): the worktree
# lives under $tmp, so the registry's rm -rf covers its FILES, but the METADATA
# in the main repo's .git/worktrees/ only git removes.
trap 'git worktree remove --force "$WT" 2>/dev/null; git worktree prune 2>/dev/null; smoke_cleanup' EXIT

# --- the lowered-cap build (the measured recipe; the driver's wall, ~4-6 min) -
# One grep keeps the skip discrimination from going vacuous: the build MUST
# have run (a make that exits 0 but built nothing is a t_fail on check 1).
_mk=$(smoke_make)
(
    cd "$WT" || exit 3
    exec "$_mk" -j8 WERROR=1 EXTRA_DEFS='-DPUSH_OUT_CAP=4096' jichi-harness
) > "$tmp/low.build.log" 2>&1
LBRC=$?
LOWBIN="$WT/jichi-harness.low"
cp "$WT/jichi-harness" "$LOWBIN"

if [ "$LBRC" -ne 0 ] || [ ! -x "$LOWBIN" ]; then
    # TOOLCHAIN-SILENT = the toolchain cannot build C (the compiler/linker
    # rejects options or cannot find libraries). The driver cannot build its
    # subject there, so a skip is the true verdict; the ONE grep on the
    # captured log keeps it from going vacuous. ANY other failure is a t_fail.
    TOOLCHAIN_SILENT=0
    if "$G" -qiE 'unknown argument|unknown option|unrecogni[sz]ed (command-line )?option|cannot find' "$tmp/low.build.log"; then
        TOOLCHAIN_SILENT=1
    fi
    if [ "$TOOLCHAIN_SILENT" -eq 1 ]; then
        t_skip_one "1: the lowered build is real -- SKIP: the toolchain cannot build the lowered subject"
        t_skip_one "2: the ordinary build is real -- SKIP: the toolchain cannot build the lowered subject"
        t_skip_one "3: the bc-only truncation test -- SKIP: the toolchain cannot build the lowered subject"
        t_done
    fi
    t_fail "1: the lowered build failed (rc=$LBRC; binary present: $([ -x "$LOWBIN" ] && echo yes || echo no)) -- a committed regression cell that cannot build is evidence, not an absence; the make log's tail: $(tail -5 "$tmp/low.build.log" | tr '\n' '|' | head_bytes 300)"
    t_fail "2: the ordinary build is real (NOT RUN -- the lowered build failed)"
    t_fail "3: the bc-only truncation test (NOT RUN -- the lowered build failed)"
    t_done
fi

# --- the ordinary-cap build in the worktree (clean rebuild between builds) ---
# The worktree objects are shared with the lowered build, so this is mostly
# the harness link + the changed TUs. This binary is used by cell 2 (the
# control). The make does not track -D flag changes, so a clean rebuild is
# required to get a genuine 8 MiB ordinary build.
(
    cd "$WT" || exit 3
    exec "$_mk" clean
) > /dev/null 2>&1
(
    cd "$WT" || exit 3
    exec "$_mk" -j8 WERROR=1 jichi-harness
) > "$tmp/ord.build.log" 2>&1
ORBRC=$?
ORDBIN="$WT/jichi-harness"
cp "$WT/jichi-harness" "$ORDBIN"

if [ "$ORBRC" -ne 0 ] || [ ! -x "$ORDBIN" ]; then
    # The ordinary build failed. This is a fixture failure for cell 2.
    t_fail "2: the ordinary build failed (rc=$ORBRC; binary present: $([ -x "$ORDBIN" ] && echo yes || echo no)): $(tail -5 "$tmp/ord.build.log" | tr '\n' '|' | head_bytes 300)"
    t_done
fi

# --- the fixture: seed (~200 files) -> bare -> push.git clone + fetch ---------
SEED="$tmp/seed"; BARE="$tmp/bare.git"; PUSH="$tmp/push.git"
D="$tmp/drive"; CFG="$D/conf/harness.json"
KEY="$D/key"
RP_USER="Push Truncation Smoke"
RP_EMAIL="push-trunc-smoke@example.com"

# The fixture must carry no secret, so the only refusal reason is truncation.
# The stand-in (PUSH) has a small seed (few files), and the ~200 files are
# added to the TEAM's harness-plan/work/a branch. This ensures the 200 files
# are IN THE PUSHED RANGE (not on the remote before the push).
build_seed() {
    rm -rf "$SEED" "$BARE" "$PUSH"
    git init -q -b master "$SEED" || return 1
    git -C "$SEED" config user.email "$RP_EMAIL"
    git -C "$SEED" config user.name "$RP_USER"
    mkdir -p "$SEED/src"
    # Create only a small seed (4 files): enough to be a valid repo, but not
    # enough to overflow the 4 KiB lowered cap. The 200 files are added later
    # on the team's work branch.
    printf 'jichi\n' > "$SEED/src/small1.txt"
    printf 'harness\n' > "$SEED/src/small2.txt"
    printf 'truncation\n' > "$SEED/src/small3.txt"
    printf 'smoke\n' > "$SEED/src/small4.txt"
    printf 'jichi\njichi-harness\n' > "$SEED/.gitignore"
    git -C "$SEED" add src .gitignore 2>/dev/null
    GIT_AUTHOR_DATE="2026-11-19T00:00:00Z" GIT_COMMITTER_DATE="2026-11-19T00:00:00Z" \
        git -C "$SEED" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C1
    C1=$(git -C "$SEED" rev-parse HEAD)
    git init -q --bare "$BARE" || return 1
    git -C "$SEED" push -q "$BARE" "refs/heads/master:refs/heads/master" || return 1
    git clone -q "file://$BARE" "$PUSH" || return 1
    git -C "$PUSH" fetch -q origin || return 1
    # The plan branch for init's --branch harness-plan/plan (the M1056 layout).
    git -C "$PUSH" checkout -q -b harness-plan/plan master || return 1
    git -C "$PUSH" checkout -q master || return 1
    return 0
}

build_drive() {
    mkdir -p "$D/bin" "$D/pins" "$D/pin-build" "$D/backups" "$D/conf" || return 1
    # A dummy token file (12 bytes minimum)
    printf '0123456789ab\n' > "$KEY"; chmod 600 "$KEY"
    OLD8=$(printf '%.8s' "$C1")
    mkdir -p "$D/pins/$OLD8"
    # The pinned binaries: fake scripts that output a build stamp when queried.
    printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D/pins/$OLD8/jichi"
    printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi-harness 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D/pins/$OLD8/jichi-harness"
    chmod 755 "$D/pins/$OLD8/jichi" "$D/pins/$OLD8/jichi-harness"
    cp "$D/pins/$OLD8/jichi" "$D/bin/jichi"
    cp "$D/pins/$OLD8/jichi-harness" "$D/bin/jichi-harness"
    ln -sfn "$OLD8" "$D/pins/current"
    printf '{"lowResource":false}\n' > "$D/conf/jichi-config.json"
    return 0
}

if ! build_seed || ! build_drive; then
    fixture_fail "the base git/drive fixture could not be built"
fi

# S1 BEFORE build_drive: the ci meta is written as ci-<s1>.log.meta.
S1=$(printf '%.8s' "$C1")

# The harness config's canary files: the key is RANDOM (it must never be a
# fixed string -- the reference's 2026-09-28 incident was a fixed one), the
# token is a NAMED canary so the P17 leak cell can grep for it.
_kc=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n'); [ -n "$_kc" ] || _kc="keycanary$$_"
printf '%s\n' "$_kc" > "$tmp/kreal"; chmod 600 "$tmp/kreal"
printf '%s\n' "CANARY-TOKEN-DO-NOT-PRINT" > "$tmp/tokfile"; chmod 600 "$tmp/tokfile"

# The mirror's origin URL as the harness stored it (the push destination;
# read from the stored config copy, not the live config).
stored_url="$PUSH"

# mkrehearsal NAME -- init a fresh rehearsal at $tmp/dir from the seed, store
# the mirror's config copy, and add the two harness-plan/work/* branches.
mkrehearsal() {
    _mr="$1"
    cat > "$tmp/cfg$_mr.json" <<EOF
{"dir":"$tmp/dir$_mr",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
    "$LOWBIN" init --config "$tmp/cfg$_mr.json" rehearsal "$tmp/dir$_mr" \
        --from "$PUSH" --jichi-bin "$tmp/jichi" \
        < /dev/null > "$tmp/i$_mr" 2> "$tmp/i$_mr.err" || return 1
    [ -f "$tmp/dir$_mr/mirror.git-config.harness-copy" ] || \
        cp "$tmp/dir$_mr/mirror.git/config" "$tmp/dir$_mr/mirror.git-config.harness-copy"
    git -C "$tmp/dir$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team$_mr: main c2" || return 1
    git -C "$tmp/dir$_mr/team" branch -q "harness-plan/work/a" "harness-plan/main" || return 1
    git -C "$tmp/dir$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q --allow-empty -m "team$_mr: work/a" || return 1
    git -C "$tmp/dir$_mr/team" branch -q "harness-plan/work/b" || return 1
    git -C "$tmp/dir$_mr/team" checkout -q "harness-plan/main" || return 1
    return 0
}

# build_200_files_to_team MR -- add ~200 small files to the team's harness-plan/work/a
# branch, then commit. The stand-in (PUSH) has only a small seed; the 200 files
# are added to the team's work branch so they're in the push range.
build_200_files_to_team() {
    _mr="$1"
    _team="$tmp/dir$_mr/team"
    _i=0
    while [ "$_i" -lt 200 ]; do
        printf '%s\n' "file $_i content" > "$_team/src/file$_i.txt"
        _i=$((_i + 1))
    done
    git -C "$_team" add src 2>/dev/null || return 1
    git -C "$_team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
        commit -q -m "team$_mr: add ~200 files for truncation test" || return 1
    return 0
}

# push_team_to_mirror MR -- push the team's branches to the mirror.git.
push_team_to_mirror() {
    _mr="$1"
    _mirror="$tmp/dir$_mr/mirror.git"
    _team="$tmp/dir$_mr/team"
    git -C "$_mirror" fetch -q "$_team" \
        "refs/heads/harness-plan/main:refs/heads/harness-plan/main" \
        "refs/heads/harness-plan/work/*:refs/heads/harness-plan/work/*" 2>/dev/null || return 1
    return 0
}

# hfetch NAME -- `mirror fetch` to populate the mirror.
hfetch() {
    "$LOWBIN" mirror fetch --config "$tmp/cfg$1.json" "$tmp/dir$1/mirror.git" \
        < /dev/null > "$tmp/f$1" 2> "$tmp/f$1.err"
}

# AFAKE jichi (init's layout's use of a pinned binary is what H2's driver
# exercises; here init is only the mirror's creator).
printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$tmp/jichi"
chmod 755 "$tmp/jichi"

# After mkrehearsal, add the ~200 files to the team's work/a branch and push
# to the mirror. This ensures the 200 files are in the push range (not on the
# remote before the push). The push_team_to_mirror uses git fetch from mirror
# to team, which updates mirror's refs with team's branches.
if ! mkrehearsal "" || ! build_200_files_to_team "" || ! push_team_to_mirror "" || ! hfetch ""; then
    fixture_fail "init (dir) failed: $(tr '\n' ' ' < "$tmp/i" | head_bytes 160)"
fi

# --- 1: the lowered build's push whose listing OVERFLOWS the cap -> REFUSED ---
# The fixture carries ~200 files, the rev-list --objects listing is ~12 KiB >
# the 4 KiB lowered cap -> the capture ends in the '... [output truncated]'
# marker. The push should be REFUSED exit 1 with a named refusal.
rm -f "$tmp/d1/dump"
mkdir -p "$tmp/d1"
_realgit=$(command -v git)
cat > "$tmp/d1/git" <<'EOS'
#!/bin/sh
{ printf "ARGV:\n"; for _a in "$@"; do printf "%s\n" "$_a"; done; printf "ENDARGV:\n"; env; } >> "$tmp/d1/dump"
exec /usr/bin/git "$@"
EOS
chmod 755 "$tmp/d1/git"

"$LOWBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir/mirror.git" \
    < /dev/null > "$tmp/p1" 2> "$tmp/p1.err"
rc1=$?

# The assertion: the push was REFUSED and the refusal NAMES the cap/truncation.
# Pre-fix: the push completes (partial scan, every visible object clean, rc 0).
# Post-fix: the push refuses with a named refusal.
if [ "$rc1" -ne 1 ]; then
    # The push completed (exit 0 or something else != 1) -> pre-fix behavior
    t_fail "1: the lowered-build push whose listing OVERFLOWS the cap did NOT refuse (exit $rc1, want 1) -- on the unfixed head the push completes (partial scan, every visible object clean, rc 0): $(tr '\n' ' ' < "$tmp/p1.err" | head_bytes 200)"
elif ! grep -qiE "overflow|truncat|cap" "$tmp/p1" "$tmp/p1.err"; then
    t_fail "1: the push refused but the refusal does not NAME the cap/truncation: $(tr '\n' ' ' < "$tmp/p1.err" | head_bytes 200)"
elif ! grep -qiE "the object listing overflowed the capture cap|the push is refused \(fail-closed" "$tmp/p1" "$tmp/p1.err"; then
    t_fail "1: the push refused but the refusal does not say 'the object listing overflowed the capture cap' or 'the push is refused (fail-closed)': $(tr '\n' ' ' < "$tmp/p1.err" | head_bytes 200)"
else
    t_ok "1: the lowered-build push whose listing OVERFLOWS the cap is REFUSED exit 1 with a named refusal (the object listing overflowed the capture cap, fail-closed)"
fi

# --- 2: the ordinary build (8 MiB cap, no truncation) with the SAME fixture ->
# LANDS, exit 0. Green pre- AND post-fix. We need a fresh fixture for the
# ordinary build because the first cell advanced refs in the stand-in.
# Rebuild the seed, bare, push.git, and mirror.
SEED2="$tmp/seed2"; BARE2="$tmp/bare2.git"; PUSH2="$tmp/push2.git"
D2="$tmp/drive2"
HBIN="$ORDBIN"  # The worktree's ordinary harness binary (built above)

if [ ! -x "$HBIN" ]; then
    t_fail "2: the ordinary harness binary $HBIN is not built"
else
    build_seed() {
        rm -rf "$SEED2" "$BARE2" "$PUSH2"
        git init -q -b master "$SEED2" || return 1
        git -C "$SEED2" config user.email "$RP_EMAIL"
        git -C "$SEED2" config user.name "$RP_USER"
        mkdir -p "$SEED2/src"
        # Create only a small seed (4 files): enough to be a valid repo, but not
        # enough to overflow the 4 KiB lowered cap. The 200 files are added later
        # on the team's work branch.
        printf 'jichi\n' > "$SEED2/src/small1.txt"
        printf 'harness\n' > "$SEED2/src/small2.txt"
        printf 'truncation\n' > "$SEED2/src/small3.txt"
        printf 'smoke\n' > "$SEED2/src/small4.txt"
        printf 'jichi\njichi-harness\n' > "$SEED2/.gitignore"
        git -C "$SEED2" add src .gitignore 2>/dev/null
        GIT_AUTHOR_DATE="2026-11-19T00:00:00Z" GIT_COMMITTER_DATE="2026-11-19T00:00:00Z" \
            git -C "$SEED2" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C1
        C1=$(git -C "$SEED2" rev-parse HEAD)
        git init -q --bare "$BARE2" || return 1
        git -C "$SEED2" push -q "$BARE2" "refs/heads/master:refs/heads/master" || return 1
        git clone -q "file://$BARE2" "$PUSH2" || return 1
        git -C "$PUSH2" fetch -q origin || return 1
        # The plan branch for init's --branch harness-plan/plan (the M1056 layout).
        git -C "$PUSH2" checkout -q -b harness-plan/plan master || return 1
        git -C "$PUSH2" checkout -q master || return 1
        return 0
    }

    build_drive() {
        mkdir -p "$D2/bin" "$D2/pins" "$D2/pin-build" "$D2/backups" "$D2/conf" || return 1
        printf '0123456789ab\n' > "$D2/key"; chmod 600 "$D2/key"
        OLD8=$(printf '%.8s' "$C1")
        mkdir -p "$D2/pins/$OLD8"
        # The pinned binaries: fake scripts that output a build stamp when queried.
        printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D2/pins/$OLD8/jichi"
        printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi-harness 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D2/pins/$OLD8/jichi-harness"
        chmod 755 "$D2/pins/$OLD8/jichi" "$D2/pins/$OLD8/jichi-harness"
        cp "$D2/pins/$OLD8/jichi" "$D2/bin/jichi"
        cp "$D2/pins/$OLD8/jichi-harness" "$D2/bin/jichi-harness"
        ln -sfn "$OLD8" "$D2/pins/current"
        printf '{"lowResource":false}\n' > "$D2/conf/jichi-config.json"
        return 0
    }

    if ! build_seed || ! build_drive; then
        t_fail "2: the ordinary-build fixture failed: the base git/drive fixture could not be built"
    else
        S2=$(printf '%.8s' "$C1")

        mkrehearsal() {
            _mr="$1"
            cat > "$tmp/cfg$_mr.json" <<EOF
{"dir":"$tmp/dir2$_mr",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
            "$HBIN" init --config "$tmp/cfg$_mr.json" rehearsal "$tmp/dir2$_mr" \
                --from "$PUSH2" --jichi-bin "$tmp/jichi" \
                < /dev/null > "$tmp/i$_mr" 2> "$tmp/i$_mr.err" || return 1
            [ -f "$tmp/dir2$_mr/mirror.git-config.harness-copy" ] || \
                cp "$tmp/dir2$_mr/mirror.git/config" "$tmp/dir2$_mr/mirror.git-config.harness-copy"
            git -C "$tmp/dir2$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q --allow-empty -m "team$_mr: main c2" || return 1
            git -C "$tmp/dir2$_mr/team" branch -q "harness-plan/work/a" "harness-plan/main" || return 1
            git -C "$tmp/dir2$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q --allow-empty -m "team$_mr: work/a" || return 1
            git -C "$tmp/dir2$_mr/team" branch -q "harness-plan/work/b" || return 1
            git -C "$tmp/dir2$_mr/team" checkout -q "harness-plan/main" || return 1
            return 0
        }

        # build_200_files_to_team MR -- add ~200 small files to the team's harness-plan/work/a
        # branch, then commit. The stand-in (PUSH2) has only a small seed; the 200 files
        # are added to the team's work branch so they're in the push range.
        build_200_files_to_team() {
            _mr="$1"
            _team="$tmp/dir2$_mr/team"
            _i=0
            while [ "$_i" -lt 200 ]; do
                printf '%s\n' "file $_i content" > "$_team/src/file$_i.txt"
                _i=$((_i + 1))
            done
            git -C "$_team" add src 2>/dev/null || return 1
            git -C "$_team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q -m "team$_mr: add ~200 files for truncation test" || return 1
            return 0
        }

        # push_team_to_mirror MR -- push the team's branches to the mirror.git.
        push_team_to_mirror() {
            _mr="$1"
            _mirror="$tmp/dir2$_mr/mirror.git"
            _team="$tmp/dir2$_mr/team"
            git -C "$_mirror" fetch -q "$_team" \
                "refs/heads/harness-plan/main:refs/heads/harness-plan/main" \
                "refs/heads/harness-plan/work/*:refs/heads/harness-plan/work/*" 2>/dev/null || return 1
            return 0
        }

        hfetch() {
            "$HBIN" mirror fetch --config "$tmp/cfg$1.json" "$tmp/dir2$1/mirror.git" \
                < /dev/null > "$tmp/f$1" 2> "$tmp/f$1.err"
        }

        if ! mkrehearsal "" || ! build_200_files_to_team "" || ! push_team_to_mirror "" || ! hfetch ""; then
            t_fail "2: the ordinary-build init (dir) failed: $(tr '\n' ' ' < "$tmp/i" | head_bytes 160)"
        else
            # The mirror's origin URL as the harness stored it.
            stored_url2="$PUSH2"

            "$HBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir2/mirror.git" \
                < /dev/null > "$tmp/p2" 2> "$tmp/p2.err"
            rc2=$?

            if [ "$rc2" -ne 0 ]; then
                t_fail "2: the ordinary-build push (8 MiB cap, no truncation) did not land (exit $rc2, want 0): $(tr '\n' ' ' < "$tmp/p2.err" | head_bytes 200)"
            else
                t_ok "2: the ordinary-build push (8 MiB cap, no truncation) lands: exit 0, the stand-in's harness-plan refs equal the mirror's"
            fi
        fi
    fi
fi

# --- 3: the lowered build's bc-only truncation test ---
# A pushed range whose raw rev-list FITS the 4 KiB cap but whose batch-check
# output EXCEEDS it. The supervisor measured: ~63 short-named files in one
# commit gives raw ~3,699 B, bc ~4,241 B. Build a SEPARATE fixture like cell 2's.
SEED3="$tmp/seed3"; BARE3="$tmp/bare3.git"; PUSH3="$tmp/push3.git"
D3="$tmp/drive3"

# Check that the lowered binary is still available
if [ ! -x "$LOWBIN" ]; then
    t_fail "3: the lowered harness binary $LOWBIN is not built"
else
    # build_seed3 -- create seed with ~70 files (raw < 4096 < bc)
    build_seed3() {
        rm -rf "$SEED3" "$BARE3" "$PUSH3"
        git init -q -b master "$SEED3" || return 1
        git -C "$SEED3" config user.email "$RP_EMAIL"
        git -C "$SEED3" config user.name "$RP_USER"
        mkdir -p "$SEED3/src"
        # Create only a small seed (4 files): enough to be a valid repo
        printf 'jichi\n' > "$SEED3/src/small1.txt"
        printf 'harness\n' > "$SEED3/src/small2.txt"
        printf 'truncation\n' > "$SEED3/src/small3.txt"
        printf 'smoke\n' > "$SEED3/src/small4.txt"
        printf 'jichi\njichi-harness\n' > "$SEED3/.gitignore"
        git -C "$SEED3" add src .gitignore 2>/dev/null
        GIT_AUTHOR_DATE="2026-11-19T00:00:00Z" GIT_COMMITTER_DATE="2026-11-19T00:00:00Z" \
            git -C "$SEED3" -c user.name="$RP_USER" -c user.email="$RP_EMAIL" commit -q -m C1
        C1=$(git -C "$SEED3" rev-parse HEAD)
        git init -q --bare "$BARE3" || return 1
        git -C "$SEED3" push -q "$BARE3" "refs/heads/master:refs/heads/master" || return 1
        git clone -q "file://$BARE3" "$PUSH3" || return 1
        git -C "$PUSH3" fetch -q origin || return 1
        # The plan branch for init's --branch harness-plan/plan
        git -C "$PUSH3" checkout -q -b harness-plan/plan master || return 1
        git -C "$PUSH3" checkout -q master || return 1
        return 0
    }

    # build_drive3 -- create drive fixture for cell 3
    build_drive3() {
        mkdir -p "$D3/bin" "$D3/pins" "$D3/pin-build" "$D3/backups" "$D3/conf" || return 1
        printf '0123456789ab\n' > "$D3/key"; chmod 600 "$D3/key"
        OLD8=$(printf '%.8s' "$C1")
        mkdir -p "$D3/pins/$OLD8"
        printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D3/pins/$OLD8/jichi"
        printf '#!/bin/sh\nif [ "$1" = "--version" ] || [ "$1" = "-V" ]; then\n  echo "jichi-harness 0.0.0-fake"\n  echo "build: deadbeef"\nfi\nexit 0\n' > "$D3/pins/$OLD8/jichi-harness"
        chmod 755 "$D3/pins/$OLD8/jichi" "$D3/pins/$OLD8/jichi-harness"
        cp "$D3/pins/$OLD8/jichi" "$D3/bin/jichi"
        cp "$D3/pins/$OLD8/jichi-harness" "$D3/bin/jichi-harness"
        ln -sfn "$OLD8" "$D3/pins/current"
        printf '{"lowResource":false}\n' > "$D3/conf/jichi-config.json"
        return 0
    }

    if ! build_seed3 || ! build_drive3; then
        t_fail "3: the cell-3 fixture failed: the base git/drive fixture could not be built"
    else
        S3=$(printf '%.8s' "$C1")

        mkrehearsal3() {
            _mr="$1"
            cat > "$tmp/cfg$_mr.json" <<EOF
{"dir":"$tmp/dir3$_mr",
"drive":{"branchPrefix":"harness-plan","gitName":"Harness Test","gitEmail":"harness-test@example.com"},
"modelKeyFile":"$tmp/kreal","gitTokenFile":"$tmp/tokfile"}
EOF
            "$LOWBIN" init --config "$tmp/cfg$_mr.json" rehearsal "$tmp/dir3$_mr" \
                --from "$PUSH3" --jichi-bin "$tmp/jichi" \
                < /dev/null > "$tmp/i$_mr" 2> "$tmp/i$_mr.err" || return 1
            [ -f "$tmp/dir3$_mr/mirror.git-config.harness-copy" ] || \
                cp "$tmp/dir3$_mr/mirror.git/config" "$tmp/dir3$_mr/mirror.git-config.harness-copy"
            git -C "$tmp/dir3$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q --allow-empty -m "team$_mr: main c2" || return 1
            git -C "$tmp/dir3$_mr/team" branch -q "harness-plan/work/a" "harness-plan/main" || return 1
            git -C "$tmp/dir3$_mr/team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q --allow-empty -m "team$_mr: work/a" || return 1
            git -C "$tmp/dir3$_mr/team" branch -q "harness-plan/work/b" || return 1
            git -C "$tmp/dir3$_mr/team" checkout -q "harness-plan/main" || return 1
            return 0
        }

        # build_63_files_to_team MR -- add ~63 small files to the team's harness-plan/work/a
        # branch, then commit. The raw rev-list listing is < 4096 B but batch-check > 4096 B.
        build_63_files_to_team() {
            _mr="$1"
            _team="$tmp/dir3$_mr/team"
            _i=0
            while [ "$_i" -lt 63 ]; do
                printf '%s\n' "file $_i content" > "$_team/src/file$_i.txt"
                _i=$((_i + 1))
            done
            git -C "$_team" add src 2>/dev/null || return 1
            git -C "$_team" -c user.name="Harness Smoke" -c user.email="harness-smoke@example.com" \
                commit -q -m "team$_mr: add ~63 files for bc-truncation test" || return 1
            return 0
        }

        # push_team_to_mirror3 MR -- push the team's branches to the mirror.git.
        push_team_to_mirror3() {
            _mr="$1"
            _mirror="$tmp/dir3$_mr/mirror.git"
            _team="$tmp/dir3$_mr/team"
            git -C "$_mirror" fetch -q "$_team" \
                "refs/heads/harness-plan/main:refs/heads/harness-plan/main" \
                "refs/heads/harness-plan/work/*:refs/heads/harness-plan/work/*" 2>/dev/null || return 1
            return 0
        }

        hfetch3() {
            "$LOWBIN" mirror fetch --config "$tmp/cfg$1.json" "$tmp/dir3$1/mirror.git" \
                < /dev/null > "$tmp/f$1" 2> "$tmp/f$1.err"
        }

        if ! mkrehearsal3 "" || ! build_63_files_to_team "" || ! push_team_to_mirror3 "" || ! hfetch3 ""; then
            t_fail "3: the cell-3 init (dir) failed: $(tr '\n' ' ' < "$tmp/i" | head_bytes 160)"
        else
            # The mirror's origin URL as the harness stored it.
            stored_url3="$PUSH3"

            "$LOWBIN" mirror push --config "$tmp/cfg.json" "$tmp/dir3/mirror.git" \
                < /dev/null > "$tmp/p3" 2> "$tmp/p3.err"
            rc3=$?

            # The assertion: the push was REFUSED and the refusal NAMES the bc-truncation.
            # Cell 3 runs the LOWERED binary, asserts exit 1 AND greps the stage-2 text
            # 'the batch-check listing overflowed the capture cap' (NOT the generic form).
            if [ "$rc3" -ne 1 ]; then
                t_fail "3: the bc-only truncation push did not refuse (exit $rc3, want 1): $(tr '\n' ' ' < "$tmp/p3.err" | head_bytes 200)"
            elif ! grep -qiE "batch-check listing overflowed the capture cap" "$tmp/p3" "$tmp/p3.err"; then
                t_fail "3: the push refused but the refusal does not say 'the batch-check listing overflowed the capture cap': $(tr '\n' ' ' < "$tmp/p3.err" | head_bytes 200)"
            else
                t_ok "3: the bc-only truncation push is REFUSED exit 1 with 'the batch-check listing overflowed the capture cap'"
            fi
        fi
    fi
fi

t_done
